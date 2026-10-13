#include "runtime_storage.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <string_view>
#include <unordered_set>

namespace wsl::reg
{

namespace
{

constexpr std::uint64_t fnv_offset = 14695981039346656037ull;
constexpr std::uint64_t fnv_prime = 1099511628211ull;
constexpr std::size_t page_bytes = 4096;

void
hash_bytes (std::uint64_t &hash, const void *data, std::size_t size) noexcept
{
  const auto *bytes = static_cast<const std::uint8_t *> (data);
  for (std::size_t i = 0; i < size; ++i) {
    hash ^= bytes[i];
    hash *= fnv_prime;
  }
}

template <typename T>
void
hash_value (std::uint64_t &hash, T value) noexcept
{
  hash_bytes (hash, &value, sizeof value);
}

void
hash_text (std::uint64_t &hash, const std::string &text) noexcept
{
  hash_value (hash, static_cast<std::uint64_t> (text.size ()));
  hash_bytes (hash, text.data (), text.size ());
}

std::uint32_t
round_up (std::uint32_t value, std::uint32_t multiple) noexcept
{
  return ((value + multiple - 1U) / multiple) * multiple;
}

} // namespace

runtime_layout
runtime_layout::make (std::uint32_t size, std::vector<runtime_field> fields)
{
  runtime_layout layout;
  layout.size = size;

  std::uint64_t hash = fnv_offset;
  hash_value (hash, size);
  hash_value (hash, static_cast<std::uint64_t> (fields.size ()));
  for (const runtime_field &field : fields) {
    hash_text (hash, field.name);
    hash_value (hash, field.offset);
    hash_value (hash, field.size);
    hash_value (hash, static_cast<std::uint32_t> (field.kind));
  }
  layout.hash = hash;

  std::uint64_t defaults = fnv_offset;
  for (const runtime_field &field : fields) {
    hash_value (defaults,
                static_cast<std::uint64_t> (field.default_value.size ()));
    hash_bytes (defaults, field.default_value.data (),
                field.default_value.size ());
  }
  layout.defaults_hash = defaults;

  layout.fields = std::move (fields);
  return layout;
}

std::string
runtime_layout::validation_error () const
{
  using kind = wsl::das::das_engine::field_type_kind;
  if (size == 0) {
    return "its struct size could not be determined";
  }

  std::unordered_set<std::string_view> names;
  std::vector<const runtime_field *> ranges;
  ranges.reserve (fields.size ());
  for (const runtime_field &field : fields) {
    if (field.name.empty () || !names.insert (field.name).second) {
      return "it has an empty or duplicate field name";
    }
    if (field.size == 0 || field.offset > size
        || field.size > size - field.offset) {
      return "field '" + field.name + "' has an invalid memory layout";
    }
    if (!field.default_value.empty ()
        && field.default_value.size () != field.size) {
      return "field '" + field.name + "' has an invalid default value size";
    }

    bool valid_width = false;
    switch (field.kind) {
    case kind::boolean:
      valid_width = field.size == 1;
      break;
    case kind::integer:
    case kind::unsigned_integer:
      valid_width = field.size == 1 || field.size == 2 || field.size == 4
                    || field.size == 8;
      break;
    case kind::floating:
      valid_width = field.size == 4 || field.size == 8;
      break;
    case kind::floating_vector:
    case kind::integer_vector:
      valid_width
          = field.size == 8 || field.size == 12 || field.size == 16;
      break;
    case kind::string:
    case kind::unsupported:
      return "field '" + field.name + "' of type '" + field.type_name
             + "' cannot be stored (managed or unsupported type)";
    }
    if (!valid_width) {
      return "field '" + field.name + "' has an invalid memory layout";
    }
    ranges.push_back (&field);
  }

  std::sort (ranges.begin (), ranges.end (), [] (const auto *left,
                                                 const auto *right) {
    return left->offset < right->offset;
  });
  for (std::size_t i = 1; i < ranges.size (); ++i) {
    const runtime_field &previous = *ranges[i - 1];
    if (ranges[i]->offset < previous.offset + previous.size) {
      return "fields '" + previous.name + "' and '" + ranges[i]->name
             + "' overlap";
    }
  }
  return {};
}

// ── runtime_arena ──

runtime_arena::runtime_arena (std::uint32_t stride)
    : m_stride (
          round_up (std::max<std::uint32_t> (stride, 1U),
                    static_cast<std::uint32_t> (alignof (std::max_align_t))))
{
  m_per_page = std::max<std::size_t> (1, page_bytes / m_stride);
}

std::uint8_t *
runtime_arena::at (std::size_t index) noexcept
{
  auto *page = m_pages[index / m_per_page].get ();
  return reinterpret_cast<std::uint8_t *> (page)
         + (index % m_per_page) * m_stride;
}

const std::uint8_t *
runtime_arena::at (std::size_t index) const noexcept
{
  const auto *page = m_pages[index / m_per_page].get ();
  return reinterpret_cast<const std::uint8_t *> (page)
         + (index % m_per_page) * m_stride;
}

std::size_t
runtime_arena::append ()
{
  if (m_size == m_pages.size () * m_per_page) {
    const std::size_t bytes = m_per_page * m_stride;
    const std::size_t words
        = (bytes + sizeof (std::max_align_t) - 1) / sizeof (std::max_align_t);
    m_pages.push_back (std::make_unique<std::max_align_t[]> (words));
  }
  const std::size_t index = m_size++;
  std::memset (at (index), 0, m_stride);
  return index;
}

void
runtime_arena::swap_remove (std::size_t index) noexcept
{
  const std::size_t last = m_size - 1;
  if (index != last) {
    std::memcpy (at (index), at (last), m_stride);
  }
  --m_size;
}

void
runtime_arena::clear () noexcept
{
  m_size = 0;
  m_pages.clear ();
}

void
runtime_arena::shrink_to_fit ()
{
  const std::size_t needed = (m_size + m_per_page - 1) / m_per_page;
  if (m_pages.size () > needed) {
    m_pages.resize (needed);
  }
}

// ── runtime_pool ──

runtime_pool::runtime_pool (runtime_layout layout)
    : m_layout (std::move (layout)), m_arena (m_layout.size)
{
}

void
runtime_pool::update_defaults (runtime_layout layout)
{
  m_layout = std::move (layout);
}

void
runtime_pool::init_block (std::uint8_t *block) const noexcept
{
  std::memset (block, 0, m_layout.size);
  for (const runtime_field &field : m_layout.fields) {
    if (field.default_value.empty () || field.size == 0) {
      continue;
    }
    const std::size_t count
        = std::min<std::size_t> (field.size, field.default_value.size ());
    if (static_cast<std::size_t> (field.offset) + count <= m_layout.size) {
      std::memcpy (block + field.offset, field.default_value.data (), count);
    }
  }
}

bool
runtime_pool::add (entt::entity entity)
{
  if (m_set.contains (entity)) {
    return false;
  }
  m_set.push (entity);
  const std::size_t index = m_arena.append ();
  init_block (m_arena.at (index));
  return true;
}

bool
runtime_pool::remove (entt::entity entity)
{
  if (!m_set.contains (entity)) {
    return false;
  }
  // Both containers delete by swap-and-pop, so the element that was last in
  // the packed order takes the removed position in each of them.
  const std::size_t index = m_set.index (entity);
  m_set.erase (entity);
  m_arena.swap_remove (index);
  return true;
}

std::uint8_t *
runtime_pool::data (entt::entity entity) noexcept
{
  if (!m_set.contains (entity)) {
    return nullptr;
  }
  return m_arena.at (m_set.index (entity));
}

const std::uint8_t *
runtime_pool::data (entt::entity entity) const noexcept
{
  if (!m_set.contains (entity)) {
    return nullptr;
  }
  return m_arena.at (m_set.index (entity));
}

void
runtime_pool::migrate (runtime_layout target)
{
  runtime_arena fresh (target.size);
  for (std::size_t i = 0; i < m_arena.size (); ++i) {
    const std::size_t index = fresh.append ();
    copy_matching_fields (m_layout, m_arena.at (i), target, fresh.at (index));
  }
  m_arena = std::move (fresh);
  m_layout = std::move (target);
}

void
runtime_pool::clear () noexcept
{
  m_set.clear ();
  m_arena.clear ();
}

void
runtime_pool::shrink_to_fit ()
{
  m_set.shrink_to_fit ();
  m_arena.shrink_to_fit ();
}

// ── runtime_storage ──

runtime_pool *
runtime_storage::find (entt::id_type type_id) noexcept
{
  auto it = m_pools.find (type_id);
  return it == m_pools.end () ? nullptr : &it->second;
}

const runtime_pool *
runtime_storage::find (entt::id_type type_id) const noexcept
{
  auto it = m_pools.find (type_id);
  return it == m_pools.end () ? nullptr : &it->second;
}

void
sync_pool_layout (runtime_pool &pool, const runtime_layout &layout)
{
  if (!pool.layout ().same_shape (layout)) {
    pool.migrate (layout);
  } else if (pool.layout ().defaults_hash != layout.defaults_hash) {
    pool.update_defaults (layout);
  }
}

runtime_pool &
runtime_storage::acquire (entt::id_type type_id, const runtime_layout &layout)
{
  auto it = m_pools.find (type_id);
  if (it == m_pools.end ()) {
    it = m_pools.try_emplace (type_id, layout).first;
  } else {
    sync_pool_layout (it->second, layout);
  }
  return it->second;
}

void
runtime_storage::remove_entity (entt::entity entity)
{
  for (auto &[type_id, pool] : m_pools) {
    (void)type_id;
    pool.remove (entity);
  }
}

void
runtime_storage::erase (entt::id_type type_id)
{
  m_pools.erase (type_id);
}

void
runtime_storage::clear () noexcept
{
  m_pools.clear ();
}

void
runtime_storage::shrink_to_fit ()
{
  for (auto it = m_pools.begin (); it != m_pools.end ();) {
    if (it->second.size () == 0) {
      it = m_pools.erase (it);
    } else {
      it->second.shrink_to_fit ();
      ++it;
    }
  }
}

void
copy_matching_fields (const runtime_layout &source,
                      const std::uint8_t *source_block,
                      const runtime_layout &target,
                      std::uint8_t *target_block) noexcept
{
  for (const runtime_field &field : target.fields) {
    if (field.size == 0 || field.offset > target.size
        || field.size > target.size - field.offset) {
      continue;
    }

    const runtime_field *match = nullptr;
    for (const runtime_field &candidate : source.fields) {
      if (candidate.name == field.name && candidate.kind == field.kind
          && candidate.size == field.size
          && candidate.offset <= source.size
          && candidate.size <= source.size - candidate.offset) {
        match = &candidate;
        break;
      }
    }

    if (match != nullptr) {
      std::memcpy (target_block + field.offset, source_block + match->offset,
                   field.size);
    } else if (!field.default_value.empty ()) {
      const std::size_t count
          = std::min<std::size_t> (field.size, field.default_value.size ());
      std::memcpy (target_block + field.offset, field.default_value.data (),
                   count);
    }
  }
}

runtime_storage &
runtime_storage_of (entt::registry &registry)
{
  using storage_handle = std::shared_ptr<runtime_storage>;
  auto &ctx = registry.ctx ();
  if (!ctx.contains<storage_handle> ()) {
    ctx.emplace<storage_handle> (std::make_shared<runtime_storage> ());
  }
  return *ctx.get<storage_handle> ();
}

runtime_storage *
try_runtime_storage_of (entt::registry &registry)
{
  using storage_handle = std::shared_ptr<runtime_storage>;
  auto &ctx = registry.ctx ();
  if (!ctx.contains<storage_handle> ()) {
    return nullptr;
  }
  return ctx.get<storage_handle> ().get ();
}

const runtime_storage *
try_runtime_storage_of (const entt::registry &registry)
{
  using storage_handle = std::shared_ptr<runtime_storage>;
  const auto &ctx = registry.ctx ();
  if (!ctx.contains<storage_handle> ()) {
    return nullptr;
  }
  return ctx.get<storage_handle> ().get ();
}

namespace
{

using das_kind = wsl::das::das_engine::field_type_kind;

template <typename T>
T
load_as (const std::uint8_t *bytes) noexcept
{
  T value{};
  std::memcpy (&value, bytes, sizeof value);
  return value;
}

template <typename T>
void
store_as (std::uint8_t *bytes, T value) noexcept
{
  std::memcpy (bytes, &value, sizeof value);
}

std::int64_t
load_signed (const std::uint8_t *bytes, std::uint32_t size) noexcept
{
  switch (size) {
  case 1:
    return load_as<std::int8_t> (bytes);
  case 2:
    return load_as<std::int16_t> (bytes);
  case 4:
    return load_as<std::int32_t> (bytes);
  default:
    return load_as<std::int64_t> (bytes);
  }
}

std::int64_t
load_unsigned (const std::uint8_t *bytes, std::uint32_t size) noexcept
{
  switch (size) {
  case 1:
    return load_as<std::uint8_t> (bytes);
  case 2:
    return load_as<std::uint16_t> (bytes);
  case 4:
    return load_as<std::uint32_t> (bytes);
  default:
    return static_cast<std::int64_t> (load_as<std::uint64_t> (bytes));
  }
}

void
store_integer (std::uint8_t *bytes, std::uint32_t size,
               std::int64_t value) noexcept
{
  switch (size) {
  case 1:
    store_as<std::int8_t> (bytes, static_cast<std::int8_t> (value));
    break;
  case 2:
    store_as<std::int16_t> (bytes, static_cast<std::int16_t> (value));
    break;
  case 4:
    store_as<std::int32_t> (bytes, static_cast<std::int32_t> (value));
    break;
  default:
    store_as<std::int64_t> (bytes, value);
    break;
  }
}

std::optional<double>
number_of (const rfl::Generic &value)
{
  const auto &variant = value.value ();
  if (const auto *number = std::get_if<double> (&variant)) {
    return *number;
  }
  if (const auto *integer = std::get_if<std::int64_t> (&variant)) {
    return static_cast<double> (*integer);
  }
  return std::nullopt;
}

std::optional<std::int64_t>
integer_of (const rfl::Generic &value)
{
  const auto &variant = value.value ();
  if (const auto *integer = std::get_if<std::int64_t> (&variant)) {
    return *integer;
  }
  if (const auto *number = std::get_if<double> (&variant)) {
    if (std::isfinite (*number) && std::floor (*number) == *number
        && std::fabs (*number) < 9.0e18) {
      return static_cast<std::int64_t> (*number);
    }
  }
  return std::nullopt;
}

} // namespace

rfl::Generic
encode_runtime_field (const runtime_field &field, const std::uint8_t *block)
{
  const std::uint8_t *bytes = block + field.offset;
  switch (field.kind) {
  case das_kind::boolean:
    return rfl::Generic (bytes[0] != 0);
  case das_kind::integer:
    return rfl::Generic (load_signed (bytes, field.size));
  case das_kind::unsigned_integer:
    return rfl::Generic (load_unsigned (bytes, field.size));
  case das_kind::floating:
    if (field.size == sizeof (float)) {
      return rfl::Generic (static_cast<double> (load_as<float> (bytes)));
    }
    return rfl::Generic (load_as<double> (bytes));
  case das_kind::floating_vector: {
    rfl::Generic::Array lanes;
    for (std::uint32_t i = 0; i < field.size / 4; ++i) {
      lanes.push_back (
          rfl::Generic (static_cast<double> (load_as<float> (bytes + 4 * i))));
    }
    return rfl::Generic (std::move (lanes));
  }
  case das_kind::integer_vector: {
    rfl::Generic::Array lanes;
    for (std::uint32_t i = 0; i < field.size / 4; ++i) {
      lanes.push_back (rfl::Generic (
          static_cast<std::int64_t> (load_as<std::int32_t> (bytes + 4 * i))));
    }
    return rfl::Generic (std::move (lanes));
  }
  case das_kind::string:
  case das_kind::unsupported:
    break;
  }
  return rfl::Generic ();
}

bool
decode_runtime_field (const runtime_field &field, const rfl::Generic &value,
                      std::uint8_t *block)
{
  std::uint8_t *bytes = block + field.offset;
  switch (field.kind) {
  case das_kind::boolean: {
    if (const auto *flag = std::get_if<bool> (&value.value ())) {
      bytes[0] = *flag ? 1 : 0;
      return true;
    }
    const auto integer = integer_of (value);
    if (!integer) {
      return false;
    }
    bytes[0] = *integer != 0 ? 1 : 0;
    return true;
  }
  case das_kind::integer:
  case das_kind::unsigned_integer: {
    const auto integer = integer_of (value);
    if (!integer) {
      return false;
    }
    store_integer (bytes, field.size, *integer);
    return true;
  }
  case das_kind::floating: {
    const auto number = number_of (value);
    if (!number) {
      return false;
    }
    if (field.size == sizeof (float)) {
      store_as<float> (bytes, static_cast<float> (*number));
    } else {
      store_as<double> (bytes, *number);
    }
    return true;
  }
  case das_kind::floating_vector:
  case das_kind::integer_vector: {
    const auto *lanes = std::get_if<rfl::Generic::Array> (&value.value ());
    const std::size_t count = field.size / 4;
    if (lanes == nullptr || lanes->size () != count) {
      return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
      if (field.kind == das_kind::floating_vector) {
        const auto number = number_of ((*lanes)[i]);
        if (!number) {
          return false;
        }
        store_as<float> (bytes + 4 * i, static_cast<float> (*number));
      } else {
        const auto integer = integer_of ((*lanes)[i]);
        if (!integer) {
          return false;
        }
        store_as<std::int32_t> (bytes + 4 * i,
                                static_cast<std::int32_t> (*integer));
      }
    }
    return true;
  }
  case das_kind::string:
  case das_kind::unsupported:
    break;
  }
  return false;
}

const runtime_field *
find_runtime_field (const runtime_layout &layout, std::string_view name)
{
  for (const runtime_field &field : layout.fields) {
    if (field.name == name) {
      return &field;
    }
  }
  return nullptr;
}

} // namespace wsl::reg
