// ImGui inspector widgets for the engine math types (vec/mat/quat).
//
// Moved out of the core math headers so libwsl stays free of ImGui; the core
// register_meta() functions only register the reflected data fields and
// register_editor_inspectors() attaches these widgets on top.

#include "wsl/math/matrix.hpp"
#include "wsl/math/vector.hpp"

#include <glm/gtc/quaternion.hpp>
#include <imgui.h>
#include <imgui_internal.h>

namespace wsl
{
namespace math
{

bool
vec2f::custom_inspect (const char *label)
{
  auto draw_drag_with_stripe
      = [] (const char *id, float &v, ImU32 stripe_col) -> bool {
    ImGui::SetNextItemWidth (ImMax (1.0F, ImGui::CalcItemWidth ()));
    bool const changed = ImGui::DragFloat (id, &v, 0.1F);
    ImDrawList *dl = ImGui::GetWindowDrawList ();
    ImVec2 const mn = ImGui::GetItemRectMin ();
    ImVec2 const mx = ImGui::GetItemRectMax ();
    const float stripe_w = 3.0F;
    dl->AddRectFilled (mn, ImVec2 (mn.x + stripe_w, mx.y), stripe_col);
    return changed;
  };

  ImGui::PushID (label);

  float const full = ImGui::CalcItemWidth ();
  float const spacing = ImGui::GetStyle ().ItemInnerSpacing.x;
  float const w = (full - spacing) / 2.0F;

  bool changed = false;

  ImGui::SetNextItemWidth (w);
  changed |= draw_drag_with_stripe ("##x", m_x, IM_COL32 (255, 0, 0, 255));
  ImGui::SameLine (0.0F, spacing);

  ImGui::SetNextItemWidth (w);
  changed |= draw_drag_with_stripe ("##y", m_y, IM_COL32 (0, 200, 0, 255));

  ImGui::PopID ();
  return changed;
}

bool
vec3f::custom_inspect (const char *label)
{
  // Draw 3 floats on one line, each with a colored stripe on the left.
  // Stripe colors: X=red, Y=blue, Z=green (as requested).

  auto draw_drag_with_stripe
      = [] (const char *id, float &v, ImU32 stripe_col) -> bool {
    // Keep width reasonable even when the caller already placed us on
    // SameLine.
    ImGui::SetNextItemWidth (ImMax (1.0F, ImGui::CalcItemWidth ()));

    bool const changed = ImGui::DragFloat (id, &v, 0.1F);

    // Stripe overlay on the widget we just drew:
    ImDrawList *dl = ImGui::GetWindowDrawList ();
    ImVec2 const mn = ImGui::GetItemRectMin ();
    ImVec2 const mx = ImGui::GetItemRectMax ();

    const float stripe_w = 3.0F;
    dl->AddRectFilled (mn, ImVec2 (mn.x + stripe_w, mx.y), stripe_col);

    return changed;
  };

  // Use label as an ID seed so multiple vec3f on the same window don't
  // collide.
  ImGui::PushID (label);

  // Split available width into 3 items with spacing.
  float const full = ImGui::CalcItemWidth ();
  float const spacing = ImGui::GetStyle ().ItemInnerSpacing.x;
  float const w = (full - (spacing * 2.0F)) / 3.0F;

  bool changed = false;

  // X
  ImGui::SetNextItemWidth (w);
  changed |= draw_drag_with_stripe ("##x", m_x, IM_COL32 (255, 0, 0, 255));

  ImGui::SameLine (0.0F, spacing);

  // Y
  ImGui::SetNextItemWidth (w);
  changed |= draw_drag_with_stripe ("##y", m_y, IM_COL32 (0, 200, 0, 255));

  ImGui::SameLine (0.0F, spacing);

  // Z
  ImGui::SetNextItemWidth (w);
  changed |= draw_drag_with_stripe ("##z", m_z, IM_COL32 (0, 128, 255, 255));

  ImGui::PopID ();
  return changed;
}

bool
vec4f::custom_inspect (const char *label)
{
  auto draw_drag_with_stripe
      = [] (const char *id, float &v, ImU32 stripe_col) -> bool {
    ImGui::SetNextItemWidth (ImMax (1.0F, ImGui::CalcItemWidth ()));
    bool const changed = ImGui::DragFloat (id, &v, 0.1F);
    ImDrawList *dl = ImGui::GetWindowDrawList ();
    ImVec2 const mn = ImGui::GetItemRectMin ();
    ImVec2 const mx = ImGui::GetItemRectMax ();
    const float stripe_w = 3.0F;
    dl->AddRectFilled (mn, ImVec2 (mn.x + stripe_w, mx.y), stripe_col);
    return changed;
  };

  ImGui::PushID (label);

  float const full = ImGui::CalcItemWidth ();
  float const spacing = ImGui::GetStyle ().ItemInnerSpacing.x;
  float const item_w = (full - (spacing * 3.0F)) / 4.0F;

  bool changed = false;

  ImGui::SetNextItemWidth (item_w);
  changed |= draw_drag_with_stripe ("##x", m_x, IM_COL32 (255, 0, 0, 255));
  ImGui::SameLine (0.0F, spacing);

  ImGui::SetNextItemWidth (item_w);
  changed |= draw_drag_with_stripe ("##y", m_y, IM_COL32 (0, 200, 0, 255));
  ImGui::SameLine (0.0F, spacing);

  ImGui::SetNextItemWidth (item_w);
  changed |= draw_drag_with_stripe ("##z", m_z, IM_COL32 (0, 128, 255, 255));
  ImGui::SameLine (0.0F, spacing);

  ImGui::SetNextItemWidth (item_w);
  changed |= draw_drag_with_stripe ("##w", m_w, IM_COL32 (255, 255, 255, 255));

  ImGui::PopID ();
  return changed;
}

bool
quatf::custom_inspect (const char *label)
{
  auto draw_drag_with_stripe
      = [] (const char *id, float &v, ImU32 stripe_col) -> bool {
    ImGui::SetNextItemWidth (ImMax (1.0F, ImGui::CalcItemWidth ()));
    bool const changed = ImGui::DragFloat (id, &v, 0.1F);
    ImDrawList *dl = ImGui::GetWindowDrawList ();
    ImVec2 const mn = ImGui::GetItemRectMin ();
    ImVec2 const mx = ImGui::GetItemRectMax ();
    const float stripe_w = 3.0F;
    dl->AddRectFilled (mn, ImVec2 (mn.x + stripe_w, mx.y), stripe_col);
    return changed;
  };

  ImGui::PushID (label);

  float const full = ImGui::CalcItemWidth ();
  float const spacing = ImGui::GetStyle ().ItemInnerSpacing.x;
  float const w = (full - (spacing * 2.0F)) / 3.0F;

  glm::vec3 euler
      = glm::degrees (glm::eulerAngles (static_cast<glm::quat> (*this)));

  bool changed = false;

  ImGui::SetNextItemWidth (w);
  changed |= draw_drag_with_stripe ("##x", euler.x, IM_COL32 (255, 0, 0, 255));
  ImGui::SameLine (0.0F, spacing);

  ImGui::SetNextItemWidth (w);
  changed |= draw_drag_with_stripe ("##y", euler.y, IM_COL32 (0, 200, 0, 255));
  ImGui::SameLine (0.0F, spacing);

  ImGui::SetNextItemWidth (w);
  changed
      |= draw_drag_with_stripe ("##z", euler.z, IM_COL32 (0, 128, 255, 255));

  if (changed) {
    glm::quat const q = glm::quat (glm::radians (euler));
    quatf &self = const_cast<quatf &> (*this);
    self.m_x = q.x;
    self.m_y = q.y;
    self.m_z = q.z;
    self.m_w = q.w;
  }

  ImGui::PopID ();
  return changed;
}

bool
mat33f::custom_inspect (const char *label)
{
  ImGui::PushID (label);

  float const full = ImGui::CalcItemWidth ();
  float const spacing = ImGui::GetStyle ().ItemInnerSpacing.x;
  float const width = (full - (spacing * 2.0F)) / 3.0F;

  bool changed = false;

  for (int row = 0; row < 3; ++row) {
    ImGui::PushID (row);
    for (int col = 0; col < 3; ++col) {
      ImGui::SetNextItemWidth (width);
      if (col > 0) {
        ImGui::SameLine (0.0F, spacing);
      }
      changed |= ImGui::DragFloat (
          "##v", &m_data[static_cast<ptrdiff_t> ((col * 3) + row)], 0.1F);
    }
    ImGui::PopID ();
  }

  ImGui::PopID ();
  return changed;
}

bool
mat44f::custom_inspect (const char *label)
{
  ImGui::PushID (label);

  float const full = ImGui::CalcItemWidth ();
  float const spacing = ImGui::GetStyle ().ItemInnerSpacing.x;
  float const width = (full - (spacing * 3.0F)) / 4.0F;

  bool changed = false;

  for (int row = 0; row < 4; ++row) {
    ImGui::PushID (row);
    for (int col = 0; col < 4; ++col) {
      ImGui::SetNextItemWidth (width);
      if (col > 0) {
        ImGui::SameLine (0.0F, spacing);
      }
      changed |= ImGui::DragFloat (
          "##v", &m_data[static_cast<ptrdiff_t> ((col * 4) + row)], 0.1F);
    }
    ImGui::PopID ();
  }

  ImGui::PopID ();
  return changed;
}

} // namespace math
} // namespace wsl
