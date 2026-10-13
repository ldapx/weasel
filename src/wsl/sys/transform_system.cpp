#include "transform_system.hpp"

#include "../comp/hierarchy.hpp"
#include "../comp/transform.hpp"
#include "../comp/transform_2d.hpp"
#include "../comp/world_transform.hpp"
#include "event/event_hub.hpp"
#include "wsl/log/log.hpp"
#include <entt/entity/entity.hpp>
#include <entt/entity/fwd.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <unordered_set>

namespace wsl
{

namespace sys
{

void
transform_system::update_world_recursive (
    entt::registry &reg, entt::entity e, const glm::mat4 &parent_world,
    std::unordered_set<entt::entity> &path, int depth) const
{
  if (!reg.valid (e)) {
    return;
  }

  if (!path.insert (e).second) {
    wsl::log::sys ()->error (
        "transform_system: cycle detected in hierarchy path at entity {} "
        "(already visited from this root)",
        static_cast<std::uint32_t> (e));
    return;
  }

  glm::mat4 child_world = parent_world;

  if (reg.all_of<comp::transform, comp::world_transform> (e)
      && !reg.all_of<comp::transform_2d> (e)) {
    comp::transform const &tr = reg.get<comp::transform> (e);
    comp::world_transform &wt = reg.get<comp::world_transform> (e);

    glm::mat4 const local = tr.model ();
    wt.value () = parent_world * local;
    child_world = wt.value ();
  }

  if (auto *h = reg.try_get<comp::hierarchy> (e)) {
    std::unordered_set<entt::entity> siblings;
    for (entt::entity child = h->first; child != entt::null;) {
      if (!reg.valid (child) || !siblings.insert (child).second) {
        wsl::log::sys ()->warn (
            "transform_system: skipping invalid or cyclic child link from {}",
            static_cast<std::uint32_t> (e));
        break;
      }
      const auto *child_hierarchy = reg.try_get<comp::hierarchy> (child);
      if (child_hierarchy == nullptr) {
        wsl::log::sys ()->warn (
            "transform_system: child {} has no hierarchy component",
            static_cast<std::uint32_t> (child));
        break;
      }
      const entt::entity next = child_hierarchy->next;
      update_world_recursive (reg, child, static_cast<glm::mat4> (child_world),
                              path, depth + 1);
      child = next;
    }
  }

  path.erase (e);
}

void
transform_system::register_event_sources (event::event_hub &hub)
{
  (void)hub;
}

void
transform_system::register_event_sinks (event::event_hub &hub)
{
  (void)hub;
}

void
transform_system::register_iterations (event::event_hub &hub)
{
  clear_registered_iterations ();

  register_iteration<comp::transform, comp::world_transform> (
      hub, "update_world_transforms",
      [this] (entt::registry &registry, double dt) {
        update_world_transforms (registry, dt);
      });
}

void
transform_system::on_update (entt::registry &registry, double dt)
{
  run_registered_iterations (registry, dt);
}

void
transform_system::on_editor_update (entt::registry &registry, double dt)
{
  run_registered_iterations (registry, dt);
}

void
transform_system::update_world_transforms (entt::registry &reg,
                                           double /*unused*/)
{
  // 1. Process roots (no hierarchy or parent == null)
  // Skip entities with transform_2d — they use a separate 2D transform path.
  auto view = reg.view<comp::transform, comp::world_transform> (
      entt::exclude<comp::transform_2d>);

  for (entt::entity const e : view) {
    auto *h = reg.try_get<comp::hierarchy> (e);
    if ((h == nullptr) || h->parent == entt::null) {
      std::unordered_set<entt::entity> path;
      update_world_recursive (reg, e, glm::mat4{ 1.0F }, path, 0);
    }
  }
}

} // namespace sys

} // namespace wsl
