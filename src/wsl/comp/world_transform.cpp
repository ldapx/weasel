#include "world_transform.hpp"

#include "component_meta.hpp"

#include <entt/entt.hpp>

namespace wsl
{

namespace comp
{

void
world_transform::register_meta ()
{
  using namespace entt::literals;

  // The ImGui inspector (custom_inspect) is registered by editor_support.
  {
    entt::meta_factory<comp::world_transform> ()
        .type (entt::type_hash<comp::world_transform>::value ())
        .custom<comp::meta_info> (meta_info{
            "World Transform", "Computed world-space transform (read-only)",
            "engine://icons/comp_world_transform.svg" });
  }

  entt::meta_factory<comp::world_transform> ()
      .data<&comp::world_transform::m_value> ("matrix"_hs)
      .custom<comp::meta_info> (meta_info{
          "Matrix", "Final world matrix after hierarchy evaluation", "" });
}

} // namespace comp

} // namespace wsl
