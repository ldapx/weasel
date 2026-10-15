// Consumer validation for the promoted named modules — `wsl.math` and
// `wsl.event`. Compiled with module flags when `with_modules` is enabled;
// otherwise it falls back to the legacy header includes so the file works
// in both build modes.

// GCC 16's std declarations conflict with doctest's forward-declaration
// block when real std headers are included first (via wsl.math's deps).
#define DOCTEST_CONFIG_USE_STD_HEADERS

// In module mode, every include must precede the import declaration.
#include <doctest/doctest.h>
#include <glm/glm.hpp>

#if defined(WEASEL_MATH_MODULE_CONSUMER)

import wsl.math;
import wsl.event;

#else

#include "wsl/math/vector.hpp"
#include "wsl/math/matrix.hpp"
#include "wsl/event/event_hub.hpp"
#include "wsl/event/message_event.hpp"

#endif

TEST_CASE ("wsl.math module consumer round-trip")
{
  wsl::math::vec3f v{ 1.0F, 2.0F, 3.0F };
  CHECK (v.x () == 1.0F);
  CHECK (v.y () == 2.0F);
  CHECK (v.z () == 3.0F);

  // glm interop survives the module boundary.
  glm::vec3 g = v;
  CHECK (g.x == 1.0F);

  wsl::math::quatf q{ 0.0F, 0.0F, 0.0F, 1.0F };
  CHECK (q.w () == 1.0F);

  wsl::math::mat44f m;
  m.data ()[0] = 2.0F;
  CHECK (m.data ()[0] == 2.0F);

  // The entt-facing members compile and link through the module
  // (their third-party deps live in the GMF, shared with legacy TUs).
  wsl::math::vec2f::register_meta ();
}

TEST_CASE ("wsl.event module consumer")
{
  // The message_event concept is std-only: usable without any third-party
  // include on the consumer side.
  static_assert (wsl::event::message_event<float>);

  // The hub's template API instantiates comp::stable_type_id at the call
  // site — this exercises the shared (global-module) component_meta
  // declarations from inside a module consumer, alongside wsl.math.
  wsl::event::event_hub hub;
  hub.note_listener<struct p2_test_signal> ();
  hub.note_emit<struct p2_test_signal> ();
  CHECK (true);
}
