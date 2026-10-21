#pragma once

#include "../das/das_engine.hpp"
#include <entt/core/fwd.hpp>
#ifndef IN_MODULE_INTERFACE
#include <filesystem>
#endif
#ifndef IN_MODULE_INTERFACE
#include <cstdint>
#endif
#ifndef IN_MODULE_INTERFACE
#include <future>
#endif
#ifndef IN_MODULE_INTERFACE
#include <memory>
#endif
#ifndef IN_MODULE_INTERFACE
#include <string>
#endif
#ifndef IN_MODULE_INTERFACE
#include <vector>
#endif

namespace wsl
{

class app;

namespace comp::singl
{
class runtime_context;
}

namespace rsc
{
struct project;
class scene;
}

namespace reg
{

/**
 * Daslang project loading, metadata caching, and reload helpers.
 *
 * The \c runtime sub-namespace provides:
 * - \c runtime_project_module: discovers and executes user Daslang component
 * and system files.
 */
namespace runtime
{

/**
 * High-level state of the runtime-module loading pipeline.
 *
 * A single authoritative state replaces the former pair of independent
 * booleans ("module loaded", "cached metadata loaded") whose interactions
 * were hard to reason about.
 */
enum class load_state_t
{
  /** Nothing is loaded for this session. */
  unloaded,

  /** Registration names restored from the on-disk metadata cache only
   * (placeholders; no live Daslang context). */
  metadata_cache,

  /** Runtime sources compiled and registrations applied. */
  loaded
};

/**
 * Loads user-authored Daslang runtime code for a project.
 *
 * User C++ runtime sources are rejected. Engine C++ components remain
 * statically registered by the engine itself.
 */
class runtime_project_module
{
public:
  /**
   * Creates a runtime module bound to a runtime context.
   * :param runtime_ctx: Runtime context that owns the registries to populate.
   */
  explicit runtime_project_module (comp::singl::runtime_context *runtime_ctx);
  ~runtime_project_module ();

  /**
   * Rebuilds and loads the runtime code for a project.
   * :param project: Project description that provides source directories.
   * :return: \c true on success, otherwise \c false.
   */
  bool compile_and_load (const rsc::project &project);

  /**
   * Starts an asynchronous reload of the runtime code.
   *
   * Use \c poll_async_reload() in the main loop to check completion and
   * call \c finalize_load() on the main thread when ready.
   */
  void compile_and_load_async (const rsc::project &project);

  /**
   * Polls an in-progress async reload.
   *
   * If the background compilation finished, this calls \c finalize_load()
   * on the calling (main) thread and returns \c true.
   * :return: \c true if a reload just completed this call, otherwise \c false.
   */
  bool poll_async_reload ();

  /**
   * Reports whether an async reload is currently in progress.
   *
   * :return: \c true if compilation is still running in the background.
   */
  bool is_reloading () const;

  /**
   * Loads cached runtime registration metadata for a project.
   *
   * This is a fast path for commands that only need runtime names and system
   * placeholders. It does not load user C++ types.
   *
   * The cache is only trusted when it is non-empty while Daslang sources
   * exist; an empty-but-valid-hash cache is treated as corrupt, deleted, and
   * ``false`` is returned so callers can escalate to a full compile.
   *
   * :param project: Project description that provides source directories.
   * :return: ``true`` if metadata was applied, otherwise ``false``.
   */
  bool load_cached_metadata (const rsc::project &project);
  // Drop metadata-cache registrations so the next load takes the full-compile
  // path. No-op once the module is fully loaded.
  void clear_cached_metadata ();
  void register_project_fs_roots (const std::filesystem::path &project_root,
                                  const rsc::project &project,
                                  wsl::das::das_engine &engine);

  /**
   * Resets the in-memory load state and deletes the on-disk registration
   * cache file.
   *
   * :param project: Project whose cache file should be removed. May be
   *   ``nullptr`` to use the currently loaded project root (if any).
   */
  void invalidate (const rsc::project *project);

  /**
   * Finalizes the loading process on the main thread.
   *
   * This clears the registries and applies the registrations.
   */
  bool finalize_load ();

  /** Clears runtime-loaded registrations and resets runtime-module state. */
  void unload ();

  /**
   * Returns the latest human-readable status message.
   * :return: The most recent status string.
   */
  const std::string &
  last_status () const
  {
    return m_last_status;
  }

