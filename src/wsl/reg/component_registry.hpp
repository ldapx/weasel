#pragma once

#include "../comp/component_meta.hpp"
#include "../das/das_engine.hpp"
#include "../serialize/types.hpp"
#include "../serialize/component_adapters.hpp"

#include "detail/registry_helpers.hpp"
#include "runtime_storage.hpp"
#include "wsl/log/log.hpp"

#ifndef IN_MODULE_INTERFACE
#include <entt/entt.hpp>
#endif
#ifndef IN_MODULE_INTERFACE
#include <entt/core/type_info.hpp>
#endif

#ifndef IN_MODULE_INTERFACE
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>
#endif

namespace wsl
{

namespace reg
{

/** Component type classification for generic dispatch. */
enum class ComponentKind
{
  /** C++ native component (engine-built, stored in EnTT typed storage). */
  CPP_NATIVE,
  /** daScript component (no C++ backing type, runtime-typed storage). */
  DAS_SCRIPT
};

/** Type-erased metadata for a registered component type. */
struct ComponentTypeInfo
{
  /** Stable type identifier. */
  uint64_t type_id = 0;
  /** Component storage kind. */
  ComponentKind kind = ComponentKind::DAS_SCRIPT;
  /** Size of the component struct in bytes. */
  size_t struct_size = 0;
};

/** Ordering modes for world component descriptor queries. */
enum class world_component_order
{
  /** Sort by display name, falling back to the C++ type name. */
  display_name,

  /** Sort by stable type identifier. */
  type_id
};

/** Registration options for entity-owned world components. */
struct world_component_registration_options
{
  /** Optional editor-facing display name override. */
  std::string_view display_name;

  /** Whether the registration was supplied by runtime project code. */
  bool runtime_registered = false;
};

/**
 * Central registry for component types in the engine.
 *
 * Native (C++) components are EnTT typed storages driven by function pointers
 * held in each descriptor. Daslang components have no C++ type: their blocks
 * live in the registry-local runtime storage and are described by a layout.
 * Every component has exactly one identifier, its stable type id, which is the
 * EnTT type hash of the C++ type.
 */
class component_registry
{
public:
  /** Describes a registered component type. */
  struct descriptor
  {
    /** One field of a Daslang component. */
    using das_field = runtime_field;

    /** Stable type identifier. */
    entt::id_type type_id{};
    /** Full type name as provided by the compiler, or the script name. */
    std::string type_name;
    /** Human-readable name for the component. */
    std::string display_name;
    /** Whether the component was registered at runtime. */
    bool runtime_registered = false;
    /** Whether the component can be default-constructed and added to an entity.
     */
    bool can_add_default = false;
    /** Whether this is a daScript component (no C++ backing type). */
    bool is_das_component = false;
    /**
     * Whether the component can be stored and persisted. A Daslang component
     * with fields that hold managed memory (strings, arrays, handles) is
     * registered for listing but marked unsupported, with the reason in
     * das_reject_reason.
     */
    bool das_supported = true;
    std::string das_reject_reason;
    /** Layout of a Daslang component: size, fields and default values. */
    runtime_layout das_layout;
    /** Compatibility view used by the CLI, inspector, and cached projects. */
    int das_struct_size = 0;
    std::vector<das_field> das_fields;

    /** Checks if the component exists on the given entity. */
    bool (*contains) (entt::registry &, entt::entity) = nullptr;
    /** Emplaces a default instance of the component on the given entity. */
    bool (*emplace_default) (entt::registry &, entt::entity) = nullptr;
    /** Removes the component from the given entity. */
    bool (*remove) (entt::registry &, entt::entity) = nullptr;
    /** Copies the component from a source entity to a destination entity. */
    void (*copy) (entt::registry &src_reg, entt::entity src_ent,
                  entt::registry &dst_reg, entt::entity dst_ent) = nullptr;
    /** Saves component data to a binary writer. */
    void (*save_binary) (serialize::binary_writer &, entt::registry &)
        = nullptr;
    /** Loads component data from a binary reader. */
    void (*load_binary) (serialize::binary_reader &, entt::registry &)
        = nullptr;
    /** Saves component data to a JSON writer. */
    void (*save_json) (serialize::json_writer &, entt::registry &) = nullptr;
    /** Loads component data from a JSON reader. */
    void (*load_json) (serialize::json_reader &, entt::registry &) = nullptr;

  };

