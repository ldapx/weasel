#pragma once

#include "wsl/debug/debug_renderer.hpp"
#include "wsl/gfx/render_context.hpp"
#include "wsl/gfx/render_window.hpp"
#include "wsl/phys/physics_engine.hpp"

#include <memory>
#include <vector>

namespace wsl
{
namespace debug
{

/**
 * Debug renderer that batches world-space lines and submits them to the
 * scene renderer's debug line pass.
 *
 * Physics uses it to visualize colliders: Box3D emits wireframes through
 * `phys::engine::draw_debug`, `draw_physics_debug` feeds them in during the
 * build pass, and `end_frame` draws them while the 3D pass is open so they
 * depth-test against the scene.
 */
class physics_debug_renderer final : public debug_renderer_interface
{
public:
  physics_debug_renderer (wsl::gfx::render_window &, wsl::gfx::render_context *)
  {
  }

  void begin_frame () override;
  void draw_segment (const glm::vec3 &begin, const glm::vec3 &end,
                     const glm::vec4 &color) override;
  void end_frame (wsl::gfx::scene_renderer &renderer,
                  const glm::mat4 &view_proj) override;

private:
  struct line
  {
    glm::vec3 begin{};
    glm::vec3 end{};
    glm::vec4 color{};
  };

  std::vector<line> m_lines;
};

inline std::unique_ptr<debug_renderer_interface>
make_physics_debug_renderer (wsl::gfx::render_window &window,
                             wsl::gfx::render_context *ctx)
{
  return std::make_unique<physics_debug_renderer> (window, ctx);
}

/**
 * Appends every collider wireframe and contact line of `engine` to
 * `renderer`.
 *
 * :param engine: Physics engine to visualize.
 * :param renderer: Debug renderer collecting this frame's lines.
 */
void draw_physics_debug (wsl::phys::engine &engine,
                         debug_renderer_interface &renderer);

} // namespace debug
} // namespace wsl
