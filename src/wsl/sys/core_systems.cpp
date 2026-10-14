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
#include <algorithm>
#include <entt/entity/fwd.hpp>

#include <tracy/Tracy.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_float3.hpp>
#include <memory>
#include <unordered_map>
#include <vector>

#ifdef WEASEL_BUILD_EDITOR
#include "wsl/comp/singl/editor_context.hpp"
#endif
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

  auto run_update
      = [&] (sys::ecs_system *s, entt::registry &r) { s->update (&r, dt); };
  m_scheduler.run_pass (sys::system_pass::update, all, registry,
                        m_runtime_ctx->system_factory_registry (), m_stages,
                        run_update);

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
  sys::render_submission submission{};
  entt::entity main_viewport = entt::null;
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
      main_viewport = (rendering_mgr != nullptr)
                          ? rendering_mgr->render_viewport
                          : entt::null;

      // Defensive: if render_viewport points to a non-subviewport entity
      // (e.g. stale code writing to the old main_camera field), treat as
      // root.
      if (main_viewport != entt::null
          && !registry.all_of<comp::subviewport> (main_viewport)) {
        main_viewport = entt::null;
      }

#ifdef WEASEL_BUILD_EDITOR
      if (m_editor_ctx != nullptr) {
        if (!m_runtime_ctx->is_running ()) {
          main_viewport = m_editor_ctx->game_view_selected_viewport ();
        }
      }
