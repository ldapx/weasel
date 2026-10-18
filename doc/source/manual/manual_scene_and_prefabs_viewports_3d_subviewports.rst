3D SubViewports
===============

A 3D subviewport is an entity with ``comp::transform`` /
``comp::world_transform`` and ``comp::subviewport``. It creates an independent
3D rendering context containing its descendants, renders that world from its
assigned ``comp::camera``, and displays the result on a world-space quad.

The quad
--------

* **World Quad Size** — width and height of the display quad in world space.
* The quad is transformed by the entity's ``world_transform`` and drawn inside
  the parent viewport's 3D pass, so it participates in the parent's depth
  test.
* **Container Size** (computed, read-only) — derived from the screen-projected
  bounds of the quad; the render target keeps the quad's world aspect ratio so
  the texture is not stretched.

Presentation pipeline (``HDR``)
-------------------------------

**``hdr == false`` (default)**
  The subviewport runs its own tonemap and bloom pass into an LDR/sRGB target
  honoring its Environment (exposure, bloom, tonemapper). The quad samples a
  display-referred texture and is not tonemapped again by the parent.

**``hdr == true``**
  The subviewport skips tonemapping and outputs raw linear half-float HDR
  without post-processing. The quad then participates in parent scene lighting
  and bloom — intended for in-world HDR screens, portals and security
  monitors.

The quad uses an unlit / pure emissive presentation: the sub texture is
sampled directly without PBR diffuse/specular modulation and does not receive
parent shadows, so parent lights do not darken or glare over in-world screens.

Visibility
----------

The default ``When Visible`` update mode tests the quad's world-space bounding
box against the parent camera's view frustum. ``When Parent Visible``
suppresses the subviewport whenever its parent viewport did not render this
frame.

.. toctree::
   :maxdepth: 1

   manual_scene_and_prefabs_viewports_3d_subviewports_quad_subviewport_rendering
