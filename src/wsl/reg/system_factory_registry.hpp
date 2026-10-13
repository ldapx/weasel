#pragma once

#include "../comp/component_meta.hpp"
#include "../rsc/scene.hpp"
#include "../sys/stage_registry.hpp"

#ifndef IN_MODULE_INTERFACE
#include <entt/core/type_info.hpp>
#endif
#ifndef IN_MODULE_INTERFACE
#include <functional>
#endif
#ifndef IN_MODULE_INTERFACE
#include <memory>
#endif
#ifndef IN_MODULE_INTERFACE
#include <string>
#endif
#ifndef IN_MODULE_INTERFACE
#include <string_view>
#endif
#ifndef IN_MODULE_INTERFACE
#include <unordered_map>
#endif
#ifndef IN_MODULE_INTERFACE
#include <unordered_set>
#endif

namespace wsl
{

namespace das
{
class das_engine;
}

namespace reg
{

namespace sig
{
struct system_iteration_debug_entry;
struct event_hub;
} // namespace sig

/** Forward-declaration of system iteration descriptor. */
using system_iteration_descriptor = event::system_iteration_debug_entry;

/** Ordering modes for registered system descriptors. */
enum class system_order
{
  /** Sort by display name. */
  display_name,

  /** Sort by stable system type identifier. */
  type_id
};

/** Registration options for system descriptors. */
struct system_registration_options
{
  /** Editor-facing or scene-facing system name. */
  std::string_view display_name;

  /** Execution stage name (must exist in `stage_registry`); empty → "logic". */
  std::string_view stage;

  /** Whether the registration was supplied by runtime project code. */
  bool runtime_registered = false;
};

/** Human-readable reference to a registered system type. */
struct system_type_ref
{
  /** Stable system type identifier. */
  entt::id_type type_id{};

  /** Reflected C++ type name. */
  std::string type_name;

  /** Scene/editor-facing system name. */
  std::string display_name;
};

/** Function signature for system factory functions. */
using system_factory_fn
    = std::function<std::unique_ptr<sys::ecs_system> (rsc::scene &)>;

/**
 * Registry for system factories, allowing dynamic creation of systems by
 * name.
 *
 * This class maps system names to factory functions that can instantiate
 * those systems for a given scene.
 */
class system_factory_registry
{
public:
  /** Describes a registered system factory. */
  struct system_descriptor
  {
    /** Stable system type identifier. */
    entt::id_type type_id{};

    /** Reflected C++ type name. */
    std::string type_name;

    /** Scene/editor-facing system name. */
    std::string display_name;

    /** Source file for a Daslang system; empty for native factories. */
    std::string script_path;

    /** Factory function that creates a system instance. */
    system_factory_fn factory;

    /** Whether this factory was registered at runtime. */
    bool runtime_registered = false;

    /** Declared system dependencies. */
    std::vector<entt::id_type> dependencies;

    /** Declared system conflicts. */
    std::vector<entt::id_type> conflicts;

    /** Execution stage name. */
    std::string stage;
  };

  using descriptor = system_descriptor;

  /**
   * Sets the signal hub used for iteration queries.
   * :param hub: Pointer to the engine signal hub.
   */
  void
  set_event_hub (event::event_hub *hub)
  {
    m_event_hub = hub;
  }

  /**
   * Registers a system type with a default factory.
   * :param T: The system type.
   * :param name: The name to register the system under.
   * :param runtime_registered: Whether this system is registered at runtime.
   */
  template <typename T>
  void
  register_system (const char *name, bool runtime_registered = false)
  {
    system_registration_options options{};
    if (name != nullptr) {
      options.display_name = name;
    }
    options.runtime_registered = runtime_registered;
    register_system_type<T> (options);
  }

  /**
   * Registers a system type with the engine-facing API.
   * :param T: System type to register.
   * :param options: Registration options.
   */
  template <typename T>
  void register_system_type (const system_registration_options &options = {});

  /**
   * Registers cached metadata for a runtime system (placeholder).
   *
   * Creates a no-op placeholder system for CLI operations.
   */
  bool register_cached_runtime_system (entt::id_type type_id,
                                       std::string_view type_name,
                                       std::string_view display_name);

