Viewport Recipes
================

Concrete setups for the most common viewport use cases. Each recipe builds on
the concepts in :doc:`manual_scene_and_prefabs_viewports` and uses the
component names as they appear in the editor's **Add Component** list.

Minimap / picture-in-picture
----------------------------

1. Create an entity (for example ``Minimap``).
2. Add **Transform 2D** and **Sub-Viewport**. The Transform 2D component is
   what selects 2D mode.
3. Create a camera entity as a **child** of ``Minimap`` and add
   **Transform 2D** + **Camera 2D**. Position it and set its zoom to frame the
   area the minimap should show. Cameras only belong to the subviewport whose
   subtree they live in, so parenting matters.
4. On the ``Minimap`` Sub-Viewport:

   * **2D Size** — the extents in the parent's virtual space, e.g.
     ``320 x 180``.
   * **Camera** — pick the camera entity you just created (the picker only
     lists `Camera 2D` descendants).
   * **Virtual Size** — the canvas the minimap's own content lays out in.
   * **Display Order** — a value above your sprite/HUD layers so the minimap
     draws on top.
   * **Update Mode** — keep the default ``When Visible`` so the minimap stops
     rendering when it scrolls off-screen.

5. Position the entity with its Transform 2D. With the root viewport's
   default ``Disabled`` scaling mode, one unit in the root's virtual space is
   one pixel of the Game View, so a position of ``(20, 20)`` places the
   minimap 20 pixels from the top-left.

HUD / UI panel
--------------

Follow the minimap recipe, then:

* Set **Virtual Size** to your design canvas (for example ``1280 x 720``) so
  the panel's contents lay out independently of the window size.
* Set **Update Mode** to ``Always`` — HUD panels are normally on screen every
  frame.
* Leave **Input Routing** enabled if you want pointers routed into the panel;
  ``sys::route_viewport_point()`` returns the deepest hit viewport and the
  pointer position in its local virtual space.
* Give the panel a distinct **Display Order** so it composites predictably
  against other 2D content.

The RmlUi game UI does not use a subviewport: it renders in the root
viewport's virtual canvas, so it scales and letterboxes together with the
scene. Use subviewports for game-world panels; use RmlUi for application UI.

In-world screen / security monitor
----------------------------------

1. Create an entity (for example ``Monitor``) placed in the world and add
   **Transform** and **Sub-Viewport**. The Transform component selects 3D
   mode.
2. Create a camera entity as a **child** of ``Monitor`` and add **Transform**
   + **Camera**. Position and orient it to film what the screen should show.
3. On the ``Monitor`` Sub-Viewport:

   * **World Quad Size** — the quad's width and height in world units, e.g.
     ``2 x 1.5``. The render target keeps this aspect ratio, so the image is
     not stretched.
   * **Camera** — pick the child camera.
   * **Update Mode** — ``When Visible`` (default) culls the monitor when the
     quad leaves the parent camera's frustum.
   * **HDR** — ``false`` for a display-referred screen whose own exposure and
     bloom make it look like a monitor; ``true`` when the screen should emit
     raw light values and bloom into the surrounding scene.

4. Optional: turn **Shadow Caster** off for a screen that should not cast a
   shadow, and set a distinctive **Environment** (for example a different
   skybox) if the screen shows a different world.

The quad is presented unlit: parent lights and shadows do not darken or glare
over it.

Nested viewports (screen-in-screen)
-----------------------------------

SubViewports nest arbitrarily. A subviewport parented under another renders
first and composites into its parent's target, so only the presentation root
ever reaches the window. For example, put a 2D minimap subviewport *inside* a
3D security-monitor subviewport to show a map on the monitor; the minimap
renders, the monitor composites it, and the root composites the monitor quad.

Keep the whole chain's **Update Mode** consistent: a nested viewport with
``When Parent Visible`` is suppressed automatically whenever its parent did
not render.

Debugging what you see
----------------------

* **Root Viewport Resolution** (rendering manager, read-only) is the root
  render target size; **Container Size** is the output area it is shown in
  (window in standalone builds, Game View panel in the editor).
* A subviewport's **Container Size** (read-only) is derived from the parent:
  ``2D Size * parent scaling factor`` for 2D subviewports, screen-projected
  quad bounds for 3D subviewports.
* If a viewport looks the wrong size, compare its Container Size with its
  Display Order sibling and check the *parent's* scale mode and aspect ratio
  policy first — those decide the scaling factor every child inherits.

Configuring from the CLI
------------------------

Viewport settings are reflected components, so they can be edited without the
editor. With a project open, for an entity named ``Minimap``:

.. code-block:: bash

   # Attach a subviewport to an existing entity
   weasel-cli --project /path/to/project --scene main.wscn \
       comp add Minimap subviewport

   # 2D extents and compositing order
   weasel-cli ... comp set Minimap subviewport size [320,180]
   weasel-cli ... comp set Minimap subviewport display_order 10

   # Scaling mode and update cadence (enum values use their display names)
   weasel-cli ... comp set Minimap subviewport settings.scale_mode "Viewport Scaling"
   weasel-cli ... comp set Minimap subviewport settings.update_mode "When Visible"

   # Per-viewport clear colour override
   weasel-cli ... comp set Minimap subviewport settings.env.clear_color_use_global false

The root viewport is configured through the rendering manager singleton:

.. code-block:: bash

   weasel-cli ... singl set rendering_manager root_viewport.aspect_policy "Keep Width"
   weasel-cli ... singl set rendering_manager root_viewport.scale_mode "Viewport Scaling"

Editors attached with ``weasel-cli -a`` apply the same commands live.
