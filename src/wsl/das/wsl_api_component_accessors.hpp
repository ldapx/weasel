#pragma once

#include "wsl/comp/area3d.hpp"
#include "wsl/comp/audio.hpp"
#include "wsl/comp/camera_2d.hpp"
#include "wsl/comp/character_body.hpp"
#include "wsl/comp/directional_light.hpp"
#include "wsl/comp/hierarchy.hpp"
#include "wsl/comp/model_instance_3d.hpp"
#include "wsl/comp/point_light.hpp"
#include "wsl/comp/prefab_instance.hpp"
#include "wsl/comp/rigid_body.hpp"
#include "wsl/comp/spot_light.hpp"
#include "wsl/comp/sprite_2d.hpp"
#include "wsl/comp/subviewport.hpp"
#include "wsl/comp/transform.hpp"
#include "wsl/comp/transform_2d.hpp"
#include "wsl/comp/world_transform.hpp"
#include "wsl/math/vector.hpp"

#ifndef IN_MODULE_INTERFACE
#include <daScript/misc/vectypes.h>
#endif
#ifndef IN_MODULE_INTERFACE
#include <daScript/simulate/runtime_matrices.h>
#endif

#ifndef IN_MODULE_INTERFACE
#include <entt/entt.hpp>
#endif

#ifndef IN_MODULE_INTERFACE
#include <type_traits>
#endif

// The vector properties are exposed to daslang as references to the dasLang
// built-in float2/3/4 types, so the engine math types must be layout-identical
// to them (the getters reinterpret live component storage). These asserts keep
// that contract from silently breaking if either side's layout changes.

static_assert (sizeof (::wsl::math::vec2f) == sizeof (::das::float2)
                   && alignof (::wsl::math::vec2f) == alignof (::das::float2)
                   && std::is_standard_layout_v<::wsl::math::vec2f>
                   && std::is_trivially_copyable_v<::wsl::math::vec2f>,
               "::wsl::math::vec2f must be layout-identical to das::float2");

static_assert (sizeof (::wsl::math::vec3f) == sizeof (::das::float3)
                   && alignof (::wsl::math::vec3f) == alignof (::das::float3)
                   && std::is_standard_layout_v<::wsl::math::vec3f>
                   && std::is_trivially_copyable_v<::wsl::math::vec3f>,
               "::wsl::math::vec3f must be layout-identical to das::float3");

static_assert (sizeof (::wsl::math::vec4f) == sizeof (::das::float4)
                   && alignof (::wsl::math::vec4f) == alignof (::das::float4)
                   && std::is_standard_layout_v<::wsl::math::vec4f>
                   && std::is_trivially_copyable_v<::wsl::math::vec4f>,
               "::wsl::math::vec4f must be layout-identical to das::float4");

static_assert (sizeof (::wsl::math::quatf) == sizeof (::das::float4)
                   && alignof (::wsl::math::quatf) == alignof (::das::float4)
                   && std::is_standard_layout_v<::wsl::math::quatf>
                   && std::is_trivially_copyable_v<::wsl::math::quatf>,
               "::wsl::math::quatf must be layout-identical to das::float4");

namespace das
{
class Module;
class ModuleLibrary;
struct LineInfoArg;
}

namespace wsl::das
{

entt::registry *active_registry ();

template <typename T> struct component_ref
{
  entt::registry *registry;
  entt::entity entity;

  void
  bind (entt::registry *owner, entt::entity target)
  {
    registry = owner;
    entity = target;
  }

  T *
  get () const
  {
    entt::registry *active = active_registry ();
    if (registry == nullptr || active != registry
        || !registry->valid (entity)) {
      return nullptr;
    }
    return registry->template try_get<T> (entity);
  }

  operator T *() const { return get (); }
  T *
  operator->() const
  {
    return get ();
  }
};

// The vector properties below are exposed to daslang as references to the
// dasLang built-in float2/3/4 types. math::vec2f/vec3f/vec4f and
// das::float2/3/4 are layout-identical (three/four consecutive floats), so a
// reinterpreted reference lets daslang read/write live component storage
// directly (chained writes like `t.scale.x = 2.0` and full-struct writes
// like `t.scale = float3(...)` both hit the component in place).

struct TransformProxy
{
  entt::entity entity;
  component_ref<comp::transform> comp;

  bool valid () const;
  void bind (comp::transform *component);

  ::das::float3 &position ();
  ::das::float4 &rotation ();
  ::das::float3 &scale ();
};

struct Transform2DProxy
{
  entt::entity entity;
  component_ref<comp::transform_2d> comp;

  bool valid () const;
  void bind (comp::transform_2d *component);

  ::das::float2 &position ();
  ::das::float2 &scale ();
  float &rotation ();
};

struct Camera2DProxy
{
  entt::entity entity;
  component_ref<comp::camera_2d> comp;

  bool valid () const;
  void bind (comp::camera_2d *component);

  float &zoom ();
};

struct Sprite2DProxy
{
  entt::entity entity;
  component_ref<comp::sprite_2d> comp;

  bool valid () const;
  void bind (comp::sprite_2d *component);

  ::das::float4 &color ();
  ::das::float2 &size ();
};

struct PointLightProxy
{
  entt::entity entity;
  component_ref<comp::point_light> comp;

  bool valid () const;
  void bind (comp::point_light *component);

  ::das::float3 &color ();
  float &intensity ();
};

struct DirectionalLightProxy
{
  entt::entity entity;
  component_ref<comp::directional_light> comp;

  bool valid () const;
  void bind (comp::directional_light *component);

