#pragma once

#include "../gfx/viewport_resolve.hpp"

#ifndef IN_MODULE_INTERFACE
#include <entt/entt.hpp>
#endif
#ifndef IN_MODULE_INTERFACE
#include <glm/mat4x4.hpp>
#endif
#ifndef IN_MODULE_INTERFACE
#include <cstdint>
#endif
#ifndef IN_MODULE_INTERFACE
#include <vector>
#endif

namespace wsl::sys
{

/**
 * One viewport tree node recorded for input routing (TODO I1).
 *
 * The viewport renderer publishes this table into the scene registry context
 * every frame, so pointer events can be translated from window space through
 * the scale/aspect/letterbox chain into the deepest viewport's local virtual
 * space without recomputing the tree.
 */
struct viewport_route_entry
{
  entt::entity entity = entt::null; // null = presentation/root viewport
  /** Index of the parent entry, or -1 for the chain root. */
  int parent_index = -1;
  gfx::resolved_viewport resolved{};
  bool is_2d = false;
  bool input_routing = true;

  /** 2D placement inside the parent's virtual space (D2). */
  math::vec2f position{ 0.0F, 0.0F };
  math::vec2f size{ 0.0F, 0.0F };
  int display_order = 0;

  /** 3D quads: the world transform and the parent camera used to draw it. */
  bool has_quad = false;
  glm::mat4 quad_world{ 1.0F };
  glm::mat4 parent_view_proj{ 1.0F };
  /** Parent content rect inside its render target (3D pass viewport). */
  gfx::pixel_rect parent_render_rect{};
  math::vec2f parent_render_size{ 0.0F, 0.0F };
};

/**
 * Pointer-routing result.
 *
 * ``viewport`` is entt::null when the point landed on the presentation/root
 * viewport. ``point`` is expressed in that viewport's virtual space.
 */
struct viewport_route_point
{
  entt::entity viewport = entt::null;
  math::vec2f point{ 0.0F, 0.0F };
  bool hit = false;
};

/** Per-frame table of viewport tree nodes (entry 0 is the chain root). */
struct viewport_route_table
{
  std::vector<viewport_route_entry> entries;
  /** Window-space output rect of the chain root (letterbox aware). */
  gfx::pixel_rect root_output_rect{};

  void
  clear ()
  {
    entries.clear ();
    root_output_rect = gfx::pixel_rect{};
  }
};

/**
 * Translates a window-space pointer into the deepest routed viewport and its
 * local virtual coordinates.
 *
 * Honors `input_routing` per subviewport, descends 2D children through their
 * composed rects (highest `display_order` first) and 3D quads through a
 * ray/quad test against the parent camera stored in the table.
 */
viewport_route_point route_viewport_point (const viewport_route_table &table,
                                           math::vec2f window_point);

/** Convenience wrapper reading the table from the registry context. */
viewport_route_point route_viewport_point (entt::registry &registry,
                                           math::vec2f window_point);

} // namespace wsl::sys
