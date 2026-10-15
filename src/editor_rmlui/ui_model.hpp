#pragma once

// MODEL — all RmlUi editor state plus the engine-domain operations that work
// on it. The model never touches RmlUi: views render from it, the controller
// mutates it in response to input. It holds the editor services (runtime,
// editor context, window, command hooks) that give access to the underlying
// engine models (scene registry, resource manager).

#include "editor_ui.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <entt/entt.hpp>

#include "wsl/rsc/resource_manager.hpp"
#include "wsl/rsc/xmake_file_api.hpp"

namespace editor_rml
{

class agent_session;

struct ui_model
{
  explicit ui_model (editor_ui::services in_services);

  editor_ui::services svc;

  std::vector<std::string> console_lines;

  bool dirty = true;

  // Panels / menus / dialogs.
  bool show_welcome = true;
  bool welcome_project_seen = false;
  bool show_entities = true;
  bool show_systems = true;
  bool show_inspector = true;
  bool show_console = true;
  bool show_log = true;
  bool show_agent = true;
  std::string open_menu_id;

  // Tab selections per panel group.
  int left_tab = 0;   // 0 = entities, 1 = systems, 2 = files
  int right_tab = 0;  // 0 = inspector, 1 = build
  int bottom_tab = 0; // 0 = console, 1 = log, 2 = agent

  // Panel layout, in percent of the window; adjustable via drag handles.
  float left_pct = 22.0F;
  float right_pct = 25.0F;
  float bottom_pct = 27.0F;

  // New-project modal state.
  bool show_new_project = false;
  bool new_project_create_folder = true;

  // Native file/folder dialogs (async, one at a time).
  enum class dialog_mode
  {
    none,
    open_project,
    load_scene,
    save_scene,
    select_folder
  };
  dialog_mode pending_dialog = dialog_mode::none;
  bool waiting_for_dialog = false;
  bool dialog_finished = false;
  std::optional<std::string> dialog_result;

  std::vector<std::string> recent_projects;

  // Entity treeview state: collapsed rows (raw entity values) and the
  // singleton selected in the list below the tree (entity selection and
  // singleton selection are exclusive).
  std::unordered_set<std::uint32_t> collapsed_nodes;
  std::optional<entt::id_type> selected_singleton;
  // Drop-target highlight while an entity row is being dragged.
  std::string drop_target_id;

  // Collapsed inspector components, keyed by stable type id.
  std::unordered_set<std::uint64_t> collapsed_comps;
  // Forces one inspector rebuild even while an inspector input is focused
  // (selection changes and commits must never show a stale entity).
  bool inspector_refresh_forced = false;

  // Log panel filters (mirror the ImGui logger panel).
  bool log_show_trace = true;
  bool log_show_debug = true;
  bool log_show_info = true;
  bool log_show_warn = true;
  bool log_show_error = true;
  bool log_show_fatal = true;
  std::string log_category = "All";
  bool log_autoscroll = true;

  // Files tab: cached project file scan.
  struct file_entry
  {
    std::string rel;
    std::string kind;
  };
  std::string files_project;
  std::vector<file_entry> file_entries;
  std::vector<wsl::rsc::scene_resource_info> file_scenes;

  // Build tab: xmake project info plus one background job at a time.
  struct build_job_state
  {
    std::mutex mutex;
    bool done = false;
    std::string message;
    std::optional<wsl::rsc::xmake::xmake_file_api::project_info> info;
  };
  wsl::rsc::xmake::xmake_file_api build_api;
  std::optional<wsl::rsc::xmake::xmake_file_api::project_info> build_info;
  std::string build_project;
  int build_kit_idx = 0;
  int build_target_idx = -1;
  int build_test_idx = -1;
  std::shared_ptr<build_job_state> build_job;

  // Collapsed agent tool-call cards, keyed by "message.tool" index pair.
  std::unordered_set<std::string> agent_collapsed_tools;

  /** Live ACP agent session backing the Agent tab (null until used). */
  std::unique_ptr<agent_session> agent;

  /** Live lookup target for one inspector commit. */
  struct inspector_target
  {
    entt::registry *reg = nullptr;
    entt::meta_any instance;
    entt::entity entity = entt::null;
  };

