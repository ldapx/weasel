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
#include <glm/mat4x4.hpp>
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
 * Manipulation mode of the transform gizmo.
 *
 * ``none`` hides the widget and drops any active drag.
 */
enum class transform_gizmo_mode : uint8_t
{
  none,
  translate,
  rotate,
  scale,
};

/**
 * Pointer and camera state for one transform gizmo frame.
 */
struct transform_gizmo_input
{
  /** View transform of the camera that renders the viewport. */
  glm::mat4 view{ 1.0F };

  /** Projection transform of the camera that renders the viewport. */
  glm::mat4 proj{ 1.0F };

  /** Camera origin in world space. */
  glm::vec3 camera_position{ 0.0F };

  /** Viewport top-left corner in screen pixels (y down). */
  glm::vec2 viewport_min{ 0.0F };

  /** Viewport size in screen pixels. */
  glm::vec2 viewport_size{ 1.0F };

  /** Pointer position in screen pixels. */
  glm::vec2 pointer_position{ 0.0F };

  /**
   * True when the pointer may grab a handle. A drag that is already active
   * keeps running while this is false.
   */
  bool pointer_over_viewport = false;

  /** True while the primary mouse button is held. */
  bool primary_down = false;
};

/**
 * World-space translate, rotate, and scale manipulation widget.
 *
 * Controls a single 4x4 world matrix: dragging an axis handle moves, rotates,
 * or scales the matrix along that world axis, plane handles translate inside
 * their plane, and the center handle scales uniformly. The widget owns no
 * ImGui state: callers feed pointer state through
 * :cpp:func:`manipulate` and draw the returned geometry with
 * :cpp:func:`build_overlay` through the scene renderer's overlay pass.
 *
 * A single instance is stateful: call :cpp:func:`manipulate` exactly once per
 * frame and viewport.
 */
class transform_gizmo
{
public:
  /**
   * Runs one interaction frame and applies any active drag.
   *
   * :param input: Pointer and camera state for the frame.
   * :param mode: Active manipulation mode.
   * :param matrix: World matrix to manipulate in place.
   * :return: ``true`` when the matrix was updated during this call.
   */
  bool manipulate (const transform_gizmo_input &input,
                   transform_gizmo_mode mode, glm::mat4 &matrix);

  /**
   * Appends the widget's screen-space triangles to *out*.
   *
   * Uses the hover and drag state captured by the most recent
   * :cpp:func:`manipulate` call. Existing entries in *out* are preserved.
   *
   * :param matrix: World matrix the widget is attached to.
   * :param mode: Active manipulation mode.
   * :param view_proj: View-projection transform of the active camera.
   * :param camera_position: Camera origin in world space.
   * :param viewport_size: Render target size in pixels.
   * :param out: Destination vertex list.
   */
  void build_overlay (const glm::mat4 &matrix, transform_gizmo_mode mode,
                      const glm::mat4 &view_proj,
                      const glm::vec3 &camera_position,
                      const glm::vec2 &viewport_size,
                      std::vector<overlay_vertex> &out) const;

  /** Returns ``true`` while a handle drag is active. */
  [[nodiscard]] bool is_using () const;

  /** Returns ``true`` while the pointer hovers a handle. */
  [[nodiscard]] bool is_over () const;

  /** Drops hover and drag state; call when the target or mode changes. */
  void reset ();

private:
  /** Grabbable part of the widget. */
  enum class handle : uint8_t
  {
    none,
    translate_x,
    translate_y,
    translate_z,
    translate_xy,
    translate_yz,
    translate_xz,
    rotate_x,
    rotate_y,
    rotate_z,
    rotate_screen,
    scale_x,
    scale_y,
    scale_z,
    scale_uniform,
  };

  /** Returns the world axis index of an axis handle, or ``-1``. */
  static int axis_of (handle value);

  /** Returns the two world axis indices of a plane handle. */
  static bool plane_axes_of (handle value, int &out_u, int &out_v);

  /** Returns the axis handle of *mode* for a world axis index. */
  static handle axis_handle_of (transform_gizmo_mode mode, int axis);

  /** Returns the plane handle for two world axis indices. */
  static handle plane_handle_of (int u, int v);

  /** Returns true when *value* is the axis handle of *mode* and *axis*. */
  static bool is_axis_handle (handle value, transform_gizmo_mode mode,
                              int axis);

  /** Picks the handle under the pointer, or ``handle::none``. */
  [[nodiscard]] handle pick (const transform_gizmo_input &input,
                             transform_gizmo_mode mode, const glm::vec3 &origin,
                             float length) const;

  /** Captures drag start state for a picked handle. */
  void begin_drag (const transform_gizmo_input &input, handle picked,
                   const glm::mat4 &matrix, const glm::vec3 &origin);

  /** Applies the active drag to *matrix*. */
  bool apply_drag (const transform_gizmo_input &input, float length,
                   glm::mat4 &matrix);

  transform_gizmo_mode m_active_mode = transform_gizmo_mode::translate;
  handle m_hovered = handle::none;
  handle m_active = handle::none;
  bool m_pointer_was_down = false;
  bool m_release_on_next_frame = false;

  // Drag state captured when a handle is grabbed.
  glm::mat4 m_drag_start_matrix{ 1.0F };
  glm::vec3 m_drag_origin{ 0.0F };
  glm::vec3 m_drag_axis{ 0.0F };
  glm::vec3 m_drag_plane_point{ 0.0F };
  glm::vec3 m_drag_start_vector{ 0.0F };
  glm::vec3 m_drag_last_vector{ 0.0F };
  float m_drag_start_param = 0.0F;
  float m_drag_start_distance = 0.0F;
  float m_drag_accumulated_angle = 0.0F;
  glm::vec3 m_drag_start_position{ 0.0F };
  glm::quat m_drag_start_rotation{ 1.0F, 0.0F, 0.0F, 0.0F };
  glm::vec3 m_drag_start_scale{ 1.0F };
};

} // namespace gfx
} // namespace wsl
