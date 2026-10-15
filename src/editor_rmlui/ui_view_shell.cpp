#include "ui_view.hpp"

#include "ui_util.hpp"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/Traits.h>

#include <cmath>

#include "wsl/comp/singl/editor_context.hpp"
#include "wsl/gfx/render_window.hpp"
#include "wsl/gfx/transform_gizmo.hpp"
#include "wsl/log/log.hpp"
#include "wsl/rsc/project.hpp"
#include "wsl/rsc/scene.hpp"
#include "wsl/rsc/scene_manager.hpp"
#include "wsl/reg/singleton_registry.hpp"

#include "wsl/comp/singl/runtime_context.hpp"

namespace editor_rml
{

void
ui_view::set_rml_if_changed (Rml::Element *el, const std::string &html,
                             std::string &cache)
{
  if ((el == nullptr) || (html == cache)) {
    return;
  }
  cache = html;
  el->SetInnerRML (html);
}

void
ui_view::refresh ()
{
  if (document_ == nullptr) {
    return;
  }
  refresh_status ();
  refresh_hierarchy ();
  refresh_singletons ();
  refresh_systems ();
  refresh_files ();
  refresh_build ();
  refresh_inspector ();
  refresh_log ();
  refresh_console ();
  refresh_toolbar ();
  refresh_panels ();
  refresh_welcome ();
  refresh_new_project_modal ();
}

void
ui_view::refresh_toolbar ()
{
  auto set_class = [this] (const char *id, bool active) {
    if (Rml::Element *el = document_->GetElementById (id); el != nullptr) {
      el->SetClass ("active", active);
    }
  };

  const bool running = model_.svc.runtime->is_running ();
  const bool session = model_.svc.runtime->in_play_session ();
  set_class ("btn-play", running);
  set_class ("btn-pause", session && !running);
  set_class ("btn-stop", !session);

  const auto mode = model_.svc.editor->gizmo_mode ();
  set_class ("btn-translate",
             mode == wsl::gfx::transform_gizmo_mode::translate);
  set_class ("btn-rotate", mode == wsl::gfx::transform_gizmo_mode::rotate);
  set_class ("btn-scale", mode == wsl::gfx::transform_gizmo_mode::scale);
}

void
ui_view::refresh_status ()
{
  const bool running = model_.svc.runtime->is_running ();
  const bool session = model_.svc.runtime->in_play_session ();
  if (Rml::Element *status = document_->GetElementById ("status");
      status != nullptr) {
    std::string html;
    if (running) {
      html += "<span class='mode mode-playing'>PLAYING</span>";
    } else if (session) {
      html += "<span class='mode mode-paused'>PAUSED</span>";
    } else {
      html += "<span class='mode mode-edit'>EDIT</span>";
    }
    if (auto project
        = model_.svc.runtime->resource_manager ().current_project ()) {
      html += "<span class='st-sep'>|</span><span class='st-dim'>";
      html += html_escape (project->name);
      html += "</span>";
    } else {
      html += "<span class='st-sep'>|</span><span class='st-dim'>no "
              "project</span>";
    }
    const entt::entity selected = model_.svc.editor->selected_entity ();
    if (selected != entt::null) {
      if (auto *scene = model_.svc.runtime->scene_manager ().get_active ();
          scene != nullptr) {
        html += "<span class='st-sep'>|</span>";
        html += html_escape (scene->get_entity_name (selected));
      }
    } else if (model_.selected_singleton.has_value ()) {
      if (const auto *desc = model_.svc.runtime->singleton_registry ().find (
              *model_.selected_singleton);
          desc != nullptr) {
        html += "<span class='st-sep'>|</span>";
        html += html_escape (desc->display_name);
      }
    }
    set_rml_if_changed (status, html, status_html_cache);
  }
}

void
ui_view::refresh_panels ()
{
  auto set_hidden = [this] (const char *id, bool hidden) {
    if (Rml::Element *el = document_->GetElementById (id); el != nullptr) {
      el->SetClass ("hidden", hidden);
    }
  };
  auto set_active = [this] (const char *id, bool active) {
    if (Rml::Element *el = document_->GetElementById (id); el != nullptr) {
      el->SetClass ("active", active);
    }
  };

  // Left group: Entities | Systems | Files tabs.
  const bool left_hidden = !model_.show_entities && !model_.show_systems;
  set_hidden ("left", left_hidden);
  if (!left_hidden) {
    if (!model_.show_entities && model_.left_tab == 0) {
      model_.left_tab = 2;
    }
    if (!model_.show_systems && model_.left_tab == 1) {
      model_.left_tab = 2;
    }
    set_hidden ("tabbtn-entities", !model_.show_entities);
    set_hidden ("tabbtn-systems", !model_.show_systems);
    set_active ("tabbtn-entities", model_.left_tab == 0);
    set_active ("tabbtn-systems", model_.left_tab == 1);
    set_active ("tabbtn-files", model_.left_tab == 2);
    set_hidden ("left-entities", model_.left_tab != 0);
    set_hidden ("left-systems", model_.left_tab != 1);
    set_hidden ("left-files", model_.left_tab != 2);
  }

  // Right group: Inspector | Build tabs.
  set_hidden ("right", !model_.show_inspector);
  if (model_.show_inspector) {
    set_active ("tabbtn-inspector", model_.right_tab == 0);
    set_active ("tabbtn-build", model_.right_tab == 1);
    set_hidden ("right-inspector", model_.right_tab != 0);
    set_hidden ("right-build", model_.right_tab != 1);
  }

  // Bottom group: Console | Log tabs.
  const bool bottom_hidden = !model_.show_console && !model_.show_log;
  set_hidden ("bottom", bottom_hidden);
  if (!bottom_hidden) {
    if (!model_.show_console) {
      model_.bottom_tab = 1;
    }
    if (!model_.show_log) {
      model_.bottom_tab = 0;
    }
    set_hidden ("tabbtn-console", !model_.show_console);
    set_hidden ("tabbtn-log", !model_.show_log);
    set_active ("tabbtn-console", model_.bottom_tab == 0);
    set_active ("tabbtn-log", model_.bottom_tab == 1);
    set_hidden ("bottom-console", model_.bottom_tab != 0);
    set_hidden ("bottom-log", model_.bottom_tab != 1);
  }

  apply_layout ();
}

/**
 * Applies the panel percentages (and hidden-panel expansion) to the
 * involved elements. Inline properties override the stylesheet defaults
 * so the drag handles can resize the layout at runtime. Bottom-anchored
 * edges sit above the 22px status bar, so percentage heights are resolved
 * to pixels against the current window height here.
 */

void
ui_view::apply_layout ()
{
  if (document_ == nullptr) {
    return;
  }
  auto set_pct = [this] (const char *id, const char *prop, float pct) {
    if (Rml::Element *el = document_->GetElementById (id); el != nullptr) {
      el->SetProperty (
          prop, std::to_string (static_cast<int> (std::lround (pct))) + "%");
    }
  };
  auto set_px = [this] (const char *id, const char *prop, int px) {
    if (Rml::Element *el = document_->GetElementById (id); el != nullptr) {
      el->SetProperty (prop, std::to_string (px) + "px");
    }
  };
  auto set_hidden = [this] (const char *id, bool hidden) {
    if (Rml::Element *el = document_->GetElementById (id); el != nullptr) {
      el->SetClass ("hidden", hidden);
    }
  };

  static constexpr int kStatusBarHeight = 22;

  const bool bottom_hidden = !model_.show_console && !model_.show_log;
  const bool left_visible = model_.show_entities || model_.show_systems;
  const float left = left_visible ? model_.left_pct : 0.0F;
  const float right = model_.show_inspector ? model_.right_pct : 0.0F;

  set_pct ("left", "width", model_.left_pct);
  set_pct ("right", "width", model_.right_pct);
  set_pct ("bottom", "height", model_.bottom_pct);

  for (const char *id : { "gameview", "gameview-toolbar", "bottom" }) {
    set_pct (id, "left", left);
    set_pct (id, "right", right);
  }

  // Bottom edges clear the status bar: resolve the percentage against the
  // window height and add the bar height. Falls back to plain percentages
  // while the window size is unknown.
  int win_w = 0;
  int win_h = 0;
  if (model_.svc.window != nullptr) {
    model_.svc.window->get_size (win_w, win_h);
  }
  if (win_h > 0) {
    const float bottom_frac = bottom_hidden ? 0.0F : model_.bottom_pct;
    const int bottom_px
        = static_cast<int> (
              std::lround (static_cast<float> (win_h) * bottom_frac / 100.0F))
          + kStatusBarHeight;
    set_px ("gameview", "bottom", bottom_px);
    set_px ("handle-bottom", "bottom", bottom_px);
  } else {
    set_pct ("gameview", "bottom", bottom_hidden ? 0.0F : model_.bottom_pct);
    set_pct ("handle-bottom", "bottom", model_.bottom_pct);
  }

  set_pct ("handle-left", "left", model_.left_pct);
  set_pct ("handle-right", "right", model_.right_pct);
  set_pct ("handle-bottom", "left", left);
  set_pct ("handle-bottom", "right", right);

  set_hidden ("handle-left", !left_visible);
  set_hidden ("handle-right", !model_.show_inspector);
  set_hidden ("handle-bottom", bottom_hidden);
}

/** Window pixel density, matching RmlSDL's mouse-coordinate convention. */

void
ui_view::log_tab_diagnostics ()
{
  if ((document_ == nullptr) || (context_ == nullptr)) {
    return;
  }

  for (const char *id : { "tabbtn-entities", "tabbtn-systems", "tabbtn-console",
                          "tabbtn-log", "tabbtn-inspector" }) {
    Rml::Element *el = document_->GetElementById (id);
    if (el == nullptr) {
      wsl::log::editor ()->info ("editor tab {}: missing", id);
      continue;
    }
    const Rml::Vector2f off = el->GetAbsoluteOffset (Rml::BoxArea::Border);
    const Rml::Vector2f size = el->GetBox ().GetSize (Rml::BoxArea::Border);
    wsl::log::editor ()->info (
        "editor tab {}: pos=({:.1f},{:.1f}) size={:.1f}x{:.1f} display={} "
        "visible={}",
        id, off.x, off.y, size.x, size.y,
        static_cast<int> (el->GetComputedValues ().display ()),
        static_cast<int> (el->IsVisible (true)));
  }

  if (Rml::Element *el = document_->GetElementById ("tabbtn-entities");
      el != nullptr) {
    const Rml::Vector2f off = el->GetAbsoluteOffset (Rml::BoxArea::Border);
    const Rml::Vector2f size = el->GetBox ().GetSize (Rml::BoxArea::Border);
    const Rml::Vector2f centre (off.x + size.x * 0.5F, off.y + size.y * 0.5F);
    std::string hit_id = "<none>";
    for (Rml::Element *it = context_->GetElementAtPoint (centre); it != nullptr;
         it = it->GetParentNode ()) {
      if (const std::string id = it->GetId (); !id.empty ()) {
        hit_id = id;
        break;
      }
    }
    wsl::log::editor ()->info ("editor tab hit test at ({:.1f},{:.1f}) -> {}",
                               centre.x, centre.y, hit_id);
  }

  wsl::log::editor ()->info ("editor rml source: {}",
                             editor_document_root (model_.svc));

  // Overlay state. A "shown" overlay whose background fails to parse is
  // invisible yet swallows every mouse hit over the editor (rgba() alpha is
  // an integer 0-255 in RmlUi); log the effective state so this is visible.
  for (const char *id : { "welcome", "modal-new-project" }) {
    if (Rml::Element *el = document_->GetElementById (id); el != nullptr) {
      const Rml::Colourb bg = el->GetComputedValues ().background_color ();
      wsl::log::editor ()->info (
          "editor overlay {}: shown={} display={} bg alpha={}", id,
          static_cast<int> (el->IsClassSet ("shown")),
          static_cast<int> (el->GetComputedValues ().display ()),
          static_cast<int> (bg.alpha));
    }
  }
}

/** Returns the drag-handle id under the given window-space point. */

void
ui_view::refresh_welcome ()
{
  if (Rml::Element *welcome = document_->GetElementById ("welcome");
      welcome != nullptr) {
    welcome->SetClass ("shown", model_.show_welcome);
  }

  Rml::Element *list = document_->GetElementById ("recent-list");
  if (list == nullptr) {
    return;
  }

  std::string html;
  if (model_.recent_projects.empty ()) {
    html = "<div class='empty-message'>No recent projects.</div>";
  } else {
    for (std::size_t i = 0; i < model_.recent_projects.size (); ++i) {
      html += "<div id='recent-";
      html += std::to_string (i);
      html += "' class='item'>";
      html += html_escape (model_.recent_projects[i]);
      html += "</div>";
    }
  }
  set_rml_if_changed (list, html, recents_html_cache);
}

void
ui_view::refresh_new_project_modal ()
{
  if (Rml::Element *modal = document_->GetElementById ("modal-new-project");
      modal != nullptr) {
    modal->SetClass ("shown", model_.show_new_project);
  }
  if (Rml::Element *toggle = document_->GetElementById ("np-create-folder");
      toggle != nullptr) {
    const std::string label = model_.new_project_create_folder
                                  ? "Create Folder: on"
                                  : "Create Folder: off";
    set_rml_if_changed (toggle, label, np_toggle_cache);
  }
}

/**
 * Returns the sorted direct children of *parent* via the first/next
 * sibling chain. Guards against malformed cycles with a step budget.
 */
void
ui_view::attach (Rml::ElementDocument *document, Rml::Context *context)
{
  document_ = document;
  context_ = context;
}

void
ui_view::detach ()
{
  document_ = nullptr;
  context_ = nullptr;
}

void
ui_view::set_log_sink (log_sink *sink)
{
  sink_ = sink;
}

void
ui_view::update (double dt)
{
  if (context_ == nullptr) {
    return;
  }

  refresh_timer += dt;
  if (model_.dirty || refresh_timer >= 0.25) {
    refresh_timer = 0.0;
    model_.dirty = false;
    refresh ();
  }

  // Update AFTER any content changes: RmlUi requires Context::Update to
  // run after elements are constructed/modified and before Render, or the
  // changed subtrees render one frame with stale layout (visible flicker).
  context_->Update ();

  if (!logged_diagnostics) {
    logged_diagnostics = true;
    log_tab_diagnostics ();
  }
}
} // namespace editor_rml
