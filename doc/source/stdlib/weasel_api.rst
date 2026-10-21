weasel_api
==========

.. das:module:: weasel_api

Constants
----------

.. das:data:: SDL_BUTTON_LEFT

   :type: uint32_t

   Value: ``1``

.. das:data:: SDL_BUTTON_RIGHT

   :type: uint32_t

   Value: ``3``

.. das:data:: SDL_SCANCODE_ESCAPE

   :type: int32_t

   Value: ``41``

Component accessor proxies
--------------------------

Component data is read and written through live proxy values created with
``get_component(entity, Tag)``.  The proxy captures the component pointer
once at call time; every later property access reads or writes the actual
entity component in place.  Example::

    def on_update (entity : uint) {
        let t = get_component(entity, transform())
        t.scale.x = 2.0                    // chained leaf write
        t.scale = float3(1.0, 2.0, 3.0)    // full-struct write
        t.position.y = 3.5
        let sx = t.scale.x                 // chained read
    }

The available tags and their properties:

.. list-table::
   :header-rows: 1

   * - Tag function
     - Proxy type
     - Properties
   * - ``transform()``
     - ``TransformAccessor``
     - ``position.x/y/z``, ``scale.x/y/z``
   * - ``transform_2d()``
     - ``Transform2DAccessor``
     - ``position.x/y``, ``scale.x/y``, ``rotation``
   * - ``camera_2d()``
     - ``Camera2DAccessor``
     - ``zoom``
   * - ``sprite_2d()``
     - ``Sprite2DAccessor``
     - ``color.x/y/z/w``, ``size.x/y``
   * - ``point_light()``
     - ``PointLightAccessor``
     - ``color.x/y/z``, ``intensity``
   * - ``directional_light()``
     - ``DirectionalLightAccessor``
     - ``color.x/y/z``, ``intensity``
   * - ``spot_light()``
     - ``SpotLightAccessor``
     - ``color.x/y/z``, ``intensity``

A proxy stays valid only while the entity is alive and still has the
component.  Re-call ``get_component`` after structural changes to the
entity.

Entity operations
-----------------

Functions for creating, destroying, and querying entities.

.. das:function:: entity_create() : uint

   Creates a new empty entity and returns its ID.

   :returns: uint

.. das:function:: entity_destroy(entity : uint)

   Destroys an entity and all of its components.

   :param:
      entity (uint)

.. das:function:: entity_valid(entity : uint) : bool

   Returns ``true`` if the entity ID refers to a live entity.

   :param:
      entity (uint)

   :returns: bool

.. das:function:: entity_is_null(entity : uint) : bool

   Returns ``true`` if the entity ID is the null sentinel.

   :param:
      entity (uint)

   :returns: bool

.. das:function:: null_entity() : uint

   Returns the null entity sentinel value.

   :returns: uint

Scene operations
----------------

Query and modify the active scene (find entities, cameras).

.. das:function:: find_entity_by_name(entity : var) : uint

   Finds an entity by its scene name. Returns null if not found.

   :param:
      entity (var)

   :returns: uint

.. das:function:: set_entity_name(entity : uint, name : var)

   :param:
      entity (uint)
      name (var)

.. das:function:: get_active_camera() : uint

   Returns the entity ID of the active camera.

   :returns: uint

.. das:function:: set_active_camera(entity : uint)

   Sets the active camera to the given entity.

   :param:
      entity (uint)

.. das:function:: instantiate_prefab(path : var) : uint

   :param:
      path (var)

   :returns: uint

Component type ID constants
---------------------------

Stable type IDs for built-in component types.

.. das:function:: TYPE_TRANSFORM() : uint

   Stable type ID for the Transform component.

   :returns: uint

.. das:function:: TYPE_CAMERA() : uint

   Stable type ID for the Camera component.

   :returns: uint

.. das:function:: TYPE_HIERARCHY() : uint

   Stable type ID for the Hierarchy component.

   :returns: uint

.. das:function:: TYPE_WORLD_TRANSFORM() : uint

   Stable type ID for the World Transform component.

   :returns: uint

.. das:function:: TYPE_TRANSFORM_2D() : uint

   :returns: uint

.. das:function:: TYPE_CAMERA_2D() : uint

   :returns: uint

.. das:function:: TYPE_SPRITE_2D() : uint

   :returns: uint

.. das:function:: TYPE_POINT_LIGHT() : uint

   :returns: uint