  ::das::float3 &color ();
  float &intensity ();
};

struct SpotLightProxy
{
  entt::entity entity;
  component_ref<comp::spot_light> comp;

  bool valid () const;
  void bind (comp::spot_light *component);

  ::das::float3 &color ();
  float &intensity ();
};

TransformProxy get_transform_accessor (uint32_t entity,
                                       ::das::LineInfoArg *at = nullptr);
Transform2DProxy get_transform_2d_accessor (uint32_t entity,
                                            ::das::LineInfoArg *at = nullptr);
Camera2DProxy get_camera_2d_accessor (uint32_t entity,
                                      ::das::LineInfoArg *at = nullptr);
Sprite2DProxy get_sprite_2d_accessor (uint32_t entity,
                                      ::das::LineInfoArg *at = nullptr);
PointLightProxy get_point_light_accessor (uint32_t entity,
                                          ::das::LineInfoArg *at = nullptr);
DirectionalLightProxy get_directional_light_accessor (uint32_t entity,
                                                      ::das::LineInfoArg *at
                                                      = nullptr);
SpotLightProxy get_spot_light_accessor (uint32_t entity,
                                        ::das::LineInfoArg *at = nullptr);

struct HierarchyProxy
{
  entt::entity entity;
  component_ref<comp::hierarchy> comp;

  bool valid () const;
  void bind (comp::hierarchy *component);

  uint32_t &parent ();
  uint32_t &first ();
  uint32_t &next ();
};

struct WorldTransformProxy
{
  entt::entity entity;
  component_ref<comp::world_transform> comp;

  bool valid () const;
  void bind (comp::world_transform *component);

  ::das::float4x4 &matrix ();
};

struct RigidBodyProxy
{
  entt::entity entity;
  component_ref<comp::rigid_body> comp;

  bool valid () const;
  void bind (comp::rigid_body *component);

  int32_t &shape ();
  ::das::float3 &position ();
  ::das::float4 &rotation ();
  ::das::float3 &half_extents ();
  float &radius ();
  float &density ();
  bool &dynamic ();
  float &friction ();
  float &restitution ();
  float get_mass () const;
};

struct CharacterBodyProxy
{
  entt::entity entity;
  component_ref<comp::character_body> comp;

  bool valid () const;
  void bind (comp::character_body *component);

  float &height ();
  float &radius ();
  ::das::float3 &desired_velocity ();
};

struct ModelInstance3DProxy
{
  entt::entity entity;
  component_ref<comp::model_instance_3d> comp;

  bool valid () const;
  void bind (comp::model_instance_3d *component);

  uint32_t &model_id ();
  uint32_t &scene_index ();
  uint32_t &material_override ();
  float &mip_lod_bias ();
  float &geometry_lod_bias ();
  float &visibility_range ();
};

struct Area3DProxy
{
  entt::entity entity;
  component_ref<comp::area> comp;

  bool valid () const;
  void bind (comp::area *component);

  int32_t &shape ();
  ::das::float3 &position ();
  ::das::float4 &rotation ();
  ::das::float3 &half_extents ();
  float &radius ();
};

struct AudioProxy
{
  entt::entity entity;
  component_ref<comp::audio> comp;

  bool valid () const;
  void bind (comp::audio *component);

  uint32_t &audio_resource ();
  bool &loop ();
  bool &play_on_start ();
  float &volume ();
  bool &playing ();
};

struct PrefabInstanceProxy
{
  entt::entity entity;
  component_ref<comp::prefab_instance> comp;

  bool valid () const;
  void bind (comp::prefab_instance *component);

  uint32_t &prefab_id ();
  uint32_t &prefab_entity ();
};

struct SubviewportProxy
{
  entt::entity entity;
  component_ref<comp::subviewport> comp;

  bool valid () const;
  void bind (comp::subviewport *component);

  ::das::float2 &size ();
  ::das::float2 &world_quad_size ();
  ::das::float2 &container_size ();
  ::das::float2 &virtual_size ();
  uint32_t &camera ();
  int &display_order ();
  bool &input_routing ();
  bool &hdr ();
};

HierarchyProxy get_hierarchy_accessor (uint32_t entity,
                                       ::das::LineInfoArg *at = nullptr);
WorldTransformProxy get_world_transform_accessor (uint32_t entity,
                                                  ::das::LineInfoArg *at
                                                  = nullptr);
RigidBodyProxy get_rigid_body_accessor (uint32_t entity,
                                        ::das::LineInfoArg *at = nullptr);
float get_rigid_body_mass (RigidBodyProxy &proxy,
                           ::das::LineInfoArg *at = nullptr);
CharacterBodyProxy
get_character_body_accessor (uint32_t entity, ::das::LineInfoArg *at = nullptr);
ModelInstance3DProxy get_model_instance_3d_accessor (uint32_t entity,
                                                     ::das::LineInfoArg *at
                                                     = nullptr);
Area3DProxy get_area_3d_accessor (uint32_t entity,
                                  ::das::LineInfoArg *at = nullptr);
AudioProxy get_audio_accessor (uint32_t entity,
                               ::das::LineInfoArg *at = nullptr);
PrefabInstanceProxy get_prefab_instance_accessor (uint32_t entity,
                                                  ::das::LineInfoArg *at
                                                  = nullptr);
SubviewportProxy get_subviewport_accessor (uint32_t entity,
                                           ::das::LineInfoArg *at = nullptr);

void register_component_accessors (::das::Module &mod,
                                   ::das::ModuleLibrary &lib);

} // namespace wsl::das
