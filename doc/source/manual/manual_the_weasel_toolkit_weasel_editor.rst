Weasel Editor
=============

The Weasel Editor is the integrated environment for scene authoring,
inspection and play/test iteration.

Two editor front-ends share the engine runtime, the editor session state and
the attach server used by ``weasel-cli -a`` and the MCP server:

* ``weasel`` — the current ImGui editor.
* ``weasel-rmlui`` — the successor editor with an RML/CSS (HTML-like) UI.

They expose the same editing model (entity hierarchy, inspector, Game View,
play/pause/stop), so the workflow described here applies to both.

The Game View panel
-------------------

The Game View renders the active scene exactly as the game will present it —
same viewports, same scaling, same post-processing — and is where you test
viewport setups without leaving the editor.

Its toolbar has two selectors:

**Viewport selector**
  ``Root Viewport`` renders the whole scene through the root viewport. Picking
  a subviewport previews *that* viewport's world and framing. The selector is
  disabled while the game is running.

**Camera selector**
  Chooses which camera frames the Game View while stopped:

  ========================== =============================================
  Default Editor Camera      The editor's own camera. Its mode (2D or 3D)
                             follows the selected viewport's contents.
  Editor 3D Camera           Force the 3D editor camera.
  Editor 2D Camera           Force the 2D editor camera.
  Default Runtime Camera     The camera the game would use (the subviewport's
                             ``Camera`` picker, or the scene camera for the
                             root).
  *Entity camera*            Any camera entity in the scene.
  ========================== =============================================

Play mode
---------

While **stopped**, the editor selection always wins for the Game View — it is
a preview tool, not the runtime. Press **Play** and the runtime selection
takes over:

* subviewports use their own ``Camera`` pickers,
* the root viewport uses the scene camera,
* the Game View toolbar selectors become read-only.

**Pause** freezes the running scene, **Stop** ends the session and restores
the editor selection. This is the behaviour behind the classic minimap
workflow: select the minimap subviewport in the toolbar while stopped to see
the editor camera framed inside it, then press Play to see the minimap's own
camera.

The Game View also hosts the transform gizmos (translate / rotate / scale),
the view gizmo and entity picking while stopped.

Window versus Game View
-----------------------

The editor host window is managed by the editor; the project's window
configuration (initial size, window mode, resizability) applies to standalone
game builds only. Inside the editor the game is always presented in the Game
View panel, whose size stands in for the window. See
:doc:`manual_projects` for the window settings and
:doc:`manual_scene_and_prefabs_viewports` for how the root viewport adapts
scene content to whatever output area it is given.
