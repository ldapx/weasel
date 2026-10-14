#pragma once

#include "wsl/comp/singl/runtime_context_fwd.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

#include "wsl/gfx/imgui_renderer_interface.hpp"
#include "wsl/gfx/render_window.hpp"
#include "wsl/gfx/render_context.hpp"
#include "wsl/gfx/view_gizmo.hpp"
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

namespace wsl::rsc
{
class resource_manager;
}

namespace editor
{

class renderer_imgui : public wsl::gfx::imgui_renderer_interface
{
public:
  renderer_imgui (wsl::gfx::render_window &window,
                  wsl::gfx::render_context *ctx);
  ~renderer_imgui () override;

  void begin_frame () override;
  void end_frame () override;

  void prepare (ImDrawData *draw_data) override;
  void render (ImDrawData *draw_data) override;
  void render_requested_previews () override;

  void request_model_preview (wsl::comp::singl::runtime_context *runtime_ctx,
                              wsl::rsc::resource_manager *resource_manager,
                              entt::id_type model_eid, uint32_t w,
                              uint32_t h) override;

  void on_resize (uint32_t w, uint32_t h) override;

  void preview_navigate (const wsl::gfx::view_gizmo_input &input,
                         const wsl::gfx::view_gizmo_layout &layout) override;
  void preview_reset_camera_to_default () override;

  SDL_GPUTexture *get_model_preview_texture () const override;

  void apply_editor_style (const wsl::gfx::editor_theme &t) override;
  const wsl::gfx::editor_theme &
  get_theme () const override
  {
    return m_theme;
  }

  wsl::gfx::imgui_fonts &
  get_fonts () override
  {
    return fonts;
  }
  const wsl::gfx::imgui_fonts &
  get_fonts () const override
  {
    return fonts;
  }

  wsl::gfx::imgui_fonts fonts;

private:
  void setup_style ();
  void create_fonts ();
  void create_device_objects ();
  void destroy_device_objects ();

  void render_model_preview (wsl::comp::singl::runtime_context &runtime_ctx,
                             wsl::rsc::resource_manager &resource_manager,
                             entt::id_type model_id);

  void destroy_preview_targets ();
  void ensure_preview_targets (uint32_t w, uint32_t h);

  wsl::gfx::render_window *m_window;
  wsl::gfx::render_context *m_ctx;

  SDL_GPUGraphicsPipeline *m_pipeline;
  SDL_GPUTexture *m_font_texture;
  SDL_GPUSampler *m_font_sampler;

  // Preview system resources
  SDL_GPUTexture *m_preview_color_tex = nullptr;
  SDL_GPUTexture *m_preview_depth_tex = nullptr;
  SDL_GPUTexture *m_preview_color_msaa = nullptr;
  SDL_GPUTexture *m_preview_depth_msaa = nullptr;
  SDL_GPUTexture *m_preview_bloom_msaa = nullptr;
  SDL_GPUTexture *m_preview_resolve = nullptr;
  SDL_GPUTexture *m_preview_bloom_resolve = nullptr;
  wsl::comp::singl::runtime_context *m_preview_runtime = nullptr;

  glm::vec3 m_preview_cam_pos{ 0.0F, 0.0F, 5.0F };
  glm::quat m_preview_cam_rot{ 1.0F, 0.0F, 0.0F, 0.0F };

  // Preview view navigation widget. Input arrives through `preview_navigate`
  // and the overlay is drawn while the preview pass is being recorded.
  wsl::gfx::view_gizmo m_preview_gizmo;
  wsl::gfx::view_gizmo_layout m_preview_gizmo_layout;
  bool m_preview_gizmo_visible = false;

  glm::vec3 m_preview_default_pos{ 0.0F, 0.0F, 5.0F };
  glm::quat m_preview_default_rot{ 1.0F, 0.0F, 0.0F, 0.0F };
  glm::vec3 m_preview_pivot{ 0.0F };

  entt::id_type m_preview_model = entt::null;
  uint32_t m_requested_w = 0;
  uint32_t m_requested_h = 0;
  uint32_t m_desired_w = 0;
  uint32_t m_desired_h = 0;
  uint32_t m_preview_w = 0;
  uint32_t m_preview_h = 0;

  bool m_preview_dirty = false;
  bool m_preview_user_camera_active = false;
  bool m_preview_rotate = true;
  double m_preview_last_gizmo_time = 0.0;
  float m_preview_yaw = 0.0F;

  float m_preview_idle_reset_seconds = 5.0F;
  float m_preview_yaw_speed = 0.5F;

  wsl::gfx::editor_theme m_theme{};

  bool
  render_model_preview_low_lod (wsl::comp::singl::runtime_context &runtime_ctx,
                                entt::id_type model_id, uint32_t w, uint32_t h);
};

} // namespace editor