  using world_component_descriptor = descriptor;

  /**
   * Registers an entity-owned world component type.
   * :param T: The world component type to register.
   * :param options: Registration options.
   */
  template <comp::world_component_type T>
  void
  register_world_component (const world_component_registration_options &options
                            = {});

  /**
   * Registers a Daslang component type with its storage layout.
   *
   * A layout whose fields cannot be stored is still registered so the editor
   * can list it, but such a component cannot be added (see das_supported).
   *
   * :return: \c false when the type id is already used by a different type.
   */
  bool register_runtime_world_component (entt::id_type type_id,
                                         std::string_view type_name,
                                         std::string_view display_name,
                                         runtime_layout layout);

  /** Registers a runtime component from the legacy cache representation. */
  bool register_cached_runtime_world_component (
      entt::id_type type_id, std::string_view type_name,
      std::string_view display_name, int struct_size = 0,
      std::vector<descriptor::das_field> fields = {});

  /** Finds a registered world component by stable type identifier. */
  const descriptor *find_world_component (entt::id_type type_id) const;

  /**
   * Finds a registered world component by name.
   *
   * Tries the full type name, the display name and then the unqualified short
   * name. A short name shared by several components resolves to nothing.
   */
  const descriptor *find_world_component (std::string_view name) const;

  /** Returns whether a world component descriptor exists for the ID. */
  bool contains_world_component (entt::id_type type_id) const;

  /** Returns registered world components in the requested order. */
  std::vector<const descriptor *>
  get_world_components (world_component_order order
                        = world_component_order::display_name) const;

  /** Returns the world components that may still be added to an entity. */
  std::vector<const descriptor *>
  get_addable_world_components (entt::registry &registry,
                                entt::entity entity) const;

  /** Copies a single registered native component between entities. */
  bool copy_world_component (entt::registry &src_registry,
                             entt::entity src_entity,
                             entt::registry &dst_registry,
                             entt::entity dst_entity,
                             entt::id_type component_type_id) const;

  /** Saves one native component storage to a binary writer. */
  bool save_world_component_binary (serialize::binary_writer &writer,
                                    entt::registry &registry,
                                    entt::id_type component_type_id) const;

  /** Loads one native component storage from a binary reader. */
  bool load_world_component_binary (serialize::binary_reader &reader,
                                    entt::registry &registry,
                                    entt::id_type component_type_id) const;

  /** Saves one native component storage to a JSON writer. */
  bool save_world_component_json (serialize::json_writer &writer,
                                  entt::registry &registry,
                                  entt::id_type component_type_id) const;

  /** Loads one native component storage from a JSON reader. */
  bool load_world_component_json (serialize::json_reader &reader,
                                  entt::registry &registry,
                                  entt::id_type component_type_id) const;

  /**
   * Saves every Daslang component of a registry.
   *
   * Each component is one document with its type name, stable id, entity and
   * its fields by name, so saved data survives a change of struct layout.
   */
  void save_das_components_json (serialize::json_writer &writer,
                                 const entt::registry &registry) const;

  /**
   * Loads Daslang components written by save_das_components_json.
   *
   * Fields are matched by name. Documents whose type is not registered, or
   * whose fields cannot be read, are reported and skipped.
   */
  void load_das_components_json (serialize::json_reader &reader,
                                 entt::registry &registry);

  /** Binary counterpart of save_das_components_json (play/stop snapshots). */
  void save_das_components_binary (serialize::binary_writer &writer,
                                   const entt::registry &registry) const;

  /** Binary counterpart of load_das_components_json. */
  void load_das_components_binary (serialize::binary_reader &reader,
                                   entt::registry &registry);

