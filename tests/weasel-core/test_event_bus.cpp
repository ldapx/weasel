#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "wsl/event/event_hub.hpp"
#include "wsl/event/message_bus.hpp"
#include "wsl/event/message_event.hpp"
#include "wsl/log/log.hpp"
#include "wsl/sys/system.hpp"

#include <entt/entt.hpp>

#include <cstdint>
#include <string>
#include <thread>
#include <vector>

using namespace wsl;

// Global log initializer (idempotent, safe to call multiple times).
// Several engine paths (e.g. event_hub::connect) log through
// wsl::log::* unconditionally; without init() those accessors return
// empty shared_ptr s and the dereference segfaults. Every other test
// target bootstraps logging the same way (see weasel-cli tests).
namespace
{
struct log_initializer
{
  log_initializer () { wsl::log::init (); }
};
static log_initializer init_log;
} // namespace

// A trivially-copyable payload used by both message-bus and observer-event
// tests.
struct combat_event
{
  entt::entity attacker{};
  entt::entity victim{};
  int damage = 0;
};
static_assert (event::message_event<combat_event>,
               "combat_event must satisfy message_event");

// ----------------------------------------------------------------------------
// Message bus
// ----------------------------------------------------------------------------

TEST_CASE ("message_bus: post -> drain -> reader round-trip")
{
  event::message_bus bus;
  entt::registry reg;

  bus.post<combat_event> ({ entt::entity{ 1 }, entt::entity{ 2 }, 10 });
  bus.post<combat_event> ({ entt::entity{ 3 }, entt::entity{ 4 }, 20 });

  bus.drain (reg);

  auto reader = bus.reader<combat_event> ();
  std::vector<combat_event> seen;
  reader.for_each ([&] (const combat_event &e) { seen.push_back (e); });

  REQUIRE (seen.size () == 2);
  REQUIRE (seen[0].damage == 10);
  REQUIRE (seen[1].damage == 20);
}

TEST_CASE ("message_bus: each reader gets every message exactly once")
{
  event::message_bus bus;
  entt::registry reg;

  bus.post<combat_event> ({ {}, {}, 7 });

  bus.drain (reg);

  auto a = bus.reader<combat_event> ();
  auto b = bus.reader<combat_event> ();

  int a_count = 0;
  int b_count = 0;
  a.for_each ([&] (const combat_event &) { ++a_count; });
  b.for_each ([&] (const combat_event &) { ++b_count; });

  REQUIRE (a_count == 1);
  REQUIRE (b_count == 1);

  // A second for_each in the same frame must not re-deliver (cursor advanced).
  int a_again = 0;
  a.for_each ([&] (const combat_event &) { ++a_again; });
  REQUIRE (a_again == 0);
}

TEST_CASE ("message_bus: no recursive drain (events posted during drain roll "
           "to next frame)")
{
  event::message_bus bus;
  entt::registry reg;

  bus.post<combat_event> ({ {}, {}, 1 });

  // A subscribe callback that posts during drain should NOT be delivered this
  // frame; it must roll to the next frame.
  bus.subscribe<combat_event> (
      [&] (const combat_event &) { bus.post<combat_event> ({ {}, {}, 999 }); });

  bus.drain (reg);

  auto reader = bus.reader<combat_event> ();
  int first_frame = 0;
  reader.for_each ([&] (const combat_event &e) { first_frame += e.damage; });
  REQUIRE (first_frame == 1);

  bus.drain (reg);
  int second_frame = 0;
  reader.for_each ([&] (const combat_event &e) { second_frame += e.damage; });
  REQUIRE (second_frame == 999);
}

TEST_CASE ("message_bus: catch-all reader<message> sees every type")
{
  event::message_bus bus;
  entt::registry reg;

  struct other_event
  {
    int value = 0;
  };
  static_assert (event::message_event<other_event>);

  bus.post<combat_event> ({ {}, {}, 5 });
  bus.post<other_event> ({ 42 });

  bus.drain (reg);

  auto all = bus.reader<event::message> ();
  int count = 0;
  all.for_each ([&] (const event::message &) { ++count; });
  REQUIRE (count == 2);
}