.. das:function:: TYPE_DIRECTIONAL_LIGHT() : uint

   :returns: uint

.. das:function:: TYPE_SPOT_LIGHT() : uint

   :returns: uint

.. das:function:: TYPE_RIGID_BODY() : uint

   :returns: uint

.. das:function:: TYPE_CHARACTER_BODY() : uint

   :returns: uint

.. das:function:: TYPE_MODEL_INSTANCE_3D() : uint

   :returns: uint

.. das:function:: TYPE_AREA_3D() : uint

   :returns: uint

.. das:function:: TYPE_AUDIO() : uint

   :returns: uint

.. das:function:: TYPE_PREFAB_INSTANCE() : uint

   :returns: uint

.. das:function:: TYPE_SUBVIEWPORT() : uint

   :returns: uint

SDL window operations
---------------------

Window management: cursor, mouse mode, window size.

.. das:function:: set_relative_mouse_mode(enabled : bool) : bool

   Enables or disables relative mouse mode (FPS-style mouse).

   :param:
      enabled (bool)

   :returns: bool

.. das:function:: cursor_visible() : bool

   Returns ``true`` if the cursor is currently visible.

   :returns: bool

.. das:function:: show_cursor()

   Shows the system cursor.

.. das:function:: hide_cursor()

   Hides the system cursor.

Component type lookup
---------------------

Look up component type IDs at runtime.

.. das:function:: get_component_type_id(display_name : var) : uint

   Looks up a component type ID by its display name (e.g. ``"Transform"``).

   :param:
      display_name (var)

   :returns: uint

Other
-----

.. das:function:: get_transform_accessor()

.. das:function:: get_transform_2d_accessor()

.. das:function:: get_camera_2d_accessor()

.. das:function:: get_sprite_2d_accessor()

.. das:function:: get_point_light_accessor()

.. das:function:: get_directional_light_accessor()

.. das:function:: get_spot_light_accessor()

.. das:function:: get_hierarchy_accessor()

.. das:function:: get_world_transform_accessor()

.. das:function:: get_rigid_body_accessor()

.. das:function:: get_rigid_body_mass()

.. das:function:: get_character_body_accessor()

.. das:function:: get_model_instance_3d_accessor()

.. das:function:: get_area_3d_accessor()

.. das:function:: get_audio_accessor()

.. das:function:: get_prefab_instance_accessor()

.. das:function:: get_subviewport_accessor()

.. das:function:: each_world_entity_with()

.. das:function:: each_world_entity_with_except()

Model instance
--------------

Swap models and material overrides on entities.

.. das:function:: log_info(msg : var)

   Logs a message at info level.

   :param:
      msg (var)

.. das:function:: _log_info(msg : var)

   :param:
      msg (var)

.. das:function:: log_debug(msg : var)

   Logs a message at debug level.

   :param:
      msg (var)

.. das:function:: _log_debug(msg : var)

   :param:
      msg (var)

.. das:function:: log_warn(msg : var)

   Logs a message at warning level.

   :param:
      msg (var)

.. das:function:: _log_warn(msg : var)

   :param:
      msg (var)

.. das:function:: log_error(msg : var)

   Logs a message at error level.

   :param:
      msg (var)

.. das:function:: _log_error(msg : var)

   :param:
      msg (var)

.. das:function:: log_trace(msg : var)

   :param:
      msg (var)

.. das:function:: _log_trace(msg : var)

   :param:
      msg (var)

.. das:function:: _current_world()

.. das:function:: _ui_bind_button(element_id : var, method_name : var) : bool

   :param:
      element_id (var)
      method_name (var)

   :returns: bool

.. das:function:: _world_create_entity(world : var) : uint

   :param:
      world (var)

   :returns: uint

.. das:function:: _world_destroy_entity(world : var, entity_world : var, entity_id : uint, context : var, at : var) : bool

   :param:
      world (var)
      entity_world (var)
      entity_id (uint)
      context (var)
      at (var)

   :returns: bool

.. das:function:: _world_entity_is_alive(world : var, entity_world : var, entity_id : uint) : bool

   :param:
      world (var)
      entity_world (var)
      entity_id (uint)

   :returns: bool

.. das:function:: _world_entity_is_null(entity_world : var, entity_id : uint) : bool

   :param:
      entity_world (var)
      entity_id (uint)

   :returns: bool

.. das:function:: _world_find_entity(world : var, name : var) : uint

   :param:
      world (var)
      name (var)

   :returns: uint