  /** Checks if a Daslang component is present on an entity. */
  bool das_component_contains (const entt::registry &registry,
                               entt::id_type type_id,
                               entt::entity entity) const;

  /** Adds a Daslang component to an entity, initialised with its defaults. */
  bool das_component_add (entt::registry &registry, entt::id_type type_id,
                          entt::entity entity);

  /** Removes a Daslang component from an entity. */
  bool das_component_remove (entt::registry &registry, entt::id_type type_id,
                             entt::entity entity);

  /**
   * Mutable block of a Daslang component, or nullptr when absent. The pool is
   * brought up to the current layout first.
   */
  uint8_t *das_component_data (entt::registry &registry, entt::id_type type_id,
                               entt::entity entity);

  /** Read-only block of a Daslang component, or nullptr when absent. */
  const uint8_t *das_component_data (const entt::registry &registry,
                                     entt::id_type type_id,
                                     entt::entity entity) const;

  /**
   * Pool of a Daslang component type for query iteration, brought up to the
   * current layout first. Returns nullptr when the registry has no such pool.
   */
  runtime_pool *das_component_pool (entt::registry &registry,
                                    entt::id_type type_id) const;

  /** Re-lays out every Daslang pool of a registry to the current layouts. */
  void migrate_das_component_pools (entt::registry &registry) const;

  /** Clears all Daslang component payloads attached to a registry. */
  void clear_das_component_storage (entt::registry &registry) const;

  /** Removes one entity from all Daslang component pools in a registry. */
  void clear_das_component_entity (entt::registry &registry,
                                   entt::entity entity) const;

  /** Clears descriptors that belong to runtime project code. */
  void clear_runtime_world_components ();

  /** Finds a component descriptor by its stable type identifier. */
  const descriptor *
  find (entt::id_type type_id) const
  {
    return find_world_component (type_id);
  }

  /** EnTT 4 storage keys are the stable type hashes used by descriptors. */
  [[nodiscard]] entt::id_type
  to_stable_id (entt::id_type storage_id) const noexcept
  {
    return storage_id;
  }

  /** Returns all registered descriptors ordered by display name. */
  std::vector<const descriptor *>
  ordered () const
  {
    return get_world_components (world_component_order::display_name);
  }

  /** Returns all registered descriptors sorted by type identifier. */
  std::vector<const descriptor *>
  by_type_id () const
  {
    return get_world_components (world_component_order::type_id);
  }

  /** Clears all components registered at runtime. */
  void
  clear_runtime_components ()
  {
    clear_runtime_world_components ();
  }

  /**
   * Registers component type info in the name lookup table.
   * :param das_type_name: The daScript-visible type name, spelled exactly as
   *   `typeinfo typename` spells it.
   */
  void register_component_type_info (const std::string &das_type_name,
                                     uint64_t type_id, ComponentKind kind,
                                     size_t struct_size);

  /** Looks up component type info by its exact daScript type name. */
  const ComponentTypeInfo *
  find_component_type_info (std::string_view das_type_name) const;

  /** Looks up component type info by type_id. */
  const ComponentTypeInfo *
  find_component_type_info_by_id (uint64_t type_id) const;

private:
  /** Transparent hashing so name lookups never allocate. */
  struct name_hash
  {
    using is_transparent = void;

    std::size_t
    operator() (std::string_view text) const noexcept
    {
      return std::hash<std::string_view>{}(text);
    }
  };

  using name_index = std::unordered_map<std::string, entt::id_type, name_hash,
                                        std::equal_to<>>;

  // ------------------------------------------------------------------
  // Named JSON and binary wrappers. EnTT's snapshot protocol writes the
  // per-entity stream as unnamed values; these wrappers keep the stream
  // readable and positionally aligned between save and load.
  // ------------------------------------------------------------------

