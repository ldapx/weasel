Projects
========

A Weasel project bundles scenes, resources, scripts and configuration into a
playable or shippable application.

A project is a directory described by a manifest, ``wslpro.json``. The
manifest records metadata (name, author), the resource folders
(``scenes_path``, ``models_path``, ``images_path``, ...) and the default scene
(``default_scene_path``). On top of that it owns the two groups of settings
that are *global to the application* rather than to a scene:

* **Window configuration** — how the game window is created.
* **Quality defaults** — the fallback values every viewport's
  ``use_global`` knobs follow.

Both are described below. Scene-level rendering settings, including the base
resolution and the root viewport configuration, deliberately live in the
scene's ``rendering_manager`` singleton instead — see
:doc:`manual_scene_and_prefabs_viewports`.

Window configuration
--------------------

.. list-table::
   :header-rows: 1

   * - Key
     - Meaning
   * - ``window.width`` / ``window.height``
     - Initial window size in pixels. Independent of the scene's base
       resolution: a 1600x900 window may render a 1280x720 base resolution.
   * - ``window.mode``
     - Initial window mode: ``windowed``, ``borderless_fullscreen``,
       ``fullscreen`` or ``minimized``.
   * - ``window.min_width`` / ``window.min_height``
     - Smallest size the player can resize the window to.
   * - ``window.max_width`` / ``window.max_height``
     - Largest size the player can resize the window to. ``0`` means
       unlimited.
   * - ``window.resizable``
     - Whether the player can resize the window at all.

The window settings apply to **standalone game builds only**. When a project
is opened in the editor, the host window's size, mode and resizability are
managed by the editor and are left untouched; the game is presented inside
the Game View panel instead. This keeps editor layouts stable when switching
between projects.

Quality defaults
----------------

.. list-table::
   :header-rows: 1

   * - Key
     - Meaning
   * - ``window.quality.filter``
     - Default scaling filter when a viewport texture is presented:
       ``linear`` or ``nearest``.
   * - ``window.quality.msaa``
     - Default antialiasing sample count: ``1`` (off), ``2`` or ``4``.
   * - ``window.quality.render_scale``
     - Default rendering-resolution multiplier (``1.0`` renders at the
       resolved size; lower values trade sharpness for performance).

Every viewport (root and subviewports) stores its own value plus a
``use_global`` flag for each of these knobs. While the flag is on, the value
stored here applies live; turning it off switches the viewport to its stored
override. Because the defaults are read at render time, editing them changes
every following viewport immediately.

Example manifest
----------------

.. code-block:: json

   {
     "name": "My Game",
     "author": "me",
     "root_path": "/home/me/my-game",
     "scenes_path": "rsc/scenes",
     "default_scene_path": "rsc/scenes/main.wscn",
     "window": {
       "width": 1600,
       "height": 900,
       "mode": "windowed",
       "min_width": 320,
       "min_height": 200,
       "max_width": 0,
       "max_height": 0,
       "resizable": true,
       "quality": {
         "filter": "linear",
         "msaa": 1,
         "render_scale": 1.0
       }
     }
   }

.. note::

   Fresh projects are created with MSAA off (``1``). Set ``window.quality.msaa``
   to ``4`` (or per-viewport overrides) when you want antialiasing.

Anchoring a viewport to the window
----------------------------------

Window settings describe the *host* window. To make scene content follow the
window resolution, use the root viewport's scaling mode and aspect ratio
policy instead of hard-coding sizes: ``Viewport Scaling`` renders at the base
resolution and presents scaled to the window, while the aspect policy decides
whether the extra space becomes letterbox bars or more visible world. See the
:doc:`viewport documentation <manual_scene_and_prefabs_viewports>` for the full
resolution chain.
