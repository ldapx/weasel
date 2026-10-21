weasel
======

.. das:module:: weasel

Gameplay scripts use one import (``require weasel``). Native
bindings and legacy proxy helpers are implementation details
of this module.

This reference is generated from the facade source
(``src/wsl/das/modules/weasel.das``). Nothing here is
hand-written; edit the ``//`` doc comments in the facade
and regenerate.

Lifecycle
---------

.. das:class:: SystemContext

   .. das:attribute:: world : World = World()

   .. das:attribute:: commands : CommandBuffer = CommandBuffer()

   .. das:attribute:: time : Time = Time()

   .. das:attribute:: input : Input = Input()

   .. das:attribute:: log : Logger = Logger()

   .. das:attribute:: ui : UiService = UiService()

.. das:data:: ScriptContext

.. das:class:: EcsSystem

   Base class for daslang ECS systems, kept in this module so gameplay code
   needs only `require weasel`. The C++ engine calls these methods through
   the class adapter pattern: it instantiates the class literally named
   `System` in each game module, so game code declares
   `class System : GameSystem` and overrides the `on_*` lifecycle hooks
   (mapped to the canonical `SystemContext` hooks below).

   .. das:method:: on_init() : void

   .. das:method:: on_update(dt : float) : void

   .. das:method:: on_fixed_update(fixed_dt : float) : void

   .. das:method:: on_late_update(dt : float) : void

   .. das:method:: on_inactive() : void

.. das:class:: GameSystem

   Base: ``EcsSystem``

   .. das:attribute:: context : SystemContext = SystemContext()

   .. das:method:: on_start(var ctx : SystemContext) : void

   .. das:method:: update(var ctx : SystemContext; dt : float) : void

   .. das:method:: fixed_update(var ctx : SystemContext; fixed_dt : float) : void

   .. das:method:: late_update(var ctx : SystemContext; dt : float) : void

   .. das:method:: on_stop(var ctx : SystemContext) : void

   .. das:method:: on_init() : void

   .. das:method:: on_update(dt : float) : void

   .. das:method:: on_fixed_update(fixed_dt : float) : void

   .. das:method:: on_late_update(dt : float) : void

   .. das:method:: on_inactive() : void

.. das:function:: current_world() : World

Core handles
------------

.. das:class:: Time

   One import is the whole gameplay surface: native bindings and the legacy
   component proxies are implementation details of this module, so scripts
   `require weasel` and nothing else.

   Frame time for gameplay scripts. `delta` is the scaled step consumed by
   gameplay (zero while paused); `unscaled_delta` is the raw engine dt.
   `elapsed` is wall-clock seconds since process start. `fixed_delta` is the
   physics step; `fixed_elapsed` accumulates completed fixed steps;
   `frame_index` counts renderer frames. Pause and scale are engine-owned
   gameplay state on the runtime context (one per game instance): while
   paused, fixed steps and physics stop, but variable/late passes keep
   running for UI and menus.

   .. das:attribute:: delta : float

   .. das:attribute:: unscaled_delta : float

   .. das:attribute:: fixed_delta : float

   .. das:attribute:: fixed_elapsed : float

   .. das:attribute:: elapsed : float

   .. das:attribute:: frame_index : uint64

   .. das:method:: refresh(dt : float) : void

      Recomputes every field from the raw engine dt. Called by `System`
      once per lifecycle entry; one-off scripts can call it on an owned
      value. (Field access on `ctx.time` is fine in const contexts, but
      method calls are not, so `System` hooks take `var ctx`.)

   .. das:method:: paused() : bool

      Pause and scale are engine-owned gameplay state (one per game
      instance), so these are accessors, not local fields: while paused,
      fixed steps and physics stop but variable/late passes keep running
      for UI and menus. Negative scales clamp to zero.

   .. das:method:: set_paused(paused : bool) : void

   .. das:method:: time_scale() : float

   .. das:method:: set_time_scale(scale : float) : void

