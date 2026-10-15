// ImGui-specific parts of comp::singl::editor_context.
//
// The core class stays toolkit-neutral (see wsl/comp/singl/editor_context.hpp):
// the ImGui renderer slot is stored type-erased as shared_ptr<void> and the
// typed accessors, plus the ImGui inspector widget, are defined here.

#include "editor_support/imgui_renderer_interface.hpp"

#include "wsl/comp/singl/editor_context.hpp"
#include "wsl/comp/singl/runtime_context.hpp"

#include <glm/gtc/quaternion.hpp>
#include <imgui.h>
#include <memory>

namespace wsl::comp::singl
{

wsl::gfx::imgui_renderer_interface *
editor_context::get_imgui_renderer () const
{
  return static_cast<wsl::gfx::imgui_renderer_interface *> (
      m_imgui_renderer.get ());
}

void
editor_context::set_imgui_renderer (
    std::unique_ptr<wsl::gfx::imgui_renderer_interface> ptr)
{
  if (ptr != nullptr) {
    m_imgui_renderer = std::shared_ptr<void> (ptr.release (), [] (void *raw) {
      delete static_cast<wsl::gfx::imgui_renderer_interface *> (raw);
    });
  } else {
    m_imgui_renderer.reset ();
  }
}

bool
editor_context::custom_inspect (
    const char *label, wsl::comp::singl::runtime_context *runtime_ctx_ptr)
{
  (void)label;
  (void)runtime_ctx_ptr;

  ImGui::Separator ();
  ImGui::Value ("Game Fullscreen", m_game_fullscreen);
  ImGui::Value ("Pending Project Load",
                m_pending_project_load.value_or ("None").c_str () != nullptr);
  ImGui::Value ("Selected Entity", (uint32_t)m_selected_entity);

  if (ImGui::TreeNode ("Editor 3D Camera")) {
    ImGui::DragFloat3 ("Position", &m_editor_cam_pos.x, 0.1F);

    glm::vec3 euler = glm::degrees (glm::eulerAngles (m_editor_cam_rot));
    if (ImGui::DragFloat3 ("Rotation (Euler)", &euler.x, 0.1F)) {
      m_editor_cam_rot = glm::quat (glm::radians (euler));
    }

    ImGui::DragFloat ("FOV", &m_editor_camera.fov (), 1.0F, 10.0F, 120.0F);
    ImGui::DragFloat ("Near", &m_editor_camera.near (), 0.01F, 0.01F, 10.0F);
    ImGui::DragFloat ("Far", &m_editor_camera.far (), 10.0F, 10.0F, 10000.0F);

    if (ImGui::Button ("Reset 3D Camera")) {
      reset_editor_camera ();
    }

    ImGui::TreePop ();
  }

  if (ImGui::TreeNode ("Editor 2D Camera")) {
    ImGui::DragFloat2 ("Position", &m_editor_cam_2d_pos.x, 0.1F);
    ImGui::DragFloat ("Zoom", &m_editor_camera_2d.zoom, 0.01F, 0.01F, 100.0F);

    ImGui::TreePop ();
  }

  return false;
}

} // namespace wsl::comp::singl
