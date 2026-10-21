Subscription Events
===================

Subscription events are Weasel's explicit, connection-tracked eventing model
(formerly the *signal* system). An emitter system **declares** an event source;
handler systems **declare** named sinks; and connections are **wired at runtime**
between a source and a sink of the same event type. When the source emits, the
event is dispatched immediately to every connected sink.

Key properties:

* An event source is owned by exactly **one emitter system** -- there is no
  component relation and no entity scoping.
* A sink is a named handler (``handler_name``) owned by a handler system.
* Connections are identified by ``(event_type_id, system_type_id, handler_name)``
  only. Any entity identity a handler needs is a **field of the event payload
  itself**, not a property of the connection.
* Connections are centralized in the :cpp:any:`wsl::event::event_hub` and
  serialized into scenes, so wired connections persist across saves.

Daslang: subscribing from gameplay code
-----------------------------------------

From Daslang, a system subscribes one of its methods and gets back a
disposable handle. Event types are passed as strings because Daslang
cannot name C++ types directly:

.. code-block:: das

   class System : GameSystem {
       sub : Subscription = Subscription()

       def override on_start(var ctx : SystemContext) : void {
           sub = subscribe_event("PlayerHit", "on_player_hit")
       }

       def on_player_hit(hit : PlayerHit) : void {
           // ...
       }

       def override on_stop(var ctx : SystemContext) : void {
           sub.dispose()
       }
   }

   // equivalent one-shot wiring without a handle (call once, e.g.
   // from a setup routine, not every frame):
   def wire_hit_once() : void {
       var c = event_connect("PlayerHit", "Damage System", "on_player_hit")
       assert(c)
       var d = event_disconnect("PlayerHit", "Damage System", "on_player_hit")
       assert(d)
   }

The emitted payload is just a Daslang struct whose layout matches the C++
event struct, so a connected handler can read entity ids, damage, and
flags directly (convert raw ids with ``entity_from_id`` before keeping
them). Every other operation -- declaring sources and sinks, emitting,
and queued delivery -- is shown below.

Defining the event payload
--------------------------

An event is a plain, trivially-copyable struct. Put any entity identity in the
payload. Payload types can originate on **either side**, and each side mirrors
the other's layout:

.. code-block:: das

    // Daslang: script-defined or mirror of the C++ struct below
    struct PlayerHit {
        attacker : uint   // source identity in the payload (entt::entity as int)
        victim : uint     // target identity in the payload
        damage : float
    }

.. code-block:: cpp

   // C++: the canonical declaration mirrored above
   struct PlayerHit {
     entt::entity attacker;   // source identity in the payload
     entt::entity victim;     // target identity in the payload
     float        damage;
   };

* A C++ system declares its payload struct and declares the source through its
  registration hook; Daslang mirrors the layout to emit or handle it.
* A Daslang-only event has no C++ counterpart at all. It is registered
  automatically on first use -- ``event_emit`` under an unknown name creates it
  and latches the payload layout, and ``event_declare_source`` /
  ``event_declare_sink`` do the same while also recording ownership. Declaring
  a source additionally gives the event editor/debug visibility and an owner
  system.

.. note:: The two structs must match exactly (same field order, types and
   sizes). ``event_emit`` validates the size against the registered type and
   rejects mismatched payloads; handlers receive the raw bytes reinterpreted
   through their declared parameter struct.

Declaring the source -- emitter
-------------------------------

Each system declares the events it owns. From Daslang, most gameplay
events need no declaration at all: the first ``event_emit`` (or
``event_post``) under an unknown name latches the payload layout and
registers it as a script-defined event. Declare a source explicitly only
when you need hub tooling to list it with an owner:

.. code-block:: das

   class System : GameSystem {
       def override on_start(var ctx : SystemContext) : void {
           // ownership recorded; payload still latched on first emit
           var declared = event_declare_source("PlayerHit", "Combat System")
           assert(declared)
       }
   }

The call returns ``false`` when the system name is unknown. Script
declarations survive scene reloads and play-mode toggles: they are recorded on
the script system and replayed through its registration hook whenever the hub
re-registers declarations.

In C++, the emitter declares the source from its registration hook:

.. code-block:: cpp

   void CombatSystem::register_event_sources(wsl::event::event_hub &hub) {
     hub.declare_event_source<PlayerHit, CombatSystem>();
   }

Declaring a sink -- handler
---------------------------

A sink is a named handler owned by a handler system.

