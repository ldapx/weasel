Daslang API Contract (v1)
==========================

Version 1 of the gameplay scripting contract. This document is normative:
where it conflicts with examples or older manual pages, this document wins.
Anything not listed here is not part of the contract.

Scope
-----

Covered: ``require weasel`` (plus ``require weasel_editor`` for editor
tools), ``World``/``Entity`` handles, typed component access, ``query``
with typed exclusion tags, the command buffer, the five lifecycle hooks,
named input actions, ``Time``, ``Logger`` with fields, typed events with
``emit``/``post``/``Subscription``, system enable/disable and scheduling
hints, camera/sprite/light service helpers, pick-ray/plane/window
helpers, and the numeric budgets below.

Explicitly out of scope for v1 (tracked as future work, not promised):
``AssetRef<T>``, ``Result<T>``, ``Ticket<T>``, optional/changed-since/
predicate query terms, cached query plans, gamepad bindings and map
persistence, playback handles, material parameters, environments,
subviewports, asset/prefab/scene tickets, UI documents and data models,
and gameplay networking.

Handles: validity and lifetime
------------------------------

``World`` is a cheap copyable handle (a registry address). ``Entity``
pairs it with a generation-aware entity id. A handle is *stale* when its
entity was destroyed, recycled, belongs to another world, or the world
itself is gone.

* ``is_null(entity)`` is true for default-constructed handles and for
  lookups that found nothing. Null handles never panic; operations on
  them fail gracefully (see below).
* ``world.is_alive(entity)`` is false for stale and cross-world
  handles, true otherwise. It never panics.
* ``destroy`` invalidates the handle immediately to callers. Destroying
  an already-destroyed entity returns ``false`` (no panic, no double
  free).
* Handles are plain values: copying, storing in system fields, and
  passing to functions are all fine. There is no handle ownership or
  borrowing; validity is always re-checked against the live registry.

Per-operation contract
----------------------

Unless noted, every ``bool`` below is ``false`` on stale/missing input
(never a panic), and every entity-returning lookup yields a null
``Entity`` instead.

* ``create_entity`` / ``CommandBuffer.create_entity``: always succeeds
  outside a query. Inside a query, ``world.create_entity`` returns a
  null entity (use ``commands.create_entity``, which returns a valid
  bare handle immediately; follow-up commands flush at the barrier).
* ``destroy(entity, mode)`` (default ``subtree``): destroys
  descendants; ``entity_only`` detaches children to the destroyed
  entity's parent, preserving 3D world poses (2D-only chains keep
  linkage only). Inside a query this panics — use
  ``commands.destroy``.
* ``clone_entity``: copies the source subtree and relinks hierarchy;
  returns null on stale source/parent or inside a query.
* ``copy_component``: copies one component value; ``false`` when
  either endpoint is stale, the source lacks the component, or the
  destination already has it.
* ``entity_count`` / ``find_by_name`` / ``find_all_by_name`` /
  ``set_name``: names live in scenes; ``set_name`` on a scene-less
  registry returns ``false``.
* ``world.parent_of`` / ``world.children_of`` / ``world.set_parent`` /
  ``world.detach`` / ``world.is_child_of``: reject null handles (null
  results / ``false``); ``set_parent`` rejects self-parenting and
  cycles; direct ``set_parent`` inside a query panics (use
  ``commands.set_parent``). Every one of these is a ``World`` method:
  the facade has exactly one spelling per operation, and only generic
  component access (``has`` / ``add`` / ``get`` / ...) stays a free
  function, because Daslang forbids generic struct members.
* ``change_scene(name)``: ``false`` for unknown scenes. The named scene
  must already be loaded; async loading tickets do not exist yet.
* ``instantiate_prefab(world, path)``: null entity when the path is
  not a loaded prefab. Never rely on null meaning "still loading".
* ``set_active_camera(world, entity)``: ``false`` for stale entities.
  ``active_camera`` yields a null entity when no camera is active.

Component access: one rule per call shape
-----------------------------------------

* ``has(world, entity, tag)`` / ``add`` / ``emplace`` / ``remove``:
  ``bool``; ``false`` on stale entities or (for ``has``) absence.
  ``emplace`` default-constructs; value-carrying ``add`` is future work.
