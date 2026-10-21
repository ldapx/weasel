#include "runtime_project_module.hpp"
#include "../das/das_engine.hpp"

#include "../rsc/project.hpp"
#include "../rsc/scene.hpp"
#include "../comp/singl/runtime_context.hpp"
#include "component_registry.hpp"
#include "singleton_registry.hpp"
#include "system_factory_registry.hpp"

#include <cctype>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iterator>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <optional>
#include <ostream>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>
#include "../log/log.hpp"

namespace wsl
{

namespace reg
{

namespace runtime
{

namespace fs = std::filesystem;

namespace
{

constexpr std::string_view k_registration_cache_file
    = "runtime_registration_cache.json";

bool
is_cpp_file (const fs::path &path)
{
  const std::string ext = path.extension ().string ();
  return ext == ".cpp" || ext == ".cc" || ext == ".cxx";
}

bool
is_header_file (const fs::path &path)
{
  const std::string ext = path.extension ().string ();
  return ext == ".hpp" || ext == ".hh" || ext == ".hxx" || ext == ".h";
}

void
clear_runtime_registries (comp::singl::runtime_context &runtime_ctx)
{
  runtime_ctx.component_registry ().clear_runtime_components ();
  runtime_ctx.singleton_registry ().clear_runtime_singletons (
      runtime_ctx.world ());
  runtime_ctx.system_factory_registry ().clear_runtime_systems ();
}

std::string
bytes_to_hex (const std::vector<uint8_t> &bytes)
{
  static constexpr char digits[] = "0123456789abcdef";
  std::string hex;
  hex.reserve (bytes.size () * 2);
  for (const uint8_t byte : bytes) {
    hex.push_back (digits[(byte >> 4U) & 0x0FU]);
    hex.push_back (digits[byte & 0x0FU]);
  }
  return hex;
}

std::vector<uint8_t>
hex_to_bytes (const std::string &hex)
{
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

  std::vector<uint8_t> bytes;
  if ((hex.size () % 2) != 0) {
    return bytes;
  }
  bytes.reserve (hex.size () / 2);
  for (std::size_t i = 0; i < hex.size (); i += 2) {
    const int hi = nibble (hex[i]);
    const int lo = nibble (hex[i + 1]);
    if ((hi < 0) || (lo < 0)) {
      return {};
    }
    bytes.push_back (static_cast<uint8_t> ((hi << 4) | lo));
  }
  return bytes;
}

void
write_cached_registration (
    rapidjson::Writer<rapidjson::StringBuffer> &writer,
    const runtime_project_module::cached_registration &registration)
{
  writer.StartObject ();
  writer.Key ("type_id");
  writer.Uint64 (registration.type_id);
  writer.Key ("type_name");
  writer.String (
      registration.type_name.c_str (),
      static_cast<rapidjson::SizeType> (registration.type_name.size ()));
  writer.Key ("display_name");
  writer.String (
      registration.display_name.c_str (),
      static_cast<rapidjson::SizeType> (registration.display_name.size ()));
  writer.Key ("is_das_component");
  writer.Bool (registration.is_das_component);
  writer.Key ("das_struct_size");
  writer.Int (registration.das_struct_size);
  writer.Key ("script_path");
  writer.String (
      registration.script_path.c_str (),
      static_cast<rapidjson::SizeType> (registration.script_path.size ()));
  writer.Key ("das_fields");
  writer.StartArray ();
  for (const auto &f : registration.das_fields) {
    writer.StartObject ();
    writer.Key ("name");
    writer.String (f.name.c_str (),
                   static_cast<rapidjson::SizeType> (f.name.size ()));
    writer.Key ("type_name");
    writer.String (f.type_name.c_str (),
                   static_cast<rapidjson::SizeType> (f.type_name.size ()));
    writer.Key ("offset");
    writer.Int (f.offset);
    writer.Key ("size");
    writer.Int (f.size);
    writer.Key ("kind");
    writer.Int (f.kind);
    writer.Key ("default_hex");
    writer.String (f.default_hex.c_str (),
                   static_cast<rapidjson::SizeType> (f.default_hex.size ()));
    writer.EndObject ();
  }
  writer.EndArray ();
  writer.EndObject ();
}

runtime_project_module::cached_registration
make_cached_registration (const component_registry::descriptor &descriptor)
{
  runtime_project_module::cached_registration registration{};
  registration.type_id = descriptor.type_id;
  registration.type_name = descriptor.type_name;
  registration.display_name = descriptor.display_name;
  registration.is_das_component = descriptor.is_das_component;
  registration.das_struct_size = descriptor.das_struct_size;
  for (const auto &f : descriptor.das_fields) {
    registration.das_fields.push_back (
        { f.name, f.type_name, static_cast<int> (f.offset),
          static_cast<int> (f.size), static_cast<int> (f.kind),
          bytes_to_hex (f.default_value) });
  }
  return registration;
}

template <typename Descriptor>
runtime_project_module::cached_registration
make_cached_registration (const Descriptor &descriptor)
{
  runtime_project_module::cached_registration registration{};
  registration.type_id = descriptor.type_id;
  registration.type_name = descriptor.type_name;
  registration.display_name = descriptor.display_name;
  return registration;
}

// Daslang singletons need their value layout cached too: without it the
// descriptor is discovery-only and `singl add` has nothing to construct.
runtime_project_module::cached_registration
make_cached_singleton_registration (
    const singleton_registry::descriptor &descriptor)
{
  runtime_project_module::cached_registration registration{};
  registration.type_id = descriptor.type_id;
  registration.type_name = descriptor.type_name;
  registration.display_name = descriptor.display_name;
  registration.is_das_component = descriptor.is_das_singleton;
  registration.das_struct_size = descriptor.das_struct_size;
  for (const auto &f : descriptor.das_fields) {
    registration.das_fields.push_back ({ f.name, f.type_name, f.offset, f.size,
                                         static_cast<int> (f.kind),
                                         bytes_to_hex (f.default_value) });
  }
  return registration;
}

bool
read_cached_registration (const rapidjson::Value &value,
                          runtime_project_module::cached_registration &out)
{
  if (!value.IsObject () || !value.HasMember ("type_id")
      || !value.HasMember ("type_name") || !value.HasMember ("display_name")
      || !value["type_id"].IsUint64 () || !value["type_name"].IsString ()
      || !value["display_name"].IsString ()) {
    return false;
  }

  out.type_id = value["type_id"].GetUint64 ();
  out.type_name = value["type_name"].GetString ();
  out.display_name = value["display_name"].GetString ();

  // Optional das fields (backward-compatible with old caches)
  out.is_das_component = value.HasMember ("is_das_component")
                                 && value["is_das_component"].IsBool ()
                             ? value["is_das_component"].GetBool ()
                             : false;
  out.das_struct_size
      = value.HasMember ("das_struct_size") && value["das_struct_size"].IsInt ()
            ? value["das_struct_size"].GetInt ()
            : 0;
  out.script_path
      = value.HasMember ("script_path") && value["script_path"].IsString ()
            ? value["script_path"].GetString ()
            : "";
  if (value.HasMember ("das_fields") && value["das_fields"].IsArray ()) {
    for (const auto &f : value["das_fields"].GetArray ()) {
      if (!f.IsObject () || !f.HasMember ("name") || !f["name"].IsString ()
          || !f.HasMember ("type_name") || !f["type_name"].IsString ()
          || !f.HasMember ("offset") || !f["offset"].IsInt ()
          || !f.HasMember ("size") || !f["size"].IsInt ()
          || !f.HasMember ("kind") || !f["kind"].IsInt ()) {
        continue;
      }
      out.das_fields.push_back (
          { f["name"].GetString (), f["type_name"].GetString (),
            f["offset"].GetInt (), f["size"].GetInt (), f["kind"].GetInt (),
            (f.HasMember ("default_hex") && f["default_hex"].IsString ())
                ? f["default_hex"].GetString ()
                : std::string{} });
    }
  }
  return true;
}

bool
read_cached_registration_array (
    const rapidjson::Document &doc, const char *name,
    std::vector<runtime_project_module::cached_registration> &out)
{
  if (!doc.HasMember (name) || !doc[name].IsArray ()) {
    return false;
  }

  for (const rapidjson::Value &value : doc[name].GetArray ()) {
    runtime_project_module::cached_registration registration{};
    if (!read_cached_registration (value, registration)) {
      return false;
    }
    out.push_back (std::move (registration));
  }

  return true;
}

} // anonymous namespace

// Member function definitions

bool
runtime_project_module::is_das_file (const fs::path &path)
{
  return path.extension () == ".das";
}

runtime_project_module::runtime_project_module (
    comp::singl::runtime_context *runtime_ctx)
    : m_runtime_ctx (runtime_ctx)
{
}

runtime_project_module::~runtime_project_module ()
{
  // Ensure we unload runtime registrations before destruction to
  // avoid registry destructors invoking std::function targets that reference
  // shared library code which may already be freed.
  unload ();
}

void
runtime_project_module::gather_files (const fs::path &base, source_set &out)
{
  if (base.empty () || !fs::exists (base)) {
    return;
  }

  for (const auto &entry : fs::recursive_directory_iterator (base)) {
    if (!entry.is_regular_file ()) {
      continue;
    }

    const fs::path path = fs::weakly_canonical (entry.path ());

    if (is_cpp_file (path)) {
      out.cpp_sources.push_back (path);
    } else if (is_header_file (path)) {
      out.headers.push_back (path);
    } else if (is_das_file (path)) {
      out.das_sources.push_back (path);
    }
  }

  auto by_path = [] (const fs::path &lhs, const fs::path &rhs) {
    return lhs.generic_string () < rhs.generic_string ();
  };

  std::sort (out.headers.begin (), out.headers.end (), by_path);
  std::sort (out.cpp_sources.begin (), out.cpp_sources.end (), by_path);
  std::sort (out.das_sources.begin (), out.das_sources.end (), by_path);
}

std::size_t
runtime_project_module::compute_source_hash (const source_set &sources)
{
  std::size_t seed = std::hash<std::string_view>{}("runtime-type-id-path-v2");

  auto hash_combine = [] (std::size_t s, std::size_t v) {
    return s ^ (v + 0x9e3779b9 + (s << 6) + (s >> 2));
  };

  seed = hash_combine (seed, sources.headers.size ());
  seed = hash_combine (seed, sources.cpp_sources.size ());
  seed = hash_combine (seed, sources.das_sources.size ());

  for (const fs::path &path : sources.headers) {
    seed
        = hash_combine (seed, std::hash<std::string>{}(path.generic_string ()));
    std::error_code ec;
    auto ftime = fs::last_write_time (path, ec);
    if (!ec) {
      seed = hash_combine (
          seed, static_cast<std::size_t> (ftime.time_since_epoch ().count ()));
    }
  }

  for (const fs::path &path : sources.cpp_sources) {
    seed
        = hash_combine (seed, std::hash<std::string>{}(path.generic_string ()));
    std::error_code ec;
    auto ftime = fs::last_write_time (path, ec);
    if (!ec) {
      seed = hash_combine (
          seed, static_cast<std::size_t> (ftime.time_since_epoch ().count ()));
    }
  }

  for (const fs::path &path : sources.das_sources) {
    seed
        = hash_combine (seed, std::hash<std::string>{}(path.generic_string ()));
    std::error_code ec;
    auto ftime = fs::last_write_time (path, ec);
    if (!ec) {
      seed = hash_combine (
          seed, static_cast<std::size_t> (ftime.time_since_epoch ().count ()));
    }
  }

  return seed;
}

fs::path
runtime_project_module::registration_cache_path (const fs::path &project_root)
{
  return project_root / "build" / "weasel_runtime" / k_registration_cache_file;
}

bool
runtime_project_module::read_registration_cache (const fs::path &path,
                                                 std::size_t source_hash,
                                                 registration_cache &out)
{
  std::ifstream input (path);
  if (!input) {
    return false;
  }

  std::string const content ((std::istreambuf_iterator<char> (input)),
                             std::istreambuf_iterator<char> ());
  rapidjson::Document doc;
  if (doc.Parse (content.c_str ()).HasParseError () || !doc.IsObject ()) {
    return false;
  }

  if (!doc.HasMember ("version") || !doc["version"].IsUint ()
      || doc["version"].GetUint () != 2 || !doc.HasMember ("source_hash")
      || !doc["source_hash"].IsUint64 ()) {
    return false;
  }

  if (static_cast<std::size_t> (doc["source_hash"].GetUint64 ())
      != source_hash) {
    return false;
  }

  registration_cache cache{};
  cache.source_hash = source_hash;
  if (!read_cached_registration_array (doc, "components", cache.components)
      || !read_cached_registration_array (doc, "singletons", cache.singletons)
      || !read_cached_registration_array (doc, "systems", cache.systems)) {
    return false;
  }

  out = std::move (cache);
  return true;
}

bool
runtime_project_module::write_registration_cache () const
{
  if (m_runtime_ctx == nullptr || m_loaded_project_root.empty ()
      || m_source_hash == 0) {
    return false;
  }

  registration_cache cache{};
  cache.source_hash = m_source_hash;

  for (const component_registry::descriptor *desc :
       m_runtime_ctx->component_registry ().get_world_components (
           world_component_order::type_id)) {
    if (desc != nullptr && desc->runtime_registered) {
      cache.components.push_back (make_cached_registration (*desc));
    }
  }

  for (const singleton_registry::descriptor *desc :
       m_runtime_ctx->singleton_registry ().get_singleton_components (
           singleton_component_order::type_id)) {
    if (desc != nullptr && desc->runtime_registered) {
      cache.singletons.push_back (make_cached_singleton_registration (*desc));
    }
  }

  // Build a map from type_id to script_path for das systems
  std::unordered_map<std::uint64_t, std::string> das_script_paths;
  for (const auto &reg : m_das_registrations) {
    if (reg.kind == das_registration::system) {
      das_script_paths[reg.type_id] = reg.script_path;
    }
  }

  for (const system_factory_registry::system_descriptor *desc :
       m_runtime_ctx->system_factory_registry ().get_systems (
           system_order::type_id)) {
    if (desc != nullptr && desc->runtime_registered) {
      auto cached = make_cached_registration (*desc);
      // Populate script_path from m_das_registrations if available
      auto it
          = das_script_paths.find (static_cast<std::uint64_t> (desc->type_id));
      if (it != das_script_paths.end ()) {
        cached.script_path = it->second;
      }
      cache.systems.push_back (std::move (cached));
    }
  }

  const fs::path path = registration_cache_path (m_loaded_project_root);
  std::error_code ec;
  fs::create_directories (path.parent_path (), ec);
  if (ec) {
    wsl::log::xmake ()->warn ("Could not create runtime cache directory: {}",
                              ec.message ());
    return false;
  }

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer (buffer);
  writer.StartObject ();
  writer.Key ("version");
  writer.Uint (2);
  writer.Key ("source_hash");
  writer.Uint64 (static_cast<std::uint64_t> (cache.source_hash));

  auto write_array
      = [&writer] (const char *name,
                   const std::vector<cached_registration> &entries) {
          writer.Key (name);
          writer.StartArray ();
          for (const cached_registration &entry : entries) {
            write_cached_registration (writer, entry);
          }
          writer.EndArray ();
        };

  write_array ("components", cache.components);
  write_array ("singletons", cache.singletons);
  write_array ("systems", cache.systems);
  writer.EndObject ();

  // Write atomically: write to a temp file then rename
  const fs::path tmp_path
      = path.parent_path () / (path.filename ().string () + ".tmp");
  {
    std::ofstream out (tmp_path, std::ios::binary | std::ios::trunc);
    if (!out) {
      wsl::log::xmake ()->warn ("Could not write runtime cache temp file: {}",
                                tmp_path.string ());
      return false;
    }
    out << buffer.GetString ();
    out.flush ();
    if (!out.good ()) {
      wsl::log::xmake ()->warn ("Failed writing to runtime cache temp file: {}",
                                tmp_path.string ());
      // attempt to remove temp file
      std::error_code rem_ec;
      fs::remove (tmp_path, rem_ec);
      return false;
    }
  }

  std::error_code rename_ec;
  fs::rename (tmp_path, path, rename_ec);
  if (rename_ec) {
    wsl::log::xmake ()->warn (
        "Could not move runtime cache into place: {} -> {} ({})",
        tmp_path.string (), path.string (), rename_ec.message ());
    // best-effort cleanup
    std::error_code rem_ec;
    fs::remove (tmp_path, rem_ec);
    return false;
  }

  return true;
}

bool
runtime_project_module::apply_registration_cache (
    const registration_cache &cache)
{
  clear_runtime_registries (*m_runtime_ctx);

  for (const cached_registration &entry : cache.components) {
    if (!entry.is_das_component) {
      continue;
    }
    std::vector<component_registry::descriptor::das_field> fields;
    for (const auto &f : entry.das_fields) {
      fields.push_back (
          { f.name, f.type_name, static_cast<std::uint32_t> (f.offset),
            static_cast<std::uint32_t> (f.size),
            static_cast<wsl::das::das_engine::field_type_kind> (f.kind),
            hex_to_bytes (f.default_hex) });
    }
    if (!m_runtime_ctx->component_registry ()
             .register_cached_runtime_world_component (
                 static_cast<entt::id_type> (entry.type_id), entry.type_name,
                 entry.display_name,
                 entry.is_das_component ? entry.das_struct_size : 0,
                 std::move (fields))) {
      clear_runtime_registries (*m_runtime_ctx);
      return false;
    }

    // Also register the short and qualified names used by typeinfo typename.
    m_runtime_ctx->component_registry ().register_component_type_info (
        entry.type_name, static_cast<uint64_t> (entry.type_id),
        reg::ComponentKind::DAS_SCRIPT,
        static_cast<size_t> (entry.das_struct_size));
    // `typeinfo typename` yields "player_control::PlayerControl const", which
    // is the name finalize_load() registers.  Use the same spelling here; the
    // previous "player_control::player_control const" only worked on the
    // full-compile path, so cache-loaded projects could not resolve the type.
    std::string pascal_name;
    bool capitalize_next = true;
    for (char ch : entry.type_name) {
      if (ch == '_' || ch == '-') {
        capitalize_next = true;
      } else if (capitalize_next) {
        pascal_name += static_cast<char> (
            std::toupper (static_cast<unsigned char> (ch)));
        capitalize_next = false;
      } else {
        pascal_name += ch;
      }
    }
    std::string qualified_name
        = entry.type_name + "::" + pascal_name + " const";
    m_runtime_ctx->component_registry ().register_component_type_info (
        qualified_name, static_cast<uint64_t> (entry.type_id),
        reg::ComponentKind::DAS_SCRIPT,
        static_cast<size_t> (entry.das_struct_size));
    // Keep the old spelling as an alias so existing caches/scripts still
    // resolve.
    m_runtime_ctx->component_registry ().register_component_type_info (
        entry.type_name + "::" + entry.type_name + " const",
        static_cast<uint64_t> (entry.type_id), reg::ComponentKind::DAS_SCRIPT,
        static_cast<size_t> (entry.das_struct_size));
  }

  for (const cached_registration &entry : cache.singletons) {
    std::vector<singleton_registry::descriptor::das_field> fields;
    for (const auto &f : entry.das_fields) {
      fields.push_back (
          { f.name, f.type_name, f.offset, f.size,
            static_cast<wsl::das::das_engine::field_type_kind> (f.kind),
            hex_to_bytes (f.default_hex) });
    }
    if (!m_runtime_ctx->singleton_registry ()
             .register_cached_runtime_singleton_component (
                 static_cast<entt::id_type> (entry.type_id), entry.type_name,
                 entry.display_name, entry.das_struct_size,
                 std::move (fields))) {
      clear_runtime_registries (*m_runtime_ctx);
      return false;
    }
  }

  for (const cached_registration &entry : cache.systems) {
#if WEASEL_HAS_DASLANG
    // Must use the Daslang-aware overload.  The 3-argument one builds a
    // `placeholder_system` for every user system, which (a) never runs any
    // on_update, and (b) shares one type id across all instances, so
    // scene::add_system_instance()'s duplicate check silently keeps only the
    // FIRST user system of a scene.  das_engine::instantiate_class() compiles
    // the .das on demand, so this still works straight from the cache.
    if (!m_runtime_ctx->system_factory_registry ()
             .register_cached_runtime_system (
                 static_cast<entt::id_type> (entry.type_id), entry.type_name,
                 entry.display_name, entry.script_path, *get_das_engine ())) {
      clear_runtime_registries (*m_runtime_ctx);
      return false;
    }
#else
    if (!m_runtime_ctx->system_factory_registry ()
             .register_cached_runtime_system (
                 static_cast<entt::id_type> (entry.type_id), entry.type_name,
                 entry.display_name)) {
      clear_runtime_registries (*m_runtime_ctx);
      return false;
    }
#endif
  }

  return true;
}

void
runtime_project_module::load_das_registrations_from_cache (
    const registration_cache &cache)
{
  m_das_registrations.clear ();

  auto convert_fields = [] (const std::vector<cached_das_field> &cached)
      -> std::vector<wsl::das::das_engine::field_info> {
    std::vector<wsl::das::das_engine::field_info> fields;
    fields.reserve (cached.size ());
    for (const auto &f : cached) {
      fields.push_back (
          { f.name, f.type_name, f.offset, f.size,
            static_cast<wsl::das::das_engine::field_type_kind> (f.kind),
            hex_to_bytes (f.default_hex) });
    }
    return fields;
  };

  for (const cached_registration &entry : cache.components) {
    if (entry.is_das_component) {
      m_das_registrations.push_back ({
          das_registration::component,
          entry.type_name,
          entry.display_name,
          entry.type_id,
          entry.das_struct_size,
          {},
          convert_fields (entry.das_fields),
      });
    }
  }

  for (const cached_registration &entry : cache.singletons) {
    if (entry.is_das_component) {
      m_das_registrations.push_back ({
          das_registration::singleton,
          entry.type_name,
          entry.display_name,
          entry.type_id,
          entry.das_struct_size,
          {},
          convert_fields (entry.das_fields),
      });
    }
  }

  for (const cached_registration &entry : cache.systems) {
    m_das_registrations.push_back ({
        das_registration::system,
        entry.type_name,
        entry.display_name,
        entry.type_id,
        entry.das_struct_size,
        entry.script_path,
        convert_fields (entry.das_fields),
    });
  }

  wsl::log::xmake ()->debug (
      "load_das_registrations_from_cache: Restored {} das registrations",
      m_das_registrations.size ());
}

// Register the project's component / singleton / system directories as daScript
// extra roots so that bare `require` statements (e.g. `require mouse_rotate`
// from a system file) resolve across directories.  Both the full-compile path
// and the metadata-cache path need this: without it a cached system that
// `require`s a project module fails to compile lazily.
void
runtime_project_module::register_project_fs_roots (const fs::path &project_root,
                                                   const rsc::project &project,
                                                   wsl::das::das_engine &engine)
{
  if (!engine.initialize ()) {
    wsl::log::xmake ()->error ("Failed to initialize daslang engine.");
    return;
  }

  const auto comp_dir
      = fs::weakly_canonical (project_root / project.components_path).string ();
  const auto singl_dir
      = fs::weakly_canonical (project_root / project.singletons_path).string ();
  const auto sys_dir
      = fs::weakly_canonical (project_root / project.systems_path).string ();

  engine.addFsRoot ("components", comp_dir);
  engine.addFsRoot ("singletons", singl_dir);
  engine.addFsRoot ("systems", sys_dir);
}

bool
runtime_project_module::load_cached_metadata (const rsc::project &project)
{
  if (m_load_state != load_state_t::unloaded) {
    m_last_status = "Runtime metadata is already loaded.";
    return true;
  }

  if (m_runtime_ctx == nullptr) {
    m_last_status = "Runtime metadata cache has no runtime context.";
    return false;
  }

  const fs::path project_root = fs::weakly_canonical (project.root_path);

  source_set sources;
  gather_files (project_root / project.components_path, sources);
  gather_files (project_root / project.systems_path, sources);
  gather_files (project_root / project.singletons_path, sources);

  const std::size_t current_hash = compute_source_hash (sources);
  registration_cache cache{};
  if (!read_registration_cache (registration_cache_path (project_root),
                                current_hash, cache)) {
    m_last_status = "Runtime metadata cache is missing or stale.";
    return false;
  }

  // Trust the cache only when it is plausible: an empty registry while
  // Daslang sources exist means the cache was written by a broken run.
  // Escalate to a full compile instead of poisoning this session with an
  // empty registry, and delete the bad file so it cannot strike again.
  const std::size_t cached_total = cache.components.size ()
                                   + cache.singletons.size ()
                                   + cache.systems.size ();
  if (!sources.das_sources.empty () && cached_total == 0) {
    m_last_status
        = "Runtime metadata cache is empty although Daslang sources exist; "
          "ignoring it.";
    m_last_error = m_last_status;
    wsl::log::xmake ()->warn ("{}", m_last_status);
    std::error_code ec;
    fs::remove (registration_cache_path (project_root), ec);
    return false;
  }

  if (!apply_registration_cache (cache)) {
    m_last_status
        = "Runtime metadata cache contains conflicting registrations.";
    m_last_error = m_last_status;
    wsl::log::xmake ()->warn ("{}", m_last_status);
    return false;
  }
  load_das_registrations_from_cache (cache);
  register_project_fs_roots (project_root, project, *get_das_engine ());
  m_loaded_project_root = project_root;
  m_source_hash = current_hash;
  m_load_state = load_state_t::metadata_cache;
  m_last_error.clear ();
  m_last_status = "Runtime metadata loaded from cache.";
  wsl::log::xmake ()->debug ("{}", m_last_status);
  return true;
}

void
runtime_project_module::invalidate (const rsc::project *project)
{
  suspend_script_systems ();

  // Drop in-memory state unconditionally so the next command starts fresh.
  if (m_load_state != load_state_t::unloaded && m_runtime_ctx != nullptr) {
    clear_runtime_registries (*m_runtime_ctx);
  }
  m_das_registrations.clear ();

  // Shut down the Daslang engine so stale programs are released.
  if (m_das_engine) {
    m_das_engine->shutdown ();
    m_das_engine.reset ();
  }
  m_staged_das_engine.reset ();
  m_suspended_script_systems.clear ();
  m_suspended_event_connections.clear ();

  m_load_state = load_state_t::unloaded;
  m_source_hash = 0;

  // Delete the on-disk cache for the requested (or currently loaded) root.
  fs::path root;
  if (project != nullptr && !project->root_path.empty ()) {
    root = fs::weakly_canonical (project->root_path);
  } else if (!m_loaded_project_root.empty ()) {
    root = m_loaded_project_root;
  }
  if (!root.empty ()) {
    std::error_code ec;
    fs::remove (registration_cache_path (root), ec);
    if (ec) {
      wsl::log::xmake ()->debug ("Cache removal ignored: {}", ec.message ());
    }
  }

  m_loaded_project_root.clear ();
  m_last_error.clear ();
  m_last_status = "Runtime module state invalidated.";
}

void
runtime_project_module::clear_cached_metadata ()
{
  if (m_load_state == load_state_t::metadata_cache
      && m_runtime_ctx != nullptr) {
    clear_runtime_registries (*m_runtime_ctx);
  }
  m_das_registrations.clear ();
  m_load_state = load_state_t::unloaded;
}

bool
runtime_project_module::finalize_load ()
{
  if (m_runtime_ctx == nullptr) {
    m_last_status = "Cannot finalize runtime module without a runtime context.";
    m_last_error = m_last_status;
    m_load_state = load_state_t::unloaded;
    return false;
  }

  clear_runtime_registries (*m_runtime_ctx);

  if (m_staged_das_engine != nullptr) {
    m_das_engine.swap (m_staged_das_engine);
    m_staged_das_engine.reset ();
  }

  // Re-apply stored daslang registrations
  for (const auto &reg : m_das_registrations) {
    // Convert field_info to descriptor::das_field
    std::vector<wsl::reg::component_registry::descriptor::das_field> fields;
    for (const auto &fi : reg.fields) {
      fields.push_back (
          { fi.name, fi.type_name, static_cast<std::uint32_t> (fi.offset),
            static_cast<std::uint32_t> (fi.size), fi.kind, fi.default_value });
    }

    switch (reg.kind) {
    case das_registration::component:
      if (!m_runtime_ctx->component_registry ()
               .register_cached_runtime_world_component (
                   static_cast<entt::id_type> (reg.type_id), reg.type_name,
                   reg.display_name, reg.struct_size, std::move (fields))) {
        goto registration_failed;
      }

      // Also register in the lookup table for generic dispatch
      m_runtime_ctx->component_registry ().register_component_type_info (
          reg.type_name, static_cast<uint64_t> (reg.type_id),
          reg::ComponentKind::DAS_SCRIPT,
          static_cast<size_t> (reg.struct_size));
      // Also register with the full qualified name that typeinfo typename
      // returns (e.g., "mouse_rotate::MouseRotate const")
      {
        std::string pascal_name;
        bool capitalize_next = true;
        for (char ch : reg.type_name) {
          if (ch == '_' || ch == '-') {
            capitalize_next = true;
          } else if (capitalize_next) {
            auto upper = static_cast<char> (
                std::toupper (static_cast<unsigned char> (ch)));
            pascal_name += upper;
            capitalize_next = false;
          } else {
            pascal_name += ch;
          }
        }
        std::string qualified_name
            = reg.type_name + "::" + pascal_name + " const";
        m_runtime_ctx->component_registry ().register_component_type_info (
            qualified_name, static_cast<uint64_t> (reg.type_id),
            reg::ComponentKind::DAS_SCRIPT,
            static_cast<size_t> (reg.struct_size));
      }

      break;
    case das_registration::singleton: {
      std::vector<singleton_registry::descriptor::das_field> fields;
      fields.reserve (reg.fields.size ());
      for (const auto &f : reg.fields) {
        fields.push_back (
            { f.name, f.type_name, f.offset, f.size, f.kind, f.default_value });
      }
      if (!m_runtime_ctx->singleton_registry ()
               .register_cached_runtime_singleton_component (
                   static_cast<entt::id_type> (reg.type_id), reg.type_name,
                   reg.display_name, reg.struct_size, std::move (fields))) {
        goto registration_failed;
      }
      break;
    }
    case das_registration::system:
#if WEASEL_HAS_DASLANG
      if (!m_runtime_ctx->system_factory_registry ()
               .register_cached_runtime_system (
                   static_cast<entt::id_type> (reg.type_id), reg.type_name,
                   reg.display_name, reg.script_path, *get_das_engine ())) {
        goto registration_failed;
      }
#else
      if (!m_runtime_ctx->system_factory_registry ()
               .register_cached_runtime_system (
                   static_cast<entt::id_type> (reg.type_id), reg.type_name,
                   reg.display_name)) {
        goto registration_failed;
      }
#endif
      break;
    }
  }

  m_load_state = load_state_t::loaded;
  m_last_error.clear ();
  write_registration_cache ();
  restore_script_systems ();
  return true;

registration_failed:
  clear_runtime_registries (*m_runtime_ctx);
  m_suspended_script_systems.clear ();
  m_suspended_event_connections.clear ();
  m_das_registrations.clear ();
  m_load_state = load_state_t::unloaded;
  m_last_status
      = "Runtime registration failed; no script systems were restored.";
  m_last_error = m_last_status;
  wsl::log::xmake ()->error ("{}", m_last_status);
  return false;
}

void
runtime_project_module::suspend_script_systems ()
{
  if (m_runtime_ctx == nullptr || !m_suspended_script_systems.empty ()) {
    return;
  }

  std::unordered_set<entt::id_type> script_type_ids;
  for (const auto &scene : m_runtime_ctx->world ().get_scenes ()) {
    if (scene == nullptr) {
      continue;
    }
    for (sys::ecs_system *system : scene->get_systems ()) {
      if (system != nullptr
          && std::string_view (system->get_type_name ()) == "das_system") {
        script_type_ids.insert (system->get_type_id ());
        m_suspended_script_systems.push_back (
            { scene.get (), system->get_type_id () });
      }
    }
  }

  if (script_type_ids.empty ()) {
    return;
  }

  for (const event::event_connection_data &connection :
       m_runtime_ctx->event_hub ().get_all_connections ()) {
    if (script_type_ids.contains (connection.system_type_id)) {
      m_suspended_event_connections.push_back ({ connection.event_type_id,
                                                 connection.system_type_id,
                                                 connection.handler_name });
    }
  }

  for (const suspended_script_system &entry : m_suspended_script_systems) {
    if (entry.scene == nullptr) {
      continue;
    }
    for (sys::ecs_system *system : entry.scene->get_systems ()) {
      if (system != nullptr && system->get_type_id () == entry.type_id
          && std::string_view (system->get_type_name ()) == "das_system") {
        entry.scene->remove_system (system);
        break;
      }
    }
  }
}

void
runtime_project_module::restore_script_systems ()
{
  if (m_runtime_ctx == nullptr) {
    m_suspended_script_systems.clear ();
    m_suspended_event_connections.clear ();
    return;
  }

  auto &factories = m_runtime_ctx->system_factory_registry ();
  for (const suspended_script_system &entry : m_suspended_script_systems) {
    if (entry.scene == nullptr) {
      continue;
    }
    auto system = factories.create_system (entry.type_id, *entry.scene);
    if (system != nullptr) {
      entry.scene->add_system_instance (std::move (system));
    } else {
      wsl::log::xmake ()->error (
          "Could not restore Daslang system {:#x} after reload", entry.type_id);
    }
  }

  for (const suspended_event_connection &connection :
       m_suspended_event_connections) {
    m_runtime_ctx->event_hub ().connect (connection.event_type_id,
                                         connection.system_type_id,
                                         connection.handler_name);
  }

  m_suspended_script_systems.clear ();
  m_suspended_event_connections.clear ();
}

void
runtime_project_module::unload ()
{
  if (m_load_state == load_state_t::unloaded) {
    return;
  }

  wsl::log::xmake ()->trace ("Unloading module");

  suspend_script_systems ();

  // Clear runtime registries before releasing Daslang programs and contexts.
  if (m_runtime_ctx != nullptr) {
    clear_runtime_registries (*m_runtime_ctx);
  }

  // Shutdown daslang engine
  if (m_das_engine) {
    m_das_engine->shutdown ();
    m_das_engine.reset ();
  }
  m_staged_das_engine.reset ();
  m_suspended_script_systems.clear ();
  m_suspended_event_connections.clear ();

  m_load_state = load_state_t::unloaded;
  m_loaded_project_root.clear ();
  m_source_hash = 0;
  m_last_status = "Module unloaded.";
}

std::size_t
runtime_project_module::das_component_count () const
{
  std::size_t count = 0;
  for (const das_registration &reg : m_das_registrations) {
    if (reg.kind == das_registration::component)
      ++count;
  }
  return count;
}

std::size_t
runtime_project_module::das_singleton_count () const
{
  std::size_t count = 0;
  for (const das_registration &reg : m_das_registrations) {
    if (reg.kind == das_registration::singleton)
      ++count;
  }
  return count;
}

std::size_t
runtime_project_module::das_system_count () const
{
  std::size_t count = 0;
  for (const das_registration &reg : m_das_registrations) {
    if (reg.kind == das_registration::system)
      ++count;
  }
  return count;
}

wsl::das::das_engine *
runtime_project_module::get_das_engine ()
{
  if (!m_das_engine) {
    m_das_engine = std::make_unique<wsl::das::das_engine> ();
  }
  return m_das_engine.get ();
}

bool
runtime_project_module::compile_and_load (const rsc::project &project)
{
  const fs::path project_root = fs::weakly_canonical (project.root_path);

  wsl::log::xmake ()->trace ("Compile and load started for project root: {}",
                             project_root.string ());

  source_set sources;
  wsl::log::xmake ()->trace (
      "Gathering Daslang files from components, systems, and singletons...");
  wsl::log::xmake ()->trace (
      "  components_path: {}",
      (project_root / project.components_path).string ());
  wsl::log::xmake ()->trace ("  systems_path: {}",
                             (project_root / project.systems_path).string ());
  wsl::log::xmake ()->trace (
      "  singletons_path: {}",
      (project_root / project.singletons_path).string ());
  gather_files (project_root / project.components_path, sources);
  gather_files (project_root / project.systems_path, sources);
  gather_files (project_root / project.singletons_path, sources);
  wsl::log::xmake ()->trace (
      "Gathered {} headers, {} cpp sources, and {} das sources",
      sources.headers.size (), sources.cpp_sources.size (),
      sources.das_sources.size ());

  if (!sources.cpp_sources.empty () || !sources.headers.empty ()) {
    m_last_status = "User C++ runtime components/systems are not supported; "
                    "use Daslang. ";
    m_last_error = m_last_status;
    wsl::log::xmake ()->error ("{}", m_last_status);
    return false;
  }

  const std::size_t current_hash = compute_source_hash (sources);

  if (m_load_state == load_state_t::loaded && current_hash == m_source_hash) {
    m_loaded_project_root = project_root;
    m_last_status
        = "Runtime module is already up to date (no changes detected).";
    wsl::log::xmake ()->trace ("{}", m_last_status);
    return true;
  }

  if (m_load_state == load_state_t::loaded) {
    suspend_script_systems ();
  }

  if (m_load_state == load_state_t::metadata_cache) {
    clear_runtime_registries (*m_runtime_ctx);
    m_load_state = load_state_t::unloaded;
  }

  const auto previous_registrations = m_das_registrations;
  const auto previous_project_root = m_loaded_project_root;
  const auto restore_previous_compile_state = [&] {
    m_das_registrations = previous_registrations;
    m_loaded_project_root = previous_project_root;
    restore_script_systems ();
  };

  m_loaded_project_root = project_root;
  m_das_registrations.clear ();

  // User runtime code is Daslang source. There is no shared-library cache.
  if (sources.cpp_sources.empty () && sources.das_sources.empty ()) {
    m_last_status = "No source files found to compile.";
    wsl::log::xmake ()->debug ("{}", m_last_status);
    m_staged_das_engine = std::make_unique<wsl::das::das_engine> ();
    m_load_state = load_state_t::loaded;
    m_source_hash = current_hash;
    return true;
  }

  // Compile first; only mark the module as loaded once every source ran
  // successfully. A failed compile must leave the state at `unloaded` so the
  // next attempt is a real retry instead of being short-circuited.
  wsl::log::xmake ()->debug ("Daslang runtime sources discovered.");

  // Execute daslang files and store registrations for finalize_load
  auto staged_engine = std::make_unique<wsl::das::das_engine> ();
  if (!sources.das_sources.empty ()) {
    wsl::log::xmake ()->debug ("Executing {} daslang files...",
                               sources.das_sources.size ());
    if (!staged_engine->initialize ()) {
      m_last_status = "Failed to initialize daslang engine.";
      m_last_error = m_last_status + "\n" + staged_engine->last_error ();
      wsl::log::xmake ()->error ("{}", m_last_error);
      restore_previous_compile_state ();
      return false;
    }

    register_project_fs_roots (project_root, project, *staged_engine);

    for (const auto &das_file : sources.das_sources) {
      wsl::log::xmake ()->debug ("Executing daslang file: {}",
                                 das_file.string ());
      if (!staged_engine->execute_file (das_file)) {
        m_last_status
            = "Failed to execute daslang file: " + das_file.string () + ".";
        m_last_error = m_last_status + "\n" + staged_engine->last_error ();
        wsl::log::xmake ()->error ("{}", m_last_error);
        restore_previous_compile_state ();
        return false;
      }

      // Store the registration for apply in finalize_load
      const auto canonical = fs::weakly_canonical (das_file).generic_string ();
      const auto stem = das_file.stem ().string ();

      // Convert snake_case stem to Title Case for display and PascalCase for
      // lookup
      std::string titled;
      std::string pascal;
      bool capitalize_next = true;
      for (char ch : stem) {
        if (ch == '_' || ch == '-') {
          titled += ' ';
          capitalize_next = true;
        } else if (capitalize_next) {
          auto upper = static_cast<char> (
              std::toupper (static_cast<unsigned char> (ch)));
          titled += upper;
          pascal += upper;
          capitalize_next = false;
        } else {
          titled += ch;
          pascal += ch;
        }
      }

      const std::string type_key
          = fs::relative (das_file, project_root).generic_string ();
      const std::uint64_t type_id = static_cast<std::uint64_t> (
          entt::hashed_string{ type_key.c_str () });
      const std::string canonical_path = das_file.generic_string ();
      const auto collision = std::find_if (
          m_das_registrations.begin (), m_das_registrations.end (),
          [type_id, &canonical_path] (const das_registration &existing) {
            return existing.type_id == type_id
                   && existing.script_path != canonical_path;
          });
      if (collision != m_das_registrations.end ()) {
        m_last_status = "Runtime type identifier collision between '"
                        + collision->script_path + "' and '" + canonical_path
                        + "'.";
        m_last_error = m_last_status;
        wsl::log::xmake ()->error ("{}", m_last_status);
        restore_previous_compile_state ();
        return false;
      }

      // Extract struct fields from the compiled das file
      // Try PascalCase first (e.g. "MouseRotate"), then stem (e.g.
      // "mouse_rotate")
      std::string struct_name = pascal;
      auto si = staged_engine->get_struct_info (das_file, struct_name);
      if (si.fields.empty () && struct_name != stem) {
        si = staged_engine->get_struct_info (das_file, stem);
      }

      const auto comp_dir
          = fs::weakly_canonical (project_root / project.components_path)
                .string ();
      const auto singl_dir
          = fs::weakly_canonical (project_root / project.singletons_path)
                .string ();
      const auto sys_dir
          = fs::weakly_canonical (project_root / project.systems_path)
                .string ();
      const auto display_name_for = [&] (const std::string &category_dir) {
        const std::filesystem::path relative_parent
            = fs::relative (das_file.parent_path (), category_dir);
        if (relative_parent.empty () || relative_parent == ".") {
          return titled;
        }
        return relative_parent.generic_string () + " / " + titled;
      };

      if (canonical.compare (0, comp_dir.size (), comp_dir) == 0) {
        m_das_registrations.push_back (
            { das_registration::component, stem, display_name_for (comp_dir),
              type_id, si.size_of, das_file.string (), std::move (si.fields) });
      } else if (canonical.compare (0, singl_dir.size (), singl_dir) == 0) {
        m_das_registrations.push_back (
            { das_registration::singleton, stem, display_name_for (singl_dir),
              type_id, si.size_of, das_file.string (), std::move (si.fields) });
      } else if (canonical.compare (0, sys_dir.size (), sys_dir) == 0) {
        m_das_registrations.push_back (
            { das_registration::system, stem, display_name_for (sys_dir),
              type_id, si.size_of, das_file.string (), std::move (si.fields) });
      }
    }
  }

  for (const das_registration &reg : m_das_registrations) {
    const auto type_id = static_cast<entt::id_type> (reg.type_id);
    if (reg.kind == das_registration::component) {
      const auto *existing
          = m_runtime_ctx->component_registry ().find_world_component (type_id);
      if (existing != nullptr
          && (!existing->runtime_registered
              || existing->type_name != reg.type_name)) {
        m_last_status = "Runtime component id collision for '" + reg.type_name
                        + "' at " + reg.script_path + ".";
        m_last_error = m_last_status;
        wsl::log::xmake ()->error ("{}", m_last_status);
        restore_previous_compile_state ();
        return false;
      }
    } else if (reg.kind == das_registration::singleton) {
      const auto *existing
          = m_runtime_ctx->singleton_registry ().find_singleton_component (
              type_id);
      if (existing != nullptr
          && (!existing->runtime_registered
              || existing->type_name != reg.type_name)) {
        m_last_status = "Runtime singleton id collision for '" + reg.type_name
                        + "' at " + reg.script_path + ".";
        m_last_error = m_last_status;
        wsl::log::xmake ()->error ("{}", m_last_status);
        restore_previous_compile_state ();
        return false;
      }
    } else {
      const auto &factories = m_runtime_ctx->system_factory_registry ();
      const auto *existing_by_id = factories.find_system (type_id);
      const auto *existing_by_name = factories.find_system (reg.display_name);
      const bool id_conflict
          = existing_by_id != nullptr
            && (!existing_by_id->runtime_registered
                || existing_by_id->type_name != reg.type_name
                || existing_by_id->script_path != reg.script_path);
      const bool name_conflict = existing_by_name != nullptr
                                 && !existing_by_name->runtime_registered
                                 && existing_by_name->type_id != type_id;
      const auto duplicate_name = std::find_if (
          m_das_registrations.begin (), m_das_registrations.end (),
          [&reg, type_id] (const das_registration &other) {
            return &other != &reg && other.kind == das_registration::system
                   && other.display_name == reg.display_name
                   && other.type_id != type_id;
          });
      if (id_conflict || name_conflict
          || duplicate_name != m_das_registrations.end ()) {
        m_last_status = "Runtime system registration collision for '"
                        + reg.display_name + "' at " + reg.script_path + ".";
        m_last_error = m_last_status;
        wsl::log::xmake ()->error ("{}", m_last_status);
        restore_previous_compile_state ();
        return false;
      }
    }
  }

  m_staged_das_engine = std::move (staged_engine);
  m_source_hash = current_hash;
  m_load_state = load_state_t::loaded;
  m_last_error.clear ();
  m_last_status = "Runtime systems/components compiled and registered.";
  wsl::log::xmake ()->trace ("{}", m_last_status);
  return true;
}

// ── Async reload ──

void
runtime_project_module::compile_and_load_async (const rsc::project &project)
{
  // If a previous async reload already finished but hasn't been polled yet,
  // finalize it now so we don't lose the result before starting a new one.
  if (m_async_reload_future.valid ()
      && m_async_reload_future.wait_for (std::chrono::seconds (0))
             != std::future_status::timeout) {
    poll_async_reload ();
  }

  if (is_reloading ()) {
    wsl::log::xmake ()->warn (
        "Async reload already in progress, ignoring duplicate request");
    return;
  }

  wsl::log::xmake ()->debug ("Starting async runtime reload for project: {}",
                             project.name);
  m_async_reload_future
      = std::async (std::launch::deferred, [this, project] () {
          return this->compile_and_load (project);
        });
}

bool
runtime_project_module::poll_async_reload ()
{
  if (!m_async_reload_future.valid ()) {
    return false;
  }

  const std::future_status status
      = m_async_reload_future.wait_for (std::chrono::seconds (0));
  if (status == std::future_status::ready
      || status == std::future_status::deferred) {
    const bool success = m_async_reload_future.get ();
    if (success) {
      wsl::log::xmake ()->debug (
          "Async reload complete, calling finalize_load on main thread");
      if (!finalize_load ()) {
        wsl::log::xmake ()->warn ("Async runtime finalization failed: {}",
                                  m_last_error);
      }
    } else {
      wsl::log::xmake ()->warn ("Async reload failed: {}", m_last_status);
    }
    return true;
  }
  return false;
}

bool
runtime_project_module::is_reloading () const
{
  if (!m_async_reload_future.valid ()) {
    return false;
  }
  return m_async_reload_future.wait_for (std::chrono::seconds (0))
         != std::future_status::ready;
}

} // namespace runtime
} // namespace reg
} // namespace wsl
