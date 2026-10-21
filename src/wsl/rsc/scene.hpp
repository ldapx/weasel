#pragma once

#ifndef IN_MODULE_INTERFACE
#include <entt/entt.hpp>
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

#include "resource_ids.hpp"
#include "resource_ref.hpp"
#include "../sys/system.hpp"

namespace wsl
{

namespace comp
{
namespace singl
{
class runtime_context;
class editor_context;
} // namespace singl
} // namespace comp

namespace rsc
{

/**
 * Represents a collection of entities and systems that form a unit of
 * gameplay or world space.
 *
 * The scene manages an EnTT registry and a list of systems that operate on that
 * registry. It also handles entity naming, resource tracking for asynchronous
 * loading, and entity copying.
 */
class scene
{
public:
  /**
   * Constructs a scene with a runtime and optional editor context.
   * :param runtime_ctx: Pointer to the runtime context.
   * :param editor_ctx: Pointer to the editor context.
   * :param name: The name of the scene.
   */
  explicit scene (comp::singl::runtime_context *runtime_ctx,
                  comp::singl::editor_context *editor_ctx,
                  const std::string &name);
  ~scene ();

  /**
   * Move constructor.
   *
   * NOT defaulted, because the constructor registers an EnTT signal whose
   * delegate captures a raw `this` (see on_entity_destroyed). A defaulted move
   * copies that delegate verbatim, so the moved-to scene keeps a signal bound
   * to the *old* object's address -- which, for `scene_loader::operator()` and
   * `world::add_scene()`, is a temporary destroyed immediately after the move.
   * The next `registry.clear()` then invoked the handler through that dangling
   * pointer and read garbage members from it, crashing the editor on stop.
   *
   * The registry moves with the scene (correct: entities and their handlers
   * belong together), so only the captured `this` needs refreshing.
   */
  scene (scene &&other);
  scene &operator= (scene &&other);

  scene (const scene &) = delete;
  scene &operator= (const scene &) = delete;

  /** Returns the EnTT registry owned by this scene. */
  entt::registry &get_registry ();

  /** Returns the scene name. */
  const std::string &get_name () const;

  /** Sets the scene name. */
  void set_name (std::string name);

  /**
   * Adds a system to the scene.
   * :param T: The type of the system.
   * :param args: Arguments to pass to the system's constructor.
   * :return: Reference to the created system.
   */
  template <typename T, typename... Args> T &add_system (Args &&...args);

  /**
   * Adds a system instance to the scene.
   * :param sys: Unique pointer to the system instance.
   * :param initialize_if_running: Whether to initialize the system if the scene
   * is already running.
   * :return: Reference to the added system.
   */
  sys::ecs_system &add_system_instance (std::unique_ptr<sys::ecs_system> sys,
                                        bool initialize_if_running = true);

  /** Removes a system from the scene. */
  void remove_system (sys::ecs_system *system);

  /** Initializes the scene and its systems. */
  void init ();

  /** Pauses the scene and its systems. */
  void pause ();

  /** Resumes the scene and its systems. */
  void resume ();

  /** Updates all active systems in the scene. */
  void update (double dt);

  /** Shuts down all systems and clears the registry. */
  void stop_and_clear ();

  /** Shuts down all systems and clears their storage. */
  void clear ();

  /** Clears all entities and their names from the registry. */
  void clear_registry ();

  /** Destroys an entity and its descendants, keeping hierarchy links valid. */
  void destroy_entity_subtree (entt::entity entity);

  /** Adds a resource to the scene's load list. */
  void add_resource (io::resource_type type, entt::id_type id);

  /** Removes a resource from the scene's load list. */
  void remove_resource (io::resource_type type, entt::id_type id);

  /** Checks if a resource is in the scene's load list. */
  bool has_resource (io::resource_type type, entt::id_type id) const;

  /** Returns the list of resources to be loaded for this scene. */
  const std::vector<io::resource_ref> &get_load_list () const;

  /** Sets the name of an entity. */
  void set_entity_name (entt::entity e, std::string name);

  /** Gets the name of an entity. */
  const std::string &get_entity_name (entt::entity e) const;

  /** Removes an entity name entry. */
  void remove_entity_name (entt::entity e);

  /** Returns the internal mapping of entities to names. */
  std::unordered_map<entt::entity, std::string> &get_entity_names ();

  /** Returns the internal mapping of entities to names (const). */
  const std::unordered_map<entt::entity, std::string> &
  get_entity_names () const;

  /**
   * Copies an entity and its components from another scene to this one.
   * :param src_scene: The source scene.
   * :param src_entity: The source entity to copy.
   * :param dst_parent: Optional parent entity in this scene.
   * :param is_instantiating_prefab: Whether the copy is part of a prefab
   * instantiation.
   * :param prefab_id: The ID of the prefab if applicable.
   * :return: The newly created entity in this scene.
   */
  entt::entity copy_entity (scene &src_scene, entt::entity src_entity,
                            entt::entity dst_parent = entt::null,
                            bool is_instantiating_prefab = false,
                            rsc::scene_id prefab_id = {});

  /** Returns a list of raw pointers to systems in this scene. */
  std::vector<sys::ecs_system *> get_systems ();

  /** The systems associated with this scene. */
  std::vector<std::unique_ptr<sys::ecs_system>> systems;

  /** Whether the scene has been initialized (systems refreshed and ready). */
  bool
  is_initialized () const
  {
    return m_initialized;
  }

  /** The active camera entity in this scene. */
  entt::entity camera{ entt::null };

protected:
  bool m_running = false;

private:
  friend class scene_manager;
  void on_system_added (sys::ecs_system &system);
  void ensure_context_bindings ();
  /**
   * (Re)binds the on_destroy signal to this object's address.
   * :param previous: The address this scene previously lived at, when the
   *   registry arrived here by a move. Its delegate is removed first.
   */
  void connect_destroy_signal (scene *previous = nullptr);
  void reset_scene_context ();
  void refresh_system_states ();
  void shutdown_systems ();
  void on_entity_destroyed (entt::registry &registry, entt::entity entity);
  bool is_playing () const;

  std::string m_name;
  std::unordered_map<entt::entity, std::string> m_entity_names;

  entt::registry m_registry;
  std::vector<io::resource_ref> m_load_list;

  bool m_initialized = false;

  /**
   * Leftover fixed-step time for the frame-phase driver. Lives on the
   * scene so serial scene::update and the scheduler path each keep their
   * own accumulator (only one drives a given registry in practice).
   */
  double m_fixed_accumulator = 0.0;

  comp::singl::runtime_context *m_runtime_ctx;
  comp::singl::editor_context *m_editor_ctx;
};

template <typename T, typename... Args>
inline T &
scene::add_system (Args &&...args)
{
  static_assert (std::is_base_of_v<sys::ecs_system, T>);
  auto sys = std::make_unique<T> (std::forward<Args> (args)...);
  return static_cast<T &> (add_system_instance (std::move (sys)));
}

} // namespace rsc

} // namespace wsl
