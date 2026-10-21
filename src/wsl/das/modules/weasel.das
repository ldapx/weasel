options gen2
options remove_unused_symbols = false
module weasel

require weasel_api public
require daslib/ast
require daslib/ast_boost
require daslib/templates_boost
require daslib/macro_boost
require daslib/rtti

// One import is the whole gameplay surface: native bindings and the legacy
// component proxies are implementation details of this module, so scripts
// `require weasel` and nothing else.
//
// Frame time for gameplay scripts. `delta` is the scaled step consumed by
// gameplay (zero while paused); `unscaled_delta` is the raw engine dt.
// `elapsed` is wall-clock seconds since process start. `fixed_delta` is the
// physics step; `fixed_elapsed` accumulates completed fixed steps;
// `frame_index` counts renderer frames. Pause and scale are engine-owned
// gameplay state on the runtime context (one per game instance): while
// paused, fixed steps and physics stop, but variable/late passes keep
// running for UI and menus.
struct Time {
    delta : float
    unscaled_delta : float
    fixed_delta : float
    fixed_elapsed : float
    elapsed : float
    frame_index : uint64

    // Recomputes every field from the raw engine dt. Called by `System`
    // once per lifecycle entry; one-off scripts can call it on an owned
    // value. (Field access on `ctx.time` is fine in const contexts, but
    // method calls are not, so `System` hooks take `var ctx`.)
    def refresh(dt : float) : void {
        var scale = time_scale()
        var is_paused_now = paused()
        unscaled_delta = dt
        if (is_paused_now) {
            delta = 0.0
        } else {
            delta = dt * scale
        }
        fixed_delta = _time_fixed_delta()
        fixed_elapsed = _time_fixed_elapsed()
        frame_index = _time_frame_index()
        elapsed = get_elapsed_time()
    }

    // Pause and scale are engine-owned gameplay state (one per game
    // instance), so these are accessors, not local fields: while paused,
    // fixed steps and physics stop but variable/late passes keep running
    // for UI and menus. Negative scales clamp to zero.
    def paused() : bool {
        return _time_is_paused()
    }

    def set_paused(paused : bool) : void {
        _time_set_paused(paused)
    }

    def time_scale() : float {
        return _time_get_scale()
    }

    def set_time_scale(scale : float) : void {
        _time_set_scale(scale)
    }
}

// World-scoped entity handle. The identity fields are implementation
// details: compare handles with `==` / `!=` and test absence with
// `is_null` (or `world.is_alive`) instead of reading raw ids.
struct Entity {
    _registry : uint64 = 0
    _id : uint = 0xFFFFFFFFu

    def is_null() : bool {
        return _world_entity_is_null(_registry, _id)
    }
}

def operator ==(a, b : Entity implicit) : bool {
    return a._registry == b._registry && a._id == b._id
}

def operator !=(a, b : Entity implicit) : bool {
    return !(a == b)
}

enum DestroyMode {
    entity_only,
    subtree
}

// World-scoped handle to one registry. Every entity operation below is a
// method so there is exactly one spelling per operation; component access
// stays free functions because Daslang forbids generic struct members.
struct World {
    _registry : uint64 = 0

    // Creates a bare entity. An optional name is applied immediately.
    def create_entity(name : string = "") : Entity {
        var entity = Entity()
        entity._registry = _registry
        entity._id = _world_create_entity(_registry)
        if (name != "") {
            set_name(entity, name)
        }
        return entity
    }

    // Destroys the entity and its subtree (Unity/Godot semantics), or only
    // the entity when `DestroyMode.entity_only` is requested: children are
    // re-attached to the destroyed entity's parent and keep their world
    // pose. Returns false for a stale or foreign entity.
    def destroy(entity : Entity;
                mode : DestroyMode = DestroyMode.subtree) : bool {
        if (mode == DestroyMode.entity_only) {
            return _world_destroy_entity_only(_registry, entity._registry,
                                              entity._id)
        }
        return _world_destroy_entity(_registry, entity._registry, entity._id)
    }

    def is_alive(entity : Entity) : bool {
        return _world_entity_is_alive(_registry, entity._registry, entity._id)
    }

    // Null entity when nothing matches (check with `Entity.is_null`).
    def find_by_name(name : string) : Entity {
        var entity = Entity()
        entity._registry = _registry
        entity._id = _world_find_entity(_registry, name)
        return entity
    }

    def set_name(entity : Entity; name : string) : bool {
        return _world_set_name(_registry, entity._registry, entity._id, name)
    }

    // Copies the source subtree (hierarchy relinked, not duplicated) under
    // `parent`. A default-constructed parent detaches the clone.
    def clone_entity(src : Entity; parent : Entity = Entity()) : Entity {
        return _make_entity(_self(), _world_clone_entity(_registry,
                                                         src._registry, src._id,
                                                         parent._registry,
                                                         parent._id))
    }

    def entity_count() : uint {
        return _world_entity_count(_registry)
    }

    def find_all_by_name(name : string) : array<Entity> {
        var result : array<Entity>
        let total = _world_find_all_count(_registry, name)
        for (i in range(total)) {
            let found = _world_find_all_at(_registry, name, uint(i))
            result |> push(_make_entity(_self(), found))
        }
        return <- result
    }

    // ── Hierarchy ──

    // Re-parents `child` under `parent`. A default-constructed parent
    // detaches. Both entities must belong to this world; self-parenting
    // and cycles are rejected. World poses are preserved by default.
    def set_parent(child : Entity; parent : Entity = Entity();
                   keep_world : bool = true) : bool {
        return _world_set_parent(_registry, child._registry, child._id,
                                 parent._registry, parent._id, keep_world)
    }

    // Detaches an entity from its parent, keeping its world pose.
    def detach(entity : Entity; keep_world : bool = true) : bool {
        return set_parent(entity, Entity(), keep_world)
    }

    // Null entity when the entity has no parent in this world.
    def parent_of(entity : Entity) : Entity {
        return _make_entity(_self(), _world_get_parent(_registry,
                                                       entity._registry,
                                                       entity._id))
    }

    def children_of(entity : Entity) : array<Entity> {
        var result : array<Entity>
        let count = _world_get_child_count(_registry, entity._registry,
                                           entity._id)
        for (i in range(count)) {
            let child_id = _world_get_child(_registry, entity._registry,
                                            entity._id, uint(i))
            result |> push(_make_entity(_self(), child_id))
        }
        return <- result
    }

    // True when `ancestor` is on the parent chain of `entity`.
    def is_child_of(entity : Entity; ancestor : Entity) : bool {
        if (entity == ancestor) {
            return false
        }
        var current = parent_of(entity)
        // The chain is cycle-checked at link time; the bound only guards
        // against a corrupted hierarchy.
        for (step in range(4096)) {
            if (is_null(current)) {
                return false
            }
            if (current == ancestor) {
                return true
            }
            current = parent_of(current)
        }
        return false
    }

    // Wraps a raw entity id (for payload conversion); the result is a null
    // entity when the id is not alive in this world.
    def entity_from_id(entity_id : uint) : Entity {
        return _make_entity(_self(),
                            _world_entity_from_id(_registry, entity_id))
    }