.. das:function:: _world_set_name(world : var, entity_world : var, entity_id : uint, name : var) : bool

   :param:
      world (var)
      entity_world (var)
      entity_id (uint)
      name (var)

   :returns: bool

.. das:function:: _world_set_parent(world : var, child_world : var, child_id : uint, parent_world : var, parent_id : uint, keep_world : bool, context : var, at : var) : bool

   :param:
      world (var)
      child_world (var)
      child_id (uint)
      parent_world (var)
      parent_id (uint)
      keep_world (bool)
      context (var)
      at (var)

   :returns: bool

.. das:function:: _world_get_parent(world : var, entity_world : var, entity_id : uint) : uint

   :param:
      world (var)
      entity_world (var)
      entity_id (uint)

   :returns: uint

.. das:function:: _world_get_child_count(world : var, entity_world : var, entity_id : uint) : uint

   :param:
      world (var)
      entity_world (var)
      entity_id (uint)

   :returns: uint

.. das:function:: _world_get_child(world : var, entity_world : var, entity_id : uint, index : uint) : uint

   :param:
      world (var)
      entity_world (var)
      entity_id (uint)
      index (uint)

   :returns: uint

.. das:function:: _world_change_scene(world : var, scene_name : var) : bool

   :param:
      world (var)
      scene_name (var)

   :returns: bool

.. das:function:: _world_entity_from_id(world : var, entity_id : uint) : uint

   :param:
      world (var)
      entity_id (uint)

   :returns: uint

.. das:function:: _world_entity_id(world : var, entity_world : var, entity_id : uint) : uint

   :param:
      world (var)
      entity_world (var)
      entity_id (uint)

   :returns: uint

.. das:function:: _world_component_type_id(world : var, type_name : var) : uint

   :param:
      world (var)
      type_name (var)

   :returns: uint

.. das:function:: _world_has_component(world : var, entity_world : var, entity_id : uint, type_id : uint) : bool

   :param:
      world (var)
      entity_world (var)
      entity_id (uint)
      type_id (uint)

   :returns: bool

.. das:function:: _world_has_animator(world : var, entity_world : var, entity_id : uint) : bool

   :param:
      world (var)
      entity_world (var)
      entity_id (uint)

   :returns: bool

.. das:function:: _world_add_component(world : var, entity_world : var, entity_id : uint, type_id : uint, context : var, at : var) : bool

   :param:
      world (var)
      entity_world (var)
      entity_id (uint)
      type_id (uint)
      context (var)
      at (var)

   :returns: bool

.. das:function:: _world_remove_component(world : var, entity_world : var, entity_id : uint, type_id : uint, context : var, at : var) : bool

   :param:
      world (var)
      entity_world (var)
      entity_id (uint)
      type_id (uint)
      context (var)
      at (var)

   :returns: bool

.. das:function:: _world_defer_destroy(world : var, entity_world : var, entity_id : uint) : bool

   :param:
      world (var)
      entity_world (var)
      entity_id (uint)

   :returns: bool

.. das:function:: _world_defer_add_component(world : var, entity_world : var, entity_id : uint, type_id : uint) : bool

   :param:
      world (var)
      entity_world (var)
      entity_id (uint)
      type_id (uint)

   :returns: bool

.. das:function:: _world_defer_remove_component(world : var, entity_world : var, entity_id : uint, type_id : uint) : bool

   :param:
      world (var)
      entity_world (var)
      entity_id (uint)
      type_id (uint)

   :returns: bool

.. das:function:: _world_singleton_type_id(world : var, type_name : var) : uint

   :param:
      world (var)
      type_name (var)

   :returns: uint

.. das:function:: _world_has_singleton(world : var, type_id : uint) : bool

   :param:
      world (var)
      type_id (uint)

   :returns: bool

.. das:function:: _world_add_singleton(world : var, type_id : uint) : bool

   :param:
      world (var)
      type_id (uint)

   :returns: bool

.. das:function:: _world_remove_singleton(world : var, type_id : uint) : bool

   :param:
      world (var)
      type_id (uint)

   :returns: bool

.. das:function:: _world_singleton_data(world : var, type_id : uint)

   :param:
      world (var)
      type_id (uint)

.. das:function:: _world_component_data(world : var, entity_world : var, entity_id : uint, type_id : uint)

   :param:
      world (var)
      entity_world (var)
      entity_id (uint)
      type_id (uint)

