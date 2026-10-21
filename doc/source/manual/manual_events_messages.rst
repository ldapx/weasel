Messages
========

Messages are Weasel's buffered, **pull-only** event model. Unlike subscription
events, there is **no wiring and no broadcast**: a producer ``post``\ s a typed
message into a per-frame queue, and any system that cares about that type holds a
``message_reader<T>`` and pulls the messages during its own update.

This keeps the cost proportional to *(systems × their own message types)* rather
than *(systems × all messages)*, and avoids double-delivery and opt-out ambiguity.
It is the model to use for global, fire-and-forget, decoupled notifications -- input,
UI and game-state changes.

Daslang: reading messages
-------------------------

From Daslang, the engine's built-in input messages are exposed through a pull
API that reads the active runtime context's published buffer. There is no
``on_event`` hook -- iterate the per-frame accessors inside your system's
``update``:

.. code-block:: das

   class System : GameSystem {
       def override update(var ctx : SystemContext; dt : float) : void {
           for (i in range(mouse_motion_count())) {
               ctx.log.debug("mouse moved")
           }
       }
   }

The available pull accessors are ``mouse_motion_count`` / ``mouse_motion_at(i)``,
``mouse_button_count`` / ``mouse_button_at(i)``, ``mouse_wheel_count`` /
``mouse_wheel_at(i)``, ``key_pressed_count`` / ``key_pressed_at(i)``,
``key_released_count`` / ``key_released_at(i)``, ``text_input_count`` /
``text_input_at(i)`` / ``text_input_text(i)``, ``window_resized_count`` /
``window_resized_at(i)`` and ``quit_requested_count`` / ``quit_requested_at(i)``.
The typed payloads are ``MouseMotion``, ``MouseButton``, ``MouseWheel``,
``KeyPressed``, ``KeyReleased``, ``TextInput``, ``WindowResized`` and
``QuitRequested``.

For any *other* message type -- engine or user-defined -- the name-keyed API
works uniformly: ``message_post``, ``message_count`` and
``for_each_message`` address messages by string name.

Defining a message
------------------

A message is a plain Weasel data struct. No registration is required. Message
types can originate on **either side**, and each side mirrors the other's
layout:

.. code-block:: das

    // Daslang: script-defined or mirror of the C++ struct below
    struct PlayerDied {
        who : uint    // entt::entity as int
        cause : int
    }

.. code-block:: cpp

   // C++: the canonical declaration mirrored above
   struct PlayerDied {
     entt::entity who;
     int          cause;
   };

The engine's own input messages are posted by the SDL poll boundary and pulled
from Daslang via the accessors above. Posting a Daslang struct under a new
name registers that layout on first use, so purely script-defined message
types need no C++ counterpart.

Posting
-------

From Daslang, post by message name with a struct mirroring the payload:

.. code-block:: das

    class System : GameSystem {
        def override update(var ctx : SystemContext; dt : float) : void {
            var msg : PlayerDied
            msg.who = 7u
            msg.cause = 0
            message_post("PlayerDied", msg)
        }
    }

Posting under an unknown name registers it on first use (name-hashed id plus
the posted struct's size), so purely script-defined message types work without
any C++ counterpart. The post is rejected when the struct's size does not match
the registered layout.

In C++, post through the :cpp:any:`wsl::event::message_bus` -- a singleton on
the active :cpp:any:`wsl::comp::runtime_context`, reachable from anywhere (a
system update, an async task, etc.). Posting is thread-safe:

.. code-block:: cpp

   void combat_system::on_update(entt::registry &reg, double dt) {
     if (m_just_died) {
       m_runtime_ctx->message_bus().post<PlayerDied>({m_player, 0});
     }
   }

Pulling with a reader
---------------------

From Daslang, pull by name with ``message_count`` and ``for_each_message``,
which mirrors the built-in accessors but works for every registered message
type:

.. code-block:: das

    class System : GameSystem {
        def override update(var ctx : SystemContext; dt : float) : void {
            // for_each_message drives the block with each matching index;
            // message_at copies that message out (never alias the buffer).
            for_each_message("PlayerDied") $(i : uint) {
                var d = message_at("PlayerDied", i, PlayerDied())
                ctx.log.info("player died: {d.who}")
            }
        }
    }

In C++, each consuming system holds one ``message_reader<T>`` per message type
and pulls it during its own update. The reader keeps its own cursor, so
multiple systems holding a reader for the same type each receive every message
exactly once:

.. code-block:: cpp

   void player_system::on_update(entt::registry &reg, double dt) {
     m_deaths.for_each([](const PlayerDied &d) {
       wsl::log::sys()->info("player died: {}", d.who);
     });
   }

   // m_deaths = m_runtime_ctx->message_bus().reader<PlayerDied>();

The engine's own input types (``key_pressed``, ``key_released``, ``text_input``,
``mouse_motion``, ``mouse_button``, ``mouse_wheel``, ``window_resized``,
``quit_requested``) are posted by the SDL poll boundary translator and pulled the
same way -- through the typed accessors shown above, or generically via
``for_each_message("key_pressed") ...``. A catch-all ``message_reader<message>``
is available for debug/logging systems that genuinely need every type.

Frame lifecycle
---------------

``message_bus::drain(registry)`` is called once per frame, right after the SDL poll
loop. It publishes the pending queue into the per-type reader buffers (and delivers
any ``subscribe`` callbacks), then clears the pending queue. Events posted *during*
a drain roll to the next frame, so there is no infinite loop and the queue cannot
grow unbounded. At each drain every reader cursor is reset to the start of the new
buffer, so a system reads exactly that frame's messages once.

.. code-block:: text

   while (SDL_PollEvent(&e))                  // drain OS queue
       translate_and_post(bus, engine_event{e});  // SDL -> engine type, post
   message_bus.drain(registry);               // publish pending -> reader buffers
       └─ each system pulls its message_reader<T> during its own update

Non-system consumers
--------------------

For consumers that are not systems (e.g. the application or runtime context
handling window/quit), use a ``subscribe`` callback instead of a reader.

From Daslang, subscribe a **no-capture script function** by message name.
Callbacks fire during the per-frame drain on the main thread:

.. code-block:: das

    // no-capture script function: the payload arrives as one struct
    def on_window_resized(ev : WindowResized) : void {
    }

    class System : GameSystem {
        def override on_start(var ctx : SystemContext) : void {
            message_subscribe("window_resized", @@on_window_resized)
            // later: message_unsubscribe("window_resized")
        }
    }

The handler receives the posted payload as a single struct argument whose
layout must match the message type. Subscriptions live until
``message_unsubscribe`` or until the subscribing script program's context is
destroyed (e.g. when the program is recompiled), at which point they are
dropped automatically.

In C++, use a typed or catch-all ``subscribe`` callback:

.. code-block:: cpp

   bus.subscribe<window_resized>([](const window_resized &e) {
     wsl::log::sys()->info("window resized to {}x{}", e.width, e.height);
   });
