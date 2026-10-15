# `wsl::gfx` — Graphics / Rendering

SDL_GPU-based renderer with mesh, shader, material, texture, and post-processing pipeline. Supports HDR rendering, bloom, MSAA, and shadow mapping.

## Key Classes & Types

| Type | Header | Description |
|------|--------|-------------|
| `render_window` | `render_window.hpp` | Owns an SDL window and GPU device. Manages swapchain, depth buffer, HDR textures, bloom ping-pong, and render passes. |
| `render_context` | `render_context.hpp` | GPU device + command buffer context for the current frame. |
| `renderer` | `renderer.hpp` | Base marker type; also defines `lighting_ubo` struct shared by shaders. |
| `scene_renderer` | `scene_renderer.hpp` | High-level scene rendering — collects entities with `model_instance_3d` + `world_transform` and issues draw calls. |
| `model_3d` | `model_3d.hpp` | GPU-ready model with vertex/index buffers, LOD groups, scenes, and bounding boxes. |
| `mesh` | `mesh.hpp` | Collection of `primitive` (vertex + index data + material). |
| `material` | `material.hpp` | PBR material parameters (base color, metallic, roughness, emissive) + GPU texture references. |
| `shader` | `shader.hpp` | Static helpers to load SDL_GPUShader from disk or resource manager. |
| `image` | `image.hpp` | GPU texture wrapper (SDL_GPUTexture + dimensions). |
| `cubemap` | `cubemap.hpp` | Cubemap texture for skyboxes / IBL. |
| `vertex` | `mesh.hpp` | Vertex layout: position, normal, uv, tangent. |
| `texture` | `mesh.hpp` | Lightweight GPU texture handle (texture + width + height). |
| `lighting` | `lighting.hpp` | Lighting constants and UBO layout matching shader uniforms. |

## Rendering Pipeline

```
  3D Pass (opaque/transparent)
       │
       ▼
  HDR Scene Texture
       │
       ▼
  Bloom (downsample → blur × N → composite)
       │
       ▼
  Tonemap → LDR Present Texture
       │
       ▼
  UI Pass (RmlUi overlay)
       │
       ▼
  Swapchain Present
```

## Usage

```cpp
#include <wsl/gfx/render_window.hpp>
#include <wsl/gfx/model_3d.hpp>
#include <wsl/gfx/shader.hpp>

// Create render window
wsl::gfx::render_window window("Viewport", 1920, 1080, render_ctx, res_mgr);

// Render frame
window.begin_3d_pass();

// Bind model and draw
model->bind(pass);
SDL_DrawGPUIndexedPrimitives(pass, model->index_count, 1, 0, 0, 0);

window.end_3d_pass();
window.postprocess_hdr_bloom();
window.begin_ui_pass();
// ... RmlUi draw ...
window.end_ui_pass();

// Load shaders
SDL_GPUShader *vs = wsl::gfx::shader::load(device, "shaders/default.vert",
    SDL_GPU_SHADERSTAGE_VERTEX, 2, 1);

// Create procedural models
auto quad = wsl::gfx::model_3d::make_unit_quad();
auto cube = wsl::gfx::model_3d::make_unit_cube();
auto sphere = wsl::gfx::model_3d::make_unit_sphere();
```

## Material Parameters

```cpp
wsl::gfx::material mat;
mat.base_color_factor = { 1.0f, 0.2f, 0.2f, 1.0f }; // red
mat.metallic_factor = 0.5f;
mat.roughness_factor = 0.3f;
mat.base_color_tex = texture->texture_data;
mat.sampler = sampler;
```
