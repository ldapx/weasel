# `wsl::reg` — Registry & Registration Infrastructure

Central registration system for components, singletons, system factories, and signal/event wiring. This namespace bridges the ECS core with the editor and serialization layers.

## Key Classes

| Class | Header | Description |
|-------|--------|-------------|
| `component_registry` | `component_registry.hpp` | Registers engine component metadata and scene-local Daslang component pools backed by EnTT entity membership. |
| `singleton_registry` | `singleton_registry.hpp` | Registers singleton (registry-scoped) components with serialize/deserialize support. |
| `system_factory_registry` | `system_factory_registry.hpp` | Maps system names to factory functions for dynamic instantiation in scenes. |
| `registry_queries` | `registry_queries.hpp` | Cross-concept query interface — find which systems/iterations/signals relate to a given component or entity. |
| `registry_handle` | `registry_handle.hpp` | Lightweight, non-owning handle to `entt::registry`. Used in public APIs (including daslang bindings) to avoid leaking the registry implementation detail. |

## Component Types

Components are classified into native engine types and Daslang runtime types:

| Kind | Storage | Description |
|------|---------|-------------|
| `CPP_NATIVE` | `entt::registry::storage<T>` | Engine-built C++ components (transform, camera, etc.) |
| `DAS_SCRIPT` | EnTT `sparse_set` membership plus a scene-local fixed-page value store in `registry.ctx()` | Daslang-defined components without a generated C++ backing type |

Both kinds are unified through `ComponentTypeInfo`:

```cpp
struct ComponentTypeInfo {
    uint64_t type_id;           // Stable type identifier
    ComponentKind kind;         // Storage kind
    size_t struct_size;         // Size in bytes
};
```

Type IDs are stable hashes: canonical Daslang component identity for script
types and `wsl::comp::stable_type_id<T>()` for engine types. Native EnTT
storage IDs are tracked separately from stable serialized IDs.

## Event System (`event/`)

| Class | Header | Description |
|-------|--------|-------------|
| `event_hub` | `event/event_hub.hpp` | Central hub for declaring observer events, iterations, and event handlers. Supports typed event emission and explicit runtime connection. Events are type-level: entity identity lives in the event payload, not the connection. |
| `event_hub_fwd` | `event/event_hub_fwd.hpp` | Forward declarations for event types. |
| `event_source` / `event_sink` | `event/event_hub.hpp` | Non-owning typed handles; `add_listener`/`remove_listener` forward to `event_hub::connect`/`disconnect`. |

## Runtime Module Loading

`runtime_project_module` handles discovery and loading of Daslang project code:

```cpp
#include <wsl/reg/runtime_project_module.hpp>

wsl::reg::runtime::runtime_project_module rt_module(runtime_ctx);

// Synchronous compilation and loading
rt_module.compile_and_load(project);

// Deferred reload; polling performs compilation on the polling thread.
rt_module.compile_and_load_async(project);
while (rt_module.is_reloading()) {
    rt_module.poll_async_reload();  // compiles and finalizes when polled
}

// Access the daslang engine for script execution
auto *engine = rt_module.get_das_engine();
```

## Usage

```cpp
#include <wsl/reg/component_registry.hpp>
#include <wsl/reg/singleton_registry.hpp>
#include <wsl/reg/system_factory_registry.hpp>

// Register a world component
wsl::reg::component_registry comp_reg;
comp_reg.register_world_component<wsl::comp::transform>();
comp_reg.register_world_component<wsl::comp::camera>();

// Query registered components
auto desc = comp_reg.find("transform");
if (desc && desc->can_add_default) {
    desc->emplace_default(registry, entity);
}

// Copy a component between entities
comp_reg.copy_world_component(src_reg, src_ent, dst_reg, dst_ent, type_id);

// Register a singleton
wsl::reg::singleton_registry sing_reg;
sing_reg.register_singleton_component<wsl::comp::singl::runtime_context>(
    { .core = true });

// Ensure core singletons exist in a registry
sing_reg.apply_core_singleton_components(registry);

// Register a system factory
wsl::reg::system_factory_registry sys_reg;
sys_reg.register_system<wsl::sys::physics_system>("Physics");
sys_reg.register_system<wsl::sys::transform_system>("Transform");

// Create a system instance in a scene
auto system = sys_reg.create("Physics", scene);

// Declare dependencies and conflicts
sys_reg.declare_system_dependency<wsl::sys::shadow_system, wsl::sys::lighting_system>();
sys_reg.declare_system_conflict<wsl::sys::my_custom_physics, wsl::sys::physics_system>();
```

## Serialization Support

Component and singleton registries provide function pointers (`save_binary`, `load_binary`, `save_json`, `load_json`) for each registered type. They take the rfl-based wrappers from `serialize/types.hpp` and are used by the scene snapshot serializer:

```cpp
serialize::json_writer writer(ss);
for (auto &desc : comp_reg.ordered ()) {
    desc->save_json(writer, registry);
}
```

## Debug Queries

`registry_queries` provides introspection:

- `get_addable_world_components()` — components not yet on an entity
- `get_matching_iterations()` — system iterations that operate on an entity's components
- `find_signals_using_world_component()` — signals relevant to a component type
