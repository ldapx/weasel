#pragma once

#include "../../rsc/world.hpp"
#include "../../rsc/scene_manager.hpp"
#include "../../reg/runtime_project_module.hpp"
#include "../../events.hpp"
#include "../../input.hpp"
#include "../../sys/frame_phases.hpp"

#ifndef IN_MODULE_INTERFACE
#include <entt/entt.hpp>
#endif
#ifndef IN_MODULE_INTERFACE
#include <functional>
#endif
#ifndef IN_MODULE_INTERFACE
#include <cstdint>
#endif
#ifndef IN_MODULE_INTERFACE
#include <string>
#endif
#ifndef IN_MODULE_INTERFACE
#include <unordered_map>
#endif
#ifndef IN_MODULE_INTERFACE
#include <memory>
#endif
#ifndef IN_MODULE_INTERFACE
#include <mutex>
#endif
#ifndef IN_MODULE_INTERFACE
#include <vector>
#endif

// Forward declarations for heavy subsystems stored by unique_ptr.
// Full definitions are only needed in runtime_context.cpp.
namespace wsl
{
namespace rsc
{
class resource_manager;
struct resource_manager_view;
} // namespace rsc
namespace reg
{
class component_registry;
class singleton_registry;
class system_factory_registry;
class registry_queries;
} // namespace reg
namespace event
{
struct event_debug_db;
struct event_hub;
class message_bus;
} // namespace event
namespace gfx
{
class render_context;
class render_window;
class scene_renderer;
} // namespace gfx
namespace sys
{
class core_systems;
} // namespace sys
namespace phys
{
class engine;
} // namespace phys
namespace comp::singl
{
class ui_manager;
struct rendering_manager;
struct physics_manager;
} // namespace comp::singl
} // namespace wsl

namespace wsl
{

namespace comp::singl
{

/** Tunable parallel-systems execution policy. */
struct system_parallelism_settings
{
  bool parallel_systems_enabled = true;
  bool parallel_render_build_enabled = true;
  std::size_t max_system_worker_threads = 0; // 0 = hardware concurrency
  bool stage_fences_enabled = false;
  bool strict_system_ordering = false;
  bool warn_undeclared_cross_tier = true;
  bool deterministic_system_order = false;
};

/**
 * Core shared state for a Weasel runtime instance.
 *
 * This singleton aggregates all major subsystems (resource manager, scene
 * manager, world, dispatcher) and provides a central point of access for
 * runtime logic.
 */
class runtime_context : public comp::singleton_component
{
public:
  /**
   * Constructs the runtime context.
   * :param name: Window title.
   * :param width: Window width.
   * :param height: Window height.
   * :param engine_res_path: Base path for engine resources.
   */
  explicit runtime_context (const char *name, int width, int height,
                            const std::string &engine_res_path,
                            bool headless = false);

  ~runtime_context ();

  runtime_context (const runtime_context &) = delete;
  runtime_context &operator= (const runtime_context &) = delete;
  runtime_context (runtime_context &&) = delete;
  runtime_context &operator= (runtime_context &&) = delete;

  /** Register reflection metadata for this class. */
  static void register_meta ();

  /** Mutable parallel-systems execution policy. */
  system_parallelism_settings &
  parallel_settings ()
  {
    return m_parallel_settings;
  }

  const system_parallelism_settings &
  parallel_settings () const
  {
    return m_parallel_settings;
  }

  /** Returns the active rendering manager from the current scene. */
  rendering_manager *get_active_rendering_manager () const;

  /** Attempts to get the active scene renderer, may return nullptr. */
  gfx::scene_renderer *try_get_active_scene_renderer ();

  /** Gets the active scene renderer, ensuring it exists. */
  gfx::scene_renderer &get_active_scene_renderer ();

  /** Returns the active physics manager from the current scene. */
  physics_manager *get_active_physics_manager () const;

  /** Attempts to get the active physics engine, may return nullptr. */
  phys::engine *try_get_active_physics_engine ();

  /** Gets the active physics engine, ensuring it exists. */
  phys::engine &get_active_physics_engine ();

  /** Sets the simulation running state. */
  void set_running (bool value);

  /** Stops the current play session and restores scene states. */
  void stop ();

  /**
   * Requests a play-session stop that takes effect at the next frame boundary.
   *
   * Prefer this over calling stop() from a UI/render callback. `stop()` tears
   * the session's scene and renderer down, which releases GPU pipelines into
   * SDL's pending-destroy queue. Calling it from inside `render_impl` means
   * that destruction is then performed as a side effect of the very next
   * `begin_frame` fence wait -- while the driver may still have work in flight
   * referencing those pipelines. On the RADON driver that path blocks
   * indefinitely inside `vkDestroyGraphicsPipeline`.
   *
   * Deferring to a frame boundary keeps the teardown out of the middle of a
   * frame being recorded.
   */
  void request_stop ();

  /**
   * Performs a stop requested by request_stop(), if one is pending.
   *
   * Called at the top of a frame, before any GPU work is recorded.
   */
  void process_pending_stop ();