  /** JSON save logic for one native component type. */
  template <typename T>
  static void
  save_component_json (serialize::json_writer &writer, entt::registry &registry)
  {
    using storage_t = entt::registry::storage_for_type<T>;
    const auto &const_registry = registry;
    const storage_t *storage = const_registry.template storage<T> ();

    // Always emit a document, even when this component has no storage.
    // The loader consumes one document per registered component in the
    // same order, so a skipped document would desynchronize the stream.
    std::vector<detail::component_save_entry<T>> entries;
    if (storage != nullptr) {
      entries.reserve (storage->size ());

      if constexpr (detail::is_in_place_storage_v<T>) {
        for (auto it = storage->rbegin (), last = storage->rend (); it != last;
             ++it) {
          const auto ent = *it;
          if (ent == entt::tombstone) {
            detail::component_save_entry<T> tomb;
            tomb.data = nullptr;
            entries.push_back (tomb);
          } else {
            entries.push_back (
                detail::component_save_entry<T>{ ent, &storage->get (ent) });
          }
        }
      } else {
        for (auto elem : storage->reach ()) {
          entries.push_back (detail::component_save_entry<T>{
              std::get<0> (elem), &std::get<1> (elem) });
        }
      }
    }

    writer.write (entries);
  }

  /** JSON load logic for one native component type. */
  template <typename T>
  static void
  load_component_json (serialize::json_reader &reader, entt::registry &registry)
  {
    using storage_t = entt::registry::storage_for_type<T>;
    storage_t &storage = registry.template storage<T> ();

    std::vector<detail::component_load_entry<T>> entries;
    reader.read (entries);

    for (auto &entry : entries) {
      if (!entry.data.has_value ()) {
        continue;
      }
      if (storage.contains (entry.entity_id)) {
        storage.get (entry.entity_id) = std::move (entry.data.value ());
      } else {
        storage.emplace (entry.entity_id, std::move (entry.data.value ()));
      }
    }
  }

  /** Binary save logic for one native component type. */
  template <typename T>
  static void
  save_binary_impl (serialize::binary_writer &writer, entt::registry &registry)
  {
    using storage_t = entt::registry::storage_for_type<T>;
    const storage_t *storage = static_cast<const entt::registry *> (&registry)
                                   ->template storage<T> ();

    // Always emit a document, even when this component has no storage,
    // to keep the save/load document streams positionally aligned.
    std::vector<detail::component_save_entry<T>> entries;
    if (storage != nullptr) {
      entries.reserve (storage->size ());

      if constexpr (detail::is_in_place_storage_v<T>) {
        for (auto it = storage->rbegin (), last = storage->rend (); it != last;
             ++it) {
          const auto ent = *it;
          if (ent == entt::tombstone) {
            detail::component_save_entry<T> tomb;
            tomb.data = nullptr;
            entries.push_back (tomb);
          } else {
            entries.push_back (
                detail::component_save_entry<T>{ ent, &storage->get (ent) });
          }
        }
      } else {
        for (auto elem : storage->reach ()) {
          entries.push_back (detail::component_save_entry<T>{
              std::get<0> (elem), &std::get<1> (elem) });
        }
      }
    }

    writer.write (entries);
  }

  /** Binary load logic for one native component type. */
  template <typename T>
  static void
  load_binary_impl (serialize::binary_reader &reader, entt::registry &registry)
  {
    using storage_t = entt::registry::storage_for_type<T>;
    storage_t &storage = registry.template storage<T> ();

    std::vector<detail::component_load_entry<T>> entries;
    reader.read (entries);

    for (auto &entry : entries) {
      if (!entry.data.has_value ()) {
        continue;
      }
      if (storage.contains (entry.entity_id)) {
        storage.get (entry.entity_id) = std::move (entry.data.value ());
      } else {
        storage.emplace (entry.entity_id, std::move (entry.data.value ()));
      }
    }
  }

  void rebuild_name_index ();
  runtime_pool *sync_das_pool (entt::registry &registry,
                               const descriptor &desc) const;

  std::unordered_map<entt::id_type, descriptor> m_descriptors;
  name_index m_type_name_to_stable;
  name_index m_display_name_to_stable;
  /** Unqualified name → id. A name shared by several components maps to null.
   */
  name_index m_short_name_to_stable;

