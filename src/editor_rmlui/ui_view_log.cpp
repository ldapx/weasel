#include "ui_view.hpp"

#include "ui_util.hpp"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/Traits.h>

#include <sstream>

#include "wsl/comp/singl/editor_context.hpp"
#include "wsl/comp/singl/runtime_context.hpp"
#include "wsl/rsc/scene.hpp"
#include "wsl/rsc/scene_manager.hpp"
namespace editor_rml
{

void
ui_view::refresh_log ()
{
  refresh_log_filters ();

  Rml::Element *log = document_->GetElementById ("log-output");
  if (log == nullptr || sink_ == nullptr) {
    return;
  }

  const std::vector<log_sink::entry> records = sink_->entries (200);
  std::string html;
  html.reserve (records.size () * 80 + 16);
  for (const log_sink::entry &rec : records) {
    if (rec.level == "trace" && !model_.log_show_trace) {
      continue;
    }
    if (rec.level == "debug" && !model_.log_show_debug) {
      continue;
    }
    if (rec.level == "info" && !model_.log_show_info) {
      continue;
    }
    if (rec.level == "warn" && !model_.log_show_warn) {
      continue;
    }
    if (rec.level == "error" && !model_.log_show_error) {
      continue;
    }
    if ((rec.level == "fatal" || rec.level == "off") && !model_.log_show_fatal) {
      continue;
    }
    if (model_.log_category != "All" && rec.category != model_.log_category) {
      continue;
    }

    std::string open = "<div class='line";
    if (rec.level == "warn") {
      open += " lv-warn";
    } else if (rec.level == "error" || rec.level == "fatal") {
      open += " lv-error";
    }
    open += "'>";

    // Preserve embedded line breaks: one div per physical line.
    std::istringstream stream (rec.message);
    std::string frag;
    bool first = true;
    while (std::getline (stream, frag)) {
      if (first) {
        first = false;
        html += open;
        html += "[";
        html += html_escape (rec.level);
        html += "] ";
        html += html_escape (rec.category);
        html += ": ";
      } else {
        html += "<div class='line dim'>... ";
      }
      html += html_escape (frag);
      html += "</div>";
    }
    if (first) {
      // Empty payload: still show the prefix line.
      html += open;
      html += "[";
      html += html_escape (rec.level);
      html += "] ";
      html += html_escape (rec.category);
      html += ": </div>";
    }
  }
  const bool changed = (html != log_html_cache);
  set_rml_if_changed (log, html, log_html_cache);
  if (changed && model_.log_autoscroll) {
    // Keep the newest lines visible when content was appended.
    log->SetScrollTop (log->GetScrollHeight ());
  }
}

/**
 * Syncs the log filter bar (level toggles + autoscroll label) with the
 * current filter state. The category select keeps its own widget state.
 */

void
ui_view::refresh_log_filters ()
{
  auto set_active = [this] (const char *id, bool active) {
    if (Rml::Element *el = document_->GetElementById (id); el != nullptr) {
      el->SetClass ("active", active);
    }
  };
  set_active ("btn-lv-trace", model_.log_show_trace);
  set_active ("btn-lv-debug", model_.log_show_debug);
  set_active ("btn-lv-info", model_.log_show_info);
  set_active ("btn-lv-warn", model_.log_show_warn);
  set_active ("btn-lv-error", model_.log_show_error);
  set_active ("btn-lv-fatal", model_.log_show_fatal);

  if (Rml::Element *toggle = document_->GetElementById ("btn-log-autoscroll");
      toggle != nullptr) {
    const std::string label
        = model_.log_autoscroll ? "Autoscroll: on" : "Autoscroll: off";
    set_rml_if_changed (toggle, label, log_autoscroll_cache);
  }
}

void
ui_view::refresh_console ()
{
  Rml::Element *out = document_->GetElementById ("console-output");
  if (out == nullptr) {
    return;
  }
  std::string html;
  html.reserve (model_.console_lines.size () * 64 + 16);
  for (const std::string &line : model_.console_lines) {
    if (!line.empty () && line[0] == '>') {
      html += "<div class='line cmd'>";
    } else {
      html += "<div class='line'>";
    }
    html += html_escape (line);
    html += "</div>";
  }
  const bool changed = (html != console_html_cache);
  set_rml_if_changed (out, html, console_html_cache);
  if (changed) {
    out->SetScrollTop (out->GetScrollHeight ());
  }
}

// ------------------------------------------------------------- events
} // namespace editor_rml
