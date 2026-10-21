Logging
=======

Weasel provides structured logging through an `spdlog <https://github.com/gabime/spdlog>`_-based
logger. Every message carries a severity level, the name of the subsystem that
emitted it, and a timestamp, and is fanned out to multiple *sinks* at once:
a colored console sink and a Tracy sink that forwards log lines into the
Tracy profiler (see :doc:`Tracy Quick Start Guide
<manual_profiling_tracy_quick_start_guide>`).

Subsystem loggers
-----------------

The engine exposes one named logger per major subsystem. In C++ you obtain them
through the ``wsl::log`` namespace:

- ``wsl::log::core()`` -- core engine and ECS
- ``wsl::log::gfx()`` -- rendering / GPU
- ``wsl::log::rsc()`` -- resources and scene loading
- ``wsl::log::sys()`` -- systems
- ``wsl::log::editor()`` -- the Weasel Editor
- ``wsl::log::cli()`` -- the command-line interface
- ``wsl::log::phys()`` -- physics
- ``wsl::log::net()`` -- networking
- ``wsl::log::cmake()`` -- build / CMake integration

Each returns a standard ``std::shared_ptr<spdlog::logger>``, so you use the
usual spdlog API on it.

Log levels
----------

Messages are filtered by severity. Weasel uses spdlog's levels, in increasing
order of severity:

``trace < debug < info < warn < error < critical``

The engine initializes the global level to ``debug``, so ``trace`` messages are
suppressed by default while ``debug`` and above are shown. Logging is
initialized once at startup via ``wsl::log::init()``.

Output format
-------------

Console lines follow the pattern::

   [2026-08-19 12:34:56.789] [gfx] [info] vkCreateBuffer succeeded

i.e. a timestamp, the ``[logger name]``, the ``[level]``, and the message. In
the editor the same stream is captured by the **Console** panel, and (when
Tracy is enabled) the line is also visible inside the Tracy profiler view.

Logging from C++
----------------

Use the subsystem logger that matches your code, with spdlog's ``{}``
positional formatting:

.. code-block:: cpp

   #include "wsl/log/log.hpp"

   wsl::log::sys ()->info ("player spawned at {}", position);
   wsl::log::gfx ()->warn ("frame took {} ms", dt * 1000.0);
   wsl::log::phys ()->error ("body {} fell out of world", body_id);

Logging from daslang
--------------------

In daScript (for example inside a system) use ``ctx.log``. The ``Logger``
methods route to the **sys** logger:

- ``debug(message)`` / ``info(message)`` / ``warn(message)`` /
  ``error(message)`` / ``trace(message)``
- ``debug_fields(message, fields)`` (and ``info_`` / ``warn_`` /
  ``error_`` / ``trace_`` variants) for structured fields

.. code-block:: das

   class System : GameSystem {
       def override on_start(var ctx : SystemContext) : void {
           ctx.log.info("My system initialized!")
       }

       def override update(var ctx : SystemContext; dt : float) : void {
           var fields = LogFields()
           fields.add("dt", "{dt}")
           ctx.log.debug_fields("frame", fields)
       }
   }

``LogFields`` is an ordered key/value map, so ``warn_fields("slow frame",
fields)`` renders deterministically as ``slow frame k1=v1 k2=v2``.
There is no debug drawing or profiling scope API yet; see the
:doc:`Tracy Quick Start Guide <manual_profiling_tracy_quick_start_guide>`
for CPU-side profiling.

These are the same routines used throughout the ECS Programming examples.
