#pragma once

// Phase B — rfl adapters for all ECS component types
// Replaces per-component serialize() methods.
// Each component type T has a Helper schema struct with from_class/to_class,
// and an rfl::CustomParser that enables rfl to serialize T through Helper.

#include "adapters.hpp"

#ifndef IN_MODULE_INTERFACE
#include <rfl.hpp>
#endif
#ifndef IN_MODULE_INTERFACE
#include <rfl/DefaultVal.hpp>
#endif
#ifndef IN_MODULE_INTERFACE
#include <rfl/Field.hpp>
#endif

#ifndef IN_MODULE_INTERFACE
#include <cstdint>
#endif
#ifndef IN_MODULE_INTERFACE
#include <string>
#endif
#ifndef IN_MODULE_INTERFACE
#include <unordered_map>
#endif
#ifndef IN_MODULE_INTERFACE
#include <vector>
#endif

#include "../comp/transform.hpp"
#include "../comp/hierarchy.hpp"
#include "../comp/world_transform.hpp"
#include "../comp/transform_2d.hpp"
#include "../comp/sprite_2d.hpp"
#include "../comp/point_light.hpp"
#include "../comp/directional_light.hpp"
#include "../comp/spot_light.hpp"
#include "../comp/prefab_instance.hpp"
#include "../comp/camera.hpp"
#include "../comp/character_body.hpp"
#include "../comp/area3d.hpp"
#include "../comp/animator.hpp"
#include "../comp/rigid_body.hpp"
#include "../comp/audio.hpp"
#include "../comp/model_instance_3d.hpp"
#include "../comp/subviewport.hpp"
#include "../comp/singl/rendering_manager.hpp"
#include "../comp/singl/physics_manager.hpp"
#include "../gfx/shader_graph_codegen.hpp"
#include "../rsc/resource_ids.hpp"

// windows.h (pulled in by SDL/RmlUi/Windows SDK headers) defines near/far
// as empty macros, breaking the CameraHelper fields and accessors below
// whenever such a header precedes this one. Save and clear them for this
// header only (no socket/FD_* code lives here, so nothing needs them).
#ifdef near
#pragma push_macro("near")
#undef near
#define WSL_ADAPTERS_POP_NEAR
#endif
#ifdef far
#pragma push_macro("far")
#undef far
#define WSL_ADAPTERS_POP_FAR
#endif

namespace wsl::serialize
{

/** Sentinel value used to represent entt::null in serialized form. */
inline constexpr std::uint32_t serialized_null_entity = 0xFFFFFFFF;

// =============================================================================
// transform
// =============================================================================
struct TransformHelper
{
  math::vec3f position{ 0, 0, 0 };
  math::quatf rotation{ 0, 0, 0, 1 };
  math::vec3f scale{ 1, 1, 1 };

  static TransformHelper
  from_class (const comp::transform &t)
  {
    return { t.position, t.rotation, t.scale };
  }

  comp::transform
  to_class () const
  {
    return comp::transform{ position, rotation, scale };
  }
};

// =============================================================================
// hierarchy
// =============================================================================
struct HierarchyHelper
{
  std::uint32_t parent = 0xFFFFFFFF;
  std::uint32_t first = 0xFFFFFFFF;
  std::uint32_t next = 0xFFFFFFFF;

  static HierarchyHelper
  from_class (const comp::hierarchy &h)
  {
    return { static_cast<std::uint32_t> (h.parent),
             static_cast<std::uint32_t> (h.first),
             static_cast<std::uint32_t> (h.next) };
  }

  comp::hierarchy
  to_class () const
  {
    comp::hierarchy h;
    h.parent = (parent == serialized_null_entity) ? entt::null
                                                  : entt::entity{ parent };
    h.first = (first == serialized_null_entity) ? entt::null
                                                : entt::entity{ first };
    h.next
        = (next == serialized_null_entity) ? entt::null : entt::entity{ next };
    return h;
  }
};

// =============================================================================
// point_light
// =============================================================================
struct PointLightHelper
{
  math::vec3f color{ 1, 1, 1 };
  float intensity = 1.0F;
  float radius = 10.0F;
  bool cast_shadows = false;
  float shadow_far = 35.0F;
  float shadow_bias = 0.06F;

  static PointLightHelper
  from_class (const comp::point_light &p)
  {
    return { p.color,        p.intensity,  p.radius,
             p.cast_shadows, p.shadow_far, p.shadow_bias };
  }

  comp::point_light
  to_class () const
  {
    comp::point_light p;
    p.color = color;
    p.intensity = intensity;
    p.radius = radius;
    p.cast_shadows = cast_shadows;
    p.shadow_far = shadow_far;
    p.shadow_bias = shadow_bias;
    return p;
  }
};

// =============================================================================
// directional_light
// =============================================================================
struct DirectionalLightHelper
{
  math::vec3f color{ 1, 1, 1 };
  float intensity = 1.0F;

