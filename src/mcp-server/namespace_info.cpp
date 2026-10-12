#include "namespace_info.hpp"
#include "mcp_helpers.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <unordered_map>

namespace wsl::mcp_server
{

namespace
{

// ---------------------------------------------------------------------------
// Documentation for each namespace
// ---------------------------------------------------------------------------

struct ns_doc
{
  std::string name;
  std::string brief;
  std::string detail;
};

const std::unordered_map<std::string, ns_doc> &
get_namespace_docs ()
{
  static const std::unordered_map<std::string, ns_doc> docs = {

    // ===================================================================
    // comp
    // ===================================================================
    { "comp",
      { "comp – ECS Components (entity data)",
        "Data components attached to entities. Define what an entity *is*: its "
        "transform, "
        "camera, physics body, lights, audio source, etc.",

        R"doc(== wsl::comp — ECS Components ==

The ECS data layer. Components are plain structs attached to entities via
the EnTT registry. They carry no logic — only state.

── World Components (per-entity) ──

hierarchy (hierarchy.hpp)
  Parent/child/sibling links forming the entity tree.
  Fields: parent (entt::entity), first (entt::entity), next (entt::entity).
  Use to build scene graphs: parent moves, children follow.

world_transform (world_transform.hpp)
  Cached world-space matrix. Updated automatically by transform_system
  from the hierarchy + local transform chain.

transform (transform.hpp)
  Local position (vec3f), rotation (quatf), scale (vec3f).
  model() returns the local-to-parent 4x4 matrix.
  Use set_rotation_xyz(degrees) for Euler angle input.

model_instance_3d (model_instance_3d.hpp)
  References a gfx::model_3d for rendering. Holds model_id + material overrides.

camera (camera.hpp)
  Projection parameters only: fov (degrees), near, far, aspect_ratio.
  View matrix derived from world_transform (inverse of world).
  Static helper: camera::view(world_transform) → glm::mat4

point_light (point_light.hpp)
  Point light with color, intensity, range.

spot_light (spot_light.hpp)
  Cone light with inner/outer angle, color, intensity, range.

directional_light (directional_light.hpp)
  Sun-like light with direction derived from transform rotation.

rigid_body (rigid_body.hpp)
  Physics body: shape (box/sphere), motion type (Static/Kinematic/Dynamic),
  collision layer/mask, friction, restitution.
  Has create_body/destroy_body/rebuild_body lifecycle tied to physics_engine.

area (area3d.hpp)
  Trigger zone in 3D space. Generates overlap events via physics sensors.

character_body (character_body.hpp)
  Character controller physics body with movement and collision response.

audio (audio.hpp)
  Audio source component referencing a sound asset.

prefab_instance (prefab_instance.hpp)
  Links an entity to its prefab scene for re-syncing.

── Singleton Components (registry-scoped, in singl/) ──

runtime_context
  Global runtime state: play mode flag, project paths, frame counter.

editor_context
  Editor session state: selected entity, viewport config, tool mode.

rendering_manager
  Renderer config, shadow map settings, pipeline cache.

physics_manager
  Holds the phys::engine pointer for the scene.

ui_manager
  Docking layout, window visibility, theme.

skybox_instance_3d
  Active skybox cubemap reference and tint.

── Typical Workflow ──

  1. Create entity:  registry.create()
  2. Add components: registry.emplace<comp::transform>(entity, pos)
  3. Modify data:    registry.get<comp::transform>(entity).position.y += 1
  4. Query:          auto view = registry.view<comp::transform, comp::camera>()

── Registration (for serialization + editor) ──

  All component types are listed in component_types / singleton_types
  in components.hpp. Each component should define:
    • register_meta() — EnTT reflection for property grid
    • serialize(Archive&) — Cereal serialization

  Register at runtime via:
    reg::component_registry::register_world_component<T>()
    reg::singleton_registry::register_singleton_component<T>()
)doc" } },

    // ===================================================================
    // sys
    // ===================================================================
    { "sys",
      { "sys – ECS Systems (game logic)",
        "Systems contain the behavior that operates on entities. User "
        "systems are authored in Daslang only (C++ user systems are not "
        "supported): create with 'sys create', load with 'script reload', "
        "attach with 'sys add'.",

        R"doc(== wsl::sys — ECS Systems ==

The logic layer. Systems iterate over entity components and implement
behavior.

IMPORTANT — authoring rules:
  • User systems are written in DASLANG ONLY. Files live under the
    project's src/systems directory (one system per .das file).
  • User C++ runtime components/systems are NOT supported; the runtime
    module loader rejects C++ sources.

── Lifecycle of a user system (all via weasel-cli) ──

  1. sys create ball_controller_system
       → generates src/systems/ball_controller_system.das containing
         'class System : EcsSystem' with an empty on_update override.
  2. Edit the .das file and implement on_update.
  3. script reload
       → compiles all project .das sources and registers them.
       ('script status' shows the current load state, registry counts,
        cache path, and last error if a compile failed.)
  4. sys avail
       → lists registered user systems by display name.
  5. sys add "Ball Controller System"
       → attaches an instance to the active scene (quote names that
         contain spaces).

── Display-name derivation ──

The file stem defines the registered display name:
  foo_bar_system.das → type 'foo_bar_system' → display 'Foo Bar System'
(underscores become spaces, first letter capitalized).

── Daslang system interface (weasel_ecs module) ──

class EcsSystem {
    def on_init() : void { }          // once after load
    def on_update(dt : float) : void { }   // every frame while playing
    def on_inactive() : void { }      // cleanup
}

Input is pull-based: read keyboard/mouse each frame with helpers from
the weasel_api module (e.g. is_key_pressed(scancode)).

── Minimal movement/physics example ──

  options gen2
  require weasel_ecs
  require weasel_helpers

  class System : EcsSystem {
      def override on_update(dt : float) : void {
          each_entity_id_with([ TYPE_RIGID_BODY() ]) <| $(e : uint) {
              apply_force(e, 0.0, 6500.0, 0.0) // ~g lift for a 524 kg ball
          }
      }
  }

Notes:
  • each_entity_id_with([TYPE_X(), ...]) visits entities that own ALL
    listed components and passes the entity id to the block — use it
    when you need the id (apply_force, apply_impulse, ...).
  • query() $(t : Transform&) { t.position.y += dt } binds LIVE
    component references for in-place writes when no id is needed.
  • Physics bodies use density 1000 kg/m^3: a builtin://sphere
    (radius 0.5) masses ~524 kg, so forces must be in the thousands.

── Built-in Systems ──

transform_system
  Recomputes world_transform for every entity with transform + hierarchy.
  Runs every frame. Required for all scene graph operations.

physics_system
  Steps the physics engine. Syncs rigid_body ↔ transform:
    • Before step: copies transform → physics body position/rotation
    • After step:  copies physics body → world_transform
  Dynamic bodies own their transform during simulation; writing
  transform.position directly on them mid-play is ignored (use forces/
  impulses, or remove + re-add the rigid_body component to teleport).

lighting_system
  Scans point_light, spot_light, directional_light components and
  uploads lighting UBO data to the GPU.

shadow_system
  Renders shadow-map depth passes for directional lights.

render_3d_system
  Collects entities with model_instance_3d + world_transform,
  sorts by material, and issues GPU draw calls.

render_ui_system
  Renders the ImGui overlay. Intercepts ImGui draw data and
  records GPU commands.

render_frame
  Manages frame lifecycle: begin/end render passes, swapchain present.

skybox_system
  Renders a cubemap skybox behind all other geometry.

audio_system
  Manages audio source playback, 3D positioning, and mixer state.

── Default Scene State ──

Every scene created via scene_manager::create_scene() starts with:

  • An empty per-scene systems vector (no systems attached)
  • Registry context bindings: runtime_context*, scene_manager*,
    resource_manager_view*, editor_context*, ui_manager*
  • Core singleton components auto-emplaced in the registry:
    - Runtime Context      (play mode, frame counter)
    - Scene Manager        (scene lifecycle)
    - Resource Manager     (asset loading/caching)
    - UI Manager           (docking layout, themes)
    - Rendering Manager    (renderer config, shadow settings)
    - Physics Manager      (physics engine pointer)

No per-scene systems are pre-attached to a newly created scene.
Use sys add <name> to attach user systems.

── Global Core Systems ──

Separate from per-scene systems, the engine maintains 8 global
system instances in core_systems that always run each frame on
the active scene's registry, regardless of which per-scene systems
are attached:

  1. Audio     — audio source playback and 3D positioning
  2. Physics   — steps physics simulation, syncs rigid_body ↔ transform
  3. Transform — recomputes world_transform from hierarchy + transforms
  4. Shadow    — renders shadow-map depth passes
  5. Lighting  — uploads light data to GPU UBO
  6. Skybox    — renders cubemap background
  7. 3D Render — collects models and issues draw calls
  8. UI        — renders ImGui overlay

Core systems are always present; sys add is only needed for custom
systems created via sys create.

── Per-Scene Systems ──

Per-scene systems are optional user systems stored in the scene's
systems vector. They run in addition to (and at a different point in
the frame from) the global core systems. Attach them with
'sys add "<display name>"' after loading them with 'script reload'.
)doc" } },

    // ===================================================================
    // rsc
    // ===================================================================
    { "rsc",
      { "rsc – Resource Management",
        "Loading, caching, serializing and managing assets: models, textures, "
        "scenes, shaders, audio, fonts, and UI layouts.",

        R"doc(== wsl::rsc — Resource Management ==

Central resource system with async loading, GPU upload staging, scene
lifecycle, and project management.

── Resource Manager (resource_manager.hpp) ──

The main entry point. Tracks all assets by typed IDs:

  Asset types: model, image, cubemap, scene, shader, audio, ui_layout, font
  ID types:    model_id, image_id, cubemap_id, scene_id, shader_id, etc.

Typical lifecycle:
  1. Import:  res_mgr.import_model("models/foo.glb")
  2. Load:    res_mgr.load(model_id) → returns entt::resource_handle
  3. Use:     handle->ensure_gpu_buffers(ctx)
  4. Unload:  res_mgr.unload(model_id)

Import vs Register:
  • import = register + auto-load (async)
  • register = just assign an ID, no loading

Async pipeline:
  Models are loaded on a worker thread → CPU data → GPU upload session
  → finalized by calling update_async_uploads() on main thread.

── Scene System (scene.hpp, scene_manager.hpp) ──

A scene owns an entt::registry + a list of systems:

  rsc::scene scene(runtime_ctx, editor_ctx, "Level1");
  scene.add_system<sys::physics_system>();
  scene.add_system<sys::transform_system>();
  scene.init();

  scene.update(dt);
  scene.stop_and_clear();

scene_manager handles scene activation, prefab instantiation, and
multi-scene workflows:

  scene_manager mgr;
  mgr.add_scene("Level1", std::move(scene1));
  mgr.activate_scene("Level1");

Prefabs: scenes can be marked as prefabs. Instantiate with:
  res_mgr.instantiate_prefab(prefab_scene_id, parent_entity);

── Serialization ──

Scenes serialize via Cereal (binary or JSON). Snapshot serializer
captures all registered components + singletons.

Resource references use io::resource_ref { resource_type, entt::id_type }
to decouple scenes from concrete asset paths.

── Project System ──

  rsc::project holds metadata: name, version, default scene, etc.
  Create: res_mgr.new_project(project)
  Load:   res_mgr.load_project("path/to/wslpro.json")
  Access: res_mgr.current_project() → shared_ptr<project>

── Built-in Primitives ──

  The engine registers built-in procedural models accessible via
  builtin:// URIs. These require no asset files on disk:

    builtin://cube       — unit cube (1×1×1), Box shape
    builtin://sphere     — unit sphere, radius 0.5
    builtin://cylinder   — unit cylinder, height 1, radius 0.5
    builtin://prism      — triangular prism
    builtin://quad       — unit quad (2D, 1×1)

  Use with model_instance_3d.model_id via CLI:
    comp set <id> model_instance_3d model_id builtin://cube

  Or in C++:
    res_mgr.register_model("builtin://cube") → model_id

  Procedural mesh generators (gfx/model_3d):
    model_3d::make_unit_quad(), make_unit_cube(),
    make_unit_sphere(), make_unit_cylinder(), make_unit_prism()

── resource_manager_view ──

  Singleton component wrapping a pointer to the resource_manager.
  Provides an ImGui inspector for debugging.
)doc" } },

    // ===================================================================
    // phys
    // ===================================================================
    { "phys",
      { "phys – Physics Engine (Box3D wrapper)",
        "Rigid body simulation, collision detection, sensors, and ray "
        "casting backed by Box3D.",

        R"doc(== wsl::phys — Physics (Box3D Integration) ==

A C++ wrapper around Box3D providing body creation, simulation
stepping, collision queries, and sensor overlap events.

── phys::engine (physics_engine.hpp) ──

  Owns the Box3D world and simulation state.

  Configuration:
    set_gravity(double)     — default -9.8
    set_fixed_step(double)  — default 1/60
    set_max_substeps(int)   — default 5
    set_max_frame_time(double) — clamp to avoid spiral of death

  Simulation:
    step(dt) — advances physics by accumulating and sub-stepping

── Body Lifecycle ──

  Bodies are created/removed via the neutral engine API:

    auto id = engine.create_body(desc);
    engine.destroy_body(id);

  Motion types:  Static (immovable), Kinematic (user-controlled), Dynamic (simulated)
  Allowed DOFs:  All, TranslationX/Y/Z, RotationX/Y/Z (bitfield)

── Sensors ──

  Bodies can be registered as sensors. Sensor overlap generates events:

    physics.register_sensor(body_id);
    physics.step(dt);
    auto events = physics.drain_sensor_events();
    // events: { sensor, other, entered (bool) }

── Querying ──

    // Ray casts and shape queries via the physics engine API
    auto result = engine.ray_cast(origin, direction, max_dist);

── Integration with ECS ──

  comp::rigid_body wraps body lifecycle. physics_system syncs:
    • Before step: transform → body position/rotation
    • After step:  body → world_transform
  Rebuild body when shape/dims/motion change via:
    rigid_body.rebuild_body(engine, scale);
)doc" } },

    // ===================================================================
    // gfx
    // ===================================================================
    { "gfx",
      { "gfx – Graphics / Rendering (SDL_GPU)",
        "GPU-powered 3D rendering: meshes, materials, shaders, HDR bloom, "
        "shadow maps, and the full frame pipeline.",

        R"doc(== wsl::gfx — Graphics & Rendering ==

Built on SDL_GPU. Supports HDR rendering, MSAA, bloom post-processing,
shadow mapping, and immediate-mode UI overlay via ImGui.

── render_window (render_window.hpp) ──

  The central rendering object. Owns the SDL_Window, GPU device, swapchain,
  depth buffer, HDR textures, and bloom ping-pong buffers.

    gfx::render_window window("Viewport", 1920, 1080, render_ctx, res_mgr);

  Render passes (call in order each frame):
    1. begin_3d_pass()   — clears depth, sets up HDR color target
    2. ... draw models ...
    3. end_3d_pass()     — resolves MSAA → HDR texture
    4. postprocess_hdr_bloom() — downsample → blur × N → composite
    5. begin_ui_pass()   — ImGui overlay
    6. end_ui_pass()     — present to swapchain

── model_3d (model_3d.hpp) ──

  GPU-ready model resource with vertex and index buffers.

    model->build_gpu_buffers(ctx);
    model->bind(pass);
    SDL_DrawGPUIndexedPrimitives(pass, model->index_count, 1, 0, 0, 0);

  Procedural generators (no asset file needed):
    model_3d::make_unit_quad()
    model_3d::make_unit_cube()
    model_3d::make_unit_sphere()
    model_3d::make_unit_cylinder()
    model_3d::make_unit_prism()

  LOD support: lod_groups contain named groups of mesh* levels.

── mesh, primitive, vertex (mesh.hpp) ──

  mesh = vector<primitive>
  primitive = first_index + vertices + indices + material
  vertex = pos (vec3) + normal (vec3) + uv (vec2) + tangent (vec4)

── material (material.hpp) ──

  PBR material parameters:
    base_color_factor (vec4) — albedo + alpha
    metallic_factor, roughness_factor (float)
    emissive_factor (vec3)
    base_color_tex, metallic_roughness_tex, normal_tex,
    occlusion_tex, emissive_tex — SDL_GPUTexture pointers

── shader (shader.hpp) ──

  Static loader for SDL_GPUShader:
    shader::load(device, path, stage, num_uniform_buffers, num_samplers)
    shader::load_from_manager(device, res_mgr, shader_id, stage, buffers, samplers)
    shader::load_ui_shader(device, path, stage)
    shader::load_skybox_shader(device, path, stage)
    shader::native_format() → SDL_GPUShaderFormat (SPIR-V/MSL/DXIL)

── scene_renderer (scene_renderer.hpp) ──

  High-level collector. Scans entities with model_instance_3d +
  world_transform, calls model->ensure_gpu_buffers(), and records
  draw commands onto the active render pass.

── Lighting UBO ──

  gfx::lighting_ubo is the shared uniform block for lighting:
    light_pos, camera_pos, ambient, diffuse, specular, shininess

── Render Pipeline Summary ──

  1. Build draw data  (systems collect visible entities)
  2. Prepare GPU rsc   (upload textures, buffers)
  3. Shadow pass       (directional light depth)
  4. 3D pass           (opaque → transparent)
  5. Bloom post-fx     (downsample → blur × N → composite)
  6. UI pass           (ImGui)
  7. Present
)doc" } },

    // ===================================================================
    // math
    // ===================================================================
    { "math",
      { "math – Math Types & Utilities",
        "Core math types (vec3f, quatf) with automatic conversion between "
        "GLM, and ImGui. Also includes MikkTSpace tangent computation.",

        R"doc(== wsl::math — Math Types ==

Lightweight math types used as component fields across the engine.
Designed for seamless interop between GLM and ImGui.

── vec3f (vector.hpp) ──

  3-component float vector (x, y, z).

    math::vec3f v{ 1, 2, 3 };

    // Implicit conversions to/from GLM
    glm::vec3 gv = v;
    math::vec3f v2 = glm::vec3{4,5,6};

    // Operators
    v += glm::vec3{0, 1, 0};
    v -= glm::vec3{1, 0, 0};

    // Editor: ImGui drag-float with colored stripes (X=red, Y=green, Z=blue)
    v.custom_inspect("label");

    // Serialization: handled by rfl adapters (serialize/adapters.hpp)
    rfl::Field<"position", &Transform::pos>()

    // EnTT meta reflection
    v.register_meta();

── quatf (vector.hpp) ──

  Quaternion (x, y, z, w), default identity (0,0,0,1).

    math::quatf q{ 0, 0, 0, 1 };

    // Conversion
    glm::quat gq = q;            // implicit
    math::quatf q2 = glm::quat{};

    // Serialization: rfl adapters (CustomParser in serialize/adapters.hpp)

── MikkTSpace (mikktspace*.hpp) ──

  Implementation of the MikkTSpace algorithm for computing tangent-space
  basis vectors from vertex positions, normals, and UVs.

  Used internally by model_loader to produce tangent data required for
  normal-mapped materials. Not typically called directly.

── Design Notes ──

  These types exist to decouple component definitions from direct GLM
  dependencies. Each type provides:
    • Implicit conversion operators to/from the major math libraries
    • Cereal serialize() for scene snapshot serialization
    • EnTT register_meta() for editor property-grid reflection
    • ImGui custom_inspect() for in-editor manipulation
)doc" } },

    // ===================================================================
    // reg
    // ===================================================================
    { "reg",
      { "reg – Registry & Registration Infrastructure",
        "Central registration for components, singletons, systems, and "
        "events. Enables serialization, editor reflection, and dynamic "
        "queries.",

        R"doc(== wsl::reg — Registry & Registration ==

The registration layer bridges the ECS core with editor, serialization,
and runtime code. It makes the engine introspectable and extensible.

── component_registry (component_registry.hpp) ──

  Registers world component types (per-entity) with function pointers for
  generic operations:

    reg::component_registry reg;
    reg.register_world_component<comp::transform>();

  Each descriptor provides:
    • contains(registry, entity)     → bool
    • emplace_default(registry, ent) → bool
    • remove(registry, entity)       → bool
    • copy(src_reg, src, dst_reg, dst) → void
    • save_binary / load_binary / save_json / load_json

  Querying:
    reg.find("Transform")                  → descriptor*
    reg.find(entt::id_type)                → descriptor*
    reg.get_world_components(order)        → vector<descriptor*>
    reg.get_addable_world_components(reg, entity) → components not yet on entity

── singleton_registry (singleton_registry.hpp) ──

  Same pattern but for registry-scoped singleton components:

    sing_reg.register_singleton_component<comp::singl::runtime_context>(
        { .core = true });

  Key methods:
    apply_core_singleton_components(registry)  — ensure core singletons exist
    reset_scene_singleton_components(registry) — remove non-core singletons
    save_singleton_binary/json / load_singleton_binary/json

── system_factory_registry (system_factory_registry.hpp) ──

  Maps system names to factory functions:

    sys_reg.register_system<sys::physics_system>("Physics");
    sys_reg.register_system<sys::transform_system>("Transform");

  Dynamic system creation:
    auto sys = sys_reg.create("Physics", scene);  // → unique_ptr<ecs_system>

  Dependencies and conflicts:
    sys_reg.declare_system_dependency<ShadowSystem, LightingSystem>();
    sys_reg.declare_system_conflict<MyPhysics, PhysicsSystem>();

── registry_queries (registry_queries.hpp) ──

   Cross-concept query interface:
    • get_matching_iterations(registry, entity) — systems that process this entity
    • get_matching_systems(registry, entity) — systems relevant to this entity
    • find_event_sources_owned_by_system(system_id) — event sources an emitter declares
    • find_event_sinks_owned_by_system(system_id) — event sinks a handler declares
    • find_connections_for_event(event_id)
    • find_connections_for_system(system_id)

── Signal System (event/event_hub.hpp) ──

  The observer event hub (renamed from "signal hub"); events carry no
  component/entity relation — entity identity lives in the event payload:
    • declare_event_source<Event, EmitterSystem>() — declare an emitter (one owner)
    • declare_event_sink<Event, HandlerSystem>(name, invoke) — declare a handler
    • event_source<Event, EmitterSystem>{hub}.emit(args...) — fire to connected sinks
    • event_source<Event, Emitter>.add_listener(event_sink<Event, Handler>) — connect
    • event_hub.connect(event_type_id, system_type_id, handler_name) — runtime connect
    • wsl::event::emit(hub, Event{...}) — free emit helper

  Buffered / message events live in a separate, pull-only subsystem
  (event/message_bus.hpp): post<T>(...) enqueues; message_reader<T> pulls each
  frame after message_bus.drain(registry). Used internally by
  ecs_system_t::register_iteration() and by the editor event inspector.

── Detail (detail/registry_helpers.hpp) ──

  Internal helpers used by all three registries:
    • ensure_meta_registered<T>() — calls T::register_meta() if defined
    • resolve_display_name<T>() — fallback chain for human-readable names
    • make_archive_name() — naming convention for Cereal archive nodes
)doc" } }
  };

  return docs;
}

// Case-insensitive key lookup
const ns_doc *
find_namespace (const std::string &name)
{
  std::string key = name;
  for (auto &c : key)
    c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));

  for (const auto &[k, v] : get_namespace_docs ()) {
    if (k == key)
      return &v;
  }
  return nullptr;
}

} // namespace

// ---------------------------------------------------------------------------
// Handlers
// ---------------------------------------------------------------------------

agentsdk::result<mcp::tool_result, mcp::mcp_error>
handle_list_namespaces (const std::string &args_json)
{
  (void)args_json;
  std::ostringstream oss;
  oss << "Weasel Engine Namespaces\n\n";
  oss << "The engine core library (wsl) is organized into the following "
         "namespaces. Use describe_namespace with any of these names to get "
         "detailed documentation.\n\n";

  for (const auto &[key, doc] : get_namespace_docs ()) {
    oss << "  " << doc.name << "\n";
    oss << "    " << doc.brief << "\n\n";
  }

  oss << "To get started writing a game:\n";
  oss << "  1. describe_namespace('comp') — entity components (what things "
         "ARE)\n";
  oss << "  2. describe_namespace('sys')  — systems (what things DO)\n";
  oss << "  3. describe_namespace('rsc')  — resources & scenes\n";
  oss << "  4. describe_namespace('phys') — physics simulation\n";
  oss << "  5. describe_namespace('gfx')  — rendering pipeline\n";
  oss << "  6. describe_namespace('math') — vector/quaternion types\n";
  oss << "  7. describe_namespace('reg')  — registration infrastructure\n";

  return text_result (oss.str ());
}

agentsdk::result<mcp::tool_result, mcp::mcp_error>
handle_describe_namespace (const std::string &args_json)
{
  std::string name;
  std::string error;
  if (!get_string_arg (args_json, "name", name, error)) {
    return param_error (error);
  }

  const auto *doc = find_namespace (name);

  if (!doc) {
    std::string valid;
    for (const auto &[k, v] : get_namespace_docs ()) {
      if (!valid.empty ())
        valid += ", ";
      valid += k;
    }
    return param_error ("Unknown namespace: " + name + ". Valid: " + valid);
  }

  return text_result (doc->detail);
}

} // namespace wsl::mcp_server
