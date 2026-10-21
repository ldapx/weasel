#include "core_systems.hpp"
#include "comp/singl/ui_manager.hpp"
#include "reg/singleton_registry.hpp"
#include "reg/system_factory_registry.hpp"
#include "rsc/resource_manager.hpp"
#include "sys/audio_system.hpp"
#include "sys/animation_system.hpp"
#include "sys/lighting_system.hpp"
#include "sys/physics_system.hpp"
#include "sys/render_3d_system.hpp"
#include "sys/render_ui_system.hpp"
#include "sys/shadow_system.hpp"
#include "sys/skybox_system.hpp"
#include "sys/system.hpp"
#include "sys/transform_system.hpp"
#include "sys/viewport_renderer.hpp"
#include <algorithm>
#include <entt/entity/fwd.hpp>

#include <tracy/Tracy.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_float3.hpp>
#include <memory>
#include <unordered_map>
#include <vector>

// The editor context is toolkit-neutral state; this header is ImGui-free.
#include "wsl/comp/singl/editor_context.hpp"
#include "../comp/singl/rendering_manager.hpp"
#include "../comp/singl/runtime_context.hpp"
#include "../comp/camera.hpp"
#include "../comp/camera_2d.hpp"
#include "../comp/hierarchy.hpp"
#include "../comp/subviewport.hpp"
#include "../comp/transform.hpp"
#include "../comp/transform_2d.hpp"
#include "../comp/world_transform.hpp"
#include "render_frame.hpp"
#include "wsl/gfx/model_3d.hpp"

#include <utility>