  static DirectionalLightHelper
  from_class (const comp::directional_light &d)
  {
    return { d.color, d.intensity };
  }

  comp::directional_light
  to_class () const
  {
    comp::directional_light d;
    d.color = color;
    d.intensity = intensity;
    return d;
  }
};

// =============================================================================
// spot_light
// =============================================================================
struct SpotLightHelper
{
  math::vec3f color{ 1, 1, 1 };
  float intensity = 1.0F;
  float inner_cos = 0.9F;
  float outer_cos = 0.8F;

  static SpotLightHelper
  from_class (const comp::spot_light &s)
  {
    return { s.color, s.intensity, s.inner_cos, s.outer_cos };
  }

  comp::spot_light
  to_class () const
  {
    comp::spot_light s;
    s.color = color;
    s.intensity = intensity;
    s.inner_cos = inner_cos;
    s.outer_cos = outer_cos;
    return s;
  }
};

// =============================================================================
// world_transform
// =============================================================================
struct WorldTransformHelper
{
  math::mat44f matrix{ 1.0f };

  static WorldTransformHelper
  from_class (const comp::world_transform &w)
  {
    WorldTransformHelper h;
    h.matrix = w.value ();
    return h;
  }

  comp::world_transform
  to_class () const
  {
    comp::world_transform w;
    w.value () = matrix;
    return w;
  }
};

// =============================================================================
// transform_2d
// =============================================================================
struct Transform2DHelper
{
  math::vec2f position{ 0.0F, 0.0F };
  float rotation = 0.0F;
  math::vec2f scale{ 1.0F, 1.0F };
  math::vec2f pivot{ 0.5F, 0.5F };

  static Transform2DHelper
  from_class (const comp::transform_2d &t)
  {
    return { t.position, t.rotation, t.scale, t.pivot };
  }

  comp::transform_2d
  to_class () const
  {
    comp::transform_2d t;
    t.position = position;
    t.rotation = rotation;
    t.scale = scale;
    t.pivot = pivot;
    return t;
  }
};

// =============================================================================
// sprite_2d
// =============================================================================
struct Sprite2DHelper
{
  std::uint64_t image_value = 0;
  math::vec2f size{ 100.0F, 100.0F };
  math::vec4f color{ 1.0F, 1.0F, 1.0F, 1.0F };
  math::vec2f uv_offset{ 0.0F, 0.0F };
  math::vec2f uv_scale{ 1.0F, 1.0F };
  bool flip_v = false;
  bool flip_h = false;
  int z_index = 0;

  static Sprite2DHelper
  from_class (const comp::sprite_2d &s)
  {
    return { s.image.value, s.size,   s.color,  s.uv_offset,
             s.uv_scale,    s.flip_v, s.flip_h, s.z_index };
  }

  comp::sprite_2d
  to_class () const
  {
    comp::sprite_2d s;
    s.image.value = image_value;
    s.size = size;
    s.color = color;
    s.uv_offset = uv_offset;
    s.uv_scale = uv_scale;
    s.flip_v = flip_v;
    s.flip_h = flip_h;
    s.z_index = z_index;
    return s;
  }
};

// =============================================================================
// prefab_instance
// =============================================================================
struct PrefabInstanceHelper
{
  std::uint64_t prefab_id_value = 0;
  std::uint32_t prefab_entity = 0xFFFFFFFF;

  static PrefabInstanceHelper
  from_class (const comp::prefab_instance &p)
  {
    return { p.prefab_id.value,
             (p.prefab_entity == entt::null)
                 ? serialized_null_entity
                 : static_cast<std::uint32_t> (p.prefab_entity) };
  }

  comp::prefab_instance
  to_class () const
  {
    comp::prefab_instance p;
    p.prefab_id.value = prefab_id_value;
    p.prefab_entity = (prefab_entity == serialized_null_entity)
                          ? entt::null
                          : entt::entity{ prefab_entity };
    return p;
  }
};

// =============================================================================
// camera (uses private members — must go through accessors)
// =============================================================================
struct CameraHelper
{
  float fov = 60.0F;
  float near = 0.5F;
  float far = 50.0F;
  float aspect_ratio = 1.0F;
  bool only_for_editor = false;

  static CameraHelper
  from_class (const comp::camera &c)
  {
    CameraHelper h;
    h.fov = c.fov ();
    h.near = c.near ();
    h.far = c.far ();
    h.aspect_ratio = c.aspect_ratio ();
    h.only_for_editor = c.only_for_editor ();
    return h;
  }

  comp::camera
  to_class () const
  {
    comp::camera c;
    c.fov () = fov;
    c.near () = near;
    c.far () = far;
    c.aspect_ratio () = aspect_ratio;
    c.only_for_editor () = only_for_editor;
    return c;
  }
};

// =============================================================================
// character_body
// =============================================================================
struct CharacterBodyHelper
{
  float height = 1.8F;
  float radius = 0.4F;
  math::vec3f desired_velocity{ 0.0F, 0.0F, 0.0F };