  /** Buffers one console line; the view renders it on the next refresh. */
  void append_console (const std::string &line);

  // ------------------------------------------------------------ hierarchy

  /**
   * Returns the sorted direct children of *parent* via the first/next
   * sibling chain. Guards against malformed cycles with a step budget.
   */
  std::vector<entt::entity> hierarchy_children (entt::registry &reg,
                                                entt::entity parent);

  /**
   * Returns ``true`` when *candidate* equals *ancestor* or lies in its
   * subtree. Used to reject re-parents that would create a hierarchy cycle.
   */
  bool is_descendant_or_self (entt::registry &reg, entt::entity ancestor,
                              entt::entity candidate);

  /**
   * Attaches *child* to *new_parent* (``entt::null`` detaches to root),
   * maintaining the old and new sibling chains. Missing hierarchy
   * components are emplaced so entities created without one can still be
   * arranged in the tree.
   */
  void set_entity_parent (entt::registry &reg, entt::entity child,
                          entt::entity new_parent);

  /**
   * Handles a press-drag-release from one entity row onto another row (or
   * the tree background). Returns ``true`` when a re-parent was applied.
   */
  bool handle_entity_drop (const std::string &source_id,
                           const std::string &target_id);

  // ---------------------------------------------------------------- files

  /**
   * Rescans the current project for loadable scenes and script/source
   * files. Results are cached until the project changes or the user
   * presses Refresh.
   */
  void rescan_files ();

  /**
   * Loads the cached scene with the given resource id (from a Files tab
   * ``scn-<id>`` row).
   */
  void open_scene_by_id (entt::id_type scene_id);

  // ---------------------------------------------------------------- build

  /** Returns ``true`` when a build job is currently running. */
  bool build_busy () const;

  /**
   * Starts a background build job. The worker only touches its own copies
   * plus the job state (never live UI), and :cpp:func:`poll_build_job`
   * reaps the result on the UI thread.
   */
  void
  start_build_job (const std::string &name,
                   std::function<void (std::shared_ptr<build_job_state>)> work);

  /** Reaps a finished build job on the UI thread. */
  void poll_build_job ();

  static std::string build_extra_args (const std::string &wsl_path,
                                       const std::string &kit);

  void start_build_refresh ();
  void start_build_configure ();
  void start_build_target ();
  void start_build_test ();

  // ------------------------------------------------------------- inspector

  /**
   * Resolves the live object behind an inspector row. Rejects stale rows
   * whose component type no longer matches the current selection.
   */
  bool find_inspector_target (entt::id_type comp_type, inspector_target &out);

  /**
   * Writes one edited inspector field back to the live object.
   *
   * :param comp_type: Stable component/singleton type id (``fld-`` prefix).
   * :param field_id: Reflected field id within the type.
   * :param text: Raw editor text.
   * :return: ``true`` when the value was accepted.
   */
  bool commit_inspector_field (entt::id_type comp_type, entt::id_type field_id,
                               const std::string &text);

  /**
   * Writes one lane of a vector/quaternion inspector field (from a
   * ``fld-<comp>-<field>-<lane>`` input). Euler lanes of the transform
   * rotation are handled in degrees like whole-value commits.
   */
  bool commit_inspector_lane (entt::id_type comp_type, entt::id_type field_id,
                              int lane, const std::string &text);

  /**
   * Flips a boolean inspector field (from a ``fldb-`` toggle button).
   */
  void toggle_inspector_bool (entt::id_type comp_type, entt::id_type field_id);

  // ------------------------------------------------------------- projects

  void open_project (const std::string &path);

  /**
   * Creates a project; the caller supplies the form values so the model
   * never touches RmlUi.
   */
  void create_project (const std::string &name, const std::string &folder);

  void reload_active_scene ();

  void load_recent_projects ();
  void add_recent_project (const std::string &path);
  void save_recent_projects () const;

  void request_exit ();

  // -------------------------------------------------------------- dialogs

  static void dialog_callback (void *userdata, const char *const *filelist,
                               int filter);

  void show_project_dialog ();
  void show_load_scene_dialog ();
  void show_save_scene_dialog ();
  void show_select_folder_dialog ();

  // ---------------------------------------------------------------- console

  void run_console_command (const std::string &command);
};

} // namespace editor_rml
