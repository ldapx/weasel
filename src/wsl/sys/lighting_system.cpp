#include "lighting_system.hpp"

#include "../comp/camera.hpp"
#include "../comp/directional_light.hpp"
#include "../comp/point_light.hpp"
#include "../comp/singl/rendering_manager.hpp"
#include "../comp/viewport_environment.hpp"
#include "../comp/singl/runtime_context.hpp"
#include "../comp/spot_light.hpp"
#include "../comp/world_transform.hpp"

#include "../gfx/scene_renderer.hpp"
#include "gfx/lighting.hpp"

#include <cmath>
#include <entt/entity/fwd.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>
#include <vector>

#include <tracy/Tracy.hpp>

namespace wsl
{

namespace sys
{

void
lighting_system::on_render_record_draw_cmd (entt::registry &registry)
{
  ZoneScoped;
  auto &ctx = registry.ctx ();
  if (!ctx.contains<comp::singl::runtime_context *> ()) {
    return;
  }
  auto &runtime_ctx = *ctx.get<comp::singl::runtime_context *> ();

  auto *renderer = runtime_ctx.try_get_active_scene_renderer ();
  if ((renderer == nullptr) || !renderer->has_active_frame ()) {
    return;
  }

  renderer->build_ssao_for_visible_models ();

  lighting_ubo lighting{};
  lighting.counts = glm::ivec4 (0, 0, 0, 0);
  lighting.camera_pos = glm::vec4 (renderer->camera_position (), 0.0F);

  glm::vec3 ambient (0.01F, 0.01F, 0.01F);
  if (ctx.contains<comp::resolved_viewport_environment> ()) {
    // Per-viewport environment (C3): ambient is scoped to the viewport
    // currently being rendered; lights themselves stay global in v1.
    const auto &env = ctx.get<comp::resolved_viewport_environment> ();
    ambient
        = static_cast<glm::vec3> (env.ambient_color) * env.ambient_intensity;
  } else if (ctx.contains<comp::singl::rendering_manager> ()) {
    const auto &rendering = ctx.get<comp::singl::rendering_manager> ();
    ambient = static_cast<glm::vec3> (rendering.ambient_color)
              * rendering.ambient_intensity;
  }
  lighting.ambient = glm::vec4 (ambient, 0.0F);

  for (int i = 0; i < max_shadowed_dir_lights; ++i) {
    lighting.shadowed_dirs[i].light_view_proj = glm::mat4 (1.0F);
    lighting.shadowed_dirs[i].params = glm::vec4 (0.0F);
  }

  for (int i = 0; i < max_shadowed_spot_lights; ++i) {
    lighting.shadowed_spots[i].light_view_proj = glm::mat4 (1.0F);
    lighting.shadowed_spots[i].params = glm::vec4 (0.0F);
  }

  for (int i = 0; i < max_shadowed_point_lights; ++i) {
    lighting.shadowed_points[i].pos_far = glm::vec4 (0.0F);
    lighting.shadowed_points[i].params = glm::vec4 (0.0F);
  }

  // ----- Point lights: build a separate vector and feed the clustered
  // compute pass. The UBO no longer carries point lights.
  std::vector<gpu_point_light> point_lights;
  int next_point_shadow_slot = 0;
  const auto &point_shadows = renderer->point_shadow_maps ();

  auto point_light_view
      = registry.view<comp::world_transform, comp::point_light> ();
  point_lights.reserve (point_light_view.size_hint ());

  for (entt::entity const entity : point_light_view) {
    const comp::world_transform &world
        = point_light_view.get<comp::world_transform> (entity);
    const comp::point_light &light
        = point_light_view.get<comp::point_light> (entity);

    glm::mat4 const wm = world.value ();
    const glm::vec3 position = glm::vec3 (wm[3]);

    gpu_point_light gpu_light{};
    gpu_light.pos_radius = glm::vec4 (position, light.radius);
    gpu_light.color_intensity
        = glm::vec4 (static_cast<glm::vec3> (light.color), light.intensity);
    gpu_light.shadow_info = glm::ivec4 (-1, 0, 0, 0);

    if (light.cast_shadows
        && next_point_shadow_slot < max_shadowed_point_lights) {
      gpu_light.shadow_info.x = next_point_shadow_slot;
      gpu_light.shadow_info.y = 1;

      const auto &shadow = point_shadows[next_point_shadow_slot];
      lighting.shadowed_points[next_point_shadow_slot].pos_far
          = glm::vec4 (shadow.light_pos, shadow.far_plane);
      lighting.shadowed_points[next_point_shadow_slot].params = glm::vec4 (
          shadow.bias, shadow.strength, shadow.enabled ? 1.0F : 0.0F, 0.0F);

      ++next_point_shadow_slot;
    }

    point_lights.push_back (gpu_light);
  }

  auto dir_light_view
      = registry.view<comp::world_transform, comp::directional_light> ();
  for (entt::entity const entity : dir_light_view) {
    if (lighting.counts.x >= max_dir_lights) {
      break;
    }

    const comp::world_transform &world
        = dir_light_view.get<comp::world_transform> (entity);
    const comp::directional_light &light
        = dir_light_view.get<comp::directional_light> (entity);

    gpu_dir_light &gpu_light = lighting.dirs[lighting.counts.x++];
    glm::mat4 const wm = world.value ();
    const glm::vec3 direction = -glm::normalize (glm::vec3 (wm[2]));

    gpu_light.dir_pad = glm::vec4 (direction, 0.0F);
    gpu_light.color_intensity
        = glm::vec4 (static_cast<glm::vec3> (light.color), light.intensity);
  }

  auto spot_light_view
      = registry.view<comp::world_transform, comp::spot_light> ();
  for (entt::entity const entity : spot_light_view) {
    if (lighting.counts.y >= max_spot_lights) {
      break;
    }

    const comp::world_transform &world
        = spot_light_view.get<comp::world_transform> (entity);
    const comp::spot_light &light
        = spot_light_view.get<comp::spot_light> (entity);

    gpu_spot_light &gpu_light = lighting.spots[lighting.counts.y++];
    glm::mat4 const wm = world.value ();
    const glm::vec3 position = glm::vec3 (wm[3]);
    const glm::vec3 direction = -glm::normalize (glm::vec3 (wm[2]));

    gpu_light.pos_pad = glm::vec4 (position, 0.0F);
    gpu_light.dir_pad = glm::vec4 (direction, 0.0F);
    gpu_light.color_intensity
        = glm::vec4 (static_cast<glm::vec3> (light.color), light.intensity);
    gpu_light.angles_pad
        = glm::vec4 (light.inner_cos, light.outer_cos, 0.0F, 0.0F);
  }

  int shadow_dir_index = 0;
  for (entt::entity const entity : dir_light_view) {
    if (shadow_dir_index >= max_shadowed_dir_lights) {
      break;
    }

    const comp::directional_light &light
        = dir_light_view.get<comp::directional_light> (entity);
    if (!light.cast_shadows) {
      continue;
    }

    gpu_shadowed_dir &gpu_shadow = lighting.shadowed_dirs[shadow_dir_index++];
    gpu_shadow.light_view_proj = renderer->directional_shadow_view ();
    gpu_shadow.params = glm::vec4 (renderer->shadow_map_bias (),
                                   renderer->shadow_map_strength (),
                                   renderer->shadow_map_resolution (), 1.0F);
  }

  int shadow_spot_index = 0;
  const auto &spot_shadows = renderer->spot_shadow_maps ();
  for (entt::entity const entity : spot_light_view) {
    if (shadow_spot_index >= max_shadowed_spot_lights) {
      break;
    }

    const comp::spot_light &light
        = spot_light_view.get<comp::spot_light> (entity);
    if (!light.cast_shadows) {
      continue;
    }

    const auto &shadow = spot_shadows[shadow_spot_index];
    gpu_shadowed_spot &gpu_shadow = lighting.shadowed_spots[shadow_spot_index];
    gpu_shadow.light_view_proj = shadow.light_vp;
    gpu_shadow.params
        = glm::vec4 (shadow.bias, shadow.strength, shadow.map_size,
                     shadow.enabled ? 1.0F : 0.0F);

    ++shadow_spot_index;
  }

  // Resolve camera near/far for the cluster pass. Prefer the active
  // camera entity; fall back to safe defaults otherwise.
  float z_near = 0.1F;
  float z_far = 100.0F;
  const auto &view = renderer->frame_view ();
  if (view.valid) {
    // Recover near/far from the perspective projection matrix:
    //   m[2][2] = f / (n - f)
    //   m[3][2] = (n * f) / (n - f)
    const glm::mat4 &p = view.proj;
    if (std::abs (p[2][2] + 1.0F) > 1e-5F) {
      z_far = p[3][2] / (p[2][2] + 1.0F);
      z_near = (p[3][2] * z_far) / (p[3][2] - z_far * (p[2][2] + 1.0F));
    }
  }

  renderer->run_clustered_lighting (point_lights, view.view, z_near, z_far);
  renderer->upload_lighting (lighting);
}

} // namespace sys

} // namespace wsl
