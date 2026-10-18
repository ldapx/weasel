Viewports
=========

The Weasel viewport system renders a scene through a tree of *viewports* and
presents the result in the game window.

There are exactly two viewport concepts:

* The :ref:`root viewport <viewports_root>` — the per-scene primary rendering
  target, configured through the ``rendering_manager`` singleton.
* The :ref:`subviewport <viewports_sub>` — an entity component
  (``comp::subviewport``) that defines an independent local rendering context
  for its descendants.

Everything else in the rendering backend is an internal, resolved pixel
rectangle produced by the engine-level resolution chain.

.. toctree::
   :maxdepth: 2

   manual_scene_and_prefabs_viewports_2d_3d_mode
   manual_scene_and_prefabs_viewports_2d_3d_cameras
   manual_scene_and_prefabs_viewports_the_main_viewport
   manual_scene_and_prefabs_viewports_2d_subviewports
   manual_scene_and_prefabs_viewports_3d_subviewports
   manual_scene_and_prefabs_viewports_recipes

.. _viewports_root:

Root Viewport
-------------

Every scene has exactly one root viewport. It defines the scene's primary
rendering context and how its contents are presented in the game window.

Configuration lives in the ``rendering_manager`` singleton:

* **Base Resolution** (``root_viewport_virtual_size``) — the per-scene design
  resolution used as the reference for scaling and 2D layout.
* **Root Viewport Settings** (``root_viewport``) — the same shared
  ``viewport_settings`` structure used by every subviewport: scaling mode,
  aspect ratio policy, filtering, antialiasing, render scale, update/clear
  modes, and environment overrides.
* **Root Viewport Resolution** (computed, read-only) — the effective
  rendering dimensions of the root render target.
* **Render Viewport** — which viewport the game view displays: the root when
  empty, otherwise the selected subviewport entity. This is a display selector
  only; it does not change what renders where.

.. _viewports_resolution_chain:

Resolution Chain
----------------

Window dimensions, base resolution, viewport resolution and the final
rendering resolution are independent stages. Changing one does not implicitly
change another unless the selected scaling configuration requires it:

1. **Base resolution** — per-scene design size (root virtual size).
2. **Container resolution** — the available output area in pixels: the game
   window in standalone builds, the Game View panel rect in the editor, or the
   derived pixel rect of a subviewport inside its parent.
3. **Rendering resolution** — the size of the GPU render target. It depends on
   the scaling mode and render scale.
4. **Viewport resolution** — the per-viewport target size; every subviewport
   can render at its own resolution.
5. **Window resolution** — the desktop window size, owned by Project Settings
   (standalone builds only).

The physical **display resolution** is owned by the operating system. The game
window is presented on the display at whatever size the OS/window manager
gives it; fullscreen modes may change the presentation resolution through the
OS rather than through the engine.

Scaling Modes
-------------

**Disabled**
  No automatic scaling; one virtual unit corresponds to one rendered pixel
  (times the render-scale quality knob). Resizing the window changes the
  visible area or the amount of unused space, depending on the aspect ratio
  policy.

**2D Scaling**
  Content is rendered directly at the target rendering resolution instead of
  being rendered into a fixed-size viewport texture and upscaled. The base
  resolution supplies the reference dimensions for 2D layout, the target
  resolution supplies the pixel density.

**Viewport Scaling**
  Content renders into a target whose dimensions match the configured virtual
  size. The texture is then scaled to fit the output area subject to the
  aspect ratio policy. For example, a viewport configured at 1280x720 shown
  in a 1920x1080 window renders at 1280x720 and is presented with a scale
  factor of 1.5.

Aspect Ratio Policies
---------------------

Given container size ``(Wc, Hc)`` and base size ``(Wb, Hb)``:

**Ignore**
  Fills the whole area; the content stretches (``sx = Wc / Wb``,
  ``sy = Hc / Hb``).

**Keep**
  Uniform scale ``min(Wc / Wb, Hc / Hb)``; the content is centred with
  black letterbox/pillarbox borders.

**Keep Width**
  The virtual width stays at ``Wb``; the visible height becomes ``Wb / Ac``
  where ``Ac`` is the container aspect ratio. Taller containers reveal more
  vertical content; no letterbox bars.

**Keep Height**
  The virtual height stays at ``Hb``; the visible width becomes ``Hb * Ac``.
  Wider containers reveal more horizontal content; no pillarbox bars.

**Expand**
  Scale ``Hc / Hb`` (wider container) or ``Wc / Wb`` (taller container) while
  expanding the visible virtual area to the container aspect ratio. No black
  bars, reference framing preserved along the limiting axis.

.. _viewports_sub:

SubViewports
------------

``comp::subviewport`` creates an independent rendering context for its
descendants. The mode is component-driven:

* An entity with ``comp::transform_2d`` is a **2D SubViewport**. Its 2D
  position/rotation/pivot place it in the parent's virtual space, ``2D Size``
  gives its extents, and it skips all 3D passes and 3D-only render targets.
* An entity with ``comp::transform`` / ``comp::world_transform`` is a **3D
  SubViewport**. Its rendered texture is displayed on a world-space quad sized
  by ``World Quad Size``.

SubViewports nest arbitrarily. Rendering is post-order (children first) so
every nested texture exists before its parent composites it:

* A 2D child is composited into the parent's render target as a 2D overlay,
  sorted with regular 2D draws by ``Display Order``.
