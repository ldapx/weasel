Quad Subviewport Rendering
==========================

A 3D subviewport's texture is presented on a quad sized by ``World Quad Size``
and transformed by the subviewport entity's ``world_transform``.

Target sizing
-------------

The render target keeps the quad's world aspect ratio
(``world_quad_size.x / world_quad_size.y``) while its magnitude comes from the
viewport's ``Virtual Size``, so mapping the texture onto the quad does not
introduce stretching. The camera aspect ratio used for the projection matches
the render target.

Colour handoff
--------------

The presentation is unlit/emissive, so the target's colour space determines
how it participates in the parent scene:

* ``hdr == false`` (default): the subviewport tonemaps and blooms into an
  LDR/sRGB texture. The quad shows exactly what the sub-camera saw, and the
  parent's tonemap does not re-process it.
* ``hdr == true``: the target stays linear HDR. The quad feeds raw light
  values into the parent's HDR target, so parent bloom and exposure affect the
  screen like any other bright surface.

Implementation notes
--------------------

* The quad uses the shared unit-quad model; the sub texture replaces the
  model's base-colour texture and the sampler follows the subviewport's
  scaling filter (``linear`` or ``nearest``).
* The scene renderer's ``set_force_unlit`` path is enabled for quad draws,
  which bypasses PBR lighting and shadow reception.
* Child targets are composited post-order: nested subviewports are fully
  rendered before their parent quad samples them.
