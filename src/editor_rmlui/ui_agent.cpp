// Agent session (ACP chat): conversation logic ported from the ImGui
// chat_panel, minus all ImGui rendering. See ui_agent.hpp for the layering.

#include "ui_agent.hpp"

#include "wsl/log/log.hpp"

#include <algorithm>
#include <spdlog/spdlog.h>
#include <simdjson.h>

#include <filesystem>

namespace editor_rml
{

namespace
{

using agentsdk::acp::tool_call_content_item;

/**
 * Parse the ``content`` array of a tool call update into typed items.
 *
 * :param update: The ``update`` object of a session/update notification.
 * :return: The parsed items, or nullopt when the update carries none.
 */
std::optional<std::vector<tool_call_content_item>>
parse_tool_call_content (const simdjson::dom::element &update)
{
  auto content_el = update["content"];
  if (content_el.error () || !content_el.value ().is_array ()) {
    return std::nullopt;
  }

  std::vector<tool_call_content_item> items;

  for (auto item_el : content_el.value ().get_array ()) {
    std::string_view type;
    auto type_el = item_el["type"];
    if (type_el.error () || type_el.get_string ().get (type) != 0) {
      continue;
    }

    tool_call_content_item item;

    if (type == "text") {
      auto text_el = item_el["text"];
      if (text_el.error ()) {
        continue;
      }
      std::string_view text;
      if (text_el.get_string ().get (text) != 0) {
        continue;
      }

      tool_call_content_item::content_wrapper wrapper;
      wrapper.content = agentsdk::acp::text_content{ std::string (text) };
      item.item = std::move (wrapper);
    } else if (type == "diff") {
      tool_call_content_item::diff_content diff;

      auto path_el = item_el["path"];
      if (!path_el.error ()) {
        std::string_view path;
        if (path_el.get_string ().get (path) == 0) {
          diff.path = std::string (path);
        }
      }

      auto old_el = item_el["oldText"];
      if (!old_el.error ()) {
        std::string_view old_text;
        if (old_el.get_string ().get (old_text) == 0) {
          diff.old_text = std::string (old_text);
        }
      }

      auto new_el = item_el["newText"];
      if (!new_el.error ()) {
        std::string_view new_text;
        if (new_el.get_string ().get (new_text) == 0) {
          diff.new_text = std::string (new_text);
        }
      }

      item.item = std::move (diff);
    } else if (type == "resource_link") {
      tool_call_content_item::content_wrapper wrapper;
      agentsdk::acp::resource_link_content link;

      auto name_el = item_el["name"];
      if (!name_el.error ()) {
        std::string_view name;
        if (name_el.get_string ().get (name) == 0) {
          link.name = std::string (name);
        }
      }
      auto uri_el = item_el["uri"];
      if (!uri_el.error ()) {
        std::string_view uri;
        if (uri_el.get_string ().get (uri) == 0) {
          link.uri = std::string (uri);
        }
      }

      wrapper.content = std::move (link);
      item.item = std::move (wrapper);
    } else {
      continue;
    }

    items.push_back (std::move (item));
  }

  if (items.empty ()) {
    return std::nullopt;
  }
  return items;
}

} // namespace

agent_session::agent_session () : m_session (m_client)
{
  m_available_agents = m_agent_manager.discover_agents ();
  if (!m_available_agents.empty ()) {
    m_selected_agent = m_available_agents[0].name;
  }

  m_session.set_update_handler (
      [this] (const std::string &method, const std::string &params) {
        on_session_update (method, params);
      });

  m_session.set_agent_request_handler (
      [this] (const std::string &method,
              const std::string &params) -> agentsdk::acp::agent_response {
        return on_agent_request (method, params);
      });
}

agent_session::~agent_session ()
{
  // Unblock any in-flight RPC waits before joining their threads.
  m_client.terminate ();

  if (m_connect_thread.joinable ()) {
    m_connect_thread.join ();
  }
  if (m_prompt_thread.joinable ()) {
    m_prompt_thread.join ();
  }

  if (m_connected) {
    m_session.close_session ();
  }
}

bool
agent_session::poll ()
{
  bool changed = false;

  // Background connect completion.
  if (m_initializing && m_connect_done.load ()) {
    if (m_connect_thread.joinable ()) {
      m_connect_thread.join ();
    }

    if (m_connect_result.load ()) {
      m_connected = true;
      m_initializing = false;

      message info_msg;
      info_msg.msg_role = message::role::system;
      info_msg.text = "Connected to " + m_selected_agent
                      + " (session: " + m_connect_session_id + ")";
      push_message (std::move (info_msg));
    } else {
      m_initializing = false;
      message err_msg;
      err_msg.msg_role = message::role::system;
      err_msg.text = m_connect_error;
      push_message (std::move (err_msg));
    }
    m_connect_done.store (false);
    changed = true;
  }

  // Background prompt completion: stop the streaming marker.
  if (m_prompt_done.load ()) {
    if (m_prompt_thread.joinable ()) {
      m_prompt_thread.join ();
    }
    {
      std::lock_guard<std::mutex> lock (m_msg_mutex);
      if (!m_messages.empty ()
          && m_messages.back ().msg_role == message::role::assistant
          && m_messages.back ().streaming) {
        m_messages.back ().streaming = false;
        changed = true;
      }
    }
    m_prompt_done.store (false);
  }

  return changed;
}

void
agent_session::connect (const std::string &project_root)
{
  if (m_connected || m_initializing) {
    return;
  }

  const agentsdk::acp::agent_entry *agent = nullptr;
  for (const auto &a : m_available_agents) {
    if (a.name == m_selected_agent) {
      agent = &a;
      break;
    }
  }

  if (agent == nullptr) {
    wsl::log::editor ()->error ("Agent: no agent selected");
    return;
  }

  m_initializing = true;
  m_connect_result.store (false);
  m_connect_error.clear ();
  m_connect_session_id.clear ();

  message info_msg;
  info_msg.msg_role = message::role::system;
  info_msg.text = "Connecting to " + agent->display_name + "...";
  push_message (std::move (info_msg));

  // Launch agent subprocess (fast, non-blocking).
  if (!m_agent_manager.launch (m_client, *agent)) {
    wsl::log::editor ()->error ("Agent: failed to launch agent: {}",
                                agent->command);
    m_initializing = false;

    message err_msg;
    err_msg.msg_role = message::role::system;
    err_msg.text
        = "Error: Failed to launch agent '" + agent->display_name + "'";
    push_message (std::move (err_msg));
    return;
  }

  // Run initialize + new_session in a background thread; the project root
  // becomes the agent workspace (falls back to the process cwd).
  std::string cwd = project_root.empty ()
                        ? std::filesystem::current_path ().string ()
                        : project_root;
  m_connect_thread = std::thread ([this, cwd] { connect_async (cwd); });
}

void
agent_session::disconnect ()
{
  if (!m_connected) {
    return;
  }

  // Unblock any in-flight prompt wait before joining its thread.
  m_client.terminate ();

  if (m_prompt_thread.joinable ()) {
    m_prompt_thread.join ();
  }

  m_session.close_session ();
  m_connected = false;

  {
    std::lock_guard<std::mutex> lock (m_msg_mutex);
    m_plan.clear ();
    m_pending_permission.reset ();
  }

  message info_msg;
  info_msg.msg_role = message::role::system;
  info_msg.text = "Disconnected";
  push_message (std::move (info_msg));
}

void
agent_session::cancel ()
{
  if (m_connected && m_session.is_processing ()) {
    m_session.cancel ();

    message info_msg;
    info_msg.msg_role = message::role::system;
    info_msg.text = "Cancelled";
    push_message (std::move (info_msg));
  }
}

void
agent_session::send (const std::string &text)
{
  if (text.empty () || !m_connected || m_initializing) {
    return;
  }

  {
    std::lock_guard<std::mutex> lock (m_msg_mutex);

    message user_msg;
    user_msg.msg_role = message::role::user;
    user_msg.text = text;
    m_messages.push_back (std::move (user_msg));

    // Prepare the assistant placeholder BEFORE sending so streamed chunks
    // have a message to append to immediately.
    message assistant_msg;
    assistant_msg.msg_role = message::role::assistant;
    assistant_msg.streaming = true;
    m_messages.push_back (std::move (assistant_msg));
  }

  prompt_async (text);
}

void
agent_session::dismiss_permission ()
{
  std::lock_guard<std::mutex> lock (m_msg_mutex);
  m_pending_permission.reset ();
}

void
agent_session::set_config_option (const std::string &id,
                                  const std::string &value)
{
  m_session.set_config_option (id, value);
}

void
agent_session::selected_agent (const std::string &name)
{
  m_selected_agent = name;
}

bool
agent_session::connected () const
{
  return m_connected;
}

bool
agent_session::initializing () const
{
  return m_initializing;
}

bool
agent_session::processing () const
{
  return m_session.is_processing ();
}

std::string
agent_session::selected_agent () const
{
  return m_selected_agent;
}

std::vector<agentsdk::acp::agent_entry>
agent_session::available_agents () const
{
  return m_available_agents;
}

std::vector<agentsdk::acp::config_option>
agent_session::config_options () const
{
  return m_session.config_options ();
}

std::vector<agent_session::message>
agent_session::messages () const
{
  std::lock_guard<std::mutex> lock (m_msg_mutex);
  return m_messages;
}

std::vector<agentsdk::acp::plan_entry>
agent_session::plan () const
{
  std::lock_guard<std::mutex> lock (m_msg_mutex);
  return m_plan;
}

std::optional<agentsdk::acp::tool_call_update>
agent_session::pending_permission () const
{
  std::lock_guard<std::mutex> lock (m_msg_mutex);
  return m_pending_permission;
}

void
agent_session::connect_async (const std::string &cwd)
{
  agentsdk::acp::client_capabilities caps;
  caps.fs.read_text_file = true;
  caps.fs.write_text_file = true;
  caps.terminal = true;

  agentsdk::acp::implementation_info info;
  info.name = "weasel";
  info.title = "Weasel Engine";
  info.version = "1.0.0";

  if (!m_session.initialize (caps, info)) {
    m_connect_error = "Error: Agent initialization failed";
    m_client.terminate ();
    m_connect_done.store (true);
    return;
  }

  std::string session_id = m_session.new_session (cwd);

  if (session_id.empty ()) {
    m_connect_error = "Error: Failed to create session";
    m_client.terminate ();
    m_connect_done.store (true);
    return;
  }

  m_connect_session_id = session_id;
  m_connect_result.store (true);
  m_connect_done.store (true);
}

void
agent_session::prompt_async (const std::string &text)
{
  // Join any previous prompt thread.
  if (m_prompt_thread.joinable ()) {
    m_prompt_thread.join ();
  }

  m_prompt_done.store (false);

  m_prompt_thread = std::thread ([this, text] {
    if (!m_session.prompt (text)) {
      wsl::log::editor ()->error ("Agent: prompt failed");
    }

    m_prompt_done.store (true);
  });
}

void
agent_session::push_message (message msg)
{
  std::lock_guard<std::mutex> lock (m_msg_mutex);
  m_messages.push_back (std::move (msg));
}

void
agent_session::on_session_update (const std::string & /*method*/,
                                  const std::string &params)
{
  // Runs on the ACP reader thread; serialize access against the UI thread.
  std::lock_guard<std::mutex> lock (m_msg_mutex);

  simdjson::dom::parser parser;
  auto doc = parser.parse (params);

  auto update_el = doc["update"];
  if (update_el.error ()) {
    return;
  }

  auto update = update_el.value ();
  auto session_update = update["sessionUpdate"];
  if (session_update.error ()) {
    return;
  }

  std::string_view update_type;
  if (session_update.get_string ().get (update_type) != 0) {
    return;
  }

  if (update_type == "agent_message_chunk") {
    // Text chunk from the assistant: append to the last assistant message.
    auto content_el = update["content"];
    if (!content_el.error ()) {
      auto type_el = content_el["type"];
      if (!type_el.error ()) {
        std::string_view type;
        if (type_el.get_string ().get (type) == 0 && type == "text") {
          auto text_el = content_el["text"];
          if (!text_el.error ()) {
            std::string_view text;
            if (text_el.get_string ().get (text) == 0) {
              if (!m_messages.empty ()
                  && m_messages.back ().msg_role == message::role::assistant) {
                m_messages.back ().text += std::string (text);
              }
            }
          }
        }
      }
    }
  } else if (update_type == "tool_call" || update_type == "tool_call_update") {
    // Tool call creation or progress update.
    agentsdk::acp::tool_call_update tc;

    auto id_el = update["toolCallId"];
    if (!id_el.error ()) {
      std::string_view id;
      if (id_el.get_string ().get (id) == 0) {
        tc.tool_call_id = std::string (id);
      }
    }

    auto title_el = update["title"];
    if (!title_el.error ()) {
      std::string_view title;
      if (title_el.get_string ().get (title) == 0) {
        tc.title = std::string (title);
      }
    }

    auto kind_el = update["kind"];
    if (!kind_el.error ()) {
      std::string_view kind;
      if (kind_el.get_string ().get (kind) == 0) {
        auto parsed = agentsdk::acp::parse_tool_kind (std::string (kind));
        if (parsed) {
          tc.kind = *parsed;
        }
      }
    }

    auto status_el = update["status"];
    if (!status_el.error ()) {
      std::string_view status;
      if (status_el.get_string ().get (status) == 0) {
        auto parsed
            = agentsdk::acp::parse_tool_call_status (std::string (status));
        if (parsed) {
          tc.status = *parsed;
        }
      }
    }

    // Command output / diffs produced by the tool call.
    tc.content = parse_tool_call_content (update);

    // Files touched by the tool call.
    auto locations_el = update["locations"];
    if (!locations_el.error () && locations_el.value ().is_array ()) {
      std::vector<agentsdk::acp::tool_call_location> locations;
      for (auto loc_el : locations_el.value ().get_array ()) {
        agentsdk::acp::tool_call_location loc;

        auto path_el = loc_el["path"];
        if (!path_el.error ()) {
          std::string_view path;
          if (path_el.get_string ().get (path) == 0) {
            loc.path = std::string (path);
          }
        }

        auto line_el = loc_el["line"];
        if (!line_el.error () && line_el.value ().is_uint64 ()) {
          loc.line = static_cast<uint32_t> (
              line_el.value ().get_uint64 ().value_unsafe ());
        }

        if (!loc.path.empty ()) {
          locations.push_back (std::move (loc));
        }
      }
      if (!locations.empty ()) {
        tc.locations = std::move (locations);
      }
    }

    apply_tool_call_update (std::move (tc));
  } else if (update_type == "plan") {
    // Plan update.
    auto entries_el = update["entries"];
    if (!entries_el.error () && entries_el.value ().is_array ()) {
      m_plan.clear ();
      for (auto entry : entries_el.value ().get_array ()) {
        agentsdk::acp::plan_entry pe;

        auto content_el2 = entry["content"];
        if (!content_el2.error ()) {
          std::string_view content;
          if (content_el2.get_string ().get (content) == 0) {
            pe.content = std::string (content);
          }
        }

        auto priority_el = entry["priority"];
        if (!priority_el.error ()) {
          std::string_view priority;
          if (priority_el.get_string ().get (priority) == 0) {
            pe.priority = std::string (priority);
          }
        }

        auto status_el2 = entry["status"];
        if (!status_el2.error ()) {
          std::string_view status;
          if (status_el2.get_string ().get (status) == 0) {
            pe.status = std::string (status);
          }
        }

        m_plan.push_back (pe);
      }
    }
  } else if (update_type == "permission") {
    // Permission request (informational: the session's built-in protocol
    // handlers answer the request; the dialog just shows what happened).
    agentsdk::acp::tool_call_update tc;
    tc.tool_call_id = "permission";

    auto title_el = update["title"];
    if (!title_el.error ()) {
      std::string_view title;
      if (title_el.get_string ().get (title) == 0) {
        tc.title = std::string (title);
      }
    }

    m_pending_permission = tc;

    auto options_el = update["options"];
    if (!options_el.error () && options_el.value ().is_array ()) {
      m_permission_options.clear ();
      for (auto opt : options_el.value ().get_array ()) {
        agentsdk::acp::permission_option po;

        auto id_el = opt["id"];
        if (!id_el.error ()) {
          std::string_view id;
          if (id_el.get_string ().get (id) == 0) {
            po.option_id = std::string (id);
          }
        }

        auto name_el = opt["name"];
        if (!name_el.error ()) {
          std::string_view name;
          if (name_el.get_string ().get (name) == 0) {
            po.name = std::string (name);
          }
        }

        m_permission_options.push_back (po);
      }
    }
  } else if (update_type == "completed") {
    // Turn completed.
    if (!m_messages.empty ()
        && m_messages.back ().msg_role == message::role::assistant) {
      m_messages.back ().streaming = false;
    }
  }
}

void
agent_session::apply_tool_call_update (agentsdk::acp::tool_call_update &&update)
{
  if (m_messages.empty ()
      || m_messages.back ().msg_role != message::role::assistant) {
    return;
  }

  auto &tool_calls = m_messages.back ().tool_calls;
  auto it = std::find_if (tool_calls.begin (), tool_calls.end (),
                          [&] (const agentsdk::acp::tool_call_update &tc) {
                            return tc.tool_call_id == update.tool_call_id;
                          });

  if (it == tool_calls.end ()) {
    tool_calls.push_back (std::move (update));
    return;
  }

  // Progress update for an existing tool call: only overwrite the
  // fields the agent actually sent.
  if (update.title) {
    it->title = std::move (*update.title);
  }
  if (update.kind) {
    it->kind = *update.kind;
  }
  if (update.status) {
    it->status = *update.status;
  }
  if (update.content) {
    it->content = std::move (*update.content);
  }
  if (update.locations) {
    it->locations = std::move (*update.locations);
  }
}

agentsdk::acp::agent_response
agent_session::on_agent_request (const std::string & /*method*/,
                                 const std::string & /*params*/)
{
  // No editor-specific handling yet: return unhandled so the built-in
  // protocol handlers in agentsdk::acp::acp_session answer fs and
  // permission requests.
  return {};
}

} // namespace editor_rml
