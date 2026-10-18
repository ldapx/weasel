#include "render_frame.hpp"

#include "../comp/hierarchy.hpp"
#include "../comp/model_instance_3d.hpp"
#include "../comp/skeleton_pose.hpp"
#include "../comp/transform.hpp"
#include "../comp/singl/editor_context.hpp"
#include "../comp/singl/rendering_manager.hpp"
#include "../comp/singl/runtime_context.hpp"
#include "../comp/subviewport.hpp"
#include "../comp/world_transform.hpp"
#include "comp/camera.hpp"
#include "comp/camera_2d.hpp"
#include "comp/transform_2d.hpp"

#include <tracy/Tracy.hpp>
#include "gfx/scene_renderer.hpp"
#include <algorithm>
#include <cstdint>
#include <entt/entity/entity.hpp>
#include <entt/entity/fwd.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/matrix.hpp>
#include <glm/trigonometric.hpp>

namespace wsl
{

void
sys::render_submission::reset ()
{
  view = gfx::scene_renderer::view_state{};
  // Palettes must go with the draw commands that reference them; clearing one
  // without the other would leave dangling pointers in the other.
  draw_commands.clear ();
  palettes.clear ();
  environment = nullptr;
  is_2d_view = false;
}

bool
sys::build_render_frame (entt::registry &registry,
                         comp::singl::runtime_context &runtime_ctx,
                         render_submission &out,
                         const render_view_target &target)
{
  ZoneScoped;
  out.reset ();

  auto *scene = runtime_ctx.scene_manager ().get_active ();
  if (scene == nullptr) {
    return false;
  }

  auto &ctx = registry.ctx ();

  entt::entity const vp = target.viewport;
  bool const is_2d_vp = target.mode == comp::subviewport_mode::mode_2d;
  float const aspect = target.resolved.aspect_ratio ();
  float const virt_w = target.resolved.virtual_size.x ();
  float const virt_h = target.resolved.virtual_size.y ();

  entt::entity effective_camera = entt::null;
  bool view_resolved = false;

  auto *editor_ctx = runtime_ctx.editor_ctx ();
  bool const paused_editor
      = (editor_ctx != nullptr) && !runtime_ctx.is_running ();

  // ---- Editor override (D9) ----
  // While stopped, the editor-selected viewport + camera always win for the
  // game view, including sub preview. Pressing play switches to the
  // subviewport's own cameras (runtime selection).
  bool editor_override = false;
  if (editor_ctx != nullptr) {
    if (vp == entt::null) {
      editor_override = true;
    } else if (paused_editor
               && editor_ctx->game_view_selected_viewport () == vp) {
      editor_override = true;
    }
  }

  if (editor_override) {
    wsl::comp::singl::editor_context::resolved_camera resolved_camera;
    if (editor_ctx->resolve_game_view_camera (registry, scene, resolved_camera)
        && resolved_camera.valid ()) {
      out.view.valid = true;
      out.view.aspect_ratio = aspect;
      out.view.world_position = resolved_camera.world_pos ();
      out.view.view = resolved_camera.view ();
      out.view.proj = resolved_camera.proj ();
      out.view.view_proj = resolved_camera.vp ();
      effective_camera = resolved_camera.entity ();
      view_resolved = true;

      if (paused_editor) {
        auto mode = editor_ctx->resolve_game_view_mode (registry, scene);
        out.is_2d_view = (mode
                              == wsl::comp::singl::editor_context::
                                  game_view_mode::mode_2d_edit
                          || mode
                                 == wsl::comp::singl::editor_context::
                                     game_view_mode::mode_2d_view);
      }
    } else if (vp == entt::null) {
      // Root viewport without a resolvable editor camera: nothing to draw.
      return false;
    }
  }

  // ---- Runtime camera (component-driven mode) ----
  if (!view_resolved) {
    if (vp == entt::null) {
      effective_camera = scene->camera;
    } else if (auto *sv = registry.try_get<comp::subviewport> (vp)) {
      if (sv->camera.value != entt::null && registry.valid (sv->camera.value)) {
        effective_camera = sv->camera.value;
      }
    }

    bool camera_usable = false;
    if (effective_camera != entt::null) {
      if (is_2d_vp) {
        camera_usable = registry.all_of<comp::camera_2d, comp::transform_2d> (
            effective_camera);
      } else {
        camera_usable = registry.all_of<comp::camera, comp::world_transform> (
            effective_camera);
      }
    }

    if (camera_usable) {
      if (is_2d_vp) {
        const auto &cam2d = registry.get<comp::camera_2d> (effective_camera);
        const auto &t2d = registry.get<comp::transform_2d> (effective_camera);

        out.view.valid = true;
        out.view.aspect_ratio = aspect;
        out.view.world_position
            = glm::vec3 (t2d.position.x (), t2d.position.y (), 0);
        // Top-left convention: the camera's `position` is the top-left of
        // the visible area and `zoom` scales world units. `transform_2d`
        // sprite positions are in virtual-canvas pixels with (0, 0) at the
        // top-left, matching the 3D view.
        glm::mat4 view_mat = glm::mat4 (1.0F);
        view_mat
            = glm::scale (view_mat, glm::vec3 (cam2d.zoom, cam2d.zoom, 1.0F));
        view_mat = glm::translate (
            view_mat, glm::vec3 (-t2d.position.x () * cam2d.zoom,
                                 -t2d.position.y () * cam2d.zoom, 0.0F));
        out.view.view = view_mat;
        out.view.proj = glm::ortho (0.0F, virt_w, virt_h, 0.0F, -1.0F, 1.0F);
        out.is_2d_view = true;
      } else {
        const auto &cam = registry.get<comp::camera> (effective_camera);
        const comp::world_transform &wt
            = registry.get<comp::world_transform> (effective_camera);
        glm::mat4 const wtm = wt.value ();

        // When the camera lives inside a subviewport its world_transform
        // inherits the parent subviewport's scale (world_quad_size). Scale in
        // the view matrix distorts the projection frustum and causes both the
        // skybox and 3D models to be clipped, so orthonormalise the 3x3 part
        // while keeping the world position.
        bool const cam_is_inside_viewport
            = (vp != entt::null
               && comp::find_nearest_viewport (registry, effective_camera)
                      == vp);

        glm::mat4 cam_transform = wtm;
        if (cam_is_inside_viewport) {
          glm::vec3 scale;
          glm::vec3 skew;
          glm::vec3 translation;
          glm::vec4 perspective;
          glm::quat rotation;
          glm::decompose (wtm, scale, rotation, translation, skew, perspective);
          cam_transform = glm::translate (glm::mat4 (1.0F), translation)
                          * glm::mat4_cast (rotation);
        }

        out.view.valid = true;
        out.view.aspect_ratio = aspect;
        out.view.world_position = glm::vec3 (cam_transform[3]);
        out.view.view = glm::inverse (cam_transform);
        out.view.proj = glm::perspective (glm::radians (cam.fov ()), aspect,
                                          cam.near (), cam.far ());
      }
      out.view.view_proj = out.view.proj * out.view.view;
      view_resolved = true;
    } else if (vp == entt::null) {
      // Root viewport keeps the historical standalone fallback: look at the
      // origin from (0, 0, 5).
      out.view.valid = true;
      out.view.aspect_ratio = aspect;
      out.view.world_position = glm::vec3 (0, 0, 5);
      out.view.view = glm::lookAt (out.view.world_position, glm::vec3 (0),
                                   glm::vec3 (0, 1, 0));
      out.view.proj
          = glm::perspective (glm::radians (60.0F), aspect, 0.1F, 1000.0F);
      out.view.view_proj = out.view.proj * out.view.view;
      view_resolved = true;
    } else {
      // SubViewport without an assigned (valid, mode-matching) camera: the
      // viewport clears its target and skips all draw passes (S2).
      return false;
    }

    // If the editor did not already flag 2D, an actual 2D camera decides.
    if (!out.is_2d_view && effective_camera != entt::null
        && registry.valid (effective_camera)
        && registry.all_of<comp::camera_2d> (effective_camera)) {
      out.is_2d_view = true;
    }
  }

  // ---- Environment (per-viewport skybox, D4/C3) ----
  if (target.skybox.value != rsc::no_resource_id) {
    auto cubemap = runtime_ctx.resource_manager ().get (target.skybox);
    if (cubemap) {
      out.environment = &(*cubemap);
    }
  }

  // Only collect 3D draw commands for non-2D views.
  if (!out.is_2d_view) {
    const entt::entity selected_entity
        = ((runtime_ctx.editor_ctx () != nullptr) && !runtime_ctx.is_running ())
              ? runtime_ctx.editor_ctx ()->selected_entity ()
              : entt::null;

    auto draw_view
        = registry.view<comp::world_transform, comp::model_instance_3d> ();
    out.draw_commands.reserve (draw_view.size_hint ());

    for (entt::entity const entity : draw_view) {
      // Hierarchical filtering: only collect entities belonging to this
      // viewport.
      if (!comp::entity_in_viewport_scope (registry, entity, vp)) {
        continue;
      }

      const comp::world_transform &world
          = draw_view.get<comp::world_transform> (entity);
      const comp::model_instance_3d &instance
          = draw_view.get<comp::model_instance_3d> (entity);

      auto model = runtime_ctx.resource_manager ().get (instance.id);
      if (!model) {
        continue;
      }

      // Publish the animated joint palette when the animation system produced
      // one for this entity.
      const comp::skeleton_pose *pose
          = registry.try_get<comp::skeleton_pose> (entity);
      const gfx::scene_renderer::joint_palette *palette = nullptr;
      if (pose != nullptr && !pose->palette.empty ()) {
        const std::size_t count
            = std::min (pose->palette.size (),
                        gfx::scene_renderer::joint_palette::max_joints);
        out.palettes.emplace_back ();
        out.palettes.back ().matrices.assign (
            pose->palette.begin (),
            pose->palette.begin () + static_cast<ptrdiff_t> (count));
        palette = &out.palettes.back ();
      }

      out.draw_commands.push_back (gfx::scene_renderer::draw_command{
          .model = &(*model),
          .scene_index = instance.scene_index,
          .transform = static_cast<glm::mat4> (world.value ()),
          .entity = entity,
          .draw_outline = (entity == selected_entity),
          .mip_lod_bias = instance.mip_lod_bias,
          .geometry_lod_bias = instance.geometry_lod_bias,
          .visibility_range = instance.visibility_range,
          .material_override = instance.material_override,
          .palette = palette,
      });
    }
  }

  return out.view.valid;
}

} // namespace wsl