.. das:class:: Entity

   World-scoped entity handle. The identity fields are implementation
   details: compare handles with `==` / `!=` and test absence with
   `is_null` (or `world.is_alive`) instead of reading raw ids.

   .. das:method:: is_null() : bool

.. das:class:: DestroyMode

   .. das:attribute:: entity_only

   .. das:attribute:: subtree

.. das:class:: World

   World-scoped handle to one registry. Every entity operation below is a
   method so there is exactly one spelling per operation; component access
   stays free functions because Daslang forbids generic struct members.

   .. das:method:: create_entity(name : string = "") : Entity

      Creates a bare entity. An optional name is applied immediately.

   .. das:method:: destroy(entity : Entity; mode : DestroyMode = DestroyMode.subtree) : bool

      Destroys the entity and its subtree (Unity/Godot semantics), or only
      the entity when `DestroyMode.entity_only` is requested: children are
      re-attached to the destroyed entity's parent and keep their world
      pose. Returns false for a stale or foreign entity.

   .. das:method:: is_alive(entity : Entity) : bool

   .. das:method:: find_by_name(name : string) : Entity

      Null entity when nothing matches (check with `Entity.is_null`).

   .. das:method:: set_name(entity : Entity; name : string) : bool

   .. das:method:: clone_entity(src : Entity; parent : Entity = Entity()) : Entity

      Copies the source subtree (hierarchy relinked, not duplicated) under
      `parent`. A default-constructed parent detaches the clone.

   .. das:method:: entity_count() : uint

   .. das:method:: find_all_by_name(name : string) : array<Entity>

   .. das:method:: set_parent(child : Entity; parent : Entity = Entity(); keep_world : bool = true) : bool

      Re-parents `child` under `parent`. A default-constructed parent
      detaches. Both entities must belong to this world; self-parenting
      and cycles are rejected. World poses are preserved by default.

   .. das:method:: detach(entity : Entity; keep_world : bool = true) : bool

      Detaches an entity from its parent, keeping its world pose.

   .. das:method:: parent_of(entity : Entity) : Entity

      Null entity when the entity has no parent in this world.

   .. das:method:: children_of(entity : Entity) : array<Entity>

   .. das:method:: is_child_of(entity : Entity; ancestor : Entity) : bool

      True when `ancestor` is on the parent chain of `entity`.

   .. das:method:: entity_from_id(entity_id : uint) : Entity

      Wraps a raw entity id (for payload conversion); the result is a null
      entity when the id is not alive in this world.

   .. das:method:: sensor_area(overlap : SensorOverlap) : Entity

      Converts the raw entity ids inside an Area3D overlap payload.

   .. das:method:: sensor_other(overlap : SensorOverlap) : Entity

   .. das:method:: change_scene(name : string) : bool

      Activates an already-loaded scene by name; the scene must already be
      loaded for this to succeed.

.. das:class:: SensorOverlap

   Raw-POD layout matches the engine's Area3D entered/exited payloads. Convert
   entity IDs with `world.sensor_area`/`world.sensor_other` before retaining
   them.

   .. das:attribute:: area_entity_id : uint

   .. das:attribute:: other_entity_id : uint

   .. das:attribute:: other_body_id : uint64

.. das:class:: CommandBuffer

   Queues structural changes issued while a query is iterating. Commands
   outside iteration apply immediately; inside the outermost query they are
   applied at that query's structural barrier. `destroy` mirrors
   `World.destroy`, including `DestroyMode`.

   .. das:attribute:: world : World = World()

   .. das:method:: destroy(entity : Entity; mode : DestroyMode = DestroyMode.subtree) : bool

   .. das:method:: set_parent(child : Entity; parent : Entity = Entity(); keep_world : bool = true) : bool

   .. das:method:: create_entity(name : string = "") : Entity

      Spawns a bare entity and returns its handle, even inside a query:
      bare creation touches no component pools, so live query references
      stay valid, and the new entity matches no required-component query
      until follow-up commands apply. Queue component, parent, and
      lifetime changes with the other operations on this buffer; they
      apply at the outermost query barrier (or immediately, outside
      iteration). Prefer this over `world.create_entity` inside a query,
      which is rejected.

