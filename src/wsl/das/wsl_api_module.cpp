#if WEASEL_HAS_DASLANG
#include "wsl_api_module.hpp"
#include "wsl_api_component_accessors.hpp"
#include "das_interop.hpp"
#include "daScript/ast/ast.h"
#include "daScript/ast/ast_handle.h"
#include "daScript/ast/ast_interop.h"
#include "daScript/daScriptModule.h"
#include "daScript/misc/arraytype.h"
#include "daScript/simulate/aot.h"

#include "wsl/comp/transform.hpp"
#include "wsl/comp/camera.hpp"
#include "wsl/comp/hierarchy.hpp"
#include "wsl/comp/world_transform.hpp"
#include "wsl/comp/component_meta.hpp"
#include "wsl/comp/singl/runtime_context.hpp"
#include "wsl/comp/singl/editor_context.hpp"
#include "wsl/reg/system_factory_registry.hpp"
#include "wsl/comp/rigid_body.hpp"
#include "wsl/comp/character_body.hpp"
#include "wsl/comp/transform_2d.hpp"
#include "wsl/comp/camera_2d.hpp"
#include "wsl/comp/sprite_2d.hpp"
#include "wsl/comp/model_instance_3d.hpp"
#include "wsl/comp/point_light.hpp"
#include "wsl/comp/directional_light.hpp"
#include "wsl/comp/spot_light.hpp"
#include "wsl/comp/audio.hpp"
#include "wsl/comp/animator.hpp"
#include "wsl/comp/singl/runtime_context.hpp"
#include "wsl/comp/singl/ui_manager.hpp"
#include "wsl/log/log.hpp"
#include "wsl/ray.hpp"
#include "wsl/rsc/scene.hpp"
#include "wsl/rsc/resource_manager.hpp"
#include "wsl/reg/component_registry.hpp"
#include "wsl/reg/singleton_registry.hpp"
#include "wsl/event/event_hub.hpp"
#include "wsl/event/message_bus.hpp"
#include "wsl/input.hpp"
#include "wsl/phys/physics_engine.hpp"
#include "das_system_adapter.hpp"
#include "wsl_event_binds.hpp"

#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_keyboard.h>
#include <entt/core/hashed_string.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>

MAKE_TYPE_FACTORY (Transform, wsl::das::TransformProxy)
MAKE_TYPE_FACTORY (Transform2D, wsl::das::Transform2DProxy)
MAKE_TYPE_FACTORY (Camera2D, wsl::das::Camera2DProxy)
MAKE_TYPE_FACTORY (Sprite2D, wsl::das::Sprite2DProxy)
MAKE_TYPE_FACTORY (PointLight, wsl::das::PointLightProxy)
MAKE_TYPE_FACTORY (DirectionalLight, wsl::das::DirectionalLightProxy)
MAKE_TYPE_FACTORY (SpotLight, wsl::das::SpotLightProxy)
MAKE_TYPE_FACTORY (Hierarchy, wsl::das::HierarchyProxy)
MAKE_TYPE_FACTORY (WorldTransform, wsl::das::WorldTransformProxy)
MAKE_TYPE_FACTORY (RigidBody, wsl::das::RigidBodyProxy)
MAKE_TYPE_FACTORY (CharacterBody, wsl::das::CharacterBodyProxy)
MAKE_TYPE_FACTORY (ModelInstance3D, wsl::das::ModelInstance3DProxy)
MAKE_TYPE_FACTORY (Area3D, wsl::das::Area3DProxy)
MAKE_TYPE_FACTORY (Audio, wsl::das::AudioProxy)
MAKE_TYPE_FACTORY (PrefabInstance, wsl::das::PrefabInstanceProxy)
MAKE_TYPE_FACTORY (Subviewport, wsl::das::SubviewportProxy)

