#include "editor_app.hpp"

#include "wsl/comp/singl/runtime_context.hpp"
#include "wsl/rsc/project.hpp"
#include "wsl/rsc/project_loader.hpp"
#include "wsl/rsc/resource_manager.hpp"
#include "cli/editor_server.hpp"
#include "cli/command_executor.hpp"
#include "renderer_imgui.hpp"
#include "wsl/debug/physics_debug_drawer.hpp"
#include "engine_ui.hpp"
#include "wsl/log/log.hpp"
#include <filesystem>
#include <vector>
namespace editor
{

namespace
{
/**
 * Resolves a ``--scene`` argument to an existing scene file.
 *
 * Accepts an absolute path, a path relative to the project root, a path
 * relative to the project's scenes directory, a ``res://`` path, or a bare
 * scene name (with or without the ``.wscn`` suffix).
 *
 * :param proj: Project the scene belongs to.
 * :param scene_arg: Raw scene argument provided on the command line.
 * :return: Absolute path of an existing scene file, or ``std::nullopt``
 *   when no candidate exists on disk.
 */
std::optional<std::string>
resolve_scene_path (const wsl::rsc::project &proj, const std::string &scene_arg)
{
  namespace fs = std::filesystem;
  fs::path const root (proj.root_path);
  fs::path const scenes_dir = root / proj.scenes_path;

  std::vector<fs::path> candidates;
  if (scene_arg.rfind ("res://", 0) == 0) {
    candidates.push_back (root / scene_arg.substr (6));
  } else if (fs::path (scene_arg).is_absolute ()) {
    candidates.push_back (fs::path (scene_arg));
  } else {
    candidates.push_back (root / scene_arg);
    candidates.push_back (scenes_dir / scene_arg);
    candidates.push_back (root / (scene_arg + wsl::rsc::scene_file::extension));
    candidates.push_back (scenes_dir
                          / (scene_arg + wsl::rsc::scene_file::extension));
  }

  for (const fs::path &candidate : candidates) {
    std::error_code ec;
    if (fs::is_regular_file (candidate, ec)) {
      return fs::absolute (candidate, ec).string ();
    }
  }
  return std::nullopt;
}
}

editor_app::editor_app (const std::string &name, int width, int height,
                        const std::string &engine_res_path)
    : wsl::editor_app (name, width, height, engine_res_path)
{
  init_editor_subsystems ();

  m_server = std::make_unique<wsl::cli::editor_server> ();
  m_server->set_command_handler (
      [this] (const std::string &cmd) { return this->execute_command (cmd); });
}

editor_app::~editor_app () { m_server->stop (); }

std::string
editor_app::execute_command (const std::string &command)
{
  wsl::cli::command_executor executor (*m_runtime_context);
  auto proj = m_runtime_context->resource_manager ().current_project ();
  if (proj) {
    executor.set_current_project (std::move (proj));
  }
  return executor.execute (command);
}

bool
editor_app::set_project_path (const std::string &path)
{
  // Accept a project directory as well as the manifest file itself, like
  // the REPL's `proj load` does.
  std::string manifest_path = path;
  if (std::filesystem::is_directory (path)) {
    manifest_path = (std::filesystem::path (path)
                     / wsl::rsc::project_loader::manifest_file)
                        .string ();
  }
  return m_runtime_context->resource_manager ().load_project (manifest_path);
}

void
editor_app::set_scene_path (const std::string &path)
{
  m_pending_scene = path;
}

void
editor_app::update_pending_scene ()
{
  if (!m_pending_scene) {
    return;
  }

  wsl::rsc::resource_manager &rm = m_runtime_context->resource_manager ();
  std::shared_ptr<wsl::rsc::project> const proj = rm.current_project ();
  if (!proj) {
    // Project assets (and the scene table) are filled in asynchronously;
    // retry on a later frame.
    return;
  }

  if (!m_pending_scene_id) {
    std::optional<std::string> const resolved
        = resolve_scene_path (*proj, *m_pending_scene);
    if (!resolved) {
      wsl::log::editor ()->error ("Scene not found: {}", *m_pending_scene);
      m_pending_scene.reset ();
      return;
    }
    // Registers the scene (reusing the project's entry when it is already
    // known) and requests an asynchronous load.
    m_pending_scene_id = rm.import_scene (*resolved);
    wsl::log::editor ()->info ("Opening scene {}", *resolved);
  }

  wsl::rsc::scene_state const state = rm.state (*m_pending_scene_id);
  if (state == wsl::rsc::scene_state::loading) {
    return;
  }
  if (state == wsl::rsc::scene_state::not_loaded) {
    // import_scene() already requested a load, so not_loaded here means the
    // load job failed.
    wsl::log::editor ()->error ("Failed to load scene '{}'", *m_pending_scene);
    m_pending_scene.reset ();
    m_pending_scene_id.reset ();
    return;
  }

  // Wait until every scene load has settled: the project's default scene
  // activates itself when its load job completes and would otherwise steal
  // the active scene from the one requested on the command line.
  for (const wsl::rsc::scene_resource_info &info : rm.list_scenes ()) {
    if (info.state == wsl::rsc::scene_state::loading) {
      return;
    }
  }

  if (!rm.activate_scene (*m_pending_scene_id)) {
    wsl::log::editor ()->error ("Failed to activate scene '{}'",
                                *m_pending_scene);
  }
  m_pending_scene.reset ();
  m_pending_scene_id.reset ();
}

std::unique_ptr<wsl::gfx::imgui_renderer_interface>
editor_app::create_imgui_renderer (wsl::gfx::render_window &window,
                                   wsl::gfx::render_context *ctx)
{
  return std::make_unique<editor::renderer_imgui> (window, ctx);
}

std::unique_ptr<wsl::debug::debug_renderer_interface>
editor_app::create_debug_renderer (wsl::gfx::render_window &window,
                                   wsl::gfx::render_context *ctx)
{
  return wsl::debug::make_physics_debug_renderer (window, ctx);
}

std::unique_ptr<wsl::editor::editor_ui_layer_interface>
editor_app::create_ui_layer (wsl::comp::singl::runtime_context *runtime_ctx,
                             wsl::comp::singl::editor_context *editor_ctx)
{
  return std::make_unique<editor::engine_ui> (runtime_ctx, editor_ctx);
}

void
editor_app::on_init ()
{
  wsl::editor_app::on_init ();

  // Wire the interactive console directly into the editor's runtime context
  ui_layer ()->set_console_command_handler (
      [this] (const std::string &cmd) { return this->execute_command (cmd); });
}

void
editor_app::on_update (double dt)
{
  wsl::editor_app::on_update (dt);

  // Start or restart the editor server when a project becomes loaded
  if (auto current_proj
      = m_runtime_context->resource_manager ().current_project ()) {
    std::string proj_root
        = std::filesystem::weakly_canonical (current_proj->root_path).string ();
    if (m_project_path != proj_root) {
      m_project_path = proj_root;
      if (m_server) {
        if (m_server->is_running ()) {
          m_server->stop ();
        }
        wsl::log::editor ()->info ("Starting server for project: {}",
                                   m_project_path);
        if (!m_server->start (m_project_path)) {
          wsl::log::editor ()->error ("Failed to start editor server");
        }
      }
    }
  }

  // Poll editor server for incoming commands
  m_server->poll ();

  // Open the scene requested with --scene once the project is up.
  update_pending_scene ();
}

} // namespace editor
