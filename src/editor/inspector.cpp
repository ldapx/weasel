#include "inspector.hpp"

#include "wsl/comp/singl/runtime_context.hpp"
#include "wsl/reg/component_registry.hpp"
#include "comp/component_meta.hpp"
#include "comp/singl/ui_manager.hpp"
#include "wsl/das/das_engine.hpp"

#include <cstring>
#include "gfx/image.hpp"
#include "rsc/resource_ids.hpp"
#include "rsc/resource_manager.hpp"
#include "wsl/comp/camera.hpp"
#include "wsl/comp/camera_2d.hpp"
#include "wsl/comp/hierarchy.hpp"
#include "wsl/comp/prefab_instance.hpp"
#include "wsl/comp/skeleton_pose.hpp"
#include "wsl/comp/subviewport.hpp"
#include "wsl/comp/transform.hpp"
#include "wsl/comp/singl/editor_context.hpp"
#include "renderer_imgui.hpp"
#include "ecs_inspector_utils.hpp"

#include <cstdint>
#include <entt/core/fwd.hpp>
#include <entt/core/type_info.hpp>
#include <entt/entity/entity.hpp>
#include <entt/entity/fwd.hpp>
#include <entt/meta/resolve.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>
#include <cstdio>
#include <entt/meta/container.hpp>
#include <entt/meta/meta.hpp>
#include <entt/meta/pointer.hpp>
#include <imgui.h>
#include <string>
#include <vector>