  static CharacterBodyHelper
  from_class (const comp::character_body &c)
  {
    CharacterBodyHelper h;
    h.height = c.height;
    h.radius = c.radius;
    h.desired_velocity = c.desired_velocity;
    return h;
  }

  comp::character_body
  to_class () const
  {
    comp::character_body c;
    c.height = height;
    c.radius = radius;
    c.desired_velocity = desired_velocity;
    return c;
  }
};

// =============================================================================
// area3d — complex: enum, sanitization, runtime handles
// Serialized: shape, position, rotation, half_extents, radius
// NOT serialized: body_id, applied_* cache
// =============================================================================
struct Area3DHelper
{
  int shape = 0; // 0=box, 1=sphere
  math::vec3f position{ 0, 0, 0 };
  math::quatf rotation{ 0, 0, 0, 1 };
  math::vec3f half_extents{ 0.5F, 0.5F, 0.5F };
  float radius = 0.5F;

  static Area3DHelper
  from_class (const comp::area &a)
  {
    Area3DHelper h;
    h.shape = static_cast<int> (a.shape);
    h.position = a.position;
    h.rotation = a.rotation;
    h.half_extents = a.half_extents;
    h.radius = a.radius;
    return h;
  }

  comp::area
  to_class () const
  {
    comp::area a;
    a.shape = static_cast<comp::area::shape_type> (shape);
    a.position = position;
    a.rotation = rotation;
    a.half_extents = half_extents;
    a.radius = radius;
    a.sanitize_dimensions ();
    // body_id and applied_* cache are runtime-only, reset on load
    return a;
  }
};

// =============================================================================
// rigid_body — complex: many enums, sanitization, runtime handles
// Serialized: shape, position, rotation, half_extents, radius, density,
//             dynamic, motion_type, allowed_dofs, collision_layer,
//             collision_mask, friction, restitution
// NOT serialized: body_id, applied_* cache
// =============================================================================
struct RigidBodyHelper
{
  int shape = 0;
  math::vec3f position{ 0, 0, 0 };
  math::quatf rotation{ 0, 0, 0, 1 };
  math::vec3f half_extents{ 0.5F, 0.5F, 0.5F };
  float radius = 0.5F;
  float density = 1000.0F;
  bool dynamic = true;
  int motion_type = 2;     // Dynamic=2
  int allowed_dofs = 0x7F; // All=0x7F
  int collision_layer = 0;
  int collision_mask = 0xFFFF; // all_collision_layers
  float friction = 0.2F;
  float restitution = 0.0F;

  static RigidBodyHelper
  from_class (const comp::rigid_body &r)
  {
    RigidBodyHelper h;
    h.shape = static_cast<int> (r.shape);
    h.position = r.position;
    h.rotation = r.rotation;
    h.half_extents = r.half_extents;
    h.radius = r.radius;
    h.density = r.density;
    h.dynamic = r.dynamic;
    h.motion_type = static_cast<int> (r.motion_type.value);
    h.allowed_dofs = static_cast<int> (r.allowed_dofs.value);
    h.collision_layer = static_cast<int> (r.collision_layer.value);
    h.collision_mask = static_cast<int> (r.collision_mask.value);
    h.friction = r.friction;
    h.restitution = r.restitution;
    return h;
  }

  comp::rigid_body
  to_class () const
  {
    comp::rigid_body r;
    r.shape = static_cast<comp::rigid_body::shape_type> (shape);
    r.position = position;
    r.rotation = rotation;
    r.half_extents = half_extents;
    r.radius = radius;
    r.density = density;
    r.dynamic = dynamic;
    r.motion_type.value = static_cast<phys::motion_type> (motion_type);
    r.allowed_dofs.value = static_cast<phys::allowed_dofs> (allowed_dofs);
    r.collision_layer.value = phys::layers::clamp_layer_index (
        static_cast<phys::layers::layer_index_t> (collision_layer));
    r.collision_mask.value = phys::layers::clamp_layer_mask (
        static_cast<phys::layers::layer_mask_t> (collision_mask));
    r.friction = friction;
    r.restitution = restitution;
    r.sanitize_dimensions ();
    r.sanitize_surface_properties ();
    // body_id and applied_* cache are runtime-only, reset on load
    return r;
  }
};

// =============================================================================
// audio — complex: resource ID as path string
// Serialized: audio_path (as string), loop, play_on_start, volume
// NOT serialized: playing, was_playing (runtime-only)
// =============================================================================
struct AudioHelper
{
  std::string audio_path = "None";
  bool loop = false;
  bool play_on_start = true;
  float volume = 1.0F;

