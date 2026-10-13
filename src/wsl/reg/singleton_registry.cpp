#include "singleton_registry.hpp"

#include "runtime_storage.hpp"

#include "../rsc/scene.hpp"
#include "../rsc/world.hpp"

#include <algorithm>
#include <cstring>
#include <entt/core/fwd.hpp>
#include <entt/entity/fwd.hpp>
#include <entt/meta/factory.hpp>
#include <memory>
#include <rfl/json.hpp>
#include <unordered_map>
#include <utility>
#include <vector>

namespace wsl
{

namespace reg
{

namespace
{

constexpr ::entt::id_type ambiguous_singleton_name
    = static_cast<::entt::id_type> (-1);

das_singleton_storage &
storage_for (::entt::registry &registry)
{
  if (!registry.ctx ().contains<das_singleton_storage> ()) {
    registry.ctx ().emplace<das_singleton_storage> ();
  }
  return registry.ctx ().get<das_singleton_storage> ();
}

const das_singleton_storage *
try_storage_for (const ::entt::registry &registry)
{
  if (!registry.ctx ().contains<das_singleton_storage> ()) {
    return nullptr;
  }
  return &registry.ctx ().get<das_singleton_storage> ();
}

das_singleton_storage *
try_storage_for (::entt::registry &registry)
{
  if (!registry.ctx ().contains<das_singleton_storage> ()) {
    return nullptr;
  }
  return &registry.ctx ().get<das_singleton_storage> ();
}

std::string
block_to_hex (const das_singleton_storage::block *block)
{
  if ((block == nullptr) || (block->data () == nullptr) || (block->size == 0)) {
    return {};
  }

  static constexpr char digits[] = "0123456789abcdef";
  std::string hex;
  hex.reserve (block->size * 2);
  for (std::size_t i = 0; i < block->size; ++i) {
    hex.push_back (digits[(block->data ()[i] >> 4U) & 0x0FU]);
    hex.push_back (digits[block->data ()[i] & 0x0FU]);
  }
  return hex;
}

} // namespace

bool
das_singleton_storage::contains (::entt::id_type type_id) const
{
  return m_blocks.find (type_id) != m_blocks.end ();
}

das_singleton_storage::block *
das_singleton_storage::emplace_default (::entt::id_type type_id,
                                        const runtime_layout &layout)
{
  return emplace_with (type_id, layout);
}

das_singleton_storage::block *
das_singleton_storage::emplace_with (::entt::id_type type_id,
                                     const runtime_layout &layout)
{
  if (!layout.validation_error ().empty () || contains (type_id)) {
    return nullptr;
  }

  block b;
  b.layout = std::make_shared<runtime_layout> (layout);
  b.size = layout.size;
  b.words.assign ((b.size + sizeof (std::max_align_t) - 1)
                      / sizeof (std::max_align_t),
                  std::max_align_t{});
  std::fill (b.words.begin (), b.words.end (), std::max_align_t{});

  uint8_t *bytes = b.data ();
  for (const runtime_field &field : layout.fields) {
    if (field.default_value.empty ()) {
      continue;
    }
    std::memcpy (bytes + field.offset, field.default_value.data (), field.size);
  }

  return &m_blocks.emplace (type_id, std::move (b)).first->second;
}

das_singleton_storage::block *
das_singleton_storage::migrate (::entt::id_type type_id,
                               const runtime_layout &layout)
{
  if (!layout.validation_error ().empty ()) {
    return nullptr;
  }
  auto it = m_blocks.find (type_id);
  if (it == m_blocks.end ()) {
    return nullptr;
  }

  block &current = it->second;
  if (current.layout && current.layout->same_shape (layout)) {
    current.layout = std::make_shared<runtime_layout> (layout);
    return &current;
  }

  block migrated;
  migrated.layout = std::make_shared<runtime_layout> (layout);
  migrated.size = layout.size;
  migrated.words.assign ((migrated.size + sizeof (std::max_align_t) - 1)
                             / sizeof (std::max_align_t),
                         std::max_align_t{});
  std::fill (migrated.words.begin (), migrated.words.end (),
             std::max_align_t{});
  uint8_t *bytes = migrated.data ();
  for (const runtime_field &field : layout.fields) {
    if (!field.default_value.empty ()) {
      std::memcpy (bytes + field.offset, field.default_value.data (),
                   field.size);
    }
  }
  if (current.layout) {
    copy_matching_fields (*current.layout, current.data (), layout, bytes);
  }
  current = std::move (migrated);
  return &current;
}

das_singleton_storage::block *
das_singleton_storage::data (::entt::id_type type_id)
{
  const auto it = m_blocks.find (type_id);
  return it != m_blocks.end () ? &it->second : nullptr;
}

const das_singleton_storage::block *
das_singleton_storage::data (::entt::id_type type_id) const
{
  const auto it = m_blocks.find (type_id);
  return it != m_blocks.end () ? &it->second : nullptr;
}

bool
das_singleton_storage::remove (::entt::id_type type_id)
{
  return m_blocks.erase (type_id) != 0;
}

void
das_singleton_storage::clear ()
{
  m_blocks.clear ();
}

bool
singleton_registry::register_cached_runtime_singleton_component (
    ::entt::id_type type_id, std::string_view type_name,
    std::string_view display_name, int das_struct_size,
    std::vector<descriptor::das_field> das_fields)
{
  if (const descriptor *existing = find (type_id);
      existing != nullptr && existing->type_name != type_name) {
    wsl::log::xmake ()->error (
        "Daslang singleton id collision: '{}' and '{}' both hash to {}",
        existing->type_name, type_name, type_id);
    return false;
  }

  descriptor desc{};
  desc.type_id = type_id;
  desc.type_name = std::string (type_name);
  desc.display_name = display_name.empty ()
                          ? comp::humanize_identifier (type_name)
                          : std::string (display_name);
  desc.runtime_registered = true;
  desc.serialize_with_scene = true;

  // A daslang singleton with a known layout can be created, removed and
  // serialized. The value bytes live in das_singleton_storage, and the
  // descriptor's bare function-pointer callbacks receive no type id, so they
  // cannot be filled in here. `das_singleton_*` below is the access path
  // instead -- the same arrangement daslang components use in
  // component_registry. Without a recorded layout this stays
  // discovery-only, as before.
  if (das_struct_size > 0) {
    desc.is_das_singleton = true;
    desc.das_struct_size = das_struct_size;
    desc.das_fields = std::move (das_fields);
    std::vector<runtime_field> layout_fields;
    layout_fields.reserve (desc.das_fields.size ());
    for (const auto &field : desc.das_fields) {
      layout_fields.push_back ({
          .name = field.name,
          .type_name = field.type_name,
          .offset = static_cast<std::uint32_t> (field.offset),
          .size = static_cast<std::uint32_t> (field.size),
          .kind = field.kind,
          .default_value = field.default_value,
      });
    }
    auto layout = std::make_shared<runtime_layout> (runtime_layout::make (
        static_cast<std::uint32_t> (das_struct_size), std::move (layout_fields)));
    const std::string reason = layout->validation_error ();
    desc.can_add_default = reason.empty ();
    desc.das_layout = std::move (layout);
    if (!desc.can_add_default) {
      wsl::log::xmake ()->error ("Daslang singleton '{}' is not usable: {}",
                                 type_name, reason);
    }
  }

  const auto index_name = [type_id] (auto &index, const std::string &name) {
    auto [it, inserted] = index.try_emplace (name, type_id);
    if (!inserted && it->second != type_id) {
      it->second = ambiguous_singleton_name;
    }
  };
  index_name (m_type_name_to_type_id, desc.type_name);
  index_name (m_display_name_to_type_id, desc.display_name);
  m_descriptors[type_id] = std::move (desc);
  return true;
}

bool
singleton_registry::das_singleton_contains (const ::entt::registry &registry,
                                            ::entt::id_type type_id) const
{
  const das_singleton_storage *storage = try_storage_for (registry);
  const descriptor *desc = find_singleton_component (type_id);
  return storage != nullptr && desc != nullptr && desc->is_das_singleton
         && desc->can_add_default && storage->contains (type_id);
}

bool
singleton_registry::das_singleton_add (::entt::registry &registry,
                                       ::entt::id_type type_id) const
{
  const descriptor *desc = find_singleton_component (type_id);
  if ((desc == nullptr) || !desc->is_das_singleton
      || !desc->can_add_default
      || (desc->das_layout == nullptr)
      || (desc->das_struct_size <= 0)
      || das_singleton_contains (registry, type_id)) {
    return false;
  }
  return storage_for (registry).emplace_with (type_id, *desc->das_layout)
         != nullptr;
}

bool
singleton_registry::das_singleton_remove (::entt::registry &registry,
                                          ::entt::id_type type_id) const
{
  const descriptor *desc = find_singleton_component (type_id);
  if ((desc == nullptr) || !desc->is_das_singleton) {
    return false;
  }
  if (!registry.ctx ().contains<das_singleton_storage> ()) {
    return false;
  }
  return registry.ctx ().get<das_singleton_storage> ().remove (type_id);
}

uint8_t *
singleton_registry::das_singleton_data (::entt::registry &registry,
                                        ::entt::id_type type_id) const
{
  const descriptor *desc = find_singleton_component (type_id);
  if (desc == nullptr || !desc->is_das_singleton || !desc->can_add_default
      || desc->das_layout == nullptr) {
    return nullptr;
  }
  das_singleton_storage *storage = try_storage_for (registry);
  if (storage == nullptr) {
    return nullptr;
  }
  das_singleton_storage::block *block
      = storage->migrate (type_id, *desc->das_layout);
  return block != nullptr ? block->data () : nullptr;
}

std::string
singleton_registry::das_singleton_hex (::entt::registry &registry,
                                       ::entt::id_type type_id) const
{
  const descriptor *desc = find_singleton_component (type_id);
  if (desc == nullptr || !desc->is_das_singleton || !desc->can_add_default
      || desc->das_layout == nullptr) {
    return {};
  }
  das_singleton_storage *storage = try_storage_for (registry);
  if (storage == nullptr) {
    return {};
  }
  return block_to_hex (storage->migrate (type_id, *desc->das_layout));
}

std::string
singleton_registry::das_singleton_hex (const ::entt::registry &registry,
                                       ::entt::id_type type_id) const
{
  const descriptor *desc = find_singleton_component (type_id);
  if (desc == nullptr || !desc->is_das_singleton || !desc->can_add_default
      || desc->das_layout == nullptr) {
    return {};
  }
  const das_singleton_storage *storage = try_storage_for (registry);
  if (storage == nullptr) {
    return {};
  }
  const das_singleton_storage::block *block = storage->data (type_id);
  if ((block == nullptr) || (block->data () == nullptr) || (block->size == 0)
      || (block->layout == nullptr)
      || !block->layout->same_shape (*desc->das_layout)
      || (block->size != desc->das_layout->size)) {
    return {};
  }

  return block_to_hex (block);
}

std::string
singleton_registry::das_singleton_json (::entt::registry &registry,
                                        ::entt::id_type type_id) const
{
  const descriptor *desc = find_singleton_component (type_id);
  if (desc == nullptr || !desc->is_das_singleton || !desc->can_add_default
      || desc->das_layout == nullptr) {
    return "{}";
  }
  das_singleton_storage *storage = try_storage_for (registry);
  if (storage == nullptr) {
    return "{}";
  }
  das_singleton_storage::block *block
      = storage->migrate (type_id, *desc->das_layout);
  if (block == nullptr || block->data () == nullptr) {
    return "{}";
  }
  rfl::Generic::Object fields;
  for (const runtime_field &field : desc->das_layout->fields) {
    fields.insert (field.name, encode_runtime_field (field, block->data ()));
  }
  return rfl::json::write (rfl::Generic (std::move (fields)));
}

std::string
singleton_registry::das_singleton_json (const ::entt::registry &registry,
                                        ::entt::id_type type_id) const
{
  return das_singleton_json (const_cast<::entt::registry &> (registry), type_id);
}

bool
singleton_registry::das_singleton_load_json (::entt::registry &registry,
                                             ::entt::id_type type_id,
                                             const std::string &json_str)
{
  const descriptor *desc = find_singleton_component (type_id);
  if (desc == nullptr || !desc->is_das_singleton || !desc->can_add_default
      || desc->das_layout == nullptr) {
    return false;
  }
  auto parsed = rfl::json::read<rfl::Generic> (json_str);
  if (!parsed) {
    return false;
  }
  const auto *fields
      = std::get_if<rfl::Generic::Object> (&parsed.value ().value ());
  if (fields == nullptr) {
    return false;
  }
  das_singleton_storage &storage = storage_for (registry);
  if (!storage.contains (type_id)
      && (storage.emplace_default (type_id, *desc->das_layout) == nullptr)) {
    return false;
  }
  das_singleton_storage::block *block
      = storage.migrate (type_id, *desc->das_layout);
  if (block == nullptr || block->data () == nullptr) {
    return false;
  }
  for (const auto &[name, value] : *fields) {
    const runtime_field *field = find_runtime_field (*desc->das_layout, name);
    if (field != nullptr) {
      decode_runtime_field (*field, value, block->data ());
    }
  }
  return true;
}

bool
singleton_registry::das_singleton_load_hex (::entt::registry &registry,
                                            ::entt::id_type type_id,
                                            std::string_view hex) const
{
  const descriptor *desc = find_singleton_component (type_id);
  if ((desc == nullptr) || !desc->is_das_singleton
      || !desc->can_add_default
      || (desc->das_layout == nullptr)
      || (desc->das_struct_size <= 0)) {
    return false;
  }

  const auto nibble = [] (char c) -> int {
    if ((c >= '0') && (c <= '9')) {
      return c - '0';
    }
    if ((c >= 'a') && (c <= 'f')) {
      return 10 + (c - 'a');
    }
    if ((c >= 'A') && (c <= 'F')) {
      return 10 + (c - 'A');
    }
    return -1;
  };

  const std::size_t expected = static_cast<std::size_t> (desc->das_struct_size);
  if (hex.size () != expected * 2) {
    return false;
  }

  std::vector<uint8_t> decoded (expected);
  for (std::size_t i = 0; i < expected; ++i) {
    const int hi = nibble (hex[i * 2]);
    const int lo = nibble (hex[i * 2 + 1]);
    if ((hi < 0) || (lo < 0)) {
      return false;
    }
    decoded[i] = static_cast<uint8_t> ((hi << 4) | lo);
  }

  das_singleton_storage &storage = storage_for (registry);
  if (!storage.contains (type_id)
      && (storage.emplace_default (type_id, *desc->das_layout) == nullptr)) {
    return false;
  }
  das_singleton_storage::block *block
      = storage.migrate (type_id, *desc->das_layout);
  if (block == nullptr || block->data () == nullptr || block->size != expected) {
    return false;
  }
  std::memcpy (block->data (), decoded.data (), expected);
  return true;
}

const singleton_registry::descriptor *
singleton_registry::find_singleton_component (::entt::id_type type_id) const
{
  if (std::unordered_map<::entt::id_type, descriptor>::const_iterator const it
      = m_descriptors.find (type_id);
      it != m_descriptors.end ()) {
    return &it->second;
  }

  return nullptr;
}

const singleton_registry::descriptor *
singleton_registry::find_singleton_component (std::string_view type_name) const
{
  // 1. Fully qualified C++ type name (e.g. "wsl::comp::singl::physics_manager")
  if (std::unordered_map<std::string, ::entt::id_type>::const_iterator const it
      = m_type_name_to_type_id.find (std::string (type_name));
      it != m_type_name_to_type_id.end ()
      && it->second != ambiguous_singleton_name) {
    return find_singleton_component (it->second);
  }

  // 2. Display name (e.g. "Physics Manager", "Score")
  if (std::unordered_map<std::string, ::entt::id_type>::const_iterator const dit
      = m_display_name_to_type_id.find (std::string (type_name));
      dit != m_display_name_to_type_id.end ()
      && dit->second != ambiguous_singleton_name) {
    return find_singleton_component (dit->second);
  }

  // 3. Short name — last segment after "::" (e.g. "physics_manager", "score")
  const descriptor *match = nullptr;
  for (const auto &entry : m_descriptors) {
    std::string_view const full = entry.second.type_name;
    std::size_t const pos = full.rfind ("::");
    std::string_view const short_name
        = (pos != std::string_view::npos) ? full.substr (pos + 2) : full;
    if (short_name == type_name) {
      if (match != nullptr) {
        return nullptr;
      }
      match = &entry.second;
    }
  }

  return match;
}

std::vector<const singleton_registry::descriptor *>
singleton_registry::get_singleton_components (
    singleton_component_order order) const
{
  std::vector<const descriptor *> out;
  out.reserve (m_descriptors.size ());

  for (const auto &entry : m_descriptors) {
    out.push_back (&entry.second);
  }

  if (order == singleton_component_order::type_id) {
    detail::sort_by_type_id (out);
  } else {
    detail::sort_by_display_name (out);
  }

  return out;
}

void
singleton_registry::apply_core_singleton_components (
    ::entt::registry &registry) const
{
  for (const descriptor *desc :
       get_singleton_components (singleton_component_order::type_id)) {
    if ((desc == nullptr) || !desc->core
        || (desc->emplace_default == nullptr)) {
      continue;
    }

    desc->emplace_default (registry);
  }
}

void
singleton_registry::reset_scene_singleton_components (
    ::entt::registry &registry) const
{
  for (const descriptor *desc :
       get_singleton_components (singleton_component_order::type_id)) {
    if (desc == nullptr) {
      continue;
    }

    if (desc->core) {
      if (desc->emplace_default != nullptr) {
        desc->emplace_default (registry);
      }
      continue;
    }

    if (desc->remove != nullptr) {
      desc->remove (registry);
    }
  }
}

void
singleton_registry::clear_runtime_singleton_components (rsc::world &world)
{
  std::vector<::entt::id_type> runtime_ids;
  runtime_ids.reserve (m_descriptors.size ());

  for (const std::pair<const ::entt::id_type, descriptor> &entry :
       m_descriptors) {
    if (entry.second.runtime_registered) {
      runtime_ids.push_back (entry.first);
    }
  }

  for (std::unique_ptr<rsc::scene> &scene_ptr : world.get_scenes ()) {
    if (!scene_ptr) {
      continue;
    }

    ::entt::registry &registry = scene_ptr->get_registry ();
    for (::entt::id_type const type_id : runtime_ids) {
      if (const descriptor *desc = find (type_id);
          (desc != nullptr) && (desc->remove != nullptr) && !desc->core) {
        desc->remove (registry);
      }
    }
  }

  for (::entt::id_type const type_id : runtime_ids) {
    ::entt::meta_reset (type_id);
    if (std::unordered_map<::entt::id_type, descriptor>::const_iterator const it
        = m_descriptors.find (type_id);
        it != m_descriptors.end ()) {
      m_type_name_to_type_id.erase (it->second.type_name);
      m_display_name_to_type_id.erase (it->second.display_name);
    }
    m_descriptors.erase (type_id);
  }

  m_type_name_to_type_id.clear ();
  m_display_name_to_type_id.clear ();
  for (const auto &[type_id, desc] : m_descriptors) {
    const auto index_name = [type_id] (auto &index, const std::string &name) {
      auto [it, inserted] = index.try_emplace (name, type_id);
      if (!inserted && it->second != type_id) {
        it->second = ambiguous_singleton_name;
      }
    };
    index_name (m_type_name_to_type_id, desc.type_name);
    index_name (m_display_name_to_type_id, desc.display_name);
  }
}

bool
singleton_registry::save_singleton_binary (serialize::binary_writer &writer,
                                           ::entt::registry &registry,
                                           ::entt::id_type type_id) const
{
  const descriptor *desc = find_singleton_component (type_id);
  if ((desc == nullptr) || (desc->save_binary == nullptr)) {
    return false;
  }

  desc->save_binary (writer, registry);
  return true;
}

bool
singleton_registry::load_singleton_binary (serialize::binary_reader &reader,
                                           ::entt::registry &registry,
                                           ::entt::id_type type_id) const
{
  const descriptor *desc = find_singleton_component (type_id);
  if ((desc == nullptr) || (desc->load_binary == nullptr)) {
    return false;
  }

  desc->load_binary (reader, registry);
  return true;
}

bool
singleton_registry::save_singleton_json (serialize::json_writer &writer,
                                         ::entt::registry &registry,
                                         ::entt::id_type type_id) const
{
  const descriptor *desc = find_singleton_component (type_id);
  if ((desc == nullptr) || (desc->save_json == nullptr)) {
    return false;
  }

  desc->save_json (writer, registry);
  return true;
}

bool
singleton_registry::load_singleton_json (serialize::json_reader &reader,
                                         ::entt::registry &registry,
                                         ::entt::id_type type_id) const
{
  const descriptor *desc = find_singleton_component (type_id);
  if ((desc == nullptr) || (desc->load_json == nullptr)) {
    return false;
  }

  desc->load_json (reader, registry);
  return true;
}

} // namespace reg

} // namespace wsl
