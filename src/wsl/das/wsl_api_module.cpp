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
#include "wsl/log/log.hpp"
#include "wsl/ray.hpp"
#include "wsl/rsc/scene.hpp"
#include "wsl/rsc/resource_manager.hpp"
#include "wsl/reg/component_registry.hpp"
#include "wsl/event/event_hub.hpp"
#include "wsl/phys/physics_engine.hpp"
#include "das_system_adapter.hpp"
#include "wsl_event_binds.hpp"

#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_keyboard.h>
#include <entt/core/hashed_string.hpp>
#include <array>
#include <chrono>
#include <cstring>

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

entt::registry *
get_registry ()
{
  return g_registry;
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

float &
SubviewportProxy::x ()
{
  return comp ? comp->x : fallback_value<float> ();
}

float &
SubviewportProxy::y ()
{
  return comp ? comp->y : fallback_value<float> ();
}

float &
SubviewportProxy::width ()
{
  return comp ? comp->width : fallback_value<float> ();
}

float &
SubviewportProxy::height ()
{
  return comp ? comp->height : fallback_value<float> ();
}

bool &
SubviewportProxy::clear_color ()
{
  return comp ? comp->clear_color : fallback_value<bool> ();
}

bool &
SubviewportProxy::clear_depth ()
{
  return comp ? comp->clear_depth : fallback_value<bool> ();
}

float &
SubviewportProxy::clear_r ()
{
  return comp ? comp->clear_r : fallback_value<float> ();
}

float &
SubviewportProxy::clear_g ()
{
  return comp ? comp->clear_g : fallback_value<float> ();
}

float &
SubviewportProxy::clear_b ()
{
  return comp ? comp->clear_b : fallback_value<float> ();
}

float &
SubviewportProxy::clear_a ()
{
  return comp ? comp->clear_a : fallback_value<float> ();
}

uint32_t &
SubviewportProxy::camera_2d ()
{
  return comp ? reinterpret_cast<uint32_t &> (comp->camera_2d.value)
              : fallback_value<uint32_t> ();
}

uint32_t &
SubviewportProxy::camera_3d ()
{
  return comp ? reinterpret_cast<uint32_t &> (comp->camera_3d.value)
              : fallback_value<uint32_t> ();
}

::das::float2 &
SubviewportProxy::world_quad_size ()
{
  return comp ? reinterpret_cast<::das::float2 &> (comp->world_quad_size)
              : fallback_value<::das::float2> ();
}

::das::float2 &
SubviewportProxy::container_size ()
{
  return comp ? reinterpret_cast<::das::float2 &> (comp->container_size)
              : fallback_value<::das::float2> ();
}

::das::float2 &
SubviewportProxy::container_position ()
{
  return comp ? reinterpret_cast<::das::float2 &> (comp->container_position)
              : fallback_value<::das::float2> ();
}

::das::float2 &
SubviewportProxy::virtual_size ()
{
  return comp ? reinterpret_cast<::das::float2 &> (comp->virtual_size)
              : fallback_value<::das::float2> ();
}

bool &
SubviewportProxy::render_2d_only ()
{
  return comp ? comp->render_2d_only : fallback_value<bool> ();
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
    addProperty<DAS_BIND_MANAGED_PROP (x)> ("x", "x");
    addProperty<DAS_BIND_MANAGED_PROP (y)> ("y", "y");
    addProperty<DAS_BIND_MANAGED_PROP (width)> ("width", "width");
    addProperty<DAS_BIND_MANAGED_PROP (height)> ("height", "height");
    addProperty<DAS_BIND_MANAGED_PROP (clear_color)> ("clear_color",
                                                      "clear_color");
    addProperty<DAS_BIND_MANAGED_PROP (clear_depth)> ("clear_depth",
                                                      "clear_depth");
    addProperty<DAS_BIND_MANAGED_PROP (clear_r)> ("clear_r", "clear_r");
    addProperty<DAS_BIND_MANAGED_PROP (clear_g)> ("clear_g", "clear_g");
    addProperty<DAS_BIND_MANAGED_PROP (clear_b)> ("clear_b", "clear_b");
    addProperty<DAS_BIND_MANAGED_PROP (clear_a)> ("clear_a", "clear_a");
    addProperty<DAS_BIND_MANAGED_PROP (camera_2d)> ("camera_2d", "camera_2d");
    addProperty<DAS_BIND_MANAGED_PROP (camera_3d)> ("camera_3d", "camera_3d");
    addProperty<DAS_BIND_MANAGED_PROP (world_quad_size)> ("world_quad_size",
                                                          "world_quad_size");
    addProperty<DAS_BIND_MANAGED_PROP (container_size)> ("container_size",
                                                         "container_size");
    addProperty<DAS_BIND_MANAGED_PROP (container_position)> (
        "container_position", "container_position");
    addProperty<DAS_BIND_MANAGED_PROP (virtual_size)> ("virtual_size",
                                                       "virtual_size");
    addProperty<DAS_BIND_MANAGED_PROP (render_2d_only)> ("render_2d_only",
                                                         "render_2d_only");
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

// ── Generic component queries ──

bool
wsl_has_component (uint32_t type_id, uint32_t entity)
{
  auto *reg = get_registry ();
  if (!reg) {
    return false;
  }
  auto e = static_cast<entt::entity> (entity);
  if (!reg->valid (e)) {
    return false;
  }
  // Check entt storage first (C++ components)
  auto *storage = reg->storage (type_id);
  if (storage && storage->contains (e)) {
    return true;
  }
  // Check das component storage
  if (reg->ctx ().contains<comp::singl::runtime_context *> ()) {
    auto *runtime_ctx = reg->ctx ().get<comp::singl::runtime_context *> ();
    if (runtime_ctx) {
      return runtime_ctx->component_registry ().das_component_contains (
          *reg, type_id, e);
    }
  }
  return false;
}

// ── Generic component add/remove ──

bool
wsl_add_component (uint32_t type_id, uint32_t entity, ::das::Context *context,
                   ::das::LineInfoArg *at)
{
  if (g_query_depth > 0) {
    context->throw_error_at (
        at, "cannot add_component during a query; structural mutations are "
            "not allowed while component references are live");
  }
  auto *reg = get_registry ();
  if (!reg) {
    return false;
  }
  auto e = static_cast<entt::entity> (entity);
  if (!reg->valid (e)) {
    return false;
  }
  if (!reg->ctx ().contains<comp::singl::runtime_context *> ()) {
    return false;
  }
  auto *runtime_ctx = reg->ctx ().get<comp::singl::runtime_context *> ();
  if (!runtime_ctx) {
    return false;
  }
  auto &comp_reg = runtime_ctx->component_registry ();
  auto *desc = comp_reg.find_world_component (type_id);
  if (!desc) {
    return false;
  }
  if (desc->is_das_component) {
    return comp_reg.das_component_add (*reg, type_id, e);
  }
  if (desc->emplace_default) {
    return desc->emplace_default (*reg, e);
  }
  return false;
}

bool
wsl_remove_component (uint32_t type_id, uint32_t entity,
                      ::das::Context *context, ::das::LineInfoArg *at)
{
  if (g_query_depth > 0) {
    context->throw_error_at (
        at, "cannot remove_component during a query; structural mutations are "
            "not allowed while component references are live");
  }
  auto *reg = get_registry ();
  if (!reg) {
    return false;
  }
  auto e = static_cast<entt::entity> (entity);
  if (!reg->valid (e)) {
    return false;
  }
  if (!reg->ctx ().contains<comp::singl::runtime_context *> ()) {
    return false;
  }
  auto *runtime_ctx = reg->ctx ().get<comp::singl::runtime_context *> ();
  if (!runtime_ctx) {
    return false;
  }
  auto &comp_reg = runtime_ctx->component_registry ();
  auto *desc = comp_reg.find_world_component (type_id);
  if (!desc) {
    return false;
  }
  if (desc->is_das_component) {
    return comp_reg.das_component_remove (*reg, type_id, e);
  }
  if (desc->remove) {
    return desc->remove (*reg, e);
  }
  return false;
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

// ── Generic entity iteration (DECS-style `query` primitive) ──

void
each_entity_id_with (const ::das::TArray<uint32_t> &type_ids,
                     const ::das::TBlock<void, uint32_t> &blk,
                     ::das::Context *context, ::das::LineInfoArg *at)
{
  auto *reg = get_registry ();
  if (!reg || type_ids.size == 0) {
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
      if ((candidate.native != nullptr
           && !candidate.native->contains (entity))
          || (candidate.das != nullptr
              && !candidate.das->contains (entity))) {
        return;
      }
    }
    ::das::das_invoke<void>::invoke<uint32_t> (context, at, blk,
                                               static_cast<uint32_t> (entity));
  };

  const term &selected = terms[driver_index];
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

void *
wsl_get_component_data (uint32_t entity, uint32_t type_id)
{
  auto *reg = get_registry ();
  if (!reg || !reg->ctx ().contains<comp::singl::runtime_context *> ()) {
    return nullptr;
  }

  auto *runtime_ctx = reg->ctx ().get<comp::singl::runtime_context *> ();
  if (!runtime_ctx) {
    return nullptr;
  }

  const auto *info = runtime_ctx->component_registry ().find_world_component (
      static_cast<entt::id_type> (type_id));
  if (!info || !info->is_das_component) {
    return nullptr;
  }

  return runtime_ctx->component_registry ().das_component_data (
      *reg, static_cast<entt::id_type> (type_id),
      static_cast<entt::entity> (entity));
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
    wsl::log::sys ()->error (
        "event_declare_sink: payload size mismatch for '{}' (source {}, sink {})",
        event_name, event_size, handler_payload_size);
    return false;
  }

  return adapter->add_script_event_sink (event_id, event_type_name,
                                         handler_name, handler_payload_size,
                                         handler_layout_hash);
}

/**
 * Emits a registered event from a daScript struct payload.
 *
 * The payload is accepted as ``anyArgument``; it must be a structure whose
 * size matches the registered C++ event type. Bytes are dispatched directly
 * to connected sinks (no intermediate copy).
 */
static vec4f
wsl_event_emit (::das::Context &, ::das::SimNode_CallBase *call, vec4f *args)
{
  const char *event_name = ::das::cast<char *>::to (args[0]);

  auto *rc = try_get_runtime_context ();
  if (!rc || !event_name) {
    return v_zero ();
  }

  ::das::TypeInfo *ti = call->types[1];
  if (ti == nullptr || ti->type != ::das::Type::tStructure) {
    wsl::log::sys ()->error (
        "event_emit: payload for '{}' must be a structure type", event_name);
    return v_zero ();
  }
  if (ti->structType == nullptr || !ti->isRawPod ()) {
    wsl::log::sys ()->error (
        "event_emit: payload for '{}' must be a hashable raw-POD struct",
        event_name);
    return v_zero ();
  }

  const std::size_t payload_size
      = static_cast<std::size_t> (::das::getTypeSize (ti));
  const std::uint64_t payload_layout_hash
      = das_struct_layout_hash (ti->structType);
  if (payload_layout_hash == 0) {
    wsl::log::sys ()->error (
        "event_emit: payload layout for '{}' could not be determined",
        event_name);
    return v_zero ();
  }
  const void *data = ::das::cast<void *>::to (args[1]);
  if (data == nullptr || payload_size == 0) {
    wsl::log::sys ()->error (
        "event_emit: payload for '{}' is null or has zero size", event_name);
    return v_zero ();
  }

  auto &hub = rc->event_hub ();
  if (hub.db == nullptr) {
    return v_zero ();
  }

  entt::id_type event_id = resolve_event_type_id (event_name);
  auto it = hub.db->entries.find (event_id);
  if (it == hub.db->entries.end ()) {
    // Undeclared event: register it on first emit as a script-defined type
    // (mirrors message_post's first-use registration).
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
    // Script-defined event: first emit latches the payload layout so later
    // emits and sink wiring validate against it.
    it->second.size = payload_size;
  } else if (it->second.size != payload_size) {
    wsl::log::sys ()->error (
        "event_emit: payload size mismatch for '{}' (expected {}, got {})",
        event_name, it->second.size, payload_size);
    return v_zero ();
  }

  if (it->second.payload_layout_hash == 0) {
    it->second.payload_layout_hash = payload_layout_hash;
  } else if (it->second.payload_layout_hash != payload_layout_hash) {
    wsl::log::sys ()->error (
        "event_emit: payload layout mismatch for '{}'", event_name);
    return v_zero ();
  }

  hub.dispatch_by_id (event_id, data, payload_size, payload_layout_hash);
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

    addExtern<DAS_BIND_FUN (wsl_log_debug)> (*this, lib, "log_debug",
                                             ::das::SideEffects::modifyExternal,
                                             "wsl::das::wsl_log_debug")
        ->arg ("msg");

    addExtern<DAS_BIND_FUN (wsl_log_warn)> (*this, lib, "log_warn",
                                            ::das::SideEffects::modifyExternal,
                                            "wsl::das::wsl_log_warn")
        ->arg ("msg");

    addExtern<DAS_BIND_FUN (wsl_log_error)> (*this, lib, "log_error",
                                             ::das::SideEffects::modifyExternal,
                                             "wsl::das::wsl_log_error")
        ->arg ("msg");

    // Entity operations
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

    // Generic component queries
    addExtern<DAS_BIND_FUN (wsl_has_component)> (
        *this, lib, "has_component", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_has_component")
        ->args ({ "type_id", "entity" });

    // Generic component add/remove
    addExtern<DAS_BIND_FUN (wsl_add_component)> (
        *this, lib, "add_component", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_add_component");

    addExtern<DAS_BIND_FUN (wsl_remove_component)> (
        *this, lib, "remove_component", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_remove_component");

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

    // Generic query iteration primitive (backs the `query` macro)
    addExtern<DAS_BIND_FUN (each_entity_id_with)> (
        *this, lib, "each_entity_id_with", ::das::SideEffects::invoke,
        "wsl::das::each_entity_id_with")
        ->args ({ "type_ids", "blk", "context", "at" });

    // Generic component type lookup and direct payload access
    addExtern<DAS_BIND_FUN (wsl_get_component_type_id_by_name)> (
        *this, lib, "_get_component_type_id_by_name",
        ::das::SideEffects::accessExternal,
        "wsl::das::wsl_get_component_type_id_by_name")
        ->arg ("type_name");

    addExtern<DAS_BIND_FUN (wsl_get_component_data)> (
        *this, lib, "_get_component_data", ::das::SideEffects::accessExternal,
        "wsl::das::wsl_get_component_data")
        ->args ({ "entity", "type_id" });

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

    // ── Physics: rigid body ──

    addExtern<DAS_BIND_FUN (wsl_apply_impulse)> (
        *this, lib, "apply_impulse", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_apply_impulse")
        ->args ({ "entity", "x", "y", "z" });
    addExtern<DAS_BIND_FUN (wsl_apply_force)> (
        *this, lib, "apply_force", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_apply_force")
        ->args ({ "entity", "x", "y", "z" });

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

    addExtern<DAS_BIND_FUN (wsl_event_declare_source)> (
        *this, lib, "event_declare_source", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_event_declare_source")
        ->args ({ "event_name", "system_name" });
    addInterop<wsl_event_emit, void, const char *, vec4f> (
        *this, lib, "event_emit", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_event_emit")
        ->args ({ "event_name", "payload" });
    addExtern<DAS_BIND_FUN (wsl_event_declare_sink)> (
        *this, lib, "event_declare_sink", ::das::SideEffects::modifyExternal,
        "wsl::das::wsl_event_declare_sink")
        ->args ({ "event_name", "system_name", "handler_name" });

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

int
wsl_api_get_query_depth () noexcept
{
  return g_query_depth;
}

void
wsl_api_restore_query_depth (int depth) noexcept
{
  g_query_depth = depth;
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

} // namespace wsl::das
#endif
