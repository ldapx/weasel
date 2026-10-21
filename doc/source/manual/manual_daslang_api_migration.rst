Daslang Weasel API Migration Guide
==================================

.. note::
   This guide tracked the incremental Daslang API overhaul. The cutover
   is complete: the old surface (``weasel_helpers`` / ``weasel_ecs``
   modules, raw-id component helpers, the old ``query`` macro) no longer
   exists. ``require weasel`` is the only gameplay import; ``EcsSystem``
   lives in the facade and game systems extend ``GameSystem``.

Old systems import three modules, juggle raw integer entity ids, and mix
two component access models. New systems import one module, work with
explicit world-scoped handles, and share one typed component model.

Imports: three modules become one
----------------------------------

.. list-table::
   :header-rows: 1

   * - Old
     - New
   * - .. code-block:: das

          require weasel_api
          require weasel_helpers
          require weasel_ecs
     - .. code-block:: das

          require weasel

The new ``weasel`` module also provides ``SystemContext`` (and the
``ScriptContext`` alias), the ``GameSystem`` base class (game code declares
``class System : GameSystem`` — the engine instantiates the class literally
named ``System``), the ``query`` macro (with typed exclusion tags), and
every helper below. Editor-only code instead requires ``weasel_editor``
(see `Editor boundary`_).

Lifecycle: adapter names become gameplay names
-------------------------------------------------

Game systems still declare ``class System`` (the engine instantiates the
class literally named ``System``), but now extend the ``GameSystem``
facade base instead of ``EcsSystem`` directly. Lifecycle hooks take a
mutable ``var ctx`` so service methods (``ctx.input.*``, ``ctx.log.*``)
bind; a plain ``ctx`` parameter is const and restricts you to field
reads and free functions.

.. list-table::
   :header-rows: 1

   * - Old (``class System : EcsSystem``)
     - New (``class System : GameSystem``)
   * - .. code-block:: das

          def override on_init() : void
     - .. code-block:: das

          def on_start(var ctx : SystemContext) : void
   * - .. code-block:: das

          def override on_update(dt : float) : void
     - .. code-block:: das

          def update(var ctx : SystemContext; dt : float) : void
   * - .. code-block:: das

          def override on_inactive() : void
     - .. code-block:: das

          def on_stop(var ctx : SystemContext) : void

``fixed_update`` runs zero or more times per frame with the physics fixed
step (before the variable pass); ``late_update`` runs once per frame after
it. Both paths — serial ``scene.update()`` and the scheduled
``core_systems`` update — run the same fixed → variable → late order.
Per-substep interleaving of script code inside the physics step loop is
out of scope; fixed script phases run before the variable pass that steps
physics. ``ctx.time.fixed_elapsed`` accumulates completed fixed steps.
Pausing zeroes script ``delta`` but does not skip fixed steps yet.

Time: wall clock plus scaled frame time
---------------------------------------

``ctx.time`` is refreshed on every lifecycle entry. ``delta`` is the
scaled gameplay step (zero while paused); ``unscaled_delta`` is the raw
engine dt. ``fixed_delta`` reads the physics step, ``fixed_elapsed``
accumulates completed fixed steps, ``frame_index`` counts renderer
frames, and ``elapsed`` is wall-clock seconds since process start.
Pause and scale live on the engine runtime (one state per game
instance):

.. code-block:: das

   var dt = ctx.time.delta
   var raw = ctx.time.unscaled_delta
   if ctx.time.paused() {
       // gameplay held; wall clock in ctx.time.elapsed still advances
   }
   ctx.time.set_time_scale(0.5)  // slow motion; back to 1.0 to resume

Negative scales clamp to zero. Pausing freezes the simulation (fixed
script phases and physics stop with no catch-up burst on resume) while
variable/late passes keep running for UI and menus.

Entity identity: integers become handles
----------------------------------------

.. list-table::
   :header-rows: 1

   * - Old
     - New
   * - Raw ``uint`` entity ids (``0u``, ``NULL_ENTITY_ID``),
       ``entity_valid(e)``, ``entity_is_null(e)``, ``null_entity()``.
     - ``Entity`` values from ``world.create_entity()``.
       ``world.is_alive(entity)``, ``is_null(entity)``.
       The active registry is never implicit: pass ``world`` (or
       ``ctx.world``) explicitly.
   * - ``find_entity_by_name(name)`` returns an id (null entity when
       absent).
     - ``world.find_by_name`` / ``world.find_all_by_name`` behave the
       same, with ``Entity`` results.
   * - ``set_entity_name(entity, name)``.
     - ``world.set_name(entity, name)``.