  /**
   * Returns the detailed error from the most recent failed operation.
   *
   * Empty when nothing has failed yet. Unlike \c last_status, this is not
   * overwritten by successful transitions, so a compile failure remains
   * visible until the next success.
   *
   * :return: The most recent error string, or an empty string.
   */
  const std::string &
  last_error () const
  {
    return m_last_error;
  }

  /**
   * Reports whether a runtime module has already been loaded.
   * :return: \c true if runtime code is active for this session.
   */
  bool
  has_loaded_module () const
  {
    return m_load_state == load_state_t::loaded;
  }

  /** Reports whether cached runtime metadata is active. */
  bool
  has_loaded_cached_metadata () const
  {
    return m_load_state == load_state_t::metadata_cache;
  }

  /** Returns the current position in the loading pipeline. */
  load_state_t
  get_load_state () const
  {
    return m_load_state;
  }

  /** Returns the project root of the currently loaded module (if any). */
  const std::filesystem::path &
  loaded_project_root () const
  {
    return m_loaded_project_root;
  }

  /** Returns the source hash of the currently loaded module. */
  std::size_t
  source_hash () const
  {
    return m_source_hash;
  }

  /// Counts of live Daslang registrations held by this module.
  std::size_t das_component_count () const;
  std::size_t das_singleton_count () const;
  std::size_t das_system_count () const;

  /**
   * Returns the path of the on-disk registration cache for a project root.
   *
   * Exposed publicly so tooling can inspect or delete the cache file
   * directly.
   */
  static std::filesystem::path
  registration_cache_path (const std::filesystem::path &project_root);

  /**
   * Returns the daslang engine instance.
   *
   * The engine is created on first use. Returns nullptr if daslang
   * is not enabled.
   */
  wsl::das::das_engine *get_das_engine ();

  struct cached_das_field
  {
    std::string name;
    std::string type_name;
    int offset = 0;
    int size = 0;
    int kind = 0;
    /** Initialiser bytes as lowercase hex; empty when not recorded. */
    std::string default_hex;
  };

  struct cached_registration
  {
    std::uint64_t type_id = 0;
    std::string type_name;
    std::string display_name;
    bool is_das_component = false;
    int das_struct_size = 0;
    std::string script_path;
    std::vector<cached_das_field> das_fields;
  };

  struct registration_cache
  {
    std::size_t source_hash = 0;
    std::vector<cached_registration> components;
    std::vector<cached_registration> singletons;
    std::vector<cached_registration> systems;
  };

private:
  struct source_set
  {
    std::vector<std::filesystem::path> headers;
    std::vector<std::filesystem::path> cpp_sources;
    std::vector<std::filesystem::path> das_sources;
  };

  struct das_registration
  {
    enum kind_t
    {
      component,
      singleton,
      system
    };
    kind_t kind;
    std::string type_name;
    std::string display_name;
    std::uint64_t type_id;
    int struct_size = 0;
    std::string script_path;
    std::vector<wsl::das::das_engine::field_info> fields;
  };

  struct suspended_script_system
  {
    rsc::scene *scene = nullptr;
    entt::id_type type_id{};
  };

  struct suspended_event_connection
  {
    entt::id_type event_type_id{};
    entt::id_type system_type_id{};
    std::string handler_name;
  };

  static void gather_files (const std::filesystem::path &base, source_set &out);

  static bool is_das_file (const std::filesystem::path &path);

  static std::size_t compute_source_hash (const source_set &sources);

  static bool read_registration_cache (const std::filesystem::path &path,
                                       std::size_t source_hash,
                                       registration_cache &out);

  bool write_registration_cache () const;

  bool apply_registration_cache (const registration_cache &cache);

  void load_das_registrations_from_cache (const registration_cache &cache);
  void suspend_script_systems ();
  void restore_script_systems ();

  static bool
  write_generated_translation_unit (const std::filesystem::path &generated_path,
                                    const source_set &sources);

  comp::singl::runtime_context *m_runtime_ctx = nullptr;
  std::filesystem::path m_loaded_project_root;
  std::string m_last_status;
  /** Detailed error of the most recent failed operation (empty if none). */
  std::string m_last_error;
  load_state_t m_load_state = load_state_t::unloaded;
  std::size_t m_source_hash = 0;

  std::unique_ptr<wsl::das::das_engine> m_das_engine;
  std::unique_ptr<wsl::das::das_engine> m_staged_das_engine;
  std::vector<das_registration> m_das_registrations;
  std::vector<suspended_script_system> m_suspended_script_systems;
  std::vector<suspended_event_connection> m_suspended_event_connections;

  std::future<bool> m_async_reload_future;
};

} // namespace runtime
} // namespace reg

} // namespace wsl
