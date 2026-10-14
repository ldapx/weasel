#include "transform_gizmo.hpp"

#include <cmath>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/matrix.hpp>

namespace wsl
{
namespace gfx
{

namespace
{

/** World length of the gizmo per unit of camera distance. */
constexpr float gizmo_length_factor = 0.15F;
/** Pixel radius used to grab axis and plane handles. */
constexpr float pick_radius_pixels = 9.0F;
/** Pixel radius used to grab the uniform scale handle. */
constexpr float pick_radius_center_pixels = 11.0F;
constexpr float axis_line_width = 1.5F;
constexpr float circle_line_width = 2.0F;
constexpr float arrow_head_pixels = 14.0F;
constexpr float arrow_half_pixels = 5.0F;
constexpr float box_half_pixels = 5.0F;
constexpr float center_box_half_pixels = 6.0F;
/** Plane handles span this fraction of the gizmo length. */
constexpr float plane_inner_fraction = 0.22F;
constexpr float plane_outer_fraction = 0.48F;
constexpr int circle_segments = 64;
constexpr float pi = 3.14159265358979323846F;
constexpr float two_pi = 2.0F * pi;

constexpr glm::vec4 axis_colors[3] = {
  glm::vec4 (230.0F / 255.0F, 51.0F / 255.0F, 51.0F / 255.0F, 1.0F),
  glm::vec4 (51.0F / 255.0F, 230.0F / 255.0F, 51.0F / 255.0F, 1.0F),
  glm::vec4 (51.0F / 255.0F, 128.0F / 255.0F, 255.0F / 255.0F, 1.0F),
};
constexpr glm::vec4 highlight_color{ 1.0F, 1.0F, 0.0F, 1.0F };
constexpr glm::vec4 center_color{ 210.0F / 255.0F, 210.0F / 255.0F,
                                  210.0F / 255.0F, 1.0F };

constexpr glm::vec3 world_axes[3]
    = { glm::vec3 (1.0F, 0.0F, 0.0F), glm::vec3 (0.0F, 1.0F, 0.0F),
        glm::vec3 (0.0F, 0.0F, 1.0F) };

/** Converts a screen pixel position into normalized device coordinates. */
glm::vec2
to_ndc (const glm::vec2 &screen, const glm::vec2 &viewport_min,
        const glm::vec2 &viewport_size)
{
  const glm::vec2 local = (screen - viewport_min) / viewport_size;
  return glm::vec2 ((local.x * 2.0F) - 1.0F, 1.0F - (local.y * 2.0F));
}

/** Builds a world-space pointer ray for the current pointer position. */
bool
make_pointer_ray (const transform_gizmo_input &input, glm::vec3 &out_origin,
                  glm::vec3 &out_direction)
{
  const glm::mat4 inverse_view_proj = glm::inverse (input.proj * input.view);
  const glm::vec2 ndc = to_ndc (input.pointer_position, input.viewport_min,
                                input.viewport_size);

  glm::vec4 near_point = inverse_view_proj * glm::vec4 (ndc, -1.0F, 1.0F);
  glm::vec4 far_point = inverse_view_proj * glm::vec4 (ndc, 1.0F, 1.0F);
  if ((std::abs (near_point.w) < 1.0e-6F)
      || (std::abs (far_point.w) < 1.0e-6F)) {
    return false;
  }

  near_point /= near_point.w;
  far_point /= far_point.w;
  out_origin = glm::vec3 (near_point);
  out_direction = glm::vec3 (far_point) - out_origin;

  const float length = glm::length (out_direction);
  if (length < 1.0e-6F) {
    return false;
  }
  out_direction /= length;
  return std::isfinite (out_direction.x);
}

/** Returns the parameter of the axis point closest to the ray, if any. */
bool
closest_axis_param (const glm::vec3 &ray_origin, const glm::vec3 &ray_direction,
                    const glm::vec3 &axis_origin, const glm::vec3 &axis,
                    float &out_param)
{
  const float parallel = glm::dot (axis, ray_direction);
  const float denominator = 1.0F - (parallel * parallel);
  if (denominator < 1.0e-5F) {
    return false;
  }
  const glm::vec3 offset = axis_origin - ray_origin;
  out_param = ((parallel * glm::dot (offset, ray_direction))
               - glm::dot (offset, axis))
              / denominator;
  return std::isfinite (out_param);
}

/** Intersects the ray with a plane; returns false on miss or hit behind. */
bool
intersect_plane (const glm::vec3 &ray_origin, const glm::vec3 &ray_direction,
                 const glm::vec3 &plane_point, const glm::vec3 &plane_normal,
                 glm::vec3 &out_point)
{
  const float denominator = glm::dot (plane_normal, ray_direction);
  if (std::abs (denominator) < 1.0e-5F) {
    return false;
  }
  const float t
      = glm::dot (plane_normal, plane_point - ray_origin) / denominator;
  if (t < 0.0F) {
    return false;
  }
  out_point = ray_origin + (ray_direction * t);
  return true;
}

/**
 * Projects a world point to overlay-local pixels (origin at the viewport
 * center, y down) so the overlay builder can place antialiased primitives.
 */
bool
project_local (const glm::mat4 &view_proj, const glm::vec3 &point,
               const glm::vec2 &viewport_size, glm::vec2 &out_local)
{
  const glm::vec4 clip = view_proj * glm::vec4 (point, 1.0F);
  if (clip.w <= 1.0e-5F) {
    return false;
  }
  const glm::vec3 ndc = glm::vec3 (clip) / clip.w;
  out_local = glm::vec2 (ndc.x * viewport_size.x * 0.5F,
                         -ndc.y * viewport_size.y * 0.5F);
  return true;
}

/** Projects a world point to screen pixels. */
bool
project_screen (const glm::mat4 &view_proj, const glm::vec3 &point,
                const glm::vec2 &viewport_min, const glm::vec2 &viewport_size,
                glm::vec2 &out_screen)
{
  glm::vec2 local;
  if (!project_local (view_proj, point, viewport_size, local)) {
    return false;
  }
  out_screen = viewport_min
               + glm::vec2 (local.x + (viewport_size.x * 0.5F),
                            local.y + (viewport_size.y * 0.5F));
  return true;
}

/** Returns the world-space length of the gizmo at *origin*. */
float
gizmo_length (const glm::vec3 &origin, const glm::vec3 &camera_position)
{
  return glm::max (
      glm::distance (camera_position, origin) * gizmo_length_factor, 1.0e-3F);
}

/** Returns the world size of one screen pixel at the gizmo origin. */
float
world_per_pixel (const transform_gizmo_input &input, const glm::vec3 &origin)
{
  const float distance = glm::distance (input.camera_position, origin);
  const float tan_half_fov
      = 1.0F / glm::max (std::abs (input.proj[1][1]), 1.0e-5F);
  return (2.0F * distance * tan_half_fov)
         / glm::max (input.viewport_size.y, 1.0F);
}

/** Split a matrix into position, rotation, and scale. */
void
decompose_transform (const glm::mat4 &matrix, glm::vec3 &out_position,
                     glm::quat &out_rotation, glm::vec3 &out_scale)
{
  out_position = glm::vec3 (matrix[3]);

  glm::vec3 x (matrix[0]);
  glm::vec3 y (matrix[1]);
  glm::vec3 z (matrix[2]);
  out_scale = glm::vec3 (glm::length (x), glm::length (y), glm::length (z));

  x = (out_scale.x > 1.0e-8F) ? x / out_scale.x : glm::vec3 (1.0F, 0.0F, 0.0F);
  y = (out_scale.y > 1.0e-8F) ? y / out_scale.y : glm::vec3 (0.0F, 1.0F, 0.0F);
  z = (out_scale.z > 1.0e-8F) ? z / out_scale.z : glm::vec3 (0.0F, 0.0F, 1.0F);

  out_rotation = glm::quat_cast (glm::mat3 (x, y, z));
}

/** Composes a world matrix from position, rotation, and scale. */
glm::mat4
compose_transform (const glm::vec3 &position, const glm::quat &rotation,
                   const glm::vec3 &scale)
{
  glm::mat4 matrix = glm::translate (glm::mat4 (1.0F), position);
  matrix *= glm::mat4_cast (rotation);
  matrix = glm::scale (matrix, scale);
  return matrix;
}

/** Returns the signed angle to rotate *from* onto *to* around *axis*. */
float
signed_angle (const glm::vec3 &from, const glm::vec3 &to, const glm::vec3 &axis)
{
  const float cosine = glm::clamp (glm::dot (from, to), -1.0F, 1.0F);
  const float angle = std::acos (cosine);
  const float sign
      = (glm::dot (glm::cross (from, to), axis) < 0.0F) ? -1.0F : 1.0F;
  return angle * sign;
}

/** Appends a screen-space arrow head pointing from *from* to *tip*. */
void
append_arrow_head (overlay_builder &builder, const glm::vec2 &from,
                   const glm::vec2 &tip, const glm::vec4 &color)
{
  const glm::vec2 delta = tip - from;
  const float length = glm::length (delta);
  if (length < 1.0e-3F) {
    return;
  }
  const glm::vec2 direction = delta / length;
  const glm::vec2 normal (-direction.y, direction.x);
  const glm::vec2 base = tip - (direction * arrow_head_pixels);
  builder.triangle (tip, base + (normal * arrow_half_pixels),
                    base - (normal * arrow_half_pixels), color);
}

/** Appends a screen-space square oriented along *direction*. */
void
append_box (overlay_builder &builder, const glm::vec2 &center,
            const glm::vec2 &direction, float half_size, const glm::vec4 &color)
{
  glm::vec2 unit (1.0F, 0.0F);
  const float length = glm::length (direction);
  if (length > 1.0e-4F) {
    unit = direction / length;
  }
  const glm::vec2 normal (-unit.y, unit.x);
  const glm::vec2 a = center + (unit * half_size) + (normal * half_size);
  const glm::vec2 b = center - (unit * half_size) + (normal * half_size);
  const glm::vec2 c = center - (unit * half_size) - (normal * half_size);
  const glm::vec2 d = center + (unit * half_size) - (normal * half_size);
  builder.triangle (a, b, c, color);
  builder.triangle (a, c, d, color);
}

} // namespace

int
transform_gizmo::axis_of (handle value)
{
  switch (value) {
  case handle::translate_x:
  case handle::rotate_x:
  case handle::scale_x:
    return 0;
  case handle::translate_y:
  case handle::rotate_y:
  case handle::scale_y:
    return 1;
  case handle::translate_z:
  case handle::rotate_z:
  case handle::scale_z:
    return 2;
  default:
    return -1;
  }
}

bool
transform_gizmo::plane_axes_of (handle value, int &out_u, int &out_v)
{
  switch (value) {
  case handle::translate_xy:
    out_u = 0;
    out_v = 1;
    return true;
  case handle::translate_yz:
    out_u = 1;
    out_v = 2;
    return true;
  case handle::translate_xz:
    out_u = 0;
    out_v = 2;
    return true;
  default:
    return false;
  }
}

transform_gizmo::handle
transform_gizmo::axis_handle_of (transform_gizmo_mode mode, int axis)
{
  switch (mode) {
  case transform_gizmo_mode::translate:
    return static_cast<handle> (static_cast<uint8_t> (handle::translate_x)
                                + axis);
  case transform_gizmo_mode::rotate:
    return static_cast<handle> (static_cast<uint8_t> (handle::rotate_x) + axis);
  case transform_gizmo_mode::scale:
    return static_cast<handle> (static_cast<uint8_t> (handle::scale_x) + axis);
  default:
    return handle::none;
  }
}

transform_gizmo::handle
transform_gizmo::plane_handle_of (int u, int v)
{
  if ((u == 0) && (v == 1)) {
    return handle::translate_xy;
  }
  if ((u == 1) && (v == 2)) {
    return handle::translate_yz;
  }
  if ((u == 0) && (v == 2)) {
    return handle::translate_xz;
  }
  return handle::none;
}

bool
transform_gizmo::is_axis_handle (handle value, transform_gizmo_mode mode,
                                 int axis)
{
  return (value != handle::none) && (value == axis_handle_of (mode, axis));
}

transform_gizmo::handle
transform_gizmo::pick (const transform_gizmo_input &input,
                       transform_gizmo_mode mode, const glm::vec3 &origin,
                       float length) const
{
  if (!input.pointer_over_viewport) {
    return handle::none;
  }

  glm::vec3 ray_origin;
  glm::vec3 ray_direction;
  if (!make_pointer_ray (input, ray_origin, ray_direction)) {
    return handle::none;
  }

  const glm::mat4 view_proj = input.proj * input.view;
  const glm::vec2 &pointer = input.pointer_position;
  const float world_threshold
      = pick_radius_pixels * world_per_pixel (input, origin);

  handle best = handle::none;
  float best_distance = pick_radius_pixels;

  const auto consider_screen_point
      = [&] (handle candidate, const glm::vec3 &point, float radius_pixels) {
          glm::vec2 screen;
          if (!project_screen (view_proj, point, input.viewport_min,
                               input.viewport_size, screen)) {
            return;
          }
          const float distance = glm::length (screen - pointer);
          if ((distance < radius_pixels) && (distance < best_distance)) {
            best = candidate;
            best_distance = distance;
          }
        };

  // The center handle sits on top of the axes, so it wins when the pointer
  // is close to the origin.
  if (mode == transform_gizmo_mode::scale) {
    glm::vec2 screen;
    if (project_screen (view_proj, origin, input.viewport_min,
                        input.viewport_size, screen)) {
      const float distance = glm::length (screen - pointer);
      if (distance < pick_radius_center_pixels) {
        return handle::scale_uniform;
      }
    }
  }

  if (mode == transform_gizmo_mode::translate) {
    for (int u = 0; u < 3; ++u) {
      for (int v = u + 1; v < 3; ++v) {
        const handle candidate = plane_handle_of (u, v);
        if (candidate == handle::none) {
          continue;
        }
        const glm::vec3 normal = glm::cross (world_axes[u], world_axes[v]);
        glm::vec3 hit;
        if (!intersect_plane (ray_origin, ray_direction, origin, normal, hit)) {
          continue;
        }
        const float su = glm::dot (hit - origin, world_axes[u]);
        const float sv = glm::dot (hit - origin, world_axes[v]);
        if ((su < length * plane_inner_fraction)
            || (su > length * plane_outer_fraction)
            || (sv < length * plane_inner_fraction)
            || (sv > length * plane_outer_fraction)) {
          continue;
        }
        consider_screen_point (candidate, hit, pick_radius_pixels);
      }
    }
  }

  if (mode == transform_gizmo_mode::rotate) {
    // Axis circles by ring distance, then the view-aligned outer ring.
    for (int axis = 0; axis < 3; ++axis) {
      glm::vec3 hit;
      if (!intersect_plane (ray_origin, ray_direction, origin, world_axes[axis],
                            hit)) {
        continue;
      }
      const float radial = glm::length (hit - origin);
      if (std::abs (radial - length) > world_threshold) {
        continue;
      }
      consider_screen_point (axis_handle_of (mode, axis), hit,
                             pick_radius_pixels);
    }

    const glm::vec3 to_camera = input.camera_position - origin;
    if (glm::length (to_camera) < 1.0e-5F) {
      return best;
    }
    const glm::vec3 screen_axis = glm::normalize (to_camera);
    glm::vec3 hit;
    if (intersect_plane (ray_origin, ray_direction, origin, screen_axis, hit)) {
      const float radial = glm::length (hit - origin);
      if (std::abs (radial - (length * 1.15F)) <= world_threshold) {
        consider_screen_point (handle::rotate_screen, hit, pick_radius_pixels);
      }
    }
    return best;
  }

  for (int axis = 0; axis < 3; ++axis) {
    float param = 0.0F;
    if (!closest_axis_param (ray_origin, ray_direction, origin,
                             world_axes[axis], param)) {
      continue;
    }
    param = glm::clamp (param, 0.0F, length);
    consider_screen_point (axis_handle_of (mode, axis),
                           origin + (world_axes[axis] * param),
                           pick_radius_pixels);
  }

  return best;
}

void
transform_gizmo::begin_drag (const transform_gizmo_input &input, handle picked,
                             const glm::mat4 &matrix, const glm::vec3 &origin)
{
  m_active = picked;
  m_drag_start_matrix = matrix;
  m_drag_origin = origin;
  m_drag_start_param = 0.0F;
  m_drag_start_distance = 0.0F;
  m_drag_accumulated_angle = 0.0F;
  decompose_transform (matrix, m_drag_start_position, m_drag_start_rotation,
                       m_drag_start_scale);

  glm::vec3 ray_origin;
  glm::vec3 ray_direction;
  if (!make_pointer_ray (input, ray_origin, ray_direction)) {
    return;
  }

  const int axis = axis_of (picked);
  if (axis >= 0) {
    m_drag_axis = world_axes[axis];
    float param = 0.0F;
    if (closest_axis_param (ray_origin, ray_direction, origin, m_drag_axis,
                            param)) {
      m_drag_start_param = param;
    }
  }

  int plane_u = 0;
  int plane_v = 0;
  if (plane_axes_of (picked, plane_u, plane_v)) {
    const glm::vec3 normal
        = glm::cross (world_axes[plane_u], world_axes[plane_v]);
    static_cast<void> (intersect_plane (ray_origin, ray_direction, origin,
                                        normal, m_drag_plane_point));
  }

  if ((picked == handle::rotate_x) || (picked == handle::rotate_y)
      || (picked == handle::rotate_z) || (picked == handle::rotate_screen)) {
    const glm::vec3 to_camera = input.camera_position - origin;
    if ((picked == handle::rotate_screen)
        && (glm::length (to_camera) > 1.0e-5F)) {
      m_drag_axis = glm::normalize (to_camera);
    } else if (picked == handle::rotate_screen) {
      m_drag_axis = glm::vec3 (0.0F, 0.0F, 1.0F);
    } else {
      m_drag_axis = world_axes[axis];
    }
    glm::vec3 hit;
    if (intersect_plane (ray_origin, ray_direction, origin, m_drag_axis, hit)) {
      m_drag_start_vector = glm::normalize (hit - origin);
      m_drag_last_vector = m_drag_start_vector;
    }
  }

  if (picked == handle::scale_uniform) {
    glm::vec2 screen;
    if (project_screen (input.proj * input.view, origin, input.viewport_min,
                        input.viewport_size, screen)) {
      m_drag_start_distance
          = glm::max (glm::length (screen - input.pointer_position), 1.0F);
    } else {
      m_drag_start_distance = 1.0F;
    }
  }
}

bool
transform_gizmo::apply_drag (const transform_gizmo_input &input, float length,
                             glm::mat4 &matrix)
{
  glm::vec3 ray_origin;
  glm::vec3 ray_direction;
  if (!make_pointer_ray (input, ray_origin, ray_direction)) {
    return false;
  }

  const int axis = axis_of (m_active);

  switch (m_active) {
  case handle::translate_x:
  case handle::translate_y:
  case handle::translate_z: {
    float param = 0.0F;
    if (!closest_axis_param (ray_origin, ray_direction, m_drag_origin,
                             m_drag_axis, param)) {
      return false;
    }
    matrix = m_drag_start_matrix;
    matrix[3] = glm::vec4 (m_drag_start_position
                               + (m_drag_axis * (param - m_drag_start_param)),
                           1.0F);
    return true;
  }

  case handle::translate_xy:
  case handle::translate_yz:
  case handle::translate_xz: {
    int plane_u = 0;
    int plane_v = 0;
    static_cast<void> (plane_axes_of (m_active, plane_u, plane_v));
    const glm::vec3 normal
        = glm::cross (world_axes[plane_u], world_axes[plane_v]);
    glm::vec3 hit;
    if (!intersect_plane (ray_origin, ray_direction, m_drag_origin, normal,
                          hit)) {
      return false;
    }
    matrix = m_drag_start_matrix;
    matrix[3]
        = glm::vec4 (m_drag_start_position + (hit - m_drag_plane_point), 1.0F);
    return true;
  }

  case handle::rotate_x:
  case handle::rotate_y:
  case handle::rotate_z:
  case handle::rotate_screen: {
    glm::vec3 hit;
    if (!intersect_plane (ray_origin, ray_direction, m_drag_origin, m_drag_axis,
                          hit)) {
      return false;
    }
    const glm::vec3 current = glm::normalize (hit - m_drag_origin);
    if (glm::length (m_drag_last_vector) > 1.0e-4F) {
      const float step
          = signed_angle (m_drag_last_vector, current, m_drag_axis);
      if (std::isfinite (step)) {
        m_drag_accumulated_angle += step;
      }
    }
    m_drag_last_vector = current;

    const glm::quat rotation
        = glm::angleAxis (m_drag_accumulated_angle, m_drag_axis)
          * m_drag_start_rotation;
    matrix = compose_transform (m_drag_origin, rotation, m_drag_start_scale);
    return true;
  }

  case handle::scale_x:
  case handle::scale_y:
  case handle::scale_z: {
    float param = 0.0F;
    if (!closest_axis_param (ray_origin, ray_direction, m_drag_origin,
                             m_drag_axis, param)) {
      return false;
    }
    const float factor = glm::max (
        0.001F,
        1.0F + ((param - m_drag_start_param) / glm::max (length, 1.0e-4F)));
    glm::vec3 scale = m_drag_start_scale;
    scale[axis] = m_drag_start_scale[axis] * factor;
    matrix = compose_transform (m_drag_origin, m_drag_start_rotation, scale);
    return true;
  }

  case handle::scale_uniform: {
    glm::vec2 screen;
    if (!project_screen (input.proj * input.view, m_drag_origin,
                         input.viewport_min, input.viewport_size, screen)) {
      return false;
    }
    const float distance = glm::length (screen - input.pointer_position);
    const float factor = distance / glm::max (m_drag_start_distance, 1.0F);
    matrix = compose_transform (m_drag_origin, m_drag_start_rotation,
                                m_drag_start_scale * factor);
    return true;
  }

  default:
    return false;
  }
}

bool
transform_gizmo::manipulate (const transform_gizmo_input &input,
                             transform_gizmo_mode mode, glm::mat4 &matrix)
{
  if (mode == transform_gizmo_mode::none) {
    reset ();
    m_active_mode = mode;
    return false;
  }

  // The drag survives its release frame so callers can apply the final
  // delta; it is dropped at the start of the next frame.
  if (m_release_on_next_frame) {
    m_active = handle::none;
    m_hovered = handle::none;
    m_release_on_next_frame = false;
  }

  // Mode switches invalidate in-flight drags captured for another handle
  // set.
  if ((mode != m_active_mode) && (m_active != handle::none)) {
    reset ();
  }
  m_active_mode = mode;

  const glm::vec3 origin = glm::vec3 (matrix[3]);
  const float length = gizmo_length (origin, input.camera_position);

  const bool pressed = input.primary_down && !m_pointer_was_down;
  m_pointer_was_down = input.primary_down;

  if (m_active == handle::none) {
    m_hovered = handle::none;
    if (pressed) {
      const handle picked = pick (input, mode, origin, length);
      if (picked != handle::none) {
        begin_drag (input, picked, matrix, origin);
      }
    } else if (!input.primary_down) {
      m_hovered = pick (input, mode, origin, length);
    }

    if (m_active == handle::none) {
      return false;
    }
  } else {
    m_hovered = m_active;
  }

  const bool changed = apply_drag (input, length, matrix);
  if (!input.primary_down) {
    m_release_on_next_frame = true;
  }
  return changed;
}

void
transform_gizmo::build_overlay (const glm::mat4 &matrix,
                                transform_gizmo_mode mode,
                                const glm::mat4 &view_proj,
                                const glm::vec3 &camera_position,
                                const glm::vec2 &viewport_size,
                                std::vector<overlay_vertex> &out) const
{
  if ((mode == transform_gizmo_mode::none) || (viewport_size.x <= 0.0F)
      || (viewport_size.y <= 0.0F)) {
    return;
  }

  const glm::vec3 origin = glm::vec3 (matrix[3]);
  const float length = gizmo_length (origin, camera_position);

  overlay_transform transform;
  transform.ndc_per_pixel
      = glm::vec2 (2.0F / viewport_size.x, -2.0F / viewport_size.y);
  transform.pixels_per_unit = 1.0F;
  overlay_builder builder (out, transform);

  const auto project = [&] (const glm::vec3 &point, glm::vec2 &out_local) {
    return project_local (view_proj, point, viewport_size, out_local);
  };

  const auto emit_circle = [&] (const glm::vec3 &normal, float radius,
                                const glm::vec4 &color, float width) {
    const glm::vec3 helper = (std::abs (normal.x) < 0.9F)
                                 ? glm::vec3 (1.0F, 0.0F, 0.0F)
                                 : glm::vec3 (0.0F, 1.0F, 0.0F);
    glm::vec3 axis_u = glm::cross (helper, normal);
    if (glm::length (axis_u) < 1.0e-4F) {
      return;
    }
    axis_u = glm::normalize (axis_u);
    const glm::vec3 axis_v = glm::cross (normal, axis_u);

    glm::vec2 previous;
    bool have_previous = false;
    for (int i = 0; i <= circle_segments; ++i) {
      const float angle = two_pi * static_cast<float> (i)
                          / static_cast<float> (circle_segments);
      const glm::vec3 point
          = origin
            + ((axis_u * std::cos (angle)) + (axis_v * std::sin (angle)))
                  * radius;
      glm::vec2 current;
      if (!project (point, current)) {
        have_previous = false;
        continue;
      }
      if (have_previous) {
        builder.line (previous, current, width, color);
      }
      previous = current;
      have_previous = true;
    }
  };

  for (int axis = 0; axis < 3; ++axis) {
    const glm::vec3 direction = world_axes[axis];
    const bool highlighted = is_axis_handle (m_hovered, mode, axis)
                             || is_axis_handle (m_active, mode, axis);
    const glm::vec4 color = highlighted ? highlight_color : axis_colors[axis];

    if (mode == transform_gizmo_mode::translate) {
      const glm::vec3 tip = origin + (direction * length);
      glm::vec2 origin_local;
      glm::vec2 tip_local;
      if (project (origin, origin_local) && project (tip, tip_local)) {
        builder.line (origin_local, tip_local, axis_line_width, color);
        append_arrow_head (builder, origin_local, tip_local, color);
      }
    } else if (mode == transform_gizmo_mode::rotate) {
      emit_circle (direction, length, color,
                   highlighted ? circle_line_width + 1.0F : circle_line_width);
    } else {
      const glm::vec3 tip = origin + (direction * length);
      glm::vec2 origin_local;
      glm::vec2 tip_local;
      if (project (origin, origin_local) && project (tip, tip_local)) {
        builder.line (origin_local, tip_local, axis_line_width, color);
        append_box (builder, tip_local, tip_local - origin_local,
                    box_half_pixels, color);
      }
    }
  }

  if (mode == transform_gizmo_mode::translate) {
    for (int u = 0; u < 3; ++u) {
      for (int v = u + 1; v < 3; ++v) {
        const handle plane_handle = plane_handle_of (u, v);
        if (plane_handle == handle::none) {
          continue;
        }
        const bool highlighted
            = (m_hovered == plane_handle) || (m_active == plane_handle);
        glm::vec4 color = glm::mix (axis_colors[u], axis_colors[v], 0.5F);
        color.a = highlighted ? 0.55F : 0.30F;

        const float inner = length * plane_inner_fraction;
        const float outer = length * plane_outer_fraction;
        const glm::vec3 corner_a
            = origin + (world_axes[u] * inner) + (world_axes[v] * inner);
        const glm::vec3 corner_b
            = origin + (world_axes[u] * outer) + (world_axes[v] * inner);
        const glm::vec3 corner_c
            = origin + (world_axes[u] * outer) + (world_axes[v] * outer);
        const glm::vec3 corner_d
            = origin + (world_axes[u] * inner) + (world_axes[v] * outer);

        glm::vec2 a;
        glm::vec2 b;
        glm::vec2 c;
        glm::vec2 d;
        if (!project (corner_a, a) || !project (corner_b, b)
            || !project (corner_c, c) || !project (corner_d, d)) {
          continue;
        }
        builder.triangle (a, b, c, color);
        builder.triangle (a, c, d, color);

        color.a = 1.0F;
        builder.line (a, b, 1.0F, color);
        builder.line (b, c, 1.0F, color);
        builder.line (c, d, 1.0F, color);
        builder.line (d, a, 1.0F, color);
      }
    }
  }

  if (mode == transform_gizmo_mode::rotate) {
    const glm::vec3 to_camera = camera_position - origin;
    if (glm::length (to_camera) > 1.0e-5F) {
      const bool highlighted = (m_hovered == handle::rotate_screen)
                               || (m_active == handle::rotate_screen);
      emit_circle (glm::normalize (to_camera), length * 1.15F,
                   highlighted ? highlight_color
                               : glm::vec4 (0.85F, 0.85F, 0.85F, 1.0F),
                   circle_line_width);
    }
  }

  if (mode == transform_gizmo_mode::scale) {
    glm::vec2 center;
    if (project (origin, center)) {
      const bool highlighted = (m_hovered == handle::scale_uniform)
                               || (m_active == handle::scale_uniform);
      append_box (builder, center, glm::vec2 (1.0F, 0.0F),
                  center_box_half_pixels,
                  highlighted ? highlight_color : center_color);
    }
  }
}

bool
transform_gizmo::is_using () const
{
  return m_active != handle::none;
}

bool
transform_gizmo::is_over () const
{
  return m_hovered != handle::none;
}

void
transform_gizmo::reset ()
{
  m_hovered = handle::none;
  m_active = handle::none;
  m_pointer_was_down = false;
  m_release_on_next_frame = false;
}

} // namespace gfx
} // namespace wsl
