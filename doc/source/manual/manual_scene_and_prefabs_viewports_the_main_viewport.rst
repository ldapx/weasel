The Root Viewport
=================

The root viewport is the primary rendering target of a scene. Every scene has
exactly one, configured through the ``rendering_manager`` singleton:

============ ==========================================================
Property     Description
============ ==========================================================
Base         Per-scene design resolution (``root_viewport_virtual_size``).
Resolution   Used as the reference for viewport scaling and 2D layout, and
             as the virtual canvas for RmlUi (Virtual-Canvas UI).
Root         The shared ``viewport_settings`` block: scale mode, aspect
Viewport     ratio policy, filtering, antialiasing, render scale, update
Settings     and clear mode, and environment overrides. The fresh-scene
             defaults are:

             * scale mode ``Disabled`` with aspect ``Ignore`` (fullscreen
               presentation),
             * update mode ``always``, clear mode ``always``,
             * linear filtering, MSAA off, render scale ``1.0``.
Root         Computed, read-only effective rendering dimensions of the
Viewport     root render target. Use it to verify which resolution stage a
Resolution   wrong on-screen size comes from.
Render       The viewport displayed in the game view: root when empty,
Viewport     otherwise the selected subviewport entity. A display selector
             only — it does not change what renders where.
============ ==========================================================

Root rendering
--------------

The root viewport renders its subtree (all entities and nested subviewports)
into the root render target, then presents the result through the single
frame-wide post-process (bloom + tonemap). With ``Keep`` aspect policies the
unused border is cleared black; with ``Keep Width``, ``Keep Height`` and
``Expand`` the visible virtual area changes instead of adding bars.

The root viewport's virtual canvas is also the coordinate space for the
Virtual-Canvas UI (RmlUi, D15): UI renders at base resolution into an
offscreen canvas and composites through the resolved output rect, so UI
scaling and pointer mapping follow the root's scaling mode and aspect ratio
policy. ``Root Viewport Resolution`` and ``Container Size`` in the rendering
manager inspector are computed, read-only values for diagnosing the chain.

Window configuration (initial size, mode, minimum/maximum size and whether the
window is resizable) is owned by Project Settings and applies to standalone
game builds only. Editor host window settings are preserved.
