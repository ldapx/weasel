#include "ui_view.hpp"

#include "ui_util.hpp"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/Traits.h>

#include <cstdio>
#include <filesystem>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include "wsl/comp/component_meta.hpp"
#include "wsl/comp/hierarchy.hpp"
#include "wsl/comp/singl/editor_context.hpp"
#include "wsl/comp/transform.hpp"
#include "wsl/math/vector.hpp"
#include "wsl/reg/component_registry.hpp"
#include "wsl/reg/singleton_registry.hpp"
#include "wsl/rsc/scene.hpp"
#include "wsl/rsc/scene_manager.hpp"

#include "wsl/comp/singl/runtime_context.hpp"

namespace editor_rml
{

void
ui_view::append_comp_header (std::string &html, std::uint64_t key,
                             const std::string &display_name,
                             const std::string &icon_src)
{
  const bool collapsed = model_.collapsed_comps.contains (key);
  html += "<div class='comp'><div id='cmp-";
  html += std::to_string (key);
  html += "' class='comp-name'><span class='toggle";
  if (!collapsed) {
    html += " expanded";
  }
  html += "'>";
  html += collapsed ? "+" : "-";
  html += "</span>";
  if (!icon_src.empty ()) {
    html += "<svg class='comp-icon' src='";
    html += html_escape (icon_src);
    html += "' width='16' height='16'></svg>";
  }
  html += html_escape (display_name);
  html += "</div>";
}

/**
 * Maps a reflected type's ``engine://icons/<name>.svg`` icon to the SVG
 * next to the editor documents (``../icons/<name>.svg``), or "" when it
 * is missing. Rendered with RmlUi's SVG plugin (LunaSVG) `<svg>` element.
 */

std::string
ui_view::component_icon_src (const entt::meta_type &meta)
{
  if (!meta) {
    return {};
  }
  const entt::id_type type = meta.info ().hash ();
  const auto cached = icon_src_cache.find (type);
  if (cached != icon_src_cache.end ()) {
    return cached->second;
  }
  std::string src;
  const std::string path = wsl::comp::meta_icon_path (meta);
  const std::size_t slash = path.rfind ('/');
  std::string stem
      = (slash == std::string::npos) ? path : path.substr (slash + 1);
  const std::size_t dot = stem.rfind ('.');
  if (dot != std::string::npos) {
    stem = stem.substr (0, dot);
  }
  if (!stem.empty ()) {
    const std::string rel = "../icons/" + stem + ".svg";
    try {
      if (std::filesystem::exists (editor_document_root (model_.svc) + "/"
                                   + rel)) {
        src = rel;
      }
    } catch (...) {
    }
  }
  icon_src_cache[type] = src;
  return src;
}

/**
 * Emits one inspector field row: a bool toggle button, one text input per
 * vector/quaternion lane, a single text input for other editable scalars,
 * or read-only text. Transform rotation edits in Euler degrees; hierarchy
 * first/next stay read-only (parent commits go through the validated
 * re-parent helper).
 */

void
ui_view::append_field_row (std::string &html, entt::id_type comp_type,
                           entt::id_type field_id, const entt::meta_data &data,
                           entt::meta_any value)
{
  const entt::id_type k_transform = static_cast<entt::id_type> (
      entt::type_hash<wsl::comp::transform>::value ());
  const entt::id_type k_hierarchy = static_cast<entt::id_type> (
      entt::type_hash<wsl::comp::hierarchy>::value ());
  const entt::id_type k_rotation
      = static_cast<entt::id_type> (entt::hashed_string{ "rotation" });
  const entt::id_type k_first
      = static_cast<entt::id_type> (entt::hashed_string{ "first" });
  const entt::id_type k_next
      = static_cast<entt::id_type> (entt::hashed_string{ "next" });

  const entt::meta_type value_type = value.type ();

  // Lane values for vector/quaternion fields (Euler degrees for the
  // transform rotation).
  std::vector<float> lanes;
  if (comp_type == k_transform && field_id == k_rotation && value) {
    if (const auto *q = value.try_cast<wsl::math::quatf> ()) {
      const glm::vec3 euler
          = glm::degrees (glm::eulerAngles (static_cast<glm::quat> (*q)));
      lanes = { euler.x, euler.y, euler.z };
    }
  } else if (value_type == entt::resolve<wsl::math::vec2f> ()) {
    if (const auto *v = value.try_cast<wsl::math::vec2f> ()) {
      lanes = { v->x (), v->y () };
    }
  } else if (value_type == entt::resolve<wsl::math::vec3f> ()) {
    if (const auto *v = value.try_cast<wsl::math::vec3f> ()) {
      lanes = { v->x (), v->y (), v->z () };
    }
  } else if (value_type == entt::resolve<wsl::math::vec4f> ()) {
    if (const auto *v = value.try_cast<wsl::math::vec4f> ()) {
      lanes = { v->x (), v->y (), v->z (), v->w () };
    }
  } else if (value_type == entt::resolve<wsl::math::quatf> ()) {
    if (const auto *v = value.try_cast<wsl::math::quatf> ()) {
      lanes = { v->x (), v->y (), v->z (), v->w () };
    }
  }

  std::string text;
  if (lanes.empty ()) {
    text = format_meta_value (value);
  }

  bool read_only = !field_kind_editable (value_type);
  if (comp_type == k_hierarchy && (field_id == k_first || field_id == k_next)) {
    read_only = true;
  }

  html += "<div class='field'><span class='field-name'>";
  html += html_escape (wsl::comp::meta_display_name (data, "<unnamed>"));
  html += "</span>";
  if (!lanes.empty () && !read_only) {
    html += "<span class='field-lanes'>";
    const int width = 100 / static_cast<int> (lanes.size ()) - 1;
    for (std::size_t i = 0; i < lanes.size (); ++i) {
      char buffer[32];
      std::snprintf (buffer, sizeof (buffer), "%.3f",
                     static_cast<double> (lanes[i]));
      html += "<input type='text' id='fld-";
      html += std::to_string (comp_type);
      html += "-";
      html += std::to_string (field_id);
      html += "-";
      html += std::to_string (i);
      html += "' value='";
      html += html_escape (buffer);
      html += "' style='width: ";
      html += std::to_string (width);
      html += "%;'/>";
    }
    html += "</span>";
  } else if (!read_only && value_type == entt::resolve<bool> ()) {
    html += "<button id='fldb-";
    html += std::to_string (comp_type);
    html += "-";
    html += std::to_string (field_id);
    html += "' class='boolbtn'>";
    html += html_escape (text);
    html += "</button>";
  } else if (!read_only) {
    html += "<input type='text' id='fld-";
    html += std::to_string (comp_type);
    html += "-";
    html += std::to_string (field_id);
    html += "' value='";
    html += html_escape (text);
    html += "'/>";
  } else {
    html += "<span class='field-value'>";
    html += html_escape (text);
    html += "</span>";
  }
  html += "</div>";
}

/**
 * Returns ``true`` when keyboard focus sits inside the inspector (i.e. the
 * user is editing a field). Periodic refreshes must skip rebuilding then,
 * or typing would be wiped out from under the cursor.
 */

bool
ui_view::inspector_input_focused ()
{
  if (context_ == nullptr || document_ == nullptr) {
    return false;
  }
  Rml::Element *focused = context_->GetFocusElement ();
  Rml::Element *panel = document_->GetElementById ("inspector");
  if (focused == nullptr || panel == nullptr) {
    return false;
  }
  for (Rml::Element *el = focused; el != nullptr; el = el->GetParentNode ()) {
    if (el == panel) {
      return true;
    }
  }
  return false;
}

/** Live lookup target for one inspector commit. */
struct inspector_target
{
  entt::registry *reg = nullptr;
  entt::meta_any instance;
  entt::entity entity = entt::null;
};

/**
 * Resolves the live object behind an inspector row. Rejects stale rows
 * whose component type no longer matches the current selection.
 */

void
ui_view::refresh_inspector ()
{
  Rml::Element *inspector = document_->GetElementById ("inspector");
  if (inspector == nullptr) {
    return;
  }

  // Never rebuild under a focused input: typing would be wiped out.
  // Selection changes and commits set model_.inspector_refresh_forced instead.
  if (!model_.inspector_refresh_forced && inspector_input_focused ()) {
    return;
  }
  model_.inspector_refresh_forced = false;

  auto *scene = model_.svc.runtime->scene_manager ().get_active ();
  const entt::entity selected = model_.svc.editor->selected_entity ();

  if (model_.selected_singleton.has_value ()) {
    if (scene == nullptr) {
      set_rml_if_changed (inspector, "<div class='dim'>No active scene.</div>",
                          inspector_html_cache);
      return;
    }
    entt::registry &reg = scene->get_registry ();
    const auto *desc = model_.svc.runtime->singleton_registry ().find (
        *model_.selected_singleton);
    if (desc == nullptr) {
      set_rml_if_changed (inspector,
                          "<div class='dim'>Unregistered singleton.</div>",
                          inspector_html_cache);
      return;
    }
    void *instance_ptr
        = (desc->get_ptr != nullptr) ? desc->get_ptr (reg) : nullptr;
    std::string html;
    html += "<div class='ent-title'>";
    html += html_escape (desc->display_name);
    html += " <span class='dim'>singleton</span></div>";
    if (instance_ptr == nullptr) {
      html += "<div class='dim'>Singleton not present in scene.</div>";
      set_rml_if_changed (inspector, html, inspector_html_cache);
      return;
    }
    const entt::meta_type meta = entt::resolve (desc->type_id);
    if (!meta) {
      if (desc->is_das_singleton) {
        const std::string json
            = model_.svc.runtime->singleton_registry ().das_singleton_json (
                reg, desc->type_id);
        const std::uint64_t sing_key
            = ~static_cast<std::uint64_t> (desc->type_id);
        append_comp_header (html, sing_key, desc->display_name);
        if (!model_.collapsed_comps.contains (sing_key)) {
          html += "<div class='mono line'>";
          html += html_escape (json.empty () ? "{}" : json);
          html += "</div>";
        }
        html += "</div>";
      } else {
        html += "<div class='dim'>No reflection data.</div>";
      }
      set_rml_if_changed (inspector, html, inspector_html_cache);
      return;
    }
    entt::meta_any instance = meta.from_void (instance_ptr);
    if (!instance) {
      html += "<div class='dim'>Failed to inspect singleton.</div>";
      set_rml_if_changed (inspector, html, inspector_html_cache);
      return;
    }
    const std::uint64_t sing_key = ~static_cast<std::uint64_t> (desc->type_id);
    append_comp_header (html, sing_key, desc->display_name,
                        component_icon_src (meta));
    if (!model_.collapsed_comps.contains (sing_key)) {
      for (auto &&[data_id, data] : meta.data ()) {
        if (field_hidden (data)) {
          continue;
        }
        entt::meta_any value = data.get (instance);
        if (!value) {
          continue;
        }
        append_field_row (html, desc->type_id, data_id, data, value);
      }
    }
    html += "</div>";
    set_rml_if_changed (inspector, html, inspector_html_cache);
    return;
  }

  if ((scene == nullptr) || (selected == entt::null)) {
    set_rml_if_changed (
        inspector,
        "<div class='empty-message'>Select an entity in the hierarchy.</div>",
        inspector_html_cache);
    return;
  }

  entt::registry &reg = scene->get_registry ();
  if (!reg.valid (selected)) {
    set_rml_if_changed (inspector,
                        "<div class='empty-message'>Selection is "
                        "invalid.</div>",
                        inspector_html_cache);
    return;
  }

  std::string html;
  html += "<div class='ent-title'>";
  html += html_escape (scene->get_entity_name (selected));
  html += "<span class='eid'>#";
  html += std::to_string (static_cast<std::uint32_t> (selected));
  html += "</span></div>";

  auto &comp_reg = model_.svc.runtime->component_registry ();

  for (auto &&[internal_id, storage] : reg.storage ()) {
    if (!storage.contains (selected)) {
      continue;
    }

    const entt::id_type type_id = comp_reg.to_stable_id (internal_id);
    const auto *descriptor = comp_reg.find (type_id);
    const std::string display_name
        = (descriptor != nullptr) ? descriptor->display_name : "Unknown";

    const entt::meta_type meta = entt::resolve (type_id);
    const std::uint64_t comp_key = static_cast<std::uint64_t> (type_id);
    const bool comp_collapsed = model_.collapsed_comps.contains (comp_key);
    append_comp_header (html, comp_key, display_name,
                        component_icon_src (meta));

    if (comp_collapsed) {
      html += "</div>";
      continue;
    }

    if (meta) {
      entt::meta_any instance = meta.from_void (storage.value (selected));
      if (instance) {
        for (auto &&[data_id, data] : meta.data ()) {
          if (field_hidden (data)) {
            continue;
          }

          entt::meta_any value = data.get (instance);
          if (!value) {
            continue;
          }

          append_field_row (html, type_id, data_id, data, value);
        }
      }
    } else {
      html += "<div class='dim'>No reflection data.</div>";
    }

    html += "</div>";
  }

  set_rml_if_changed (inspector, html, inspector_html_cache);
}
} // namespace editor_rml
