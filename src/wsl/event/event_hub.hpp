#pragma once

#include "../comp/component_meta.hpp"
#include "../log/log.hpp"
#include "event_hub_fwd.hpp"

#ifndef IN_MODULE_INTERFACE
#include <algorithm>
#endif
#ifndef IN_MODULE_INTERFACE
#include <cstdint>
#endif
#ifndef IN_MODULE_INTERFACE
#include <mutex>
#endif
#ifndef IN_MODULE_INTERFACE
#include <ranges>
#endif
#ifndef IN_MODULE_INTERFACE
#include <unordered_set>
#endif

namespace wsl::event
{

/**
 * Describes a related component type used by signal and iteration
 * metadata.
 */
struct component_type_debug_entry
{
  entt::id_type type_id{};
  std::string type_name;
};

template <typename... Components>
inline std::vector<component_type_debug_entry>
make_component_type_debug_entries ()
{
  std::vector<component_type_debug_entry> entries;
  entries.reserve (sizeof...(Components));

  (entries.push_back (component_type_debug_entry{
       comp::stable_type_id<Components> (),
       std::string (entt::type_name<Components> ().value ()) }),
   ...);

  return entries;
}

inline bool
contains_component_type (
    const std::vector<component_type_debug_entry> &component_types,
    entt::id_type component_type_id)
{
  return std::ranges::any_of (
      component_types,
      [component_type_id] (const component_type_debug_entry &entry) {
        return entry.type_id == component_type_id;
      });
}

template <typename... Components>
inline bool
matches_component_set (entt::registry &registry, entt::entity entity)
{
  if (!registry.valid (entity)) {
    return false;
  }

  if constexpr (sizeof...(Components) == 0) {
    return true;
  }

  return registry.all_of<Components...> (entity);
}

template <typename... Components>
inline entity_match_predicate_t
make_entity_match_predicate ()
{
  if constexpr (sizeof...(Components) == 0) {
    return nullptr;
  }

  return +[] (entt::registry &registry, entt::entity entity) -> bool {
    return matches_component_set<Components...> (registry, entity);
  };
}

/** Debug and editor metadata for a declared signal. */
struct event_source_debug_entry
{
  entt::id_type type_id{};
  std::string type_name;
  /** Payload size in bytes (``sizeof (Signal)``); 0 when unknown. */
  std::size_t size = 0;
  /** Runtime structure signature when a payload is declared by daScript. */
  std::uint64_t payload_layout_hash = 0;
  std::size_t listener_count = 0;
  std::size_t emit_count = 0;
  entt::id_type owner_system_type_id{};
  std::string owner_system_type_name;
};

/** Debug metadata for a declared event handler. */
struct system_handler_debug_entry
{
  entt::id_type system_type_id{};
  std::string system_type_name;
  std::string handler_name;
};

/** Debug metadata for a declared system iteration. */
struct system_iteration_debug_entry
{
  entt::id_type system_type_id{};
  std::string system_type_name;
  std::string iteration_name;
  std::vector<component_type_debug_entry> component_types;
  entity_match_predicate_t entity_matches = nullptr;

  bool
  matches_entity (entt::registry &registry, entt::entity entity) const
  {
    if (entity_matches != nullptr) {
      return entity_matches (registry, entity);
    }

    return true;
  }

  bool
  has_component (entt::id_type component_type_id) const
  {
    return contains_component_type (component_types, component_type_id);
  }
};

/** Debug metadata for a declared event sink (connectable handler). */
struct event_sink_debug_entry
{
  entt::id_type event_type_id{};
  std::string event_type_name;
  entt::id_type system_type_id{};
  std::string system_type_name;
  std::string handler_name;
};

/** Debug metadata for an explicit event-source → sink connection. */
struct event_connection_debug_entry
{
  entt::id_type event_type_id{};
  std::string event_type_name;
  entt::id_type system_type_id{};
  std::string system_type_name;
  std::string handler_name;
};

/** Serializable data for an explicit event connection. */
struct event_connection_data
{
  entt::id_type event_type_id{};
  entt::id_type system_type_id{};
  std::string handler_name;

  template <class Archive>
  void
  serialize (Archive &archive)
  {
    archive (event_type_id, system_type_id, handler_name);
  }
};

/** Editor/debug database that mirrors declared signals and connections. */
struct event_debug_db
{
  std::unordered_map<entt::id_type, event_source_debug_entry> entries;
  std::vector<system_handler_debug_entry> system_handlers;
  std::vector<system_iteration_debug_entry> system_iterations;
  std::vector<event_sink_debug_entry> event_sinks;
  std::vector<event_connection_debug_entry> connections;

  void
  note_listener (entt::id_type type_id, std::string_view type_name)
  {
    event_source_debug_entry &entry = entries[type_id];
    entry.type_id = type_id;
    if (entry.type_name.empty ()) {
      entry.type_name = std::string (type_name);
    }
    entry.listener_count++;
  }

