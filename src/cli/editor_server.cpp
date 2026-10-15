#include "editor_server.hpp"
#include "wsl/log/log.hpp"
#include "wsl/net/command_protocol.hpp"
#include "wsl/net/socket_compat.hpp"
#include <tracy/Tracy.hpp>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <system_error>
#include <thread>

namespace wsl::cli
{

class editor_server::impl
{
public:
  impl () = default;
  ~impl () { cleanup (); }

  bool
  start (const std::string &project_path)
  {
    m_project_path = project_path;
    m_socket_path = wsl::net::command_protocol::socket_path (project_path);

    // Remove existing socket file if present
    std::error_code ec;
    std::filesystem::remove (m_socket_path, ec);

    m_server_fd = wsl::net::socket_open_server (m_socket_path);
    if (m_server_fd == wsl::net::invalid_socket) {
      wsl::log::net ()->error ("Failed to create editor socket {}: {}",
                               m_socket_path, wsl::net::socket_error_string ());
      cleanup ();
      return false;
    }

    wsl::log::net ()->info ("Editor server started on {}", m_socket_path);
    return true;
  }

  void
  stop ()
  {
    cleanup ();
  }

  bool
  is_running () const
  {
    return m_server_fd != wsl::net::invalid_socket;
  }

  void
  poll (const editor_server::command_handler &handler)
  {
    if (m_server_fd == wsl::net::invalid_socket) {
      return;
    }

    // Non-blocking accept
    wsl::net::socket_handle client_fd
        = wsl::net::socket_try_accept (m_server_fd);
    if (client_fd != wsl::net::invalid_socket) {
      std::thread (&impl::handle_client, this, client_fd, handler).detach ();
    }
  }

private:
  void
  handle_client (wsl::net::socket_handle client_fd,
                 editor_server::command_handler handler)
  {
    // Label this thread for Tracy. A new detached std::thread is
    // spawned per accepted client connection, so without a name
    // they all show up as bare thread IDs in the Tracy Threads
    // pane. The docs require a string literal — the API stores
    // the pointer and expects the bytes to outlive the process.
    tracy::SetThreadName ("Editor Server Client");

    wsl::log::net ()->debug ("Handle client called, client_fd={}", client_fd);
    // Read project path from client (handshake)
    std::string client_project = read_line (client_fd);
    wsl::log::net ()->debug ("Received project: {}", client_project);

    // Discovery query: report our project so path-less clients can attach,
    // then fall through to the regular validation with their reply.
    if (client_project == wsl::net::command_protocol::HANDSHAKE_QUERY) {
      if (m_project_path.empty ()) {
        write_all (
            client_fd,
            std::string (wsl::net::command_protocol::HANDSHAKE_NO_PROJECT)
                + "\n");
        wsl::net::close_socket (client_fd);
        return;
      }

      write_all (
          client_fd,
          std::string (wsl::net::command_protocol::HANDSHAKE_PROJECT_PREFIX)
              + m_project_path + "\n");
      client_project = read_line (client_fd);
      wsl::log::net ()->debug ("Discovery reply project: {}", client_project);
    }

    // Validate project match
    if (client_project != m_project_path) {
      std::string response
          = wsl::net::command_protocol::HANDSHAKE_PROJECT_MISMATCH;
      write_all (client_fd, response + "\n");
      wsl::log::net ()->warn ("Project mismatch - client: {}, server: {}",
                              client_project, m_project_path);
      wsl::net::close_socket (client_fd);
      return;
    }

    // Send OK handshake
    write_all (client_fd,
               std::string (wsl::net::command_protocol::HANDSHAKE_OK) + "\n");

    // Process commands from client
    while (true) {
      std::string command = read_line (client_fd);
      if (command.empty ())
        break;

      wsl::log::net ()->debug ("Received command: {}", command);

      // Execute command through the installed handler.
      std::string output;
      if (handler) {
        output = handler (command);
      } else {
        output = "ERROR: No command handler installed\n";
      }

      // Send response with terminator
      write_all (client_fd, output);
      write_all (client_fd, wsl::net::command_protocol::RESPONSE_TERMINATOR);
      write_all (client_fd, "\n");
    }

    wsl::net::close_socket (client_fd);
  }

  std::string
  read_line (wsl::net::socket_handle fd)
  {
    std::string result;
    char buffer[1];
    while (wsl::net::socket_recv (fd, buffer, 1) > 0) {
      if (buffer[0] == '\n')
        break;
      result += buffer[0];
    }
    return result;
  }

  bool
  write_all (wsl::net::socket_handle fd, const std::string &data)
  {
    size_t total = 0;
    while (total < data.size ()) {
      long written = wsl::net::socket_send (fd, data.c_str () + total,
                                            data.size () - total);
      if (written < 0)
        return false;
      total += written;
    }
    return true;
  }

  void
  cleanup ()
  {
    if (m_server_fd != wsl::net::invalid_socket) {
      wsl::net::close_socket (m_server_fd);
      m_server_fd = wsl::net::invalid_socket;
    }
    if (!m_socket_path.empty ()) {
      std::error_code ec;
      std::filesystem::remove (m_socket_path, ec);
      m_socket_path.clear ();
    }
  }

  wsl::net::socket_handle m_server_fd = wsl::net::invalid_socket;
  std::string m_project_path;
  std::string m_socket_path;
};

editor_server::editor_server () : m_impl (std::make_unique<impl> ()) {}
editor_server::~editor_server () = default;

bool
editor_server::start (const std::string &project_path)
{
  return m_impl->start (project_path);
}

void
editor_server::stop ()
{
  m_impl->stop ();
}

bool
editor_server::is_running () const
{
  return m_impl->is_running ();
}

void
editor_server::poll ()
{
  m_impl->poll (m_command_handler);
}

} // namespace wsl::cli
