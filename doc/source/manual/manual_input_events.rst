Events
======

React to discrete input events -- key presses, mouse buttons, the wheel,
mouse motion, text input and window changes.

Gameplay code should use **named actions**: bind physical inputs to names
once, then read held/edge/axis state every frame. The engine polls SDL
and publishes each input occurrence once per frame; actions read that
published state, so several systems can each observe the same event
independently. The same bus is visible from both daScript and C++ -- the
examples below show the daScript form first, followed by its C++ equivalent.

.. code-block:: das

   class System : GameSystem {
       def override on_start(var ctx : SystemContext) : void {
           ctx.input.bind_key("jump", 44)      // Space scancode
           ctx.input.bind_mouse("fire", 1)     // left button
       }

       def override update(var ctx : SystemContext; dt : float) : void {
           if (ctx.input.just_pressed("jump")) {
               // edge: fires once on press (repeats excluded)
           }
           if (ctx.input.pressed("fire")) {
               // held state
           }
           var move = ctx.input.value2("move_left", "move_right",
                                       "move_up", "move_down")
       }
   }

``just_pressed`` / ``just_released`` report this frame's edges (key
repeats never count as fresh presses); ``pressed`` is held state, and
``value`` / ``value2`` / ``value3`` compose signed axes (+1, 0, -1).
Read the consuming ``mouse_delta`` at most once per frame:

.. code-block:: das
   :caption: daScript

   class System : GameSystem {
       def override update(var ctx : SystemContext; dt : float) : void {
           var pos = ctx.input.mouse_position()
           var delta = ctx.input.mouse_delta()  // consumes the motion
           var left_down = ctx.input.mouse_button_down(1)
           if (!ctx.input.is_cursor_visible()) {
               ctx.input.set_cursor_visible(true)
           }
       }
   }

Raw pull accessors (escape hatch)
---------------------------------

The per-frame published messages behind actions are also readable
directly. Each event type exposes a ``*_count`` accessor and a
``*_at(index)`` accessor in daScript, and a typed ``message_reader`` in
C++. Prefer named actions for gameplay; use these for diagnostics and
specialized tooling. Note: holding a native message struct in a local
requires an ``unsafe`` block (it wraps native memory) -- read fields
straight off the accessor call instead.

The following SDL constants are available from daScript: ``SDL_BUTTON_LEFT``,
``SDL_BUTTON_RIGHT`` and ``SDL_SCANCODE_ESCAPE`` (identical values apply in
C++ through ``SDL3/SDL_mouse.h`` and ``SDL3/SDL_keyboard.h``).

Mouse button
------------

.. code-block:: das
   :caption: daScript

   class System : GameSystem {
       def override update(var ctx : SystemContext; dt : float) : void {
           for (i in range(mouse_button_count())) {
               if (mouse_button_at(i).down && mouse_button_at(i).button == SDL_BUTTON_LEFT) {
                   ctx.log.info("left click")
               }
           }
       }
   }

.. code-block:: cpp
   :caption: C++

   #include "wsl/comp/singl/runtime_context.hpp"
   #include "wsl/event/message_bus.hpp"
   #include "wsl/input.hpp"
   #include "wsl/log/log.hpp"

   void my_system::update(entt::registry &reg, float dt)
   {
       auto &rc = reg.ctx<wsl::comp::runtime_context>();
       auto reader = rc.message_bus().reader<wsl::input::mouse_button>();
       reader.for_each([&](const wsl::input::mouse_button &e) {
           if (e.down && e.button == SDL_BUTTON_LEFT) {
               wsl::log::sys()->info("left click at {}, {}", e.x, e.y);
           }
       });
   }

Keyboard
--------

.. code-block:: das
   :caption: daScript

   class System : GameSystem {
       def override update(var ctx : SystemContext; dt : float) : void {
           for (i in range(key_pressed_count())) {
               if (key_pressed_at(i).scancode == SDL_SCANCODE_ESCAPE) {
                   ctx.log.info("escape pressed")
               }
           }
       }
   }

