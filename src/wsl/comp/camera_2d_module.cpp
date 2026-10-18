module;
#include <entt/meta/factory.hpp>
#include <cstdint>

module wsl.core;

namespace wsl::comp
{

void
camera_2d::register_meta ()
{
  using namespace entt::literals;

  auto factory = reflect_type<camera_2d> (
      stable_type_id<camera_2d> (), "Camera 2D", "2D orthographic camera.", "");

  reflect_field<camera_2d, &camera_2d::zoom> (factory, "zoom", "Zoom");
  reflect_field<camera_2d, &camera_2d::layer> (factory, "layer", "Layer");
  reflect_field<camera_2d, &camera_2d::only_for_editor> (
      factory, "only_for_editor", "Only For Editor");
}

} // namespace wsl::comp