.. das:class:: Subscription

   A live event connection owned by the calling script system. `dispose`
   disconnects it; the handle stays inert afterwards. An empty handle (both
   fields blank) means subscription failed and `dispose` is a no-op.

   .. das:attribute:: event : string = ""

   .. das:attribute:: handler : string = ""

   .. das:method:: dispose() : bool

   .. das:method:: is_valid() : bool

.. das:class:: Camera

   Marker tag for the 3D camera component. Presence checks (`has`) work;
   field access goes through active_camera()/set_active_camera() and the
   fov/near/far/aspect helpers (there is no live 3D-camera proxy yet, so
   `get(world, e, Camera())` panics like any other absent live reference).

.. das:operator:: operator ==(a, b : Entity implicit) : bool

.. das:operator:: operator !=(a, b : Entity implicit) : bool

.. das:function:: is_null(entity : Entity) : bool

Components
----------

.. das:function:: copy_component(world : World; dst : Entity; src : Entity; tag : auto(T)) : bool

.. das:function:: has(world : World; entity : Entity; tag : auto(T)) : bool

.. das:function:: add(world : World; entity : Entity; tag : auto(T)) : bool

.. das:function:: remove(world : World; entity : Entity; tag : auto(T)) : bool

.. das:function:: emplace(world : World; entity : Entity; tag : auto(T)) : bool

   Default-constructs a component on an entity. This is currently identical to
   `add`: value-carrying construction is future work, so both apply the
   registered defaults (built-in or Daslang).

.. das:function:: get(world : World; entity : Entity; tag : Transform) : Transform

.. das:function:: get(world : World; entity : Entity; tag : Transform2D) : Transform2D

.. das:function:: get(world : World; entity : Entity; tag : Camera2D) : Camera2D

.. das:function:: get(world : World; entity : Entity; tag : Sprite2D) : Sprite2D

.. das:function:: get(world : World; entity : Entity; tag : PointLight) : PointLight

.. das:function:: get(world : World; entity : Entity; tag : DirectionalLight) : DirectionalLight

.. das:function:: get(world : World; entity : Entity; tag : SpotLight) : SpotLight

.. das:function:: get(world : World; entity : Entity; tag : Hierarchy) : Hierarchy

.. das:function:: get(world : World; entity : Entity; tag : WorldTransform) : WorldTransform

.. das:function:: get(world : World; entity : Entity; tag : RigidBody) : RigidBody

.. das:function:: get(world : World; entity : Entity; tag : CharacterBody) : CharacterBody

.. das:function:: get(world : World; entity : Entity; tag : ModelInstance3D) : ModelInstance3D

.. das:function:: get(world : World; entity : Entity; tag : Area3D) : Area3D

.. das:function:: get(world : World; entity : Entity; tag : Audio) : Audio

.. das:function:: get(world : World; entity : Entity; tag : PrefabInstance) : PrefabInstance

.. das:function:: get(world : World; entity : Entity; tag : Subviewport) : Subviewport

.. das:function:: get(world : World; entity : Entity; tag : auto(T)) : T&

.. das:function:: get_or_default(world : World; entity : Entity; tag : Transform) : Transform

.. das:function:: get_or_default(world : World; entity : Entity; tag : Transform2D) : Transform2D

.. das:function:: get_or_default(world : World; entity : Entity; tag : Camera2D) : Camera2D

.. das:function:: get_or_default(world : World; entity : Entity; tag : Sprite2D) : Sprite2D

.. das:function:: get_or_default(world : World; entity : Entity; tag : PointLight) : PointLight

.. das:function:: get_or_default(world : World; entity : Entity; tag : DirectionalLight) : DirectionalLight

.. das:function:: get_or_default(world : World; entity : Entity; tag : SpotLight) : SpotLight

.. das:function:: get_or_default(world : World; entity : Entity; tag : Hierarchy) : Hierarchy

.. das:function:: get_or_default(world : World; entity : Entity; tag : WorldTransform) : WorldTransform