.. code-block:: cpp
   :caption: C++

   #include "wsl/comp/singl/runtime_context.hpp"
   #include "wsl/event/message_bus.hpp"
   #include "wsl/input.hpp"
   #include "wsl/log/log.hpp"

   void my_system::update(entt::registry &reg, float dt)
   {
       auto &rc = reg.ctx<wsl::comp::runtime_context>();
       auto reader = rc.message_bus().reader<wsl::input::key_pressed>();
       reader.for_each([&](const wsl::input::key_pressed &e) {
           if (e.scancode == SDL_SCANCODE_ESCAPE) {
               wsl::log::sys()->info("escape pressed (repeat={})", e.repeat);
           }
       });
   }

Mouse motion
------------

.. code-block:: das
   :caption: daScript

   class System : GameSystem {
       def override update(var ctx : SystemContext; dt : float) : void {
           for (i in range(mouse_motion_count())) {
               ctx.log.debug("mouse moved")
           }
       }
   }

.. code-block:: cpp
   :caption: C++

   #include "wsl/comp/singl/runtime_context.hpp"
   #include "wsl/event/message_bus.hpp"
   #include "wsl/input.hpp"
   #include "wsl/log/log.hpp"

   void my_system::update(entt::registry &reg, float dt)
   {
       auto &rc = reg.ctx<wsl::comp::runtime_context>();
       auto reader = rc.message_bus().reader<wsl::input::mouse_motion>();
       reader.for_each([&](const wsl::input::mouse_motion &e) {
           wsl::log::sys()->debug("mouse at {},{} delta {},{}",
                                  e.x, e.y, e.xrel, e.yrel);
       });
   }

Mouse wheel
-----------

.. code-block:: das
   :caption: daScript

   class System : GameSystem {
       def override update(var ctx : SystemContext; dt : float) : void {
           for (i in range(mouse_wheel_count())) {
               ctx.log.info("wheel moved")
           }
       }
   }

.. code-block:: cpp
   :caption: C++

   #include "wsl/comp/singl/runtime_context.hpp"
   #include "wsl/event/message_bus.hpp"
   #include "wsl/input.hpp"
   #include "wsl/log/log.hpp"

   void my_system::update(entt::registry &reg, float dt)
   {
       auto &rc = reg.ctx<wsl::comp::runtime_context>();
       auto reader = rc.message_bus().reader<wsl::input::mouse_wheel>();
       reader.for_each([&](const wsl::input::mouse_wheel &e) {
           wsl::log::sys()->info("wheel {},{} flipped={}", e.x, e.y, e.flipped);
       });
   }

Text input
----------

.. code-block:: das
   :caption: daScript

   class System : GameSystem {
       def override update(var ctx : SystemContext; dt : float) : void {
           for (i in range(text_input_count())) {
               ctx.log.info("typed text")
               text_input_text(i)
           }
       }
   }

.. code-block:: cpp
   :caption: C++

   #include "wsl/comp/singl/runtime_context.hpp"
   #include "wsl/event/message_bus.hpp"
   #include "wsl/input.hpp"
   #include "wsl/log/log.hpp"

   void my_system::update(entt::registry &reg, float dt)
   {
       auto &rc = reg.ctx<wsl::comp::runtime_context>();
       auto reader = rc.message_bus().reader<wsl::input::text_input>();
       reader.for_each([&](const wsl::input::text_input &e) {
           wsl::log::sys()->info("typed: {}", e.text);
       });
   }

Window resized
--------------

.. code-block:: das
   :caption: daScript

   class System : GameSystem {
       def override update(var ctx : SystemContext; dt : float) : void {
           for (i in range(window_resized_count())) {
               ctx.log.info("window resized")
           }
       }
   }

.. code-block:: cpp
   :caption: C++

   #include "wsl/comp/singl/runtime_context.hpp"
   #include "wsl/event/message_bus.hpp"
   #include "wsl/input.hpp"
   #include "wsl/log/log.hpp"

   void my_system::update(entt::registry &reg, float dt)
   {
       auto &rc = reg.ctx<wsl::comp::runtime_context>();
       auto reader = rc.message_bus().reader<wsl::input::window_resized>();
       reader.for_each([&](const wsl::input::window_resized &e) {
           wsl::log::sys()->info("window now {}x{}", e.width, e.height);
       });
   }
