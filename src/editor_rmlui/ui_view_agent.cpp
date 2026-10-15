// VIEW — Agent tab transcript rendering. Reads the ACP agent session
// under its lock and emits toolbar, transcript, and permission modal
// markup. Never touches the session's threads.

#include "ui_view.hpp"

#include "ui_agent.hpp"
#include "ui_util.hpp"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <sstream>
#include <variant>

namespace editor_rml
{

namespace
{

// Maximum tool-output lines rendered per card; the rest is summarized.
constexpr std::size_t kMaxToolLines = 40;

const char *
role_label (agent_session::message::role role)
{
  switch (role) {
  case agent_session::message::role::user:
    return "You";
  case agent_session::message::role::assistant:
    return "Agent";
  case agent_session::message::role::system:
    return "System";
  }
  return "";
}

const char *
role_class (agent_session::message::role role)
{
  switch (role) {
  case agent_session::message::role::user:
    return "role-user";
  case agent_session::message::role::assistant:
    return "role-assistant";
  case agent_session::message::role::system:
    return "role-system";
  }
  return "";
}

const char *
tool_status_class (const agentsdk::acp::tool_call_update &tc)
{
  if (!tc.status) {
    return "tc-pending";
  }
  switch (*tc.status) {
  case agentsdk::acp::tool_call_status::pending:
    return "tc-pending";
  case agentsdk::acp::tool_call_status::in_progress:
    return "tc-running";
  case agentsdk::acp::tool_call_status::completed:
    return "tc-completed";
  case agentsdk::acp::tool_call_status::failed:
    return "tc-failed";
  }
  return "tc-pending";
}

const char *
tool_status_label (const agentsdk::acp::tool_call_update &tc)
{
  if (!tc.status) {
    return "pending";
  }
  switch (*tc.status) {
  case agentsdk::acp::tool_call_status::pending:
    return "pending";
  case agentsdk::acp::tool_call_status::in_progress:
    return "running";
  case agentsdk::acp::tool_call_status::completed:
    return "completed";
  case agentsdk::acp::tool_call_status::failed:
    return "failed";
  }
  return "pending";
}

std::string
tool_title (const agentsdk::acp::tool_call_update &tc)
{
  if (tc.title && !tc.title->empty ()) {
    return *tc.title;
  }
  if (tc.kind) {
    return agentsdk::acp::tool_kind_name (*tc.kind);
  }
  return "Tool Call";
}

/** Appends *text* as one `.line` div per physical line. */
void
append_text_lines (std::string &html, const std::string &text)
{
  std::istringstream stream (text);
  std::string frag;
  bool any = false;
  while (std::getline (stream, frag)) {
    any = true;
    html += "<div class='line'>";
    html += html_escape (frag);
    html += "</div>";
  }
  if (!any) {
    html += "<div class='line'></div>";
  }
}

/** Appends capped tool output: first lines plus a truncation note. */
void
append_capped_lines (std::string &html, const std::string &text,
                     const char *line_class = nullptr)
{
  std::istringstream stream (text);
  std::string frag;
  std::size_t shown = 0;
  std::size_t total = 0;
  std::string kept;
  kept.reserve (text.size ());
  while (std::getline (stream, frag)) {
    ++total;
    if (shown < kMaxToolLines) {
      ++shown;
      kept += "<div class='line";
      if (line_class != nullptr) {
        kept += " ";
        kept += line_class;
      }
      kept += "'>";
      kept += html_escape (frag);
      kept += "</div>";
    }
  }
  html += kept;
  if (total > shown) {
    html += "<div class='line dim'>... ";
    html += std::to_string (total - shown);
    html += " more lines</div>";
  }
  if (total == 0) {
    html += "<div class='line'></div>";
  }
}

} // namespace

void
ui_view::refresh_agent ()
{
  Rml::Element *toolbar = document_ != nullptr
                              ? document_->GetElementById ("agent-toolbar")
                              : nullptr;
  Rml::Element *messages_el = document_ != nullptr
                                  ? document_->GetElementById ("agent-messages")
                                  : nullptr;
  if (toolbar == nullptr || messages_el == nullptr) {
    return;
  }
  if (!model_.agent) {
    set_rml_if_changed (messages_el,
                        "<div class='empty-message'>Agent unavailable.</div>",
                        agent_html_cache);
    return;
  }

  const bool connected = model_.agent->connected ();
  const bool initializing = model_.agent->initializing ();
  const bool processing = model_.agent->processing ();
  const std::string selected = model_.agent->selected_agent ();
  const std::vector<agentsdk::acp::agent_entry> agents
      = model_.agent->available_agents ();

  // -- toolbar ------------------------------------------------------
  std::string bar;
  bar += "<span class='dim'>Agent:</span>";
  if (connected) {
    bar += "<span class='dim'>";
    bar += html_escape (selected.empty () ? "-" : selected);
    bar += "</span>";
  } else if (agents.empty ()) {
    bar += "<span class='dim'>No agents found.</span>";
  } else {
    bar += "<select id='agent-select'>";
    for (const auto &entry : agents) {
      bar += "<option value='";
      bar += html_escape (entry.name);
      bar += "'";
      if (entry.name == selected) {
        bar += " selected='selected'";
      }
      bar += ">";
      bar += html_escape (entry.display_name);
      bar += "</option>";
    }
    bar += "</select>";
  }

  bar += "<span id='agent-status'>";
  if (connected) {
    bar += "<span class='ag-ok'>● Connected</span>";
  } else if (initializing) {
    bar += "<span class='ag-busy'>● Connecting...</span>";
  } else {
    bar += "<span class='ag-off'>● Disconnected</span>";
  }
  bar += "</span>";

  if (connected) {
    for (const auto &opt : model_.agent->config_options ()) {
      if (opt.type != agentsdk::acp::config_option_type::select
          || opt.options.empty ()) {
        continue;
      }
      bar += "<span class='dim'>";
      bar += html_escape (opt.name);
      bar += ":</span><select id='agent-cfg-";
      bar += html_escape (opt.id);
      bar += "'>";
      for (const auto &choice : opt.options) {
        bar += "<option value='";
        bar += html_escape (choice.value);
        bar += "'";
        if (choice.value == opt.current_value) {
          bar += " selected='selected'";
        }
        bar += ">";
        bar += html_escape (choice.name);
        bar += "</option>";
      }
      bar += "</select>";
    }
  }

  if (!connected) {
    if (!agents.empty ()) {
      bar += "<button id='btn-agent-connect'>Connect</button>";
    }
  } else {
    if (processing) {
      bar += "<button id='btn-agent-stop'>Stop</button>";
    }
    bar += "<button id='btn-agent-disconnect'>Disconnect</button>";
  }
  set_rml_if_changed (toolbar, bar, agent_toolbar_cache);

  // -- transcript ---------------------------------------------------
  const std::vector<agent_session::message> messages
      = model_.agent->messages ();
  const std::vector<agentsdk::acp::plan_entry> plan = model_.agent->plan ();

  std::string html;
  if (!plan.empty ()) {
    html += "<div class='list-head'>Plan</div>";
    for (const auto &entry : plan) {
      const bool done = entry.status == "completed";
      const bool active = entry.status == "in_progress";
      html += "<div class='line";
      if (done) {
        html += " plan-done";
      } else if (active) {
        html += " plan-active";
      }
      html += "'>";
      html += done ? "[x] " : (active ? "[>] " : "[ ] ");
      html += html_escape (entry.content);
      html += "</div>";
    }
  }

  if (messages.empty ()) {
    html += "<div class='empty-message'>No messages. Connect an agent to "
            "start.</div>";
  }
  for (std::size_t mi = 0; mi < messages.size (); ++mi) {
    const auto &msg = messages[mi];
    html += "<div class='amsg'><div class='arole ";
    html += role_class (msg.msg_role);
    html += "'>";
    html += role_label (msg.msg_role);
    if (msg.streaming) {
      html += " ...";
    }
    html += "</div><div class='abody'>";
    append_text_lines (html, msg.text);
    for (std::size_t ti = 0; ti < msg.tool_calls.size (); ++ti) {
      const auto &tc = msg.tool_calls[ti];
      const std::string key
          = "m" + std::to_string (mi) + "-c" + std::to_string (ti);
      const bool collapsed = model_.agent_collapsed_tools.contains (key);
      html += "<div class='comp'><div id='tool-";
      html += key;
      html += "' class='comp-name'><span class='toggle'>";
      html += collapsed ? "+" : "-";
      html += "</span><span class='";
      html += tool_status_class (tc);
      html += "'>●</span> ";
      html += html_escape (tool_title (tc));
      html += " <span class='dim'>";
      html += tool_status_label (tc);
      html += "</span></div>";
      if (!collapsed) {
        html += "<div class='tcontent'>";
        if (tc.content) {
          for (const auto &item : *tc.content) {
            using item_t = agentsdk::acp::tool_call_content_item;
            if (const auto *wrapper
                = std::get_if<item_t::content_wrapper> (&item.item)) {
              if (const auto *text = std::get_if<agentsdk::acp::text_content> (
                      &wrapper->content)) {
                html += "<div class='tout'>";
                append_capped_lines (html, text->text);
                html += "</div>";
              } else if (const auto *img
                         = std::get_if<agentsdk::acp::image_content> (
                             &wrapper->content)) {
                html += "<div class='line dim'>[Image: ";
                html += html_escape (img->mime_type);
                html += "]</div>";
              } else if (const auto *link
                         = std::get_if<agentsdk::acp::resource_link_content> (
                             &wrapper->content)) {
                html += "<div class='line dim'>[Resource] ";
                html += html_escape (link->uri);
                html += "</div>";
              }
            } else if (const auto *diff
                       = std::get_if<item_t::diff_content> (&item.item)) {
              html += "<div class='line dim'>";
              html += html_escape (diff->path);
              html += "</div>";
              if (diff->old_text) {
                html += "<div class='tout'>";
                append_capped_lines (html, *diff->old_text, "diff-del");
                html += "</div>";
              }
              if (!diff->new_text.empty ()) {
                html += "<div class='tout'>";
                append_capped_lines (html, diff->new_text, "diff-add");
                html += "</div>";
              }
            } else if (const auto *term
                       = std::get_if<item_t::terminal_content> (&item.item)) {
              html += "<div class='line dim'>[Terminal ";
              html += html_escape (term->terminal_id);
              html += "]</div>";
            }
          }
        }
        if (tc.locations) {
          for (const auto &loc : *tc.locations) {
            html += "<div class='line dim'>  ";
            html += html_escape (loc.path);
            if (loc.line) {
              html += ":";
              html += std::to_string (*loc.line);
            }
            html += "</div>";
          }
        }
        html += "</div>";
      }
      html += "</div>";
    }
    html += "</div></div>";
  }

  const bool changed = (html != agent_html_cache);
  set_rml_if_changed (messages_el, html, agent_html_cache);
  if (changed) {
    messages_el->SetScrollTop (messages_el->GetScrollHeight ());
  }

  // -- permission modal ---------------------------------------------
  Rml::Element *modal = document_->GetElementById ("modal-agent-permission");
  const std::optional<agentsdk::acp::tool_call_update> pending
      = model_.agent->pending_permission ();
  if (modal != nullptr) {
    modal->SetClass ("shown", pending.has_value ());
  }
  if (pending.has_value ()) {
    if (Rml::Element *title = document_->GetElementById ("perm-title");
        title != nullptr) {
      std::string desc;
      if (pending->title && !pending->title->empty ()) {
        desc = *pending->title;
      } else {
        desc = "The agent is requesting permission.";
      }
      set_rml_if_changed (title, html_escape (desc), agent_perm_cache);
    }
  } else {
    agent_perm_cache.clear ();
  }
}

} // namespace editor_rml
