#include "game_view.hpp"

#include "editor/ecs_inspector_utils.hpp"
#include "gfx/mesh.hpp"
#include "gfx/render_window.hpp"
#include "rsc/resource_ids.hpp"
#include "wsl/comp/hierarchy.hpp"
#include "wsl/comp/model_instance_3d.hpp"
#include "wsl/comp/subviewport.hpp"
#include "wsl/comp/camera_2d.hpp"
#include "wsl/comp/sprite_2d.hpp"
#include "wsl/comp/transform_2d.hpp"
#include "wsl/comp/singl/editor_context.hpp"
#include "wsl/comp/singl/rendering_manager.hpp"
#include "wsl/comp/singl/runtime_context.hpp"
#include "wsl/comp/transform.hpp"
#include "wsl/comp/world_transform.hpp"
#include "wsl/gfx/transform_gizmo.hpp"
#include "wsl/gfx/view_gizmo.hpp"
#include "wsl/ray.hpp"

#include <ImGizmo2D.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <entt/entity/entity.hpp>
#include <entt/entity/fwd.hpp>
#include <glm/common.hpp>
#include <glm/ext/matrix_float3x3.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/fwd.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/matrix.hpp>
#include <imgui.h>
#include <limits>
#include <unordered_set>
#include <utility>

