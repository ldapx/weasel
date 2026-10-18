#pragma once

#include "gpu_resources.hpp"
#ifndef IN_MODULE_INTERFACE
#include <cstdint>
#endif

namespace wsl::gfx
{

class render_window;
class render_context;

/** Allocation parameters for a subviewport render-target set (S5). */
struct subviewport_target_config
{
  uint32_t width = 0;
  uint32_t height = 0;
  /**
   * MSAA sample count for the render targets: 1 (off), 2 or 4. Ignored for
   * 2D subviewports, which never allocate MSAA or depth targets.
   */
  int sample_count = 1;
  /** 2D subviewports skip depth/MSAA/bloom allocation and 3D passes. */
  bool is_2d = false;
  /** 3D mode only: keep raw linear HDR instead of tonemapping to LDR. */
  bool hdr = false;
  /** Allocate bloom targets and run the bloom pass during post-processing. */
  bool bloom = false;
};

/**
 * GPU render-target set for a single subviewport.
 *
 * Contains an optional MSAA colour target + 1x resolve (HDR), an optional
 * LDR/display-referred output, optional bloom source + ping-pong targets and
 * an optional depth target. 2D subviewports allocate only the single-sample
 * colour target; 3D subviewports with `hdr == false` additionally allocate
 * the LDR output that the per-viewport tonemap writes into.
 */
struct subviewport_target
{
  gpu_texture color_msaa;     // MSAA render target (sample_count > 1)
  gpu_texture color_resolve;  // 1x HDR target / resolve destination
  gpu_texture ldr_color;      // LDR tonemapped output (3D, hdr == false)
  gpu_texture bloom_src_msaa; // MRT bloom source (3D + bloom)
  gpu_texture bloom_src_resolve;
  gpu_texture bloom_a; // half-res ping-pong
  gpu_texture bloom_b;
  gpu_texture depth; // 3D only

  uint32_t width = 0;
  uint32_t height = 0;
  int sample_count = 1;
  bool is_2d = false;
  bool hdr = false;
  bool bloom_enabled = false;

  static subviewport_target create (render_window *window, render_context *ctx,
                                    const subviewport_target_config &config);
  static void destroy (subviewport_target &target);

  /** True when the existing target matches the requested configuration. */
  [[nodiscard]] bool
  matches (const subviewport_target_config &config) const
  {
    return width == config.width && height == config.height
           && sample_count == (config.is_2d ? 1 : config.sample_count)
           && is_2d == config.is_2d && hdr == config.hdr
           && bloom_enabled == config.bloom;
  }

  /**
   * Texture handed to the parent viewport for composition.
   *
   * 2D subviewports and HDR 3D subviewports expose their linear resolve
   * target; LDR 3D subviewports expose the tonemapped display-referred
   * output (C2).
   */
  [[nodiscard]] gpu_texture *
  present_texture ()
  {
    if (!is_2d && !hdr && ldr_color) {
      return &ldr_color;
    }
    return &color_resolve;
  }
  [[nodiscard]] const gpu_texture *
  present_texture () const
  {
    if (!is_2d && !hdr && ldr_color) {
      return &ldr_color;
    }
    return &color_resolve;
  }
};

} // namespace wsl::gfx