  void
  note_emit (entt::id_type type_id, std::string_view type_name)
  {
    event_source_debug_entry &entry = entries[type_id];
    entry.type_id = type_id;
    if (entry.type_name.empty ()) {
      entry.type_name = std::string (type_name);
    }
    entry.emit_count++;
  }

  template <typename Signal, typename OwnerSystem>
  void
  declare_event_source ()
  {
    const entt::id_type event_type_id = comp::stable_type_id<Signal> ();
    event_source_debug_entry &entry = entries[event_type_id];
    entry.type_id = event_type_id;
    entry.type_name = std::string (entt::type_name<Signal> ().value ());
    entry.size = sizeof (Signal);
    entry.owner_system_type_id = comp::stable_type_id<OwnerSystem> ();
    entry.owner_system_type_name
        = std::string (entt::type_name<OwnerSystem> ().value ());
  }

  /**
   * Type-erased variant of :cpp:func:`declare_event_source`.
   *
   * :param event_type_id: Stable event type identifier.
   * :param event_type_name: Reflected event type name.
   * :param owner_system_type_id: Stable identifier of the owning system.
   * :param owner_system_type_name: Reflected name of the owning system.
   * :param event_size: Payload size in bytes; used by byte-level emitters.
   */
  void
  declare_event_source_by_id (entt::id_type event_type_id,
                              std::string_view event_type_name,
                              entt::id_type owner_system_type_id,
                              std::string_view owner_system_type_name,
                              std::size_t event_size)
  {
    event_source_debug_entry &entry = entries[event_type_id];
    entry.type_id = event_type_id;
    entry.type_name = std::string (event_type_name);
    entry.size = event_size;
    entry.owner_system_type_id = owner_system_type_id;
    entry.owner_system_type_name = std::string (owner_system_type_name);
  }

  /**
   * Type-erased variant of :cpp:func:`declare_event_sink` for the debug
   * database only (runtime registration lives on :cpp:struct:`event_hub`).
   */
  void
  declare_event_sink_by_id (entt::id_type event_type_id,
                            std::string_view event_type_name,
                            entt::id_type system_type_id,
                            std::string_view system_type_name,
                            const char *handler_name)
  {
    const std::string name = handler_name != nullptr ? handler_name : "";

    std::erase_if (event_sinks, [event_type_id, system_type_id,
                                 &name] (const event_sink_debug_entry &entry) {
      return entry.event_type_id == event_type_id
             && entry.system_type_id == system_type_id
             && entry.handler_name == name;
    });

    event_sink_debug_entry entry;
    entry.event_type_id = event_type_id;
    entry.event_type_name = std::string (event_type_name);
    entry.system_type_id = system_type_id;
    entry.system_type_name = std::string (system_type_name);
    entry.handler_name = name;
    event_sinks.push_back (std::move (entry));
  }

  template <typename OwnerSystem>
  void
  declare_handler (const char *handler_name)
  {
    const entt::id_type system_type_id = comp::stable_type_id<OwnerSystem> ();
    const std::string name = handler_name != nullptr ? handler_name : "";

    std::erase_if (
        system_handlers,
        [system_type_id, &name] (const system_handler_debug_entry &entry) {
          return entry.system_type_id == system_type_id
                 && entry.handler_name == name;
        });

    system_handlers.push_back (
        { system_type_id,
          std::string (entt::type_name<OwnerSystem> ().value ()), name });
  }

  template <typename OwnerSystem, typename... Components>
  void
  declare_iteration (const char *iteration_name)
  {
    static_assert ((comp::world_component_type<Components> && ...),
                   "System iteration declarations may only reference "
                   "wsl::comp::world_component types.");

    const entt::id_type system_type_id = comp::stable_type_id<OwnerSystem> ();
    const std::string name = iteration_name != nullptr ? iteration_name : "";

    std::erase_if (
        system_iterations,
        [system_type_id, &name] (const system_iteration_debug_entry &entry) {
          return entry.system_type_id == system_type_id
                 && entry.iteration_name == name;
        });

    system_iteration_debug_entry entry;
    entry.system_type_id = system_type_id;
    entry.system_type_name
        = std::string (entt::type_name<OwnerSystem> ().value ());
    entry.iteration_name = name;
    entry.component_types = make_component_type_debug_entries<Components...> ();
    entry.entity_matches = make_entity_match_predicate<Components...> ();
    system_iterations.push_back (std::move (entry));
  }