namespace wsl
{

namespace
{

void
apply_rendering_manager (entt::registry &registry,
                         comp::singl::runtime_context &runtime_ctx)
{
  if (!registry.ctx ().contains<comp::singl::rendering_manager> ()) {
    return;
  }

  auto &rendering = registry.ctx ().get<comp::singl::rendering_manager> ();
  auto &renderer = rendering.ensure_renderer (runtime_ctx.window (),
                                              runtime_ctx.render_ctx (),
                                              &runtime_ctx.resource_manager ());
  renderer.ssao_enabled = rendering.ssao_enabled;
  renderer.ssao_radius = rendering.ssao_radius;
  renderer.ssao_bias = rendering.ssao_bias;
  renderer.ssao_power = rendering.ssao_power;
  renderer.ssao_intensity = rendering.ssao_intensity;
  renderer.bloom_threshold = rendering.bloom_threshold;
  renderer.bloom_knee = rendering.bloom_knee;
  renderer.bloom_intensity = rendering.bloom_intensity;
  renderer.outline_color
      = glm::vec4 (static_cast<glm::vec3> (rendering.outline_color),
                   rendering.outline_alpha);
  renderer.outline_width = rendering.outline_width;
  renderer.set_shadow_map_bias (rendering.shadow_bias);
  renderer.set_shadow_map_strength (rendering.shadow_strength);
  renderer.set_ibl_intensity (rendering.ibl_intensity);

  runtime_ctx.window ().scene_clear_color (
      SDL_FColor{ rendering.clear_color.x (), rendering.clear_color.y (),
                  rendering.clear_color.z (), rendering.clear_alpha });
  runtime_ctx.window ().exposure (rendering.exposure);
  runtime_ctx.window ().bloom_intensity (rendering.bloom_intensity);
}

} // namespace

namespace sys
{

core_systems::core_systems () = default;

void
core_systems::register_debug_metadata ()
{
  if (m_runtime_ctx == nullptr) {
    return;
  }

  for (sys::ecs_system *sys : to_vec ()) {
    if (sys == nullptr) {
      continue;
    }

    m_runtime_ctx->event_hub ().clear_system_declarations (sys->get_type_id ());
    sys->register_event_sources (m_runtime_ctx->event_hub ());
    sys->register_event_sinks (m_runtime_ctx->event_hub ());
    sys->register_iterations (m_runtime_ctx->event_hub ());
  }

  if (rsc::scene *scene = m_runtime_ctx->scene_manager ().get_active ()) {
    for (auto &sys : scene->systems) {
      m_runtime_ctx->event_hub ().clear_system_declarations (
          sys->get_type_id ());
      sys->register_event_sources (m_runtime_ctx->event_hub ());
      sys->register_event_sinks (m_runtime_ctx->event_hub ());
      sys->register_iterations (m_runtime_ctx->event_hub ());
    }
  }
}

void
core_systems::register_factory_types (comp::singl::runtime_context &rtc)
{
  auto &factory = rtc.system_factory_registry ();
  using sys_opts = wsl::reg::system_registration_options;
  factory.register_system_type<transform_system> (
      sys_opts{ .display_name = "Transform", .stage = "transform" });
  factory.register_system_type<animation_system> (
      sys_opts{ .display_name = "Animation", .stage = "animation" });
  factory.register_system_type<physics_system> (
      sys_opts{ .display_name = "Physics", .stage = "physics" });
  factory.register_system_type<render_3d_system> (
      sys_opts{ .display_name = "3D Render", .stage = "render_build" });
  factory.register_system_type<render_2d_system> (
      sys_opts{ .display_name = "2D Render", .stage = "render_build" });
  factory.register_system_type<audio_system> (
      sys_opts{ .display_name = "Audio", .stage = "logic" });
  factory.register_system_type<lighting_system> (
      sys_opts{ .display_name = "Lighting", .stage = "render_build" });
  factory.register_system_type<skybox_system> (
      sys_opts{ .display_name = "Skybox", .stage = "render_build" });
  factory.register_system_type<shadow_system> (
      sys_opts{ .display_name = "Shadow", .stage = "render_build" });
  factory.register_system_type<render_ui_system> (
      sys_opts{ .display_name = "UI", .stage = "render_build" });
}

void
core_systems::init (comp::singl::runtime_context *runtime_ctx,
                    comp::singl::editor_context *editor_ctx)
{
  ZoneScopedN ("core_systems::init");

  this->m_runtime_ctx = runtime_ctx;
  this->m_editor_ctx = editor_ctx;

  ensure_dummy_context_bindings ();

  if (!render_3d_sys) {
    render_3d_sys = std::make_unique<render_3d_system> ("3D Render System");
    render_3d_sys->set_stage ("render_build");
  }
  if (!render_2d_sys) {
    render_2d_sys = std::make_unique<render_2d_system> ("2D Render System");
    render_2d_sys->set_stage ("render_build");
  }
  if (!physics_sys) {
    physics_sys = std::make_unique<physics_system> ("Physics System");
    physics_sys->set_stage ("physics");
  }
  if (!render_ui_sys) {
    render_ui_sys
        = std::make_unique<render_ui_system> ("Application UI System");
    render_ui_sys->set_stage ("render_build");
  }
  if (!lighting_sys) {
    lighting_sys = std::make_unique<lighting_system> ("Lighting System");
    lighting_sys->set_stage ("render_build");
  }
  if (!skybox_sys) {
    skybox_sys = std::make_unique<skybox_system> ("Skybox System");
    skybox_sys->set_stage ("render_build");
  }
  if (!transform_sys) {
    transform_sys = std::make_unique<transform_system> ("Transform System");
    transform_sys->set_stage ("transform");
  }
  if (!animation_sys) {
    animation_sys = std::make_unique<animation_system> ("Animation System");
    animation_sys->set_stage ("animation");
  }
  if (!shadow_sys) {
    shadow_sys = std::make_unique<shadow_system> ("Shadow System");
    shadow_sys->set_stage ("render_build");
  }
  if (!audio_sys) {
    audio_sys = std::make_unique<audio_system> ("Audio System");
    audio_sys->set_stage ("logic");
  }

  if (m_runtime_ctx) {
    register_factory_types (*m_runtime_ctx);
  }

  // Build the cached system list + sorted id set now that all unique_ptr
  // members have been constructed. This is the only place the cache needs
  // to be built at runtime — the system membership is fixed after init.
  rebuild_system_cache ();

  // MUST be called AFTER rebuild_system_cache(): register_debug_metadata
  // iterates `to_vec()` to register each system's iterations. If the
  // cached system list is still empty here, every core system's
  // `m_iterations` is left empty and per-frame iterations such as the
  // transform system's `update_world_transforms` never run — which
  // manifests as the world_transform never refreshing and any entity
  // driven by mouse input appearing "frozen".
  register_debug_metadata ();

  sync_activation ();

  wsl::log::sys ()->debug ("Initialized {} built-in systems",
                           to_vec ().size ());
}

void
core_systems::set_editor_ctx (comp::singl::editor_context *editor_ctx)
{
  this->m_editor_ctx = editor_ctx;
  ensure_dummy_context_bindings ();
}

void
core_systems::sync_activation ()
{
  if (m_runtime_ctx == nullptr) {
    return;
  }

  ensure_dummy_context_bindings ();

  rsc::scene *scene = m_runtime_ctx->scene_manager ().get_active ();
  entt::registry *registry
      = (scene != nullptr) ? &scene->get_registry () : &m_dummy_registry;

  if ((m_bound_registry != nullptr) && m_bound_registry != registry) {
    wsl::log::sys ()->trace ("Shutting down systems for old registry");
    for (sys::ecs_system *sys : to_vec ()) {
      if (sys == nullptr) {
        continue;
      }

      sys->shutdown (m_bound_registry);
    }
  }

  m_bound_registry = registry;
  if (registry == nullptr) {
    return;
  }

  for (sys::ecs_system *sys : to_vec ()) {
    if (sys == nullptr) {
      continue;
    }

    sys->refresh_activation (registry, m_runtime_ctx->is_running ());
  }

  if (scene != nullptr) {
    for (auto &sys : scene->systems) {
      sys->refresh_activation (registry, m_runtime_ctx->is_running ());
    }
  }
}

void
core_systems::ensure_scheduler ()
{
  sys::graph_config cfg;
  const auto &s = m_runtime_ctx->parallel_settings ();
  cfg.parallel_systems_enabled = s.parallel_systems_enabled;
  cfg.parallel_render_build_enabled = s.parallel_render_build_enabled;
  cfg.stage_fences_enabled = s.stage_fences_enabled;
  cfg.strict_system_ordering = s.strict_system_ordering;
  cfg.warn_undeclared_cross_tier = s.warn_undeclared_cross_tier;
  cfg.deterministic_system_order = s.deterministic_system_order;

  std::size_t workers = s.max_system_worker_threads == 0
                            ? std::thread::hardware_concurrency ()
                            : s.max_system_worker_threads;
  m_scheduler.configure (cfg, workers);
  m_scheduler_ready = true;
}

void
core_systems::update (double dt)
{
  ZoneScopedN ("core_systems::update");

  sync_activation ();
  m_runtime_ctx->sync ();
  m_runtime_ctx->resource_manager ().update_async_uploads ();

  // Standalone builds apply the project's window configuration once per
  // loaded project (D7/P1). The editor keeps its host window as-is.
  if (m_editor_ctx == nullptr) {
    auto project = m_runtime_ctx->resource_manager ().current_project ();
    if (project && project.get () != m_window_settings_project) {
      m_window_settings_project = project.get ();
      m_runtime_ctx->window ().apply_window_settings (project->window);
    }
  }

  if (m_editor_ctx != nullptr) {
    m_editor_ctx->editor_resources ().update_async_uploads ();
  }

  rsc::scene *scene = m_runtime_ctx->scene_manager ().get_active ();
  entt::registry &registry
      = (scene != nullptr) ? scene->get_registry () : m_dummy_registry;

  ensure_scheduler ();

  // Combined list of systems to drive this frame (core + scene). Each
  // system self-gates on its own active/editor_active flags inside
  // update()/editor_update(), so passing all of them is safe.
  std::vector<sys::ecs_system *> all;
  all.reserve (to_vec ().size ()
               + (scene != nullptr ? scene->systems.size () : 0));
  for (sys::ecs_system *s : to_vec ()) {
    if (s != nullptr) {
      all.push_back (s);
    }
  }
  if (scene != nullptr) {
    for (auto &sp : scene->systems) {
      all.push_back (sp.get ());
    }
  }

  // Deliver events queued by `event_post` before the update pass, matching
  // scene::update so serial and scheduled execution agree: a post during
  // frame N is visible to frame N+1 systems.
  m_runtime_ctx->pump_posted_events ();
  auto run_phase = [&] (sys::frame_phase phase, double step_dt) {
    switch (phase) {
    case sys::frame_phase::fixed: {
      auto run_fixed = [&] (sys::ecs_system *s, entt::registry &r) {
        s->fixed_update (&r, step_dt);
      };
      m_scheduler.run_pass (sys::system_pass::fixed_update, all, registry,
                            m_runtime_ctx->system_factory_registry (), m_stages,
                            run_fixed);
      break;
    }
    case sys::frame_phase::late: {
      auto run_late = [&] (sys::ecs_system *s, entt::registry &r) {
        s->late_update (&r, step_dt);
      };
      m_scheduler.run_pass (sys::system_pass::late_update, all, registry,
                            m_runtime_ctx->system_factory_registry (), m_stages,
                            run_late);
      break;
    }
    case sys::frame_phase::variable:
    default: {
      auto run_update = [&] (sys::ecs_system *s, entt::registry &r) {
        s->update (&r, step_dt);
      };
      m_scheduler.run_pass (sys::system_pass::update, all, registry,
                            m_runtime_ctx->system_factory_registry (), m_stages,
                            run_update);
      break;
    }
    }
  };
  sys::run_frame_phases (dt, m_runtime_ctx->frame_phase_defaults (),
                         m_fixed_accumulator, run_phase,
                         m_runtime_ctx->fixed_step_hook (),
                         m_runtime_ctx->is_paused ());

  auto run_editor = [&] (sys::ecs_system *s, entt::registry &r) {
    s->editor_update (&r, dt);
  };
  m_scheduler.run_pass (sys::system_pass::editor_update, all, registry,
                        m_runtime_ctx->system_factory_registry (), m_stages,
                        run_editor);

  wsl::log::sys ()->trace ("Update: {} systems, dt={}s, scene='{}'",
                           all.size (), dt,
                           scene ? scene->get_name ().c_str () : "(none)");
}

void
core_systems::render (wsl::gfx::render_window &window,
                      const render_callbacks &callbacks)
{
  ZoneScopedN ("core_systems::render");

  render_impl (window, callbacks);
}

void
core_systems::rebuild_system_cache ()
{
  m_cached_systems.clear ();
  m_cached_systems.reserve (10);

  const auto push = [this] (sys::ecs_system *sys) {
    if (sys) {
      m_cached_systems.push_back (sys);
    }
  };

  push (audio_sys.get ());
  push (physics_sys.get ());
  push (transform_sys.get ());
  push (animation_sys.get ());
  push (shadow_sys.get ());
  push (lighting_sys.get ());
  push (skybox_sys.get ());
  push (render_3d_sys.get ());
  push (render_2d_sys.get ());
  push (render_ui_sys.get ());

  m_cached_core_ids.clear ();
  m_cached_core_ids.reserve (m_cached_systems.size ());
  for (sys::ecs_system *sys : m_cached_systems) {
    m_cached_core_ids.push_back (sys->get_type_id ());
  }
  std::sort (m_cached_core_ids.begin (), m_cached_core_ids.end ());
}

const std::vector<sys::ecs_system *> &
core_systems::to_vec () const
{
  return m_cached_systems;
}

void
core_systems::ensure_dummy_context_bindings ()
{
  auto &ctx = m_dummy_registry.ctx ();

  if (m_runtime_ctx != nullptr) {
    if (ctx.contains<comp::singl::runtime_context *> ()) {
      ctx.erase<comp::singl::runtime_context *> ();
    }
    ctx.emplace<comp::singl::runtime_context *> (m_runtime_ctx);
  }

  if (m_editor_ctx != nullptr) {
    if (ctx.contains<comp::singl::editor_context *> ()) {
      ctx.erase<comp::singl::editor_context *> ();
    }
    ctx.emplace<comp::singl::editor_context *> (m_editor_ctx);
  }

  if (m_runtime_ctx != nullptr) {
    if (ctx.contains<rsc::scene_manager *> ()) {
      ctx.erase<rsc::scene_manager *> ();
    }
    ctx.emplace<rsc::scene_manager *> (&m_runtime_ctx->scene_manager ());

    if (ctx.contains<rsc::resource_manager_view *> ()) {
      ctx.erase<rsc::resource_manager_view *> ();
    }
    ctx.emplace<rsc::resource_manager_view *> (
        &m_runtime_ctx->resource_manager_view ());

    if (ctx.contains<comp::singl::ui_manager *> ()) {
      ctx.erase<comp::singl::ui_manager *> ();
    }
    ctx.emplace<comp::singl::ui_manager *> (&m_runtime_ctx->ui_manager ());

    m_runtime_ctx->singleton_registry ().apply_core_singletons (
        m_dummy_registry);
  }
}
void
core_systems::render_impl (wsl::gfx::render_window &window,
                           const render_callbacks &callbacks)
{
  ZoneScoped;
  if (m_runtime_ctx == nullptr) {
    return;
  }

  // Service a deferred play-session stop before any GPU work for this frame is
  // recorded. Tearing the session down here -- rather than from a UI callback
  // deeper inside render_impl -- keeps renderer/pipeline release out of the
  // middle of a frame that is being recorded.
  m_runtime_ctx->process_pending_stop ();

  rsc::scene *scene = nullptr;
  entt::registry *registry_ptr = nullptr;
  {
    ZoneScopedN ("render_impl::setup");
    sync_activation ();

    scene = m_runtime_ctx->scene_manager ().get_active ();
    registry_ptr
        = (scene != nullptr) ? &scene->get_registry () : &m_dummy_registry;
  }
  entt::registry &registry = *registry_ptr;

  {
    ZoneScopedN ("render_impl::build_draw_data");
    ensure_scheduler ();

    std::vector<sys::ecs_system *> all;
    all.reserve (to_vec ().size ()
                 + (scene != nullptr ? scene->systems.size () : 0));
    for (sys::ecs_system *s : to_vec ()) {
      if (s != nullptr) {
        all.push_back (s);
      }
    }
    if (scene != nullptr) {
      for (auto &sp : scene->systems) {
        all.push_back (sp.get ());
      }
    }

    auto run_build = [&] (sys::ecs_system *s, entt::registry &r) {
      s->render_build_draw_data (&r);
    };
    m_scheduler.run_pass (sys::system_pass::render_build, all, registry,
                          m_runtime_ctx->system_factory_registry (), m_stages,
                          run_build);

    if (callbacks.build_draw_data) {
      callbacks.build_draw_data (registry);
    }
  }

  {
    ZoneScopedN ("render_impl::begin_cmd");
    m_runtime_ctx->render_ctx ().begin_cmd ();
  }
  if (!m_runtime_ctx->render_ctx ().has_active_frame ()) {
    return;
  }

  // Wrap the entire frame's GPU work in a "Frame N" debug group so the
  // Event Browser shows the per-frame commands as one nested subtree.
  // Push the group AFTER begin_cmd but before any work so the group
  // is balanced when end_cmd submits.
  if (m_runtime_ctx->render_ctx ().command_buffer () != nullptr) {
    char label[32];
    std::snprintf (
        label, sizeof (label), "Frame %llu",
        (unsigned long long)m_runtime_ctx->render_ctx ().frame_index ());
    SDL_PushGPUDebugGroup (m_runtime_ctx->render_ctx ().command_buffer (),
                           label);
  }

  {
    ZoneScopedN ("render_impl::acquire_swapchain");
    window.new_swapchain ();
  }

  // `core_ids` is the sorted set of type_ids for the built-in core
  // systems. It is built once in `init()` (see `rebuild_system_cache`) and
  // is used below to skip scene-system duplicates that would otherwise
  // double-render. The previous version of this code rebuilt and sorted
  // `core_ids` every frame, which became a measurable hot spot.
  const std::vector<entt::id_type> &core_ids = m_cached_core_ids;

  {
    ZoneScopedN ("render_impl::prepare_gpu_rsc");
    // The ImGui vertex / index buffer uploads (and any future system
    // upload) happen here. Wrap them in a debug group so they show
    // up as a labelled region in the Event Browser rather than as
    // bare copy commands.
    if (m_runtime_ctx->render_ctx ().command_buffer () != nullptr) {
      SDL_PushGPUDebugGroup (m_runtime_ctx->render_ctx ().command_buffer (),
                             "Prepare GPU Resources");
    }
    for (sys::ecs_system *sys : to_vec ()) {
      if (sys == nullptr) {
        continue;
      }

      sys->render_prepare_gpu_rsc (&registry);
    }

    if (callbacks.prepare_gpu_rsc) {
      callbacks.prepare_gpu_rsc (registry);
    }

    if (scene != nullptr) {
      for (auto &sys : scene->systems) {
        if (sys == nullptr) {
          continue;
        }
        if (std::binary_search (core_ids.begin (), core_ids.end (),
                                sys->get_type_id ())) {
          continue;
        }
        sys->render_prepare_gpu_rsc (&registry);
      }
    }
    if (m_runtime_ctx->render_ctx ().command_buffer () != nullptr) {
      SDL_PopGPUDebugGroup (m_runtime_ctx->render_ctx ().command_buffer ());
    }
  }

  gfx::scene_renderer *renderer = nullptr;
  comp::singl::rendering_manager *rendering_mgr = nullptr;

  if (scene != nullptr) {
    {
      ZoneScopedN ("render_impl::apply_rendering_manager");
      apply_rendering_manager (registry, *m_runtime_ctx);
    }
    {
      ZoneScopedN ("render_impl::resolve_scene");
      renderer = &m_runtime_ctx->get_active_scene_renderer ();
      rendering_mgr = m_runtime_ctx->get_active_rendering_manager ();
    }

    if (renderer != nullptr && rendering_mgr != nullptr) {
      ZoneScopedN ("render_impl::render_viewports");

      sys::viewport_render_params params{};
      params.window = &window;
      params.runtime_ctx = m_runtime_ctx;
      params.scene = scene;
      params.registry = &registry;
      params.rendering = rendering_mgr;
      params.renderer = renderer;
      params.shadow_sys = shadow_sys.get ();
      params.lighting_sys = lighting_sys.get ();
      params.skybox_sys = skybox_sys.get ();
      params.render_3d_sys = render_3d_sys.get ();
      params.render_2d_sys = render_2d_sys.get ();
      params.physics_sys = physics_sys.get ();
      params.core_system_ids = &core_ids;

      sys::render_viewports (params);

      {
        // Draw editor overlays that must bypass tone mapping into the
        // final present texture.
        ZoneScopedN ("render_impl::renderer_flush_overlays");
        renderer->flush_debug_overlay ();
      }

      {
        ZoneScopedN ("render_impl::renderer_end_frame");
        renderer->end_frame ();
      }
    }

    registry.ctx ().erase<entt::entity> (); // clear viewport context
  }

  {
    ZoneScopedN ("render_impl::ui_pass");
    if (render_ui_sys) {
      render_ui_sys->render_record_draw_cmd (&registry);
    }

    // Pass-owning UI backends (e.g. RmlUi's SDL_GPU render interface) draw
    // here: the command buffer is open and no render pass is active.
    if (callbacks.record_ui_overlay) {
      callbacks.record_ui_overlay (registry);
    }

    window.begin_ui_pass ();
    if (callbacks.record_ui_draw_cmd) {
      callbacks.record_ui_draw_cmd (registry);
    }
    window.end_ui_pass ();
  }

  {
    ZoneScopedN ("render_impl::end_cmd");
    if (m_runtime_ctx->render_ctx ().has_active_frame ()
        && m_runtime_ctx->render_ctx ().command_buffer () != nullptr) {
      SDL_PopGPUDebugGroup (m_runtime_ctx->render_ctx ().command_buffer ());
    }
    m_runtime_ctx->render_ctx ().end_cmd ();
  }
}

} // namespace sys

} // namespace wsl