    // Converts the raw entity ids inside an Area3D overlap payload.
    def sensor_area(overlap : SensorOverlap) : Entity {
        return entity_from_id(overlap.area_entity_id)
    }

    def sensor_other(overlap : SensorOverlap) : Entity {
        return entity_from_id(overlap.other_entity_id)
    }

    // Activates an already-loaded scene by name; the scene must already be
    // loaded for this to succeed.
    def change_scene(name : string) : bool {
        return _world_change_scene(_registry, name)
    }

    // A copy of this handle, for the free `_make_entity` helper.
    def _self() : World {
        var world = World()
        world._registry = _registry
        return world
    }
}

struct Input {
    // Raw hardware state (escape hatch). Prefer named actions for gameplay:
    // they survive rebinding and combine keyboard, mouse and gamepad inputs.
    // `scancode` is an SDL scancode (for example SDL_SCANCODE_SPACE).
    def key_pressed(scancode : int) : bool {
        return is_key_pressed(scancode)
    }

    def set_mouse_captured(captured : bool) : bool {
        return set_relative_mouse_mode(captured)
    }

    def mouse_position() : float2 {
        return float2(get_mouse_x(), get_mouse_y())
    }

    // Consumes the accumulated relative motion: read at most once per frame.
    def mouse_delta() : float2 {
        return float2(get_mouse_dx(), get_mouse_dy())
    }

    // SDL button numbering: 1 = left, 2 = middle, 3 = right.
    def mouse_button_down(button : int) : bool {
        return mouse_button_pressed(button)
    }

    def is_cursor_visible() : bool {
        return cursor_visible()
    }

    def set_cursor_visible(visible : bool) : void {
        if (visible) {
            show_cursor()
        } else {
            hide_cursor()
        }
    }

    // Named action map. Bind keyboard scancodes with bind_key or SDL mouse
    // buttons (1 = left, 2 = middle, 3 = right) with bind_mouse; unbind
    // removes the mapping. Held state polls SDL directly, while
    // just_pressed / just_released report this frame's published input
    // events (repeats do not count as fresh presses).
    def pressed(action : string) : bool {
        return action_pressed(action)
    }

    def just_pressed(action : string) : bool {
        return action_just_pressed(action)
    }

    def just_released(action : string) : bool {
        return action_just_released(action)
    }

    // Signed 1D axis from two actions: +1, 0, or -1.
    def value(neg_action : string; pos_action : string) : float {
        return action_axis(neg_action, pos_action)
    }

    // Two-axis vector from four actions.
    def value2(neg_x : string; pos_x : string; neg_y : string; pos_y : string) : float2 {
        return float2(action_axis(neg_x, pos_x), action_axis(neg_y, pos_y))
    }

    // Three-axis vector from six actions.
    def value3(neg_x : string; pos_x : string; neg_y : string; pos_y : string; neg_z : string; pos_z : string) : float3 {
        return float3(action_axis(neg_x, pos_x), action_axis(neg_y, pos_y), action_axis(neg_z, pos_z))
    }

    def bind_key(action : string; scancode : int) : bool {
        return action_bind_key(action, scancode)
    }

    def bind_mouse(action : string; button : int) : bool {
        return action_bind_mouse(action, button)
    }

    def unbind(action : string) : bool {
        return action_unbind(action)
    }
}

struct Logger {
    def debug(message : string) : void { _log_debug(message) }
    def info(message : string) : void { _log_info(message) }
    def warn(message : string) : void { _log_warn(message) }
    def error(message : string) : void { _log_error(message) }
    def trace(message : string) : void { _log_trace(message) }

    def debug_fields(message : string; fields : LogFields) : void {
        _log_debug(format_log_message(message, fields))
    }
    def info_fields(message : string; fields : LogFields) : void {
        _log_info(format_log_message(message, fields))
    }
    def warn_fields(message : string; fields : LogFields) : void {
        _log_warn(format_log_message(message, fields))
    }
    def error_fields(message : string; fields : LogFields) : void {
        _log_error(format_log_message(message, fields))
    }
    def trace_fields(message : string; fields : LogFields) : void {
        _log_trace(format_log_message(message, fields))
    }
}

// Ordered structured-log fields. Parallel key/value arrays keep call sites
// readable and emission order deterministic (table iteration order is not).
// Values are preformatted by the caller; the engine logs the joined line.
struct LogFields {
    keys : array<string>
    values : array<string>

    def add(key : string; value : string) : void {
        keys |> push(key)
        values |> push(value)
    }
}

// Renders "message k1=v1 k2=v2"; a field-less message is returned as-is.
def format_log_message(message : string; fields : LogFields) : string {
    var count = length(fields.keys)
    var other = length(fields.values)
    if (other < count) {
        count = other
    }
    if (count == 0) {
        return message
    }
    var out = message
    for (i in range(count)) {
        out += " " + fields.keys[i] + "=" + fields.values[i]
    }
    return out
}

struct UiService {
    def bind_button(element_id : string; handler : string) : bool {
        return _ui_bind_button(element_id, handler)
    }
}

// Raw-POD layout matches the engine's Area3D entered/exited payloads. Convert
// entity IDs with `world.sensor_area`/`world.sensor_other` before retaining
// them.
struct SensorOverlap {
    area_entity_id : uint
    other_entity_id : uint
    other_body_id : uint64
}

// Builds the raw-POD overlap payload from two entity handles. This is the
// one place a script needs raw entity ids, because the engine's physics
// events carry them; convert them back with `world.sensor_area` /
// `world.sensor_other`.
def sensor_overlap(area : Entity; other : Entity) : SensorOverlap {
    var overlap = SensorOverlap()
    overlap.area_entity_id = area._id
    overlap.other_entity_id = other._id
    return overlap
}

// Queues structural changes issued while a query is iterating. Commands
// outside iteration apply immediately; inside the outermost query they are
// applied at that query's structural barrier. `destroy` mirrors
// `World.destroy`, including `DestroyMode`.
struct CommandBuffer {
    world : World = World()

    def destroy(entity : Entity;
                mode : DestroyMode = DestroyMode.subtree) : bool {
        if (mode == DestroyMode.entity_only) {
            return _world_defer_destroy_only(world._registry, entity._registry,
                                             entity._id)
        }
        return _world_defer_destroy(world._registry, entity._registry,
                                    entity._id)
    }

    def set_parent(child : Entity; parent : Entity = Entity();
                   keep_world : bool = true) : bool {
        return _world_defer_set_parent(world._registry, child._registry,
                                       child._id, parent._registry, parent._id,
                                       keep_world)
    }

    // Spawns a bare entity and returns its handle, even inside a query:
    // bare creation touches no component pools, so live query references
    // stay valid, and the new entity matches no required-component query
    // until follow-up commands apply. Queue component, parent, and
    // lifetime changes with the other operations on this buffer; they
    // apply at the outermost query barrier (or immediately, outside
    // iteration). Prefer this over `world.create_entity` inside a query,
    // which is rejected.
    def create_entity(name : string = "") : Entity {
        return _make_entity(world, _world_defer_create_entity(world._registry,
                                                              name))
    }
}