  template <typename Signal, typename OwnerSystem>
  void
  declare_event_sink (const char *handler_name)
  {
    const entt::id_type event_type_id = comp::stable_type_id<Signal> ();
    const entt::id_type system_type_id = comp::stable_type_id<OwnerSystem> ();
    const std::string name = handler_name != nullptr ? handler_name : "";

    std::erase_if (event_sinks, [event_type_id, system_type_id,
                                 &name] (const event_sink_debug_entry &entry) {
      return entry.event_type_id == event_type_id
             && entry.system_type_id == system_type_id
             && entry.handler_name == name;
    });

    event_sink_debug_entry entry;
    entry.event_type_id = event_type_id;
    entry.event_type_name = std::string (entt::type_name<Signal> ().value ());
    entry.system_type_id = system_type_id;
    entry.system_type_name
        = std::string (entt::type_name<OwnerSystem> ().value ());
    entry.handler_name = name;
    event_sinks.push_back (std::move (entry));
  }

  void
  rebuild_listener_counts ()
  {
    for (auto &[_, entry] : entries) {
      entry.listener_count = 0;
    }

    for (const event_connection_debug_entry &connection : connections) {
      note_listener (connection.event_type_id, connection.event_type_name);
    }
  }

  void
  note_connection (entt::id_type event_type_id, const char *event_type_name,
                   entt::id_type system_type_id, const char *system_type_name,
                   const char *handler_name)
  {
    connections.push_back (
        { event_type_id, event_type_name != nullptr ? event_type_name : "",
          system_type_id, system_type_name != nullptr ? system_type_name : "",
          handler_name != nullptr ? handler_name : "" });
    rebuild_listener_counts ();
  }

  void
  clear_connections ()
  {
    connections.clear ();
    rebuild_listener_counts ();
  }

  void
  clear_system_declarations (entt::id_type system_type_id)
  {
    std::erase_if (system_handlers,
                   [system_type_id] (const system_handler_debug_entry &entry) {
                     return entry.system_type_id == system_type_id;
                   });

    std::erase_if (
        system_iterations,
        [system_type_id] (const system_iteration_debug_entry &entry) {
          return entry.system_type_id == system_type_id;
        });

    std::erase_if (event_sinks,
                   [system_type_id] (const event_sink_debug_entry &entry) {
                     return entry.system_type_id == system_type_id;
                   });

    for (auto it = entries.begin (); it != entries.end ();) {
      if (it->second.owner_system_type_id == system_type_id) {
        it = entries.erase (it);
      } else {
        ++it;
      }
    }

    rebuild_listener_counts ();
  }
};

/** Runtime signal declaration and explicit connection hub. */
struct event_hub
{
  struct registered_event_sink
  {
    entt::id_type event_type_id{};
    std::string event_type_name;
    entt::id_type system_type_id{};
    std::string system_type_name;
    std::string handler_name;
    handler_invoke_fn invoke = nullptr;
    // For owners that are not `ecs_system`s (e.g. `runtime_context`), this is
    // the captured owner instance. Left null for `ecs_system` owners, which are
    // resolved at dispatch time via `resolve_system_by_type`.
    void *owner_ptr = nullptr;
    void *owner_identity = nullptr;
    std::size_t payload_size = 0;
    std::uint64_t payload_layout_hash = 0;
  };

  struct registered_event_source
  {
    entt::id_type event_type_id{};
    std::string event_type_name;
    entt::id_type owner_system_type_id{};
    std::string owner_system_type_name;
  };

  struct connected_sink
  {
    entt::id_type event_type_id{};
    std::string event_type_name;
    entt::id_type system_type_id{};
    std::string system_type_name;
    std::string handler_name;
    handler_invoke_fn invoke = nullptr;
    void *owner_ptr = nullptr;
    void *owner_identity = nullptr;
    std::size_t payload_size = 0;
    std::uint64_t payload_layout_hash = 0;
  };

  /**
   * Guards the registration/connection vectors below.
   *
   * Scene loading runs on a background thread -- `resource_manager::load`
   * dispatches the scene loader through `std::async` -- and the scene
   * serializer calls `clear_connections()` / `connect()` on the shared hub
   * while the main thread may be dispatching events. A concurrent `push_back`
   * reallocates `connected_handlers` underneath the other thread, which
   * relocates `connected_sink` elements with a stale data pointer and aborts in
   * the allocator ("free(): invalid pointer" / "unaligned tcache chunk").
   *
   * Recursive because the dispatch helpers lock it, and a handler running
   * under a dispatch is allowed to connect or disconnect.
   */
  mutable std::recursive_mutex mutex;

  std::vector<registered_event_source> registered_signal_sources;
  std::vector<registered_event_sink> registered_event_sinks;
  std::vector<connected_sink> connected_handlers;

  /**
   * A dispatch-ready copy of one connection. Handlers are invoked outside the
   * lock, so dispatch must not iterate `connected_handlers` directly: a handler
   * may call `connect`/`disconnect` and invalidate the iteration.
   */
  struct dispatch_entry
  {
    entt::id_type system_type_id{};
    handler_invoke_fn invoke = nullptr;
    void *owner_ptr = nullptr;
    std::size_t payload_size = 0;
    std::uint64_t payload_layout_hash = 0;
  };

