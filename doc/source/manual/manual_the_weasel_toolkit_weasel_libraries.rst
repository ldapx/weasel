Weasel Libraries
=================

Weasel is built from a set of libraries that can be used together or
individually. Two libraries form the foundation of every Weasel project: the
core engine library and the Daslang bindings that expose it to scripting.

Weasel Core Library (libwsl)
----------------------------

The core library is also referred to as **libwsl**. It is written in **C++** and
is distributed as a **shared library** (``libwsl.so`` on Linux, ``wsl.dll`` on
Windows).

libwsl contains the entire Weasel API. It implements the low-level engine
systems and is where the bulk of the engine lives, including:

- The **Entity Component System** (ECS) used to model entities, world
  components, singleton components and systems.
- The **rendering** pipeline built on SDL3's GPU API (2D and 3D, PBR, lighting
  clusters, shader graph).
- **Physics** integration (3D through Box3D).
- **Audio**, **input**, **resources** and **scene** management.
- **Engine and editor abstractions** -- the data structures, registries and
  services that both the runtime and the Weasel Editor build upon.

Because libwsl is a plain C++ shared library, it can be linked directly into
native applications or consumed by higher-level tooling and bindings.

Weasel Daslang Bindings
-----------------------

The Daslang bindings wrap libwsl so that game and application logic can be
written in daScript. They are oriented toward **application-level** code: the
systems, components and glue that make up a playable project, rather than the
engine internals.

The bindings are exposed through one gameplay module:

- **weasel** -- the gameplay facade: world-scoped entities, the
  ``GameSystem`` base class (``class System : GameSystem`` with
  ``on_start`` / ``update`` / ``fixed_update`` / ``late_update`` /
  ``on_stop``), typed component access, queries, input, time, events,
  and engine services.
- **weasel_editor** -- editor-only helpers (viewport size/refresh).
  Gameplay scripts must never require it.

(``weasel_api`` is the underlying native module; game code reaches it
only through ``weasel``. The legacy ``weasel_helpers`` / ``weasel_ecs``
das modules were removed after the facade absorbed them.)

Together these modules let authors drive the full libwsl API from daScript
without dropping down into C++.

The Daslang bindings are the **main way to create user-defined components and
systems**: authors declare their components and implement system callbacks in
daScript rather than in C++.

daScript is **interpreted by default in the editor**, which enables fast
iteration -- changes to systems and scenes are picked up live without a
rebuild. For distribution, the same daslang code can be **compiled ahead of
time through CMake** into a native build. See
:doc:`Application Build & Playback <manual_application_build_playback>` for the
interpreted and AOT compilation workflows.
