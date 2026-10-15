#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace wsl::cli
{

/**
 * Hosts the attach-mode command socket for a running editor.
 *
 * The server side of the weasel-cli / MCP attach protocol: a client
 * (``weasel-cli -a``, the MCP server) connects, and the editor executes the
 * commands through the installed handler. Shared by all editor front-ends.
 */
class editor_server
{
public:
  /** Executes one command and returns its textual response. */
  using command_handler = std::function<std::string (const std::string &)>;

  editor_server ();
  ~editor_server ();

  /** Starts the server for the given project path (used for validation). */
  bool start (const std::string &project_path);

  /** Stops the server and removes the socket. */
  void stop ();

  bool is_running () const;

  /** Polls for incoming client connections and commands (non-blocking). */
  void poll ();

  /** Installs the command executor (typically a cli::command_executor). */
  void
  set_command_handler (command_handler handler)
  {
    m_command_handler = std::move (handler);
  }

private:
  class impl;
  std::unique_ptr<impl> m_impl;
  command_handler m_command_handler;
  std::atomic<bool> m_running{ false };
};

} // namespace wsl::cli
