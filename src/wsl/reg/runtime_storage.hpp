#pragma once

#include "../das/das_engine.hpp"

#ifndef IN_MODULE_INTERFACE
#include <entt/entt.hpp>
#endif

#ifndef IN_MODULE_INTERFACE
#include <cstddef>
#include <cstdint>
#include <memory>
#include <rfl/Generic.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
#endif

namespace wsl::reg
{

/** One field of a runtime (Daslang) struct as the storage layer sees it. */
struct runtime_field
{
  std::string name;
  std::string type_name;
  std::uint32_t offset = 0;
  std::uint32_t size = 0;
  wsl::das::das_engine::field_type_kind kind
      = wsl::das::das_engine::field_type_kind::unsupported;
  /** Bytes of the field's default value, or empty when there is none. */
  std::vector<std::uint8_t> default_value;
};

/**
 * Layout signature of a runtime struct: its total size and field list.
 *
 * The hash covers everything that determines how bytes are interpreted
 * (size, field names, offsets, sizes and kinds). Default values are not part
 * of the hash: they only affect newly created blocks.
 */
struct runtime_layout
{
  std::uint32_t size = 0;
  std::vector<runtime_field> fields;
  /** Hash of size and field shape. */
  std::uint64_t hash = 0;
  /** Hash of the default values, used to detect changed defaults cheaply. */
  std::uint64_t defaults_hash = 0;

  static runtime_layout make (std::uint32_t size,
                              std::vector<runtime_field> fields);

  /** Returns an explanatory error when the layout cannot be stored safely. */
  std::string validation_error () const;

  bool
  same_shape (const runtime_layout &other) const noexcept
  {
    return size == other.size && hash == other.hash;
  }
};

/** Encodes one field from a runtime memory block into an rfl::Generic value. */
rfl::Generic encode_runtime_field (const runtime_field &field,
                                   const std::uint8_t *block);

/** Decodes an rfl::Generic value into a runtime memory block. Returns false on invalid input. */
bool decode_runtime_field (const runtime_field &field,
                           const rfl::Generic &value,
                           std::uint8_t *block);

/** Finds a field by name in a runtime layout; nullptr when not found. */
const runtime_field *find_runtime_field (const runtime_layout &layout,
                                         std::string_view name);

/**
 * Stable-address byte arena. Elements live in fixed-size pages, so a pointer
 * returned by at() stays valid while further elements are appended. Removal
 * is swap-and-pop, matching the packed order of the owning sparse set.
 */
class runtime_arena
{
public:
  explicit runtime_arena (std::uint32_t stride);

  runtime_arena (runtime_arena &&) noexcept = default;
  runtime_arena &operator= (runtime_arena &&) noexcept = default;
  runtime_arena (const runtime_arena &) = delete;
  runtime_arena &operator= (const runtime_arena &) = delete;
  ~runtime_arena () = default;

  std::uint32_t
  stride () const noexcept
  {
    return m_stride;
  }

  std::size_t
  size () const noexcept
  {
    return m_size;
  }

  std::uint8_t *at (std::size_t index) noexcept;
  const std::uint8_t *at (std::size_t index) const noexcept;

  /** Appends one zero-filled element and returns its index. */
  std::size_t append ();

  /** Removes the element at index by moving the last element into its place. */
  void swap_remove (std::size_t index) noexcept;

  void clear () noexcept;
  void shrink_to_fit ();

private:
  std::uint32_t m_stride;
  std::size_t m_per_page;
  std::size_t m_size = 0;
  std::vector<std::unique_ptr<std::max_align_t[]>> m_pages;
};

/**
 * Entity-keyed block storage for one runtime type. The sparse set provides
 * membership and the packed order; the arena holds the bytes, indexed by the
 * same packed position, so iteration and lookup are both O(1) and no per-entity
 * heap allocation is needed.
 */
class runtime_pool
{
public:
  explicit runtime_pool (runtime_layout layout);

