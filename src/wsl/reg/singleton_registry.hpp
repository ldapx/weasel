#pragma once

#include "detail/registry_helpers.hpp"

#include "../rsc/world.hpp"
#include "../serialize/types.hpp"
#include "../serialize/component_adapters.hpp"

#ifndef IN_MODULE_INTERFACE
#include "../das/das_engine.hpp"
#endif

#ifndef IN_MODULE_INTERFACE
#include <cstdint>
#endif
#ifndef IN_MODULE_INTERFACE
#include <cstddef>
#endif
#ifndef IN_MODULE_INTERFACE
#include <entt/entt.hpp>
#endif

#ifndef IN_MODULE_INTERFACE
#include <cassert>
#endif
#ifndef IN_MODULE_INTERFACE
#include <memory>
#endif
#ifndef IN_MODULE_INTERFACE
#include <optional>
#endif
#ifndef IN_MODULE_INTERFACE
#include <string>
#endif
#ifndef IN_MODULE_INTERFACE
#include <string_view>
#endif
#ifndef IN_MODULE_INTERFACE
#include <type_traits>
#endif
#ifndef IN_MODULE_INTERFACE
#include <unordered_map>
#endif
#ifndef IN_MODULE_INTERFACE
#include <vector>
#endif

namespace wsl
{

namespace reg
{

struct runtime_layout;

/** Ordering modes for singleton component descriptor queries. */
enum class singleton_component_order
{
  /** Sort by display name, falling back to the C++ type name. */
  display_name,

  /** Sort by stable type identifier. */
  type_id
};

/** Registration options for singleton components. */
struct singleton_component_registration_options
{
  /** Optional editor-facing display name override. */
  std::string_view display_name;

  /** Whether the singleton is owned by the engine core. */
  bool core = false;

  /** Whether the registration came from runtime project code. */
  bool runtime_registered = false;

  /** Whether the singleton should be serialized with a scene snapshot. */
  bool serialize_with_scene = true;
};

/** Concept for types that have a serialize method compatible with rfl. */
template <typename T>
concept has_serialize = requires (T &v) {
  { v.serialize () } -> std::same_as<void>;
};

/**
 * Type-erased storage for Daslang singleton values.
 *
 * C++ singletons live in `registry.ctx()` as concrete types, which is what the
 * descriptor callbacks in `singleton_registry` operate on. A Daslang singleton
 * has no C++ type, so its bytes are kept here instead, keyed by type id and
 * attached to the registry's context so it follows the scene like everything
 * else.
 */
class das_singleton_storage
{
public:
  /** One type-erased value block, aligned for any scalar daslang field. */
  struct block
  {
    std::vector<std::max_align_t> words;
    std::size_t size = 0;
    std::shared_ptr<runtime_layout> layout;

    uint8_t *
    data ()
    {
      return words.empty () ? nullptr
                            : reinterpret_cast<uint8_t *> (words.data ());
    }

    const uint8_t *
    data () const
    {
      return words.empty () ? nullptr
                            : reinterpret_cast<const uint8_t *> (words.data ());
    }
  };

  /** Whether a value exists for the given type id. */
  bool contains (::entt::id_type type_id) const;

  /**
   * Creates a value with the layout defaults for the given type id.
   * @return The new block, or `nullptr` when one already exists or the size is
   * zero.
   */
  block *emplace_default (::entt::id_type type_id,
                          const runtime_layout &layout);

  /**
   * Creates a value with the layout defaults. Returns `nullptr` when present.
   */
  block *emplace_with (::entt::id_type type_id, const runtime_layout &layout);

  /** Migrates an existing value to a new shape while preserving matching
   * fields. Returns `nullptr` when no value exists. */
  block *migrate (::entt::id_type type_id, const runtime_layout &layout);

  /** Returns the value for the given type id, or `nullptr`. */
  block *data (::entt::id_type type_id);

  /** Returns the value for the given type id, or `nullptr`. */
  const block *data (::entt::id_type type_id) const;

  /** Destroys the value for the given type id. */
  bool remove (::entt::id_type type_id);

  /** Destroys every value. */
  void clear ();

