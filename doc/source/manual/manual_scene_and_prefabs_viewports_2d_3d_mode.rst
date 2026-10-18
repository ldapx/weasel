2D & 3D Mode
============

A subviewport's mode is *component-driven*; there is no mode enum to keep in
sync. The components on the subviewport entity decide everything:

* ``comp::transform_2d`` present → **2D SubViewport**. All 3D passes and
  3D-only render targets (depth, MSAA, bloom) are skipped automatically.
* ``comp::transform`` / ``comp::world_transform`` present → **3D SubViewport**.

The camera picker follows the mode: 2D subviewports list descendant entities
with ``comp::camera_2d``, 3D subviewports list descendant entities with
``comp::camera``.

.. list-table:: Mode matrix
   :header-rows: 1

   * - Child mode
     - Parent mode
     - Composition
   * - 2D
     - 2D
     - Child texture composited as a 2D overlay inside the parent's target,
       sorted by ``Display Order``.
   * - 2D
     - 3D
     - Child texture composited as a 2D overlay on top of the parent's 3D
       pass (before the parent's tonemap), sorted by ``Display Order``.
   * - 3D
     - 3D
     - Child texture drawn on a world-space quad inside the parent's 3D pass
       with depth testing. Unlit/emissive presentation: parent lights and
       shadows do not modulate the screen.
   * - 3D
     - 2D
     - Unsupported combination in v1: a 3D child inside a 2D parent has no
       meaningful world placement, so its quad is not composited. Use a 3D
       parent for in-world screens.

If no camera is assigned (or the assigned entity is invalid / does not match
the mode), the viewport clears its render target with its configured clear
colour and depth, and skips every draw pass.
