#pragma once

#ifndef IN_MODULE_INTERFACE
#include <cstdint>
#endif

namespace wsl
{

namespace gfx
{

/** Integer rectangle in render-target pixels. */
struct pixel_rect
{
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;

  [[nodiscard]] bool
  empty () const
  {
    return width <= 0 || height <= 0;
  }

  [[nodiscard]] bool
  covers (uint32_t target_width, uint32_t target_height) const
  {
    return x <= 0 && y <= 0 && width >= static_cast<int> (target_width)
           && height >= static_cast<int> (target_height);
  }

  [[nodiscard]] bool
  operator== (const pixel_rect &other) const
  {
    return x == other.x && y == other.y && width == other.width
           && height == other.height;
  }

  [[nodiscard]] bool
  operator!= (const pixel_rect &other) const
  {
    return !(*this == other);
  }
};

/**
 * Internal resolved viewport rectangle.
 *
 * After TODO R5 this is *not* an authored viewport concept: the only
 * viewport concepts are the root viewport (stored in
 * ``comp::singl::rendering_manager``) and ``comp::subviewport``. This type
 * only carries the GPU-applied pixel rectangle and depth range produced by
 * the engine-level resolve helper for the rendering backend.
 */
struct viewport
{
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  float min_depth = 0.0F;
  float max_depth = 1.0F;

  [[nodiscard]] pixel_rect
  rect () const
  {
    return pixel_rect{ x, y, width, height };
  }

  [[nodiscard]] bool
  is_fullscreen (int screen_width, int screen_height) const
  {
    return x <= 0 && y <= 0 && width >= screen_width && height >= screen_height;
  }

  [[nodiscard]] float
  aspect_ratio () const
  {
    if (height <= 0 || width <= 0) {
      return 1.0F;
    }
    return static_cast<float> (width) / static_cast<float> (height);
  }

  [[nodiscard]] bool
  operator== (const viewport &other) const
  {
    return x == other.x && y == other.y && width == other.width
           && height == other.height && min_depth == other.min_depth
           && max_depth == other.max_depth;
  }

  [[nodiscard]] bool
  operator!= (const viewport &other) const
  {
    return !(*this == other);
  }

  template <class Archive>
  void
  serialize (Archive &ar)
  {
    ar (x, y, width, height, min_depth, max_depth);
  }
};

} // namespace gfx
} // namespace wsl