  /**
   * Snapshots the connections registered for *event_type_id*.
   */
  std::vector<dispatch_entry>
  collect_handlers (entt::id_type event_type_id) const
  {
    std::lock_guard<std::recursive_mutex> lock (mutex);

    std::vector<dispatch_entry> result;
    for (const connected_sink &connection : connected_handlers) {
      if (connection.event_type_id == event_type_id
          && connection.invoke != nullptr) {
        result.push_back ({ connection.system_type_id, connection.invoke,
                            connection.owner_ptr, connection.payload_size,
                            connection.payload_layout_hash });
      }
    }

    return result;
  }

  /**
   * Resolves the owner of a snapshotted connection, falling back to the
   * system's instance for `ecs_system` owners.
   */
  void *
  resolve_owner (const dispatch_entry &entry) const
  {
    if (entry.owner_ptr != nullptr) {
      return entry.owner_ptr;
    }

    if (!resolve_system_by_type) {
      return nullptr;
    }

    ::wsl::sys::ecs_system *system
        = resolve_system_by_type (entry.system_type_id);
    return system;
  }

  entt::dispatcher *dispatcher = nullptr;
  event_debug_db *db = nullptr;
  std::function<entt::registry *()> resolve_active_registry;
  std::function<::wsl::sys::ecs_system *(entt::id_type)> resolve_system_by_type;

  event_hub () = default;

  event_hub (entt::dispatcher &dispatcher_ref, event_debug_db &db_ref)
      : dispatcher (&dispatcher_ref), db (&db_ref)
  {
  }

  explicit event_hub (event_debug_db &db_ref) : db (&db_ref) {}

  void
  clear_connections ()
  {
    std::lock_guard<std::recursive_mutex> lock (mutex);
    connected_handlers.clear ();
    if (db != nullptr) {
      db->clear_connections ();
    }
  }

  void
  clear_system_declarations (entt::id_type system_type_id)
  {
    std::lock_guard<std::recursive_mutex> lock (mutex);
    std::erase_if (registered_signal_sources,
                   [system_type_id] (const registered_event_source &entry) {
                     return entry.owner_system_type_id == system_type_id;
                   });

    std::erase_if (registered_event_sinks,
                   [system_type_id] (const registered_event_sink &entry) {
                     return entry.system_type_id == system_type_id;
                   });
    std::erase_if (connected_handlers,
                   [system_type_id] (const connected_sink &entry) {
                     return entry.system_type_id == system_type_id;
                   });

    if (db != nullptr) {
      std::erase_if (db->connections,
                     [system_type_id] (const event_connection_debug_entry &entry) {
                       return entry.system_type_id == system_type_id;
                     });
      db->clear_system_declarations (system_type_id);
      db->rebuild_listener_counts ();
    }
  }

  void
  clear_system_instance (const void *owner_identity)
  {
    if (owner_identity == nullptr) {
      return;
    }

    std::lock_guard<std::recursive_mutex> lock (mutex);
    std::vector<event_connection_data> removed_connections;
    for (const connected_sink &entry : connected_handlers) {
      if (entry.owner_identity == owner_identity) {
        removed_connections.push_back ({ entry.event_type_id,
                                         entry.system_type_id,
                                         entry.handler_name });
      }
    }
    std::erase_if (registered_event_sinks,
                   [owner_identity] (const registered_event_sink &entry) {
                     return entry.owner_identity == owner_identity;
                   });
    std::erase_if (connected_handlers,
                   [owner_identity] (const connected_sink &entry) {
                     return entry.owner_identity == owner_identity;
                   });
    if (db != nullptr) {
      std::erase_if (db->connections,
                     [&removed_connections] (
                         const event_connection_debug_entry &entry) {
                       return std::ranges::any_of (
                           removed_connections,
                           [&entry] (const event_connection_data &removed) {
                             return removed.event_type_id == entry.event_type_id
                                    && removed.system_type_id
                                           == entry.system_type_id
                                    && removed.handler_name
                                           == entry.handler_name;
                           });
                     });
      db->rebuild_listener_counts ();
    }
  }

  template <typename OwnerSystem>
  void
  clear_system_declarations ()
  {
    clear_system_declarations (comp::stable_type_id<OwnerSystem> ());
  }

  std::vector<event_connection_data>
  get_all_connections () const
  {
    std::lock_guard<std::recursive_mutex> lock (mutex);
    std::vector<event_connection_data> result;
    result.reserve (connected_handlers.size ());

    for (const connected_sink &handler : connected_handlers) {
      result.push_back ({ handler.event_type_id, handler.system_type_id,
                          handler.handler_name });
    }

    return result;
  }

