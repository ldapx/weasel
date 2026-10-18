#pragma once

#include "../math/vector.hpp"
#include "../rsc/resource_ids.hpp"
#include "viewport_settings.hpp"

#ifndef IN_MODULE_INTERFACE
#include <entt/entt.hpp>
#endif

namespace wsl::comp
{

/**
 * Per-viewport environment with every `use_global` fallback already
 * applied against the scene environment (`comp::singl::rendering_manager`).
 *
 * Stored in the registry context while a viewport is being rendered so
 * systems shared by all viewports (skybox, lighting) can scope their
 * per-viewport contributions without duplicating the global state.
 */
struct resolved_viewport_environment
{
  rsc::cubemap_id skybox{};
  math::quatf skybox_rotation{ 0.0F, 0.0F, 0.0F, 1.0F };

  math::vec3f ambient_color{ 1.0F, 1.0F, 1.0F };
  float ambient_intensity = 0.01F;

  float ibl_intensity = 1.0F;
  float exposure = 1.0F;

  bool bloom_enabled = true;
  float bloom_threshold = 1.0F;
  float bloom_knee = 0.5F;
  float bloom_intensity = 1.0F;

  bool ssao_enabled = true;
  float ssao_radius = 0.6F;
  float ssao_bias = 0.025F;
  float ssao_power = 1.25F;
  float ssao_intensity = 1.0F;

  bool clear_color_enabled = true;
  math::vec4f clear_color{ 0.0F, 0.0F, 0.0F, 1.0F };
  bool clear_depth = true;

  /** When false the subtree is excluded from shadow casting (S5). */
  bool shadow_caster = true;
};

/**
 * Resolves a viewport's environment.
 *
 * :param viewport: SubViewport entity, or entt::null for the root viewport.
 *                  The root viewport always uses the scene environment.
 */
resolved_viewport_environment
resolve_viewport_environment (entt::registry &registry, entt::entity viewport);

} // namespace wsl::comp
