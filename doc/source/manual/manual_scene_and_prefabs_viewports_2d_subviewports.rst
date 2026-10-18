2D SubViewports
===============

A 2D subviewport is an entity with ``comp::transform_2d`` and
``comp::subviewport``. It defines a local 2D rendering context for its
descendants and its resulting image is composited as a 2D overlay into the
parent viewport.

Placement and size
------------------

* **Position / Rotation / Pivot** — owned by ``comp::transform_2d``, expressed
  in the parent viewport's virtual space.
* **2D Size** — the extents of the subviewport in the parent's virtual space.
* **Virtual Size** — the internal logical canvas used for the subviewport's
  own layout, 2D coordinates and rendering.
* **Container Size** (computed, read-only) — the resolved pixel rect in the
  parent render target: ``2D Size * parent scaling factor``. Example: a parent
  whose base is 1280x720 shown in a 1920x1080 window has a scaling factor of
  1.5, so a 320x180 sub placed at (100, 50) covers a 480x270 pixel rect at
  (150, 75) in the parent.

Compositing
-----------

The sub's texture is inserted into the parent's 2D sprite queue with
``Display Order`` as its z-index, so it sorts together with regular 2D draws
(ties break by hierarchy order). ``Linear``/``Nearest`` scaling filtering and
the aspect ratio policy are applied when the quad is composited. With ``Keep``
the unused border inside the sub's container is drawn black.

The subviewport renders only while it is visible according to its update mode:
the default ``When Visible`` tests the 2D rect
``[position, position + 2D Size]`` against the parent's visible virtual area.

Common uses: minimaps, UI panels, picture-in-picture views, and independent
2D scenes with their own resolution and scaling settings.
