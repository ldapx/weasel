#include "ui_view.hpp"

#include "ui_util.hpp"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/Traits.h>

#include <algorithm>

#include "wsl/comp/hierarchy.hpp"
#include "wsl/rsc/scene.hpp"
#include "wsl/rsc/scene_manager.hpp"
#include "wsl/reg/singleton_registry.hpp"
#include "wsl/sys/core_systems.hpp"
#include "wsl/sys/system.hpp"

#include "wsl/comp/singl/editor_context.hpp"
#include "wsl/comp/singl/runtime_context.hpp"
#include "wsl/rsc/scene.hpp"
#include "wsl/rsc/scene_manager.hpp"
namespace editor_rml
{

void
ui_view::append_entity_subtree (wsl::rsc::scene *scene, entt::registry &reg, entt::entity entity, int depth, std::string &html, std::unordered_set<std::uint32_t> &rendered)
{
  const std::uint32_t raw = static_cast<std::uint32_t> (entity);
  if (!rendered.insert (raw).second || depth > 64) {
    return;
  }
  const bool even = ((rendered.size () - 1) % 2) == 0;
  const entt::entity selected = model_.svc.editor->selected_entity ();
  std::vector<entt::entity> children = model_.hierarchy_children (reg, entity);
  std::sort (children.begin (), children.end (),
             [&scene] (entt::entity lhs, entt::entity rhs) {
               return scene->get_entity_name (lhs)
                      < scene->get_entity_name (rhs);
             });

  const bool collapsed = model_.collapsed_nodes.contains (raw) && !children.empty ();

  html += "<div id='ent-";
  html += std::to_string (raw);
  html += "' class='item";
  html += even ? " even" : " odd";
  if (entity == selected) {
    html += " selected";
  }
  if (model_.drop_target_id == ("ent-" + std::to_string (raw))) {
    html += " drop-target";
  }
  html += "' style='margin-left: ";
  html += std::to_string (depth * 14);
  html += "px;'>";
  if (children.empty ()) {
    html += "<span class='toggle leaf'>&#183;</span>";
  } else if (collapsed) {
    html += "<span id='tgl-";
    html += std::to_string (raw);
    html += "' class='toggle'>+</span>";
  } else {
    html += "<span id='tgl-";
    html += std::to_string (raw);
    html += "' class='toggle'>-</span>";
  }
  html += "<span class='tlabel'>";
  html += html_escape (scene->get_entity_name (entity));
  html += "</span><span class='eid'>#";
  html += std::to_string (raw);
  html += "</span></div>";

  if (collapsed) {
    return;
  }
  for (entt::entity child : children) {
    append_entity_subtree (scene, reg, child, depth + 1, html, rendered);
  }
}

void
ui_view::refresh_hierarchy ()
{
  Rml::Element *list = document_->GetElementById ("hierarchy");
  if (list == nullptr) {
    return;
  }

  auto *scene = model_.svc.runtime->scene_manager ().get_active ();
  if (scene == nullptr) {
    set_rml_if_changed (
        list,
        "<div class='empty-message'>No active scene. Load a project/scene "
        "via the console (e.g. 'project load &lt;path&gt;').</div>",
        hierarchy_html_cache);
    return;
  }

  entt::registry &reg = scene->get_registry ();

  // Drop cached collapse state for entities that no longer exist.
  for (auto it = model_.collapsed_nodes.begin (); it != model_.collapsed_nodes.end ();) {
    if (!reg.valid (static_cast<entt::entity> (*it))) {
      it = model_.collapsed_nodes.erase (it);
    } else {
      ++it;
    }
  }

  // Roots are entities without a valid hierarchy parent, sorted by name.
  std::vector<entt::entity> roots;
  for (auto entity : reg.view<entt::entity> ()) {
    const auto *h = reg.try_get<wsl::comp::hierarchy> (entity);
    if (h == nullptr || h->parent == entt::null || h->parent == entity
        || !reg.valid (h->parent)) {
      roots.push_back (entity);
    }
  }
  std::sort (roots.begin (), roots.end (),
             [&scene] (entt::entity lhs, entt::entity rhs) {
               return scene->get_entity_name (lhs)
                      < scene->get_entity_name (rhs);
             });

  std::string html;
  const std::size_t entity_count = reg.view<entt::entity> ().size ();
  if (roots.empty () && entity_count == 0) {
    html = "<div class='empty-message'>Scene has no entities.</div>";
  } else {
    html += "<div class='list-head'>Entities (";
    html += std::to_string (entity_count);
    html += ")</div>";
  }
  std::unordered_set<std::uint32_t> rendered;
  for (entt::entity root : roots) {
    append_entity_subtree (scene, reg, root, 0, html, rendered);
  }
  // Orphans (e.g. parent-pointer cycles): render anything not reached.
  if (rendered.size () != reg.view<entt::entity> ().size ()) {
    std::vector<entt::entity> orphans;
    for (auto entity : reg.view<entt::entity> ()) {
      if (!rendered.contains (static_cast<std::uint32_t> (entity))) {
        orphans.push_back (entity);
      }
    }
    std::sort (orphans.begin (), orphans.end (),
               [&scene] (entt::entity lhs, entt::entity rhs) {
                 return scene->get_entity_name (lhs)
                        < scene->get_entity_name (rhs);
               });
    for (entt::entity orphan : orphans) {
      append_entity_subtree (scene, reg, orphan, 0, html, rendered);
    }
  }
  if (html.empty () || rendered.empty ()) {
    html = "<div class='empty-message'>Scene has no entities.</div>";
  }
  set_rml_if_changed (list, html, hierarchy_html_cache);
}

void
ui_view::refresh_singletons ()
{
  Rml::Element *list = document_->GetElementById ("singletons");
  if (list == nullptr) {
    return;
  }

  auto *scene = model_.svc.runtime->scene_manager ().get_active ();
  if (scene == nullptr) {
    set_rml_if_changed (list,
                        "<div class='empty-message'>No active "
                        "scene.</div>",
                        singleton_html_cache);
    return;
  }

  entt::registry &reg = scene->get_registry ();
  if (model_.selected_singleton.has_value ()
      && model_.svc.runtime->singleton_registry ().find (*model_.selected_singleton)
             == nullptr) {
    model_.selected_singleton.reset ();
  }
  std::string html;
  std::size_t sing_count = 0;

  for (const auto *desc : model_.svc.runtime->singleton_registry ().ordered ()) {
    if (desc == nullptr || desc->contains == nullptr) {
      continue;
    }
    const bool present = desc->contains (reg);
    // Core singletons are always listed; scene singletons only when added.
    if (!desc->core && !present) {
      continue;
    }
    const bool selected = model_.selected_singleton.has_value ()
                          && *model_.selected_singleton == desc->type_id;
    html += "<div id='sing-";
    html += std::to_string (desc->type_id);
    html += "' class='item";
    html += (sing_count % 2 == 0) ? " even" : " odd";
    if (desc->core) {
      html += " core-sing";
    }
    if (selected) {
      html += " selected";
    }
    if (!present) {
      html += " dim";
    }
    html += "'>";
    html += html_escape (desc->display_name);
    html += "</div>";
    ++sing_count;
  }

  if (html.empty ()) {
    html = "<div class='empty-message'>No singletons.</div>";
  }
  set_rml_if_changed (list, html, singleton_html_cache);

  if (Rml::Element *title = document_->GetElementById ("singletons-title");
      title != nullptr) {
    set_rml_if_changed (title,
                        "Singletons (" + std::to_string (sing_count) + ")",
                        singleton_title_cache);
  }
}

void
ui_view::refresh_systems ()
{
  Rml::Element *list = document_->GetElementById ("systems");
  if (list == nullptr) {
    return;
  }

  std::string html;
  std::size_t row = 0;
  auto append
      = [&html, &row] (const wsl::sys::ecs_system *system, const char *kind) {
          if (system == nullptr) {
            return;
          }
          html += "<div class='item";
          html += (row % 2 == 0) ? " even" : " odd";
          html += "'>";
          ++row;
          html += html_escape (system->get_name ());
          html += " <span class='dim'>";
          html += kind;
          if (!system->get_stage ().empty ()) {
            html += " - ";
            html += html_escape (system->get_stage ());
          }
          html += "</span></div>";
        };

  if (auto &core_ptr = model_.svc.runtime->core_systems (); core_ptr != nullptr) {
    for (wsl::sys::ecs_system *system : core_ptr->to_vec ()) {
      append (system, "core");
    }
  }
  if (auto *scene = model_.svc.runtime->scene_manager ().get_active ();
      scene != nullptr) {
    for (const auto &system : scene->systems) {
      append (system.get (), "scene");
    }
  }

  if (html.empty ()) {
    html = "<div class='empty-message'>No systems registered.</div>";
  }
  set_rml_if_changed (list, html, systems_html_cache);
}

/**
 * Rescans the current project for loadable scenes and script/source
 * files. Results are cached until the project changes or the user
 * presses Refresh.
 */
} // namespace editor_rml
