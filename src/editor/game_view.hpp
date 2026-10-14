#pragma once

#include "wsl/comp/singl/runtime_context_fwd.hpp"
#include <SDL3/SDL_gpu.h>
#include <imgui.h>

#include "wsl/gfx/render_window.hpp"
#include "wsl/gfx/transform_gizmo.hpp"
#include "inspector.hpp"
#include "ecs_inspector_utils.hpp"
#include "wsl/gfx/mesh.hpp"
#include <entt/entt.hpp>

namespace editor
{

class game_view
{
public:
  game_view (wsl::comp::singl::runtime_context *runtime_ctx,
             wsl::comp::singl::editor_context *editor_ctx);

  void set_render_texture (const wsl::gfx::texture &texture);

  void draw (entt::registry &registry, wsl::gfx::render_window &rw);
  void draw_camera_header (entt::registry &registry,
                           wsl::gfx::render_window &rw);

  void set_selection (ecs_selection *sel);

  bool visible = true;

private:
  wsl::comp::singl::runtime_context *m_runtime_ctx;
  wsl::comp::singl::editor_context *m_editor_ctx;

  ecs_selection *m_selection = nullptr;

  SDL_GPUTexture *m_render_texture = nullptr;
  int m_tex_width = 0;
  int m_tex_height = 0;

  glm::vec3 m_orbit_pivot = glm::vec3 (0.0F);

  float m_toolbar_height = -1.0F;
  bool m_show_grid = true;
  wsl::gfx::transform_gizmo_mode m_current_op
      = wsl::gfx::transform_gizmo_mode::translate;
};

} // namespace editor