namespace wsl::das
{

namespace
{

// Tracks nested `query` iteration so structural mutations (add/remove
// component, entity destroy) can be rejected while references into the
// candidate pools are live. A query may mutate component fields in place, but
// must not add/remove components or destroy entities.
thread_local int g_query_depth = 0;

struct query_depth_guard
{
  query_depth_guard () { ++g_query_depth; }
  ~query_depth_guard () { --g_query_depth; }
};

thread_local entt::registry *g_registry = nullptr;

enum class deferred_operation
{
  destroy_entity,
  destroy_entity_only,
  add_component,
  remove_component,
  set_parent
};

struct deferred_command
{
  deferred_operation operation;
  uint64_t world;
  uint64_t entity_world;
  uint32_t entity_id;
  uint32_t type_id = 0;
  uint64_t parent_world = 0;
  uint32_t parent_id = NULL_ENTITY_ID;
  bool keep_world = true;
};

thread_local std::vector<deferred_command> g_deferred_commands;

void flush_deferred_commands ();

entt::registry *
get_registry ()
{
  return g_registry;
}

entt::registry *
registry_from_handle (uint64_t handle)
{
  auto *registry
      = reinterpret_cast<entt::registry *> (static_cast<uintptr_t> (handle));
  return registry != nullptr && registry == get_registry () ? registry
                                                            : nullptr;
}

template <typename T>
T &
fallback_value ()
{
  static thread_local T value{};
  value = T{};
  return value;
}

} // anonymous namespace

entt::registry *
active_registry ()
{
  return get_registry ();
}

// ── Component accessor proxy helpers ──

bool
TransformProxy::valid () const
{
  return comp != nullptr;
}

void
TransformProxy::bind (comp::transform *component)
{
  (void)component;
  comp.bind (active_registry (), entity);
}

::das::float3 &
TransformProxy::position ()
{
  return comp ? *reinterpret_cast<::das::float3 *> (&comp->position)
              : fallback_value<::das::float3> ();
}

::das::float4 &
TransformProxy::rotation ()
{
  return comp ? *reinterpret_cast<::das::float4 *> (&comp->rotation)
              : fallback_value<::das::float4> ();
}

::das::float3 &
TransformProxy::scale ()
{
  return comp ? *reinterpret_cast<::das::float3 *> (&comp->scale)
              : fallback_value<::das::float3> ();
}

bool
Transform2DProxy::valid () const
{
  return comp != nullptr;
}

void
Transform2DProxy::bind (comp::transform_2d *component)
{
  (void)component;
  comp.bind (active_registry (), entity);
}

::das::float2 &
Transform2DProxy::position ()
{
  return comp ? *reinterpret_cast<::das::float2 *> (&comp->position)
              : fallback_value<::das::float2> ();
}

::das::float2 &
Transform2DProxy::scale ()
{
  return comp ? *reinterpret_cast<::das::float2 *> (&comp->scale)
              : fallback_value<::das::float2> ();
}

float &
Transform2DProxy::rotation ()
{
  return comp ? comp->rotation : fallback_value<float> ();
}

bool
Camera2DProxy::valid () const
{
  return comp != nullptr;
}

void
Camera2DProxy::bind (comp::camera_2d *component)
{
  (void)component;
  comp.bind (active_registry (), entity);
}

float &
Camera2DProxy::zoom ()
{
  return comp ? comp->zoom : fallback_value<float> ();
}

bool
Sprite2DProxy::valid () const
{
  return comp != nullptr;
}

void
Sprite2DProxy::bind (comp::sprite_2d *component)
{
  (void)component;
  comp.bind (active_registry (), entity);
}

::das::float4 &
Sprite2DProxy::color ()
{
  return comp ? *reinterpret_cast<::das::float4 *> (&comp->color)
              : fallback_value<::das::float4> ();
}

::das::float2 &
Sprite2DProxy::size ()
{
  return comp ? *reinterpret_cast<::das::float2 *> (&comp->size)
              : fallback_value<::das::float2> ();
}

::das::float2 &
Sprite2DProxy::uv_offset ()
{
  return comp ? *reinterpret_cast<::das::float2 *> (&comp->uv_offset)
              : fallback_value<::das::float2> ();
}

::das::float2 &
Sprite2DProxy::uv_scale ()
{
  return comp ? *reinterpret_cast<::das::float2 *> (&comp->uv_scale)
              : fallback_value<::das::float2> ();
}

bool &
Sprite2DProxy::flip_v ()
{
  return comp ? comp->flip_v : fallback_value<bool> ();
}

bool &
Sprite2DProxy::flip_h ()
{
  return comp ? comp->flip_h : fallback_value<bool> ();
}

int &
Sprite2DProxy::z_index ()
{
  return comp ? comp->z_index : fallback_value<int> ();
}

bool
PointLightProxy::valid () const
{
  return comp != nullptr;
}

void
PointLightProxy::bind (comp::point_light *component)
{
  (void)component;
  comp.bind (active_registry (), entity);
}

::das::float3 &
PointLightProxy::color ()
{
  return comp ? *reinterpret_cast<::das::float3 *> (&comp->color)
              : fallback_value<::das::float3> ();
}

float &
PointLightProxy::intensity ()
{
  return comp ? comp->intensity : fallback_value<float> ();
}

float &
PointLightProxy::radius ()
{
  return comp ? comp->radius : fallback_value<float> ();
}

bool
DirectionalLightProxy::valid () const
{
  return comp != nullptr;
}

void
DirectionalLightProxy::bind (comp::directional_light *component)
{
  (void)component;
  comp.bind (active_registry (), entity);
}

::das::float3 &
DirectionalLightProxy::color ()
{
  return comp ? *reinterpret_cast<::das::float3 *> (&comp->color)
              : fallback_value<::das::float3> ();
}

float &
DirectionalLightProxy::intensity ()
{
  return comp ? comp->intensity : fallback_value<float> ();
}

bool
SpotLightProxy::valid () const
{
  return comp != nullptr;
}

void
SpotLightProxy::bind (comp::spot_light *component)
{
  (void)component;
  comp.bind (active_registry (), entity);
}

::das::float3 &
SpotLightProxy::color ()
{
  return comp ? *reinterpret_cast<::das::float3 *> (&comp->color)
              : fallback_value<::das::float3> ();
}

float &
SpotLightProxy::intensity ()
{
  return comp ? comp->intensity : fallback_value<float> ();
}

float &
SpotLightProxy::range ()
{
  return comp ? comp->range : fallback_value<float> ();
}

float &
SpotLightProxy::inner_cos ()
{
  return comp ? comp->inner_cos : fallback_value<float> ();
}

float &
SpotLightProxy::outer_cos ()
{
  return comp ? comp->outer_cos : fallback_value<float> ();
}

bool
HierarchyProxy::valid () const
{
  return comp != nullptr;
}

void
HierarchyProxy::bind (comp::hierarchy *component)
{
  (void)component;
  comp.bind (active_registry (), entity);
}

uint32_t &
HierarchyProxy::parent ()
{
  return comp ? reinterpret_cast<uint32_t &> (comp->parent)
              : fallback_value<uint32_t> ();
}

uint32_t &
HierarchyProxy::first ()
{
  return comp ? reinterpret_cast<uint32_t &> (comp->first)
              : fallback_value<uint32_t> ();
}

uint32_t &
HierarchyProxy::next ()
{
  return comp ? reinterpret_cast<uint32_t &> (comp->next)
              : fallback_value<uint32_t> ();
}

bool
WorldTransformProxy::valid () const
{
  return comp != nullptr;
}

void
WorldTransformProxy::bind (comp::world_transform *component)
{
  (void)component;
  comp.bind (active_registry (), entity);
}

::das::float4x4 &
WorldTransformProxy::matrix ()
{
  return comp ? reinterpret_cast<::das::float4x4 &> (comp->value ())
              : fallback_value<::das::float4x4> ();
}

bool
RigidBodyProxy::valid () const
{
  return comp != nullptr;
}

void
RigidBodyProxy::bind (comp::rigid_body *component)
{
  (void)component;
  comp.bind (active_registry (), entity);
}

int32_t &
RigidBodyProxy::shape ()
{
  return comp ? reinterpret_cast<int32_t &> (comp->shape)
              : fallback_value<int32_t> ();
}

::das::float3 &
RigidBodyProxy::position ()
{
  return comp ? reinterpret_cast<::das::float3 &> (comp->position)
              : fallback_value<::das::float3> ();
}

::das::float4 &
RigidBodyProxy::rotation ()
{
  return comp ? reinterpret_cast<::das::float4 &> (comp->rotation)
              : fallback_value<::das::float4> ();
}

::das::float3 &
RigidBodyProxy::half_extents ()
{
  return comp ? reinterpret_cast<::das::float3 &> (comp->half_extents)
              : fallback_value<::das::float3> ();
}

float &
RigidBodyProxy::radius ()
{
  return comp ? comp->radius : fallback_value<float> ();
}

float &
RigidBodyProxy::density ()
{
  return comp ? comp->density : fallback_value<float> ();
}

bool &
RigidBodyProxy::dynamic ()
{
  return comp ? comp->dynamic : fallback_value<bool> ();
}

float
RigidBodyProxy::get_mass () const
{
  return comp ? comp->mass () : 0.0F;
}

float &
RigidBodyProxy::friction ()
{
  return comp ? comp->friction : fallback_value<float> ();
}

float &
RigidBodyProxy::restitution ()
{
  return comp ? comp->restitution : fallback_value<float> ();
}

bool
CharacterBodyProxy::valid () const
{
  return comp != nullptr;
}

void
CharacterBodyProxy::bind (comp::character_body *component)
{
  (void)component;
  comp.bind (active_registry (), entity);
}

float &
CharacterBodyProxy::height ()
{
  return comp ? comp->height : fallback_value<float> ();
}

float &
CharacterBodyProxy::radius ()
{
  return comp ? comp->radius : fallback_value<float> ();
}

::das::float3 &
CharacterBodyProxy::desired_velocity ()
{
  return comp ? reinterpret_cast<::das::float3 &> (comp->desired_velocity)
              : fallback_value<::das::float3> ();
}

bool
ModelInstance3DProxy::valid () const
{
  return comp != nullptr;
}

void
ModelInstance3DProxy::bind (comp::model_instance_3d *component)
{
  (void)component;
  comp.bind (active_registry (), entity);
}

uint32_t &
ModelInstance3DProxy::model_id ()
{
  return comp ? comp->id.value : fallback_value<uint32_t> ();
}

uint32_t &
ModelInstance3DProxy::scene_index ()
{
  return comp ? comp->scene_index : fallback_value<uint32_t> ();
}

uint32_t &
ModelInstance3DProxy::material_override ()
{
  return comp ? comp->material_override.value : fallback_value<uint32_t> ();
}

float &
ModelInstance3DProxy::mip_lod_bias ()
{
  return comp ? comp->mip_lod_bias : fallback_value<float> ();
}

float &
ModelInstance3DProxy::geometry_lod_bias ()
{
  return comp ? comp->geometry_lod_bias : fallback_value<float> ();
}

float &
ModelInstance3DProxy::visibility_range ()
{
  return comp ? comp->visibility_range : fallback_value<float> ();
}

bool
Area3DProxy::valid () const
{
  return comp != nullptr;
}

void
Area3DProxy::bind (comp::area *component)
{
  (void)component;
  comp.bind (active_registry (), entity);
}

int32_t &
Area3DProxy::shape ()
{
  return comp ? reinterpret_cast<int32_t &> (comp->shape)
              : fallback_value<int32_t> ();
}

::das::float3 &
Area3DProxy::position ()
{
  return comp ? reinterpret_cast<::das::float3 &> (comp->position)
              : fallback_value<::das::float3> ();
}

::das::float4 &
Area3DProxy::rotation ()
{
  return comp ? reinterpret_cast<::das::float4 &> (comp->rotation)
              : fallback_value<::das::float4> ();
}

::das::float3 &
Area3DProxy::half_extents ()
{
  return comp ? reinterpret_cast<::das::float3 &> (comp->half_extents)
              : fallback_value<::das::float3> ();
}

float &
Area3DProxy::radius ()
{
  return comp ? comp->radius : fallback_value<float> ();
}

bool
AudioProxy::valid () const
{
  return comp != nullptr;
}

void
AudioProxy::bind (comp::audio *component)
{
  (void)component;
  comp.bind (active_registry (), entity);
}

uint32_t &
AudioProxy::audio_resource ()
{
  return comp ? comp->audio_resource.value : fallback_value<uint32_t> ();
}

bool &
AudioProxy::loop ()
{
  return comp ? comp->loop : fallback_value<bool> ();
}

bool &
AudioProxy::play_on_start ()
{
  return comp ? comp->play_on_start : fallback_value<bool> ();
}

float &
AudioProxy::volume ()
{
  return comp ? comp->volume : fallback_value<float> ();
}

bool &
AudioProxy::playing ()
{
  return comp ? comp->playing : fallback_value<bool> ();
}

bool
PrefabInstanceProxy::valid () const
{
  return comp != nullptr;
}

void
PrefabInstanceProxy::bind (comp::prefab_instance *component)
{
  (void)component;
  comp.bind (active_registry (), entity);
}

uint32_t &
PrefabInstanceProxy::prefab_id ()
{
  return comp ? comp->prefab_id.value : fallback_value<uint32_t> ();
}

uint32_t &
PrefabInstanceProxy::prefab_entity ()
{
  return comp ? reinterpret_cast<uint32_t &> (comp->prefab_entity)
              : fallback_value<uint32_t> ();
}

bool
SubviewportProxy::valid () const
{
  return comp != nullptr;
}

void
SubviewportProxy::bind (comp::subviewport *component)
{
  (void)component;
  comp.bind (active_registry (), entity);
}

::das::float2 &
SubviewportProxy::world_quad_size ()
{
  return comp ? reinterpret_cast<::das::float2 &> (comp->world_quad_size)
              : fallback_value<::das::float2> ();
}

::das::float2 &
SubviewportProxy::size ()
{
  return comp ? reinterpret_cast<::das::float2 &> (comp->size)
              : fallback_value<::das::float2> ();
}

::das::float2 &
SubviewportProxy::container_size ()
{
  return comp ? reinterpret_cast<::das::float2 &> (comp->container_size)
              : fallback_value<::das::float2> ();
}

::das::float2 &
SubviewportProxy::virtual_size ()
{
  return comp ? reinterpret_cast<::das::float2 &> (comp->virtual_size)
              : fallback_value<::das::float2> ();
}

uint32_t &
SubviewportProxy::camera ()
{
  return comp ? reinterpret_cast<uint32_t &> (comp->camera.value)
              : fallback_value<uint32_t> ();
}

int &
SubviewportProxy::display_order ()
{
  return comp ? comp->display_order : fallback_value<int> ();
}

bool &
SubviewportProxy::input_routing ()
{
  return comp ? comp->input_routing : fallback_value<bool> ();
}

bool &
SubviewportProxy::hdr ()
{
  return comp ? comp->hdr : fallback_value<bool> ();
}

namespace
{

struct TransformProxyAnnotation
    : ::das::ManagedStructureAnnotation<TransformProxy, false, false>
{
  explicit TransformProxyAnnotation (::das::ModuleLibrary &lib)
      : ::das::ManagedStructureAnnotation<TransformProxy, false, false> (
            "Transform", lib, "wsl::das::TransformProxy")
  {
    addProperty<DAS_BIND_MANAGED_PROP (position)> ("position", "position");
    addProperty<DAS_BIND_MANAGED_PROP (rotation)> ("rotation", "rotation");
    addProperty<DAS_BIND_MANAGED_PROP (scale)> ("scale", "scale");
  }
};

struct Transform2DProxyAnnotation
    : ::das::ManagedStructureAnnotation<Transform2DProxy, false, false>
{
  explicit Transform2DProxyAnnotation (::das::ModuleLibrary &lib)
      : ::das::ManagedStructureAnnotation<Transform2DProxy, false, false> (
            "Transform2D", lib, "wsl::das::Transform2DProxy")
  {
    addProperty<DAS_BIND_MANAGED_PROP (position)> ("position", "position");
    addProperty<DAS_BIND_MANAGED_PROP (scale)> ("scale", "scale");
    addProperty<DAS_BIND_MANAGED_PROP (rotation)> ("rotation", "rotation");
  }
};

struct Camera2DProxyAnnotation
    : ::das::ManagedStructureAnnotation<Camera2DProxy, false, false>
{
  explicit Camera2DProxyAnnotation (::das::ModuleLibrary &lib)
      : ::das::ManagedStructureAnnotation<Camera2DProxy, false, false> (
            "Camera2D", lib, "wsl::das::Camera2DProxy")
  {
    addProperty<DAS_BIND_MANAGED_PROP (zoom)> ("zoom", "zoom");
  }
};

struct Sprite2DProxyAnnotation
    : ::das::ManagedStructureAnnotation<Sprite2DProxy, false, false>
{
  explicit Sprite2DProxyAnnotation (::das::ModuleLibrary &lib)
      : ::das::ManagedStructureAnnotation<Sprite2DProxy, false, false> (
            "Sprite2D", lib, "wsl::das::Sprite2DProxy")
  {
    addProperty<DAS_BIND_MANAGED_PROP (color)> ("color", "color");
    addProperty<DAS_BIND_MANAGED_PROP (size)> ("size", "size");
    addProperty<DAS_BIND_MANAGED_PROP (uv_offset)> ("uv_offset", "uv_offset");
    addProperty<DAS_BIND_MANAGED_PROP (uv_scale)> ("uv_scale", "uv_scale");
    addProperty<DAS_BIND_MANAGED_PROP (flip_v)> ("flip_v", "flip_v");
    addProperty<DAS_BIND_MANAGED_PROP (flip_h)> ("flip_h", "flip_h");
    addProperty<DAS_BIND_MANAGED_PROP (z_index)> ("z_index", "z_index");
  }
};

struct PointLightProxyAnnotation
    : ::das::ManagedStructureAnnotation<PointLightProxy, false, false>
{
  explicit PointLightProxyAnnotation (::das::ModuleLibrary &lib)
      : ::das::ManagedStructureAnnotation<PointLightProxy, false, false> (
            "PointLight", lib, "wsl::das::PointLightProxy")
  {
    addProperty<DAS_BIND_MANAGED_PROP (color)> ("color", "color");
    addProperty<DAS_BIND_MANAGED_PROP (intensity)> ("intensity", "intensity");
    addProperty<DAS_BIND_MANAGED_PROP (radius)> ("radius", "radius");
  }
};

struct DirectionalLightProxyAnnotation
    : ::das::ManagedStructureAnnotation<DirectionalLightProxy, false, false>
{
  explicit DirectionalLightProxyAnnotation (::das::ModuleLibrary &lib)
      : ::das::ManagedStructureAnnotation<DirectionalLightProxy, false, false> (
            "DirectionalLight", lib, "wsl::das::DirectionalLightProxy")
  {
    addProperty<DAS_BIND_MANAGED_PROP (color)> ("color", "color");
    addProperty<DAS_BIND_MANAGED_PROP (intensity)> ("intensity", "intensity");
  }
};

struct SpotLightProxyAnnotation
    : ::das::ManagedStructureAnnotation<SpotLightProxy, false, false>
{
  explicit SpotLightProxyAnnotation (::das::ModuleLibrary &lib)
      : ::das::ManagedStructureAnnotation<SpotLightProxy, false, false> (
            "SpotLight", lib, "wsl::das::SpotLightProxy")
  {
    addProperty<DAS_BIND_MANAGED_PROP (color)> ("color", "color");
    addProperty<DAS_BIND_MANAGED_PROP (intensity)> ("intensity", "intensity");
    // `range` is a reserved daslang keyword, so the cone range is exposed
    // under a prefixed name.
    addProperty<DAS_BIND_MANAGED_PROP (range)> ("spot_range", "spot_range");
    addProperty<DAS_BIND_MANAGED_PROP (inner_cos)> ("inner_cos", "inner_cos");
    addProperty<DAS_BIND_MANAGED_PROP (outer_cos)> ("outer_cos", "outer_cos");
  }
};

struct HierarchyProxyAnnotation
    : ::das::ManagedStructureAnnotation<HierarchyProxy, false, false>
{
  explicit HierarchyProxyAnnotation (::das::ModuleLibrary &lib)
      : ::das::ManagedStructureAnnotation<HierarchyProxy, false, false> (
            "Hierarchy", lib, "wsl::das::HierarchyProxy")
  {
    addProperty<DAS_BIND_MANAGED_PROP (parent)> ("parent", "parent");
    addProperty<DAS_BIND_MANAGED_PROP (first)> ("first", "first");
    addProperty<DAS_BIND_MANAGED_PROP (next)> ("next", "next");
  }
};

struct WorldTransformProxyAnnotation
    : ::das::ManagedStructureAnnotation<WorldTransformProxy, false, false>
{
  explicit WorldTransformProxyAnnotation (::das::ModuleLibrary &lib)
      : ::das::ManagedStructureAnnotation<WorldTransformProxy, false, false> (
            "WorldTransform", lib, "wsl::das::WorldTransformProxy")
  {
    addProperty<DAS_BIND_MANAGED_PROP (matrix)> ("matrix", "matrix");
  }
};

struct RigidBodyProxyAnnotation
    : ::das::ManagedStructureAnnotation<RigidBodyProxy, false, false>
{
  explicit RigidBodyProxyAnnotation (::das::ModuleLibrary &lib)
      : ::das::ManagedStructureAnnotation<RigidBodyProxy, false, false> (
            "RigidBody", lib, "wsl::das::RigidBodyProxy")
  {
    addProperty<DAS_BIND_MANAGED_PROP (shape)> ("shape", "shape");
    addProperty<DAS_BIND_MANAGED_PROP (position)> ("position", "position");
    addProperty<DAS_BIND_MANAGED_PROP (rotation)> ("rotation", "rotation");
    addProperty<DAS_BIND_MANAGED_PROP (half_extents)> ("half_extents",
                                                       "half_extents");
    addProperty<DAS_BIND_MANAGED_PROP (radius)> ("radius", "radius");
    addProperty<DAS_BIND_MANAGED_PROP (density)> ("density", "density");
    addProperty<DAS_BIND_MANAGED_PROP (dynamic)> ("dynamic", "dynamic");
    addProperty<DAS_BIND_MANAGED_PROP (friction)> ("friction", "friction");
    addProperty<DAS_BIND_MANAGED_PROP (restitution)> ("restitution",
                                                      "restitution");
  }
};

struct CharacterBodyProxyAnnotation
    : ::das::ManagedStructureAnnotation<CharacterBodyProxy, false, false>
{
  explicit CharacterBodyProxyAnnotation (::das::ModuleLibrary &lib)
      : ::das::ManagedStructureAnnotation<CharacterBodyProxy, false, false> (
            "CharacterBody", lib, "wsl::das::CharacterBodyProxy")
  {
    addProperty<DAS_BIND_MANAGED_PROP (height)> ("height", "height");
    addProperty<DAS_BIND_MANAGED_PROP (radius)> ("radius", "radius");
    addProperty<DAS_BIND_MANAGED_PROP (desired_velocity)> ("desired_velocity",
                                                           "desired_velocity");
  }
};

struct ModelInstance3DProxyAnnotation
    : ::das::ManagedStructureAnnotation<ModelInstance3DProxy, false, false>
{
  explicit ModelInstance3DProxyAnnotation (::das::ModuleLibrary &lib)
      : ::das::ManagedStructureAnnotation<ModelInstance3DProxy, false, false> (
            "ModelInstance3D", lib, "wsl::das::ModelInstance3DProxy")
  {
    addProperty<DAS_BIND_MANAGED_PROP (model_id)> ("model_id", "model_id");
    addProperty<DAS_BIND_MANAGED_PROP (scene_index)> ("scene_index",
                                                      "scene_index");
    addProperty<DAS_BIND_MANAGED_PROP (material_override)> (
        "material_override", "material_override");
    addProperty<DAS_BIND_MANAGED_PROP (mip_lod_bias)> ("mip_lod_bias",
                                                       "mip_lod_bias");
    addProperty<DAS_BIND_MANAGED_PROP (geometry_lod_bias)> (
        "geometry_lod_bias", "geometry_lod_bias");
    addProperty<DAS_BIND_MANAGED_PROP (visibility_range)> ("visibility_range",
                                                           "visibility_range");
  }
};

struct Area3DProxyAnnotation
    : ::das::ManagedStructureAnnotation<Area3DProxy, false, false>
{
  explicit Area3DProxyAnnotation (::das::ModuleLibrary &lib)
      : ::das::ManagedStructureAnnotation<Area3DProxy, false, false> (
            "Area3D", lib, "wsl::das::Area3DProxy")
  {
    addProperty<DAS_BIND_MANAGED_PROP (shape)> ("shape", "shape");
    addProperty<DAS_BIND_MANAGED_PROP (position)> ("position", "position");
    addProperty<DAS_BIND_MANAGED_PROP (rotation)> ("rotation", "rotation");
    addProperty<DAS_BIND_MANAGED_PROP (half_extents)> ("half_extents",
                                                       "half_extents");
    addProperty<DAS_BIND_MANAGED_PROP (radius)> ("radius", "radius");
  }
};

struct AudioProxyAnnotation
    : ::das::ManagedStructureAnnotation<AudioProxy, false, false>
{
  explicit AudioProxyAnnotation (::das::ModuleLibrary &lib)
      : ::das::ManagedStructureAnnotation<AudioProxy, false, false> (
            "Audio", lib, "wsl::das::AudioProxy")
  {
    addProperty<DAS_BIND_MANAGED_PROP (audio_resource)> ("audio_resource",
                                                         "audio_resource");
    addProperty<DAS_BIND_MANAGED_PROP (loop)> ("loop", "loop");
    addProperty<DAS_BIND_MANAGED_PROP (play_on_start)> ("play_on_start",
                                                        "play_on_start");
    addProperty<DAS_BIND_MANAGED_PROP (volume)> ("volume", "volume");
    addProperty<DAS_BIND_MANAGED_PROP (playing)> ("playing", "playing");
  }
};

struct PrefabInstanceProxyAnnotation
    : ::das::ManagedStructureAnnotation<PrefabInstanceProxy, false, false>
{
  explicit PrefabInstanceProxyAnnotation (::das::ModuleLibrary &lib)
      : ::das::ManagedStructureAnnotation<PrefabInstanceProxy, false, false> (
            "PrefabInstance", lib, "wsl::das::PrefabInstanceProxy")
  {
    addProperty<DAS_BIND_MANAGED_PROP (prefab_id)> ("prefab_id", "prefab_id");
    addProperty<DAS_BIND_MANAGED_PROP (prefab_entity)> ("prefab_entity",
                                                        "prefab_entity");
  }
};

struct SubviewportProxyAnnotation
    : ::das::ManagedStructureAnnotation<SubviewportProxy, false, false>
{
  explicit SubviewportProxyAnnotation (::das::ModuleLibrary &lib)
      : ::das::ManagedStructureAnnotation<SubviewportProxy, false, false> (
            "Subviewport", lib, "wsl::das::SubviewportProxy")
  {
    addProperty<DAS_BIND_MANAGED_PROP (size)> ("size", "size");
    addProperty<DAS_BIND_MANAGED_PROP (world_quad_size)> ("world_quad_size",
                                                          "world_quad_size");
    addProperty<DAS_BIND_MANAGED_PROP (container_size)> ("container_size",
                                                         "container_size");
    addProperty<DAS_BIND_MANAGED_PROP (virtual_size)> ("virtual_size",
                                                       "virtual_size");
    addProperty<DAS_BIND_MANAGED_PROP (camera)> ("camera", "camera");
    addProperty<DAS_BIND_MANAGED_PROP (display_order)> ("display_order",
                                                        "display_order");
    addProperty<DAS_BIND_MANAGED_PROP (input_routing)> ("input_routing",
                                                        "input_routing");
    addProperty<DAS_BIND_MANAGED_PROP (hdr)> ("hdr", "hdr");
  }
};

} // anonymous namespace

void
register_component_accessors (::das::Module &mod, ::das::ModuleLibrary &lib)
{
  mod.addAnnotation (new TransformProxyAnnotation (lib));
  mod.addAnnotation (new Transform2DProxyAnnotation (lib));
  mod.addAnnotation (new Camera2DProxyAnnotation (lib));
  mod.addAnnotation (new Sprite2DProxyAnnotation (lib));
  mod.addAnnotation (new PointLightProxyAnnotation (lib));
  mod.addAnnotation (new DirectionalLightProxyAnnotation (lib));
  mod.addAnnotation (new SpotLightProxyAnnotation (lib));
  mod.addAnnotation (new HierarchyProxyAnnotation (lib));
  mod.addAnnotation (new WorldTransformProxyAnnotation (lib));
  mod.addAnnotation (new RigidBodyProxyAnnotation (lib));
  mod.addAnnotation (new CharacterBodyProxyAnnotation (lib));
  mod.addAnnotation (new ModelInstance3DProxyAnnotation (lib));
  mod.addAnnotation (new Area3DProxyAnnotation (lib));
  mod.addAnnotation (new AudioProxyAnnotation (lib));
  mod.addAnnotation (new PrefabInstanceProxyAnnotation (lib));
  mod.addAnnotation (new SubviewportProxyAnnotation (lib));

  addExtern<DAS_BIND_FUN (get_transform_accessor),
            ::das::SimNode_ExtFuncCallAndCopyOrMove> (
      mod, lib, "get_transform_accessor", ::das::SideEffects::accessExternal,
      "wsl::das::get_transform_accessor")
      ->args ({ "entity", "at" });

  addExtern<DAS_BIND_FUN (get_transform_2d_accessor),
            ::das::SimNode_ExtFuncCallAndCopyOrMove> (
      mod, lib, "get_transform_2d_accessor", ::das::SideEffects::accessExternal,
      "wsl::das::get_transform_2d_accessor")
      ->args ({ "entity", "at" });

  addExtern<DAS_BIND_FUN (get_camera_2d_accessor),
            ::das::SimNode_ExtFuncCallAndCopyOrMove> (
      mod, lib, "get_camera_2d_accessor", ::das::SideEffects::accessExternal,
      "wsl::das::get_camera_2d_accessor")
      ->args ({ "entity", "at" });

  addExtern<DAS_BIND_FUN (get_sprite_2d_accessor),
            ::das::SimNode_ExtFuncCallAndCopyOrMove> (
      mod, lib, "get_sprite_2d_accessor", ::das::SideEffects::accessExternal,
      "wsl::das::get_sprite_2d_accessor")
      ->args ({ "entity", "at" });

  addExtern<DAS_BIND_FUN (get_point_light_accessor),
            ::das::SimNode_ExtFuncCallAndCopyOrMove> (
      mod, lib, "get_point_light_accessor", ::das::SideEffects::accessExternal,
      "wsl::das::get_point_light_accessor")
      ->args ({ "entity", "at" });

  addExtern<DAS_BIND_FUN (get_directional_light_accessor),
            ::das::SimNode_ExtFuncCallAndCopyOrMove> (
      mod, lib, "get_directional_light_accessor",
      ::das::SideEffects::accessExternal,
      "wsl::das::get_directional_light_accessor")
      ->args ({ "entity", "at" });

  addExtern<DAS_BIND_FUN (get_spot_light_accessor),
            ::das::SimNode_ExtFuncCallAndCopyOrMove> (
      mod, lib, "get_spot_light_accessor", ::das::SideEffects::accessExternal,
      "wsl::das::get_spot_light_accessor")
      ->args ({ "entity", "at" });

  addExtern<DAS_BIND_FUN (get_hierarchy_accessor),
            ::das::SimNode_ExtFuncCallAndCopyOrMove> (
      mod, lib, "get_hierarchy_accessor", ::das::SideEffects::accessExternal,
      "wsl::das::get_hierarchy_accessor")
      ->args ({ "entity", "at" });

  addExtern<DAS_BIND_FUN (get_world_transform_accessor),
            ::das::SimNode_ExtFuncCallAndCopyOrMove> (
      mod, lib, "get_world_transform_accessor",
      ::das::SideEffects::accessExternal,
      "wsl::das::get_world_transform_accessor")
      ->args ({ "entity", "at" });

  addExtern<DAS_BIND_FUN (get_rigid_body_accessor),
            ::das::SimNode_ExtFuncCallAndCopyOrMove> (
      mod, lib, "get_rigid_body_accessor", ::das::SideEffects::accessExternal,
      "wsl::das::get_rigid_body_accessor")
      ->args ({ "entity", "at" });

  addExtern<DAS_BIND_FUN (get_rigid_body_mass)> (
      mod, lib, "get_rigid_body_mass", ::das::SideEffects::accessExternal,
      "wsl::das::get_rigid_body_mass")
      ->args ({ "proxy", "at" });

  addExtern<DAS_BIND_FUN (get_character_body_accessor),
            ::das::SimNode_ExtFuncCallAndCopyOrMove> (
      mod, lib, "get_character_body_accessor",
      ::das::SideEffects::accessExternal,
      "wsl::das::get_character_body_accessor")
      ->args ({ "entity", "at" });

  addExtern<DAS_BIND_FUN (get_model_instance_3d_accessor),
            ::das::SimNode_ExtFuncCallAndCopyOrMove> (
      mod, lib, "get_model_instance_3d_accessor",
      ::das::SideEffects::accessExternal,
      "wsl::das::get_model_instance_3d_accessor")
      ->args ({ "entity", "at" });

  addExtern<DAS_BIND_FUN (get_area_3d_accessor),
            ::das::SimNode_ExtFuncCallAndCopyOrMove> (
      mod, lib, "get_area_3d_accessor", ::das::SideEffects::accessExternal,
      "wsl::das::get_area_3d_accessor")
      ->args ({ "entity", "at" });

  addExtern<DAS_BIND_FUN (get_audio_accessor),
            ::das::SimNode_ExtFuncCallAndCopyOrMove> (
      mod, lib, "get_audio_accessor", ::das::SideEffects::accessExternal,
      "wsl::das::get_audio_accessor")
      ->args ({ "entity", "at" });

  addExtern<DAS_BIND_FUN (get_prefab_instance_accessor),
            ::das::SimNode_ExtFuncCallAndCopyOrMove> (
      mod, lib, "get_prefab_instance_accessor",
      ::das::SideEffects::accessExternal,
      "wsl::das::get_prefab_instance_accessor")
      ->args ({ "entity", "at" });

  addExtern<DAS_BIND_FUN (get_subviewport_accessor),
            ::das::SimNode_ExtFuncCallAndCopyOrMove> (
      mod, lib, "get_subviewport_accessor", ::das::SideEffects::accessExternal,
      "wsl::das::get_subviewport_accessor")
      ->args ({ "entity", "at" });
}

TransformProxy
get_transform_accessor (uint32_t entity, ::das::LineInfoArg *at)
{
  (void)at;
  TransformProxy proxy{};
  proxy.entity = static_cast<entt::entity> (entity);
  auto *reg = get_registry ();
  proxy.bind (reg ? reg->try_get<comp::transform> (proxy.entity) : nullptr);
  return proxy;
}

Transform2DProxy
get_transform_2d_accessor (uint32_t entity, ::das::LineInfoArg *at)
{
  (void)at;
  Transform2DProxy proxy{};
  proxy.entity = static_cast<entt::entity> (entity);
  auto *reg = get_registry ();
  proxy.bind (reg ? reg->try_get<comp::transform_2d> (proxy.entity) : nullptr);
  return proxy;
}

Camera2DProxy
get_camera_2d_accessor (uint32_t entity, ::das::LineInfoArg *at)
{
  (void)at;
  Camera2DProxy proxy{};
  proxy.entity = static_cast<entt::entity> (entity);
  auto *reg = get_registry ();
  proxy.bind (reg ? reg->try_get<comp::camera_2d> (proxy.entity) : nullptr);
  return proxy;
}

Sprite2DProxy
get_sprite_2d_accessor (uint32_t entity, ::das::LineInfoArg *at)
{
  (void)at;
  Sprite2DProxy proxy{};
  proxy.entity = static_cast<entt::entity> (entity);
  auto *reg = get_registry ();
  proxy.bind (reg ? reg->try_get<comp::sprite_2d> (proxy.entity) : nullptr);
  return proxy;
}

PointLightProxy
get_point_light_accessor (uint32_t entity, ::das::LineInfoArg *at)
{
  (void)at;
  PointLightProxy proxy{};
  proxy.entity = static_cast<entt::entity> (entity);
  auto *reg = get_registry ();
  proxy.bind (reg ? reg->try_get<comp::point_light> (proxy.entity) : nullptr);
  return proxy;
}

DirectionalLightProxy
get_directional_light_accessor (uint32_t entity, ::das::LineInfoArg *at)
{
  (void)at;
  DirectionalLightProxy proxy{};
  proxy.entity = static_cast<entt::entity> (entity);
  auto *reg = get_registry ();
  proxy.bind (reg ? reg->try_get<comp::directional_light> (proxy.entity)
                  : nullptr);
  return proxy;
}

SpotLightProxy
get_spot_light_accessor (uint32_t entity, ::das::LineInfoArg *at)
{
  (void)at;
  SpotLightProxy proxy{};
  proxy.entity = static_cast<entt::entity> (entity);
  auto *reg = get_registry ();
  proxy.bind (reg ? reg->try_get<comp::spot_light> (proxy.entity) : nullptr);
  return proxy;
}

HierarchyProxy
get_hierarchy_accessor (uint32_t entity, ::das::LineInfoArg *at)
{
  (void)at;
  HierarchyProxy proxy{};
  proxy.entity = static_cast<entt::entity> (entity);
  auto *reg = get_registry ();
  proxy.bind (reg ? reg->try_get<comp::hierarchy> (proxy.entity) : nullptr);
  return proxy;
}

WorldTransformProxy
get_world_transform_accessor (uint32_t entity, ::das::LineInfoArg *at)
{
  (void)at;
  WorldTransformProxy proxy{};
  proxy.entity = static_cast<entt::entity> (entity);
  auto *reg = get_registry ();
  proxy.bind (reg ? reg->try_get<comp::world_transform> (proxy.entity)
                  : nullptr);
  return proxy;
}

RigidBodyProxy
get_rigid_body_accessor (uint32_t entity, ::das::LineInfoArg *at)
{
  (void)at;
  RigidBodyProxy proxy{};
  proxy.entity = static_cast<entt::entity> (entity);
  auto *reg = get_registry ();
  proxy.bind (reg ? reg->try_get<comp::rigid_body> (proxy.entity) : nullptr);
  return proxy;
}

float
get_rigid_body_mass (RigidBodyProxy &proxy, ::das::LineInfoArg *at)
{
  (void)at;
  return proxy.get_mass ();
}

CharacterBodyProxy
get_character_body_accessor (uint32_t entity, ::das::LineInfoArg *at)
{
  (void)at;
  CharacterBodyProxy proxy{};
  proxy.entity = static_cast<entt::entity> (entity);
  auto *reg = get_registry ();
  proxy.bind (reg ? reg->try_get<comp::character_body> (proxy.entity)
                  : nullptr);
  return proxy;
}

ModelInstance3DProxy
get_model_instance_3d_accessor (uint32_t entity, ::das::LineInfoArg *at)
{
  (void)at;
  ModelInstance3DProxy proxy{};
  proxy.entity = static_cast<entt::entity> (entity);
  auto *reg = get_registry ();
  proxy.bind (reg ? reg->try_get<comp::model_instance_3d> (proxy.entity)
                  : nullptr);
  return proxy;
}

Area3DProxy
get_area_3d_accessor (uint32_t entity, ::das::LineInfoArg *at)
{
  (void)at;
  Area3DProxy proxy{};
  proxy.entity = static_cast<entt::entity> (entity);
  auto *reg = get_registry ();
  proxy.bind (reg ? reg->try_get<comp::area> (proxy.entity) : nullptr);
  return proxy;
}

AudioProxy
get_audio_accessor (uint32_t entity, ::das::LineInfoArg *at)
{
  (void)at;
  AudioProxy proxy{};
  proxy.entity = static_cast<entt::entity> (entity);
  auto *reg = get_registry ();
  proxy.bind (reg ? reg->try_get<comp::audio> (proxy.entity) : nullptr);
  return proxy;
}

PrefabInstanceProxy
get_prefab_instance_accessor (uint32_t entity, ::das::LineInfoArg *at)
{
  (void)at;
  PrefabInstanceProxy proxy{};
  proxy.entity = static_cast<entt::entity> (entity);
  auto *reg = get_registry ();
  proxy.bind (reg ? reg->try_get<comp::prefab_instance> (proxy.entity)
                  : nullptr);
  return proxy;
}

SubviewportProxy
get_subviewport_accessor (uint32_t entity, ::das::LineInfoArg *at)
{
  (void)at;
  SubviewportProxy proxy{};
  proxy.entity = static_cast<entt::entity> (entity);
  auto *reg = get_registry ();
  proxy.bind (reg ? reg->try_get<comp::subviewport> (proxy.entity) : nullptr);
  return proxy;
}

namespace
{

// ── Entity operations ──

uint32_t
wsl_entity_create ()
{
  auto *reg = get_registry ();
  if (!reg) {
    return NULL_ENTITY_ID;
  }
  return static_cast<uint32_t> (reg->create ());
}

void
wsl_entity_destroy (uint32_t entity, ::das::Context *context,
                    ::das::LineInfoArg *at)
{
  if (g_query_depth > 0) {
    context->throw_error_at (
        at, "cannot entity_destroy during a query; structural mutations are "
            "not allowed while component references are live");
  }
  auto *reg = get_registry ();
  if (!reg) {
    return;
  }
  auto e = static_cast<entt::entity> (entity);
  if (reg->valid (e)) {
    if (reg->ctx ().contains<rsc::scene *> ()) {
      reg->ctx ().get<rsc::scene *> ()->destroy_entity_subtree (e);
    } else {
      reg->destroy (e);
    }
  }
}

bool
wsl_entity_valid (uint32_t entity)
{
  auto *reg = get_registry ();
  if (!reg) {
    return false;
  }
  return reg->valid (static_cast<entt::entity> (entity));
}

bool
wsl_entity_is_null (uint32_t entity)
{
  return entity == NULL_ENTITY_ID;
}

uint32_t
wsl_null_entity ()
{
  return NULL_ENTITY_ID;
}

// ── Camera field access ──

float
wsl_get_camera_fov (uint32_t camera)
{
  auto *reg = get_registry ();
  if (!reg) {
    return 60.0f;
  }
  auto e = static_cast<entt::entity> (camera);
  if (!reg->valid (e) || !reg->all_of<comp::camera> (e)) {
    return 60.0f;
  }
  return reg->get<comp::camera> (e).fov ();
}

void
wsl_set_camera_fov (uint32_t camera, float fov)
{
  auto *reg = get_registry ();
  if (!reg) {
    return;
  }
  auto e = static_cast<entt::entity> (camera);
  if (!reg->valid (e) || !reg->all_of<comp::camera> (e)) {
    return;
  }
  reg->get<comp::camera> (e).fov () = fov;
}

float
wsl_get_camera_near (uint32_t camera)
{
  auto *reg = get_registry ();
  if (!reg) {
    return 0.5f;
  }
  auto e = static_cast<entt::entity> (camera);
  if (!reg->valid (e) || !reg->all_of<comp::camera> (e)) {
    return 0.5f;
  }
  return reg->get<comp::camera> (e).near ();
}

void
wsl_set_camera_near (uint32_t camera, float near_val)
{
  auto *reg = get_registry ();
  if (!reg) {
    return;
  }
  auto e = static_cast<entt::entity> (camera);
  if (!reg->valid (e) || !reg->all_of<comp::camera> (e)) {
    return;
  }
  reg->get<comp::camera> (e).near () = near_val;
}

float
wsl_get_camera_far (uint32_t camera)
{
  auto *reg = get_registry ();
  if (!reg) {
    return 50.0f;
  }
  auto e = static_cast<entt::entity> (camera);
  if (!reg->valid (e) || !reg->all_of<comp::camera> (e)) {
    return 50.0f;
  }
  return reg->get<comp::camera> (e).far ();
}

void
wsl_set_camera_far (uint32_t camera, float far_val)
{
  auto *reg = get_registry ();
  if (!reg) {
    return;
  }
  auto e = static_cast<entt::entity> (camera);
  if (!reg->valid (e) || !reg->all_of<comp::camera> (e)) {
    return;
  }
  reg->get<comp::camera> (e).far () = far_val;
}

float
wsl_get_camera_aspect_ratio (uint32_t camera)
{
  auto *reg = get_registry ();
  if (!reg) {
    return 1.0f;
  }
  auto e = static_cast<entt::entity> (camera);
  if (!reg->valid (e) || !reg->all_of<comp::camera> (e)) {
    return 1.0f;
  }
  return reg->get<comp::camera> (e).aspect_ratio ();
}

void
wsl_set_camera_aspect_ratio (uint32_t camera, float aspect)
{
  auto *reg = get_registry ();
  if (!reg) {
    return;
  }
  auto e = static_cast<entt::entity> (camera);
  if (!reg->valid (e) || !reg->all_of<comp::camera> (e)) {
    return;
  }
  reg->get<comp::camera> (e).aspect_ratio () = aspect;
}

// ── Transform operations (per-component getters) ──

// ── Scene operations ──

uint32_t
wsl_find_entity_by_name (const char *name)
{
  auto *reg = get_registry ();
  if (!reg || !name) {
    return NULL_ENTITY_ID;
  }

  if (!reg->ctx ().contains<comp::singl::runtime_context *> ()) {
    return NULL_ENTITY_ID;
  }
  auto *runtime_ctx = reg->ctx ().get<comp::singl::runtime_context *> ();
  if (!runtime_ctx) {
    return NULL_ENTITY_ID;
  }

  auto *scene = runtime_ctx->scene_manager ().get_active ();
  if (!scene) {
    return NULL_ENTITY_ID;
  }

  for (const auto &[entity, entity_name] : scene->get_entity_names ()) {
    if (entity_name == name) {
      return static_cast<uint32_t> (entity);
    }
  }
  return NULL_ENTITY_ID;
}

void
wsl_set_entity_name (uint32_t entity, const char *name)
{
  auto *reg = get_registry ();
  if (!reg || !name) {
    return;
  }

  if (!reg->ctx ().contains<comp::singl::runtime_context *> ()) {
    return;
  }
  auto *runtime_ctx = reg->ctx ().get<comp::singl::runtime_context *> ();
  if (!runtime_ctx) {
    return;
  }

  auto *scene = runtime_ctx->scene_manager ().get_active ();
  if (!scene) {
    return;
  }

  auto e = static_cast<entt::entity> (entity);
  if (reg->valid (e)) {
    scene->set_entity_name (e, name);
  }
}

uint32_t
wsl_get_active_camera ()
{
  auto *reg = get_registry ();
  if (!reg) {
    return NULL_ENTITY_ID;
  }

  if (!reg->ctx ().contains<comp::singl::runtime_context *> ()) {
    return NULL_ENTITY_ID;
  }
  auto *runtime_ctx = reg->ctx ().get<comp::singl::runtime_context *> ();
  if (!runtime_ctx) {
    return NULL_ENTITY_ID;
  }

  auto *scene = runtime_ctx->scene_manager ().get_active ();
  if (!scene) {
    return NULL_ENTITY_ID;
  }

  return static_cast<uint32_t> (scene->camera);
}

void
wsl_set_active_camera (uint32_t entity)
{
  auto *reg = get_registry ();
  if (!reg) {
    return;
  }

  if (!reg->ctx ().contains<comp::singl::runtime_context *> ()) {
    return;
  }
  auto *runtime_ctx = reg->ctx ().get<comp::singl::runtime_context *> ();
  if (!runtime_ctx) {
    return;
  }

  auto *scene = runtime_ctx->scene_manager ().get_active ();
  if (!scene) {
    return;
  }

  scene->camera = static_cast<entt::entity> (entity);
}

uint32_t
wsl_instantiate_prefab (const char *path)
{
  if (g_query_depth > 0) {
    wsl::log::sys ()->error (
        "instantiate_prefab cannot run while a script query is active");
    return NULL_ENTITY_ID;
  }

  auto *reg = get_registry ();
  if (!reg || !path) {
    return NULL_ENTITY_ID;
  }

  if (!reg->ctx ().contains<comp::singl::runtime_context *> ()) {
    return NULL_ENTITY_ID;
  }
  auto *runtime_ctx = reg->ctx ().get<comp::singl::runtime_context *> ();
  if (!runtime_ctx) {
    return NULL_ENTITY_ID;
  }

  const entt::id_type prefab_hash = entt::hashed_string{ path };
  rsc::scene_id prefab_id{ prefab_hash };
  auto &resources = runtime_ctx->resource_manager ();
  const rsc::scene_state state = resources.state (prefab_id);
  if (state != rsc::scene_state::loaded) {
    if (state == rsc::scene_state::not_loaded) {
      static_cast<void> (resources.load (prefab_id));
      wsl::log::sys ()->warn (
          "Prefab '{}' is loading asynchronously; retry instantiate_prefab "
          "after it finishes",
          path);
    }
    return NULL_ENTITY_ID;
  }

  entt::entity e = resources.instantiate_prefab (prefab_id);
  return static_cast<uint32_t> (e);
}

// ── Editor viewport (global-state) ──

float g_editor_img_min_x = 0.0f;
float g_editor_img_min_y = 0.0f;
float g_editor_img_size_x = 0.0f;
float g_editor_img_size_y = 0.0f;

void
wsl_refresh_editor_viewport ()
{
  auto *reg = get_registry ();
  if (!reg) {
    return;
  }
  if (!reg->ctx ().contains<comp::singl::runtime_context *> ()) {
    return;
  }
  auto *runtime_ctx = reg->ctx ().get<comp::singl::runtime_context *> ();
  if (!runtime_ctx) {
    return;
  }
  auto *editor_ctx = runtime_ctx->editor_ctx ();
  if (!editor_ctx) {
    g_editor_img_min_x = 0.0f;
    g_editor_img_min_y = 0.0f;
    g_editor_img_size_x = 0.0f;
    g_editor_img_size_y = 0.0f;
    return;
  }
  auto const &mn = editor_ctx->last_img_min ();
  auto const &sz = editor_ctx->last_img_size ();
  g_editor_img_min_x = mn.x;
  g_editor_img_min_y = mn.y;
  g_editor_img_size_x = sz.x;
  g_editor_img_size_y = sz.y;
}

float
wsl_get_editor_img_min_x ()
{
  return g_editor_img_min_x;
}
float
wsl_get_editor_img_min_y ()
{
  return g_editor_img_min_y;
}
float
wsl_get_editor_img_size_x ()
{
  return g_editor_img_size_x;
}
float
wsl_get_editor_img_size_y ()
{
  return g_editor_img_size_y;
}

// ── Component type ID constants ──

uint32_t
wsl_type_id_transform ()
{
  return static_cast<uint32_t> (entt::type_hash<comp::transform>::value ());
}

uint32_t
wsl_type_id_camera ()
{
  return static_cast<uint32_t> (entt::type_hash<comp::camera>::value ());
}

uint32_t
wsl_type_id_hierarchy ()
{
  return static_cast<uint32_t> (entt::type_hash<comp::hierarchy>::value ());
}

uint32_t
wsl_type_id_world_transform ()
{
  return static_cast<uint32_t> (
      entt::type_hash<comp::world_transform>::value ());
}

uint32_t
wsl_type_id_transform_2d ()
{
  return static_cast<uint32_t> (entt::type_hash<comp::transform_2d>::value ());
}

uint32_t
wsl_type_id_camera_2d ()
{
  return static_cast<uint32_t> (entt::type_hash<comp::camera_2d>::value ());
}

uint32_t
wsl_type_id_sprite_2d ()
{
  return static_cast<uint32_t> (entt::type_hash<comp::sprite_2d>::value ());
}

uint32_t
wsl_type_id_point_light ()
{
  return static_cast<uint32_t> (entt::type_hash<comp::point_light>::value ());
}

uint32_t
wsl_type_id_directional_light ()
{
  return static_cast<uint32_t> (
      entt::type_hash<comp::directional_light>::value ());
}

uint32_t
wsl_type_id_spot_light ()
{
  return static_cast<uint32_t> (entt::type_hash<comp::spot_light>::value ());
}

uint32_t
wsl_type_id_rigid_body ()
{
  return static_cast<uint32_t> (entt::type_hash<comp::rigid_body>::value ());
}

uint32_t
wsl_type_id_character_body ()
{
  return static_cast<uint32_t> (
      entt::type_hash<comp::character_body>::value ());
}

uint32_t
wsl_type_id_model_instance_3d ()
{
  return static_cast<uint32_t> (
      entt::type_hash<comp::model_instance_3d>::value ());
}

uint32_t
wsl_type_id_area_3d ()
{
  return static_cast<uint32_t> (entt::type_hash<comp::area>::value ());
}

uint32_t
wsl_type_id_audio ()
{
  return static_cast<uint32_t> (entt::type_hash<comp::audio>::value ());
}

uint32_t
wsl_type_id_prefab_instance ()
{
  return static_cast<uint32_t> (
      entt::type_hash<comp::prefab_instance>::value ());
}

uint32_t
wsl_type_id_subviewport ()
{
  return static_cast<uint32_t> (entt::type_hash<comp::subviewport>::value ());
}

// ── Keyboard state ──

bool
wsl_is_key_pressed (int scancode)
{
  int num_keys = 0;
  const bool *state = SDL_GetKeyboardState (&num_keys);
  if (!state || scancode < 0 || scancode >= num_keys) {
    return false;
  }
  return state[scancode];
}

// ── Mouse pointer state ──
//
// Polled straight from SDL. Position and button state are idempotent reads;
// the relative-motion getters consume the accumulated delta, so gameplay
// code should read them at most once per frame.

float
wsl_get_mouse_x ()
{
  float x = 0.0F;
  float y = 0.0F;
  SDL_GetMouseState (&x, &y);
  return x;
}

float
wsl_get_mouse_y ()
{
  float x = 0.0F;
  float y = 0.0F;
  SDL_GetMouseState (&x, &y);
  return y;
}

float
wsl_get_mouse_dx ()
{
  float x = 0.0F;
  float y = 0.0F;
  SDL_GetRelativeMouseState (&x, &y);
  return x;
}

float
wsl_get_mouse_dy ()
{
  float x = 0.0F;
  float y = 0.0F;
  SDL_GetRelativeMouseState (&x, &y);
  return y;
}

bool
wsl_mouse_button_down (int button)
{
  if (button < 1 || button > 32) {
    return false;
  }
  return (SDL_GetMouseState (nullptr, nullptr) & SDL_BUTTON_MASK (button)) != 0;
}

// ── Named input actions ──
//
// Actions are names bound to a keyboard key or mouse button in the app
// input map. Held state polls SDL directly; just_pressed / just_released
// scan this frame's published input messages, so they are edge-triggered
// without extra backend state.

namespace
{

const wsl::input::key_binding *
find_action_binding (const char *action)
{
  if (action == nullptr || action[0] == '\0') {
    return nullptr;
  }
  auto *reg = get_registry ();
  if (reg == nullptr
      || !reg->ctx ().contains<comp::singl::runtime_context *> ()) {
    return nullptr;
  }
  auto *runtime_ctx = reg->ctx ().get<comp::singl::runtime_context *> ();
  if (runtime_ctx == nullptr) {
    return nullptr;
  }
  const auto &bindings = runtime_ctx->get_app_input_map ().bindings;
  const auto it = bindings.find (action);
  return it == bindings.end () ? nullptr : &it->second;
}

wsl::input::action_map *
mutable_action_map ()
{
  auto *reg = get_registry ();
  if (reg == nullptr
      || !reg->ctx ().contains<comp::singl::runtime_context *> ()) {
    return nullptr;
  }
  auto *runtime_ctx = reg->ctx ().get<comp::singl::runtime_context *> ();
  if (runtime_ctx == nullptr) {
    return nullptr;
  }
  return &runtime_ctx->get_app_input_map ();
}

// Calls fn for every published message of type Pod until fn returns true.
// Returns false when the bus, buffer, or match is absent.
template <typename Pod, typename Fn>
bool
any_published_message (Fn &&fn)
{
  auto *bus = wsl_api_get_active_message_bus ();
  if (bus == nullptr) {
    return false;
  }
  auto *buf = bus->find_published (wsl::comp::stable_type_id<Pod> ());
  if (buf == nullptr || buf->bytes.empty () || buf->element_size == 0) {
    return false;
  }
  const std::size_t n = buf->bytes.size () / buf->element_size;
  for (std::size_t i = 0; i < n; ++i) {
    Pod v{};
    std::memcpy (&v, buf->bytes.data () + i * buf->element_size,
                 buf->element_size);
    if (fn (v)) {
      return true;
    }
  }
  return false;
}

bool
binding_matches_key_press (const wsl::input::key_binding &b,
                           const wsl::input::key_pressed &e)
{
  if (e.repeat || b.is_mouse ()) {
    return false;
  }
  if (b.is_scancode ()) {
    return e.scancode == b.scancode;
  }
  if (b.is_keycode ()) {
    return e.keycode == b.keycode;
  }
  return false;
}

bool
binding_matches_key_release (const wsl::input::key_binding &b,
                             const wsl::input::key_released &e)
{
  if (b.is_mouse ()) {
    return false;
  }
  if (b.is_scancode ()) {
    return e.scancode == b.scancode;
  }
  if (b.is_keycode ()) {
    return e.keycode == b.keycode;
  }
  return false;
}

bool
binding_matches_mouse_button (const wsl::input::key_binding &b,
                              const wsl::input::mouse_button &e, bool down)
{
  return b.is_mouse () && e.button == b.mouse_button && e.down == down;
}

} // namespace

bool
wsl_action_bind_key (const char *action, int scancode)
{
  auto *map = mutable_action_map ();
  if (map == nullptr || action == nullptr || action[0] == '\0'
      || scancode < 0) {
    return false;
  }
  wsl::input::key_binding binding;
  binding.scancode = static_cast<SDL_Scancode> (scancode);
  map->bindings[action] = binding;
  return true;
}

bool
wsl_action_bind_mouse (const char *action, int button)
{
  auto *map = mutable_action_map ();
  if (map == nullptr || action == nullptr || action[0] == '\0' || button < 1) {
    return false;
  }
  wsl::input::key_binding binding;
  binding.mouse_button = button;
  map->bindings[action] = binding;
  return true;
}

bool
wsl_action_unbind (const char *action)
{
  auto *map = mutable_action_map ();
  if (map == nullptr || action == nullptr || action[0] == '\0') {
    return false;
  }
  return map->bindings.erase (action) > 0;
}

bool
wsl_action_pressed (const char *action)
{
  const auto *b = find_action_binding (action);
  if (b == nullptr) {
    return false;
  }
  if (b->is_mouse ()) {
    return (SDL_GetMouseState (nullptr, nullptr)
            & SDL_BUTTON_MASK (b->mouse_button))
           != 0;
  }
  if (b->is_scancode ()) {
    return wsl_is_key_pressed (static_cast<int> (b->scancode));
  }
  if (b->is_keycode ()) {
    const SDL_Scancode scancode = SDL_GetScancodeFromKey (b->keycode, nullptr);
    return scancode != SDL_SCANCODE_UNKNOWN
           && wsl_is_key_pressed (static_cast<int> (scancode));
  }
  return false;
}

bool
wsl_action_just_pressed (const char *action)
{
  const auto *b = find_action_binding (action);
  if (b == nullptr) {
    return false;
  }
  if (any_published_message<wsl::input::key_pressed> (
          [&] (const wsl::input::key_pressed &e) {
            return binding_matches_key_press (*b, e);
          })) {
    return true;
  }
  return any_published_message<wsl::input::mouse_button> (
      [&] (const wsl::input::mouse_button &e) {
        return binding_matches_mouse_button (*b, e, true);
      });
}

bool
wsl_action_just_released (const char *action)
{
  const auto *b = find_action_binding (action);
  if (b == nullptr) {
    return false;
  }
  if (any_published_message<wsl::input::key_released> (
          [&] (const wsl::input::key_released &e) {
            return binding_matches_key_release (*b, e);
          })) {
    return true;
  }
  return any_published_message<wsl::input::mouse_button> (
      [&] (const wsl::input::mouse_button &e) {
        return binding_matches_mouse_button (*b, e, false);
      });
}

float
wsl_action_axis (const char *neg_action, const char *pos_action)
{
  const bool neg = wsl_action_pressed (neg_action);
  const bool pos = wsl_action_pressed (pos_action);
  if (neg == pos) {
    return 0.0F;
  }
  return pos ? 1.0F : -1.0F;
}

// ── SDL window operations ──

bool
wsl_set_relative_mouse_mode (bool enabled)
{
  auto *reg = get_registry ();
  if (!reg) {
    return false;
  }
  if (!reg->ctx ().contains<comp::singl::runtime_context *> ()) {
    return false;
  }
  auto *runtime_ctx = reg->ctx ().get<comp::singl::runtime_context *> ();
  if (!runtime_ctx || !runtime_ctx->window ().handler ()) {
    return false;
  }
  return SDL_SetWindowRelativeMouseMode (runtime_ctx->window ().handler (),
                                         enabled);
}

bool
wsl_cursor_visible ()
{
  return SDL_CursorVisible ();
}

void
wsl_show_cursor ()
{
  SDL_ShowCursor ();
}

void
wsl_hide_cursor ()
{
  SDL_HideCursor ();
}

// ── Window size (global-state) ──

uint32_t g_window_w = 0;
uint32_t g_window_h = 0;

void
wsl_refresh_window_size ()
{
  auto *reg = get_registry ();
  if (!reg) {
    g_window_w = 0;
    g_window_h = 0;
    return;
  }
  if (!reg->ctx ().contains<comp::singl::runtime_context *> ()) {
    g_window_w = 0;
    g_window_h = 0;
    return;
  }
  auto *runtime_ctx = reg->ctx ().get<comp::singl::runtime_context *> ();
  if (!runtime_ctx) {
    g_window_w = 0;
    g_window_h = 0;
    return;
  }
  runtime_ctx->window ().get_size (g_window_w, g_window_h);
}

uint32_t
wsl_get_window_width ()
{
  return g_window_w;
}

uint32_t
wsl_get_window_height ()
{
  return g_window_h;
}

// ── World-scoped entity iteration (back the `query` macros) ──
void
each_world_entity_with (uint64_t world, const ::das::TArray<uint32_t> &type_ids,
                        const ::das::TBlock<void, uint32_t> &blk,
                        ::das::Context *context, ::das::LineInfoArg *at)
{
  auto *reg = registry_from_handle (world);
  if (reg == nullptr || type_ids.size == 0) {
    return;
  }
  auto *runtime_ctx = reg->ctx ().contains<comp::singl::runtime_context *> ()
                          ? reg->ctx ().get<comp::singl::runtime_context *> ()
                          : nullptr;

  struct term
  {
    const entt::sparse_set *native = nullptr;
    const ::wsl::reg::runtime_pool *das = nullptr;
    std::size_t size = 0;
  };

  constexpr std::size_t stack_term_count = 8;
  std::array<term, stack_term_count> stack_terms{};
  term *terms = stack_terms.data ();
  if (type_ids.size > stack_term_count) {
    static thread_local std::vector<std::vector<term>> overflow_terms_by_depth;
    const std::size_t depth = wsl_api_get_query_depth ();
    if (overflow_terms_by_depth.size () <= depth) {
      overflow_terms_by_depth.resize (depth + 1);
    }
    auto &overflow_terms = overflow_terms_by_depth[depth];
    overflow_terms.resize (type_ids.size);
    terms = overflow_terms.data ();
  }

  auto *component_registry
      = runtime_ctx != nullptr ? &runtime_ctx->component_registry () : nullptr;
  std::size_t driver_index = 0;
  for (uint32_t i = 0; i < type_ids.size; ++i) {
    const entt::id_type type_id = entt::id_type{ type_ids[i] };
    const auto *desc = component_registry != nullptr
                           ? component_registry->find_world_component (type_id)
                           : nullptr;
    term &current = terms[i];
    current = {};
    if (desc != nullptr && desc->is_das_component) {
      if (component_registry == nullptr) {
        return;
      }
      current.das = component_registry->das_component_pool (*reg, type_id);
      if (current.das == nullptr) {
        return;
      }
      current.size = current.das->size ();
    } else {
      current.native = reg->storage (type_id);
      if (current.native == nullptr) {
        return;
      }
      current.size = current.native->size ();
    }
    if (i == 0 || current.size < terms[driver_index].size) {
      driver_index = i;
    }
  }

  auto invoke_if_matching = [&] (entt::entity entity) {
    if (!reg->valid (entity)) {
      return;
    }
    for (std::size_t i = 0; i < type_ids.size; ++i) {
      if (i == driver_index) {
        continue;
      }
      const term &candidate = terms[i];
      if ((candidate.native != nullptr && !candidate.native->contains (entity))
          || (candidate.das != nullptr && !candidate.das->contains (entity))) {
        return;
      }
    }
    ::das::das_invoke<void>::invoke<uint32_t> (context, at, blk,
                                               static_cast<uint32_t> (entity));
  };

  const term &selected = terms[driver_index];
  {
    query_depth_guard guard;
    if (selected.native != nullptr) {
      for (entt::entity entity : *selected.native) {
        invoke_if_matching (entity);
      }
    } else {
      for (entt::entity entity : selected.das->entities ()) {
        invoke_if_matching (entity);
      }
    }
  }
  if (g_query_depth == 0) {
    flush_deferred_commands ();
  }
}

void
each_world_entity_with_except (uint64_t world,
                               const ::das::TArray<uint32_t> &type_ids,
                               const ::das::TArray<uint32_t> &except_ids,
                               const ::das::TBlock<void, uint32_t> &blk,
                               ::das::Context *context, ::das::LineInfoArg *at)
{
  auto *reg = registry_from_handle (world);
  if (reg == nullptr || type_ids.size == 0) {
    return;
  }
  auto *runtime_ctx = reg->ctx ().contains<comp::singl::runtime_context *> ()
                          ? reg->ctx ().get<comp::singl::runtime_context *> ()
                          : nullptr;

  struct term
  {
    const entt::sparse_set *native = nullptr;
    const ::wsl::reg::runtime_pool *das = nullptr;
    std::size_t size = 0;
  };

  // Resolves one id list to matchable terms. A missing pool aborts the whole
  // query for required terms (nothing can match); for excluded terms it
  // yields an empty term that matches nothing.
  auto resolve_terms = [&] (const ::das::TArray<uint32_t> &ids, term *terms,
                            std::size_t &driver_index, bool required) -> bool {
    auto *component_registry = runtime_ctx != nullptr
                                   ? &runtime_ctx->component_registry ()
                                   : nullptr;
    driver_index = 0;
    for (uint32_t i = 0; i < ids.size; ++i) {
      const entt::id_type type_id = entt::id_type{ ids[i] };
      const auto *desc
          = component_registry != nullptr
                ? component_registry->find_world_component (type_id)
                : nullptr;
      term &current = terms[i];
      current = {};
      if (desc != nullptr && desc->is_das_component) {
        if (component_registry == nullptr) {
          return false;
        }
        current.das = component_registry->das_component_pool (*reg, type_id);
        if (current.das == nullptr) {
          if (required) {
            return false;
          }
          continue;
        }
        current.size = current.das->size ();
      } else {
        current.native = reg->storage (type_id);
        if (current.native == nullptr) {
          if (required) {
            return false;
          }
          continue;
        }
        current.size = current.native->size ();
      }
      if (required && (i == 0 || current.size < terms[driver_index].size)) {
        driver_index = i;
      }
    }
    return true;
  };

  constexpr std::size_t stack_term_count = 8;
  std::array<term, stack_term_count> stack_terms{};
  term *terms = stack_terms.data ();
  if (type_ids.size > stack_term_count) {
    static thread_local std::vector<std::vector<term>> overflow_terms_by_depth;
    const std::size_t depth = wsl_api_get_query_depth ();
    if (overflow_terms_by_depth.size () <= depth) {
      overflow_terms_by_depth.resize (depth + 1);
    }
    auto &overflow_terms = overflow_terms_by_depth[depth];
    overflow_terms.resize (type_ids.size);
    terms = overflow_terms.data ();
  }

  std::size_t driver_index = 0;
  if (!resolve_terms (type_ids, terms, driver_index, true)) {
    return;
  }

  std::array<term, stack_term_count> stack_except{};
  term *except_terms = stack_except.data ();
  std::vector<term> heap_except;
  if (except_ids.size > stack_term_count) {
    heap_except.resize (except_ids.size);
    except_terms = heap_except.data ();
  }
  std::size_t except_driver = 0;
  if (!resolve_terms (except_ids, except_terms, except_driver, false)) {
    return;
  }

  auto is_excluded = [&] (entt::entity entity) {
    for (uint32_t i = 0; i < except_ids.size; ++i) {
      const term &candidate = except_terms[i];
      if ((candidate.native != nullptr && candidate.native->contains (entity))
          || (candidate.das != nullptr && candidate.das->contains (entity))) {
        return true;
      }
    }
    return false;
  };

  auto invoke_if_matching = [&] (entt::entity entity) {
    if (!reg->valid (entity) || is_excluded (entity)) {
      return;
    }
    for (std::size_t i = 0; i < type_ids.size; ++i) {
      if (i == driver_index) {
        continue;
      }
      const term &candidate = terms[i];
      if ((candidate.native != nullptr && !candidate.native->contains (entity))
          || (candidate.das != nullptr && !candidate.das->contains (entity))) {
        return;
      }
    }
    ::das::das_invoke<void>::invoke<uint32_t> (context, at, blk,
                                               static_cast<uint32_t> (entity));
  };

  const term &selected = terms[driver_index];
  {
    query_depth_guard guard;
    if (selected.native != nullptr) {
      for (entt::entity entity : *selected.native) {
        invoke_if_matching (entity);
      }
    } else {
      for (entt::entity entity : selected.das->entities ()) {
        invoke_if_matching (entity);
      }
    }
  }
  if (g_query_depth == 0) {
    flush_deferred_commands ();
  }
}

// ── Component type lookup ──

uint32_t
wsl_get_component_type_id (const char *display_name)
{
  auto *reg = get_registry ();
  if (!reg || !display_name) {
    return 0;
  }
  if (!reg->ctx ().contains<comp::singl::runtime_context *> ()) {
    return 0;
  }
  auto *runtime_ctx = reg->ctx ().get<comp::singl::runtime_context *> ();
  if (!runtime_ctx) {
    return 0;
  }
  auto &comp_reg = runtime_ctx->component_registry ();
  auto *desc = comp_reg.find_world_component (display_name);
  if (!desc) {
    return 0;
  }
  return static_cast<uint32_t> (desc->type_id);
}

// ── Generic component type lookup ──

uint32_t
wsl_get_component_type_id_by_name (const char *type_name)
{
  auto *reg = get_registry ();
  if (!reg || !type_name) {
    return 0;
  }
  if (!reg->ctx ().contains<comp::singl::runtime_context *> ()) {
    return 0;
  }
  auto *runtime_ctx = reg->ctx ().get<comp::singl::runtime_context *> ();
  if (!runtime_ctx) {
    return 0;
  }
  auto &comp_reg = runtime_ctx->component_registry ();
  auto *info = comp_reg.find_component_type_info (type_name);
  if (!info) {
    return 0;
  }
  return static_cast<uint32_t> (info->type_id);
}

// ── Raycasting (global-state) ──

float g_ray_origin[3] = {};
float g_ray_dir[3] = {};
float g_hit_point[3] = {};

bool
wsl_make_pick_ray (uint32_t camera_entity, float mouse_x, float mouse_y,
                   float vp_x, float vp_y, float vp_w, float vp_h)
{
  auto *reg = get_registry ();
  if (!reg) {
    return false;
  }
  auto e = static_cast<entt::entity> (camera_entity);
  if (!reg->valid (e)
      || !reg->all_of<comp::camera, comp::world_transform> (e)) {
    return false;
  }
  wsl::pick_ray ray
      = wsl::make_pick_ray (*reg, e, mouse_x, mouse_y, vp_x, vp_y, vp_w, vp_h);
  g_ray_origin[0] = ray.origin.x;
  g_ray_origin[1] = ray.origin.y;
  g_ray_origin[2] = ray.origin.z;
  g_ray_dir[0] = ray.dir.x;
  g_ray_dir[1] = ray.dir.y;
  g_ray_dir[2] = ray.dir.z;
  return true;
}

float
wsl_get_ray_origin_x ()
{
  return g_ray_origin[0];
}
float
wsl_get_ray_origin_y ()
{
  return g_ray_origin[1];
}
float
wsl_get_ray_origin_z ()
{
  return g_ray_origin[2];
}

float
wsl_get_ray_dir_x ()
{
  return g_ray_dir[0];
}
float
wsl_get_ray_dir_y ()
{
  return g_ray_dir[1];
}
float
wsl_get_ray_dir_z ()
{
  return g_ray_dir[2];
}

bool
wsl_ray_plane_intersect (float origin_x, float origin_y, float origin_z,
                         float dir_x, float dir_y, float dir_z, float plane_x,
                         float plane_y, float plane_z, float plane_nx,
                         float plane_ny, float plane_nz)
{
  wsl::pick_ray ray;
  ray.origin = glm::vec3 (origin_x, origin_y, origin_z);
  ray.dir = glm::vec3 (dir_x, dir_y, dir_z);
  glm::vec3 plane_point (plane_x, plane_y, plane_z);
  glm::vec3 plane_normal (plane_nx, plane_ny, plane_nz);
  float t_hit = 0.0F;
  glm::vec3 hit_point;
  if (!wsl::ray_plane_intersect (ray, plane_point, plane_normal, t_hit,
                                 hit_point)) {
    return false;
  }
  g_hit_point[0] = hit_point.x;
  g_hit_point[1] = hit_point.y;
  g_hit_point[2] = hit_point.z;
  return true;
}

float
wsl_get_hit_x ()
{
  return g_hit_point[0];
}
float
wsl_get_hit_y ()
{
  return g_hit_point[1];
}
float
wsl_get_hit_z ()
{
  return g_hit_point[2];
}

// ── Runtime context / subsystem accessors ──

phys::engine *
try_get_physics_engine ()
{
  auto *rc = try_get_runtime_context ();
  if (!rc) {
    return nullptr;
  }
  auto *pm = rc->get_active_physics_manager ();
  if (!pm) {
    return nullptr;
  }
  return pm->try_engine ();
}

// ── Time ──
//
// Pause and time scale are engine-owned gameplay state on the runtime
// context (one per game instance), so pausing actually freezes the
// simulation: the frame-phase driver skips fixed steps and physics stops
// stepping. `delta` reported to scripts is the raw frame dt multiplied by
// the scale (zero when paused); wall-clock `elapsed` is unaffected.
// Without an active runtime context (bare interpreters) reads report
// defaults and writes are no-ops.

float
wsl_get_elapsed_time ()
{
  static const auto start = std::chrono::steady_clock::now ();
  return static_cast<float> (
      std::chrono::duration<double> (std::chrono::steady_clock::now () - start)
          .count ());
}

float
wsl_get_time ()
{
  return static_cast<float> (wsl_get_elapsed_time ());
}

float
wsl_get_fixed_delta ()
{
  if (auto *eng = try_get_physics_engine ()) {
    const double step = eng->get_fixed_step ();
    if (step > 0.0) {
      return static_cast<float> (step);
    }
  }
  return 1.0F / 60.0F;
}

uint64_t
wsl_get_frame_index ()
{
  if (auto *rc = try_get_runtime_context ()) {
    return rc->render_ctx ().frame_index ();
  }
  return 0;
}

float
wsl_get_time_scale ()
{
  if (auto *rc = try_get_runtime_context ()) {
    return static_cast<float> (rc->time_scale ());
  }
  return 1.0F;
}

void
wsl_set_time_scale (float scale)
{
  if (auto *rc = try_get_runtime_context ()) {
    rc->set_time_scale (static_cast<double> (scale));
  }
}

bool
wsl_is_time_paused ()
{
  if (auto *rc = try_get_runtime_context ()) {
    return rc->is_paused ();
  }
  return false;
}

void
wsl_set_time_paused (bool paused)
{
  if (auto *rc = try_get_runtime_context ()) {
    rc->set_paused (paused);
  }
}

// ── Physics: rigid body ──

void
wsl_apply_impulse (uint32_t entity, float x, float y, float z)
{
  auto *eng = try_get_physics_engine ();
  auto *reg = get_registry ();
  if (!eng || !reg) {
    return;
  }
  auto e = static_cast<entt::entity> (entity);
  if (!reg->valid (e) || !reg->all_of<comp::rigid_body> (e)) {
    return;
  }
  auto &rb = reg->get<comp::rigid_body> (e);
  if (!phys::is_valid_body_id (rb.body_id)) {
    return;
  }
  eng->add_impulse (rb.body_id, { x, y, z });
}

void
wsl_apply_force (uint32_t entity, float x, float y, float z)
{
  auto *eng = try_get_physics_engine ();
  auto *reg = get_registry ();
  if (!eng || !reg) {
    return;
  }
  auto e = static_cast<entt::entity> (entity);
  if (!reg->valid (e) || !reg->all_of<comp::rigid_body> (e)) {
    return;
  }
  auto &rb = reg->get<comp::rigid_body> (e);
  if (!phys::is_valid_body_id (rb.body_id)) {
    return;
  }
  eng->add_force (rb.body_id, { x, y, z });
}

namespace
{

/** Shared rigid-body lookup for the velocity accessors below. */
const comp::rigid_body *
find_live_rigid_body (entt::registry *reg, uint32_t entity,
                      phys::engine *&eng_out)
{
  eng_out = try_get_physics_engine ();
  if (eng_out == nullptr || reg == nullptr) {
    return nullptr;
  }
  auto e = static_cast<entt::entity> (entity);
  if (!reg->valid (e) || !reg->all_of<comp::rigid_body> (e)) {
    return nullptr;
  }
  auto &rb = reg->get<comp::rigid_body> (e);
  if (!phys::is_valid_body_id (rb.body_id)) {
    return nullptr;
  }
  return &rb;
}

} // namespace

void
wsl_set_body_velocity (uint32_t entity, float x, float y, float z)
{
  auto *reg = get_registry ();
  phys::engine *eng = nullptr;
  const comp::rigid_body *rb = find_live_rigid_body (reg, entity, eng);
  if (rb == nullptr) {
    return;
  }
  eng->set_body_velocity (rb->body_id, { x, y, z });
}

float
wsl_get_body_velocity_x (uint32_t entity)
{
  auto *reg = get_registry ();
  phys::engine *eng = nullptr;
  const comp::rigid_body *rb = find_live_rigid_body (reg, entity, eng);
  if (rb == nullptr) {
    return 0.0F;
  }
  return eng->get_body_velocity (rb->body_id).x;
}

float
wsl_get_body_velocity_y (uint32_t entity)
{
  auto *reg = get_registry ();
  phys::engine *eng = nullptr;
  const comp::rigid_body *rb = find_live_rigid_body (reg, entity, eng);
  if (rb == nullptr) {
    return 0.0F;
  }
  return eng->get_body_velocity (rb->body_id).y;
}

float
wsl_get_body_velocity_z (uint32_t entity)
{
  auto *reg = get_registry ();
  phys::engine *eng = nullptr;
  const comp::rigid_body *rb = find_live_rigid_body (reg, entity, eng);
  if (rb == nullptr) {
    return 0.0F;
  }
  return eng->get_body_velocity (rb->body_id).z;
}

float
wsl_get_body_position_x (uint32_t entity)
{
  auto *reg = get_registry ();
  phys::engine *eng = nullptr;
  const comp::rigid_body *rb = find_live_rigid_body (reg, entity, eng);
  if (rb == nullptr) {
    return 0.0F;
  }
  return eng->get_body_position (rb->body_id).x;
}

float
wsl_get_body_position_y (uint32_t entity)
{
  auto *reg = get_registry ();
  phys::engine *eng = nullptr;
  const comp::rigid_body *rb = find_live_rigid_body (reg, entity, eng);
  if (rb == nullptr) {
    return 0.0F;
  }
  return eng->get_body_position (rb->body_id).y;
}

float
wsl_get_body_position_z (uint32_t entity)
{
  auto *reg = get_registry ();
  phys::engine *eng = nullptr;
  const comp::rigid_body *rb = find_live_rigid_body (reg, entity, eng);
  if (rb == nullptr) {
    return 0.0F;
  }
  return eng->get_body_position (rb->body_id).z;
}

void
wsl_set_body_position (uint32_t entity, float x, float y, float z)
{
  auto *reg = get_registry ();
  phys::engine *eng = nullptr;
  const comp::rigid_body *rb = find_live_rigid_body (reg, entity, eng);
  if (rb == nullptr) {
    return;
  }
  // Rotation is preserved: only the world position is teleported.
  const phys::quaternion rot = eng->get_body_rotation (rb->body_id);
  eng->set_body_transform (rb->body_id, { x, y, z }, rot);
}

// ── Systems: parallel execution hints ──

void
wsl_set_system_stage (const char *stage)
{
  wsl::das::das_system_adapter::set_current_stage (stage);
}

void
wsl_set_system_dependency (const char *name)
{
  wsl::das::das_system_adapter::add_current_dependency (name);
}

void
wsl_set_system_conflict (const char *name)
{
  wsl::das::das_system_adapter::add_current_conflict (name);
}

// ── Audio ──

// ── Event runtime wiring ──

static entt::id_type
resolve_event_type_id (const std::string &name)
{
  auto *rc = try_get_runtime_context ();
  if (rc == nullptr || rc->event_hub ().db == nullptr) {
    return 0;
  }

  for (auto &kv : rc->event_hub ().db->entries) {
    const auto &entry = kv.second;
    if (entry.type_name == name) {
      return kv.first;
    }

    auto pos = entry.type_name.rfind ("::");
    std::string simple = (pos == std::string::npos)
                             ? entry.type_name
                             : entry.type_name.substr (pos + 2);
    if (simple == name) {
      return kv.first;
    }
  }

  return 0;
}

static bool
wsl_event_connect (const char *event_name, const char *system_name,
                   const char *handler_name)
{
  auto *rc = try_get_runtime_context ();
  if (!rc || !event_name || !system_name || !handler_name) {
    return false;
  }

  const entt::id_type event_id = resolve_event_type_id (event_name);
  if (event_id == 0) {
    return false;
  }

  const auto *sd = rc->system_factory_registry ().find_system (system_name);
  if (sd == nullptr) {
    return false;
  }

  return rc->event_hub ().connect (event_id, sd->type_id, handler_name);
}

static bool
wsl_event_disconnect (const char *event_name, const char *system_name,
                      const char *handler_name)
{
  auto *rc = try_get_runtime_context ();
  if (!rc || !event_name || !system_name || !handler_name) {
    return false;
  }

  const entt::id_type event_id = resolve_event_type_id (event_name);
  if (event_id == 0) {
    return false;
  }

  const auto *sd = rc->system_factory_registry ().find_system (system_name);
  if (sd == nullptr) {
    return false;
  }

  return rc->event_hub ().disconnect (event_id, sd->type_id, handler_name);
}

// Like wsl_event_connect/disconnect, but for the script system that is
// currently executing a lifecycle call: no system name is needed because the
// running adapter supplies its own identity. Used to implement ergonomic
// subscribe/dispose without threading display names through scripts.
static bool
wsl_event_connect_current (const char *event_name, const char *handler_name)
{
  auto *adapter = das_system_adapter::current ();
  auto *rc = try_get_runtime_context ();
  if (adapter == nullptr || rc == nullptr || !event_name || !handler_name) {
    return false;
  }
  const entt::id_type event_id = resolve_event_type_id (event_name);
  if (event_id == 0) {
    return false;
  }
  return rc->event_hub ().connect (event_id, adapter->get_type_id (),
                                   handler_name);
}

static bool
wsl_event_disconnect_current (const char *event_name, const char *handler_name)
{
  auto *adapter = das_system_adapter::current ();
  auto *rc = try_get_runtime_context ();
  if (adapter == nullptr || rc == nullptr || !event_name || !handler_name) {
    return false;
  }
  const entt::id_type event_id = resolve_event_type_id (event_name);
  if (event_id == 0) {
    return false;
  }
  return rc->event_hub ().disconnect (event_id, adapter->get_type_id (),
                                      handler_name);
}

// ── Name-keyed event declaration / emission ──

/**
 * Declares the caller-named system as an emitter of an event type.
 *
 * The event may be C++-defined (declared through the regular hooks) or
 * script-defined: an unknown name synthesizes a name-hashed id whose payload
 * size latches on first emit.
 */
static bool
wsl_event_declare_source (const char *event_name, const char *system_name)
{
  auto *rc = try_get_runtime_context ();
  if (!rc || !event_name || !system_name) {
    return false;
  }

  auto &hub = rc->event_hub ();
  if (hub.db == nullptr) {
    return false;
  }

  std::string event_type_name;
  std::size_t event_size = 0;

  entt::id_type event_id = resolve_event_type_id (event_name);
  if (event_id != 0) {
    const auto it = hub.db->entries.find (event_id);
    if (it == hub.db->entries.end ()) {
      return false;
    }
    event_type_name = it->second.type_name;
    event_size = it->second.size;
  } else {
    // Script-defined event type: synthesize a stable id from the name. The
    // payload size latches on first emit.
    event_id = entt::id_type{ entt::hashed_string (event_name) };
    event_type_name = event_name;
  }

  const auto *sd = rc->system_factory_registry ().find_system (system_name);
  if (sd == nullptr) {
    wsl::log::sys ()->error ("event_declare_source: unknown system '{}'",
                             system_name);
    return false;
  }

  // When called from inside a daScript system, record the declaration on the
  // adapter so it survives register_debug_metadata() wipe passes.
  auto *adapter = das_system_adapter::current ();
  if (adapter != nullptr && adapter->get_name () == system_name) {
    return adapter->add_script_event_source (event_id, event_type_name,
                                             event_size);
  }

  hub.declare_event_source_by_id (event_id, event_type_name, sd->type_id,
                                  sd->type_name, event_size);
  return true;
}

/**
 * Declares a class method of the calling daScript system as a connectable
 * event handler. The method is resolved by name at dispatch time and must
 * accept the event payload struct as its single argument.
 */
static bool
wsl_event_declare_sink (const char *event_name, const char *system_name,
                        const char *handler_name)
{
  auto *adapter = das_system_adapter::current ();
  if (adapter == nullptr) {
    wsl::log::sys ()->error (
        "event_declare_sink: can only be called from inside a daScript "
        "system lifecycle call");
    return false;
  }

  if (!event_name || !system_name || !handler_name) {
    return false;
  }

  if (adapter->get_name () != system_name) {
    wsl::log::sys ()->error (
        "event_declare_sink: owner '{}' does not match the calling script "
        "system '{}'",
        system_name, adapter->get_name ());
    return false;
  }
  std::string event_type_name = event_name;
  entt::id_type event_id = resolve_event_type_id (event_name);
  std::size_t event_size = 0;
  if (event_id == 0) {
    // Script-defined event type: synthesize a name-hashed id (mirrors
    // event_declare_source).
    event_id = entt::id_type{ entt::hashed_string (event_name) };
  } else if (auto *rc = try_get_runtime_context (); rc && rc->event_hub ().db) {
    const auto entry_it = rc->event_hub ().db->entries.find (event_id);
    if (entry_it != rc->event_hub ().db->entries.end ()) {
      event_type_name = entry_it->second.type_name;
      event_size = entry_it->second.size;
    }
  }

  // Ensure the event is resolvable by name even when the sink is declared
  // before the source: event_connect resolves ids through the source entries.
  // The minimal entry has no owner, so wipe passes keep it.
  if (auto *rc = try_get_runtime_context (); rc && rc->event_hub ().db) {
    auto &entries = rc->event_hub ().db->entries;
    if (entries.find (event_id) == entries.end ()) {
      ::wsl::event::event_source_debug_entry entry{};
      entry.type_id = event_id;
      entry.type_name = event_type_name;
      entries.emplace (event_id, std::move (entry));
    }
  }

  if (!adapter->has_method (handler_name)) {
    wsl::log::sys ()->error (
        "event_declare_sink: method '{}' not found on script system '{}'",
        handler_name, system_name);
    return false;
  }

  std::size_t handler_payload_size = 0;
  std::uint64_t handler_layout_hash = 0;
  if (!adapter->event_handler_payload_signature (
          handler_name, handler_payload_size, handler_layout_hash)) {
    wsl::log::sys ()->error (
        "event_declare_sink: handler '{}' must accept one raw-POD struct "
        "payload",
        handler_name);
    return false;
  }
  if (event_size != 0 && event_size != handler_payload_size) {
    wsl::log::sys ()->error ("event_declare_sink: payload size mismatch for "
                             "'{}' (source {}, sink {})",
                             event_name, event_size, handler_payload_size);
    return false;
  }

  return adapter->add_script_event_sink (event_id, event_type_name,
                                         handler_name, handler_payload_size,
                                         handler_layout_hash);
}

static bool
wsl_event_declare_sink_current (const char *event_name,
                                const char *handler_name)
{
  auto *adapter = das_system_adapter::current ();
  if (adapter == nullptr) {
    return false;
  }
  const std::string system_name = adapter->get_name ();
  return wsl_event_declare_sink (event_name, system_name.c_str (),
                                 handler_name);
}

/**
 * Emits a registered event from a daScript struct payload.
 *
 * The payload is accepted as ``anyArgument``; it must be a structure whose
 * size matches the registered C++ event type. Bytes are dispatched directly
 * to connected sinks (no intermediate copy).
 */

/**
 * Validates a script payload against the event record, latching size and
 * layout for script-defined events on first use (mirrors message_post's
 * first-use registration). Shared by immediate `emit` and queued `post`
 * so both enforce identical rules. Returns false after logging; on
 * success fills the out-params and returns true.
 */
static bool
resolve_event_payload_for_delivery (const char *verb, const char *event_name,
                                    ::das::TypeInfo *ti, const void *data,
                                    ::wsl::event::event_hub &hub,
                                    entt::id_type &event_id,
                                    std::size_t &payload_size,
                                    std::uint64_t &payload_layout_hash)
{
  if (ti == nullptr || ti->type != ::das::Type::tStructure) {
    wsl::log::sys ()->error ("{}: payload for '{}' must be a structure type",
                             verb, event_name);
    return false;
  }
  if (ti->structType == nullptr || !ti->isRawPod ()) {
    wsl::log::sys ()->error (
        "{}: payload for '{}' must be a hashable raw-POD struct", verb,
        event_name);
    return false;
  }

  payload_size = static_cast<std::size_t> (::das::getTypeSize (ti));
  payload_layout_hash = das_struct_layout_hash (ti->structType);
  if (payload_layout_hash == 0) {
    wsl::log::sys ()->error (
        "{}: payload layout for '{}' could not be determined", verb,
        event_name);
    return false;
  }
  if (data == nullptr || payload_size == 0) {
    wsl::log::sys ()->error ("{}: payload for '{}' is null or has zero size",
                             verb, event_name);
    return false;
  }

  if (hub.db == nullptr) {
    return false;
  }

  event_id = resolve_event_type_id (event_name);
  auto it = hub.db->entries.find (event_id);
  if (it == hub.db->entries.end ()) {
    // Undeclared event: register it on first use as a script-defined type.
    if (event_id == 0) {
      event_id = entt::id_type{ entt::hashed_string (event_name) };
    }
    it = hub.db->entries
             .emplace (event_id, ::wsl::event::event_source_debug_entry{})
             .first;
    it->second.type_id = event_id;
    it->second.type_name = event_name;
  }

  if (it->second.size == 0) {
    // Script-defined event: first use latches the payload layout so later
    // uses and sink wiring validate against it.
    it->second.size = payload_size;
  } else if (it->second.size != payload_size) {
    wsl::log::sys ()->error (
        "{}: payload size mismatch for '{}' (expected {}, got {})", verb,
        event_name, it->second.size, payload_size);
    return false;
  }

  if (it->second.payload_layout_hash == 0) {
    it->second.payload_layout_hash = payload_layout_hash;
  } else if (it->second.payload_layout_hash != payload_layout_hash) {
    wsl::log::sys ()->error ("{}: payload layout mismatch for '{}'", verb,
                             event_name);
    return false;
  }
  return true;
}

static vec4f
wsl_event_emit (::das::Context &, ::das::SimNode_CallBase *call, vec4f *args)
{
  const char *event_name = ::das::cast<char *>::to (args[0]);

  auto *rc = try_get_runtime_context ();
  if (!rc || !event_name) {
    return v_zero ();
  }

  entt::id_type event_id = 0;
  std::size_t payload_size = 0;
  std::uint64_t payload_layout_hash = 0;
  if (!resolve_event_payload_for_delivery (
          "event_emit", event_name, call->types[1],
          ::das::cast<void *>::to (args[1]), rc->event_hub (), event_id,
          payload_size, payload_layout_hash)) {
    return v_zero ();
  }

  rc->event_hub ().dispatch_by_id (event_id, ::das::cast<void *>::to (args[1]),
                                   payload_size, payload_layout_hash);
  return v_zero ();
}

/**
 * Queues a registered event from a daScript struct payload for deferred
 * delivery. Validation and first-use latching are identical to
 * `event_emit`; instead of dispatching, the payload bytes are copied into
 * the runtime's FIFO queue and delivered at the next pump (start of the
 * next systems update), in posting order. Events posted while no scene is
 * active wait (cap-bound); events posted during a pump roll to the next
 * pump. Use this when the receiver must not run reentrantly inside the
 * sender (e.g. posting from inside a query or a handler); use `emit` when
 * synchronous delivery is required.
 */
static vec4f
wsl_event_post (::das::Context &, ::das::SimNode_CallBase *call, vec4f *args)
{
  const char *event_name = ::das::cast<char *>::to (args[0]);

  auto *rc = try_get_runtime_context ();
  if (!rc || !event_name) {
    return v_zero ();
  }

  entt::id_type event_id = 0;
  std::size_t payload_size = 0;
  std::uint64_t payload_layout_hash = 0;
  const void *data = ::das::cast<void *>::to (args[1]);
  if (!resolve_event_payload_for_delivery (
          "event_post", event_name, call->types[1], data, rc->event_hub (),
          event_id, payload_size, payload_layout_hash)) {
    return v_zero ();
  }

  if (!rc->post_event (event_id, data, payload_size, payload_layout_hash)) {
    wsl::log::sys ()->error ("event_post: queue rejected event '{}'",
                             event_name);
  }
  return v_zero ();
}

void
wsl_audio_play (uint32_t entity)
{
  auto *rc = try_get_runtime_context ();
  if (!rc) {
    return;
  }
  ::wsl::event::emit (rc->event_hub (),
                      comp::audio::play{ static_cast<entt::entity> (entity) });
}

void
wsl_audio_stop (uint32_t entity)
{
  auto *rc = try_get_runtime_context ();
  if (!rc) {
    return;
  }
  ::wsl::event::emit (rc->event_hub (),
                      comp::audio::stop{ static_cast<entt::entity> (entity) });
}

void
wsl_audio_pause (uint32_t entity)
{
  auto *rc = try_get_runtime_context ();
  if (!rc) {
    return;
  }
  ::wsl::event::emit (rc->event_hub (),
                      comp::audio::pause{ static_cast<entt::entity> (entity) });
}

void
wsl_audio_resume (uint32_t entity)
{
  auto *rc = try_get_runtime_context ();
  if (!rc) {
    return;
  }
  ::wsl::event::emit (
      rc->event_hub (),
      comp::audio::resume{ static_cast<entt::entity> (entity) });
}

void
wsl_audio_set_volume (uint32_t entity, float volume)
{
  auto *rc = try_get_runtime_context ();
  if (!rc) {
    return;
  }
  ::wsl::event::emit (
      rc->event_hub (),
      comp::audio::set_volume{ static_cast<entt::entity> (entity), volume });
}

// ── Model instance ──

// ── Animation ──
//
// These write straight to comp::animator rather than going through the event
// hub: the animation system already diffs clip_path every frame, so changing
// it is what starts a crossfade. There is no need to bounce the request off a
// system callback to reach the same state.

/** Resolves the registry and an entity carrying comp::animator, or null. */
static comp::animator *
animator_of (uint32_t entity)
{
  auto *reg = get_registry ();
  if (!reg) {
    return nullptr;
  }
  auto e = static_cast<entt::entity> (entity);
  if (!reg->valid (e) || !reg->all_of<comp::animator> (e)) {
    return nullptr;
  }
  return &reg->get<comp::animator> (e);
}

void
wsl_anim_play (uint32_t entity, const char *clip)
{
  if (!clip) {
    return;
  }
  if (comp::animator *anim = animator_of (entity); anim != nullptr) {
    anim->clip_path = clip;
    anim->time = 0.0F;
    anim->finished = false;
    anim->playing = true;
    anim->paused = false;
  }
}

void
wsl_anim_crossfade (uint32_t entity, const char *clip, float duration)
{
  if (!clip) {
    return;
  }
  if (comp::animator *anim = animator_of (entity); anim != nullptr) {
    // Changing clip_path is what the animation system watches to begin a
    // crossfade, so this matches anim_play apart from the fade length.
    anim->clip_path = clip;
    anim->crossfade_duration = std::max (0.0F, duration);
    anim->time = 0.0F;
    anim->finished = false;
    anim->playing = true;
    anim->paused = false;
  }
}

void
wsl_anim_stop (uint32_t entity)
{
  if (comp::animator *anim = animator_of (entity); anim != nullptr) {
    anim->playing = false;
    anim->paused = false;
    anim->time = 0.0F;
    anim->finished = false;
  }
}

void
wsl_anim_set_speed (uint32_t entity, float speed)
{
  if (comp::animator *anim = animator_of (entity); anim != nullptr) {
    anim->speed = speed;
  }
}

void
wsl_anim_set_time (uint32_t entity, float time)
{
  if (comp::animator *anim = animator_of (entity); anim != nullptr) {
    anim->time = std::max (0.0F, time);
    anim->finished = false;
  }
}

void
wsl_anim_set_loop (uint32_t entity, bool loop)
{
  if (comp::animator *anim = animator_of (entity); anim != nullptr) {
    anim->loop = loop;
  }
}

void
wsl_anim_pause (uint32_t entity)
{
  if (comp::animator *anim = animator_of (entity); anim != nullptr) {
    anim->paused = true;
  }
}

void
wsl_anim_resume (uint32_t entity)
{
  if (comp::animator *anim = animator_of (entity); anim != nullptr) {
    anim->paused = false;
  }
}

bool
wsl_anim_is_playing (uint32_t entity)
{
  const comp::animator *anim = animator_of (entity);
  return anim != nullptr && anim->playing;
}

bool
wsl_anim_is_paused (uint32_t entity)
{
  const comp::animator *anim = animator_of (entity);
  return anim != nullptr && anim->paused;
}

bool
wsl_anim_is_finished (uint32_t entity)
{
  const comp::animator *anim = animator_of (entity);
  return anim != nullptr && anim->finished;
}

float
wsl_anim_get_time (uint32_t entity)
{
  const comp::animator *anim = animator_of (entity);
  return anim != nullptr ? anim->time : -1.0F;
}

float
wsl_anim_get_duration (uint32_t entity)
{
  const comp::animator *anim = animator_of (entity);
  return anim != nullptr && anim->clip_duration > 0.0F ? anim->clip_duration
                                                       : -1.0F;
}

bool
wsl_audio_is_playing (uint32_t entity)
{
  auto *reg = get_registry ();
  if (!reg) {
    return false;
  }
  const auto e = static_cast<entt::entity> (entity);
  if (!reg->valid (e)) {
    return false;
  }
  const auto *audio = reg->try_get<comp::audio> (e);
  return audio != nullptr && audio->playing;
}

void
wsl_set_model (uint32_t entity, const char *path)
{
  auto *rc = try_get_runtime_context ();
  auto *reg = get_registry ();
  if (!rc || !reg || !path) {
    return;
  }
  auto e = static_cast<entt::entity> (entity);
  if (!reg->valid (e) || !reg->all_of<comp::model_instance_3d> (e)) {
    return;
  }
  reg->get<comp::model_instance_3d> (e).id
      = rc->resource_manager ().register_model (std::string (path));
}

void
wsl_set_model_material_override (uint32_t entity, const char *path)
{
  auto *rc = try_get_runtime_context ();
  auto *reg = get_registry ();
  if (!rc || !reg || !path) {
    return;
  }
  auto e = static_cast<entt::entity> (entity);
  if (!reg->valid (e) || !reg->all_of<comp::model_instance_3d> (e)) {
    return;
  }
  reg->get<comp::model_instance_3d> (e).material_override
      = rc->resource_manager ().register_material (std::string (path));
}

void
wsl_set_model_visibility_range (uint32_t entity, float range)
{
  auto *reg = get_registry ();
  if (!reg) {
    return;
  }
  auto e = static_cast<entt::entity> (entity);
  if (!reg->valid (e) || !reg->all_of<comp::model_instance_3d> (e)) {
    return;
  }
  reg->get<comp::model_instance_3d> (e).visibility_range = range;
}

class Module_WeaselApi : public ::das::Module
{
public:
  Module_WeaselApi () : Module ("weasel_api")
  {
    ::das::ModuleLibrary lib (this);
    lib.addBuiltInModule ();
    register_component_accessors (*this, lib);

    // Logging
    addExtern<DAS_BIND_FUN (wsl_log_info)> (*this, lib, "log_info",
                                            ::das::SideEffects::modifyExternal,
                                            "wsl::das::wsl_log_info")
        ->arg ("msg");
    addExtern<DAS_BIND_FUN (wsl_log_info)> (*this, lib, "_log_info",
                                            ::das::SideEffects::modifyExternal,
                                            "wsl::das::wsl_log_info")
        ->arg ("msg");

    addExtern<DAS_BIND_FUN (wsl_log_debug)> (*this, lib, "log_debug",
                                             ::das::SideEffects::modifyExternal,
                                             "wsl::das::wsl_log_debug")
        ->arg ("msg");
    addExtern<DAS_BIND_FUN (wsl_log_debug)> (*this, lib, "_log_debug",
                                             ::das::SideEffects::modifyExternal,
                                             "wsl::das::wsl_log_debug")
        ->arg ("msg");

    addExtern<DAS_BIND_FUN (wsl_log_warn)> (*this, lib, "log_warn",
                                            ::das::SideEffects::modifyExternal,
                                            "wsl::das::wsl_log_warn")
        ->arg ("msg");
    addExtern<DAS_BIND_FUN (wsl_log_warn)> (*this, lib, "_log_warn",
                                            ::das::SideEffects::modifyExternal,
                                            "wsl::das::wsl_log_warn")
        ->arg ("msg");

    addExtern<DAS_BIND_FUN (wsl_log_error)> (*this, lib, "log_error",
                                             ::das::SideEffects::modifyExternal,
                                             "wsl::das::wsl_log_error")
        ->arg ("msg");
    addExtern<DAS_BIND_FUN (wsl_log_error)> (*this, lib, "_log_error",
                                             ::das::SideEffects::modifyExternal,
                                             "wsl::das::wsl_log_error")
        ->arg ("msg");

    addExtern<DAS_BIND_FUN (wsl_log_trace)> (*this, lib, "log_trace",
                                             ::das::SideEffects::modifyExternal,
                                             "wsl::das::wsl_log_trace")
        ->arg ("msg");
    addExtern<DAS_BIND_FUN (wsl_log_trace)> (*this, lib, "_log_trace",
                                             ::das::SideEffects::modifyExternal,
                                             "wsl::das::wsl_log_trace")
        ->arg ("msg");

    // Scene system control (engine display names; gameplay facade wraps these
    // with documented enable/disable/query/scheduling helpers).
    addExtern<DAS_BIND_FUN (wsl_enable_system)> (
        *this, lib, "_enable_system", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_enable_system")
        ->arg ("name");
    addExtern<DAS_BIND_FUN (wsl_disable_system)> (
        *this, lib, "_disable_system", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_disable_system")
        ->arg ("name");
    addExtern<DAS_BIND_FUN (wsl_system_enabled)> (
        *this, lib, "_system_enabled", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_system_enabled")
        ->arg ("name");

    // Entity operations
    addExtern<DAS_BIND_FUN (wsl_current_world)> (
        *this, lib, "_current_world", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_current_world");

    addExtern<DAS_BIND_FUN (wsl_ui_bind_button)> (
        *this, lib, "_ui_bind_button", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_ui_bind_button")
        ->args ({ "element_id", "method_name" });

    addExtern<DAS_BIND_FUN (wsl_world_create_entity)> (
        *this, lib, "_world_create_entity", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_world_create_entity")
        ->arg ("world");

    addExtern<DAS_BIND_FUN (wsl_world_destroy_entity)> (
        *this, lib, "_world_destroy_entity", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_world_destroy_entity")
        ->args ({ "world", "entity_world", "entity_id", "context", "at" });

    addExtern<DAS_BIND_FUN (wsl_world_entity_is_alive)> (
        *this, lib, "_world_entity_is_alive",
        ::das::SideEffects::accessExternal,
        "wsl::das::wsl_world_entity_is_alive")
        ->args ({ "world", "entity_world", "entity_id" });

    addExtern<DAS_BIND_FUN (wsl_world_entity_is_null)> (
        *this, lib, "_world_entity_is_null", ::das::SideEffects::none,
        "wsl::das::wsl_world_entity_is_null")
        ->args ({ "entity_world", "entity_id" });

    addExtern<DAS_BIND_FUN (wsl_world_find_entity_by_name)> (
        *this, lib, "_world_find_entity", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_world_find_entity_by_name")
        ->args ({ "world", "name" });

    addExtern<DAS_BIND_FUN (wsl_world_set_entity_name)> (
        *this, lib, "_world_set_name", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_world_set_entity_name")
        ->args ({ "world", "entity_world", "entity_id", "name" });

    addExtern<DAS_BIND_FUN (wsl_world_set_parent)> (
        *this, lib, "_world_set_parent", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_world_set_parent")
        ->args ({ "world", "child_world", "child_id", "parent_world",
                  "parent_id", "keep_world", "context", "at" });

    addExtern<DAS_BIND_FUN (wsl_world_get_parent)> (
        *this, lib, "_world_get_parent", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_world_get_parent")
        ->args ({ "world", "entity_world", "entity_id" });

    addExtern<DAS_BIND_FUN (wsl_world_get_child_count)> (
        *this, lib, "_world_get_child_count",
        ::das::SideEffects::accessExternal,
        "wsl::das::wsl_world_get_child_count")
        ->args ({ "world", "entity_world", "entity_id" });

    addExtern<DAS_BIND_FUN (wsl_world_get_child)> (
        *this, lib, "_world_get_child", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_world_get_child")
        ->args ({ "world", "entity_world", "entity_id", "index" });

    addExtern<DAS_BIND_FUN (wsl_world_change_scene)> (
        *this, lib, "_world_change_scene", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_world_change_scene")
        ->args ({ "world", "scene_name" });

    addExtern<DAS_BIND_FUN (wsl_world_entity_from_id)> (
        *this, lib, "_world_entity_from_id", ::das::SideEffects::none,
        "wsl::das::wsl_world_entity_from_id")
        ->args ({ "world", "entity_id" });

    addExtern<DAS_BIND_FUN (wsl_world_entity_id)> (
        *this, lib, "_world_entity_id", ::das::SideEffects::none,
        "wsl::das::wsl_world_entity_id")
        ->args ({ "world", "entity_world", "entity_id" });

    addExtern<DAS_BIND_FUN (wsl_world_component_type_id)> (
        *this, lib, "_world_component_type_id",
        ::das::SideEffects::accessExternal,
        "wsl::das::wsl_world_component_type_id")
        ->args ({ "world", "type_name" });

    addExtern<DAS_BIND_FUN (wsl_world_has_component)> (
        *this, lib, "_world_has_component", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_world_has_component")
        ->args ({ "world", "entity_world", "entity_id", "type_id" });

    addExtern<DAS_BIND_FUN (wsl_world_has_animator)> (
        *this, lib, "_world_has_animator", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_world_has_animator")
        ->args ({ "world", "entity_world", "entity_id" });

    addExtern<DAS_BIND_FUN (wsl_world_add_component)> (
        *this, lib, "_world_add_component", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_world_add_component")
        ->args ({ "world", "entity_world", "entity_id", "type_id", "context",
                  "at" });

    addExtern<DAS_BIND_FUN (wsl_world_remove_component)> (
        *this, lib, "_world_remove_component",
        ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_world_remove_component")
        ->args ({ "world", "entity_world", "entity_id", "type_id", "context",
                  "at" });

    addExtern<DAS_BIND_FUN (wsl_world_defer_destroy)> (
        *this, lib, "_world_defer_destroy", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_world_defer_destroy")
        ->args ({ "world", "entity_world", "entity_id" });

    addExtern<DAS_BIND_FUN (wsl_world_defer_add_component)> (
        *this, lib, "_world_defer_add_component",
        ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_world_defer_add_component")
        ->args ({ "world", "entity_world", "entity_id", "type_id" });

    addExtern<DAS_BIND_FUN (wsl_world_defer_remove_component)> (
        *this, lib, "_world_defer_remove_component",
        ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_world_defer_remove_component")
        ->args ({ "world", "entity_world", "entity_id", "type_id" });

    addExtern<DAS_BIND_FUN (wsl_world_singleton_type_id)> (
        *this, lib, "_world_singleton_type_id",
        ::das::SideEffects::accessExternal,
        "wsl::das::wsl_world_singleton_type_id")
        ->args ({ "world", "type_name" });

    addExtern<DAS_BIND_FUN (wsl_world_has_singleton)> (
        *this, lib, "_world_has_singleton", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_world_has_singleton")
        ->args ({ "world", "type_id" });

    addExtern<DAS_BIND_FUN (wsl_world_add_singleton)> (
        *this, lib, "_world_add_singleton", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_world_add_singleton")
        ->args ({ "world", "type_id" });

    addExtern<DAS_BIND_FUN (wsl_world_remove_singleton)> (
        *this, lib, "_world_remove_singleton",
        ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_world_remove_singleton")
        ->args ({ "world", "type_id" });

    addExtern<DAS_BIND_FUN (wsl_world_singleton_data)> (
        *this, lib, "_world_singleton_data", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_world_singleton_data")
        ->args ({ "world", "type_id" });

    addExtern<DAS_BIND_FUN (wsl_world_component_data)> (
        *this, lib, "_world_component_data", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_world_component_data")
        ->args ({ "world", "entity_world", "entity_id", "type_id" });

    addExtern<DAS_BIND_FUN (wsl_component_default_data)> (
        *this, lib, "_component_default_data",
        ::das::SideEffects::accessExternal,
        "wsl::das::wsl_component_default_data")
        ->args ({ "type_id" });

    addExtern<DAS_BIND_FUN (wsl_world_clone_entity)> (
        *this, lib, "_world_clone_entity", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_world_clone_entity")
        ->args ({ "world", "src_world", "src_id", "parent_world", "parent_id",
                  "context", "at" });

    addExtern<DAS_BIND_FUN (wsl_world_destroy_entity_only)> (
        *this, lib, "_world_destroy_entity_only",
        ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_world_destroy_entity_only")
        ->args ({ "world", "entity_world", "entity_id", "context", "at" });

    addExtern<DAS_BIND_FUN (wsl_world_entity_count)> (
        *this, lib, "_world_entity_count", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_world_entity_count")
        ->arg ("world");

    addExtern<DAS_BIND_FUN (wsl_world_find_all_by_name_count)> (
        *this, lib, "_world_find_all_count", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_world_find_all_by_name_count")
        ->args ({ "world", "name" });

    addExtern<DAS_BIND_FUN (wsl_world_find_all_by_name_at)> (
        *this, lib, "_world_find_all_at", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_world_find_all_by_name_at")
        ->args ({ "world", "name", "index" });

    addExtern<DAS_BIND_FUN (wsl_world_copy_component)> (
        *this, lib, "_world_copy_component", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_world_copy_component")
        ->args ({ "world", "dst_world", "dst_id", "src_world", "src_id",
                  "type_id", "context", "at" });

    addExtern<DAS_BIND_FUN (wsl_world_defer_destroy_only)> (
        *this, lib, "_world_defer_destroy_only",
        ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_world_defer_destroy_only")
        ->args ({ "world", "entity_world", "entity_id" });

    addExtern<DAS_BIND_FUN (wsl_world_defer_set_parent)> (
        *this, lib, "_world_defer_set_parent",
        ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_world_defer_set_parent")
        ->args ({ "world", "child_world", "child_id", "parent_world",
                  "parent_id", "keep_world" });

    addExtern<DAS_BIND_FUN (wsl_world_defer_create_entity)> (
        *this, lib, "_world_defer_create_entity",
        ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_world_defer_create_entity")
        ->args ({ "world", "name" });

    addExtern<DAS_BIND_FUN (wsl_entity_create)> (
        *this, lib, "entity_create", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_entity_create");

    addExtern<DAS_BIND_FUN (wsl_entity_destroy)> (
        *this, lib, "entity_destroy", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_entity_destroy");

    addExtern<DAS_BIND_FUN (wsl_entity_valid)> (
        *this, lib, "entity_valid", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_entity_valid")
        ->arg ("entity");

    addExtern<DAS_BIND_FUN (wsl_entity_is_null)> (
        *this, lib, "entity_is_null", ::das::SideEffects::none,
        "wsl::das::wsl_entity_is_null")
        ->arg ("entity");

    addExtern<DAS_BIND_FUN (wsl_null_entity)> (*this, lib, "null_entity",
                                               ::das::SideEffects::none,
                                               "wsl::das::wsl_null_entity");

    // Scene operations
    addExtern<DAS_BIND_FUN (wsl_find_entity_by_name)> (
        *this, lib, "find_entity_by_name", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_find_entity_by_name")
        ->arg ("name");

    addExtern<DAS_BIND_FUN (wsl_set_entity_name)> (
        *this, lib, "set_entity_name", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_set_entity_name")
        ->args ({ "entity", "name" });

    addExtern<DAS_BIND_FUN (wsl_get_active_camera)> (
        *this, lib, "get_active_camera", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_get_active_camera");

    addExtern<DAS_BIND_FUN (wsl_set_active_camera)> (
        *this, lib, "set_active_camera", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_set_active_camera")
        ->arg ("entity");

    addExtern<DAS_BIND_FUN (wsl_instantiate_prefab)> (
        *this, lib, "instantiate_prefab", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_instantiate_prefab")
        ->arg ("path");

    // Editor viewport
    addExtern<DAS_BIND_FUN (wsl_refresh_editor_viewport)> (
        *this, lib, "refresh_editor_viewport",
        ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_refresh_editor_viewport");

    addExtern<DAS_BIND_FUN (wsl_get_editor_img_min_x)> (
        *this, lib, "get_editor_img_min_x", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_get_editor_img_min_x");
    addExtern<DAS_BIND_FUN (wsl_get_editor_img_min_y)> (
        *this, lib, "get_editor_img_min_y", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_get_editor_img_min_y");
    addExtern<DAS_BIND_FUN (wsl_get_editor_img_size_x)> (
        *this, lib, "get_editor_img_size_x", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_get_editor_img_size_x");
    addExtern<DAS_BIND_FUN (wsl_get_editor_img_size_y)> (
        *this, lib, "get_editor_img_size_y", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_get_editor_img_size_y");

    // Component type ID constants
    addExtern<DAS_BIND_FUN (wsl_type_id_transform)> (
        *this, lib, "TYPE_TRANSFORM", ::das::SideEffects::none,
        "wsl::das::wsl_type_id_transform");

    addExtern<DAS_BIND_FUN (wsl_type_id_camera)> (
        *this, lib, "TYPE_CAMERA", ::das::SideEffects::none,
        "wsl::das::wsl_type_id_camera");

    addExtern<DAS_BIND_FUN (wsl_type_id_hierarchy)> (
        *this, lib, "TYPE_HIERARCHY", ::das::SideEffects::none,
        "wsl::das::wsl_type_id_hierarchy");

    addExtern<DAS_BIND_FUN (wsl_type_id_world_transform)> (
        *this, lib, "TYPE_WORLD_TRANSFORM", ::das::SideEffects::none,
        "wsl::das::wsl_type_id_world_transform");

    addExtern<DAS_BIND_FUN (wsl_type_id_transform_2d)> (
        *this, lib, "TYPE_TRANSFORM_2D", ::das::SideEffects::none,
        "wsl::das::wsl_type_id_transform_2d");

    addExtern<DAS_BIND_FUN (wsl_type_id_camera_2d)> (
        *this, lib, "TYPE_CAMERA_2D", ::das::SideEffects::none,
        "wsl::das::wsl_type_id_camera_2d");

    addExtern<DAS_BIND_FUN (wsl_type_id_sprite_2d)> (
        *this, lib, "TYPE_SPRITE_2D", ::das::SideEffects::none,
        "wsl::das::wsl_type_id_sprite_2d");

    addExtern<DAS_BIND_FUN (wsl_type_id_point_light)> (
        *this, lib, "TYPE_POINT_LIGHT", ::das::SideEffects::none,
        "wsl::das::wsl_type_id_point_light");

    addExtern<DAS_BIND_FUN (wsl_type_id_directional_light)> (
        *this, lib, "TYPE_DIRECTIONAL_LIGHT", ::das::SideEffects::none,
        "wsl::das::wsl_type_id_directional_light");

    addExtern<DAS_BIND_FUN (wsl_type_id_spot_light)> (
        *this, lib, "TYPE_SPOT_LIGHT", ::das::SideEffects::none,
        "wsl::das::wsl_type_id_spot_light");

    addExtern<DAS_BIND_FUN (wsl_type_id_rigid_body)> (
        *this, lib, "TYPE_RIGID_BODY", ::das::SideEffects::none,
        "wsl::das::wsl_type_id_rigid_body");

    addExtern<DAS_BIND_FUN (wsl_type_id_character_body)> (
        *this, lib, "TYPE_CHARACTER_BODY", ::das::SideEffects::none,
        "wsl::das::wsl_type_id_character_body");

    addExtern<DAS_BIND_FUN (wsl_type_id_model_instance_3d)> (
        *this, lib, "TYPE_MODEL_INSTANCE_3D", ::das::SideEffects::none,
        "wsl::das::wsl_type_id_model_instance_3d");

    addExtern<DAS_BIND_FUN (wsl_type_id_area_3d)> (
        *this, lib, "TYPE_AREA_3D", ::das::SideEffects::none,
        "wsl::das::wsl_type_id_area_3d");

    addExtern<DAS_BIND_FUN (wsl_type_id_audio)> (*this, lib, "TYPE_AUDIO",
                                                 ::das::SideEffects::none,
                                                 "wsl::das::wsl_type_id_audio");

    addExtern<DAS_BIND_FUN (wsl_type_id_prefab_instance)> (
        *this, lib, "TYPE_PREFAB_INSTANCE", ::das::SideEffects::none,
        "wsl::das::wsl_type_id_prefab_instance");

    addExtern<DAS_BIND_FUN (wsl_type_id_subviewport)> (
        *this, lib, "TYPE_SUBVIEWPORT", ::das::SideEffects::none,
        "wsl::das::wsl_type_id_subviewport");

    // Keyboard state
    addExtern<DAS_BIND_FUN (wsl_is_key_pressed)> (
        *this, lib, "is_key_pressed", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_is_key_pressed")
        ->arg ("scancode");

    // Named input actions (see the native comments for read semantics).
    // Actions live in the app input map and cover keyboard scancodes and
    // SDL mouse button ids.
    addExtern<DAS_BIND_FUN (wsl_action_bind_key)> (
        *this, lib, "action_bind_key", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_action_bind_key")
        ->args ({ "action", "scancode" });
    addExtern<DAS_BIND_FUN (wsl_action_bind_mouse)> (
        *this, lib, "action_bind_mouse", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_action_bind_mouse")
        ->args ({ "action", "button" });
    addExtern<DAS_BIND_FUN (wsl_action_unbind)> (
        *this, lib, "action_unbind", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_action_unbind")
        ->arg ("action");
    addExtern<DAS_BIND_FUN (wsl_action_pressed)> (
        *this, lib, "action_pressed", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_action_pressed")
        ->arg ("action");
    addExtern<DAS_BIND_FUN (wsl_action_just_pressed)> (
        *this, lib, "action_just_pressed", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_action_just_pressed")
        ->arg ("action");
    addExtern<DAS_BIND_FUN (wsl_action_just_released)> (
        *this, lib, "action_just_released", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_action_just_released")
        ->arg ("action");
    addExtern<DAS_BIND_FUN (wsl_action_axis)> (
        *this, lib, "action_axis", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_action_axis")
        ->args ({ "neg_action", "pos_action" });

    // Camera field access
    addExtern<DAS_BIND_FUN (wsl_get_camera_fov)> (
        *this, lib, "get_camera_fov", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_get_camera_fov")
        ->arg ("camera");
    addExtern<DAS_BIND_FUN (wsl_set_camera_fov)> (
        *this, lib, "set_camera_fov", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_set_camera_fov")
        ->args ({ "camera", "fov" });
    addExtern<DAS_BIND_FUN (wsl_get_camera_near)> (
        *this, lib, "get_camera_near", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_get_camera_near")
        ->arg ("camera");
    addExtern<DAS_BIND_FUN (wsl_set_camera_near)> (
        *this, lib, "set_camera_near", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_set_camera_near")
        ->args ({ "camera", "near_val" });
    addExtern<DAS_BIND_FUN (wsl_get_camera_far)> (
        *this, lib, "get_camera_far", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_get_camera_far")
        ->arg ("camera");
    addExtern<DAS_BIND_FUN (wsl_set_camera_far)> (
        *this, lib, "set_camera_far", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_set_camera_far")
        ->args ({ "camera", "far_val" });
    addExtern<DAS_BIND_FUN (wsl_get_camera_aspect_ratio)> (
        *this, lib, "get_camera_aspect_ratio",
        ::das::SideEffects::accessExternal,
        "wsl::das::wsl_get_camera_aspect_ratio")
        ->arg ("camera");
    addExtern<DAS_BIND_FUN (wsl_set_camera_aspect_ratio)> (
        *this, lib, "set_camera_aspect_ratio",
        ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_set_camera_aspect_ratio")
        ->args ({ "camera", "aspect" });

    // SDL window operations
    addExtern<DAS_BIND_FUN (wsl_set_relative_mouse_mode)> (
        *this, lib, "set_relative_mouse_mode",
        ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_set_relative_mouse_mode")
        ->arg ("enabled");

    // SDL mouse pointer state (see the native comments for read semantics).
    addExtern<DAS_BIND_FUN (wsl_get_mouse_x)> (
        *this, lib, "get_mouse_x", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_get_mouse_x");
    addExtern<DAS_BIND_FUN (wsl_get_mouse_y)> (
        *this, lib, "get_mouse_y", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_get_mouse_y");
    addExtern<DAS_BIND_FUN (wsl_get_mouse_dx)> (
        *this, lib, "get_mouse_dx", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_get_mouse_dx");
    addExtern<DAS_BIND_FUN (wsl_get_mouse_dy)> (
        *this, lib, "get_mouse_dy", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_get_mouse_dy");
    addExtern<DAS_BIND_FUN (wsl_mouse_button_down)> (
        *this, lib, "mouse_button_pressed", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_mouse_button_down")
        ->arg ("button");

    addExtern<DAS_BIND_FUN (wsl_cursor_visible)> (
        *this, lib, "cursor_visible", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_cursor_visible");

    addExtern<DAS_BIND_FUN (wsl_show_cursor)> (
        *this, lib, "show_cursor", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_show_cursor");

    addExtern<DAS_BIND_FUN (wsl_hide_cursor)> (
        *this, lib, "hide_cursor", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_hide_cursor");

    addExtern<DAS_BIND_FUN (wsl_refresh_window_size)> (
        *this, lib, "refresh_window_size", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_refresh_window_size");

    addExtern<DAS_BIND_FUN (wsl_get_window_width)> (
        *this, lib, "get_window_width", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_get_window_width");

    addExtern<DAS_BIND_FUN (wsl_get_window_height)> (
        *this, lib, "get_window_height", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_get_window_height");

    // Component type lookup
    addExtern<DAS_BIND_FUN (wsl_get_component_type_id)> (
        *this, lib, "get_component_type_id", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_get_component_type_id")
        ->arg ("display_name");

    // World-scoped query iteration primitives (back the `query` macros).
    // The registry-local `each_entity_id_with` is gone with the old
    // raw-id query path; use the world-scoped forms instead.

    addExtern<DAS_BIND_FUN (each_world_entity_with)> (
        *this, lib, "each_world_entity_with", ::das::SideEffects::invoke,
        "wsl::das::each_world_entity_with")
        ->args ({ "world", "type_ids", "blk", "context", "at" });

    addExtern<DAS_BIND_FUN (each_world_entity_with_except)> (
        *this, lib, "each_world_entity_with_except", ::das::SideEffects::invoke,
        "wsl::das::each_world_entity_with_except")
        ->args ({ "world", "type_ids", "except_ids", "blk", "context", "at" });

    // Generic component type lookup for Daslang-defined components.
    // (Raw payload access went away with the old raw-id component API;
    // world-scoped access goes through `_world_component_data`.)
    addExtern<DAS_BIND_FUN (wsl_get_component_type_id_by_name)> (
        *this, lib, "_get_component_type_id_by_name",
        ::das::SideEffects::accessExternal,
        "wsl::das::wsl_get_component_type_id_by_name")
        ->arg ("type_name");

    // Raycasting
    addExtern<DAS_BIND_FUN (wsl_make_pick_ray)> (
        *this, lib, "make_pick_ray", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_make_pick_ray")
        ->args ({ "camera_entity", "mouse_x", "mouse_y", "vp_x", "vp_y", "vp_w",
                  "vp_h" });

    addExtern<DAS_BIND_FUN (wsl_get_ray_origin_x)> (
        *this, lib, "get_ray_origin_x", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_get_ray_origin_x");
    addExtern<DAS_BIND_FUN (wsl_get_ray_origin_y)> (
        *this, lib, "get_ray_origin_y", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_get_ray_origin_y");
    addExtern<DAS_BIND_FUN (wsl_get_ray_origin_z)> (
        *this, lib, "get_ray_origin_z", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_get_ray_origin_z");

    addExtern<DAS_BIND_FUN (wsl_get_ray_dir_x)> (
        *this, lib, "get_ray_dir_x", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_get_ray_dir_x");
    addExtern<DAS_BIND_FUN (wsl_get_ray_dir_y)> (
        *this, lib, "get_ray_dir_y", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_get_ray_dir_y");
    addExtern<DAS_BIND_FUN (wsl_get_ray_dir_z)> (
        *this, lib, "get_ray_dir_z", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_get_ray_dir_z");

    addExtern<DAS_BIND_FUN (wsl_ray_plane_intersect)> (
        *this, lib, "ray_plane_intersect", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_ray_plane_intersect")
        ->args ({ "origin_x", "origin_y", "origin_z", "dir_x", "dir_y", "dir_z",
                  "plane_x", "plane_y", "plane_z", "plane_nx", "plane_ny",
                  "plane_nz" });

    addExtern<DAS_BIND_FUN (wsl_get_hit_x)> (*this, lib, "get_hit_x",
                                             ::das::SideEffects::accessExternal,
                                             "wsl::das::wsl_get_hit_x");
    addExtern<DAS_BIND_FUN (wsl_get_hit_y)> (*this, lib, "get_hit_y",
                                             ::das::SideEffects::accessExternal,
                                             "wsl::das::wsl_get_hit_y");
    addExtern<DAS_BIND_FUN (wsl_get_hit_z)> (*this, lib, "get_hit_z",
                                             ::das::SideEffects::accessExternal,
                                             "wsl::das::wsl_get_hit_z");

    // Constants
    ::das::addConstant<uint32_t> (*this, "SDL_BUTTON_LEFT", 1);
    ::das::addConstant<uint32_t> (*this, "SDL_BUTTON_RIGHT", 3);
    ::das::addConstant<int32_t> (*this, "SDL_SCANCODE_ESCAPE", 41);

    // ── Time ──
    addExtern<DAS_BIND_FUN (wsl_get_time)> (*this, lib, "get_time",
                                            ::das::SideEffects::accessExternal,
                                            "wsl::das::wsl_get_time");
    addExtern<DAS_BIND_FUN (wsl_get_elapsed_time)> (
        *this, lib, "get_elapsed_time", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_get_elapsed_time");

    // Frame/time-service state for the `weasel` Time facade. Underscored:
    // gameplay scripts go through `Time` fields and methods instead.
    addExtern<DAS_BIND_FUN (wsl_get_fixed_delta)> (
        *this, lib, "_time_fixed_delta", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_get_fixed_delta");
    addExtern<DAS_BIND_FUN (wsl_get_fixed_elapsed)> (
        *this, lib, "_time_fixed_elapsed", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_get_fixed_elapsed");
    addExtern<DAS_BIND_FUN (wsl_get_frame_index)> (
        *this, lib, "_time_frame_index", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_get_frame_index");
    addExtern<DAS_BIND_FUN (wsl_get_time_scale)> (
        *this, lib, "_time_get_scale", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_get_time_scale");
    addExtern<DAS_BIND_FUN (wsl_set_time_scale)> (
        *this, lib, "_time_set_scale", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_set_time_scale")
        ->arg ("scale");
    addExtern<DAS_BIND_FUN (wsl_is_time_paused)> (
        *this, lib, "_time_is_paused", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_is_time_paused");
    addExtern<DAS_BIND_FUN (wsl_set_time_paused)> (
        *this, lib, "_time_set_paused", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_set_time_paused")
        ->arg ("paused");

    // ── Physics: rigid body ──

    addExtern<DAS_BIND_FUN (wsl_apply_impulse)> (
        *this, lib, "apply_impulse", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_apply_impulse")
        ->args ({ "entity", "x", "y", "z" });
    addExtern<DAS_BIND_FUN (wsl_apply_force)> (
        *this, lib, "apply_force", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_apply_force")
        ->args ({ "entity", "x", "y", "z" });
    addExtern<DAS_BIND_FUN (wsl_set_body_velocity)> (
        *this, lib, "set_body_velocity", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_set_body_velocity")
        ->args ({ "entity", "x", "y", "z" });
    addExtern<DAS_BIND_FUN (wsl_get_body_velocity_x)> (
        *this, lib, "get_body_velocity_x", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_get_body_velocity_x")
        ->arg ("entity");
    addExtern<DAS_BIND_FUN (wsl_get_body_velocity_y)> (
        *this, lib, "get_body_velocity_y", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_get_body_velocity_y")
        ->arg ("entity");
    addExtern<DAS_BIND_FUN (wsl_get_body_velocity_z)> (
        *this, lib, "get_body_velocity_z", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_get_body_velocity_z")
        ->arg ("entity");
    addExtern<DAS_BIND_FUN (wsl_set_body_position)> (
        *this, lib, "set_body_position", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_set_body_position")
        ->args ({ "entity", "x", "y", "z" });
    addExtern<DAS_BIND_FUN (wsl_get_body_position_x)> (
        *this, lib, "get_body_position_x", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_get_body_position_x")
        ->arg ("entity");
    addExtern<DAS_BIND_FUN (wsl_get_body_position_y)> (
        *this, lib, "get_body_position_y", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_get_body_position_y")
        ->arg ("entity");
    addExtern<DAS_BIND_FUN (wsl_get_body_position_z)> (
        *this, lib, "get_body_position_z", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_get_body_position_z")
        ->arg ("entity");

    // ── Audio ──
    addExtern<DAS_BIND_FUN (wsl_audio_play)> (
        *this, lib, "audio_play", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_audio_play")
        ->arg ("entity");
    addExtern<DAS_BIND_FUN (wsl_audio_stop)> (
        *this, lib, "audio_stop", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_audio_stop")
        ->arg ("entity");
    addExtern<DAS_BIND_FUN (wsl_audio_pause)> (
        *this, lib, "audio_pause", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_audio_pause")
        ->arg ("entity");
    addExtern<DAS_BIND_FUN (wsl_audio_resume)> (
        *this, lib, "audio_resume", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_audio_resume")
        ->arg ("entity");
    addExtern<DAS_BIND_FUN (wsl_audio_set_volume)> (
        *this, lib, "audio_set_volume", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_audio_set_volume")
        ->args ({ "entity", "volume" });
    addExtern<DAS_BIND_FUN (wsl_audio_is_playing)> (
        *this, lib, "audio_is_playing", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_audio_is_playing")
        ->arg ("entity");

    // ── Animation ──
    addExtern<DAS_BIND_FUN (wsl_anim_play)> (*this, lib, "anim_play",
                                             ::das::SideEffects::modifyExternal,
                                             "wsl::das::wsl_anim_play")
        ->args ({ "entity", "clip" });
    addExtern<DAS_BIND_FUN (wsl_anim_crossfade)> (
        *this, lib, "anim_crossfade", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_anim_crossfade")
        ->args ({ "entity", "clip", "duration" });
    addExtern<DAS_BIND_FUN (wsl_anim_stop)> (*this, lib, "anim_stop",
                                             ::das::SideEffects::modifyExternal,
                                             "wsl::das::wsl_anim_stop")
        ->arg ("entity");
    addExtern<DAS_BIND_FUN (wsl_anim_set_speed)> (
        *this, lib, "anim_set_speed", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_anim_set_speed")
        ->args ({ "entity", "speed" });
    addExtern<DAS_BIND_FUN (wsl_anim_set_time)> (
        *this, lib, "anim_set_time", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_anim_set_time")
        ->args ({ "entity", "time" });
    addExtern<DAS_BIND_FUN (wsl_anim_set_loop)> (
        *this, lib, "anim_set_loop", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_anim_set_loop")
        ->args ({ "entity", "loop" });
    addExtern<DAS_BIND_FUN (wsl_anim_pause)> (
        *this, lib, "anim_pause", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_anim_pause")
        ->arg ("entity");
    addExtern<DAS_BIND_FUN (wsl_anim_resume)> (
        *this, lib, "anim_resume", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_anim_resume")
        ->arg ("entity");
    addExtern<DAS_BIND_FUN (wsl_anim_is_playing)> (
        *this, lib, "anim_is_playing", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_anim_is_playing")
        ->arg ("entity");
    addExtern<DAS_BIND_FUN (wsl_anim_is_paused)> (
        *this, lib, "anim_is_paused", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_anim_is_paused")
        ->arg ("entity");
    addExtern<DAS_BIND_FUN (wsl_anim_is_finished)> (
        *this, lib, "anim_is_finished", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_anim_is_finished")
        ->arg ("entity");
    addExtern<DAS_BIND_FUN (wsl_anim_get_time)> (
        *this, lib, "anim_get_time", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_anim_get_time")
        ->arg ("entity");
    addExtern<DAS_BIND_FUN (wsl_anim_get_duration)> (
        *this, lib, "anim_get_duration", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_anim_get_duration")
        ->arg ("entity");

    // ── Systems: parallel execution hints ──
    addExtern<DAS_BIND_FUN (wsl_set_system_stage)> (
        *this, lib, "set_system_stage", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_set_system_stage")
        ->arg ("stage");
    addExtern<DAS_BIND_FUN (wsl_set_system_dependency)> (
        *this, lib, "set_system_dependency", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_set_system_dependency")
        ->arg ("name");
    addExtern<DAS_BIND_FUN (wsl_set_system_conflict)> (
        *this, lib, "set_system_conflict", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_set_system_conflict")
        ->arg ("name");

    // ── Event runtime wiring ──
    register_event_message_bindings (*this, lib);

    addExtern<DAS_BIND_FUN (wsl_event_connect)> (
        *this, lib, "event_connect", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_event_connect")
        ->args ({ "event_name", "system_name", "handler_name" });
    addExtern<DAS_BIND_FUN (wsl_event_disconnect)> (
        *this, lib, "event_disconnect", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_event_disconnect")
        ->args ({ "event_name", "system_name", "handler_name" });
    addExtern<DAS_BIND_FUN (wsl_event_connect_current)> (
        *this, lib, "_event_connect_current",
        ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_event_connect_current")
        ->args ({ "event_name", "handler_name" });
    addExtern<DAS_BIND_FUN (wsl_event_disconnect_current)> (
        *this, lib, "_event_disconnect_current",
        ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_event_disconnect_current")
        ->args ({ "event_name", "handler_name" });

    addExtern<DAS_BIND_FUN (wsl_event_declare_source)> (
        *this, lib, "event_declare_source", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_event_declare_source")
        ->args ({ "event_name", "system_name" });
    addInterop<wsl_event_emit, void, const char *, vec4f> (
        *this, lib, "event_emit", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_event_emit")
        ->args ({ "event_name", "payload" });
    addInterop<wsl_event_post, void, const char *, vec4f> (
        *this, lib, "event_post", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_event_post")
        ->args ({ "event_name", "payload" });
    addExtern<DAS_BIND_FUN (wsl_event_declare_sink)> (
        *this, lib, "event_declare_sink", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_event_declare_sink")
        ->args ({ "event_name", "system_name", "handler_name" });
    addExtern<DAS_BIND_FUN (wsl_event_declare_sink_current)> (
        *this, lib, "_event_declare_sink_current",
        ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_event_declare_sink_current")
        ->args ({ "event_name", "handler_name" });

    // ── Model instance ──
    addExtern<DAS_BIND_FUN (wsl_set_model)> (*this, lib, "set_model",
                                             ::das::SideEffects::modifyExternal,
                                             "wsl::das::wsl_set_model")
        ->args ({ "entity", "path" });
    addExtern<DAS_BIND_FUN (wsl_set_model_material_override)> (
        *this, lib, "set_model_material_override",
        ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_set_model_material_override")
        ->args ({ "entity", "path" });
    addExtern<DAS_BIND_FUN (wsl_set_model_visibility_range)> (
        *this, lib, "set_model_visibility_range",
        ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_set_model_visibility_range")
        ->args ({ "entity", "range" });

    // Low-level interop functions (addInterop)
    register_interop_functions (*this);
  }

  virtual ::das::ModuleAotType
  aotRequire (::das::TextWriter &tw) const override
  {
    tw << "#include \"wsl/das/wsl_api_module.hpp\"\n";
    tw << "#include \"wsl/das/wsl_api_component_accessors.hpp\"\n";
    return ::das::ModuleAotType::cpp;
  }
};

} // anonymous namespace

comp::singl::runtime_context *
try_get_runtime_context ()
{
  auto *reg = get_registry ();
  if (!reg) {
    return nullptr;
  }
  if (!reg->ctx ().contains<comp::singl::runtime_context *> ()) {
    return nullptr;
  }
  return reg->ctx ().get<comp::singl::runtime_context *> ();
}

// Accumulated simulated fixed-step time (seconds). Advanced once per
// completed fixed step by the frame-phase driver through
// wsl_api_notify_fixed_step, which the das adapter installs as the
// runtime's fixed-step hook on first init. External linkage: the adapter
// calls it cross-TU (unlike the binding natives, which live in the
// anonymous namespace above and are only addressed in-TU).
namespace
{

std::atomic<double> g_fixed_elapsed{ 0.0 };

} // namespace

void
wsl_api_notify_fixed_step (double fixed_dt)
{
  if (fixed_dt <= 0.0) {
    return;
  }
  double prev = g_fixed_elapsed.load ();
  double next = 0.0;
  do {
    next = prev + fixed_dt;
  } while (!g_fixed_elapsed.compare_exchange_weak (prev, next));
}

float
wsl_get_fixed_elapsed ()
{
  return static_cast<float> (g_fixed_elapsed.load ());
}

static ::das::Module *g_weasel_api = nullptr;

void
register_wsl_api_module (::das::ModuleGroup &module_group)
{
  g_weasel_api = new Module_WeaselApi ();
  module_group.addModule (g_weasel_api);
}

::das::Module *
get_wsl_api_module ()
{
  return g_weasel_api;
}

::das::Module *
create_worker_weasel_api_module ()
{
  return new Module_WeaselApi ();
}

entt::registry *
wsl_api_set_active_registry (entt::registry *registry)
{
  entt::registry *previous = g_registry;
  g_registry = registry;
  return previous;
}

uint64_t
wsl_current_world ()
{
  return static_cast<uint64_t> (reinterpret_cast<uintptr_t> (get_registry ()));
}

bool
wsl_ui_bind_button (const char *element_id, const char *method_name)
{
  auto *registry = get_registry ();
  auto *adapter = das_system_adapter::current ();
  auto *runtime = try_get_runtime_context ();
  if (registry == nullptr || adapter == nullptr || runtime == nullptr
      || element_id == nullptr || method_name == nullptr
      || !adapter->has_noarg_method (method_name)) {
    return false;
  }
  return runtime->ui_manager ().bind_button (*registry, element_id, adapter,
                                             method_name);
}

uint32_t
wsl_world_entity_from_id (uint64_t world, uint32_t entity_id)
{
  auto *registry = registry_from_handle (world);
  if (registry == nullptr || entity_id == NULL_ENTITY_ID
      || !registry->valid (static_cast<entt::entity> (entity_id))) {
    return NULL_ENTITY_ID;
  }
  return entity_id;
}

uint32_t
wsl_world_entity_id (uint64_t world, uint64_t entity_world, uint32_t entity_id)
{
  if (world != entity_world) {
    return NULL_ENTITY_ID;
  }
  return wsl_world_entity_from_id (world, entity_id);
}

uint32_t
wsl_world_create_entity (uint64_t world)
{
  auto *registry = registry_from_handle (world);
  if (registry == nullptr || g_query_depth > 0) {
    return NULL_ENTITY_ID;
  }
  return static_cast<uint32_t> (registry->create ());
}

bool
wsl_world_entity_is_alive (uint64_t world, uint64_t entity_world,
                           uint32_t entity_id)
{
  return wsl_world_entity_id (world, entity_world, entity_id) != NULL_ENTITY_ID;
}

bool
wsl_world_entity_is_null (uint64_t entity_world, uint32_t entity_id)
{
  return entity_world == 0 || entity_id == NULL_ENTITY_ID;
}

bool
wsl_world_destroy_entity (uint64_t world, uint64_t entity_world,
                          uint32_t entity_id, ::das::Context *context,
                          ::das::LineInfoArg *at)
{
  if (g_query_depth > 0) {
    if (context != nullptr) {
      context->throw_error_at (
          at, "cannot destroy an entity during a query; use a command buffer");
    }
    return false;
  }
  auto *registry = registry_from_handle (world);
  const uint32_t id = wsl_world_entity_id (world, entity_world, entity_id);
  if (registry == nullptr || id == NULL_ENTITY_ID) {
    return false;
  }
  const auto target = static_cast<entt::entity> (id);
  if (registry->ctx ().contains<rsc::scene *> ()) {
    if (auto *scene = registry->ctx ().get<rsc::scene *> ()) {
      scene->destroy_entity_subtree (target);
      return true;
    }
  }

  // A registry used by isolated script execution may not have a scene
  // context. Preserve the normal subtree-destruction behavior there too.
  if (const auto *root_hierarchy = registry->try_get<comp::hierarchy> (target);
      root_hierarchy != nullptr && root_hierarchy->parent != entt::null
      && registry->valid (root_hierarchy->parent)) {
    auto *parent_hierarchy
        = registry->try_get<comp::hierarchy> (root_hierarchy->parent);
    if (parent_hierarchy != nullptr) {
      auto sibling = parent_hierarchy->first;
      entt::entity previous = entt::null;
      const auto max_steps = registry->storage<entt::entity> ().size () + 1;
      for (std::size_t step = 0;
           sibling != entt::null && registry->valid (sibling)
           && step < max_steps;
           ++step) {
        const auto *sibling_hierarchy
            = registry->try_get<comp::hierarchy> (sibling);
        const auto next = sibling_hierarchy != nullptr ? sibling_hierarchy->next
                                                       : entt::null;
        if (sibling == target) {
          if (previous == entt::null) {
            parent_hierarchy->first = next;
          } else if (auto *previous_hierarchy
                     = registry->try_get<comp::hierarchy> (previous)) {
            previous_hierarchy->next = next;
          }
          break;
        }
        previous = sibling;
        sibling = next;
      }
    }
  }

  std::vector<entt::entity> pending{ target };
  std::vector<entt::entity> subtree;
  std::unordered_set<uint32_t> visited;
  const auto max_steps = registry->storage<entt::entity> ().size () + 1;
  while (!pending.empty ()) {
    const auto current = pending.back ();
    pending.pop_back ();
    if (!registry->valid (current)
        || !visited.insert (static_cast<uint32_t> (current)).second) {
      continue;
    }
    subtree.push_back (current);
    const auto *hierarchy = registry->try_get<comp::hierarchy> (current);
    if (hierarchy == nullptr) {
      continue;
    }
    auto child = hierarchy->first;
    for (std::size_t step = 0;
         child != entt::null && registry->valid (child) && step < max_steps;
         ++step) {
      pending.push_back (child);
      const auto *child_hierarchy = registry->try_get<comp::hierarchy> (child);
      child = child_hierarchy != nullptr ? child_hierarchy->next : entt::null;
    }
  }
  for (auto it = subtree.rbegin (); it != subtree.rend (); ++it) {
    if (registry->valid (*it)) {
      registry->destroy (*it);
    }
  }
  return true;
}

uint32_t
wsl_world_find_entity_by_name (uint64_t world, const char *name)
{
  auto *registry = registry_from_handle (world);
  if (registry == nullptr || name == nullptr
      || !registry->ctx ().contains<rsc::scene *> ()) {
    return NULL_ENTITY_ID;
  }
  auto *scene = registry->ctx ().get<rsc::scene *> ();
  if (scene == nullptr) {
    return NULL_ENTITY_ID;
  }
  for (const auto &[entity, entity_name] : scene->get_entity_names ()) {
    if (entity_name == name && registry->valid (entity)) {
      return static_cast<uint32_t> (entity);
    }
  }
  return NULL_ENTITY_ID;
}

bool
wsl_world_set_entity_name (uint64_t world, uint64_t entity_world,
                           uint32_t entity_id, const char *name)
{
  const uint32_t id = wsl_world_entity_id (world, entity_world, entity_id);
  auto *registry = registry_from_handle (world);
  if (registry == nullptr || id == NULL_ENTITY_ID || name == nullptr
      || !registry->ctx ().contains<rsc::scene *> ()) {
    return false;
  }
  auto *scene = registry->ctx ().get<rsc::scene *> ();
  if (scene == nullptr) {
    return false;
  }
  scene->set_entity_name (static_cast<entt::entity> (id), name);
  return true;
}

namespace
{

/** Local pose of an entity, or identity when it has no 3D transform. */
glm::mat4
local_pose_matrix (entt::registry &registry, entt::entity entity)
{
  if (const auto *t = registry.try_get<comp::transform> (entity)) {
    return t->model ();
  }
  return glm::mat4 (1.0F);
}

/**
 * World pose of an entity, chained on demand from local transforms.
 * Computed fresh instead of reading the cached `world_transform` component,
 * so it reflects edits made earlier in the same frame.
 */
glm::mat4
world_pose_matrix (entt::registry &registry, entt::entity entity)
{
  std::vector<entt::entity> chain;
  const auto max_steps = registry.storage<entt::entity> ().size () + 1;
  for (auto current = entity; current != entt::null && registry.valid (current)
                              && chain.size () <= max_steps;) {
    chain.push_back (current);
    const auto *hierarchy = registry.try_get<comp::hierarchy> (current);
    current = hierarchy != nullptr ? hierarchy->parent : entt::null;
  }
  glm::mat4 world (1.0F);
  for (auto it = chain.rbegin (); it != chain.rend (); ++it) {
    world = world * local_pose_matrix (registry, *it);
  }
  return world;
}

/**
 * Rewrites an entity's local 3D transform so its world pose stays `world`
 * under its current parent. Returns false when the entity has no 3D
 * transform to preserve or the pose is degenerate (best effort: callers keep
 * the new linkage regardless). 2D-only hierarchies are not adjusted.
 */
bool
preserve_world_pose (entt::registry &registry, entt::entity entity,
                     const glm::mat4 &world)
{
  auto *t = registry.try_get<comp::transform> (entity);
  if (t == nullptr) {
    return false;
  }
  glm::mat4 parent_world (1.0F);
  if (const auto *hierarchy = registry.try_get<comp::hierarchy> (entity);
      hierarchy != nullptr && hierarchy->parent != entt::null
      && registry.valid (hierarchy->parent)) {
    parent_world = world_pose_matrix (registry, hierarchy->parent);
  }
  if (std::fabs (glm::determinant (parent_world)) < 1e-12F) {
    return false;
  }
  const glm::mat4 local = glm::inverse (parent_world) * world;
  if (std::fabs (glm::determinant (local)) < 1e-12F) {
    return false;
  }
  glm::vec3 scale, translation, skew;
  glm::vec4 perspective;
  glm::quat rotation;
  if (!glm::decompose (local, scale, rotation, translation, skew,
                       perspective)) {
    return false;
  }
  t->position = math::vec3f{ translation };
  t->rotation = math::quatf{ rotation };
  t->scale = math::vec3f{ scale };
  return true;
}

} // namespace

bool
wsl_world_set_parent (uint64_t world, uint64_t child_world, uint32_t child_id,
                      uint64_t parent_world, uint32_t parent_id,
                      bool keep_world, ::das::Context *context,
                      ::das::LineInfoArg *at)
{
  if (g_query_depth > 0) {
    if (context != nullptr) {
      context->throw_error_at (
          at, "cannot change hierarchy during a query; defer the operation");
    }
    return false;
  }

  auto *registry = registry_from_handle (world);
  const uint32_t valid_child
      = wsl_world_entity_id (world, child_world, child_id);
  const bool no_parent = parent_world == 0 && parent_id == NULL_ENTITY_ID;
  const uint32_t valid_parent
      = no_parent ? NULL_ENTITY_ID
                  : wsl_world_entity_id (world, parent_world, parent_id);
  if (registry == nullptr || valid_child == NULL_ENTITY_ID
      || (!no_parent && valid_parent == NULL_ENTITY_ID)) {
    return false;
  }

  const auto child = static_cast<entt::entity> (valid_child);
  const auto parent
      = no_parent ? entt::null : static_cast<entt::entity> (valid_parent);
  if (child == parent) {
    return false;
  }

  // Reject cycles before touching either linked list. The guard also makes a
  // pre-existing malformed cycle fail closed instead of looping forever.
  auto ancestor = parent;
  const auto max_steps = registry->storage<entt::entity> ().size () + 1;
  for (std::size_t step = 0; ancestor != entt::null && step < max_steps;
       ++step) {
    if (ancestor == child) {
      return false;
    }
    const auto *ancestor_hierarchy
        = registry->try_get<comp::hierarchy> (ancestor);
    ancestor = ancestor_hierarchy != nullptr ? ancestor_hierarchy->parent
                                             : entt::null;
  }
  if (ancestor != entt::null) {
    return false;
  }

  auto &child_hierarchy = registry->get_or_emplace<comp::hierarchy> (child);
  const auto old_parent = child_hierarchy.parent;
  if (old_parent == parent) {
    return true;
  }

  // Capture the world pose before touching the linkage so it can be
  // re-applied under the new parent afterwards.
  glm::mat4 previous_world (1.0F);
  const bool preserve
      = keep_world && registry->try_get<comp::transform> (child) != nullptr;
  if (preserve) {
    previous_world = world_pose_matrix (*registry, child);
  }

  if (old_parent != entt::null && registry->valid (old_parent)) {
    auto *old_parent_hierarchy
        = registry->try_get<comp::hierarchy> (old_parent);
    if (old_parent_hierarchy != nullptr) {
      auto sibling = old_parent_hierarchy->first;
      entt::entity previous = entt::null;
      for (std::size_t step = 0;
           sibling != entt::null && registry->valid (sibling)
           && step < max_steps;
           ++step) {
        auto *sibling_hierarchy = registry->try_get<comp::hierarchy> (sibling);
        const auto next = sibling_hierarchy != nullptr ? sibling_hierarchy->next
                                                       : entt::null;
        if (sibling == child) {
          if (previous == entt::null) {
            old_parent_hierarchy->first = next;
          } else if (auto *previous_hierarchy
                     = registry->try_get<comp::hierarchy> (previous)) {
            previous_hierarchy->next = next;
          }
          break;
        }
        previous = sibling;
        sibling = next;
      }
    }
  }

  registry->get<comp::hierarchy> (child).parent = entt::null;
  registry->get<comp::hierarchy> (child).next = entt::null;
  if (parent != entt::null) {
    auto &parent_hierarchy = registry->get_or_emplace<comp::hierarchy> (parent);
    auto &updated_child_hierarchy = registry->get<comp::hierarchy> (child);
    updated_child_hierarchy.parent = parent;
    updated_child_hierarchy.next = parent_hierarchy.first;
    parent_hierarchy.first = child;
  }
  if (preserve) {
    preserve_world_pose (*registry, child, previous_world);
  }
  return true;
}

uint32_t
wsl_world_get_parent (uint64_t world, uint64_t entity_world, uint32_t entity_id)
{
  auto *registry = registry_from_handle (world);
  const uint32_t id = wsl_world_entity_id (world, entity_world, entity_id);
  if (registry == nullptr || id == NULL_ENTITY_ID) {
    return NULL_ENTITY_ID;
  }
  const auto *hierarchy
      = registry->try_get<comp::hierarchy> (static_cast<entt::entity> (id));
  if (hierarchy == nullptr || hierarchy->parent == entt::null
      || !registry->valid (hierarchy->parent)) {
    return NULL_ENTITY_ID;
  }
  return static_cast<uint32_t> (hierarchy->parent);
}

uint32_t
wsl_world_get_child_count (uint64_t world, uint64_t entity_world,
                           uint32_t entity_id)
{
  auto *registry = registry_from_handle (world);
  const uint32_t id = wsl_world_entity_id (world, entity_world, entity_id);
  if (registry == nullptr || id == NULL_ENTITY_ID) {
    return 0;
  }
  const auto *hierarchy
      = registry->try_get<comp::hierarchy> (static_cast<entt::entity> (id));
  if (hierarchy == nullptr) {
    return 0;
  }
  uint32_t count = 0;
  auto child = hierarchy->first;
  const auto max_steps = registry->storage<entt::entity> ().size () + 1;
  while (child != entt::null && registry->valid (child) && count < max_steps) {
    ++count;
    const auto *child_hierarchy = registry->try_get<comp::hierarchy> (child);
    child = child_hierarchy != nullptr ? child_hierarchy->next : entt::null;
  }
  return count;
}

uint32_t
wsl_world_get_child (uint64_t world, uint64_t entity_world, uint32_t entity_id,
                     uint32_t index)
{
  auto *registry = registry_from_handle (world);
  const uint32_t id = wsl_world_entity_id (world, entity_world, entity_id);
  if (registry == nullptr || id == NULL_ENTITY_ID) {
    return NULL_ENTITY_ID;
  }
  const auto *hierarchy
      = registry->try_get<comp::hierarchy> (static_cast<entt::entity> (id));
  if (hierarchy == nullptr) {
    return NULL_ENTITY_ID;
  }
  auto child = hierarchy->first;
  const auto max_steps = registry->storage<entt::entity> ().size () + 1;
  for (uint32_t offset = 0; child != entt::null && offset < max_steps;
       ++offset) {
    if (!registry->valid (child)) {
      return NULL_ENTITY_ID;
    }
    if (offset == index) {
      return static_cast<uint32_t> (child);
    }
    const auto *child_hierarchy = registry->try_get<comp::hierarchy> (child);
    child = child_hierarchy != nullptr ? child_hierarchy->next : entt::null;
  }
  return NULL_ENTITY_ID;
}

bool
wsl_world_change_scene (uint64_t world, const char *scene_name)
{
  auto *registry = registry_from_handle (world);
  if (registry == nullptr || scene_name == nullptr
      || !registry->ctx ().contains<comp::singl::runtime_context *> ()) {
    return false;
  }
  auto *runtime_ctx = registry->ctx ().get<comp::singl::runtime_context *> ();
  if (runtime_ctx == nullptr) {
    return false;
  }
  auto &manager = runtime_ctx->scene_manager ();
  for (const auto &scene : manager.get_world ().get_scenes ()) {
    if (scene != nullptr && scene->get_name () == scene_name) {
      manager.set_active (scene.get ());
      return true;
    }
  }
  return false;
}

uint32_t
wsl_world_component_type_id (uint64_t world, const char *type_name)
{
  auto *registry = registry_from_handle (world);
  if (registry == nullptr || type_name == nullptr
      || !registry->ctx ().contains<comp::singl::runtime_context *> ()) {
    return 0;
  }
  auto *runtime_ctx = registry->ctx ().get<comp::singl::runtime_context *> ();
  if (runtime_ctx == nullptr) {
    return 0;
  }
  const auto *info
      = runtime_ctx->component_registry ().find_component_type_info (type_name);
  return info != nullptr ? static_cast<uint32_t> (info->type_id) : 0;
}

bool
wsl_world_has_component (uint64_t world, uint64_t entity_world,
                         uint32_t entity_id, uint32_t type_id)
{
  auto *registry = registry_from_handle (world);
  const uint32_t id = wsl_world_entity_id (world, entity_world, entity_id);
  if (registry == nullptr || id == NULL_ENTITY_ID || type_id == 0) {
    return false;
  }
  const auto target = static_cast<entt::entity> (id);
  if (const auto *storage = registry->storage (type_id);
      storage != nullptr && storage->contains (target)) {
    return true;
  }
  if (!registry->ctx ().contains<comp::singl::runtime_context *> ()) {
    return false;
  }
  auto *runtime_ctx = registry->ctx ().get<comp::singl::runtime_context *> ();
  return runtime_ctx != nullptr
         && runtime_ctx->component_registry ().das_component_contains (
             *registry, type_id, target);
}

bool
wsl_world_has_animator (uint64_t world, uint64_t entity_world,
                        uint32_t entity_id)
{
  auto *registry = registry_from_handle (world);
  const uint32_t id = wsl_world_entity_id (world, entity_world, entity_id);
  return registry != nullptr && id != NULL_ENTITY_ID
         && registry->all_of<comp::animator> (static_cast<entt::entity> (id));
}

bool
wsl_world_add_component (uint64_t world, uint64_t entity_world,
                         uint32_t entity_id, uint32_t type_id,
                         ::das::Context *context, ::das::LineInfoArg *at)
{
  if (g_query_depth > 0) {
    if (context != nullptr) {
      context->throw_error_at (
          at, "cannot add a component during a query; use a command buffer");
    }
    return false;
  }
  auto *registry = registry_from_handle (world);
  const uint32_t id = wsl_world_entity_id (world, entity_world, entity_id);
  if (registry == nullptr || id == NULL_ENTITY_ID || type_id == 0) {
    return false;
  }
  const auto target = static_cast<entt::entity> (id);
  auto *runtime_ctx
      = registry->ctx ().contains<comp::singl::runtime_context *> ()
            ? registry->ctx ().get<comp::singl::runtime_context *> ()
            : nullptr;
  if (runtime_ctx != nullptr) {
    auto &components = runtime_ctx->component_registry ();
    if (const auto *desc = components.find_world_component (type_id);
        desc != nullptr) {
      if (desc->is_das_component) {
        return components.das_component_add (*registry, type_id, target);
      }
      if (desc->emplace_default != nullptr) {
        return desc->emplace_default (*registry, target);
      }
    }
  }

#define WSL_DAS_ADD_BUILTIN(Type, TypeIdFn)                                    \
  if (type_id == TypeIdFn ()) {                                                \
    if (registry->all_of<Type> (target)) {                                     \
      return false;                                                            \
    }                                                                          \
    registry->emplace<Type> (target);                                          \
    return true;                                                               \
  }
  WSL_DAS_ADD_BUILTIN (comp::transform, wsl_type_id_transform)
  WSL_DAS_ADD_BUILTIN (comp::transform_2d, wsl_type_id_transform_2d)
  WSL_DAS_ADD_BUILTIN (comp::camera, wsl_type_id_camera)
  WSL_DAS_ADD_BUILTIN (comp::camera_2d, wsl_type_id_camera_2d)
  WSL_DAS_ADD_BUILTIN (comp::sprite_2d, wsl_type_id_sprite_2d)
  WSL_DAS_ADD_BUILTIN (comp::hierarchy, wsl_type_id_hierarchy)
  WSL_DAS_ADD_BUILTIN (comp::world_transform, wsl_type_id_world_transform)
  WSL_DAS_ADD_BUILTIN (comp::point_light, wsl_type_id_point_light)
  WSL_DAS_ADD_BUILTIN (comp::directional_light, wsl_type_id_directional_light)
  WSL_DAS_ADD_BUILTIN (comp::spot_light, wsl_type_id_spot_light)
  WSL_DAS_ADD_BUILTIN (comp::rigid_body, wsl_type_id_rigid_body)
  WSL_DAS_ADD_BUILTIN (comp::character_body, wsl_type_id_character_body)
  WSL_DAS_ADD_BUILTIN (comp::model_instance_3d, wsl_type_id_model_instance_3d)
  WSL_DAS_ADD_BUILTIN (comp::area, wsl_type_id_area_3d)
  WSL_DAS_ADD_BUILTIN (comp::audio, wsl_type_id_audio)
  WSL_DAS_ADD_BUILTIN (comp::prefab_instance, wsl_type_id_prefab_instance)
  WSL_DAS_ADD_BUILTIN (comp::subviewport, wsl_type_id_subviewport)
#undef WSL_DAS_ADD_BUILTIN
  return false;
}

bool
wsl_world_remove_component (uint64_t world, uint64_t entity_world,
                            uint32_t entity_id, uint32_t type_id,
                            ::das::Context *context, ::das::LineInfoArg *at)
{
  if (g_query_depth > 0) {
    if (context != nullptr) {
      context->throw_error_at (at, "cannot remove a component during a query; "
                                   "use a command buffer");
    }
    return false;
  }
  auto *registry = registry_from_handle (world);
  const uint32_t id = wsl_world_entity_id (world, entity_world, entity_id);
  if (registry == nullptr || id == NULL_ENTITY_ID || type_id == 0) {
    return false;
  }
  const auto target = static_cast<entt::entity> (id);
  auto *runtime_ctx
      = registry->ctx ().contains<comp::singl::runtime_context *> ()
            ? registry->ctx ().get<comp::singl::runtime_context *> ()
            : nullptr;
  if (runtime_ctx != nullptr) {
    auto &components = runtime_ctx->component_registry ();
    if (const auto *desc = components.find_world_component (type_id);
        desc != nullptr) {
      if (desc->is_das_component) {
        return components.das_component_remove (*registry, type_id, target);
      }
      if (desc->remove != nullptr) {
        return desc->remove (*registry, target);
      }
    }
  }

#define WSL_DAS_REMOVE_BUILTIN(Type, TypeIdFn)                                 \
  if (type_id == TypeIdFn ()) {                                                \
    if (!registry->all_of<Type> (target)) {                                    \
      return false;                                                            \
    }                                                                          \
    registry->remove<Type> (target);                                           \
    return true;                                                               \
  }
  WSL_DAS_REMOVE_BUILTIN (comp::transform, wsl_type_id_transform)
  WSL_DAS_REMOVE_BUILTIN (comp::transform_2d, wsl_type_id_transform_2d)
  WSL_DAS_REMOVE_BUILTIN (comp::camera, wsl_type_id_camera)
  WSL_DAS_REMOVE_BUILTIN (comp::camera_2d, wsl_type_id_camera_2d)
  WSL_DAS_REMOVE_BUILTIN (comp::sprite_2d, wsl_type_id_sprite_2d)
  WSL_DAS_REMOVE_BUILTIN (comp::hierarchy, wsl_type_id_hierarchy)
  WSL_DAS_REMOVE_BUILTIN (comp::world_transform, wsl_type_id_world_transform)
  WSL_DAS_REMOVE_BUILTIN (comp::point_light, wsl_type_id_point_light)
  WSL_DAS_REMOVE_BUILTIN (comp::directional_light,
                          wsl_type_id_directional_light)
  WSL_DAS_REMOVE_BUILTIN (comp::spot_light, wsl_type_id_spot_light)
  WSL_DAS_REMOVE_BUILTIN (comp::rigid_body, wsl_type_id_rigid_body)
  WSL_DAS_REMOVE_BUILTIN (comp::character_body, wsl_type_id_character_body)
  WSL_DAS_REMOVE_BUILTIN (comp::model_instance_3d,
                          wsl_type_id_model_instance_3d)
  WSL_DAS_REMOVE_BUILTIN (comp::area, wsl_type_id_area_3d)
  WSL_DAS_REMOVE_BUILTIN (comp::audio, wsl_type_id_audio)
  WSL_DAS_REMOVE_BUILTIN (comp::prefab_instance, wsl_type_id_prefab_instance)
  WSL_DAS_REMOVE_BUILTIN (comp::subviewport, wsl_type_id_subviewport)
#undef WSL_DAS_REMOVE_BUILTIN
  return false;
}

bool
wsl_world_defer_destroy (uint64_t world, uint64_t entity_world,
                         uint32_t entity_id)
{
  if (!wsl_world_entity_is_alive (world, entity_world, entity_id)) {
    return false;
  }
  if (g_query_depth == 0) {
    return wsl_world_destroy_entity (world, entity_world, entity_id, nullptr,
                                     nullptr);
  }
  g_deferred_commands.push_back (
      { deferred_operation::destroy_entity, world, entity_world, entity_id });
  return true;
}

bool
wsl_world_defer_add_component (uint64_t world, uint64_t entity_world,
                               uint32_t entity_id, uint32_t type_id)
{
  if (!wsl_world_entity_is_alive (world, entity_world, entity_id)
      || type_id == 0) {
    return false;
  }
  if (g_query_depth == 0) {
    return wsl_world_add_component (world, entity_world, entity_id, type_id,
                                    nullptr, nullptr);
  }
  g_deferred_commands.push_back ({ deferred_operation::add_component, world,
                                   entity_world, entity_id, type_id });
  return true;
}

bool
wsl_world_defer_remove_component (uint64_t world, uint64_t entity_world,
                                  uint32_t entity_id, uint32_t type_id)
{
  if (!wsl_world_entity_is_alive (world, entity_world, entity_id)
      || type_id == 0) {
    return false;
  }
  if (g_query_depth == 0) {
    return wsl_world_remove_component (world, entity_world, entity_id, type_id,
                                       nullptr, nullptr);
  }
  g_deferred_commands.push_back ({ deferred_operation::remove_component, world,
                                   entity_world, entity_id, type_id });
  return true;
}

namespace
{

/** Copies the non-hierarchy components of `src` onto a fresh entity. */
entt::entity
clone_entity_recursive (entt::registry &registry, entt::entity src,
                        entt::entity parent)
{
  const entt::entity dst = registry.create ();

  auto *runtime_ctx
      = registry.ctx ().contains<comp::singl::runtime_context *> ()
            ? registry.ctx ().get<comp::singl::runtime_context *> ()
            : nullptr;

  if (runtime_ctx != nullptr) {
    auto &components = runtime_ctx->component_registry ();
    for (const auto *desc : components.get_world_components (
             reg::world_component_order::type_id)) {
      if (desc == nullptr
          || desc->type_id == entt::type_hash<comp::hierarchy>::value ()) {
        continue;
      }
      if (desc->is_das_component) {
        if (!components.das_component_contains (registry, desc->type_id, src)) {
          continue;
        }
        if (!components.das_component_contains (registry, desc->type_id, dst)
            && !components.das_component_add (registry, desc->type_id, dst)) {
          continue;
        }
        uint8_t *dst_data
            = components.das_component_data (registry, desc->type_id, dst);
        const uint8_t *src_data
            = components.das_component_data (registry, desc->type_id, src);
        if (dst_data != nullptr && src_data != nullptr
            && desc->das_struct_size > 0) {
          std::memcpy (dst_data, src_data,
                       static_cast<std::size_t> (desc->das_struct_size));
        }
      } else if (desc->copy != nullptr) {
        desc->copy (registry, src, registry, dst);
      }
    }
  }

  // Builtins that are not covered by a registered descriptor still copy by
  // value. Hierarchy is deliberately excluded: links are rebuilt below.
#define WSL_DAS_COPY_BUILTIN(Type)                                             \
  if (!registry.all_of<Type> (dst) && registry.all_of<Type> (src)) {           \
    registry.emplace_or_replace<Type> (dst, registry.get<Type> (src));         \
  }
  WSL_DAS_COPY_BUILTIN (comp::transform)
  WSL_DAS_COPY_BUILTIN (comp::transform_2d)
  WSL_DAS_COPY_BUILTIN (comp::camera)
  WSL_DAS_COPY_BUILTIN (comp::camera_2d)
  WSL_DAS_COPY_BUILTIN (comp::sprite_2d)
  WSL_DAS_COPY_BUILTIN (comp::world_transform)
  WSL_DAS_COPY_BUILTIN (comp::point_light)
  WSL_DAS_COPY_BUILTIN (comp::directional_light)
  WSL_DAS_COPY_BUILTIN (comp::spot_light)
  WSL_DAS_COPY_BUILTIN (comp::rigid_body)
  // character_body is move-only (owns its physics body); cloning it needs
  // backend body re-creation and is left out until that path exists.
  WSL_DAS_COPY_BUILTIN (comp::model_instance_3d)
  WSL_DAS_COPY_BUILTIN (comp::area)
  WSL_DAS_COPY_BUILTIN (comp::audio)
  WSL_DAS_COPY_BUILTIN (comp::prefab_instance)
  WSL_DAS_COPY_BUILTIN (comp::subviewport)
#undef WSL_DAS_COPY_BUILTIN

  if (parent != entt::null && registry.valid (parent)) {
    auto &dst_hierarchy = registry.get_or_emplace<comp::hierarchy> (dst);
    auto &parent_hierarchy = registry.get_or_emplace<comp::hierarchy> (parent);
    dst_hierarchy.parent = parent;
    dst_hierarchy.next = parent_hierarchy.first;
    parent_hierarchy.first = dst;
  }

  const auto *src_hierarchy = registry.try_get<comp::hierarchy> (src);
  if (src_hierarchy != nullptr) {
    std::vector<entt::entity> children;
    auto child = src_hierarchy->first;
    const auto max_steps = registry.storage<entt::entity> ().size () + 1;
    for (std::size_t step = 0;
         child != entt::null && registry.valid (child) && step < max_steps;
         ++step) {
      children.push_back (child);
      const auto *child_hierarchy = registry.try_get<comp::hierarchy> (child);
      child = child_hierarchy != nullptr ? child_hierarchy->next : entt::null;
    }
    for (const auto c : children) {
      clone_entity_recursive (registry, c, dst);
    }
  }

  return dst;
}

/** Removes `child` from its current parent's child list, if any. */
void
unlink_from_parent (entt::registry &registry, entt::entity child)
{
  const auto *child_hierarchy = registry.try_get<comp::hierarchy> (child);
  if (child_hierarchy == nullptr || child_hierarchy->parent == entt::null
      || !registry.valid (child_hierarchy->parent)) {
    return;
  }
  auto *parent_hierarchy
      = registry.try_get<comp::hierarchy> (child_hierarchy->parent);
  if (parent_hierarchy == nullptr) {
    return;
  }
  auto sibling = parent_hierarchy->first;
  entt::entity previous = entt::null;
  const auto max_steps = registry.storage<entt::entity> ().size () + 1;
  for (std::size_t step = 0;
       sibling != entt::null && registry.valid (sibling) && step < max_steps;
       ++step) {
    auto *sibling_hierarchy = registry.try_get<comp::hierarchy> (sibling);
    const auto next
        = sibling_hierarchy != nullptr ? sibling_hierarchy->next : entt::null;
    if (sibling == child) {
      if (previous == entt::null) {
        parent_hierarchy->first = next;
      } else if (auto *previous_hierarchy
                 = registry.try_get<comp::hierarchy> (previous)) {
        previous_hierarchy->next = next;
      }
      break;
    }
    previous = sibling;
    sibling = next;
  }
}

} // namespace

uint32_t
wsl_world_clone_entity (uint64_t world, uint64_t src_world, uint32_t src_id,
                        uint64_t parent_world, uint32_t parent_id,
                        ::das::Context *context, ::das::LineInfoArg *at)
{
  if (g_query_depth > 0) {
    if (context != nullptr) {
      context->throw_error_at (
          at, "cannot clone an entity during a query; use a command buffer");
    }
    return NULL_ENTITY_ID;
  }
  auto *registry = registry_from_handle (world);
  const uint32_t valid_src = wsl_world_entity_id (world, src_world, src_id);
  const bool no_parent = parent_world == 0 && parent_id == NULL_ENTITY_ID;
  const uint32_t valid_parent
      = no_parent ? NULL_ENTITY_ID
                  : wsl_world_entity_id (world, parent_world, parent_id);
  if (registry == nullptr || valid_src == NULL_ENTITY_ID
      || (!no_parent && valid_parent == NULL_ENTITY_ID)) {
    return NULL_ENTITY_ID;
  }
  const auto src = static_cast<entt::entity> (valid_src);
  const auto parent
      = no_parent ? entt::null : static_cast<entt::entity> (valid_parent);
  if (registry->ctx ().contains<rsc::scene *> ()) {
    if (auto *scene = registry->ctx ().get<rsc::scene *> ()) {
      const auto cloned = scene->copy_entity (*scene, src, parent);
      if (parent != entt::null) {
        // Cloning preserves local transforms verbatim (Unity-style
        // Instantiate semantics), so the fix-up link keeps them as copied.
        wsl_world_set_parent (world, world, static_cast<uint32_t> (cloned),
                              world, valid_parent, false, nullptr, nullptr);
      }
      return static_cast<uint32_t> (cloned);
    }
  }
  return static_cast<uint32_t> (
      clone_entity_recursive (*registry, src, parent));
}

bool
wsl_world_destroy_entity_only (uint64_t world, uint64_t entity_world,
                               uint32_t entity_id, ::das::Context *context,
                               ::das::LineInfoArg *at)
{
  if (g_query_depth > 0) {
    if (context != nullptr) {
      context->throw_error_at (
          at, "cannot destroy an entity during a query; use a command buffer");
    }
    return false;
  }
  auto *registry = registry_from_handle (world);
  const uint32_t id = wsl_world_entity_id (world, entity_world, entity_id);
  if (registry == nullptr || id == NULL_ENTITY_ID) {
    return false;
  }
  const auto target = static_cast<entt::entity> (id);

  // Children keep their world pose: they are re-attached to the destroyed
  // entity's parent (or detached to the root when there is none) with local
  // transforms recomputed from their captured world poses. Poses are captured
  // before the target is unlinked, while the ancestry chain is still intact.
  entt::entity new_parent = entt::null;
  std::vector<std::pair<entt::entity, glm::mat4>> reattach;
  if (const auto *hierarchy = registry->try_get<comp::hierarchy> (target)) {
    if (hierarchy->parent != entt::null
        && registry->valid (hierarchy->parent)) {
      new_parent = hierarchy->parent;
    }
    auto child = hierarchy->first;
    const auto max_steps = registry->storage<entt::entity> ().size () + 1;
    for (std::size_t step = 0;
         child != entt::null && registry->valid (child) && step < max_steps;
         ++step) {
      reattach.emplace_back (child, world_pose_matrix (*registry, child));
      const auto *child_hierarchy = registry->try_get<comp::hierarchy> (child);
      child = child_hierarchy != nullptr ? child_hierarchy->next : entt::null;
    }
  }

  unlink_from_parent (*registry, target);

  for (const auto &[child, previous_world] : reattach) {
    if (!registry->valid (child)) {
      continue;
    }
    auto &child_hierarchy = registry->get_or_emplace<comp::hierarchy> (child);
    child_hierarchy.parent = new_parent;
    child_hierarchy.next = entt::null;
    if (new_parent != entt::null) {
      auto &parent_hierarchy
          = registry->get_or_emplace<comp::hierarchy> (new_parent);
      child_hierarchy.next = parent_hierarchy.first;
      parent_hierarchy.first = child;
    }
    // Children keep their world pose under the new parent. Entities without
    // a 3D transform, or with a degenerate pose, keep their linkage only.
    preserve_world_pose (*registry, child, previous_world);
  }

  if (registry->ctx ().contains<rsc::scene *> ()) {
    if (auto *scene = registry->ctx ().get<rsc::scene *> ()) {
      scene->remove_entity_name (target);
    }
  }
  registry->destroy (target);
  return true;
}

uint32_t
wsl_world_entity_count (uint64_t world)
{
  auto *registry = registry_from_handle (world);
  if (registry == nullptr) {
    return 0;
  }
  // Count live entities explicitly: destroyed slots may be recycled by a
  // later create without growing the underlying storage.
  uint32_t live = 0;
  for (const auto entity : registry->storage<entt::entity> ()) {
    if (registry->valid (entity)) {
      ++live;
    }
  }
  return live;
}

uint32_t
wsl_world_find_all_by_name_count (uint64_t world, const char *name)
{
  auto *registry = registry_from_handle (world);
  if (registry == nullptr || name == nullptr
      || !registry->ctx ().contains<rsc::scene *> ()) {
    return 0;
  }
  auto *scene = registry->ctx ().get<rsc::scene *> ();
  if (scene == nullptr) {
    return 0;
  }
  uint32_t count = 0;
  for (const auto &[entity, entity_name] : scene->get_entity_names ()) {
    if (entity_name == name && registry->valid (entity)) {
      ++count;
    }
  }
  return count;
}

uint32_t
wsl_world_find_all_by_name_at (uint64_t world, const char *name, uint32_t index)
{
  auto *registry = registry_from_handle (world);
  if (registry == nullptr || name == nullptr
      || !registry->ctx ().contains<rsc::scene *> ()) {
    return NULL_ENTITY_ID;
  }
  auto *scene = registry->ctx ().get<rsc::scene *> ();
  if (scene == nullptr) {
    return NULL_ENTITY_ID;
  }
  uint32_t seen = 0;
  for (const auto &[entity, entity_name] : scene->get_entity_names ()) {
    if (entity_name == name && registry->valid (entity)) {
      if (seen == index) {
        return static_cast<uint32_t> (entity);
      }
      ++seen;
    }
  }
  return NULL_ENTITY_ID;
}

bool
wsl_world_copy_component (uint64_t world, uint64_t dst_world, uint32_t dst_id,
                          uint64_t src_world, uint32_t src_id, uint32_t type_id,
                          ::das::Context *context, ::das::LineInfoArg *at)
{
  if (g_query_depth > 0) {
    if (context != nullptr) {
      context->throw_error_at (
          at, "cannot copy a component during a query; use a command buffer");
    }
    return false;
  }
  auto *registry = registry_from_handle (world);
  const uint32_t valid_dst = wsl_world_entity_id (world, dst_world, dst_id);
  const uint32_t valid_src = wsl_world_entity_id (world, src_world, src_id);
  if (registry == nullptr || valid_dst == NULL_ENTITY_ID
      || valid_src == NULL_ENTITY_ID || type_id == 0) {
    return false;
  }
  // Hierarchy links must go through set_parent; a raw copy would duplicate
  // the parent/first/next linkage and corrupt both child lists.
  if (type_id == wsl_type_id_hierarchy ()) {
    return false;
  }
  const auto dst = static_cast<entt::entity> (valid_dst);
  const auto src = static_cast<entt::entity> (valid_src);
  if (dst == src) {
    return wsl_world_has_component (world, dst_world, dst_id, type_id);
  }
  if (auto *runtime_ctx
      = registry->ctx ().contains<comp::singl::runtime_context *> ()
            ? registry->ctx ().get<comp::singl::runtime_context *> ()
            : nullptr;
      runtime_ctx != nullptr) {
    auto &components = runtime_ctx->component_registry ();
    if (const auto *desc = components.find_world_component (type_id);
        desc != nullptr) {
      if (desc->is_das_component) {
        if (!components.das_component_contains (*registry, type_id, src)) {
          return false;
        }
        if (!components.das_component_contains (*registry, type_id, dst)
            && !components.das_component_add (*registry, type_id, dst)) {
          return false;
        }
        uint8_t *dst_data
            = components.das_component_data (*registry, type_id, dst);
        const uint8_t *src_data
            = components.das_component_data (*registry, type_id, src);
        if (dst_data == nullptr || src_data == nullptr
            || desc->das_struct_size <= 0) {
          return false;
        }
        std::memcpy (dst_data, src_data,
                     static_cast<std::size_t> (desc->das_struct_size));
        return true;
      }
      if (desc->copy != nullptr) {
        desc->copy (*registry, src, *registry, dst);
        return true;
      }
    }
  }

#define WSL_DAS_COPY_BUILTIN(Type, TypeIdFn)                                   \
  if (type_id == TypeIdFn ()) {                                                \
    if (!registry->all_of<Type> (src)) {                                       \
      return false;                                                            \
    }                                                                          \
    registry->emplace_or_replace<Type> (dst, registry->get<Type> (src));       \
    return true;                                                               \
  }
  WSL_DAS_COPY_BUILTIN (comp::transform, wsl_type_id_transform)
  WSL_DAS_COPY_BUILTIN (comp::transform_2d, wsl_type_id_transform_2d)
  WSL_DAS_COPY_BUILTIN (comp::camera, wsl_type_id_camera)
  WSL_DAS_COPY_BUILTIN (comp::camera_2d, wsl_type_id_camera_2d)
  WSL_DAS_COPY_BUILTIN (comp::sprite_2d, wsl_type_id_sprite_2d)
  WSL_DAS_COPY_BUILTIN (comp::world_transform, wsl_type_id_world_transform)
  WSL_DAS_COPY_BUILTIN (comp::point_light, wsl_type_id_point_light)
  WSL_DAS_COPY_BUILTIN (comp::directional_light, wsl_type_id_directional_light)
  WSL_DAS_COPY_BUILTIN (comp::spot_light, wsl_type_id_spot_light)
  WSL_DAS_COPY_BUILTIN (comp::rigid_body, wsl_type_id_rigid_body)
  // character_body is move-only (owns its physics body); copying it needs
  // backend body re-creation and is rejected until that path exists.
  WSL_DAS_COPY_BUILTIN (comp::model_instance_3d, wsl_type_id_model_instance_3d)
  WSL_DAS_COPY_BUILTIN (comp::area, wsl_type_id_area_3d)
  WSL_DAS_COPY_BUILTIN (comp::audio, wsl_type_id_audio)
  WSL_DAS_COPY_BUILTIN (comp::prefab_instance, wsl_type_id_prefab_instance)
  WSL_DAS_COPY_BUILTIN (comp::subviewport, wsl_type_id_subviewport)
#undef WSL_DAS_COPY_BUILTIN
  return false;
}

bool
wsl_world_defer_destroy_only (uint64_t world, uint64_t entity_world,
                              uint32_t entity_id)
{
  if (!wsl_world_entity_is_alive (world, entity_world, entity_id)) {
    return false;
  }
  if (g_query_depth == 0) {
    return wsl_world_destroy_entity_only (world, entity_world, entity_id,
                                          nullptr, nullptr);
  }
  g_deferred_commands.push_back ({ deferred_operation::destroy_entity_only,
                                   world, entity_world, entity_id });
  return true;
}

bool
wsl_world_defer_set_parent (uint64_t world, uint64_t child_world,
                            uint32_t child_id, uint64_t parent_world,
                            uint32_t parent_id, bool keep_world)
{
  if (!wsl_world_entity_is_alive (world, child_world, child_id)) {
    return false;
  }
  const bool no_parent = parent_world == 0 && parent_id == NULL_ENTITY_ID;
  if (!no_parent
      && !wsl_world_entity_is_alive (world, parent_world, parent_id)) {
    return false;
  }
  if (g_query_depth == 0) {
    return wsl_world_set_parent (world, child_world, child_id, parent_world,
                                 parent_id, keep_world, nullptr, nullptr);
  }
  g_deferred_commands.push_back ({ deferred_operation::set_parent, world,
                                   child_world, child_id, 0, parent_world,
                                   parent_id, keep_world });
  return true;
}

uint32_t
wsl_world_defer_create_entity (uint64_t world, const char *name)
{
  auto *registry = registry_from_handle (world);
  if (registry == nullptr) {
    return NULL_ENTITY_ID;
  }
  // Creates the bare entity immediately, even inside a query. Bare creation
  // touches no component pools, so live query references stay valid, and a
  // component-less entity matches no required-component query. Follow-up
  // structural commands (add/remove/set_parent/destroy) still go through the
  // deferred queue and apply at the outermost query barrier, or immediately
  // outside iteration. This keeps the returned handle valid for those
  // follow-ups without placeholder machinery or stale-handle hazards; the
  // observable difference from a fully deferred creation is only that
  // `is_alive`/`entity_count` observe the bare entity right away.
  const auto created = registry->create ();
  if (name != nullptr && name[0] != '\0'
      && registry->ctx ().contains<rsc::scene *> ()) {
    if (auto *scene = registry->ctx ().get<rsc::scene *> ()) {
      scene->set_entity_name (created, name);
    }
  }
  return static_cast<uint32_t> (created);
}

namespace
{

void
flush_deferred_commands ()
{
  if (g_query_depth != 0 || g_deferred_commands.empty ()) {
    return;
  }
  auto commands = std::move (g_deferred_commands);
  g_deferred_commands.clear ();
  for (const deferred_command &command : commands) {
    switch (command.operation) {
    case deferred_operation::destroy_entity:
      (void)wsl_world_destroy_entity (command.world, command.entity_world,
                                      command.entity_id, nullptr, nullptr);
      break;
    case deferred_operation::destroy_entity_only:
      (void)wsl_world_destroy_entity_only (command.world, command.entity_world,
                                           command.entity_id, nullptr, nullptr);
      break;
    case deferred_operation::add_component:
      (void)wsl_world_add_component (command.world, command.entity_world,
                                     command.entity_id, command.type_id,
                                     nullptr, nullptr);
      break;
    case deferred_operation::remove_component:
      (void)wsl_world_remove_component (command.world, command.entity_world,
                                        command.entity_id, command.type_id,
                                        nullptr, nullptr);
      break;
    case deferred_operation::set_parent:
      (void)wsl_world_set_parent (command.world, command.entity_world,
                                  command.entity_id, command.parent_world,
                                  command.parent_id, command.keep_world,
                                  nullptr, nullptr);
      break;
    }
  }
}

} // namespace

namespace
{

/** Fetches the singleton registry for a world handle, or nullptr. */
reg::singleton_registry *
singleton_registry_for (uint64_t world)
{
  auto *registry = registry_from_handle (world);
  if (registry == nullptr
      || !registry->ctx ().contains<comp::singl::runtime_context *> ()) {
    return nullptr;
  }
  auto *runtime_ctx = registry->ctx ().get<comp::singl::runtime_context *> ();
  return runtime_ctx != nullptr ? &runtime_ctx->singleton_registry () : nullptr;
}

} // namespace

uint32_t
wsl_world_singleton_type_id (uint64_t world, const char *type_name)
{
  auto *singletons = singleton_registry_for (world);
  if (singletons == nullptr || type_name == nullptr) {
    return 0;
  }
  const auto *desc = singletons->find_singleton_component (type_name);
  return desc != nullptr ? static_cast<uint32_t> (desc->type_id) : 0;
}

bool
wsl_world_has_singleton (uint64_t world, uint32_t type_id)
{
  auto *singletons = singleton_registry_for (world);
  if (singletons == nullptr || type_id == 0) {
    return false;
  }
  auto *registry = registry_from_handle (world);
  const auto *desc = singletons->find_singleton_component (
      static_cast<entt::id_type> (type_id));
  if (desc == nullptr) {
    return false;
  }
  if (desc->is_das_singleton) {
    return singletons->das_singleton_contains (*registry, desc->type_id);
  }
  return desc->contains != nullptr && desc->contains (*registry);
}

bool
wsl_world_add_singleton (uint64_t world, uint32_t type_id)
{
  auto *singletons = singleton_registry_for (world);
  if (singletons == nullptr || type_id == 0) {
    return false;
  }
  auto *registry = registry_from_handle (world);
  const auto *desc = singletons->find_singleton_component (
      static_cast<entt::id_type> (type_id));
  if (desc == nullptr) {
    return false;
  }
  if (desc->is_das_singleton) {
    return singletons->das_singleton_add (*registry, desc->type_id);
  }
  return desc->emplace_default != nullptr && desc->emplace_default (*registry);
}

bool
wsl_world_remove_singleton (uint64_t world, uint32_t type_id)
{
  auto *singletons = singleton_registry_for (world);
  if (singletons == nullptr || type_id == 0) {
    return false;
  }
  auto *registry = registry_from_handle (world);
  const auto *desc = singletons->find_singleton_component (
      static_cast<entt::id_type> (type_id));
  if (desc == nullptr) {
    return false;
  }
  if (desc->is_das_singleton) {
    return singletons->das_singleton_remove (*registry, desc->type_id);
  }
  return desc->remove != nullptr && desc->remove (*registry);
}

void *
wsl_world_singleton_data (uint64_t world, uint32_t type_id)
{
  auto *singletons = singleton_registry_for (world);
  if (singletons == nullptr || type_id == 0) {
    return nullptr;
  }
  auto *registry = registry_from_handle (world);
  const auto *desc = singletons->find_singleton_component (
      static_cast<entt::id_type> (type_id));
  if (desc == nullptr) {
    return nullptr;
  }
  if (desc->is_das_singleton) {
    return singletons->das_singleton_data (*registry, desc->type_id);
  }
  return desc->get_ptr != nullptr ? desc->get_ptr (*registry) : nullptr;
}

void *
wsl_world_component_data (uint64_t world, uint64_t entity_world,
                          uint32_t entity_id, uint32_t type_id)
{
  auto *registry = registry_from_handle (world);
  const uint32_t id = wsl_world_entity_id (world, entity_world, entity_id);
  if (registry == nullptr || id == NULL_ENTITY_ID || type_id == 0
      || !registry->ctx ().contains<comp::singl::runtime_context *> ()) {
    return nullptr;
  }
  auto *runtime_ctx = registry->ctx ().get<comp::singl::runtime_context *> ();
  if (runtime_ctx == nullptr) {
    return nullptr;
  }
  const auto type = static_cast<entt::id_type> (type_id);
  const auto *desc
      = runtime_ctx->component_registry ().find_world_component (type);
  if (desc == nullptr || !desc->is_das_component) {
    return nullptr;
  }
  return runtime_ctx->component_registry ().das_component_data (
      *registry, type, static_cast<entt::entity> (id));
}

void *
wsl_component_default_data (uint32_t type_id)
{
  // Per-thread, zero-initialized storage for one component type.
  //
  // `get_or_default` binds this sink when the entity is stale or the
  // component is absent, so a script that writes through the returned
  // reference hits harmless scratch memory instead of corrupting the
  // registry -- the same "live view or no-op" contract the built-in proxy
  // accessors already have. Never returned for `type_id == 0`.
  if (type_id == 0) {
    return nullptr;
  }
  thread_local std::unordered_map<uint32_t, std::vector<std::byte>> sinks;
  const auto it = sinks.find (type_id);
  if (it != sinks.end ()) {
    return it->second.data ();
  }
  std::size_t size = 0;
  const wsl::reg::runtime_layout *layout = nullptr;
  if (auto *registry = get_registry ()) {
    if (registry->ctx ().contains<comp::singl::runtime_context *> ()) {
      if (auto *runtime_ctx
          = registry->ctx ().get<comp::singl::runtime_context *> ()) {
        const auto *desc
            = runtime_ctx->component_registry ().find_world_component (
                static_cast<entt::id_type> (type_id));
        if (desc != nullptr && desc->is_das_component) {
          size = desc->das_layout.size;
          layout = &desc->das_layout;
        }
      }
    }
  }
  // An unregistered type has no layout to honour; the buffer only has to be
  // large enough for the script's own view of the type.
  if (size == 0) {
    size = 4096;
  }
  std::vector<std::byte> buffer (size, std::byte{ 0 });
  if (layout != nullptr) {
    // Seed the sink with the registered field defaults, so a read through
    // `get_or_default` sees the defaults a freshly added component would.
    for (const auto &field : layout->fields) {
      if (field.default_value.empty ()
          || static_cast<std::size_t> (field.offset)
                     + field.default_value.size ()
                 > buffer.size ()) {
        continue;
      }
      std::memcpy (buffer.data () + field.offset, field.default_value.data (),
                   field.default_value.size ());
    }
  }
  const auto inserted = sinks.emplace (type_id, std::move (buffer));
  return inserted.first->second.data ();
}

int
wsl_api_get_query_depth () noexcept
{
  return g_query_depth;
}

void
wsl_api_restore_query_depth (int depth) noexcept
{
  g_query_depth = depth;
  if (g_query_depth == 0) {
    try {
      flush_deferred_commands ();
    } catch (...) {
    }
  }
}

wsl::event::message_bus *
wsl_api_get_active_message_bus ()
{
  if (g_registry == nullptr) {
    return nullptr;
  }
  if (g_registry->ctx ().contains<comp::singl::runtime_context *> ()) {
    return &g_registry->ctx ()
                .get<comp::singl::runtime_context *> ()
                ->message_bus ();
  }
  return nullptr;
}

void
wsl_log_info (const char *msg)
{
  wsl::log::sys ()->info ("{}", msg ? msg : "");
}

void
wsl_log_trace (const char *msg)
{
  wsl::log::sys ()->trace ("{}", msg ? msg : "");
}

void
wsl_log_debug (const char *msg)
{
  wsl::log::sys ()->debug ("{}", msg ? msg : "");
}

void
wsl_log_warn (const char *msg)
{
  wsl::log::sys ()->warn ("{}", msg ? msg : "");
}

void
wsl_log_error (const char *msg)
{
  wsl::log::sys ()->error ("{}", msg ? msg : "");
}

// ── Scene system control ──
//
// Looks up a system by engine display name in the active scene, scoped to
// the calling registry so gameplay scripts can only affect their own scene.
// Disabling deactivates immediately (on_stop runs); enabling re-runs init
// (on_start) when needed and activates. Unknown names report false.

namespace
{

sys::ecs_system *
find_scene_system_by_name (const char *name)
{
  if (name == nullptr || name[0] == '\0') {
    return nullptr;
  }
  auto *reg = get_registry ();
  if (reg == nullptr
      || !reg->ctx ().contains<comp::singl::runtime_context *> ()) {
    return nullptr;
  }
  auto *runtime_ctx = reg->ctx ().get<comp::singl::runtime_context *> ();
  if (runtime_ctx == nullptr) {
    return nullptr;
  }
  auto *scene = runtime_ctx->scene_manager ().get_active ();
  if (scene == nullptr || &scene->get_registry () != reg) {
    return nullptr;
  }
  for (const auto &sys : scene->systems) {
    if (sys != nullptr && sys->get_name () == name) {
      return sys.get ();
    }
  }
  return nullptr;
}

} // namespace

bool
wsl_enable_system (const char *name)
{
  auto *sys = find_scene_system_by_name (name);
  if (sys == nullptr) {
    return false;
  }
  sys->set_active (true, get_registry ());
  return true;
}

bool
wsl_disable_system (const char *name)
{
  auto *sys = find_scene_system_by_name (name);
  if (sys == nullptr) {
    return false;
  }
  sys->set_active (false, get_registry ());
  return true;
}

bool
wsl_system_enabled (const char *name)
{
  auto *sys = find_scene_system_by_name (name);
  return sys != nullptr && sys->is_active ();
}

} // namespace wsl::das
#endif