namespace editor
{

static void
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

static wsl::pick_ray
make_mouse_ray (const ImVec2 &mouse_pos, const ImVec2 &img_min,
                const ImVec2 &img_size,
                const wsl::comp::singl::editor_context::resolved_camera &rc)
{
  // Mouse inside image -> NDC
  float const x = (mouse_pos.x - img_min.x) / img_size.x;
  float const y = (mouse_pos.y - img_min.y) / img_size.y;

  float const ndc_x = (x * 2.0F) - 1.0F;
  float const ndc_y = 1.0F - (y * 2.0F);

  glm::mat4 const inv_vp = glm::inverse (rc.vp ());

  // Use far plane point; works fine for ray direction.
  glm::vec4 const far_clip (ndc_x, ndc_y, 1.0F, 1.0F);
  glm::vec4 far_world4 = inv_vp * far_clip;
  far_world4 /= far_world4.w;

  wsl::pick_ray ray;
  ray.origin = rc.world_pos ();
  ray.dir = glm::normalize (glm::vec3 (far_world4) - ray.origin);
  return ray;
}

static bool
ray_aabb_intersect (const glm::vec3 &ray_origin, const glm::vec3 &ray_dir,
                    const glm::vec3 &bmin, const glm::vec3 &bmax, float &t_hit)
{
  float tmin = 0.0F;
  float tmax = std::numeric_limits<float>::max ();

  for (int i = 0; i < 3; ++i) {
    float const ro = ray_origin[i];
    float const rd = ray_dir[i];
    float const mn = bmin[i];
    float const mx = bmax[i];

    if (std::abs (rd) < 1e-8F) {
      if (ro < mn || ro > mx) {
        return false;
      }
      continue;
    }

    float const inv = 1.0F / rd;
    float t1 = (mn - ro) * inv;
    float t2 = (mx - ro) * inv;

    if (t1 > t2) {
      std::swap (t1, t2);
    }

    tmin = std::max (tmin, t1);
    tmax = std::min (tmax, t2);

    if (tmin > tmax) {
      return false;
    }
  }

  t_hit = tmin;
  return true;
}

static void
transform_aabb (const glm::vec3 &local_min, const glm::vec3 &local_max,
                const glm::mat4 &m, glm::vec3 &out_min, glm::vec3 &out_max)
{
  glm::vec3 const corners[8] = {
    { local_min.x, local_min.y, local_min.z },
    { local_max.x, local_min.y, local_min.z },
    { local_min.x, local_max.y, local_min.z },
    { local_max.x, local_max.y, local_min.z },
    { local_min.x, local_min.y, local_max.z },
    { local_max.x, local_min.y, local_max.z },
    { local_min.x, local_max.y, local_max.z },
    { local_max.x, local_max.y, local_max.z },
  };

  out_min = glm::vec3 (std::numeric_limits<float>::max ());
  out_max = glm::vec3 (-std::numeric_limits<float>::max ());

  for (const glm::vec3 &c : corners) {
    glm::vec3 const wc = glm::vec3 (m * glm::vec4 (c, 1.0F));
    out_min = glm::min (out_min, wc);
    out_max = glm::max (out_max, wc);
  }
}

static entt::entity
pick_entity_from_game_view (
    entt::registry &registry, wsl::comp::singl::runtime_context *runtime_ctx,
    const wsl::comp::singl::editor_context::resolved_camera &rc,
    const ImVec2 &img_min, const ImVec2 &img_size, const ImVec2 &mouse_pos)
{

  if (runtime_ctx == nullptr) {
    return entt::null;
  }

  auto view
      = registry
            .view<wsl::comp::world_transform, wsl::comp::model_instance_3d> ();

  wsl::pick_ray const ray = make_mouse_ray (mouse_pos, img_min, img_size, rc);

  entt::entity best = entt::null;
  float best_t = std::numeric_limits<float>::max ();

  for (entt::entity const entity : view) {
    wsl::comp::world_transform const &world
        = view.get<wsl::comp::world_transform> (entity);
    wsl::comp::model_instance_3d const &instance
        = view.get<wsl::comp::model_instance_3d> (entity);

    auto model = runtime_ctx->resource_manager ().get (instance.id);
    if (!model) {
      continue;
    }

    // You need this helper on model_3d (recommended).
    glm::vec3 local_min;
    glm::vec3 local_max;
    if (!model->get_scene_bounds (instance.scene_index, local_min, local_max)) {
      continue;
    }

    glm::vec3 world_min;
    glm::vec3 world_max;
    transform_aabb (local_min, local_max, world.value (), world_min, world_max);

    float t_hit = 0.0F;
    if (ray_aabb_intersect (ray.origin, ray.dir, world_min, world_max, t_hit)) {
      if (t_hit < best_t) {
        best_t = t_hit;
        best = entity;
      }
    }
  }

  return best;
}

static entt::entity
pick_entity_2d (entt::registry &registry,
                const wsl::comp::singl::editor_context::resolved_camera &rc,
                const ImVec2 &img_min, const ImVec2 &img_size,
                const ImVec2 &mouse_pos, int tex_width, int tex_height)
{
  if (img_size.x <= 0.0F || img_size.y <= 0.0F || tex_width <= 0
      || tex_height <= 0) {
    return entt::null;
  }

  // Map mouse from displayed-image space back to window/texture space
  float const scale_x = static_cast<float> (tex_width) / img_size.x;
  float const scale_y = static_cast<float> (tex_height) / img_size.y;
  float const px = (mouse_pos.x - img_min.x) * scale_x;
  float const py = (mouse_pos.y - img_min.y) * scale_y;

  float const ndc_x = ((px / static_cast<float> (tex_width)) * 2.0F) - 1.0F;
  float const ndc_y = 1.0F - ((py / static_cast<float> (tex_height)) * 2.0F);

  glm::mat4 const inv_vp = glm::inverse (rc.vp ());
  glm::vec4 world = inv_vp * glm::vec4 (ndc_x, ndc_y, 0.0F, 1.0F);
  world /= world.w;
  glm::vec2 const mouse_world (world.x, world.y);

  auto view = registry.view<wsl::comp::sprite_2d, wsl::comp::transform_2d> ();

  entt::entity best = entt::null;
  int best_z = std::numeric_limits<int>::min ();

  for (entt::entity const entity : view) {
    wsl::comp::sprite_2d const &sprite
        = view.get<wsl::comp::sprite_2d> (entity);
    wsl::comp::transform_2d const &t2d
        = view.get<wsl::comp::transform_2d> (entity);

    // Compute sprite AABB in world space (top-left origin)
    glm::vec2 const position = t2d.position;
    glm::vec2 const pivot = t2d.pivot;
    glm::vec2 const size = sprite.size;
    glm::vec2 const scale = t2d.scale;
    glm::vec2 const pos = position - pivot * size * scale;
    glm::vec2 const half_size = size * scale * 0.5F;
    glm::vec2 const center = pos + half_size;

    // Transform mouse into sprite-local space and apply inverse rotation
    glm::vec2 const local = mouse_world - center;
    float const rad = glm::radians (-t2d.rotation);
    float const cos_r = std::cos (rad);
    float const sin_r = std::sin (rad);
    glm::vec2 const rotated (local.x * cos_r - local.y * sin_r,
                             local.x * sin_r + local.y * cos_r);

    if (std::abs (rotated.x) <= half_size.x
        && std::abs (rotated.y) <= half_size.y) {
      if (sprite.z_index > best_z) {
        best_z = sprite.z_index;
        best = entity;
      }
    }
  }

  return best;
}

static glm::vec3
extract_translation (const glm::mat4 &m)
{
  return glm::vec3 (m[3]);
}

static void
extract_trs (const glm::mat4 &m, glm::vec3 &pos, glm::quat &rot,
             glm::vec3 &scale)
{
  pos = glm::vec3 (m[3]);

  glm::vec3 x = glm::vec3 (m[0]);
  glm::vec3 y = glm::vec3 (m[1]);
  glm::vec3 z = glm::vec3 (m[2]);

  scale.x = glm::length (x);
  scale.y = glm::length (y);
  scale.z = glm::length (z);

  if (scale.x != 0.0F) {
    x /= scale.x;
  }
  if (scale.y != 0.0F) {
    y /= scale.y;
  }
  if (scale.z != 0.0F) {
    z /= scale.z;
  }

  glm::mat3 const rot_m (x, y, z);
  rot = glm::quat_cast (rot_m);
}

void
game_view::set_render_texture (const wsl::gfx::texture &texture)
{
  m_render_texture = texture.texture_data;
  m_tex_width = (int)texture.width;
  m_tex_height = (int)texture.height;
}

void
game_view::draw (entt::registry &registry, wsl::gfx::render_window &rw)
{
  if (!visible) {
    return;
  }

  if (!ImGui::Begin ("Game View", &visible,
                     ImGuiWindowFlags_NoScrollbar
                         | ImGuiWindowFlags_NoScrollWithMouse)) {
    ImGui::End ();
    return;
  }

  if (m_toolbar_height < 0) {
    m_toolbar_height = ImGui::GetFrameHeightWithSpacing () + 4.0F;
  }

  ImGui::BeginChild ("##toolbar_area", ImVec2 (0, m_toolbar_height), 0,
                     ImGuiWindowFlags_NoScrollbar
                         | ImGuiWindowFlags_NoScrollWithMouse);
  draw_camera_header (registry, rw);
  ImGui::EndChild ();

  draw_hsplitter ("##toolbar_splitter", m_toolbar_height);

  const float spacing = ImGui::GetStyle ().ItemSpacing.x;
  const float avail_w = ImGui::GetContentRegionAvail ().x;
  const float max_height = (avail_w - 24.0F - (10.0F * spacing)) / 8.0F;

  m_toolbar_height
      = std::clamp (m_toolbar_height, 20.0F, std::max (20.0F, max_height));

  if ((m_editor_ctx != nullptr) && (m_runtime_ctx != nullptr)
      && !m_runtime_ctx->is_running ()) {
    m_editor_ctx->tick_editor_camera_anim (ImGui::GetIO ().DeltaTime);
  }

  if (m_render_texture == nullptr) {
    ImGui::TextDisabled ("No render target");
    ImGui::End ();
    return;
  }

  // --- Fit render texture to available space (keep aspect) ---
  ImVec2 const avail = ImGui::GetContentRegionAvail ();

  float const aspect
      = (m_tex_height == 0) ? 1.0F : (float)m_tex_width / (float)m_tex_height;
  ImVec2 size = avail;

  if (size.y <= 0.0F) {
    size.y = 1.0F;
  }

  if (size.x / size.y > aspect) {
    size.x = size.y * aspect;
  } else {
    size.y = size.x / aspect;
  }

  ImVec2 const cursor = ImGui::GetCursorPos ();
  ImVec2 const offset ((avail.x - size.x) * 0.5F, (avail.y - size.y) * 0.5F);

  // --- Image rect in screen space ---
  ImVec2 const win_pos = ImGui::GetWindowPos ();
  ImVec2 const img_min (win_pos.x + cursor.x + offset.x,
                        win_pos.y + cursor.y + offset.y);
  ImVec2 const img_size = size;

  if (m_editor_ctx != nullptr) {
    m_editor_ctx->last_img_min (glm::vec2 (img_min.x, img_min.y));
    m_editor_ctx->last_img_size (glm::vec2 (img_size.x, img_size.y));
  }

  ImGui::SetCursorPos (ImVec2 (cursor.x + offset.x, cursor.y + offset.y));
  ImGui::Image ((ImTextureID)m_render_texture, size, ImVec2 (0, 0),
                ImVec2 (1, 1));

  // Resolve the SAME camera used to render the Game View
  auto *scene = ((m_runtime_ctx) != nullptr)
                    ? m_runtime_ctx->scene_manager ().get_active ()
                    : nullptr;

  wsl::comp::singl::editor_context::resolved_camera rc{};
  bool have_cam = false;
  wsl::comp::singl::editor_context::game_view_mode current_mode
      = wsl::comp::singl::editor_context::game_view_mode::mode_3d_edit;

  if ((scene != nullptr) && (m_editor_ctx != nullptr)) {
    have_cam = m_editor_ctx->resolve_game_view_camera (registry, scene, rc);
    if (!m_runtime_ctx->is_running ()) {
      current_mode = m_editor_ctx->resolve_game_view_mode (registry, scene);
    }
  }

  const bool img_hovered = ImGui::IsMouseHoveringRect (
      img_min, ImVec2 (img_min.x + img_size.x, img_min.y + img_size.y), false);

  // ---------- Mode-specific editor interactions (paused only) ----------
  bool block_picking = false;
  bool block_entity_gizmo = false;

  if ((m_runtime_ctx != nullptr) && !m_runtime_ctx->is_running () && have_cam) {
    switch (current_mode) {
    case wsl::comp::singl::editor_context::game_view_mode::mode_3d_edit:
    case wsl::comp::singl::editor_context::game_view_mode::mode_3d_fly: {
      // 3D view gizmo
      bool show_view_gizmo = false;
      if (m_editor_ctx != nullptr) {
        show_view_gizmo = (m_editor_ctx->game_view_camera_selection ()
                           == wsl::comp::singl::editor_context::
                               game_view_camera_sel::default_editor)
                          || (m_editor_ctx->game_view_camera_selection ()
                              == wsl::comp::singl::editor_context::
                                  game_view_camera_sel::editor_3d);
      }

      if (show_view_gizmo) {
        const float gizmo_scale = 0.30F;
        const float gizmo_half = 128.0F * gizmo_scale;
        const float pad = 8.0F;

        const glm::vec2 view_size (img_size.x, img_size.y);
        const glm::vec2 gizmo_center (view_size.x - gizmo_half - pad,
                                      gizmo_half + pad);

        glm::vec3 cam_pos = m_editor_ctx->editor_cam_pos ();
        glm::quat cam_rot = m_editor_ctx->editor_cam_rot ();
        glm::vec3 pivot (0.0F);

        if ((m_selection != nullptr)
            && m_selection->kind == selection_kind::entity
            && m_selection->selected_entity != entt::null
            && registry.all_of<wsl::comp::world_transform> (
                m_selection->selected_entity)) {
          const wsl::comp::world_transform &wt
              = registry.get<wsl::comp::world_transform> (
                  m_selection->selected_entity);
          pivot = extract_translation (wt.value ());
        }

        m_orbit_pivot = pivot;

        const float min_dist = 0.10F;
        glm::vec3 v = cam_pos - pivot;
        float d2 = glm::dot (v, v);

        if (d2 < min_dist * min_dist) {
          if (d2 < 1e-8F) {
            glm::vec3 forward = cam_rot * glm::vec3 (0.0F, 0.0F, -1.0F);
            if (glm::dot (forward, forward) < 1e-8F) {
              forward = glm::vec3 (0.0F, 0.0F, -1.0F);
            }
            v = -glm::normalize (forward);
            d2 = 1.0F;
          }
          cam_pos = pivot + ((v / std::sqrt (d2)) * min_dist);
        }

        const ImVec2 mouse = ImGui::GetMousePos ();
        const ImVec2 mouse_delta = ImGui::GetIO ().MouseDelta;
        const ImVec2 mouse_local (mouse.x - img_min.x, mouse.y - img_min.y);
        const ImVec2 delta_global (mouse_delta.x, mouse_delta.y);

        wsl::gfx::view_gizmo_input gizmo_input;
        gizmo_input.pointer_position = wsl::gfx::view_gizmo_ndc_from_pixel (
            glm::vec2 (mouse_local.x, mouse_local.y), view_size);
        gizmo_input.pointer_delta = wsl::gfx::view_gizmo_ndc_delta_from_pixel (
            glm::vec2 (delta_global.x, delta_global.y), view_size);
        gizmo_input.delta_time = ImGui::GetIO ().DeltaTime;
        gizmo_input.primary_down = ImGui::IsMouseDown (ImGuiMouseButton_Left);
        gizmo_input.primary_released
            = ImGui::IsMouseReleased (ImGuiMouseButton_Left);
        gizmo_input.pointer_over_viewport = img_hovered;

        const wsl::gfx::view_gizmo_layout gizmo_layout
            = wsl::gfx::view_gizmo_layout::from_pixel_center (
                gizmo_center, view_size, gizmo_scale);

        wsl::gfx::view_gizmo_camera gizmo_camera;
        gizmo_camera.position = cam_pos;
        gizmo_camera.rotation = cam_rot;
        gizmo_camera.pivot = pivot;

        const bool modified = m_editor_ctx->view_gizmo_widget ().interact (
            gizmo_input, gizmo_layout, gizmo_camera);

        if (modified || m_editor_ctx->view_gizmo_widget ().is_active ()) {
          m_editor_ctx->editor_cam_pos (gizmo_camera.position);
          m_editor_ctx->editor_cam_rot (gizmo_camera.rotation);
          m_editor_ctx->cancel_editor_camera_anim ();
        }

        // Hand the widget placement to the 3D pass, which draws it with the
        // scene renderer instead of an ImGui draw list.
        m_editor_ctx->view_gizmo_layout (gizmo_layout);
        m_editor_ctx->view_gizmo_visible (true);

        block_picking = (m_editor_ctx->view_gizmo_widget ().is_active ()
                         || m_editor_ctx->view_gizmo_widget ().is_hovered ());
        block_entity_gizmo = block_picking;
      }

      // 3D entity gizmo
      if (!block_entity_gizmo) {
        glm::mat4 view = rc.view ();
        glm::mat4 proj = rc.proj ();

        if (m_show_grid) {
          glm::vec3 const forward
              = -glm::vec3 (view[0][2], view[1][2], view[2][2]);
          float const tilt = std::abs (forward.y);
          float const ground_t
              = (tilt > 0.01F) ? (std::abs (rc.world_pos ().y) / tilt) : 500.0F;
          float const base_fog_radius
              = std::min (250.0F, (ground_t * std::sqrt (ground_t)) + 10.0F);
          glm::vec3 const flat_fwd
              = (glm::length (glm::vec2 (forward.x, forward.z)) > 0.001F)
                    ? glm::normalize (glm::vec3 (forward.x, 0.0F, forward.z))
                    : glm::vec3 (0, 0, 0);
          float const shift_dist = std::min (ground_t, base_fog_radius * 0.5F);
          glm::vec3 const fog_center
              = glm::vec3 (rc.world_pos ().x, 0, rc.world_pos ().z)
                + flat_fwd * shift_dist;

          m_editor_ctx->grid_visible (true);
          m_editor_ctx->grid_camera_pos (rc.world_pos ());
          m_editor_ctx->grid_fog_center (fog_center);
          m_editor_ctx->grid_fog_radius (base_fog_radius);
        } else {
          m_editor_ctx->grid_visible (false);
        }

        wsl::gfx::transform_gizmo &gizmo_widget
            = m_editor_ctx->transform_gizmo_widget ();
        m_editor_ctx->gizmo_mode (m_current_op);

        entt::entity selected = entt::null;
        if ((m_selection != nullptr)
            && m_selection->kind == selection_kind::entity) {
          selected = m_selection->selected_entity;
        }

        const bool have_target
            = (selected != entt::null)
              && registry
                     .all_of<wsl::comp::transform, wsl::comp::world_transform> (
                         selected);

        if (have_target
            && (m_current_op != wsl::gfx::transform_gizmo_mode::none)) {
          wsl::comp::world_transform const &wt
              = registry.get<wsl::comp::world_transform> (selected);
          glm::mat4 world = wt.value ();

          const ImVec2 mouse = ImGui::GetMousePos ();

          wsl::gfx::transform_gizmo_input gizmo_input;
          gizmo_input.view = view;
          gizmo_input.proj = proj;
          gizmo_input.camera_position = rc.world_pos ();
          gizmo_input.viewport_min = glm::vec2 (img_min.x, img_min.y);
          gizmo_input.viewport_size = glm::vec2 (img_size.x, img_size.y);
          gizmo_input.pointer_position = glm::vec2 (mouse.x, mouse.y);
          gizmo_input.pointer_over_viewport = img_hovered;
          gizmo_input.primary_down = ImGui::IsMouseDown (ImGuiMouseButton_Left);

          gizmo_widget.manipulate (gizmo_input, m_current_op, world);

          if (gizmo_widget.is_using ()) {
            wsl::comp::transform &tr
                = registry.get<wsl::comp::transform> (selected);
            glm::mat4 local = world;

            if (auto *h = registry.try_get<wsl::comp::hierarchy> (selected)) {
              if (h->parent != entt::null
                  && registry.all_of<wsl::comp::world_transform> (h->parent)) {
                const wsl::comp::world_transform &pwt
                    = registry.get<wsl::comp::world_transform> (h->parent);
                local = glm::inverse (static_cast<glm::mat4> (pwt.value ()))
                        * world;
              }
            }

            glm::vec3 pos;
            glm::vec3 scl;
            glm::quat rot;
            extract_trs (local, pos, rot, scl);

            tr.position = wsl::math::vec3f{ pos };
            tr.rotation = wsl::math::quatf{ rot };
            tr.scale = wsl::math::vec3f{ scl };
          }

          m_editor_ctx->transform_gizmo_visible (true);
        } else {
          gizmo_widget.reset ();
        }

        if (!block_picking) {
          block_picking = gizmo_widget.is_using () || gizmo_widget.is_over ();
        }
      }

      // 3D picking
      if (!block_picking && (m_selection != nullptr) && img_hovered
          && ImGui::IsMouseClicked (ImGuiMouseButton_Left)) {
        entt::entity const picked
            = pick_entity_from_game_view (registry, m_runtime_ctx, rc, img_min,
                                          img_size, ImGui::GetMousePos ());

        if (picked != entt::null) {
          m_selection->select_entity (picked);
          m_editor_ctx->selected_entity (picked);
        } else {
          m_selection->clear_entity_singleton ();
          m_editor_ctx->selected_entity (entt::null);
        }
      }
      break;
    }

    case wsl::comp::singl::editor_context::game_view_mode::mode_2d_edit: {
      // Hide 3D grid
      m_editor_ctx->grid_visible (false);

      // 2D camera controls (pan / zoom)
      if (img_hovered) {
        // Mouse position in the image's local pixel coordinates
        // (origin at the image's top-left).
        ImVec2 const mouse_global = ImGui::GetMousePos ();
        float const mouse_x = mouse_global.x - img_min.x;
        float const mouse_y = mouse_global.y - img_min.y;

        float const wheel = ImGui::GetIO ().MouseWheel;
        if (wheel != 0.0F) {
          wsl::comp::camera_2d cam2d = m_editor_ctx->editor_camera_2d ();
          float zoom = cam2d.zoom;
          float const old_zoom = zoom;
          zoom = std::clamp (zoom + (wheel * 0.1F), 0.01F, 100.0F);
          cam2d.zoom = zoom;
          m_editor_ctx->editor_camera_2d (cam2d);

          if (zoom != old_zoom) {
            float const dpos = (1.0F / old_zoom) - (1.0F / zoom);
            glm::vec2 pos = m_editor_ctx->editor_cam_2d_pos ();
            pos.x += mouse_x * dpos;
            pos.y += mouse_y * dpos;
            m_editor_ctx->editor_cam_2d_pos (pos);
          }
        }

        if (ImGui::IsMouseDown (ImGuiMouseButton_Middle)) {
          ImVec2 const delta = ImGui::GetIO ().MouseDelta;
          if (delta.x != 0.0F || delta.y != 0.0F) {
            float const inv_zoom
                = 1.0F / m_editor_ctx->editor_camera_2d ().zoom;
            glm::vec2 pos = m_editor_ctx->editor_cam_2d_pos ();
            pos.x -= delta.x * inv_zoom;
            pos.y -= delta.y * inv_zoom;
            m_editor_ctx->editor_cam_2d_pos (pos);
          }
        }
      }

      // Clip gizmo and boundary to the displayed image area
      ImDrawList *dl = ImGui::GetWindowDrawList ();
      dl->PushClipRect (img_min,
                        ImVec2 (img_min.x + img_size.x, img_min.y + img_size.y),
                        true);

      // ImGizmo2D for selected entity
      if ((m_selection != nullptr)
          && m_selection->kind == selection_kind::entity
          && m_selection->selected_entity != entt::null
          && registry.all_of<wsl::comp::transform_2d> (
              m_selection->selected_entity)) {
        // The camera projection is built from the window/framebuffer size,
        // but ImGizmo2D works in the fitted display rect (img_size).
        // Compute the effective zoom so that world-to-screen mapping matches
        // the rendered image exactly.
        float const half_w = 1.0F / rc.proj ()[0][0];
        float const half_h = 1.0F / rc.proj ()[1][1];
        float const world_w = 2.0F * half_w;
        float const world_h = 2.0F * half_h;
        float const effective_zoom = img_size.x / world_w;

        // ImGizmo2D uses a centred convention: it treats the camera
        // position as the *centre* of the visible area. Our editor
        // 2D camera uses a top-left convention (matching the 3D
        // view), so we pass the centre of the visible area (in
        // world units) and include the camera zoom in the ImGizmo2D
        // scale factor.
        float const cam_zoom = m_editor_ctx->editor_camera_2d ().zoom;
        float const center_x = m_editor_ctx->editor_cam_2d_pos ().x
                               + (world_w / (2.0F * cam_zoom));
        float const center_y = m_editor_ctx->editor_cam_2d_pos ().y
                               + (world_h / (2.0F * cam_zoom));
        float const imguizmo_zoom = effective_zoom * cam_zoom;

        ImGizmo2D::SetDrawList (dl);
        ImGizmo2D::SetViewRect (img_min, img_size);
        ImGizmo2D::SetViewTransform (center_x, center_y, imguizmo_zoom);
        ImGizmo2D::BeginFrame ();

        wsl::comp::transform_2d &t2d = registry.get<wsl::comp::transform_2d> (
            m_selection->selected_entity);

        bool modified = false;
        switch (m_current_op) {
        case wsl::gfx::transform_gizmo_mode::translate: {
          float pos_x = t2d.position.x ();
          float pos_y = t2d.position.y ();
          modified = ImGizmo2D::Translate ("selected", &pos_x, &pos_y);
          if (modified) {
            t2d.position.x () = pos_x;
            t2d.position.y () = pos_y;
          }
          break;
        }
        case wsl::gfx::transform_gizmo_mode::rotate: {
          float angle = t2d.rotation;
          modified = ImGizmo2D::Rotate ("selected", &t2d.position.x (),
                                        &t2d.position.y (), &angle);
          if (modified) {
            t2d.rotation = angle;
          }
          break;
        }
        case wsl::gfx::transform_gizmo_mode::scale: {
          float sx = t2d.scale.x ();
          float sy = t2d.scale.y ();
          modified = ImGizmo2D::Scale ("selected", &t2d.position.x (),
                                       &t2d.position.y (), &sx, &sy);
          if (modified) {
            t2d.scale.x () = sx;
            t2d.scale.y () = sy;
          }
          break;
        }
        default:
          break;
        }

        block_picking = ImGizmo2D::IsActive () || ImGizmo2D::IsHovered ();
      }

      // Draw root viewport virtual size boundary
      if (registry.ctx ().contains<wsl::comp::singl::rendering_manager> ()) {
        auto &rendering
            = registry.ctx ().get<wsl::comp::singl::rendering_manager> ();
        float const vw = rendering.root_viewport_virtual_size.x ();
        float const vh = rendering.root_viewport_virtual_size.y ();

        // Top-left origin convention: the virtual viewport spans
        // (0, 0) → (vw, vh) in world space, matching the 3D view's
        // top-left origin.
        glm::vec3 const corners[4] = {
          glm::vec3 (0.0F, 0.0F, 0.0F),
          glm::vec3 (vw, 0.0F, 0.0F),
          glm::vec3 (vw, vh, 0.0F),
          glm::vec3 (0.0F, vh, 0.0F),
        };

        ImVec2 screen[4];
        for (int i = 0; i < 4; ++i) {
          glm::vec4 ndc = rc.vp () * glm::vec4 (corners[i], 1.0F);
          if (ndc.w != 0.0F) {
            ndc /= ndc.w;
          }
          screen[i].x = img_min.x + (((ndc.x * 0.5F) + 0.5F) * img_size.x);
          screen[i].y = img_min.y + ((1.0F - ndc.y) * 0.5F * img_size.y);
        }

        ImU32 const col = IM_COL32 (255, 255, 255, 128);
        for (int i = 0; i < 4; ++i) {
          dl->AddLine (screen[i], screen[(i + 1) % 4], col, 2.0F);
        }
      }

      dl->PopClipRect ();

      // 2D picking (AABB based on sprite bounds)
      if (!block_picking && (m_selection != nullptr) && img_hovered
          && ImGui::IsMouseClicked (ImGuiMouseButton_Left)) {
        entt::entity const picked
            = pick_entity_2d (registry, rc, img_min, img_size,
                              ImGui::GetMousePos (), m_tex_width, m_tex_height);

        if (picked != entt::null) {
          m_selection->select_entity (picked);
          m_editor_ctx->selected_entity (picked);
        } else {
          m_selection->clear_entity_singleton ();
          m_editor_ctx->selected_entity (entt::null);
        }
      }
      break;
    }

    case wsl::comp::singl::editor_context::game_view_mode::mode_2d_view:
    case wsl::comp::singl::editor_context::game_view_mode::mode_3d_view:
      // View mode: no editor gizmos, no picking
      m_editor_ctx->grid_visible (false);
      break;
    }
  }

  // Tool shortcuts (shared between 3D and 2D edit modes)
  if (ImGui::IsWindowFocused (ImGuiFocusedFlags_RootAndChildWindows)) {
    if (ImGui::IsKeyPressed (ImGuiKey_W)) {
      m_current_op = wsl::gfx::transform_gizmo_mode::translate;
    }
    if (ImGui::IsKeyPressed (ImGuiKey_E)) {
      m_current_op = wsl::gfx::transform_gizmo_mode::rotate;
    }
    if (ImGui::IsKeyPressed (ImGuiKey_R)) {
      m_current_op = wsl::gfx::transform_gizmo_mode::scale;
    }
  }

  ImGui::End ();
}

static void
draw_icon_button (wsl::comp::singl::editor_context *editor_ctx,
                  wsl::rsc::image_id icon_id, const char *tooltip,
                  bool *clicked, bool enabled = true, bool active = false,
                  float size = -1.0F)
{
  if (size < 0) {
    size = ImGui::GetFrameHeight ();
  }

  if (!enabled) {
    ImGui::BeginDisabled ();
  }

  if (active) {
    ImGui::PushStyleColor (ImGuiCol_Button,
                           ImGui::GetStyleColorVec4 (ImGuiCol_ButtonHovered));
  }

  const float padding = 4.0F;
  const ImVec2 image_size (size - (padding * 2.0F), size - (padding * 2.0F));

  auto handle = editor_ctx->editor_resources ().get (icon_id);
  if (!handle || ((*handle).texture.get () == nullptr)) {
    editor_ctx->editor_resources ().load (icon_id);
    if (ImGui::Button ("??", ImVec2 (size, size))) {
      if (clicked != nullptr) {
        *clicked = true;
      }
    }
  } else {
    ImGui::PushStyleVar (ImGuiStyleVar_FramePadding, ImVec2 (padding, padding));
    if (ImGui::ImageButton (tooltip, (ImTextureID)(*handle).texture.get (),
                            image_size)) {
      if (clicked != nullptr) {
        *clicked = true;
      }
    }
    ImGui::PopStyleVar ();
  }

  if (active) {
    ImGui::PopStyleColor ();
  }

  if (ImGui::IsItemHovered (ImGuiHoveredFlags_AllowWhenDisabled)) {
    ImGui::SetTooltip ("%s", tooltip);
  }

  if (!enabled) {
    ImGui::EndDisabled ();
  }
}

void
game_view::draw_camera_header (entt::registry &registry,
                               wsl::gfx::render_window & /*unused*/)
{
  wsl::comp::singl::runtime_context &runtime_ctx = *this->m_runtime_ctx;

  const float btn_size = m_toolbar_height;
  const float spacing = ImGui::GetStyle ().ItemSpacing.x;
  const float combo_vp_w = 100.0F;
  const float combo_cam_w = 120.0F;

  // Total width = 8 buttons + 3 dummies + 2 combos + spacing between items
  float const group_w = (8.0F * btn_size) + (3.0F * 8.0F) + combo_vp_w
                        + combo_cam_w + (12.0F * spacing);

  float const avail = ImGui::GetContentRegionAvail ().x;
  float const offset = (avail - group_w) * 0.5F;
  if (offset > 0.0F) {
    ImGui::SetCursorPosX (ImGui::GetCursorPosX () + offset);
  }

  ImGui::BeginGroup ();

  if (runtime_ctx.in_play_session ()) {
    if (runtime_ctx.is_running ()) {
      bool do_pause = false;
      draw_icon_button (m_editor_ctx, m_editor_ctx->icon_pause (), "Pause",
                        &do_pause, true, false, btn_size);
      if (do_pause) {
        runtime_ctx.set_running (false);
      }
    } else {
      bool do_play = false;
      draw_icon_button (m_editor_ctx, m_editor_ctx->icon_play (), "Play",
                        &do_play, true, false, btn_size);
      if (do_play) {
        if (m_editor_ctx->get_das_compile_mode ()
            != wsl::comp::singl::editor_context::das_compile_mode::
                interpretation) {
          wsl::log::core ()->error (
              "Daslang JIT and AOT modes are not yet implemented. "
              "Falling back to interpretation mode.");
        }
        runtime_ctx.set_running (true);
        ImGui::SetWindowFocus ();
      }
    }
    ImGui::SameLine ();
    bool do_stop = false;
    draw_icon_button (m_editor_ctx, m_editor_ctx->icon_stop (), "Stop",
                      &do_stop, true, false, btn_size);
    if (do_stop) {
      // Deferred, not stop(): this runs from inside render_impl's draw-data
      // pass. Tearing the session down here would release GPU pipelines that
      // the driver may still be executing, and the next frame's fence wait then
      // blocks forever inside vkDestroyGraphicsPipeline.
      runtime_ctx.request_stop ();
    }
  } else {
    bool do_play = false;
    draw_icon_button (m_editor_ctx, m_editor_ctx->icon_play (), "Play",
                      &do_play, true, false, btn_size);
    if (do_play) {
      if (m_editor_ctx->get_das_compile_mode ()
          != wsl::comp::singl::editor_context::das_compile_mode::
              interpretation) {
        wsl::log::core ()->error (
            "Daslang JIT and AOT modes are not yet implemented. "
            "Falling back to interpretation mode.");
      }
      runtime_ctx.set_running (true);
      ImGui::SetWindowFocus ();
    }
    ImGui::SameLine ();
    draw_icon_button (m_editor_ctx, m_editor_ctx->icon_stop (), "Stop", nullptr,
                      false, false, btn_size);
  }

  // ===================== gizmo mode buttons =====================
  ImGui::SameLine ();
  ImGui::Dummy (ImVec2 (8.0F, 0.0F));
  ImGui::SameLine ();

  auto toggle_op = [this] (wsl::gfx::transform_gizmo_mode op) {
    m_current_op
        = (m_current_op == op) ? wsl::gfx::transform_gizmo_mode::none : op;
  };

  if (runtime_ctx.is_running ()) {
    ImGui::BeginDisabled ();
  }

  const bool tr_on
      = (m_current_op == wsl::gfx::transform_gizmo_mode::translate);
  const bool rot_on = (m_current_op == wsl::gfx::transform_gizmo_mode::rotate);
  const bool scl_on = (m_current_op == wsl::gfx::transform_gizmo_mode::scale);

  bool clicked_tr = false;
  draw_icon_button (m_editor_ctx, m_editor_ctx->icon_translate (),
                    "Translate (W)", &clicked_tr, !runtime_ctx.is_running (),
                    tr_on, btn_size);
  if (clicked_tr) {
    toggle_op (wsl::gfx::transform_gizmo_mode::translate);
  }

  ImGui::SameLine ();

  bool clicked_rot = false;
  draw_icon_button (m_editor_ctx, m_editor_ctx->icon_rotate (), "Rotate (E)",
                    &clicked_rot, !runtime_ctx.is_running (), rot_on, btn_size);
  if (clicked_rot) {
    toggle_op (wsl::gfx::transform_gizmo_mode::rotate);
  }

  ImGui::SameLine ();

  bool clicked_scl = false;
  draw_icon_button (m_editor_ctx, m_editor_ctx->icon_scale (), "Scale (R)",
                    &clicked_scl, !runtime_ctx.is_running (), scl_on, btn_size);
  if (clicked_scl) {
    toggle_op (wsl::gfx::transform_gizmo_mode::scale);
  }

  ImGui::SameLine ();
  ImGui::Dummy (ImVec2 (8.0F, 0.0F));
  ImGui::SameLine ();

  // ===================== viewport combo =====================
  auto *scene = m_runtime_ctx->scene_manager ().get_active ();
  if (runtime_ctx.is_running ()) {
    ImGui::BeginDisabled ();
  }

  wsl::comp::singl::editor_context &ed = *this->m_editor_ctx;

  // ===================== viewport combo =====================
  const char *vp_preview = "Root Viewport";
  char vp_buf[256];
  if (ed.game_view_selected_viewport () != entt::null && scene != nullptr) {
    auto const &reg = scene->get_registry ();
    if (reg.valid (ed.game_view_selected_viewport ())) {
      std::snprintf (
          vp_buf, sizeof (vp_buf), "%s",
          scene->get_entity_name (ed.game_view_selected_viewport ()).c_str ());
      vp_preview = vp_buf;
    }
  }

  ImGui::SetNextItemWidth (combo_vp_w);
  if (ImGui::BeginCombo ("##viewport_combo", vp_preview)) {
    if (ImGui::Selectable ("Root Viewport",
                           ed.game_view_selected_viewport () == entt::null)) {
      ed.game_view_selected_viewport (entt::null);
      ed.game_view_camera_selection (wsl::comp::singl::editor_context::
                                         game_view_camera_sel::default_editor);
      ed.game_view_selected_camera_entity (entt::null);
    }
    if (ed.game_view_selected_viewport () == entt::null) {
      ImGui::SetItemDefaultFocus ();
    }

    if (scene != nullptr) {
      auto const &reg = scene->get_registry ();
      auto sv_view = reg.view<wsl::comp::subviewport> ();
      for (entt::entity const e : sv_view) {
        std::string const &name = scene->get_entity_name (e);
        bool const selected = (e == ed.game_view_selected_viewport ());
        if (ImGui::Selectable (name.c_str (), selected)) {
          ed.game_view_selected_viewport (e);
          ed.game_view_camera_selection (
              wsl::comp::singl::editor_context::game_view_camera_sel::
                  default_editor);
          ed.game_view_selected_camera_entity (entt::null);
        }
        if (selected) {
          ImGui::SetItemDefaultFocus ();
        }
      }
    }
    ImGui::EndCombo ();
  }

  ImGui::SameLine ();

  // ===================== camera combo =====================
  const char *cam_preview = "Default Editor Camera";
  char cam_buf[256];

  auto get_camera_preview_name = [&] () -> const char * {
    switch (ed.game_view_camera_selection ()) {
    case wsl::comp::singl::editor_context::game_view_camera_sel::default_editor:
      return "Default Editor Camera";
    case wsl::comp::singl::editor_context::game_view_camera_sel::editor_3d:
      return "Editor 3D Camera";
    case wsl::comp::singl::editor_context::game_view_camera_sel::editor_2d:
      return "Editor 2D Camera";
    case wsl::comp::singl::editor_context::game_view_camera_sel::
        default_runtime:
      return "Default Runtime Camera";
    case wsl::comp::singl::editor_context::game_view_camera_sel::entity:
      if (ed.game_view_selected_camera_entity () != entt::null
          && scene != nullptr) {
        auto const &reg = scene->get_registry ();
        if (reg.valid (ed.game_view_selected_camera_entity ())) {
          std::snprintf (
              cam_buf, sizeof (cam_buf), "%s",
              scene->get_entity_name (ed.game_view_selected_camera_entity ())
                  .c_str ());
          return cam_buf;
        }
      }
      return "Camera";
    }
    return "Default Editor Camera";
  };

  cam_preview = get_camera_preview_name ();

  ImGui::SetNextItemWidth (combo_cam_w);
  if (ImGui::BeginCombo ("##camera_combo", cam_preview)) {
    // Fixed options
    struct option
    {
      wsl::comp::singl::editor_context::game_view_camera_sel sel;
      const char *label;
    };
    const option fixed_opts[] = {
      { wsl::comp::singl::editor_context::game_view_camera_sel::default_editor,
        "Default Editor Camera" },
      { wsl::comp::singl::editor_context::game_view_camera_sel::editor_3d,
        "Editor 3D Camera" },
      { wsl::comp::singl::editor_context::game_view_camera_sel::editor_2d,
        "Editor 2D Camera" },
      { wsl::comp::singl::editor_context::game_view_camera_sel::default_runtime,
        "Default Runtime Camera" },
    };

    for (auto const &opt : fixed_opts) {
      bool const selected = (ed.game_view_camera_selection () == opt.sel
                             && opt.sel
                                    != wsl::comp::singl::editor_context::
                                        game_view_camera_sel::entity);
      if (ImGui::Selectable (opt.label, selected)) {
        ed.game_view_camera_selection (opt.sel);
        ed.game_view_selected_camera_entity (entt::null);
      }
      if (selected) {
        ImGui::SetItemDefaultFocus ();
      }
    }

    // Entity cameras that belong to the current viewport
    if (scene != nullptr) {
      auto &reg = scene->get_registry ();
      entt::entity const target_vp = ed.game_view_selected_viewport ();

      auto add_camera_opts = [&] (auto cam_view) {
        for (entt::entity const e : cam_view) {
          if (!wsl::comp::entity_in_viewport_scope (reg, e, target_vp)) {
            continue;
          }
          const std::string &name = scene->get_entity_name (e);
          bool const selected
              = (ed.game_view_camera_selection ()
                     == wsl::comp::singl::editor_context::game_view_camera_sel::
                         entity
                 && e == ed.game_view_selected_camera_entity ());
          if (ImGui::Selectable (name.c_str (), selected)) {
            ed.game_view_camera_selection (
                wsl::comp::singl::editor_context::game_view_camera_sel::entity);
            ed.game_view_selected_camera_entity (e);
          }
          if (selected) {
            ImGui::SetItemDefaultFocus ();
          }
        }
      };

      add_camera_opts (reg.view<wsl::comp::camera> ());
      add_camera_opts (reg.view<wsl::comp::camera_2d> ());
    }

    ImGui::EndCombo ();
  }

  if (runtime_ctx.is_running ()) {
    ImGui::EndDisabled ();
  }

  ImGui::SameLine ();

  // ===================== reset camera button =====================
  bool clicked_reset = false;
  draw_icon_button (m_editor_ctx, m_editor_ctx->icon_reset_cam (),
                    "Reset Camera", &clicked_reset, !runtime_ctx.is_running (),
                    false, btn_size);

  if (clicked_reset && (m_editor_ctx != nullptr)) {
    m_orbit_pivot = glm::vec3 (0.0F, 0.0F, 0.0F);
    m_editor_ctx->reset_editor_camera ();

    ed.game_view_camera_selection (
        wsl::comp::singl::editor_context::game_view_camera_sel::default_editor);
    ed.game_view_selected_camera_entity (entt::null);
    ed.game_view_selected_viewport (entt::null);
  }

  ImGui::SameLine ();

  bool const can_focus
      = (!runtime_ctx.is_running () && (m_editor_ctx != nullptr)
         && (m_selection != nullptr)
         && m_selection->kind == selection_kind::entity
         && m_selection->selected_entity != entt::null
         && registry.all_of<wsl::comp::world_transform> (
             m_selection->selected_entity));

  bool clicked_focus = false;
  draw_icon_button (m_editor_ctx, m_editor_ctx->icon_focus_cam (),
                    "Focus on Selection (F)", &clicked_focus, can_focus, false,
                    btn_size);

  if (clicked_focus && can_focus) {
    wsl::comp::singl::editor_context &ed = *this->m_editor_ctx;
    ed.game_view_camera_selection (
        wsl::comp::singl::editor_context::game_view_camera_sel::default_editor);
    ed.game_view_selected_camera_entity (entt::null);
    ed.game_view_selected_viewport (entt::null);

    const wsl::comp::world_transform &wt
        = registry.get<wsl::comp::world_transform> (
            m_selection->selected_entity);
    glm::vec3 const target = extract_translation (wt.value ());

    m_orbit_pivot = target;
    m_editor_ctx->focus_editor_camera_to_point (target);
  }

  ImGui::SameLine ();
  ImGui::Dummy (ImVec2 (8.0F, 0.0F));
  ImGui::SameLine ();

  bool clicked_grid = false;
  draw_icon_button (m_editor_ctx, m_editor_ctx->icon_grid (), "Toggle Grid (G)",
                    &clicked_grid, true, m_show_grid, btn_size);

  if (clicked_grid) {
    m_show_grid = !m_show_grid;
  }

  if (runtime_ctx.is_running ()) {
    ImGui::EndDisabled ();
  }

  ImGui::EndGroup ();
}

void
game_view::set_selection (ecs_selection *sel)
{
  m_selection = sel;
}

game_view::game_view (wsl::comp::singl::runtime_context *runtime_ctx,
                      wsl::comp::singl::editor_context *editor_ctx)
    : m_runtime_ctx (runtime_ctx), m_editor_ctx (editor_ctx)
{
}

} // namespace editor
