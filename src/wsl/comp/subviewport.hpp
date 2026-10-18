#pragma once

#include "../math/vector.hpp"
#include "component_meta.hpp"
#include "viewport_settings.hpp"

#ifndef IN_MODULE_INTERFACE
#include <entt/entt.hpp>
#endif

namespace wsl::comp
{

namespace singl
{
class runtime_context;
}

/**
 * Custom UI type for the subviewport camera picker.
 *
 * Wraps entt::entity to provide a custom inspector that only shows cameras
 * that are descendants of the subviewport entity. The picker's 2D/3D filter
 * is derived from the hosting entity (component-driven mode): an entity with
 * ``comp::transform_2d`` filters for ``comp::camera_2d`` descendants, an
 * entity with ``comp::transform`` filters for ``comp::camera`` descendants.
 */
struct subviewport_camera_ui
{
  entt::entity value = entt::null;

  bool custom_inspect (const char *label,
                       comp::singl::runtime_context *runtime_ctx);

  static void register_meta ();
};

/**
 * Defines a viewport sub-region for rendering.
 *
 * Placed on an entity in the hierarchy. The mode is component-driven:
 *
 * - An entity with ``comp::transform_2d`` is a 2D SubViewport. Its 2D
 *   transform (position/rotation/pivot) and ``size`` place it inside the
 *   parent viewport's virtual space, and it skips all 3D passes and 3D-only
 *   render targets.
 * - An entity with ``comp::transform`` / ``comp::world_transform`` is a 3D
 *   SubViewport. Its rendered texture is displayed on a world-space quad of
 *   ``world_quad_size``.
 *
 * Cameras find their containing viewport by walking up to the nearest
 * ancestor with this component (with cycle detection). Descendants render
 * into this viewport's own render target; the result composites into the
 * nearest ancestor viewport (2D overlay or 3D quad), and never presents
 * directly to the window unless this is the presentation root.
 */
struct subviewport : world_component
{
  /** 2D extents in the parent viewport's virtual space (D2). */
  math::vec2f size{ 320.0F, 180.0F };

  /** Size of the quad when rendered in 3D space. */
  math::vec2f world_quad_size{ 1.0F, 1.0F };

  /** Internal logical canvas used for layout and rendering calculations. */
  math::vec2f virtual_size{ 1920.0F, 1080.0F };

  /** 2D compositing order in the parent viewport (D11). */
  int display_order = 0;

  /** Enables routing mouse/pointer events into this subviewport (D14). */
  bool input_routing = true;

  /** 3D mode only: output raw linear half-float HDR without tonemapping. */
  bool hdr = false;

  /** Single mode-filtered camera entity picker. */
  subviewport_camera_ui camera{};

  /** Shared viewport settings (scale, aspect, filter, AA, env, ...). */
  viewport_settings settings{};

  /**
   * Derived, read-only: resolved output pixel dimensions.
   *
   * For a 2D SubViewport this is ``size * parent_scaling_factor`` (the pixel
   * rect in the parent render target). For a 3D SubViewport it is derived
   * from the screen-projected bounds of the quad. Never authored directly;
   * recomputed every frame by the viewport renderer.
   */
  math::vec2f container_size{ 0.0F, 0.0F };

  static void register_meta ();
};

/** Component-driven 2D/3D mode of a subviewport entity. */
enum class subviewport_mode : unsigned char
{
  /** Entity has ``comp::transform_2d``. */
  mode_2d = 0,
  /** Entity has ``comp::transform`` / ``comp::world_transform``. */
  mode_3d = 1,
};

/** Returns the component-driven mode for a subviewport entity. */
subviewport_mode subviewport_render_mode (entt::registry &registry,
                                          entt::entity entity);

/** True when the entity carries ``comp::transform_2d``. */
bool is_2d_subviewport (entt::registry &registry, entt::entity entity);

/**
 * Walks up the hierarchy from entity to find the nearest ancestor with a
 * subviewport component.
 *
 * Uses Floyd's cycle detection over the parent chain so a malformed
 * hierarchy cannot trap the walk (N1).
 *
 * :return: The nearest subviewport entity, or entt::null if none.
 */
entt::entity find_nearest_viewport (entt::registry &registry,
                                    entt::entity entity);

/**
 * Finds the viewport that owns an entity through its parent chain.
 *
 * For subviewport entities this returns the containing parent viewport, not
 * the entity itself. This is useful when traversing/rendering viewport nodes.
 */
entt::entity find_parent_viewport (entt::registry &registry,
                                   entt::entity entity);

/**
 * Returns true when an entity belongs to the render scope of a viewport.
 *
 * Root viewport membership is represented by target_viewport == entt::null.
 * Subviewport membership requires a descendant entity; the subviewport entity
 * itself is not part of its own render contents.
 */
bool entity_in_viewport_scope (entt::registry &registry, entt::entity entity,
                               entt::entity target_viewport);

/**
 * Returns true when an entity participates in shadow casting.
 *
 * Walks the ancestor chain: when any enclosing subviewport sets
 * ``settings.shadow_caster == false``, the whole subtree is excluded from
 * shadow passes (S5).
 */
bool entity_casts_shadows (entt::registry &registry, entt::entity entity);

} // namespace wsl::comp