  /** Read-only access to every stored value. */
  const std::unordered_map<::entt::id_type, block> &
  entries () const
  {
    return m_blocks;
  }

private:
  std::unordered_map<::entt::id_type, block> m_blocks;
};

/**
 * Central registry for singleton components (singletons) in the engine.
 *
 * This class manages the registration, creation, and destruction of singletons,
 * which are components that exist globally within a scene or registry.
 */
class singleton_registry
{
public:
  /** Describes a registered singleton type. */
  struct descriptor
  {
    /** The stable type ID of the singleton. */
    ::entt::id_type type_id{};
    /** The C++ type name. */
    std::string type_name;
    /** The user-friendly display name. */
    std::string display_name;
    /** Whether this singleton was registered by user code at runtime. */
    bool runtime_registered = false;
    /** Whether this is a core engine singleton that cannot be removed. */
    bool core = false;
    /**
     * Whether this singleton should be serialized as part of the scene
     * snapshot.
     */
    bool serialize_with_scene = false;
    /** Whether the singleton can be default-constructed. */
    bool can_add_default = false;
    /** Whether this is a daslang singleton (no C++ backing type). */
    bool is_das_singleton = false;
    /** Size of the daslang value in bytes; 0 when unknown. */
    int das_struct_size = 0;
    /** Field layout of a daslang singleton value. */
    struct das_field
    {
      std::string name;
      std::string type_name;
      int offset = 0;
      int size = 0;
      wsl::das::das_engine::field_type_kind kind
          = wsl::das::das_engine::field_type_kind::unsupported;
      /**
       * Initialiser bytes from the daslang struct definition, so `singl add`
       * produces the declared defaults rather than zeroes.
       */
      std::vector<uint8_t> default_value;
    };
    std::vector<das_field> das_fields;
    /** Validated layout used to initialize and migrate stored values. */
    std::shared_ptr<runtime_layout> das_layout;
    /** Function pointer to check if the singleton exists in a registry. */
    bool (*contains) (::entt::registry &) = nullptr;
    /** Function pointer to emplace a default instance of the singleton. */
    bool (*emplace_default) (::entt::registry &) = nullptr;
    /** Function pointer to remove the singleton from a registry. */
    bool (*remove) (::entt::registry &) = nullptr;
    /** Function pointer to get a raw pointer to the singleton instance. */
    void *(*get_ptr) (::entt::registry &) = nullptr;
    /** Function pointer to save the singleton to a binary writer. */
    void (*save_binary) (serialize::binary_writer &, ::entt::registry &)
        = nullptr;
    /** Function pointer to load the singleton from a binary reader. */
    void (*load_binary) (serialize::binary_reader &, ::entt::registry &)
        = nullptr;
    /** Function pointer to save the singleton to a JSON writer. */
    void (*save_json) (serialize::json_writer &, ::entt::registry &) = nullptr;
    /** Function pointer to load the singleton from a JSON reader. */
    void (*load_json) (serialize::json_reader &, ::entt::registry &) = nullptr;
  };

  using singleton_component_descriptor = descriptor;

  /**
   * Registers a value-owned singleton component type.
   * :param T: Singleton component type.
   * :param options: Registration options.
   */
  template <comp::singleton_component_type T>
  void register_singleton_component (
      const singleton_component_registration_options &options = {});

  /**
   * Registers metadata for a runtime singleton without loading its C++
   * type.
   *
   * For daslang singletons the value layout is supplied so the descriptor can
   * carry working `contains` / `emplace_default` / `remove` / `get_ptr`
   * callbacks backed by `das_singleton_storage`. Passing a zero
   * `das_struct_size` registers a discovery-only descriptor, which is what
   * happens for stale caches written before the layout was recorded.
   */
  bool register_cached_runtime_singleton_component (
      ::entt::id_type type_id, std::string_view type_name,
      std::string_view display_name, int das_struct_size = 0,
      std::vector<descriptor::das_field> das_fields = {});

  // ── Daslang singletons ──
  //
  // A daslang singleton has no C++ type to key `registry.ctx()` on, so its
  // bytes are kept in a `das_singleton_storage` attached to the registry's
  // context. These are the accessors the CLI and the scene serializer use
  // instead of the descriptor's function pointers.

  /** Whether a daslang singleton value exists in the registry. */
  bool das_singleton_contains (const ::entt::registry &registry,
                               ::entt::id_type type_id) const;

  /** Creates a zero-filled daslang singleton value. */
  bool das_singleton_add (::entt::registry &registry,
                          ::entt::id_type type_id) const;

  /** Destroys a daslang singleton value. */
  bool das_singleton_remove (::entt::registry &registry,
                             ::entt::id_type type_id) const;

  /** Returns a pointer to the daslang singleton value, or `nullptr`. */
  uint8_t *das_singleton_data (::entt::registry &registry,
                               ::entt::id_type type_id) const;

  /** Serializes a daslang singleton value as lowercase hex, or empty. */
  std::string das_singleton_hex (::entt::registry &registry,
                                ::entt::id_type type_id) const;
  std::string das_singleton_hex (const ::entt::registry &registry,
                                 ::entt::id_type type_id) const;

  /** Serializes a daslang singleton value as a JSON object with typed fields. */
  std::string das_singleton_json (::entt::registry &registry,
                                 ::entt::id_type type_id) const;
  std::string das_singleton_json (const ::entt::registry &registry,
                                  ::entt::id_type type_id) const;