* A 3D child is drawn as a world-space quad inside the parent's 3D pass, so it
  participates in the parent's depth buffer.

Only the presentation chain's root presents to the window; child targets never
do.

Common properties (see the component inspector):

* **Camera** — a single picker filtered by mode: ``camera_2d`` descendants for
  2D subviewports, ``camera`` descendants for 3D subviewports. Without a
  camera the viewport clears its target and skips all draw passes.
* **Virtual Size** — the internal logical canvas used for layout and
  rendering.
* **Container Size** (computed, read-only) — the resolved output pixel size:
  ``2D Size * parent scaling factor`` for 2D subviewports, screen-projected
  bounds for 3D subviewports.
* **Display Order** — 2D compositing order in the parent. 3D quads ignore it
  and use depth.
* **Input Routing** — whether pointer events may be routed into this
  viewport. When enabled, ``sys::route_viewport_point()`` translates a
  window-space pointer through the letterbox/scale chain, the parent's 2D
  composed rects (highest ``Display Order`` first) and 3D quad ray tests,
  returning the deepest viewport and its local virtual coordinates. Setting it
  to ``false`` skips the viewport and its whole subtree.

  .. code-block:: cpp

     sys::viewport_route_point hit = sys::route_viewport_point (registry, {x, y});
     // hit.viewport == entt::null -> root viewport; hit.point is in virtual space
     // hit.hit == false -> the point landed outside the viewport's output rect

* **Update Mode** — ``disabled``, ``once``, ``when_visible``,
  ``when_parent_visible`` or ``always``:

  * ``disabled`` — skipped entirely.
  * ``once`` — renders a single frame, then switches itself to ``disabled``
    (useful for static content such as a map preview).
  * ``when_visible`` (default) — culls 2D rects against the parent's visible
    virtual area and 3D quads against the parent camera frustum.
  * ``when_parent_visible`` — renders only while its parent viewport rendered
    this frame; hidden parents suppress descendants.
  * ``always`` — updates every frame.

* **Clear Mode** — ``always`` (default), ``next_frame`` or ``never``:

  * ``always`` — clears the target every frame.
  * ``next_frame`` — clears on the next frame, then behaves like ``never``.
  * ``never`` — keeps the previous frame's contents.

* **Environment** — per-viewport skybox, ambient, IBL, exposure, bloom, SSAO
  and clear settings. Every knob has a ``use_global`` flag that follows the
  scene environment live. The inspector renders this shared widget identically
  for the root viewport and subviewports.
* **Shadow Caster** — when off, the viewport's subtree is excluded from all
  shadow passes (lights and the clustered lighting grid stay global in v1).
* **HDR** — 3D only. When false (default) the subviewport tonemaps and blooms
  into an LDR/sRGB texture. When true it outputs raw linear HDR so the quad
  participates in parent lighting and bloom.

Fresh-scene defaults
--------------------

.. list-table::
   :header-rows: 1

   * - Property
     - Root viewport
     - SubViewport
   * - Scale Mode
     - ``Disabled``
     - ``Viewport Scaling``
   * - Aspect Ratio
     - ``Ignore``
     - ``Ignore``
   * - Filter
     - Project default (``linear``)
     - Project default (``linear``)
   * - MSAA
     - Project default (off)
     - Project default (off)
   * - Render Scale
     - Project default (``1.0``)
     - Project default (``1.0``)
   * - Update Mode
     - ``Always``
     - ``When Visible``
   * - Clear Mode
     - ``Always``
     - ``Always``
   * - Bloom target
     - Allocated (scene post-process)
     - Not allocated unless Bloom Enabled
   * - HDR
     - n/a (always tonemapped)
     - ``false`` (LDR tonemapped)

A fresh subviewport therefore renders into a target sized from its **Virtual
Size** and composites that texture into its 2D size (``Viewport Scaling`` +
``Ignore``). Its first frame clears with the scene clear colour; override the
colour with the Environment's Clear Color knobs when a different background is
wanted.

Virtual-Canvas UI (RmlUi)
-------------------------

The game UI (RmlUi) renders into the root viewport's virtual canvas at base
resolution and is composited through the resolved output rect, so it scales
with the viewport's scaling mode and aspect ratio policy. Pointer input is
mapped from window space through the same chain into virtual-canvas
coordinates, keeping UI hit-testing exact under letterboxing and scaling.

Performance notes
-----------------

* **Update modes are the main lever.** ``When Visible`` stops a subviewport
  from rendering while it is off-screen, ``Once`` renders static content a
  single time, and ``disabled`` skips it entirely; the whole subtree follows
  the decision.
* **Bloom targets are opt-in for subviewports.** Enable ``Bloom Enabled`` in a
  subviewport's Environment only when the effect is wanted.
* **MSAA cost scales with the sample count** and is off by default: enable it
  per viewport or through the project quality defaults when edges matter.
* **Render Scale below 1.0** shrinks the render target and the shading cost
  that goes with it.
* **Lighting and shadows are shared.** Scene lights, the procedural sun,
  shadow maps and the clustered lighting grid render once for all viewports in
  v1, so extra viewports do not multiply lighting cost.
* **Keep a subviewport's target at or below its on-screen size** (the minimap
  convention). Rendering far more pixels than are displayed is the usual
  source of wasted GPU time; there is no hard viewport cap.
* **Targets reallocate lazily** when the resolved size changes (settings
  edits, window resizes), so animating scale mode, render scale or window size
  every frame reallocates every frame — change them sparingly.