struct SystemContext {
    world : World = World()
    commands : CommandBuffer = CommandBuffer()
    time : Time = Time()
    input : Input = Input()
    log : Logger = Logger()
    ui : UiService = UiService()
}

typedef public ScriptContext = SystemContext

def current_world() : World {
    var world = World()
    world._registry = _current_world()
    return world
}

// Internal liveness check for the free functions below: they take `World`
// by value (which Daslang treats as const), and a method cannot bind to a
// const receiver. User code calls `world.is_alive(entity)`.
def _is_alive(world : World; entity : Entity) : bool {
    return _world_entity_is_alive(world._registry, entity._registry,
                                  entity._id)
}

def _make_entity(world : World; id : uint) : Entity {
    var entity = Entity()
    entity._registry = world._registry
    entity._id = id
    return entity
}

def copy_component(world : World; dst : Entity; src : Entity;
                   tag : auto(T)) : bool {
    return _world_copy_component(world._registry, dst._registry, dst._id,
                                 src._registry, src._id,
                                 _component_type_id_of(tag))
}

// Generic component changes cannot be `CommandBuffer` methods (Daslang
// forbids generic members), so they stay free functions; every non-generic
// structural operation is a `CommandBuffer` method above.
def defer_add(commands : CommandBuffer; entity : Entity; tag : auto(T)) : bool {
    return _world_defer_add_component(commands.world._registry,
                                      entity._registry, entity._id,
                                      _component_type_id_of(tag))
}

def defer_remove(commands : CommandBuffer; entity : Entity;
                  tag : auto(T)) : bool {
    return _world_defer_remove_component(commands.world._registry,
                                         entity._registry, entity._id,
                                         _component_type_id_of(tag))
}

def connect_area_entered(handler_name : string) : bool {
    return _event_declare_sink_current("entered", handler_name)
}

def connect_area_exited(handler_name : string) : bool {
    return _event_declare_sink_current("exited", handler_name)
}

// Typed subscription to the engine-owned Area3D overlap events. Declares
// the handler (validating its payload against SensorOverlap) and connects
// it, returning a disposable handle; empty when the handler signature does
// not match the engine payload. Works for physics emits and
// `event_post("entered"/"exited", overlap)` alike.
def subscribe_area_entered(handler_name : string) : Subscription {
    var sub = Subscription()
    if (!connect_area_entered(handler_name)) {
        return sub
    }
    if (!_event_connect_current("entered", handler_name)) {
        return sub
    }
    sub.event = "entered"
    sub.handler = handler_name
    return sub
}

def subscribe_area_exited(handler_name : string) : Subscription {
    var sub = Subscription()
    if (!connect_area_exited(handler_name)) {
        return sub
    }
    if (!_event_connect_current("exited", handler_name)) {
        return sub
    }
    sub.event = "exited"
    sub.handler = handler_name
    return sub
}

// A live event connection owned by the calling script system. `dispose`
// disconnects it; the handle stays inert afterwards. An empty handle (both
// fields blank) means subscription failed and `dispose` is a no-op.
struct Subscription {
    event : string = ""
    handler : string = ""

    def dispose() : bool {
        if (event == "" || handler == "") {
            return false
        }
        var gone = unsubscribe_event(event, handler)
        event = ""
        handler = ""
        return gone
    }

    def is_valid() : bool {
        return !(event == "" || handler == "")
    }
}

// Declares a raw-POD handler method on the calling system and connects it to
// a same-payload event source, returning a disposable handle. Must be called
// from inside a system lifecycle call; returns an empty handle otherwise (or
// when the handler signature does not match the event payload).
def subscribe_event(event_name : string; handler_name : string) : Subscription {
    var sub = Subscription()
    var declared = _event_declare_sink_current(event_name, handler_name)
    if (!declared) {
        return sub
    }
    var connected = _event_connect_current(event_name, handler_name)
    if (!connected) {
        return sub
    }
    sub.event = event_name
    sub.handler = handler_name
    return sub
}

def unsubscribe_event(event_name : string; handler_name : string) : bool {
    return _event_disconnect_current(event_name, handler_name)
}

// Event timing: `event_emit` dispatches synchronously to connected handlers
// in connection order (a handler that emits re-enters dispatch
// immediately). `event_post` validates identically but queues the payload
// copy and returns: the runtime delivers queued events in FIFO posting
// order at the next pump, which runs at the start of the next systems
// update in both the serial and scheduled paths. A post made while no
// scene is active waits (cap-bound); a post made during a pump rolls to
// the following pump. Engine-owned payloads (e.g. the Area3D
// `entered`/`exited` overlap structs) flow through the same validation,
// so `event_post("entered", overlap)` reaches `connect_area_entered`
// handlers exactly like a physics emit would.

// Scene system control, by engine display name (the name passed to the
// native system constructor; for script systems, the adapter name given at
// instantiation). Disabling runs on_stop and stops updates; enabling
// re-runs on_start when needed and resumes updates. Unknown names report
// false. Only systems in the calling scene are affected; core engine
// systems are not reachable here.
def enable_system(name : string) : bool {
    return _enable_system(name)
}

def disable_system(name : string) : bool {
    return _disable_system(name)
}

def system_enabled(name : string) : bool {
    return _system_enabled(name)
}

// Scheduling declarations for the calling system. Call once from on_start:
// stages order execution phases, dependencies force before/after edges, and
// conflicts forbid co-scheduling. These only affect the real frame
// scheduler; direct scene.update() drives systems in registration order.
def set_stage(stage : string) : void {
    set_system_stage(stage)
}

def depends_on(name : string) : void {
    set_system_dependency(name)
}

def conflicts_with(name : string) : void {
    set_system_conflict(name)
}

def is_null(entity : Entity) : bool {
    return _world_entity_is_null(entity._registry, entity._id)
}

def has(world : World; entity : Entity; tag : auto(T)) : bool {
    return _world_has_component(world._registry, entity._registry, entity._id,
                                _component_type_id_of(tag))
}

def add(world : World; entity : Entity; tag : auto(T)) : bool {
    return _world_add_component(world._registry, entity._registry, entity._id,
                                _component_type_id_of(tag))
}

def remove(world : World; entity : Entity; tag : auto(T)) : bool {
    return _world_remove_component(world._registry, entity._registry, entity._id,
                                   _component_type_id_of(tag))
}

// Default-constructs a component on an entity. This is currently identical to
// `add`: value-carrying construction is future work, so both apply the
// registered defaults (built-in or Daslang).
def emplace(world : World; entity : Entity; tag : auto(T)) : bool {
    return _world_add_component(world._registry, entity._registry, entity._id,
                                _component_type_id_of(tag))
}

def _checked_component_entity(world : World; entity : Entity;
                                 tag : auto(T)) : uint {
    if (!_is_alive(world, entity) || !has(world, entity, tag)) {
        panic("entity is stale or component is missing")
    }
    return _world_entity_id(world._registry, entity._registry, entity._id)
}

def get(world : World; entity : Entity; tag : Transform) : Transform {
    return get_transform_accessor(_checked_component_entity(world, entity, tag))
}

