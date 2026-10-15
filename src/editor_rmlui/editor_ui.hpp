#pragma once

#include <functional>
#include <memory>
#include <string>

namespace Rml
{
class Context;
class ElementDocument;
}

namespace wsl
{
namespace comp::singl
{
class runtime_context;
class editor_context;
}
namespace gfx
{
class render_window;
}
class engine_event;
}

namespace editor_rml
{

class log_sink;

/**
 * RmlUi-based editor UI: toolbar (play/pause/stop, gizmo modes), entity
 * hierarchy, component inspector, console and log panels.
 *
 * Owns its RmlUi context and the shared SDL_GPU/system backend instances
 * (published through wsl::gfx::rml_runtime so scene-level runtime UI shares
 * them). The 3D scene renders full-window behind the panels; the UI is
 * recorded through the core's `record_ui_overlay` render callback.
 */
class editor_ui
{
public:
  struct services
  {
    wsl::comp::singl::runtime_context *runtime = nullptr;
    wsl::comp::singl::editor_context *editor = nullptr;
    wsl::gfx::render_window *window = nullptr;
    /** Engine resource root (fonts), e.g. the build directory. */
    std::string asset_root;
    /** Source tree root used to locate rsc/editor_rml documents. */
    std::string source_root;
    /** Executes one console command and returns its textual output. */
    std::function<std::string (const std::string &)> execute_command;
    /** Starts loading the project manifest at the path (UI "Load Project"). */
    std::function<bool (const std::string &)> load_project;
    /** Queues a scene to open once the project is up ("Load Scene"). */
    std::function<void (const std::string &)> load_scene;
  };

  explicit editor_ui (services svc);
  ~editor_ui ();

  editor_ui (const editor_ui &) = delete;
  editor_ui &operator= (const editor_ui &) = delete;

  /** Initialises RmlUi, loads fonts + the editor document. */
  bool initialize ();
  void shutdown ();

  /** Forwards one engine (SDL) event to the UI. */
  void handle_event (const wsl::engine_event &event);
  /** Per-frame update: throttled data refresh, camera fly controls. */
  void update (double dt);
  /** Records the UI; call from the core's record_ui_overlay hook. */
  void render ();

  /** Sink whose recent lines are shown in the log panel (may be null). */
  void set_log_sink (log_sink *sink);

private:
  struct impl;
  std::unique_ptr<impl> m_impl;
  log_sink *m_log_sink = nullptr;
};

} // namespace editor_rml
