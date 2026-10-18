// ImGui inspector widgets for engine components (moved out of the core
// library so libwsl stays free of ImGui). The reflected data fields remain
// registered by the core; register_editor_inspectors() attaches these
// widgets on top.

#include "wsl/comp/animator.hpp"
#include "wsl/comp/camera.hpp"
#include "wsl/comp/camera_2d.hpp"
#include "wsl/comp/hierarchy.hpp"
#include "wsl/comp/rigid_body.hpp"
#include "wsl/comp/singl/editor_context.hpp"
#include "wsl/comp/singl/runtime_context.hpp"
#include "wsl/comp/subviewport.hpp"
#include "wsl/comp/transform_2d.hpp"
#include "wsl/comp/world_transform.hpp"
#include "wsl/phys/layers.hpp"
#include "wsl/rsc/scene.hpp"
#include "wsl/rsc/scene_manager.hpp"

#include <algorithm>
#include <cstdio>
#include <imgui.h>
#include <string>
#include <vector>

namespace wsl
{

namespace comp
{

namespace
{

// ---------------- rigid_body helpers ----------------

bool
draw_layer_button (const char *label, bool active)
{
  const ImVec2 size{ 28.0F, 28.0F };
  const ImVec4 color = active ? ImVec4 (0.26F, 0.56F, 0.96F, 1.0F)
                              : ImVec4 (0.19F, 0.20F, 0.23F, 1.0F);
  const ImVec4 hovered = active ? ImVec4 (0.33F, 0.63F, 1.0F, 1.0F)
                                : ImVec4 (0.25F, 0.27F, 0.31F, 1.0F);
  const ImVec4 pressed = active ? ImVec4 (0.21F, 0.49F, 0.88F, 1.0F)
                                : ImVec4 (0.16F, 0.17F, 0.20F, 1.0F);

  ImGui::PushStyleColor (ImGuiCol_Button, color);
  ImGui::PushStyleColor (ImGuiCol_ButtonHovered, hovered);
  ImGui::PushStyleColor (ImGuiCol_ButtonActive, pressed);
  const bool clicked = ImGui::Button (label, size);
  ImGui::PopStyleColor (3);
  return clicked;
}

template <typename IsActiveFn, typename OnPressFn>
bool
draw_layer_grid (const char *label, IsActiveFn &&is_active,
                 OnPressFn &&on_press)
{
  bool changed = false;

  ImGui::PushID (label);
  ImGui::BeginGroup ();

  for (uint32_t i = 0; i < phys::layers::collision_layer_count; ++i) {
    if (i > 0 && (i % 4) != 0) {
      ImGui::SameLine ();
    }

    char button_label[4];
    std::snprintf (button_label, sizeof (button_label), "%u", i + 1);

    if (draw_layer_button (button_label, is_active (i))) {
      on_press (i);
      changed = true;
    }
  }

  ImGui::EndGroup ();
  ImGui::PopID ();

  return changed;
}

// ---------------- animator helpers ----------------

/** Formats seconds as m:ss for the transport readout. */
std::string
format_time (float seconds)
{
  if (!(seconds > 0.0F)) {
    return "--:--";
  }
  const int total = static_cast<int> (seconds);
  char buffer[32];
  std::snprintf (buffer, sizeof (buffer), "%d:%02d", total / 60, total % 60);
  return buffer;
}

/** Shows the assigned path read-only, with a hint when it is derived. */
void
draw_path_field (const char *label, const std::string &value)
{
  ImGui::TextDisabled ("%s", label);
  ImGui::SameLine ();
  ImGui::TextUnformatted (value.empty () || value == "None" ? "(derived)"
                                                            : value.c_str ());
}

} // namespace

// ---------------- camera ----------------

bool
camera::custom_inspect (const char * /*label*/,
                        comp::singl::runtime_context *runtime_ctx)
{
  if (runtime_ctx == nullptr) {
    return false;
  }

  bool changed = false;

  changed |= ImGui::DragFloat ("FOV", &m_fov, 0.5F, 1.0F, 179.0F, "%.1f");
  changed
      |= ImGui::DragFloat ("Near", &m_near, 0.01F, 0.01F, m_far * 0.5F, "%.3f");
  changed |= ImGui::DragFloat ("Far", &m_far, 0.1F, m_near * 2.0F, 10000.0F,
                               "%.1f");
  changed |= ImGui::Checkbox ("Only For Editor", &m_only_for_editor);

  return changed;
}

// ---------------- world_transform ----------------

bool
world_transform::custom_inspect (const char * /*label*/,
                                 comp::singl::runtime_context * /*runtime*/)
{
  if (ImGui::TreeNodeEx ("Matrix", 0)) {
    for (int i = 0; i < 4; ++i) {
      ImGui::PushID (i);
      for (int j = 0; j < 4; ++j) {
        ImGui::PushID (j);
        float v = m_value[i][j];
        ImGui::SetNextItemWidth (ImGui::CalcItemWidth () / 4.1F);
        ImGui::InputFloat ("##v", &v, 0.0F, 0.0F, "%.3f",
                           ImGuiInputTextFlags_ReadOnly);
        if (j < 3)
          ImGui::SameLine ();
        ImGui::PopID ();
      }
      ImGui::PopID ();
    }
    ImGui::TreePop ();
  }
  return false; // read-only
}

// ---------------- subviewport_camera_ui ----------------

bool
subviewport_camera_ui::custom_inspect (
    const char * /*label*/, comp::singl::runtime_context *runtime_ctx)
{
  if (runtime_ctx == nullptr || runtime_ctx->editor_ctx () == nullptr) {
    return false;
  }

  bool changed = false;
  rsc::scene *scene = runtime_ctx->scene_manager ().get_active ();
  if (scene == nullptr) {
    return false;
  }

  entt::registry &registry = scene->get_registry ();

  // The entity currently being inspected in the editor
  entt::entity self = runtime_ctx->editor_ctx ()->selected_entity ();
  if (self == entt::null || !registry.all_of<subviewport> (self)) {
    self = entt::null;
  }

  // Component-driven mode: 2D subviewports (transform_2d) list
  // camera_2d descendants; 3D subviewports list camera descendants (S2/E1).
  bool const filter_2d
      = (self != entt::null) && registry.all_of<comp::transform_2d> (self);

  const char *preview = "None";
  if (value != entt::null && registry.valid (value)) {
    preview = scene->get_entity_name (value).c_str ();
  }

  if (ImGui::BeginCombo ("##camera_combo", preview)) {
    if (ImGui::Selectable ("None", value == entt::null)) {
      value = entt::null;
      changed = true;
    }

    if (self != entt::null) {
      // Collect descendants that match the camera type filter
      std::vector<entt::entity> stack;
      auto h_view = registry.view<hierarchy> ();
      for (auto const e : h_view) {
        if (h_view.get<hierarchy> (e).parent == self) {
          stack.push_back (e);
        }
      }

      while (!stack.empty ()) {
        entt::entity const e = stack.back ();
        stack.pop_back ();

        bool matches_filter = false;
        if (filter_2d) {
          matches_filter = registry.all_of<comp::camera_2d> (e);
        } else {
          matches_filter = registry.all_of<comp::camera> (e);
        }

        if (matches_filter) {
          const std::string &name = scene->get_entity_name (e);
          bool const selected = (e == value);
          if (ImGui::Selectable (name.c_str (), selected)) {
            value = e;
            changed = true;
          }
          if (selected) {
            ImGui::SetItemDefaultFocus ();
          }
        }

        // push children
        for (auto const child : h_view) {
          if (h_view.get<hierarchy> (child).parent == e) {
            stack.push_back (child);
          }
        }
      }
    }
    ImGui::EndCombo ();
  }

  return changed;
}

// ---------------- animator ----------------

bool
animator::custom_inspect (const char * /*label*/,
                          comp::singl::runtime_context * /*runtime*/)
{
  bool changed = false;

  // ---- clip picker ----
  // Only clips the owning model actually declares are offered, so a
  // multi-model scene cannot assign a clip with mismatched joints.
  if (available_clips.empty ()) {
    ImGui::TextDisabled ("No animation clips for this model");
  } else {
    int current = -1;
    for (std::size_t i = 0; i < available_clips.size (); ++i) {
      if (available_clips[i].path == clip_path) {
        current = static_cast<int> (i);
        break;
      }
    }

    std::string preview
        = current >= 0
              ? available_clips[static_cast<std::size_t> (current)].name
              : std::string ("(none)");

    if (ImGui::BeginCombo ("Clip", preview.c_str ())) {
      if (ImGui::Selectable ("(none)", current < 0)) {
        clip_path = "None";
        time = 0.0F;
        finished = false;
        changed = true;
      }
      for (std::size_t i = 0; i < available_clips.size (); ++i) {
        const bool selected = (static_cast<int> (i) == current);
        if (ImGui::Selectable (available_clips[i].name.c_str (), selected)) {
          clip_path = available_clips[i].path;
          time = 0.0F;
          finished = false;
          changed = true;
        }
        if (selected) {
          ImGui::SetItemDefaultFocus ();
        }
      }
      ImGui::EndCombo ();
    }
  }

  draw_path_field ("Skeleton", skeleton_path);

  // ---- playback settings ----
  ImGui::PushID (this);
  ImGui::Spacing ();

  ImGui::SetNextItemWidth (120.0F);
  if (ImGui::SliderFloat ("Speed", &speed, -4.0F, 4.0F, "%.2fx")) {
    changed = true;
  }
  ImGui::SameLine ();
  if (ImGui::Checkbox ("Loop", &loop)) {
    changed = true;
  }

  ImGui::SetNextItemWidth (120.0F);
  if (ImGui::DragFloat ("Crossfade", &crossfade_duration, 0.01F, 0.0F, 10.0F,
                        "%.2f s")) {
    crossfade_duration = std::max (0.0F, crossfade_duration);
    changed = true;
  }

  int skin = skin_index;
  ImGui::SetNextItemWidth (120.0F);
  if (ImGui::DragInt ("Skin", &skin, 1, 0)) {
    skin_index = std::max (0, skin);
    changed = true;
  }

  // ---- transport ----
  // A clip switch resets the clock, so scrubbing to the end here is a
  // deliberate authoring action rather than a live-playback side effect.
  ImGui::Spacing ();
  ImGui::SetNextItemWidth (200.0F);
  float const max_time = std::max (clip_duration, 0.001F);
  if (ImGui::SliderFloat ("Time", &time, 0.0F, max_time,
                          format_time (time).c_str ())) {
    finished = false;
    changed = true;
  }
  ImGui::SameLine ();
  if (ImGui::Button (playing ? "Pause" : "Play")) {
    playing = !playing;
    paused = false;
    finished = false;
    changed = true;
  }
  ImGui::SameLine ();
  ImGui::TextDisabled ("%s / %s", format_time (time).c_str (),
                       format_time (clip_duration).c_str ());

  ImGui::PopID ();
  return changed;
}

// ---------------- rigid_body::allowed_dofs_ui ----------------

bool
rigid_body::allowed_dofs_ui::custom_inspect (
    const char *label, comp::singl::runtime_context * /*unused*/)
{
  static const char *const names[] = { "All",
                                       "Translation X",
                                       "Translation Y",
                                       "Translation Z",
                                       "Rotation X",
                                       "Rotation Y",
                                       "Rotation Z",
                                       "Translation XY",
                                       "Translation XZ",
                                       "Translation YZ",
                                       "Rotation XY",
                                       "Rotation XZ",
                                       "Rotation YZ" };

  static const phys::allowed_dofs vals[] = {
    phys::allowed_dofs::All,
    phys::allowed_dofs::TranslationX,
    phys::allowed_dofs::TranslationY,
    phys::allowed_dofs::TranslationZ,
    phys::allowed_dofs::RotationX,
    phys::allowed_dofs::RotationY,
    phys::allowed_dofs::RotationZ,
    phys::allowed_dofs::TranslationX | phys::allowed_dofs::TranslationY,
    phys::allowed_dofs::TranslationX | phys::allowed_dofs::TranslationZ,
    phys::allowed_dofs::TranslationY | phys::allowed_dofs::TranslationZ,
    phys::allowed_dofs::RotationX | phys::allowed_dofs::RotationY,
    phys::allowed_dofs::RotationX | phys::allowed_dofs::RotationZ,
    phys::allowed_dofs::RotationY | phys::allowed_dofs::RotationZ,
  };

  int cur = 0;
  for (int i = 0; i < (int)(sizeof (vals) / sizeof (vals[0])); ++i) {
    if (value == vals[i]) {
      cur = i;
      break;
    }
  }

  bool changed = false;
  if (ImGui::BeginCombo (label, names[cur])) {
    for (int i = 0; i < (int)(sizeof (vals) / sizeof (vals[0])); ++i) {
      const bool sel = (i == cur);
      if (ImGui::Selectable (names[i], sel)) {
        value = vals[i];
        changed = true;
      }
      if (sel) {
        ImGui::SetItemDefaultFocus ();
      }
    }
    ImGui::EndCombo ();
  }
  return changed;
}

// ---------------- rigid_body::motion_type_ui ----------------

bool
rigid_body::motion_type_ui::custom_inspect (
    const char *label, comp::singl::runtime_context * /*unused*/)
{
  static const char *const names[] = { "Static", "Kinematic", "Dynamic" };
  static const phys::motion_type vals[]
      = { phys::motion_type::Static, phys::motion_type::Kinematic,
          phys::motion_type::Dynamic };

  int cur = 0;
  for (int i = 0; i < 3; ++i) {
    if (value == vals[i]) {
      cur = i;
      break;
    }
  }

  bool changed = false;
  if (ImGui::BeginCombo (label, names[cur])) {
    for (int i = 0; i < 3; ++i) {
      const bool sel = (i == cur);
      if (ImGui::Selectable (names[i], sel)) {
        value = vals[i];
        changed = true;
      }
      if (sel) {
        ImGui::SetItemDefaultFocus ();
      }
    }
    ImGui::EndCombo ();
  }

  return changed;
}

// ---------------- rigid_body::collision_layer_ui ----------------

bool
rigid_body::collision_layer_ui::custom_inspect (
    const char *label, comp::singl::runtime_context * /*unused*/)
{
  return draw_layer_grid (
      label, [this] (uint32_t index) { return value == index; },
      [this] (uint32_t index) {
        value = phys::layers::clamp_layer_index (
            static_cast<phys::layers::layer_index_t> (index));
      });
}

// ---------------- rigid_body::collision_mask_ui ----------------

bool
rigid_body::collision_mask_ui::custom_inspect (
    const char *label, comp::singl::runtime_context * /*unused*/)
{
  return draw_layer_grid (
      label,
      [this] (uint32_t index) {
        return (value
                & phys::layers::bit_for_layer (
                    static_cast<phys::layers::layer_index_t> (index)))
               != 0;
      },
      [this] (uint32_t index) {
        const phys::layers::layer_mask_t bit = phys::layers::bit_for_layer (
            static_cast<phys::layers::layer_index_t> (index));
        value = (value & bit) != 0 ? value & ~bit : value | bit;
        value = phys::layers::clamp_layer_mask (value);
      });
}

} // namespace comp

} // namespace wsl
