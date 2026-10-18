#pragma once

#include "gpu_resources.hpp"
#include "renderer.hpp"
#include "wsl/rsc/resource_ids.hpp"
#ifndef IN_MODULE_INTERFACE
#include <glm/glm.hpp>
#endif
#ifndef IN_MODULE_INTERFACE
#include <optional>
#endif
#ifndef IN_MODULE_INTERFACE
#include <vector>
#endif
#ifndef IN_MODULE_INTERFACE
#include <map>
#endif

namespace wsl::gfx
{

/**
 * High-performance 2D renderer using batching.
 *
 * Groups sprites by texture to minimize draw calls and state changes.
 * Supports transparency and z-index sorting.
 */
class batch_renderer_2d : public renderer
{
public:
  struct vertex_2d
  {
    glm::vec2 pos;
    glm::vec2 uv;
    glm::vec4 color;
  };

  struct draw_command
  {
    wsl::rsc::image_id image;
    /**
     * If non-null, bypasses the resource manager and uses this texture
     * directly.  Used for subviewport offscreen targets.
     */
    SDL_GPUTexture *texture_override = nullptr;

    glm::vec2 position;
    glm::vec2 size;
    float rotation = 0.0F;
    glm::vec4 color{ 1.0F };
    glm::vec2 uv_offset{ 0.0F };
    glm::vec2 uv_scale{ 1.0F };
    int z_index = 0;
    bool flip_h = false;
    bool flip_v = false;
    /** Use nearest-neighbour sampling for this draw (viewport filter). */
    bool nearest_filter = false;
  };

  batch_renderer_2d (wsl::gfx::render_window &window, render_context *ctx,
                     wsl::rsc::resource_manager *res_mgr);
  ~batch_renderer_2d () override;

  /** Submits a sprite for rendering in the current frame. */
  void submit (const draw_command &cmd);

  /**
   * Overrides the orthographic projection used during flush.
   * Default is glm::ortho(0, w, h, 0, -1, 1) for screen-space rendering.
   */
  void set_projection (const glm::mat4 &proj);

  /**
   * Builds vertex data from the current queue and uploads it to the GPU.
   * Must be called OUTSIDE an active render pass.
   */
  void build_and_upload ();

  /**
   * Draws the previously-uploaded batches. Must be called INSIDE a render
   * pass.
   */
  void draw ();

  /**
   * Convenience: uploads (if needed) and draws in one call.
   * Only safe when no render pass is active during upload.
   */
  void flush ();

  /** Returns a copy of the current draw queue (for multi-viewport replay). */
  [[nodiscard]] std::vector<draw_command> snapshot_queue () const;

  /** Replaces the draw queue with a previously saved snapshot. */
  void restore_queue (const std::vector<draw_command> &cmds);

private:
  void create_pipeline ();
  void destroy_pipeline ();
  void create_buffers ();
  void destroy_buffers ();

  struct batch
  {
    SDL_GPUTexture *texture = nullptr;
    uint32_t first_vertex = 0;
    uint32_t vertex_count = 0;
    bool nearest_filter = false;
  };

  std::vector<draw_command> m_queue;
  std::vector<vertex_2d> m_vertices;
  std::vector<batch> m_batches;
  glm::mat4 m_projection{ 1.0F };
  std::optional<glm::mat4> m_override_projection;

  gpu_graphics_pipeline m_pipeline;
  gpu_sampler m_sampler;
  gpu_sampler m_nearest_sampler;
  gpu_buffer m_vbo;
  gpu_transfer_buffer m_vbo_transfer;

  static constexpr uint32_t max_sprites_per_flush = 4096;
  static constexpr uint32_t max_vertices = max_sprites_per_flush * 6;
};

} // namespace wsl::gfx
