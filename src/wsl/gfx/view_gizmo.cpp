#include "view_gizmo.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat3x3.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace wsl
{
namespace gfx
{

namespace
{

/**
 * Visual constants in gizmo design pixels. The layout scale turns them into
 * viewport pixels; 256 design pixels span the rotation widget at scale 1.
 * The values match the widget this implementation replaces.
 */
constexpr float design_half_extent = 128.0F;
constexpr float design_handle_distance = 64.0F;
constexpr float design_axis_radius = 15.0F;
constexpr float design_center_radius = 80.0F;
constexpr float design_line_width = 4.0F;
constexpr float design_highlight_radius = 17.0F;
constexpr float design_highlight_width = 2.0F;
constexpr float design_tool_radius = 25.0F;
constexpr float design_tool_stroke = 2.0F;
constexpr float design_label_height = 16.0F;
constexpr float design_label_stroke = 2.5F;

/** Centers of the dolly and pan buttons relative to the widget center. */
constexpr glm::vec2 dolly_button_center{ 103.0F, 180.0F };
constexpr glm::vec2 pan_button_center{ 103.0F, 250.0F };

/** Alpha applied to axes pointing away from the camera. */
constexpr float color_fade = 0.25F;

constexpr glm::vec4 axis_colors[3] = {
  glm::vec4 (230.0F / 255.0F, 51.0F / 255.0F, 51.0F / 255.0F, 1.0F),
  glm::vec4 (51.0F / 255.0F, 230.0F / 255.0F, 51.0F / 255.0F, 1.0F),
  glm::vec4 (51.0F / 255.0F, 128.0F / 255.0F, 255.0F / 255.0F, 1.0F),
};
constexpr glm::vec4 highlight_color{ 1.0F, 1.0F, 0.0F, 1.0F };
constexpr glm::vec4 label_color{ 1.0F, 1.0F, 1.0F, 1.0F };
constexpr glm::vec4 center_color{ 1.0F, 1.0F, 1.0F, 50.0F / 255.0F };
constexpr glm::vec4 tool_color{ 144.0F / 255.0F, 144.0F / 255.0F,
                                144.0F / 255.0F, 50.0F / 255.0F };
constexpr glm::vec4 tool_hovered_color{ 215.0F / 255.0F, 215.0F / 255.0F,
                                        215.0F / 255.0F, 50.0F / 255.0F };
constexpr glm::vec4 tool_icon_color{ 215.0F / 255.0F, 215.0F / 255.0F,
                                     215.0F / 255.0F, 225.0F / 255.0F };

/** One projected axis handle of the rotation widget. */
struct gizmo_axis
{
  /** Hover id in ``0..5`` (``+X, -X, +Y, -Y, +Z, -Z``). */
  int id = 0;

  /** World axis index in ``0..2`` (``X, Y, Z``). */
  int index = 0;

  /** Camera-space depth of the handle; positive is closer to the camera. */
  float depth = 0.0F;

  /** Handle position in design pixels, y pointing down. */
  glm::vec2 handle{ 0.0F };
};

/**
 * Projects the six axis handles for a camera rotation.
 *
 * The widget mirrors what the camera sees, so a world axis projects to the
 * same side of the widget as it does in the viewport. Axes are sorted far to
 * near so later entries both draw and hit-test on top.
 */
std::array<gizmo_axis, 6>
make_axes (const glm::quat &rotation)
{
  const glm::mat3 world_to_camera = glm::transpose (glm::mat3_cast (rotation));
  const glm::vec3 world_axes[3]
      = { glm::vec3 (1.0F, 0.0F, 0.0F), glm::vec3 (0.0F, 1.0F, 0.0F),
          glm::vec3 (0.0F, 0.0F, 1.0F) };

  std::array<gizmo_axis, 6> axes{};
  for (int axis_index = 0; axis_index < 3; ++axis_index) {
    const glm::vec3 local = world_to_camera * world_axes[axis_index];
    const glm::vec2 handle (design_handle_distance * local.x,
                            -design_handle_distance * local.y);
    axes[axis_index * 2] = { axis_index * 2, axis_index, local.z, handle };
    axes[axis_index * 2 + 1]
        = { axis_index * 2 + 1, axis_index, -local.z, -handle };
  }

  std::sort (axes.begin (), axes.end (),
             [] (const gizmo_axis &lhs, const gizmo_axis &rhs) {
               return lhs.depth < rhs.depth;
             });
  return axes;
}

/**
 * Normalizes *value*, falling back to *fallback* when it is degenerate.
 */
glm::vec3
safe_normalize (const glm::vec3 &value, const glm::vec3 &fallback)
{
  const float length_sq = glm::dot (value, value);
  if (length_sq < 1.0e-8F) {
    return fallback;
  }
  return value / std::sqrt (length_sq);
}

/** Converts an NDC point to design pixels relative to the widget center. */
glm::vec2
to_design (const glm::vec2 &point, const view_gizmo_layout &layout)
{
  return (point - layout.center) / layout.ndc_per_pixel / layout.scale;
}

} // namespace

view_gizmo_layout
view_gizmo_layout::from_pixel_center (const glm::vec2 &center_pixel,
                                      const glm::vec2 &viewport_size,
                                      float scale)
{
  view_gizmo_layout layout;
  if ((viewport_size.x <= 0.0F) || (viewport_size.y <= 0.0F)) {
    return layout;
  }

  layout.center = view_gizmo_ndc_from_pixel (center_pixel, viewport_size);
  layout.ndc_per_pixel
      = glm::vec2 (2.0F / viewport_size.x, -2.0F / viewport_size.y);
  layout.scale = scale;
  return layout;
}

glm::vec2
view_gizmo_ndc_from_pixel (const glm::vec2 &pixel,
                           const glm::vec2 &viewport_size)
{
  if ((viewport_size.x <= 0.0F) || (viewport_size.y <= 0.0F)) {
    return glm::vec2 (0.0F);
  }
  return glm::vec2 ((2.0F * pixel.x / viewport_size.x) - 1.0F,
                    1.0F - (2.0F * pixel.y / viewport_size.y));
}

glm::vec2
view_gizmo_ndc_delta_from_pixel (const glm::vec2 &delta,
                                 const glm::vec2 &viewport_size)
{
  if ((viewport_size.x <= 0.0F) || (viewport_size.y <= 0.0F)) {
    return glm::vec2 (0.0F);
  }
  return glm::vec2 (2.0F * delta.x / viewport_size.x,
                    -2.0F * delta.y / viewport_size.y);
}

bool
view_gizmo::interact (const view_gizmo_input &input,
                      const view_gizmo_layout &layout,
                      view_gizmo_camera &camera)
{
  const bool layout_valid = (layout.ndc_per_pixel.x != 0.0F)
                            && (layout.ndc_per_pixel.y != 0.0F)
                            && (layout.scale > 0.0F);
  if (!layout_valid) {
    return false;
  }

  bool modified = false;

  // An in-flight snap animation keeps owning the camera until it reaches the
  // recorded target pose.
  if (m_animating) {
    m_animation_time += input.delta_time;
    const float raw
        = std::min (1.0F, m_animation_time / snap_animation_duration);
    const float t = 1.0F - ((1.0F - raw) * (1.0F - raw)); // ease-out quad

    const glm::vec3 current_direction = safe_normalize (
        glm::mix (m_animation_start_direction, m_animation_target_direction, t),
        m_animation_target_direction);
    const float current_distance
        = glm::mix (m_animation_start_distance, m_animation_target_distance, t);
    const glm::vec3 current_up = safe_normalize (
        glm::mix (m_animation_start_up, m_animation_target_up, t),
        m_animation_target_up);

    camera.position = camera.pivot + (current_direction * current_distance);
    camera.rotation = glm::quatLookAt (-current_direction, current_up);
    modified = true;

    if (raw >= 1.0F) {
      camera.position = m_animation_target_position;
      camera.rotation = glm::quatLookAt (-m_animation_target_direction,
                                         m_animation_target_up);
      m_animating = false;
    }
  }

  const bool can_interact = input.pointer_over_viewport;
  const glm::vec2 pointer = can_interact
                                ? to_design (input.pointer_position, layout)
                                : glm::vec2 (0.0F);

  // Hover state is rebuilt from scratch every frame.
  m_hovered_axis = axis_none;
  m_dolly_hovered = false;
  m_pan_hovered = false;

  if (can_interact && (m_active_tool == tool::none) && !m_animating) {
    const float interaction_radius = design_half_extent + design_axis_radius;
    if (glm::dot (pointer, pointer) < interaction_radius * interaction_radius) {
      const std::array<gizmo_axis, 6> axes = make_axes (camera.rotation);
      const float radius_sq = design_axis_radius * design_axis_radius;
      for (const gizmo_axis &axis : axes) {
        if (axis.depth < -0.1F) {
          continue;
        }
        const glm::vec2 offset = axis.handle - pointer;
        if (glm::dot (offset, offset) < radius_sq) {
          m_hovered_axis = axis.id;
        }
      }

      if ((m_hovered_axis == axis_none)
          && (glm::dot (pointer, pointer)
              < design_center_radius * design_center_radius)) {
        m_hovered_axis = axis_center;
      }
    }
  }

  if (can_interact
      && ((m_active_tool == tool::none) || (m_active_tool == tool::dolly))) {
    const glm::vec2 offset = dolly_button_center - pointer;
    if (glm::dot (offset, offset) < design_tool_radius * design_tool_radius) {
      m_dolly_hovered = true;
    }
  }

  if (can_interact
      && ((m_active_tool == tool::none) || (m_active_tool == tool::pan))) {
    const glm::vec2 offset = pan_button_center - pointer;
    if (glm::dot (offset, offset) < design_tool_radius * design_tool_radius) {
      m_pan_hovered = true;
    }
  }

  // Drag start.
  if (can_interact && input.primary_down && (m_active_tool == tool::none)) {
    if (m_hovered_axis == axis_center) {
      m_active_tool = tool::rotate;
      m_animating = false;
    } else if (m_dolly_hovered) {
      m_active_tool = tool::dolly;
      m_animating = false;
    } else if (m_pan_hovered) {
      m_active_tool = tool::pan;
      m_animating = false;
    }
  }

  // Camera movement from the active drag. Deltas are converted back to
  // viewport pixels so the sensitivity is independent of the widget scale.
  const glm::vec2 pixel_delta = input.pointer_delta / layout.ndc_per_pixel;

  if (m_active_tool == tool::rotate) {
    constexpr float rotation_speed = 0.01F;
    const glm::vec3 up (0.0F, -1.0F, 0.0F);
    const float yaw_angle = -pixel_delta.x * rotation_speed;
    const float pitch_angle = -pixel_delta.y * rotation_speed;

    const glm::quat yaw_rotation = glm::angleAxis (yaw_angle, up);
    const glm::vec3 right_axis = camera.rotation * glm::vec3 (1.0F, 0.0F, 0.0F);
    const glm::quat pitch_rotation = glm::angleAxis (pitch_angle, right_axis);
    const glm::quat total_rotation = yaw_rotation * pitch_rotation;

    camera.position
        = camera.pivot + (total_rotation * (camera.position - camera.pivot));
    camera.rotation = total_rotation * camera.rotation;
    modified = true;
  }

  if ((m_active_tool == tool::dolly) && (pixel_delta.y != 0.0F)) {
    constexpr float zoom_speed = 0.05F;
    const glm::vec3 backward = camera.rotation * glm::vec3 (0.0F, 0.0F, 1.0F);
    camera.position += backward * (-pixel_delta.y * zoom_speed);
    modified = true;
  }

  if ((m_active_tool == tool::pan)
      && ((pixel_delta.x != 0.0F) || (pixel_delta.y != 0.0F))) {
    constexpr float pan_speed = 0.01F;
    const glm::vec3 down (0.0F, -1.0F, 0.0F);
    const glm::vec3 right_movement
        = (camera.rotation * glm::vec3 (1.0F, 0.0F, 0.0F))
          * (-pixel_delta.x * pan_speed);
    const glm::vec3 down_movement
        = (camera.rotation * down) * (pixel_delta.y * pan_speed);
    camera.position += right_movement + down_movement;
    modified = true;
  }

  // Releasing over an axis handle snaps the camera to that axis, keeping the
  // current orbit distance and looking back at the pivot.
  if (can_interact && input.primary_released && (m_hovered_axis >= 0)
      && (m_hovered_axis <= axis_negative_z) && (m_active_tool == tool::none)) {
    const int axis_index = m_hovered_axis / 2;
    const float sign = ((m_hovered_axis % 2) == 0) ? 1.0F : -1.0F;
    const glm::vec3 world_axes[3]
        = { glm::vec3 (1.0F, 0.0F, 0.0F), glm::vec3 (0.0F, 1.0F, 0.0F),
            glm::vec3 (0.0F, 0.0F, 1.0F) };
    const glm::vec3 target_direction = world_axes[axis_index] * sign;

    const float current_distance = glm::length (camera.position - camera.pivot);
    const glm::vec3 target_position
        = camera.pivot + (target_direction * current_distance);

    const glm::vec3 target_forward = -glm::normalize (target_direction);
    glm::vec3 target_up (0.0F, 1.0F, 0.0F);
    if (std::abs (glm::dot (target_forward, target_up)) > 0.999F) {
      target_up = (target_forward.y > 0.0F) ? glm::vec3 (0.0F, 0.0F, 1.0F)
                                            : glm::vec3 (0.0F, 0.0F, -1.0F);
    }
    const glm::quat target_rotation
        = glm::quatLookAt (target_forward, target_up);

    const glm::vec3 position_offset = camera.position - target_position;
    const bool position_differs
        = glm::dot (position_offset, position_offset) > 0.0001F;
    const bool rotation_differs
        = (1.0F - std::abs (glm::dot (camera.rotation, target_rotation)))
          > 0.0001F;

    if (position_differs || rotation_differs) {
      m_animating = true;
      m_animation_time = 0.0F;
      m_animation_target_position = target_position;
      m_animation_start_up = camera.rotation * glm::vec3 (0.0F, 1.0F, 0.0F);
      m_animation_target_up = target_up;
      m_animation_start_distance = glm::length (camera.position - camera.pivot);
      m_animation_target_distance
          = glm::length (target_position - camera.pivot);
      m_animation_start_direction
          = (m_animation_start_distance > 0.0001F)
                ? (camera.position - camera.pivot) / m_animation_start_distance
                : glm::vec3 (0.0F, 0.0F, 1.0F);
      m_animation_target_direction
          = (m_animation_target_distance > 0.0001F)
                ? (target_position - camera.pivot) / m_animation_target_distance
                : glm::vec3 (0.0F, 0.0F, 1.0F);
    }
  }

  // Safety release when the button went up outside this widget's viewport.
  if (!input.primary_down && (m_active_tool != tool::none)) {
    m_active_tool = tool::none;
  }

  return modified;
}

void
view_gizmo::build_overlay (const view_gizmo_camera &camera,
                           const view_gizmo_layout &layout,
                           std::vector<overlay_vertex> &out,
                           const view_gizmo_build_options &options) const
{
  overlay_transform transform;
  transform.origin = layout.center;
  transform.ndc_per_pixel = layout.ndc_per_pixel;
  transform.pixels_per_unit = layout.scale;

  overlay_builder writer (out, transform, options.antialias_feather);
  const std::array<gizmo_axis, 6> axes = make_axes (camera.rotation);

  if ((m_hovered_axis == axis_center) || (m_active_tool == tool::rotate)) {
    writer.disc (glm::vec2 (0.0F), design_center_radius, center_color);
  }

  const char labels[3] = { 'X', 'Y', 'Z' };
  for (const gizmo_axis &axis : axes) {
    const float fade = glm::mix (color_fade, 1.0F, (axis.depth + 1.0F) * 0.5F);
    glm::vec4 color = axis_colors[axis.index];
    color.a *= fade;

    const float handle_length = glm::length (axis.handle);
    if (handle_length > 1.0e-5F) {
      const glm::vec2 direction = axis.handle / handle_length;
      writer.line (glm::vec2 (0.0F),
                   axis.handle - (direction * design_axis_radius),
                   design_line_width, color);
    }
    writer.disc (axis.handle, design_axis_radius, color);

    if (m_hovered_axis == axis.id) {
      writer.ring (axis.handle, design_highlight_radius, design_highlight_width,
                   highlight_color);
    }

    const float label_fade
        = glm::clamp (1.0F + (axis.depth * 2.5F), 0.0F, 1.0F);
    if (label_fade > 0.01F) {
      glm::vec4 label = label_color;
      label.a *= label_fade;
      writer.glyph (labels[axis.index], axis.handle, design_label_height,
                    design_label_stroke, label);
    }
  }

  // Dolly button: magnifying glass with a plus sign.
  const bool dolly_active = (m_active_tool == tool::dolly);
  writer.disc (dolly_button_center, design_tool_radius,
               (m_dolly_hovered || dolly_active) ? tool_hovered_color
                                                 : tool_color);
  {
    const glm::vec2 glass_center = dolly_button_center - glm::vec2 (1.0F, 1.0F);
    const float glass_radius = 10.5F;
    writer.ring (glass_center, glass_radius, design_tool_stroke,
                 tool_icon_color);
    writer.line (dolly_button_center + glm::vec2 (6.25F, 6.25F),
                 dolly_button_center + glm::vec2 (10.5F, 10.5F),
                 design_tool_stroke, tool_icon_color);

    const float plus_arm = glass_radius * 0.5F;
    writer.line (glass_center - glm::vec2 (0.0F, plus_arm),
                 glass_center + glm::vec2 (0.0F, plus_arm), design_tool_stroke,
                 tool_icon_color);
    writer.line (glass_center - glm::vec2 (plus_arm, 0.0F),
                 glass_center + glm::vec2 (plus_arm, 0.0F), design_tool_stroke,
                 tool_icon_color);
  }

  // Pan button: four inward chevrons.
  const bool pan_active = (m_active_tool == tool::pan);
  writer.disc (pan_button_center, design_tool_radius,
               (m_pan_hovered || pan_active) ? tool_hovered_color : tool_color);
  {
    const glm::vec2 center = pan_button_center;
    const float size = 12.5F;
    const float arm = size * 0.25F;

    const glm::vec2 top = center + glm::vec2 (0.0F, -size);
    writer.line (top + glm::vec2 (-arm, arm), top, design_tool_stroke,
                 tool_icon_color);
    writer.line (top + glm::vec2 (arm, arm), top, design_tool_stroke,
                 tool_icon_color);

    const glm::vec2 bottom = center + glm::vec2 (0.0F, size);
    writer.line (bottom + glm::vec2 (-arm, -arm), bottom, design_tool_stroke,
                 tool_icon_color);
    writer.line (bottom + glm::vec2 (arm, -arm), bottom, design_tool_stroke,
                 tool_icon_color);

    const glm::vec2 left = center + glm::vec2 (-size, 0.0F);
    writer.line (left + glm::vec2 (arm, -arm), left, design_tool_stroke,
                 tool_icon_color);
    writer.line (left + glm::vec2 (arm, arm), left, design_tool_stroke,
                 tool_icon_color);

    const glm::vec2 right = center + glm::vec2 (size, 0.0F);
    writer.line (right + glm::vec2 (-arm, -arm), right, design_tool_stroke,
                 tool_icon_color);
    writer.line (right + glm::vec2 (-arm, arm), right, design_tool_stroke,
                 tool_icon_color);
  }
}

bool
view_gizmo::is_active () const
{
  return m_active_tool != tool::none;
}

bool
view_gizmo::is_hovered () const
{
  return (m_hovered_axis != axis_none) || m_dolly_hovered || m_pan_hovered;
}

bool
view_gizmo::is_animating () const
{
  return m_animating;
}

} // namespace gfx
} // namespace wsl