Component access: one model instead of two
------------------------------------------

The old surface mixes live proxy references (built-ins via
``get_component_or(e, Transform())`` field writes) with value copies (the
generic ``get_component_or`` overload returns a copy, or a default when
absent). The new surface makes the distinction explicit per call:

.. list-table::
   :header-rows: 1

   * - Old
     - New
   * - ``get_component_or(e, T())`` (live for built-ins, copy for
       script types; default when absent).
     - ``get(world, e, T())``: live reference, panics when absent.
       ``get_or_default(world, e, T())``: live reference, registered
       defaults when absent (spell the destination ``var v : T&`` to keep
       the view). ``try_get(world, e, T(), value)``: checked copy-out,
       fills ``value`` and reports presence.
   * - ``has_component(TYPE_X(), e)``.
     - ``has(world, e, X())``.
   * - ``add_component(TYPE_X(), e)`` / ``remove_component(TYPE_X(), e)``.
     - ``add(world, e, X())`` / ``remove(world, e, X())`` (``emplace``
       is the same default-construction operation today).
   * - (no equivalent)
     - ``copy_component(world, dst, src, X())`` copies one component
       between entities; ``clone_entity``/``world.clone_entity`` copies
       whole subtrees.
   * - (no equivalent: singletons were C++-only)
     - ``has/add/remove/get/get_or_default/try_get_singleton(world, T())``.
       ``get_singleton`` panics when absent.

Two component rules changed on purpose. First, absence handling is now
per call (panic, default, or boolean) instead of per type. Second,
built-in field writes that used to go through proxies
(``t.position += ...``) keep working, and the same code now also works
for script-defined components, whose query bindings and ``get`` return
live references too.

Queries: same macro, explicit world, plus exclusion
---------------------------------------------------

.. list-table::
   :header-rows: 1

   * - Old
     - New
   * - ``query() $(t : Transform&; ...)`` (implicit active registry).
     - ``query(world) $(t : Transform&; ...)`` (or ``query()`` for the
       active system world).
   * - (no equivalent)
     - ``query(world, Sprite2D(), Audio()) $(...)`` skips entities
       owning any of the listed components. The tags are typed, so no raw
       component id reaches gameplay code; omit them (or pass no world)
       to disable filtering.
   * - Direct ``entity_destroy`` / ``add_component`` inside a query
       (throws a script error).
     - Same rule, enforced the same way. Use ``ctx.commands`` /
       ``CommandBuffer``: ``destroy(entity, mode)``, ``set_parent``,
       ``create_entity`` (returns a usable handle immediately;
       follow-up commands still flush at the barrier), plus the
       ``defer_add`` / ``defer_remove`` free functions, which stay free
       because generic functions cannot be struct members.

Optional, changed-since, predicate, and cached-query terms do not exist
yet; every query term is still a required live reference.

Hierarchy: explicit parents, optional pose preservation
-------------------------------------------------------

.. list-table::
   :header-rows: 1

   * - Old
     - New
   * - Manual ``hierarchy`` component surgery.
     - ``world.set_parent(child, parent, keep_world = true)``,
       ``world.detach(entity)``, ``world.parent_of(entity)``,
       ``world.children_of(entity)``,
       ``world.is_child_of(entity, ancestor)`` and subtree
       ``world.destroy(entity)``. All of them are ``World`` methods:
       there is no second, free-function spelling.
   * - (no equivalent)
     - ``world.destroy(entity, DestroyMode.entity_only)`` detaches
       children to the destroyed entity's parent, preserving their world
       poses. ``DestroyMode.subtree`` (the default) takes the descendants
       with it.
   * - (no equivalent)
     - ``world.clone_entity(src, parent)`` copies a subtree;
       ``world.create_entity(name)`` names the entity as it is created.

By default, reparenting and entity-only destruction recompute local
transforms from on-demand world matrices so the visual pose is preserved
(3D chains; degenerate poses keep linkage only; 2D chains ignore
hierarchy when rendering). Pass ``keep_world = false`` for the old
linkage-only behaviour.

Events: same verbs, plus managed subscriptions
----------------------------------------------

Declaring, connecting, and emitting keep their names:
``event_declare_sink`` / ``event_declare_source`` / ``event_connect`` /
``event_disconnect`` / ``event_emit``. New in the facade:

* ``subscribe_event(name, handler)`` declares *and* connects a handler
  on the calling system and returns a ``Subscription``;
  ``unsubscribe_event(name, handler)`` disconnects it.