namespace editor
{

static entt::registry &
get_singleton_inspector_registry (
    wsl::comp::singl::runtime_context &runtime_ctx,
    wsl::comp::singl::editor_context &editor_ctx,
    entt::registry &fallback_registry)
{
  if (wsl::rsc::scene *scene = runtime_ctx.scene_manager ().get_active ()) {
    return scene->get_registry ();
  }

  auto &ctx = fallback_registry.ctx ();

  if (ctx.template contains<wsl::comp::singl::runtime_context *> ()) {
    ctx.erase<wsl::comp::singl::runtime_context *> ();
  }
  ctx.emplace<wsl::comp::singl::runtime_context *> (&runtime_ctx);

  if (ctx.template contains<wsl::comp::singl::editor_context *> ()) {
    ctx.erase<wsl::comp::singl::editor_context *> ();
  }
  ctx.emplace<wsl::comp::singl::editor_context *> (&editor_ctx);

  if (ctx.template contains<wsl::rsc::scene_manager *> ()) {
    ctx.erase<wsl::rsc::scene_manager *> ();
  }
  ctx.emplace<wsl::rsc::scene_manager *> (&runtime_ctx.scene_manager ());

  if (ctx.template contains<wsl::rsc::resource_manager_view *> ()) {
    ctx.erase<wsl::rsc::resource_manager_view *> ();
  }
  ctx.emplace<wsl::rsc::resource_manager_view *> (
      &runtime_ctx.resource_manager_view ());

  if (ctx.template contains<wsl::comp::singl::ui_manager *> ()) {
    ctx.erase<wsl::comp::singl::ui_manager *> ();
  }
  ctx.emplace<wsl::comp::singl::ui_manager *> (&runtime_ctx.ui_manager ());
  runtime_ctx.singleton_registry ().apply_core_singletons (fallback_registry);

  return fallback_registry;
}

enum class editor_trait : uint16_t
{
  hidden = 1 << 1
};

static constexpr editor_trait
operator& (editor_trait a, editor_trait b) noexcept
{
  return static_cast<editor_trait> (static_cast<uint16_t> (a)
                                    & static_cast<uint16_t> (b));
}

static bool
draw_field_value (const char *label, void *data,
                  wsl::das::das_engine::field_type_kind kind, int size)
{
  switch (kind) {
  case wsl::das::das_engine::field_type_kind::integer: {
    int v = 0;
    std::memcpy (&v, data, static_cast<std::size_t> (size));
    if (ImGui::DragInt (label, &v, 0.1f)) {
      std::memcpy (data, &v, static_cast<std::size_t> (size));
      return true;
    }
    return false;
  }
  case wsl::das::das_engine::field_type_kind::unsigned_integer: {
    int v = 0;
    std::memcpy (&v, data, static_cast<std::size_t> (size));
    if (ImGui::DragInt (label, &v, 0.1f, 0)) {
      std::memcpy (data, &v, static_cast<std::size_t> (size));
      return true;
    }
    return false;
  }
  case wsl::das::das_engine::field_type_kind::floating: {
    float v = 0.0f;
    std::memcpy (&v, data, static_cast<std::size_t> (size));
    if (ImGui::DragFloat (label, &v, 0.1f)) {
      std::memcpy (data, &v, static_cast<std::size_t> (size));
      return true;
    }
    return false;
  }
  case wsl::das::das_engine::field_type_kind::boolean: {
    bool v = false;
    std::memcpy (&v, data, sizeof (bool));
    if (ImGui::Checkbox (label, &v)) {
      std::memcpy (data, &v, sizeof (bool));
      return true;
    }
    return false;
  }
  default:
    ImGui::TextUnformatted (label);
    return false;
  }
}

struct component_header_result
{
  bool expanded = false;
  bool remove_requested = false;
};

// Renders a reflected field marked `meta_info::read_only` as plain text.
// Used for derived values such as resolved viewport resolutions, which must
// never be authored directly.
static void
draw_read_only_meta_value (const entt::meta_any &value)
{
  auto const type = value.type ();

  auto draw_vec = [] (const entt::meta_any &v, int components) {
    float data[4]{ 0.0F, 0.0F, 0.0F, 0.0F };
    switch (components) {
    case 2: {
      auto const vec = v.cast<wsl::math::vec2f> ();
      data[0] = vec.x ();
      data[1] = vec.y ();
      break;
    }
    case 3: {
      auto const vec = v.cast<wsl::math::vec3f> ();
      data[0] = vec.x ();
      data[1] = vec.y ();
      data[2] = vec.z ();
      break;
    }
    case 4:
    default: {
      auto const vec = v.cast<wsl::math::vec4f> ();
      data[0] = vec.x ();
      data[1] = vec.y ();
      data[2] = vec.z ();
      data[3] = vec.w ();
      break;
    }
    }
    ImGui::TextDisabled ("%.2f, %.2f", data[0], data[1]);
    if (components >= 3) {
      ImGui::SameLine ();
      ImGui::TextDisabled ("%.2f", data[2]);
    }
    if (components >= 4) {
      ImGui::SameLine ();
      ImGui::TextDisabled ("%.2f", data[3]);
    }
  };

  if (type == entt::resolve<wsl::math::vec2f> ()) {
    draw_vec (value, 2);
    return;
  }
  if (type == entt::resolve<wsl::math::vec3f> ()) {
    draw_vec (value, 3);
    return;
  }
  if (type == entt::resolve<wsl::math::vec4f> ()) {
    draw_vec (value, 4);
    return;
  }
  if (type == entt::resolve<float> ()) {
    ImGui::TextDisabled ("%.3f", value.cast<float> ());
    return;
  }
  if (type == entt::resolve<int> ()) {
    ImGui::TextDisabled ("%d", value.cast<int> ());
    return;
  }
  if (type == entt::resolve<bool> ()) {
    ImGui::TextDisabled ("%s", value.cast<bool> () ? "true" : "false");
    return;
  }
  if (type.is_enum ()) {
    entt::meta_any copy = value;
    if (copy.allow_cast<int> ()) {
      ImGui::TextDisabled ("%d", copy.cast<int> ());
      return;
    }
    ImGui::TextDisabled ("<enum>");
    return;
  }

  ImGui::TextDisabled ("<computed>");
}

static component_header_result
draw_component_header (entt::id_type type_id, const char *display_name,
                       const char *type_name, const std::string &icon,
                       const std::string &description,
                       wsl::comp::singl::editor_context *editor_ctx)
{
  component_header_result result;

  ImGuiTreeNodeFlags const flags
      = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap
        | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth;

  result.expanded = ImGui::TreeNodeEx ((void *)(intptr_t)type_id, flags, " ");
  ImGui::SameLine ();

  if (!icon.empty ()) {
    auto *editor_res = &editor_ctx->editor_resources ();
    auto img_id = editor_res->register_image (icon);

    if (editor_res->state (img_id) == wsl::rsc::image_state::not_loaded) {
      editor_res->load (img_id);
    }

    auto handle = editor_res->get (img_id);
    if (handle && editor_res->state (img_id) == wsl::rsc::image_state::loaded) {
      wsl::gfx::image const *img = handle.handle ().get ();
      if ((img != nullptr) && (img->texture.get () != nullptr)) {
        ImGui::Image ((ImTextureID)img->texture.get (), ImVec2 (16, 16));
        ImGui::SameLine (0.0F, 6.0F);
      }
    }
  }

  ImGui::TextUnformatted (display_name);

  if (type_name != nullptr && type_name[0] != '\0') {
    ImGui::SameLine ();
    ImGui::TextDisabled ("%s", type_name);
  }

  if (!description.empty () && ImGui::IsItemHovered ()) {
    ImGui::SetTooltip ("%s", description.c_str ());
  }

  ImGui::SameLine ();
  ImGui::SetCursorPosX (ImGui::GetWindowContentRegionMax ().x
                        - ImGui::CalcTextSize ("x").x
                        - (ImGui::GetStyle ().FramePadding.x * 2) - 10);

  if (ImGui::SmallButton ("x")) {
    result.remove_requested = true;
  }
  if (ImGui::IsItemHovered ()) {
    ImGui::SetTooltip ("Remove component");
  }

  return result;
}

inspector::inspector (wsl::comp::singl::runtime_context *runtime_ctx,
                      wsl::comp::singl::editor_context *editor_ctx,
                      ecs_selection &selected)
    : m_runtime_ctx (runtime_ctx), m_editor_ctx (editor_ctx),
      m_selection (selected)
{
}

static std::string
get_icon_path (const entt::meta_type &meta)
{
  return wsl::comp::meta_icon_path (meta);
}

static void
draw_system_name_list (const char *label, const std::vector<std::string> &items)
{
  ImGui::TextUnformatted (label);

  if (items.empty ()) {
    ImGui::TextDisabled ("None");
    return;
  }

  ImGui::Indent ();
  for (const std::string &item : items) {
    ImGui::BulletText ("%s", item.c_str ());
  }
  ImGui::Unindent ();
}

void
inspector::draw ()
{
  ImGui::PushFont (m_editor_ctx->get_imgui_renderer ()->get_fonts ().bold);
  bool const open = ImGui::Begin ("Inspector");
  ImGui::PopFont ();

  if (!open) {
    ImGui::End ();
    return;
  }

  if (m_selection.selected_system != nullptr) {
    draw_system_inspector (m_selection.selected_system);
  } else {
    switch (m_selection.kind) {
    case selection_kind::entity:
      if ((m_runtime_ctx->scene_manager ().get_active () != nullptr)
          && m_runtime_ctx->scene_manager ()
                 .get_active ()
                 ->get_registry ()
                 .valid (m_selection.selected_entity)) {
        draw_entity_inspector (m_selection.selected_entity);
      } else {
        draw_centered_icon (
            m_editor_ctx, m_editor_ctx->icon_inspector (), 128.0F,
            "Select a system, entity or singleton component\nto inspect and "
            "edit their fields.");
      }
      break;

    case selection_kind::singleton:
      draw_singleton_inspector (m_selection.singleton_type);
      break;

    default:
      draw_centered_icon (
          m_editor_ctx, m_editor_ctx->icon_inspector (), 128.0F,
          "Select a system, entity or singleton component\nto inspect and "
          "edit their fields.");
      break;
    }
  }

  ImGui::End ();
}

void
inspector::draw_system_inspector (wsl::sys::ecs_system *system)
{
  if (system == nullptr) {
    ImGui::TextDisabled ("No system selected");
    return;
  }

  ImGui::PushFont (m_editor_ctx->get_imgui_renderer ()->get_fonts ().semibold);
  ImGui::TextUnformatted (system->get_name ().c_str ());
  ImGui::PopFont ();

  ImGui::TextDisabled ("%s", system->get_type_name ());
  ImGui::Separator ();

  entt::registry *registry = nullptr;
  if (wsl::rsc::scene *scene = m_runtime_ctx->scene_manager ().get_active ()) {
    registry = &scene->get_registry ();
  }

  bool init_on_startup = system->is_init_on_startup ();
  if (ImGui::Checkbox ("Init On Startup", &init_on_startup)) {
    system->set_init_on_startup (init_on_startup, registry,
                                 m_runtime_ctx->is_running ());
  }

  bool editor_active = system->is_editor_active ();
  if (ImGui::Checkbox ("Editor Active", &editor_active)) {
    system->set_editor_active (editor_active);
  }

  ImGui::TextDisabled ("Current State: %s",
                       system->is_active () ? "Active" : "Inactive");

  ImGui::Spacing ();
  draw_system_name_list ("Dependencies", system->get_dependencies ());
  ImGui::Spacing ();
  draw_system_name_list ("Conflicts", system->get_conflicts ());
  ImGui::Spacing ();
  ImGui::Separator ();
  ImGui::Spacing ();

  ImGui::PushFont (m_editor_ctx->get_imgui_renderer ()->get_fonts ().medium);
  ImGui::TextUnformatted ("Iterations");
  ImGui::PopFont ();

  auto &db = m_runtime_ctx->event_db ();
  const entt::id_type sys_tid = system->get_type_id ();

  bool any = false;

  for (const auto &it : db.system_iterations) {
    if (it.system_type_id != sys_tid) {
      continue;
    }

    any = true;

    if (ImGui::TreeNode (it.iteration_name.c_str ())) {
      if (it.component_types.empty ()) {
        ImGui::TextDisabled ("No component type list.");
      } else {
        ImGui::TextUnformatted ("Components:");
        ImGui::Indent ();
        for (const auto &ct : it.component_types) {
          ImGui::BulletText ("%s", ct.type_name.c_str ());
        }
        ImGui::Unindent ();
      }

      ImGui::TreePop ();
    }
  }

  if (!any) {
    ImGui::TextDisabled ("No iterations registered for this system.");
  }
}

void
inspector::draw_singleton_inspector (entt::id_type type)
{
  entt::registry &registry = get_singleton_inspector_registry (
      *m_runtime_ctx, *m_editor_ctx, m_no_scene_registry);
  const bool has_scene
      = m_runtime_ctx->scene_manager ().get_active () != nullptr;

  const wsl::reg::singleton_registry::descriptor *desc
      = m_runtime_ctx->singleton_registry ().find (type);
  entt::meta_type const meta = entt::resolve (type);

  if (desc == nullptr) {
    ImGui::TextDisabled ("Unregistered singleton");
    return;
  }

  bool remove_requested = false;
  draw_singleton_header (*desc, meta, !desc->core, &remove_requested);

  if (remove_requested && (desc->remove != nullptr)) {
    if (desc->remove (registry)) {
      m_selection.clear_entity_singleton ();
    }
    return;
  }

  void *instance_ptr
      = (desc->get_ptr != nullptr) ? desc->get_ptr (registry) : nullptr;
  if (instance_ptr == nullptr) {
    ImGui::TextDisabled (has_scene ? "Singleton not present in scene"
                                   : "Singleton unavailable without an active "
                                     "scene");
    return;
  }

  if (!meta) {
    ImGui::TextDisabled ("No reflection data available for this singleton.");
    return;
  }

  bool has_visible_members = false;
  for (auto &&[id, data] : meta.data ()) {
    (void)id;
    if (!is_hidden (data)) {
      has_visible_members = true;
      break;
    }
  }

  const bool has_custom_inspector
      = static_cast<bool> (meta.func (entt::hashed_string{ "custom_inspect" }));
  if (!has_visible_members && !has_custom_inspector) {
    ImGui::TextDisabled ("No reflected fields.");
    return;
  }

  entt::meta_any obj = meta.from_void (instance_ptr);
  if (!obj) {
    ImGui::TextDisabled ("Failed to create meta instance.");
    return;
  }
  draw_meta_class (obj);
}

void
inspector::draw_entity_inspector (entt::entity entity)
{
  auto &scene_mgr = m_runtime_ctx->scene_manager ();
  auto &scene = *scene_mgr.get_active ();
  entt::registry &reg = scene.get_registry ();

  // Prefab instance logic
  m_is_prefab_instance = reg.all_of<wsl::comp::prefab_instance> (entity);
  m_prefab_registry = nullptr;
  m_prefab_entity = entt::null;

  if (m_is_prefab_instance) {
    auto &pi = reg.get<wsl::comp::prefab_instance> (entity);
    if (auto prefab_scene
        = m_runtime_ctx->resource_manager ().get (pi.prefab_id)) {
      m_prefab_registry = &prefab_scene->get_registry ();
      m_prefab_entity = pi.prefab_entity;
    }
  }

  const std::string &name = scene.get_entity_name (entity);

  ImGui::PushFont (m_editor_ctx->get_imgui_renderer ()->get_fonts ().semibold);
  ImGui::TextUnformatted (name.c_str ());
  ImGui::PopFont ();

  ImGui::SameLine ();

  ImGui::PushFont (m_editor_ctx->get_imgui_renderer ()->get_fonts ().light);
  ImGui::TextDisabled (" (%u)", static_cast<uint32_t> (entity));
  ImGui::PopFont ();

  if (m_is_prefab_instance && (m_prefab_registry != nullptr)) {
    ImGui::SameLine ();
    ImGui::TextDisabled ("[Prefab Instance]");
  }

  entt::id_type component_to_remove = entt::null;

  for (auto &&[internal_id, storage] : reg.storage ()) {

    if (!storage.contains (entity)) {
      continue;
    }

    entt::id_type const type_id
        = m_runtime_ctx->component_registry ().to_stable_id (internal_id);
    const auto *descriptor
        = m_runtime_ctx->component_registry ().find (type_id);
    std::string const display_name
        = (descriptor != nullptr) ? descriptor->display_name : "Unknown";

    // Skip runtime-only components.
    //
    // `skeleton_pose` is rebuilt by the animation system every frame and is
    // deliberately absent from component_types, so it never reaches the
    // registry or the reflected type metadata. Without this filter it surfaced
    // as an "Unknown" component reading "(No reflection data available)" the
    // moment an animator produced a pose -- i.e. as soon as a clip was
    // selected. There is nothing for a user to edit, and saving the entity
    // would not round-trip it.
    if (type_id == entt::type_hash<wsl::comp::skeleton_pose>::value ()) {
      continue;
    }

    // Skip internal/special components
    if (type_id == entt::type_hash<wsl::comp::prefab_instance>::value ()) {
      continue;
    }

    entt::meta_type const meta = entt::resolve (type_id);
    if (!meta) {
      // Fallback: draw a basic header even if we can't reflect the fields
      ImGui::Separator ();
      ImGui::PushFont (
          m_editor_ctx->get_imgui_renderer ()->get_fonts ().medium);
      ImGui::TextUnformatted (display_name.c_str ());
      ImGui::PopFont ();
      ImGui::TextDisabled ("(No reflection data available)");
      continue;
    }
    if (!meta.is_class ()) {
      continue;
    }

    ImGui::PushID (static_cast<int> (type_id));

    std::string const icon = get_icon_path (meta);
    const std::string comp_desc = get_description (meta);
    auto hdr = draw_component_header (type_id, display_name.c_str (), nullptr,
                                      icon, comp_desc, m_editor_ctx);
    if (hdr.remove_requested) {
      component_to_remove = type_id;
    }

    if (hdr.expanded) {
      if (component_to_remove != type_id) {
        void *ptr = storage.value (entity);
        if (ptr != nullptr) {
          ImGui::PushID ((int)entt::to_integral (entity));

          glm::vec3 entity_scale{ 1.0F, 1.0F, 1.0F };
          if (auto *t = reg.try_get<wsl::comp::transform> (entity); t) {
            entity_scale
                = glm::vec3 (t->scale.x (), t->scale.y (), t->scale.z ());
          }

          if (type_id == entt::type_hash<wsl::comp::hierarchy>::value ()) {
            draw_hierarchy_component (entity);
          } else {
            entt::meta_any instance = meta.from_void (ptr);
            if (!instance) {
              ImGui::PopID ();
              continue;
            }

            // If it's a prefab instance, try to find the same component in the
            // prefab
            entt::meta_any prefab_instance;
            if ((m_prefab_registry != nullptr)
                && m_prefab_registry->valid (m_prefab_entity)
                && (m_prefab_registry->storage (type_id) != nullptr)
                && m_prefab_registry->storage (type_id)->contains (
                    m_prefab_entity)) {
              void *p_ptr = m_prefab_registry->storage (type_id)->value (
                  m_prefab_entity);
              if (p_ptr != nullptr) {
                prefab_instance = meta.from_void (p_ptr);
              }
            }

            draw_meta_class (instance, entity_scale,
                             prefab_instance ? &prefab_instance : nullptr);
          }

          ImGui::PopID ();
        }
      }
      ImGui::TreePop ();
    }

    ImGui::PopID ();
  }

  if (component_to_remove != entt::null) {
    if (auto *storage = reg.storage (component_to_remove)) {
      storage->remove (entity);
    } else {
      auto &comp_reg = m_runtime_ctx->component_registry ();
      if (const auto *desc = comp_reg.find (component_to_remove);
          desc != nullptr && desc->is_das_component) {
        comp_reg.das_component_remove (reg, component_to_remove, entity);
      }
    }
  }

  // Draw das components attached to this entity
  {
    auto &comp_reg = m_runtime_ctx->component_registry ();
    auto ordered = comp_reg.ordered ();
    for (const auto *desc : ordered) {
      if (desc == nullptr || !desc->is_das_component) {
        continue;
      }
      if (!comp_reg.das_component_contains (reg, desc->type_id, entity)) {
        continue;
      }

      ImGui::PushID (static_cast<int> (desc->type_id));

      auto hdr = draw_component_header (
          desc->type_id, desc->display_name.c_str (), desc->type_name.c_str (),
          "", "", m_editor_ctx);
      if (hdr.remove_requested) {
        component_to_remove = desc->type_id;
      }

      if (hdr.expanded) {
        if (component_to_remove != desc->type_id) {
          uint8_t *data
              = comp_reg.das_component_data (reg, desc->type_id, entity);

          for (std::size_t fi = 0; fi < desc->das_fields.size (); ++fi) {
            const auto &field = desc->das_fields[fi];

            ImGui::PushID (static_cast<int> (fi));

            ImGui::TextUnformatted (field.name.c_str ());

            if (ImGui::IsItemHovered ()) {
              ImGui::BeginTooltip ();
              ImGui::Text ("Type: %s", field.type_name.c_str ());
              ImGui::EndTooltip ();
            }

            if (data == nullptr) {
              ImGui::PopID ();
              continue;
            }

            auto *field_ptr = data + field.offset;

            ImGui::PushItemWidth (-1.0f);
            draw_field_value ("##field", field_ptr, field.kind, field.size);
            ImGui::PopItemWidth ();

            ImGui::PopID ();
          }
          if (desc->das_fields.empty ()) {
            ImGui::TextDisabled ("(no fields)");
          }
        }
        ImGui::TreePop ();
      }

      ImGui::PopID ();
    }
  }

  ImGui::Separator ();
  draw_add_component_ui (entity);
}

void
inspector::draw_hierarchy_component (entt::entity entity)
{

  entt::registry &registry
      = m_runtime_ctx->scene_manager ().get_active ()->get_registry ();

  ImGui::PushID ((int)entt::to_integral (entity));

  wsl::comp::hierarchy const &h = registry.get<wsl::comp::hierarchy> (entity);

  wsl::rsc::scene const *scene = m_runtime_ctx->scene_manager ().get_active ();

  const char *preview = "None";
  if (h.parent != entt::null) {
    preview = scene->get_entity_name (h.parent).c_str ();
  }

  if (ImGui::BeginCombo ("Parent", preview)) {

    if (ImGui::Selectable ("None", h.parent == entt::null)) {
      set_parent (entity, entt::null);
    }

    for (auto e : registry.view<entt::entity> ()) {
      if (e == entity) {
        continue;
      }

      const std::string &name = scene->get_entity_name (e);
      bool const selected = (e == h.parent);

      if (ImGui::Selectable (name.c_str (), selected)) {
        set_parent (entity, e);
      }

      if (selected) {
        ImGui::SetItemDefaultFocus ();
      }
    }

    ImGui::EndCombo ();
  }

  ImGui::PushFont (m_editor_ctx->get_imgui_renderer ()->get_fonts ().light);
  ImGui::TextDisabled ("First child: %u", (uint32_t)h.first);
  ImGui::TextDisabled ("Next sibling: %u", (uint32_t)h.next);
  ImGui::PopFont ();

  ImGui::PopID ();
}

void
inspector::set_parent (entt::entity child, entt::entity new_parent)
{

  entt::registry &registry
      = m_runtime_ctx->scene_manager ().get_active ()->get_registry ();

  wsl::comp::hierarchy &ch = registry.get<wsl::comp::hierarchy> (child);

  // detach from old parent
  if (ch.parent != entt::null) {
    wsl::comp::hierarchy &old = registry.get<wsl::comp::hierarchy> (ch.parent);

    entt::entity *link = &old.first;
    while (*link != entt::null) {
      if (*link == child) {
        *link = registry.get<wsl::comp::hierarchy> (*link).next;
        break;
      }
      link = &registry.get<wsl::comp::hierarchy> (*link).next;
    }
  }

  ch.parent = new_parent;
  ch.next = entt::null;

  if (new_parent != entt::null) {
    wsl::comp::hierarchy &ph = registry.get<wsl::comp::hierarchy> (new_parent);
    ch.next = ph.first;
    ph.first = child;
  }
}

static bool
meta_any_equal (const entt::meta_any &a, const entt::meta_any &b)
{
  if (!a || !b) {
    return false;
  }
  if (a.type () != b.type ()) {
    return false;
  }

  auto type = a.type ();

  // Try using meta equality operator if registered
  if (auto eq = type.func (entt::hashed_string{ "operator==" }); eq) {
    if (auto res = eq.invoke (a, b); res) {
      return res.cast<bool> ();
    }
  }

  // Fallback for common primitive types
  if (type == entt::resolve<float> ()) {
    return a.cast<float> () == b.cast<float> ();
  }
  if (type == entt::resolve<int> ()) {
    return a.cast<int> () == b.cast<int> ();
  }
  if (type == entt::resolve<bool> ()) {
    return a.cast<bool> () == b.cast<bool> ();
  }
  if (type == entt::resolve<std::string> ()) {
    return a.cast<std::string> () == b.cast<std::string> ();
  }
  if (type == entt::resolve<entt::entity> ()) {
    return a.cast<entt::entity> () == b.cast<entt::entity> ();
  }
  if (type == entt::resolve<uint32_t> ()) {
    return a.cast<uint32_t> () == b.cast<uint32_t> ();
  }
  if (type == entt::resolve<wsl::math::vec3f> ()) {
    return a.cast<wsl::math::vec3f> () == b.cast<wsl::math::vec3f> ();
  }
  if (type == entt::resolve<wsl::math::quatf> ()) {
    return a.cast<wsl::math::quatf> () == b.cast<wsl::math::quatf> ();
  }
  if (type == entt::resolve<wsl::math::mat33f> ()) {
    return a.cast<wsl::math::mat33f> () == b.cast<wsl::math::mat33f> ();
  }
  if (type == entt::resolve<wsl::math::mat44f> ()) {
    return a.cast<wsl::math::mat44f> () == b.cast<wsl::math::mat44f> ();
  }
  if (type == entt::resolve<wsl::rsc::model_id> ()) {
    return a.cast<wsl::rsc::model_id> () == b.cast<wsl::rsc::model_id> ();
  }
  if (type == entt::resolve<wsl::rsc::material_id> ()) {
    return a.cast<wsl::rsc::material_id> () == b.cast<wsl::rsc::material_id> ();
  }
  if (type == entt::resolve<wsl::rsc::cubemap_id> ()) {
    return a.cast<wsl::rsc::cubemap_id> () == b.cast<wsl::rsc::cubemap_id> ();
  }
  if (type == entt::resolve<wsl::rsc::audio_id> ()) {
    return a.cast<wsl::rsc::audio_id> () == b.cast<wsl::rsc::audio_id> ();
  }
  if (type == entt::resolve<glm::mat4> ()) {
    return a.cast<glm::mat4> () == b.cast<glm::mat4> ();
  }

  return false;
}

bool
inspector::draw_meta_class (entt::meta_any &object, const glm::vec3 &scale,
                            entt::meta_any *prefab_object,
                            entt::meta_any *default_object)
{
  auto type = object.type ();

  // If the class provides a full custom inspector, let it run.
  if (auto fn = type.func (entt::hashed_string{ "custom_inspect" }); fn) {
    if (entt::meta_any ret = fn.invoke (
            object, static_cast<const char *> ("##component"), m_runtime_ctx);
        ret) {
      (void)ret.allow_cast<bool> ();
      const bool changed = ret.cast<bool> ();

      // If custom inspector changed something, also notify.
      if (changed) {
        if (auto on_changed
            = type.func (entt::hashed_string{ "on_inspector_changed" });
            on_changed) {
          on_changed.invoke (object, m_runtime_ctx, scale);
        }
      }
      return changed;
    }

    if (entt::meta_any ret
        = fn.invoke (object, static_cast<const char *> ("##component"));
        ret) {
      (void)ret.allow_cast<bool> ();
      const bool changed = ret.cast<bool> ();

      if (changed) {
        if (auto on_changed
            = type.func (entt::hashed_string{ "on_inspector_changed" });
            on_changed) {
          on_changed.invoke (object, m_runtime_ctx, scale);
        }
      }
      return changed;
    }

    return false;
  }

  // Default per-field inspector
  bool changed = false;

  // Create a default instance if none was provided
  entt::meta_any default_instance;
  if (default_object == nullptr) {
    default_instance = type.construct ();
    if (default_instance) {
      default_object = &default_instance;
    }
  }

  for (auto &&[id, data] : type.data ()) {
    if (is_hidden (data)) {
      continue;
    }

    entt::meta_any value = data.get (object);
    if (!value) {
      continue;
    }

    entt::meta_any *prefab_value = nullptr;
    entt::meta_any prefab_value_storage;
    if (prefab_object != nullptr) {
      prefab_value_storage = data.get (*prefab_object);
      if (prefab_value_storage) {
        prefab_value = &prefab_value_storage;
      }
    }

    entt::meta_any *default_value = nullptr;
    entt::meta_any default_value_storage;
    if (default_object != nullptr) {
      default_value_storage = data.get (*default_object);
      if (default_value_storage) {
        default_value = &default_value_storage;
      }
    }

    ImGui::PushID (static_cast<int> (id));

    const std::string field_name
        = wsl::comp::meta_display_name (data, "<unnamed>");

    // Derived/computed fields render as read-only text and never write back.
    auto const read_only_info = wsl::comp::get_meta_info (data);
    if (read_only_info && read_only_info->read_only) {
      ImGui::TextUnformatted (field_name.c_str ());
      if (ImGui::IsItemHovered ()) {
        ImGui::BeginTooltip ();
        ImGui::Text ("Type: %s", meta_type_debug_name (data.type ()).c_str ());
        const std::string desc = get_description (data);
        if (!desc.empty ()) {
          ImGui::Separator ();
          ImGui::TextWrapped ("%s", desc.c_str ());
        }
        ImGui::EndTooltip ();
      }
      ImGui::PushItemWidth (-1.0F);
      draw_read_only_meta_value (value);
      ImGui::PopItemWidth ();
      ImGui::PopID ();
      continue;
    }

    // --- Field name line with refresh button anchored right ---
    float line_start_x = ImGui::GetCursorPosX ();
    float avail_width = ImGui::GetContentRegionAvail ().x;
    bool show_refresh = false;
    if (default_value != nullptr && !meta_any_equal (value, *default_value)) {
      show_refresh = true;
    }

    ImGui::TextUnformatted (field_name.c_str ());

    if (ImGui::IsItemHovered ()) {
      ImGui::BeginTooltip ();
      ImGui::Text ("Type: %s", meta_type_debug_name (data.type ()).c_str ());
      const std::string desc = get_description (data);
      if (!desc.empty ()) {
        ImGui::Separator ();
        ImGui::TextWrapped ("%s", desc.c_str ());
      }
      ImGui::EndTooltip ();
    }

    if (show_refresh) {
      ImGui::SameLine ();
      float btn_size = ImGui::GetTextLineHeight ();
      float btn_x = line_start_x + avail_width - btn_size
                    - ImGui::GetStyle ().FramePadding.x;
      if (btn_x > ImGui::GetCursorPosX ())
        ImGui::SetCursorPosX (btn_x);

      auto *editor_res = &m_editor_ctx->editor_resources ();
      auto img_id = m_editor_ctx->icon_refresh ();
      if (editor_res->state (img_id) == wsl::rsc::image_state::not_loaded) {
        editor_res->load (img_id);
      }
      auto handle = editor_res->get (img_id);
      if (handle
          && editor_res->state (img_id) == wsl::rsc::image_state::loaded) {
        wsl::gfx::image const *img = handle.handle ().get ();
        if (img != nullptr && img->texture.get () != nullptr) {
          if (ImGui::ImageButton ("##refresh", (ImTextureID)img->texture.get (),
                                  ImVec2 (btn_size, btn_size))) {
            data.set (object, *default_value);
            changed = true;
          }
        } else {
          if (ImGui::SmallButton ("R")) {
            data.set (object, *default_value);
            changed = true;
          }
        }
      } else {
        if (ImGui::SmallButton ("R")) {
          data.set (object, *default_value);
          changed = true;
        }
      }
      if (ImGui::IsItemHovered ()) {
        ImGui::SetTooltip ("Reset to default");
      }
    }

    // --- Field value line below ---
    ImGui::PushItemWidth (-1.0f);

    // Special-case render_viewport entity fields to show only SubViewport
    // entities.
    bool field_changed = false;
    if (data.type () == entt::resolve<entt::entity> ()
        && id == entt::hashed_string{ "render_viewport" }) {
      field_changed = draw_render_viewport_field (value);
    } else {
      field_changed = draw_meta_object ("##value", value, prefab_value);
    }

    if (field_changed) {
      data.set (object, value);
      changed = true;
    }
    ImGui::PopItemWidth ();

    ImGui::PopID ();
  }

  // after any edit, notify the object (e.g. rigid_body)
  if (changed) {
    if (auto on_changed
        = type.func (entt::hashed_string{ "on_inspector_changed" });
        on_changed) {
      on_changed.invoke (object, m_runtime_ctx, scale);
    }
  }

  return changed;
}

bool
inspector::draw_render_viewport_field (entt::meta_any &object)
{
  auto &scene = *m_runtime_ctx->scene_manager ().get_active ();
  auto &registry = scene.get_registry ();

  entt::entity current = object.cast<entt::entity> ();

  const char *preview = "Root Viewport";
  if (current != entt::null && registry.valid (current)) {
    preview = scene.get_entity_name (current).c_str ();
  }

  bool changed = false;
  if (ImGui::BeginCombo ("##value", preview)) {
    if (ImGui::Selectable ("Root Viewport", current == entt::null)) {
      object = entt::null;
      changed = true;
    }

    auto sv_view = registry.view<wsl::comp::subviewport> ();
    for (entt::entity const e : sv_view) {
      const std::string &name = scene.get_entity_name (e);
      bool const selected = (e == current);

      if (ImGui::Selectable (name.c_str (), selected)) {
        object = e;
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
inspector::draw_meta_object (const char *label, entt::meta_any &object,
                             entt::meta_any *prefab_object)
{

  auto type = object.type ();

  if (auto fn = type.func (entt::hashed_string{ "custom_inspect" }); fn) {
    // Try (label, runtime_ctx) first:
    if (entt::meta_any ret = fn.invoke (object, label, m_runtime_ctx); ret) {
      (void)ret.allow_cast<bool> ();
      return ret.cast<bool> ();
    }

    // Fallback: allow old signature (label) like wsl::math::vec3f
    if (entt::meta_any ret = fn.invoke (object, label); ret) {
      (void)ret.allow_cast<bool> ();
      return ret.cast<bool> ();
    }

    return false;
  }

  // ---- existing logic continues below ----
  if (type == entt::resolve<entt::entity> ()) {
    return draw_meta_value (label, object, prefab_object);
  }

  if (type == entt::resolve<std::string> ()) {
    return draw_meta_value (label, object, prefab_object);
  }

  if (type == entt::resolve<wsl::rsc::model_id> ()) {
    return draw_meta_value (label, object, prefab_object);
  }

  if (type == entt::resolve<wsl::rsc::cubemap_id> ()) {
    return draw_meta_value (label, object, prefab_object);
  }

  if (type == entt::resolve<wsl::rsc::audio_id> ()) {
    return draw_meta_value (label, object, prefab_object);
  }

  if (type == entt::resolve<wsl::rsc::image_id> ()) {
    return draw_meta_value (label, object, prefab_object);
  }

  if (type == entt::resolve<wsl::rsc::material_id> ()) {
    return draw_meta_value (label, object, prefab_object);
  }

  if (type.is_enum ()) {
    {
      return draw_meta_enum (label, object);
    }
  }
  if (type.is_sequence_container ())
    return draw_meta_sequence (label, object);
  else if (type.is_class ()) {
    if (ImGui::TreeNode (label)) {
      bool changed
          = draw_meta_class (object, { 1.0F, 1.0F, 1.0F }, prefab_object);
      ImGui::TreePop ();
      return changed;
    }
  } else {
    return draw_meta_value (label, object, prefab_object);
  }
  return false;
}

static const char *
enum_label (const entt::meta_data &data)
{
  // meta_custom stores a value of type const char*,
  // so the safe pointer conversion yields const char**.
  if (const char *const *p = data.custom ();
      (p != nullptr) && ((*p) != nullptr)) {
    return *p;
  }
  return "<unnamed>";
}

bool
inspector::draw_meta_enum (const char *label, entt::meta_any &object)
{
  auto type = object.type ();

  (void)object.allow_cast<int> ();
  const int current = object.cast<int> ();

  const char *preview = "...";

  for (auto &&[id, data] : type.data ()) {
    entt::meta_any v = data.get ({});
    if (!v) {
      continue;
    }

    (void)v.allow_cast<int> ();
    if (v.cast<int> () == current) {
      preview = enum_label (data);
      break;
    }
  }

  bool changed = false;

  if (ImGui::BeginCombo (label, preview)) {
    for (auto &&[id, data] : type.data ()) {
      entt::meta_any v = data.get ({});
      if (!v) {
        continue;
      }

      (void)v.allow_cast<int> ();
      const int value = v.cast<int> ();
      const bool selected = (value == current);

      if (ImGui::Selectable (enum_label (data), selected)) {
        object = value;
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
inspector::draw_meta_sequence (const char *label, entt::meta_any &object)
{
  auto view = object.as_sequence_container ();
  if (!view) {
    return false;
  }

  bool changed = false;

  if (ImGui::TreeNode (label)) {
    std::size_t i = 0;
    for (auto it = view.begin (); it != view.end (); ++it, ++i) {
      entt::meta_any element = *it;
      ImGui::PushID (static_cast<int> (i));
      if (draw_meta_object ("element", element)) {
        *it = element;
        changed = true;
      }
      ImGui::PopID ();
    }
    ImGui::TreePop ();
  }

  return changed;
}

bool
inspector::draw_meta_value (const char *label, entt::meta_any &object,
                            entt::meta_any *prefab_object)
{
  auto type = object.type ();

  if (!m_runtime_ctx->scene_manager ().get_active ()) {
    return false;
  }
  entt::registry &registry
      = m_runtime_ctx->scene_manager ().get_active ()->get_registry ();

  bool changed = false;

  auto draw_restore_button = [&] () {
    if (prefab_object && !meta_any_equal (object, *prefab_object)) {
      ImGui::SameLine ();
      if (ImGui::SmallButton ("Restore")) {
        object = *prefab_object;
        changed = true;
      }
      if (ImGui::IsItemHovered ()) {
        ImGui::SetTooltip ("Restore to prefab default");
      }
    }
  };

  if (type == entt::resolve<entt::entity> ()) {
    auto &scene = *m_runtime_ctx->scene_manager ().get_active ();

    entt::entity const current = object.cast<entt::entity> ();

    const char *preview = "None";
    if (current != entt::null && registry.valid (current)) {
      preview = scene.get_entity_name (current).c_str ();
    }

    if (ImGui::BeginCombo (label, preview)) {

      if (ImGui::Selectable ("None", current == entt::null)) {
        object = entt::null;
        changed = true;
      }

      for (auto e : registry.view<entt::entity> ()) {

        const std::string &name = scene.get_entity_name (e);
        bool const selected = (e == current);

        if (ImGui::Selectable (name.c_str (), selected)) {
          object = e;
          changed = true;
        }

        if (selected) {
          ImGui::SetItemDefaultFocus ();
        }
      }

      ImGui::EndCombo ();
    }

    draw_restore_button ();
    return changed;
  }
  if (type == entt::resolve<wsl::rsc::cubemap_id> ()) {
    wsl::rsc::resource_manager *res_mgr = &m_runtime_ctx->resource_manager ();

    auto &current = object.cast<wsl::rsc::cubemap_id &> ();

    const char *preview = "None";
    static char buf[64];

    if (current.value != entt::null) {
      std::snprintf (buf, sizeof (buf), "Cubemap %08X",
                     (uint32_t)current.value);
      preview = buf;
    }

    if (ImGui::BeginCombo (label, preview)) {

      if (ImGui::Selectable ("None", current.value == entt::null)) {
        current.value = entt::null;
        changed = true;
      }

      for (const auto &rec : res_mgr->list_cubemaps ()) {
        if (rec.state != wsl::rsc::cubemap_state::loaded)
          continue;

        bool selected = (rec.id == current.value);

        char name[64];
        std::snprintf (name, sizeof (name), "Cubemap %08X", (uint32_t)rec.id);

        if (ImGui::Selectable (name, selected)) {
          current.value = rec.id;
          changed = true;
        }

        if (selected)
          ImGui::SetItemDefaultFocus ();
      }

      ImGui::EndCombo ();
    }

    draw_restore_button ();
    return changed;
  } else if (type == entt::resolve<wsl::rsc::model_id> ()) {
    auto &current = object.cast<wsl::rsc::model_id &> ();
    if (current.custom_inspect (label, m_runtime_ctx)) {
      changed = true;
    }
    draw_restore_button ();
    return changed;
  } else if (type == entt::resolve<wsl::rsc::material_id> ()) {
    auto &current = object.cast<wsl::rsc::material_id &> ();
    if (current.custom_inspect (label, m_runtime_ctx)) {
      changed = true;
    }
    draw_restore_button ();
    return changed;
  } else if (type == entt::resolve<wsl::rsc::audio_id> ()) {
    auto &current = object.cast<wsl::rsc::audio_id &> ();
    if (current.custom_inspect (label, m_runtime_ctx)) {
      changed = true;
    }
    draw_restore_button ();
    return changed;
  } else if (type == entt::resolve<wsl::rsc::image_id> ()) {
    auto &current = object.cast<wsl::rsc::image_id &> ();
    if (current.custom_inspect (label, m_runtime_ctx)) {
      changed = true;
    }
    draw_restore_button ();
    return changed;
  } else if (type == entt::resolve<std::string> ()) {
    std::string &str = object.cast<std::string &> ();

    static char buffer[256];
    std::snprintf (buffer, sizeof (buffer), "%s", str.c_str ());

    if (ImGui::InputText (label, buffer, sizeof (buffer))) {
      object = std::string (buffer);
      changed = true;
    }
    draw_restore_button ();
    return changed;
  } else if (type == entt::resolve<bool> ()) {
    bool v = object.cast<bool> ();
    if (draw_field_value (label, &v,
                          wsl::das::das_engine::field_type_kind::boolean,
                          sizeof (bool))) {
      object = v;
      changed = true;
    }
    draw_restore_button ();
    return changed;
  } else if (type == entt::resolve<int> ()) {
    int v = object.cast<int> ();
    if (draw_field_value (label, &v,
                          wsl::das::das_engine::field_type_kind::integer,
                          sizeof (int))) {
      object = v;
      changed = true;
    }
    draw_restore_button ();
    return changed;
  } else if (type == entt::resolve<uint32_t> ()) {
    uint32_t v = object.cast<uint32_t> ();
    if (draw_field_value (
            label, &v, wsl::das::das_engine::field_type_kind::unsigned_integer,
            sizeof (uint32_t))) {
      object = v;
      changed = true;
    }
    draw_restore_button ();
    return changed;
  } else if (type == entt::resolve<float> ()) {
    float v = object.cast<float> ();
    if (draw_field_value (label, &v,
                          wsl::das::das_engine::field_type_kind::floating,
                          sizeof (float))) {
      object = v;
      changed = true;
    }
    draw_restore_button ();
    return changed;
  } else if (type == entt::resolve<glm::mat4> ()) {
    const glm::mat4 &m = object.cast<const glm::mat4 &> ();

    ImGui::TextUnformatted (label);
    ImGui::PushID (label);

    ImGui::BeginGroup ();
    for (int i = 0; i < 4; ++i) {
      ImGui::PushID (i);
      for (int j = 0; j < 4; ++j) {
        ImGui::PushID (j);
        float v = m[i][j];
        ImGui::SetNextItemWidth (ImGui::CalcItemWidth () / 4.1F);
        ImGui::InputFloat ("##v", &v, 0.0F, 0.0F, "%.3f",
                           ImGuiInputTextFlags_ReadOnly);
        if (j < 3)
          ImGui::SameLine ();
        ImGui::PopID ();
      }
      ImGui::PopID ();
    }
    ImGui::EndGroup ();

    ImGui::PopID ();
    return false; // Read-only, so never changed
  }

  return false;
}

bool
inspector::is_hidden (const entt::meta_data &data)
{
  return (data.traits<editor_trait> () & editor_trait::hidden)
         == editor_trait::hidden;
}

void
inspector::draw_add_component_ui (entt::entity entity)
{
  static entt::id_type selected_type = entt::null;

  ImGui::PushFont (m_editor_ctx->get_imgui_renderer ()->get_fonts ().medium);
  ImGui::TextUnformatted ("Add Component");
  ImGui::PopFont ();

  auto &reg = m_runtime_ctx->scene_manager ().get_active ()->get_registry ();
  auto &comp_reg = m_runtime_ctx->component_registry ();

  auto label_for = [] (const wsl::reg::component_registry::descriptor &desc,
                       const entt::meta_type &meta) -> std::string {
    if (meta) {
      return wsl::comp::meta_display_name (meta, desc.display_name.c_str ());
    }
    return desc.display_name;
  };

  const bool has_valid_selection
      = selected_type != entt::null
        && selected_type != entt::type_hash<void>::value ();

  const char *preview = "Select component";
  std::string preview_storage;
  if (has_valid_selection) {
    if (const auto *desc = comp_reg.find (selected_type); desc != nullptr) {
      preview_storage = label_for (*desc, entt::resolve (selected_type));
      preview = preview_storage.c_str ();
    }
  }

  if (ImGui::BeginCombo ("##add_component", preview)) {
    for (const auto *desc : comp_reg.ordered ()) {
      if ((desc == nullptr) || !desc->can_add_default) {
        continue;
      }

      if (desc->is_das_component) {
        if (comp_reg.das_component_contains (reg, desc->type_id, entity)) {
          continue;
        }
      } else if ((desc->contains != nullptr) && desc->contains (reg, entity)) {
        continue;
      }

      const bool selected = (selected_type == desc->type_id);
      const std::string name = label_for (*desc, entt::resolve (desc->type_id));
      if (ImGui::Selectable (name.c_str (), selected)) {
        selected_type = desc->type_id;
      }
      if (selected) {
        ImGui::SetItemDefaultFocus ();
      }
    }

    ImGui::EndCombo ();
  }

  if (selected_type != entt::null) {
    if (ImGui::Button ("Add")) {
      if (const auto *desc = comp_reg.find (selected_type); desc != nullptr) {
        if (desc->is_das_component) {
          comp_reg.das_component_add (reg, selected_type, entity);
        } else if (desc->emplace_default != nullptr) {
          desc->emplace_default (reg, entity);
        }
      }

      selected_type = entt::null;
    }
  }
}

void
inspector::draw_singleton_header (
    const wsl::reg::singleton_registry::descriptor &desc,
    const entt::meta_type &meta, bool can_remove, bool *remove_requested)
{
  ImGui::PushFont (m_editor_ctx->get_imgui_renderer ()->get_fonts ().medium);
  ImGui::TextUnformatted (desc.display_name.c_str ());
  ImGui::PopFont ();

  (void)meta;
  ImGui::TextDisabled ("%s", desc.type_name.c_str ());

  if (!can_remove) {
    ImGui::BeginDisabled ();
  }

  ImGui::SameLine ();
  if (ImGui::SmallButton ("Remove") && (remove_requested != nullptr)) {
    *remove_requested = true;
  }

  if (ImGui::IsItemHovered () && !can_remove) {
    ImGui::SetTooltip ("Core singleton components cannot be removed.");
  }

  if (!can_remove) {
    ImGui::EndDisabled ();
  }

  ImGui::Separator ();
}

} // namespace editor