  static AudioHelper
  from_class (const comp::audio &a)
  {
    auto *mgr = rsc::resource_manager::serialization_context::get ();
    std::string path = "None";
    if (mgr && a.audio_resource.value != entt::null) {
      path = mgr->get_resource_path (a.audio_resource);
    }
    return { path, a.loop, a.play_on_start, a.volume };
  }

  comp::audio
  to_class () const
  {
    comp::audio a;
    a.loop = loop;
    a.play_on_start = play_on_start;
    a.volume = volume;
    auto *mgr = rsc::resource_manager::serialization_context::get ();
    if (audio_path != "None" && !audio_path.empty () && mgr) {
      a.audio_resource = mgr->register_audio (audio_path);
    }
    return a;
  }
};

// =============================================================================
// model_instance_3d — complex: resource IDs as path strings
// Serialized: model_path, material_path, scene_index, mip_lod_bias,
//             geometry_lod_bias, visibility_range
// =============================================================================
struct ModelInstance3DHelper
{
  std::string model_path = "None";
  std::string material_path = "None";
  std::uint32_t scene_index = 0;
  float mip_lod_bias = 0.0F;
  float geometry_lod_bias = 0.0F;
  float visibility_range = 0.0F;

  static ModelInstance3DHelper
  from_class (const comp::model_instance_3d &m)
  {
    auto *mgr = rsc::resource_manager::serialization_context::get ();
    std::string path = "None";
    if (mgr && m.id.value != entt::null) {
      path = mgr->get_resource_path (m.id);
    }
    std::string mat_path = "None";
    if (mgr && m.material_override.value != entt::null) {
      mat_path = mgr->get_resource_path (m.material_override);
    }
    return { path,
             mat_path,
             m.scene_index,
             m.mip_lod_bias,
             m.geometry_lod_bias,
             m.visibility_range };
  }

  comp::model_instance_3d
  to_class () const
  {
    comp::model_instance_3d m;
    m.scene_index = scene_index;
    m.mip_lod_bias = mip_lod_bias;
    m.geometry_lod_bias = geometry_lod_bias;
    m.visibility_range = visibility_range;
    auto *mgr = rsc::resource_manager::serialization_context::get ();
    if (model_path != "None" && !model_path.empty () && mgr) {
      m.id = mgr->register_model (model_path);
    }
    if (material_path != "None" && !material_path.empty () && mgr) {
      m.material_override = mgr->register_material (material_path);
    }
    return m;
  }
};

// =============================================================================
// animator — transient ids/transition state are intentionally excluded
// =============================================================================
struct AnimatorHelper
{
  std::string clip_path = "None";
  std::string skeleton_path = "None";
  int skin_index = 0;
  float speed = 1.0F;
  bool loop = true;
  float crossfade_duration = 0.25F;
  bool playing = true;
  float time = 0.0F;

  static AnimatorHelper
  from_class (const comp::animator &a)
  {
    return { a.clip_path, a.skeleton_path,      a.skin_index, a.speed,
             a.loop,      a.crossfade_duration, a.playing,    a.time };
  }

  comp::animator
  to_class () const
  {
    comp::animator a;
    a.clip_path = clip_path;
    a.skeleton_path = skeleton_path;
    a.skin_index = skin_index;
    a.speed = speed;
    a.loop = loop;
    a.crossfade_duration = crossfade_duration;
    a.playing = playing;
    a.time = time;

    auto *mgr = rsc::resource_manager::serialization_context::get ();
    if (mgr != nullptr) {
      if (clip_path != "None" && !clip_path.empty ()) {
        a.current_animation = mgr->register_animation (clip_path);
      }
      if (skeleton_path != "None" && !skeleton_path.empty ()) {
        a.current_skeleton = mgr->register_skeleton (skeleton_path);
      }
    }
    return a;
  }
};

// =============================================================================
// subviewport_camera_ui (used inside subviewport)
// =============================================================================
struct SubviewportCameraUIHelper
{
  std::uint32_t value = 0xFFFFFFFF;

  static SubviewportCameraUIHelper
  from_class (const comp::subviewport_camera_ui &c)
  {
    return { (c.value == entt::null) ? serialized_null_entity
                                     : static_cast<std::uint32_t> (c.value) };
  }

  comp::subviewport_camera_ui
  to_class () const
  {
    comp::subviewport_camera_ui c;
    c.value = (value == serialized_null_entity) ? entt::null
                                                : entt::entity{ value };
    return c;
  }
};

// =============================================================================
// viewport_environment (per-viewport environment, D4)
// =============================================================================
struct ViewportEnvironmentHelper
{
  bool skybox_use_global = true;
  rsc::cubemap_id skybox{};
  bool skybox_rotation_use_global = true;
  math::quatf skybox_rotation{ 0, 0, 0, 1 };
  bool ambient_use_global = true;
  math::vec3f ambient_color{ 1, 1, 1 };
  float ambient_intensity = 0.01F;
  bool ibl_use_global = true;
  float ibl_intensity = 1.0F;
  bool exposure_use_global = true;
  float exposure = 1.0F;
  bool bloom_use_global = true;
  bool bloom_enabled = false;
  float bloom_threshold = 1.0F;
  float bloom_knee = 0.5F;
  float bloom_intensity = 1.0F;
  bool ssao_use_global = true;
  bool ssao_enabled = true;
  float ssao_radius = 0.6F;
  float ssao_bias = 0.025F;
  float ssao_power = 1.25F;
  float ssao_intensity = 1.0F;
  bool clear_color_enabled = true;
  bool clear_color_use_global = true;
  math::vec4f clear_color{ 0, 0, 0, 1 };
  bool clear_depth = true;

