#pragma once

#include "../math/vector.hpp"
#include "component_meta.hpp"

#ifndef IN_MODULE_INTERFACE
#include <entt/entt.hpp>
#endif

namespace wsl::comp
{

/**
 * 2D orthographic camera component.
 *
 * An entity with this component should also have a transform_2d component that
 * defines the camera centre.  The projection is always orthographic.
 */
struct camera_2d : world_component
{
  float zoom = 1.0F;
  int layer = 0;
  bool only_for_editor = false;

  static void register_meta ();

  template <class Archive>
  void
  serialize (Archive &archive)
  {
    camera_2d def{};
    serialize_field_if_diff (archive, "zoom", zoom, def.zoom);
    serialize_field_if_diff (archive, "layer", layer, def.layer);
    serialize_field_if_diff (archive, "only_for_editor", only_for_editor,
                             def.only_for_editor);
  }
};

} // namespace wsl::comp
