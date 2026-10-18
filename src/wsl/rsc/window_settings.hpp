#pragma once

#include "../comp/viewport_settings.hpp"

#ifndef IN_MODULE_INTERFACE
#include <cstdint>
#endif

namespace wsl::rsc
{

/** Initial window mode owned by Project Settings (D7). */
enum class window_mode : std::uint8_t
{
  windowed = 0,
  borderless_fullscreen = 1,
  fullscreen = 2,
  minimized = 3,
};

/**
 * Window configuration owned by Project Settings (D7).
 *
 * Applies to standalone game builds only (``!is_editor``); editor host
 * window dimensions and mode are preserved. Also holds the global viewport
 * quality defaults followed by every ``use_global`` viewport knob (D5).
 */
struct window_settings
{
  /** Initial window size in pixels. */
  int width = 1600;
  int height = 900;

  /** Initial window mode. */
  window_mode mode = window_mode::windowed;

  /** Minimum allowed window dimensions. */
  int min_width = 320;
  int min_height = 200;

  /** Maximum allowed window dimensions (0 = unlimited). */
  int max_width = 0;
  int max_height = 0;

  /** Whether the player can resize the game window. */
  bool resizable = true;

  /** Global viewport quality defaults (`use_global` fallback, D5). */
  comp::viewport_quality_defaults quality{};

  bool
  operator== (const window_settings &o) const
  {
    return width == o.width && height == o.height && mode == o.mode
           && min_width == o.min_width && min_height == o.min_height
           && max_width == o.max_width && max_height == o.max_height
           && resizable == o.resizable && quality.filter == o.quality.filter
           && quality.msaa == o.quality.msaa
           && quality.render_scale == o.quality.render_scale;
  }
  bool
  operator!= (const window_settings &o) const
  {
    return !(*this == o);
  }
};

} // namespace wsl::rsc