  /**
   * Registers a daslang runtime system with live script execution.
   *
   * Creates a das_system that delegates lifecycle callbacks to the daslang
   * script via the engine.
   */
#if WEASEL_HAS_DASLANG
  bool register_cached_runtime_system (entt::id_type type_id,
                                       std::string_view type_name,
                                       std::string_view display_name,
                                       std::string_view script_path,
                                       das::das_engine &engine);
#endif

  /**
   * Registers a system with a custom factory function.
   * :param name: The name to register the system under.
   * :param factory: The custom factory function.
   * :param runtime_registered: Whether this system is registered at runtime.
   */
  void
  register_system (const char *name, system_factory_fn factory,
                   bool runtime_registered = false)
  {
    register_system_factory (name, std::move (factory), runtime_registered);
  }

  /**
   * Registers a system with a custom factory function.
   * :param display_name: Scene/editor-facing system name.
   * :param factory: Factory function that creates the system.
   * :param runtime_registered: Whether the factory came from runtime code.
   */
  void
  register_system_factory (std::string_view display_name,
                           system_factory_fn factory,
                           bool runtime_registered = false)
  {
    system_registration_options options{};
    options.display_name = display_name;
    options.runtime_registered = runtime_registered;
    register_system_factory (std::move (factory), options);
  }

  /**
   * Registers a system with a custom factory function.
   * :param factory: Factory function that creates the system.
   * :param options: Registration options.
   */
  void register_system_factory (system_factory_fn factory,
                                const system_registration_options &options
                                = {});

  /**
   * Finds a system descriptor by scene/editor-facing name.
   * :param display_name: Registered system name.
   * :return: Matching descriptor, or `nullptr` when not found.
   */
  const system_descriptor *find_system (std::string_view display_name) const;

  /**
   * Finds a system descriptor by stable type ID.
   * :param type_id: Stable system type identifier.
   * :return: Matching descriptor, or `nullptr` when not found.
   */
  const system_descriptor *find_system (entt::id_type type_id) const;

  /**
   * Returns registered systems in the requested order.
   * :param order: Requested descriptor ordering.
   * :return: Ordered system descriptor list.
   */
  std::vector<const system_descriptor *>
  get_systems (system_order order = system_order::display_name) const;

  /**
   * Returns the user-facing system names for editor search and scene
   * persistence.
   */
  std::vector<std::string> get_system_factory_names () const;

  /**
   * Creates a system instance by name.
   * :param name: The name of the system to create.
   * :param scene: The scene the system will be associated with.
   * :return: A unique pointer to the created system, or nullptr if not found.
   */
  std::unique_ptr<sys::ecs_system> create (const std::string &name,
                                           rsc::scene &scene);

  /**
   * Instantiates the requested system for the target scene.
   * :param display_name: Scene/editor-facing system name.
   * :param scene: Scene that will own the system instance.
   * :return: Instantiated system, or `nullptr` when not found.
   */
  std::unique_ptr<sys::ecs_system>
  create_system (std::string_view display_name, rsc::scene &scene)
  {
    return create (std::string (display_name), scene);
  }

  std::unique_ptr<sys::ecs_system>
  create_system (entt::id_type type_id, rsc::scene &scene)
  {
    const system_descriptor *descriptor = find_system (type_id);
    return descriptor != nullptr ? descriptor->factory (scene) : nullptr;
  }

  /**
   * Declares that one system depends on another.
   * :param OwnerSystem: The system that has the dependency.
   * :param DependencySystem: The system that must exist for the owner.
   */
  template <typename OwnerSystem, typename DependencySystem>
  void declare_system_dependency ();

  /**
   * Declares that one system conflicts with another.
   * :param OwnerSystem: The system that has the conflict.
   * :param ConflictSystem: The system that cannot exist with the owner.
   */
  template <typename OwnerSystem, typename ConflictSystem>
  void declare_system_conflict ();

  /**
   * Returns the declared dependency list for the system.
   * :param system_type_id: Stable system type identifier.
   * :return: List of dependencies as system type references.
   */
  std::vector<system_type_ref>
  get_system_dependencies (entt::id_type system_type_id) const;

  /**
   * Returns the declared conflict list for the system.
   * :param system_type_id: Stable system type identifier.
   * :return: List of conflicts as system type references.
   */
  std::vector<system_type_ref>
  get_system_conflicts (entt::id_type system_type_id) const;

  /**
   * Returns the declared iterations for the system.
   * :param system_type_id: Stable system type identifier.
   * :return: List of system iteration descriptors.
   */
  std::vector<const system_iteration_descriptor *>
  get_system_iterations (entt::id_type system_type_id) const;

