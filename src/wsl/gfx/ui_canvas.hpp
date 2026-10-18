#pragma once

#include "gpu_resources.hpp"
#include "viewport.hpp"

#ifndef IN_MODULE_INTERFACE
#include <cstdint>
#endif

namespace wsl::gfx
{

class render_window;
class render_context;

/**
 * Offscreen virtual-canvas target for the game UI (D15 / TODO U1).
 *
 * RmlUi renders into this texture at the root viewport's virtual (base)
 * resolution. `composite()` blits it into the window's present/swapchain
 * targets through the resolved output rect, so the UI scales with the
 * viewport's scaling mode and aspect ratio policy exactly like scene content.
 */
class ui_canvas
{
public:
  ui_canvas () = default;
  ~ui_canvas ();

  ui_canvas (const ui_canvas &) = delete;
  ui_canvas &operator= (const ui_canvas &) = delete;

  /** (Re)allocates the canvas when the virtual size changes. */
  void ensure (render_window &window, render_context &ctx, uint32_t width,
               uint32_t height);

  /** Clears the canvas to transparent for this frame. */
  void begin_frame (render_context &ctx) const;

  /**
   * Composites the canvas over the window present texture and, when
   * `include_swapchain` is set (standalone presentation), the swapchain.
   */
  void composite (render_window &window, pixel_rect output_rect,
                  bool include_swapchain);

  void destroy ();

  [[nodiscard]] SDL_GPUTexture *
  texture () const
  {
    return m_canvas.get ();
  }
  [[nodiscard]] uint32_t
  width () const
  {
    return m_width;
  }
  [[nodiscard]] uint32_t
  height () const
  {
    return m_height;
  }
  [[nodiscard]] bool
  valid () const
  {
    return static_cast<bool> (m_canvas);
  }

private:
  void ensure_pipeline (render_window &window, render_context &ctx);

  gpu_texture m_canvas;
  gpu_graphics_pipeline m_pipeline;
  gpu_sampler m_sampler;
  uint32_t m_width = 0;
  uint32_t m_height = 0;
};

} // namespace wsl::gfx
