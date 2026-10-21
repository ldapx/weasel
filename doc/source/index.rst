Weasel Engine Documentation
===========================

.. toctree::
   :maxdepth: 2
   :numbered:

   manual/index
   tutorials/index
   stdlib/index
   cpp/index

The Weasel engine exposes its API to daScript through one gameplay module:

- **weasel** -- the gameplay facade: world-scoped entities, the
  ``GameSystem`` base class, typed component access, queries, input,
  time, events, and engine services. (``weasel_editor`` holds
  editor-only helpers; ``weasel_api`` is the underlying native module,
  reached only through the facade.)

Quick Example
-------------

.. code-block:: das

    require weasel

    class System : GameSystem {
        def override on_start(var ctx : SystemContext) : void {
            var e = ctx.world.create_entity()
            ctx.world.set_name(e, "Player")
            add(ctx.world, e, Transform())
        }

        def override update(var ctx : SystemContext; dt : float) : void {
            query(ctx.world) $(t : Transform&) {
                t.position.y += 1.0 * dt
            }
        }
    }
