// Registration of the ImGui-backed `custom_inspect` meta functions.
//
// The widget implementations live in editor_support (see the inspectors_*.cpp
// files); the core library only registers the reflected data fields. Editors
// call wsl::editor::register_editor_inspectors() after core registration.

#include "editor_support/editor_inspectors.hpp"

#include "wsl/comp/animator.hpp"
#include "wsl/comp/camera.hpp"
#include "wsl/comp/rigid_body.hpp"
#include "wsl/comp/singl/editor_context.hpp"
#include "wsl/comp/singl/runtime_context.hpp"
#include "wsl/comp/singl/ui_manager.hpp"
#include "wsl/comp/subviewport.hpp"
#include "wsl/comp/viewport_settings.hpp"
#include "wsl/comp/world_transform.hpp"
#include "wsl/math/matrix.hpp"
#include "wsl/math/vector.hpp"
#include "wsl/rsc/resource_manager.hpp"
#include "wsl/rsc/scene_manager.hpp"

#include <entt/core/hashed_string.hpp>
#include <entt/meta/factory.hpp>

namespace wsl
{
namespace editor
{

void
register_editor_inspectors ()
{
  static bool registered = false;
  if (registered) {
    return;
  }
  registered = true;

  using namespace entt::literals;

  // ---- editor session state ----
  (entt::meta_factory<comp::singl::editor_context> ()
       .func<
           &comp::singl::editor_context::custom_inspect>)("custom_inspect"_hs);

  // ---- world components ----
  (entt::meta_factory<comp::camera> ()
       .func<&comp::camera::custom_inspect>)("custom_inspect"_hs);
  (entt::meta_factory<comp::world_transform> ()
       .func<&comp::world_transform::custom_inspect>)("custom_inspect"_hs);
  (entt::meta_factory<comp::subviewport_camera_ui> ()
       .func<
           &comp::subviewport_camera_ui::custom_inspect>)("custom_inspect"_hs);

  // ---- shared root/sub viewport settings widget (E1) ----
  (entt::meta_factory<comp::viewport_settings> ()
       .func<&comp::viewport_settings::custom_inspect>)("custom_inspect"_hs);
  (entt::meta_factory<comp::animator> ()
       .func<&comp::animator::custom_inspect>)("custom_inspect"_hs);
  (entt::meta_factory<comp::rigid_body::allowed_dofs_ui> ()
       .func<&comp::rigid_body::allowed_dofs_ui::
                 custom_inspect>)("custom_inspect"_hs);
  (entt::meta_factory<comp::rigid_body::motion_type_ui> ()
       .func<&comp::rigid_body::motion_type_ui::
                 custom_inspect>)("custom_inspect"_hs);
  (entt::meta_factory<comp::rigid_body::collision_layer_ui> ()
       .func<&comp::rigid_body::collision_layer_ui::
                 custom_inspect>)("custom_inspect"_hs);
  (entt::meta_factory<comp::rigid_body::collision_mask_ui> ()
       .func<&comp::rigid_body::collision_mask_ui::
                 custom_inspect>)("custom_inspect"_hs);

  // ---- singletons / managers ----
  (entt::meta_factory<comp::singl::ui_manager> ()
       .func<&comp::singl::ui_manager::custom_inspect>)("custom_inspect"_hs);
  (entt::meta_factory<rsc::scene_manager> ()
       .func<&rsc::scene_manager::custom_inspect>)("custom_inspect"_hs);
  (entt::meta_factory<rsc::resource_manager_view> ()
       .func<&rsc::resource_manager_view::custom_inspect>)("custom_inspect"_hs);
  (entt::meta_factory<rsc::model_id> ()
       .func<&rsc::model_id::custom_inspect>)("custom_inspect"_hs);
  (entt::meta_factory<rsc::audio_id> ()
       .func<&rsc::audio_id::custom_inspect>)("custom_inspect"_hs);
  (entt::meta_factory<rsc::material_id> ()
       .func<&rsc::material_id::custom_inspect>)("custom_inspect"_hs);
  (entt::meta_factory<rsc::image_id> ()
       .func<&rsc::image_id::custom_inspect>)("custom_inspect"_hs);

  // ---- math types ----
  (entt::meta_factory<math::vec2f> ()
       .func<&math::vec2f::custom_inspect>)("custom_inspect"_hs);
  (entt::meta_factory<math::vec3f> ()
       .func<&math::vec3f::custom_inspect>)("custom_inspect"_hs);
  (entt::meta_factory<math::vec4f> ()
       .func<&math::vec4f::custom_inspect>)("custom_inspect"_hs);
  (entt::meta_factory<math::quatf> ()
       .func<&math::quatf::custom_inspect>)("custom_inspect"_hs);
  (entt::meta_factory<math::mat33f> ()
       .func<&math::mat33f::custom_inspect>)("custom_inspect"_hs);
  (entt::meta_factory<math::mat44f> ()
       .func<&math::mat44f::custom_inspect>)("custom_inspect"_hs);
}

} // namespace editor
} // namespace wsl
