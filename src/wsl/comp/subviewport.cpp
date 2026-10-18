#include "subviewport.hpp"

#include "../log/log.hpp"
#include "../rsc/scene.hpp"
#include "../rsc/scene_manager.hpp"
#include "camera.hpp"
#include "camera_2d.hpp"
#include "hierarchy.hpp"
#include "singl/runtime_context.hpp"
#include "transform.hpp"
#include "transform_2d.hpp"

#include <entt/meta/factory.hpp>

namespace wsl::comp
{

void
subviewport_camera_ui::register_meta ()
{
  using namespace entt::literals;

  // The ImGui inspector (custom_inspect) is registered by editor_support.
  entt::meta_factory<comp::subviewport_camera_ui> ()
      .type (entt::type_hash<comp::subviewport_camera_ui>::value ())
      .data<&comp::subviewport_camera_ui::value> ("value"_hs)
      .custom<comp::meta_info> (
          meta_info{ "Camera", "Camera entity for this viewport.", "" });
}

void
subviewport::register_meta ()
{
  // Ensure the nested UI type and the shared settings are registered.
  subviewport_camera_ui::register_meta ();
  register_viewport_settings_meta ();

  using namespace entt::literals;

  entt::meta_factory<comp::subviewport> ()
      .type (entt::type_hash<comp::subviewport>::value ())
      .custom<comp::meta_info> (meta_info{
          "Sub-Viewport",
          "Independent rendering context for its descendants; mode is "
          "component-driven (transform_2d = 2D, transform = 3D).",
          "" })
      .data<&subviewport::size> ("size"_hs)
      .custom<comp::meta_info> (meta_info{
          "2D Size", "Extents in the parent viewport's virtual space.", "" })
      .data<&subviewport::world_quad_size> ("world_quad_size"_hs)
      .custom<comp::meta_info> (
          meta_info{ "World Quad Size",
                     "Size of the quad when rendered in 3D space.", "" })
      .data<&subviewport::virtual_size> ("virtual_size"_hs)
      .custom<comp::meta_info> (meta_info{
          "Virtual Size",
          "Internal logical canvas size used for layout and rendering.", "" })
      .data<&subviewport::display_order> ("display_order"_hs)
      .custom<comp::meta_info> (meta_info{
          "Display Order", "2D compositing order in the parent viewport.", "" })
      .data<&subviewport::input_routing> ("input_routing"_hs)
      .custom<comp::meta_info> (meta_info{
          "Input Routing", "Route pointer input into this subviewport.", "" })
      .data<&subviewport::hdr> ("hdr"_hs)
      .custom<comp::meta_info> (meta_info{
          "HDR", "3D only: output raw linear HDR instead of an LDR tonemap.",
          "" })
      .data<&subviewport::camera> ("camera"_hs)
      .custom<comp::meta_info> (
          meta_info{ "Camera", "Camera assigned to this viewport.", "" })
      .data<&subviewport::settings> ("settings"_hs)
      .custom<comp::meta_info> (
          meta_info{ "Settings",
                     "Scaling, aspect, quality and environment settings.", "" })
      .data<&subviewport::container_size> ("container_size"_hs)
      .custom<comp::meta_info> (meta_info{
          "Container Size", "Computed pixel rect in the parent render target.",
          "", true });
}

subviewport_mode
subviewport_render_mode (entt::registry &registry, entt::entity entity)
{
  if (entity != entt::null && registry.all_of<transform_2d> (entity)) {
    return subviewport_mode::mode_2d;
  }
  return subviewport_mode::mode_3d;
}

bool
is_2d_subviewport (entt::registry &registry, entt::entity entity)
{
  return entity != entt::null && registry.all_of<transform_2d> (entity);
}

entt::entity
find_nearest_viewport (entt::registry &registry, entt::entity entity)
{
  auto parent_of = [&registry] (entt::entity e) -> entt::entity {
    if (e == entt::null) {
      return entt::null;
    }
    auto *h = registry.try_get<hierarchy> (e);
    return (h != nullptr) ? h->parent : entt::null;
  };

  entt::entity current = entity;
  entt::entity slow = entity;
  entt::entity fast = entity;
  bool cycle_reported = false;

  while (current != entt::null) {
    if (registry.all_of<subviewport> (current)) {
      return current;
    }

    current = parent_of (current);

    // Floyd's cycle detection over the same parent chain. A cycle is
    // malformed hierarchy data; abort the walk instead of looping forever.
    slow = parent_of (slow);
    entt::entity const fast_step = parent_of (fast);
    fast = parent_of (fast_step);
    if (slow != entt::null && slow == fast) {
      if (!cycle_reported) {
        wsl::log::core ()->error (
            "find_nearest_viewport: hierarchy cycle detected; treating the "
            "entity as root-scoped");
        cycle_reported = true;
      }
      return entt::null;
    }
  }
  return entt::null;
}

entt::entity
find_parent_viewport (entt::registry &registry, entt::entity entity)
{
  auto *h = registry.try_get<hierarchy> (entity);
  if (h == nullptr) {
    return entt::null;
  }
  return find_nearest_viewport (registry, h->parent);
}

bool
entity_in_viewport_scope (entt::registry &registry, entt::entity entity,
                          entt::entity target_viewport)
{
  if (entity == entt::null) {
    return false;
  }
  if (target_viewport != entt::null && entity == target_viewport) {
    return false;
  }
  return find_nearest_viewport (registry, entity) == target_viewport;
}

bool
entity_casts_shadows (entt::registry &registry, entt::entity entity)
{
  if (entity == entt::null) {
    return true;
  }

  // Any ancestor subviewport with `shadow_caster == false` excludes the
  // whole subtree. Bounded walk: a malformed hierarchy cycle terminates
  // instead of looping forever.
  constexpr int max_depth = 4096;
  entt::entity current = entity;
  for (int depth = 0; current != entt::null && depth < max_depth; ++depth) {
    auto *sv = registry.try_get<subviewport> (current);
    if (sv != nullptr && !sv->settings.shadow_caster) {
      return false;
    }
    auto *h = registry.try_get<hierarchy> (current);
    current = (h != nullptr) ? h->parent : entt::null;
  }
  return true;
}

} // namespace wsl::comp