.. das:function:: _world_clone_entity(world : var, src_world : var, src_id : uint, parent_world : var, parent_id : uint, context : var, at : var) : uint

   :param:
      world (var)
      src_world (var)
      src_id (uint)
      parent_world (var)
      parent_id (uint)
      context (var)
      at (var)

   :returns: uint

.. das:function:: _world_destroy_entity_only(world : var, entity_world : var, entity_id : uint, context : var, at : var) : bool

   :param:
      world (var)
      entity_world (var)
      entity_id (uint)
      context (var)
      at (var)

   :returns: bool

.. das:function:: _world_entity_count(world : var) : uint

   :param:
      world (var)

   :returns: uint

.. das:function:: _world_find_all_count(world : var, name : var) : uint

   :param:
      world (var)
      name (var)

   :returns: uint

.. das:function:: _world_find_all_at(world : var, name : var, index : uint) : uint

   :param:
      world (var)
      name (var)
      index (uint)

   :returns: uint

.. das:function:: _world_copy_component(world : var, dst_world : var, dst_id : uint, src_world : var, src_id : uint, type_id : uint, context : var, at : var) : bool

   :param:
      world (var)
      dst_world (var)
      dst_id (uint)
      src_world (var)
      src_id (uint)
      type_id (uint)
      context (var)
      at (var)

   :returns: bool

.. das:function:: _world_defer_destroy_only(world : var, entity_world : var, entity_id : uint) : bool

   :param:
      world (var)
      entity_world (var)
      entity_id (uint)

   :returns: bool

.. das:function:: _world_defer_set_parent(world : var, child_world : var, child_id : uint, parent_world : var, parent_id : uint, keep_world : bool) : bool

   :param:
      world (var)
      child_world (var)
      child_id (uint)
      parent_world (var)
      parent_id (uint)
      keep_world (bool)

   :returns: bool

.. das:function:: _world_defer_create_entity(world : var, name : var) : uint

   :param:
      world (var)
      name (var)

   :returns: uint

.. das:function:: _time_fixed_elapsed() : float

   :returns: float

Scene system control
--------------------

.. das:function:: _enable_system(name : var) : bool

   :param:
      name (var)

   :returns: bool

.. das:function:: _disable_system(name : var) : bool

   :param:
      name (var)

   :returns: bool

.. das:function:: _system_enabled(element_id : var) : bool

   :param:
      element_id (var)

   :returns: bool

Editor viewport (global-state)
------------------------------

.. das:function:: refresh_editor_viewport()

.. das:function:: get_editor_img_min_x() : float

   :returns: float

.. das:function:: get_editor_img_min_y() : float

   :returns: float

.. das:function:: get_editor_img_size_x() : float

   :returns: float

.. das:function:: get_editor_img_size_y() : float

   :returns: float

Keyboard state
--------------

.. das:function:: is_key_pressed(action : int) : bool

   :param:
      action (int)

   :returns: bool

Named input actions
-------------------

.. das:function:: action_bind_key(action : var, scancode : int) : bool

   :param:
      action (var)
      scancode (int)

   :returns: bool

.. das:function:: action_bind_mouse(action : var, button : int) : bool

   :param:
      action (var)
      button (int)

   :returns: bool

.. das:function:: action_unbind(action : var) : bool

   :param:
      action (var)

   :returns: bool

.. das:function:: action_pressed(action : var) : bool

   :param:
      action (var)

   :returns: bool

.. das:function:: action_just_pressed(neg_action : var) : bool

   :param:
      neg_action (var)

   :returns: bool

.. das:function:: action_just_released(neg_action : var) : bool

   :param:
      neg_action (var)

   :returns: bool

.. das:function:: action_axis(neg_action : var, pos_action : var) : float

   :param:
      neg_action (var)
      pos_action (var)

   :returns: float

Camera field access
-------------------

.. das:function:: get_camera_fov(camera : uint) : float

   :param:
      camera (uint)

   :returns: float

.. das:function:: set_camera_fov(camera : uint, fov : float)

   :param:
      camera (uint)
      fov (float)

.. das:function:: get_camera_near(camera : uint) : float

   :param:
      camera (uint)

   :returns: float

.. das:function:: set_camera_near(camera : uint, near_val : float)

   :param:
      camera (uint)
      near_val (float)

.. das:function:: get_camera_far(camera : uint) : float

   :param:
      camera (uint)

   :returns: float

.. das:function:: set_camera_far(camera : uint, far_val : float)

   :param:
      camera (uint)
      far_val (float)