From Daslang, subscribe one of the calling system's methods; you get a
disposable handle. Declaring the handler separately and wiring it
manually is the same thing spelled out:

.. code-block:: das

   class System : GameSystem {
       sub : Subscription = Subscription()
       m_total_damage : float = 0.0

       def override on_start(var ctx : SystemContext) : void {
           // handler method: one argument matching the event payload
           sub = subscribe_event("PlayerHit", "on_player_hit")
           var subscribed = sub.is_valid()
           assert(subscribed)
       }

       def on_player_hit(ev : PlayerHit) : void {
           m_total_damage += ev.damage
       }

       def override on_stop(var ctx : SystemContext) : void {
           sub.dispose()
       }
   }

The handler is resolved by method name at dispatch time, so it survives script
reloads; the payload struct must match the event layout exactly
(mismatches are logged and the handler is skipped).

In C++, the handler is a lambda with the signature
``void(void *owner, entt::registry &, const void *)`` that downcasts the owner
and the event payload:

.. code-block:: cpp

   void DamageSystem::register_event_sinks(wsl::event::event_hub &hub) {
     hub.declare_event_sink<PlayerHit, DamageSystem>(
         "on_player_hit",
         +[] (void *owner, entt::registry &reg, const void *sig) {
           const auto &e = *static_cast<const PlayerHit *>(sig);
           static_cast<DamageSystem &>(*static_cast<sys::ecs_system *>(owner))
               .on_player_hit(reg, e);
         });
   }

   void DamageSystem::on_player_hit(entt::registry &reg, const PlayerHit &e) {
     auto &hp = reg.get<health>(e.victim);
     hp.value -= e.damage;   // attacker/victim read straight from the payload
   }

Once both ends are declared, wire them with ``event_connect`` (shown above).

Emitting
--------

From Daslang, fill a struct mirroring the payload and emit it by event name.
Entity identities travel as plain integers (convert them with
``entity_from_id`` before keeping them):

.. code-block:: das

   struct Ping {
       seq : int = 0
   }

   class System : GameSystem {
       frame : int = 0

       def override update(var ctx : SystemContext; dt : float) : void {
           frame = frame + 1
           if (frame == 1) {
               var hit : PlayerHit
               hit.attacker = 1u
               hit.victim = 2u
               hit.damage = 10.0
               event_emit("PlayerHit", hit)
               var first = Ping()
               first.seq = 1
               event_post("Ping", first)
           }
       }
   }

``event_emit`` dispatches immediately to every connected sink. The payload must
be a structure; the first emit under an unknown event name registers it as a
script-defined type and latches the struct layout, and later emits validate
against it (mismatches are logged and dropped). ``event_post`` validates
identically but queues the payload and returns; it is delivered at the next
pump, in posting order. Prefer ``post`` when the receiver must not run
re-entrantly (notably inside another handler or a query).

In C++, construct a handle from the hub and emit. The handle is a non-owning
view; the source must already be declared. Entity identity travels in the
payload, never in the connection:

.. code-block:: cpp

   void CombatSystem::on_update(entt::registry &reg, double dt) {
     if (m_hit_landed) {
       wsl::event::event_source<PlayerHit, CombatSystem>{hub}
           .emit(m_attacker, m_victim, m_damage);
       // equivalently:
       wsl::event::emit(hub, PlayerHit{m_attacker, m_victim, m_damage});
     }
   }

Connecting and disconnecting at runtime
---------------------------------------

From Daslang, prefer the ``Subscription`` returned by ``subscribe_event``
(``dispose()`` disconnects it). Declared ends can also be wired by name:

.. code-block:: das

   // same effect as sub = subscribe_event("PlayerHit", "on_player_hit");
   // call once, not per frame:
   def wire_hit_once() : void {
       var c = event_connect("PlayerHit", "Damage System", "on_player_hit")
       assert(c)
       var d = event_disconnect("PlayerHit", "Damage System", "on_player_hit")
       assert(d)
   }

In C++, after both sides are declared, use the typed handle API (no entity
arguments):

.. code-block:: cpp

   wsl::event::event_source<PlayerHit, CombatSystem> src{hub};
   wsl::event::event_sink<PlayerHit, DamageSystem>   sink{hub, "on_player_hit"};

   src.add_listener(sink);        // connect
   src.remove_listener(sink);     // disconnect

You can also drive the hub directly with type ids:

.. code-block:: cpp

   hub.connect(event_type_id, system_type_id, "on_player_hit");
   hub.disconnect(event_type_id, system_type_id, "on_player_hit");