* ``get(world, entity, tag)``: live reference; **panics** when the
  entity is stale or the component is missing. Use it when absence is a
  scripting bug.
* ``get_or_default(world, entity, tag)``: live reference; when the
  entity is stale or the component is absent it binds a per-type
  default sink instead, so reads see the registered defaults and
  writes are harmless no-ops. Never panics. Spell the destination
  ``var v : T&`` to keep the view — ``var v = ...`` copies, because
  Daslang only preserves a reference when the destination type says so.
* ``try_get(world, entity, tag, value)``: checked copy-out; fills
  ``value`` and returns whether it was present. Never panics. Daslang
  cannot spell an optional reference (a local reference must be
  initialised, and a tuple cannot hold one), so this copies rather than
  aliases; use ``has`` plus ``get`` / ``get_or_default`` to write
  through.
* ``get_singleton(world, tag)`` panics when absent;
  ``get_singleton_or_default`` is the same live-view-or-sink form as
  ``get_or_default``; ``try_get_singleton`` is a checked copy-out.
  Singleton lifecycle is ``has/add/remove`` like components, without
  entities.
* Built-in proxies (``Transform``, sprites, lights, ...) and Daslang
  component references share exactly these absence semantics; both are
  live views and both write through to live storage.

Queries and the structural barrier
----------------------------------

* ``query(world) $(t : Transform&; ...)`` iterates entities owning
  **all** listed components, driving the smallest pool. The block
  receives live references plus ``entity``. Extra arguments between the
  world and the block are typed exclusion tags —
  ``query(world, Sprite2D(), Audio()) $(t : Transform&)`` skips
  entities owning any of them. ``query()`` uses the active system
  world; omitting the tags disables filtering. No raw component id
  reaches gameplay code.
* Per-invocation cost is one stable-id resolution per term plus view
  setup (~0.43us measured empty). Type ids are resolved per query call,
  never per entity; there are no per-entity allocations or copies in a
  warmed query (measured +0.0 bytes heap growth).
* Direct structural changes inside a query panics
  (``destroy``/``clone``/``add``/``remove``/``set_parent``/
  ``create_entity`` returns null instead of throwing).
  ``CommandBuffer`` (``ctx.commands``) queues destroy, destroy-only,
  add, remove, set-parent, and creation; creation hands back a usable
  bare handle at once, everything else applies at the outermost query
  barrier. Outside iteration, deferred commands apply immediately.

Lifecycle, phases, pause
------------------------

* Order per frame, in both the serial (``scene.update``) and scheduled
  (``core_systems``) paths: fixed steps (0+, physics fixed step) →
  ``update`` → ``late_update``. ``on_start`` runs once after the owning
  scene is ready; ``on_stop`` runs on disable and before scene removal
  or script replacement.
* ``fixed_update`` may run zero times on a slow frame (clamped dt,
  capped substeps, no catch-up spiral); per-substep interleaving inside
  the physics step loop is out of scope.
* A script panic (including a barrier violation) marks the system
  failed; it is skipped afterwards and the failure is logged.
* ``disable_system`` runs ``on_stop`` and stops updates;
  ``enable_system`` re-runs init (``on_start`` runs again) and resumes.
  ``set_stage`` / ``depends_on`` / ``conflicts_with`` (from
  ``on_start``) feed the scheduler; direct ``scene.update`` drives
  registration order.
* Pause/time-scale live on the engine runtime (one state per game).
  While paused, script ``delta`` reads zero, fixed steps do not run
  (accumulator frozen, no catch-up burst), and physics stops stepping;
  variable/late passes and rendering continue so UI and menus stay
  alive. Negative scales clamp to zero.

Events: emit vs post
--------------------

* ``event_emit`` dispatches synchronously in subscription-connection
  order. A handler that emits re-enters dispatch immediately.
* ``event_post`` validates identically, then queues a payload copy and
  returns. Delivery is FIFO at the next pump, which runs at the start
  of the next systems update in both execution paths. Nested posts roll
  to the following pump. Posts with no active scene wait (cap-bound at
  256, drops warn). Dispatch itself still needs an active scene.
* Use ``post`` when the receiver must not run reentrantly (notably from
  inside a query or another handler); use ``emit`` when synchronous
  delivery is required.