.. das:function:: get_camera_aspect_ratio(camera : uint) : float

   :param:
      camera (uint)

   :returns: float

.. das:function:: set_camera_aspect_ratio(camera : uint, aspect : float)

   :param:
      camera (uint)
      aspect (float)

Mouse pointer state
-------------------

.. das:function:: get_mouse_x() : float

   :returns: float

.. das:function:: get_mouse_y() : float

   :returns: float

.. das:function:: get_mouse_dx() : float

   :returns: float

.. das:function:: get_mouse_dy() : float

   :returns: float

.. das:function:: mouse_button_pressed(button : int) : bool

   :param:
      button (int)

   :returns: bool

Window size (global-state)
--------------------------

.. das:function:: refresh_window_size()

   Queries the OS for the current window size (must call before get_window_width/Height).

.. das:function:: get_window_width() : uint

   Returns the window width. Call ``refresh_window_size`` first.

   :returns: uint

.. das:function:: get_window_height() : uint

   Returns the window height. Call ``refresh_window_size`` first.

   :returns: uint

Generic component type lookup
-----------------------------

.. das:function:: _get_component_type_id_by_name(camera_entity : var) : uint

   :param:
      camera_entity (var)

   :returns: uint

Raycasting (global-state)
-------------------------

.. das:function:: make_pick_ray(camera_entity : uint, mouse_x : float, mouse_y : float, vp_x : float, vp_y : float, vp_w : float, vp_h : float) : bool

   Casts a ray from the camera through the given screen coordinates.

   :param:
      camera_entity (uint)
      mouse_x (float)
      mouse_y (float)
      vp_x (float)
      vp_y (float)
      vp_w (float)
      vp_h (float)

   :returns: bool

.. das:function:: get_ray_origin_x() : float

   Returns the X origin of the last pick ray.

   :returns: float

.. das:function:: get_ray_origin_y() : float

   Returns the Y origin of the last pick ray.

   :returns: float

.. das:function:: get_ray_origin_z() : float

   Returns the Z origin of the last pick ray.

   :returns: float

.. das:function:: get_ray_dir_x() : float

   Returns the X direction of the last pick ray.

   :returns: float

.. das:function:: get_ray_dir_y() : float

   Returns the Y direction of the last pick ray.

   :returns: float

.. das:function:: get_ray_dir_z() : float

   Returns the Z direction of the last pick ray.

   :returns: float

.. das:function:: ray_plane_intersect(origin_x : float, origin_y : float, origin_z : float, dir_x : float, dir_y : float, dir_z : float, plane_x : float, plane_y : float, plane_z : float, plane_nx : float, plane_ny : float, plane_nz : float) : bool

   Intersects the last pick ray with a plane. Returns true if hit.

   :param:
      origin_x (float)
      origin_y (float)
      origin_z (float)
      dir_x (float)
      dir_y (float)
      dir_z (float)
      plane_x (float)
      plane_y (float)
      plane_z (float)
      plane_nx (float)
      plane_ny (float)
      plane_nz (float)

   :returns: bool

.. das:function:: get_hit_x() : float

   Returns the X coordinate of the last ray-plane intersection.

   :returns: float

.. das:function:: get_hit_y() : float

   Returns the Y coordinate of the last ray-plane intersection.

   :returns: float

.. das:function:: get_hit_z() : float

   Returns the Z coordinate of the last ray-plane intersection.

   :returns: float

Time
----

Monotonic clock accessible from any system callback.

.. das:function:: get_time() : float

   Returns the elapsed time in seconds since the module was first queried.

   :returns: float

.. das:function:: get_elapsed_time() : float

   Returns the elapsed time in seconds since the module was first queried.

   :returns: float

.. das:function:: _time_fixed_delta() : float

   :returns: float

.. das:function:: _time_frame_index()

.. das:function:: _time_get_scale() : float

   :returns: float

.. das:function:: _time_set_scale(scale : float)

   :param:
      scale (float)

.. das:function:: _time_is_paused() : bool

   :returns: bool

.. das:function:: _time_set_paused(entity : bool)

   :param:
      entity (bool)

Physics: rigid body
-------------------

.. das:function:: apply_impulse(entity : uint, x : float, y : float, z : float)

   Applies an impulse to the rigid body.

   :param:
      entity (uint)
      x (float)
      y (float)
      z (float)

.. das:function:: apply_force(entity : uint, x : float, y : float, z : float)

   Applies a continuous force to the rigid body.

   :param:
      entity (uint)
      x (float)
      y (float)
      z (float)

