#include "component_registry.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include <entt/meta/factory.hpp>
#include <rfl/Generic.hpp>

#include "../log/log.hpp"

namespace wsl
{

namespace reg
{

namespace
{

using das_kind = wsl::das::das_engine::field_type_kind;

/** Marks a short name shared by more than one component. */
constexpr entt::id_type ambiguous_name
    = std::numeric_limits<entt::id_type>::max ();

/**
 * One Daslang component on the wire. Fields are stored by name, so a change of
 * struct layout between save and load neither corrupts nor silently drops data
 * that still has a counterpart.
 */
struct das_component_document
{
  std::string type;
  std::uint32_t type_id = 0;
  std::uint32_t entity = 0;
  rfl::Generic fields;
};

das_component_document
encode_document (const std::string &type_name, entt::id_type type_id,
                 entt::entity entity, const runtime_layout &layout,
                 const std::uint8_t *block)
{
  das_component_document doc;
  doc.type = type_name;
  doc.type_id = static_cast<std::uint32_t> (type_id);
  doc.entity = static_cast<std::uint32_t> (entt::to_integral (entity));

  rfl::Generic::Object fields;
  for (const runtime_field &field : layout.fields) {
    fields.insert (field.name, encode_runtime_field (field, block));
  }
  doc.fields = rfl::Generic (std::move (fields));
  return doc;
}

std::vector<das_component_document>
collect_documents (const component_registry &components,
                   const entt::registry &registry)
{
  std::vector<das_component_document> docs;
  const runtime_storage *storage = try_runtime_storage_of (registry);
  if (storage == nullptr) {
    return docs;
  }

  for (const component_registry::descriptor *desc :
       components.get_world_components (world_component_order::type_id)) {
    if (!desc->is_das_component || !desc->das_supported) {
      continue;
    }
    const runtime_pool *pool = storage->find (desc->type_id);
    if (pool == nullptr) {
      continue;
    }
    for (entt::entity entity : pool->entities ()) {
      const std::size_t index = pool->entities ().index (entity);
      docs.push_back (encode_document (desc->type_name, desc->type_id, entity,
                                       pool->layout (), pool->block (index)));
    }
  }
  return docs;
}

void
apply_documents (const component_registry &components,
                 const std::vector<das_component_document> &docs,
                 entt::registry &registry)
{
  std::unordered_map<std::string, std::size_t> unknown_types;
  std::unordered_map<std::string, std::size_t> unsupported_types;
  std::unordered_map<std::string, std::size_t> bad_values;
  std::size_t invalid_entities = 0;

  for (const das_component_document &doc : docs) {
    const component_registry::descriptor *desc
        = components.find_world_component (std::string_view (doc.type));
    if (desc == nullptr) {
      desc = components.find_world_component (
          static_cast<entt::id_type> (doc.type_id));
    }
    if (desc == nullptr || !desc->is_das_component) {
      ++unknown_types[doc.type];
      continue;
    }
    if (!desc->das_supported) {
      ++unsupported_types[doc.type];
      continue;
    }

    const auto entity = static_cast<entt::entity> (doc.entity);
    if (!registry.valid (entity)) {
      ++invalid_entities;
      continue;
    }

    runtime_pool &pool = runtime_storage_of (registry).acquire (
        desc->type_id, desc->das_layout);
    if (!pool.contains (entity)) {
      pool.add (entity);
    }
    std::uint8_t *block = pool.data (entity);

    const auto *fields
        = std::get_if<rfl::Generic::Object> (&doc.fields.value ());
    if (fields == nullptr) {
      ++bad_values[doc.type];
      continue;
    }
    for (const auto &[name, value] : *fields) {
      const runtime_field *field = find_runtime_field (pool.layout (), name);
      if (field == nullptr) {
        continue; // Field removed from the script since the save.
      }
      if (!decode_runtime_field (*field, value, block)) {
        ++bad_values[doc.type];
      }
    }
  }

  for (const auto &[type, count] : unknown_types) {
    wsl::log::rsc ()->warn (
        "Daslang component '{}' is not registered; {} instance(s) were not "
        "loaded",
        type, count);
  }
  for (const auto &[type, count] : unsupported_types) {
    wsl::log::rsc ()->warn (
        "Daslang component '{}' cannot be stored; {} instance(s) were not "
        "loaded",
        type, count);
  }
  for (const auto &[type, count] : bad_values) {
    wsl::log::rsc ()->warn ("Daslang component '{}': {} field value(s) did "
                            "not match the struct and kept their defaults",
                            type, count);
  }
  if (invalid_entities > 0) {
    wsl::log::rsc ()->warn (
        "{} Daslang component(s) referenced entities that do not exist",
        invalid_entities);
  }
}

} // namespace

// ── Name index ──

void
component_registry::rebuild_name_index ()
{
  m_type_name_to_stable.clear ();
  m_display_name_to_stable.clear ();
  m_short_name_to_stable.clear ();

  const auto insert_unique = [] (auto &index, const std::string &name,
                                 entt::id_type id) {
    auto [it, inserted] = index.try_emplace (name, id);
    if (!inserted && it->second != id) {
      it->second = ambiguous_name;
    }
  };

  for (const auto &[id, desc] : m_descriptors) {
    if (!desc.type_name.empty ()) {
      insert_unique (m_type_name_to_stable, desc.type_name, id);
    }
    if (!desc.display_name.empty ()) {
      insert_unique (m_display_name_to_stable, desc.display_name, id);
    }

    const std::string_view full = desc.type_name;
    const std::size_t pos = full.rfind ("::");
    const std::string_view short_name
        = pos == std::string_view::npos ? full : full.substr (pos + 2);
    if (short_name.empty ()) {
      continue;
    }
    auto [it, inserted]
        = m_short_name_to_stable.try_emplace (std::string (short_name), id);
    if (!inserted && it->second != id) {
      it->second = ambiguous_name;
    }
  }
}

// ── Lookup ──

const component_registry::descriptor *
component_registry::find_world_component (entt::id_type type_id) const
{
  if (auto it = m_descriptors.find (type_id); it != m_descriptors.end ()) {
    return &it->second;
  }
  return nullptr;
}

const component_registry::descriptor *
component_registry::find_world_component (std::string_view name) const
{
  if (auto it = m_type_name_to_stable.find (name);
      it != m_type_name_to_stable.end () && it->second != ambiguous_name) {
    return find_world_component (it->second);
  }
  if (auto it = m_display_name_to_stable.find (name);
      it != m_display_name_to_stable.end () && it->second != ambiguous_name) {
    return find_world_component (it->second);
  }
  if (auto it = m_short_name_to_stable.find (name);
      it != m_short_name_to_stable.end () && it->second != ambiguous_name) {
    return find_world_component (it->second);
  }
  return nullptr;
}

bool
component_registry::contains_world_component (entt::id_type type_id) const
{
  return find_world_component (type_id) != nullptr;
}

std::vector<const component_registry::descriptor *>
component_registry::get_world_components (world_component_order order) const
{
  std::vector<const descriptor *> out;
  out.reserve (m_descriptors.size ());

  for (const auto &entry : m_descriptors) {
    out.push_back (&entry.second);
  }

  if (order == world_component_order::type_id) {
    detail::sort_by_type_id (out);
  } else {
    detail::sort_by_display_name (out);
  }

  return out;
}

std::vector<const component_registry::descriptor *>
component_registry::get_addable_world_components (entt::registry &registry,
                                                  entt::entity entity) const
{
  std::vector<const descriptor *> out;
  if (!registry.valid (entity)) {
    return out;
  }

  for (const descriptor *desc : get_world_components ()) {
    if (desc == nullptr || !desc->can_add_default) {
      continue;
    }
    if (desc->is_das_component) {
      if (das_component_contains (registry, desc->type_id, entity)) {
        continue;
      }
    } else if (desc->contains == nullptr || desc->contains (registry, entity)) {
      continue;
    }

    out.push_back (desc);
  }

  return out;
}

// ── Native components ──

bool
component_registry::copy_world_component (entt::registry &src_registry,
                                          entt::entity src_entity,
                                          entt::registry &dst_registry,
                                          entt::entity dst_entity,
                                          entt::id_type component_type_id) const
{
  const descriptor *desc = find_world_component (component_type_id);
  if ((desc == nullptr) || desc->is_das_component || (desc->copy == nullptr)) {
    return false;
  }

  desc->copy (src_registry, src_entity, dst_registry, dst_entity);
  return true;
}

bool
component_registry::save_world_component_binary (
    serialize::binary_writer &writer, entt::registry &registry,
    entt::id_type component_type_id) const
{
  const descriptor *desc = find_world_component (component_type_id);
  if ((desc == nullptr) || desc->is_das_component
      || (desc->save_binary == nullptr)) {
    return false;
  }

  desc->save_binary (writer, registry);
  return true;
}

bool
component_registry::load_world_component_binary (
    serialize::binary_reader &reader, entt::registry &registry,
    entt::id_type component_type_id) const
{
  const descriptor *desc = find_world_component (component_type_id);
  if ((desc == nullptr) || desc->is_das_component
      || (desc->load_binary == nullptr)) {
    return false;
  }

  desc->load_binary (reader, registry);
  return true;
}

bool
component_registry::save_world_component_json (
    serialize::json_writer &writer, entt::registry &registry,
    entt::id_type component_type_id) const
{
  const descriptor *desc = find_world_component (component_type_id);
  if ((desc == nullptr) || desc->is_das_component
      || (desc->save_json == nullptr)) {
    return false;
  }

  desc->save_json (writer, registry);
  return true;
}

bool
component_registry::load_world_component_json (
    serialize::json_reader &reader, entt::registry &registry,
    entt::id_type component_type_id) const
{
  const descriptor *desc = find_world_component (component_type_id);
  if ((desc == nullptr) || desc->is_das_component
      || (desc->load_json == nullptr)) {
    return false;
  }

  desc->load_json (reader, registry);
  return true;
}

// ── Daslang components: registration ──

bool
component_registry::register_runtime_world_component (
    entt::id_type type_id, std::string_view type_name,
    std::string_view display_name, runtime_layout layout)
{
  if (const descriptor *existing = find_world_component (type_id)) {
    if (!existing->is_das_component || existing->type_name != type_name) {
      wsl::log::xmake ()->error (
          "Daslang component id collision: '{}' and '{}' both hash to {}",
          existing->type_name, type_name, type_id);
      return false;
    }
  }

  descriptor desc{};
  desc.type_id = type_id;
  desc.type_name = std::string (type_name);
  desc.display_name = display_name.empty ()
                          ? comp::humanize_identifier (type_name)
                          : std::string (display_name);
  desc.runtime_registered = true;
  desc.is_das_component = true;
  desc.das_layout = std::move (layout);
  desc.das_struct_size = static_cast<int> (desc.das_layout.size);
  desc.das_fields = desc.das_layout.fields;

  const std::string reason = desc.das_layout.validation_error ();
  desc.das_supported = reason.empty ();
  desc.das_reject_reason = reason;
  desc.can_add_default = desc.das_supported;

  if (!desc.das_supported) {
    wsl::log::xmake ()->error ("Daslang component '{}' is not usable: {}",
                               type_name, reason);
  }

  const std::uint64_t size = desc.das_layout.size;
  m_descriptors[type_id] = std::move (desc);
  rebuild_name_index ();
  register_component_type_info (std::string (type_name), type_id,
                                ComponentKind::DAS_SCRIPT,
                                static_cast<size_t> (size));
  return true;
}

bool
component_registry::register_cached_runtime_world_component (
    entt::id_type type_id, std::string_view type_name,
    std::string_view display_name, int struct_size,
    std::vector<descriptor::das_field> fields)
{
  const auto size = struct_size > 0 ? static_cast<std::uint32_t> (struct_size)
                                    : 0U;
  auto layout = runtime_layout::make (size, std::move (fields));
  return register_runtime_world_component (type_id, type_name, display_name,
                                           std::move (layout));
}

void
component_registry::clear_runtime_world_components ()
{
  std::unordered_set<entt::id_type> runtime_ids;
  for (const auto &[id, desc] : m_descriptors) {
    if (desc.runtime_registered) {
      runtime_ids.insert (id);
    }
  }

  for (entt::id_type id : runtime_ids) {
    entt::meta_reset (id);
    m_descriptors.erase (id);
  }

  for (auto it = m_type_info_by_name.begin ();
       it != m_type_info_by_name.end ();) {
    if (runtime_ids.contains (
            static_cast<entt::id_type> (it->second.type_id))) {
      m_type_id_to_name.erase (it->second.type_id);
      it = m_type_info_by_name.erase (it);
    } else {
      ++it;
    }
  }

  rebuild_name_index ();
}

// ── Daslang components: storage ──

runtime_pool *
component_registry::sync_das_pool (entt::registry &registry,
                                   const descriptor &desc) const
{
  runtime_storage *storage = try_runtime_storage_of (registry);
  if (storage == nullptr) {
    return nullptr;
  }
  runtime_pool *pool = storage->find (desc.type_id);
  if (pool != nullptr) {
    sync_pool_layout (*pool, desc.das_layout);
  }
  return pool;
}

bool
component_registry::das_component_contains (const entt::registry &registry,
                                            entt::id_type type_id,
                                            entt::entity entity) const
{
  const runtime_storage *storage = try_runtime_storage_of (registry);
  if (storage == nullptr) {
    return false;
  }
  const runtime_pool *pool = storage->find (type_id);
  return pool != nullptr && pool->contains (entity);
}

bool
component_registry::das_component_add (entt::registry &registry,
                                       entt::id_type type_id,
                                       entt::entity entity)
{
  const descriptor *desc = find_world_component (type_id);
  if (desc == nullptr || !desc->is_das_component || !desc->das_supported
      || desc->das_layout.size == 0) {
    return false;
  }

  runtime_pool &pool
      = runtime_storage_of (registry).acquire (type_id, desc->das_layout);
  return pool.add (entity);
}

bool
component_registry::das_component_remove (entt::registry &registry,
                                          entt::id_type type_id,
                                          entt::entity entity)
{
  runtime_storage *storage = try_runtime_storage_of (registry);
  if (storage == nullptr) {
    return false;
  }
  runtime_pool *pool = storage->find (type_id);
  return pool != nullptr && pool->remove (entity);
}

uint8_t *
component_registry::das_component_data (entt::registry &registry,
                                        entt::id_type type_id,
                                        entt::entity entity)
{
  const descriptor *desc = find_world_component (type_id);
  if (desc == nullptr) {
    return nullptr;
  }
  runtime_pool *pool = sync_das_pool (registry, *desc);
  return pool == nullptr ? nullptr : pool->data (entity);
}

const uint8_t *
component_registry::das_component_data (const entt::registry &registry,
                                        entt::id_type type_id,
                                        entt::entity entity) const
{
  const runtime_storage *storage = try_runtime_storage_of (registry);
  if (storage == nullptr) {
    return nullptr;
  }
  const runtime_pool *pool = storage->find (type_id);
  return pool == nullptr ? nullptr : pool->data (entity);
}

runtime_pool *
component_registry::das_component_pool (entt::registry &registry,
                                        entt::id_type type_id) const
{
  const descriptor *desc = find_world_component (type_id);
  if (desc == nullptr || !desc->is_das_component) {
    return nullptr;
  }
  return sync_das_pool (registry, *desc);
}

void
component_registry::migrate_das_component_pools (entt::registry &registry) const
{
  runtime_storage *storage = try_runtime_storage_of (registry);
  if (storage == nullptr) {
    return;
  }
  storage->for_each_pool ([this] (entt::id_type type_id, runtime_pool &pool) {
    const descriptor *desc = find_world_component (type_id);
    if (desc != nullptr && desc->is_das_component && desc->das_supported) {
      sync_pool_layout (pool, desc->das_layout);
    }
  });
}

void
component_registry::clear_das_component_storage (entt::registry &registry) const
{
  if (runtime_storage *storage = try_runtime_storage_of (registry)) {
    storage->clear ();
  }
}

void
component_registry::clear_das_component_entity (entt::registry &registry,
                                                entt::entity entity) const
{
  if (runtime_storage *storage = try_runtime_storage_of (registry)) {
    storage->remove_entity (entity);
  }
}

// ── Daslang components: persistence ──

void
component_registry::save_das_components_json (
    serialize::json_writer &writer, const entt::registry &registry) const
{
  writer.write (collect_documents (*this, registry));
}

void
component_registry::load_das_components_json (serialize::json_reader &reader,
                                              entt::registry &registry)
{
  std::vector<das_component_document> docs;
  std::string error;
  if (!reader.read (docs, &error)) {
    wsl::log::rsc ()->warn ("Daslang components could not be read: {}", error);
    return;
  }
  apply_documents (*this, docs, registry);
}

void
component_registry::save_das_components_binary (
    serialize::binary_writer &writer, const entt::registry &registry) const
{
  writer.write (collect_documents (*this, registry));
}

void
component_registry::load_das_components_binary (
    serialize::binary_reader &reader, entt::registry &registry)
{
  std::vector<das_component_document> docs;
  std::string error;
  if (!reader.read (docs, &error)) {
    wsl::log::rsc ()->warn ("Daslang components could not be read: {}", error);
    return;
  }
  apply_documents (*this, docs, registry);
}

// ── Type info table ──

void
component_registry::register_component_type_info (
    const std::string &das_type_name, uint64_t type_id, ComponentKind kind,
    size_t struct_size)
{
  m_type_info_by_name[das_type_name] = ComponentTypeInfo{
    .type_id = type_id, .kind = kind, .struct_size = struct_size
  };
  m_type_id_to_name[type_id] = das_type_name;
}

const ComponentTypeInfo *
component_registry::find_component_type_info (
    std::string_view das_type_name) const
{
  if (auto it = m_type_info_by_name.find (das_type_name);
      it != m_type_info_by_name.end ()) {
    return &it->second;
  }
  return nullptr;
}

const ComponentTypeInfo *
component_registry::find_component_type_info_by_id (uint64_t type_id) const
{
  if (auto it = m_type_id_to_name.find (type_id);
      it != m_type_id_to_name.end ()) {
    return find_component_type_info (it->second);
  }
  return nullptr;
}

} // namespace reg

} // namespace wsl