* ``Subscription.dispose()`` disconnects and invalidates the handle;
  ``is_valid()`` reports whether it holds a live connection.

Handlers take exactly one raw-POD struct payload
(``def on_ping(ping : Ping)``). Payload size and layout are validated at
connect time and again at dispatch; mismatches are logged and the
handler is skipped. Dispatch requires an active scene.

Delivery timing is explicit. ``event_emit`` dispatches synchronously in
subscription-connection order; a handler that emits re-enters dispatch
immediately. ``event_post`` validates identically but queues a payload
copy and returns; the runtime delivers queued events FIFO at the next
pump, which runs at the start of the next systems update in both the
serial and the scheduled paths. A post made while no scene is active
waits (cap-bound at 256, drops warn); a post made during a pump rolls to
the following pump. Structural mutations from a handler go through
``ctx.commands`` either way. Prefer ``post`` when the receiver must not
run reentrantly inside the sender (notably from inside a query or
another handler); prefer ``emit`` when synchronous delivery is required.

Engine-owned payloads use the same path. The physics Area3D overlap
events (``entered``/``exited``) carry a raw-POD ``SensorOverlap``;
``subscribe_area_entered(handler)`` / ``subscribe_area_exited(handler)``
declare and connect a typed handler and return a disposable
``Subscription``, and ``event_post("entered", overlap)`` reaches those
handlers exactly like a physics emit would. Convert ids with
``world.sensor_area(overlap)`` / ``world.sensor_other(overlap)`` before
retaining handles.

Services: input, physics, audio, animation, rendering, camera
-------------------------------------------------------------

.. list-table::
   :header-rows: 1

   * - Old
     - New
   * - ``is_key_pressed(scancode)``, ``set_relative_mouse_mode``,
       ``mouse_motion_at(i)``, platform message counts.
     - ``input.key_pressed(scancode)``, ``input.set_mouse_captured(...)``,
       ``input.mouse_position()``, ``input.mouse_delta()``
       (consumes the accumulated delta: read at most once per frame),
       ``input.mouse_button_down(button)`` (SDL numbering, 1 = left),
       ``input.is_cursor_visible()`` / ``set_cursor_visible(...)``.
       Named keyboard/mouse actions: ``input.bind_key`` /
       ``bind_mouse``, ``pressed`` (held), ``just_pressed`` /
       ``just_released`` (per-frame edges from the message bus; repeats
       excluded), ``value`` / ``value2`` / ``value3`` axes, ``unbind``.
       Gamepad bindings and file-backed persistence do not exist yet.
   * - ``apply_force(id, ...)`` / ``apply_impulse(id, ...)`` on raw ids.
     - ``apply_force(world, e, ...)`` / ``apply_impulse(world, e, ...)``,
       ``move_character(world, e, velocity)``.
       Velocity/raycast/sweep/contact queries do not exist yet.
   * - Entity audio helpers (``audio_play(e)`` ...).
     - Same entity helpers plus ``is_audio_playing``. Per-instance
       playback handles do not exist yet.
   * - ``anim_play`` ... ``anim_set_loop`` on raw ids.
     - Same operations on entities plus ``pause/resume_animation`` and
       ``is_animation_playing/paused/finished``, ``animation_time``,
       ``animation_duration`` (``-1`` when unknown).
   * - ``get/set_camera_fov`` ... on raw ids and globals.
     - ``active_camera(world)`` / ``set_active_camera`` and
       ``camera_fov/near/far/aspect_ratio`` get/set on entities.
   * - Model path/material/visibility setters on raw ids.
     - Same operations on entities. Sprite tint/size/uv/flip/layer and
       point/directional/spot light color/intensity/range/cones are now
       writable through ``get`` live references. Material parameters,
       environments, and subviewports are not scriptable yet.
   * - ``instantiate_prefab(path)`` (null while loading).
     - ``instantiate_prefab(world, path)`` behaves the same (a loaded
       scene is required; async tickets do not exist yet).

Logging, time, and system control
---------------------------------

* ``_log_debug/info/warn/error`` (and the ``log_*`` aliases) are wrapped
  by ``ctx.log`` (``debug/info/warn/error/trace``), plus a ``trace``
  level. Structured fields go through ``LogFields`` (ordered key/value
  pairs, deterministic emission) and the ``*_fields`` methods, e.g.
  ``ctx.log.warn_fields("Jump failed", fields)`` renders
  ``Jump failed reason=oops``. Use ``format_log_message`` to preview
  the line. Debug drawing and profile scopes do not exist yet.