Name-keyed event declaration / emission
---------------------------------------

.. das:function:: audio_play(entity : uint)

   Starts playback of the entity's audio component.

   :param:
      entity (uint)

.. das:function:: audio_stop(entity : uint)

   Stops playback of the entity's audio component.

   :param:
      entity (uint)

.. das:function:: audio_pause(entity : uint)

   Pauses playback of the entity's audio component.

   :param:
      entity (uint)

.. das:function:: audio_resume(entity : uint)

   Resumes playback of the entity's audio component.

   :param:
      entity (uint)

.. das:function:: audio_set_volume(entity : uint, volume : float)

   Sets the playback volume (0.0 to 1.0) of the entity's audio component.

   :param:
      entity (uint)
      volume (float)

.. das:function:: event_declare_source(event_name : var, system_name : var)

   :param:
      event_name (var)
      system_name (var)

.. das:function:: event_emit(event_name : var, payload : var)

   :param:
      event_name (var)
      payload (var)

.. das:function:: event_post(event_name : var, payload : var)

   :param:
      event_name (var)
      payload (var)

.. das:function:: event_declare_sink(event_name : var, system_name : var, handler_name : var)

   :param:
      event_name (var)
      system_name (var)
      handler_name (var)

.. das:function:: _event_declare_sink_current(event_name : var, handler_name : var)

   :param:
      event_name (var)
      handler_name (var)

Animation
---------

.. das:function:: audio_is_playing(entity : uint) : bool

   :param:
      entity (uint)

   :returns: bool

.. das:function:: anim_play(entity : uint, clip : var)

   :param:
      entity (uint)
      clip (var)

.. das:function:: anim_crossfade(entity : uint, clip : var, duration : float)

   :param:
      entity (uint)
      clip (var)
      duration (float)

.. das:function:: anim_stop(entity : uint)

   :param:
      entity (uint)

.. das:function:: anim_set_speed(entity : uint, speed : float)

   :param:
      entity (uint)
      speed (float)

.. das:function:: anim_set_time(entity : uint, time : float)

   :param:
      entity (uint)
      time (float)

.. das:function:: anim_set_loop(entity : uint, loop : bool)

   :param:
      entity (uint)
      loop (bool)

.. das:function:: anim_pause(entity : uint)

   :param:
      entity (uint)

.. das:function:: anim_resume(entity : uint)

   :param:
      entity (uint)

.. das:function:: anim_is_playing(entity : uint) : bool

   :param:
      entity (uint)

   :returns: bool

.. das:function:: anim_is_paused(entity : uint) : bool

   :param:
      entity (uint)

   :returns: bool

.. das:function:: anim_is_finished(entity : uint) : bool

   :param:
      entity (uint)

   :returns: bool

.. das:function:: anim_get_time(entity : uint) : float

   :param:
      entity (uint)

   :returns: float

.. das:function:: anim_get_duration(entity : uint) : float

   :param:
      entity (uint)

   :returns: float

.. das:function:: set_model(entity : uint, path : var)

   Sets the model on the entity from a resource path (e.g. "res://...").

   :param:
      entity (uint)
      path (var)

.. das:function:: set_model_material_override(entity : uint, path : var)

   Sets a per-instance material override from a resource path.

   :param:
      entity (uint)
      path (var)

.. das:function:: set_model_visibility_range(entity : uint, range : float)

   Sets the model max draw distance in world units (0 = unlimited).

   :param:
      entity (uint)
      range (float)

Systems: parallel execution hints
---------------------------------

.. das:function:: set_system_stage(stage : var)

   :param:
      stage (var)

.. das:function:: set_system_dependency(name : var)

   :param:
      name (var)

.. das:function:: set_system_conflict(event_name : var)

   :param:
      event_name (var)

Event runtime wiring
--------------------

.. das:function:: event_connect(event_name : var, system_name : var, handler_name : var)

   :param:
      event_name (var)
      system_name (var)
      handler_name (var)

.. das:function:: event_disconnect(event_name : var, system_name : var, handler_name : var)

   :param:
      event_name (var)
      system_name (var)
      handler_name (var)

.. das:function:: _event_connect_current(event_name : var, handler_name : var)

   :param:
      event_name (var)
      handler_name (var)

.. das:function:: _event_disconnect_current(event_name : var, handler_name : var)

   :param:
      event_name (var)
      handler_name (var)

