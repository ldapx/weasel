#pragma once

#ifndef IN_MODULE_INTERFACE
#include <vector>
#endif
#ifndef IN_MODULE_INTERFACE
#include <glm/vec2.hpp>
#endif
#ifndef IN_MODULE_INTERFACE
#include <glm/vec3.hpp>
#endif
#ifndef IN_MODULE_INTERFACE
#include <glm/vec4.hpp>
#endif

namespace wsl
{
namespace gfx
{

/**
 * Screen-space vertex consumed by the scene renderer's debug overlay pass.
 *
 * Positions are in normalized device coordinates (x and y within ``-1..1``,
 * y pointing up). The z component is currently unused and must be zero.
 */
struct overlay_vertex
{
  /** Position in normalized device coordinates. */
  glm::vec3 position;

  /** RGBA color; alpha selects blending. */
  glm::vec4 color;
};

/**
 * Maps local overlay coordinates to normalized device coordinates.
 *
 * Local coordinates are measured in local units; ``pixels_per_unit`` converts
 * them to viewport pixels. A local point ``p`` maps to
 * ``origin + (p * pixels_per_unit * ndc_per_pixel)``.
 */
struct overlay_transform
{
  /** NDC position of the local origin. */
  glm::vec2 origin{ 0.0F };

  /** NDC units per viewport pixel along each axis (``y`` is negative). */
  glm::vec2 ndc_per_pixel{ 0.0F };

  /** Viewport pixels per local unit. */
  float pixels_per_unit = 1.0F;

  /** Converts a local point to normalized device coordinates. */
  glm::vec2 to_ndc (const glm::vec2 &local) const;

  /** Converts a normalized device coordinate point to local units. */
  glm::vec2 to_local (const glm::vec2 &ndc) const;
};

/**
 * Appends antialiased overlay primitives to a vertex list.
 *
 * Coordinates are in local units of the attached
 * :cpp:struct:`overlay_transform`. Edges receive an alpha fringe whose width
 * is measured in viewport pixels, approximating the antialiasing a
 * multisampled target would provide.
 */
class overlay_builder
{
public:
  /**
   * :param out: Vertex list that receives the generated triangles.
   * :param transform: Mapping from local units to the render target.
   * :param antialias_feather: Fringe width in viewport pixels; zero emits
   *   hard edges, which is preferable when the target is already multisampled.
   */
  overlay_builder (std::vector<overlay_vertex> &out,
                   const overlay_transform &transform,
                   float antialias_feather = 1.0F);

  /** Appends a single-color triangle. */
  void triangle (const glm::vec2 &a, const glm::vec2 &b, const glm::vec2 &c,
                 const glm::vec4 &color);

  /** Appends a thick line segment with antialiased sides. */
  void line (const glm::vec2 &a, const glm::vec2 &b, float width,
             const glm::vec4 &color);

  /** Appends a filled, antialiased circle. */
  void disc (const glm::vec2 &center, float radius, const glm::vec4 &color);

  /** Appends a stroked, antialiased circle. */
  void ring (const glm::vec2 &center, float radius, float thickness,
             const glm::vec4 &color);

  /** Appends an axis label glyph drawn as antialiased line segments. */
  void glyph (char symbol, const glm::vec2 &center, float height, float stroke,
              const glm::vec4 &color);

private:
  /** Appends a triangle with per-vertex colors. */
  void triangle_colored (const glm::vec2 &a, const glm::vec4 &color_a,
                         const glm::vec2 &b, const glm::vec4 &color_b,
                         const glm::vec2 &c, const glm::vec4 &color_c);

  /** Appends an annulus with separate inner and outer rim colors. */
  void annulus (const glm::vec2 &center, float inner_radius, float outer_radius,
                const glm::vec4 &inner_color, const glm::vec4 &outer_color,
                int segments);

  /** Returns the fringe width in local units. */
  [[nodiscard]] float feather () const;

  std::vector<overlay_vertex> &m_out;
  overlay_transform m_transform;
  float m_antialias_feather = 1.0F;
};

} // namespace gfx
} // namespace wsl
