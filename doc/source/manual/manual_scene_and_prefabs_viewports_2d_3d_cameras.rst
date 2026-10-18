2D & 3D Cameras
===============

Cameras frame the scene; 2D and 3D cameras differ in projection and controls.

3D camera (``comp::camera``)
  Perspective projection from the entity's ``comp::world_transform``. The
  field of view, near and far planes come from the component. A camera inside
  a subviewport is detached from the subviewport's quad scale so the frustum
  and skybox are not distorted.

2D camera (``comp::camera_2d``)
  Orthographic projection from the entity's ``comp::transform_2d``. The
  camera position is the *top-left* of the visible area and ``zoom`` scales
  world units, so sprite coordinates remain in virtual-canvas pixels with
  ``(0, 0)`` at the top-left — the same convention as the 3D view.

Camera assignment
-----------------

Cameras do not carry placement information for viewports. A camera belongs to
the nearest ancestor subviewport (walking the hierarchy with cycle detection)
or to the root viewport when there is none. Inside a subviewport, its own
``Camera`` picker selects the camera used to render it:

* 2D subviewports filter for ``comp::camera_2d`` descendants.
* 3D subviewports filter for ``comp::camera`` descendants.

The scene camera (``scene->camera``) is only used for the root viewport.
In the editor, the game-view camera selection overrides the runtime camera
while stopped; pressing play switches back to the subviewport's own cameras.
