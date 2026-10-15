#include "ui_model.hpp"

#include "ui_agent.hpp"
#include "ui_util.hpp"

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_events.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include "wsl/comp/hierarchy.hpp"
#include "wsl/comp/singl/editor_context.hpp"
#include "wsl/gfx/render_window.hpp"
#include "wsl/comp/singl/runtime_context.hpp"
#include "wsl/comp/transform.hpp"
#include "wsl/log/log.hpp"
#include "wsl/math/vector.hpp"
#include "wsl/reg/component_registry.hpp"
#include "wsl/reg/singleton_registry.hpp"
#include "wsl/rsc/project.hpp"
#include "wsl/rsc/project_loader.hpp"
#include "wsl/rsc/scene.hpp"
#include "wsl/rsc/scene_manager.hpp"

namespace editor_rml
{

ui_model::ui_model (editor_ui::services in_services)
    : svc (std::move (in_services))
{
}

std::vector<entt::entity>
ui_model::hierarchy_children (entt::registry &reg, entt::entity parent)
{
  std::vector<entt::entity> children;
  const auto *h = reg.try_get<wsl::comp::hierarchy> (parent);
  if (h == nullptr) {
    return children;
  }
  std::unordered_set<std::uint32_t> seen;
  entt::entity child = h->first;
  for (int guard = 0; guard < 4096 && child != entt::null && reg.valid (child);
       ++guard) {
    const auto *ch = reg.try_get<wsl::comp::hierarchy> (child);
    if (ch == nullptr) {
      break;
    }
    if (!seen.insert (static_cast<std::uint32_t> (child)).second) {
      break;
    }
    children.push_back (child);
    child = ch->next;
  }
  return children;
}

/**
 * Returns ``true`` when *candidate* equals *ancestor* or lies in its
 * subtree. Used to reject re-parents that would create a hierarchy cycle.
 */

bool
ui_model::is_descendant_or_self (entt::registry &reg, entt::entity ancestor,
                                 entt::entity candidate)
{
  for (entt::entity cur = candidate; cur != entt::null && reg.valid (cur);) {
    if (cur == ancestor) {
      return true;
    }
    const auto *h = reg.try_get<wsl::comp::hierarchy> (cur);
    if (h == nullptr || h->parent == cur) {
      break;
    }
    cur = h->parent;
  }
  return false;
}

/**
 * Attaches *child* to *new_parent* (``entt::null`` detaches to root),
 * maintaining the old and new sibling chains. Missing hierarchy
 * components are emplaced so entities created without one can still be
 * arranged in the tree.
 */

void
ui_model::set_entity_parent (entt::registry &reg, entt::entity child,
                             entt::entity new_parent)
{
  if (!reg.valid (child)) {
    return;
  }
  if (new_parent != entt::null && !reg.valid (new_parent)) {
    return;
  }
  if (child == new_parent
      || (new_parent != entt::null
          && is_descendant_or_self (reg, child, new_parent))) {
    return;
  }
  if (!reg.all_of<wsl::comp::hierarchy> (child)) {
    reg.emplace<wsl::comp::hierarchy> (child);
  }
  if (new_parent != entt::null
      && !reg.all_of<wsl::comp::hierarchy> (new_parent)) {
    reg.emplace<wsl::comp::hierarchy> (new_parent);
  }

  wsl::comp::hierarchy &ch = reg.get<wsl::comp::hierarchy> (child);
  if (ch.parent == new_parent) {
    return;
  }

  // Detach from the old parent's child list.
  if (ch.parent != entt::null && reg.valid (ch.parent)
      && reg.all_of<wsl::comp::hierarchy> (ch.parent)) {
    wsl::comp::hierarchy &old = reg.get<wsl::comp::hierarchy> (ch.parent);
    entt::entity *link = &old.first;
    for (int guard = 0; guard < 4096 && *link != entt::null; ++guard) {
      if (*link == child) {
        *link = reg.get<wsl::comp::hierarchy> (*link).next;
        break;
      }
      if (!reg.valid (*link) || !reg.all_of<wsl::comp::hierarchy> (*link)) {
        break;
      }
      link = &reg.get<wsl::comp::hierarchy> (*link).next;
    }
  }

  ch.parent = new_parent;
  ch.next = entt::null;

  if (new_parent != entt::null) {
    wsl::comp::hierarchy &ph = reg.get<wsl::comp::hierarchy> (new_parent);
    ch.next = ph.first;
    ph.first = child;
    collapsed_nodes.erase (static_cast<std::uint32_t> (new_parent));
  }
}

/**
 * Handles a press-drag-release from one entity row onto another row (or
 * the tree background). Returns ``true`` when a re-parent was applied.
 */

bool
ui_model::handle_entity_drop (const std::string &source_id,
                              const std::string &target_id)
{
  auto *scene = svc.runtime->scene_manager ().get_active ();
  if (scene == nullptr) {
    return false;
  }
  entt::registry &reg = scene->get_registry ();
  const auto child = static_cast<entt::entity> (
      std::strtoul (source_id.c_str () + 4, nullptr, 10));
  if (!reg.valid (child)) {
    return false;
  }

  entt::entity new_parent = entt::null;
  if (target_id.rfind ("ent-", 0) == 0) {
    new_parent = static_cast<entt::entity> (
        std::strtoul (target_id.c_str () + 4, nullptr, 10));
    if (!reg.valid (new_parent)) {
      return false;
    }
  } else if (target_id != "hierarchy" && target_id != "left-entities") {
    return false;
  }

  const auto *ch = reg.try_get<wsl::comp::hierarchy> (child);
  const entt::entity old_parent = (ch != nullptr) ? ch->parent : entt::null;
  if (old_parent == new_parent) {
    return false;
  }
  if (new_parent != entt::null
      && is_descendant_or_self (reg, child, new_parent)) {
    append_console ("Cannot re-parent: target is a descendant of '"
                    + scene->get_entity_name (child) + "'.");
    return false;
  }

  set_entity_parent (reg, child, new_parent);
  svc.editor->selected_entity (child);
  selected_singleton.reset ();
  svc.editor->transform_gizmo_visible (true);
  append_console ("Re-parented '" + scene->get_entity_name (child) + "' to '"
                  + (new_parent == entt::null
                         ? "<root>"
                         : scene->get_entity_name (new_parent))
                  + "'.");
  return true;
}

void
ui_model::rescan_files ()
{
  file_entries.clear ();
  file_scenes.clear ();

  auto project = svc.runtime->resource_manager ().current_project ();
  if (!project) {
    files_project.clear ();
    return;
  }
  files_project = project->root_path;

  file_scenes = svc.runtime->resource_manager ().list_scenes ();
  std::sort (file_scenes.begin (), file_scenes.end (),
             [] (const wsl::rsc::scene_resource_info &lhs,
                 const wsl::rsc::scene_resource_info &rhs) {
               return lhs.name < rhs.name;
             });

  const std::filesystem::path root (project->root_path);
  for (const char *dir :
       { project->components_path.c_str (), project->systems_path.c_str (),
         project->singletons_path.c_str () }) {
    if (dir == nullptr || dir[0] == '\0') {
      continue;
    }
    std::error_code ec;
    const std::filesystem::path base = root / dir;
    if (!std::filesystem::is_directory (base, ec)) {
      continue;
    }
    for (std::filesystem::recursive_directory_iterator it (base, ec), end;
         it != end && file_entries.size () < 300; it.increment (ec)) {
      if (ec || !it->is_regular_file (ec)) {
        continue;
      }
      const std::string ext = it->path ().extension ().string ();
      if (ext != ".cpp" && ext != ".hpp" && ext != ".das" && ext != ".h") {
        continue;
      }
      std::string rel;
      try {
        rel = std::filesystem::relative (it->path (), root, ec).string ();
      } catch (...) {
        continue;
      }
      if (ec) {
        continue;
      }
      file_entries.push_back (file_entry{ rel, ext });
    }
  }
  std::sort (file_entries.begin (), file_entries.end (),
             [] (const file_entry &lhs, const file_entry &rhs) {
               return lhs.rel < rhs.rel;
             });
}

void
ui_model::open_scene_by_id (entt::id_type scene_id)
{
  for (const auto &info : file_scenes) {
    if (info.id == scene_id) {
      if (svc.load_scene) {
        svc.load_scene (info.path);
        append_console ("Opening scene: " + info.path);
      }
      return;
    }
  }
  // Fall back to a fresh scan: the registry may have changed.
  rescan_files ();
  for (const auto &info : file_scenes) {
    if (info.id == scene_id && svc.load_scene) {
      svc.load_scene (info.path);
      append_console ("Opening scene: " + info.path);
      return;
    }
  }
  append_console ("Scene no longer available.");
  dirty = true;
}

/** Returns ``true`` when a build job is currently running. */

bool
ui_model::build_busy () const
{
  if (build_job == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock (build_job->mutex);
  return !build_job->done;
}

/**
 * Starts a background build job. The worker only touches its own copies
 * plus the job state (never live UI/registers), and
 * :cpp:func:`poll_build_job` reaps the result on the UI thread.
 */

void
ui_model::start_build_job (
    const std::string &name,
    std::function<void (std::shared_ptr<build_job_state>)> work)
{
  if (build_busy ()) {
    append_console ("A build job is already running.");
    return;
  }
  auto project = svc.runtime->resource_manager ().current_project ();
  if (!project) {
    append_console ("No project loaded.");
    return;
  }
  auto job = std::make_shared<build_job_state> ();
  build_job = job;
  append_console (name + "...");
  dirty = true;
  std::thread ([job, work = std::move (work)] () {
    try {
      work (job);
    } catch (const std::exception &e) {
      std::lock_guard<std::mutex> lock (job->mutex);
      job->message = std::string ("Job failed: ") + e.what ();
      job->done = true;
    } catch (...) {
      std::lock_guard<std::mutex> lock (job->mutex);
      job->message = "Job failed with an unknown error.";
      job->done = true;
    }
  }).detach ();
}

/** Reaps a finished build job on the UI thread. */

void
ui_model::poll_build_job ()
{
  if (build_job == nullptr) {
    return;
  }
  std::string message;
  std::optional<wsl::rsc::xmake::xmake_file_api::project_info> info;
  bool finished = false;
  {
    std::lock_guard<std::mutex> lock (build_job->mutex);
    if (!build_job->done) {
      return;
    }
    finished = true;
    message = std::move (build_job->message);
    info = std::move (build_job->info);
  }
  if (finished) {
    build_job.reset ();
    if (info.has_value ()) {
      build_info = std::move (info);
      build_target_idx = -1;
      build_test_idx = -1;
    }
    if (!message.empty ()) {
      append_console (message);
    }
    dirty = true;
  }
}

std::string
ui_model::build_extra_args (const std::string &wsl_path, const std::string &kit)
{
  std::string extra;
  if (!wsl_path.empty ()) {
    extra = "--weasel_dir=" + wsl_path;
  }
  if (kit != "Default") {
    if (!extra.empty ()) {
      extra += " ";
    }
    std::string mode = kit;
    std::transform (
        mode.begin (), mode.end (), mode.begin (),
        [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
    extra += "-m " + mode;
  }
  return extra;
}

void
ui_model::start_build_refresh ()
{
  auto project = svc.runtime->resource_manager ().current_project ();
  if (!project) {
    append_console ("No project loaded.");
    return;
  }
  if (build_busy ()) {
    append_console ("A build job is already running.");
    return;
  }
  const std::string root = project->root_path;
  const std::string wsl_path = svc.editor->wsl_library_path ();
  wsl::rsc::xmake::xmake_file_api api = build_api;
  auto *res_mgr = &svc.runtime->resource_manager ();
  start_build_job (
      "Refreshing project info",
      [api, res_mgr, root,
       wsl_path] (std::shared_ptr<build_job_state> job) mutable {
        const std::string build_dir = root + "/build";
        if (api.query_and_configure (root, build_dir,
                                     build_extra_args (wsl_path, "Default"),
                                     *res_mgr)) {
          auto parsed = api.parse_replies (root, build_dir);
          std::lock_guard<std::mutex> lock (job->mutex);
          job->info = std::move (parsed);
          job->message = "Project info refreshed.";
          job->done = true;
        } else {
          std::lock_guard<std::mutex> lock (job->mutex);
          job->message = "Project configure failed; see log.";
          job->done = true;
        }
      });
}

void
ui_model::start_build_configure ()
{
  static const char *const kits[] = { "Default", "Debug", "Release" };
  auto project = svc.runtime->resource_manager ().current_project ();
  if (!project) {
    append_console ("No project loaded.");
    return;
  }
  if (build_busy ()) {
    append_console ("A build job is already running.");
    return;
  }
  const std::string kit = kits[std::clamp (build_kit_idx, 0, 2)];
  const std::string root = project->root_path;
  const std::string wsl_path = svc.editor->wsl_library_path ();
  wsl::rsc::xmake::xmake_file_api api = build_api;
  auto *res_mgr = &svc.runtime->resource_manager ();
  start_build_job (
      "Configuring project (" + kit + ")",
      [api, res_mgr, root, wsl_path,
       kit] (std::shared_ptr<build_job_state> job) mutable {
        const std::string build_dir = root + "/build";
        if (api.query_and_configure (
                root, build_dir, build_extra_args (wsl_path, kit), *res_mgr)) {
          auto parsed = api.parse_replies (root, build_dir);
          std::lock_guard<std::mutex> lock (job->mutex);
          job->info = std::move (parsed);
          job->message = "Configure finished.";
          job->done = true;
        } else {
          std::lock_guard<std::mutex> lock (job->mutex);
          job->message = "Configure failed; see log.";
          job->done = true;
        }
      });
}

void
ui_model::start_build_target ()
{
  auto project = svc.runtime->resource_manager ().current_project ();
  if (!project) {
    append_console ("No project loaded.");
    return;
  }
  if (build_busy ()) {
    append_console ("A build job is already running.");
    return;
  }
  if (!build_info.has_value () || build_target_idx < 0
      || build_target_idx >= static_cast<int> (build_info->targets.size ())) {
    append_console ("Select a build target first (Build tab, Refresh).");
    return;
  }
  const std::string root = project->root_path;
  const std::string target
      = build_info->targets[static_cast<std::size_t> (build_target_idx)].name;
  start_build_job ("Building target " + target,
                   [root, target] (std::shared_ptr<build_job_state> job) {
                     const std::string command
                         = "cd \"" + root + "\" && xmake -y " + target;
                     wsl::log::editor ()->debug ("Executing {}", command);
                     const int rc = std::system (command.c_str ());
                     std::lock_guard<std::mutex> lock (job->mutex);
                     job->message
                         = rc == 0 ? "Build finished: " + target
                                   : "Build failed: " + target + " (see log)";
                     job->done = true;
                   });
}

void
ui_model::start_build_test ()
{
  auto project = svc.runtime->resource_manager ().current_project ();
  if (!project) {
    append_console ("No project loaded.");
    return;
  }
  if (build_busy ()) {
    append_console ("A build job is already running.");
    return;
  }
  if (!build_info.has_value () || build_test_idx < 0
      || build_test_idx >= static_cast<int> (build_info->tests.size ())) {
    append_console ("Select a test first (Build tab, Refresh).");
    return;
  }
  const std::string root = project->root_path;
  const std::string test
      = build_info->tests[static_cast<std::size_t> (build_test_idx)].name;
  start_build_job ("Running test " + test,
                   [root, test] (std::shared_ptr<build_job_state> job) {
                     const std::string command = "cd \"" + root
                                                 + "\" && xmake test -y \""
                                                 + test + "\"";
                     wsl::log::editor ()->debug ("Executing {}", command);
                     const int rc = std::system (command.c_str ());
                     std::lock_guard<std::mutex> lock (job->mutex);
                     job->message = rc == 0
                                        ? "Test finished: " + test
                                        : "Test failed: " + test + " (see log)";
                     job->done = true;
                   });
}

bool
ui_model::find_inspector_target (entt::id_type comp_type, inspector_target &out)
{
  auto *scene = svc.runtime->scene_manager ().get_active ();
  if (scene == nullptr) {
    return false;
  }
  out.reg = &scene->get_registry ();
  entt::registry &reg = *out.reg;

  if (selected_singleton.has_value ()) {
    const auto *desc
        = svc.runtime->singleton_registry ().find (*selected_singleton);
    if (desc == nullptr || desc->get_ptr == nullptr) {
      return false;
    }
    void *ptr = desc->get_ptr (reg);
    if (ptr == nullptr) {
      return false;
    }
    out.instance = entt::resolve (desc->type_id).from_void (ptr);
  } else {
    out.entity = svc.editor->selected_entity ();
    if (out.entity == entt::null || !reg.valid (out.entity)) {
      return false;
    }
    void *ptr = nullptr;
    auto &comp_reg = svc.runtime->component_registry ();
    for (auto &&[internal_id, storage] : reg.storage ()) {
      if (comp_reg.to_stable_id (internal_id) == comp_type
          && storage.contains (out.entity)) {
        ptr = storage.value (out.entity);
        break;
      }
    }
    if (ptr == nullptr) {
      return false;
    }
    out.instance = entt::resolve (comp_type).from_void (ptr);
  }
  if (!out.instance) {
    return false;
  }
  // The row may be stale (selection changed while the input was focused):
  // never apply a field to an object of a different type.
  if (out.instance.type ().info ().hash () != comp_type) {
    return false;
  }
  return true;
}

/**
 * Writes one edited inspector field back to the live object.
 *
 * :param comp_type: Stable component/singleton type id (``fld-`` prefix).
 * :param field_id: Reflected field id within the type.
 * :param text: Raw editor text.
 * :return: ``true`` when the value was accepted.
 */

bool
ui_model::commit_inspector_field (entt::id_type comp_type,
                                  entt::id_type field_id,
                                  const std::string &text)
{
  inspector_target target;
  if (!find_inspector_target (comp_type, target)) {
    return false;
  }
  entt::registry &reg = *target.reg;
  entt::meta_any instance = target.instance;
  const entt::entity target_entity = target.entity;

  const entt::meta_type meta = instance.type ();
  entt::meta_data data = meta.data (field_id);
  if (!data) {
    return false;
  }
  const entt::meta_type type = data.type ();
  if (!type) {
    return false;
  }

  // Transform rotation edits in Euler degrees, like the ImGui inspector.
  const entt::id_type k_rotation
      = static_cast<entt::id_type> (entt::hashed_string{ "rotation" });
  if (!selected_singleton.has_value ()
      && comp_type == entt::type_hash<wsl::comp::transform>::value ()
      && field_id == k_rotation && type == entt::resolve<wsl::math::quatf> ()) {
    float deg[3]{};
    if (!parse_float_list (text, 3, deg)) {
      return false;
    }
    const glm::quat quat
        = glm::quat (glm::radians (glm::vec3 (deg[0], deg[1], deg[2])));
    return static_cast<bool> (data.set (instance, wsl::math::quatf{ quat }));
  }

  // Hierarchy links go through the validated re-parent helper so the
  // sibling chains cannot be corrupted by raw edits.
  const entt::id_type k_hierarchy = static_cast<entt::id_type> (
      entt::type_hash<wsl::comp::hierarchy>::value ());
  const entt::id_type k_parent
      = static_cast<entt::id_type> (entt::hashed_string{ "parent" });
  if (!selected_singleton.has_value () && comp_type == k_hierarchy
      && field_id == k_parent) {
    unsigned long raw = 0;
    if (!parse_scalar (text, raw)) {
      return false;
    }
    const entt::entity parent = static_cast<entt::entity> (raw);
    if (parent != entt::null && !reg.valid (parent)) {
      return false;
    }
    if (!reg.all_of<wsl::comp::hierarchy> (target_entity)) {
      reg.emplace<wsl::comp::hierarchy> (target_entity);
    }
    set_entity_parent (reg, target_entity, parent);
    return true;
  }

  if (type == entt::resolve<bool> ()) {
    bool value = false;
    if (text == "true" || text == "1") {
      value = true;
    } else if (text == "false" || text == "0") {
      value = false;
    } else {
      return false;
    }
    return static_cast<bool> (data.set (instance, value));
  }
  if (type == entt::resolve<int> ()) {
    int value = 0;
    if (!parse_scalar (text, value)) {
      return false;
    }
    return static_cast<bool> (data.set (instance, value));
  }
  if (type == entt::resolve<std::uint32_t> ()) {
    unsigned long value = 0;
    if (!parse_scalar (text, value)) {
      return false;
    }
    return static_cast<bool> (
        data.set (instance, static_cast<std::uint32_t> (value)));
  }
  if (type == entt::resolve<float> ()) {
    float value = 0.0F;
    if (!parse_scalar (text, value)) {
      return false;
    }
    return static_cast<bool> (data.set (instance, value));
  }
  if (type == entt::resolve<double> ()) {
    double value = 0.0;
    if (!parse_scalar (text, value)) {
      return false;
    }
    return static_cast<bool> (data.set (instance, value));
  }
  if (type == entt::resolve<std::string> ()) {
    return static_cast<bool> (data.set (instance, text));
  }
  if (type == entt::resolve<entt::entity> ()) {
    unsigned long value = 0;
    if (!parse_scalar (text, value)) {
      return false;
    }
    return static_cast<bool> (
        data.set (instance, static_cast<entt::entity> (value)));
  }
  if (type == entt::resolve<wsl::math::vec2f> ()) {
    float v[2]{};
    if (!parse_float_list (text, 2, v)) {
      return false;
    }
    return static_cast<bool> (
        data.set (instance, wsl::math::vec2f{ glm::vec2 (v[0], v[1]) }));
  }
  if (type == entt::resolve<wsl::math::vec3f> ()) {
    float v[3]{};
    if (!parse_float_list (text, 3, v)) {
      return false;
    }
    return static_cast<bool> (
        data.set (instance, wsl::math::vec3f{ glm::vec3 (v[0], v[1], v[2]) }));
  }
  if (type == entt::resolve<wsl::math::vec4f> ()) {
    float v[4]{};
    if (!parse_float_list (text, 4, v)) {
      return false;
    }
    return static_cast<bool> (data.set (
        instance, wsl::math::vec4f{ glm::vec4 (v[0], v[1], v[2], v[3]) }));
  }
  if (type == entt::resolve<wsl::math::quatf> ()) {
    float v[4]{};
    if (!parse_float_list (text, 4, v)) {
      return false;
    }
    return static_cast<bool> (data.set (
        instance, wsl::math::quatf{ glm::quat (v[3], v[0], v[1], v[2]) }));
  }
  return false;
}

/**
 * Flips a boolean inspector field (from a ``fldb-`` toggle button).
 */

void
ui_model::toggle_inspector_bool (entt::id_type comp_type,
                                 entt::id_type field_id)
{
  inspector_target target;
  if (!find_inspector_target (comp_type, target)) {
    return;
  }
  entt::meta_any instance = target.instance;
  entt::meta_data data = instance.type ().data (field_id);
  if (!data || data.type () != entt::resolve<bool> ()) {
    return;
  }
  entt::meta_any current = data.get (instance);
  const bool *value = current.try_cast<bool> ();
  if (value == nullptr) {
    return;
  }
  data.set (instance, !*value);
  dirty = true;
  inspector_refresh_forced = true;
}

/**
 * Writes one lane of a vector/quaternion inspector field (from a
 * ``fld-<comp>-<field>-<lane>`` input). Euler lanes of the transform
 * rotation are handled in degrees like whole-value commits.
 */

bool
ui_model::commit_inspector_lane (entt::id_type comp_type,
                                 entt::id_type field_id, int lane,
                                 const std::string &text)
{
  inspector_target target;
  if (!find_inspector_target (comp_type, target)) {
    return false;
  }
  entt::meta_any instance = target.instance;
  entt::meta_data data = instance.type ().data (field_id);
  if (!data) {
    return false;
  }
  const entt::meta_type type = data.type ();
  if (!type) {
    return false;
  }

  float scalar = 0.0F;
  if (!parse_scalar (text, scalar)) {
    return false;
  }

  const entt::id_type k_transform = static_cast<entt::id_type> (
      entt::type_hash<wsl::comp::transform>::value ());
  const entt::id_type k_rotation
      = static_cast<entt::id_type> (entt::hashed_string{ "rotation" });
  if (!selected_singleton.has_value () && comp_type == k_transform
      && field_id == k_rotation && type == entt::resolve<wsl::math::quatf> ()) {
    if (lane < 0 || lane > 2) {
      return false;
    }
    entt::meta_any current = data.get (instance);
    const auto *q = current.try_cast<wsl::math::quatf> ();
    if (q == nullptr) {
      return false;
    }
    glm::vec3 euler
        = glm::degrees (glm::eulerAngles (static_cast<glm::quat> (*q)));
    euler[lane] = scalar;
    const glm::quat quat = glm::quat (glm::radians (euler));
    return static_cast<bool> (data.set (instance, wsl::math::quatf{ quat }));
  }

  if (type == entt::resolve<wsl::math::vec2f> ()) {
    if (lane < 0 || lane > 1) {
      return false;
    }
    entt::meta_any current = data.get (instance);
    const auto *v = current.try_cast<wsl::math::vec2f> ();
    if (v == nullptr) {
      return false;
    }
    glm::vec2 updated (*v);
    updated[lane] = scalar;
    return static_cast<bool> (data.set (instance, wsl::math::vec2f{ updated }));
  }
  if (type == entt::resolve<wsl::math::vec3f> ()) {
    if (lane < 0 || lane > 2) {
      return false;
    }
    entt::meta_any current = data.get (instance);
    const auto *v = current.try_cast<wsl::math::vec3f> ();
    if (v == nullptr) {
      return false;
    }
    glm::vec3 updated (*v);
    updated[lane] = scalar;
    return static_cast<bool> (data.set (instance, wsl::math::vec3f{ updated }));
  }
  if (type == entt::resolve<wsl::math::vec4f> ()) {
    if (lane < 0 || lane > 3) {
      return false;
    }
    entt::meta_any current = data.get (instance);
    const auto *v = current.try_cast<wsl::math::vec4f> ();
    if (v == nullptr) {
      return false;
    }
    glm::vec4 updated (*v);
    updated[lane] = scalar;
    return static_cast<bool> (data.set (instance, wsl::math::vec4f{ updated }));
  }
  if (type == entt::resolve<wsl::math::quatf> ()) {
    if (lane < 0 || lane > 3) {
      return false;
    }
    entt::meta_any current = data.get (instance);
    const auto *v = current.try_cast<wsl::math::quatf> ();
    if (v == nullptr) {
      return false;
    }
    glm::quat updated (*v);
    updated[lane] = scalar;
    return static_cast<bool> (data.set (instance, wsl::math::quatf{ updated }));
  }
  return false;
}

void
ui_model::append_console (const std::string &line)
{
  std::istringstream stream (line);
  std::string segment;
  while (std::getline (stream, segment)) {
    console_lines.push_back (segment);
  }
  if (line.empty ()) {
    console_lines.emplace_back ();
  }
  constexpr std::size_t kMaxConsoleLines = 400;
  if (console_lines.size () > kMaxConsoleLines) {
    console_lines.erase (console_lines.begin (),
                         console_lines.begin ()
                             + static_cast<std::ptrdiff_t> (
                                 console_lines.size () - kMaxConsoleLines));
  }
}

void
ui_model::open_project (const std::string &path)
{
  if (svc.load_project && svc.load_project (path)) {
    add_recent_project (path);
    show_welcome = false;
    selected_singleton.reset ();
    collapsed_nodes.clear ();
    drop_target_id.clear ();
    append_console ("Loading project: " + path);
  } else {
    append_console ("Failed to load project: " + path);
  }
  dirty = true;
}

void
ui_model::reload_active_scene ()
{
  auto *active = svc.runtime->scene_manager ().get_active ();
  if (active == nullptr) {
    return;
  }
  for (const auto &info : svc.runtime->resource_manager ().list_scenes ()) {
    if (!info.is_prefab && info.name == active->get_name ()) {
      wsl::rsc::scene_id const sid{ info.id };
      svc.runtime->resource_manager ().unload (sid);
      static_cast<void> (svc.runtime->resource_manager ().load (sid));
      break;
    }
  }
  dirty = true;
}

void
ui_model::load_recent_projects ()
{
  recent_projects.clear ();
  std::ifstream file ("recent_projects.txt");
  std::string line;
  while (std::getline (file, line)) {
    if (!line.empty ()) {
      recent_projects.push_back (line);
    }
  }
  if (recent_projects.size () > 10) {
    recent_projects.resize (10);
  }
}

void
ui_model::add_recent_project (const std::string &path)
{
  recent_projects.erase (
      std::remove (recent_projects.begin (), recent_projects.end (), path),
      recent_projects.end ());
  recent_projects.insert (recent_projects.begin (), path);
  if (recent_projects.size () > 10) {
    recent_projects.resize (10);
  }
  save_recent_projects ();
}

void
ui_model::save_recent_projects () const
{
  std::ofstream file ("recent_projects.txt");
  if (!file) {
    return;
  }
  for (const std::string &path : recent_projects) {
    file << path << "\n";
  }
}

void
ui_model::request_exit ()
{
  SDL_Event quit_event{};
  quit_event.type = SDL_EVENT_QUIT;
  SDL_PushEvent (&quit_event);
}

// ------------------------------------------------------------- dialogs

void
ui_model::dialog_callback (void *userdata, const char *const *filelist,
                           int /*filter*/)
{
  auto *self = static_cast<ui_model *> (userdata);
  if (self == nullptr) {
    return;
  }
  self->dialog_finished = true;
  if ((filelist != nullptr) && (filelist[0] != nullptr)) {
    self->dialog_result = std::string (filelist[0]);
  } else {
    self->dialog_result.reset ();
  }
}

void
ui_model::show_project_dialog ()
{
  if (waiting_for_dialog) {
    return;
  }
  SDL_DialogFileFilter filters[]
      = { { "WSL Project", "json" }, { "All Files", "*" } };
  pending_dialog = dialog_mode::open_project;
  waiting_for_dialog = true;
  dialog_finished = false;
  dialog_result.reset ();
  SDL_ShowOpenFileDialog (&ui_model::dialog_callback, this,
                          svc.window->handler (), filters,
                          SDL_arraysize (filters), nullptr, false);
}

void
ui_model::show_load_scene_dialog ()
{
  if (waiting_for_dialog) {
    return;
  }
  SDL_DialogFileFilter filters[] = { { "Scene files", "scene;json" },
                                     { "Prefab files", "prefab" },
                                     { "All files", "*" } };
  pending_dialog = dialog_mode::load_scene;
  waiting_for_dialog = true;
  dialog_finished = false;
  dialog_result.reset ();
  SDL_ShowOpenFileDialog (&ui_model::dialog_callback, this,
                          svc.window->handler (), filters,
                          SDL_arraysize (filters), nullptr, false);
}

void
ui_model::show_save_scene_dialog ()
{
  if (waiting_for_dialog) {
    return;
  }
  SDL_DialogFileFilter filters[] = { { "Scene files", "scene;json" },
                                     { "Prefab files", "prefab" },
                                     { "All files", "*" } };
  pending_dialog = dialog_mode::save_scene;
  waiting_for_dialog = true;
  dialog_finished = false;
  dialog_result.reset ();
  SDL_ShowSaveFileDialog (&ui_model::dialog_callback, this,
                          svc.window->handler (), filters,
                          SDL_arraysize (filters), nullptr);
}

void
ui_model::show_select_folder_dialog ()
{
  if (waiting_for_dialog) {
    return;
  }
  pending_dialog = dialog_mode::select_folder;
  waiting_for_dialog = true;
  dialog_finished = false;
  dialog_result.reset ();
  SDL_ShowOpenFolderDialog (&ui_model::dialog_callback, this,
                            svc.window->handler (), nullptr, false);
}

void
ui_model::run_console_command (const std::string &command)
{
  append_console ("> " + command);
  if (svc.execute_command) {
    append_console (svc.execute_command (command));
  } else {
    append_console ("no command handler");
  }
  dirty = true;
}

// ------------------------------------------------------------ camera
void
ui_model::create_project (const std::string &name, const std::string &folder)
{
  std::string root = folder;
  if (new_project_create_folder) {
    root += "/";
    root += name;
  }

  wsl::rsc::project proj;
  proj.name = name;
  proj.author = "Unknown";
  proj.root_path = root;
  proj.systems_path = "src/systems";
  proj.components_path = "src/components";
  proj.singletons_path = "src/singletons";
  proj.ui_layouts_path = "src/ui";
  proj.scenes_path = "rsc/scenes";
  proj.models_path = "rsc/models";
  proj.images_path = "rsc/textures";
  proj.cubemaps_path = "rsc/textures/cubemaps";
  proj.audio_path = "rsc/audio";
  proj.fonts_path = "rsc/fonts";
  proj.materials_path = "rsc/materials";
  proj.default_scene_path = "";

  svc.editor->selected_entity (entt::null);
  selected_singleton.reset ();
  collapsed_nodes.clear ();
  drop_target_id.clear ();
  svc.editor->reset_editor_camera ();
  static_cast<void> (svc.runtime->resource_manager ().new_project (proj));

  const std::string project_file = (std::filesystem::path (proj.root_path)
                                    / wsl::rsc::project_loader::manifest_file)
                                       .string ();
  add_recent_project (project_file);

  show_new_project = false;
  show_welcome = false;
  append_console ("Created project: " + project_file);
  dirty = true;
}
} // namespace editor_rml