* ``subscribe_event`` / ``unsubscribe_event`` and ``Subscription.dispose``
  manage connections; the engine also clears them on scene/system
  teardown, but handlers should dispose in ``on_stop``.
* Engine-owned payloads use the same path: the Area3D ``entered``/
  ``exited`` structs travel as raw-POD ``SensorOverlap``;
  ``subscribe_area_entered`` / ``subscribe_area_exited`` declare and
  connect typed handlers returning disposable subscriptions, and
  ``event_post("entered", overlap)`` reaches them exactly like a physics
  emit. Convert ids with ``world.sensor_area`` / ``world.sensor_other``
  before retaining handles.
* Unknown event names latch a script-defined record on first use; emits
  and posts to zero subscribers are silent no-ops.

Time, input, logging
--------------------

* ``Time``: ``delta`` (scaled, zero while paused), ``unscaled_delta``
  (raw dt), ``fixed_delta`` (physics step, 1/60 fallback),
  ``fixed_elapsed`` (completed fixed steps), ``elapsed`` (wall clock),
  ``frame_index`` (renderer count, 0 headless). Refreshed on every
  lifecycle entry. Pause and scale are service methods on the same
  value: ``ctx.time.paused()`` / ``set_paused(...)`` /
  ``time_scale()`` / ``set_time_scale(...)``.
* Named actions (``bind_key``/``bind_mouse``/``unbind`` onto the app
  input map at runtime): ``pressed`` is held state;
  ``just_pressed``/``just_released`` are per-frame edges scanned from
  the published message bus (repeats never count as fresh presses);
  ``value``/``value2``/``value3`` compose axes (±1/0, neutral on ties).
  Unknown actions read ``false``/``0``; rebinding returns whether it
  took effect. ``mouse_delta`` consumes the accumulated motion: read at
  most once per frame.
* ``Logger`` levels ``trace``/``debug``/``info``/``warn``/``error``
  plus ``*_fields`` variants taking an ordered ``LogFields`` map
  (deterministic ``k=v`` emission). No debug drawing or profile scopes
  yet.

Services with backend prerequisites
-----------------------------------

Camera get/set and fov/near/far/aspect reads and writes, sprite
tint/size/uv/flip/layer, point/directional/spot light color/intensity/
range/cones, 2D move/set-position, and pick-ray/plane/window helpers are
bound. Not scriptable yet: environments, material parameters,
subviewports, body velocity/transform controls, character movement,
raycast/sweep/overlap queries, solid contact events, animation
finished/marker events, per-instance audio handles, asset load/status,
prefab/scene tickets, UI documents/data-model, gamepad bindings and
persistence, and file-backed configuration.

Budgets (v1)
------------

Measured on Linux x86_64 release (informational, machine-dependent);
budgets carry ~2x headroom below the measured floor:

* Single-component query throughput: >= 2M ent/s (measured ~3.7–4.8M).
* Two-component query throughput: >= 2.5M ent/s (measured ~4.6–6.1M).
* Steady-state heap growth per warmed query: exactly 0 bytes
  (asserted in-test where measurable).
* Per-query-call overhead (empty registry): <= 2us (measured ~0.43us).
* Direct C++ EnTT runs the same populations at ~61–180M ent/s;
  interpreter dispatch dominates (~20–35x). AOT numbers are open.

Error taxonomy and cancellation
-------------------------------

* **Panic** (marks the system failed, skips it): missing component or
  singleton on ``get``, structural mutation inside a query, handler
  signature mismatch at connect time, explicit ``panic`` / failed
  ``assert`` in script code.
* **``false`` / null / default** (no failure): stale or cross-world
  handles, absent components on ``has``/``get_or_default``/
  ``try_get``, unknown scenes/prefabs/cameras/bindings/actions,
  missing document elements, failed unbind/dispose of nothing.
* **Drop with warn**: event-post queue beyond 256 entries.
* **Silent no-op**: emit/post with no subscribers or no active scene
  at dispatch; fixed steps skipped while paused.
* **Cancellation**: only ``Subscription.dispose`` exists. Queued posts
  cannot be individually cancelled; tickets (and their cancellation)
  do not exist yet.

Version history
---------------

* **v1** (current): initial contract covering the implemented facade.
  Known omissions are listed under Scope; each names the plan item
  tracking it. The contract advances by appending versions, never by
  silently changing v1 behavior.
