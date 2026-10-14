#pragma once

#include "wsl/gfx/debug_overlay.hpp"

#ifndef IN_MODULE_INTERFACE
#include <cstdint>
#endif
#ifndef IN_MODULE_INTERFACE
#include <vector>
#endif
#ifndef IN_MODULE_INTERFACE
#include <glm/gtc/quaternion.hpp>
#endif
#ifndef IN_MODULE_INTERFACE
#include <glm/vec2.hpp>
#endif
#ifndef IN_MODULE_INTERFACE
#include <glm/vec3.hpp>
#endif

namespace wsl
{
namespace gfx
{

/**
 * Pointer and timing state consumed by :cpp:func:`view_gizmo::interact`.
 *
 * All positions and deltas are in normalized device coordinates relative to
 * the widget's viewport, so callers do not need to know the render target
 * resolution. ``y`` points up in NDC, matching the render pipeline.
 */
struct view_gizmo_input
{
  /** Pointer position in normalized device coordinates. */
  glm::vec2 pointer_position{ 0.0F };

  /** Pointer movement since the previous frame, in NDC units. */
  glm::vec2 pointer_delta{ 0.0F };

  /** Seconds elapsed since the previous frame. */
  float delta_time = 0.0F;

  /** True while the primary mouse button is held. */
  bool primary_down = false;

  /** True only on the frame the primary mouse button is released. */
  bool primary_released = false;

  /**
   * True when the pointer is allowed to start a new interaction, typically
   * because it hovers the owning viewport. An interaction that is already
   * active keeps running while this is false.
   */
  bool pointer_over_viewport = false;
};

/**
 * Camera state manipulated by :cpp:class:`view_gizmo`.
 *
 * The rotation is a camera-to-world quaternion; ``forward`` is the rotated
 * ``-Z`` axis, matching :cpp:class:`wsl::comp::camera` usage elsewhere.
 */
struct view_gizmo_camera
{
  /** Camera origin in world space. */
  glm::vec3 position{ 0.0F };

  /** Camera-to-world rotation. */
  glm::quat rotation{ 1.0F, 0.0F, 0.0F, 0.0F };

  /** World-space point the camera orbits around. */
  glm::vec3 pivot{ 0.0F };
};

/**
 * Placement of a :cpp:class:`view_gizmo` inside a viewport.
 *
 * The widget is authored in "design pixels": 256 design pixels span the
 * rotation widget at scale 1. ``scale`` converts design pixels to viewport
 * pixels, while ``ndc_per_pixel`` converts viewport pixels to the normalized
 * device coordinates emitted by :cpp:func:`view_gizmo::build_overlay`.
 */
struct view_gizmo_layout
{
  /** Widget center in normalized device coordinates. */
  glm::vec2 center{ 0.0F };

  /**
   * NDC units per viewport pixel along each axis. ``x`` is positive and ``y``
   * is negative because NDC y points up while viewport pixels point down.
   */
  glm::vec2 ndc_per_pixel{ 0.0F };

  /** Uniform widget size factor; 1.0 maps the reference design to 256 px. */
  float scale = 0.30F;