  /** Restores a daslang singleton value from a JSON object with typed fields. */
  bool das_singleton_load_json (::entt::registry &registry,
                                ::entt::id_type type_id,
                                const std::string &json_str);

  /**
   * Restores a daslang singleton value from lowercase hex.
   * @return `true` when the value was written; `false` when the descriptor is
   * unknown, the layout is unknown, or the hex does not match the layout.
   */
  bool das_singleton_load_hex (::entt::registry &registry,
                               ::entt::id_type type_id,
                               std::string_view hex) const;

  /**
   * Registers a bound singleton component type stored as a raw pointer.
   * :param T: Singleton component type.
   * :param options: Registration options.
   */
  template <comp::singleton_component_type T>
  void register_bound_singleton_component (
      const singleton_component_registration_options &options = {});

  /** Finds a singleton component descriptor by type ID. */
  const descriptor *find_singleton_component (::entt::id_type type_id) const;

  /**
   * Finds a singleton component descriptor by C++ type name.
   * :param type_name: Reflected type name.
   * :return: Matching descriptor, or `nullptr` when not found.
   */
  const descriptor *find_singleton_component (std::string_view type_name) const;

  /**
   * Returns registered singleton component descriptors in the requested
   * order.
   * :param order: Requested descriptor ordering.
   * :return: Ordered descriptor list.
   */
  std::vector<const descriptor *>
  get_singleton_components (singleton_component_order order
                            = singleton_component_order::display_name) const;

  /** Ensures all core singleton components exist in the registry. */
  void apply_core_singleton_components (::entt::registry &registry) const;

  /** Resets or removes non-core singleton components in the registry. */
  void reset_scene_singleton_components (::entt::registry &registry) const;

  /** Clears runtime-registered singleton components from the world. */
  void clear_runtime_singleton_components (rsc::world &world);

  /** Saves one registered singleton component to a binary writer. */
  bool save_singleton_binary (serialize::binary_writer &writer,
                              ::entt::registry &registry,
                              ::entt::id_type type_id) const;

  /** Loads one registered singleton component from a binary reader. */
  bool load_singleton_binary (serialize::binary_reader &reader,
                              ::entt::registry &registry,
                              ::entt::id_type type_id) const;

  /** Saves one registered singleton component to a JSON writer. */
  bool save_singleton_json (serialize::json_writer &writer,
                            ::entt::registry &registry,
                            ::entt::id_type type_id) const;

  /** Loads one registered singleton component from a JSON reader. */
  bool load_singleton_json (serialize::json_reader &reader,
                            ::entt::registry &registry,
                            ::entt::id_type type_id) const;

  /** Finds a singleton descriptor by type ID. */
  const descriptor *
  find (::entt::id_type type_id) const
  {
    return find_singleton_component (type_id);
  }
  /** Returns all registered descriptors sorted by display name. */
  std::vector<const descriptor *>
  ordered () const
  {
    return get_singleton_components (singleton_component_order::display_name);
  }
  /** Returns all registered descriptors sorted by type ID. */
  std::vector<const descriptor *>
  by_type_id () const
  {
    return get_singleton_components (singleton_component_order::type_id);
  }

