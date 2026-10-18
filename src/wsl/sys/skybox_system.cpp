#include "skybox_system.hpp"

#include "../comp/singl/runtime_context.hpp"
#include "../comp/singl/rendering_manager.hpp"
#include "../comp/viewport_environment.hpp"
#include "rsc/resource_manager.hpp"
#include <entt/entity/fwd.hpp>

namespace wsl
{

namespace sys
{

void
skybox_system::on_render_record_draw_cmd (entt::registry &registry)
{
  auto &ctx = registry.ctx ();
  if (!ctx.contains<comp::singl::runtime_context *> ()) {
    return;
  }
  auto &runtime_ctx = *ctx.get<comp::singl::runtime_context *> ();

  auto *rendering = runtime_ctx.get_active_rendering_manager ();
  if (rendering == nullptr) {
    return;
  }

  // Per-viewport environment (C3): the skybox is scoped to the viewport
  // currently being rendered; falls back to the scene environment.
  comp::resolved_viewport_environment const *env = nullptr;
  if (ctx.contains<comp::resolved_viewport_environment> ()) {
    env = &ctx.get<comp::resolved_viewport_environment> ();
  }

  rsc::cubemap_id const skybox
      = (env != nullptr) ? env->skybox : rendering->skybox;
  math::quatf const rotation
      = (env != nullptr) ? env->skybox_rotation : rendering->skybox_rotation;

  auto *renderer = rendering->try_renderer ();
  if (renderer != nullptr) {
    // If procedural skybox is active, ensure it is baked with current sun
    // direction
    if (skybox.value == rsc::builtin_skybox_procedural) {
      auto handle = runtime_ctx.resource_manager ().get (skybox);
      if (handle) {
        renderer->bake_procedural_skybox (*handle,
                                          rendering->get_sun_direction ());
      }
    }

    renderer->draw_active_environment (rotation);
  }
}

} // namespace sys

} // namespace wsl