  /**
   * Returns connections only for systems whose type_id is in *allowed*.
   * Connections with a non-null ``owner_ptr`` (i.e. non-ecs_system owners
   * such as ``runtime_context``) are always included regardless of the
   * allowed set, because they are global handlers that are not tied to a
   * specific scene.
   */
  std::vector<event_connection_data>
  get_connections_for_systems (
      const std::unordered_set<entt::id_type> &allowed) const
  {
    std::lock_guard<std::recursive_mutex> lock (mutex);
    std::vector<event_connection_data> result;
    result.reserve (connected_handlers.size ());

    for (const connected_sink &handler : connected_handlers) {
      if (handler.owner_ptr != nullptr
          || allowed.contains (handler.system_type_id)) {
        result.push_back ({ handler.event_type_id, handler.system_type_id,
                            handler.handler_name });
      }
    }

    return result;
  }

  std::vector<const event_source_debug_entry *>
  get_events_for_system (entt::id_type system_type_id) const
  {
    std::vector<const event_source_debug_entry *> result;
    if (db == nullptr) {
      return result;
    }

    result.reserve (db->entries.size ());
    for (const auto &[_, entry] : db->entries) {
      if (entry.owner_system_type_id == system_type_id) {
        result.push_back (&entry);
      }
    }

    std::sort (result.begin (), result.end (),
               [] (const event_source_debug_entry *lhs,
                   const event_source_debug_entry *rhs) {
                 return lhs->type_name < rhs->type_name;
               });
    return result;
  }

  std::vector<const system_iteration_debug_entry *>
  get_matching_iterations (entt::registry &registry, entt::entity entity) const
  {
    std::vector<const system_iteration_debug_entry *> result;
    if (db == nullptr) {
      return result;
    }

    for (const system_iteration_debug_entry &iteration :
         db->system_iterations) {
      if (iteration.matches_entity (registry, entity)) {
        result.push_back (&iteration);
      }
    }

    return result;
  }

  template <typename Signal>
  void
  note_listener ()
  {
    if (db != nullptr) {
      db->note_listener (comp::stable_type_id<Signal> (),
                         entt::type_name<Signal> ().value ());
    }
  }

  template <typename Signal>
  void
  note_emit ()
  {
    if (db != nullptr) {
      db->note_emit (comp::stable_type_id<Signal> (),
                     entt::type_name<Signal> ().value ());
    }
  }

  /** Type-erased variant of :cpp:func:`note_emit`. */
  void
  note_emit (entt::id_type type_id, std::string_view type_name)
  {
    if (db != nullptr) {
      db->note_emit (type_id, type_name);
    }
  }

  /**
   * Declares an event owned by a system-like type using pre-resolved ids.
   * Mirrors :cpp:func:`declare_event_source` for name-keyed bindings.
   *
   * :param event_type_id: Stable event type identifier.
   * :param event_type_name: Reflected event type name.
   * :param owner_system_type_id: Stable identifier of the owning system.
   * :param owner_system_type_name: Reflected name of the owning system.
   * :param event_size: Payload size in bytes.
   */
  void
  declare_event_source_by_id (entt::id_type event_type_id,
                              std::string_view event_type_name,
                              entt::id_type owner_system_type_id,
                              std::string_view owner_system_type_name,
                              std::size_t event_size)
  {
    std::lock_guard<std::recursive_mutex> lock (mutex);
    if (db != nullptr) {
      db->declare_event_source_by_id (event_type_id, event_type_name,
                                      owner_system_type_id,
                                      owner_system_type_name, event_size);
    }

    std::erase_if (registered_signal_sources,
                   [event_type_id] (const registered_event_source &entry) {
                     return entry.event_type_id == event_type_id;
                   });

    registered_event_source source;
    source.event_type_id = event_type_id;
    source.event_type_name = std::string (event_type_name);
    source.owner_system_type_id = owner_system_type_id;
    source.owner_system_type_name = std::string (owner_system_type_name);
    registered_signal_sources.push_back (std::move (source));
  }

