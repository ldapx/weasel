User-Defined Systems
====================

Game systems are written in daScript. Declare ``class System :
GameSystem`` in a ``.das`` file under your project's ``src/systems``
directory and override the lifecycle hooks you need. The engine
instantiates the class literally named ``System``:

.. code-block:: das

   options gen2
   module my_system

   require weasel

   class System : GameSystem {
       def override on_start(var ctx : SystemContext) : void {
           ctx.input.bind_key("jump", 44)
       }

       def override update(var ctx : SystemContext; dt : float) : void {
           if (ctx.input.just_pressed("jump")) {
               query(ctx.world) $(t : Transform&) {
                   t.position.y += 1.0
               }
           }
       }

       def override on_stop(var ctx : SystemContext) : void {
       }
   }

Lifecycle hooks take a mutable ``var ctx`` so service methods
(``ctx.input.*``, ``ctx.log.*``) bind; ``fixed_update`` and
``late_update`` use the same shape. See the migration guide's lifecycle
and das-notes sections for the rules that bite (``var ctx``, no
same-named base, hoisted asserts, and why ``class System : System``
does not work).
