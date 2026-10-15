#include "physics_system.hpp"

#include "../comp/area3d.hpp"
#include "../comp/character_body.hpp"
#include "../comp/hierarchy.hpp"
#include "../comp/rigid_body.hpp"
#include "event/event_hub.hpp"
#include "../comp/transform.hpp"
#include "../comp/world_transform.hpp"

#include "../comp/singl/physics_manager.hpp"
#include "../comp/singl/runtime_context.hpp"
#include "../phys/physics_engine.hpp"
#include "sys/system.hpp"
#include <cstddef>
#include <entt/entity/entity.hpp>
#include <entt/entity/fwd.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/quaternion_common.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/fwd.hpp>
#include <glm/matrix.hpp>
#include <string>

#include "../debug/physics_debug_drawer.hpp"
#include "../comp/singl/editor_context.hpp"
#include "../debug/debug_renderer.hpp"

#include <glm/gtc/quaternion.hpp>

#include <unordered_map>

namespace wsl
{

namespace sys
{

physics_system::physics_system (const std::string &name) : ecs_system_t (name)
{
  set_relationships ({ "Transform System" });
}

physics_system::~physics_system () {}

void
physics_system::set_local_from_world (entt::registry &reg, entt::entity e,
                                      const glm::vec3 &world_pos,
                                      const glm::quat &world_rot)
{
  comp::transform &t = reg.get<comp::transform> (e);

  if (auto *h = reg.try_get<comp::hierarchy> (e);
      (h != nullptr) && h->parent != entt::null
      && reg.all_of<comp::world_transform> (h->parent)) {

    glm::mat4 const parent_wt
        = reg.get<comp::world_transform> (h->parent).value ();

    glm::mat4 const inv_parent = glm::inverse (parent_wt);

    glm::vec4 const lp = inv_parent * glm::vec4 (world_pos, 1.0F);
    t.position = math::vec3f{ lp.x, lp.y, lp.z };

    glm::quat const parent_rot = glm::quat_cast (parent_wt);
    t.rotation = math::quatf (glm::inverse (parent_rot) * world_rot);
  } else {
    t.position = math::vec3f{ world_pos.x, world_pos.y, world_pos.z };
    t.rotation
        = math::quatf{ world_rot.x, world_rot.y, world_rot.z, world_rot.w };
  }

  // CRITICAL: Update world_transform immediately so other systems (like
  // rendering) see the new position in this same frame.
  if (auto *wt = reg.try_get<comp::world_transform> (e)) {
    wt->value () = glm::translate (glm::mat4 (1.0F), world_pos)
                   * glm::mat4_cast (world_rot)
                   * glm::scale (glm::mat4 (1.0F), (glm::vec3)t.scale);
  }
}

comp::singl::physics_manager *
physics_system::get_registry_physics_manager (entt::registry &registry)
{
  auto &ctx = registry.ctx ();
  if (!ctx.contains<comp::singl::physics_manager> ()) {
    return nullptr;
  }

  return &ctx.get<comp::singl::physics_manager> ();
}

void
physics_system::register_event_sources (event::event_hub &hub)
{
  // these signal types may still live in component namespaces,
  // but ownership is now explicitly the physics system.
  hub.declare_event_source<comp::area::entered, physics_system> ();
  hub.declare_event_source<comp::area::exited, physics_system> ();
}

void
physics_system::register_event_sinks (event::event_hub &hub)
{
  // add declarations here later if you connect dispatcher sinks for this
  // system event::declare_handler<physics_system>(hub,
  // "on_some_event");
  (void)hub;
}

void
physics_system::register_iterations (event::event_hub &hub)
{
  clear_registered_iterations ();

  register_iteration<comp::character_body> (
      hub, "update_character_controllers",
      [this] (entt::registry &registry, double dt) {
        update_character_controllers (registry, dt);
      });

  register_iteration<comp::transform, comp::rigid_body> (
      hub, "sync_transforms_to_rigid_bodies",
      [this] (entt::registry &registry, double dt) {
        sync_transforms_to_rigid_bodies (registry, dt);
      });

  register_iteration<> (hub, "step_world",
                        [this] (entt::registry &registry, double dt) {
                          step_world (registry, dt);
                        });

  register_iteration<comp::area> (hub, "dispatch_sensor_overlap_events",
                                  [this] (entt::registry &registry, double dt) {
                                    dispatch_sensor_overlap_events (registry,
                                                                    dt);
                                  });

  register_iteration<comp::transform, comp::rigid_body> (
      hub, "sync_rigid_bodies_to_transforms",
      [this] (entt::registry &registry, double dt) {
        sync_rigid_bodies_to_transforms (registry, dt);
      });

  register_iteration<comp::transform, comp::character_body> (
      hub, "sync_characters_to_transforms",
      [this] (entt::registry &registry, double dt) {
        sync_characters_to_transforms (registry, dt);
      });
}

void
physics_system::on_init (entt::registry &registry)
{
  this->m_registry = &registry;

  recreate_all_bodies (registry);

  {
    auto s_crb = (registry.on_construct<comp::rigid_body>)();
    (s_crb.connect<&physics_system::on_rigid_body_constructed>)(this);
  }
  {
    auto s_drb = (registry.on_destroy<comp::rigid_body>)();
    (s_drb.connect<&physics_system::on_rigid_body_removed>)(this);
  }
  {
    auto s_ca = (registry.on_construct<comp::area>)();
    (s_ca.connect<&physics_system::on_area_constructed>)(this);
  }
  {
    auto s_da = (registry.on_destroy<comp::area>)();
    (s_da.connect<&physics_system::on_area_removed>)(this);
  }
  {
    auto s_dcb = (registry.on_destroy<comp::character_body>)();
    (s_dcb.connect<&physics_system::on_character_body_removed>)(this);
  }
}

void
physics_system::on_inactive (entt::registry &registry)
{
  comp::singl::physics_manager *physics
      = get_registry_physics_manager (registry);
  if (physics != nullptr) {
    phys::engine &engine = physics->ensure_engine ();

    // Manually cleanup all bodies because they might not be destroyed yet
    // or the registry is being cleared after shutdown.
    auto rb_view = registry.view<comp::rigid_body> ();
    for (entt::entity const e : rb_view) {
      registry.get<comp::rigid_body> (e).destroy_body (engine);
    }

    auto area_view = registry.view<comp::area> ();
    for (entt::entity const e : area_view) {
      registry.get<comp::area> (e).destroy_body (engine);
    }

    auto char_view = registry.view<comp::character_body> ();
    for (entt::entity const e : char_view) {
      registry.get<comp::character_body> (e).destroy_body ();
    }
  }

  {
    auto s_crb = (registry.on_construct<comp::rigid_body>)();
    (s_crb.disconnect<&physics_system::on_rigid_body_constructed>)(this);
  }
  {
    auto s_drb = (registry.on_destroy<comp::rigid_body>)();
    (s_drb.disconnect<&physics_system::on_rigid_body_removed>)(this);
  }
  {
    auto s_ca = (registry.on_construct<comp::area>)();
    (s_ca.disconnect<&physics_system::on_area_constructed>)(this);
  }
  {
    auto s_da = (registry.on_destroy<comp::area>)();
    (s_da.disconnect<&physics_system::on_area_removed>)(this);
  }
  {
    auto s_dcb = (registry.on_destroy<comp::character_body>)();
    (s_dcb.disconnect<&physics_system::on_character_body_removed>)(this);
  }

  this->m_registry = nullptr;
}

void
physics_system::on_update (entt::registry &registry, double dt)
{
  this->m_registry = &registry;

  auto &ctx = registry.ctx ();
  if (!ctx.contains<comp::singl::runtime_context *> ()) {
    return;
  }

  auto &runtime = *ctx.get<comp::singl::runtime_context *> ();
  if (!runtime.is_running ()) {
    return;
  }

  run_registered_iterations (registry, dt);
}

void
physics_system::on_editor_update (entt::registry &registry, double dt)
{
  this->m_registry = &registry;

  auto &ctx = registry.ctx ();
  if (!ctx.contains<comp::singl::runtime_context *> ()) {
    return;
  }

  // Only sync if the physics engine already exists.  The dummy registry's
  // physics_manager is reset every frame by ensure_dummy_context_bindings →
  // apply_core_singletons, which would destroy and recreate the engine
  // unnecessarily (and spam the log) if we called ensure_engine() here.
  comp::singl::physics_manager *physics
      = get_registry_physics_manager (registry);
  if ((physics == nullptr) || (physics->try_engine () == nullptr)) {
    return;
  }

  // Keep rigid-body world positions in sync with entity transforms so
  // collision-debug wireframes render at the correct location.
  // force_all=true skips the dynamic-body check so editor movement is
  // reflected in the debug draw in real time.
  sync_transforms_to_rigid_bodies (registry, dt, true);
  sync_transforms_to_areas (registry, dt);
}

void
physics_system::on_render_build_draw_data (entt::registry &registry)
{
  auto &ctx = registry.ctx ();
  if (!ctx.contains<comp::singl::editor_context *> ()) {
    return;
  }
  auto &editor_ctx = *ctx.get<comp::singl::editor_context *> ();

  wsl::debug::debug_renderer_interface *debug_renderer
      = editor_ctx.get_debug_renderer ();
  if (debug_renderer == nullptr) {
    return;
  }

  // Drop last frame's batch first so toggling "Show Debug" off can never
  // leave stale wireframes behind.
  debug_renderer->begin_frame ();

  comp::singl::physics_manager *physics
      = get_registry_physics_manager (registry);
  if ((physics == nullptr) || !physics->show_debug) {
    return;
  }

  // try_engine() rather than ensure_engine(): the dummy registry's
  // physics_manager is rebuilt every frame, so creating an engine here would
  // churn (see on_editor_update).
  if (phys::engine *engine = physics->try_engine (); engine != nullptr) {
    debug::draw_physics_debug (*engine, *debug_renderer);
  }
}

void
physics_system::on_render_record_draw_cmd (entt::registry &registry)
{
  auto &ctx = registry.ctx ();
  if (!ctx.contains<comp::singl::runtime_context *> ()) {
    return;
  }
  auto &runtime = *ctx.get<comp::singl::runtime_context *> ();

  if (!ctx.contains<comp::singl::editor_context *> ()) {
    return;
  }
  auto &editor_ctx = *ctx.get<comp::singl::editor_context *> ();

  auto *scene = runtime.scene_manager ().get_active ();
  if (scene == nullptr) {
    return;
  }

  comp::singl::editor_context::resolved_camera rc;
  if (!editor_ctx.resolve_game_view_camera (registry, scene, rc)) {
    return;
  }

  // Subviewport passes replay these systems with a different camera, while
  // the batch was collected for the game view camera resolved above. This
  // matches the guard render_3d_system uses for its editor gizmos.
  bool const is_subviewport_pass
      = ctx.contains<entt::entity> () && ctx.get<entt::entity> () != entt::null;
  if (is_subviewport_pass) {
    return;
  }

  wsl::debug::debug_renderer_interface *debug_renderer
      = editor_ctx.get_debug_renderer ();
  wsl::gfx::scene_renderer *scene_renderer
      = runtime.try_get_active_scene_renderer ();
  if ((debug_renderer == nullptr) || (scene_renderer == nullptr)) {
    return;
  }

  debug_renderer->end_frame (*scene_renderer, rc.vp ());
}

void
physics_system::update_character_controllers (entt::registry &registry,
                                              double dt)
{
  comp::singl::physics_manager *physics
      = get_registry_physics_manager (registry);
  if (physics == nullptr) {
    return;
  }
  phys::engine &engine = physics->ensure_engine ();

  const float step = static_cast<float> (dt);

  // Character body update is deferred until Box3D provides a character
  // controller.
  (void)step;
  (void)engine;
}

void
physics_system::step_world (entt::registry &registry, double dt)
{
  comp::singl::physics_manager *physics
      = get_registry_physics_manager (registry);
  if (physics == nullptr) {
    return;
  }
  phys::engine &engine = physics->ensure_engine ();

  const float step = static_cast<float> (dt);
  engine.step (step);
}

void
physics_system::dispatch_sensor_overlap_events (entt::registry &registry,
                                                double /*dt*/)
{
  auto &ctx = registry.ctx ();
  if (!ctx.contains<comp::singl::runtime_context *> ()) {
    return;
  }
  auto &runtime = *ctx.get<comp::singl::runtime_context *> ();
  comp::singl::physics_manager *physics
      = get_registry_physics_manager (registry);
  if (physics == nullptr) {
    return;
  }
  phys::engine &engine = physics->ensure_engine ();

  auto events = engine.drain_sensor_events ();
  if (events.empty ()) {
    return;
  }

  struct bid_hash
  {
    std::size_t
    operator() (const phys::body_id &id) const noexcept
    {
      return static_cast<std::size_t> (id);
    }
  };

  std::unordered_map<phys::body_id, entt::entity, bid_hash> body_to_entity;

  {
    auto view = registry.view<comp::rigid_body> ();
    for (entt::entity const e : view) {
      comp::rigid_body const &rb = view.get<comp::rigid_body> (e);
      if (phys::is_valid_body_id (rb.body_id)) {
        body_to_entity[rb.body_id] = e;
      }
    }
  }

  {
    auto view = registry.view<comp::area> ();
    for (entt::entity const e : view) {
      comp::area const &a = view.get<comp::area> (e);
      if (phys::is_valid_body_id (a.body_id)) {
        body_to_entity[a.body_id] = e;
      }
    }
  }

  {
    auto view = registry.view<comp::character_body> ();
    for (entt::entity const e : view) {
      comp::character_body const &character
          = view.get<comp::character_body> (e);
      const phys::body_id body_id = character.get_id ();
      if (phys::is_valid_body_id (body_id)) {
        body_to_entity[body_id] = e;
      }
    }
  }

  for (auto &ev : events) {
    auto it_area = body_to_entity.find (ev.sensor);
    if (it_area == body_to_entity.end ()) {
      continue;
    }

    entt::entity const area_ent = it_area->second;
    entt::entity other_ent = entt::null;

    if (auto it_other = body_to_entity.find (ev.other);
        it_other != body_to_entity.end ()) {
      other_ent = it_other->second;
    }

    if (ev.entered) {
      wsl::event::emit<comp::area::entered> (
          runtime.event_hub (),
          comp::area::entered{ area_ent, other_ent, ev.other });
    } else {
      wsl::event::emit<comp::area::exited> (
          runtime.event_hub (),
          comp::area::exited{ area_ent, other_ent, ev.other });
    }
  }
}

void
physics_system::sync_transforms_to_rigid_bodies (entt::registry &registry,
                                                 double /*dt*/, bool force_all)
{
  comp::singl::physics_manager *physics
      = get_registry_physics_manager (registry);
  if (physics == nullptr) {
    return;
  }
  phys::engine &engine = physics->ensure_engine ();
  auto view = registry.view<comp::transform, comp::rigid_body> ();

  for (entt::entity const e : view) {
    comp::rigid_body &rb = view.get<comp::rigid_body> (e);
    if (!phys::is_valid_body_id (rb.body_id)) {
      continue;
    }

    comp::transform const &t = view.get<comp::transform> (e);

    // Dynamic bodies should not be reset by their transform during gameplay,
    // as physics is the source of truth. Transform -> Physics sync should only
    // happen for Kinematic/Static bodies. When force_all is set (editor mode),
    // sync every body so the debug wireframe follows entity movement.
    if (!force_all && rb.motion_type.value == phys::motion_type::Dynamic) {
      continue;
    }

    glm::vec3 world_pos = t.position;
    glm::quat world_rot = t.rotation;
    glm::vec3 scale{ 1.0F, 1.0F, 1.0F };

    // If it has a parent, we MUST use the WorldTransform because
    // ECS transform is local but the physics engine wants world.
    auto *h = registry.try_get<comp::hierarchy> (e);
    bool const has_parent = (h != nullptr) && h->parent != entt::null;
    if (has_parent) {
      if (auto *wt = registry.try_get<comp::world_transform> (e)) {
        glm::mat4 const wm = wt->value ();
        world_pos = glm::vec3 (wm[3]);
        world_rot = glm::quat_cast (wm);
        scale = glm::vec3 (glm::length (glm::vec3 (wm[0])),
                           glm::length (glm::vec3 (wm[1])),
                           glm::length (glm::vec3 (wm[2])));
      }
    } else {
      scale = glm::vec3 (t.scale.x (), t.scale.y (), t.scale.z ());
    }

    // Check for scale change — rebuild body to update the debug wireframe.
    math::vec3f const current_scale{ scale.x, scale.y, scale.z };
    if (rb.has_scale_change (current_scale)) {
      // Compute final world pos/rot with offset for the rebuild
      glm::vec3 const final_pos
          = world_pos + world_rot * (glm::vec3)rb.position;
      glm::quat const final_rot = world_rot * (glm::quat)rb.rotation;
      rb.rebuild_body (engine, final_pos, final_rot, scale);
      rb.applied_scale = current_scale;
      continue; // already re-positioned inside rebuild_body
    }

    // Apply rigid_body's local offset
    world_pos = world_pos + (world_rot * (glm::vec3)rb.position);
    world_rot = world_rot * (glm::quat)rb.rotation;

    engine.set_body_transform (
        rb.body_id, { world_pos.x, world_pos.y, world_pos.z },
        { world_rot.x, world_rot.y, world_rot.z, world_rot.w });
  }
}

void
physics_system::recreate_all_bodies (entt::registry &registry)
{
  comp::singl::physics_manager *physics
      = get_registry_physics_manager (registry);
  if (physics == nullptr) {
    return;
  }
  phys::engine &engine = physics->ensure_engine ();

  // Recreate rigid bodies
  {
    auto view = registry.view<comp::rigid_body> ();
    for (entt::entity const e : view) {
      comp::rigid_body &rb = view.get<comp::rigid_body> (e);
      if (!phys::is_valid_body_id (rb.body_id)) {
        glm::vec3 world_pos{ 0.0F, 0.0F, 0.0F };
        glm::quat world_rot{ 1.0F, 0.0F, 0.0F, 0.0F };
        glm::vec3 scale{ 1.0F, 1.0F, 1.0F };
        if (auto *wt = registry.try_get<comp::world_transform> (e); wt) {
          glm::mat4 const &wm = wt->value ();
          world_pos = glm::vec3 (wm[3]);
          world_rot = glm::quat_cast (wm);
          // Extract scale from world transform
          scale = glm::vec3 (glm::length (glm::vec3 (wm[0])),
                             glm::length (glm::vec3 (wm[1])),
                             glm::length (glm::vec3 (wm[2])));
        } else if (auto *t = registry.try_get<comp::transform> (e); t) {
          world_pos = (glm::vec3)t->position;
          world_rot = (glm::quat)t->rotation;
          scale = glm::vec3 (t->scale.x (), t->scale.y (), t->scale.z ());
        }
        // Apply rigid_body offset to get the final body world position
        world_pos = world_pos + (world_rot * (glm::vec3)rb.position);
        world_rot = world_rot * (glm::quat)rb.rotation;
        rb.create_body (engine, world_pos, world_rot, scale);
        rb.applied_scale = math::vec3f{ scale.x, scale.y, scale.z };
      }
    }
  }

  // Recreate sensor areas
  {
    auto view = registry.view<comp::area> ();
    for (entt::entity const e : view) {
      comp::area &a = view.get<comp::area> (e);
      if (!phys::is_valid_body_id (a.body_id)) {
        glm::vec3 world_pos{ 0.0F, 0.0F, 0.0F };
        glm::quat world_rot{ 1.0F, 0.0F, 0.0F, 0.0F };
        glm::vec3 scale{ 1.0F, 1.0F, 1.0F };
        if (auto *wt = registry.try_get<comp::world_transform> (e); wt) {
          glm::mat4 const &wm = wt->value ();
          world_pos = glm::vec3 (wm[3]);
          world_rot = glm::quat_cast (wm);
          scale = glm::vec3 (glm::length (glm::vec3 (wm[0])),
                             glm::length (glm::vec3 (wm[1])),
                             glm::length (glm::vec3 (wm[2])));
        } else if (auto *t = registry.try_get<comp::transform> (e); t) {
          world_pos = (glm::vec3)t->position;
          world_rot = (glm::quat)t->rotation;
          scale = glm::vec3 (t->scale.x (), t->scale.y (), t->scale.z ());
        }
        // Apply area offset
        world_pos = world_pos + (world_rot * (glm::vec3)a.position);
        world_rot = world_rot * (glm::quat)a.rotation;
        a.create_body (engine, world_pos, world_rot, scale);
      }
    }
  }

  // Recreate character bodies
  {
    auto view = registry.view<comp::character_body> ();
    for (entt::entity const e : view) {
      comp::character_body &c = view.get<comp::character_body> (e);
      if (!c.valid ()) {
        if (auto *wt = registry.try_get<comp::world_transform> (e); wt) {
          c.recreate (engine,
                      glm::vec3 (static_cast<glm::mat4> (wt->value ())[3]));
        } else if (auto *t = registry.try_get<comp::transform> (e); t) {
          c.recreate (engine, (glm::vec3)t->position);
        }
      }
    }
  }
}

void
physics_system::sync_rigid_bodies_to_transforms (entt::registry &registry,
                                                 double /*dt*/)
{
  comp::singl::physics_manager *physics
      = get_registry_physics_manager (registry);
  if (physics == nullptr) {
    return;
  }
  phys::engine &engine = physics->ensure_engine ();

  auto view = registry.view<comp::transform, comp::rigid_body> ();

  for (entt::entity const e : view) {
    comp::rigid_body const &rb = view.get<comp::rigid_body> (e);

    if (!phys::is_valid_body_id (rb.body_id)) {
      continue;
    }

    // The physics body sits at transform_world + offset.  Remove the offset
    // so that set_local_from_world sees the transform's own world position.
    const phys::vector3 body_position = engine.get_body_position (rb.body_id);
    const phys::quaternion body_rotation
        = engine.get_body_rotation (rb.body_id);
    glm::vec3 const body_world (body_position.x, body_position.y,
                                body_position.z);
    glm::quat const body_rot (body_rotation.w, body_rotation.x, body_rotation.y,
                              body_rotation.z);
    glm::quat const inv_offset = glm::inverse ((glm::quat)rb.rotation);
    glm::quat const xform_rot = body_rot * inv_offset;
    glm::vec3 const xform_pos = body_world - xform_rot * (glm::vec3)rb.position;

    set_local_from_world (registry, e, xform_pos, xform_rot);
  }
}

void
physics_system::sync_characters_to_transforms (entt::registry &registry,
                                               double /*dt*/)
{
  comp::singl::physics_manager *physics
      = get_registry_physics_manager (registry);
  if (physics == nullptr) {
    return;
  }
  phys::engine &engine = physics->ensure_engine ();

  auto view = registry.view<comp::transform, comp::character_body> ();

  for (entt::entity const e : view) {
    comp::character_body &c = view.get<comp::character_body> (e);
    if (!c.valid ()) {
      continue;
    }

    phys::vector3 const pos = engine.get_body_position (c.get_id ());
    phys::quaternion const rot = engine.get_body_rotation (c.get_id ());
    set_local_from_world (registry, e, glm::vec3 (pos.x, pos.y, pos.z),
                          glm::quat (rot.w, rot.x, rot.y, rot.z));
  }
}

void
physics_system::on_rigid_body_constructed (entt::registry &registry,
                                           entt::entity entity)
{
  comp::singl::physics_manager *physics
      = get_registry_physics_manager (registry);
  if (physics == nullptr) {
    return;
  }
  phys::engine &engine = physics->ensure_engine ();

  auto &rb = registry.get<comp::rigid_body> (entity);
  if (phys::is_valid_body_id (rb.body_id)) {
    return; // already has a body
  }

  // Compute world position from the entity's transform + rigid_body offset.
  //
  // IMPORTANT: prefer comp::transform over comp::world_transform for root
  // entities because world_transform may be stale — it is recomputed only
  // during transform_system::on_editor_update, so if the user moves the
  // entity and then adds rigid_body between frames, world_transform still
  // holds the previous frame's value (or identity for a brand-new entity).
  // For root entities, transform.position IS the world position and is
  // always up-to-date.
  glm::vec3 world_pos{ 0.0F, 0.0F, 0.0F };
  glm::quat world_rot{ 1.0F, 0.0F, 0.0F, 0.0F };
  glm::vec3 scale{ 1.0F, 1.0F, 1.0F };

  auto *t = registry.try_get<comp::transform> (entity);
  auto *h = registry.try_get<comp::hierarchy> (entity);
  bool const has_parent = (h != nullptr) && h->parent != entt::null;

  if (t != nullptr) {
    if (has_parent) {
      // Child entity — world_transform is the most reliable source.
      if (auto *wt = registry.try_get<comp::world_transform> (entity); wt) {
        glm::mat4 const &wm = wt->value ();
        world_pos = glm::vec3 (wm[3]);
        world_rot = glm::quat_cast (wm);
        scale = glm::vec3 (glm::length (glm::vec3 (wm[0])),
                           glm::length (glm::vec3 (wm[1])),
                           glm::length (glm::vec3 (wm[2])));
      } else {
        world_pos = (glm::vec3)t->position;
        world_rot = (glm::quat)t->rotation;
        scale = glm::vec3 (t->scale.x (), t->scale.y (), t->scale.z ());
      }
    } else {
      // Root entity — transform is always current.
      world_pos = (glm::vec3)t->position;
      world_rot = (glm::quat)t->rotation;
      scale = glm::vec3 (t->scale.x (), t->scale.y (), t->scale.z ());
    }
  } else if (auto *wt = registry.try_get<comp::world_transform> (entity); wt) {
    glm::mat4 const &wm = wt->value ();
    world_pos = glm::vec3 (wm[3]);
    world_rot = glm::quat_cast (wm);
    scale = glm::vec3 (glm::length (glm::vec3 (wm[0])),
                       glm::length (glm::vec3 (wm[1])),
                       glm::length (glm::vec3 (wm[2])));
  }

  // Apply rigid_body offset to get the final body world position
  world_pos = world_pos + (world_rot * (glm::vec3)rb.position);
  world_rot = world_rot * (glm::quat)rb.rotation;

  rb.create_body (engine, world_pos, world_rot, scale);
  rb.applied_scale = math::vec3f{ scale.x, scale.y, scale.z };
}

void
physics_system::sync_transforms_to_areas (entt::registry &registry,
                                          double /*dt*/)
{
  comp::singl::physics_manager *physics
      = get_registry_physics_manager (registry);
  if (physics == nullptr) {
    return;
  }
  phys::engine &engine = physics->ensure_engine ();
  auto view = registry.view<comp::area> ();

  for (entt::entity const e : view) {
    comp::area const &a = view.get<comp::area> (e);
    if (!phys::is_valid_body_id (a.body_id)) {
      continue;
    }

    glm::vec3 world_pos{ 0.0F, 0.0F, 0.0F };
    glm::quat world_rot{ 1.0F, 0.0F, 0.0F, 0.0F };

    auto *t = registry.try_get<comp::transform> (e);
    auto *h = registry.try_get<comp::hierarchy> (e);
    bool const has_parent = (h != nullptr) && h->parent != entt::null;

    if (t != nullptr) {
      if (has_parent) {
        if (auto *wt = registry.try_get<comp::world_transform> (e); wt) {
          glm::mat4 const wm = wt->value ();
          world_pos = glm::vec3 (wm[3]);
          world_rot = glm::quat_cast (wm);
        } else {
          world_pos = (glm::vec3)t->position;
          world_rot = (glm::quat)t->rotation;
        }
      } else {
        world_pos = (glm::vec3)t->position;
        world_rot = (glm::quat)t->rotation;
      }
    } else if (auto *wt = registry.try_get<comp::world_transform> (e); wt) {
      glm::mat4 const wm = wt->value ();
      world_pos = glm::vec3 (wm[3]);
      world_rot = glm::quat_cast (wm);
    }

    // Apply area offset
    world_pos = world_pos + (world_rot * (glm::vec3)a.position);
    world_rot = world_rot * (glm::quat)a.rotation;

    engine.set_body_transform (
        a.body_id, { world_pos.x, world_pos.y, world_pos.z },
        { world_rot.x, world_rot.y, world_rot.z, world_rot.w });
  }
}

void
physics_system::on_area_constructed (entt::registry &registry,
                                     entt::entity entity)
{
  comp::singl::physics_manager *physics
      = get_registry_physics_manager (registry);
  if (physics == nullptr) {
    return;
  }
  phys::engine &engine = physics->ensure_engine ();

  auto &a = registry.get<comp::area> (entity);
  if (phys::is_valid_body_id (a.body_id)) {
    return;
  }

  // Compute world position from transform + offset (same logic as
  // on_rigid_body_constructed).
  glm::vec3 world_pos{ 0.0F, 0.0F, 0.0F };
  glm::quat world_rot{ 1.0F, 0.0F, 0.0F, 0.0F };
  glm::vec3 scale{ 1.0F, 1.0F, 1.0F };

  auto *t = registry.try_get<comp::transform> (entity);
  auto *h = registry.try_get<comp::hierarchy> (entity);
  bool const has_parent = (h != nullptr) && h->parent != entt::null;

  if (t != nullptr) {
    if (has_parent) {
      if (auto *wt = registry.try_get<comp::world_transform> (entity); wt) {
        glm::mat4 const &wm = wt->value ();
        world_pos = glm::vec3 (wm[3]);
        world_rot = glm::quat_cast (wm);
        scale = glm::vec3 (glm::length (glm::vec3 (wm[0])),
                           glm::length (glm::vec3 (wm[1])),
                           glm::length (glm::vec3 (wm[2])));
      } else {
        world_pos = (glm::vec3)t->position;
        world_rot = (glm::quat)t->rotation;
        scale = glm::vec3 (t->scale.x (), t->scale.y (), t->scale.z ());
      }
    } else {
      world_pos = (glm::vec3)t->position;
      world_rot = (glm::quat)t->rotation;
      scale = glm::vec3 (t->scale.x (), t->scale.y (), t->scale.z ());
    }
  } else if (auto *wt = registry.try_get<comp::world_transform> (entity); wt) {
    glm::mat4 const &wm = wt->value ();
    world_pos = glm::vec3 (wm[3]);
    world_rot = glm::quat_cast (wm);
    scale = glm::vec3 (glm::length (glm::vec3 (wm[0])),
                       glm::length (glm::vec3 (wm[1])),
                       glm::length (glm::vec3 (wm[2])));
  }

  // Apply area offset
  world_pos = world_pos + (world_rot * (glm::vec3)a.position);
  world_rot = world_rot * (glm::quat)a.rotation;

  a.create_body (engine, world_pos, world_rot, scale);
}

void
physics_system::on_rigid_body_removed (entt::registry &registry,
                                       entt::entity entity)
{
  comp::singl::physics_manager *physics
      = get_registry_physics_manager (registry);
  if (physics == nullptr) {
    return;
  }
  phys::engine &engine = physics->ensure_engine ();

  if (auto *rb = registry.try_get<comp::rigid_body> (entity)) {
    rb->destroy_body (engine);
  }
}

void
physics_system::on_area_removed (entt::registry &registry, entt::entity entity)
{
  comp::singl::physics_manager *physics
      = get_registry_physics_manager (registry);
  if (physics == nullptr) {
    return;
  }
  phys::engine &engine = physics->ensure_engine ();

  if (auto *a = registry.try_get<comp::area> (entity)) {
    a->destroy_body (engine);
  }
}

void
physics_system::on_character_body_removed (entt::registry &registry,
                                           entt::entity entity)
{
  if (auto *cb = registry.try_get<comp::character_body> (entity)) {
    cb->destroy_body ();
  }
}

} // namespace sys

} // namespace wsl