// ----------------------------------------------------------------------------
// Observer event hub: connect / disconnect / dispatch
// ----------------------------------------------------------------------------

namespace
{
struct test_emitter : sys::ecs_system_t<test_emitter>
{
  test_emitter () : ecs_system_t ("test_emitter") {}
  void
  register_event_sources (event::event_hub &hub) override
  {
    hub.declare_event_source<combat_event, test_emitter> ();
  }
};

struct test_handler : sys::ecs_system_t<test_handler>
{
  test_handler () : ecs_system_t ("test_handler") {}
  int hits = 0;
  entt::entity last_victim{};
  void
  register_event_sinks (event::event_hub &hub) override
  {
    hub.declare_event_sink<combat_event, test_handler> (
        "on_hit", +[] (void *sys, entt::registry &, const void *sig) {
          auto &self = static_cast<test_handler &> (
              *static_cast<sys::ecs_system *> (sys));
          const auto &e = *static_cast<const combat_event *> (sig);
          ++self.hits;
          self.last_victim = e.victim;
        });
  }
};
} // namespace

TEST_CASE ("event_hub: declare source needs only the emitter (no component "
           "relation)")
{
  event::event_hub hub;
  event::event_debug_db db;
  hub.db = &db;

  test_emitter emitter;
  emitter.register_event_sources (hub);

  REQUIRE (hub.has_event_source (comp::stable_type_id<combat_event> ()));
}

TEST_CASE ("event_hub: connect / disconnect / dispatch at runtime")
{
  event::event_hub hub;
  event::event_debug_db db;
  hub.db = &db;

  entt::registry reg;
  test_emitter emitter;
  test_handler handler;

  emitter.register_event_sources (hub);
  handler.register_event_sinks (hub);

  hub.resolve_active_registry = [&] () -> entt::registry * { return &reg; };
  hub.resolve_system_by_type = [&] (entt::id_type id) -> sys::ecs_system * {
    if (id == comp::stable_type_id<test_handler> ()) {
      return &handler;
    }
    return nullptr;
  };

  const entt::id_type ev_id = comp::stable_type_id<combat_event> ();
  const entt::id_type h_id = comp::stable_type_id<test_handler> ();

  // Not connected yet: dispatch does nothing.
  hub.dispatch (combat_event{ {}, {}, 1 });
  REQUIRE (handler.hits == 0);

  REQUIRE (hub.connect (ev_id, h_id, "on_hit"));
  REQUIRE (hub.get_all_connections ().size () == 1);

  hub.dispatch (combat_event{ entt::entity{ 10 }, entt::entity{ 20 }, 5 });
  REQUIRE (handler.hits == 1);
  REQUIRE (handler.last_victim == entt::entity{ 20 });

  // Double connect is idempotent and reports success: the desired
  // state is already achieved (scene loads rely on this — see the
  // "already connected" branch in event_hub::connect) and no
  // duplicate connection may be added.
  REQUIRE (hub.connect (ev_id, h_id, "on_hit"));
  REQUIRE (hub.get_all_connections ().size () == 1);

  REQUIRE (hub.disconnect (ev_id, h_id, "on_hit"));
  REQUIRE (hub.get_all_connections ().empty ());

  hub.dispatch (combat_event{ {}, {}, 9 });
  REQUIRE (handler.hits == 1); // unchanged after disconnect
}

TEST_CASE ("event_hub: type-erased dispatch rejects equal-size layout mismatch")
{
  struct payload
  {
    std::uint32_t value = 7;
  };

  event::event_hub hub;
  entt::registry registry;
  int hits = 0;
  constexpr entt::id_type event_id = 0x101;
  constexpr entt::id_type system_id = 0x202;
  constexpr std::uint64_t expected_layout = 0x303;

  hub.resolve_active_registry = [&] () -> entt::registry * {
    return &registry;
  };
  hub.resolve_system_by_type = [] (entt::id_type) -> sys::ecs_system * {
    return nullptr;
  };
  hub.declare_event_sink_by_id (
      event_id, "script_event", system_id, "script_system", "on_event",
      +[] (void *owner, entt::registry &, const void *) {
        ++*static_cast<int *> (owner);
      },
      &hits, sizeof (payload), nullptr, expected_layout);

  REQUIRE (hub.connect (event_id, system_id, "on_event"));
  const payload event{};
  hub.dispatch_by_id (event_id, &event, sizeof event, expected_layout + 1);
  CHECK (hits == 0);

  hub.dispatch_by_id (event_id, &event, sizeof event, expected_layout);
  CHECK (hits == 1);
}