  /**
   * Returns every declared system iteration whose required component set
   * includes the queried world component.
   * :param component_type_id: Stable world component identifier.
   * :return: List of matching system iteration descriptors.
   */
  std::vector<const system_iteration_descriptor *>
  find_iterations_using_world_component (entt::id_type component_type_id) const;

  /** Clears all runtime-registered system factories. */
  void clear_runtime_systems ();

  /** Returns a list of all registered system factory names. */
  std::vector<std::string>
  get_factory_names () const
  {
    return get_system_factory_names ();
  }

private:
  bool register_cached_runtime_descriptor (system_descriptor desc);
  void rebuild_type_name_index ();

  template <typename T>
  static std::unique_ptr<sys::ecs_system>
  make_default_system (const std::string &display_name, rsc::scene &scene)
  {
    (void)scene;
    static_assert (
        std::is_base_of_v<sys::ecs_system, T>,
        "Registered system types must derive from wsl::sys::ecs_system.");

    if constexpr (std::is_constructible_v<T, const std::string &>
                  || std::is_constructible_v<T, std::string>) {
      return std::make_unique<T> (display_name);
    } else if constexpr (std::is_constructible_v<T, const char *>) {
      return std::make_unique<T> (display_name.c_str ());
    } else {
      static_assert (std::is_default_constructible_v<T>,
                     "Registered systems must be default constructible or "
                     "constructible from a display name.");
      return std::make_unique<T> ();
    }
  }

  std::unordered_map<std::string, system_descriptor> m_factories;
  std::unordered_map<entt::id_type, std::string> m_type_to_name;
  std::unordered_map<std::string, std::string> m_type_name_to_display_name;
  std::unordered_set<std::string> m_ambiguous_type_names;
  event::event_hub *m_event_hub = nullptr;
};

template <typename T>
inline void
system_factory_registry::register_system_type (
    const system_registration_options &options)
{
  static_assert (
      std::is_base_of_v<sys::ecs_system, T>,
      "Registered system types must derive from wsl::sys::ecs_system.");

  system_descriptor desc{};
  desc.type_id = wsl::comp::stable_type_id<T> ();
  desc.type_name = std::string (entt::type_name<T> ().value ());
  desc.display_name = options.display_name.empty ()
                          ? comp::humanize_identifier (desc.type_name)
                          : std::string (options.display_name);
  desc.runtime_registered = options.runtime_registered;
  desc.stage = options.stage.empty ()
                   ? std::string (sys::stage_registry::default_stage)
                   : std::string (options.stage);
  desc.factory = [display_name = desc.display_name,
                  stage = desc.stage] (rsc::scene &scene) {
    auto sys = make_default_system<T> (display_name, scene);
    sys->set_stage (stage);
    return sys;
  };

  m_type_to_name[desc.type_id] = desc.display_name;
  m_type_name_to_display_name[desc.type_name] = desc.display_name;
  m_factories[desc.display_name] = std::move (desc);
}

template <typename OwnerSystem, typename DependencySystem>
inline void
system_factory_registry::declare_system_dependency ()
{
  const entt::id_type owner_id = wsl::comp::stable_type_id<OwnerSystem> ();
  const entt::id_type dep_id = wsl::comp::stable_type_id<DependencySystem> ();

  if (system_descriptor *desc
      = const_cast<system_descriptor *> (find_system (owner_id))) {
    if (std::find (desc->dependencies.begin (), desc->dependencies.end (),
                   dep_id)
        == desc->dependencies.end ()) {
      desc->dependencies.push_back (dep_id);
    }
  }
}

template <typename OwnerSystem, typename ConflictSystem>
inline void
system_factory_registry::declare_system_conflict ()
{
  const entt::id_type owner_id = wsl::comp::stable_type_id<OwnerSystem> ();
  const entt::id_type conflict_id
      = wsl::comp::stable_type_id<ConflictSystem> ();

  if (system_descriptor *desc
      = const_cast<system_descriptor *> (find_system (owner_id))) {
    if (std::find (desc->conflicts.begin (), desc->conflicts.end (),
                   conflict_id)
        == desc->conflicts.end ()) {
      desc->conflicts.push_back (conflict_id);
    }
  }
}

} // namespace reg

} // namespace wsl
