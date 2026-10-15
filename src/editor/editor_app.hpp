#pragma once

#include "editor_support/editor_app.hpp"
#include "wsl/rsc/resource_ids.hpp"
#include "engine_ui.hpp"
#include "cli/editor_server.hpp"

#include <optional>
#include <string>

namespace editor
{

class editor_app : public wsl::editor_app
{
public:
  editor_app (const std::string &name, int width, int height,
              const std::string &engine_res_path = ".");
  ~editor_app () override;

  wsl::comp::singl::runtime_context *
  get_runtime_context () const
  {
    return m_runtime_context.get ();
  }
  /**
   * Starts loading the project at the provided path.
   *
   * :param path: Path to the project manifest (``wslpro.json``) or to a
   *   directory containing it.
   * :return: ``true`` when the manifest was found and loading started.
   */
  bool set_project_path (const std::string &path);

  /**
   * Queues a scene to open once the project has finished loading.
   *
   * :param path: Scene file path, path relative to the project root, or
   *   bare scene name inside the project's scenes directory.
   */
  void set_scene_path (const std::string &path);

  std::string execute_command (const std::string &command);

protected:
  std::unique_ptr<wsl::gfx::imgui_renderer_interface>
  create_imgui_renderer (wsl::gfx::render_window &window,
                         wsl::gfx::render_context *ctx) override;

  std::unique_ptr<wsl::debug::debug_renderer_interface>
  create_debug_renderer (wsl::gfx::render_window &window,
                         wsl::gfx::render_context *ctx) override;

  std::unique_ptr<wsl::editor::editor_ui_layer_interface>
  create_ui_layer (wsl::comp::singl::runtime_context *runtime_ctx,
                   wsl::comp::singl::editor_context *editor_ctx) override;

  void on_init () override;
  void on_update (double dt) override;

private:
  /** Resolves and activates the scene requested via ``set_scene_path``. */
  void update_pending_scene ();

  std::unique_ptr<wsl::cli::editor_server> m_server;
  std::string m_project_path;
  std::optional<std::string> m_pending_scene;
  std::optional<wsl::rsc::scene_id> m_pending_scene_id;
};

} // namespace editor