  /**
   * Registers a connectable handler for an event using pre-resolved ids.
   * Mirrors :cpp:func:`declare_event_sink` for name-keyed bindings.
   *
   * :param event_type_id: Stable event type identifier.
   * :param event_type_name: Reflected event type name.
   * :param system_type_id: Stable identifier of the owning system.
   * :param system_type_name: Reflected name of the owning system.
   * :param handler_name: Connectable handler name.
   * :param invoke: Handler thunk invoked on dispatch.
   * :param owner: Captured owner instance, or ``nullptr`` to resolve the
   *   owner through ``resolve_system_by_type`` at dispatch time.
   */
  void
  declare_event_sink_by_id (entt::id_type event_type_id,
                            std::string_view event_type_name,
                            entt::id_type system_type_id,
                            std::string_view system_type_name,
                            const char *handler_name, handler_invoke_fn invoke,
                            void *owner = nullptr,
                            std::size_t payload_size = 0,
                            const void *owner_identity = nullptr,
                            std::uint64_t payload_layout_hash = 0)
  {
    std::lock_guard<std::recursive_mutex> lock (mutex);
    if (db != nullptr) {
      db->declare_event_sink_by_id (event_type_id, event_type_name,
                                    system_type_id, system_type_name,
                                    handler_name);
    }

    const std::string name = handler_name != nullptr ? handler_name : "";

    std::erase_if (registered_event_sinks,
                   [event_type_id, system_type_id,
                    &name] (const registered_event_sink &entry) {
                     return entry.event_type_id == event_type_id
                            && entry.system_type_id == system_type_id
                            && entry.handler_name == name;
                   });

    registered_event_sink handler;
    handler.event_type_id = event_type_id;
    handler.event_type_name = std::string (event_type_name);
    handler.system_type_id = system_type_id;
    handler.system_type_name = std::string (system_type_name);
    handler.handler_name = name;
    handler.invoke = invoke;
    handler.owner_ptr = owner;
    handler.owner_identity = const_cast<void *> (owner_identity);
    handler.payload_size = payload_size;
    handler.payload_layout_hash = payload_layout_hash;
    registered_event_sinks.push_back (std::move (handler));
  }

  template <typename Signal, typename OwnerSystem>
  void
  declare_event_source ()
  {
    const entt::id_type event_type_id = comp::stable_type_id<Signal> ();

    if (db != nullptr) {
      db->template declare_event_source<Signal, OwnerSystem> ();
    }

    std::erase_if (registered_signal_sources,
                   [event_type_id] (const registered_event_source &entry) {
                     return entry.event_type_id == event_type_id;
                   });

    registered_event_source source;
    source.event_type_id = event_type_id;
    source.event_type_name = std::string (entt::type_name<Signal> ().value ());
    source.owner_system_type_id = comp::stable_type_id<OwnerSystem> ();
    source.owner_system_type_name
        = std::string (entt::type_name<OwnerSystem> ().value ());
    registered_signal_sources.push_back (std::move (source));
  }

  template <typename OwnerSystem>
  void
  declare_handler (const char *handler_name)
  {
    if (db != nullptr) {
      db->template declare_handler<OwnerSystem> (handler_name);
    }
  }

  template <typename OwnerSystem, typename... Components>
  void
  declare_iteration (const char *iteration_name)
  {
    if (db != nullptr) {
      db->template declare_iteration<OwnerSystem, Components...> (
          iteration_name);
    }
  }

  template <typename Signal, typename OwnerSystem>
  void
  declare_event_sink (const char *handler_name, handler_invoke_fn invoke,
                      void *owner = nullptr)
  {
    const entt::id_type event_type_id = comp::stable_type_id<Signal> ();
    const entt::id_type system_type_id = comp::stable_type_id<OwnerSystem> ();
    const std::string name = handler_name != nullptr ? handler_name : "";

    if (db != nullptr) {
      db->template declare_event_sink<Signal, OwnerSystem> (handler_name);
    }

    std::erase_if (registered_event_sinks,
                   [event_type_id, system_type_id,
                    &name] (const registered_event_sink &entry) {
                     return entry.event_type_id == event_type_id
                            && entry.system_type_id == system_type_id
                            && entry.handler_name == name;
                   });

    registered_event_sink handler;
    handler.event_type_id = event_type_id;
    handler.event_type_name = std::string (entt::type_name<Signal> ().value ());
    handler.system_type_id = system_type_id;
    handler.system_type_name
        = std::string (entt::type_name<OwnerSystem> ().value ());
    handler.handler_name = name;
    handler.invoke = invoke;
    handler.owner_ptr = owner;
    handler.payload_size = sizeof (Signal);
    registered_event_sinks.push_back (std::move (handler));
  }

  bool
  has_event_source (entt::id_type event_type_id) const
  {
    std::lock_guard<std::recursive_mutex> lock (mutex);
    return std::ranges::any_of (
        registered_signal_sources,
        [event_type_id] (const registered_event_source &source) {
          return source.event_type_id == event_type_id;
        });
  }