.. das:function:: get_or_default(world : World; entity : Entity; tag : RigidBody) : RigidBody

.. das:function:: get_or_default(world : World; entity : Entity; tag : CharacterBody) : CharacterBody

.. das:function:: get_or_default(world : World; entity : Entity; tag : ModelInstance3D) : ModelInstance3D

.. das:function:: get_or_default(world : World; entity : Entity; tag : Area3D) : Area3D

.. das:function:: get_or_default(world : World; entity : Entity; tag : Audio) : Audio

.. das:function:: get_or_default(world : World; entity : Entity; tag : PrefabInstance) : PrefabInstance

.. das:function:: get_or_default(world : World; entity : Entity; tag : Subviewport) : Subviewport

.. das:function:: get_or_default(world : World; entity : Entity; tag : auto(T) -const) : T&

   Live reference to a Daslang component, or a per-type zero sink when the
   entity is stale or the component is absent. The sink keeps the return
   type a reference (so writes compile) while making the write a no-op,
   which is exactly how the built-in proxy accessors behave.

.. das:function:: try_get(world : World; entity : Entity; tag : auto(T) -const; var value : auto(Q)&) : bool

   Checked copy-out: fills `value` and returns true when the component is
   present, otherwise leaves `value` untouched and returns false.

   Daslang cannot spell an optional reference (a local reference must be
   initialized, and a tuple cannot hold one), so this copies the component
   into the caller's variable instead of aliasing it. Use `has` plus `get`
   (or `get_or_default`, which is a live view) when the caller needs to
   write through to the component. The tag and the destination are separate
   witnesses because Daslang deduces `auto` constness from each argument:
   the tag temporary keeps the `const` spelling used by the type registry,
   while `var value` stays mutable.

.. das:function:: has_singleton(world : World; tag : auto(T) -const) : bool

   Scene-local singleton values, owned by the world rather than an entity.
   `get_singleton` panics when absent; `get_singleton_or_default` is a live
   view that falls back to a zero sink, and `try_get_singleton` is a checked
   copy-out. The tag uses `auto(T) -const` so the deduced type stays mutable
   (and its `typeinfo` spelling has no trailing `const`), which is what
   `get_singleton` returns by reference.

.. das:function:: add_singleton(world : World; tag : auto(T) -const) : bool

.. das:function:: remove_singleton(world : World; tag : auto(T) -const) : bool

.. das:function:: get_singleton(world : World; tag : auto(T) -const) : T&

.. das:function:: get_singleton_or_default(world : World; tag : auto(T) -const) : T&

   Live reference to a scene-local singleton, or a per-type zero sink when
   it is absent (same live-view-or-no-op contract as `get_or_default`).

.. das:function:: try_get_singleton(world : World; tag : auto(T) -const; var value : auto(Q)&) : bool

Commands
--------

.. das:function:: defer_add(commands : CommandBuffer; entity : Entity; tag : auto(T)) : bool

   Generic component changes cannot be `CommandBuffer` methods (Daslang
   forbids generic members), so they stay free functions; every non-generic
   structural operation is a `CommandBuffer` method above.

.. das:function:: defer_remove(commands : CommandBuffer; entity : Entity; tag : auto(T)) : bool

Events
------

.. das:function:: connect_area_entered(handler_name : string) : bool

.. das:function:: connect_area_exited(handler_name : string) : bool

.. das:function:: subscribe_area_entered(handler_name : string) : Subscription

   Typed subscription to the engine-owned Area3D overlap events. Declares
   the handler (validating its payload against SensorOverlap) and connects
   it, returning a disposable handle; empty when the handler signature does
   not match the engine payload. Works for physics emits and
   `event_post("entered"/"exited", overlap)` alike.

.. das:function:: subscribe_area_exited(handler_name : string) : Subscription

.. das:function:: subscribe_event(event_name : string; handler_name : string) : Subscription

   Declares a raw-POD handler method on the calling system and connects it to
   a same-payload event source, returning a disposable handle. Must be called
   from inside a system lifecycle call; returns an empty handle otherwise (or
   when the handler signature does not match the event payload).

