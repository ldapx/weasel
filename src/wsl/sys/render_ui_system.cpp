#include "render_ui_system.hpp"
#include "wsl/log/log.hpp"

#include <RmlUi/Core/Math.h>
#include <RmlUi_Platform_SDL.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>
#include <entt/entity/entity.hpp>
#include <entt/entity/fwd.hpp>
#include <algorithm>
#include <cmath>
#include <string>

#include "wsl/event/event_hub.hpp"
#include "wsl/comp/singl/rendering_manager.hpp"
#include "wsl/comp/singl/ui_manager.hpp"
#include "wsl/comp/singl/runtime_context.hpp"
#include "wsl/comp/singl/editor_context.hpp"
#include "wsl/comp/singl/engine_resources.hpp"
#include "wsl/gfx/ui_canvas.hpp"
#include "wsl/gfx/viewport_resolve.hpp"
#include "wsl/rsc/resource_manager.hpp"

namespace wsl
{

namespace sys
{

namespace
{

/**
 * Resolves the root viewport for the Virtual-Canvas UI (D15/U1). The
 * container is the present texture in editor mode (window-sized) and the
 * window in standalone builds.
 */
gfx::resolved_viewport
resolve_root_ui_viewport (comp::singl::runtime_context &runtime_ctx)
{
  int width = 0;
  int height = 0;
  if ((runtime_ctx.editor_ctx () != nullptr)
      && !runtime_ctx.editor_ctx ()->game_fullscreen ()) {
    width = static_cast<int> (runtime_ctx.window ().present_tex ().width);
    height = static_cast<int> (runtime_ctx.window ().present_tex ().height);
  } else {
    runtime_ctx.window ().get_size (width, height);
  }

  comp::viewport_quality_defaults quality{};
  if (auto project = runtime_ctx.resource_manager ().current_project ()) {
    quality = project->window.quality;
  }

  comp::viewport_settings settings{};
  math::vec2f base{ 1920.0F, 1080.0F };
  if (auto *rendering = runtime_ctx.get_active_rendering_manager ();
      rendering != nullptr) {
    settings = rendering->root_viewport;
    base = rendering->root_viewport_virtual_size;
  }

  return gfx::resolve_viewport (
      settings,
      math::vec2f{ static_cast<float> (std::max (width, 1)),
                   static_cast<float> (std::max (height, 1)) },
      base, settings.resolved_render_scale (quality.render_scale));
}

} // namespace

void
render_ui_system::register_event_sources (event::event_hub &hub)
{
  (void)hub;
}

void
render_ui_system::register_iterations (event::event_hub &hub)
{
  (void)hub;
}

void
render_ui_system::on_init (entt::registry & /*unused*/)
{
}

void
render_ui_system::on_render_build_draw_data (entt::registry &registry)
{
  if (!registry.ctx ().template contains<comp::singl::runtime_context *> ()
      || !registry.ctx ().template contains<comp::singl::ui_manager *> ()) {
    return;
  }

  auto &ui = *registry.ctx ().template get<comp::singl::ui_manager *> ();

  ui.needs_reload () = true;
}

void
render_ui_system::on_update (entt::registry &registry, double /*unused*/)
{
  if (!registry.ctx ().template contains<comp::singl::runtime_context *> ()
      || !registry.ctx ().template contains<comp::singl::ui_manager *> ()) {
    return;
  }

  auto &runtime_ctx
      = *registry.ctx ().template get<comp::singl::runtime_context *> ();
  auto &ui = *registry.ctx ().template get<comp::singl::ui_manager *> ();

  ui.prepare_scene (registry);

  const bool needs_reload
      = ui.needs_reload ()
        || ui.loaded_document_id ().value != ui.active_document_id ().value;
  if (needs_reload) {
    // Ensure all registered fonts are loaded into RmlUi
    for (const auto &font : runtime_ctx.resource_manager ().list_fonts ()) {
      std::string const resolved
          = runtime_ctx.resource_manager ().resolve_path (font.path);
      ui.load_font (resolved);
    }

    // Load engine fonts from editor context if available
    if (runtime_ctx.editor_ctx () != nullptr) {
      for (const auto &font :
           runtime_ctx.editor_ctx ()->editor_resources ().list_fonts ()) {
        std::string const resolved
            = runtime_ctx.editor_ctx ()->editor_resources ().resolve_path (
                font.path);
        ui.load_font (resolved);
      }
    }

    if (ui.active_document_instance () != nullptr) {
      ui.detach_event_listeners ();
      ui.active_document_instance ()->Close ();
      ui.active_document_instance () = nullptr;
    }
    ui.loaded_document_id ().value = entt::null;

    if (ui.active_document_id ().value != entt::null) {
      if (auto info
          = runtime_ctx.resource_manager ().info (ui.active_document_id ())) {
        std::string const resolved
            = runtime_ctx.resource_manager ().resolve_path (info->path);
        ui.active_document_instance () = ui.context ()->LoadDocument (resolved);
        if (ui.active_document_instance () != nullptr) {
          ui.active_document_instance ()->Show ();
          ui.loaded_document_id () = ui.active_document_id ();
          ui.attach_event_listeners ();
        } else {
          wsl::log::sys ()->error ("Failed to load RML document: {}", resolved);
        }
      }
    }
    ui.needs_reload () = false;
  }

  if (ui.context () != nullptr) {
    ui.context ()->Update ();
  }
}

void
render_ui_system::on_render_record_draw_cmd (entt::registry &registry)
{
  auto &ctx = registry.ctx ();
  if (!ctx.template contains<comp::singl::runtime_context *> ()
      || !ctx.template contains<comp::singl::ui_manager *> ()) {
    return;
  }

  auto &runtime_ctx = *ctx.get<comp::singl::runtime_context *> ();
  auto &ui = *ctx.get<comp::singl::ui_manager *> ();

  if (ui.context () == nullptr) {
    return;
  }

  // Nothing to composite while no document is configured.
  if (ui.active_document_instance () == nullptr
      && ui.active_document_id ().value == entt::null) {
    return;
  }

  // Virtual-canvas UI (D15/U1): RmlUi renders at the root viewport's virtual
  // (base) resolution into an offscreen canvas, which is then composited
  // through the resolved output rect so the UI scales with the viewport's
  // scaling mode and aspect ratio policy.
  gfx::resolved_viewport const resolved
      = resolve_root_ui_viewport (runtime_ctx);

  uint32_t const virt_w
      = static_cast<uint32_t> (std::max (1.0F, resolved.virtual_size.x ()));
  uint32_t const virt_h
      = static_cast<uint32_t> (std::max (1.0F, resolved.virtual_size.y ()));

  Rml::Vector2i const dimensions{ static_cast<int> (virt_w),
                                  static_cast<int> (virt_h) };
  Rml::Vector2i const current = ui.context ()->GetDimensions ();
  if (current.x != dimensions.x || current.y != dimensions.y) {
    ui.context ()->SetDimensions (dimensions);
  }

  auto &window = runtime_ctx.window ();
  gfx::ui_canvas &canvas
      = ui.ensure_virtual_canvas (*window.ctx (), window, virt_w, virt_h);
  if (!canvas.valid ()) {
    return;
  }

  canvas.begin_frame (*window.ctx ());

  if (ui.render_interface () != nullptr) {
    ui.render_interface ()->BeginFrame (window.ctx ()->main_cmd,
                                        canvas.texture (), virt_w, virt_h);
  }

  ui.context ()->Render ();

  if (ui.render_interface () != nullptr) {
    ui.render_interface ()->EndFrame ();
  }

  // Standalone builds present the UI to the swapchain as well; editors sample
  // `present_tex` into their game view and keep their host window separate.
  bool const standalone = runtime_ctx.editor_ctx () == nullptr;
  canvas.composite (window, resolved.output_rect, standalone);
}

void
render_ui_system::handle_sdl_event (entt::registry &registry,
                                    const engine_event &ev)
{

  auto &ctx = registry.ctx ();
  if (!ctx.template contains<comp::singl::runtime_context *> ()
      || !ctx.template contains<comp::singl::ui_manager *> ()) {
    return;
  }

  auto &runtime_ctx = *ctx.get<comp::singl::runtime_context *> ();
  auto &ui = *ctx.get<comp::singl::ui_manager *> ();

  if ((runtime_ctx.editor_ctx () != nullptr) && !runtime_ctx.is_running ()) {
    return;
  }

  // Virtual-canvas pointer translation (D15/U1): window space -> present
  // pixels (editor game view) -> root viewport virtual space.
  gfx::resolved_viewport const resolved
      = resolve_root_ui_viewport (runtime_ctx);

  bool const in_editor_game_view
      = (runtime_ctx.editor_ctx () != nullptr)
        && !runtime_ctx.editor_ctx ()->game_fullscreen ();

  if (in_editor_game_view) {
    SDL_Event adjusted_ev = ev.sdl ();
    bool process = true;

    auto &ed = *runtime_ctx.editor_ctx ();
    float const img_x = ed.last_img_min ().x;
    float const img_y = ed.last_img_min ().y;
    float const img_w = ed.last_img_size ().x;
    float const img_h = ed.last_img_size ().y;

    if (img_w > 0 && img_h > 0) {
      float const scale_x
          = (float)runtime_ctx.window ().present_tex ().width / img_w;
      float const scale_y
          = (float)runtime_ctx.window ().present_tex ().height / img_h;

      // Mouse state straight from SDL keeps this system independent of the
      // editor UI toolkit (the previous implementation queried ImGui).
      float mouse_x = 0.0F;
      float mouse_y = 0.0F;
      SDL_GetMouseState (&mouse_x, &mouse_y);

      math::vec2f virtual_point{ 0.0F, 0.0F };
      bool const inside = gfx::map_output_point_to_virtual (
          math::vec2f{ (mouse_x - img_x) * scale_x,
                       (mouse_y - img_y) * scale_y },
          resolved, virtual_point);

      if (ev.type () == SDL_EVENT_MOUSE_MOTION) {
        adjusted_ev.motion.x = virtual_point.x ();
        adjusted_ev.motion.y = virtual_point.y ();
      } else if (ev.type () == SDL_EVENT_MOUSE_BUTTON_DOWN
                 || ev.type () == SDL_EVENT_MOUSE_BUTTON_UP) {
        adjusted_ev.button.x = virtual_point.x ();
        adjusted_ev.button.y = virtual_point.y ();

        // Ignore clicks outside the game view / virtual canvas.
        if (!inside || mouse_x < img_x || mouse_x > img_x + img_w
            || mouse_y < img_y || mouse_y > img_y + img_h) {
          process = false;
        }
      }
    }

    if (process) {
      RmlSDL::InputEventHandler (ui.context (),
                                 runtime_ctx.window ().handler (), adjusted_ev);
    }
  } else {
    // Standalone: map the window point straight into virtual space.
    SDL_Event adjusted_ev = ev.sdl ();
    float mouse_x = 0.0F;
    float mouse_y = 0.0F;
    SDL_GetMouseState (&mouse_x, &mouse_y);

    math::vec2f virtual_point{ 0.0F, 0.0F };
    gfx::map_output_point_to_virtual (math::vec2f{ mouse_x, mouse_y }, resolved,
                                      virtual_point);

    if (ev.type () == SDL_EVENT_MOUSE_MOTION) {
      adjusted_ev.motion.x = virtual_point.x ();
      adjusted_ev.motion.y = virtual_point.y ();
    } else if (ev.type () == SDL_EVENT_MOUSE_BUTTON_DOWN
               || ev.type () == SDL_EVENT_MOUSE_BUTTON_UP) {
      adjusted_ev.button.x = virtual_point.x ();
      adjusted_ev.button.y = virtual_point.y ();
    }

    RmlSDL::InputEventHandler (ui.context (), runtime_ctx.window ().handler (),
                               adjusted_ev);
  }
}

} // namespace sys

} // namespace wsl