  bool
  connect (entt::id_type event_type_id, entt::id_type system_type_id,
           const std::string &handler_name)
  {
    std::lock_guard<std::recursive_mutex> lock (mutex);
    const registered_event_sink *registered_handler = nullptr;
    for (const registered_event_sink &handler : registered_event_sinks) {
      if (handler.event_type_id == event_type_id
          && handler.system_type_id == system_type_id
          && handler.handler_name == handler_name) {
        registered_handler = &handler;
        break;
      }
    }

    if (registered_handler == nullptr) {
      wsl::log::rsc ()->trace (
          "connect: no sink found for event={:#x} system={:#x} handler='{}'",
          event_type_id, system_type_id, handler_name);
      return false;
    }

    if (db != nullptr && registered_handler->payload_size != 0) {
      const auto event = db->entries.find (event_type_id);
      if (event != db->entries.end () && event->second.size != 0
          && event->second.size != registered_handler->payload_size) {
        wsl::log::rsc ()->error (
            "connect: payload size mismatch for event={:#x} handler='{}' "
            "(source {}, sink {})",
            event_type_id, handler_name, event->second.size,
            registered_handler->payload_size);
        return false;
      }
    }

    if (registered_handler->payload_layout_hash != 0) {
      if (db != nullptr) {
        const auto event = db->entries.find (event_type_id);
        if (event != db->entries.end ()
            && event->second.payload_layout_hash != 0
            && event->second.payload_layout_hash
                   != registered_handler->payload_layout_hash) {
          wsl::log::rsc ()->error (
              "connect: payload layout mismatch for event={:#x} handler='{}'",
              event_type_id, handler_name);
          return false;
        }
      }
      for (const registered_event_sink &other : registered_event_sinks) {
        if (other.event_type_id == event_type_id
            && other.payload_layout_hash != 0
            && other.payload_layout_hash
                   != registered_handler->payload_layout_hash) {
          wsl::log::rsc ()->error (
              "connect: script sinks disagree on payload layout for event={:#x}",
              event_type_id);
          return false;
        }
      }
    }

    for (const connected_sink &connection : connected_handlers) {
      if (connection.event_type_id == event_type_id
          && connection.system_type_id == system_type_id
          && connection.handler_name == handler_name) {
        // Connection already exists — the desired state is already
        // achieved.  This is expected when scenes load in parallel
        // (each scene's snapshot may redundantly carry connections
        // established by another scene) or when restoring from old
        // scene files that were saved with the global-connections
        // snapshot.
        wsl::log::rsc ()->trace (
            "connect: already connected event={:#x} system={:#x} handler='{}'",
            event_type_id, system_type_id, handler_name);
        return true;
      }
    }

    connected_sink connection;
    connection.event_type_id = event_type_id;
    connection.event_type_name = registered_handler->event_type_name;
    connection.system_type_id = system_type_id;
    connection.system_type_name = registered_handler->system_type_name;
    connection.handler_name = handler_name;
    connection.invoke = registered_handler->invoke;
    connection.owner_ptr = registered_handler->owner_ptr;
    connection.owner_identity = registered_handler->owner_identity;
    connection.payload_size = registered_handler->payload_size;
    connection.payload_layout_hash
        = registered_handler->payload_layout_hash;
    connected_handlers.push_back (connection);

    if (db != nullptr) {
      db->note_connection (
          event_type_id, registered_handler->event_type_name.c_str (),
          system_type_id, registered_handler->system_type_name.c_str (),
          handler_name.c_str ());
    }

    return true;
  }

  bool
  disconnect (entt::id_type event_type_id, entt::id_type system_type_id,
              const std::string &handler_name)
  {
    std::lock_guard<std::recursive_mutex> lock (mutex);
    const std::size_t old_size = connected_handlers.size ();
    std::erase_if (connected_handlers,
                   [event_type_id, system_type_id,
                    &handler_name] (const connected_sink &connection) {
                     return connection.event_type_id == event_type_id
                            && connection.system_type_id == system_type_id
                            && connection.handler_name == handler_name;
                   });

    if (connected_handlers.size () == old_size) {
      return false;
    }

    if (db != nullptr) {
      std::erase_if (db->connections,
                     [event_type_id, system_type_id, &handler_name] (
                         const event_connection_debug_entry &connection) {
                       return connection.event_type_id == event_type_id
                              && connection.system_type_id == system_type_id
                              && connection.handler_name == handler_name;
                     });
      db->rebuild_listener_counts ();
    }

    return true;
  }

  template <typename Signal>
  void
  dispatch (const Signal &event)
  {
    note_emit<Signal> ();

    if (dispatcher != nullptr) {
      dispatcher->trigger (event);
    }

    if (!resolve_active_registry || !resolve_system_by_type) {
      return;
    }

    entt::registry *registry = resolve_active_registry ();
    if (registry == nullptr) {
      return;
    }

    const entt::id_type event_type_id = comp::stable_type_id<Signal> ();

    // Iterate a snapshot rather than `connected_handlers`: handlers run
    // unlocked, and one of them may connect or disconnect, which would
    // invalidate an iteration over the live vector.
    for (const dispatch_entry &entry : collect_handlers (event_type_id)) {
      if (entry.payload_size != 0 && entry.payload_size != sizeof (Signal)) {
        continue;
      }
      void *owner = resolve_owner (entry);
      if (owner == nullptr) {
        continue;
      }

      entry.invoke (owner, *registry, &event);
    }
  }

