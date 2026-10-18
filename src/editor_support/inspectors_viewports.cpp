// Shared root/sub viewport settings widget (TODO E1).
//
// The root viewport (inside the rendering manager) and every subviewport use
// the same `comp::viewport_settings` structure; this widget renders it once
// for both. Mode-relevant properties only: quality knobs and every
// environment entry expose their `use_global` flag inline and hide the stored
// override while it follows the project/scene default.

#include "wsl/comp/singl/runtime_context.hpp"
#include "wsl/comp/viewport_settings.hpp"
#include "wsl/rsc/resource_manager.hpp"

#include <imgui.h>

#include <array>
#include <cstddef>
#include <string>

namespace wsl::comp
{

namespace
{

void
help_marker (const char *text)
{
  ImGui::SameLine ();
  ImGui::TextDisabled ("(?)");
  if (ImGui::IsItemHovered ()) {
    ImGui::SetTooltip ("%s", text);
  }
}

template <typename E, std::size_t N>
bool
enum_combo (const char *label, E &value, const std::array<E, N> &values,
            const std::array<const char *, N> &names)
{
  int current = 0;
  for (std::size_t i = 0; i < N; ++i) {
    if (value == values[i]) {
      current = static_cast<int> (i);
      break;
    }
  }

  bool changed = false;
  if (ImGui::BeginCombo (label, names[static_cast<std::size_t> (current)])) {
    for (std::size_t i = 0; i < N; ++i) {
      bool const selected = (static_cast<int> (i) == current);
      if (ImGui::Selectable (names[i], selected)) {
        value = values[i];
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
msaa_combo (const char *label, int &value)
{
  struct entry
  {
    int samples;
    const char *name;
  };
  static constexpr std::array<entry, 3> entries{ {
      { 1, "Off (1x)" },
      { 2, "2x MSAA" },
      { 4, "4x MSAA" },
  } };

  const char *preview = entries[0].name;
  for (auto const &e : entries) {
    if (e.samples == value) {
      preview = e.name;
      break;
    }
  }

  bool changed = false;
  if (ImGui::BeginCombo (label, preview)) {
    for (auto const &e : entries) {
      bool const selected = (e.samples == value);
      if (ImGui::Selectable (e.name, selected)) {
        value = e.samples;
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

void
draw_cubemap_picker (const char *label, rsc::cubemap_id &id,
                     comp::singl::runtime_context *runtime_ctx)
{
  if (runtime_ctx == nullptr) {
    return;
  }

  std::string preview = "(none)";
  if (id.value != rsc::no_resource_id) {
    if (auto info = runtime_ctx->resource_manager ().info (id)) {
      preview = info->path;
    }
  }

  if (ImGui::BeginCombo (label, preview.c_str ())) {
    if (ImGui::Selectable ("(none)", id.value == rsc::no_resource_id)) {
      id = rsc::cubemap_id{};
    }
    for (auto const &info : runtime_ctx->resource_manager ().list_cubemaps ()) {
      bool const selected = (info.id == id.value);
      if (ImGui::Selectable (info.name.c_str (), selected)) {
        id = rsc::cubemap_id{ info.id };
      }
      if (selected) {
        ImGui::SetItemDefaultFocus ();
      }
    }
    ImGui::EndCombo ();
  }
}

} // namespace

bool
viewport_settings::custom_inspect (const char *label,
                                   comp::singl::runtime_context *runtime_ctx)
{
  bool changed = false;
  ImGui::PushID (label != nullptr ? label : "viewport_settings");

  // ---- Scaling ----
  changed |= enum_combo (
      "Scale Mode", scale_mode,
      std::array{ viewport_scale_mode::disabled, viewport_scale_mode::scale_2d,
                  viewport_scale_mode::viewport },
      std::array{ "Disabled", "2D Scaling", "Viewport Scaling" });
  help_marker ("How this viewport's content maps onto the available output "
               "area (spec section 3).");

  changed |= enum_combo (
      "Aspect Ratio", aspect_policy,
      std::array{ viewport_aspect_policy::ignore, viewport_aspect_policy::keep,
                  viewport_aspect_policy::keep_width,
                  viewport_aspect_policy::keep_height,
                  viewport_aspect_policy::expand },
      std::array{ "Ignore", "Keep", "Keep Width", "Keep Height", "Expand" });
  help_marker ("Fitting policy between the virtual size and the container "
               "(spec section 4).");

  // ---- Quality (use_global follows Project Settings, D5) ----
  ImGui::SeparatorText ("Quality");

  changed |= ImGui::Checkbox ("Filter: Project Default", &filter_use_global);
  if (!filter_use_global) {
    changed |= enum_combo (
        "Filter", filter,
        std::array{ viewport_filter::nearest, viewport_filter::linear },
        std::array{ "Nearest", "Linear" });
  }

  changed |= ImGui::Checkbox ("MSAA: Project Default", &msaa_use_global);
  if (!msaa_use_global) {
    changed |= msaa_combo ("MSAA", msaa);
  }

  changed |= ImGui::Checkbox ("Render Scale: Project Default",
                              &render_scale_use_global);
  if (!render_scale_use_global) {
    changed |= ImGui::DragFloat ("Render Scale", &render_scale, 0.01F, 0.05F,
                                 4.0F, "%.2f");
  }

  // ---- Update / clear ----
  ImGui::SeparatorText ("Update & Clear");

  changed |= enum_combo ("Update Mode", update_mode,
                         std::array{ viewport_update_mode::disabled,
                                     viewport_update_mode::once,
                                     viewport_update_mode::when_visible,
                                     viewport_update_mode::when_parent_visible,
                                     viewport_update_mode::always },
                         std::array{ "Disabled", "Once", "When Visible",
                                     "When Parent Visible", "Always" });
  help_marker ("`When Visible` culls 2D rects against the parent's visible "
               "virtual area and 3D quads against the parent camera frustum.");

  changed |= enum_combo ("Clear Mode", clear_mode,
                         std::array{ viewport_clear_mode::always,
                                     viewport_clear_mode::next_frame,
                                     viewport_clear_mode::never },
                         std::array{ "Always", "Next Frame", "Never" });

  // ---- Environment ----
  if (ImGui::TreeNodeEx ("Environment", ImGuiTreeNodeFlags_DefaultOpen)) {
    viewport_environment &env = this->env;

    // Skybox
    if (ImGui::TreeNodeEx ("Skybox", ImGuiTreeNodeFlags_DefaultOpen)) {
      changed
          |= ImGui::Checkbox ("Skybox: Scene Default", &env.skybox_use_global);
      if (!env.skybox_use_global) {
        draw_cubemap_picker ("Skybox", env.skybox, runtime_ctx);
      }
      changed |= ImGui::Checkbox ("Rotation: Scene Default",
                                  &env.skybox_rotation_use_global);
      if (!env.skybox_rotation_use_global) {
        float rotation[4]
            = { env.skybox_rotation.x (), env.skybox_rotation.y (),
                env.skybox_rotation.z (), env.skybox_rotation.w () };
        if (ImGui::DragFloat4 ("Rotation", rotation, 0.01F, -1.0F, 1.0F,
                               "%.3f")) {
          env.skybox_rotation = math::quatf{ rotation[0], rotation[1],
                                             rotation[2], rotation[3] };
          changed = true;
        }
      }
      ImGui::TreePop ();
    }

    // Ambient
    if (ImGui::TreeNodeEx ("Ambient", ImGuiTreeNodeFlags_DefaultOpen)) {
      changed |= ImGui::Checkbox ("Ambient: Scene Default",
                                  &env.ambient_use_global);
      if (!env.ambient_use_global) {
        float color[3] = { env.ambient_color.x (), env.ambient_color.y (),
                           env.ambient_color.z () };
        if (ImGui::ColorEdit3 ("Ambient Color", color)) {
          env.ambient_color = math::vec3f{ color[0], color[1], color[2] };
          changed = true;
        }
        changed
            |= ImGui::DragFloat ("Ambient Intensity", &env.ambient_intensity,
                                 0.001F, 0.0F, 8.0F, "%.3f");
      }
      ImGui::TreePop ();
    }

    // IBL + exposure
    changed |= ImGui::Checkbox ("IBL: Scene Default", &env.ibl_use_global);
    if (!env.ibl_use_global) {
      changed |= ImGui::DragFloat ("IBL Intensity", &env.ibl_intensity, 0.01F,
                                   0.0F, 16.0F, "%.2f");
    }

    changed |= ImGui::Checkbox ("Exposure: Scene Default",
                                &env.exposure_use_global);
    if (!env.exposure_use_global) {
      changed |= ImGui::DragFloat ("Exposure", &env.exposure, 0.01F, 0.0F,
                                   16.0F, "%.2f");
    }

    // Bloom
    if (ImGui::TreeNode ("Bloom")) {
      changed |= ImGui::Checkbox ("Bloom Enabled", &env.bloom_enabled);
      help_marker ("Sub-viewports do not allocate bloom targets unless this "
                   "is enabled (D12).");
      changed
          |= ImGui::Checkbox ("Bloom: Scene Default", &env.bloom_use_global);
      if (!env.bloom_use_global) {
        changed |= ImGui::DragFloat ("Threshold", &env.bloom_threshold, 0.01F,
                                     0.0F, 32.0F, "%.2f");
        changed |= ImGui::DragFloat ("Knee", &env.bloom_knee, 0.01F, 0.0F, 1.0F,
                                     "%.2f");
        changed |= ImGui::DragFloat ("Intensity", &env.bloom_intensity, 0.01F,
                                     0.0F, 16.0F, "%.2f");
      }
      ImGui::TreePop ();
    }

    // SSAO
    if (ImGui::TreeNode ("SSAO")) {
      changed |= ImGui::Checkbox ("SSAO: Scene Default", &env.ssao_use_global);
      if (!env.ssao_use_global) {
        changed |= ImGui::Checkbox ("SSAO Enabled", &env.ssao_enabled);
        changed |= ImGui::DragFloat ("Radius", &env.ssao_radius, 0.01F, 0.01F,
                                     8.0F, "%.2f");
        changed |= ImGui::DragFloat ("Bias", &env.ssao_bias, 0.001F, 0.0F, 0.5F,
                                     "%.3f");
        changed |= ImGui::DragFloat ("Power", &env.ssao_power, 0.01F, 0.1F,
                                     8.0F, "%.2f");
        changed |= ImGui::DragFloat ("Intensity", &env.ssao_intensity, 0.01F,
                                     0.0F, 8.0F, "%.2f");
      }
      ImGui::TreePop ();
    }

    // Clear
    if (ImGui::TreeNodeEx ("Clear", ImGuiTreeNodeFlags_DefaultOpen)) {
      changed
          |= ImGui::Checkbox ("Clear Color Enabled", &env.clear_color_enabled);
      changed |= ImGui::Checkbox ("Clear Color: Scene Default",
                                  &env.clear_color_use_global);
      if (!env.clear_color_use_global) {
        float color[4] = { env.clear_color.x (), env.clear_color.y (),
                           env.clear_color.z (), env.clear_color.w () };
        if (ImGui::ColorEdit4 ("Clear Color", color)) {
          env.clear_color
              = math::vec4f{ color[0], color[1], color[2], color[3] };
          changed = true;
        }
      }
      changed |= ImGui::Checkbox ("Clear Depth", &env.clear_depth);
      ImGui::TreePop ();
    }

    ImGui::TreePop ();
  }

  // ---- Culling opt-outs ----
  ImGui::SeparatorText ("Shadows");
  changed |= ImGui::Checkbox ("Shadow Caster", &shadow_caster);
  help_marker ("When off, this viewport's subtree is excluded from shadow "
               "casting (S5).");

  ImGui::PopID ();
  return changed;
}

} // namespace wsl::comp
