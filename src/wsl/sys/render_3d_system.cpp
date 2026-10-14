#include "render_3d_system.hpp"

#include "../comp/directional_light.hpp"
#include "../comp/singl/editor_context.hpp"
#include "../comp/singl/runtime_context.hpp"
#include "../gfx/debug_overlay.hpp"
#include "../gfx/scene_renderer.hpp"
#include "../gfx/transform_gizmo.hpp"
#include "../gfx/view_gizmo.hpp"
#include "comp/camera.hpp"
#include "comp/world_transform.hpp"
#include <cmath>
#include <entt/entity/fwd.hpp>
#include <glm/common.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/geometric.hpp>
#include <glm/matrix.hpp>
#include <glm/trigonometric.hpp>
#include <utility>
#include <vector>

namespace wsl
{

namespace sys
{

namespace
{

/**
 * Projects a world-space segment into normalized device coordinates.
 *
 * Endpoints behind the near plane are clipped against ``w = 1e-3`` so the
 * perspective divide stays finite.
 *
 * :param view_proj: View-projection matrix of the active frame.
 * :param a: World-space start point.
 * :param b: World-space end point.
 * :param out_a: Receives the projected start point.
 * :param out_b: Receives the projected end point.
 * :return: ``false`` when the segment lies completely behind the camera.
 */
bool
project_segment (const glm::mat4 &view_proj, const glm::vec3 &a,
                 const glm::vec3 &b, glm::vec2 &out_a, glm::vec2 &out_b)
{
  constexpr float near_w = 1.0e-3F;
  glm::vec4 clip_a = view_proj * glm::vec4 (a, 1.0F);
  glm::vec4 clip_b = view_proj * glm::vec4 (b, 1.0F);

  if ((clip_a.w <= near_w) && (clip_b.w <= near_w)) {
    return false;
  }
  if (clip_a.w <= near_w) {
    clip_a = glm::mix (clip_a, clip_b,
                       (near_w - clip_a.w) / (clip_b.w - clip_a.w));
  } else if (clip_b.w <= near_w) {
    clip_b = glm::mix (clip_b, clip_a,
                       (near_w - clip_b.w) / (clip_a.w - clip_b.w));
  }

  out_a = glm::vec2 (clip_a.x / clip_a.w, clip_a.y / clip_a.w);
  out_b = glm::vec2 (clip_b.x / clip_b.w, clip_b.y / clip_b.w);
  return true;
}

} // namespace

void
render_3d_system::on_render_record_draw_cmd (entt::registry &registry)
{
  auto &ctx = registry.ctx ();
  if (!ctx.contains<comp::singl::runtime_context *> ()) {
    return;
  }
  auto &runtime_ctx = *ctx.get<comp::singl::runtime_context *> ();

  auto *renderer = runtime_ctx.try_get_active_scene_renderer ();
  if (renderer == nullptr) {
    return;
  }

  renderer->draw_visible_models ();
  renderer->draw_visible_model_outlines ();

  // If in editor mode, draw the infinite grid and camera gizmos.
  // Skip these for subviewport offscreen passes so that editor decorations
  // do not occlude the subviewport's own contents (e.g. skybox).
  bool const is_subviewport_pass
      = ctx.contains<entt::entity> () && ctx.get<entt::entity> () != entt::null;

  if (!is_subviewport_pass && ctx.contains<comp::singl::editor_context *> ()) {
    auto &editor_ctx = *ctx.get<comp::singl::editor_context *> ();
    if (editor_ctx.grid_visible ()) {
      renderer->draw_grid (editor_ctx.grid_camera_pos (),
                           editor_ctx.grid_fog_center (),
                           editor_ctx.grid_fog_radius ());
    }

    // Editor camera and light gizmos are projected to screen space and
    // queued for the post-tonemap overlay pass below. Depth-tested
    // decorations such as the grid stay in the 3D pass.
    if ((renderer != nullptr)
        && ctx.contains<comp::singl::runtime_context *> ()) {
      auto &runtime_ctx = *ctx.get<comp::singl::runtime_context *> ();
      if (!runtime_ctx.is_running ()) {
        comp::singl::editor_context::resolved_camera rc;
        auto *scene = runtime_ctx.scene_manager ().get_active ();
        bool const have_rc
            = editor_ctx.resolve_game_view_camera (registry, scene, rc);

        // Colors
        glm::vec4 const default_color = glm::vec4 (
            150.0F / 255.0F, 200.0F / 255.0F, 255.0F / 255.0F, 1.0F);
        glm::vec4 const selected_color = glm::vec4 (
            255.0F / 255.0F, 185.0F / 255.0F, 80.0F / 255.0F, 1.0F);

        // Camera and light gizmos are projected to screen space and queued
        // for the renderer's post-tonemap overlay pass, so their colors are
        // not affected by exposure, bloom, or gamma encoding.
        const glm::vec2 target_size = renderer->render_target_size ();
        gfx::overlay_transform gizmo_transform;
        gizmo_transform.ndc_per_pixel
            = glm::vec2 (2.0F / target_size.x, -2.0F / target_size.y);
        gizmo_transform.pixels_per_unit = 1.0F;

        std::vector<gfx::overlay_vertex> overlay;
        gfx::overlay_builder overlay_lines (overlay, gizmo_transform);

        const glm::mat4 &view_proj = renderer->frame_view ().view_proj;
        auto push_segment = [&overlay_lines, &gizmo_transform, &view_proj] (
                                const glm::vec3 &a, const glm::vec3 &b,
                                const glm::vec4 &color) {
          glm::vec2 ndc_a;
          glm::vec2 ndc_b;
          if (project_segment (view_proj, a, b, ndc_a, ndc_b)) {
            overlay_lines.line (gizmo_transform.to_local (ndc_a),
                                gizmo_transform.to_local (ndc_b), 1.0F, color);
          }
        };

        auto view = registry.view<comp::camera, comp::world_transform> ();

        for (entt::entity const entity : view) {
          if (have_rc && !rc.using_engine_default ()
              && entity == rc.entity ()) {
            continue;
          }

          const comp::camera &camera = view.get<comp::camera> (entity);
          const comp::world_transform &wt
              = view.get<comp::world_transform> (entity);
          glm::mat4 const wtm = wt.value ();

          // compute basis
          glm::vec3 const origin = glm::vec3 (wtm[3]);
          glm::vec3 right = glm::normalize (glm::vec3 (wtm[0]));
          if (!std::isfinite (right.x)) {
            right = glm::vec3 (1, 0, 0);
          }
          glm::vec3 up_guess = glm::normalize (glm::vec3 (wtm[1]));
          if (!std::isfinite (up_guess.x)) {
            up_guess = glm::vec3 (0, 1, 0);
          }
          glm::vec3 forward = glm::normalize (-glm::cross (right, up_guess));
          if (!std::isfinite (forward.x)) {
            forward = glm::vec3 (0, 0, -1);
          }
          glm::vec3 const up = glm::normalize (glm::cross (right, forward));
          right = glm::normalize (glm::cross (forward, up));

          float const vertical_fov_deg
              = glm::clamp (camera.fov (), 1.0F, 175.0F);
          float const aspect_ratio = glm::clamp (
              camera.aspect_ratio () > 0.001F
                  ? camera.aspect_ratio ()
                  : (rc.aspect_ratio () > 0.0F ? rc.aspect_ratio () : 1.0F),
              0.1F, 10.0F);
          float const tan_half_fov
              = std::tan (glm::radians (vertical_fov_deg) * 0.5F);

          float const view_distance
              = glm::distance (renderer->camera_position (), origin);
          float const corner_length
              = glm::clamp (view_distance * 0.18F, 0.45F, 5.0F);
          float const shape_term
              = std::sqrt (1.0F
                           + (tan_half_fov * tan_half_fov
                              * (1.0F + (aspect_ratio * aspect_ratio))));
          float const quad_depth = corner_length / shape_term;
          float const quad_half_height = quad_depth * tan_half_fov;
          float const quad_half_width = quad_half_height * aspect_ratio;

          glm::vec3 const quad_center = origin + forward * quad_depth;
          glm::vec3 const quad[4] = {
            quad_center + up * quad_half_height - right * quad_half_width,
            quad_center + up * quad_half_height + right * quad_half_width,
            quad_center - up * quad_half_height + right * quad_half_width,
            quad_center - up * quad_half_height - right * quad_half_width,
          };

          float const origin_extent
              = glm::clamp (corner_length * 0.12F, 0.08F, 0.35F);

          glm::vec4 const col = (entity == editor_ctx.selected_entity ())
                                    ? selected_color
                                    : default_color;

          push_segment (origin - right * origin_extent,
                        origin + right * origin_extent, col);
          push_segment (origin - up * origin_extent,
                        origin + up * origin_extent, col);
          push_segment (origin - forward * origin_extent,
                        origin + forward * origin_extent, col);

          for (int i = 0; i < 4; ++i) {
            push_segment (quad[i], quad[(i + 1) % 4], col);
            push_segment (origin, quad[i], col);
          }
        }

        // Draw directional light gizmos: a '+' at the light origin (like the
        // camera origin point) plus an arrow pointing where the light shines
        // (editor-only). Direction matches the lighting system: -Z of the
        // entity transform.
        glm::vec4 const light_default_color = glm::vec4 (
            255.0F / 255.0F, 230.0F / 255.0F, 120.0F / 255.0F, 1.0F);

        auto light_view
            = registry.view<comp::world_transform, comp::directional_light> ();
        for (entt::entity const entity : light_view) {
          const comp::world_transform &wt
              = light_view.get<comp::world_transform> (entity);
          glm::mat4 const wtm = wt.value ();

          glm::vec3 const origin = glm::vec3 (wtm[3]);

          // Light direction: -Z of the transform (same as lighting_system).
          glm::vec3 dir = -glm::normalize (glm::vec3 (wtm[2]));
          if (!std::isfinite (dir.x)) {
            dir = glm::vec3 (0.0F, 0.0F, -1.0F);
          }

          // Orthonormal basis around the direction for the arrow head.
          glm::vec3 side_guess = glm::normalize (glm::vec3 (wtm[0]));
          if (!std::isfinite (side_guess.x)
              || glm::length (glm::cross (side_guess, dir)) < 1e-4F) {
            side_guess = glm::vec3 (0.0F, 1.0F, 0.0F);
          }
          glm::vec3 const side = glm::normalize (glm::cross (side_guess, dir));
          glm::vec3 const up = glm::normalize (glm::cross (dir, side));

          float const view_distance
              = glm::distance (renderer->camera_position (), origin);
          float const origin_extent
              = glm::clamp (view_distance * 0.12F, 0.08F, 0.35F);
          float const shaft = glm::clamp (view_distance * 0.30F, 0.60F, 4.0F);
          float const head = shaft * 0.22F;

          glm::vec4 const col = (entity == editor_ctx.selected_entity ())
                                    ? selected_color
                                    : light_default_color;

          // '+' at the light origin.
          push_segment (origin - side * origin_extent,
                        origin + side * origin_extent, col);
          push_segment (origin - up * origin_extent,
                        origin + up * origin_extent, col);
          push_segment (origin - dir * origin_extent,
                        origin + dir * origin_extent, col);

          // Arrow along the light direction with a 4-line star head.
          glm::vec3 const tip = origin + dir * shaft;
          glm::vec3 const base = tip - dir * head;
          push_segment (origin, tip, col);
          push_segment (tip, base + side * head, col);
          push_segment (tip, base - side * head, col);
          push_segment (tip, base + up * head, col);
          push_segment (tip, base - up * head, col);
        }

        // View navigation gizmo: built by the editor UI and appended to the
        // same post-tonemap overlay batch.
        if (editor_ctx.view_gizmo_visible ()
            && (editor_ctx.view_gizmo_layout ().scale > 0.0F)) {
          gfx::view_gizmo_camera gizmo_camera;
          gizmo_camera.position = editor_ctx.editor_cam_pos ();
          gizmo_camera.rotation = editor_ctx.editor_cam_rot ();
          editor_ctx.view_gizmo_widget ().build_overlay (
              gizmo_camera, editor_ctx.view_gizmo_layout (), overlay);
        }

        // Transform manipulation gizmo: drawn on top of everything else so
        // its handles stay reachable.
        if (editor_ctx.transform_gizmo_visible ()
            && (editor_ctx.gizmo_mode () != gfx::transform_gizmo_mode::none)) {
          const entt::entity selected = editor_ctx.selected_entity ();
          if ((selected != entt::null)
              && registry.all_of<comp::world_transform> (selected)) {
            const glm::mat4 target
                = registry.get<comp::world_transform> (selected).value ();
            editor_ctx.transform_gizmo_widget ().build_overlay (
                target, editor_ctx.gizmo_mode (),
                renderer->frame_view ().view_proj,
                renderer->frame_view ().world_position,
                renderer->render_target_size (), overlay);
          }
        }

        renderer->queue_debug_overlay (std::move (overlay));
      }
    }
  }
}

} // namespace sys

} // namespace wsl