.. das:function:: unsubscribe_event(event_name : string; handler_name : string) : bool

Messages
--------

.. das:function:: message_at(name : string; index : uint; tag : auto(T)) : T

   Typed read of one published message by index. Returns a value copy (or
   `default<T>` when the type is unknown, unpublished, or the index is out
   of range), so callers never alias the buffer the next drain recycles.
   Pair with `for_each_message(name) $(i : uint)` for typed iteration.

Systems
-------

.. das:function:: enable_system(name : string) : bool

   Scene system control, by engine display name (the name passed to the
   native system constructor; for script systems, the adapter name given at
   instantiation). Disabling runs on_stop and stops updates; enabling
   re-runs on_start when needed and resumes updates. Unknown names report
   false. Only systems in the calling scene are affected; core engine
   systems are not reachable here.

.. das:function:: disable_system(name : string) : bool

.. das:function:: system_enabled(name : string) : bool

.. das:function:: set_stage(stage : string) : void

   Scheduling declarations for the calling system. Call once from on_start:
   stages order execution phases, dependencies force before/after edges, and
   conflicts forbid co-scheduling. These only affect the real frame
   scheduler; direct scene.update() drives systems in registration order.

.. das:function:: depends_on(name : string) : void

.. das:function:: conflicts_with(name : string) : void

Transform
---------

.. das:function:: move(world : World; entity : Entity; delta : float3) : bool

.. das:function:: set_position(world : World; entity : Entity; position : float3) : bool

.. das:function:: move_2d(world : World; entity : Entity; delta : float2) : bool

.. das:function:: set_position_2d(world : World; entity : Entity; position : float2) : bool

Physics
-------

.. das:function:: apply_force(world : World; entity : Entity; force : float3) : bool

.. das:function:: apply_impulse(world : World; entity : Entity; impulse : float3) : bool

.. das:function:: move_character(world : World; entity : Entity; velocity : float3) : bool

Animation
---------

.. das:function:: has_animation(world : World; entity : Entity) : bool

.. das:function:: play_animation(world : World; entity : Entity; clip : string) : bool

.. das:function:: crossfade_animation(world : World; entity : Entity; clip : string; duration : float) : bool

.. das:function:: stop_animation(world : World; entity : Entity) : bool

.. das:function:: set_animation_speed(world : World; entity : Entity; speed : float) : bool

.. das:function:: set_animation_time(world : World; entity : Entity; seconds : float) : bool

.. das:function:: set_animation_loop(world : World; entity : Entity; looping : bool) : bool

.. das:function:: pause_animation(world : World; entity : Entity) : bool

.. das:function:: resume_animation(world : World; entity : Entity) : bool

.. das:function:: is_animation_playing(world : World; entity : Entity) : bool

.. das:function:: is_animation_paused(world : World; entity : Entity) : bool

.. das:function:: is_animation_finished(world : World; entity : Entity) : bool

.. das:function:: animation_time(world : World; entity : Entity) : float

.. das:function:: animation_duration(world : World; entity : Entity) : float

Audio
-----

.. das:function:: play_audio(world : World; entity : Entity) : bool

.. das:function:: stop_audio(world : World; entity : Entity) : bool

.. das:function:: pause_audio(world : World; entity : Entity) : bool

.. das:function:: resume_audio(world : World; entity : Entity) : bool

.. das:function:: set_audio_volume(world : World; entity : Entity; volume : float) : bool

.. das:function:: is_audio_playing(world : World; entity : Entity) : bool

Camera and picking
------------------

.. das:function:: active_camera(world : World) : Entity

.. das:function:: set_active_camera(world : World; entity : Entity) : bool

.. das:function:: camera_fov(world : World; camera : Entity) : float

.. das:function:: set_camera_fov(world : World; camera : Entity; fov : float) : bool

.. das:function:: camera_near(world : World; camera : Entity) : float

.. das:function:: set_camera_near(world : World; camera : Entity; near_val : float) : bool

