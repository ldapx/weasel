.. _cpp-animation:

Animation
=========

Skeletal animation in Weasel is built on `ozz-animation
<https://github.com/guillaumeblanc/ozz-animation>`_. The engine does **not**
decode glTF animation data at runtime. Instead a glTF is converted once, offline,
into ozz runtime data, and playback samples that data on the CPU before handing
a joint palette to the GPU.

.. code-block:: text

   .glb ──(gltf2ozz, once)──> Fox.skel.ozz          skeleton
                              Fox_Walk.anim.ozz     one file per clip
                              Fox_Run.anim.ozz
                              Fox.import.json       cache sidecar

The pipeline
------------

#. **Import.** Drop a rigged glTF into the project's models directory. On project
   scan the engine notices the model declares animation clips, runs
   ``gltf2ozz`` once, and writes the ``.ozz`` outputs next to the source along
   with a ``.import.json`` sidecar. The sidecar records the source file's size
   and modification time, so an unchanged model is never re-converted and a
   re-exported one is converted again.

   Conversion is **best-effort**: if the tool is unavailable or a model is
   malformed, a warning is logged and the model still loads and renders. It just
   has no clips. Models that declare no animations at all are recorded as such
   and never cost a subprocess.

   The explicit form remains available for CI and one-off conversions::

      weasel-cli import anim rsc/models/Fox.glb

#. **Skin data.** Loading the glTF imports the skin: joint node indices, joint
   names and inverse-bind matrices, plus the per-vertex ``JOINTS_0``/``WEIGHTS_0``
   attributes. Four influences are supported.

#. **Playback.** :cpp:class:`wsl::comp::animator` names a clip and the
   :cpp:class:`sys::animation_system` samples it.

Using the Animator component
----------------------------

Add **Animator** to the entity that also carries ``Model Instance 3D`` for a
rigged model. The inspector offers a **Clip** dropdown listing exactly the clips
that model declares, so a clip with mismatched joints cannot be assigned by
accident.

============================ ==================================================
Field                        Meaning
============================ ==================================================
``clip_path``                Path to the ``.anim.ozz`` clip to play.
``skeleton_path``            Optional ``.skel.ozz`` path. Left as ``None`` the
                             conventional ``<model>.skel.ozz`` is derived from
                             the clip name.
``skin_index``               Which model skin to drive; ``0`` for a single-skin
                             model.
``speed``                    Playback rate multiplier; negative plays in
                             reverse.
``loop``                     Wrap at the end of the clip.
``crossfade_duration``       Fade length, in seconds, when the clip changes.
``playing``                  Authored enable flag.
``time``                     Playback position, in seconds.
============================ ==================================================

Choosing a new clip starts a crossfade from the outgoing clip. Changing
``clip_path`` is exactly what triggers it, which is also what the scripting API
below relies on.

Scripting
---------

The ``weasel`` facade exposes the same operations as checked
world-scoped functions (``play_animation(world, entity, ...)``,
``crossfade_animation(...)``, ``set_animation_speed/time/loop``,
``stop_animation``); the raw natives below work the same way with a raw
entity id and are what the facade calls internally:

.. code-block:: das

   require weasel

   class System : GameSystem {
       def override update(var ctx : SystemContext; dt : float) : void {
           var player = ctx.world.find_by_name("Player")
           if (!ctx.world.is_alive(player)) {
               return
           }
           // Start a clip on an entity that has an Animator.
           play_animation(ctx.world, player, "res://rsc/models/Fox_Walk.anim.ozz")

           // ...or fade into one over half a second.
           crossfade_animation(ctx.world, player, "res://rsc/models/Fox_Run.anim.ozz", 0.5)

           set_animation_speed(ctx.world, player, 1.5)
           set_animation_time(ctx.world, player, 0.0)   // scrub
           set_animation_loop(ctx.world, player, true)
           if (is_animation_finished(ctx.world, player)) {
               stop_animation(ctx.world, player)
           }
       }
   }

The raw ``anim_*`` natives take a raw entity id and are what the facade
calls internally; gameplay code should use the ``*_animation`` functions
above.

GPU skinning
------------

:cpp:class:`sys::animation_system` writes a model-space joint palette into
``comp::skeleton_pose`` (``joint model matrix * inverse-bind``), in the model
skin's own joint order. ``render_frame`` publishes it on the draw command and
``scene_renderer`` binds a skinned pipeline variant for that draw only.

The blend itself is a 4-influence linear blend skin performed in the vertex
shader, implemented in ``rsc/shaders/skinning.slang`` and shared by the main,
shadow, point-shadow, SSAO-prepass and outline passes, so shadows and selection
outlines follow the deformation. Static meshes use the plain shader variant,
which has no palette binding at all and costs nothing.

The palette is a per-draw vertex uniform at set 1, capped at 128 joints; only
the used prefix is uploaded.

Known limits
------------

* **Culling uses model-space bounds.** A rig that animates well outside its
  bind pose can be culled too early. Per-clip bounds are a later optimization.
* **Four influences.** Additional ``JOINTS_n``/``WEIGHTS_n`` sets are rejected at
  import.
* **Rigs up to 128 joints.** Longer rigs are truncated rather than refused, so
  verify the joint count in the inspector if deformation looks wrong.

.. cpp:autodoc:: wsl/comp/animator.hpp
.. cpp:autodoc:: wsl/comp/skeleton_pose.hpp

.. note::

   The importer and loader headers (``animation_importer.hpp``,
   ``animation_loader.hpp``, ``skeleton_loader.hpp``) are not autodoc'd here:
   they include ozz runtime headers that the documentation toolchain does not
   have on its include path. Their public API is summarised in
   `The pipeline`_ above.