  /** Synchronizes deferred state changes. */
  void sync ();

  /** Saves the state of a specific scene for later restoration. */
  void save_scene_state (rsc::scene *scene);

  /** Saves the state of the currently active scene. */
  void save_active_scene_state ();

  /** Callback for scene change events. */
  void on_scene_changed (const wsl::event::scene_changed &event);

  /** Assigns an editor context for tool-specific behaviors. */
  void set_editor_ctx (class editor_context *editor_ctx);

  /** Returns the application input map. */
  wsl::input::action_map &
  get_app_input_map ()
  {
    return m_app_input_map;
  }

  /** Returns the application input map. */
  const wsl::input::action_map &
  get_app_input_map () const
  {
    return m_app_input_map;
  }

  /** Returns current input map (may be null). */
  wsl::input::action_map *
  get_current_input_map () const
  {
    return m_current_input_map;
  }

  /** Returns whether this context was created in headless mode. */
  bool
  is_headless () const
  {
    return m_headless;
  }

  rsc::world const &
  world () const
  {
    return m_world;
  }
  rsc::world &
  world ()
  {
    return m_world;
  }
  rsc::scene_manager const &
  scene_manager () const
  {
    return m_scene_manager;
  }
  rsc::scene_manager &
  scene_manager ()
  {
    return m_scene_manager;
  }
  reg::component_registry const &
  component_registry () const
  {
    return *m_component_registry;
  }
  reg::component_registry &
  component_registry ()
  {
    return *m_component_registry;
  }
  reg::singleton_registry const &
  singleton_registry () const
  {
    return *m_singleton_registry;
  }
  reg::singleton_registry &
  singleton_registry ()
  {
    return *m_singleton_registry;
  }
  reg::system_factory_registry const &
  system_factory_registry () const
  {
    return *m_system_factory_registry;
  }
  reg::system_factory_registry &
  system_factory_registry ()
  {
    return *m_system_factory_registry;
  }
  event::event_debug_db const &
  event_db () const
  {
    return *m_event_db;
  }
  event::event_debug_db &
  event_db ()
  {
    return *m_event_db;
  }
  event::event_hub const &
  event_hub () const
  {
    return *m_event_hub;
  }
  event::event_hub &
  event_hub ()
  {
    return *m_event_hub;
  }
  event::message_bus const &
  message_bus () const
  {
    return *m_message_bus;
  }
  event::message_bus &
  message_bus ()
  {
    return *m_message_bus;
  }
  /**
   * Gameplay pause state, owned by the engine (not scripts). While paused,
   * script `delta` reads zero, the frame-phase driver skips fixed steps,
   * and physics stops stepping; variable/late passes (UI, menus) keep
   * running. Defaults to unpaused.
   */
  bool
  is_paused () const
  {
    return m_paused;
  }
  void
  set_paused (bool paused)
  {
    m_paused = paused;
  }
  /**
   * Gameplay time scale applied to script `delta` (`unscaled_delta` stays
   * raw). Negative values clamp to zero. Defaults to 1.
   */
  double
  time_scale () const
  {
    return m_time_scale;
  }
  void
  set_time_scale (double scale)
  {
    m_time_scale = (scale >= 0.0) ? scale : 0.0;
  }
  /**
   * Hook invoked once per completed fixed simulation step by the
   * frame-phase driver (after that step's systems ran). Engine-owned clock
   * state; subscribers (e.g. the Daslang fixed-step clock) must be cheap
   * and thread-compatible with the system pass. Empty by default.
   */
  using fixed_step_hook_fn = std::function<void (double fixed_dt)>;
  void
  set_fixed_step_hook (fixed_step_hook_fn hook)
  {
    m_fixed_step_hook = std::move (hook);
  }
  const fixed_step_hook_fn &
  fixed_step_hook () const
  {
    return m_fixed_step_hook;
  }
  /**
   * One queued event posted for deferred delivery. Bytes are an opaque
   * copy of a validated payload (script-defined or engine-owned);
   * `layout_hash` re-validates at dispatch exactly like `emit`.
   */
  struct posted_event
  {
    entt::id_type type = 0;
    std::string bytes;
    std::size_t size = 0;
    std::uint64_t layout_hash = 0;
  };
  /** Upper bound on queued events; posts beyond it are dropped with a warn. */
  static constexpr std::size_t max_posted_events = 256;
  /**
   * Queues an event for delivery at the next pump (start of the next
   * systems update). Thread-safe. Returns false (dropping the event) when
   * the queue is full or the payload is empty.
   */
  bool post_event (entt::id_type type_id, const void *data, std::size_t size,
                   std::uint64_t layout_hash);
  /**
   * Delivers queued events in FIFO posting order through the event hub,
   * then clears the queue. Runs only while a scene is active; otherwise
   * the queue is left intact (cap-bound). Events posted during a pump
   * roll to the next pump. Called by both system-driving paths
   * (`scene::update` and `core_systems::update`) before the update pass,
   * so serial and scheduled execution agree.
   */
  void pump_posted_events ();
  /**
   * Fixed-step policy for the frame-phase driver, derived from the active
   * physics engine when one exists (same step, clamp, and substep cap the
   * engine simulates with), otherwise engine-wide defaults.
   */
  sys::frame_phase_config frame_phase_defaults () const;
  reg::registry_queries const &
  reg_queries () const
  {
    return *m_reg_queries;
  }
  reg::registry_queries &
  reg_queries ()
  {
    return *m_reg_queries;
  }
  reg::runtime::runtime_project_module const &
  runtime_project_module () const
  {
    return m_runtime_project_module;
  }
  reg::runtime::runtime_project_module &
  runtime_project_module ()
  {
    return m_runtime_project_module;
  }