  /**
   * Builds a layout for a widget centered at a viewport pixel position.
   *
   * :param center_pixel: Widget center in viewport pixels (y down).
   * :param viewport_size: Viewport size in pixels; must be positive.
   * :param scale: Uniform widget size factor.
   * :return: Layout ready for :cpp:func:`view_gizmo::interact`.
   */
  static view_gizmo_layout from_pixel_center (const glm::vec2 &center_pixel,
                                              const glm::vec2 &viewport_size,
                                              float scale = 0.30F);
};

/**
 * Converts a viewport pixel position (y down) to normalized device
 * coordinates (y up).
 *
 * :param pixel: Position relative to the viewport's top-left corner.
 * :param viewport_size: Viewport size in pixels.
 * :return: Position in NDC.
 */
glm::vec2 view_gizmo_ndc_from_pixel (const glm::vec2 &pixel,
                                     const glm::vec2 &viewport_size);

/**
 * Converts a viewport pixel delta (y down) to an NDC delta (y up).
 *
 * :param delta: Movement relative to the previous frame, in pixels.
 * :param viewport_size: Viewport size in pixels.
 * :return: Movement in NDC.
 */
glm::vec2 view_gizmo_ndc_delta_from_pixel (const glm::vec2 &delta,
                                           const glm::vec2 &viewport_size);

/**
 * Options controlling view gizmo overlay geometry generation.
 */
struct view_gizmo_build_options
{
  /**
   * Width in viewport pixels of the antialiasing fringe. Zero emits hard
   * edges, which is preferable when the target is already multisampled.
   */
  float antialias_feather = 1.0F;
};

/**
 * Immediate-mode 3D view navigation widget.
 *
 * Mirrors the interaction of the previous editor view gizmo without depending
 * on ImGui: dragging the axis triad orbits the camera around its pivot,
 * clicking an axis handle snaps the camera to that axis, and the two tool
 * buttons dolly and pan the camera. Geometry is emitted as screen-space
 * triangles through :cpp:func:`build_overlay` and is expected to be drawn by
 * :cpp:func:`wsl::gfx::scene_renderer::draw_debug_overlay`.
 *
 * A single instance is stateful: call :cpp:func:`interact` exactly once per
 * frame and viewport while the widget is visible.
 */
class view_gizmo
{
public:
  /**
   * Runs one interaction frame and applies new camera state.
   *
   * :param input: Pointer and timing state for the frame.
   * :param layout: Widget placement in the owning viewport.
   * :param camera: Camera state to manipulate in place.
   * :return: ``true`` when the camera changed during this call.
   */
  bool interact (const view_gizmo_input &input, const view_gizmo_layout &layout,
                 view_gizmo_camera &camera);

  /**
   * Appends the widget's screen-space triangles to *out*.
   *
   * Uses the hover and drag state captured by the most recent
   * :cpp:func:`interact` call. Existing entries in *out* are preserved.
   *
   * :param camera: Camera state the widget was interacted with.
   * :param layout: Widget placement; must match the one passed to
   *   :cpp:func:`interact`.
   * :param out: Destination vertex list.
   * :param options: Geometry generation options.
   */
  void build_overlay (const view_gizmo_camera &camera,
                      const view_gizmo_layout &layout,
                      std::vector<overlay_vertex> &out,
                      const view_gizmo_build_options &options = {}) const;

  /** Returns ``true`` while a rotation, dolly, or pan drag is active. */
  [[nodiscard]] bool is_active () const;

  /** Returns ``true`` while the pointer hovers an axis or tool button. */
  [[nodiscard]] bool is_hovered () const;

  /** Returns ``true`` while an axis snap animation is playing. */
  [[nodiscard]] bool is_animating () const;

private:
  /** Active drag mode; only one tool can own the pointer at a time. */
  enum class tool : uint8_t
  {
    none,
    rotate,
    dolly,
    pan,
  };

  /** Axis ids used by the hover state. */
  enum axis_id : int
  {
    axis_none = -1,
    axis_positive_x = 0,
    axis_negative_x = 1,
    axis_positive_y = 2,
    axis_negative_y = 3,
    axis_positive_z = 4,
    axis_negative_z = 5,
    axis_center = 6,
  };

  /** Duration of the axis snap animation in seconds. */
  static constexpr float snap_animation_duration = 0.5F;

  int m_hovered_axis = axis_none;
  bool m_dolly_hovered = false;
  bool m_pan_hovered = false;
  tool m_active_tool = tool::none;

  // Axis snap animation state. While animating, the widget owns the camera
  // until the recorded target pose is reached.
  bool m_animating = false;
  float m_animation_time = 0.0F;
  float m_animation_start_distance = 0.0F;
  float m_animation_target_distance = 0.0F;
  glm::vec3 m_animation_target_position{ 0.0F };
  glm::vec3 m_animation_start_direction{ 0.0F };
  glm::vec3 m_animation_target_direction{ 0.0F };
  glm::vec3 m_animation_start_up{ 0.0F, 1.0F, 0.0F };
  glm::vec3 m_animation_target_up{ 0.0F, 1.0F, 0.0F };
};

} // namespace gfx
} // namespace wsl
