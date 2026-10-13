#include "system_factory_registry.hpp"
#include "event/event_hub.hpp"
#include "../rsc/scene.hpp"
#include "../sys/system.hpp"
#include "../das/das_system_adapter.hpp"
#include "../das/das_engine.hpp"
#include "../log/log.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace wsl
{

namespace reg
{

namespace
{

class placeholder_system : public sys::ecs_system_t<placeholder_system>
{
public:
  using ecs_system_t::ecs_system_t;
};

} // namespace

void
system_factory_registry::register_system_factory (
    system_factory_fn factory, const system_registration_options &options)
{
  system_descriptor desc{};
  desc.display_name = std::string (options.display_name);
  desc.runtime_registered = options.runtime_registered;
  desc.factory = std::move (factory);

  // We don't have a type_id for custom factories unless they are registered
  // through register_system_type. For custom factories, we use the display
  // name as the primary identity.
  m_factories[desc.display_name] = std::move (desc);
}

bool
system_factory_registry::register_cached_runtime_system (
    entt::id_type type_id, std::string_view type_name,
    std::string_view display_name)
{
  system_descriptor desc{};
  desc.type_id = type_id;
  desc.type_name = std::string (type_name);
  desc.display_name = display_name.empty ()
                          ? comp::humanize_identifier (type_name)
                          : std::string (display_name);
  desc.runtime_registered = true;
  desc.factory = [display_name = desc.display_name] (rsc::scene &) {
    return std::make_unique<placeholder_system> (display_name);
  };
  return register_cached_runtime_descriptor (std::move (desc));
}

#if WEASEL_HAS_DASLANG
bool
system_factory_registry::register_cached_runtime_system (
    entt::id_type type_id, std::string_view type_name,
    std::string_view display_name, std::string_view script_path,
    das::das_engine &engine)
{
  system_descriptor desc{};
  desc.type_id = type_id;
  desc.type_name = std::string (type_name);
  desc.display_name = display_name.empty ()
                          ? comp::humanize_identifier (type_name)
                          : std::string (display_name);
  desc.runtime_registered = true;
  desc.stage = std::string (sys::stage_registry::default_stage);
  desc.script_path = std::string (script_path);
  desc.factory = [display_name = desc.display_name,
                  script_path = desc.script_path, type_id,
                  &engine] (rsc::scene &) -> std::unique_ptr<sys::ecs_system> {
    // User systems must use the Daslang class-adapter contract.
    std::string error;
    auto instance = engine.instantiate_class (script_path, "System", error);
    if (instance.ptr && instance.info && instance.ctx) {
      return std::make_unique<das::das_system_adapter> (
          display_name, script_path, engine, type_id, instance.ptr,
          instance.info, instance.ctx);
    }
    wsl::log::xmake ()->error (
        "Daslang system '{}' must define class System : EcsSystem: {}",
        script_path, error);
    return nullptr;
  };

  return register_cached_runtime_descriptor (std::move (desc));
}
#endif

bool
system_factory_registry::register_cached_runtime_descriptor (
    system_descriptor desc)
{
  const auto same_id = std::find_if (
      m_factories.begin (), m_factories.end (),
      [&desc] (const auto &entry) { return entry.second.type_id == desc.type_id; });
  if (same_id != m_factories.end ()) {
    const system_descriptor &existing = same_id->second;
    if (!existing.runtime_registered || existing.type_name != desc.type_name
        || existing.script_path != desc.script_path) {
      wsl::log::xmake ()->error (
          "Runtime system id collision: '{}' ('{}') conflicts with '{}' ('{}')",
          existing.type_name, existing.script_path, desc.type_name,
          desc.script_path);
      return false;
    }
    m_factories.erase (same_id);
  }

  if (const auto same_name = m_factories.find (desc.display_name);
      same_name != m_factories.end ()
      && same_name->second.type_id != desc.type_id) {
    wsl::log::xmake ()->error (
        "Runtime system display name '{}' is ambiguous between '{}' and '{}'",
        desc.display_name, same_name->second.script_path, desc.script_path);
    return false;
  }

  m_factories[desc.display_name] = std::move (desc);
  rebuild_type_name_index ();
  return true;
}

void
system_factory_registry::rebuild_type_name_index ()
{
  m_type_to_name.clear ();
  m_type_name_to_display_name.clear ();
  m_ambiguous_type_names.clear ();

  for (const auto &[display_name, desc] : m_factories) {
    if (desc.type_id != entt::id_type{}) {
      m_type_to_name[desc.type_id] = display_name;
    }
    if (desc.type_name.empty ()
        || m_ambiguous_type_names.contains (desc.type_name)) {
      continue;
    }
    const auto [it, inserted]
        = m_type_name_to_display_name.try_emplace (desc.type_name,
                                                   display_name);
    if (!inserted && it->second != display_name) {
      m_type_name_to_display_name.erase (it);
      m_ambiguous_type_names.insert (desc.type_name);
    }
  }
}

const system_factory_registry::system_descriptor *
system_factory_registry::find_system (std::string_view name) const
{
  // 1. Display name (e.g. "Player Control System")
  if (std::unordered_map<std::string, system_descriptor>::const_iterator const
          it = m_factories.find (std::string (name));
      it != m_factories.end ()) {
    return &it->second;
  }

  // 2. Fully qualified C++ type name (e.g. "wsl::sys::PlayerControlSystem")
  if (std::unordered_map<std::string, std::string>::const_iterator const tn_it
      = m_type_name_to_display_name.find (std::string (name));
      tn_it != m_type_name_to_display_name.end ()
      && !m_ambiguous_type_names.contains (std::string (name))) {
    return find_system (tn_it->second);
  }

  // 3. Short name — last segment after "::" (e.g. "PlayerControlSystem")
  const system_descriptor *match = nullptr;
  for (const auto &entry : m_factories) {
    if (entry.second.type_name.empty ())
      continue;
    std::string_view const full = entry.second.type_name;
    std::size_t const pos = full.rfind ("::");
    std::string_view const short_name
        = (pos != std::string_view::npos) ? full.substr (pos + 2) : full;
    if (short_name == name) {
      if (match != nullptr) {
        return nullptr;
      }
      match = &entry.second;
    }
  }

  return match;
}

const system_factory_registry::system_descriptor *
system_factory_registry::find_system (entt::id_type type_id) const
{
  if (std::unordered_map<entt::id_type, std::string>::const_iterator const it
      = m_type_to_name.find (type_id);
      it != m_type_to_name.end ()) {
    return find_system (it->second);
  }

  return nullptr;
}

std::vector<const system_factory_registry::system_descriptor *>
system_factory_registry::get_systems (system_order order) const
{
  std::vector<const system_descriptor *> systems;
  systems.reserve (m_factories.size ());

  for (const std::pair<const std::string, system_descriptor> &entry :
       m_factories) {
    systems.push_back (&entry.second);
  }

  if (order == system_order::type_id) {
    std::sort (systems.begin (), systems.end (),
               [] (const system_descriptor *lhs, const system_descriptor *rhs) {
                 return lhs->type_id < rhs->type_id;
               });
    return systems;
  }

  std::sort (systems.begin (), systems.end (),
             [] (const system_descriptor *lhs, const system_descriptor *rhs) {
               if (lhs->display_name == rhs->display_name) {
                 return lhs->type_id < rhs->type_id;
               }
               return lhs->display_name < rhs->display_name;
             });
  return systems;
}

std::unique_ptr<sys::ecs_system>
system_factory_registry::create (const std::string &name, rsc::scene &scene)
{
  if (std::unordered_map<std::string, system_descriptor>::iterator it
      = m_factories.find (name);
      it != m_factories.end ()) {
    return it->second.factory (scene);
  }

  // The factory table is keyed by display name, but `find_system` also
  // accepts the type name and the short name. Resolve through it so every name
  // the lookup accepts can also be instantiated -- otherwise `sys add
  // orb_anim_system` resolves a descriptor and then silently creates nothing.
  if (const system_descriptor *desc = find_system (name);
      desc != nullptr && desc->display_name != name) {
    return create (desc->display_name, scene);
  }

  return nullptr;
}

std::vector<system_type_ref>
system_factory_registry::get_system_dependencies (
    entt::id_type system_type_id) const
{
  std::vector<system_type_ref> result;
  const system_descriptor *desc = find_system (system_type_id);
  if (desc == nullptr) {
    return result;
  }

  result.reserve (desc->dependencies.size ());
  for (entt::id_type dep_id : desc->dependencies) {
    if (const system_descriptor *dep_desc = find_system (dep_id)) {
      result.push_back (
          { dep_id, dep_desc->type_name, dep_desc->display_name });
    } else {
      result.push_back ({ dep_id, "Unknown", "Unknown" });
    }
  }

  return result;
}

std::vector<system_type_ref>
system_factory_registry::get_system_conflicts (
    entt::id_type system_type_id) const
{
  std::vector<system_type_ref> result;
  const system_descriptor *desc = find_system (system_type_id);
  if (desc == nullptr) {
    return result;
  }

  result.reserve (desc->conflicts.size ());
  for (entt::id_type conflict_id : desc->conflicts) {
    if (const system_descriptor *conflict_desc = find_system (conflict_id)) {
      result.push_back ({ conflict_id, conflict_desc->type_name,
                          conflict_desc->display_name });
    } else {
      result.push_back ({ conflict_id, "Unknown", "Unknown" });
    }
  }

  return result;
}

std::vector<const system_iteration_descriptor *>
system_factory_registry::get_system_iterations (
    entt::id_type system_type_id) const
{
  std::vector<const system_iteration_descriptor *> result;
  if (m_event_hub == nullptr || m_event_hub->db == nullptr) {
    return result;
  }

  for (const event::system_iteration_debug_entry &iteration :
       m_event_hub->db->system_iterations) {
    if (iteration.system_type_id == system_type_id) {
      result.push_back (&iteration);
    }
  }

  return result;
}

std::vector<const system_iteration_descriptor *>
system_factory_registry::find_iterations_using_world_component (
    entt::id_type component_type_id) const
{
  std::vector<const system_iteration_descriptor *> result;
  if (m_event_hub == nullptr || m_event_hub->db == nullptr) {
    return result;
  }

  for (const event::system_iteration_debug_entry &iteration :
       m_event_hub->db->system_iterations) {
    if (iteration.has_component (component_type_id)) {
      result.push_back (&iteration);
    }
  }

  return result;
}

void
system_factory_registry::clear_runtime_systems ()
{
  std::vector<std::string> runtime_names;
  runtime_names.reserve (m_factories.size ());

  for (const std::pair<const std::string, system_descriptor> &entry :
       m_factories) {
    const system_descriptor &desc = entry.second;
    if (desc.runtime_registered) {
      runtime_names.push_back (entry.first);
    }
  }

  for (const std::string &name : runtime_names) {
    m_factories.erase (name);
  }
  rebuild_type_name_index ();
}

std::vector<std::string>
system_factory_registry::get_system_factory_names () const
{
  std::vector<std::string> names;
  std::vector<const system_descriptor *> systems = get_systems ();
  names.reserve (systems.size ());
  for (const system_descriptor *desc : systems) {
    if (desc == nullptr) {
      continue;
    }
    names.push_back (desc->display_name);
  }
  return names;
}

} // namespace reg

} // namespace wsl
