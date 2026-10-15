// Part of weasel_core_tests; the doctest main lives in
// test_event_bus.cpp.
#include "doctest.h"

#include "wsl/debug/physics_debug_drawer.hpp"
#include "wsl/phys/physics_engine.hpp"

#include <vector>

using namespace wsl::phys;

namespace
{
bool
within_bounds (const debug_line &line, vector3 lower, vector3 upper,
               float tolerance = 1.0e-3F)
{
  auto inside = [&] (vector3 p) {
    return (p.x >= lower.x - tolerance) && (p.x <= upper.x + tolerance)
           && (p.y >= lower.y - tolerance) && (p.y <= upper.y + tolerance)
           && (p.z >= lower.z - tolerance) && (p.z <= upper.z + tolerance);
  };
  return inside (line.a) && inside (line.b);
}
} // namespace

// The editor draws collider wireframes through this path while paused, so it
// must work on a world that has never been stepped.
TEST_CASE ("debug draw emits a wireframe for box colliders")
{
  engine physics;

  body_desc box;
  box.motion = motion_type::Static;
  box.shape = shape_type::box;
  box.half_extents = { 1.0F, 0.5F, 2.0F };
  box.position = { 3.0F, 0.0F, -1.0F };
  REQUIRE (is_valid_body_id (physics.create_body (box)));

  std::vector<debug_line> lines;
  physics.draw_debug (lines);

  // A box hull has 12 edges.
  REQUIRE (lines.size () == 12);

  const vector3 lower{ 3.0F - 1.0F, 0.0F - 0.5F, -1.0F - 2.0F };
  const vector3 upper{ 3.0F + 1.0F, 0.0F + 0.5F, -1.0F + 2.0F };
  for (const debug_line &line : lines) {
    CHECK (within_bounds (line, lower, upper));
    CHECK ((line.rgba & 0x00FFFFFFU) != 0U);
  }
}

TEST_CASE ("debug draw emits circles for sphere colliders")
{
  engine physics;

  body_desc sphere;
  sphere.motion = motion_type::Dynamic;
  sphere.shape = shape_type::sphere;
  sphere.radius = 0.75F;
  sphere.position = { -4.0F, 2.0F, 0.0F };
  REQUIRE (is_valid_body_id (physics.create_body (sphere)));

  std::vector<debug_line> lines;
  physics.draw_debug (lines);

  // Three orthogonal circles of 16 segments each.
  REQUIRE (lines.size () == 3 * 16);

  const vector3 lower{ -4.0F - 0.75F, 2.0F - 0.75F, -0.75F };
  const vector3 upper{ -4.0F + 0.75F, 2.0F + 0.75F, 0.75F };
  for (const debug_line &line : lines) {
    CHECK (within_bounds (line, lower, upper));
  }
}

TEST_CASE ("debug draw appends instead of replacing the caller's lines")
{
  engine physics;

  body_desc box;
  box.motion = motion_type::Static;
  box.shape = shape_type::box;
  REQUIRE (is_valid_body_id (physics.create_body (box)));

  std::vector<debug_line> lines;
  lines.push_back (
      debug_line{ { 0.0F, 0.0F, 0.0F }, { 1.0F, 1.0F, 1.0F }, 0x123456U });
  const std::size_t seed_count = lines.size ();

  physics.draw_debug (lines);

  CHECK (lines.size () == seed_count + 12);
  CHECK (lines.front ().rgba == 0x123456U);
}

// A world with nothing in it must not fabricate geometry.
TEST_CASE ("debug draw stays empty for an empty world")
{
  engine physics;

  std::vector<debug_line> lines;
  physics.draw_debug (lines);

  CHECK (lines.empty ());
}

namespace
{
/** Test double standing in for the scene renderer's line pass. */
struct capture_renderer final : wsl::debug::debug_renderer_interface
{
  struct segment
  {
    glm::vec3 begin{};
    glm::vec3 end{};
    glm::vec4 color{};
  };

  std::vector<segment> segments;

  void
  begin_frame () override
  {
    segments.clear ();
  }

  void
  draw_segment (const glm::vec3 &begin, const glm::vec3 &end,
                const glm::vec4 &color) override
  {
    segments.push_back (segment{ begin, end, color });
  }

  void
  end_frame (wsl::gfx::scene_renderer &, const glm::mat4 &) override
  {
  }
};
} // namespace

// The editor bridge turns the engine's wireframe into renderer segments.
TEST_CASE ("draw_physics_debug forwards collider wireframes to the renderer")
{
  engine physics;

  body_desc box;
  box.motion = motion_type::Static;
  box.shape = shape_type::box;
  box.position = { 1.0F, 2.0F, 3.0F };
  REQUIRE (is_valid_body_id (physics.create_body (box)));

  capture_renderer renderer;
  renderer.begin_frame ();
  wsl::debug::draw_physics_debug (physics, renderer);

  REQUIRE (renderer.segments.size () == 12);
  for (const capture_renderer::segment &segment : renderer.segments) {
    // Colors arrive as Box3D's per-body-type palette with alpha restored.
    CHECK (segment.color.a == doctest::Approx (1.0F));
    CHECK (segment.color.r > 0.0F);
    CHECK (segment.color.g > 0.0F);
    CHECK (segment.color.b > 0.0F);
  }
}
