# `wsl::rsc` — Resource Management

Asset loading, serialization, scene lifecycle, and project management. The resource system supports async loading with CPU/GPU staging and incremental uploads.

## Key Classes

| Class | Header | Description |
|-------|--------|-------------|
| `resource_manager` | `resource_manager.hpp` | Central hub for importing, loading, caching, and unloading all asset types. |
| `resource_manager_view` | `resource_manager.hpp` | Singleton component wrapping a non-owning pointer to the manager. |
| `scene` | `scene.hpp` | An EnTT registry + system list that forms a playable world. |
| `scene_manager` | `scene_manager.hpp` | Manages multiple scene instances (loading, activation, prefab instantiation). |
| `world` | `world.hpp` | Top-level container holding the active scene and context. |
| `project` | `project.hpp` | Metadata for an engine project. |

## Resource Types

| Type | ID type | Loader | Description |
|------|---------|--------|-------------|
| Model | `model_id` | `model_loader` | 3D models (glTF). Produces `gfx::model_3d`. |
| Image | `image_id` | `image_loader` | 2D textures (PNG, HDR). Produces `gfx::image`. |
| Cubemap | `cubemap_id` | `cubemap_loader` | Skybox/environment maps. Produces `gfx::cubemap`. |
| Scene | `scene_id` | `scene_loader` | Serialized entity snapshots. Produces `rsc::scene`. |
| Shader | `shader_id` | `shader_loader` | SDL GPU shaders. Produces `gfx::shader_module`. |
| Audio | `audio_id` | — | Audio clips via SDL_mixer. |
| UI Layout | `ui_layout_id` | — | RmlUi layout definitions (`.rml`). |
| Font | `font_id` | — | Font file paths for text rendering. |

## Usage

```cpp
#include <wsl/rsc/resource_manager.hpp>

wsl::rsc::resource_manager res_mgr(runtime_ctx, "path/to/resources");

// Import and load a model
auto model_id = res_mgr.import_model("models/character.glb");
auto model_handle = res_mgr.load(model_id);

// Access the loaded resource
if (auto *model = model_handle.operator->()) {
    model->ensure_gpu_buffers(render_ctx);
}

// Load a scene
auto scene_id = res_mgr.import_scene("scenes/level.wscene");
auto scene_handle = res_mgr.load(scene_id);
res_mgr.activate_scene(scene_id);

// Load a shader
auto shader_id = res_mgr.register_shader("shaders/test.frag");
auto shader_handle = res_mgr.load(shader_id);
```

## Scene Lifecycle

```cpp
wsl::rsc::scene scene(runtime_ctx, editor_ctx, "MyScene");
scene.add_system<wsl::sys::transform_system>();
scene.add_system<wsl::sys::physics_system>();
scene.init();

while (running) {
    scene.update(dt);
}

scene.stop_and_clear();
```

## Serialization

Scenes use Cereal (binary/JSON) archives. The `scene_snapshot_serializer` handles full registry snapshots including all registered components and singletons. Resource references use `io::resource_ref` (type + id pairs) for cross-scene asset binding.