#endif
    }

    {
      ZoneScopedN ("render_impl::build_render_frame");
      if (sys::build_render_frame (registry, *m_runtime_ctx, submission,
                                   main_viewport)) {

        {
          ZoneScopedN ("render_impl::set_environment");
          renderer->set_environment (submission.environment);
        }

        // Shadow and lighting passes (global)
        {
          ZoneScopedN ("render_impl::global_shadow_and_light");
          if (m_runtime_ctx->render_ctx ().command_buffer () != nullptr) {
            SDL_PushGPUDebugGroup (
                m_runtime_ctx->render_ctx ().command_buffer (),
                "Shadows & Lighting");
          }
          renderer->begin_frame (submission.view);
          if (shadow_sys) {
            shadow_sys->render_record_draw_cmd (&registry);
          }
          if (lighting_sys) {
            lighting_sys->render_record_draw_cmd (&registry);
          }
          if (m_runtime_ctx->render_ctx ().command_buffer () != nullptr) {
            SDL_PopGPUDebugGroup (
                m_runtime_ctx->render_ctx ().command_buffer ());
          }
        }

        // Collect child viewports that belong to the main viewport
        std::vector<entt::entity> child_vps;
        {
          auto sv_view = registry.view<comp::subviewport> ();
          for (auto const e : sv_view) {
            if (e == main_viewport) {
              continue;
            }
            if (comp::find_parent_viewport (registry, e) == main_viewport) {
              child_vps.push_back (e);
            }
          }
        }

        // Ensure offscreen targets exist and are up-to-date for child viewports
        if (rendering_mgr != nullptr) {
          for (auto const e : child_vps) {
            auto *sv = registry.try_get<comp::subviewport> (e);
            if (sv == nullptr) {
              continue;
            }

            uint32_t vw, vh;
            bool const is_3d_quad
                = registry.all_of<comp::transform, comp::world_transform> (e);
            if (is_3d_quad && sv->world_quad_size.y () > 0.0F) {
              // 3D quad: render-target aspect ratio must match world_quad_size
              // so the offscreen texture is not stretched when mapped onto
              // the quad.  Resolution magnitude is taken from virtual_size.
              float const world_aspect
                  = sv->world_quad_size.x () / sv->world_quad_size.y ();
              float const max_virtual
                  = std::max (sv->virtual_size.x (), sv->virtual_size.y ());
              if (world_aspect >= 1.0F) {
                vw = static_cast<uint32_t> (max_virtual);
                vh = static_cast<uint32_t> (max_virtual / world_aspect);
              } else {
                vh = static_cast<uint32_t> (max_virtual);
                vw = static_cast<uint32_t> (max_virtual * world_aspect);
              }
            } else {
              // 2D overlay or render_2d_only: use virtual_size directly.
              vw = static_cast<uint32_t> (sv->virtual_size.x ());
              vh = static_cast<uint32_t> (sv->virtual_size.y ());
            }

            auto it = rendering_mgr->subviewport_targets.find (e);
            if (it == rendering_mgr->subviewport_targets.end ()
                || it->second.width != vw || it->second.height != vh) {
              if (it != rendering_mgr->subviewport_targets.end ()) {
                gfx::subviewport_target::destroy (it->second);
              }
              rendering_mgr->subviewport_targets[e]
                  = gfx::subviewport_target::create (
                      &window, &m_runtime_ctx->render_ctx (), vw, vh);
            }
          }
          // Clean up targets for destroyed subviewports
          for (auto it = rendering_mgr->subviewport_targets.begin ();
               it != rendering_mgr->subviewport_targets.end ();) {
            if (!registry.valid (it->first)
                || !registry.all_of<comp::subviewport> (it->first)) {
              gfx::subviewport_target::destroy (it->second);
              it = rendering_mgr->subviewport_targets.erase (it);
            } else {
              ++it;
            }
          }
        }

        // Helper to render a viewport (3D + 2D)
        auto render_viewport_full = [&] (entt::entity vp_entity,
                                         const sys::render_submission &sub,
                                         bool apply_vp_rect = true,
                                         gfx::subviewport_target *offscreen
                                         = nullptr) {
          ZoneScopedN ("render_viewport_full");

          registry.ctx ().insert_or_assign (vp_entity);

          bool render_2d_only = false;
          if (vp_entity != entt::null) {
            if (auto *sv = registry.try_get<comp::subviewport> (vp_entity)) {
              render_2d_only = sv->render_2d_only;
            }
          }

          if (offscreen != nullptr) {
            window.begin_subviewport_pass (*offscreen, true, true);
          } else {
            window.begin_3d_pass (true, true);
          }

          {
            ZoneScopedN ("render_viewport_full::apply_viewport");
            if (apply_vp_rect && vp_entity != entt::null
                && offscreen == nullptr) {
              if (auto *sv = registry.try_get<comp::subviewport> (vp_entity)) {
                gfx::viewport vp{};
                vp.x = sv->x;
                vp.y = sv->y;
                vp.width = sv->width;
                vp.height = sv->height;
                vp.clear_color = sv->clear_color;
                vp.clear_depth = sv->clear_depth;
                vp.clear_color_value.r = sv->clear_r;
                vp.clear_color_value.g = sv->clear_g;
                vp.clear_color_value.b = sv->clear_b;
                vp.clear_color_value.a = sv->clear_a;
                window.apply_viewport (vp);
              }
            } else if (!apply_vp_rect && offscreen == nullptr) {
              window.apply_viewport ({});
            }
          }

          {
            ZoneScopedN ("render_viewport_full::begin_frame");
            renderer->set_visible_draws (sub.draw_commands);
            renderer->begin_frame (sub.view);
          }

          if (!sub.is_2d_view && !render_2d_only) {
            if (skybox_sys) {
              ZoneScopedN ("render_viewport_full::skybox");
              skybox_sys->render_record_draw_cmd (&registry);
            }
            if (render_3d_sys) {
              ZoneScopedN ("render_viewport_full::render_3d");
              render_3d_sys->render_record_draw_cmd (&registry);
            }
            if (physics_sys) {
              ZoneScopedN ("render_viewport_full::physics");
              physics_sys->render_record_draw_cmd (&registry);
            }
          }

          if (scene != nullptr && !render_2d_only) {
            ZoneScopedN ("render_viewport_full::scene_systems");
            for (auto &sys : scene->systems) {
              if (sys == nullptr)
                continue;
              if (std::binary_search (core_ids.begin (), core_ids.end (),
                                      sys->get_type_id ()))
                continue;
              sys->render_record_draw_cmd (&registry);
            }
          }

          // Draw subviewport 3D quads inside the main 3D pass before
          // ending it, so they participate in depth testing.
          if (offscreen == nullptr && !child_vps.empty ()
              && rendering_mgr != nullptr) {
            for (auto const e : child_vps) {
              if (!registry.all_of<comp::transform, comp::world_transform> (
                      e)) {
                continue;
              }
              auto it = rendering_mgr->subviewport_targets.find (e);
              if (it == rendering_mgr->subviewport_targets.end ()) {
                continue;
              }
              auto *sv = registry.try_get<comp::subviewport> (e);
              if (sv == nullptr) {
                continue;
              }

              if (!rendering_mgr->subviewport_quad_model) {
                rendering_mgr->subviewport_quad_model
                    = gfx::model_3d::make_unit_quad ();
              }
              auto model = rendering_mgr->subviewport_quad_model;
              if (!model || model->meshes.empty ()
                  || model->meshes[0].primitives.empty ()) {
                continue;
              }

              const auto &wt = registry.get<comp::world_transform> (e);
              glm::mat4 m = wt.value ();
              m = glm::scale (m, glm::vec3 (sv->world_quad_size.x (),
                                            sv->world_quad_size.y (), 1.0F));

              auto &prim = model->meshes[0].primitives[0];
              SDL_GPUTexture *prev_tex = prim.mat.base_color_tex;
              SDL_GPUSampler *prev_samp = prim.mat.sampler;
              prim.mat.base_color_tex = it->second.color_resolve.get ();
              prim.mat.sampler = window.linear_sampler ().get ();
              prim.mat.device = nullptr;

              renderer->draw_model (*model, 0, m, sub.view.view_proj);

              prim.mat.base_color_tex = prev_tex;
              prim.mat.sampler = prev_samp;
              prim.mat.device = window.ctx ()->gpu_device;
            }
          }

          if (offscreen != nullptr) {
            window.end_subviewport_pass ();
          } else {
            window.end_3d_pass ();
          }

          // 2D Pass
          if (render_2d_sys) {
            ZoneScopedN ("render_viewport_full::render_2d");
            auto &r2d_ref = m_runtime_ctx->get_active_rendering_manager ()
                                ->ensure_renderer_2d (
                                    window, m_runtime_ctx->render_ctx (),
                                    &m_runtime_ctx->resource_manager ());
            auto *r2d = &r2d_ref;

            {
              ZoneScopedN ("render_viewport_full::2d_setup");
              if (sub.is_2d_view) {
                r2d->set_projection (sub.view.proj * sub.view.view);
              }

              render_2d_sys->render_record_draw_cmd (&registry);
              r2d->build_and_upload ();
            }

            {
              ZoneScopedN ("render_viewport_full::2d_begin_pass");
              if (offscreen != nullptr) {
                window.begin_subviewport_pass (*offscreen, false, false,
                                               "2D Sprite Pass");
              } else {
                window.begin_3d_pass (false, false, "2D Sprite Pass");
              }
            }

            {
              ZoneScopedN ("render_viewport_full::2d_apply_viewport");
              if (apply_vp_rect && vp_entity != entt::null
                  && offscreen == nullptr) {
                if (auto *sv
                    = registry.try_get<comp::subviewport> (vp_entity)) {
                  gfx::viewport vp{};
                  vp.x = sv->x;
                  vp.y = sv->y;
                  vp.width = sv->width;
                  vp.height = sv->height;
                  window.apply_viewport (vp);
                }
              } else if (offscreen == nullptr) {
                window.apply_viewport ({}); // Fullscreen
              }
            }

            {
              ZoneScopedN ("render_viewport_full::2d_draw");
              r2d->draw ();
            }
            {
              ZoneScopedN ("render_viewport_full::2d_end_pass");
              if (offscreen != nullptr) {
                window.end_subviewport_pass ();
              } else {
                window.end_3d_pass (true);
              }
            }
          }
        };

        // 1. Render child viewports offscreen first so the main viewport
        //    can sample them as quads / overlays.
        for (auto const e : child_vps) {
          sys::render_submission child_sub{};
          if (sys::build_render_frame (registry, *m_runtime_ctx, child_sub,
                                       e)) {
            gfx::subviewport_target *target = nullptr;
            if (rendering_mgr != nullptr) {
              auto it = rendering_mgr->subviewport_targets.find (e);
              if (it != rendering_mgr->subviewport_targets.end ()) {
                target = &it->second;
              }
            }
            render_viewport_full (e, child_sub, true, target);
          }
        }

        // 2. Render main viewport
        {
          ZoneScopedN ("render_impl::main_viewport");
          bool const preview_selected_viewport
              = (m_editor_ctx != nullptr) && !m_runtime_ctx->is_running ()
                && m_editor_ctx->game_view_selected_viewport () != entt::null;

          render_viewport_full (main_viewport, submission,
                                !preview_selected_viewport, nullptr);
        }

        // 3. Draw subviewport 2D overlays inside the main viewport
        if (!child_vps.empty () && rendering_mgr != nullptr) {
          auto &r2d_ref = rendering_mgr->ensure_renderer_2d (
              window, m_runtime_ctx->render_ctx (),
              &m_runtime_ctx->resource_manager ());

          // -- 2D Overlays (drawn after the main 2D pass) --
          // We need an extra 2D pass because the main 2D pass already
          // uploaded and drew its batches.
          bool has_2d_overlays = false;
          for (auto const e : child_vps) {
            if (registry.all_of<comp::transform_2d> (e)) {
              has_2d_overlays = true;
              break;
            }
          }
          if (has_2d_overlays) {
            // Build overlay sprite queue
            for (auto const e : child_vps) {
              if (!registry.all_of<comp::transform_2d> (e)) {
                continue;
              }
              auto it = rendering_mgr->subviewport_targets.find (e);
              if (it == rendering_mgr->subviewport_targets.end ()) {
                continue;
              }
              auto *sv = registry.try_get<comp::subviewport> (e);
              if (sv == nullptr) {
                continue;
              }

              gfx::batch_renderer_2d::draw_command cmd{};
              // Use the offscreen resolve texture directly.
              cmd.texture_override = it->second.color_resolve.get ();
              cmd.position = glm::vec2 (sv->container_position.x (),
                                        sv->container_position.y ());
              cmd.size = glm::vec2 (sv->container_size.x (),
                                    sv->container_size.y ());
              cmd.color = glm::vec4 (1.0F);
              cmd.z_index = 1000; // on top of most sprites
              r2d_ref.submit (cmd);
            }

            r2d_ref.build_and_upload ();
            window.begin_3d_pass (false, false, "Subviewport 2D Overlay Pass");
            window.apply_viewport ({});
            r2d_ref.draw ();
            window.end_3d_pass (true);
          }
        }

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
        registry.ctx ().erase<entt::entity> (); // clear viewport context
      }
    }
  }

  {
    ZoneScopedN ("render_impl::ui_pass");
    if (render_ui_sys) {
      render_ui_sys->render_record_draw_cmd (&registry);
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
