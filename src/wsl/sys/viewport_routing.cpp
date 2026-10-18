#include "viewport_routing.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>

namespace wsl::sys
{

namespace
{

using gfx::pixel_rect;

bool
point_in_rect (math::vec2f point, const pixel_rect &rect)
{
  return !rect.empty () && point.x () >= static_cast<float> (rect.x)
         && point.y () >= static_cast<float> (rect.y)
         && point.x () < static_cast<float> (rect.x + rect.width)
         && point.y () < static_cast<float> (rect.y + rect.height);
}

/** Ray/quad intersection in the quad's local (XY plane) space. */
bool
ray_hits_quad (const glm::mat4 &inv_view_proj, const glm::mat4 &quad_world,
               math::vec2f ndc, math::vec2f quad_size, math::vec2f &out_uv)
{
  glm::vec4 const near_h
      = inv_view_proj * glm::vec4 (ndc.x (), ndc.y (), -1.0F, 1.0F);
  glm::vec4 const far_h
      = inv_view_proj * glm::vec4 (ndc.x (), ndc.y (), 1.0F, 1.0F);
  if (std::abs (near_h.w) < 1e-6F || std::abs (far_h.w) < 1e-6F) {
    return false;
  }

  glm::vec3 const ray_origin = glm::vec3 (near_h) / near_h.w;
  glm::vec3 const ray_far = glm::vec3 (far_h) / far_h.w;
  glm::vec3 const ray_dir = ray_far - ray_origin;

  glm::mat4 const inv_world = glm::inverse (quad_world);
  glm::vec3 const local_origin
      = glm::vec3 (inv_world * glm::vec4 (ray_origin, 1.0F));
  glm::vec3 const local_dir = glm::mat3 (inv_world) * ray_dir;

  if (std::abs (local_dir.z) < 1e-8F) {
    return false;
  }
  float const t = -local_origin.z / local_dir.z;
  if (t < 0.0F) {
    return false;
  }

  glm::vec3 const local = local_origin + local_dir * t;
  float const half_w = quad_size.x () * 0.5F;
  float const half_h = quad_size.y () * 0.5F;
  if (half_w <= 0.0F || half_h <= 0.0F) {
    return false;
  }
  if (std::abs (local.x) > half_w || std::abs (local.y) > half_h) {
    return false;
  }

  // Texture space: U grows to the right, V grows downwards.
  out_uv = math::vec2f{ (local.x / half_w) * 0.5F + 0.5F,
                        0.5F - (local.y / half_h) * 0.5F };
  return true;
}

struct router
{
  const viewport_route_table &table;

  viewport_route_point
  descend (std::size_t index, math::vec2f container_px) const
  {
    viewport_route_point result;
    auto const &entry = table.entries[index];
    result.viewport = entry.entity;
    result.hit = true;

    math::vec2f const render_px
        = container_to_target (index, container_px, result.hit);

    // Collect children, highest display order first.
    std::vector<std::size_t> children;
    for (std::size_t i = 0; i < table.entries.size (); ++i) {
      if (table.entries[i].parent_index == static_cast<int> (index)) {
        children.push_back (i);
      }
    }
    std::stable_sort (children.begin (), children.end (),
                      [this] (std::size_t a, std::size_t b) {
                        return table.entries[a].display_order
                               > table.entries[b].display_order;
                      });

    for (std::size_t const child_index : children) {
      auto const &child = table.entries[child_index];
      if (!child.input_routing) {
        continue;
      }

      if (child.is_2d) {
        if (!result.hit) {
          continue;
        }
        pixel_rect const dest = gfx::compose_2d_rect (
            child.position, child.size, entry.resolved.target_px_per_virtual (),
            child.resolved);
        if (!point_in_rect (render_px, dest)) {
          continue;
        }
        math::vec2f const child_container_px{
          render_px.x () - static_cast<float> (dest.x)
              + static_cast<float> (child.resolved.output_rect.x),
          render_px.y () - static_cast<float> (dest.y)
              + static_cast<float> (child.resolved.output_rect.y),
        };
        return descend (child_index, child_container_px);
      }

      if (child.has_quad) {
        // The point must lie inside the parent's 3D content rect; NDC is
        // built from the render rect actually used by the 3D pass.
        pixel_rect const &content = child.parent_render_rect;
        if (!point_in_rect (render_px, content)) {
          continue;
        }
        float const u = (render_px.x () - static_cast<float> (content.x))
                        / static_cast<float> (std::max (1, content.width));
        float const v = (render_px.y () - static_cast<float> (content.y))
                        / static_cast<float> (std::max (1, content.height));
        math::vec2f const ndc{ u * 2.0F - 1.0F, 1.0F - v * 2.0F };

        math::vec2f uv;
        if (!ray_hits_quad (glm::inverse (child.parent_view_proj),
                            child.quad_world, ndc, child.size, uv)) {
          continue;
        }

        viewport_route_point quad_hit;
        quad_hit.viewport = child.entity;
        quad_hit.hit = true;
        quad_hit.point
            = math::vec2f{ uv.x () * child.resolved.virtual_size.x (),
                           uv.y () * child.resolved.virtual_size.y () };
        return quad_hit;
      }
    }

    (void)render_px;
    return result;
  }

  /** Maps the entry's container point into its render-target pixels. */
  math::vec2f
  container_to_target (std::size_t index, math::vec2f container_px,
                       bool &ok) const
  {
    math::vec2f target_px;
    ok = gfx::map_output_point_to_target (
        container_px, table.entries[index].resolved, target_px);
    return ok ? target_px : math::vec2f{ 0.0F, 0.0F };
  }
};

} // namespace

viewport_route_point
route_viewport_point (const viewport_route_table &table,
                      math::vec2f window_point)
{
  if (table.entries.empty ()) {
    return {};
  }

  // The chain root's container is the window; entry 0 is the root.
  viewport_route_point root;
  if (!gfx::map_output_point_to_virtual (
          window_point, table.entries[0].resolved, root.point)) {
    return {};
  }
  root.viewport = table.entries[0].entity;
  root.hit = true;

  router const r{ table };
  return r.descend (0, window_point);
}

viewport_route_point
route_viewport_point (entt::registry &registry, math::vec2f window_point)
{
  auto &ctx = registry.ctx ();
  if (!ctx.contains<viewport_route_table> ()) {
    return {};
  }
  return route_viewport_point (ctx.get<viewport_route_table> (), window_point);
}

} // namespace wsl::sys
