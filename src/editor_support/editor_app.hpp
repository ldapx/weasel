#pragma once

#include "wsl/app.hpp"
#include "wsl/comp/singl/editor_context.hpp"
#include "wsl/debug/debug_renderer.hpp"
#include "editor_support/editor_ui_layer_interface.hpp"
#include "editor_support/imgui_renderer_interface.hpp"

namespace wsl
{

/**
 * Abstract base class for editor-mode applications.
 *
 * Derive from this class to create a custom editor. The base class handles
 * editor_context creation, registration, and lifecycle. Concrete editors
 * must provide the renderer and UI layer implementations via the virtual
 * factory methods.
 */
class editor_app : public app
{
public:
  editor_app (const std::string &name, int width, int height,
              const std::string &engine_res_path = ".");
  ~editor_app () override;

protected:
  /// Create the ImGui renderer backend.
  virtual std::unique_ptr<gfx::imgui_renderer_interface>
  create_imgui_renderer (gfx::render_window &window, gfx::render_context *ctx)
      = 0;

  /// Create the physics debug renderer backend.
  virtual std::unique_ptr<debug::debug_renderer_interface>
  create_debug_renderer (gfx::render_window &window, gfx::render_context *ctx)
      = 0;

  /// Create the editor UI layer.
  virtual std::unique_ptr<editor::editor_ui_layer_interface>
  create_ui_layer (comp::singl::runtime_context *runtime_ctx,
                   comp::singl::editor_context *editor_ctx) = 0;

  // Lifecycle overrides
  void on_init () override;
  void on_event (const wsl::engine_event &e) override;
  void on_update (double dt) override;
  void on_render () override;

  comp::singl::editor_context *
  editor_ctx () const
  {
    return m_editor_ctx.get ();
  }
  editor::editor_ui_layer_interface *
  ui_layer () const
  {
    return m_ui_layer.get ();
  }

  // Must be called from the derived class constructor after base construction.
  void init_editor_subsystems ();

private:
  std::unique_ptr<comp::singl::editor_context> m_editor_ctx;
  std::unique_ptr<editor::editor_ui_layer_interface> m_ui_layer;
};

} // namespace wsl
