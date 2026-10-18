#pragma once

#include "render_frame.hpp"

#ifndef IN_MODULE_INTERFACE
#include <entt/entt.hpp>
#endif
#ifndef IN_MODULE_INTERFACE
#include <vector>
#endif

namespace wsl
{

namespace gfx
{
class render_window;
}

namespace rsc
{
class scene;
}

namespace comp::singl
{
class runtime_context;
struct rendering_manager;
}

namespace sys
{

class shadow_system;
class lighting_system;
class skybox_system;
class render_3d_system;
class render_2d_system;
class physics_system;

/** Everything the viewport renderer needs from the frame pipeline. */
struct viewport_render_params
{
  wsl::gfx::render_window *window = nullptr;
  comp::singl::runtime_context *runtime_ctx = nullptr;
  rsc::scene *scene = nullptr;
  entt::registry *registry = nullptr;
  comp::singl::rendering_manager *rendering = nullptr;
  gfx::scene_renderer *renderer = nullptr;

  shadow_system *shadow_sys = nullptr;
  lighting_system *lighting_sys = nullptr;
  skybox_system *skybox_sys = nullptr;
  render_3d_system *render_3d_sys = nullptr;
  render_2d_system *render_2d_sys = nullptr;
  physics_system *physics_sys = nullptr;

  /** Sorted core-system ids (duplicate suppression for scene systems). */
  const std::vector<entt::id_type> *core_system_ids = nullptr;
};

/**
 * Renders the scene's viewport tree.
 *
 * Recursive, post-order (D3/N1): every viewport renders its subtree into its
 * own render target first, then composites into the nearest ancestor target
 * (2D overlay sorted by `display_order`, or a 3D world-space quad). Only the
 * presentation chain's root presents to the window. Runs the single,
 * frame-wide shadow & lighting pass (v1 shares lights/shadows globally) and
 * exactly one root post-process per frame.
 */
void render_viewports (const viewport_render_params &params);

} // namespace sys
} // namespace wsl