  /** Ensures all core singletons are present in the given registry. */
  void
  apply_core_singletons (::entt::registry &registry) const
  {
    apply_core_singleton_components (registry);
  }
  /** Resets or removes non-core singletons in the registry. */
  void
  reset_scene_registry (::entt::registry &registry) const
  {
    reset_scene_singleton_components (registry);
  }
  /** Clears runtime-registered singletons from the specified world. */
  void
  clear_runtime_singletons (rsc::world &world)
  {
    clear_runtime_singleton_components (world);
  }

private:
  std::unordered_map<::entt::id_type, descriptor> m_descriptors;
  std::unordered_map<std::string, ::entt::id_type> m_type_name_to_type_id;
  std::unordered_map<std::string, ::entt::id_type> m_display_name_to_type_id;
};

template <comp::singleton_component_type T>
inline void
singleton_registry::register_singleton_component (
    const singleton_component_registration_options &options)
{
  static_assert (std::is_default_constructible_v<T>,
                 "Registered singleton types must be default constructible.");

  const ::entt::id_type type_id = ::entt::type_hash<T>::value ();
  detail::ensure_meta_registered<T> (type_id, options.runtime_registered);

  descriptor desc{};
  desc.type_id = type_id;
  desc.type_name = std::string (::entt::type_name<T> ().value ());
  desc.display_name = detail::resolve_display_name<T> (options.display_name);
  desc.runtime_registered = options.runtime_registered;
  desc.core = options.core;
  desc.serialize_with_scene = options.serialize_with_scene;
  m_type_name_to_type_id[desc.type_name] = type_id;
  m_display_name_to_type_id[desc.display_name] = type_id;
  desc.contains = +[] (::entt::registry &registry) -> bool {
    return registry.ctx ().contains<T> ();
  };
  desc.emplace_default = +[] (::entt::registry &registry) -> bool {
    auto &ctx = registry.ctx ();
    if (ctx.contains<T> ()) {
      return false;
    }

    ctx.emplace<T> ();
    return true;
  };
  desc.remove = +[] (::entt::registry &registry) -> bool {
    auto &ctx = registry.ctx ();
    if (!ctx.contains<T> ()) {
      return false;
    }

    ctx.erase<T> ();
    return true;
  };

  if (options.core) {
    desc.remove = +[] (::entt::registry &) -> bool { return false; };
  }
  desc.get_ptr = +[] (::entt::registry &registry) -> void * {
    auto &ctx = registry.ctx ();
    if (!ctx.contains<T> ()) {
      return nullptr;
    }

    return &ctx.get<T> ();
  };
  desc.can_add_default = true;

  if (options.serialize_with_scene) {
    desc.save_binary
        = +[] (serialize::binary_writer &writer, ::entt::registry &registry) {
            const auto &value = registry.ctx ().get<T> ();
            writer.write (value);
          };
    desc.load_binary
        = +[] (serialize::binary_reader &reader, ::entt::registry &registry) {
            T value{};
            reader.read (value);
            auto &ctx = registry.ctx ();
            if (ctx.contains<T> ()) {
              ctx.get<T> () = std::move (value);
            } else {
              ctx.emplace<T> (std::move (value));
            }
          };
    desc.save_json
        = +[] (serialize::json_writer &writer, ::entt::registry &registry) {
            const auto &value = registry.ctx ().get<T> ();
            writer.write (value);
          };
    desc.load_json
        = +[] (serialize::json_reader &reader, ::entt::registry &registry) {
            T value{};
            reader.read (value);
            auto &ctx = registry.ctx ();
            if (ctx.contains<T> ()) {
              ctx.get<T> () = std::move (value);
            } else {
              ctx.emplace<T> (std::move (value));
            }
          };
  }

  m_descriptors[type_id] = std::move (desc);
}

template <comp::singleton_component_type T>
inline void
singleton_registry::register_bound_singleton_component (
    const singleton_component_registration_options &options)
{
  const ::entt::id_type type_id = ::entt::type_hash<T>::value ();
  detail::ensure_meta_registered<T> (type_id, options.runtime_registered);

  descriptor desc{};
  desc.type_id = type_id;
  desc.type_name = std::string (::entt::type_name<T> ().value ());
  desc.display_name = detail::resolve_display_name<T> (options.display_name);
  desc.runtime_registered = options.runtime_registered;
  desc.core = options.core;
  desc.can_add_default = false;
  desc.serialize_with_scene = options.serialize_with_scene;
  m_type_name_to_type_id[desc.type_name] = type_id;
  m_display_name_to_type_id[desc.display_name] = type_id;
  desc.contains = +[] (::entt::registry &registry) -> bool {
    return registry.ctx ().contains<T *> ();
  };
  desc.remove = +[] (::entt::registry &registry) -> bool {
    auto &ctx = registry.ctx ();
    if (!ctx.contains<T *> ()) {
      return false;
    }

    ctx.erase<T *> ();
    return true;
  };

  if (options.core) {
    desc.remove = +[] (::entt::registry &) -> bool { return false; };
  }
  desc.get_ptr = +[] (::entt::registry &registry) -> void * {
    auto &ctx = registry.ctx ();
    if (!ctx.contains<T *> ()) {
      return nullptr;
    }

    return ctx.get<T *> ();
  };

  if (options.serialize_with_scene) {
    if constexpr (has_serialize<T>) {
      desc.save_binary
          = +[] (serialize::binary_writer &writer, ::entt::registry &registry) {
              const auto &value = *registry.ctx ().get<T *> ();
              writer.write (value);
            };
      desc.load_binary
          = +[] (serialize::binary_reader &reader, ::entt::registry &registry) {
              reader.read (*registry.ctx ().get<T *> ());
            };
      desc.save_json
          = +[] (serialize::json_writer &writer, ::entt::registry &registry) {
              const auto &value = *registry.ctx ().get<T *> ();
              writer.write (value);
            };
      desc.load_json
          = +[] (serialize::json_reader &reader, ::entt::registry &registry) {
              reader.read (*registry.ctx ().get<T *> ());
            };
    }
  }

  m_descriptors[type_id] = std::move (desc);
}

} // namespace reg

} // namespace wsl
