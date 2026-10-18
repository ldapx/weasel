#pragma once

#include "../comp/subviewport.hpp"
#include "../comp/viewport_settings.hpp"
#include "../gfx/cubemap.hpp"
#include "../gfx/scene_renderer.hpp"
#include "../gfx/viewport_resolve.hpp"
#include "../rsc/resource_ids.hpp"

#ifndef IN_MODULE_INTERFACE
#include <entt/entt.hpp>
#endif
#ifndef IN_MODULE_INTERFACE
#include <deque>
#endif

namespace wsl
{

namespace comp::singl
{
class runtime_context;
}

namespace sys
{

struct render_submission
{
  gfx::scene_renderer::view_state view{};
  std::vector<gfx::scene_renderer::draw_command> draw_commands;
  const gfx::cubemap *environment = nullptr;

  /**
   * Backing storage for `draw_command::palette`.
   *
   * The draw commands are copied into the renderer and consumed later in the
   * frame, so the palettes they point at must live at least as long as this
   * submission -- they cannot be a local of `build_render_frame`. A deque is
   * used because references to existing elements stay valid as it grows;
   * `draw_view.size_hint()` is the size of the *smallest* pool in the view and
   * is not a bound on how many entities match, so a vector would reallocate
   * mid-loop and dangle every pointer already handed out.
   */
  std::deque<gfx::scene_renderer::joint_palette> palettes;

  /**
   * When true, this submission is for a 2D camera/viewport and 3D objects
   * (models, skybox, physics debug) should not be rendered.
   */
  bool is_2d_view = false;

  void reset ();
};

/**
 * Identifies the viewport a render submission is built for.
 *
 * `viewport == entt::null` is the scene root viewport. The resolved viewport
 * supplies the camera aspect ratio and the 2D orthographic extents; the
 * mode is component-driven (D3/S2).
 */
struct render_view_target
{
  entt::entity viewport = entt::null;
  comp::subviewport_mode mode = comp::subviewport_mode::mode_3d;
  gfx::resolved_viewport resolved{};
  /** Resolved per-viewport environment skybox (D4). */
  rsc::cubemap_id skybox{};
};

/**
 * Builds the draw submission for one viewport.
 *
 * Returns false when the viewport has no valid camera: the caller must then
 * clear its render target with the configured clear colour/depth and skip
 * all draw passes (S2).
 */
bool build_render_frame (entt::registry &registry,
                         comp::singl::runtime_context &runtime_ctx,
                         render_submission &out,
                         const render_view_target &target);

} // namespace sys

} // namespace wsl