TEST_CASE ("event_hub: concurrent connect/dispatch stays consistent")
{
  // Regression test for a heap corruption seen in the editor: scene loading
  // runs on a background thread (`resource_manager::load` dispatches the scene
  // loader through `std::async`) and its serializer calls `clear_connections()`
  // and `connect()` on the shared hub while the main thread dispatches events.
  // A `push_back` on `connected_handlers` racing with the dispatching thread
  // reallocated the vector underneath it, relocating `connected_sink` elements
  // with a stale data pointer and aborting in the allocator with
  // "free(): invalid pointer" / "malloc(): unaligned tcache chunk detected".
  //
  // Correctness here is "does not corrupt the heap and ends in a consistent
  // state", which is what an allocator abort or a wrong connection count would
  // violate.
  event::event_hub hub;
  event::event_debug_db db;
  hub.db = &db;

  entt::registry reg;
  test_emitter emitter;
  test_handler handler;

  emitter.register_event_sources (hub);
  handler.register_event_sinks (hub);

  hub.resolve_active_registry = [&] () -> entt::registry * { return &reg; };
  hub.resolve_system_by_type = [&] (entt::id_type id) -> sys::ecs_system * {
    if (id == comp::stable_type_id<test_handler> ()) {
      return &handler;
    }
    return nullptr;
  };

  const entt::id_type ev_id = comp::stable_type_id<combat_event> ();
  const entt::id_type h_id = comp::stable_type_id<test_handler> ();
  constexpr int iterations = 400;

  // Writer threads: mimic scene loads re-establishing connections.
  std::vector<std::thread> writers;
  writers.emplace_back ([&] {
    for (int i = 0; i < iterations; ++i) {
      hub.connect (ev_id, h_id, "on_hit");
    }
  });
  writers.emplace_back ([&] {
    for (int i = 0; i < iterations; ++i) {
      hub.disconnect (ev_id, h_id, "on_hit");
    }
  });
  // Mimics a scene activation that resets the whole connection set.
  writers.emplace_back ([&] {
    for (int i = 0; i < iterations; ++i) {
      hub.clear_connections ();
    }
  });
  // Readers: dispatching and snapshotting while the above mutate.
  std::thread dispatcher ([&] {
    for (int i = 0; i < iterations; ++i) {
      hub.dispatch (combat_event{ entt::entity{ 1 }, entt::entity{ 2 }, i });
    }
  });
  std::thread snapshotter ([&] {
    for (int i = 0; i < iterations; ++i) {
      const auto connections = hub.get_all_connections ();
      // A connection is the only legal entry, and it is never duplicated.
      for (const auto &connection : connections) {
        CHECK (connection.event_type_id == ev_id);
        CHECK (connection.system_type_id == h_id);
        CHECK (connection.handler_name == "on_hit");
      }
    }
  });

  for (std::thread &writer : writers) {
    writer.join ();
  }
  dispatcher.join ();
  snapshotter.join ();

  // Settle into a known state and confirm the hub is coherent rather than
  // merely un-crashed.
  hub.clear_connections ();
  CHECK (hub.get_all_connections ().empty ());
  REQUIRE (hub.connect (ev_id, h_id, "on_hit"));
  CHECK (hub.get_all_connections ().size () == 1);

  const int hits_before = handler.hits;
  hub.dispatch (combat_event{ entt::entity{ 7 }, entt::entity{ 8 }, 3 });
  CHECK (handler.hits == hits_before + 1);
  CHECK (handler.last_victim == entt::entity{ 8 });
}
