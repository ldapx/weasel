#include "physics_debug_drawer.hpp"

#include "wsl/gfx/scene_renderer.hpp"
#include "wsl/phys/physics_engine.hpp"

namespace wsl
{
namespace debug
{

namespace
{

glm::vec4
to_color (std::uint32_t rgba)
{
  constexpr float scale = 1.0F / 255.0F;
  return { static_cast<float> ((rgba >> 16U) & 0xFFU) * scale,
           static_cast<float> ((rgba >> 8U) & 0xFFU) * scale,
           static_cast<float> (rgba & 0xFFU) * scale, 1.0F };
}

} // namespace

void
physics_debug_renderer::begin_frame ()
{
  m_lines.clear ();
}

void
physics_debug_renderer::draw_segment (const glm::vec3 &begin,
                                      const glm::vec3 &end,
                                      const glm::vec4 &color)
{
  m_lines.push_back (line{ begin, end, color });
}

void
physics_debug_renderer::end_frame (wsl::gfx::scene_renderer &renderer,
                                   const glm::mat4 &view_proj)
{
  if (m_lines.empty ()) {
    return;
  }

  std::vector<wsl::gfx::scene_renderer::debug_vertex> vertices;
  vertices.reserve (m_lines.size () * 2U);
  for (const line &segment : m_lines) {
    vertices.push_back ({ segment.begin, segment.color });
    vertices.push_back ({ segment.end, segment.color });
  }

  renderer.draw_debug_lines (vertices, view_proj);
}

void
draw_physics_debug (wsl::phys::engine &engine,
                    wsl::debug::debug_renderer_interface &renderer)
{
  std::vector<wsl::phys::debug_line> lines;
  engine.draw_debug (lines);

  for (const wsl::phys::debug_line &line : lines) {
    renderer.draw_segment ({ line.a.x, line.a.y, line.a.z },
                           { line.b.x, line.b.y, line.b.z },
                           to_color (line.rgba));
  }
}

} // namespace debug
} // namespace wsl