  runtime_pool (runtime_pool &&) noexcept = default;
  runtime_pool &operator= (runtime_pool &&) noexcept = default;
  runtime_pool (const runtime_pool &) = delete;
  runtime_pool &operator= (const runtime_pool &) = delete;
  ~runtime_pool () = default;

  const runtime_layout &
  layout () const noexcept
  {
    return m_layout;
  }

  /**
   * Replaces the default values used for new blocks. Only valid when the
   * shape is unchanged; existing data is left untouched.
   */
  void update_defaults (runtime_layout layout);

  bool
  contains (entt::entity entity) const noexcept
  {
    return m_set.contains (entity);
  }

  std::size_t
  size () const noexcept
  {
    return m_set.size ();
  }

  /** Iteration order matches block order (see block()). */
  const entt::sparse_set &
  entities () const noexcept
  {
    return m_set;
  }

  /** Creates a block initialised with the layout defaults. False when present.
   */
  bool add (entt::entity entity);

  /** Removes the block of an entity. False when absent. */
  bool remove (entt::entity entity);

  std::uint8_t *data (entt::entity entity) noexcept;
  const std::uint8_t *data (entt::entity entity) const noexcept;

  /** Block at a packed position (0..size()). */
  std::uint8_t *
  block (std::size_t packed_index) noexcept
  {
    return m_arena.at (packed_index);
  }

  const std::uint8_t *
  block (std::size_t packed_index) const noexcept
  {
    return m_arena.at (packed_index);
  }

  /**
   * Re-lays out every block under a different shape. Fields are matched by
   * name and kind; fields that are new take their defaults, fields that were
   * removed are dropped.
   */
  void migrate (runtime_layout target);

  void clear () noexcept;
  void shrink_to_fit ();

private:
  void init_block (std::uint8_t *block) const noexcept;

  runtime_layout m_layout;
  entt::sparse_set m_set;
  runtime_arena m_arena;
};

/**
 * Per-registry store of runtime-typed pools, keyed by stable type id. Lives in
 * the registry context, so it follows the scene it belongs to.
 */
class runtime_storage
{
public:
  runtime_pool *find (entt::id_type type_id) noexcept;
  const runtime_pool *find (entt::id_type type_id) const noexcept;

  /**
   * Returns the pool for a type, creating it when missing. An existing pool
   * whose shape differs from layout is migrated in place first.
   */
  runtime_pool &acquire (entt::id_type type_id, const runtime_layout &layout);

  /** Drops one entity from every pool. */
  void remove_entity (entt::entity entity);

  void erase (entt::id_type type_id);
  void clear () noexcept;
  void shrink_to_fit ();

  template <typename Fn>
  void
  for_each_pool (Fn &&fn) const
  {
    for (const auto &[type_id, pool] : m_pools) {
      fn (type_id, pool);
    }
  }

  template <typename Fn>
  void
  for_each_pool (Fn &&fn)
  {
    for (auto &[type_id, pool] : m_pools) {
      fn (type_id, pool);
    }
  }

private:
  std::unordered_map<entt::id_type, runtime_pool> m_pools;
};

/**
 * Copies every field that exists in both layouts (matched by name, kind and
 * size) from a source block to a destination block. Destination fields that
 * have no source counterpart are initialised with their defaults.
 */
void copy_matching_fields (const runtime_layout &source,
                           const std::uint8_t *source_block,
                           const runtime_layout &target,
                           std::uint8_t *target_block) noexcept;

/**
 * Brings a pool to a layout: migrates the blocks when the shape changed and
 * only swaps the defaults when they changed. Cheap when nothing changed.
 */
void sync_pool_layout (runtime_pool &pool, const runtime_layout &layout);

/** Entity used as the single key of a singleton pool. */
inline constexpr entt::entity runtime_singleton_entity{ 0 };

/** Registry-context access to the runtime storage (created on demand). */
runtime_storage &runtime_storage_of (entt::registry &registry);
runtime_storage *try_runtime_storage_of (entt::registry &registry);
const runtime_storage *try_runtime_storage_of (const entt::registry &registry);

} // namespace wsl::reg