* ``ctx.time`` carries ``delta`` (scaled, zero while paused),
  ``unscaled_delta``, ``fixed_delta``, ``fixed_elapsed``, ``frame_index``,
  and wall-clock ``elapsed``. Pause/scale are engine-owned runtime state
  reached through the ``ctx.time.paused()`` / ``set_paused(...)`` /
  ``time_scale()`` / ``set_time_scale(...)`` service methods; pausing
  freezes fixed phases and physics while variable/late passes keep
  running.
* Scene systems are toggled by engine display name:
  ``enable_system`` / ``disable_system`` / ``system_enabled``.
  Disabling runs ``on_stop``; re-enabling re-runs ``on_start``.
  Scheduling hints (``set_stage`` / ``depends_on`` / ``conflicts_with``,
  called from ``on_start``) feed the frame scheduler; direct
  ``scene.update()`` still drives registration order.

Editor boundary
---------------

Editor viewport helpers (``refresh_editor_viewport``,
``get_editor_img_*``) moved to ``require weasel_editor``
(``editor_refresh_viewport``, ``editor_viewport_min/size``) and are
no-ops without an attached editor. Gameplay scripts must not require
that module. The underlying natives still live in ``weasel_api`` for
backward compatibility; splitting them into a separate native module is
tracked with the final export removal. The CLI/MCP command channel
stays a tooling protocol, not a gameplay networking API.

Daslang specifics that bite during migration
--------------------------------------------

These are language rules, not engine choices. All of them were hit while
building the facade itself:

* ``assert`` only accepts pure expressions. Hoist every native call
  into a local first, then assert on locals and their fields.
* Function parameters are ``const``. An out-parameter needs an explicit
  ``var`` (``def f(var value : auto(T)&)``), as in daslib's own
  ``serialize_raw``.
* ``auto(T)`` deduces ``const`` from temporaries (so
  ``typeinfo`` spellings carry a ``const`` suffix), while ``-const``
  strips it (``tag : auto(T) -const`` deduces a mutable ``T``).
  The facade exposes both spellings deliberately: const-tag forms for
  type lookup, ``-const`` forms where a live mutable reference is
  returned.
* Members calling same-named free functions confuse generic inference;
  facade methods call natives directly.
* Struct methods cannot overload by arity, and methods cannot be called
  on const values. Lifecycle hooks therefore take ``var ctx`` so that
  every service method (``ctx.input.*``, ``ctx.log.*``, ``ctx.time.*``,
  ``ctx.ui.*``, ``ctx.commands.*``) binds; a plain ``ctx`` parameter
  restricts you to field reads and free functions. The same rule applies
  to ``World``: its methods need a mutable receiver, so keep ``ctx`` as
  ``var`` or copy ``ctx.world`` into a local ``var world`` first.
  Generic component access stays a free function for exactly this
  reason: ``has(world, e, T())`` takes the world by value.
* A game system class must be named exactly ``System`` (the engine
  instantiates the class literally named ``System``) and must extend
  ``GameSystem``, not ``System``: ``class System : GameSystem``.
  ``class System : System`` does not resolve the base and breaks every
  ``override``.
* Generic functions cannot be members of any struct or class
  (``error[20109]``); all generic component access is free functions.
* Never name a script variable ``self`` inside a struct method, and give
  overloaded methods distinct arities from same-named free functions.
* ``range`` is a reserved daslang keyword; it cannot be used as a
  property name (the spot-light cone range is exposed as ``spot_range``).

What still uses the old surface
-------------------------------

* Nothing ships the old surface anymore. The go-demo example runs on the
  new facade (single ``require weasel``, ``class System : GameSystem``,
  ``var ctx`` lifecycle hooks, world-scoped entities, named input
  actions); its in-editor viewport remap for picking was dropped in favor
  of window coordinates (gameplay must not require ``weasel_editor``) and
  is tracked as a known gap until an engine-level game-viewport query
  exists. The legacy test scripts were migrated to the new API;
  ``weasel_helpers.das`` is deleted and ``EcsSystem`` lives in
  ``weasel.das``.
* Anything without a new equivalent yet: raw type ids and
  ``component_data`` plumbing (kept internal), scene manager and
  prefab-async flows, and every item marked future work above.
* AOT compilation accepts module-less scripts using the new surface
  (verified by test). Scripts with an explicit ``module`` declaration
  crash the AOT compiler (upstream issue); interpreted execution is
  unaffected.
