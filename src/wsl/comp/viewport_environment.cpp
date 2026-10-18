#include "viewport_environment.hpp"

#include "singl/rendering_manager.hpp"
#include "subviewport.hpp"

namespace wsl::comp
{

resolved_viewport_environment
resolve_viewport_environment (entt::registry &registry, entt::entity viewport)
{
  resolved_viewport_environment out;

  comp::singl::rendering_manager const *scene_env = nullptr;
  auto &ctx = registry.ctx ();
  if (ctx.contains<comp::singl::rendering_manager> ()) {
    scene_env = &ctx.get<comp::singl::rendering_manager> ();
  }

  auto const *sv = (viewport != entt::null)
                       ? registry.try_get<comp::subviewport> (viewport)
                       : nullptr;

  // Root viewport (or stale entity): scene environment everywhere.
  if (sv == nullptr || scene_env == nullptr) {
    if (scene_env != nullptr) {
      out.skybox = scene_env->skybox;
      out.skybox_rotation = scene_env->skybox_rotation;
      out.ambient_color = scene_env->ambient_color;
      out.ambient_intensity = scene_env->ambient_intensity;
      out.ibl_intensity = scene_env->ibl_intensity;
      out.exposure = scene_env->exposure;
      out.bloom_threshold = scene_env->bloom_threshold;
      out.bloom_knee = scene_env->bloom_knee;
      out.bloom_intensity = scene_env->bloom_intensity;
      out.ssao_enabled = scene_env->ssao_enabled;
      out.ssao_radius = scene_env->ssao_radius;
      out.ssao_bias = scene_env->ssao_bias;
      out.ssao_power = scene_env->ssao_power;
      out.ssao_intensity = scene_env->ssao_intensity;
      out.clear_color
          = math::vec4f{ scene_env->clear_color.x (),
                         scene_env->clear_color.y (),
                         scene_env->clear_color.z (), scene_env->clear_alpha };
    }
    out.bloom_enabled = true;
    return out;
  }

  // Sub viewport: apply every `use_global` fallback.
  const viewport_environment &env = sv->settings.env;

  out.skybox = env.skybox_use_global ? scene_env->skybox : env.skybox;
  out.skybox_rotation = env.skybox_rotation_use_global
                            ? scene_env->skybox_rotation
                            : env.skybox_rotation;

  out.ambient_color
      = env.ambient_use_global ? scene_env->ambient_color : env.ambient_color;
  out.ambient_intensity = env.ambient_use_global ? scene_env->ambient_intensity
                                                 : env.ambient_intensity;

  out.ibl_intensity
      = env.ibl_use_global ? scene_env->ibl_intensity : env.ibl_intensity;
  out.exposure = env.exposure_use_global ? scene_env->exposure : env.exposure;

  out.bloom_enabled = env.bloom_enabled;
  out.bloom_threshold
      = env.bloom_use_global ? scene_env->bloom_threshold : env.bloom_threshold;
  out.bloom_knee
      = env.bloom_use_global ? scene_env->bloom_knee : env.bloom_knee;
  out.bloom_intensity
      = env.bloom_use_global ? scene_env->bloom_intensity : env.bloom_intensity;

  out.ssao_enabled
      = env.ssao_use_global ? scene_env->ssao_enabled : env.ssao_enabled;
  out.ssao_radius
      = env.ssao_use_global ? scene_env->ssao_radius : env.ssao_radius;
  out.ssao_bias = env.ssao_use_global ? scene_env->ssao_bias : env.ssao_bias;
  out.ssao_power = env.ssao_use_global ? scene_env->ssao_power : env.ssao_power;
  out.ssao_intensity
      = env.ssao_use_global ? scene_env->ssao_intensity : env.ssao_intensity;

  out.clear_color_enabled = env.clear_color_enabled;
  if (env.clear_color_use_global) {
    out.clear_color
        = math::vec4f{ scene_env->clear_color.x (), scene_env->clear_color.y (),
                       scene_env->clear_color.z (), scene_env->clear_alpha };
  } else {
    out.clear_color = env.clear_color;
  }
  out.clear_depth = env.clear_depth;

  out.shadow_caster = sv->settings.shadow_caster;

  return out;
}

} // namespace wsl::comp