  static ViewportEnvironmentHelper
  from_class (const comp::viewport_environment &e)
  {
    ViewportEnvironmentHelper h;
    h.skybox_use_global = e.skybox_use_global;
    h.skybox = e.skybox;
    h.skybox_rotation_use_global = e.skybox_rotation_use_global;
    h.skybox_rotation = e.skybox_rotation;
    h.ambient_use_global = e.ambient_use_global;
    h.ambient_color = e.ambient_color;
    h.ambient_intensity = e.ambient_intensity;
    h.ibl_use_global = e.ibl_use_global;
    h.ibl_intensity = e.ibl_intensity;
    h.exposure_use_global = e.exposure_use_global;
    h.exposure = e.exposure;
    h.bloom_use_global = e.bloom_use_global;
    h.bloom_enabled = e.bloom_enabled;
    h.bloom_threshold = e.bloom_threshold;
    h.bloom_knee = e.bloom_knee;
    h.bloom_intensity = e.bloom_intensity;
    h.ssao_use_global = e.ssao_use_global;
    h.ssao_enabled = e.ssao_enabled;
    h.ssao_radius = e.ssao_radius;
    h.ssao_bias = e.ssao_bias;
    h.ssao_power = e.ssao_power;
    h.ssao_intensity = e.ssao_intensity;
    h.clear_color_enabled = e.clear_color_enabled;
    h.clear_color_use_global = e.clear_color_use_global;
    h.clear_color = e.clear_color;
    h.clear_depth = e.clear_depth;
    return h;
  }

  comp::viewport_environment
  to_class () const
  {
    comp::viewport_environment e;
    e.skybox_use_global = skybox_use_global;
    e.skybox = skybox;
    e.skybox_rotation_use_global = skybox_rotation_use_global;
    e.skybox_rotation = skybox_rotation;
    e.ambient_use_global = ambient_use_global;
    e.ambient_color = ambient_color;
    e.ambient_intensity = ambient_intensity;
    e.ibl_use_global = ibl_use_global;
    e.ibl_intensity = ibl_intensity;
    e.exposure_use_global = exposure_use_global;
    e.exposure = exposure;
    e.bloom_use_global = bloom_use_global;
    e.bloom_enabled = bloom_enabled;
    e.bloom_threshold = bloom_threshold;
    e.bloom_knee = bloom_knee;
    e.bloom_intensity = bloom_intensity;
    e.ssao_use_global = ssao_use_global;
    e.ssao_enabled = ssao_enabled;
    e.ssao_radius = ssao_radius;
    e.ssao_bias = ssao_bias;
    e.ssao_power = ssao_power;
    e.ssao_intensity = ssao_intensity;
    e.clear_color_enabled = clear_color_enabled;
    e.clear_color_use_global = clear_color_use_global;
    e.clear_color = clear_color;
    e.clear_depth = clear_depth;
    return e;
  }
};

// =============================================================================
// viewport_settings (shared root/sub settings, R1)
// =============================================================================
struct ViewportSettingsHelper
{
  int scale_mode = 2;    // viewport
  int aspect_policy = 0; // ignore
  int filter = 1;        // linear
  bool filter_use_global = true;
  int msaa = 1;
  bool msaa_use_global = true;
  float render_scale = 1.0F;
  bool render_scale_use_global = true;
  ViewportEnvironmentHelper env;
  int update_mode = 2; // when_visible
  int clear_mode = 0;  // always
  bool shadow_caster = true;
  std::uint32_t cull_mask = 0xFFFFFFFFU;

  static ViewportSettingsHelper
  from_class (const comp::viewport_settings &s)
  {
    ViewportSettingsHelper h;
    h.scale_mode = static_cast<int> (s.scale_mode);
    h.aspect_policy = static_cast<int> (s.aspect_policy);
    h.filter = static_cast<int> (s.filter);
    h.filter_use_global = s.filter_use_global;
    h.msaa = s.msaa;
    h.msaa_use_global = s.msaa_use_global;
    h.render_scale = s.render_scale;
    h.render_scale_use_global = s.render_scale_use_global;
    h.env = ViewportEnvironmentHelper::from_class (s.env);
    h.update_mode = static_cast<int> (s.update_mode);
    h.clear_mode = static_cast<int> (s.clear_mode);
    h.shadow_caster = s.shadow_caster;
    h.cull_mask = s.cull_mask;
    return h;
  }

