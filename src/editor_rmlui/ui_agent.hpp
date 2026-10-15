#pragma once

// Agent session (ACP chat) for the RmlUi editor's Agent tab.
//
// Ports the conversation logic of the ImGui chat_panel to the MVC model
// layer: it owns the ACP client/session/agent-manager, the background
// connect/prompt threads, and the transcript/plan/permission state. It
// never touches RmlUi; the ACP reader thread mutates state under a mutex
// and the UI thread renders snapshots from it.

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <agentsdk/acp/acp_agent_manager.hpp>
#include <agentsdk/acp/acp_client.hpp>
#include <agentsdk/acp/acp_session.hpp>
#include <agentsdk/acp/acp_types.hpp>

namespace editor_rml
{

class agent_session
{
public:
  /** One transcript entry. */
  struct message
  {
    enum class role
    {
      user,
      assistant,
      system
    };
    role msg_role = role::system;
    std::string text;
    bool streaming = false;
    std::vector<agentsdk::acp::tool_call_update> tool_calls;
  };

  agent_session ();
  ~agent_session ();

  agent_session (const agent_session &) = delete;
  agent_session &operator= (const agent_session &) = delete;

  /** Polls background connect/prompt completion. Returns true when the UI
   * should refresh (call from the UI thread every frame). */
  bool poll ();

  /** Starts connecting to the selected agent with *project_root* as cwd. */
  void connect (const std::string &project_root);

  /** Disconnects and clears session state. */
  void disconnect ();

  /** Cancels an in-flight prompt. */
  void cancel ();

  /** Queues a user message and prompts the agent (no-op when disconnected
   * or when *text* is empty). */
  void send (const std::string &text);

  /** Dismisses the pending permission dialog (informational: the session's
   * built-in protocol handlers already answered the request). */
  void dismiss_permission ();

  /** Applies a config-option change. */
  void set_config_option (const std::string &id, const std::string &value);

  void selected_agent (const std::string &name);

  // ---- snapshot accessors for the view (UI thread; internally locked) ----

  bool connected () const;
  bool initializing () const;
  bool processing () const;
  std::string selected_agent () const;
  std::vector<agentsdk::acp::agent_entry> available_agents () const;
  std::vector<agentsdk::acp::config_option> config_options () const;
  std::vector<message> messages () const;
  std::vector<agentsdk::acp::plan_entry> plan () const;
  std::optional<agentsdk::acp::tool_call_update> pending_permission () const;

private:
  void connect_async (const std::string &cwd);
  void prompt_async (const std::string &text);

  void push_message (message msg);
  void on_session_update (const std::string &method, const std::string &params);
  void apply_tool_call_update (agentsdk::acp::tool_call_update &&update);
  agentsdk::acp::agent_response on_agent_request (const std::string &method,
                                                  const std::string &params);

  agentsdk::acp::acp_client m_client;
  agentsdk::acp::acp_session m_session;
  agentsdk::acp::acp_agent_manager m_agent_manager;

  // Agent state (m_messages/m_plan/m_pending_permission are also mutated
  // on the ACP reader thread; guard them with m_msg_mutex).
  bool m_connected = false;
  bool m_initializing = false;
  std::string m_selected_agent;
  std::vector<agentsdk::acp::agent_entry> m_available_agents;

  std::thread m_connect_thread;
  std::atomic<bool> m_connect_result{ false };
  std::atomic<bool> m_connect_done{ false };
  std::string m_connect_error;
  std::string m_connect_session_id;

  std::thread m_prompt_thread;
  std::atomic<bool> m_prompt_done{ false };

  mutable std::mutex m_msg_mutex;
  std::vector<message> m_messages;
  std::vector<agentsdk::acp::plan_entry> m_plan;
  std::optional<agentsdk::acp::tool_call_update> m_pending_permission;
  std::vector<agentsdk::acp::permission_option> m_permission_options;
};

} // namespace editor_rml
