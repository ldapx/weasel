Systems
=======

A **system** is where behavior lives in the ECS. A system declares one or more
*queries* -- patterns such as "every entity that has a ``Transform`` and a
``Camera``" -- and, each frame, obtains the set of matching entities and
operates on their components. By keeping logic in systems and data in
components, ECS avoids the tight coupling of traditional object hierarchies.

Weasel systems follow a common lifecycle driven by the engine. Daslang
game systems (``class System : GameSystem``) implement it as:

- **on_start(var ctx)** -- called once after the scene is ready
- **update(var ctx, dt)** -- called once per active frame
- **fixed_update(var ctx, fixed_dt)** -- zero or more times per frame,
  before physics, with the fixed step
- **late_update(var ctx, dt)** -- called after ordinary frame systems
- **on_stop(var ctx)** -- called before scene removal or script replacement

(Native C++ systems use the ``on_init`` / ``on_update`` / ``on_inactive``
entry points through the class adapter; there is no ``on_event``
callback -- input arrives through the pull-only message bus and game
events through typed subscriptions.)

Two kinds of systems exist in Weasel:

- **Core systems** are the built-in engine systems (rendering, physics,
  transform propagation, lighting, audio, ...). They are always present and are
  described in :doc:`Core Systems <manual_ecs_fundamentals_systems_core_systems>`.
- **User-defined systems** are written by authors, primarily in daScript, to
  implement game-specific logic. They are added to a scene alongside the core
  systems and appear in the **Systems** panel of the editor, where their signal
  connections and iterations can be inspected.

In the **Weasel Editor**, the **Systems** panel (docked on the left) lists all
active systems -- both the built-in core systems and any user-defined systems --
and lets you inspect how they connect to the signal hub.

.. toctree::
   :maxdepth: 1

   manual_ecs_fundamentals_systems_core_systems

