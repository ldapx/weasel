#include "runtime_context.hpp"

#include "../../rsc/resource_manager.hpp"
#include "../../rsc/scene.hpp"
#include "../../rsc/scene_snapshot_serializer.hpp"
#include "../../reg/registry_queries.hpp"
#include "../../sys/core_systems.hpp"
#include "../../event/event_hub.hpp"
#include "../../event/message_bus.hpp"
#include "comp/component_meta.hpp"
#include "comp/singl/physics_manager.hpp"
#include "comp/singl/rendering_manager.hpp"
#include "ui_manager.hpp"
#include "editor_context.hpp"
#include "events.hpp"
#include "gfx/scene_renderer.hpp"
#include "phys/physics_engine.hpp"
#include "rsc/resource_ids.hpp"
#include "sys/system.hpp"
#include "wsl/log/log.hpp"

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_init.h>
#include <cassert>
#include <entt/core/fwd.hpp>
#include <entt/core/hashed_string.hpp>
#include <entt/core/type_info.hpp>
#include <entt/entity/entity.hpp>
#include <entt/entity/fwd.hpp>
#include <entt/meta/factory.hpp>
#include <memory>
#include <ranges>
#include <utility>

namespace wsl
{

namespace
{

rsc::scene_id
find_scene_id_for_instance (comp::singl::runtime_context &runtime_ctx,
                            rsc::scene *scene)
{
  if (scene == nullptr) {
    return rsc::scene_id{ entt::null };
  }

  for (const auto &scene_info :
       runtime_ctx.resource_manager ().list_scenes ()) {
    const rsc::scene_id candidate{ scene_info.id };
    if (runtime_ctx.resource_manager ().find_loaded_scene (candidate)
        == scene) {
      return candidate;
    }
  }

  return rsc::scene_id{ entt::null };
}

bool
scene_belongs_to_world (const comp::singl::runtime_context &runtime_ctx,
                        const rsc::scene *scene)
{
  if (scene == nullptr) {
    return false;
  }

  return std::ranges::any_of (
      runtime_ctx.world ().get_scenes (),
      [scene] (const auto &candidate) { return candidate.get () == scene; });
}

} // namespace

void
comp::singl::runtime_context::register_meta ()
{
  using namespace entt::literals;

  entt::meta_factory<comp::singl::runtime_context> ()
      .type (entt::type_hash<comp::singl::runtime_context>::value ())
      .custom<comp::meta_info> (comp::meta_info{
          "Runtime Context", "Global state for the running game application.",
          "" });
}

comp::singl::runtime_context::sdl_init_guard::sdl_init_guard (bool headless)
    : is_headless (headless)
{
  if (headless) {
    wsl::log::core ()->trace ("Headless mode, skipping SDL initialization");
    return;
  }

  if (!SDL_Init (SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO)) {
    wsl::log::core ()->critical ("Failed to initialize SDL: {}",
                                 SDL_GetError ());
  } else {
    wsl::log::core ()->trace ("SDL initialized successfully");
  }
}

comp::singl::runtime_context::sdl_init_guard::~sdl_init_guard ()
{
  if (!is_headless)
    SDL_Quit ();
}

comp::singl::runtime_context::runtime_context (
    const char *name, int width, int height, const std::string &engine_res_path,
    bool headless)
    : sdl_init_guard_ (headless), m_world (this), m_scene_manager (m_world),
      m_headless (headless), m_runtime_project_module (this)
{
  // Allocate heavy subsystems via unique_ptr in dependency order.
  m_component_registry = std::make_unique<reg::component_registry> ();
  m_singleton_registry = std::make_unique<reg::singleton_registry> ();
  m_system_factory_registry = std::make_unique<reg::system_factory_registry> ();
  m_event_db = std::make_unique<event::event_debug_db> ();
  m_event_hub = std::make_unique<event::event_hub> (*m_event_db);
  m_message_bus = std::make_unique<event::message_bus> ();

  m_reg_queries = std::make_unique<reg::registry_queries> (
      *m_component_registry, *m_system_factory_registry, *m_event_hub);

  m_render_ctx = std::make_unique<gfx::render_context> (headless);
  m_resource_manager
      = std::make_unique<rsc::resource_manager> (this, engine_res_path);
  m_resource_manager_view = std::make_unique<rsc::resource_manager_view> (
      m_resource_manager.get ());
  m_window = std::make_unique<gfx::render_window> (
      name, width, height, m_render_ctx.get (), m_resource_manager.get (),
      headless);
  m_ui_manager = std::make_unique<comp::singl::ui_manager> (
      *m_render_ctx, *m_window, m_resource_manager.get ());

  m_system_factory_registry->set_event_hub (m_event_hub.get ());
  if (!headless)
    wsl::log::core ()->trace ("GPU device status: {}",
                              (void *)m_render_ctx->gpu_device);
  m_current_input_map = &m_app_input_map;

  m_event_hub->resolve_active_registry = [this] () -> entt::registry * {
    auto *scene = m_scene_manager.get_active ();
    return scene ? &scene->get_registry () : nullptr;
  };

  m_event_hub->resolve_system_by_type
      = [this] (entt::id_type system_type_id) -> sys::ecs_system * {
    if (auto *scene = m_scene_manager.get_active ()) {
      for (sys::ecs_system *system : scene->get_systems ()) {
        if (system && system->get_type_id () == system_type_id) {
          return system;
        }
      }
    }

    if (m_core_systems) {
      for (sys::ecs_system *system : m_core_systems->to_vec ()) {
        if (system && system->get_type_id () == system_type_id) {
          return system;
        }
      }
    }

    return nullptr;
  };

  // Fold the editor-wide `scene_changed` event onto the observer event hub
  // (single eventing mechanism). Declare the source (emitted by
  // `scene_manager`) and a sink owned directly by `runtime_context`; the
  // handler is invoked via a captured `void *` owner, so no `ecs_system`
  // inheritance is required.
  m_event_hub
      ->declare_event_source<wsl::event::scene_changed, rsc::scene_manager> ();
  m_event_hub->declare_event_sink<wsl::event::scene_changed, runtime_context> (
      "on_scene_changed",
      +[] (void *owner, entt::registry &, const void *ev) {
        static_cast<runtime_context *> (owner)->on_scene_changed (
            *static_cast<const wsl::event::scene_changed *> (ev));
      },
      this);
  m_event_hub->connect (comp::stable_type_id<wsl::event::scene_changed> (),
                        comp::stable_type_id<runtime_context> (),
                        "on_scene_changed");

  // Register core system factories so CLI can discover them via `sys avail`,
  // even in headless mode.  The actual system instances are only created
  // when a full rendering context is available.
  sys::core_systems::register_factory_types (*this);

  if (!headless) {
    m_core_systems = std::make_unique<sys::core_systems> ();
    m_core_systems->init (this, nullptr);
  }

  wsl::log::core ()->debug (
      "Runtime context initialized (window={}x{}, engine_res_path='{}')", width,
      height, engine_res_path);
}

comp::singl::runtime_context::~runtime_context ()
{
  wsl::log::core ()->debug ("Shutting down runtime context");
  m_runtime_project_module.unload ();
  m_scene_manager.set_active (nullptr);
  // The main resource manager depends on the runtime world, core systems, and
  // GPU device still being alive. Shut it down explicitly before member
  // destruction starts.
  m_resource_manager->shutdown ();
  m_world.clear ();
}

void
comp::singl::runtime_context::set_editor_ctx (
    comp::singl::editor_context *editor_ctx)
{
  m_editor_ctx = editor_ctx;
  if (editor_ctx != nullptr) {
    m_current_input_map = &editor_ctx->editor_input_map ();
  }
  m_world.set_editor_context (editor_ctx);
  m_resource_manager->set_editor_context (editor_ctx);
  if (m_core_systems != nullptr) {
    m_core_systems->set_editor_ctx (editor_ctx);
  }
}

void
comp::singl::runtime_context::save_scene_state (rsc::scene *scene)
{
  if (!m_in_play_session || (scene == nullptr)) {
    return;
  }

  const rsc::scene_id sid = find_scene_id_for_instance (*this, scene);
  if (sid.value == entt::null || m_scene_save_states.contains (sid.value)) {
    return;
  }

  rsc::io::scene_snapshot_serializer const serializer (this, *scene);
  std::string snapshot;
  serializer.save_to_binary_string (snapshot);
  m_scene_save_states[sid.value] = std::move (snapshot);
}

void
comp::singl::runtime_context::save_active_scene_state ()
{
  save_scene_state (m_scene_manager.get_active ());
}

void
comp::singl::runtime_context::sync ()
{
  if (m_needs_save_active_scene) {
    save_active_scene_state ();
    m_needs_save_active_scene = false;
  }
}

void
comp::singl::runtime_context::on_scene_changed (
    const wsl::event::scene_changed &event)
{
  if (!m_in_play_session) {
    return;
  }

  // Save both sides of the transition once so stop() can restore every scene
  // touched during the current play session.
  save_scene_state (event.old_scene);
  save_scene_state (event.new_scene);
  m_needs_save_active_scene = false;
}

void
comp::singl::runtime_context::set_running (bool value)
{
  if (m_is_running == value) {
    return;
  }

  // If starting play for the first time in a session, save state
  if (value && !m_in_play_session) {
    m_in_play_session = true;
    save_active_scene_state ();
    if (auto *scene = m_scene_manager.get_active ()) {
      m_play_session_origin_scene = scene;
      m_play_session_origin_scene_id
          = find_scene_id_for_instance (*this, scene);
    }
  }

  m_is_running = value;

  wsl::log::core ()->debug ("Runtime {}",
                            value ? "started (play)" : "stopped (pause)");

  if (auto *scene = m_scene_manager.get_active ()) {
    if (m_is_running) {
      scene->resume ();
    } else {
      scene->pause ();
    }
  }

  if (m_core_systems) {
    m_core_systems->sync_activation ();
  }
}

void
comp::singl::runtime_context::stop ()
{
  if (!m_in_play_session) {
    return;
  }

  // Pause everything first
  set_running (false);
  m_in_play_session = false;

  // Ensure GPU is idle before we start destroying renderers and restoring
  // states. This prevents VRAM exhaustion from deferred releases during rapid
  // play/stop cycles.

  if (m_render_ctx->gpu_device != nullptr) {
    SDL_WaitForGPUIdle (m_render_ctx->gpu_device);
  }

  // Restore ALL scenes state
  for (auto &[sid_val, snapshot] : m_scene_save_states) {
    const rsc::scene_id sid{ sid_val };
    rsc::scene *scene = m_resource_manager->find_loaded_scene (sid);
    if (scene != nullptr) {
      rsc::io::scene_snapshot_serializer serializer (this, *scene);
      serializer.load_from_binary_string (snapshot);
    }
  }

  // Then restore the original active scene
  bool restored_origin_scene = false;
  if (scene_belongs_to_world (*this, m_play_session_origin_scene)) {
    m_scene_manager.set_active (m_play_session_origin_scene);
    restored_origin_scene = true;
  }

  if (!restored_origin_scene
      && m_play_session_origin_scene_id.value != entt::null) {
    if (m_resource_manager->activate_scene (m_play_session_origin_scene_id)) {
      restored_origin_scene = true;
    } else if (rsc::scene *loaded_scene
               = m_resource_manager->find_loaded_scene (
                   m_play_session_origin_scene_id)) {
      m_scene_manager.set_active (loaded_scene);
      restored_origin_scene = true;
    }
  }

  // After restoring the scene, its camera entity may have changed (or been
  // recreated with a new identifier). The rendering manager's render_viewport
  // is a viewport entity (subviewport or root), not a camera, so we leave it
  // as-is. The scene's camera is already restored by scene_manager::set_active.
  if (rsc::scene *active_scene = m_scene_manager.get_active ()) {
    auto &reg = active_scene->get_registry ();
    auto &ctx = reg.ctx ();
    if (ctx.contains<comp::singl::rendering_manager> ()) {
      // render_viewport is a viewport entity, not a camera.
      // Default to root viewport (entt::null) after stopping.
      ctx.get<comp::singl::rendering_manager> ().render_viewport = entt::null;
    }
  }

  m_scene_save_states.clear ();
  m_play_session_origin_scene_id = rsc::scene_id{ entt::null };
  m_play_session_origin_scene = nullptr;

  wsl::log::core ()->debug ("Play session stopped");
}

void
comp::singl::runtime_context::request_stop ()
{
  m_stop_requested = true;
}

void
comp::singl::runtime_context::process_pending_stop ()
{
  if (!m_stop_requested) {
    return;
  }
  // Clear first: stop() can be re-entered indirectly (scene restoration may
  // trigger further requests), and we do not want to loop on one request.
  m_stop_requested = false;
  stop ();
}

comp::singl::rendering_manager *
comp::singl::runtime_context::get_active_rendering_manager () const
{
  auto *scene = m_scene_manager.get_active ();
  if (scene == nullptr) {
    return nullptr;
  }

  auto &ctx = scene->get_registry ().ctx ();
  if (!ctx.contains<comp::singl::rendering_manager> ()) {
    return nullptr;
  }

  return &ctx.get<comp::singl::rendering_manager> ();
}

gfx::scene_renderer *
comp::singl::runtime_context::try_get_active_scene_renderer ()
{
  if (auto *rendering = get_active_rendering_manager ()) {
    return rendering->try_renderer ();
  }

  return nullptr;
}

gfx::scene_renderer &
comp::singl::runtime_context::get_active_scene_renderer ()
{
  comp::singl::rendering_manager *rendering = get_active_rendering_manager ();
  assert (rendering && "Active scene is missing its rendering manager.");
  return rendering->ensure_renderer (*m_window, *m_render_ctx,
                                     m_resource_manager.get ());
}

comp::singl::physics_manager *
comp::singl::runtime_context::get_active_physics_manager () const
{
  auto *scene = m_scene_manager.get_active ();
  if (scene == nullptr) {
    return nullptr;
  }

  auto &ctx = scene->get_registry ().ctx ();
  if (!ctx.contains<comp::singl::physics_manager> ()) {
    return nullptr;
  }

  return &ctx.get<comp::singl::physics_manager> ();
}

phys::engine *
comp::singl::runtime_context::try_get_active_physics_engine ()
{
  if (auto *physics = get_active_physics_manager ()) {
    return physics->try_engine ();
  }

  return nullptr;
}

phys::engine &
comp::singl::runtime_context::get_active_physics_engine ()
{
  comp::singl::physics_manager *physics = get_active_physics_manager ();
  assert (physics && "Active scene is missing its physics manager.");
  return physics->ensure_engine ();
}

} // namespace wsl
