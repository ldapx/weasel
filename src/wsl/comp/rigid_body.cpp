#include "rigid_body.hpp"

#include "comp/component_meta.hpp"
#include "comp/singl/runtime_context.hpp"
#include "phys/layers.hpp"
#include "phys/physics_engine.hpp"

#include <cstdint>
#include <cstdio>
#include <entt/core/hashed_string.hpp>
#include <entt/core/type_info.hpp>
#include <entt/meta/factory.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/fwd.hpp>

namespace wsl
{

namespace comp
{

// ---------------- rigid_body::allowed_dofs_ui ----------------

void
rigid_body::allowed_dofs_ui::register_meta ()
{
  using namespace entt::literals;

  auto &&f = entt::meta_factory<rigid_body::allowed_dofs_ui> ().type (
      entt::type_hash<rigid_body::allowed_dofs_ui>::value ());
  (f.data<&rigid_body::allowed_dofs_ui::value>)("value"_hs)
      .custom<comp::meta_info> (
          comp::meta_info{ "Allowed DOFs",
                           "Degree-of-freedom bitmask: All, or any combination "
                           "of Translation X/Y/Z and Rotation X/Y/Z",
                           "" });
}

// ---------------- rigid_body::motion_type_ui ----------------

void
rigid_body::motion_type_ui::register_meta ()
{
  using namespace entt::literals;

  auto &&f = entt::meta_factory<rigid_body::motion_type_ui> ().type (
      entt::type_hash<rigid_body::motion_type_ui>::value ());
  (f.data<&rigid_body::motion_type_ui::value>)("value"_hs)
      .custom<comp::meta_info> (comp::meta_info{
          "Motion Type", "How the physics solver drives this body", "" });
}

// ---------------- rigid_body::collision_layer_ui ----------------

void
rigid_body::collision_layer_ui::register_meta ()
{
  using namespace entt::literals;

  auto &&f = entt::meta_factory<rigid_body::collision_layer_ui> ().type (
      entt::type_hash<rigid_body::collision_layer_ui>::value ());
  (f.data<&rigid_body::collision_layer_ui::value>)("value"_hs)
      .custom<comp::meta_info> (comp::meta_info{
          "Collision Layer",
          "Index of the single collision layer this body belongs to (0-7)",
          "" });
}

// ---------------- rigid_body::collision_mask_ui ----------------

void
rigid_body::collision_mask_ui::register_meta ()
{
  using namespace entt::literals;

  auto &&f = entt::meta_factory<rigid_body::collision_mask_ui> ().type (
      entt::type_hash<rigid_body::collision_mask_ui>::value ());
  (f.data<&rigid_body::collision_mask_ui::value>)("value"_hs)
      .custom<comp::meta_info> (comp::meta_info{
          "Collision Mask",
          "Bitmask of collision layers this body collides with; bit N is "
          "layer N, e.g. 255 = all 8 layers",
          "" });
}

// ---------------- rigid_body runtime ----------------

void
rigid_body::sync_applied_cache ()
{
  applied_shape = shape;
  applied_half_extents = half_extents;
  applied_radius = radius;
  applied_dynamic = dynamic;
  applied_motion = motion_type.value;
  applied_dofs = allowed_dofs.value;
  applied_collision_layer = collision_layer.value;
  applied_collision_mask = collision_mask.value;
  applied_friction = friction;
  applied_restitution = restitution;
  applied_density = density;
  applied_position = position;
  applied_rotation = rotation;
}

phys::object_layer
rigid_body::object_layer () const
{
  const phys::layers::motion_bucket motion
      = motion_type.value == phys::motion_type::Static
            ? phys::layers::motion_bucket::static_body
            : phys::layers::motion_bucket::moving_body;

  return phys::layers::make_rigidbody_object_layer (
      collision_layer.value, collision_mask.value, motion);
}

bool
rigid_body::has_structural_change () const
{
  return shape != applied_shape
         || half_extents.x () != applied_half_extents.x ()
         || half_extents.y () != applied_half_extents.y ()
         || half_extents.z () != applied_half_extents.z ()
         || radius != applied_radius || dynamic != applied_dynamic
         || motion_type.value != applied_motion
         || allowed_dofs.value != applied_dofs || density != applied_density;
}

bool
rigid_body::has_transform_change () const
{
  return position.x () != applied_position.x ()
         || position.y () != applied_position.y ()
         || position.z () != applied_position.z ()
         || rotation.x () != applied_rotation.x ()
         || rotation.y () != applied_rotation.y ()
         || rotation.z () != applied_rotation.z ()
         || rotation.w () != applied_rotation.w ();
}

bool
rigid_body::has_surface_change () const
{
  return collision_layer.value != applied_collision_layer
         || collision_mask.value != applied_collision_mask
         || friction != applied_friction || restitution != applied_restitution;
}

bool
rigid_body::has_scale_change (const math::vec3f &scale) const
{
  return scale.x () != applied_scale.x () || scale.y () != applied_scale.y ()
         || scale.z () != applied_scale.z ();
}

rigid_body
rigid_body::create_box_body (phys::engine &engine, const glm::vec3 &pos,
                             const glm::quat &rot, const glm::vec3 &he,
                             bool dyn)
{
  rigid_body rb;
  rb.shape = shape_type::box;
  rb.position = math::vec3f{ pos };
  rb.rotation = math::quatf{ rot };
  rb.half_extents = math::vec3f{ he };

  rb.dynamic = dyn;
  rb.motion_type.value
      = dyn ? phys::motion_type::Dynamic : phys::motion_type::Static;
  rb.allowed_dofs.value = phys::allowed_dofs::All;

  rb.sync_applied_cache ();
  rb.applied_scale = math::vec3f{ 1, 1, 1 };
  rb.create_body (engine, pos, rot);
  return rb;
}

rigid_body
rigid_body::create_sphere_body (phys::engine &engine, const glm::vec3 &pos,
                                float r, phys::motion_type motion,
                                phys::allowed_dofs dofs)
{
  rigid_body rb;
  rb.shape = shape_type::sphere;
  rb.position = math::vec3f{ pos };
  rb.rotation = math::quatf{ glm::quat (1, 0, 0, 0) };
  rb.radius = r;

  rb.motion_type.value = motion;
  rb.allowed_dofs.value = dofs;
  rb.dynamic = (motion == phys::motion_type::Dynamic);

  rb.sync_applied_cache ();
  rb.applied_scale = math::vec3f{ 1, 1, 1 };
  rb.create_body (engine, pos, glm::quat (1, 0, 0, 0));
  return rb;
}

void
rigid_body::destroy_body (phys::engine &engine)
{
  if (!phys::is_valid_body_id (body_id)) {
    return;
  }

  engine.on_remove_body (body_id);
  body_id = phys::null_body_id;
}

void
rigid_body::rebuild_body (phys::engine &engine, const glm::vec3 &world_pos,
                          const glm::quat &world_rot, const glm::vec3 &scale)
{
  destroy_body (engine);
  create_body (engine, world_pos, world_rot, scale);
}

void
rigid_body::create_body (phys::engine &engine, const glm::vec3 &world_pos,
                         const glm::quat &world_rot, const glm::vec3 &scale)
{
  if (phys::is_valid_body_id (body_id)) {
    destroy_body (engine);
  }

  dynamic = (motion_type.value == phys::motion_type::Dynamic);
  phys::body_desc desc;
  desc.motion = motion_type.value;
  desc.allowed_dofs = allowed_dofs.value;
  desc.layer = object_layer ();
  desc.shape = shape == shape_type::box ? phys::shape_type::box
                                        : phys::shape_type::sphere;
  desc.position = { world_pos.x, world_pos.y, world_pos.z };
  desc.rotation = { world_rot.x, world_rot.y, world_rot.z, world_rot.w };
  desc.half_extents
      = { half_extents.x () * scale.x, half_extents.y () * scale.y,
          half_extents.z () * scale.z };
  desc.radius = radius * ((scale.x + scale.y + scale.z) / 3.0F);
  desc.density = density;
  desc.friction = friction;
  desc.restitution = restitution;
  body_id = engine.create_body (desc);
}

void
rigid_body::apply_transform_to_body (phys::engine &engine) const
{
  if (!phys::is_valid_body_id (body_id)) {
    return;
  }

  // Read current body world position/rotation
  const phys::vector3 body = engine.get_body_position (body_id);
  const phys::quaternion rotation_value = engine.get_body_rotation (body_id);
  glm::vec3 const body_pos (body.x, body.y, body.z);
  glm::quat const body_rot (rotation_value.w, rotation_value.x,
                            rotation_value.y, rotation_value.z);

  // Undo old offset to recover the transform's world rotation
  glm::quat const old_off_rot = (glm::quat)applied_rotation;
  glm::quat const xform_rot = body_rot * glm::inverse (old_off_rot);

  // Apply new offset: body = transform * offset
  glm::quat const new_off_rot = (glm::quat)rotation;
  glm::vec3 const offset_delta
      = (glm::vec3)position - (glm::vec3)applied_position;
  glm::vec3 const new_body_pos = body_pos + xform_rot * offset_delta;
  glm::quat const new_body_rot = xform_rot * new_off_rot;

  engine.set_body_transform (
      body_id, { new_body_pos.x, new_body_pos.y, new_body_pos.z },
      { new_body_rot.x, new_body_rot.y, new_body_rot.z, new_body_rot.w });
}

void
rigid_body::apply_surface_properties_to_body (phys::engine &engine) const
{
  if (!phys::is_valid_body_id (body_id)) {
    return;
  }

  engine.set_body_surface_properties (body_id, friction, restitution,
                                      object_layer ());
}

void
rigid_body::on_inspector_changed (comp::singl::runtime_context *runtime,
                                  const glm::vec3 &scale)
{
  phys::engine *engine = (runtime != nullptr)
                             ? runtime->try_get_active_physics_engine ()
                             : nullptr;
  if (engine == nullptr) {
    return;
  }

  // Sanitize dimensions
  sanitize_dimensions ();
  sanitize_surface_properties ();

  const bool structural_change = has_structural_change ();
  const bool surface_change = has_surface_change ();
  const bool xform_change = has_transform_change ();

  if (!phys::is_valid_body_id (body_id)) {
    sync_applied_cache ();
    applied_scale = math::vec3f{ scale.x, scale.y, scale.z };
    return;
  }

  if (structural_change) {
    // Read current body position from physics engine to preserve world
    // placement
    const phys::vector3 current = engine->get_body_position (body_id);
    const phys::quaternion current_rotation
        = engine->get_body_rotation (body_id);
    glm::vec3 const current_pos (current.x, current.y, current.z);
    glm::quat const current_rot (current_rotation.w, current_rotation.x,
                                 current_rotation.y, current_rotation.z);
    rebuild_body (*engine, current_pos, current_rot, scale);
    applied_scale = math::vec3f{ scale.x, scale.y, scale.z };
  } else {
    if (xform_change) {
      apply_transform_to_body (*engine);
    }
    if (surface_change) {
      apply_surface_properties_to_body (*engine);
    }
  }

  sync_applied_cache ();
}

void
rigid_body::register_meta ()
{
  using namespace entt::literals;

  rigid_body::motion_type_ui::register_meta ();
  rigid_body::allowed_dofs_ui::register_meta ();
  rigid_body::collision_layer_ui::register_meta ();
  rigid_body::collision_mask_ui::register_meta ();

  entt::meta_factory<comp::rigid_body::shape_type> ()
      .type (entt::type_hash<comp::rigid_body::shape_type>::value ())
      .conv<int> ()
      .data<comp::rigid_body::shape_type::box> ("box"_hs)
      .custom<const char *> ("Box")
      .data<comp::rigid_body::shape_type::sphere> ("sphere"_hs)
      .custom<const char *> ("Sphere");

  entt::meta_factory<phys::motion_type> ()
      .type (entt::type_hash<phys::motion_type>::value ())
      .conv<int> ()
      .data<phys::motion_type::Static> ("static"_hs)
      .custom<const char *> ("Static")
      .data<phys::motion_type::Kinematic> ("kinematic"_hs)
      .custom<const char *> ("Kinematic")
      .data<phys::motion_type::Dynamic> ("dynamic"_hs)
      .custom<const char *> ("Dynamic");

  // `allowed_dofs` is a bitmask whose inspector presents the 13 combinations
  // below. Registering it as an entt enum is what lets `describe_component`
  // list usable values instead of an empty list, and lets `comp set` take
  // their names. Keep in sync with allowed_dofs_ui::custom_inspect.
  entt::meta_factory<phys::allowed_dofs> ()
      .type (entt::type_hash<phys::allowed_dofs>::value ())
      .conv<int> ()
      .data<phys::allowed_dofs::All> ("all"_hs)
      .custom<const char *> ("All")
      .data<phys::allowed_dofs::TranslationX> ("translation_x"_hs)
      .custom<const char *> ("Translation X")
      .data<phys::allowed_dofs::TranslationY> ("translation_y"_hs)
      .custom<const char *> ("Translation Y")
      .data<phys::allowed_dofs::TranslationZ> ("translation_z"_hs)
      .custom<const char *> ("Translation Z")
      .data<phys::allowed_dofs::RotationX> ("rotation_x"_hs)
      .custom<const char *> ("Rotation X")
      .data<phys::allowed_dofs::RotationY> ("rotation_y"_hs)
      .custom<const char *> ("Rotation Y")
      .data<phys::allowed_dofs::RotationZ> ("rotation_z"_hs)
      .custom<const char *> ("Rotation Z")
      .data<phys::allowed_dofs::TranslationX
            | phys::allowed_dofs::TranslationY> ("translation_xy"_hs)
      .custom<const char *> ("Translation XY")
      .data<phys::allowed_dofs::TranslationX
            | phys::allowed_dofs::TranslationZ> ("translation_xz"_hs)
      .custom<const char *> ("Translation XZ")
      .data<phys::allowed_dofs::TranslationY
            | phys::allowed_dofs::TranslationZ> ("translation_yz"_hs)
      .custom<const char *> ("Translation YZ")
      .data<phys::allowed_dofs::RotationX | phys::allowed_dofs::RotationY> (
          "rotation_xy"_hs)
      .custom<const char *> ("Rotation XY")
      .data<phys::allowed_dofs::RotationX | phys::allowed_dofs::RotationZ> (
          "rotation_xz"_hs)
      .custom<const char *> ("Rotation XZ")
      .data<phys::allowed_dofs::RotationY | phys::allowed_dofs::RotationZ> (
          "rotation_yz"_hs)
      .custom<const char *> ("Rotation YZ");

  entt::meta_factory<comp::rigid_body> ()
      .type (entt::type_hash<comp::rigid_body>::value ())
      .custom<comp::meta_info> (
          meta_info{ "Rigid Body", "Physics body simulated by Box3D",
                     "engine://icons/comp_rigidbody.svg" })
      .func<&comp::rigid_body::on_inspector_changed> ("on_inspector_changed"_hs)

      .data<&comp::rigid_body::shape> ("shape"_hs)
      .custom<comp::meta_info> (
          meta_info{ "Shape", "Collision shape type", "" })

      .data<&comp::rigid_body::position> ("position"_hs)
      .custom<comp::meta_info> (meta_info{
          "Position",
          "Local position offset relative to the entity's Transform position",
          "" })

      .data<&comp::rigid_body::rotation> ("rotation"_hs)
      .custom<comp::meta_info> (meta_info{
          "Rotation",
          "Local rotation offset relative to the entity's Transform rotation",
          "" })

      .data<&comp::rigid_body::half_extents> ("half_extents"_hs)
      .custom<comp::meta_info> (meta_info{
          "Half Extents",
          "Box half size (scaled by the entity's Transform scale)", "" })

      .data<&comp::rigid_body::radius> ("radius"_hs)
      .custom<comp::meta_info> (meta_info{
          "Radius", "Sphere radius (scaled by the entity's Transform scale)",
          "" })

      .data<&comp::rigid_body::density> ("density"_hs)
      .custom<comp::meta_info> (
          meta_info{ "Density",
                     "Material density in kg/m^3; derives the body mass from "
                     "its shape volume",
                     "" })

      .func<&comp::rigid_body::mass> ("mass"_hs)
      .custom<comp::meta_info> (meta_info{
          "Mass", "Derived body mass (kg) = density * shape volume", "" })

      .data<&comp::rigid_body::motion_type> ("motion_type"_hs)
      .custom<comp::meta_info> (
          meta_info{ "Motion Type", "Physics motion type", "" })

      .data<&comp::rigid_body::allowed_dofs> ("allowed_dofs"_hs)
      .custom<comp::meta_info> (
          meta_info{ "Allowed DOFs", "Movement constraints", "" })

      .data<&comp::rigid_body::collision_layer> ("collision_layer"_hs)
      .custom<comp::meta_info> (meta_info{
          "Collision Layer", "Which physics layer this body belongs to", "" })

      .data<&comp::rigid_body::collision_mask> ("collision_mask"_hs)
      .custom<comp::meta_info> (
          meta_info{ "Collision Mask",
                     "Which physics layers this body collides with", "" })

      .data<&comp::rigid_body::friction> ("friction"_hs)
      .custom<comp::meta_info> (meta_info{
          "Friction", "Surface friction used by the physics solver", "" })

      .data<&comp::rigid_body::restitution> ("restitution"_hs)
      .custom<comp::meta_info> (meta_info{
          "Restitution", "Surface bounciness used by the physics solver", "" })

      ;
}

} // namespace comp

} // namespace wsl
