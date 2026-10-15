#include "editor_app.hpp"

#include "editor_support/editor_inspectors.hpp"
#include "wsl/comp/singl/editor_context.hpp"
#include "wsl/comp/singl/runtime_context.hpp"
#include "wsl/das/das_engine.hpp"
#include "wsl/reg/runtime_project_module.hpp"
#include "wsl/reg/singleton_registry.hpp"
#include "wsl/sys/core_systems.hpp"
#include <spdlog/spdlog.h>

namespace wsl
{

editor_app::editor_app (const std::string &name, int width, int height,
                        const std::string &engine_res_path)
    : app (name, width, height, engine_res_path)
{
  m_editor_ctx
      = std::make_unique<comp::singl::editor_context> (*m_runtime_context);
  m_runtime_context->set_editor_ctx (m_editor_ctx.get ());

  comp::singl::editor_context::register_meta ();
  m_runtime_context->singleton_registry ()
      .register_bound_singleton_component<comp::singl::editor_context> (
          { "Editor Context", true });

  // Re-register the ImGui inspector widgets on top of the core meta data
  // (they live in editor_support, not in the core library).
  editor::register_editor_inspectors ();
}

editor_app::~editor_app () {}

void
editor_app::init_editor_subsystems ()
{
  // Create editor-specific renderers via virtual factories.
  // NOTE: this must be called from the derived class constructor
  // after the base class construction is complete.
  m_editor_ctx->set_imgui_renderer (create_imgui_renderer (
      m_runtime_context->window (), &m_runtime_context->render_ctx ()));
  m_editor_ctx->set_debug_renderer (create_debug_renderer (
      m_runtime_context->window (), &m_runtime_context->render_ctx ()));

  m_ui_layer = create_ui_layer (m_runtime_context.get (), m_editor_ctx.get ());
}

void
editor_app::on_init ()
{
  // One-time daScript global initialization on the main thread.
  das::das_engine::initialize_global ();

  m_ui_layer->initialize ();
}

void
editor_app::on_event (const wsl::engine_event &e)
{
  m_ui_layer->handle_event (e);

  auto &cs = m_runtime_context->core_systems ();
  if (cs != nullptr) {
    if (cs->render_ui_sys != nullptr) {
      if (auto *scene = m_runtime_context->scene_manager ().get_active ()) {
        cs->render_ui_sys->handle_sdl_event (scene->get_registry (), e);
      }
    }
  }
}

void
editor_app::on_update (double dt)
{
  m_editor_ctx->tick_editor_camera_anim (static_cast<float> (dt));
  m_editor_ctx->editor_resources ().update_async_uploads ();

  m_runtime_context->runtime_project_module ().poll_async_reload ();

  if (m_editor_ctx->pending_project_load ()) {
    static_cast<void> (m_runtime_context->resource_manager ().load_project (
        *m_editor_ctx->pending_project_load ()));
    m_editor_ctx->pending_project_load (std::nullopt);
  }
}

void
editor_app::on_render ()
{
  sys::core_systems::render_callbacks callbacks;
  callbacks.build_draw_data
      = [this] (entt::registry &reg) { m_ui_layer->build_draw_data (reg); };
  callbacks.prepare_gpu_rsc
      = [this] (entt::registry &) { m_ui_layer->prepare_gpu_resources (); };
  callbacks.record_ui_draw_cmd
      = [this] (entt::registry &) { m_ui_layer->record_draw_commands (); };

  m_runtime_context->core_systems ()->render (m_runtime_context->window (),
                                              callbacks);
}

} // namespace wsl