  comp::viewport_settings
  to_class () const
  {
    comp::viewport_settings s;
    s.scale_mode = static_cast<comp::viewport_scale_mode> (scale_mode);
    s.aspect_policy = static_cast<comp::viewport_aspect_policy> (aspect_policy);
    s.filter = static_cast<comp::viewport_filter> (filter);
    s.filter_use_global = filter_use_global;
    s.msaa = msaa;
    s.msaa_use_global = msaa_use_global;
    s.render_scale = render_scale;
    s.render_scale_use_global = render_scale_use_global;
    s.env = env.to_class ();
    s.update_mode = static_cast<comp::viewport_update_mode> (update_mode);
    s.clear_mode = static_cast<comp::viewport_clear_mode> (clear_mode);
    s.shadow_caster = shadow_caster;
    s.cull_mask = cull_mask;
    return s;
  }
};

// =============================================================================
// subviewport — complex nested structs
// =============================================================================
struct SubviewportHelper
{
  math::vec2f size{ 320.0F, 180.0F };
  math::vec2f world_quad_size{ 1.0F, 1.0F };
  math::vec2f virtual_size{ 1920.0F, 1080.0F };
  int display_order = 0;
  bool input_routing = true;
  bool hdr = false;
  SubviewportCameraUIHelper camera;
  ViewportSettingsHelper settings;

  static SubviewportHelper
  from_class (const comp::subviewport &s)
  {
    SubviewportHelper h;
    h.size = s.size;
    h.world_quad_size = s.world_quad_size;
    h.virtual_size = s.virtual_size;
    h.display_order = s.display_order;
    h.input_routing = s.input_routing;
    h.hdr = s.hdr;
    h.camera = SubviewportCameraUIHelper::from_class (s.camera);
    h.settings = ViewportSettingsHelper::from_class (s.settings);
    return h;
  }

  comp::subviewport
  to_class () const
  {
    comp::subviewport s;
    s.size = size;
    s.world_quad_size = world_quad_size;
    s.virtual_size = virtual_size;
    s.display_order = display_order;
    s.input_routing = input_routing;
    s.hdr = hdr;
    s.camera = camera.to_class ();
    s.settings = settings.to_class ();
    return s;
  }
};

// =============================================================================
// shader_graph types (gfx)
// =============================================================================

struct GraphPinHelper
{
  std::uint64_t id = 0;
  std::string name;
  int type = 0;
  bool is_input = true;

  static GraphPinHelper
  from_class (const gfx::graph_pin &p)
  {
    GraphPinHelper h;
    h.id = p.id;
    h.name = p.name;
    h.type = static_cast<int> (p.type);
    h.is_input = p.is_input;
    return h;
  }

  gfx::graph_pin
  to_class () const
  {
    gfx::graph_pin p;
    p.id = id;
    p.name = name;
    p.type = static_cast<gfx::graph_pin_type> (type);
    p.is_input = is_input;
    return p;
  }
};

struct GraphLinkHelper
{
  std::uint64_t from_node = 0;
  std::uint64_t from_pin = 0;
  std::uint64_t to_node = 0;
  std::uint64_t to_pin = 0;

  static GraphLinkHelper
  from_class (const gfx::graph_link &l)
  {
    return { l.from_node, l.from_pin, l.to_node, l.to_pin };
  }

  gfx::graph_link
  to_class () const
  {
    gfx::graph_link l;
    l.from_node = from_node;
    l.from_pin = from_pin;
    l.to_node = to_node;
    l.to_pin = to_pin;
    return l;
  }
};

struct GraphNodeHelper
{
  std::uint64_t id = 0;
  std::string name;
  int kind = 0;
  float pos_x = 0.0F;
  float pos_y = 0.0F;
  std::unordered_map<std::string, std::string> properties;
  std::vector<GraphPinHelper> pins;

  static GraphNodeHelper
  from_class (const gfx::graph_node &n)
  {
    GraphNodeHelper h;
    h.id = n.id;
    h.name = n.name;
    h.kind = static_cast<int> (n.kind);
    h.pos_x = n.pos_x;
    h.pos_y = n.pos_y;
    h.properties = n.properties;
    h.pins.reserve (n.pins.size ());
    for (const auto &p : n.pins) {
      h.pins.push_back (GraphPinHelper::from_class (p));
    }
    return h;
  }

  gfx::graph_node
  to_class () const
  {
    gfx::graph_node n;
    n.id = id;
    n.name = name;
    n.kind = static_cast<gfx::graph_node_kind> (kind);
    n.pos_x = pos_x;
    n.pos_y = pos_y;
    n.properties = properties;
    n.pins.reserve (pins.size ());
    for (const auto &p : pins) {
      n.pins.push_back (p.to_class ());
    }
    return n;
  }
};

struct ShaderGraphHelper
{
  std::string name;
  std::vector<GraphNodeHelper> nodes;
  std::vector<GraphLinkHelper> links;

