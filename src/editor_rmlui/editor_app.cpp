#include "editor_app.hpp"

#include "editor_ui.hpp"
#include "log_sink.hpp"

#include "cli/command_executor.hpp"
#include "wsl/comp/singl/runtime_context.hpp"
#include "wsl/das/das_engine.hpp"
#include "wsl/debug/physics_debug_drawer.hpp"
#include "wsl/log/log.hpp"
#include "wsl/reg/runtime_project_module.hpp"
#include "wsl/reg/singleton_registry.hpp"
#include "wsl/rsc/project.hpp"
#include "wsl/rsc/project_loader.hpp"
#include "wsl/rsc/resource_manager.hpp"
#include "wsl/rsc/scene.hpp"
#include "wsl/sys/core_systems.hpp"

#include <entt/core/hashed_string.hpp>
#include <filesystem>
#include <vector>

namespace editor_rml
{

namespace
{
/**
 * Resolves a ``--scene`` argument to an existing scene file.
 *
 * Accepts an absolute path, a path relative to the project root, a path
 * relative to the project's scenes directory, a ``res://`` path, or a bare
 * scene name (with or without the ``.wscn`` suffix).
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
} // namespace

editor_app::editor_app (const std::string &name, int width, int height,
                        const std::string &engine_res_path)
    : wsl::app (name, width, height, engine_res_path)
{
  // Editor session state: same neutral editor_context the ImGui editor and
  // the core render passes use (selection, gizmos, grid, game view camera).
  m_editor_ctx
      = std::make_unique<wsl::comp::singl::editor_context> (*m_runtime_context);
  m_runtime_context->set_editor_ctx (m_editor_ctx.get ());

  wsl::comp::singl::editor_context::register_meta ();
  m_runtime_context->singleton_registry ()
      .register_bound_singleton_component<wsl::comp::singl::editor_context> (
          { "Editor Context", true });

  // Physics debug lines render through the neutral debug renderer.
  m_editor_ctx->set_debug_renderer (wsl::debug::make_physics_debug_renderer (
      m_runtime_context->window (), &m_runtime_context->render_ctx ()));

  // The 3D scene fills the window; panels are an RmlUi overlay. Using the
  // editor camera by default matches the ImGui editor's game view.
  m_editor_ctx->game_view_camera_selection (
      wsl::comp::singl::editor_context::game_view_camera_sel::editor_3d);
  m_editor_ctx->game_fullscreen (true);
  m_editor_ctx->reset_editor_camera ();

  // Composite the scene to the present texture only: the UI blits it into
  // the game-view panel, so it must not cover the whole swapchain.
  m_runtime_context->window ().present_to_swapchain (false);

  // Log panel sink: attach before the UI initializes so early messages show.
  m_log_sink = std::make_shared<log_sink> ();
  for (const auto &logger :
       { wsl::log::editor (), wsl::log::core (), wsl::log::rsc (),
         wsl::log::sys (), wsl::log::gfx (), wsl::log::phys (),
         wsl::log::net () }) {
    if (logger != nullptr) {
      logger->sinks ().push_back (m_log_sink);
    }
  }

  m_server = std::make_unique<wsl::cli::editor_server> ();
  m_server->set_command_handler (
      [this] (const std::string &cmd) { return this->execute_command (cmd); });
}

editor_app::~editor_app ()
{
  if (m_server != nullptr) {
    m_server->stop ();
  }
}

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
    m_pending_scene_id = rm.import_scene (*resolved);
    wsl::log::editor ()->info ("Opening scene {}", *resolved);
  }

  wsl::rsc::scene_state const state = rm.state (*m_pending_scene_id);
  if (state == wsl::rsc::scene_state::loading) {
    return;
  }
  if (state == wsl::rsc::scene_state::not_loaded) {
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

void
editor_app::tick_editor_server ()
{
  // Start or restart the editor server when a project becomes loaded.
  if (auto current_proj
      = m_runtime_context->resource_manager ().current_project ()) {
    std::string prog_root
        = std::filesystem::weakly_canonical (current_proj->root_path).string ();
    if (m_project_path != prog_root) {
      m_project_path = prog_root;
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

  m_server->poll ();
}

void
editor_app::on_init ()
{
  wsl::das::das_engine::initialize_global ();

  editor_ui::services svc;
  svc.runtime = m_runtime_context.get ();
  svc.editor = m_editor_ctx.get ();
  svc.window = &m_runtime_context->window ();
  svc.asset_root
      = m_runtime_context->resource_manager ().get_engine_resource_path ();
  svc.source_root = WEASEL_SOURCE_DIR;
  svc.execute_command
      = [this] (const std::string &cmd) { return this->execute_command (cmd); };
  svc.load_project = [this] (const std::string &path) {
    return this->set_project_path (path);
  };
  svc.load_scene
      = [this] (const std::string &path) { this->set_scene_path (path); };

  m_ui = std::make_unique<editor_ui> (std::move (svc));
  m_ui->set_log_sink (m_log_sink.get ());
  if (!m_ui->initialize ()) {
    wsl::log::editor ()->error ("RmlUi editor UI failed to initialise");
  }
}

void
editor_app::on_event (const wsl::engine_event &e)
{
  if (m_ui != nullptr) {
    m_ui->handle_event (e);
  }

  // Play/pause/stop hotkeys, matching the ImGui editor.
  if (e.type () == SDL_EVENT_KEY_UP) {
    switch (e.as_keyboard ().scancode) {
    case SDL_SCANCODE_F5:
      if (!m_runtime_context->is_running ()) {
        m_runtime_context->set_running (true);
      }
      break;
    case SDL_SCANCODE_F7:
      if (m_runtime_context->is_running ()) {
        m_runtime_context->set_running (false);
      }
      break;
    case SDL_SCANCODE_F8:
      if (m_runtime_context->in_play_session ()) {
        m_runtime_context->request_stop ();
      }
      break;
    default:
      break;
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

  tick_editor_server ();
  update_pending_scene ();

  if (m_ui != nullptr) {
    m_ui->update (dt);
  }
}

void
editor_app::on_render ()
{
  wsl::sys::core_systems::render_callbacks callbacks;
  callbacks.record_ui_overlay = [this] (entt::registry &) { m_ui->render (); };

  m_runtime_context->core_systems ()->render (m_runtime_context->window (),
                                              callbacks);
}

} // namespace editor_rml