.. das:function:: camera_far(world : World; camera : Entity) : float

.. das:function:: set_camera_far(world : World; camera : Entity; far_val : float) : bool

.. das:function:: camera_aspect_ratio(world : World; camera : Entity) : float

.. das:function:: set_camera_aspect_ratio(world : World; camera : Entity; aspect : float) : bool

.. das:function:: pick_ray(world : World; camera : Entity; mouse_x : float; mouse_y : float; vp_x : float; vp_y : float; vp_w : float; vp_h : float) : tuple<float3,float3,bool>

   Builds a world-space pick ray through a camera pixel. Viewport describes
   the rendered region in pixels (usually the full window). Returns the ray
   origin, direction, and whether the camera had valid camera state.

.. das:function:: ray_plane_hit(origin_x : float; origin_y : float; origin_z : float; dir_x : float; dir_y : float; dir_z : float; plane_x : float; plane_y : float; plane_z : float; plane_nx : float; plane_ny : float; plane_nz : float) : tuple<float3,bool>

   Intersects a ray with a plane. Returns the hit point and whether the ray
   actually hits (grazing and backward rays miss).

.. das:function:: window_size() : float2

   Current window size in pixels. Refreshes the cached size first, mirroring
   the legacy refresh_window_size/get_window_width/get_window_height trio.

Models, prefabs, scenes
-----------------------

.. das:function:: set_model(world : World; entity : Entity; path : string) : bool

.. das:function:: set_model_material(world : World; entity : Entity; path : string) : bool

.. das:function:: set_model_visibility_range(world : World; entity : Entity; distance : float) : bool

.. das:function:: instantiate_prefab(world : World; path : string) : Entity

.. das:function:: change_scene(world : World; name : string) : bool

Services
--------

.. das:class:: Input

   .. das:method:: key_pressed(scancode : int) : bool

      Raw hardware state (escape hatch). Prefer named actions for gameplay:
      they survive rebinding and combine keyboard, mouse and gamepad inputs.
      `scancode` is an SDL scancode (for example SDL_SCANCODE_SPACE).

   .. das:method:: set_mouse_captured(captured : bool) : bool

   .. das:method:: mouse_position() : float2

   .. das:method:: mouse_delta() : float2

      Consumes the accumulated relative motion: read at most once per frame.

   .. das:method:: mouse_button_down(button : int) : bool

      SDL button numbering: 1 = left, 2 = middle, 3 = right.

   .. das:method:: is_cursor_visible() : bool

   .. das:method:: set_cursor_visible(visible : bool) : void

   .. das:method:: pressed(action : string) : bool

      Named action map. Bind keyboard scancodes with bind_key or SDL mouse
      buttons (1 = left, 2 = middle, 3 = right) with bind_mouse; unbind
      removes the mapping. Held state polls SDL directly, while
      just_pressed / just_released report this frame's published input
      events (repeats do not count as fresh presses).

   .. das:method:: just_pressed(action : string) : bool

   .. das:method:: just_released(action : string) : bool

   .. das:method:: value(neg_action : string; pos_action : string) : float

      Signed 1D axis from two actions: +1, 0, or -1.

   .. das:method:: value2(neg_x : string; pos_x : string; neg_y : string; pos_y : string) : float2

      Two-axis vector from four actions.

   .. das:method:: value3(neg_x : string; pos_x : string; neg_y : string; pos_y : string; neg_z : string; pos_z : string) : float3

      Three-axis vector from six actions.

   .. das:method:: bind_key(action : string; scancode : int) : bool

   .. das:method:: bind_mouse(action : string; button : int) : bool

   .. das:method:: unbind(action : string) : bool

.. das:class:: Logger

   .. das:method:: debug(message : string) : void

   .. das:method:: info(message : string) : void

   .. das:method:: warn(message : string) : void

   .. das:method:: error(message : string) : void

   .. das:method:: trace(message : string) : void

   .. das:method:: debug_fields(message : string; fields : LogFields) : void

   .. das:method:: info_fields(message : string; fields : LogFields) : void

   .. das:method:: warn_fields(message : string; fields : LogFields) : void

   .. das:method:: error_fields(message : string; fields : LogFields) : void

   .. das:method:: trace_fields(message : string; fields : LogFields) : void