  /**
   * Type-erased dispatch for byte-level payloads.
   *
   * Behaves like :cpp:func:`dispatch<Signal>` but skips the
   * ``entt::dispatcher`` leg (which requires the concrete C++ type) and only
   * runs the connected-handler loop.
   *
   * :param event_type_id: Stable event type identifier.
   * :param data: Payload bytes; must match the registered event size.
   */
  void
  dispatch_by_id (entt::id_type event_type_id, const void *data,
                  std::size_t payload_size,
                  std::uint64_t payload_layout_hash = 0)
  {
    if (data == nullptr || payload_size == 0) {
      return;
    }
    if (db != nullptr) {
      auto it = db->entries.find (event_type_id);
      if (it != db->entries.end ()) {
        db->note_emit (event_type_id, it->second.type_name);
      }
    }

    if (!resolve_active_registry || !resolve_system_by_type) {
      return;
    }

    entt::registry *registry = resolve_active_registry ();
    if (registry == nullptr) {
      return;
    }

    for (const dispatch_entry &entry : collect_handlers (event_type_id)) {
      if (entry.payload_size != 0 && entry.payload_size != payload_size) {
        wsl::log::rsc ()->error (
            "event dispatch payload size mismatch for event={:#x} (expected {}, got {})",
            event_type_id, entry.payload_size, payload_size);
        continue;
      }
      if (entry.payload_layout_hash != 0
          && entry.payload_layout_hash != payload_layout_hash) {
        wsl::log::rsc ()->error (
            "event dispatch payload layout mismatch for event={:#x}",
            event_type_id);
        continue;
      }
      void *owner = resolve_owner (entry);
      if (owner == nullptr) {
        continue;
      }

      entry.invoke (owner, *registry, data);
    }
  }
};

/**
 * Emits a fully constructed signal instance.
 * :param Signal: Signal type.
 * :param hub: Signal hub that owns the runtime connections.
 * :param event: Signal instance to emit.
 */
template <typename Signal>
void
emit (event_hub &hub, const Signal &event)
{
  hub.dispatch<Signal> (event);
}

/**
 * Constructs and emits a signal instance in one call.
 * :param Signal: Signal type.
 * :param Args: Constructor argument types.
 * :param hub: Signal hub that owns the runtime connections.
 * :param args: Constructor arguments used to build the signal.
 */
template <typename Signal, typename... Args>
void
emit (event_hub &hub, Args &&...args)
{
  hub.dispatch<Signal> (Signal{ std::forward<Args> (args)... });
}

/** Declares an event owned by a system-like type. */
template <typename Signal, typename OwnerSystem>
void
declare_system (event_hub &hub)
{
  hub.template declare_event_source<Signal, OwnerSystem> ();
}

/** Declares a non-connectable event handler owned by a system. */
template <typename OwnerSystem>
void
declare_handler (event_hub &hub, const char *handler_name)
{
  hub.template declare_handler<OwnerSystem> (handler_name);
}

/** Declares a named system iteration and its component contract. */
template <typename OwnerSystem, typename... Components>
void
declare_iteration (event_hub &hub, const char *iteration_name)
{
  hub.template declare_iteration<OwnerSystem, Components...> (iteration_name);
}

/**
 * Non-owning typed view of a declared event source. Constructing one does NOT
 * (re-)declare the source; it must already be registered via
 * `event_hub::declare_event_source`. Use it to emit and to wire runtime
 * connections (entity identity now lives in the event payload, not the
 * connection).
 */
template <typename Event, typename EmitterSystem> class event_sink;

template <typename Event, typename EmitterSystem> class event_source
{
public:
  explicit event_source (event_hub &hub) : m_hub (hub) {}

  template <typename... Args>
  void
  emit (Args &&...args)
  {
    Event event{ std::forward<Args> (args)... };
    wsl::event::emit (m_hub, event);
  }

  void
  emit (const Event &event)
  {
    wsl::event::emit (m_hub, event);
  }

  template <typename HandlerSystem>
  bool
  add_listener (const event_sink<Event, HandlerSystem> &sink)
  {
    return m_hub.connect (comp::stable_type_id<Event> (),
                          comp::stable_type_id<HandlerSystem> (),
                          sink.handler_name ());
  }

  template <typename HandlerSystem>
  bool
  remove_listener (const event_sink<Event, HandlerSystem> &sink)
  {
    return m_hub.disconnect (comp::stable_type_id<Event> (),
                             comp::stable_type_id<HandlerSystem> (),
                             sink.handler_name ());
  }

private:
  event_hub &m_hub;
};

/** Non-owning typed view of a declared event sink (connectable handler). */
template <typename Event, typename HandlerSystem> class event_sink
{
public:
  event_sink (event_hub &hub, std::string handler_name)
      : m_hub (hub), m_handler_name (std::move (handler_name))
  {
  }

  const std::string &
  handler_name () const
  {
    return m_handler_name;
  }

private:
  event_hub &m_hub;
  std::string m_handler_name;
};

} // namespace wsl::event