  gfx::render_context const &
  render_ctx () const
  {
    return *m_render_ctx;
  }
  gfx::render_context &
  render_ctx ()
  {
    return *m_render_ctx;
  }
  rsc::resource_manager const &
  resource_manager () const
  {
    return *m_resource_manager;
  }
  rsc::resource_manager &
  resource_manager ()
  {
    return *m_resource_manager;
  }
  rsc::resource_manager_view const &
  resource_manager_view () const
  {
    return *m_resource_manager_view;
  }
  rsc::resource_manager_view &
  resource_manager_view ()
  {
    return *m_resource_manager_view;
  }
  gfx::render_window const &
  window () const
  {
    return *m_window;
  }
  gfx::render_window &
  window ()
  {
    return *m_window;
  }
  comp::singl::ui_manager const &
  ui_manager () const
  {
    return *m_ui_manager;
  }
  comp::singl::ui_manager &
  ui_manager ()
  {
    return *m_ui_manager;
  }

  std::unique_ptr<sys::core_systems> const &
  core_systems () const
  {
    return m_core_systems;
  }
  std::unique_ptr<sys::core_systems> &
  core_systems ()
  {
    return m_core_systems;
  }

  bool
  is_running () const
  {
    return m_is_running;
  }
  bool
  in_play_session () const
  {
    return m_in_play_session;
  }
  void
  set_in_play_session (bool v)
  {
    m_in_play_session = v;
  }

  class editor_context *
  editor_ctx () const
  {
    return m_editor_ctx;
  }

  std::unordered_map<entt::id_type, std::string> const &
  scene_save_states () const
  {
    return m_scene_save_states;
  }
  std::unordered_map<entt::id_type, std::string> &
  scene_save_states ()
  {
    return m_scene_save_states;
  }

private:
  struct sdl_init_guard
  {
    sdl_init_guard (bool headless = false);
    ~sdl_init_guard ();

    bool is_headless = false;
  };
  sdl_init_guard sdl_init_guard_;

  // By-value (lightweight or needed by other by-value members)
  rsc::world m_world;
  rsc::scene_manager m_scene_manager;
  bool m_headless = false;
  fixed_step_hook_fn m_fixed_step_hook;
  bool m_paused = false;
  double m_time_scale = 1.0;
  mutable std::mutex m_posted_events_mutex;
  std::vector<posted_event> m_posted_events;

  // By-pointer (heavy, only needed in .cpp)
  std::unique_ptr<reg::component_registry> m_component_registry;
  std::unique_ptr<reg::singleton_registry> m_singleton_registry;
  std::unique_ptr<reg::system_factory_registry> m_system_factory_registry;

  // Must be declared after the registries: its destructor calls
  // clear_runtime_registries() which accesses the registries above.
  // C++ destroys members in reverse declaration order, so this must
  // be destroyed before the unique_ptrs it references.
  reg::runtime::runtime_project_module m_runtime_project_module;
  std::unique_ptr<event::event_debug_db> m_event_db;
  std::unique_ptr<event::event_hub> m_event_hub;
  std::unique_ptr<event::message_bus> m_message_bus;
  std::unique_ptr<gfx::render_context> m_render_ctx;
  std::unique_ptr<rsc::resource_manager> m_resource_manager;
  std::unique_ptr<rsc::resource_manager_view> m_resource_manager_view;
  std::unique_ptr<reg::registry_queries> m_reg_queries;
  std::unique_ptr<gfx::render_window> m_window;
  std::unique_ptr<comp::singl::ui_manager> m_ui_manager;

  // System core (already unique_ptr)
  std::unique_ptr<sys::core_systems> m_core_systems;

  // Simple state
  bool m_is_running = false;
  bool m_in_play_session = false;
  // Set by request_stop(), consumed by process_pending_stop() at a frame
  // boundary. See request_stop() for why the teardown must not run inside
  // render_impl.
  bool m_stop_requested = false;
  class editor_context *m_editor_ctx = nullptr;
  std::unordered_map<entt::id_type, std::string> m_scene_save_states;

  wsl::input::action_map m_app_input_map;
  wsl::input::action_map *m_current_input_map = nullptr;

  rsc::scene *m_play_session_origin_scene = nullptr;
  rsc::scene_id m_play_session_origin_scene_id{ entt::null };

  bool m_needs_save_active_scene = false;

  system_parallelism_settings m_parallel_settings;
};

} // namespace comp::singl

} // namespace wsl
