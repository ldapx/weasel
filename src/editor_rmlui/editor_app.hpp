#pragma once

#include "cli/editor_server.hpp"
#include "wsl/app.hpp"
#include "wsl/comp/singl/editor_context.hpp"

#include <memory>
#include <optional>
#include <string>

namespace editor_rml
{

class editor_ui;
class log_sink;

/**
 * RmlUi editor application.
 *
 * The successor to the ImGui editor: same core runtime, editor_context
 * state, gizmos and attach-mode server, but a HTML/CSS UI rendered through
 * RmlUi. Does not link ImGui or editor_support.
 */
class editor_app : public wsl::app
{
public:
  editor_app (const std::string &name, int width, int height,
              const std::string &engine_res_path = ".");
  ~editor_app () override;

  /**
   * Starts loading the project at the provided path.
   * :return: true when the manifest was found and loading started.
   */
  bool set_project_path (const std::string &path);

  /** Queues a scene to open once the project has finished loading. */
  void set_scene_path (const std::string &path);

  /** Executes one console/attach command via the CLI executor. */
  std::string execute_command (const std::string &command);

protected:
  void on_init () override;
  void on_event (const wsl::engine_event &e) override;
  void on_update (double dt) override;
  void on_render () override;

private:
  /** Resolves and activates the scene requested via ``set_scene_path``. */
  void update_pending_scene ();

  /** Starts/stops the attach server as the project changes; polls it. */
  void tick_editor_server ();

  std::unique_ptr<wsl::comp::singl::editor_context> m_editor_ctx;
  std::unique_ptr<editor_ui> m_ui;
  std::shared_ptr<log_sink> m_log_sink;
  std::unique_ptr<wsl::cli::editor_server> m_server;
  std::string m_project_path;
  std::optional<std::string> m_pending_scene;
  std::optional<wsl::rsc::scene_id> m_pending_scene_id;
};

} // namespace editor_rml
