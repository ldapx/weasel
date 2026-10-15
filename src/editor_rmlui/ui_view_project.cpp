#include "ui_view.hpp"

#include "ui_util.hpp"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/Traits.h>

#include "wsl/rsc/project.hpp"
#include "wsl/rsc/resource_manager.hpp"
#include "wsl/rsc/scene.hpp"
#include "wsl/rsc/scene_manager.hpp"

#include "wsl/comp/singl/editor_context.hpp"
#include "wsl/comp/singl/runtime_context.hpp"
#include "wsl/rsc/scene.hpp"
#include "wsl/rsc/scene_manager.hpp"
namespace editor_rml
{

void
ui_view::refresh_files ()
{
  Rml::Element *list = document_->GetElementById ("files");
  if (list == nullptr) {
    return;
  }

  auto project = model_.svc.runtime->resource_manager ().current_project ();
  if (!project) {
    model_.files_project.clear ();
    set_rml_if_changed (list,
                        "<div class='empty-message'>No project loaded.</div>",
                        files_html_cache);
    return;
  }
  if (model_.files_project != project->root_path) {
    model_.rescan_files ();
  }

  std::string active_name;
  if (auto *scene = model_.svc.runtime->scene_manager ().get_active ();
      scene != nullptr) {
    active_name = scene->get_name ();
  }

  std::string html;
  html += "<div class='list-head'>Scenes (";
  html += std::to_string (model_.file_scenes.size ());
  html += ")</div>";
  if (model_.file_scenes.empty ()) {
    html += "<div class='empty-message'>No scenes found.</div>";
  }
  std::size_t row = 0;
  for (const auto &info : model_.file_scenes) {
    html += "<div id='scn-";
    html += std::to_string (info.id);
    html += "' class='item";
    html += (row % 2 == 0) ? " even" : " odd";
    if (info.name == active_name) {
      html += " selected";
    }
    html += "'>";
    html += html_escape (info.name);
    if (info.is_prefab) {
      html += " <span class='dim'>prefab</span>";
    }
    html += "</div>";
    ++row;
  }

  html += "<div class='list-head'>Scripts (";
  html += std::to_string (model_.file_entries.size ());
  html += ")</div>";
  if (model_.file_entries.empty ()) {
    html += "<div class='empty-message'>No script files found.</div>";
  }
  for (const auto &entry : model_.file_entries) {
    html += "<div class='item static";
    html += (row % 2 == 0) ? " even" : " odd";
    html += "'>";
    html += html_escape (entry.rel);
    html += " <span class='dim'>";
    html += html_escape (entry.kind);
    html += "</span></div>";
    ++row;
  }
  set_rml_if_changed (list, html, files_html_cache);
}

/**
 * Loads the cached scene with the given resource id (from a Files tab
 * ``scn-<id>`` row).
 */

void
ui_view::refresh_build ()
{
  Rml::Element *list = document_->GetElementById ("build-lists");
  if (list == nullptr) {
    return;
  }

  auto project = model_.svc.runtime->resource_manager ().current_project ();
  if (!project) {
    model_.build_project.clear ();
    model_.build_info.reset ();
    set_rml_if_changed (list,
                        "<div class='empty-message'>No project loaded.</div>",
                        build_html_cache);
    return;
  }
  if (model_.build_project != project->root_path) {
    model_.build_project = project->root_path;
    model_.build_info.reset ();
    model_.build_target_idx = -1;
    model_.build_test_idx = -1;
  }

  static const char *const kits[] = { "Default", "Debug", "Release" };
  std::string html;
  if (model_.build_busy ()) {
    html += "<div class='list-head'>Working...</div>";
  }

  html += "<div class='list-head'>Kit</div>";
  for (int i = 0; i < 3; ++i) {
    html += "<div id='kit-";
    html += std::to_string (i);
    html += "' class='item";
    html += (i % 2 == 0) ? " even" : " odd";
    if (i == model_.build_kit_idx) {
      html += " selected";
    }
    html += "'>";
    html += kits[i];
    html += "</div>";
  }

  const std::size_t target_count
      = model_.build_info.has_value () ? model_.build_info->targets.size () : 0;
  html += "<div class='list-head'>Build Targets (";
  html += std::to_string (target_count);
  html += ")</div>";
  if (!model_.build_info.has_value ()) {
    html += "<div class='empty-message'>No project info. Click "
            "Refresh.</div>";
  }
  for (std::size_t i = 0;
       model_.build_info.has_value () && i < model_.build_info->targets.size (); ++i) {
    html += "<div id='btarget-";
    html += std::to_string (i);
    html += "' class='item";
    html += (i % 2 == 0) ? " even" : " odd";
    if (static_cast<int> (i) == model_.build_target_idx) {
      html += " selected";
    }
    html += "'>";
    html += html_escape (model_.build_info->targets[i].name);
    html += "</div>";
  }

  const std::size_t test_count
      = model_.build_info.has_value () ? model_.build_info->tests.size () : 0;
  html += "<div class='list-head'>Tests (";
  html += std::to_string (test_count);
  html += ")</div>";
  for (std::size_t i = 0;
       model_.build_info.has_value () && i < model_.build_info->tests.size (); ++i) {
    html += "<div id='btest-";
    html += std::to_string (i);
    html += "' class='item";
    html += (i % 2 == 0) ? " even" : " odd";
    if (static_cast<int> (i) == model_.build_test_idx) {
      html += " selected";
    }
    html += "'>";
    html += html_escape (model_.build_info->tests[i].name);
    html += "</div>";
  }
  set_rml_if_changed (list, html, build_html_cache);
}

/**
 * Emits a clickable inspector component header with a collapse toggle.
 * Clicks anywhere on the header dispatch as ``cmp-<key>``.
 */
} // namespace editor_rml
