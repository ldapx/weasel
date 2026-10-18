#pragma once

#include "../comp/viewport_settings.hpp"
#include "viewport.hpp"

#ifndef IN_MODULE_INTERFACE
#include <algorithm>
#endif

// Hawkmoth (the docs autodoc tool) drops comment association for free
// functions carrying a literal ``[[nodiscard]]`` attribute, which would erase
// this header from the generated reference. Routing the attribute through a
// macro keeps the compile-time guarantee and the documentation.
#define WSL_VP_NODISCARD [[nodiscard]]

namespace wsl
{
namespace gfx
{

/**
 * Resolved viewport produced by the engine-level resolution chain.
 *
 * The chain is (spec section 2 / TODO R4):
 *
 *   1. base resolution       -- ``base_size``    (design/virtual reference)
 *   2. container resolution  -- ``container_size`` (available output pixels)
 *   3. rendering resolution  -- ``render_size``  (GPU render target pixels)
 *   4. viewport resolution   -- per-viewport target dims (== ``render_size``)
 *   5. window resolution     -- outside this struct (the presenter)
 *
 * Every stage is inspectable, so a wrong on-screen size can be traced to
 * exactly one stage. ``scale`` maps virtual units to *container* pixels;
 * ``target_px_per_virtual()`` maps virtual units to render-target pixels and
 * is what 2D composition rects for nested viewports are built from.
 */
struct resolved_viewport
{
  /** 1. Base/design resolution of the viewport. */
  math::vec2f base_size{ 1280.0F, 720.0F };
  /** 2. Available output area in pixels (window / panel / parent pixels). */
  math::vec2f container_size{ 0.0F, 0.0F };
  /** 3./4. GPU render target size in pixels. */
  math::vec2f render_size{ 0.0F, 0.0F };
  /** Camera-visible virtual area. Determines the projection aspect ratio. */
  math::vec2f virtual_size{ 1280.0F, 720.0F };
  /** Virtual units -> container pixels. */
  math::vec2f scale{ 1.0F, 1.0F };
  /** Extra rendering-scale multiplier (quality knob). */
  float render_scale = 1.0F;
  /** Where the content is presented inside the container. */
  pixel_rect output_rect{};
  /** Content rect inside the render target (letterbox handled at present). */
  pixel_rect render_rect{};
  /** True when both axes use the same scale factor. */
  bool uniform_scale = true;

  [[nodiscard]] float
  aspect_ratio () const
  {
    if (virtual_size.y () <= 0.0F || virtual_size.x () <= 0.0F) {
      return 1.0F;
    }
    return virtual_size.x () / virtual_size.y ();
  }

  /** True when the render target size differs from the container size. */
  WSL_VP_NODISCARD bool
  renders_offscreen () const
  {
    return static_cast<int> (render_size.x ())
               != static_cast<int> (container_size.x ())
           || static_cast<int> (render_size.y ())
                  != static_cast<int> (container_size.y ());
  }

  /** True when the content covers the whole container. */
  WSL_VP_NODISCARD bool
  full_output () const
  {
    return output_rect.width >= static_cast<int> (container_size.x ())
           && output_rect.height >= static_cast<int> (container_size.y ());
  }

  /** Virtual units -> render-target pixels. */
  [[nodiscard]] math::vec2f
  target_px_per_virtual () const
  {
    if (virtual_size.x () <= 0.0F || virtual_size.y () <= 0.0F) {
      return scale;
    }
    return math::vec2f{ render_size.x () / virtual_size.x (),
                        render_size.y () / virtual_size.y () };
  }

  WSL_VP_NODISCARD bool
  operator== (const resolved_viewport &o) const
  {
    return base_size == o.base_size && container_size == o.container_size
           && render_size == o.render_size && virtual_size == o.virtual_size
           && scale == o.scale && render_scale == o.render_scale
           && output_rect == o.output_rect && render_rect == o.render_rect
           && uniform_scale == o.uniform_scale;
  }
};

/**
 * Resolves the viewport configuration against a container and base size.
 *
 * Pure math: safe to unit-test without a GPU. The rendering backend only
 * applies ``render_size``/``render_rect``/``output_rect``.
 *
 * - ``scale_2d`` / ``disabled`` allocate the render target at the container
 *   size (times ``render_scale``); the aspect policy positions the content
 *   inside the container (letterbox / expand / visible-area change).
 * - ``viewport`` allocates the render target at the virtual size (times
 *   ``render_scale``); the resulting texture is presented into
 *   ``output_rect`` subject to the aspect policy.
 */
WSL_VP_NODISCARD resolved_viewport resolve_viewport (
    const comp::viewport_settings &settings, math::vec2f container_size,
    math::vec2f base_size, float resolved_render_scale = 1.0F);

/**
 * Computes the dest rect of a 2D child viewport inside its parent's render
 * target.
 *
 * ``position``/``size`` are given in the parent's virtual space (D2).
 * ``parent_target_scale`` is the parent's ``target_px_per_virtual()``.
 * ``child`` supplies the child's aspect-policy output rect inside its own
 * container.
 */
WSL_VP_NODISCARD pixel_rect compose_2d_rect (math::vec2f position,
                                             math::vec2f size,
                                             math::vec2f parent_target_scale,
                                             const resolved_viewport &child);

/**
 * Maps a point from container/window pixels into the viewport's virtual
 * space through the resolved output rect (letterbox aware).
 *
 * Returns false when the point falls into the unused border area outside
 * ``output_rect``.
 */
WSL_VP_NODISCARD bool
map_output_point_to_virtual (math::vec2f point,
                             const resolved_viewport &resolved,
                             math::vec2f &out_virtual);

/**
 * Maps a point from container/window pixels into render-target pixels.
 *
 * Inverse of the presentation mapping: the output rect shows the full render
 * target, so this is used when routing input towards 3D quads.
 */
WSL_VP_NODISCARD bool
map_output_point_to_target (math::vec2f point,
                            const resolved_viewport &resolved,
                            math::vec2f &out_target_px);

/** Intersection of two pixel rects (empty rect when disjoint). */
WSL_VP_NODISCARD pixel_rect intersect_rect (const pixel_rect &a,
                                            const pixel_rect &b);

} // namespace gfx
} // namespace wsl

#undef WSL_VP_NODISCARD
