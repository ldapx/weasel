# `wsl::sys` — ECS Systems

Systems contain the *logic* that operates on entities through their components. Each system derives from `ecs_system` (or the CRTP helper `ecs_system_t<Derived>`) and overrides lifecycle hooks.

## Base Class

`ecs_system` (`system.hpp`) provides:

| Hook | Purpose |
|------|---------|
| `on_init(registry)` | One-time setup when the system is first activated. |
| `on_inactive(registry)` | Cleanup when the system is deactivated. |
| `on_update(registry, dt)` | Per-frame update logic. |
| `on_event(registry, event)` | SDL event handling. |
| `on_render_build_draw_data(registry)` | Prepare render data for the frame. |
| `on_render_prepare_gpu_rsc(registry)` | Upload GPU resources. |
| `on_render_record_draw_cmd(registry)` | Record GPU draw commands. |

The CRTP helper `ecs_system_t<Derived>` provides automatic type ID and a `register_iteration<Components...>()` method to declare typed iterations with the signal hub.

## Available Systems

| System | Header | Description |
|--------|--------|-------------|
| `transform_system` | `transform_system.hpp` | Recomputes `world_transform` from hierarchy + local transforms. |
| `animation_system` | `animation_system.hpp` | Samples ozz clips, crossfades them, and writes the model-space joint palette into `skeleton_pose`. |
| `physics_system` | `physics_system.hpp` | Steps the physics simulation and syncs `rigid_body` ↔ `transform`. |
| `lighting_system` | `lighting_system.hpp` | Updates GPU lighting UBO from `point_light`/`spot_light`/`directional_light` components. |
| `shadow_system` | `shadow_system.hpp` | Renders shadow maps for directional lights. |
| `render_3d_system` | `render_3d_system.hpp` | Collects visible meshes and issues draw calls. |
| `render_ui_system` | `render_ui_system.hpp` | Renders the RmlUi runtime UI overlay and forwards input to it. |
| `render_frame` | `render_frame.hpp` | Frame lifecycle management (begin/end render passes). |
| `skybox_system` | `skybox_system.hpp` | Renders the skybox cubemap. |
| `audio_system` | `audio_system.hpp` | Plays and manages audio sources. |

## Orchestration

`core_systems` (`core_systems.hpp`) owns and coordinates all built-in systems:

```cpp
wsl::sys::core_systems systems;
systems.init(runtime_ctx, editor_ctx);

// Per-frame
systems.update(dt);
systems.render(window, callbacks);
```

## Custom System

```cpp
class my_system : public wsl::sys::ecs_system_t<my_system>
{
public:
    using ecs_system_t::ecs_system_t;

    void on_update(entt::registry &registry, double dt) override
    {
        auto view = registry.view<comp::transform>();
        for (auto entity : view) {
            auto &t = view.get<comp::transform>(entity);
            t.position.y += static_cast<float>(dt);
        }
    }
};
```

Register with `reg::system_factory_registry` to make it available in scenes and the editor.
