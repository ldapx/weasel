#include "scene.hpp"
#include "comp/singl/ui_manager.hpp"
#include "reg/component_registry.hpp"
#include "reg/singleton_registry.hpp"
#include "rsc/resource_ids.hpp"
#include "rsc/resource_manager.hpp"
#include "rsc/resource_ref.hpp"
#include "sys/system.hpp"
#include "wsl/log/log.hpp"
#include <SDL3/SDL_events.h>
#include <algorithm>
#include <unordered_set>
#include <cstring>
#include <entt/core/fwd.hpp>
#include <entt/core/hashed_string.hpp>
#include <entt/core/type_info.hpp>
#include <entt/entity/entity.hpp>
#include <entt/entity/fwd.hpp>
#include <entt/meta/meta.hpp>
#include <entt/meta/resolve.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace entt::literals;

#include "../comp/hierarchy.hpp"
#include "../comp/prefab_instance.hpp"
#include "wsl/comp/singl/editor_context.hpp"
#include "../comp/singl/runtime_context.hpp"

namespace wsl
{

namespace rsc
{

entt::registry &
scene::get_registry ()
{
  return m_registry;
}

const std::string &
scene::get_name () const
{
  return m_name;
}

void
scene::set_name (std::string name)
{
  m_name = std::move (name);
}

sys::ecs_system &
scene::add_system_instance (std::unique_ptr<sys::ecs_system> sys,
                            bool initialize_if_running)
{
  entt::id_type new_type_id = sys->get_type_id ();
  for (const auto &existing : systems) {
    if (existing && existing->get_type_id () == new_type_id) {
      return *existing;
    }
  }

  sys::ecs_system *system = sys.get ();
  systems.emplace_back (std::move (sys));

  on_system_added (*system);

  if (initialize_if_running && m_initialized) {
    system->refresh_activation (&m_registry, is_playing ());
  }

  return *system;
}

void
scene::remove_system (sys::ecs_system *system)
{
  auto it = std::find_if (systems.begin (), systems.end (),
                          [system] (const std::unique_ptr<sys::ecs_system> &s) {
                            return s.get () == system;
                          });

  if (it != systems.end ()) {
    if (m_initialized) {
      (*it)->shutdown (&m_registry);
    }
    if (m_runtime_ctx != nullptr) {
      m_runtime_ctx->event_hub ().clear_system_instance (
          dynamic_cast<void *> (it->get ()));
    }
    systems.erase (it);
  }
}

void
scene::on_system_added (sys::ecs_system &system)
{
  if (m_runtime_ctx == nullptr) {
    return;
  }

  m_runtime_ctx->event_hub ().clear_system_declarations (system.get_type_id ());
  system.register_event_sources (m_runtime_ctx->event_hub ());
  system.register_event_sinks (m_runtime_ctx->event_hub ());
  system.register_iterations (m_runtime_ctx->event_hub ());
}

void
scene::init ()
{
  m_initialized = true;
  refresh_system_states ();
  wsl::log::sys ()->debug ("Scene '{}' initialized ({} systems)", m_name,
                           systems.size ());
}

void
scene::pause ()
{
  m_running = false;
  if (m_initialized) {
    refresh_system_states ();
  }
}

void
scene::resume ()
{
  if (m_initialized && !m_running) {
    m_running = true;
    refresh_system_states ();
  }
}

void
scene::update (double dt)
{
  if (!m_initialized) {
    return;
  }

  for (std::unique_ptr<sys::ecs_system> &sys : systems) {
    if (!sys) {
      continue;
    }

    sys->update (&m_registry, dt);
  }
}

void
scene::shutdown_systems ()
{
  for (std::unique_ptr<sys::ecs_system> &sys : systems) {
    if (!sys) {
      continue;
    }

    sys->shutdown (&m_registry);
    if (m_runtime_ctx != nullptr) {
      m_runtime_ctx->event_hub ().clear_system_instance (
          dynamic_cast<void *> (sys.get ()));
    }
  }
}

bool
scene::is_playing () const
{
  return (m_runtime_ctx != nullptr) ? m_runtime_ctx->is_running () : m_running;
}

void
scene::refresh_system_states ()
{
  if (!m_initialized) {
    return;
  }

  const bool playing = is_playing ();
  m_running = playing;

  for (std::unique_ptr<sys::ecs_system> &sys : systems) {
    if (!sys) {
      continue;
    }

    sys->refresh_activation (&m_registry, playing);
  }
}

void
scene::stop_and_clear ()
{
  shutdown_systems ();
  if (m_runtime_ctx != nullptr) {
    m_runtime_ctx->component_registry ().clear_das_component_storage (
        m_registry);
  }
  m_registry.clear ();
  m_initialized = false;
  m_running = false;
  systems.clear ();
  reset_scene_context ();
}

void
scene::clear ()
{
  shutdown_systems ();
  if (m_runtime_ctx != nullptr) {
    m_runtime_ctx->component_registry ().clear_das_component_storage (
        m_registry);
  }
  m_registry.clear ();
  systems.clear ();
  reset_scene_context ();
}

void
scene::clear_registry ()
{
  if (m_runtime_ctx != nullptr) {
    m_runtime_ctx->component_registry ().clear_das_component_storage (
        m_registry);
  }
  m_registry.clear ();
  m_entity_names.clear ();
  reset_scene_context ();

  if (m_initialized) {
    refresh_system_states ();
  }
}

void
scene::destroy_entity_subtree (entt::entity root)
{
  if (!m_registry.valid (root)) {
    return;
  }

  std::vector<entt::entity> pending{ root };
  std::vector<entt::entity> destruction_order;
  std::unordered_set<entt::entity> visited;

  while (!pending.empty ()) {
    const entt::entity current = pending.back ();
    pending.pop_back ();
    if (!m_registry.valid (current) || !visited.insert (current).second) {
      continue;
    }

    destruction_order.push_back (current);
    if (!m_registry.all_of<comp::hierarchy> (current)) {
      continue;
    }

    entt::entity child = m_registry.get<comp::hierarchy> (current).first;
    std::unordered_set<entt::entity> sibling_chain;
    while (child != entt::null && m_registry.valid (child)
           && sibling_chain.insert (child).second) {
      const auto *child_hierarchy
          = m_registry.try_get<comp::hierarchy> (child);
      if (child_hierarchy == nullptr) {
        break;
      }
      const entt::entity next = child_hierarchy->next;
      pending.push_back (child);
      child = next;
    }
  }

  for (const entt::entity current : destruction_order) {
    auto *hierarchy = m_registry.try_get<comp::hierarchy> (current);
    if (hierarchy == nullptr || !m_registry.valid (hierarchy->parent)
        || !m_registry.all_of<comp::hierarchy> (hierarchy->parent)) {
      continue;
    }

    auto &parent = m_registry.get<comp::hierarchy> (hierarchy->parent);
    entt::entity previous = entt::null;
    entt::entity child = parent.first;
    std::unordered_set<entt::entity> sibling_chain;
    while (child != entt::null && m_registry.valid (child)
           && sibling_chain.insert (child).second) {
      auto *child_hierarchy = m_registry.try_get<comp::hierarchy> (child);
      if (child_hierarchy == nullptr) {
        break;
      }
      if (child == current) {
        if (previous == entt::null) {
          parent.first = child_hierarchy->next;
        } else if (auto *previous_hierarchy
                       = m_registry.try_get<comp::hierarchy> (previous)) {
          previous_hierarchy->next = child_hierarchy->next;
        }
        break;
      }
      previous = child;
      child = child_hierarchy->next;
    }
  }

  for (auto it = destruction_order.rbegin (); it != destruction_order.rend ();
       ++it) {
    if (!m_registry.valid (*it)) {
      continue;
    }
    remove_entity_name (*it);
    m_registry.destroy (*it);
  }
}

void
scene::add_resource (io::resource_type type, entt::id_type id)
{
  for (const io::resource_ref &r : m_load_list) {
    if (r.type == type && r.id == id) {
      wsl::log::rsc ()->debug ("Resource type={} id={} already in load list",
                               (int)type, id);
      return;
    }
  }

  wsl::log::rsc ()->trace ("Adding resource (type={}) id={} to load list",
                           (int)type, id);
  m_load_list.push_back ({ type, id });

  // If this scene is the active one, load immediately
  if (m_runtime_ctx != nullptr) {
    scene const *active_scene = m_runtime_ctx->scene_manager ().get_active ();
    wsl::log::rsc ()->trace ("Active scene={} this={}", (void *)active_scene,
                             (void *)this);
    if (active_scene == this) {
      wsl::log::rsc ()->trace ("Calling resource_manager.load");
      rsc::resource_manager &res_mgr = m_runtime_ctx->resource_manager ();
      res_mgr.load ({ type, id });
    }
  }
}

void
scene::remove_resource (io::resource_type type, entt::id_type id)
{
  m_load_list.erase (std::remove_if (m_load_list.begin (), m_load_list.end (),
                                     [&] (const io::resource_ref &r) {
                                       return r.type == type && r.id == id;
                                     }),
                     m_load_list.end ());
}

bool
scene::has_resource (io::resource_type type, entt::id_type id) const
{
  for (const io::resource_ref &r : m_load_list) {
    if (r.type == type && r.id == id) {
      return true;
    }
  }
  return false;
}

const std::vector<io::resource_ref> &
scene::get_load_list () const
{
  return m_load_list;
}

void
scene::set_entity_name (entt::entity e, std::string name)
{
  m_entity_names[e] = std::move (name);
}

const std::string &
scene::get_entity_name ([[maybe_unused]] entt::entity e) const
{
  static const std::string unnamed = "<unnamed>";
  if (std::unordered_map<entt::entity, std::string>::const_iterator const it
      = m_entity_names.find (e);
      it != m_entity_names.end ()) {
    return it->second;
  }
  return unnamed;
}

void
scene::remove_entity_name (entt::entity e)
{
  m_entity_names.erase (e);
}

std::unordered_map<entt::entity, std::string> &
scene::get_entity_names ()
{
  return m_entity_names;
}

const std::unordered_map<entt::entity, std::string> &
scene::get_entity_names () const
{
  return m_entity_names;
}

scene::scene (comp::singl::runtime_context *runtime_ctx,
              comp::singl::editor_context *editor_ctx, const std::string &name)
    : m_name (name), m_runtime_ctx (runtime_ctx), m_editor_ctx (editor_ctx)
{
  auto destroy_signal = (m_registry.on_destroy<entt::entity>)();
  (destroy_signal.connect<&scene::on_entity_destroyed>)(this);
  ensure_context_bindings ();
}

scene::~scene ()
{
  shutdown_systems ();
}

scene::scene (scene &&other)
    : m_name (std::move (other.m_name)),
      m_entity_names (std::move (other.m_entity_names)),
      m_registry (std::move (other.m_registry)),
      m_load_list (std::move (other.m_load_list)),
      m_initialized (other.m_initialized), m_runtime_ctx (other.m_runtime_ctx),
      m_editor_ctx (other.m_editor_ctx), m_running (other.m_running),
      systems (std::move (other.systems))
{
  // The moved registry still carries a delegate bound to `other`'s address.
  // Nulling the source's context first makes that stale binding inert even
  // before it is removed, so it can never become a wild dereference.
  other.m_runtime_ctx = nullptr;
  connect_destroy_signal (&other);
  ensure_context_bindings ();
}

scene &
scene::operator= (scene &&other)
{
  if (this == &other) {
    return *this;
  }

  m_name = std::move (other.m_name);
  m_entity_names = std::move (other.m_entity_names);
  m_registry = std::move (other.m_registry);
  m_load_list = std::move (other.m_load_list);
  m_initialized = other.m_initialized;
  m_runtime_ctx = other.m_runtime_ctx;
  m_editor_ctx = other.m_editor_ctx;
  m_running = other.m_running;
  systems = std::move (other.systems);

  other.m_runtime_ctx = nullptr;
  connect_destroy_signal (&other);
  ensure_context_bindings ();
  return *this;
}

void
scene::connect_destroy_signal (scene *previous)
{
  // The handler is invoked through a raw `this`, so the binding must be rebuilt
  // whenever the scene occupies a new address -- i.e. after every move.
  //
  // Removing the old binding first is mandatory, not tidy-up: a moved registry
  // still holds the *previous* object's delegate, so simply connecting again
  // would leave both installed and the stale one would fire on the next
  // clear().
  //
  // `on_destroy()` returns a sink, so both operations are direct members.
  // `disconnect(const void *)` matches on the bound instance; the
  // `disconnect<Candidate>()` overload matches on a member-function pointer and
  // only supports free functions when given no instance.
  if (previous != nullptr) {
    m_registry.on_destroy<entt::entity> ().disconnect (previous);
  }

  (m_registry.on_destroy<entt::entity> ()
       .connect<&scene::on_entity_destroyed>)(this);
}

void
scene::on_entity_destroyed (entt::registry &registry, entt::entity entity)
{
  if (m_runtime_ctx != nullptr) {
    m_runtime_ctx->component_registry ().clear_das_component_entity (registry,
                                                                     entity);
  }
}

void
scene::ensure_context_bindings ()
{
  auto &ctx = m_registry.ctx ();

  if (ctx.contains<scene *> ()) {
    ctx.erase<scene *> ();
  }
  ctx.emplace<scene *> (this);

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
    if (ctx.contains<scene_manager *> ()) {
      ctx.erase<scene_manager *> ();
    }
    ctx.emplace<scene_manager *> (&m_runtime_ctx->scene_manager ());

    if (ctx.contains<resource_manager_view *> ()) {
      ctx.erase<resource_manager_view *> ();
    }
    ctx.emplace<resource_manager_view *> (
        &m_runtime_ctx->resource_manager_view ());

    if (ctx.contains<comp::singl::ui_manager *> ()) {
      ctx.erase<comp::singl::ui_manager *> ();
    }
    ctx.emplace<comp::singl::ui_manager *> (&m_runtime_ctx->ui_manager ());

    m_runtime_ctx->singleton_registry ().apply_core_singletons (m_registry);
  }
}

void
scene::reset_scene_context ()
{
  if (m_runtime_ctx != nullptr) {
    m_runtime_ctx->singleton_registry ().reset_scene_registry (m_registry);
  }

  ensure_context_bindings ();
}

std::vector<sys::ecs_system *>
scene::get_systems ()
{
  std::vector<sys::ecs_system *> out;
  out.reserve (systems.size ());

  for (std::unique_ptr<sys::ecs_system> const &sys : systems) {
    out.push_back (sys.get ());
  }

  return out;
}

entt::entity
scene::copy_entity (scene &src_scene, entt::entity src_entity,
                    entt::entity dst_parent, bool is_instantiating_prefab,
                    scene_id prefab_id)
{
  entt::registry &src_reg = src_scene.get_registry ();
  entt::registry &dst_reg = get_registry ();

  entt::entity const dst_entity = dst_reg.create ();
  set_entity_name (dst_entity, src_scene.get_entity_name (src_entity));

  // Copy all components by iterating over storages
  for (auto [id, storage] : src_reg.storage ()) {
    if (storage.contains (src_entity)) {
      // Handle hierarchy separately
      if (id == entt::type_hash<comp::hierarchy>::value ()) {
        continue;
      }

      if (m_runtime_ctx->component_registry ().copy_world_component (
              src_reg, src_entity, dst_reg, dst_entity, id)) {
        continue;
      }

      // Fallback to meta-based generic copy
      entt::meta_type const type = entt::resolve (id);
      if (type) {
        auto comp_any = type.func ("get"_hs).invoke (src_reg, src_entity);
        if (comp_any) {
          type.func ("emplace_or_replace"_hs)
              .invoke (dst_reg, dst_entity, comp_any);
        }
      }
    }
  }

  // Daslang components live in registry-local type-erased pools rather than
  // EnTT typed storages, so copy them explicitly with the owning registries.
  auto &component_registry = m_runtime_ctx->component_registry ();
  for (const auto *desc : component_registry.get_world_components (
           reg::world_component_order::type_id)) {
    if (desc == nullptr || !desc->is_das_component
        || !component_registry.das_component_contains (src_reg, desc->type_id,
                                                       src_entity)) {
      continue;
    }

    if (!component_registry.das_component_add (dst_reg, desc->type_id,
                                               dst_entity)) {
      continue;
    }

    const uint8_t *src_data = component_registry.das_component_data (
        src_reg, desc->type_id, src_entity);
    uint8_t *dst_data = component_registry.das_component_data (
        dst_reg, desc->type_id, dst_entity);
    if (src_data != nullptr && dst_data != nullptr
        && desc->das_struct_size > 0) {
      std::memcpy (dst_data, src_data,
                   static_cast<std::size_t> (desc->das_struct_size));
    }
  }

  if (is_instantiating_prefab) {
    dst_reg.emplace_or_replace<comp::prefab_instance> (dst_entity, prefab_id,
                                                       src_entity);
  }

  if (src_reg.all_of<comp::hierarchy> (src_entity)) {
    auto &src_h = src_reg.get<comp::hierarchy> (src_entity);
    auto &dst_h = dst_reg.emplace_or_replace<comp::hierarchy> (dst_entity);
    dst_h.parent = dst_parent;

    entt::entity src_child = src_h.first;
    entt::entity prev_dst_child = entt::null;
    while (src_child != entt::null) {
      entt::entity const dst_child = copy_entity (
          src_scene, src_child, dst_entity, is_instantiating_prefab, prefab_id);
      if (prev_dst_child == entt::null) {
        dst_h.first = dst_child;
      } else {
        dst_reg.get<comp::hierarchy> (prev_dst_child).next = dst_child;
      }
      prev_dst_child = dst_child;
      src_child = src_reg.get<comp::hierarchy> (src_child).next;
    }
  }

  return dst_entity;
}

} // namespace rsc

} // namespace wsl
