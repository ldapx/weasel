#include "debug_overlay.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>

namespace wsl
{
namespace gfx
{

namespace
{

/** Segment count used for discs, rings, and their antialiasing fringes. */
constexpr int disc_segments = 32;
constexpr float pi = 3.14159265358979323846F;
constexpr float two_pi = 2.0F * pi;

/** Returns the unit direction of an angle around a circle. */
glm::vec2
circle_direction (float angle)
{
  return glm::vec2 (std::cos (angle), std::sin (angle));
}

} // namespace

glm::vec2
overlay_transform::to_ndc (const glm::vec2 &local) const
{
  return origin + (local * (pixels_per_unit * ndc_per_pixel));
}

glm::vec2
overlay_transform::to_local (const glm::vec2 &ndc) const
{
  return (ndc - origin) / (pixels_per_unit * ndc_per_pixel);
}

overlay_builder::overlay_builder (std::vector<overlay_vertex> &out,
                                  const overlay_transform &transform,
                                  float antialias_feather)
    : m_out (out), m_transform (transform),
      m_antialias_feather (antialias_feather)
{
}

void
overlay_builder::triangle (const glm::vec2 &a, const glm::vec2 &b,
                           const glm::vec2 &c, const glm::vec4 &color)
{
  triangle_colored (a, color, b, color, c, color);
}

void
overlay_builder::triangle_colored (const glm::vec2 &a, const glm::vec4 &color_a,
                                   const glm::vec2 &b, const glm::vec4 &color_b,
                                   const glm::vec2 &c, const glm::vec4 &color_c)
{
  m_out.push_back ({ glm::vec3 (m_transform.to_ndc (a), 0.0F), color_a });
  m_out.push_back ({ glm::vec3 (m_transform.to_ndc (b), 0.0F), color_b });
  m_out.push_back ({ glm::vec3 (m_transform.to_ndc (c), 0.0F), color_c });
}

float
overlay_builder::feather () const
{
  if ((m_antialias_feather <= 0.0F) || (m_transform.pixels_per_unit <= 0.0F)) {
    return 0.0F;
  }
  return m_antialias_feather / m_transform.pixels_per_unit;
}

void
overlay_builder::line (const glm::vec2 &a, const glm::vec2 &b, float width,
                       const glm::vec4 &color)
{
  const glm::vec2 delta = b - a;
  const float length = glm::length (delta);
  if ((length < 1.0e-5F) || (width <= 0.0F)) {
    return;
  }

  const glm::vec2 unit = delta / length;
  const glm::vec2 normal (-unit.y, unit.x);
  const float half = width * 0.5F;
  const float fringe = feather ();
  const glm::vec2 inner = normal * half;

  if (fringe <= 0.0F) {
    triangle (a + inner, a - inner, b - inner, color);
    triangle (a + inner, b - inner, b + inner, color);
    return;
  }

  const glm::vec4 transparent (color.r, color.g, color.b, 0.0F);
  const glm::vec2 outer = normal * (half + fringe);

  // Fringe on the +normal side, the core, and the fringe on the -normal
  // side. Alpha ramps across the fringes feather the line's long edges.
  triangle_colored (a + outer, transparent, a + inner, color, b + inner, color);
  triangle_colored (a + outer, transparent, b + inner, color, b + outer,
                    transparent);
  triangle_colored (a + inner, color, a - inner, color, b - inner, color);
  triangle_colored (a + inner, color, b - inner, color, b + inner, color);
  triangle_colored (a - inner, color, a - outer, transparent, b - outer,
                    transparent);
  triangle_colored (a - inner, color, b - outer, transparent, b - inner, color);
}

void
overlay_builder::disc (const glm::vec2 &center, float radius,
                       const glm::vec4 &color)
{
  if (radius <= 0.0F) {
    return;
  }

  const float fringe = feather ();
  const float core_radius = radius - fringe;

  if (core_radius > 0.0F) {
    for (int i = 0; i < disc_segments; ++i) {
      const float angle_a = two_pi * static_cast<float> (i)
                            / static_cast<float> (disc_segments);
      const float angle_b = two_pi * static_cast<float> (i + 1)
                            / static_cast<float> (disc_segments);
      triangle (center, center + (circle_direction (angle_a) * core_radius),
                center + (circle_direction (angle_b) * core_radius), color);
    }
  }

  if (fringe > 0.0F) {
    const glm::vec4 transparent (color.r, color.g, color.b, 0.0F);
    annulus (center, std::max (core_radius, 0.0F), radius, color, transparent,
             disc_segments);
  }
}

void
overlay_builder::ring (const glm::vec2 &center, float radius, float thickness,
                       const glm::vec4 &color)
{
  const float fringe = feather ();
  const float inner_radius = radius - (thickness * 0.5F);
  const float outer_radius = radius + (thickness * 0.5F);

  if (fringe <= 0.0F) {
    annulus (center, std::max (inner_radius, 0.0F), outer_radius, color, color,
             disc_segments);
    return;
  }

  const glm::vec4 transparent (color.r, color.g, color.b, 0.0F);
  annulus (center, std::max (inner_radius - fringe, 0.0F), inner_radius,
           transparent, color, disc_segments);
  annulus (center, inner_radius, outer_radius, color, color, disc_segments);
  annulus (center, outer_radius, outer_radius + fringe, color, transparent,
           disc_segments);
}

void
overlay_builder::glyph (char symbol, const glm::vec2 &center, float height,
                        float stroke, const glm::vec4 &color)
{
  const float half = height * 0.5F;
  switch (symbol) {
  case 'X':
    line (center + glm::vec2 (-half, -half), center + glm::vec2 (half, half),
          stroke, color);
    line (center + glm::vec2 (half, -half), center + glm::vec2 (-half, half),
          stroke, color);
    break;
  case 'Y':
    line (center + glm::vec2 (-half, -half), center, stroke, color);
    line (center + glm::vec2 (half, -half), center, stroke, color);
    line (center, center + glm::vec2 (0.0F, half), stroke, color);
    break;
  case 'Z':
    line (center + glm::vec2 (-half, -half), center + glm::vec2 (half, -half),
          stroke, color);
    line (center + glm::vec2 (half, -half), center + glm::vec2 (-half, half),
          stroke, color);
    line (center + glm::vec2 (-half, half), center + glm::vec2 (half, half),
          stroke, color);
    break;
  default:
    break;
  }
}

void
overlay_builder::annulus (const glm::vec2 &center, float inner_radius,
                          float outer_radius, const glm::vec4 &inner_color,
                          const glm::vec4 &outer_color, int segments)
{
  if (outer_radius <= 0.0F) {
    return;
  }

  for (int i = 0; i < segments; ++i) {
    const float angle_a
        = two_pi * static_cast<float> (i) / static_cast<float> (segments);
    const float angle_b
        = two_pi * static_cast<float> (i + 1) / static_cast<float> (segments);
    const glm::vec2 direction_a = circle_direction (angle_a);
    const glm::vec2 direction_b = circle_direction (angle_b);

    const glm::vec2 inner_a = center + (direction_a * inner_radius);
    const glm::vec2 outer_a = center + (direction_a * outer_radius);
    const glm::vec2 outer_b = center + (direction_b * outer_radius);
    const glm::vec2 inner_b = center + (direction_b * inner_radius);

    triangle_colored (inner_a, inner_color, outer_a, outer_color, outer_b,
                      outer_color);
    triangle_colored (inner_a, inner_color, outer_b, outer_color, inner_b,
                      inner_color);
  }
}

} // namespace gfx
} // namespace wsl