  static ShaderGraphHelper
  from_class (const gfx::shader_graph &g)
  {
    ShaderGraphHelper h;
    h.name = g.name;
    h.nodes.reserve (g.nodes.size ());
    for (const auto &n : g.nodes) {
      h.nodes.push_back (GraphNodeHelper::from_class (n));
    }
    h.links.reserve (g.links.size ());
    for (const auto &l : g.links) {
      h.links.push_back (GraphLinkHelper::from_class (l));
    }
    return h;
  }

  gfx::shader_graph
  to_class () const
  {
    gfx::shader_graph g;
    g.name = name;
    g.nodes.reserve (nodes.size ());
    for (const auto &n : nodes) {
      g.nodes.push_back (n.to_class ());
    }
    g.links.reserve (links.size ());
    for (const auto &l : links) {
      g.links.push_back (l.to_class ());
    }
    return g;
  }
};

// =============================================================================
// Singleton: rendering_manager
// Excludes the runtime cache pointers (renderer / renderer_2d) — those are
// reconstructed from the rendering state, not serialized.
// =============================================================================
struct RenderingManagerHelper
{
  rsc::cubemap_id skybox{};
  math::quatf skybox_rotation{ 0, 0, 0, 1 };
  math::vec3f clear_color{ 0.1F, 0.1F, 0.1F };
  float clear_alpha = 1.0F;
  math::vec3f ambient_color{ 1.0F, 1.0F, 1.0F };
  float ambient_intensity = 0.01F;
  float sun_altitude = 40.0F;
  float sun_azimuth = 135.0F;
  float exposure = 1.0F;
  float bloom_threshold = 1.0F;
  float bloom_knee = 0.5F;
  float bloom_intensity = 1.0F;
  float ibl_intensity = 1.0F;
  bool ssao_enabled = true;
  float ssao_radius = 0.6F;
  float ssao_bias = 0.025F;
  float ssao_power = 1.25F;
  float ssao_intensity = 1.0F;
  math::vec3f outline_color{ 1.0F, 0.65F, 0.1F };
  float outline_alpha = 1.0F;
  float outline_width = 0.035F;
  float shadow_bias = 0.0025F;
  float shadow_strength = 1.0F;
  ViewportSettingsHelper root_viewport;
  std::uint32_t render_viewport = 0xFFFFFFFF;
  math::vec2f root_viewport_virtual_size{ 1920.0F, 1080.0F };

  static RenderingManagerHelper
  from_class (const comp::singl::rendering_manager &r)
  {
    RenderingManagerHelper h;
    h.skybox = r.skybox;
    h.skybox_rotation = r.skybox_rotation;
    h.clear_color = r.clear_color;
    h.clear_alpha = r.clear_alpha;
    h.ambient_color = r.ambient_color;
    h.ambient_intensity = r.ambient_intensity;
    h.sun_altitude = r.sun_altitude;
    h.sun_azimuth = r.sun_azimuth;
    h.exposure = r.exposure;
    h.bloom_threshold = r.bloom_threshold;
    h.bloom_knee = r.bloom_knee;
    h.bloom_intensity = r.bloom_intensity;
    h.ibl_intensity = r.ibl_intensity;
    h.ssao_enabled = r.ssao_enabled;
    h.ssao_radius = r.ssao_radius;
    h.ssao_bias = r.ssao_bias;
    h.ssao_power = r.ssao_power;
    h.ssao_intensity = r.ssao_intensity;
    h.outline_color = r.outline_color;
    h.outline_alpha = r.outline_alpha;
    h.outline_width = r.outline_width;
    h.shadow_bias = r.shadow_bias;
    h.shadow_strength = r.shadow_strength;
    h.root_viewport = ViewportSettingsHelper::from_class (r.root_viewport);
    h.render_viewport = (r.render_viewport == entt::null)
                            ? serialized_null_entity
                            : static_cast<std::uint32_t> (
                                  entt::to_integral (r.render_viewport));
    h.root_viewport_virtual_size = r.root_viewport_virtual_size;
    return h;
  }