def get(world : World; entity : Entity; tag : Transform2D) : Transform2D {
    return get_transform_2d_accessor(_checked_component_entity(world, entity, tag))
}

def get(world : World; entity : Entity; tag : Camera2D) : Camera2D {
    return get_camera_2d_accessor(_checked_component_entity(world, entity, tag))
}

def get(world : World; entity : Entity; tag : Sprite2D) : Sprite2D {
    return get_sprite_2d_accessor(_checked_component_entity(world, entity, tag))
}

def get(world : World; entity : Entity; tag : PointLight) : PointLight {
    return get_point_light_accessor(_checked_component_entity(world, entity, tag))
}

def get(world : World; entity : Entity; tag : DirectionalLight) : DirectionalLight {
    return get_directional_light_accessor(_checked_component_entity(world, entity, tag))
}

def get(world : World; entity : Entity; tag : SpotLight) : SpotLight {
    return get_spot_light_accessor(_checked_component_entity(world, entity, tag))
}

def get(world : World; entity : Entity; tag : Hierarchy) : Hierarchy {
    return get_hierarchy_accessor(_checked_component_entity(world, entity, tag))
}

def get(world : World; entity : Entity; tag : WorldTransform) : WorldTransform {
    return get_world_transform_accessor(_checked_component_entity(world, entity, tag))
}

def get(world : World; entity : Entity; tag : RigidBody) : RigidBody {
    return get_rigid_body_accessor(_checked_component_entity(world, entity, tag))
}

def get(world : World; entity : Entity; tag : CharacterBody) : CharacterBody {
    return get_character_body_accessor(_checked_component_entity(world, entity, tag))
}

def get(world : World; entity : Entity; tag : ModelInstance3D) : ModelInstance3D {
    return get_model_instance_3d_accessor(_checked_component_entity(world, entity, tag))
}

def get(world : World; entity : Entity; tag : Area3D) : Area3D {
    return get_area_3d_accessor(_checked_component_entity(world, entity, tag))
}

def get(world : World; entity : Entity; tag : Audio) : Audio {
    return get_audio_accessor(_checked_component_entity(world, entity, tag))
}

def get(world : World; entity : Entity; tag : PrefabInstance) : PrefabInstance {
    return get_prefab_instance_accessor(_checked_component_entity(world, entity, tag))
}

def get(world : World; entity : Entity; tag : Subviewport) : Subviewport {
    return get_subviewport_accessor(_checked_component_entity(world, entity, tag))
}

