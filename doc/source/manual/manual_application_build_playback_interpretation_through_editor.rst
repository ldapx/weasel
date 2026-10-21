Interpretation Through Editor
=============================

In the editor, a Weasel project is **interpreted live**. The editor reads the
``wslpro.json`` manifest, loads the default scene, and starts the daScript
runtime (``das_engine``) which compiles the project's ``.das`` systems on the
fly using the engine's own module environment (``Module_WeaselApi`` underneath
the ``weasel`` gameplay facade). This means ``require weasel`` resolves to the
real gameplay surface rather than stubs, and the systems run
exactly as they will when compiled.

Because there is no separate build step, iteration is immediate:

- Saving a ``.das`` file (or editing a component in the **Inspector**) is
  picked up by the running world, so you see the result without recompiling.
- The **Entities and Singletons** panel and the **Systems** panel reflect the
  live state of the simulation, letting you inspect and tweak while the
  application plays.
- You can pause, step and reload systems from within the editor.

This interpreted workflow is the recommended way to prototype and build a
Weasel application. When you are ready to distribute it, compile the same
project ahead of time -- see :doc:`AOT Compilation Through CMake
<manual_application_build_playback_aot_compilation_through_cmake>`.