  comp::singl::rendering_manager
  to_class () const
  {
    comp::singl::rendering_manager r;
    r.skybox = skybox;
    r.skybox_rotation = skybox_rotation;
    r.clear_color = clear_color;
    r.clear_alpha = clear_alpha;
    r.ambient_color = ambient_color;
    r.ambient_intensity = ambient_intensity;
    r.sun_altitude = sun_altitude;
    r.sun_azimuth = sun_azimuth;
    r.exposure = exposure;
    r.bloom_threshold = bloom_threshold;
    r.bloom_knee = bloom_knee;
    r.bloom_intensity = bloom_intensity;
    r.ibl_intensity = ibl_intensity;
    r.ssao_enabled = ssao_enabled;
    r.ssao_radius = ssao_radius;
    r.ssao_bias = ssao_bias;
    r.ssao_power = ssao_power;
    r.ssao_intensity = ssao_intensity;
    r.outline_color = outline_color;
    r.outline_alpha = outline_alpha;
    r.outline_width = outline_width;
    r.shadow_bias = shadow_bias;
    r.shadow_strength = shadow_strength;
    r.root_viewport = root_viewport.to_class ();
    r.render_viewport = (render_viewport == serialized_null_entity)
                            ? entt::null
                            : entt::entity{ render_viewport };
    r.root_viewport_virtual_size = root_viewport_virtual_size;
    return r;
  }
};

// =============================================================================
// Singleton: physics_manager
// Excludes the runtime cache pointer (engine) — reconstructed lazily.
// =============================================================================
struct PhysicsManagerHelper
{
  float gravity = -9.8F;
  float fixed_timestep = 1.0F / 60.0F;
  float max_frame_time = 0.25F;
  int max_substeps = 5;
  bool show_debug = false;

  static PhysicsManagerHelper
  from_class (const comp::singl::physics_manager &p)
  {
    return { p.gravity, p.fixed_timestep, p.max_frame_time, p.max_substeps,
             p.show_debug };
  }

  comp::singl::physics_manager
  to_class () const
  {
    comp::singl::physics_manager p;
    p.gravity = gravity;
    p.fixed_timestep = fixed_timestep;
    p.max_frame_time = max_frame_time;
    p.max_substeps = max_substeps;
    p.show_debug = show_debug;
    return p;
  }
};

} // namespace wsl::serialize

// =============================================================================
// rfl CustomParser registrations — one per component type
// =============================================================================
namespace rfl::parsing
{

#define WSL_RFL_COMPONENT_PARSER(T, Helper)                                    \
  template <class R, class W, class P>                                         \
  struct Parser<R, W, wsl::comp::T, P>                                         \
      : public CustomParser<R, W, P, wsl::comp::T, wsl::serialize::Helper>     \
  {                                                                            \
  };

#define WSL_RFL_FULL_PARSER(QualifiedT, Helper)                                \
  template <class R, class W, class P>                                         \
  struct Parser<R, W, QualifiedT, P>                                           \
      : public CustomParser<R, W, P, QualifiedT, wsl::serialize::Helper>       \
  {                                                                            \
  };

WSL_RFL_COMPONENT_PARSER (transform, TransformHelper)
WSL_RFL_COMPONENT_PARSER (hierarchy, HierarchyHelper)
WSL_RFL_COMPONENT_PARSER (point_light, PointLightHelper)
WSL_RFL_COMPONENT_PARSER (directional_light, DirectionalLightHelper)
WSL_RFL_COMPONENT_PARSER (spot_light, SpotLightHelper)
WSL_RFL_COMPONENT_PARSER (world_transform, WorldTransformHelper)
WSL_RFL_COMPONENT_PARSER (transform_2d, Transform2DHelper)
WSL_RFL_COMPONENT_PARSER (sprite_2d, Sprite2DHelper)
WSL_RFL_COMPONENT_PARSER (prefab_instance, PrefabInstanceHelper)
WSL_RFL_COMPONENT_PARSER (camera, CameraHelper)
WSL_RFL_COMPONENT_PARSER (character_body, CharacterBodyHelper)
WSL_RFL_COMPONENT_PARSER (area, Area3DHelper)
WSL_RFL_COMPONENT_PARSER (rigid_body, RigidBodyHelper)
WSL_RFL_COMPONENT_PARSER (audio, AudioHelper)
WSL_RFL_COMPONENT_PARSER (model_instance_3d, ModelInstance3DHelper)
WSL_RFL_COMPONENT_PARSER (animator, AnimatorHelper)
WSL_RFL_COMPONENT_PARSER (subviewport, SubviewportHelper)
WSL_RFL_FULL_PARSER (wsl::comp::singl::rendering_manager,
                     RenderingManagerHelper)
WSL_RFL_FULL_PARSER (wsl::comp::singl::physics_manager, PhysicsManagerHelper)
WSL_RFL_FULL_PARSER (wsl::gfx::graph_pin, GraphPinHelper)
WSL_RFL_FULL_PARSER (wsl::gfx::graph_link, GraphLinkHelper)
WSL_RFL_FULL_PARSER (wsl::gfx::graph_node, GraphNodeHelper)
WSL_RFL_FULL_PARSER (wsl::gfx::shader_graph, ShaderGraphHelper)

} // namespace rfl::parsing

#ifdef WSL_ADAPTERS_POP_FAR
#pragma pop_macro("far")
#undef WSL_ADAPTERS_POP_FAR
#endif
#ifdef WSL_ADAPTERS_POP_NEAR
#pragma pop_macro("near")
#undef WSL_ADAPTERS_POP_NEAR
#endif
