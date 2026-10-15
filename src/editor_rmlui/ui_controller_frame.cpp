#include "ui_controller.hpp"

#include "ui_util.hpp"
#include "ui_view.hpp"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/Traits.h>

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_events.h>

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/geometric.hpp>
#include <glm/matrix.hpp>
#include <glm/trigonometric.hpp>

#include "wsl/comp/hierarchy.hpp"
#include "wsl/comp/singl/editor_context.hpp"
#include "wsl/comp/singl/runtime_context.hpp"
#include "wsl/comp/transform.hpp"
#include "wsl/comp/world_transform.hpp"
#include "wsl/event.hpp"
#include "wsl/gfx/render_window.hpp"
#include "wsl/gfx/transform_gizmo.hpp"
#include "wsl/gfx/view_gizmo.hpp"
#include "wsl/math/vector.hpp"
#include "wsl/rsc/scene.hpp"
#include "wsl/rsc/scene_manager.hpp"
#include "wsl/reg/component_registry.hpp"

namespace editor_rml
{

float
ui_controller::pointer_density () const
{
  if ((model_.svc.window != nullptr)
      && (model_.svc.window->handler () != nullptr)) {
    if (const float d
        = SDL_GetWindowPixelDensity (model_.svc.window->handler ());
        d > 0.0F) {
      return d;
    }
  }
  return 1.0F;
}

/**
 * Returns the id of the deepest-named element under the point.
 *
 * A pure hit test via Context::GetElementAtPoint (no hover-chain side
 * effects). The point is scaled by the window's pixel density to match the
 * convention RmlSDL uses when it feeds motion events, so the hover state
 * and the SDL-level click dispatch always agree.
 */

std::string
ui_controller::hovered_element_id (float x, float y)
{
  if (context_ == nullptr) {
    return {};
  }
  const float density = pointer_density ();
  for (Rml::Element *el
       = context_->GetElementAtPoint (Rml::Vector2f (x * density, y * density));
       el != nullptr; el = el->GetParentNode ()) {
    if (const std::string id = el->GetId (); !id.empty ()) {
      return id;
    }
  }
  return {};
}

/**
 * One-shot diagnostic dump (tab geometry + a synthetic hit test at the
 * Entities tab's centre) written to the log. This makes layout/input issues
 * diagnosable from a log/screenshot alone.
 */

void
ui_controller::process_dialog_result ()
{
  if (!model_.dialog_finished) {
    return;
  }
  model_.dialog_finished = false;
  model_.waiting_for_dialog = false;
  const ui_model::dialog_mode mode = model_.pending_dialog;
  model_.pending_dialog = ui_model::dialog_mode::none;

  if (!model_.dialog_result) {
    model_.dirty = true;
    return;
  }

  switch (mode) {
  case ui_model::dialog_mode::open_project:
    model_.open_project (*model_.dialog_result);
    break;
  case ui_model::dialog_mode::load_scene:
    if (model_.svc.load_scene) {
      model_.svc.load_scene (*model_.dialog_result);
      model_.append_console ("Opening scene: " + *model_.dialog_result);
    }
    break;
  case ui_model::dialog_mode::save_scene: {
    wsl::rsc::scene *scene = model_.svc.runtime->scene_manager ().get_active ();
    if (scene != nullptr) {
      const std::filesystem::path path_var (*model_.dialog_result);
      const bool is_prefab = path_var.extension () == ".prefab";
      if (model_.svc.runtime->resource_manager ().save_scene (
              *scene, *model_.dialog_result, is_prefab)) {
        model_.append_console ("Saved scene: " + *model_.dialog_result);
      } else {
        model_.append_console ("Failed to save scene: "
                               + *model_.dialog_result);
      }
    }
    break;
  }
  case ui_model::dialog_mode::select_folder: {
    if (auto *control = rmlui_dynamic_cast<Rml::ElementFormControl *> (
            document_->GetElementById ("np-folder"));
        control != nullptr) {
      control->SetValue (*model_.dialog_result);
    }
    break;
  }
  case ui_model::dialog_mode::none:
    break;
  }

  model_.dialog_result.reset ();
  model_.dirty = true;
}

bool
ui_controller::camera_input_enabled () const
{
  return (model_.svc.runtime != nullptr) && !model_.svc.runtime->is_running ();
}

void
ui_controller::update_camera (double dt)
{
  if (!camera_input_enabled ()) {
    return;
  }

  glm::vec3 move (0.0F);
  const glm::quat rot = model_.svc.editor->editor_cam_rot ();
  const glm::vec3 forward = rot * glm::vec3 (0.0F, 0.0F, -1.0F);
  const glm::vec3 right = rot * glm::vec3 (1.0F, 0.0F, 0.0F);
  const glm::vec3 up = rot * glm::vec3 (0.0F, 1.0F, 0.0F);

  if (key_w) {
    move += forward;
  }
  if (key_s) {
    move -= forward;
  }
  if (key_d) {
    move += right;
  }
  if (key_a) {
    move -= right;
  }
  if (key_e) {
    move += up;
  }
  if (key_q) {
    move -= up;
  }

  if (glm::dot (move, move) <= 0.0F) {
    return;
  }

  const float speed = (key_shift ? 24.0F : 6.0F) * static_cast<float> (dt);
  model_.svc.editor->editor_cam_pos (model_.svc.editor->editor_cam_pos ()
                                     + glm::normalize (move) * speed);
}

/**
 * Drives the 3D game-view gizmos (view navigation cube + entity transform
 * handles) from the ``#gameview`` element rect.
 *
 * Mirrors the ImGui editor's game_view 3D interaction: the scene renders
 * fullscreen into the present texture and is blitted into the game-view
 * rect, so the rect origin/size double as the gizmo viewport (NDC is
 * resolution independent, and the blit maps fullscreen NDC back onto the
 * rect). The 3D render pass consumes the flags/layout set here.
 */

void
ui_controller::update_game_view_gizmos (double dt)
{
  if ((document_ == nullptr) || (model_.svc.editor == nullptr)
      || (model_.svc.runtime == nullptr) || (model_.svc.window == nullptr)) {
    return;
  }

  Rml::Element *view = document_->GetElementById ("gameview");
  if (view == nullptr) {
    return;
  }
  const Rml::Vector2f offset = view->GetAbsoluteOffset (Rml::BoxArea::Border);
  const Rml::Vector2f box_size = view->GetBox ().GetSize (Rml::BoxArea::Border);
  if ((box_size.x <= 0.0F) || (box_size.y <= 0.0F)) {
    return;
  }

  model_.svc.editor->last_img_min (glm::vec2 (offset.x, offset.y));
  model_.svc.editor->last_img_size (glm::vec2 (box_size.x, box_size.y));

  float mouse_x = 0.0F;
  float mouse_y = 0.0F;
  const SDL_MouseButtonFlags buttons = SDL_GetMouseState (&mouse_x, &mouse_y);
  const bool lmb_down = (buttons & SDL_BUTTON_LMASK) != 0;
  const bool lmb_released = (!lmb_down && prev_lmb_down);
  glm::vec2 mouse_delta (0.0F);
  if (have_prev_mouse) {
    mouse_delta = glm::vec2 (mouse_x - prev_mouse_x, mouse_y - prev_mouse_y);
  }
  prev_mouse_x = mouse_x;
  prev_mouse_y = mouse_y;
  prev_lmb_down = lmb_down;
  have_prev_mouse = true;

  auto hide_gizmos = [this] () {
    model_.svc.editor->view_gizmo_visible (false);
    model_.svc.editor->transform_gizmo_visible (false);
    model_.svc.editor->grid_visible (false);
  };

  if (model_.svc.runtime->is_running ()) {
    hide_gizmos ();
    return;
  }

  wsl::rsc::scene *scene = model_.svc.runtime->scene_manager ().get_active ();
  if (scene == nullptr) {
    hide_gizmos ();
    return;
  }

  entt::registry &registry = scene->get_registry ();
  wsl::comp::singl::editor_context::resolved_camera rc{};
  if (!model_.svc.editor->resolve_game_view_camera (registry, scene, rc)) {
    hide_gizmos ();
    return;
  }

  const auto mode = model_.svc.editor->resolve_game_view_mode (registry, scene);
  const bool is_3d_edit
      = (mode == wsl::comp::singl::editor_context::game_view_mode::mode_3d_edit)
        || (mode
            == wsl::comp::singl::editor_context::game_view_mode::mode_3d_fly);
  if (!is_3d_edit) {
    hide_gizmos ();
    return;
  }

  const glm::vec2 view_size (box_size.x, box_size.y);
  const glm::vec2 mouse_local (mouse_x - offset.x, mouse_y - offset.y);
  const bool inside = (mouse_local.x >= 0.0F) && (mouse_local.y >= 0.0F)
                      && (mouse_local.x <= view_size.x)
                      && (mouse_local.y <= view_size.y);
  const bool ui_covers = model_.show_welcome || model_.show_new_project;
  const bool pointer_over
      = inside && !ui_covers
        && (hovered_element_id (mouse_x, mouse_y) == "gameview");

  // Grid follows the resolved editor camera, like the ImGui game view.
  {
    const glm::mat4 view_mat = rc.view ();
    const glm::vec3 forward
        = -glm::vec3 (view_mat[0][2], view_mat[1][2], view_mat[2][2]);
    const float tilt = std::abs (forward.y);
    const float ground_t
        = (tilt > 0.01F) ? (std::abs (rc.world_pos ().y) / tilt) : 500.0F;
    const float base_fog_radius
        = std::min (250.0F, (ground_t * std::sqrt (ground_t)) + 10.0F);
    const glm::vec3 flat_fwd
        = (glm::length (glm::vec2 (forward.x, forward.z)) > 0.001F)
              ? glm::normalize (glm::vec3 (forward.x, 0.0F, forward.z))
              : glm::vec3 (0.0F, 0.0F, 0.0F);
    const float shift_dist = std::min (ground_t, base_fog_radius * 0.5F);
    const glm::vec3 fog_center
        = glm::vec3 (rc.world_pos ().x, 0.0F, rc.world_pos ().z)
          + flat_fwd * shift_dist;
    model_.svc.editor->grid_visible (true);
    model_.svc.editor->grid_camera_pos (rc.world_pos ());
    model_.svc.editor->grid_fog_center (fog_center);
    model_.svc.editor->grid_fog_radius (base_fog_radius);
  }

  // 3D view (navigation) gizmo in the top-right of the game view.
  bool block_entity_gizmo = false;
  {
    const bool show_view_gizmo
        = (model_.svc.editor->game_view_camera_selection ()
           == wsl::comp::singl::editor_context::game_view_camera_sel::
               default_editor)
          || (model_.svc.editor->game_view_camera_selection ()
              == wsl::comp::singl::editor_context::game_view_camera_sel::
                  editor_3d);
    if (show_view_gizmo) {
      const float gizmo_scale = 0.30F;
      const float gizmo_half = 128.0F * gizmo_scale;
      const float pad = 8.0F;
      const glm::vec2 gizmo_center (view_size.x - gizmo_half - pad,
                                    gizmo_half + pad);

      glm::vec3 pivot (0.0F);
      const entt::entity selected = model_.svc.editor->selected_entity ();
      if ((selected != entt::null) && registry.valid (selected)
          && registry.all_of<wsl::comp::world_transform> (selected)) {
        const glm::mat4 world
            = registry.get<wsl::comp::world_transform> (selected).value ();
        pivot = glm::vec3 (world[3]);
      }

      glm::vec3 cam_pos = model_.svc.editor->editor_cam_pos ();
      const float min_dist = 0.10F;
      glm::vec3 to_cam = cam_pos - pivot;
      const float dist_sq = glm::dot (to_cam, to_cam);
      if (dist_sq < min_dist * min_dist) {
        if (dist_sq < 1.0e-8F) {
          glm::vec3 forward = model_.svc.editor->editor_cam_rot ()
                              * glm::vec3 (0.0F, 0.0F, -1.0F);
          if (glm::dot (forward, forward) < 1.0e-8F) {
            forward = glm::vec3 (0.0F, 0.0F, -1.0F);
          }
          to_cam = -glm::normalize (forward);
        } else {
          to_cam /= std::sqrt (dist_sq);
        }
        cam_pos = pivot + to_cam * min_dist;
      }

      wsl::gfx::view_gizmo_input gizmo_input{};
      gizmo_input.pointer_position
          = wsl::gfx::view_gizmo_ndc_from_pixel (mouse_local, view_size);
      gizmo_input.pointer_delta
          = wsl::gfx::view_gizmo_ndc_delta_from_pixel (mouse_delta, view_size);
      gizmo_input.delta_time = static_cast<float> (dt);
      gizmo_input.primary_down = lmb_down;
      gizmo_input.primary_released = lmb_released;
      gizmo_input.pointer_over_viewport = pointer_over;

      const wsl::gfx::view_gizmo_layout gizmo_layout
          = wsl::gfx::view_gizmo_layout::from_pixel_center (
              gizmo_center, view_size, gizmo_scale);

      wsl::gfx::view_gizmo_camera gizmo_camera{};
      gizmo_camera.position = cam_pos;
      gizmo_camera.rotation = model_.svc.editor->editor_cam_rot ();
      gizmo_camera.pivot = pivot;

      const bool modified = model_.svc.editor->view_gizmo_widget ().interact (
          gizmo_input, gizmo_layout, gizmo_camera);
      if (modified || model_.svc.editor->view_gizmo_widget ().is_active ()) {
        model_.svc.editor->editor_cam_pos (gizmo_camera.position);
        model_.svc.editor->editor_cam_rot (gizmo_camera.rotation);
        model_.svc.editor->cancel_editor_camera_anim ();
      }

      model_.svc.editor->view_gizmo_layout (gizmo_layout);
      model_.svc.editor->view_gizmo_visible (true);

      block_entity_gizmo
          = model_.svc.editor->view_gizmo_widget ().is_active ()
            || model_.svc.editor->view_gizmo_widget ().is_hovered ();
    } else {
      model_.svc.editor->view_gizmo_visible (false);
    }
  }

  // 3D entity (transform) gizmo for the selected entity.
  if (!block_entity_gizmo) {
    wsl::gfx::transform_gizmo &gizmo_widget
        = model_.svc.editor->transform_gizmo_widget ();
    const auto gizmo_mode = model_.svc.editor->gizmo_mode ();
    const entt::entity selected = model_.svc.editor->selected_entity ();
    const bool have_target
        = (selected != entt::null) && registry.valid (selected)
          && registry.all_of<wsl::comp::transform, wsl::comp::world_transform> (
              selected);

    if (have_target && (gizmo_mode != wsl::gfx::transform_gizmo_mode::none)) {
      glm::mat4 world
          = registry.get<wsl::comp::world_transform> (selected).value ();

      wsl::gfx::transform_gizmo_input gizmo_input{};
      gizmo_input.view = rc.view ();
      gizmo_input.proj = rc.proj ();
      gizmo_input.camera_position = rc.world_pos ();
      gizmo_input.viewport_min = glm::vec2 (offset.x, offset.y);
      gizmo_input.viewport_size = view_size;
      gizmo_input.pointer_position = glm::vec2 (mouse_x, mouse_y);
      gizmo_input.pointer_over_viewport = pointer_over;
      gizmo_input.primary_down = lmb_down;

      gizmo_widget.manipulate (gizmo_input, gizmo_mode, world);

      if (gizmo_widget.is_using ()) {
        glm::mat4 local = world;
        if (const auto *hierarchy
            = registry.try_get<wsl::comp::hierarchy> (selected);
            hierarchy != nullptr && hierarchy->parent != entt::null
            && registry.all_of<wsl::comp::world_transform> (
                hierarchy->parent)) {
          const glm::mat4 parent_world
              = registry.get<wsl::comp::world_transform> (hierarchy->parent)
                    .value ();
          local = glm::inverse (parent_world) * world;
        }

        glm::vec3 pos (local[3]);
        glm::vec3 axis_x (local[0]);
        glm::vec3 axis_y (local[1]);
        glm::vec3 axis_z (local[2]);
        glm::vec3 scl (glm::length (axis_x), glm::length (axis_y),
                       glm::length (axis_z));
        if (scl.x > 1.0e-8F) {
          axis_x /= scl.x;
        }
        if (scl.y > 1.0e-8F) {
          axis_y /= scl.y;
        }
        if (scl.z > 1.0e-8F) {
          axis_z /= scl.z;
        }
        const glm::quat rot
            = glm::quat_cast (glm::mat3 (axis_x, axis_y, axis_z));

        wsl::comp::transform &tr
            = registry.get<wsl::comp::transform> (selected);
        tr.position = wsl::math::vec3f{ pos };
        tr.rotation = wsl::math::quatf{ rot };
        tr.scale = wsl::math::vec3f{ scl };
      }

      model_.svc.editor->transform_gizmo_visible (true);
    } else {
      gizmo_widget.reset ();
      model_.svc.editor->transform_gizmo_visible (false);
    }
  } else {
    // Keep the transform gizmo drawn while the view gizmo owns the pointer.
    model_.svc.editor->transform_gizmo_visible (
        model_.svc.editor->selected_entity () != entt::null
        && model_.svc.editor->gizmo_mode ()
               != wsl::gfx::transform_gizmo_mode::none);
  }
}

void
ui_controller::attach (Rml::ElementDocument *document, Rml::Context *context)
{
  document_ = document;
  context_ = context;
}

void
ui_controller::detach ()
{
  document_ = nullptr;
  context_ = nullptr;
}

void
ui_controller::clear_event_listeners ()
{
  listeners.clear ();
}

void
ui_controller::update_frame (double dt)
{
  if (context_ == nullptr) {
    return;
  }

  int width = 0;
  int height = 0;
  model_.svc.window->get_size (width, height);
  if (width > 0 && height > 0) {
    const Rml::Vector2i dims = context_->GetDimensions ();
    if (dims.x != width || dims.y != height) {
      context_->SetDimensions (Rml::Vector2i{ width, height });
    }
  }

  update_camera (dt);
  process_dialog_result ();
  model_.poll_build_job ();

  // Keep RmlUi's hover state tied to the real pointer position every frame.
  // Hovering then never depends on motion-event delivery (or on events being
  // lost), and a layout change under a stationary cursor is picked up at
  // once. Only done while the window has mouse focus so a cursor over
  // another window cannot leave a phantom hover highlight behind.
  if ((model_.svc.window != nullptr)
      && (model_.svc.window->handler () != nullptr)
      && ((SDL_GetWindowFlags (model_.svc.window->handler ())
           & SDL_WINDOW_MOUSE_FOCUS)
          != 0)) {
    float mx = 0.0F;
    float my = 0.0F;
    (void)SDL_GetMouseState (&mx, &my);
    context_->ProcessMouseMove (static_cast<int> (mx * pointer_density ()),
                                static_cast<int> (my * pointer_density ()), 0);
  }

  // Welcome visibility: auto-dismiss once a project first appears (like
  // the ImGui editor's welcome tab), re-openable from the Window menu.
  if (!model_.welcome_project_seen
      && model_.svc.runtime->resource_manager ().current_project ()
             != nullptr) {
    model_.welcome_project_seen = true;
    model_.show_welcome = false;
    model_.dirty = true;
  }

  update_game_view_gizmos (dt);
}

void
ui_controller::create_project_from_form ()
{
  std::string name = "NewProject";
  std::string folder = "./";
  if (document_ == nullptr) {
    return;
  }
  if (auto *el = rmlui_dynamic_cast<Rml::ElementFormControl *> (
          document_->GetElementById ("np-name"));
      el != nullptr) {
    name = el->GetValue ();
  }
  if (auto *el = rmlui_dynamic_cast<Rml::ElementFormControl *> (
          document_->GetElementById ("np-folder"));
      el != nullptr) {
    folder = el->GetValue ();
  }
  if (name.empty ()) {
    name = "NewProject";
  }
  if (folder.empty ()) {
    folder = "./";
  }
  model_.create_project (name, folder);
}
} // namespace editor_rml
