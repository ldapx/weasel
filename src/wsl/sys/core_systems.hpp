#pragma once

#include "lighting_system.hpp"
#include "audio_system.hpp"
#include "animation_system.hpp"
#include "physics_system.hpp"
#include "render_2d_system.hpp"
#include "render_3d_system.hpp"
#include "render_ui_system.hpp"
#include "shadow_system.hpp"
#include "skybox_system.hpp"
#include "transform_system.hpp"
#include "sys/stage_registry.hpp"
#include "sys/system_scheduler.hpp"

#include "wsl/event.hpp"
#include "wsl/reg/registry_handle.hpp"
#ifndef IN_MODULE_INTERFACE
#include <entt/entt.hpp>
#endif
#ifndef IN_MODULE_INTERFACE
#include <functional>
#endif
#ifndef IN_MODULE_INTERFACE
#include <memory>
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
namespace comp::singl
{
class runtime_context;
class editor_context;
}
}

namespace wsl
{

namespace sys
{

class core_systems
{
public:
  struct render_callbacks
  {
    std::function<void (entt::registry &)> build_draw_data;
    std::function<void (entt::registry &)> prepare_gpu_rsc;
    std::function<void (entt::registry &)> record_ui_draw_cmd;
    /**
     * Optional UI stage invoked after the scene passes and before the
     * swapchain UI pass, while no render pass is open. Backends that own
     * their SDL_GPU render pass (e.g. the RmlUi SDL_GPU interface) hook in
     * here; in-pass recorders (ImGui) keep using record_ui_draw_cmd.
     */
    std::function<void (entt::registry &)> record_ui_overlay;
  };

  core_systems ();

  static void register_factory_types (comp::singl::runtime_context &rtc);

  void init (comp::singl::runtime_context *runtime_ctx,
             comp::singl::editor_context *editor_ctx);
  void set_editor_ctx (comp::singl::editor_context *editor_ctx);
  void sync_activation ();
  void update (double dt);
  void render (wsl::gfx::render_window &window,
               const render_callbacks &callbacks = {});

  /**
   * Returns the cached list of core systems. The vector is built once in
   * `init()` and never mutated again; iterating it is allocation-free.
   */
  const std::vector<sys::ecs_system *> &to_vec () const;

  std::unique_ptr<render_3d_system> render_3d_sys;
  std::unique_ptr<render_2d_system> render_2d_sys;
  std::unique_ptr<physics_system> physics_sys;

  std::unique_ptr<render_ui_system> render_ui_sys;
  std::unique_ptr<lighting_system> lighting_sys;
  std::unique_ptr<skybox_system> skybox_sys;
  std::unique_ptr<transform_system> transform_sys;
  std::unique_ptr<animation_system> animation_sys;
  std::unique_ptr<shadow_system> shadow_sys;
  std::unique_ptr<audio_system> audio_sys;

private:
  void register_debug_metadata ();
  void ensure_scheduler ();
  void ensure_dummy_context_bindings ();
  void rebuild_system_cache ();
  void render_impl (wsl::gfx::render_window &window,
                    const render_callbacks &callbacks);

  comp::singl::runtime_context *m_runtime_ctx = nullptr;
  comp::singl::editor_context *m_editor_ctx = nullptr;
  entt::registry *m_bound_registry = nullptr;
  entt::registry m_dummy_registry;

  /// Engine-wide ordered stage registry used to resolve WAW ordering.
  stage_registry m_stages;
  /// Parallel dispatcher driving system passes.
  system_scheduler m_scheduler;
  bool m_scheduler_ready = false;

  /**
   * Cached list of non-null core system pointers. Built once in
   * `rebuild_system_cache()` (called from `init()` and whenever the
   * systems change at runtime). Returned by `to_vec()` as a const ref so
   * per-frame callers do not allocate.
   */
  std::vector<sys::ecs_system *> m_cached_systems;

  /**
   * Sorted set of type_ids for the cached core systems. Used by
   * `render_impl` to skip scene-system duplicates via
   * `std::binary_search`. Built alongside `m_cached_systems`.
   */
  std::vector<entt::id_type> m_cached_core_ids;

  /**
   * Project whose window configuration (D7) was last applied to the
   * standalone game window. Standalone builds only; the editor preserves its
   * host window settings.
   */
  const void *m_window_settings_project = nullptr;
};

} // namespace sys

} // namespace wsl