  // Component type lookup table: daScript type name → type info.
  std::unordered_map<std::string, ComponentTypeInfo, name_hash, std::equal_to<>>
      m_type_info_by_name;
  // Reverse lookup: type_id → daScript type name.
  std::unordered_map<uint64_t, std::string> m_type_id_to_name;
};

template <comp::world_component_type T>
inline void
component_registry::register_world_component (
    const world_component_registration_options &options)
{
  const entt::id_type type_id = wsl::comp::stable_type_id<T> ();
  if (entt::type_hash<T>::value () != type_id) {
    wsl::log::sys ()->error (
        "Component '{}' has an EnTT type hash that differs from its stable id; "
        "persisted component ids would not match",
        entt::type_name<T> ().value ());
  }

  wsl::log::sys ()->trace ("Registering component '{}' (id={})",
                           entt::type_name<T> ().value (), type_id);

  detail::ensure_meta_registered<T> (type_id, options.runtime_registered);

  descriptor desc{};
  desc.type_id = type_id;
  desc.type_name = std::string (entt::type_name<T> ().value ());
  desc.display_name = detail::resolve_display_name<T> (options.display_name);
  desc.runtime_registered = options.runtime_registered;
  desc.contains = +[] (entt::registry &registry, entt::entity entity) -> bool {
    return registry.valid (entity) && registry.all_of<T> (entity);
  };

  if constexpr (std::is_default_constructible_v<T>) {
    desc.can_add_default = true;
    desc.emplace_default
        = +[] (entt::registry &registry, entt::entity entity) -> bool {
      if (!registry.valid (entity) || registry.all_of<T> (entity)) {
        return false;
      }

      registry.emplace<T> (entity);
      return true;
    };
  }

  desc.remove = +[] (entt::registry &registry, entt::entity entity) -> bool {
    if (!registry.valid (entity) || !registry.all_of<T> (entity)) {
      return false;
    }

    (registry.remove<T>)(entity);
    return true;
  };

  if constexpr (std::is_copy_constructible_v<T>) {
    desc.copy = +[] (entt::registry &src_reg, entt::entity src_ent,
                     entt::registry &dst_reg, entt::entity dst_ent) {
      if (src_reg.all_of<T> (src_ent)) {
        if constexpr (std::is_empty_v<T>) {
          dst_reg.emplace_or_replace<T> (dst_ent);
        } else {
          dst_reg.emplace_or_replace<T> (dst_ent, src_reg.get<T> (src_ent));
        }
      }
    };
  }

  desc.save_binary
      = +[] (serialize::binary_writer &writer, entt::registry &registry) {
          component_registry::save_binary_impl<T> (writer, registry);
        };
  desc.load_binary
      = +[] (serialize::binary_reader &reader, entt::registry &registry) {
          component_registry::load_binary_impl<T> (reader, registry);
        };
  desc.save_json
      = +[] (serialize::json_writer &writer, entt::registry &registry) {
          component_registry::save_component_json<T> (writer, registry);
        };
  desc.load_json
      = +[] (serialize::json_reader &reader, entt::registry &registry) {
          component_registry::load_component_json<T> (reader, registry);
        };

  if (const descriptor *existing = find_world_component (type_id)) {
    if (existing->type_name != desc.type_name) {
      wsl::log::sys ()->error ("Component id collision between '{}' and '{}'",
                               existing->type_name, desc.type_name);
      return;
    }
  }

  m_descriptors[type_id] = std::move (desc);
  rebuild_name_index ();

  // Also register in the lookup table for generic dispatch.
  const descriptor &stored = m_descriptors[type_id];
  std::string das_name = stored.display_name.empty ()
                             ? std::string (entt::type_name<T> ().value ())
                             : stored.display_name;
  register_component_type_info (das_name, type_id, ComponentKind::CPP_NATIVE,
                                sizeof (T));
}

} // namespace reg

} // namespace wsl
