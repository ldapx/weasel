// ImGui inspector widgets for engine singletons and resource ids (moved out
// of the core library so libwsl stays free of ImGui). The core
// register_meta() functions only register the reflected data fields and
// register_editor_inspectors() attaches these widgets on top.

#include "wsl/comp/singl/runtime_context.hpp"
#include "wsl/comp/singl/ui_manager.hpp"
#include "wsl/rsc/project.hpp"
#include "wsl/rsc/resource_manager.hpp"
#include "wsl/rsc/scene.hpp"
#include "wsl/rsc/scene_manager.hpp"

#include <cstdio>
#include <filesystem>
#include <imgui.h>
#include <memory>
#include <optional>
#include <string>

namespace wsl
{

// ---------------- comp::singl::ui_manager ----------------

namespace comp
{
namespace singl
{

bool
ui_manager::custom_inspect (const char *label,
                            comp::singl::runtime_context *runtime)
{
  (void)label;
  if (runtime == nullptr) {
    return false;
  }

  auto &res_mgr = runtime->resource_manager ();
  bool changed = false;

  const char *preview = "None";
  if (m_active_document_id.value != entt::null) {
    if (auto info = res_mgr.info (m_active_document_id)) {
      preview = info->name.c_str ();
    }
  }

  if (ImGui::BeginCombo ("Active Document", preview)) {
    if (ImGui::Selectable ("None", m_active_document_id.value == entt::null)) {
      if (m_active_document_id.value != entt::null) {
        m_active_document_id.value = entt::null;
        m_needs_reload = true;
        changed = true;
      }
    }

    for (const auto &info : res_mgr.list_ui_layouts ()) {
      std::filesystem::path const p (info.path);
      if (p.extension () != ".rml") {
        continue;
      }

      const bool selected = (info.id == m_active_document_id.value);
      if (ImGui::Selectable (info.name.c_str (), selected)) {
        m_active_document_id.value = info.id;
        m_needs_reload = true;
        changed = true;
      }
      if (selected) {
        ImGui::SetItemDefaultFocus ();
      }
    }
    ImGui::EndCombo ();
  }

  if (m_active_document_id.value != entt::null) {
    if (ImGui::Button ("Reload Document")) {
      m_needs_reload = true;
      changed = true;
    }
  }

  return changed;
}

} // namespace singl
} // namespace comp

// ---------------- rsc::scene_manager ----------------

bool
rsc::scene_manager::custom_inspect (const char *label,
                                    comp::singl::runtime_context *runtime_ctx)
{
  (void)label;
  (void)runtime_ctx;

  bool changed = false;
  auto &scenes = m_main_world.get_scenes ();

  ImGui::TextDisabled ("Scene Count: %zu", scenes.size ());

  const char *preview = (m_active_scene != nullptr)
                            ? m_active_scene->get_name ().c_str ()
                            : "None";
  if (ImGui::BeginCombo ("Active Scene", preview)) {
    for (const auto &scene_ptr : scenes) {
      if (!scene_ptr) {
        continue;
      }

      const bool selected = (scene_ptr.get () == m_active_scene);
      if (ImGui::Selectable (scene_ptr->get_name ().c_str (), selected)) {
        set_active (scene_ptr.get ());
        changed = true;
      }

      if (selected) {
        ImGui::SetItemDefaultFocus ();
      }
    }
    ImGui::EndCombo ();
  }

  static int new_scene_counter = 1;
  if (ImGui::Button ("Create Scene")) {
    char name[64];
    std::snprintf (name, sizeof (name), "Scene %d", new_scene_counter++);
    create_scene (name, true);
    changed = true;
  }

  if (m_active_scene == nullptr) {
    ImGui::BeginDisabled ();
  }

  ImGui::SameLine ();
  if (ImGui::Button ("Remove Active Scene") && (m_active_scene != nullptr)) {
    scene *to_remove = m_active_scene;
    destroy_scene (to_remove);
    changed = true;
  }

  if (m_active_scene == nullptr) {
    ImGui::EndDisabled ();
  }

  return changed;
}

// ---------------- rsc::resource_manager_view ----------------

bool
rsc::resource_manager_view::custom_inspect (
    const char *label, comp::singl::runtime_context *runtime_ctx) const
{
  (void)label;
  (void)runtime_ctx;

  if (manager == nullptr) {
    ImGui::TextDisabled ("No resource manager");
    return false;
  }

  std::shared_ptr<rsc::project> const project = manager->current_project ();
  ImGui::TextDisabled ("Project");
  if (project) {
    ImGui::TextWrapped ("%s", project->name.c_str ());
    ImGui::TextDisabled ("%s", project->root_path.c_str ());
  } else {
    ImGui::TextDisabled ("No project loaded");
  }

  ImGui::Spacing ();
  ImGui::Separator ();
  ImGui::Spacing ();

  ImGui::TextDisabled ("Registered Resources");
  ImGui::BulletText ("Models: %zu", manager->list_models ().size ());
  ImGui::BulletText ("Images: %zu", manager->list_images ().size ());
  ImGui::BulletText ("Cubemaps: %zu", manager->list_cubemaps ().size ());
  ImGui::BulletText ("Scenes: %zu", manager->list_scenes ().size ());
  ImGui::BulletText ("Audio: %zu", manager->list_audio ().size ());

  const rsc::model_id preview = manager->current_preview_model ();
  if (preview.value != entt::null) {
    ImGui::Spacing ();
    ImGui::TextDisabled ("Preview Model: %08X",
                         static_cast<uint32_t> (preview.value));
  }

  return false;
}

// ---------------- resource id pickers ----------------

bool
rsc::model_id::custom_inspect (const char *label,
                               comp::singl::runtime_context *runtime)
{
  rsc::resource_manager *res_mgr
      = (runtime != nullptr) ? &runtime->resource_manager () : nullptr;
  if (res_mgr == nullptr) {
    ImGui::TextDisabled ("No resource manager");
    return false;
  }

  const char *preview = "None";
  char preview_buf[256];

  if (value != entt::null) {
    if (std::optional<model_resource_info> rec
        = res_mgr->info (model_id{ value })) {
      std::snprintf (preview_buf, sizeof (preview_buf), "%s (%s)",
                     rec->name.c_str (), rec->path.c_str ());
    } else {
      std::snprintf (preview_buf, sizeof (preview_buf), "(%08X)",
                     (uint32_t)value);
    }
    preview = preview_buf;
  }

  bool changed = false;

  if (ImGui::BeginCombo (label, preview)) {

    if (ImGui::Selectable ("None", value == entt::null)) {
      value = entt::null;
      changed = true;
    }

    for (const model_resource_info &rec : res_mgr->list_models ()) {
      const bool selected = (rec.id == value);

      char item_buf[256];
      std::snprintf (item_buf, sizeof (item_buf), "%s (%s)", rec.name.c_str (),
                     rec.path.c_str ());

      if (ImGui::Selectable (item_buf, selected)) {
        value = rec.id;
        static_cast<void> (res_mgr->load (model_id{ rec.id }));
        changed = true;
      }

      if (selected) {
        ImGui::SetItemDefaultFocus ();
      }
    }

    ImGui::EndCombo ();
  }

  return changed;
}

bool
rsc::audio_id::custom_inspect (const char *label,
                               comp::singl::runtime_context *runtime)
{
  rsc::resource_manager const *res_mgr
      = (runtime != nullptr) ? &runtime->resource_manager () : nullptr;
  if (res_mgr == nullptr) {
    ImGui::TextDisabled ("No resource manager");
    return false;
  }

  const char *preview = "None";
  char preview_buf[256];

  if (value != entt::null) {
    if (std::optional<audio_resource_info> rec
        = res_mgr->info (audio_id{ value })) {
      std::snprintf (preview_buf, sizeof (preview_buf), "%s (%s)",
                     rec->name.c_str (), rec->path.c_str ());
    } else {
      std::snprintf (preview_buf, sizeof (preview_buf), "(%08X)",
                     (uint32_t)value);
    }
    preview = preview_buf;
  }

  bool changed = false;

  if (ImGui::BeginCombo (label, preview)) {

    if (ImGui::Selectable ("None", value == entt::null)) {
      value = entt::null;
      changed = true;
    }

    for (const auto &rec : res_mgr->list_audio ()) {
      const bool selected = (rec.id == value);
      if (ImGui::Selectable (rec.name.c_str (), selected)) {
        value = rec.id;
        changed = true;
      }
      if (selected) {
        ImGui::SetItemDefaultFocus ();
      }
    }

    ImGui::EndCombo ();
  }

  return changed;
}

bool
rsc::material_id::custom_inspect (const char *label,
                                  comp::singl::runtime_context *runtime)
{
  rsc::resource_manager *res_mgr
      = (runtime != nullptr) ? &runtime->resource_manager () : nullptr;
  if (res_mgr == nullptr) {
    ImGui::TextDisabled ("No resource manager");
    return false;
  }

  const char *preview = "None";
  char preview_buf[256];

  if (value != entt::null) {
    if (std::optional<material_resource_info> rec
        = res_mgr->info (material_id{ value })) {
      std::snprintf (preview_buf, sizeof (preview_buf), "%s (%s)",
                     rec->name.c_str (), rec->path.c_str ());
    } else {
      std::snprintf (preview_buf, sizeof (preview_buf), "(%08X)",
                     (uint32_t)value);
    }
    preview = preview_buf;
  }

  bool changed = false;

  if (ImGui::BeginCombo (label, preview)) {

    if (ImGui::Selectable ("None", value == entt::null)) {
      value = entt::null;
      changed = true;
    }

    for (const auto &rec : res_mgr->list_materials ()) {
      const bool selected = (rec.id == value);

      char item_buf[256];
      std::snprintf (item_buf, sizeof (item_buf), "%s (%s)", rec.name.c_str (),
                     rec.path.c_str ());

      if (ImGui::Selectable (item_buf, selected)) {
        value = rec.id;
        static_cast<void> (res_mgr->load (material_id{ rec.id }));
        changed = true;
      }

      if (selected) {
        ImGui::SetItemDefaultFocus ();
      }
    }

    ImGui::EndCombo ();
  }

  return changed;
}

bool
rsc::image_id::custom_inspect (const char *label,
                               comp::singl::runtime_context *runtime)
{
  rsc::resource_manager *res_mgr
      = (runtime != nullptr) ? &runtime->resource_manager () : nullptr;
  if (res_mgr == nullptr) {
    ImGui::TextDisabled ("No resource manager");
    return false;
  }

  const char *preview = "None";
  char preview_buf[256];

  if (value != entt::null) {
    if (std::optional<image_resource_info> rec
        = res_mgr->info (image_id{ value })) {
      std::snprintf (preview_buf, sizeof (preview_buf), "%s (%s)",
                     rec->name.c_str (), rec->path.c_str ());
    } else {
      std::snprintf (preview_buf, sizeof (preview_buf), "(%08X)",
                     (uint32_t)value);
    }
    preview = preview_buf;
  }

  bool changed = false;

  if (ImGui::BeginCombo (label, preview)) {
    if (ImGui::Selectable ("None", value == entt::null)) {
      value = entt::null;
      changed = true;
    }

    for (const image_resource_info &rec : res_mgr->list_images ()) {
      const bool selected = (rec.id == value);

      char item_buf[256];
      std::snprintf (item_buf, sizeof (item_buf), "%s (%s)", rec.name.c_str (),
                     rec.path.c_str ());

      if (ImGui::Selectable (item_buf, selected)) {
        value = rec.id;
        static_cast<void> (res_mgr->load (image_id{ rec.id }));
        changed = true;
      }

      if (selected) {
        ImGui::SetItemDefaultFocus ();
      }
    }

    ImGui::EndCombo ();
  }

  return changed;
}

} // namespace wsl
