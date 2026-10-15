#include "subviewport.hpp"

#include "../rsc/scene.hpp"
#include "../rsc/scene_manager.hpp"
#include "camera.hpp"
#include "camera_2d.hpp"
#include "hierarchy.hpp"
#include "singl/runtime_context.hpp"

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
  // Ensure subviewport_camera_ui is registered (it's not a world component)
  subviewport_camera_ui::register_meta ();

  using namespace entt::literals;

  entt::meta_factory<comp::subviewport> ()
      .type (entt::type_hash<comp::subviewport>::value ())
      .custom<comp::meta_info> (meta_info{
          "Sub-Viewport",
          "Defines a viewport sub-region in the entity hierarchy.", "" })
      .data<&subviewport::x> ("x"_hs)
      .custom<comp::meta_info> (
          meta_info{ "X", "Normalized left edge (0 = left).", "" })
      .data<&subviewport::y> ("y"_hs)
      .custom<comp::meta_info> (
          meta_info{ "Y", "Normalized top edge (0 = top).", "" })
      .data<&subviewport::width> ("width"_hs)
      .custom<comp::meta_info> (
          meta_info{ "Width", "Normalized width (1.0 = full).", "" })
      .data<&subviewport::height> ("height"_hs)
      .custom<comp::meta_info> (
          meta_info{ "Height", "Normalized height (1.0 = full).", "" })
      .data<&subviewport::clear_color> ("clear_color"_hs)
      .custom<comp::meta_info> (meta_info{
          "Clear Color", "Clear the color target before rendering.", "" })
      .data<&subviewport::clear_depth> ("clear_depth"_hs)
      .custom<comp::meta_info> (meta_info{
          "Clear Depth", "Clear the depth target before rendering.", "" })
      .data<&subviewport::clear_r> ("clear_r"_hs)
      .custom<comp::meta_info> (meta_info{ "Clear R", "Red clear value.", "" })
      .data<&subviewport::clear_g> ("clear_g"_hs)
      .custom<comp::meta_info> (
          meta_info{ "Clear G", "Green clear value.", "" })
      .data<&subviewport::clear_b> ("clear_b"_hs)
      .custom<comp::meta_info> (meta_info{ "Clear B", "Blue clear value.", "" })
      .data<&subviewport::clear_a> ("clear_a"_hs)
      .custom<comp::meta_info> (
          meta_info{ "Clear Alpha", "Alpha clear value.", "" })
      .data<&subviewport::camera_2d> ("camera_2d"_hs)
      .custom<comp::meta_info> (
          meta_info{ "Camera 2D", "2D camera assigned to this viewport.", "" })
      .data<&subviewport::camera_3d> ("camera_3d"_hs)
      .custom<comp::meta_info> (
          meta_info{ "Camera 3D", "3D camera assigned to this viewport.", "" })
      .data<&subviewport::world_quad_size> ("world_quad_size"_hs)
      .custom<comp::meta_info> (
          meta_info{ "World Quad Size",
                     "Size of the quad when rendered in 3D space.", "" })
      .data<&subviewport::container_size> ("container_size"_hs)
      .custom<comp::meta_info> (
          meta_info{ "Container Size", "Size in pixels for 2D overlay.", "" })
      .data<&subviewport::container_position> ("container_position"_hs)
      .custom<comp::meta_info> (meta_info{
          "Container Position", "Position in pixels for 2D overlay.", "" })
      .data<&subviewport::virtual_size> ("virtual_size"_hs)
      .custom<comp::meta_info> (meta_info{
          "Virtual Size", "Internal resolution of the viewport.", "" })
      .data<&subviewport::render_2d_only> ("render_2d_only"_hs)
      .custom<comp::meta_info> (meta_info{
          "Render 2D Only",
          "If true, this viewport is rendered in 2D mode only.", "" });
}

entt::entity
find_nearest_viewport (entt::registry &registry, entt::entity entity)
{
  entt::entity ancestor = entity;
  while (ancestor != entt::null) {
    if (registry.all_of<subviewport> (ancestor)) {
      return ancestor;
    }
    auto *h = registry.try_get<hierarchy> (ancestor);
    ancestor = (h != nullptr) ? h->parent : entt::null;
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

} // namespace wsl::comp