def get(world : World; entity : Entity; tag : auto(T)) : T& {
    var type_id = _component_type_id_of(tag)
    var data = _world_component_data(world._registry, entity._registry,
                                    entity._id, type_id)
    if (data == null) {
        panic("component is missing from entity")
    }
    unsafe {
        return <- *(reinterpret<T -& -#?>(data))
    }
}

// Live view of a component, or a per-type default sink when the entity is
// stale or the component is absent. Never panics; use `has` + `get` when
// absence is a scripting error. Built-in overloads below return the same
// kind of live view (a proxy over the entity), so built-in and Daslang
// components share one mutability and missing-value policy.
//
// Declare the destination as a reference to keep the view alive:
//
//   var v : Drift& = get_or_default(world, e, Drift())
//   v.speed = 9.0            // writes through to the component
//
// `var v = get_or_default(...)` copies, because Daslang only keeps a
// reference when the destination is spelled `T&`.
def _world_entity_id_or_null(world : World; entity : Entity) : uint {
    if (entity._registry != world._registry) {
        return 0xFFFFFFFFu
    }
    return _world_entity_from_id(world._registry, entity._id)
}

def get_or_default(world : World; entity : Entity; tag : Transform) : Transform {
    return get_transform_accessor(_world_entity_id_or_null(world, entity))
}

def get_or_default(world : World; entity : Entity; tag : Transform2D) : Transform2D {
    return get_transform_2d_accessor(_world_entity_id_or_null(world, entity))
}

def get_or_default(world : World; entity : Entity; tag : Camera2D) : Camera2D {
    return get_camera_2d_accessor(_world_entity_id_or_null(world, entity))
}

def get_or_default(world : World; entity : Entity; tag : Sprite2D) : Sprite2D {
    return get_sprite_2d_accessor(_world_entity_id_or_null(world, entity))
}

def get_or_default(world : World; entity : Entity; tag : PointLight) : PointLight {
    return get_point_light_accessor(_world_entity_id_or_null(world, entity))
}

def get_or_default(world : World; entity : Entity; tag : DirectionalLight) : DirectionalLight {
    return get_directional_light_accessor(_world_entity_id_or_null(world, entity))
}

def get_or_default(world : World; entity : Entity; tag : SpotLight) : SpotLight {
    return get_spot_light_accessor(_world_entity_id_or_null(world, entity))
}

def get_or_default(world : World; entity : Entity; tag : Hierarchy) : Hierarchy {
    return get_hierarchy_accessor(_world_entity_id_or_null(world, entity))
}

def get_or_default(world : World; entity : Entity; tag : WorldTransform) : WorldTransform {
    return get_world_transform_accessor(_world_entity_id_or_null(world, entity))
}

def get_or_default(world : World; entity : Entity; tag : RigidBody) : RigidBody {
    return get_rigid_body_accessor(_world_entity_id_or_null(world, entity))
}

def get_or_default(world : World; entity : Entity; tag : CharacterBody) : CharacterBody {
    return get_character_body_accessor(_world_entity_id_or_null(world, entity))
}

def get_or_default(world : World; entity : Entity; tag : ModelInstance3D) : ModelInstance3D {
    return get_model_instance_3d_accessor(_world_entity_id_or_null(world, entity))
}

def get_or_default(world : World; entity : Entity; tag : Area3D) : Area3D {
    return get_area_3d_accessor(_world_entity_id_or_null(world, entity))
}

def get_or_default(world : World; entity : Entity; tag : Audio) : Audio {
    return get_audio_accessor(_world_entity_id_or_null(world, entity))
}

def get_or_default(world : World; entity : Entity; tag : PrefabInstance) : PrefabInstance {
    return get_prefab_instance_accessor(_world_entity_id_or_null(world, entity))
}

def get_or_default(world : World; entity : Entity; tag : Subviewport) : Subviewport {
    return get_subviewport_accessor(_world_entity_id_or_null(world, entity))
}

// Live reference to a Daslang component, or a per-type zero sink when the
// entity is stale or the component is absent. The sink keeps the return
// type a reference (so writes compile) while making the write a no-op,
// which is exactly how the built-in proxy accessors behave.
def get_or_default(world : World; entity : Entity; tag : auto(T) -const) : T& {
    var type_id = _component_type_id_of(tag)
    var data = _world_component_data(world._registry, entity._registry,
                                    entity._id, type_id)
    if (data == null) {
        data = _component_default_data(type_id)
    }
    unsafe {
        return <- *(reinterpret<T -& -#?>(data))
    }
}

// Checked copy-out: fills `value` and returns true when the component is
// present, otherwise leaves `value` untouched and returns false.
//
// Daslang cannot spell an optional reference (a local reference must be
// initialized, and a tuple cannot hold one), so this copies the component
// into the caller's variable instead of aliasing it. Use `has` plus `get`
// (or `get_or_default`, which is a live view) when the caller needs to
// write through to the component. The tag and the destination are separate
// witnesses because Daslang deduces `auto` constness from each argument:
// the tag temporary keeps the `const` spelling used by the type registry,
// while `var value` stays mutable.
def try_get(world : World; entity : Entity; tag : auto(T) -const;
            var value : auto(Q)&) : bool {
    if (!has(world, entity, tag)) {
        return false
    }
    value = get_or_default(world, entity, tag)
    return true
}

// Scene-local singleton values, owned by the world rather than an entity.
// `get_singleton` panics when absent; `get_singleton_or_default` is a live
// view that falls back to a zero sink, and `try_get_singleton` is a checked
// copy-out. The tag uses `auto(T) -const` so the deduced type stays mutable
// (and its `typeinfo` spelling has no trailing `const`), which is what
// `get_singleton` returns by reference.
def has_singleton(world : World; tag : auto(T) -const) : bool {
    return _world_has_singleton(world._registry,
                                _world_singleton_type_id(world._registry, typeinfo typename(type<T>)))
}

def add_singleton(world : World; tag : auto(T) -const) : bool {
    return _world_add_singleton(world._registry,
                                _world_singleton_type_id(world._registry, typeinfo typename(type<T>)))
}

def remove_singleton(world : World; tag : auto(T) -const) : bool {
    return _world_remove_singleton(world._registry,
                                   _world_singleton_type_id(world._registry, typeinfo typename(type<T>)))
}

def get_singleton(world : World; tag : auto(T) -const) : T& {
    var type_id = _world_singleton_type_id(world._registry, typeinfo typename(type<T>))
    var data = _world_singleton_data(world._registry, type_id)
    if (data == null) {
        panic("singleton is missing from the world")
    }
    unsafe {
        return <- *(reinterpret<T -& -#?>(data))
    }
}

// Live reference to a scene-local singleton, or a per-type zero sink when
// it is absent (same live-view-or-no-op contract as `get_or_default`).
def get_singleton_or_default(world : World; tag : auto(T) -const) : T& {
    var type_id = _world_singleton_type_id(world._registry, typeinfo typename(type<T>))
    var data = _world_singleton_data(world._registry, type_id)
    if (data == null) {
        data = _component_default_data(type_id)
    }
    unsafe {
        return <- *(reinterpret<T -& -#?>(data))
    }
}

def try_get_singleton(world : World; tag : auto(T) -const;
                      var value : auto(Q)&) : bool {
    if (!has_singleton(world, tag)) {
        return false
    }
    value = get_singleton_or_default(world, tag)
    return true
}

def move(world : World; entity : Entity; delta : float3) : bool {
    if (!has(world, entity, Transform())) {
        return false
    }
    var transform = get(world, entity, Transform())
    transform.position += delta
    return true
}

def set_position(world : World; entity : Entity; position : float3) : bool {
    if (!has(world, entity, Transform())) {
        return false
    }
    var transform = get(world, entity, Transform())
    transform.position = position
    return true
}

def move_2d(world : World; entity : Entity; delta : float2) : bool {
    if (!has(world, entity, Transform2D())) {
        return false
    }
    var transform = get(world, entity, Transform2D())
    transform.position += delta
    return true
}

def set_position_2d(world : World; entity : Entity; position : float2) : bool {
    if (!has(world, entity, Transform2D())) {
        return false
    }
    var transform = get(world, entity, Transform2D())
    transform.position = position
    return true
}

def apply_force(world : World; entity : Entity; force : float3) : bool {
    if (!_is_alive(world, entity) || !has(world, entity, RigidBody())) {
        return false
    }
    let id = _world_entity_id(world._registry, entity._registry, entity._id)
    apply_force(id, force.x, force.y, force.z)
    return true
}

def apply_impulse(world : World; entity : Entity; impulse : float3) : bool {
    if (!_is_alive(world, entity) || !has(world, entity, RigidBody())) {
        return false
    }
    let id = _world_entity_id(world._registry, entity._registry, entity._id)
    apply_impulse(id, impulse.x, impulse.y, impulse.z)
    return true
}

def move_character(world : World; entity : Entity; velocity : float3) : bool {
    if (!_is_alive(world, entity) || !has(world, entity, CharacterBody())) {
        return false
    }
    var body = get(world, entity, CharacterBody())
    body.desired_velocity = velocity
    return true
}

// Directly sets the simulated linear velocity of a dynamic rigid body,
// bypassing forces. Returns false when the entity is dead, has no
// RigidBody, or owns no live physics body yet (bodies are created by the
// physics system; a freshly added component may need one update).
def set_linear_velocity(world : World; entity : Entity; velocity : float3) : bool {
    if (!_is_alive(world, entity) || !has(world, entity, RigidBody())) {
        return false
    }
    let id = _world_entity_id(world._registry, entity._registry, entity._id)
    set_body_velocity(id, velocity.x, velocity.y, velocity.z)
    return true
}

// Reads the simulated linear velocity of a rigid body. Returns zero when
// the entity is dead, has no RigidBody, or owns no live physics body yet.
def linear_velocity(world : World; entity : Entity) : float3 {
    if (!_is_alive(world, entity) || !has(world, entity, RigidBody())) {
        return float3(0.0, 0.0, 0.0)
    }
    let id = _world_entity_id(world._registry, entity._registry, entity._id)
    return float3(get_body_velocity_x(id), get_body_velocity_y(id),
                  get_body_velocity_z(id))
}

// Teleports a rigid body to a world position, preserving its rotation.
// Dynamic bodies are body-authoritative so the move sticks; static and
// kinematic bodies are transform-driven every update, so move those with
// set_position instead. Returns false when the entity is dead, has no
// RigidBody, or owns no live physics body yet.
def teleport_body(world : World; entity : Entity; position : float3) : bool {
    if (!_is_alive(world, entity) || !has(world, entity, RigidBody())) {
        return false
    }
    let id = _world_entity_id(world._registry, entity._registry, entity._id)
    set_body_position(id, position.x, position.y, position.z)
    return true
}

// Reads the live simulated world position of a rigid body. For dynamic
// bodies this is the physics truth, which can differ from the authored
// Transform between syncs. Returns zero when the entity is dead, has no
// RigidBody, or owns no live physics body yet.
def body_position(world : World; entity : Entity) : float3 {
    if (!_is_alive(world, entity) || !has(world, entity, RigidBody())) {
        return float3(0.0, 0.0, 0.0)
    }
    let id = _world_entity_id(world._registry, entity._registry, entity._id)
    return float3(get_body_position_x(id), get_body_position_y(id),
                  get_body_position_z(id))
}

def has_animation(world : World; entity : Entity) : bool {
    return _world_has_animator(world._registry, entity._registry, entity._id)
}

def play_animation(world : World; entity : Entity; clip : string) : bool {
    if (!has_animation(world, entity)) {
        return false
    }
    anim_play(_world_entity_id(world._registry, entity._registry, entity._id), clip)
    return true
}

def crossfade_animation(world : World; entity : Entity; clip : string;
                        duration : float) : bool {
    if (!has_animation(world, entity)) {
        return false
    }
    anim_crossfade(_world_entity_id(world._registry, entity._registry, entity._id), clip, duration)
    return true
}

def stop_animation(world : World; entity : Entity) : bool {
    if (!has_animation(world, entity)) {
        return false
    }
    anim_stop(_world_entity_id(world._registry, entity._registry, entity._id))
    return true
}

def set_animation_speed(world : World; entity : Entity; speed : float) : bool {
    if (!has_animation(world, entity)) {
        return false
    }
    anim_set_speed(_world_entity_id(world._registry, entity._registry, entity._id), speed)
    return true
}

def set_animation_time(world : World; entity : Entity; seconds : float) : bool {
    if (!has_animation(world, entity)) {
        return false
    }
    anim_set_time(_world_entity_id(world._registry, entity._registry, entity._id), seconds)
    return true
}

def set_animation_loop(world : World; entity : Entity; looping : bool) : bool {
    if (!has_animation(world, entity)) {
        return false
    }
    anim_set_loop(_world_entity_id(world._registry, entity._registry, entity._id), looping)
    return true
}

def pause_animation(world : World; entity : Entity) : bool {
    if (!has_animation(world, entity)) {
        return false
    }
    anim_pause(_world_entity_id(world._registry, entity._registry, entity._id))
    return true
}

def resume_animation(world : World; entity : Entity) : bool {
    if (!has_animation(world, entity)) {
        return false
    }
    anim_resume(_world_entity_id(world._registry, entity._registry, entity._id))
    return true
}

def is_animation_playing(world : World; entity : Entity) : bool {
    if (!has_animation(world, entity)) {
        return false
    }
    return anim_is_playing(_world_entity_id(world._registry, entity._registry, entity._id))
}

def is_animation_paused(world : World; entity : Entity) : bool {
    if (!has_animation(world, entity)) {
        return false
    }
    return anim_is_paused(_world_entity_id(world._registry, entity._registry, entity._id))
}

def is_animation_finished(world : World; entity : Entity) : bool {
    if (!has_animation(world, entity)) {
        return false
    }
    return anim_is_finished(_world_entity_id(world._registry, entity._registry, entity._id))
}

def animation_time(world : World; entity : Entity) : float {
    if (!has_animation(world, entity)) {
        return -1.0
    }
    return anim_get_time(_world_entity_id(world._registry, entity._registry, entity._id))
}

def animation_duration(world : World; entity : Entity) : float {
    if (!has_animation(world, entity)) {
        return -1.0
    }
    return anim_get_duration(_world_entity_id(world._registry, entity._registry, entity._id))
}

def play_audio(world : World; entity : Entity) : bool {
    if (!_is_alive(world, entity) || !has(world, entity, Audio())) {
        return false
    }
    audio_play(_world_entity_id(world._registry, entity._registry, entity._id))
    return true
}

def stop_audio(world : World; entity : Entity) : bool {
    if (!_is_alive(world, entity) || !has(world, entity, Audio())) {
        return false
    }
    audio_stop(_world_entity_id(world._registry, entity._registry, entity._id))
    return true
}

def pause_audio(world : World; entity : Entity) : bool {
    if (!_is_alive(world, entity) || !has(world, entity, Audio())) {
        return false
    }
    audio_pause(_world_entity_id(world._registry, entity._registry, entity._id))
    return true
}

def resume_audio(world : World; entity : Entity) : bool {
    if (!_is_alive(world, entity) || !has(world, entity, Audio())) {
        return false
    }
    audio_resume(_world_entity_id(world._registry, entity._registry, entity._id))
    return true
}

def set_audio_volume(world : World; entity : Entity; volume : float) : bool {
    if (!_is_alive(world, entity) || !has(world, entity, Audio())) {
        return false
    }
    audio_set_volume(_world_entity_id(world._registry, entity._registry, entity._id), volume)
    return true
}

def is_audio_playing(world : World; entity : Entity) : bool {
    if (!_is_alive(world, entity) || !has(world, entity, Audio())) {
        return false
    }
    return audio_is_playing(_world_entity_id(world._registry, entity._registry, entity._id))
}

def active_camera(world : World) : Entity {
    return _make_entity(world, get_active_camera())
}

def set_active_camera(world : World; entity : Entity) : bool {
    if (!_is_alive(world, entity)) {
        return false
    }
    set_active_camera(_world_entity_id(world._registry, entity._registry, entity._id))
    return true
}

def camera_fov(world : World; camera : Entity) : float {
    return get_camera_fov(_world_entity_id(world._registry, camera._registry, camera._id))
}

def set_camera_fov(world : World; camera : Entity; fov : float) : bool {
    if (!_is_alive(world, camera)) {
        return false
    }
    set_camera_fov(_world_entity_id(world._registry, camera._registry, camera._id), fov)
    return true
}

def camera_near(world : World; camera : Entity) : float {
    return get_camera_near(_world_entity_id(world._registry, camera._registry, camera._id))
}

def set_camera_near(world : World; camera : Entity; near_val : float) : bool {
    if (!_is_alive(world, camera)) {
        return false
    }
    set_camera_near(_world_entity_id(world._registry, camera._registry, camera._id), near_val)
    return true
}

def camera_far(world : World; camera : Entity) : float {
    return get_camera_far(_world_entity_id(world._registry, camera._registry, camera._id))
}

def set_camera_far(world : World; camera : Entity; far_val : float) : bool {
    if (!_is_alive(world, camera)) {
        return false
    }
    set_camera_far(_world_entity_id(world._registry, camera._registry, camera._id), far_val)
    return true
}

def camera_aspect_ratio(world : World; camera : Entity) : float {
    return get_camera_aspect_ratio(_world_entity_id(world._registry, camera._registry, camera._id))
}

def set_camera_aspect_ratio(world : World; camera : Entity; aspect : float) : bool {
    if (!_is_alive(world, camera)) {
        return false
    }
    set_camera_aspect_ratio(_world_entity_id(world._registry, camera._registry, camera._id), aspect)
    return true
}

// Builds a world-space pick ray through a camera pixel. Viewport describes
// the rendered region in pixels (usually the full window). Returns the ray
// origin, direction, and whether the camera had valid camera state.
def pick_ray(world : World; camera : Entity; mouse_x : float; mouse_y : float; vp_x : float; vp_y : float; vp_w : float; vp_h : float) : tuple<float3,float3,bool> {
    if (!_is_alive(world, camera)) {
        return (float3(0.0, 0.0, 0.0), float3(0.0, 0.0, 0.0), false)
    }
    if (!has(world, camera, Camera()) || !has(world, camera, WorldTransform())) {
        return (float3(0.0, 0.0, 0.0), float3(0.0, 0.0, 0.0), false)
    }
    var ok = make_pick_ray(_world_entity_id(world._registry, camera._registry,
                                            camera._id),
                           mouse_x, mouse_y, vp_x, vp_y, vp_w, vp_h)
    if (!ok) {
        return (float3(0.0, 0.0, 0.0), float3(0.0, 0.0, 0.0), false)
    }
    return (float3(get_ray_origin_x(), get_ray_origin_y(), get_ray_origin_z()),
            float3(get_ray_dir_x(), get_ray_dir_y(), get_ray_dir_z()), true)
}

// Intersects a ray with a plane. Returns the hit point and whether the ray
// actually hits (grazing and backward rays miss).
def ray_plane_hit(origin_x : float; origin_y : float; origin_z : float; dir_x : float; dir_y : float; dir_z : float; plane_x : float; plane_y : float; plane_z : float; plane_nx : float; plane_ny : float; plane_nz : float) : tuple<float3,bool> {
    var hit = ray_plane_intersect(origin_x, origin_y, origin_z,
                                  dir_x, dir_y, dir_z,
                                  plane_x, plane_y, plane_z,
                                  plane_nx, plane_ny, plane_nz)
    if (!hit) {
        return (float3(0.0, 0.0, 0.0), false)
    }
    return (float3(get_hit_x(), get_hit_y(), get_hit_z()), true)
}

// Current window size in pixels. Refreshes the cached size first, mirroring
// the legacy refresh_window_size/get_window_width/get_window_height trio.
def window_size() : float2 {
    refresh_window_size()
    return float2(float(get_window_width()), float(get_window_height()))
}

def set_model(world : World; entity : Entity; path : string) : bool {
    if (!_is_alive(world, entity) || !has(world, entity, ModelInstance3D())) {
        return false
    }
    set_model(_world_entity_id(world._registry, entity._registry, entity._id), path)
    return true
}

def set_model_material(world : World; entity : Entity; path : string) : bool {
    if (!_is_alive(world, entity) || !has(world, entity, ModelInstance3D())) {
        return false
    }
    set_model_material_override(_world_entity_id(world._registry, entity._registry, entity._id), path)
    return true
}

def set_model_visibility_range(world : World; entity : Entity;
                               distance : float) : bool {
    if (!_is_alive(world, entity) || !has(world, entity, ModelInstance3D())) {
        return false
    }
    set_model_visibility_range(_world_entity_id(world._registry, entity._registry, entity._id), distance)
    return true
}

def instantiate_prefab(world : World; path : string) : Entity {
    let id = instantiate_prefab(path)
    return _make_entity(world, _world_entity_from_id(world._registry, id))
}

def change_scene(world : World; name : string) : bool {
    return _world_change_scene(world._registry, name)
}

// Base class for daslang ECS systems, kept in this module so gameplay code
// needs only `require weasel`. The C++ engine calls these methods through
// the class adapter pattern: it instantiates the class literally named
// `System` in each game module, so game code declares
// `class System : GameSystem` and overrides the `on_*` lifecycle hooks
// (mapped to the canonical `SystemContext` hooks below).
class EcsSystem {
    def on_init() : void { }
    def on_update(dt : float) : void { }
    def on_fixed_update(fixed_dt : float) : void { }
    def on_late_update(dt : float) : void { }
    def on_inactive() : void { }
}

class GameSystem : EcsSystem {
    context : SystemContext = SystemContext()

    def on_start(var ctx : SystemContext) : void { }
    def update(var ctx : SystemContext; dt : float) : void { }
    def fixed_update(var ctx : SystemContext; fixed_dt : float) : void { }
    def late_update(var ctx : SystemContext; dt : float) : void { }
    def on_stop(var ctx : SystemContext) : void { }

    def override on_init() : void {
        context.world = current_world()
        context.commands.world = context.world
        context.time.refresh(0.0)
        on_start(context)
    }

    def override on_update(dt : float) : void {
        context.world = current_world()
        context.commands.world = context.world
        context.time.refresh(dt)
        update(context, dt)
    }

    def override on_fixed_update(fixed_dt : float) : void {
        context.world = current_world()
        context.commands.world = context.world
        context.time.refresh(fixed_dt)
        fixed_update(context, fixed_dt)
    }

    def override on_late_update(dt : float) : void {
        context.world = current_world()
        context.commands.world = context.world
        context.time.refresh(dt)
        late_update(context, dt)
    }

    def override on_inactive() : void {
        context.world = current_world()
        context.commands.world = context.world
        context.time.refresh(0.0)
        on_stop(context)
    }
}

def _component_type_id_of(tag : Transform) : uint {
    return TYPE_TRANSFORM()
}

def _component_type_id_of(tag : Transform2D) : uint {
    return TYPE_TRANSFORM_2D()
}

def _component_type_id_of(tag : Camera2D) : uint {
    return TYPE_CAMERA_2D()
}

// Marker tag for the 3D camera component. Presence checks (`has`) work;
// field access goes through active_camera()/set_active_camera() and the
// fov/near/far/aspect helpers (there is no live 3D-camera proxy yet, so
// `get(world, e, Camera())` panics like any other absent live reference).
struct Camera { }

def _component_type_id_of(tag : Camera) : uint {
    return TYPE_CAMERA()
}

def _component_type_id_of(tag : Sprite2D) : uint {
    return TYPE_SPRITE_2D()
}

def _component_type_id_of(tag : PointLight) : uint {
    return TYPE_POINT_LIGHT()
}

def _component_type_id_of(tag : DirectionalLight) : uint {
    return TYPE_DIRECTIONAL_LIGHT()
}

def _component_type_id_of(tag : SpotLight) : uint {
    return TYPE_SPOT_LIGHT()
}

def _component_type_id_of(tag : Hierarchy) : uint {
    return TYPE_HIERARCHY()
}

def _component_type_id_of(tag : WorldTransform) : uint {
    return TYPE_WORLD_TRANSFORM()
}

def _component_type_id_of(tag : RigidBody) : uint {
    return TYPE_RIGID_BODY()
}

def _component_type_id_of(tag : CharacterBody) : uint {
    return TYPE_CHARACTER_BODY()
}

def _component_type_id_of(tag : ModelInstance3D) : uint {
    return TYPE_MODEL_INSTANCE_3D()
}

def _component_type_id_of(tag : Area3D) : uint {
    return TYPE_AREA_3D()
}

def _component_type_id_of(tag : Audio) : uint {
    return TYPE_AUDIO()
}

def _component_type_id_of(tag : PrefabInstance) : uint {
    return TYPE_PREFAB_INSTANCE()
}

def _component_type_id_of(tag : Subviewport) : uint {
    return TYPE_SUBVIEWPORT()
}

def _component_type_id_of(tag : auto(T)) : uint {
    return _get_component_type_id_by_name(typeinfo typename(type<T>))
}

def _bind_query_component(world : World; entity : Entity; type_id : uint;
                          tag : Transform) : Transform {
    return get(world, entity, tag)
}

def _bind_query_component(world : World; entity : Entity; type_id : uint;
                          tag : Transform2D) : Transform2D {
    return get(world, entity, tag)
}

def _bind_query_component(world : World; entity : Entity; type_id : uint;
                          tag : Camera2D) : Camera2D {
    return get(world, entity, tag)
}

def _bind_query_component(world : World; entity : Entity; type_id : uint;
                          tag : Sprite2D) : Sprite2D {
    return get(world, entity, tag)
}

def _bind_query_component(world : World; entity : Entity; type_id : uint;
                          tag : PointLight) : PointLight {
    return get(world, entity, tag)
}

def _bind_query_component(world : World; entity : Entity; type_id : uint;
                          tag : DirectionalLight) : DirectionalLight {
    return get(world, entity, tag)
}

def _bind_query_component(world : World; entity : Entity; type_id : uint;
                          tag : SpotLight) : SpotLight {
    return get(world, entity, tag)
}

def _bind_query_component(world : World; entity : Entity; type_id : uint;
                          tag : Hierarchy) : Hierarchy {
    return get(world, entity, tag)
}

def _bind_query_component(world : World; entity : Entity; type_id : uint;
                          tag : WorldTransform) : WorldTransform {
    return get(world, entity, tag)
}

def _bind_query_component(world : World; entity : Entity; type_id : uint;
                          tag : RigidBody) : RigidBody {
    return get(world, entity, tag)
}

def _bind_query_component(world : World; entity : Entity; type_id : uint;
                          tag : CharacterBody) : CharacterBody {
    return get(world, entity, tag)
}

def _bind_query_component(world : World; entity : Entity; type_id : uint;
                          tag : ModelInstance3D) : ModelInstance3D {
    return get(world, entity, tag)
}

def _bind_query_component(world : World; entity : Entity; type_id : uint;
                          tag : Area3D) : Area3D {
    return get(world, entity, tag)
}

def _bind_query_component(world : World; entity : Entity; type_id : uint;
                          tag : Audio) : Audio {
    return get(world, entity, tag)
}

def _bind_query_component(world : World; entity : Entity; type_id : uint;
                          tag : PrefabInstance) : PrefabInstance {
    return get(world, entity, tag)
}

def _bind_query_component(world : World; entity : Entity; type_id : uint;
                          tag : Subviewport) : Subviewport {
    return get(world, entity, tag)
}

def _bind_query_component(world : World; entity : Entity; type_id : uint;
                          tag : auto(T)) : T -const& {
    var data = _world_component_data(world._registry, entity._registry,
                                    entity._id, type_id)
    if (data == null) {
        panic("query component payload is missing")
    }
    unsafe {
        return <- *(reinterpret<T -const -& -#?>(data))
    }
}

// Typed read of one published message by index. Returns a value copy (or
// `default<T>` when the type is unknown, unpublished, or the index is out
// of range), so callers never alias the buffer the next drain recycles.
// Pair with `for_each_message(name) $(i : uint)` for typed iteration.
def message_at(name : string; index : uint; tag : auto(T)) : T {
    var data = _message_at_data(name, index)
    if (data == null) {
        return default<T>
    }
    unsafe {
        return *(reinterpret<T -#?>(data))
    }
}

// `query` lowers to a single `each_world_entity_with` (or
// `each_world_entity_with_except`) call over the smallest matching pool.
//
//   query() $(t : Transform&) { ... }                     // current world
//   query(ctx.world) $(t : Transform&) { ... }            // explicit world
//   query(ctx.world, Sprite2D(), Audio()) $(t : Transform&) { ... }
//
// The block parameters declare the required components (each must be a `&`
// reference); any arguments between the World and the block are component
// tags to exclude. Component type ids are resolved once per query call,
// never per entity.
[call_macro(name="query")]
class private QueryMacro : AstCallMacro {
    def override visit(prog : ProgramPtr; mod : Module?;
                       var call : ExprCallMacro?) : ExpressionPtr {
        let count = length(call.arguments)
        if (count == 0 || count > 64) {
            macro_error(prog, call.at,
                        "query expects a block, an optional World, and optional exclusion component tags")
            return null
        }
        let blockArg = call.arguments[count - 1]
        var blk : ExprBlock?
        if (blockArg is ExprMakeBlock) {
            blk = (blockArg as ExprMakeBlock)._block as ExprBlock
        } else {
            blk = blockArg as ExprBlock
        }
        assert(blk != null, "query expects a block argument")

        // Arguments before the block: an optional World first, then any
        // number of exclusion tags. A World is only present when at least
        // one other argument precedes the block, so the split stays
        // unambiguous: `query(world, Sprite2D())` excludes, `query(world)`
        // does not, and `query()` uses the current world.
        var worldExpr : Expression?
        var firstTag = 0
        if (count >= 2) {
            worldExpr = call.arguments[0]
            firstTag = 1
        }

        var outerStmts : array<Expression?>
        var idExprs : array<Expression?>
        var declStmts : array<Expression?>

        if (worldExpr == null) {
            outerStmts |> push <| qmacro_expr() {
                var query_world : World = current_world()
            }
        } else {
            outerStmts |> push <| qmacro_expr() {
                var query_world : World = $e(worldExpr)
            }
        }

        for (p in blk.arguments) {
            if (p._type == null || !p._type.isRefType) {
                macro_error(prog, p.at,
                            "query component parameters must be references")
                return null
            }

            var baseType = clone_type(p._type)
            baseType.flags.ref = false
            baseType.flags.constant = true
            var mutableType = clone_type(baseType)
            mutableType.flags.constant = false
            if (!p._type.isHandle) {
                mutableType.flags.ref = true
            }
            let idName = "query_id_" + string(p.name)

            outerStmts |> push <| qmacro_expr() {
                var $i(idName) : uint = _component_type_id_of(
                    default< $t(baseType) >)
            }
            idExprs |> push <| qmacro_expr() { $i(idName) }
            declStmts |> push <| qmacro_expr() {
                var $i(p.name) : $t(mutableType) = _bind_query_component(
                    query_world, entity, $i(idName),
                    default< $t(baseType) >)
            }
        }

        var callbackStmts : array<Expression?>
        callbackStmts |> push <| qmacro_expr() {
            var entity : Entity = _make_entity(query_world,
                _world_entity_from_id(query_world._registry, query_entity_id))
        }
        for (s in declStmts) {
            callbackStmts |> push <| clone_expression(s)
        }
        for (s in blk.list) {
            callbackStmts |> push <| clone_expression(s)
        }

        if (firstTag >= count - 1) {
            // No exclusions: the plain (smaller) native call.
            outerStmts |> push <| qmacro_expr() {
                each_world_entity_with(query_world._registry,
                    [ $a(idExprs) ]) <| $( query_entity_id : uint) {
                    $b(callbackStmts)
                }
            }
        } else {
            outerStmts |> push <| qmacro_expr() {
                var query_except_ids : array<uint>
            }
            for (i in range(firstTag, count - 1)) {
                let tag = call.arguments[i]
                outerStmts |> push <| qmacro_expr() {
                    query_except_ids |> push(_component_type_id_of($e(tag)))
                }
            }
            outerStmts |> push <| qmacro_expr() {
                each_world_entity_with_except(query_world._registry,
                    [ $a(idExprs) ], query_except_ids) <| $( query_entity_id : uint) {
                    $b(callbackStmts)
                }
            }
        }
        return qmacro_block() { $b(outerStmts) }
    }
}
