#include "ui_controller.hpp"

#include "ui_agent.hpp"
#include "ui_view.hpp"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/Traits.h>

#include <RmlUi_Platform_SDL.h>

#include <SDL3/SDL_events.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "wsl/comp/singl/editor_context.hpp"
#include "wsl/gfx/render_window.hpp"
#include "wsl/comp/singl/runtime_context.hpp"
#include "wsl/event.hpp"
#include "wsl/gfx/transform_gizmo.hpp"

#include "wsl/rsc/scene.hpp"
#include "wsl/rsc/scene_manager.hpp"

namespace editor_rml
{

void
ui_controller::handle_event (const wsl::engine_event &event)
{
  if (context_ != nullptr) {
    RmlSDL::InputEventHandler (context_, model_.svc.window->handler (),
                               event.sdl ());
  }

  const SDL_Event &sdl = event.sdl ();
  switch (sdl.type) {
  case SDL_EVENT_MOUSE_BUTTON_DOWN:
  case SDL_EVENT_MOUSE_BUTTON_UP: {
    const bool down = (sdl.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
    if (sdl.button.button == SDL_BUTTON_RIGHT) {
      rmb_down = down;
    } else if (sdl.button.button == SDL_BUTTON_LEFT) {
      if (down) {
        active_handle = hit_test_handle (sdl.button.x, sdl.button.y);
        pending_click_id = hovered_element_id (sdl.button.x, sdl.button.y);
        model_.drop_target_id.clear ();
        // Panel tabs act on press: switching tabs is idempotent and this
        // removes any dependence on the release landing on the same element
        // (small tab hit boxes made the press/release pairing unreliable).
        if (pending_click_id.rfind ("tabbtn-", 0) == 0) {
          dispatch_click (pending_click_id);
          pending_click_id.clear ();
        }
      } else {
        active_handle = 0;
        const std::string released_on
            = hovered_element_id (sdl.button.x, sdl.button.y);
        if (!pending_click_id.empty ()
            && pending_click_id.rfind ("ent-", 0) == 0
            && pending_click_id != released_on
            && (released_on.rfind ("ent-", 0) == 0 || released_on == "hierarchy"
                || released_on == "left-entities")) {
          // Press-drag-release across entity rows: re-parent instead of
          // selecting. The drop target highlight is tracked in
          // SDL_EVENT_MOUSE_MOTION while the button is held.
          if (model_.handle_entity_drop (pending_click_id, released_on)) {
            pending_click_id.clear ();
            model_.drop_target_id.clear ();
            model_.dirty = true;
          } else {
            pending_click_id.clear ();
            model_.drop_target_id.clear ();
          }
        } else {
          if (!pending_click_id.empty () && pending_click_id == released_on) {
            dispatch_click (released_on);
          }
          pending_click_id.clear ();
          if (!model_.drop_target_id.empty ()) {
            model_.drop_target_id.clear ();
            model_.dirty = true;
          }
        }
      }
    }
    break;
  }
  case SDL_EVENT_MOUSE_MOTION: {
    if (rmb_down && camera_input_enabled ()) {
      glm::vec3 euler = glm::degrees (
          glm::eulerAngles (model_.svc.editor->editor_cam_rot ()));
      euler.y -= sdl.motion.xrel * 0.12F;
      euler.x -= sdl.motion.yrel * 0.12F;
      euler.x = std::clamp (euler.x, -89.0F, 89.0F);
      model_.svc.editor->editor_cam_rot (glm::quat (glm::radians (euler)));
    } else if (active_handle != 0) {
      drag_handle (sdl.motion.x, sdl.motion.y);
    } else if (!pending_click_id.empty ()
               && pending_click_id.rfind ("ent-", 0) == 0) {
      // Live drop-target highlight while dragging an entity row.
      const std::string hovered
          = hovered_element_id (sdl.motion.x, sdl.motion.y);
      std::string next;
      if (hovered.rfind ("ent-", 0) == 0 && hovered != pending_click_id) {
        next = hovered;
      } else if (hovered == "hierarchy" || hovered == "left-entities") {
        next = hovered;
      }
      if (next != model_.drop_target_id) {
        model_.drop_target_id = next;
        model_.dirty = true;
      }
    }
    break;
  }
  case SDL_EVENT_KEY_DOWN:
  case SDL_EVENT_KEY_UP: {
    const bool down = (sdl.type == SDL_EVENT_KEY_DOWN);
    key_shift = (sdl.key.mod & SDL_KMOD_SHIFT) != 0;
    switch (sdl.key.scancode) {
    case SDL_SCANCODE_W:
      key_w = down;
      break;
    case SDL_SCANCODE_A:
      key_a = down;
      break;
    case SDL_SCANCODE_S:
      key_s = down;
      break;
    case SDL_SCANCODE_D:
      key_d = down;
      break;
    case SDL_SCANCODE_Q:
      key_q = down;
      break;
    case SDL_SCANCODE_E:
      key_e = down;
      break;
    default:
      break;
    }
    break;
  }
  default:
    break;
  }
}

int
ui_controller::hit_test_handle (float x, float y)
{
  if (document_ == nullptr) {
    return 0;
  }
  auto hit = [this] (const char *id, float px, float py) {
    Rml::Element *el = document_->GetElementById (id);
    if (el == nullptr) {
      return false;
    }
    const Rml::Vector2f offset = el->GetAbsoluteOffset (Rml::BoxArea::Border);
    const Rml::Vector2f size = el->GetBox ().GetSize (Rml::BoxArea::Border);
    constexpr float pad = 3.0F;
    return px >= offset.x - pad && px <= offset.x + size.x + pad
           && py >= offset.y - pad && py <= offset.y + size.y + pad;
  };

  if ((model_.show_entities || model_.show_systems)
      && hit ("handle-left", x, y)) {
    return 1;
  }
  if (model_.show_inspector && hit ("handle-right", x, y)) {
    return 2;
  }
  if ((model_.show_console || model_.show_log || model_.show_agent)
      && hit ("handle-bottom", x, y)) {
    return 3;
  }
  return 0;
}

/** Updates the dragged splitter from a window-space mouse position. */

void
ui_controller::drag_handle (float x, float y)
{
  int width = 0;
  int height = 0;
  model_.svc.window->get_size (width, height);
  if (width <= 0 || height <= 0) {
    return;
  }

  const float x_pct = (x / static_cast<float> (width)) * 100.0F;
  const float y_pct
      = ((static_cast<float> (height) - y) / static_cast<float> (height))
        * 100.0F;

  if (active_handle == 1) {
    model_.left_pct = std::clamp (x_pct, 10.0F, 45.0F);
  } else if (active_handle == 2) {
    model_.right_pct = std::clamp (100.0F - x_pct, 10.0F, 45.0F);
  } else if (active_handle == 3) {
    model_.bottom_pct = std::clamp (y_pct, 12.0F, 60.0F);
  } else {
    return;
  }

  // Keep at least 30% of the width for the game view.
  if (model_.left_pct + model_.right_pct > 70.0F) {
    if (active_handle == 1) {
      model_.left_pct = 70.0F - model_.right_pct;
    } else if (active_handle == 2) {
      model_.right_pct = 70.0F - model_.left_pct;
    }
  }

  view_.apply_layout ();
}

void
ui_controller::wire_events ()
{
  auto listen = [this] (const char *id, const char *event,
                        std::function<void (Rml::Event &)> fn,
                        bool capture = false) {
    Rml::Element *element = document_->GetElementById (id);
    if (element == nullptr) {
      return;
    }
    listeners.push_back (std::make_unique<listener_adapter> (std::move (fn)));
    element->AddEventListener (event, listeners.back ().get (), capture);
  };

  // Note: UI clicks are dispatched at the SDL level in handle_event()
  // (via Context::GetHoverElement after a hover refresh). RmlUi's click
  // event only fires when the pressed element equals the focus element,
  // which does not hold for our generic (non-focusable) divs, so relying
  // on it made tabs and menu items unclickable.

  listen ("console-input", "keydown", [this] (Rml::Event &event) {
    const int key = event.GetParameter<int> ("key_identifier", -1);
    if (key != static_cast<int> (Rml::Input::KI_RETURN)) {
      return;
    }
    auto *control = rmlui_dynamic_cast<Rml::ElementFormControl *> (
        event.GetTargetElement ());
    if (control == nullptr) {
      return;
    }
    const std::string command = control->GetValue ();
    control->SetValue ("");
    if (!command.empty ()) {
      model_.run_console_command (command);
      view_.refresh_console ();
    }
  });

  listen ("log-category", "change", [this] (Rml::Event &event) {
    auto *control = rmlui_dynamic_cast<Rml::ElementFormControl *> (
        event.GetTargetElement ());
    if (control == nullptr) {
      return;
    }
    model_.log_category = control->GetValue ();
    model_.dirty = true;
  });

  // Inspector field commits. Attached in the capture phase so no reliance
  // on change-event bubbling is needed.
  listen (
      "inspector", "change",
      [this] (Rml::Event &event) {
        Rml::Element *target = event.GetTargetElement ();
        if (target == nullptr) {
          return;
        }
        const std::string id = target->GetId ();
        if (id.rfind ("fld-", 0) != 0) {
          return;
        }
        const char *rest = id.c_str () + 4;
        char *sep = nullptr;
        const auto comp
            = static_cast<entt::id_type> (std::strtoull (rest, &sep, 10));
        if (sep == nullptr || *sep != '-') {
          return;
        }
        char *lane_sep = nullptr;
        const auto field = static_cast<entt::id_type> (
            std::strtoull (sep + 1, &lane_sep, 10));
        auto *control = rmlui_dynamic_cast<Rml::ElementFormControl *> (target);
        if (control == nullptr) {
          return;
        }
        bool ok = false;
        if (lane_sep != nullptr && *lane_sep == '-') {
          const int lane
              = static_cast<int> (std::strtol (lane_sep + 1, nullptr, 10));
          ok = model_.commit_inspector_lane (comp, field, lane,
                                             control->GetValue ());
        } else {
          ok = model_.commit_inspector_field (comp, field,
                                              control->GetValue ());
        }
        if (!ok) {
          model_.append_console ("Invalid value; edit reverted.");
        }
        model_.inspector_refresh_forced = true;
        model_.dirty = true;
      },
      true);

  listen ("agent-input", "keydown", [this] (Rml::Event &event) {
    const int key = event.GetParameter<int> ("key_identifier", -1);
    if (key != static_cast<int> (Rml::Input::KI_RETURN)) {
      return;
    }
    agent_send_from_input ();
  });

  // Agent toolbar selects (agent picker + session config options). Attached
  // in the capture phase; the toolbar element itself persists across
  // inner-RML rebuilds.
  listen (
      "agent-toolbar", "change",
      [this] (Rml::Event &event) {
        Rml::Element *target = event.GetTargetElement ();
        if (target == nullptr) {
          return;
        }
        const std::string id = target->GetId ();
        auto *control = rmlui_dynamic_cast<Rml::ElementFormControl *> (target);
        if (control == nullptr) {
          return;
        }
        ensure_agent ();
        if (id == "agent-select") {
          model_.agent->selected_agent (control->GetValue ());
        } else if (id.rfind ("agent-cfg-", 0) == 0) {
          model_.agent->set_config_option (id.substr (10),
                                           control->GetValue ());
        } else {
          return;
        }
        model_.dirty = true;
      },
      true);
}

void
ui_controller::dispatch_click (const std::string &id)
{
  // -- menus ------------------------------------------------------
  if (id.rfind ("btn-menu-", 0) == 0) {
    toggle_menu (id);
    return;
  }
  if (id.rfind ("mi-", 0) == 0) {
    close_menus ();
    handle_menu_action (id);
    return;
  }
  if (!model_.open_menu_id.empty ()) {
    close_menus ();
  }

  // -- panel tabs -------------------------------------------------
  if (id == "tabbtn-entities") {
    model_.left_tab = 0;
    model_.dirty = true;
    return;
  }
  if (id == "tabbtn-systems") {
    model_.left_tab = 1;
    model_.dirty = true;
    return;
  }
  if (id == "tabbtn-files") {
    model_.left_tab = 2;
    model_.dirty = true;
    return;
  }
  if (id == "tabbtn-inspector") {
    model_.right_tab = 0;
    model_.dirty = true;
    return;
  }
  if (id == "tabbtn-build") {
    model_.right_tab = 1;
    model_.dirty = true;
    return;
  }
  if (id == "tabbtn-console") {
    model_.bottom_tab = 0;
    model_.dirty = true;
    return;
  }
  if (id == "tabbtn-log") {
    model_.bottom_tab = 1;
    model_.dirty = true;
    return;
  }
  if (id == "tabbtn-agent") {
    model_.bottom_tab = 2;
    model_.dirty = true;
    return;
  }

  // -- lists ------------------------------------------------------
  if (id.rfind ("cmp-", 0) == 0) {
    const auto key = static_cast<std::uint64_t> (
        std::strtoull (id.c_str () + 4, nullptr, 10));
    if (model_.collapsed_comps.contains (key)) {
      model_.collapsed_comps.erase (key);
    } else {
      model_.collapsed_comps.insert (key);
    }
    model_.inspector_refresh_forced = true;
    model_.dirty = true;
    return;
  }
  if (id.rfind ("fldb-", 0) == 0) {
    const char *rest = id.c_str () + 5;
    char *sep = nullptr;
    const auto comp
        = static_cast<entt::id_type> (std::strtoull (rest, &sep, 10));
    if (sep != nullptr && *sep == '-') {
      const auto field
          = static_cast<entt::id_type> (std::strtoull (sep + 1, nullptr, 10));
      model_.toggle_inspector_bool (comp, field);
    }
    return;
  }
  if (id.rfind ("tool-", 0) == 0) {
    // tool-m<message>-c<call>
    const char *rest = id.c_str () + 5;
    if (*rest == 'm') {
      char *sep = nullptr;
      const long mi = std::strtol (rest + 1, &sep, 10);
      if (sep != nullptr && sep[0] == '-' && sep[1] == 'c') {
        const long ti = std::strtol (sep + 2, nullptr, 10);
        const std::string key
            = "m" + std::to_string (mi) + "-c" + std::to_string (ti);
        if (model_.agent_collapsed_tools.contains (key)) {
          model_.agent_collapsed_tools.erase (key);
        } else {
          model_.agent_collapsed_tools.insert (key);
        }
        model_.dirty = true;
      }
    }
    return;
  }
  if (id == "btn-agent-connect") {
    ensure_agent ();
    if (model_.agent) {
      std::string root;
      if (auto project
          = model_.svc.runtime->resource_manager ().current_project ()) {
        root = project->root_path;
      }
      model_.agent->connect (root);
      model_.dirty = true;
    }
    return;
  }
  if (id == "btn-agent-disconnect") {
    if (model_.agent) {
      model_.agent->disconnect ();
      model_.dirty = true;
    }
    return;
  }
  if (id == "btn-agent-stop") {
    if (model_.agent) {
      model_.agent->cancel ();
      model_.dirty = true;
    }
    return;
  }
  if (id == "btn-agent-send") {
    agent_send_from_input ();
    return;
  }
  if (id == "perm-allow-once" || id == "perm-allow-always"
      || id == "perm-reject-once" || id == "perm-reject") {
    if (model_.agent) {
      model_.agent->dismiss_permission ();
      model_.dirty = true;
    }
    return;
  }
  if (id.rfind ("tgl-", 0) == 0) {
    const auto raw = static_cast<std::uint32_t> (
        std::strtoul (id.c_str () + 4, nullptr, 10));
    if (model_.collapsed_nodes.contains (raw)) {
      model_.collapsed_nodes.erase (raw);
    } else {
      model_.collapsed_nodes.insert (raw);
    }
    model_.dirty = true;
    return;
  }
  if (id.rfind ("ent-", 0) == 0) {
    const auto entity = static_cast<entt::entity> (
        std::strtoul (id.c_str () + 4, nullptr, 10));
    model_.svc.editor->selected_entity (entity);
    model_.selected_singleton.reset ();
    model_.svc.editor->transform_gizmo_visible (true);
    model_.inspector_refresh_forced = true;
    model_.dirty = true;
    return;
  }
  if (id.rfind ("sing-", 0) == 0) {
    const auto type_id = static_cast<entt::id_type> (
        std::strtoull (id.c_str () + 5, nullptr, 10));
    model_.selected_singleton = type_id;
    model_.svc.editor->selected_entity (entt::null);
    model_.svc.editor->transform_gizmo_visible (false);
    model_.inspector_refresh_forced = true;
    model_.dirty = true;
    return;
  }
  if (id.rfind ("scn-", 0) == 0) {
    const auto scene_id = static_cast<entt::id_type> (
        std::strtoull (id.c_str () + 4, nullptr, 10));
    model_.open_scene_by_id (scene_id);
    return;
  }
  if (id.rfind ("kit-", 0) == 0) {
    model_.build_kit_idx
        = static_cast<int> (std::strtol (id.c_str () + 4, nullptr, 10));
    model_.dirty = true;
    return;
  }
  if (id.rfind ("btarget-", 0) == 0) {
    model_.build_target_idx
        = static_cast<int> (std::strtol (id.c_str () + 8, nullptr, 10));
    model_.dirty = true;
    return;
  }
  if (id.rfind ("btest-", 0) == 0) {
    model_.build_test_idx
        = static_cast<int> (std::strtol (id.c_str () + 6, nullptr, 10));
    model_.dirty = true;
    return;
  }
  if (id == "btn-files-refresh") {
    model_.rescan_files ();
    model_.dirty = true;
    return;
  }
  if (id == "btn-build-refresh") {
    model_.start_build_refresh ();
    return;
  }
  if (id == "btn-build-configure") {
    model_.start_build_configure ();
    return;
  }
  if (id == "btn-build-target") {
    model_.start_build_target ();
    return;
  }
  if (id == "btn-build-test") {
    model_.start_build_test ();
    return;
  }
  if (id == "btn-clear-log") {
    if (view_.sink_ != nullptr) {
      view_.sink_->clear ();
    }
    model_.dirty = true;
    return;
  }
  if (id == "btn-log-autoscroll") {
    model_.log_autoscroll = !model_.log_autoscroll;
    model_.dirty = true;
    return;
  }
  if (id.rfind ("btn-lv-", 0) == 0) {
    const std::string level = id.substr (7);
    if (level == "trace") {
      model_.log_show_trace = !model_.log_show_trace;
    } else if (level == "debug") {
      model_.log_show_debug = !model_.log_show_debug;
    } else if (level == "info") {
      model_.log_show_info = !model_.log_show_info;
    } else if (level == "warn") {
      model_.log_show_warn = !model_.log_show_warn;
    } else if (level == "error") {
      model_.log_show_error = !model_.log_show_error;
    } else if (level == "fatal") {
      model_.log_show_fatal = !model_.log_show_fatal;
    }
    model_.dirty = true;
    return;
  }
  if (id.rfind ("recent-", 0) == 0) {
    const std::size_t index = static_cast<std::size_t> (
        std::strtoul (id.c_str () + 7, nullptr, 10));
    if (index < model_.recent_projects.size ()) {
      model_.open_project (model_.recent_projects[index]);
    }
    return;
  }

  // -- new-project modal -----------------------------------------
  if (id == "np-browse") {
    model_.show_select_folder_dialog ();
    return;
  }
  if (id == "np-create-folder") {
    model_.new_project_create_folder = !model_.new_project_create_folder;
    model_.dirty = true;
    return;
  }
  if (id == "np-create") {
    create_project_from_form ();
    return;
  }
  if (id == "np-cancel") {
    model_.show_new_project = false;
    model_.dirty = true;
    return;
  }

  // -- welcome ----------------------------------------------------
  if (id == "welcome-new") {
    model_.show_new_project = true;
    model_.dirty = true;
    return;
  }
  if (id == "welcome-load") {
    model_.show_project_dialog ();
    return;
  }
  if (id == "welcome") {
    // Clicking the backdrop (outside the card) dismisses the splash.
    model_.show_welcome = false;
    model_.dirty = true;
    return;
  }

  // -- game view toolbar ------------------------------------------
  if (id == "btn-play") {
    model_.svc.runtime->set_running (true);
  } else if (id == "btn-pause") {
    if (model_.svc.runtime->is_running ()) {
      model_.svc.runtime->set_running (false);
    } else if (model_.svc.runtime->in_play_session ()) {
      model_.svc.runtime->set_running (true);
    }
  } else if (id == "btn-stop") {
    if (model_.svc.runtime->in_play_session ()) {
      model_.svc.runtime->request_stop ();
    }
  } else if (id == "btn-translate") {
    model_.svc.editor->gizmo_mode (wsl::gfx::transform_gizmo_mode::translate);
    model_.svc.editor->transform_gizmo_visible (true);
  } else if (id == "btn-rotate") {
    model_.svc.editor->gizmo_mode (wsl::gfx::transform_gizmo_mode::rotate);
    model_.svc.editor->transform_gizmo_visible (true);
  } else if (id == "btn-scale") {
    model_.svc.editor->gizmo_mode (wsl::gfx::transform_gizmo_mode::scale);
    model_.svc.editor->transform_gizmo_visible (true);
  } else if (id == "btn-refresh") {
    // Panels refresh on a throttle; force an immediate rebuild.
  }

  model_.dirty = true;
}

void
ui_controller::handle_menu_action (const std::string &id)
{
  if (id == "mi-new-project") {
    model_.show_new_project = true;
  } else if (id == "mi-load-project") {
    model_.show_project_dialog ();
  } else if (id == "mi-exit") {
    model_.request_exit ();
  } else if (id == "mi-load-scene") {
    model_.show_load_scene_dialog ();
  } else if (id == "mi-save-scene") {
    model_.show_save_scene_dialog ();
  } else if (id == "mi-reload-scene") {
    model_.reload_active_scene ();
  } else if (id == "mi-toggle-entities") {
    model_.show_entities = !model_.show_entities;
  } else if (id == "mi-toggle-systems") {
    model_.show_systems = !model_.show_systems;
  } else if (id == "mi-toggle-inspector") {
    model_.show_inspector = !model_.show_inspector;
  } else if (id == "mi-toggle-console") {
    model_.show_console = !model_.show_console;
  } else if (id == "mi-toggle-log") {
    model_.show_log = !model_.show_log;
  } else if (id == "mi-toggle-agent") {
    model_.show_agent = !model_.show_agent;
  } else if (id == "mi-toggle-welcome") {
    model_.show_welcome = !model_.show_welcome;
  }
  model_.dirty = true;
}

void
ui_controller::toggle_menu (const std::string &button_id)
{
  // "btn-menu-file" -> "menu-file"
  const std::string menu_id = "menu-" + button_id.substr (9);
  if (model_.open_menu_id == menu_id) {
    close_menus ();
    return;
  }
  close_menus ();
  if (Rml::Element *menu = document_->GetElementById (menu_id);
      menu != nullptr) {
    menu->SetClass ("shown", true);
    model_.open_menu_id = menu_id;
  }
}

void
ui_controller::close_menus ()
{
  if (model_.open_menu_id.empty ()) {
    return;
  }
  if (Rml::Element *menu = document_->GetElementById (model_.open_menu_id);
      menu != nullptr) {
    menu->SetClass ("shown", false);
  }
  model_.open_menu_id.clear ();
}

// ------------------------------------------------------------ projects
} // namespace editor_rml
