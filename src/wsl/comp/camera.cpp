#include "camera.hpp"

#include "camera_2d.hpp"
#include "component_meta.hpp"
#include "singl/rendering_manager.hpp"
#include "singl/runtime_context.hpp"
#include "../rsc/scene.hpp"
#include "../rsc/scene_manager.hpp"

#include <entt/core/hashed_string.hpp>
#include <entt/entity/entity.hpp>
#include <entt/meta/factory.hpp>

namespace wsl
{

namespace comp
{

void
camera::register_meta ()
{
  using namespace entt::literals;

  // The ImGui inspector (custom_inspect) is registered by editor_support.
  entt::meta_factory<comp::camera> ()
      .type (entt::type_hash<comp::camera>::value ())
      .custom<comp::meta_info> (meta_info{ "Camera",
                                           "Projection parameters only",
                                           "engine://icons/comp_camera.svg" })
      .data<&camera::m_fov> ("fov"_hs)
      .custom<comp::meta_info> (
          meta_info{ "FOV", "Field of view in degrees", "" })
      .data<&camera::m_near> ("near"_hs)
      .custom<comp::meta_info> (meta_info{ "Near", "Near clipping plane", "" })
      .data<&camera::m_far> ("far"_hs)
      .custom<comp::meta_info> (meta_info{ "Far", "Far clipping plane", "" });
}

} // namespace comp

} // namespace wsl
