#include "viewport_renderer.hpp"

#include "../comp/hierarchy.hpp"
#include "../comp/singl/editor_context.hpp"
#include "../comp/singl/rendering_manager.hpp"
#include "../comp/singl/runtime_context.hpp"
#include "../comp/transform_2d.hpp"
#include "../comp/viewport_environment.hpp"
#include "../comp/world_transform.hpp"
#include "../gfx/batch_renderer_2d.hpp"
#include "../gfx/model_3d.hpp"
#include "../gfx/render_window.hpp"
#include "../gfx/subviewport_target.hpp"
#include "../rsc/resource_manager.hpp"
#include "../rsc/scene.hpp"
#include "comp/camera.hpp"
#include "comp/subviewport.hpp"
#include "lighting_system.hpp"
#include "physics_system.hpp"
#include "render_2d_system.hpp"
#include "render_3d_system.hpp"
#include "shadow_system.hpp"
#include "skybox_system.hpp"
#include "viewport_routing.hpp"

#include <tracy/Tracy.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace wsl
{

namespace sys
{

namespace
{

struct viewport_node
{
  entt::entity entity = entt::null;
  entt::entity parent = entt::null;
  std::size_t parent_index = static_cast<std::size_t> (-1);
  comp::subviewport_mode mode = comp::subviewport_mode::mode_3d;
  bool chain_root = false;
  bool is_2d = false;
  bool hdr = false;
  bool bloom = false;
  int msaa = 1;
  comp::viewport_filter filter = comp::viewport_filter::linear;
  int display_order = 0;

  /** 2D placement in the parent viewport's virtual space (D2). */
  math::vec2f position{ 0.0F, 0.0F };
  math::vec2f size{ 0.0F, 0.0F };

  gfx::resolved_viewport resolved{};
  comp::resolved_viewport_environment env{};
  render_submission submission{};
  comp::viewport_settings *settings = nullptr;

  bool renders = false;
  std::vector<std::size_t> children;
};

entt::entity
parent_of (entt::registry &registry, entt::entity entity)
{
  if (entity == entt::null) {
    return entt::null;
  }
  auto *h = registry.try_get<comp::hierarchy> (entity);
  return (h != nullptr) ? h->parent : entt::null;
}

bool
rects_intersect (float ax, float ay, float aw, float ah, float bx, float by,
                 float bw, float bh)
{
  return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

/** Frustum planes from a view-projection matrix (row3 +/- rowN). */
void
extract_frustum_planes (const glm::mat4 &vp, glm::vec4 planes[6])
{
  glm::vec4 const row0{ vp[0][0], vp[1][0], vp[2][0], vp[3][0] };
  glm::vec4 const row1{ vp[0][1], vp[1][1], vp[2][1], vp[3][1] };
  glm::vec4 const row2{ vp[0][2], vp[1][2], vp[2][2], vp[3][2] };
  glm::vec4 const row3{ vp[0][3], vp[1][3], vp[2][3], vp[3][3] };

  planes[0] = row3 + row0; // left
  planes[1] = row3 - row0; // right
  planes[2] = row3 + row1; // bottom
  planes[3] = row3 - row1; // top
  planes[4] = row3 + row2; // near
  planes[5] = row3 - row2; // far
}

bool
aabb_in_frustum (const glm::mat4 &view_proj, const glm::vec3 &min,
                 const glm::vec3 &max)
{
  glm::vec4 planes[6];
  extract_frustum_planes (view_proj, planes);
  for (int i = 0; i < 6; ++i) {
    glm::vec3 const positive{ planes[i].x >= 0.0F ? max.x : min.x,
                              planes[i].y >= 0.0F ? max.y : min.y,
                              planes[i].z >= 0.0F ? max.z : min.z };
    if (glm::dot (glm::vec3 (planes[i]), positive) + planes[i].w < 0.0F) {
      return false;
    }
  }
  return true;
}

/** World-space AABB of a subviewport's quad. */
void
quad_world_aabb (entt::registry &registry, entt::entity entity,
                 math::vec2f quad_size, glm::vec3 &out_min, glm::vec3 &out_max)
{
  auto *wt = registry.try_get<comp::world_transform> (entity);
  if (wt == nullptr) {
    out_min = glm::vec3 (0.0F);
    out_max = glm::vec3 (0.0F);
    return;
  }
  glm::mat4 const m = wt->value ();
  float const hw = quad_size.x () * 0.5F;
  float const hh = quad_size.y () * 0.5F;

  glm::vec3 mn (std::numeric_limits<float>::max ());
  glm::vec3 mx (std::numeric_limits<float>::lowest ());
  for (int i = 0; i < 4; ++i) {
    glm::vec3 const corner{ (i & 1) ? hw : -hw, (i & 2) ? hh : -hh, 0.0F };
    glm::vec3 const world = glm::vec3 (m * glm::vec4 (corner, 1.0F));
    mn = glm::min (mn, world);
    mx = glm::max (mx, world);
  }
  out_min = mn;
  out_max = mx;
}

/**
 * Screen-projected size of a 3D subviewport quad in the parent's render
 * target pixels. Uses the projection scale at the quad's view-space depth:
 * for both perspective and orthographic projections
 * `proj[1][1] * 0.5 * render_h / depth` yields pixels per world unit.
 */
math::vec2f
projected_quad_size (const gfx::scene_renderer::view_state &parent_view,
                     entt::registry &registry, entt::entity entity,
                     math::vec2f quad_size)
{
  auto *wt = registry.try_get<comp::world_transform> (entity);
  if (wt == nullptr) {
    return quad_size;
  }

  glm::mat4 const w = wt->value ();
  glm::vec4 const center_world = w * glm::vec4 (0.0F, 0.0F, 0.0F, 1.0F);
  glm::vec3 const view_pos = glm::vec3 (parent_view.view * center_world);
  float const depth = std::max (1e-3F, std::abs (view_pos.z));
  float const p11 = std::abs (parent_view.proj[1][1]);
  if (p11 <= 0.0F) {
    return quad_size;
  }

  // Scale inherited from the world transform (world_quad_size is applied
  // separately by the quad model matrix).
  float const scale_x = glm::length (glm::vec3 (w[0]));
  float const scale_y = glm::length (glm::vec3 (w[1]));

  float const render_h = 720.0F; // magnitude only; aspect drives layout
  float const px_per_unit = 0.5F * render_h * p11 / depth;

  return math::vec2f{ std::max (1.0F, quad_size.x () * scale_x * px_per_unit),
                      std::max (1.0F, quad_size.y () * scale_y * px_per_unit) };
}

/**
 * 3D quad targets keep the quad's world aspect ratio so the texture is not
 * stretched when mapped onto the quad (preserves the pre-refactor
 * behaviour). The resolved virtual size keeps its magnitude from the
 * viewport's virtual size.
 */
void
force_quad_aspect (gfx::resolved_viewport &resolved, math::vec2f quad_size)
{
  if (quad_size.x () <= 0.0F || quad_size.y () <= 0.0F) {
    return;
  }
  float const quad_aspect = quad_size.x () / quad_size.y ();
  float const magnitude
      = std::max (resolved.virtual_size.x (), resolved.virtual_size.y ());
  if (magnitude <= 0.0F) {
    return;
  }

  float virt_w = magnitude;
  float virt_h = magnitude / quad_aspect;
  if (quad_aspect < 1.0F) {
    virt_h = magnitude;
    virt_w = magnitude * quad_aspect;
  }

  resolved.virtual_size = math::vec2f{ virt_w, virt_h };
  int const render_w = std::max (
      1, static_cast<int> (std::lround (virt_w * resolved.render_scale)));
  int const render_h = std::max (
      1, static_cast<int> (std::lround (virt_h * resolved.render_scale)));
  resolved.render_size = math::vec2f{ static_cast<float> (render_w),
                                      static_cast<float> (render_h) };
  resolved.render_rect = gfx::pixel_rect{ 0, 0, render_w, render_h };
  resolved.output_rect = gfx::pixel_rect{ 0, 0, render_w, render_h };
}

/** Applies the resolved environment to the renderer/window state. */
void
apply_viewport_environment (const viewport_render_params &params,
                            const comp::resolved_viewport_environment &env)
{
  auto &renderer = *params.renderer;
  renderer.ssao_enabled = env.ssao_enabled;
  renderer.ssao_radius = env.ssao_radius;
  renderer.ssao_bias = env.ssao_bias;
  renderer.ssao_power = env.ssao_power;
  renderer.ssao_intensity = env.ssao_intensity;
  renderer.bloom_threshold = env.bloom_threshold;
  renderer.bloom_knee = env.bloom_knee;
  renderer.bloom_intensity = env.bloom_intensity;
  renderer.set_ibl_intensity (env.ibl_intensity);

  params.window->exposure (env.exposure);
  params.window->bloom_intensity (env.bloom_intensity);
}

SDL_FColor
to_sdl_color (math::vec4f color)
{
  return SDL_FColor{ color.x (), color.y (), color.z (), color.w () };
}

} // namespace

void
render_viewports (const viewport_render_params &params)
{
  ZoneScoped;
  auto &registry = *params.registry;
  auto &runtime_ctx = *params.runtime_ctx;
  auto &window = *params.window;
  auto &rendering = *params.rendering;
  auto &renderer = *params.renderer;
  auto &ctx = registry.ctx ();

  // ---- Project quality defaults (D5) ----
  comp::viewport_quality_defaults quality{};
  if (auto project = runtime_ctx.resource_manager ().current_project ()) {
    quality = project->window.quality;
  }

  uint32_t win_w = 0;
  uint32_t win_h = 0;
  window.get_size (win_w, win_h);
  if (win_w == 0 || win_h == 0) {
    return;
  }
  math::vec2f const window_size{ static_cast<float> (win_w),
                                 static_cast<float> (win_h) };

  // ---- Presentation viewport (D6/D9) ----
  entt::entity presentation = entt::null;
  if (auto *editor_ctx = runtime_ctx.editor_ctx ();
      editor_ctx != nullptr && !runtime_ctx.is_running ()) {
    entt::entity const selected = editor_ctx->game_view_selected_viewport ();
    if (selected != entt::null
        && registry.all_of<comp::subviewport> (selected)) {
      presentation = selected;
    }
  } else if (rendering.render_viewport != entt::null
             && registry.all_of<comp::subviewport> (
                 rendering.render_viewport)) {
    presentation = rendering.render_viewport;
  }

  // ---- Root viewport resolve (computed, read-only fields) ----
  gfx::resolved_viewport const root_resolved = gfx::resolve_viewport (
      rendering.root_viewport, window_size,
      rendering.root_viewport_virtual_size,
      rendering.root_viewport.resolved_render_scale (quality.render_scale));
  rendering.root_viewport_resolution = root_resolved.render_size;
  rendering.root_container_size = window_size;

  // ---- Presentation resolve ----
  comp::viewport_settings const *present_settings = &rendering.root_viewport;
  comp::viewport_settings *present_settings_mut = &rendering.root_viewport;
  math::vec2f present_base = rendering.root_viewport_virtual_size;
  if (presentation != entt::null) {
    if (auto *sv = registry.try_get<comp::subviewport> (presentation)) {
      present_settings = &sv->settings;
      present_settings_mut = &sv->settings;
      present_base = sv->virtual_size;
    }
  }
  gfx::resolved_viewport present_resolved
      = (presentation == entt::null)
            ? root_resolved
            : gfx::resolve_viewport (*present_settings, window_size,
                                     present_base,
                                     present_settings->resolved_render_scale (
                                         quality.render_scale));

  int const present_msaa = present_settings->resolved_msaa (quality.msaa);
  comp::viewport_filter const present_filter
      = present_settings->resolved_filter (quality.filter);
  bool const present_is_2d
      = (presentation != entt::null)
        && comp::is_2d_subviewport (registry, presentation);

  // Ensure the window targets match the presentation viewport.
  window.ensure_root_targets (
      static_cast<uint32_t> (present_resolved.render_size.x ()),
      static_cast<uint32_t> (present_resolved.render_size.y ()), present_msaa);
  window.present_rect (present_resolved.output_rect);

  // ---- Phase 1: build the viewport tree top-down ----
  std::vector<viewport_node> nodes;
  std::unordered_map<entt::entity, std::size_t> index_of;
  std::unordered_set<entt::entity> visited;
  viewport_route_table route_table;

  nodes.emplace_back ();
  {
    viewport_node &root = nodes[0];
    root.entity = presentation;
    root.parent = parent_of (registry, presentation);
    root.parent_index = static_cast<std::size_t> (-1);
    root.chain_root = true;
    root.mode = present_is_2d ? comp::subviewport_mode::mode_2d
                              : comp::subviewport_mode::mode_3d;
    root.is_2d = present_is_2d;
    root.resolved = present_resolved;
    root.msaa = present_msaa;
    root.filter = present_filter;
    root.settings = present_settings_mut;
    root.env = comp::resolve_viewport_environment (registry, presentation);

    if (auto *sv = registry.try_get<comp::subviewport> (presentation)) {
      root.position = math::vec2f{ 0.0F, 0.0F };
      root.size = sv->size;
      root.hdr = sv->hdr;
      root.bloom = sv->settings.env.bloom_enabled;
      root.display_order = sv->display_order;
      sv->container_size = math::vec2f{ present_resolved.container_size.x (),
                                        present_resolved.container_size.y () };
    }

    sys::render_view_target target{};
    target.viewport = presentation;
    target.mode = root.mode;
    target.resolved = root.resolved;
    target.skybox = root.env.skybox;
    root.submission = sys::render_submission{};
    sys::build_render_frame (registry, runtime_ctx, root.submission, target);
    root.renders
        = root.settings->update_mode != comp::viewport_update_mode::disabled;

    // Input routing entry for the chain root.
    {
      viewport_route_entry entry{};
      entry.entity = presentation;
      entry.parent_index = -1;
      entry.resolved = root.resolved;
      entry.is_2d = root.is_2d;
      entry.input_routing = true;
      route_table.entries.push_back (entry);
      route_table.root_output_rect = root.resolved.output_rect;
    }

    if (presentation != entt::null) {
      visited.insert (presentation);
    }
    index_of[presentation] = 0;
  }

  std::vector<std::size_t> build_stack{ 0 };
  while (!build_stack.empty ()) {
    std::size_t const parent_index = build_stack.back ();
    build_stack.pop_back ();
    viewport_node const parent_copy = nodes[parent_index]; // index-stable
    entt::entity const parent_entity = parent_copy.entity;

    // Collect direct child viewports (cycle-safe via `visited`).
    std::vector<entt::entity> children;
    for (auto const e : registry.view<comp::subviewport> ()) {
      if (visited.count (e) != 0) {
        continue;
      }
      if (comp::find_parent_viewport (registry, e) == parent_entity) {
        children.push_back (e);
      }
    }

    // Order them by hierarchy order (D11): depth-first over the parent's
    // subtree following the `hierarchy` sibling links, so ties in
    // `display_order` break the same way the tree displays. Entities that the
    // (malformed) sibling chain does not reach fall back to entity-id order.
    std::unordered_map<entt::entity, int> visit_index;
    {
      int next_visit = 0;
      std::function<void (entt::entity)> visit;
      visit = [&] (entt::entity entity) {
        if (entity == entt::null || visit_index.count (entity) != 0) {
          return;
        }
        visit_index[entity] = next_visit++;
        auto *h = registry.try_get<comp::hierarchy> (entity);
        entt::entity child = (h != nullptr) ? h->first : entt::null;
        while (child != entt::null) {
          auto *ch = registry.try_get<comp::hierarchy> (child);
          entt::entity const next = (ch != nullptr) ? ch->next : entt::null;
          visit (child);
          child = next;
        }
      };

      if (parent_entity != entt::null) {
        auto *ph = registry.try_get<comp::hierarchy> (parent_entity);
        for (entt::entity e = (ph != nullptr) ? ph->first : entt::null;
             e != entt::null;) {
          auto *eh = registry.try_get<comp::hierarchy> (e);
          entt::entity const next = (eh != nullptr) ? eh->next : entt::null;
          visit (e);
          e = next;
        }
      } else {
        for (auto const e : registry.view<comp::hierarchy> ()) {
          if (registry.get<comp::hierarchy> (e).parent == entt::null) {
            visit (e);
          }
        }
      }
    }

    std::sort (children.begin (), children.end (),
               [&visit_index] (entt::entity a, entt::entity b) {
                 constexpr int unvisited = std::numeric_limits<int>::max ();
                 auto const ia = visit_index.find (a);
                 auto const ib = visit_index.find (b);
                 int const va
                     = (ia != visit_index.end ()) ? ia->second : unvisited;
                 int const vb
                     = (ib != visit_index.end ()) ? ib->second : unvisited;
                 if (va != vb) {
                   return va < vb;
                 }
                 return entt::to_integral (a) < entt::to_integral (b);
               });

    for (entt::entity const child : children) {
      auto *sv = registry.try_get<comp::subviewport> (child);
      if (sv == nullptr) {
        continue;
      }
      visited.insert (child);

      viewport_node node;
      node.entity = child;
      node.parent = parent_entity;
      node.mode = comp::subviewport_render_mode (registry, child);
      node.is_2d = node.mode == comp::subviewport_mode::mode_2d;
      node.hdr = sv->hdr;
      node.bloom = sv->settings.env.bloom_enabled;
      node.settings = &sv->settings;
      node.display_order = sv->display_order;
      node.msaa = sv->settings.resolved_msaa (quality.msaa);
      node.filter = sv->settings.resolved_filter (quality.filter);
      node.env = comp::resolve_viewport_environment (registry, child);

      // Container size: 2D children use `size * parent_scaling_factor`
      // (D2); 3D children derive from the screen-projected quad bounds.
      math::vec2f container{ 0.0F, 0.0F };
      if (node.is_2d) {
        auto *t2d = registry.try_get<comp::transform_2d> (child);
        node.position
            = (t2d != nullptr) ? t2d->position : math::vec2f{ 0.0F, 0.0F };
        node.size = sv->size;
        math::vec2f const parent_scale
            = parent_copy.resolved.target_px_per_virtual ();
        container = math::vec2f{
          std::max (1.0F, sv->size.x () * parent_scale.x ()),
          std::max (1.0F, sv->size.y () * parent_scale.y ()),
        };
      } else {
        node.position = math::vec2f{ 0.0F, 0.0F };
        node.size = sv->world_quad_size;
        if (parent_copy.submission.view.valid) {
          container
              = projected_quad_size (parent_copy.submission.view, registry,
                                     child, sv->world_quad_size);
        } else {
          container = sv->world_quad_size;
        }
      }

      node.resolved = gfx::resolve_viewport (
          sv->settings, container, sv->virtual_size,
          sv->settings.resolved_render_scale (quality.render_scale));
      if (!node.is_2d) {
        force_quad_aspect (node.resolved, sv->world_quad_size);
      }

      sys::render_view_target target{};
      target.viewport = child;
      target.mode = node.mode;
      target.resolved = node.resolved;
      target.skybox = node.env.skybox;
      sys::build_render_frame (registry, runtime_ctx, node.submission, target);
      sv->container_size = math::vec2f{ node.resolved.container_size.x (),
                                        node.resolved.container_size.y () };

      // ---- Visibility ----
      bool renders = parent_copy.renders;
      switch (sv->settings.update_mode) {
      case comp::viewport_update_mode::disabled:
        renders = false;
        break;
      case comp::viewport_update_mode::when_parent_visible:
        renders = parent_copy.renders;
        break;
      case comp::viewport_update_mode::when_visible:
        if (!node.submission.view.valid) {
          renders = false;
        } else if (node.is_2d) {
          // 2D: rect [position, position + 2D Size] vs the parent's visible
          // virtual area.
          math::vec2f const parent_virt = parent_copy.resolved.virtual_size;
          renders = rects_intersect (node.position.x (), node.position.y (),
                                     sv->size.x (), sv->size.y (), 0.0F, 0.0F,
                                     parent_virt.x (), parent_virt.y ());
        } else {
          glm::vec3 mn;
          glm::vec3 mx;
          quad_world_aabb (registry, child, sv->world_quad_size, mn, mx);
          renders
              = aabb_in_frustum (parent_copy.submission.view.view_proj, mn, mx);
        }
        renders = renders && parent_copy.renders;
        break;
      case comp::viewport_update_mode::once:
      case comp::viewport_update_mode::always:
      default:
        renders = parent_copy.renders;
        break;
      }
      node.renders = renders;
      node.parent_index = parent_index;

      std::size_t const child_index = nodes.size ();
      nodes.push_back (std::move (node));
      nodes[parent_index].children.push_back (child_index);
      index_of[child] = child_index;
      build_stack.push_back (child_index);

      // Input routing entry (I1).
      {
        viewport_route_entry entry{};
        entry.entity = child;
        entry.parent_index = static_cast<int> (parent_index);
        entry.resolved = nodes[child_index].resolved;
        entry.is_2d = nodes[child_index].is_2d;
        entry.input_routing = sv->input_routing;
        entry.position = nodes[child_index].position;
        entry.size = nodes[child_index].size;
        entry.display_order = sv->display_order;
        if (!entry.is_2d) {
          if (auto *wt = registry.try_get<comp::world_transform> (child)) {
            entry.has_quad = true;
            entry.quad_world = wt->value ();
          }
          entry.parent_view_proj = parent_copy.submission.view.view_proj;
          entry.parent_render_rect = parent_copy.resolved.render_rect;
          entry.parent_render_size = parent_copy.resolved.render_size;
        }
        route_table.entries.push_back (entry);
      }
    }
  }

  // Publish the routing table for pointer translation (I1).
  registry.ctx ().insert_or_assign (route_table);

  // ---- Phase 2: render post-order (children before parents) ----
  std::function<void (std::size_t)> render_node;
  render_node = [&] (std::size_t index) {
    viewport_node &node = nodes[index];

    for (std::size_t const child_index : node.children) {
      if (nodes[child_index].renders) {
        render_node (child_index);
      }
    }

    comp::subviewport *sv
        = (node.entity != entt::null)
              ? registry.try_get<comp::subviewport> (node.entity)
              : nullptr;

    registry.ctx ().insert_or_assign (node.env);
    registry.ctx ().insert_or_assign (node.entity);

    // ---- Render target ----
    gfx::subviewport_target *target = nullptr;
    if (!node.chain_root) {
      gfx::subviewport_target_config cfg{};
      cfg.width = static_cast<uint32_t> (
          std::max (1.0F, node.resolved.render_size.x ()));
      cfg.height = static_cast<uint32_t> (
          std::max (1.0F, node.resolved.render_size.y ()));
      cfg.sample_count = node.msaa;
      cfg.is_2d = node.is_2d;
      cfg.hdr = node.hdr;
      cfg.bloom = node.bloom;

      auto &slot = rendering.subviewport_targets[node.entity];
      if (!slot.matches (cfg)) {
        gfx::subviewport_target::destroy (slot);
        slot = gfx::subviewport_target::create (
            &window, &runtime_ctx.render_ctx (), cfg);
      }
      target = &slot;
    }

    // ---- Clear state (S5) ----
    bool clear_color = node.env.clear_color_enabled;
    bool clear_depth = node.env.clear_depth;
    if (node.settings != nullptr) {
      if (node.settings->clear_mode == comp::viewport_clear_mode::never) {
        clear_color = false;
        clear_depth = false;
      } else if (node.settings->clear_mode
                 == comp::viewport_clear_mode::next_frame) {
        node.settings->clear_mode = comp::viewport_clear_mode::never;
      }
    }
    SDL_FColor const clear_value = to_sdl_color (node.env.clear_color);
    gfx::pixel_rect const content_rect = node.resolved.render_rect;

    apply_viewport_environment (params, node.env);
    renderer.set_force_unlit (false);
    renderer.set_environment (node.submission.environment);

    bool color_pass_rendered = false;
    bool need_postprocess = false;

    if (!node.submission.view.valid) {
      // No camera: clear the target and skip all draw passes (S2).
      if (node.is_2d) {
        if (target != nullptr) {
          window.begin_subviewport_pass (*target, clear_value, clear_color,
                                         clear_depth, content_rect,
                                         "Subviewport Clear");
          window.end_subviewport_pass ();
        }
      } else {
        if (target != nullptr) {
          window.begin_subviewport_pass (*target, clear_value, clear_color,
                                         clear_depth, content_rect,
                                         "Subviewport Clear");
          window.end_subviewport_pass ();
        } else {
          window.begin_3d_pass (clear_value, clear_color, clear_depth,
                                content_rect, "Root Clear");
          window.end_3d_pass (node.chain_root);
        }
      }
    } else {
      // ---- 3D pass ----
      if (!node.is_2d) {
        ZoneScopedN ("viewport_render::3d_pass");
        renderer.set_visible_draws (node.submission.draw_commands);
        if (target != nullptr) {
          window.begin_subviewport_pass (*target, clear_value, clear_color,
                                         clear_depth, content_rect,
                                         "Subviewport 3D Pass");
        } else {
          window.begin_3d_pass (clear_value, clear_color, clear_depth,
                                content_rect, "Main 3D Pass");
        }

        renderer.begin_frame (node.submission.view);
        if (params.skybox_sys != nullptr) {
          params.skybox_sys->render_record_draw_cmd (&registry);
        }
        if (params.render_3d_sys != nullptr) {
          params.render_3d_sys->render_record_draw_cmd (&registry);
        }
        if (params.physics_sys != nullptr) {
          params.physics_sys->render_record_draw_cmd (&registry);
        }
        if (params.scene != nullptr) {
          for (auto &sys : params.scene->systems) {
            if (sys == nullptr) {
              continue;
            }
            if (params.core_system_ids != nullptr
                && std::binary_search (params.core_system_ids->begin (),
                                       params.core_system_ids->end (),
                                       sys->get_type_id ())) {
              continue;
            }
            sys->render_record_draw_cmd (&registry);
          }
        }

        // Composite 3D child quads inside this pass so they participate in
        // the parent's depth buffer (D3/C2).
        renderer.set_force_unlit (true);
        for (std::size_t const child_index : node.children) {
          viewport_node const &child = nodes[child_index];
          if (child.mode != comp::subviewport_mode::mode_3d) {
            continue;
          }
          auto it = rendering.subviewport_targets.find (child.entity);
          if (it == rendering.subviewport_targets.end ()) {
            continue;
          }
          auto *tex = it->second.present_texture ();
          if (tex == nullptr || tex->get () == nullptr) {
            continue;
          }
          auto *child_sv = registry.try_get<comp::subviewport> (child.entity);
          if (child_sv == nullptr) {
            continue;
          }

          if (!rendering.subviewport_quad_model) {
            rendering.subviewport_quad_model = gfx::model_3d::make_unit_quad ();
          }
          auto model = rendering.subviewport_quad_model;
          if (!model || model->meshes.empty ()
              || model->meshes[0].primitives.empty ()) {
            continue;
          }

          auto *wt = registry.try_get<comp::world_transform> (child.entity);
          if (wt == nullptr) {
            continue;
          }
          glm::mat4 m = wt->value ();
          m = glm::scale (m, glm::vec3 (child_sv->world_quad_size.x (),
                                        child_sv->world_quad_size.y (), 1.0F));

          auto &prim = model->meshes[0].primitives[0];
          SDL_GPUTexture *prev_tex = prim.mat.base_color_tex;
          SDL_GPUSampler *prev_samp = prim.mat.sampler;
          prim.mat.base_color_tex = tex->get ();
          prim.mat.sampler = (child.filter == comp::viewport_filter::nearest)
                                 ? window.nearest_sampler ().get ()
                                 : window.linear_sampler ().get ();
          prim.mat.device = nullptr;

          renderer.draw_model (*model, 0, m, node.submission.view.view_proj);

          prim.mat.base_color_tex = prev_tex;
          prim.mat.sampler = prev_samp;
          prim.mat.device = window.ctx ()->gpu_device;
        }
        renderer.set_force_unlit (false);

        if (target != nullptr) {
          window.end_subviewport_pass ();
        } else {
          // Root post-process runs after the 2D pass so overlays are
          // tonemapped exactly once (C1).
          window.end_3d_pass (false);
        }
      }

      // ---- 2D pass ----
      if (params.render_2d_sys != nullptr) {
        ZoneScopedN ("viewport_render::2d_pass");
        auto &r2d
            = rendering.ensure_renderer_2d (window, runtime_ctx.render_ctx (),
                                            &runtime_ctx.resource_manager ());

        if (node.is_2d) {
          r2d.set_projection (node.submission.view.proj
                              * node.submission.view.view);
        } else {
          float const vw = node.resolved.virtual_size.x ();
          float const vh = node.resolved.virtual_size.y ();
          r2d.set_projection (glm::ortho (0.0F, vw, vh, 0.0F, -1.0F, 1.0F));
        }

        params.render_2d_sys->render_record_draw_cmd (&registry);

        // Composite 2D children sorted with the regular 2D draws by
        // `display_order` (D11).
        for (std::size_t const child_index : node.children) {
          viewport_node const &child = nodes[child_index];
          if (child.mode != comp::subviewport_mode::mode_2d) {
            continue;
          }
          auto it = rendering.subviewport_targets.find (child.entity);
          if (it == rendering.subviewport_targets.end ()) {
            continue;
          }
          auto *tex = it->second.present_texture ();
          if (tex == nullptr || tex->get () == nullptr) {
            continue;
          }
          auto *child_sv = registry.try_get<comp::subviewport> (child.entity);
          if (child_sv == nullptr) {
            continue;
          }

          gfx::pixel_rect const dst = gfx::compose_2d_rect (
              child.position, child.size,
              node.resolved.target_px_per_virtual (), child.resolved);

          // Keep policy letterboxes inside the sub's container: draw the
          // unused border black (spec 4.2).
          if (!child.resolved.full_output ()) {
            auto *black = window.zero_bloom_texture ();
            gfx::batch_renderer_2d::draw_command bars{};
            bars.texture_override = black;
            bars.position = glm::vec2 (dst.x - child.resolved.output_rect.x,
                                       dst.y - child.resolved.output_rect.y);
            bars.size = glm::vec2 (
                static_cast<float> (child.resolved.container_size.x ()),
                static_cast<float> (child.resolved.container_size.y ()));
            bars.color = glm::vec4 (0.0F, 0.0F, 0.0F, 1.0F);
            bars.z_index = child.display_order - 1;
            r2d.submit (bars);
          }

          gfx::batch_renderer_2d::draw_command cmd{};
          cmd.texture_override = tex->get ();
          cmd.position = glm::vec2 (static_cast<float> (dst.x),
                                    static_cast<float> (dst.y));
          cmd.size = glm::vec2 (static_cast<float> (dst.width),
                                static_cast<float> (dst.height));
          cmd.color = glm::vec4 (1.0F);
          cmd.z_index = child.display_order;
          cmd.nearest_filter = child.filter == comp::viewport_filter::nearest;
          r2d.submit (cmd);
        }

        r2d.build_and_upload ();

        if (target != nullptr) {
          window.begin_subviewport_pass (*target, clear_value, false, false,
                                         content_rect, "2D Sprite Pass");
          r2d.draw ();
          window.end_subviewport_pass ();
        } else {
          window.begin_3d_pass (clear_value, false, false, content_rect,
                                "2D Sprite Pass");
          r2d.draw ();
          need_postprocess = true;
          window.end_3d_pass (true);
        }
        color_pass_rendered = true;
      }

      if (!color_pass_rendered && target == nullptr) {
        // No 2D renderer: post-process after the 3D pass.
        window.end_3d_pass (true);
      }
    }

    // ---- Per-viewport tonemap/bloom (D4/C2) ----
    if (target != nullptr && !node.is_2d && !node.hdr) {
      window.postprocess_subviewport (*target, node.env.exposure,
                                      node.env.bloom_intensity);
    }

    // ---- Update mode bookkeeping ----
    if (sv != nullptr && node.renders
        && sv->settings.update_mode == comp::viewport_update_mode::once) {
      sv->settings.update_mode = comp::viewport_update_mode::disabled;
    }
  };

  // Single global shadow + lighting pass (v1: lights/shadows shared, C3).
  // Subviewport subtrees with `shadow_caster == false` are filtered out of
  // the shadow casters (S5); the lighting pass reads the same visible set.
  if (nodes[0].submission.view.valid) {
    if (runtime_ctx.render_ctx ().command_buffer () != nullptr) {
      SDL_PushGPUDebugGroup (runtime_ctx.render_ctx ().command_buffer (),
                             "Shadows & Lighting");
    }
    std::vector<gfx::scene_renderer::draw_command> shadow_draws;
    shadow_draws.reserve (nodes[0].submission.draw_commands.size ());
    bool filtered = false;
    for (auto const &draw : nodes[0].submission.draw_commands) {
      if (comp::entity_casts_shadows (registry, draw.entity)) {
        shadow_draws.push_back (draw);
      } else {
        filtered = true;
      }
    }
    if (filtered) {
      renderer.set_visible_draws (shadow_draws);
    } else {
      renderer.set_visible_draws (nodes[0].submission.draw_commands);
    }

    renderer.set_environment (nodes[0].submission.environment);
    renderer.begin_frame (nodes[0].submission.view);
    if (params.shadow_sys != nullptr) {
      params.shadow_sys->render_record_draw_cmd (&registry);
    }
    if (params.lighting_sys != nullptr) {
      params.lighting_sys->render_record_draw_cmd (&registry);
    }
    if (runtime_ctx.render_ctx ().command_buffer () != nullptr) {
      SDL_PopGPUDebugGroup (runtime_ctx.render_ctx ().command_buffer ());
    }
  }

  if (nodes[0].renders) {
    render_node (0);
  }

  // Restore the default viewport scope for editor/UI passes.
  registry.ctx ().erase<entt::entity> ();
  registry.ctx ().erase<comp::resolved_viewport_environment> ();

  // ---- Clean up targets for destroyed subviewports (D13) ----
  for (auto it = rendering.subviewport_targets.begin ();
       it != rendering.subviewport_targets.end ();) {
    if (!registry.valid (it->first)
        || !registry.all_of<comp::subviewport> (it->first)) {
      gfx::subviewport_target::destroy (it->second);
      it = rendering.subviewport_targets.erase (it);
    } else {
      ++it;
    }
  }
}

} // namespace sys
} // namespace wsl
