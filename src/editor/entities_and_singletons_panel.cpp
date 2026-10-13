#include "entities_and_singletons_panel.hpp"

#include "comp/world_transform.hpp"
#include "editor/ecs_inspector_utils.hpp"
#include "wsl/rsc/project.hpp"
#include "wsl/rsc/resource_ids.hpp"
#include "wsl/reg/singleton_registry.hpp"
#include "wsl/comp/hierarchy.hpp"
#include "wsl/comp/prefab_instance.hpp"
#include "wsl/comp/singl/editor_context.hpp"
#include "renderer_imgui.hpp"
#include "wsl/comp/singl/runtime_context.hpp"
#include "wsl/comp/transform.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <entt/entity/entity.hpp>
#include <entt/entity/fwd.hpp>
#include <filesystem>
#include <imgui.h>
#include <imsearch.h>
#include <memory>
#include <string>

using namespace entt::literals;

namespace editor
{

static bool
draw_singleton_entry (ecs_selection &selection,
                      wsl::comp::singl::editor_context *editor_ctx,
                      const wsl::reg::singleton_registry::descriptor &desc,
                      bool is_core = false)
{
  const bool selected = selection.kind == selection_kind::singleton
                        && selection.singleton_type == desc.type_id;

  ImGui::PushID ((int)desc.type_id);

  if (is_core) {
    ImGui::PushStyleColor (
        ImGuiCol_Text, editor_ctx->get_imgui_renderer ()->get_theme ().primary);
  }
  const bool clicked = ImGui::Selectable (desc.display_name.c_str (), selected);
  if (is_core) {
    ImGui::PopStyleColor ();
  }

  ImGui::PopID ();

  if (clicked) {
    selection.select_singleton (desc.type_id);
  }

  return clicked;
}

entities_and_singletons_panel::entities_and_singletons_panel (
    wsl::comp::singl::runtime_context *runtime_ctx,
    wsl::comp::singl::editor_context *editor_ctx, ecs_selection &selection)
    : m_runtime_ctx (runtime_ctx), m_editor_ctx (editor_ctx),
      m_selection (selection)
{
}

namespace
{
void
draw_hsplitter (const char *id, float &top_height, float thickness = 6.0F)
{
  ImGui::InvisibleButton (id, ImVec2 (-1, thickness));

  // Nice cursor while hovering
  if (ImGui::IsItemHovered ()) {
    ImGui::SetMouseCursor (ImGuiMouseCursor_ResizeNS);
  }

  if (ImGui::IsItemActive ()) {
    top_height += ImGui::GetIO ().MouseDelta.y;
  }

  // Draw the splitter bar
  ImU32 col = ImGui::GetColorU32 (ImGuiCol_Separator);
  if (ImGui::IsItemHovered () || ImGui::IsItemActive ()) {
    col = ImGui::GetColorU32 (ImGuiCol_SeparatorHovered);
  }

  ImVec2 const min = ImGui::GetItemRectMin ();
  ImVec2 const max = ImGui::GetItemRectMax ();
  ImGui::GetWindowDrawList ()->AddRectFilled (min, max, col);
}
} // namespace

void
entities_and_singletons_panel::draw ()
{
  if (ImGui::Begin ("Entities and Singletons", nullptr,
                    ImGuiWindowFlags_NoScrollbar
                        | ImGuiWindowFlags_NoScrollWithMouse)) {
    wsl::rsc::scene *scene = m_runtime_ctx->scene_manager ().get_active ();
    const bool has_scene = (scene != nullptr);
    entt::registry *registry = has_scene ? &scene->get_registry () : nullptr;

    ImGui::AlignTextToFramePadding ();
    ImGui::PushFont (m_editor_ctx->get_imgui_renderer ()->get_fonts ().bold);
    ImGui::TextUnformatted ("Entities");
    ImGui::PopFont ();
    ImGui::SameLine ();

    if (!has_scene) {
      ImGui::BeginDisabled ();
    }

    if (ImGui::Button ("+##add_entity", ImVec2 (ImGui::GetFrameHeight (),
                                                ImGui::GetFrameHeight ()))) {
      entt::entity const e = registry->create ();

      if (m_show_hierarchy) {
        registry->emplace<wsl::comp::transform> (e);
        registry->emplace<wsl::comp::world_transform> (e);
        registry->emplace<wsl::comp::hierarchy> (e);
      }

      m_selection.select_entity (e);
    }

    if (ImGui::IsItemHovered ()) {
      ImGui::SetTooltip ("Create entity");
    }

    ImGui::SameLine ();

    const bool can_delete = has_scene
                            && m_selection.kind == selection_kind::entity
                            && registry->valid (m_selection.selected_entity);

    if (!can_delete) {
      ImGui::BeginDisabled ();
    }

    if (ImGui::Button ("-##delete_entity", ImVec2 (ImGui::GetFrameHeight (),
                                                   ImGui::GetFrameHeight ()))) {
      scene->remove_entity_name (m_selection.selected_entity);
      registry->destroy (m_selection.selected_entity);
      m_selection = {};
    }

    if (ImGui::IsItemHovered ()) {
      ImGui::SetTooltip ("Delete selected entity");
    }

    if (!can_delete) {
      ImGui::EndDisabled ();
    }

    ImGui::SameLine ();
    ImGui::Checkbox ("Hierarchy", &m_show_hierarchy);

    if (!has_scene) {
      ImGui::EndDisabled ();
    }

    ImGui::Separator ();

    // ===== SPLIT LAYOUT CALCULATION =====
    const float total_avail_h = ImGui::GetContentRegionAvail ().y;
    const float splitter_h = 6.0F;
    const float title_h = ImGui::GetTextLineHeightWithSpacing ();
    const float search_bar_h
        = ImGui::GetFrameHeightWithSpacing ()
          + 4.0F; // Approx height of search bar + internal padding

    // Overhead: 2 search bars + 1 title (Singletons) + 1 splitter
    float const total_overhead = (search_bar_h * 2.0F) + title_h + splitter_h;
    float const usable_h = std::max (0.0F, total_avail_h - total_overhead);

    // Initial distribution: 1/2 each
    if (m_entities_height < 0.0F && usable_h > 0.0F) {
      m_entities_height = usable_h / 2.0F;
    }

    // Dynamic clamping to ensure everything fits.
    //
    // The upper bound is "leave 40px for the splitter and the Singletons
    // pane". In a short panel that bound falls below the 40px minimum, which
    // inverts the clamp range -- and std::clamp asserts on !(hi < lo) in debug
    // builds, so a small window aborted the editor outright. The minimum has to
    // win: when there is no room for both, keep the 40px floor and let the
    // child overflow (ImGui clips it) rather than corrupting the range.
    m_entities_height = std::clamp (m_entities_height, 40.0F,
                                    std::max (40.0F, usable_h - 40.0F));

    // =========================
    // 1. ENTITIES
    // =========================
    ImGui::PushID ("EntitiesSearchID");
    if (ImSearch::BeginSearch ()) {
      ImSearch::SearchBar ("Search");

      ImGui::BeginChild ("EntitiesRegion", ImVec2 (0, m_entities_height), 1);
      if (m_runtime_ctx->resource_manager ().current_project ()) {
        if (has_scene) {
          draw_entity_list ();
        } else {
          ImGui::TextDisabled ("No active scene");
        }
      } else {
        draw_centered_icon (
            m_editor_ctx, m_editor_ctx->icon_entity (), 64.0F,
            "Entities are boxes of components,\nAdd an entity to the current "
            "scene\nby pressing the \"+\" Button,\nEdit the entity in the "
            "inspector.");
      }
      ImSearch::Submit ();
      ImGui::EndChild ();
      ImSearch::EndSearch ();
    }
    ImGui::PopID ();

    draw_hsplitter ("##entities_splitter", m_entities_height, splitter_h);

    // =========================
    // 2. SINGLETONS (Merged Core + Scene)
    // =========================
    ImGui::AlignTextToFramePadding ();
    ImGui::TextUnformatted ("Singletons");
    ImGui::SameLine ();

    if (!has_scene) {
      ImGui::BeginDisabled ();
    }

    draw_add_singleton_ui ();

    ImGui::SameLine ();
    const auto *selected_desc = m_runtime_ctx->singleton_registry ().find (
        m_selection.singleton_type);
    const bool can_remove_singleton
        = has_scene && m_selection.kind == selection_kind::singleton
          && (selected_desc != nullptr) && !selected_desc->core
          && (selected_desc->contains != nullptr)
          && selected_desc->contains (*registry);

    if (!can_remove_singleton && has_scene) {
      ImGui::BeginDisabled ();
    }

    if (ImGui::Button (
            "-##remove_singleton",
            ImVec2 (ImGui::GetFrameHeight (), ImGui::GetFrameHeight ()))) {
      if (selected_desc->remove != nullptr) {
        selected_desc->remove (*registry);
        m_selection = {};
      }
    }

    if (!can_remove_singleton && has_scene) {
      ImGui::EndDisabled ();
    }

    if (!has_scene) {
      ImGui::EndDisabled ();
    }

    ImGui::PushID ("SingletonsSearchID");
    if (ImSearch::BeginSearch ()) {
      ImSearch::SearchBar ("Search");

      ImGui::BeginChild ("SingletonRegion", ImVec2 (0, 0), 1);

      if (m_runtime_ctx->resource_manager ().current_project ()) {
        // Core singletons first
        auto ordered = m_runtime_ctx->singleton_registry ().ordered ();
        for (const auto *desc : ordered) {
          if ((desc != nullptr) && desc->core) {
            ImSearch::SearchableItem (
                desc->display_name.c_str (), [&, desc] (const char *) {
                  draw_singleton_entry (m_selection, m_editor_ctx, *desc, true);
                });
          }
        }

        // Scene singletons next (ONLY those added to the scene)
        if (has_scene) {
          for (const auto *desc : ordered) {
            if ((desc != nullptr) && !desc->core && (desc->contains != nullptr)
                && desc->contains (*registry)) {
              ImSearch::SearchableItem (
                  desc->display_name.c_str (), [&, desc] (const char *) {
                    draw_singleton_entry (m_selection, m_editor_ctx, *desc,
                                          false);
                  });
            }
          }
        } else {
          ImGui::TextDisabled ("Scene singletons unavailable.");
        }
      } else {
        draw_centered_icon (
            m_editor_ctx, m_editor_ctx->icon_singleton (), 64.0F,
            "Singletons are unique components that exist once in a scene,\n"
            "Manage them here to configure global scene parameters.");
      }

      ImSearch::Submit ();
      ImGui::EndChild ();
      ImSearch::EndSearch ();
    }
    ImGui::PopID ();
  }

  ImGui::End ();
}

void
entities_and_singletons_panel::draw_entity_list ()
{
  if (m_show_hierarchy) {
    draw_entity_tree ();
  } else {
    draw_entity_flat ();
  }

  if (m_request_rename_popup) {
    ImGui::OpenPopup ("Rename Entity");
    m_request_rename_popup = false;
  }

  if (ImGui::BeginPopupModal ("Rename Entity", nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize)) {

    ImGui::InputText ("Name", m_rename_buffer, 128);

    if (ImGui::Button ("OK")) {
      if (m_rename_entity != entt::null) {
        wsl::rsc::scene *scene = m_runtime_ctx->scene_manager ().get_active ();
        scene->set_entity_name (m_rename_entity, std::string (m_rename_buffer));
      }
      m_rename_entity = entt::null;
      ImGui::CloseCurrentPopup ();
    }

    ImGui::SameLine ();

    if (ImGui::Button ("Cancel")) {
      m_rename_entity = entt::null;
      ImGui::CloseCurrentPopup ();
    }

    ImGui::EndPopup ();
  }
}

void
entities_and_singletons_panel::draw_entity_flat ()
{
  auto &scene = *m_runtime_ctx->scene_manager ().get_active ();
  auto view = scene.get_registry ().view<entt::entity> ();

  for (entt::entity const entity : view) {
    const std::string &name = scene.get_entity_name (entity);
    ImSearch::SearchableItem (name.c_str (), [&, entity] (const char *) {
      draw_entity_row (entity, 0);
    });
  }
}

void
entities_and_singletons_panel::draw_entity_tree ()
{
  entt::registry &registry
      = m_runtime_ctx->scene_manager ().get_active ()->get_registry ();

  auto view = registry.view<entt::entity, wsl::comp::hierarchy> ();

  for (entt::entity const e : view) {
    wsl::comp::hierarchy const &h = registry.get<wsl::comp::hierarchy> (e);
    if (h.parent == entt::null) {
      draw_entity_subtree (e, 0);
    }
  }
}

void
entities_and_singletons_panel::draw_entity_subtree (entt::entity entity,
                                                    int depth)
{
  auto &scene = *m_runtime_ctx->scene_manager ().get_active ();
  entt::registry &registry = scene.get_registry ();

  const std::string &name = scene.get_entity_name (entity);

  if (ImSearch::PushSearchable (name.c_str (),
                                [&, entity, depth] (const char *) {
                                  draw_entity_row (entity, depth);
                                  return true; // Always show children
                                })) {
    if (registry.all_of<wsl::comp::hierarchy> (entity)) {
      wsl::comp::hierarchy const &h
          = registry.get<wsl::comp::hierarchy> (entity);
      entt::entity child = h.first;

      while (child != entt::null) {
        draw_entity_subtree (child, depth + 1);
        child = registry.get<wsl::comp::hierarchy> (child).next;
      }
    }
    ImSearch::PopSearchable ();
  }
}

void
entities_and_singletons_panel::draw_entity_row (entt::entity entity, int depth)
{
  wsl::rsc::scene const *scene = m_runtime_ctx->scene_manager ().get_active ();

  const std::string &name = scene->get_entity_name (entity);

  bool const selected = m_selection.kind == selection_kind::entity
                        && m_selection.selected_entity == entity;

  ImGui::PushID ((int)entity);

  if (depth > 0) {
    ImGui::Indent (static_cast<float> (depth) * 14.0F);
  }

  if (ImGui::Selectable ("##entity", selected,
                         ImGuiSelectableFlags_SpanAllColumns)) {
    m_selection.select_entity (entity);
  }

  if (ImGui::BeginPopupContextItem ("entity_context")) {
    if (ImGui::MenuItem ("Rename")) {
      m_rename_entity = entity;
      std::snprintf (m_rename_buffer, 128, "%s", name.c_str ());
      m_request_rename_popup = true;
    }

    if (ImGui::MenuItem ("Duplicate")) {
      duplicate_entity (entity);
    }

    if (ImGui::MenuItem ("Make Prefab")) {
      make_prefab (entity);
    }

    ImGui::Separator ();

    if (ImGui::MenuItem ("Delete")) {
      delete_entity (entity);
    }

    ImGui::EndPopup ();
  }

  ImGui::SameLine (0.0F, 4.0F);
  ImGui::TextUnformatted (name.c_str ());
  ImGui::SameLine ();
  ImGui::TextDisabled (" (%u)", (uint32_t)entity);

  if (depth > 0) {
    ImGui::Unindent (static_cast<float> (depth) * 14.0F);
  }

  ImGui::PopID ();
}

void
entities_and_singletons_panel::draw_core_singleton_list ()
{
  auto ordered = m_runtime_ctx->singleton_registry ().ordered ();
  for (const auto *desc : ordered) {
    if ((desc != nullptr) && desc->core) {
      ImSearch::SearchableItem (
          desc->display_name.c_str (), [&, desc] (const char *) {
            draw_singleton_entry (m_selection, m_editor_ctx, *desc, true);
          });
    }
  }
}

void
entities_and_singletons_panel::draw_scene_singleton_list ()
{
  auto ordered = m_runtime_ctx->singleton_registry ().ordered ();
  for (const auto *desc : ordered) {
    if ((desc != nullptr) && !desc->core) {
      ImSearch::SearchableItem (
          desc->display_name.c_str (), [&, desc] (const char *) {
            draw_singleton_entry (m_selection, m_editor_ctx, *desc, false);
          });
    }
  }
}

void
entities_and_singletons_panel::draw_add_singleton_ui ()
{
  wsl::rsc::scene *scene = m_runtime_ctx->scene_manager ().get_active ();

  if (ImGui::Button ("+##add_singleton", ImVec2 (ImGui::GetFrameHeight (),
                                                 ImGui::GetFrameHeight ()))) {
    ImGui::OpenPopup ("AddSingletonPopup");
  }

  if ((scene != nullptr) && ImGui::BeginPopup ("AddSingletonPopup")) {
    entt::registry &reg = scene->get_registry ();
    ImGui::PushID ("AddSingletonSearchID");
    if (ImSearch::BeginSearch ()) {
      ImSearch::SearchBar ("Search Singletons");
      ImGui::Separator ();

      auto ordered = m_runtime_ctx->singleton_registry ().ordered ();
      for (const auto *desc : ordered) {
        if ((desc != nullptr) && !desc->core && (desc->contains != nullptr)
            && !desc->contains (reg)) {
          ImSearch::SearchableItem (
              desc->display_name.c_str (), [&, desc] (const char *) {
                if (ImGui::MenuItem (desc->display_name.c_str ())) {
                  desc->emplace_default (reg);
                }
              });
        }
      }
      ImSearch::Submit ();
      ImSearch::EndSearch ();
    }
    ImGui::PopID ();
    ImGui::EndPopup ();
  }
}

entt::entity
entities_and_singletons_panel::duplicate_entity (entt::entity original,
                                                 entt::entity parent)
{
  wsl::rsc::scene *scene = m_runtime_ctx->scene_manager ().get_active ();
  if (scene == nullptr) {
    return entt::null;
  }

  // If no parent was specified, use the original's parent
  if (parent == entt::null
      && scene->get_registry ().all_of<wsl::comp::hierarchy> (original)) {
    parent = scene->get_registry ().get<wsl::comp::hierarchy> (original).parent;
  }

  entt::entity const clone = scene->copy_entity (*scene, original, parent);

  // If we have a parent, we need to register ourselves as a sibling if needed
  if (parent != entt::null) {
    entt::registry &reg = scene->get_registry ();
    wsl::comp::hierarchy const &ph = reg.get<wsl::comp::hierarchy> (parent);

    // Find the original in the children list to insert after it
    entt::entity curr = ph.first;
    if (curr == original) {
      entt::entity const next_to_orig
          = reg.get<wsl::comp::hierarchy> (original).next;
      reg.get<wsl::comp::hierarchy> (original).next = clone;
      reg.get<wsl::comp::hierarchy> (clone).next = next_to_orig;
    } else {
      bool found = false;
      while (curr != entt::null) {
        wsl::comp::hierarchy const &ch = reg.get<wsl::comp::hierarchy> (curr);
        if (ch.next == original) {
          entt::entity const next_to_orig
              = reg.get<wsl::comp::hierarchy> (original).next;
          reg.get<wsl::comp::hierarchy> (original).next = clone;
          reg.get<wsl::comp::hierarchy> (clone).next = next_to_orig;
          found = true;
          break;
        }
        if (ch.next == entt::null) {
          break;
        }
        curr = ch.next;
      }
      if (!found && curr != entt::null) {
        reg.get<wsl::comp::hierarchy> (curr).next = clone;
      }
    }
  }

  // Append (Copy) to name
  std::string const name = scene->get_entity_name (clone);
  scene->set_entity_name (clone, name + " (Copy)");

  return clone;
}

void
entities_and_singletons_panel::delete_entity (entt::entity entity)
{
  wsl::rsc::scene *scene = m_runtime_ctx->scene_manager ().get_active ();
  if (!scene->get_registry ().valid (entity)) {
    return;
  }

  scene->destroy_entity_subtree (entity);
  if (m_selection.kind == selection_kind::entity
      && !scene->get_registry ().valid (m_selection.selected_entity)) {
    m_selection.clear_all ();
  }
}

void
entities_and_singletons_panel::make_prefab (entt::entity entity)
{
  wsl::rsc::scene *active_scene = m_runtime_ctx->scene_manager ().get_active ();
  if (active_scene == nullptr) {
    return;
  }

  std::shared_ptr<wsl::rsc::project> const proj
      = m_runtime_ctx->resource_manager ().current_project ();
  if (!proj) {
    return;
  }

  std::string name = active_scene->get_entity_name (entity);
  if (name.empty ()) {
    name = "unnamed_prefab";
  }

  std::string filename_base = name;
  for (char &c : filename_base) {
    if (std::isspace (c) != 0) {
      c = '_';
    }
  }

  std::string const filename = filename_base + ".prefab";
  // Prefabs are scene assets: save them under the project's scenes directory
  // (scan_assets only registers scenes/prefabs found in scenes_path).
  std::filesystem::path const rel_path
      = std::filesystem::path (proj->scenes_path) / filename;
  std::filesystem::path const abs_path
      = std::filesystem::path (proj->root_path) / rel_path;

  // Ensure directory exists
  std::filesystem::create_directories (abs_path.parent_path ());

  // Isolate entity and its children in a temporary scene
  wsl::rsc::scene temp_scene (m_runtime_ctx, m_editor_ctx, name);
  temp_scene.copy_entity (*active_scene, entity, entt::null);

  // Save the temporary scene as a prefab
  if (m_runtime_ctx->resource_manager ().save_scene (
          temp_scene, abs_path.string (), true)) {
    // Automatically register the new prefab in the resource manager using
    // absolute path consistent with how scan_assets works.
    wsl::rsc::scene_id prefab_id
        = m_runtime_ctx->resource_manager ().register_scene (
            abs_path.string ());
    static_cast<void> (m_runtime_ctx->resource_manager ().load (
        prefab_id)); // Start loading it so it's available for overrides

    // Tag the original entity (and its children) as instances of this prefab
    // We need to find the root in the temp_scene to get its original ID if we
    // wanted to be precise, but here we just saved 'entity' as the root of the
    // prefab. In the prefab scene, the root entity ID might be different from
    // 'entity'. Actually, copy_entity returns the new entity.

    // Let's get the root entity from temp_scene (it should be the only root)
    entt::entity prefab_root = entt::null;
    temp_scene.get_registry ().view<entt::entity> ().each (
        [&] (entt::entity e) {
          if (temp_scene.get_registry ().all_of<wsl::comp::hierarchy> (e)) {
            if (temp_scene.get_registry ().get<wsl::comp::hierarchy> (e).parent
                == entt::null) {
              prefab_root = e;
            }
          } else {
            prefab_root = e;
          }
        });

    if (prefab_root != entt::null) {
      auto tag_recursive
          = [&] (auto self, entt::entity src, entt::entity p_src) -> void {
        active_scene->get_registry ()
            .emplace_or_replace<wsl::comp::prefab_instance> (src, prefab_id,
                                                             p_src);

        if (active_scene->get_registry ().all_of<wsl::comp::hierarchy> (src)
            && temp_scene.get_registry ().all_of<wsl::comp::hierarchy> (
                p_src)) {

          entt::entity child = active_scene->get_registry ()
                                   .get<wsl::comp::hierarchy> (src)
                                   .first;
          entt::entity p_child = temp_scene.get_registry ()
                                     .get<wsl::comp::hierarchy> (p_src)
                                     .first;

          while (child != entt::null && p_child != entt::null) {
            self (self, child, p_child);
            child = active_scene->get_registry ()
                        .get<wsl::comp::hierarchy> (child)
                        .next;
            p_child = temp_scene.get_registry ()
                          .get<wsl::comp::hierarchy> (p_child)
                          .next;
          }
        }
      };
      tag_recursive (tag_recursive, entity, prefab_root);
    }
  }
}

} // namespace editor