.. das:class:: LogFields

   Ordered structured-log fields. Parallel key/value arrays keep call sites
   readable and emission order deterministic (table iteration order is not).
   Values are preformatted by the caller; the engine logs the joined line.

   .. das:attribute:: keys : array<string>

   .. das:attribute:: values : array<string>

   .. das:method:: add(key : string; value : string) : void

.. das:class:: UiService

   .. das:method:: bind_button(element_id : string; handler : string) : bool

.. das:function:: format_log_message(message : string; fields : LogFields) : string

   Renders "message k1=v1 k2=v2"; a field-less message is returned as-is.

Miscellaneous
-------------

.. das:function:: sensor_overlap(area : Entity; other : Entity) : SensorOverlap

   Builds the raw-POD overlap payload from two entity handles. This is the
   one place a script needs raw entity ids, because the engine's physics
   events carry them; convert them back with `world.sensor_area` /
   `world.sensor_other`.

.. das:function:: set_linear_velocity(world : World; entity : Entity; velocity : float3) : bool

   Directly sets the simulated linear velocity of a dynamic rigid body,
   bypassing forces. Returns false when the entity is dead, has no
   RigidBody, or owns no live physics body yet (bodies are created by the
   physics system; a freshly added component may need one update).

.. das:function:: linear_velocity(world : World; entity : Entity) : float3

   Reads the simulated linear velocity of a rigid body. Returns zero when
   the entity is dead, has no RigidBody, or owns no live physics body yet.

.. das:function:: teleport_body(world : World; entity : Entity; position : float3) : bool

   Teleports a rigid body to a world position, preserving its rotation.
   Dynamic bodies are body-authoritative so the move sticks; static and
   kinematic bodies are transform-driven every update, so move those with
   set_position instead. Returns false when the entity is dead, has no
   RigidBody, or owns no live physics body yet.

.. das:function:: body_position(world : World; entity : Entity) : float3

   Reads the live simulated world position of a rigid body. For dynamic
   bodies this is the physics truth, which can differ from the authored
   Transform between syncs. Returns zero when the entity is dead, has no
   RigidBody, or owns no live physics body yet.

Queries
-------

Bulk work uses the ``query`` macro. The block parameters declare the
required components (each must be a ``&`` reference); any arguments
between the World and the block are typed exclusion tags. The macro
drives the smallest component pool and binds live references; structural
changes inside the block are rejected, so queue them through
``ctx.commands``.

.. code-block:: das

   // every entity with a Transform, mutating it in place
   query(ctx.world) $(t : Transform&) {
       t.position.y += 1.0 * dt
   }

   // required components only, no explicit world
   query() $(t : Transform&; v : Velocity&) {
       t.position += v.linear * dt
   }

   // every Transform holder that has no Sprite2D and no Audio
   query(ctx.world, Sprite2D(), Audio()) $(t : Transform&) {
       t.position.y += 1.0 * dt
   }

``query`` requires every listed component parameter to be a ``&``
reference. The exclusion tags are typed, so no raw component id reaches
gameplay code; omit them (or pass no world) to disable filtering.
Component type ids are resolved once per query call, never per entity.
There are no cached query plans yet; each call resolves the ids and
builds the view.

Availability notes
------------------

- Animation functions return ``false`` (or ``-1`` for time
  queries) when the entity lacks an ``Animator``.
- Audio functions return ``false`` when the entity lacks an
  ``Audio`` component.
- ``message_at`` returns ``default<T>`` for unknown types or
  out-of-range indices.
- Raw ``event_emit`` / ``event_post`` / ``event_connect`` /
  ``message_post`` / ``message_count`` / ``for_each_message``
  natives live in ``weasel_api`` (see :doc:`weasel_api`) and
  are used directly; the facade adds typed wrappers where a
  copy or handle is needed.
