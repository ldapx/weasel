#pragma once

#ifndef IN_MODULE_INTERFACE
#include <SDL3/SDL_gpu.h>
#endif
#ifndef IN_MODULE_INTERFACE
#include <entt/entt.hpp>
#endif
#ifndef IN_MODULE_INTERFACE
#include <glm/glm.hpp>
#endif
#ifndef IN_MODULE_INTERFACE
#include <glm/gtc/quaternion.hpp>
#endif
#ifndef IN_MODULE_INTERFACE
#include <imgui.h>
#endif

#include "wsl/gfx/view_gizmo.hpp"

namespace wsl::comp::singl
{
class runtime_context;
}
namespace wsl::rsc
{
class resource_manager;
}

namespace wsl::gfx
{

struct imgui_fonts
{
  ImFont *regular = nullptr;
  ImFont *light = nullptr;
  ImFont *medium = nullptr;
  ImFont *semibold = nullptr;
  ImFont *bold = nullptr;
  ImFont *title = nullptr;
  ImFont *mono = nullptr;
};

struct editor_theme
{
  ImVec4 primary;
  ImVec4 secondary;
  ImVec4 background1;
  ImVec4 background2;
  ImVec4 foreground;
};

class imgui_renderer_interface
{
public:
  virtual ~imgui_renderer_interface () = default;

  virtual void begin_frame () = 0;
  virtual void end_frame () = 0;
  virtual void prepare (ImDrawData *draw_data) = 0;
  virtual void render (ImDrawData *draw_data) = 0;
  virtual void render_requested_previews () = 0;
  virtual void on_resize (uint32_t w, uint32_t h) = 0;

  // Preview system
  virtual void
  request_model_preview (wsl::comp::singl::runtime_context *runtime_ctx,
                         wsl::rsc::resource_manager *resource_manager,
                         entt::id_type model_eid, uint32_t w, uint32_t h) = 0;

  /**
   * Feeds one frame of view-gizmo interaction to the model preview camera.
   *
   * The preview owns its navigation widget; callers supply the input and the
   * widget placement in the preview viewport. The preview render pass draws
   * the widget overlay on the next preview redraw.
   *
   * :param input: Pointer and timing state for the preview viewport.
   * :param layout: Widget placement inside the preview viewport.
   */
  virtual void preview_navigate (const wsl::gfx::view_gizmo_input &input,
                                 const wsl::gfx::view_gizmo_layout &layout) = 0;
  virtual void preview_reset_camera_to_default () = 0;
  virtual SDL_GPUTexture *get_model_preview_texture () const = 0;

  // Editor styling
  virtual void apply_editor_style (const editor_theme &t) = 0;
  virtual const editor_theme &get_theme () const = 0;

  virtual imgui_fonts &get_fonts () = 0;
  virtual const imgui_fonts &get_fonts () const = 0;
};

} // namespace wsl::gfx
