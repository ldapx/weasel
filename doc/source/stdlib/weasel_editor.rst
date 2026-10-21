weasel_editor
=============

.. das:module:: weasel_editor

Editor-only helpers
---------------------

These read editor UI state through the editor context and are no-ops
outside an editor session (headless or standalone runs report zeros).
Gameplay scripts must never require this module.

.. das:function:: editor_refresh_viewport() : void

.. das:function:: editor_viewport_min() : float2

.. das:function:: editor_viewport_size() : float2

