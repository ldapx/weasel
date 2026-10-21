#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#ifdef WEASEL_HAS_DASLANG

#include "wsl/das/das_engine.hpp"
#include "wsl/das/das_system_adapter.hpp"
#include "wsl/das/wsl_api_module.hpp"
#include "wsl/comp/animator.hpp"
#include "wsl/comp/audio.hpp"
#include "wsl/comp/camera.hpp"
#include "wsl/comp/camera_2d.hpp"
#include "wsl/comp/components.hpp"
#include "wsl/comp/directional_light.hpp"
#include "wsl/comp/point_light.hpp"
#include "wsl/comp/rigid_body.hpp"
#include "wsl/comp/spot_light.hpp"
#include "wsl/comp/sprite_2d.hpp"
#include "wsl/comp/transform.hpp"
#include "wsl/comp/transform_2d.hpp"
#include "wsl/comp/singl/runtime_context.hpp"
#include "wsl/comp/singl/physics_manager.hpp"
#include "wsl/sys/physics_system.hpp"
#include "wsl/event/message_bus.hpp"
#include "wsl/input.hpp"
#include "wsl/reg/component_registry.hpp"
#include "wsl/reg/singleton_registry.hpp"
#include "wsl/reg/system_factory_registry.hpp"
#include "wsl/math/vector.hpp"
#include "wsl/log/log.hpp"
#include "wsl/rsc/scene.hpp"
#include "wsl/rsc/scene_snapshot_serializer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>
#ifdef __linux__
#include <malloc.h>
#endif

namespace
{

const char *const k_smoke_das = R"DAS(
options gen2
require weasel

def smoke_test() {
    var world = current_world()
    var e = world.entity_from_id(0u)

    var t = get(world, e, Transform())
    t.scale.x = 2.0
    t.position.y = 3.5
    t.position.z = -1.0
    t.scale = float3(4.0, 5.0, 6.0)
    t.position = float3(1.0, 2.0, 3.0)

    var t2 = get(world, e, Transform2D())
    t2.rotation = 0.5
    t2.scale = float2(2.0, 3.0)

    var cam = get(world, e, Camera2D())
    cam.zoom = 2.25

    var spr = get(world, e, Sprite2D())
    spr.color.w = 1.0
    spr.size.x = 10.0
    spr.size.y = 20.0
    spr.color = float4(0.25, 0.5, 0.75, 1.0)

    var pl = get(world, e, PointLight())
    pl.intensity = 5.0
    pl.color = float3(0.1, 0.2, 0.3)

    var dl = get(world, e, DirectionalLight())
    dl.intensity = 3.0

    var sl = get(world, e, SpotLight())
    sl.intensity = 4.0

    var missing = world.entity_from_id(1u)
    var missing_t = get_or_default(world, missing, Transform())
    missing_t.position.x = 99.0

    var sxs : float = t.scale.x
    var sy : float = spr.size.y
    var zz : float = cam.zoom
    var cg : float = spr.color.y
    var lr : float = pl.color.x
    var tsc : float = t2.scale.y
    assert(sxs == 4.0)
    assert(sy == 20.0)
    assert(zz == 2.25)
    assert(cg == 0.5)
    assert(lr == 0.1)
    assert(tsc == 3.0)
}
)DAS";

bool
run_smoke ()
{
  wsl::das::das_engine engine;
  if (!engine.initialize ()) {
    std::fprintf (stderr, "das_engine::initialize failed\n");
    return false;
  }

  entt::registry reg;
  auto entity = reg.create ();
  auto missing_entity = reg.create ();
  auto &t = reg.emplace<wsl::comp::transform> (entity);
  t.scale = wsl::math::vec3f{ 1.0f, 1.0f, 1.0f };
  reg.emplace<wsl::comp::transform_2d> (entity);
  reg.emplace<wsl::comp::camera_2d> (entity);
  reg.emplace<wsl::comp::sprite_2d> (entity);
  reg.emplace<wsl::comp::point_light> (entity);
  reg.emplace<wsl::comp::directional_light> (entity);
  reg.emplace<wsl::comp::spot_light> (entity);
  wsl::das::wsl_api_set_active_registry (&reg);

  auto script
      = std::filesystem::temp_directory_path () / "weasel_accessor_smoke.das";
  {
    std::ofstream out (script);
    out << k_smoke_das;
  }

  bool executed = false;
  bool called = false;
  if (engine.execute_file (script)) {
    executed = true;
    called = engine.call_void_function_safe (script, "smoke_test");
    if (!called) {
      const auto &e = engine.last_das_error ();
      std::fprintf (stderr, "smoke_test call failed: %s (%s:%d): %s\n",
                    engine.last_error ().c_str (), e.file.c_str (), e.line,
                    e.message.c_str ());
    }
  } else {
    std::fprintf (stderr, "execute_file failed: %s\n",
                  engine.last_error ().c_str ());
  }

  if (!(executed && called)) {
    return false;
  }

  auto &T = reg.get<wsl::comp::transform> (entity);
  auto &T2 = reg.get<wsl::comp::transform_2d> (entity);
  auto &C = reg.get<wsl::comp::camera_2d> (entity);
  auto &S = reg.get<wsl::comp::sprite_2d> (entity);
  auto &P = reg.get<wsl::comp::point_light> (entity);
  auto &D = reg.get<wsl::comp::directional_light> (entity);
  auto &SP = reg.get<wsl::comp::spot_light> (entity);

  bool ok = true;
  auto chk = [&] (bool c) { ok = ok && c; };
  chk (std::fabs (T.scale.x () - 4.0f) < 1e-5f);
  chk (std::fabs (T.scale.y () - 5.0f) < 1e-5f);
  chk (std::fabs (T.scale.z () - 6.0f) < 1e-5f);
  chk (std::fabs (T.position.x () - 1.0f) < 1e-5f);
  chk (std::fabs (T.position.y () - 2.0f) < 1e-5f);
  chk (std::fabs (T.position.z () - 3.0f) < 1e-5f);
  chk (std::fabs (T2.rotation - 0.5f) < 1e-5f);
  chk (std::fabs (T2.scale.x () - 2.0f) < 1e-5f);
  chk (std::fabs (T2.scale.y () - 3.0f) < 1e-5f);
  chk (std::fabs (C.zoom - 2.25f) < 1e-5f);
  chk (std::fabs (S.color.w () - 1.0f) < 1e-5f);
  chk (std::fabs (S.color.y () - 0.5f) < 1e-5f);
  chk (std::fabs (S.size.x () - 10.0f) < 1e-5f);
  chk (std::fabs (S.size.y () - 20.0f) < 1e-5f);
  chk (std::fabs (P.intensity - 5.0f) < 1e-5f);
  chk (std::fabs (P.color.x () - 0.1f) < 1e-5f);
  chk (std::fabs (D.intensity - 3.0f) < 1e-5f);
  chk (std::fabs (SP.intensity - 4.0f) < 1e-5f);
  chk (!reg.all_of<wsl::comp::transform> (missing_entity));
  return ok;
}

} // namespace

TEST_CASE ("Component accessors property smoke test (interpreted)")
{
  // Module::Initialize() must run once on the main thread before worker
  // threads create das_engine instances.
  wsl::das::das_engine::initialize_global ();

  bool ok = false;
  std::thread worker ([&] { ok = run_smoke (); });
  worker.join ();
  CHECK (ok);
}

TEST_CASE ("Component accessors AOT emission produces real proxy C++")
{
  // Module::Initialize() must run once on the main thread before worker
  // threads create das_engine instances.
  wsl::das::das_engine::initialize_global ();

  const char *const k_aot_das = R"DAS(
options gen2
require weasel

def main() {
    var world = current_world()
    var e = world.entity_from_id(0u)
    var t = get(world, e, Transform())
    t.scale.x = 2.0
    t.scale = float3(1.0, 2.0, 3.0)
    print("scale.x = {t.scale.x}\n")
}
)DAS";

  auto script
      = std::filesystem::temp_directory_path () / "weasel_accessor_aot.das";
  auto out
      = std::filesystem::temp_directory_path () / "weasel_accessor_aot.das.cpp";
  {
    std::ofstream o (script);
    o << k_aot_das;
  }

  std::string error;
  bool ok = false;
  std::thread worker ([&] {
    ok = wsl::das::aot_compile_file (script.string (), out.string (), error);
  });
  worker.join ();

  if (!ok) {
    std::fprintf (stderr, "aot_compile_file failed: %s\n", error.c_str ());
  }
  CHECK (ok);
  if (!ok) {
    return;
  }

  std::ifstream in (out);
  std::string body ((std::istreambuf_iterator<char> (in)),
                    std::istreambuf_iterator<char> ());

  // The v1 bug was that `require weasel_api` resolved to a stub .das and
  // aotRequire never fired, so the generated file self-declared a 1-byte
  // placeholder `TransformAccessor` and never referenced the real proxy.
  // In-process AOT (real Module_WeaselApi loaded) must pull in the real
  // proxy/api headers and register a real AOT module — never the stub
  // placeholder. (Note: under daslang's standard AOT policy, function/method
  // bodies are not inlined into the .das.cpp — the engine drives them via the
  // das runtime — so we assert on the real module scaffolding instead.)
  CHECK (body.find ("#include \"wsl/das/wsl_api_component_accessors.hpp\"")
         != std::string::npos);
  CHECK (body.find ("#include \"wsl/das/wsl_api_module.hpp\"")
         != std::string::npos);
  CHECK (body.find ("registerAotFunctions") != std::string::npos);
  CHECK (body.find ("TransformAccessor") == std::string::npos);
}

// A real accessor script without any `[export]` annotation (the known
// segfault trigger) — used to prove the AOT pipeline handles genuine
// component-accessor scripts and that the accessor logic runs end-to-end.
static const char *const k_aot_exec_das = R"DAS(
options gen2
require weasel

def main() {
    var world = current_world()
    var e = world.entity_from_id(0u)
    var t = get(world, e, Transform())
    t.scale.x = 2.0
    t.scale.y = 2.0
    t.scale.z = 3.0
}

def smoke_test() {
    var world = current_world()
    var e = world.entity_from_id(0u)
    var t = get(world, e, Transform())
    t.scale.x = 2.0
    t.scale.y = 2.0
    t.scale.z = 3.0
}
)DAS";

// AOT-compiles `script`, asserts the emitted .das.cpp is a real AOT module
// (real proxy/api includes + registerAotFunctions), then runs the same script
// in-engine and asserts the entt component actually changed. This closes the
// loop between the AOT emitter and the runtime: the AOT pipeline must accept a
// genuine accessor script, and the accessor API must mutate the real entt
// component when driven through the engine. (daslang's standard AOT policy
// keeps function bodies in the das runtime rather than inlining them into the
// .das.cpp, so the mutation is asserted via the in-engine interpreted run of
// the same script.)
static bool
run_aot_smoke ()
{
  auto script = std::filesystem::temp_directory_path ()
                / "weasel_accessor_aot_exec.das";
  auto out = std::filesystem::temp_directory_path ()
             / "weasel_accessor_aot_exec.das.cpp";
  {
    std::ofstream o (script);
    o << k_aot_exec_das;
  }

  // Drive the script through the engine FIRST (interpreted), before spinning
  // up the AOT worker thread — the worker's thread-local das environment is
  // torn down on join and would corrupt a main-thread engine run that started
  // afterwards. The AOT step is pure code-generation and does not depend on
  // the engine having run.
  wsl::das::das_engine engine;
  if (!engine.initialize ()) {
    return false;
  }
  entt::registry reg;
  auto entity = reg.create ();
  auto &t0 = reg.emplace<wsl::comp::transform> (entity);
  t0.scale = wsl::math::vec3f{ 1.0f, 1.0f, 1.0f };
  wsl::das::wsl_api_set_active_registry (&reg);

  if (!engine.execute_file (script.string ())) {
    return false;
  }
  if (!engine.call_void_function_safe (script.string (), "smoke_test")) {
    return false;
  }

  bool mutated = true;
  auto view = reg.view<wsl::comp::transform> ();
  for (auto e : view) {
    const auto &t = view.get<wsl::comp::transform> (e);
    CHECK (t.scale.x () == doctest::Approx (2.0f));
    CHECK (t.scale.y () == doctest::Approx (2.0f));
    CHECK (t.scale.z () == doctest::Approx (3.0f));
    mutated = mutated && std::fabs (t.scale.x () - 2.0f) < 1e-5f
              && std::fabs (t.scale.y () - 2.0f) < 1e-5f
              && std::fabs (t.scale.z () - 3.0f) < 1e-5f;
  }
  if (!mutated) {
    return false;
  }

  // Now AOT-compile the same script and assert it produces a real, buildable
  // AOT module (real proxy/include + registerAotFunctions). This proves the
  // AOT emitter accepts a genuine accessor script that the engine just ran.
  std::string error;
  bool ok = false;
  std::thread worker ([&] {
    ok = wsl::das::aot_compile_file (script.string (), out.string (), error);
  });
  worker.join ();

  if (!ok) {
    std::fprintf (stderr, "aot_compile_file failed: %s\n", error.c_str ());
    return false;
  }

  std::ifstream in (out);
  std::string body ((std::istreambuf_iterator<char> (in)),
                    std::istreambuf_iterator<char> ());
  if (body.find ("#include \"wsl/das/wsl_api_component_accessors.hpp\"")
      == std::string::npos) {
    return false;
  }
  if (body.find ("registerAotFunctions") == std::string::npos) {
    return false;
  }
  return true;
}

TEST_CASE ("Component accessors AOT module executes in-engine")
{
  // das_engine must be driven from a worker thread (matching the interpreted
  // smoke test): the engine's thread-local daScriptEnvironment is set up there.
  wsl::das::das_engine::initialize_global ();
  bool ok = false;
  std::thread worker ([&] { ok = run_aot_smoke (); });
  worker.join ();
  CHECK (ok);
}

// `query() $(t : Transform&) { ... }` — world-scoped iteration over
// the entt registry via the `query` call macro. Exercises the macro
// expansion (block params -> live proxies) AND the
// `each_world_entity_with` primitive (only entities owning ALL listed
// components are visited).
static const char *const k_query_das = R"DAS(
options gen2
require weasel

def query_test() {
    var count = 0
    query() $(t : Transform&) {
        count += 1
        t.scale.x += 1.0
    }
    assert(count == 2)
}
)DAS";

static bool
run_query ()
{
  wsl::das::das_engine engine;
  if (!engine.initialize ()) {
    std::fprintf (stderr, "das_engine::initialize failed\n");
    return false;
  }

  entt::registry reg;
  auto e0 = reg.create ();
  reg.emplace<wsl::comp::transform> (e0).scale
      = wsl::math::vec3f{ 1.0f, 1.0f, 1.0f };
  auto e1 = reg.create ();
  reg.emplace<wsl::comp::transform> (e1).scale
      = wsl::math::vec3f{ 1.0f, 1.0f, 1.0f };
  // Entity with a different component only — must NOT be visited.
  auto e2 = reg.create ();
  reg.emplace<wsl::comp::camera_2d> (e2);
  wsl::das::wsl_api_set_active_registry (&reg);

  auto script
      = std::filesystem::temp_directory_path () / "weasel_query_smoke.das";
  {
    std::ofstream out (script);
    out << k_query_das;
  }

  bool executed = false;
  bool called = false;
  if (engine.execute_file (script.string ())) {
    executed = true;
    called = engine.call_void_function_safe (script.string (), "query_test");
    if (!called) {
      const auto &e = engine.last_das_error ();
      std::fprintf (stderr, "query_test call failed: %s (%s:%d): %s\n",
                    engine.last_error ().c_str (), e.file.c_str (), e.line,
                    e.message.c_str ());
    }
  } else {
    std::fprintf (stderr, "execute_file failed: %s\n",
                  engine.last_error ().c_str ());
  }

  if (!(executed && called)) {
    return false;
  }

  bool ok = true;
  auto chk = [&] (bool c) { ok = ok && c; };
  chk (std::fabs (reg.get<wsl::comp::transform> (e0).scale.x () - 2.0f)
       < 1e-5f);
  chk (std::fabs (reg.get<wsl::comp::transform> (e1).scale.x () - 2.0f)
       < 1e-5f);
  // e2 has no transform, so it must have been skipped (scale stays at 1).
  return ok;
}

TEST_CASE ("query macro iterates and mutates in place")
{
  // das_engine must be driven from a worker thread (matching the interpreted
  // smoke test): the engine's thread-local daScriptEnvironment is set up there.
  wsl::das::das_engine::initialize_global ();
  bool ok = false;
  std::thread worker ([&] { ok = run_query (); });
  worker.join ();
  CHECK (ok);
}

static const char *const k_mixed_query_das = R"DAS(
options gen2
module mouse_rotate
require weasel

struct MouseRotate {
    yaw : float = 0.0
}

def query_test() {
    query() $(t : Transform&; mr : MouseRotate&) {
        t.position.x += 1.0
        mr.yaw = 5.0
    }
}

)DAS";

static bool
run_mixed_query ()
{
  wsl::das::das_engine engine;
  if (!engine.initialize ()) {
    return false;
  }

  wsl::log::init ();

  auto script
      = std::filesystem::temp_directory_path () / "weasel_mixed_query.das";
  {
    std::ofstream out (script);
    out << k_mixed_query_das;
  }

  if (!engine.execute_file (script.string ())) {
    std::fprintf (stderr, "mixed query execute failed: %s\n",
                  engine.last_error ().c_str ());
    return false;
  }

  auto info = engine.get_struct_info (script, "MouseRotate");
  if (info.size_of <= 0 || info.fields.empty ()) {
    return false;
  }

  wsl::comp::singl::runtime_context runtime ("DasTest", 0, 0, "", true);
  const entt::id_type type_id = entt::hashed_string{ "mouse_rotate" }.value ();
  std::vector<wsl::reg::component_registry::descriptor::das_field> fields;
  for (const auto &field : info.fields) {
    fields.push_back ({ field.name, field.type_name,
                        static_cast<std::uint32_t> (field.offset),
                        static_cast<std::uint32_t> (field.size), field.kind,
                        field.default_value });
  }
  runtime.component_registry ().register_cached_runtime_world_component (
      type_id, "mouse_rotate", "Mouse Rotate", info.size_of,
      std::move (fields));
  runtime.component_registry ().register_component_type_info (
      "mouse_rotate::MouseRotate const", type_id,
      wsl::reg::ComponentKind::DAS_SCRIPT,
      static_cast<std::size_t> (info.size_of));

  entt::registry reg;
  reg.ctx ().emplace<wsl::comp::singl::runtime_context *> (&runtime);
  const entt::entity matching = reg.create ();
  reg.emplace<wsl::comp::transform> (matching);
  const entt::entity non_matching = reg.create ();
  reg.emplace<wsl::comp::transform> (non_matching);
  if (!runtime.component_registry ().das_component_add (reg, type_id,
                                                        matching)) {
    return false;
  }

  wsl::das::wsl_api_set_active_registry (&reg);
  if (!engine.call_void_function_safe (script, "query_test")) {
    std::fprintf (stderr, "mixed query call failed: %s\n",
                  engine.last_error ().c_str ());
    return false;
  }

  const auto &matching_transform = reg.get<wsl::comp::transform> (matching);
  const auto &other_transform = reg.get<wsl::comp::transform> (non_matching);
  const auto *data = runtime.component_registry ().das_component_data (
      reg, type_id, matching);
  if (data == nullptr) {
    return false;
  }

  float yaw = 0.0f;
  std::memcpy (&yaw, data + info.fields[0].offset, sizeof (yaw));
  return std::fabs (matching_transform.position.x () - 1.0f) < 1e-5f
         && std::fabs (other_transform.position.x ()) < 1e-5f
         && std::fabs (yaw - 5.0f) < 1e-5f;
}

TEST_CASE ("mixed query mutates native and Daslang storage in place")
{
  wsl::das::das_engine::initialize_global ();
  wsl::log::init ();
  bool ok = false;
  std::thread worker ([&] { ok = run_mixed_query (); });
  worker.join ();
  CHECK (ok);
}

// Generic Daslang-component accessor (`get_or_default` value copy),
// `has` / `add` / `remove` world-scoped typed operations.
static const char *const k_generic_das_das = R"DAS(
options gen2
module mouse_rotate
require weasel

struct MouseRotate {
    yaw : float = 11.0
}

def accessor_test() : void {
    var world = current_world()
    // Present: the C++ harness wrote 42.0 into the live pool bytes, so the
    // accessor must return the pooled value, not the daslang default (11.0).
    var p = get_or_default(world, world.entity_from_id(0u), MouseRotate())
    assert(p.yaw == 42.0)
    // Absent: must return the daslang default without inserting a component.
    var a = get_or_default(world, world.entity_from_id(1u), MouseRotate())
    assert(a.yaw == 11.0)
    var hp = has(world, world.entity_from_id(0u), MouseRotate())
    assert(hp)
    var ha = has(world, world.entity_from_id(1u), MouseRotate())
    assert(!ha)
    var added = add(world, world.entity_from_id(2u), MouseRotate())
    assert(added)
    var h2 = has(world, world.entity_from_id(2u), MouseRotate())
    assert(h2)
    var removed = remove(world, world.entity_from_id(2u), MouseRotate())
    assert(removed)
    var h3 = has(world, world.entity_from_id(2u), MouseRotate())
    assert(!h3)
}
)DAS";

static bool
run_generic_das_accessor ()
{
  wsl::das::das_engine engine;
  if (!engine.initialize ()) {
    return false;
  }

  wsl::log::init ();

  auto script
      = std::filesystem::temp_directory_path () / "weasel_generic_das.das";
  {
    std::ofstream out (script);
    out << k_generic_das_das;
  }

  if (!engine.execute_file (script.string ())) {
    std::fprintf (stderr, "generic das execute failed: %s\n",
                  engine.last_error ().c_str ());
    return false;
  }

  auto info = engine.get_struct_info (script, "MouseRotate");
  if (info.size_of <= 0 || info.fields.empty ()) {
    return false;
  }

  wsl::comp::singl::runtime_context runtime ("DasTest", 0, 0, "", true);
  const entt::id_type type_id = entt::hashed_string{ "mouse_rotate" }.value ();
  std::vector<wsl::reg::component_registry::descriptor::das_field> fields;
  for (const auto &field : info.fields) {
    fields.push_back ({ field.name, field.type_name,
                        static_cast<std::uint32_t> (field.offset),
                        static_cast<std::uint32_t> (field.size), field.kind,
                        field.default_value });
  }
  runtime.component_registry ().register_cached_runtime_world_component (
      type_id, "mouse_rotate", "Mouse Rotate", info.size_of,
      std::move (fields));
  runtime.component_registry ().register_component_type_info (
      "mouse_rotate::MouseRotate const", type_id,
      wsl::reg::ComponentKind::DAS_SCRIPT,
      static_cast<std::size_t> (info.size_of));

  entt::registry reg;
  reg.ctx ().emplace<wsl::comp::singl::runtime_context *> (&runtime);
  const entt::entity present = reg.create ();
  const entt::entity absent = reg.create ();
  const entt::entity managed = reg.create ();
  if (present != entt::entity{ 0 } || absent != entt::entity{ 1 }
      || managed != entt::entity{ 2 }) {
    return false;
  }
  if (!runtime.component_registry ().das_component_add (reg, type_id,
                                                        present)) {
    return false;
  }
  // Overwrite the pooled default (11.0) with a distinct value to prove the
  // accessor reads live pool bytes rather than the daslang default.
  auto *data = runtime.component_registry ().das_component_data (reg, type_id,
                                                                 present);
  if (data == nullptr) {
    return false;
  }
  const float distinct = 42.0f;
  std::memcpy (data + info.fields[0].offset, &distinct, sizeof (distinct));

  wsl::das::wsl_api_set_active_registry (&reg);
  if (!engine.call_void_function_safe (script, "accessor_test")) {
    std::fprintf (stderr, "generic das call failed: %s\n",
                  engine.last_error ().c_str ());
    return false;
  }
  return true;
}

TEST_CASE ("generic Daslang component get_component_or and typed ops")
{
  wsl::das::das_engine::initialize_global ();
  bool ok = false;
  std::thread worker ([&] { ok = run_generic_das_accessor (); });
  worker.join ();
  CHECK (ok);
}

// Structural mutation inside a `query` must be rejected (the query holds live
// component references into the candidate pools).
static const char *const k_query_guard_das = R"DAS(
options gen2
require weasel

def mutate_in_query() : void {
    var world = current_world()
    query(world) $(t : Transform&) {
        world.destroy(entity)
    }
}
)DAS";

static bool
run_query_mutation_guard ()
{
  wsl::das::das_engine engine;
  if (!engine.initialize ()) {
    return false;
  }

  entt::registry reg;
  auto e0 = reg.create ();
  reg.emplace<wsl::comp::transform> (e0);
  auto e1 = reg.create ();
  reg.emplace<wsl::comp::transform> (e1);
  wsl::das::wsl_api_set_active_registry (&reg);

  auto script
      = std::filesystem::temp_directory_path () / "weasel_query_guard.das";
  {
    std::ofstream out (script);
    out << k_query_guard_das;
  }

  if (!engine.execute_file (script.string ())) {
    return false;
  }
  // The structural mutation must panic, so the call must report failure.
  bool called = engine.call_void_function_safe (script, "mutate_in_query");
  return !called;
}

TEST_CASE ("structural mutation inside query is rejected")
{
  wsl::das::das_engine::initialize_global ();
  bool ok = false;
  std::thread worker ([&] { ok = run_query_mutation_guard (); });
  worker.join ();
  CHECK (ok);
}

// --- §9.1 test matrix ----------------------------------------------------

// Registers a `mouse_rotate::MouseRotate` Daslang component descriptor into the
// given runtime_context (mirrors what the editor does at runtime). Returns the
// type id, or 0 on failure.
static entt::id_type
register_mouse_rotate (wsl::das::das_engine &engine,
                       const std::filesystem::path &script,
                       wsl::comp::singl::runtime_context &runtime)
{
  const entt::id_type type_id = entt::hashed_string{ "mouse_rotate" }.value ();
  auto info = engine.get_struct_info (script, "MouseRotate");
  if (info.size_of <= 0 || info.fields.empty ()) {
    return 0;
  }
  std::vector<wsl::reg::component_registry::descriptor::das_field> fields;
  for (const auto &field : info.fields) {
    fields.push_back ({ field.name, field.type_name,
                        static_cast<std::uint32_t> (field.offset),
                        static_cast<std::uint32_t> (field.size), field.kind,
                        field.default_value });
  }
  runtime.component_registry ().register_cached_runtime_world_component (
      type_id, "mouse_rotate", "Mouse Rotate", info.size_of,
      std::move (fields));
  runtime.component_registry ().register_component_type_info (
      "mouse_rotate::MouseRotate const", type_id,
      wsl::reg::ComponentKind::DAS_SCRIPT,
      static_cast<std::size_t> (info.size_of));
  return type_id;
}

// §9.1 test 1: large query (>512 entities). Guards the smallest-pool iteration
// against the fixed 512-entity scratch buffer — every matching entity must be
// visited exactly once.
static const char *const k_large_query_das = R"DAS(
options gen2
require weasel

def large_query_test() : void {
    var count = 0
    query() $(t : Transform&) {
        count += 1
        t.scale.x += 1.0
    }
    assert(count == 600)
}
)DAS";

static bool
run_large_query ()
{
  wsl::das::das_engine engine;
  if (!engine.initialize ()) {
    return false;
  }

  entt::registry reg;
  const int n = 600;
  for (int i = 0; i < n; ++i) {
    auto e = reg.create ();
    reg.emplace<wsl::comp::transform> (e).scale
        = wsl::math::vec3f{ 1.0f, 1.0f, 1.0f };
  }
  wsl::das::wsl_api_set_active_registry (&reg);

  auto script
      = std::filesystem::temp_directory_path () / "weasel_large_query.das";
  {
    std::ofstream out (script);
    out << k_large_query_das;
  }

  if (!engine.execute_file (script.string ())) {
    return false;
  }
  if (!engine.call_void_function_safe (script.string (), "large_query_test")) {
    return false;
  }

  bool ok = true;
  auto view = reg.view<wsl::comp::transform> ();
  ok = ok && (static_cast<int> (view.size ()) == n);
  for (auto e : view) {
    ok = ok
         && std::fabs (view.get<wsl::comp::transform> (e).scale.x () - 2.0f)
                < 1e-5f;
  }
  return ok;
}

TEST_CASE ("large query visits every matching entity (>512)")
{
  wsl::das::das_engine::initialize_global ();
  bool ok = false;
  std::thread worker ([&] { ok = run_large_query (); });
  worker.join ();
  CHECK (ok);
}

// §9.1 test 4: scene isolation. The same entity integer (0) in two different
// scenes must reference independent Daslang component storage.
static const char *const k_scene_iso_das = R"DAS(
options gen2
module mouse_rotate
require weasel

struct MouseRotate { yaw : float = 0.0 }

def iso_test() : void {
    var world = current_world()
    var p = get_or_default(world, world.entity_from_id(0u), MouseRotate())
    assert(p.yaw == 7.0)
    var none = get_or_default(world, world.entity_from_id(1u), MouseRotate())
    assert(none.yaw == 0.0)
}
)DAS";

static bool
run_scene_isolation ()
{
  wsl::das::das_engine engine;
  if (!engine.initialize ()) {
    return false;
  }

  wsl::log::init ();

  auto script
      = std::filesystem::temp_directory_path () / "weasel_scene_iso.das";
  {
    std::ofstream out (script);
    out << k_scene_iso_das;
  }
  if (!engine.execute_file (script.string ())) {
    return false;
  }

  wsl::comp::singl::runtime_context runtimeA ("DasA", 0, 0, "", true);
  wsl::comp::singl::runtime_context runtimeB ("DasB", 0, 0, "", true);
  const entt::id_type type_id
      = register_mouse_rotate (engine, script, runtimeA);
  if (type_id == 0) {
    return false;
  }
  if (register_mouse_rotate (engine, script, runtimeB) == 0) {
    return false;
  }

  entt::registry regA;
  regA.ctx ().emplace<wsl::comp::singl::runtime_context *> (&runtimeA);
  auto a0 = regA.create ();
  auto a1 = regA.create ();
  if (a0 != entt::entity{ 0 } || a1 != entt::entity{ 1 }) {
    return false;
  }
  if (!runtimeA.component_registry ().das_component_add (regA, type_id, a0)) {
    return false;
  }
  auto *dataA
      = runtimeA.component_registry ().das_component_data (regA, type_id, a0);
  if (dataA == nullptr) {
    return false;
  }
  const float yaw = 7.0f;
  std::memcpy (dataA, &yaw, sizeof (yaw));

  entt::registry regB;
  regB.ctx ().emplace<wsl::comp::singl::runtime_context *> (&runtimeB);
  auto b0 = regB.create ();
  if (b0 != entt::entity{ 0 }) {
    return false;
  }

  wsl::das::wsl_api_set_active_registry (&regA);
  if (!engine.call_void_function_safe (script, "iso_test")) {
    return false;
  }

  // Scene B must have NO Daslang component data for entity 0 even though scene
  // A stored yaw=7 there under the same integer id.
  auto *dataB
      = runtimeB.component_registry ().das_component_data (regB, type_id, b0);
  auto *dataAa
      = runtimeA.component_registry ().das_component_data (regA, type_id, a0);
  return dataB == nullptr && dataAa != nullptr;
}

TEST_CASE ("Daslang component storage is scene-isolated")
{
  wsl::das::das_engine::initialize_global ();
  bool ok = false;
  std::thread worker ([&] { ok = run_scene_isolation (); });
  worker.join ();
  CHECK (ok);
}

// §9.1 test 6: entity-generation recycling cleanup. Destroying an entity must
// drop its Daslang component data so a recycled id does not carry stale bytes.
static const char *const k_recycle_das = R"DAS(
options gen2
module mouse_rotate
require weasel

struct MouseRotate { yaw : float = 0.0 }

def recycle_test() : void {
    var world = current_world()
    var p = get_or_default(world, world.entity_from_id(0u), MouseRotate())
    assert(p.yaw == 9.0)
}
)DAS";

static bool
run_entity_recycle ()
{
  wsl::das::das_engine engine;
  if (!engine.initialize ()) {
    return false;
  }

  wsl::log::init ();

  auto script = std::filesystem::temp_directory_path () / "weasel_recycle.das";
  {
    std::ofstream out (script);
    out << k_recycle_das;
  }
  if (!engine.execute_file (script.string ())) {
    return false;
  }

  wsl::comp::singl::runtime_context runtime ("DasTest", 0, 0, "", true);
  const entt::id_type type_id = register_mouse_rotate (engine, script, runtime);
  if (type_id == 0) {
    return false;
  }

  entt::registry reg;
  reg.ctx ().emplace<wsl::comp::singl::runtime_context *> (&runtime);
  auto e0 = reg.create ();
  if (e0 != entt::entity{ 0 }) {
    return false;
  }
  if (!runtime.component_registry ().das_component_add (reg, type_id, e0)) {
    return false;
  }
  auto *data
      = runtime.component_registry ().das_component_data (reg, type_id, e0);
  if (data == nullptr) {
    return false;
  }
  const float yaw = 9.0f;
  std::memcpy (data, &yaw, sizeof (yaw));

  wsl::das::wsl_api_set_active_registry (&reg);
  if (!engine.call_void_function_safe (script, "recycle_test")) {
    return false;
  }

  // Destroy and recycle the entity id; stale Daslang data must be gone.
  reg.destroy (e0);
  auto e_new = reg.create ();
  auto *after
      = runtime.component_registry ().das_component_data (reg, type_id, e_new);
  return after == nullptr;
}

TEST_CASE ("destroyed entity releases Daslang component data on recycle")
{
  wsl::das::das_engine::initialize_global ();
  bool ok = false;
  std::thread worker ([&] { ok = run_entity_recycle (); });
  worker.join ();
  CHECK (ok);
}

// §9.1 test 13 (partial): failure diagnostics. Invalid entity ids and empty
// queries must degrade gracefully (default value / false / zero count) rather
// than panic or crash.
static const char *const k_diag_das = R"DAS(
options gen2
module mouse_rotate
require weasel

struct MouseRotate { yaw : float = 0.0 }

def diag_test() : void {
    var world = current_world()
    var h = has(world, world.entity_from_id(99999u), MouseRotate())
    assert(!h)
    var d = get_or_default(world, world.entity_from_id(99999u), MouseRotate())
    assert(d.yaw == 0.0)
    var count = 0
    query(world) $(t : Transform&) { count += 1 }
    assert(count == 0)
}
)DAS";

static bool
run_failure_diag ()
{
  wsl::das::das_engine engine;
  if (!engine.initialize ()) {
    return false;
  }

  wsl::log::init ();

  auto script = std::filesystem::temp_directory_path () / "weasel_diag.das";
  {
    std::ofstream out (script);
    out << k_diag_das;
  }
  if (!engine.execute_file (script.string ())) {
    return false;
  }

  wsl::comp::singl::runtime_context runtime ("DasTest", 0, 0, "", true);
  if (register_mouse_rotate (engine, script, runtime) == 0) {
    return false;
  }
  entt::registry reg;
  reg.ctx ().emplace<wsl::comp::singl::runtime_context *> (&runtime);
  wsl::das::wsl_api_set_active_registry (&reg);

  return engine.call_void_function_safe (script, "diag_test");
}

TEST_CASE ("invalid entity and empty query degrade gracefully")
{
  wsl::das::das_engine::initialize_global ();
  bool ok = false;
  std::thread worker ([&] { ok = run_failure_diag (); });
  worker.join ();
  CHECK (ok);
}

// §9.1 (Phase 2): a newly-added built-in view proxy must read/write the live
// component and mutate it in place through a query, exactly like the original
// seven built-ins.
static const char *const k_builtin_view_das = R"DAS(
options gen2
require weasel

def builtin_view_test() : void {
    var world = current_world()
    var rb = get(world, world.entity_from_id(0u), RigidBody())
    rb.friction = 0.9
    assert(rb.friction == 0.9)
    query() $(r : RigidBody&) {
        r.friction = 0.5
        r.half_extents = float3(2.0, 3.0, 4.0)
    }
    assert(rb.friction == 0.5)
}
)DAS";

static bool
run_builtin_view ()
{
  wsl::das::das_engine engine;
  if (!engine.initialize ()) {
    return false;
  }

  entt::registry reg;
  auto e0 = reg.create ();
  reg.emplace<wsl::comp::rigid_body> (e0).friction = 0.2f;
  wsl::das::wsl_api_set_active_registry (&reg);

  auto script
      = std::filesystem::temp_directory_path () / "weasel_builtin_view.das";
  {
    std::ofstream out (script);
    out << k_builtin_view_das;
  }

  if (!engine.execute_file (script.string ())) {
    return false;
  }
  if (!engine.call_void_function_safe (script, "builtin_view_test")) {
    return false;
  }

  const auto &rb = reg.get<wsl::comp::rigid_body> (e0);
  bool ok = std::fabs (rb.friction - 0.5f) < 1e-5f;
  ok = ok && std::fabs (rb.half_extents.x () - 2.0f) < 1e-5f;
  ok = ok && std::fabs (rb.half_extents.y () - 3.0f) < 1e-5f;
  ok = ok && std::fabs (rb.half_extents.z () - 4.0f) < 1e-5f;
  return ok;
}

TEST_CASE ("built-in view proxy (RigidBody) mutates in place")
{
  wsl::das::das_engine::initialize_global ();
  bool ok = false;
  std::thread worker ([&] { ok = run_builtin_view (); });
  worker.join ();
  CHECK (ok);
}

// §9.1 test 10: copy and snapshot round-trip. Verifies that a Daslang component
// is copied by `scene::copy_entity` and survives scene snapshot save/load (both
// binary and JSON) through `scene_snapshot_serializer`.
static const char *const k_copy_snap_das = R"DAS(
options gen2
module mouse_rotate
require weasel

struct MouseRotate { yaw : float = 0.0 }
)DAS";

static bool
run_copy_snapshot ()
{
  wsl::das::das_engine engine;
  if (!engine.initialize ()) {
    return false;
  }
  wsl::log::init ();

  auto script
      = std::filesystem::temp_directory_path () / "weasel_copy_snap.das";
  {
    std::ofstream out (script);
    out << k_copy_snap_das;
  }
  if (!engine.execute_file (script.string ())) {
    return false;
  }

  wsl::comp::singl::runtime_context runtime ("DasTest", 0, 0, "", true);
  const entt::id_type type_id = register_mouse_rotate (engine, script, runtime);
  if (type_id == 0) {
    return false;
  }

  wsl::rsc::scene src_scene (&runtime, nullptr, "src");
  wsl::rsc::scene dst_scene (&runtime, nullptr, "dst");

  // Populate a source entity with a Daslang component carrying a distinct
  // value.
  const entt::entity src_e = src_scene.get_registry ().create ();
  if (!runtime.component_registry ().das_component_add (
          src_scene.get_registry (), type_id, src_e)) {
    return false;
  }
  auto *src_data = runtime.component_registry ().das_component_data (
      src_scene.get_registry (), type_id, src_e);
  if (src_data == nullptr) {
    return false;
  }
  const float k_value = 7.5f;
  std::memcpy (src_data, &k_value, sizeof (k_value));

  // `scene::copy_entity` must copy the Daslang pool bytes into the destination.
  const entt::entity dst_e = dst_scene.copy_entity (src_scene, src_e);
  if (dst_e == entt::null) {
    return false;
  }
  auto *dst_data = runtime.component_registry ().das_component_data (
      dst_scene.get_registry (), type_id, dst_e);
  if (dst_data == nullptr) {
    return false;
  }
  float copied = 0.0f;
  std::memcpy (&copied, dst_data, sizeof (copied));
  if (std::fabs (copied - k_value) > 1e-5f) {
    return false;
  }

  // Snapshot round-trip (binary): save the source scene and reload it into a
  // fresh scene; the Daslang component data must survive.
  wsl::rsc::scene bin_scene (&runtime, nullptr, "bin");
  {
    wsl::rsc::io::scene_snapshot_serializer ser (&runtime, src_scene);
    std::string bin;
    if (!ser.save_to_binary_string (bin)) {
      return false;
    }
    wsl::rsc::io::scene_snapshot_serializer loader (&runtime, bin_scene);
    if (!loader.load_from_binary_string (bin)) {
      return false;
    }
  }
  auto *bin_data = runtime.component_registry ().das_component_data (
      bin_scene.get_registry (), type_id, src_e);
  if (bin_data == nullptr) {
    return false;
  }
  float bin_val = 0.0f;
  std::memcpy (&bin_val, bin_data, sizeof (bin_val));
  if (std::fabs (bin_val - k_value) > 1e-5f) {
    return false;
  }

  // Snapshot round-trip (JSON): save to a file and load into another fresh
  // scene; the Daslang component data must survive.
  wsl::rsc::scene json_scene (&runtime, nullptr, "json");
  {
    auto json_path
        = std::filesystem::temp_directory_path () / "weasel_copy_snap.json";
    wsl::rsc::io::scene_snapshot_serializer ser (&runtime, src_scene);
    if (!ser.save_json (json_path.string ())) {
      return false;
    }
    wsl::rsc::io::scene_snapshot_serializer loader (&runtime, json_scene);
    if (!loader.load_json (json_path.string ())) {
      return false;
    }
  }
  auto *json_data = runtime.component_registry ().das_component_data (
      json_scene.get_registry (), type_id, src_e);
  if (json_data == nullptr) {
    return false;
  }
  float json_val = 0.0f;
  std::memcpy (&json_val, json_data, sizeof (json_val));
  if (std::fabs (json_val - k_value) > 1e-5f) {
    return false;
  }

  return true;
}

TEST_CASE ("Daslang component copy and snapshot round-trip")
{
  wsl::das::das_engine::initialize_global ();
  bool ok = false;
  std::thread worker ([&] { ok = run_copy_snapshot (); });
  worker.join ();
  CHECK (ok);
}
// Animation bindings
// ---------------------------------------------------------------------------

static const char *const k_anim_das = R"DAS(
options gen2
module anim_bindings
require weasel

def run_anim() {
    // Fresh registry: the first entity is id 0 (asserted below).
    var world = current_world()
    var entity = world.entity_from_id(0u)

    var played = play_animation(world, entity, "res://rsc/models/Fox_Walk.anim.ozz")
    assert(played)
    var sped = set_animation_speed(world, entity, 1.5)
    assert(sped)
    var timed = set_animation_time(world, entity, 0.75)
    assert(timed)
    var looped = set_animation_loop(world, entity, false)
    assert(looped)
    var faded = crossfade_animation(world, entity, "res://rsc/models/Fox_Run.anim.ozz", 0.5)
    assert(faded)
    var timed2 = set_animation_time(world, entity, -3.0)
    assert(timed2)
    var faded2 = crossfade_animation(world, entity, "res://rsc/models/Fox_Survey.anim.ozz", -1.0)
    assert(faded2)
    // No-ops: that entity has no Animator, and this id does not exist.
    var missing = world.entity_from_id(1000u)
    var played_missing = play_animation(world, missing, "res://rsc/models/Fox_Walk.anim.ozz")
    assert(!played_missing)
    var sped_missing = set_animation_speed(world, missing, 9.0)
    assert(!sped_missing)
    var stopped = stop_animation(world, entity)
    assert(stopped)
}

)DAS";

TEST_CASE ("anim bindings drive the animator component from daslang")
{
  // The binding functions have internal linkage, so they are exercised the
  // way scripts use them: compiled from a .das file and run against a live
  // registry.
  wsl::das::das_engine::initialize_global ();
  bool ok = false;
  std::thread worker ([&] {
    wsl::das::das_engine engine;
    if (!engine.initialize ()) {
      return;
    }
    wsl::log::init ();

    const auto script
        = std::filesystem::temp_directory_path () / "weasel_anim.das";
    {
      std::ofstream out (script);
      out << k_anim_das;
    }
    if (!engine.execute_file (script.string ())) {
      std::fprintf (stderr, "anim execute failed: %s\n",
                    engine.last_error ().c_str ());
      return;
    }

    wsl::comp::singl::runtime_context runtime ("DasTest", 0, 0, "", true);
    entt::registry reg;
    reg.ctx ().emplace<wsl::comp::singl::runtime_context *> (&runtime);

    const entt::entity animated = reg.create ();
    reg.emplace<wsl::comp::animator> (animated);

    wsl::das::wsl_api_set_active_registry (&reg);
    ok = (entt::to_integral (animated) == 0)
         && engine.call_void_function_safe (script.string (), "run_anim");
    wsl::das::wsl_api_set_active_registry (nullptr);

    if (!ok) {
      return;
    }

    auto &anim = reg.get<wsl::comp::animator> (animated);
    // The last clip change wins.
    CHECK (anim.clip_path == "res://rsc/models/Fox_Survey.anim.ozz");
    CHECK (anim.speed == doctest::Approx (1.5F));
    CHECK (anim.loop == false);
    // anim_stop rewinds and leaves the animator disabled.
    CHECK (anim.playing == false);
    CHECK (anim.time == doctest::Approx (0.0F));
    // Negative inputs are clamped, not pushed into the clock.
    CHECK (anim.crossfade_duration == doctest::Approx (0.0F));
  });
  worker.join ();
  CHECK (ok);
}

// Linear velocity through the gameplay facade: das `set_linear_velocity`
// reaches the live physics body and `linear_velocity` reads it back, with
// graceful degradation for missing components and dead handles.
static const char *const k_velocity_das = R"DAS(
options gen2
module velocity_check
require weasel
def run_velocity() {
    var world = current_world()
    var e = world.create_entity()
    var named = world.set_name(e, "Speeder")
    assert(named)
    var added_t = add(world, e, Transform())
    assert(added_t)
    var added_rb = add(world, e, RigidBody())
    assert(added_rb)
    var set_ok = set_linear_velocity(world, e, float3(3.0, -1.0, 0.5))
    assert(set_ok)
    var v = linear_velocity(world, e)
    assert(v.x == 3.0 && v.y == -1.0 && v.z == 0.5)
    // An entity without a RigidBody degrades gracefully.
    var bare = world.create_entity()
    var set_bare = set_linear_velocity(world, bare, float3(1.0, 1.0, 1.0))
    assert(!set_bare)
    var zero_bare = linear_velocity(world, bare)
    assert(zero_bare.x == 0.0 && zero_bare.y == 0.0 && zero_bare.z == 0.0)
    // A dead handle reads zero and refuses writes.
    var dead = Entity()
    var zero = linear_velocity(world, dead)
    assert(zero.x == 0.0 && zero.y == 0.0 && zero.z == 0.0)
    var set_dead = set_linear_velocity(world, dead, float3(1.0, 1.0, 1.0))
    assert(!set_dead)
    // Teleport the live body and read the simulated position back.
    var tp_ok = teleport_body(world, e, float3(10.0, 20.0, 30.0))
    assert(tp_ok)
    var p = body_position(world, e)
    assert(p.x == 10.0 && p.y == 20.0 && p.z == 30.0)
    // Teleport degrades like velocity on missing bodies and dead handles.
    var tp_bare = teleport_body(world, bare, float3(1.0, 1.0, 1.0))
    assert(!tp_bare)
    var p_bare = body_position(world, bare)
    assert(p_bare.x == 0.0 && p_bare.y == 0.0 && p_bare.z == 0.0)
    var tp_dead = teleport_body(world, dead, float3(1.0, 1.0, 1.0))
    assert(!tp_dead)
    var p_dead = body_position(world, dead)
    assert(p_dead.x == 0.0 && p_dead.y == 0.0 && p_dead.z == 0.0)
}
)DAS";

static bool
run_velocity_check ()
{
  wsl::das::das_engine engine;
  if (!engine.initialize ()) {
    return false;
  }
  wsl::log::init ();
  wsl::comp::singl::runtime_context runtime ("VelocityTest", 0, 0, "", true);
  runtime.set_running (true);
  wsl::rsc::scene &scene
      = runtime.scene_manager ().create_scene ("DasVelocityTest", true);
  entt::registry &reg = scene.get_registry ();
  // Headless scenes carry no managers by default; the app normally binds
  // the physics manager singleton and core systems. Emulate that so rigid
  // bodies get live backend bodies like in a real run.
  reg.ctx ().emplace<wsl::comp::singl::physics_manager> ().ensure_engine ();
  scene.add_system_instance (
      std::make_unique<wsl::sys::physics_system> ("Physics System"));
  wsl::das::wsl_api_set_active_registry (&reg);

  const auto script
      = std::filesystem::temp_directory_path () / "weasel_velocity.das";
  {
    std::ofstream out (script);
    out << k_velocity_das;
  }
  if (!engine.execute_file (script.string ())) {
    std::fprintf (stderr, "velocity execute failed: %s\n",
                  engine.last_error ().c_str ());
    return false;
  }

  scene.resume ();
  scene.update (1.0 / 60.0);
  if (!engine.call_void_function_safe (script.string (), "run_velocity")) {
    std::fprintf (stderr, "velocity call failed: %s\n",
                  engine.last_error ().c_str ());
    return false;
  }

  // The das-side asserts already verified the round-trip; confirm from C++
  // that the physics body exists and carries the scripted velocity.
  bool found = false;
  for (const auto &[entity, name] : scene.get_entity_names ()) {
    if (name == "Speeder" && reg.valid (entity)
        && reg.all_of<wsl::comp::rigid_body> (entity)) {
      const auto body = reg.get<wsl::comp::rigid_body> (entity).body_id;
      if (wsl::phys::is_valid_body_id (body)) {
        found = true;
      }
    }
  }
  if (!found) {
    std::fprintf (stderr, "velocity test speeder has no live body\n");
    return false;
  }

  wsl::das::wsl_api_set_active_registry (nullptr);
  return true;
}

TEST_CASE ("linear velocity and body teleport round-trip through the facade")
{
  wsl::das::das_engine::initialize_global ();
  bool ok = false;
  std::thread worker ([&] { ok = run_velocity_check (); });
  worker.join ();
  CHECK (ok);
}

static const char *const k_unified_api_das = R"DAS(
options gen2
module unified_api
require weasel
require weasel_editor

struct GameScore {
    points : int = 0
}

struct Drift {
    speed : float = 2.5
}

struct Missing {
    value : int = 0
}

def unified_api_smoke() {
    var world = current_world()

    var spawned = world.create_entity()
    assert(!is_null(spawned))
    var transform_added = add(world, spawned, Transform())
    assert(transform_added)
    var spawned_has_transform = has(world, spawned, Transform())
    assert(spawned_has_transform)
    var moved_spawned = set_position(world, spawned, float3(3.0, 0.0, 0.0))
    assert(moved_spawned)
    var transform_removed = remove(world, spawned, Transform())
    assert(transform_removed)
    var spawned_is_alive = world.is_alive(spawned)
    assert(spawned_is_alive)
    var destroyed = world.destroy(spawned)
    assert(destroyed)
    var spawned_is_alive_after_destroy = world.is_alive(spawned)
    assert(!spawned_is_alive_after_destroy)

    var ui = UiService()
    var button_bound_without_system = ui.bind_button("start_button", "start_game")
    assert(!button_bound_without_system)
    var sensor_connected_without_system = connect_area_entered("on_entered")
    assert(!sensor_connected_without_system)

    var root = world.create_entity()
    var child = world.create_entity()
    var attached = world.set_parent(child, root)
    assert(attached)
    var resolved_parent = world.parent_of(child)
    var resolved_parent_alive = world.is_alive(resolved_parent)
    assert(resolved_parent_alive)
    var cycle_rejected = world.set_parent(root, child)
    assert(!cycle_rejected)
    var detached = world.detach(child)
    assert(detached)
    var no_parent = world.parent_of(child)
    assert(is_null(no_parent))
    var reattached = world.set_parent(child, root)
    assert(reattached)
    var root_destroyed = world.destroy(root)
    var child_alive_after_subtree_destroy = world.is_alive(child)
    var subtree_removed = root_destroyed && !child_alive_after_subtree_destroy
    assert(subtree_removed)

    var commands = CommandBuffer()
    commands.world = world
    var doomed = Entity()
    var tagged = Entity()
    var stripped = Entity()
    var count = 0
    query(world) $(t : Transform&) {
        if (t.position.x > 10.0) {
            doomed = entity
            var destroy_queued = commands.destroy(entity)
        }
        if (t.position.x == 0.0) {
            tagged = entity
            var add_queued = defer_add(commands, entity, Sprite2D())
        }
        if (t.position.x == 1.0) {
            stripped = entity
            var remove_queued = defer_remove(commands, entity, Transform())
        }
        count += 1
        t.position.x += 1.0
    }
    assert(count == 3)
    var tag_was_added = has(world, tagged, Sprite2D())
    var component_was_removed = !has(world, stripped, Transform())
    assert(tag_was_added && component_was_removed)
    var doomed_is_alive = world.is_alive(doomed)
    assert(!doomed_is_alive)

    var count_before = world.entity_count()
    var counter = world.create_entity()
    var count_after_create = world.entity_count()
    assert(count_after_create == count_before + 1u)
    var counter_destroyed = world.destroy(counter)
    assert(counter_destroyed)
    var count_after_destroy = world.entity_count()
    assert(count_after_destroy == count_before)

    var named_a = world.create_entity()
    var named_b = world.create_entity()
    var named_a_ok = world.set_name(named_a, "Twin")
    var named_b_ok = world.set_name(named_b, "Twin")
    assert(named_a_ok && named_b_ok)
    var one_twin = world.find_by_name("Twin")
    var one_twin_alive = world.is_alive(one_twin)
    assert(one_twin_alive)
    var twins = world.find_all_by_name("Twin")
    assert(length(twins) == 2)
    var nobody = world.find_all_by_name("Nobody")
    assert(length(nobody) == 0)

    var original = world.create_entity()
    var original_has_transform = add(world, original, Transform())
    assert(original_has_transform)
    var original_placed = set_position(world, original, float3(7.0, 8.0, 9.0))
    assert(original_placed)
    var original_named = world.set_name(original, "Original")
    assert(original_named)
    var clone_parent = world.create_entity()
    var cloned = world.clone_entity(original, clone_parent)
    var cloned_alive = world.is_alive(cloned)
    assert(cloned_alive)
    var clone_is_new = !(cloned == original)
    assert(clone_is_new)
    var cloned_has_transform = has(world, cloned, Transform())
    assert(cloned_has_transform)
    var clone_parent_is = world.parent_of(cloned) == clone_parent
    assert(clone_parent_is)

    var copy_dst = world.create_entity()
    var copied = copy_component(world, copy_dst, original, Transform())
    assert(copied)
    var copy_has = has(world, copy_dst, Transform())
    assert(copy_has)
    var copy_named = world.set_name(copy_dst, "CopyDst")
    assert(copy_named)
    var hierarchy_copy = copy_component(world, copy_dst, original, Hierarchy())
    assert(!hierarchy_copy)

    var pose_parent = world.create_entity()
    var pose_parent_ok = add(world, pose_parent, Transform())
    assert(pose_parent_ok)
    var pose_parent_placed = set_position(world, pose_parent,
                                          float3(10.0, 0.0, 0.0))
    assert(pose_parent_placed)
    var pose_child = world.create_entity()
    var pose_child_ok = add(world, pose_child, Transform())
    assert(pose_child_ok)
    var pose_child_placed = set_position(world, pose_child,
                                         float3(1.0, 2.0, 3.0))
    assert(pose_child_placed)
    var pose_flat_attach = world.set_parent(pose_child, pose_parent, false)
    assert(pose_flat_attach)
    var pose_flat = get(world, pose_child, Transform())
    var pose_fx = pose_flat.position.x
    assert(pose_fx > 0.99 && pose_fx < 1.01)
    var pose_detached = world.set_parent(pose_child, Entity())
    assert(pose_detached)
    var pose_kept = get(world, pose_child, Transform())
    var pose_kx = pose_kept.position.x
    assert(pose_kx > 10.99 && pose_kx < 11.01)
    var pose_ky = pose_kept.position.y
    assert(pose_ky > 1.99 && pose_ky < 2.01)
    var pose_kz = pose_kept.position.z
    assert(pose_kz > 2.99 && pose_kz < 3.01)
    var pose_reset = set_position(world, pose_child, float3(1.0, 2.0, 3.0))
    assert(pose_reset)
    var pose_keep_attach = world.set_parent(pose_child, pose_parent)
    assert(pose_keep_attach)
    var pose_neg = get(world, pose_child, Transform())
    var pose_nx = pose_neg.position.x
    assert(pose_nx > -9.01 && pose_nx < -8.99)

    var rot_parent = world.create_entity()
    var rot_parent_ok = add(world, rot_parent, Transform())
    assert(rot_parent_ok)
    var rot_child = world.create_entity()
    var rot_child_ok = add(world, rot_child, Transform())
    assert(rot_child_ok)
    var rot_child_placed = set_position(world, rot_child,
                                         float3(1.0, 0.0, 0.0))
    assert(rot_child_placed)
    var rot_parent_t = get(world, rot_parent, Transform())
    rot_parent_t.rotation = float4(0.0, 0.7071068, 0.0, 0.7071068)
    var rot_attached = world.set_parent(rot_child, rot_parent, false)
    assert(rot_attached)
    var rot_detached = world.set_parent(rot_child, Entity())
    assert(rot_detached)
    var rot_kept = get(world, rot_child, Transform())
    var rot_kx = rot_kept.position.x
    var rot_kz = rot_kept.position.z
    assert(rot_kx > -0.02 && rot_kx < 0.02)
    assert(rot_kz > -1.02 && rot_kz < -0.98)

    var keep_root = world.create_entity()
    var keep_root_ok = add(world, keep_root, Transform())
    assert(keep_root_ok)
    var keep_root_placed = set_position(world, keep_root, float3(5.0, 0.0, 0.0))
    assert(keep_root_placed)
    var keep_kid = world.create_entity()
    var keep_kid_ok = add(world, keep_kid, Transform())
    assert(keep_kid_ok)
    var keep_kid_placed = set_position(world, keep_kid, float3(2.0, 0.0, 0.0))
    assert(keep_kid_placed)
    var keep_linked = world.set_parent(keep_kid, keep_root, false)
    assert(keep_linked)
    var keep_cleared = world.destroy(keep_root, DestroyMode.entity_only)
    assert(keep_cleared)
    var keep_pose = get(world, keep_kid, Transform())
    var keep_px = keep_pose.position.x
    assert(keep_px > 6.99 && keep_px < 7.01)
    var keep_kid_gone = world.destroy(keep_kid)
    assert(keep_kid_gone)

    var ephem = world.create_entity()
    var emplaced = emplace(world, ephem, Sprite2D())
    assert(emplaced)
    var ephem_has = has(world, ephem, Sprite2D())
    assert(ephem_has)
    var ephem_destroyed = world.destroy(ephem)
    assert(ephem_destroyed)

    var dst_check = get_or_default(world, copy_dst, Transform())
    assert(dst_check.position.x == 7.0)
    var ghost = world.create_entity()
    var ghost_value = get_or_default(world, ghost, Transform())
    assert(ghost_value.position.x == 0.0)
    var ghost_destroyed = world.destroy(ghost)
    assert(ghost_destroyed)
    var out_t = Transform()
    var got_t = try_get(world, copy_dst, Transform(), out_t)
    assert(got_t)
    assert(out_t.position.x == 7.0)
    var out_stripped = Transform()
    var got_stripped = try_get(world, stripped, Transform(), out_stripped)
    assert(!got_stripped)

    var drifter = world.create_entity()
    var drift_added = add(world, drifter, Drift())
    assert(drift_added)
    var drift_check = get_or_default(world, drifter, Drift())
    assert(drift_check.speed == 2.5)
    var drift_out = Drift()
    var drift_got = try_get(world, drifter, Drift(), drift_out)
    assert(drift_got && drift_out.speed == 2.5)
    var drift_dst = world.create_entity()
    var drift_copied = copy_component(world, drift_dst, drifter, Drift())
    assert(drift_copied)
    var drift_copy_check = get_or_default(world, drift_dst, Drift())
    assert(drift_copy_check.speed == 2.5)

    var score_absent = has_singleton(world, GameScore())
    assert(!score_absent)
    var score_added = add_singleton(world, GameScore())
    assert(score_added)
    var score_present = has_singleton(world, GameScore())
    assert(score_present)
    var sc : GameScore& = get_singleton(world, GameScore())
    sc.points = 42
    var sc_check : GameScore& = get_singleton(world, GameScore())
    assert(sc_check.points == 42)
    var sc_value = get_singleton_or_default(world, GameScore())
    assert(sc_value.points == 42)
    var sc_out = GameScore()
    var sc_got = try_get_singleton(world, GameScore(), sc_out)
    assert(sc_got && sc_out.points == 42)
    var score_removed = remove_singleton(world, GameScore())
    assert(score_removed)
    var score_gone = has_singleton(world, GameScore())
    assert(!score_gone)
    var score_default = get_singleton_or_default(world, GameScore())
    assert(score_default.points == 0)
    var missing_out = Missing()
    var missing_got = try_get_singleton(world, Missing(), missing_out)
    assert(!missing_got)
    var missing_has = has_singleton(world, Missing())
    assert(!missing_has)
    var missing_add = add_singleton(world, Missing())
    assert(!missing_add)

    var dancer = world.find_by_name("Dancer")
    var dancer_alive = world.is_alive(dancer)
    assert(dancer_alive)
    var dancer_playing = is_animation_playing(world, dancer)
    assert(dancer_playing)
    var dancer_paused = pause_animation(world, dancer)
    assert(dancer_paused)
    var dancer_is_paused = is_animation_paused(world, dancer)
    assert(dancer_is_paused)
    var dancer_resumed = resume_animation(world, dancer)
    assert(dancer_resumed)
    var dancer_unpaused = is_animation_paused(world, dancer)
    assert(!dancer_unpaused)
    var dancer_timed = set_animation_time(world, dancer, 3.5)
    assert(dancer_timed)
    var dancer_time = animation_time(world, dancer)
    assert(dancer_time > 3.49 && dancer_time < 3.51)
    var dancer_duration = animation_duration(world, dancer)
    assert(dancer_duration < 0.0)
    var dancer_finished = is_animation_finished(world, dancer)
    assert(!dancer_finished)
    var dancer_stopped = stop_animation(world, dancer)
    assert(dancer_stopped)
    var dancer_still_playing = is_animation_playing(world, dancer)
    assert(!dancer_still_playing)

    var speaker = world.find_by_name("Speaker")
    var speaker_playing = is_audio_playing(world, speaker)
    assert(speaker_playing)
    var stripped_playing = is_audio_playing(world, stripped)
    assert(!stripped_playing)

    var cam = world.find_by_name("Cam")
    var cam_alive = world.is_alive(cam)
    assert(cam_alive)
    var fov_set = set_camera_fov(world, cam, 75.0)
    assert(fov_set)
    var fov_read = camera_fov(world, cam)
    assert(fov_read > 74.99 && fov_read < 75.01)
    var near_set = set_camera_near(world, cam, 0.25)
    assert(near_set)
    var near_read = camera_near(world, cam)
    assert(near_read > 0.249 && near_read < 0.251)
    var far_set = set_camera_far(world, cam, 100.0)
    assert(far_set)
    var far_read = camera_far(world, cam)
    assert(far_read > 99.99 && far_read < 100.01)
    var aspect_set = set_camera_aspect_ratio(world, cam, 1.77)
    assert(aspect_set)
    var aspect_read = camera_aspect_ratio(world, cam)
    assert(aspect_read > 1.769 && aspect_read < 1.771)
    var no_active = is_null(active_camera(world))
    assert(no_active)

    var input = Input()
    var mouse_pos = input.mouse_position()
    var mouse_finite = mouse_pos.x == mouse_pos.x && mouse_pos.y == mouse_pos.y
    assert(mouse_finite)
    var mouse_drain = input.mouse_delta()
    var drain_ok = mouse_drain.x == mouse_drain.x && mouse_drain.y == mouse_drain.y
    assert(drain_ok)
    var mouse_still = input.mouse_delta()
    assert(mouse_still.x == 0.0 && mouse_still.y == 0.0)
    var mouse_lb = input.mouse_button_down(1)
    assert(mouse_lb == true || mouse_lb == false)
    var cursor_shown = input.is_cursor_visible()
    assert(cursor_shown == true || cursor_shown == false)
    input.set_cursor_visible(cursor_shown)
    var tick = Time()
    tick.refresh(0.016)
    var tick_unscaled = tick.unscaled_delta
    assert(tick_unscaled > 0.0159 && tick_unscaled < 0.0161)
    var tick_delta = tick.delta
    assert(tick_delta > 0.0159 && tick_delta < 0.0161)
    var tick_fixed = tick.fixed_delta
    assert(tick_fixed > 0.0166 && tick_fixed < 0.0167)
    var tick_elapsed = tick.elapsed
    assert(tick_elapsed >= 0.0)
    var tick_frame = tick.frame_index
    var tick_frame_again = _time_frame_index()
    assert(tick_frame == tick_frame_again)
    var tick_fixed_elapsed = tick.fixed_elapsed
    assert(tick_fixed_elapsed >= 0.0)
    var paused_before = tick.paused()
    assert(!paused_before)
    var scale_before = tick.time_scale()
    assert(scale_before > 0.999 && scale_before < 1.001)
    tick.set_time_scale(2.0)
    var scale_set = tick.time_scale()
    assert(scale_set > 1.999 && scale_set < 2.001)
    tick.refresh(0.016)
    var tick_scaled = tick.delta
    assert(tick_scaled > 0.0319 && tick_scaled < 0.0321)
    tick.set_time_scale(-1.0)
    var scale_clamped = tick.time_scale()
    assert(scale_clamped == 0.0)
    tick.set_time_scale(1.0)
    tick.set_paused(true)
    var paused_set = tick.paused()
    assert(paused_set)
    tick.refresh(0.016)
    var tick_held = tick.delta
    assert(tick_held == 0.0)
    tick.set_paused(false)
    var paused_cleared = tick.paused()
    assert(!paused_cleared)
    var fields = LogFields()
    var empty_msg = format_log_message("hello", fields)
    assert(empty_msg == "hello")
    fields.add("reason", "oops")
    var one_msg = format_log_message("jump failed", fields)
    assert(one_msg == "jump failed reason=oops")
    fields.add("code", "42")
    var two_msg = format_log_message("jump failed", fields)
    assert(two_msg == "jump failed reason=oops code=42")
    var log = Logger()
    log.debug("smoke debug")
    log.trace("smoke trace")
    log.info_fields("with fields", fields)
    log.warn_fields("with fields", fields)
    log.error_fields("with fields", fields)
    log.debug_fields("with fields", fields)
    log.trace_fields("with fields", fields)
    var eq_a = world.create_entity()
    var eq_b = world.create_entity()
    assert(eq_a == eq_a)
    assert(eq_a != eq_b)
    assert(!(eq_a == eq_b))
    var eq_n1 = Entity()
    var eq_n2 = Entity()
    assert(eq_n1 == eq_n2)
    assert(eq_n1 != eq_a)
    var (plane_hit, plane_ok) = ray_plane_hit(0.0, 5.0, 0.0, 0.0, -1.0, 0.0, 0.0, 2.21, 0.0, 0.0, 1.0, 0.0)
    assert(plane_ok)
    assert(plane_hit.x > -0.001 && plane_hit.x < 0.001)
    assert(plane_hit.y > 2.209 && plane_hit.y < 2.211)
    assert(plane_hit.z > -0.001 && plane_hit.z < 0.001)
    var (plane_miss, plane_miss_ok) = ray_plane_hit(0.0, 5.0, 0.0, 0.0, 1.0, 0.0, 0.0, 2.21, 0.0, 0.0, 1.0, 0.0)
    assert(!plane_miss_ok)
    var (pr_o, pr_d, pr_ok) = pick_ray(world, eq_a, 100.0, 100.0, 0.0, 0.0, 800.0, 600.0)
    assert(!pr_ok)
    var ws_f = window_size()
    assert(ws_f.x == ws_f.x && ws_f.y == ws_f.y)
    var ed_min = editor_viewport_min()
    assert(ed_min.x == 0.0 && ed_min.y == 0.0)
    var ed_size = editor_viewport_size()
    assert(ed_size.x == 0.0 && ed_size.y == 0.0)
    editor_refresh_viewport()

    var hero = world.create_entity()
    var hero_sprite = add(world, hero, Sprite2D())
    assert(hero_sprite)
    var hero_named = world.set_name(hero, "Hero")
    assert(hero_named)
    var hero_spr = get(world, hero, Sprite2D())
    hero_spr.color = float4(1.0, 0.5, 0.25, 1.0)
    hero_spr.size = float2(64.0, 32.0)
    hero_spr.uv_offset = float2(0.25, 0.5)
    hero_spr.uv_scale = float2(0.5, 0.5)
    hero_spr.flip_h = true
    var hero_flip_v = hero_spr.flip_v
    assert(!hero_flip_v)
    hero_spr.flip_v = true
    hero_spr.z_index = 7
    var hero_check = get(world, hero, Sprite2D())
    var hero_tint = hero_check.color.x
    assert(hero_tint > 0.99 && hero_tint < 1.01)
    var hero_w = hero_check.size.x
    assert(hero_w > 63.99 && hero_w < 64.01)
    var hero_u = hero_check.uv_offset.x
    assert(hero_u > 0.249 && hero_u < 0.251)
    var hero_us = hero_check.uv_scale.y
    assert(hero_us > 0.499 && hero_us < 0.501)
    var hero_fh = hero_check.flip_h
    assert(hero_fh)
    var hero_fv = hero_check.flip_v
    assert(hero_fv)
    var hero_z = hero_check.z_index
    assert(hero_z == 7)

    var lamp = world.create_entity()
    var lamp_light = add(world, lamp, PointLight())
    assert(lamp_light)
    var lamp_named = world.set_name(lamp, "Lamp")
    assert(lamp_named)
    var lamp_pl = get(world, lamp, PointLight())
    lamp_pl.color = float3(1.0, 0.8, 0.6)
    lamp_pl.intensity = 2.5
    lamp_pl.radius = 42.0
    var lamp_check = get(world, lamp, PointLight())
    var lamp_r = lamp_check.color.x
    assert(lamp_r > 0.99 && lamp_r < 1.01)
    var lamp_i = lamp_check.intensity
    assert(lamp_i > 2.49 && lamp_i < 2.51)
    var lamp_range = lamp_check.radius
    assert(lamp_range > 41.99 && lamp_range < 42.01)

    var sun = world.create_entity()
    var sun_light = add(world, sun, DirectionalLight())
    assert(sun_light)
    var sun_named = world.set_name(sun, "Sun")
    assert(sun_named)
    var sun_dl = get(world, sun, DirectionalLight())
    sun_dl.color = float3(0.9, 0.95, 1.0)
    sun_dl.intensity = 3.0
    var sun_check = get(world, sun, DirectionalLight())
    var sun_b = sun_check.color.z
    assert(sun_b > 0.99 && sun_b < 1.01)
    var sun_i = sun_check.intensity
    assert(sun_i > 2.99 && sun_i < 3.01)

    var cone = world.create_entity()
    var cone_light = add(world, cone, SpotLight())
    assert(cone_light)
    var cone_named = world.set_name(cone, "Cone")
    assert(cone_named)
    var cone_sl = get(world, cone, SpotLight())
    cone_sl.color = float3(1.0, 1.0, 0.8)
    cone_sl.intensity = 4.0
    cone_sl.spot_range = 30.0
    cone_sl.inner_cos = 0.99
    cone_sl.outer_cos = 0.9
    var cone_check = get(world, cone, SpotLight())
    var cone_i = cone_check.intensity
    assert(cone_i > 3.99 && cone_i < 4.01)
    var cone_range = cone_check.spot_range
    assert(cone_range > 29.99 && cone_range < 30.01)
    var cone_inner = cone_check.inner_cos
    assert(cone_inner > 0.989 && cone_inner < 0.991)
    var cone_outer = cone_check.outer_cos
    assert(cone_outer > 0.899 && cone_outer < 0.901)

    var flat = world.create_entity()
    var flat_added = add(world, flat, Transform2D())
    assert(flat_added)
    var flat_moved = move_2d(world, flat, float2(1.0, 2.0))
    assert(flat_moved)
    var flat_placed = set_position_2d(world, flat, float2(4.0, 5.0))
    assert(flat_placed)
    var flat_check = get_or_default(world, flat, Transform2D())
    var flat_x = flat_check.position.x
    assert(flat_x > 3.99 && flat_x < 4.01)
    var flat_y = flat_check.position.y
    assert(flat_y > 4.99 && flat_y < 5.01)
    var flat_gone = world.destroy(flat)
    assert(flat_gone)

    var nursery = world.create_entity()
    var kid = world.create_entity()
    var nursery_attached = world.set_parent(kid, nursery)
    assert(nursery_attached)
    var nursery_destroyed_only = world.destroy(nursery, DestroyMode.entity_only)
    assert(nursery_destroyed_only)
    var nursery_alive = world.is_alive(nursery)
    assert(!nursery_alive)
    var kid_alive = world.is_alive(kid)
    assert(kid_alive)
    var kid_orphaned = is_null(world.parent_of(kid))
    assert(kid_orphaned)

    var dm_parent = world.create_entity()
    var dm_child = world.create_entity()
    var dm_attached = world.set_parent(dm_child, dm_parent)
    assert(dm_attached)
    var dm_destroyed = world.destroy(dm_parent, DestroyMode.entity_only)
    assert(dm_destroyed)
    var dm_parent_gone = !world.is_alive(dm_parent)
    assert(dm_parent_gone)
    var dm_child_alive = world.is_alive(dm_child)
    assert(dm_child_alive)
    var dm_root = world.create_entity()
    var dm_leaf = world.create_entity()
    var dm_linked = world.set_parent(dm_leaf, dm_root)
    assert(dm_linked)
    var dm_subtree = world.destroy(dm_root, DestroyMode.subtree)
    assert(dm_subtree)
    var dm_root_gone = !world.is_alive(dm_root)
    assert(dm_root_gone)
    var dm_leaf_gone = !world.is_alive(dm_leaf)
    assert(dm_leaf_gone)
    var dm_solo = world.create_entity()
    var dm_default = world.destroy(dm_solo)
    var dm_solo_gone = !world.is_alive(dm_solo)
    assert(dm_default && dm_solo_gone)

    var adoptee = world.create_entity()
    var adoptee_has_transform = add(world, adoptee, Transform())
    assert(adoptee_has_transform)
    var adoptee_named = world.set_name(adoptee, "Adoptee")
    assert(adoptee_named)
    var adopter = world.create_entity()
    var adopter_named = world.set_name(adopter, "Adopter")
    assert(adopter_named)
    var reparent_commands = CommandBuffer()
    reparent_commands.world = world
    var reparent_seen = false
    query(world) $(t : Transform&) {
        if (t.position.x == 7.0) {
            var queued = reparent_commands.set_parent(adoptee, adopter)
            assert(queued)
            reparent_seen = true
        }
    }
    assert(reparent_seen)
    var adoptee_parent_is = world.parent_of(adoptee) == adopter
    assert(adoptee_parent_is)
    var adopter_destroyed_only = world.destroy(adopter, DestroyMode.entity_only)
    assert(adopter_destroyed_only)
    var adoptee_alive = world.is_alive(adoptee)
    assert(adoptee_alive)
    var adoptee_orphaned = is_null(world.parent_of(adoptee))
    assert(adoptee_orphaned)

    var spare_root = world.create_entity()
    var spare_kid = world.create_entity()
    var spare_attached = world.set_parent(spare_kid, spare_root)
    assert(spare_attached)
    var spare_commands = CommandBuffer()
    spare_commands.world = world
    var spare_queued = false
    query(world) $(t : Transform&) {
        if (!spare_queued) {
            spare_queued = spare_commands.destroy(spare_root, DestroyMode.entity_only)
        }
    }
    assert(spare_queued)
    var spare_root_alive = world.is_alive(spare_root)
    assert(!spare_root_alive)
    var spare_kid_alive = world.is_alive(spare_kid)
    assert(spare_kid_alive)
    var spare_kid_orphaned = is_null(world.parent_of(spare_kid))
    assert(spare_kid_orphaned)
    var spare_kid_destroyed = world.destroy(spare_kid)
    assert(spare_kid_destroyed)
    var adoptee_destroyed = world.destroy(adoptee)
    assert(adoptee_destroyed)

    var spawn_commands = CommandBuffer()
    spawn_commands.world = world
    var spawned_in_query = Entity()
    var spawn_seen = false
    query(world) $(t : Transform&) {
        if (!spawn_seen && t.position.x == 7.0) {
            spawned_in_query = spawn_commands.create_entity("Spawned")
            var spawn_add = defer_add(spawn_commands, spawned_in_query,
                                      Sprite2D())
            assert(spawn_add)
            spawn_seen = true
        }
    }
    assert(spawn_seen)
    var spawned_alive = world.is_alive(spawned_in_query)
    assert(spawned_alive)
    var spawned_has = has(world, spawned_in_query, Sprite2D())
    assert(spawned_has)
    var found_spawn = world.find_by_name("Spawned")
    assert(found_spawn == spawned_in_query)

    var outside_commands = CommandBuffer()
    outside_commands.world = world
    var outside_spawn = outside_commands.create_entity()
    var outside_alive = world.is_alive(outside_spawn)
    assert(outside_alive)
    var outside_destroyed = world.destroy(outside_spawn)
    assert(outside_destroyed)

    var ex_a = world.create_entity()
    var ex_a_has_transform = add(world, ex_a, Transform())
    assert(ex_a_has_transform)
    var ex_a_has_sprite = add(world, ex_a, Sprite2D())
    assert(ex_a_has_sprite)
    var ex_b = world.create_entity()
    var ex_b_has_transform = add(world, ex_b, Transform())
    assert(ex_b_has_transform)
    var excl_hit_b = false
    var excl_hit_a = false
    query(world, Sprite2D()) $(t : Transform&) {
        if (entity == ex_b) {
            t.position.x += 1000.0
            excl_hit_b = true
        }
        if (entity == ex_a) {
            excl_hit_a = true
        }
    }
    assert(excl_hit_b)
    assert(!excl_hit_a)
    var ex_b_moved = get(world, ex_b, Transform())
    var ex_b_x = ex_b_moved.position.x
    assert(ex_b_x > 999.99 && ex_b_x < 1000.01)
    var ex_a_still = get(world, ex_a, Transform())
    var ex_a_x = ex_a_still.position.x
    assert(ex_a_x > -0.01 && ex_a_x < 0.01)
    var plain_hit_a = false
    query(world) $(t : Transform&) {
        if (entity == ex_a) {
            plain_hit_a = true
        }
    }
    assert(plain_hit_a)
    var empty_hit_a = false
    query(world) $(t : Transform&) {
        if (entity == ex_a) {
            empty_hit_a = true
        }
    }
    assert(empty_hit_a)
    var ex_a_gone = world.destroy(ex_a)
    assert(ex_a_gone)
    var ex_b_gone = world.destroy(ex_b)
    assert(ex_b_gone)
}
)DAS";

static bool
run_unified_api_smoke ()
{
  wsl::das::das_engine engine;
  if (!engine.initialize ()) {
    return false;
  }

  wsl::log::init ();
  wsl::comp::singl::runtime_context runtime ("DasTest", 0, 0, "", true);
  wsl::comp::for_each_type<wsl::comp::component_types>::apply (
      [&]<typename T> () {
        runtime.component_registry ().register_world_component<T> ();
      });
  wsl::rsc::scene test_scene (&runtime, nullptr, "DasUnifiedApiTest");
  entt::registry &reg = test_scene.get_registry ();
  const entt::entity first = reg.create ();
  reg.emplace<wsl::comp::transform> (first);
  const entt::entity second = reg.create ();
  reg.emplace<wsl::comp::transform> (second).position.x () = 1.0F;
  const entt::entity deferred_destroy = reg.create ();
  reg.emplace<wsl::comp::transform> (deferred_destroy).position.x () = 100.0F;
  const entt::entity dancer = reg.create ();
  reg.emplace<wsl::comp::animator> (dancer);
  test_scene.set_entity_name (dancer, "Dancer");
  const entt::entity speaker = reg.create ();
  reg.emplace<wsl::comp::audio> (speaker).playing = true;
  test_scene.set_entity_name (speaker, "Speaker");
  const entt::entity cam = reg.create ();
  reg.emplace<wsl::comp::camera> (cam);
  test_scene.set_entity_name (cam, "Cam");
  wsl::das::wsl_api_set_active_registry (&reg);

  const auto script
      = std::filesystem::temp_directory_path () / "weasel_unified_api.das";
  {
    std::ofstream out (script);
    out << k_unified_api_das;
  }
  if (!engine.execute_file (script.string ())) {
    std::fprintf (stderr, "unified API execute failed: %s\n",
                  engine.last_error ().c_str ());
    return false;
  }

  // Register the Das-defined types used by the smoke script: a `Drift`
  // component and a `GameScore` singleton, both laid out from the compiled
  // struct info exactly like the project cache loader does.
  const entt::id_type drift_component_id
      = entt::hashed_string{ "unified_api::Drift" }.value ();
  {
    auto drift_info = engine.get_struct_info (script, "Drift");
    if (drift_info.size_of <= 0 || drift_info.fields.empty ()) {
      std::fprintf (stderr, "unified API Drift struct info missing\n");
      return false;
    }
    std::vector<wsl::reg::component_registry::descriptor::das_field> fields;
    for (const auto &field : drift_info.fields) {
      fields.push_back ({ field.name, field.type_name,
                          static_cast<std::uint32_t> (field.offset),
                          static_cast<std::uint32_t> (field.size), field.kind,
                          field.default_value });
    }
    if (!runtime.component_registry ().register_cached_runtime_world_component (
            drift_component_id, "unified_api::Drift", "Drift",
            drift_info.size_of, std::move (fields))) {
      return false;
    }
    runtime.component_registry ().register_component_type_info (
        "unified_api::Drift const", drift_component_id,
        wsl::reg::ComponentKind::DAS_SCRIPT,
        static_cast<std::size_t> (drift_info.size_of));
  }
  constexpr entt::id_type game_score_singleton_id = 0x00DA5602U;
  {
    auto score_info = engine.get_struct_info (script, "GameScore");
    if (score_info.size_of <= 0 || score_info.fields.empty ()) {
      std::fprintf (stderr, "unified API GameScore struct info missing\n");
      return false;
    }
    std::vector<wsl::reg::singleton_registry::descriptor::das_field> fields;
    for (const auto &field : score_info.fields) {
      fields.push_back ({ field.name, field.type_name, field.offset, field.size,
                          field.kind, field.default_value });
    }
    if (!runtime.singleton_registry ()
             .register_cached_runtime_singleton_component (
                 game_score_singleton_id, "unified_api::GameScore",
                 "Game Score", score_info.size_of, std::move (fields))) {
      return false;
    }
  }

  if (!engine.call_void_function_safe (script.string (), "unified_api_smoke")) {
    std::fprintf (stderr, "unified API call failed: %s\n",
                  engine.last_error ().c_str ());
    return false;
  }
  // The script adds, mutates, and removes the GameScore singleton; the
  // removal must be visible through the registry afterwards.
  if (runtime.singleton_registry ().das_singleton_contains (
          reg, game_score_singleton_id)) {
    std::fprintf (stderr, "unified API singleton was not removed\n");
    return false;
  }
  const auto *dancer_anim = reg.try_get<wsl::comp::animator> (dancer);
  if (dancer_anim == nullptr || dancer_anim->playing
      || std::fabs (dancer_anim->time) > 1e-5F) {
    std::fprintf (stderr, "unified API animation state mismatch\n");
    return false;
  }
  if (std::fabs (reg.get<wsl::comp::camera> (cam).fov () - 75.0F) > 1e-5F) {
    std::fprintf (stderr, "unified API camera fov mismatch\n");
    return false;
  }
  int twin_count = 0;
  int original_count = 0;
  int spawned_count = 0;
  bool copy_position_ok = false;
  bool hero_ok = false;
  bool lamp_ok = false;
  bool sun_ok = false;
  bool cone_ok = false;
  for (const auto &[entity, name] : test_scene.get_entity_names ()) {
    if (name == "Twin" && reg.valid (entity)) {
      ++twin_count;
    }
    if (name == "Hero" && reg.valid (entity)
        && reg.all_of<wsl::comp::sprite_2d> (entity)) {
      const auto &sprite = reg.get<wsl::comp::sprite_2d> (entity);
      hero_ok = sprite.flip_h && sprite.flip_v && sprite.z_index == 7
                && std::fabs (sprite.size.x () - 64.0F) < 1e-5F
                && std::fabs (sprite.uv_offset.x () - 0.25F) < 1e-5F;
    }
    if (name == "Lamp" && reg.valid (entity)
        && reg.all_of<wsl::comp::point_light> (entity)) {
      const auto &light = reg.get<wsl::comp::point_light> (entity);
      lamp_ok = std::fabs (light.intensity - 2.5F) < 1e-5F
                && std::fabs (light.radius - 42.0F) < 1e-5F;
    }
    if (name == "Sun" && reg.valid (entity)
        && reg.all_of<wsl::comp::directional_light> (entity)) {
      const auto &light = reg.get<wsl::comp::directional_light> (entity);
      sun_ok = std::fabs (light.color.z () - 1.0F) < 1e-5F
               && std::fabs (light.intensity - 3.0F) < 1e-5F;
    }
    if (name == "Cone" && reg.valid (entity)
        && reg.all_of<wsl::comp::spot_light> (entity)) {
      const auto &light = reg.get<wsl::comp::spot_light> (entity);
      cone_ok = std::fabs (light.range - 30.0F) < 1e-5F
                && std::fabs (light.inner_cos - 0.99F) < 1e-4F
                && std::fabs (light.outer_cos - 0.9F) < 1e-4F;
    }
    if (name == "Spawned" && reg.valid (entity)
        && reg.all_of<wsl::comp::sprite_2d> (entity)) {
      ++spawned_count;
    }
    if (name == "Original" && reg.valid (entity)
        && reg.all_of<wsl::comp::transform> (entity)
        && std::fabs (reg.get<wsl::comp::transform> (entity).position.x ()
                      - 7.0F)
               < 1e-5F) {
      ++original_count;
    }
    if (name == "CopyDst" && reg.valid (entity)
        && reg.all_of<wsl::comp::transform> (entity)
        && std::fabs (reg.get<wsl::comp::transform> (entity).position.x ()
                      - 7.0F)
               < 1e-5F
        && std::fabs (reg.get<wsl::comp::transform> (entity).position.y ()
                      - 8.0F)
               < 1e-5F) {
      copy_position_ok = true;
    }
  }
  // The clone inherits the source name, so "Original" appears twice.
  if (twin_count != 2 || original_count != 2 || spawned_count != 1
      || !copy_position_ok || !hero_ok || !lamp_ok || !sun_ok || !cone_ok) {
    std::fprintf (stderr,
                  "unified API name/clone/copy check failed: twins=%d "
                  "originals=%d spawned=%d copy_ok=%d hero=%d lamp=%d sun=%d "
                  "cone=%d\n",
                  twin_count, original_count, spawned_count,
                  (int)copy_position_ok, (int)hero_ok, (int)lamp_ok,
                  (int)sun_ok, (int)cone_ok);
    return false;
  }
  return std::fabs (reg.get<wsl::comp::transform> (first).position.x () - 1.0f)
             < 1e-5f
         && reg.all_of<wsl::comp::sprite_2d> (first)
         && !reg.all_of<wsl::comp::transform> (second)
         && !reg.valid (deferred_destroy);
}

TEST_CASE ("Unified weasel import exposes typed world operations")
{
  wsl::das::das_engine::initialize_global ();
  bool ok = false;
  std::thread worker ([&] { ok = run_unified_api_smoke (); });
  worker.join ();
  CHECK (ok);
}

// The consolidated facade surface: one spelling per operation (World
// methods), the new hierarchy helpers, live `get_or_default` references for
// Daslang components, multi-tag typed query exclusions, and the `Time`
// service accessors.
static const char *const k_facade_surface_das = R"DAS(
options gen2
module facade_surface

require weasel

struct Drift {
    speed : float = 2.5
}

def surface_test() : void {
    var world = current_world()

    // Named creation is a single call.
    var named = world.create_entity("Root")
    var named_alive = world.is_alive(named)
    assert(named_alive)
    var resolved = world.find_by_name("Root")
    var resolved_same = resolved == named
    assert(resolved_same)

    // Hierarchy: link, query, detach.
    var kid = world.create_entity("Kid")
    var linked_ok = world.set_parent(kid, named)
    assert(linked_ok)
    var kid_is_child = world.is_child_of(kid, named)
    assert(kid_is_child)
    var root_is_child = world.is_child_of(named, kid)
    assert(!root_is_child)
    var self_is_child = world.is_child_of(named, named)
    assert(!self_is_child)
    var kids = world.children_of(named)
    var kid_count = length(kids)
    assert(kid_count == 1)
    var kid_matches = kids[0] == kid
    assert(kid_matches)
    var detached_ok = world.detach(kid)
    assert(detached_ok)
    var kid_still_child = world.is_child_of(kid, named)
    assert(!kid_still_child)
    var kid_parent = world.parent_of(kid)
    var kid_orphaned = is_null(kid_parent)
    assert(kid_orphaned)
    var relinked_ok = world.set_parent(kid, named, false)
    assert(relinked_ok)

    // get_or_default is a live reference: writes reach the component.
    var drifter = world.create_entity()
    var added = add(world, drifter, Drift())
    assert(added)
    // `T&` keeps the view live, so the write reaches the component.
    var view : Drift& = get_or_default(world, drifter, Drift())
    var view_speed = view.speed
    assert(view_speed == 2.5)
    view.speed = 9.0
    var reread : Drift& = get_or_default(world, drifter, Drift())
    var reread_speed = reread.speed
    assert(reread_speed == 9.0)
    // Absent: still a usable reference, holding the registered defaults.
    var ghost = world.create_entity()
    var ghost_view : Drift& = get_or_default(world, ghost, Drift())
    var ghost_speed = ghost_view.speed
    assert(ghost_speed == 2.5)
    var ghost_has = has(world, ghost, Drift())
    assert(!ghost_has)
    ghost_view.speed = 1.0
    var ghost_still_absent = has(world, ghost, Drift())
    assert(!ghost_still_absent)

    // Multi-tag typed exclusion: no raw component ids in script code.
    var only_a = world.create_entity()
    var only_a_added = add(world, only_a, Transform())
    assert(only_a_added)
    var with_sprite = world.create_entity()
    var with_sprite_added = add(world, with_sprite, Transform())
    assert(with_sprite_added)
    var with_sprite_sprite = add(world, with_sprite, Sprite2D())
    assert(with_sprite_sprite)
    var with_audio = world.create_entity()
    var with_audio_added = add(world, with_audio, Transform())
    assert(with_audio_added)
    var with_audio_audio = add(world, with_audio, Audio())
    assert(with_audio_audio)
    var hit_a = false
    var hit_sprite = false
    var hit_audio = false
    query(world, Sprite2D(), Audio()) $(t : Transform&) {
        if (entity == only_a) {
            t.position.x += 5.0
            hit_a = true
        }
        if (entity == with_sprite) {
            hit_sprite = true
        }
        if (entity == with_audio) {
            hit_audio = true
        }
    }
    assert(hit_a)
    assert(!hit_sprite)
    assert(!hit_audio)
    var moved = get(world, only_a, Transform())
    var moved_x = moved.position.x
    assert(moved_x > 4.99 && moved_x < 5.01)

    // Time service accessors.
    var scale_before = ctx_scale_probe()
    var tick = Time()
    var paused_default = tick.paused()
    assert(!paused_default)
    var scale_now = tick.time_scale()
    assert(scale_now == scale_before)
    tick.set_paused(true)
    var paused_now = tick.paused()
    assert(paused_now)
    tick.set_paused(false)
    var unpaused_now = tick.paused()
    assert(!unpaused_now)
    tick.set_time_scale(2.0)
    var scaled = tick.time_scale()
    assert(scaled > 1.99 && scaled < 2.01)
    tick.set_time_scale(scale_before)

    // Cleanup: entity_only keeps the child, subtree takes it with the root.
    var doomed = world.create_entity()
    var doomed_kid = world.create_entity()
    var doomed_linked = world.set_parent(doomed_kid, doomed)
    assert(doomed_linked)
    var doomed_gone = world.destroy(doomed, DestroyMode.entity_only)
    assert(doomed_gone)
    var doomed_alive = world.is_alive(doomed)
    assert(!doomed_alive)
    var doomed_kid_alive = world.is_alive(doomed_kid)
    assert(doomed_kid_alive)
    var kid_gone = world.destroy(doomed_kid)
    assert(kid_gone)
    var a_gone = world.destroy(only_a)
    assert(a_gone)
    var sprite_gone = world.destroy(with_sprite)
    assert(sprite_gone)
    var audio_gone = world.destroy(with_audio)
    assert(audio_gone)
    var drifter_gone = world.destroy(drifter)
    assert(drifter_gone)
    var ghost_gone = world.destroy(ghost)
    assert(ghost_gone)
    var named_gone = world.destroy(named)
    assert(named_gone)
}

def ctx_scale_probe() : float {
    var t = Time()
    return t.time_scale()
}
)DAS";

static bool
run_facade_surface ()
{
  wsl::das::das_engine engine;
  if (!engine.initialize ()) {
    return false;
  }

  wsl::log::init ();
  wsl::comp::singl::runtime_context runtime ("DasTest", 0, 0, "", true);
  wsl::comp::for_each_type<wsl::comp::component_types>::apply (
      [&]<typename T> () {
        runtime.component_registry ().register_world_component<T> ();
      });
  wsl::rsc::scene test_scene (&runtime, nullptr, "DasFacadeSurface");
  entt::registry &reg = test_scene.get_registry ();
  wsl::das::wsl_api_set_active_registry (&reg);

  const auto script
      = std::filesystem::temp_directory_path () / "weasel_facade_surface.das";
  {
    std::ofstream out (script);
    out << k_facade_surface_das;
  }
  if (!engine.execute_file (script.string ())) {
    std::fprintf (stderr, "facade surface execute failed: %s\n",
                  engine.last_error ().c_str ());
    return false;
  }

  const entt::id_type drift_component_id
      = entt::hashed_string{ "Drift" }.value ();
  {
    auto drift_info = engine.get_struct_info (script, "Drift");
    if (drift_info.size_of <= 0 || drift_info.fields.empty ()) {
      std::fprintf (stderr, "facade surface Drift struct info missing\n");
      return false;
    }
    std::vector<wsl::reg::component_registry::descriptor::das_field> fields;
    for (const auto &field : drift_info.fields) {
      fields.push_back ({ field.name, field.type_name,
                          static_cast<std::uint32_t> (field.offset),
                          static_cast<std::uint32_t> (field.size), field.kind,
                          field.default_value });
    }
    if (!runtime.component_registry ().register_cached_runtime_world_component (
            drift_component_id, "facade_surface::Drift", "Drift",
            drift_info.size_of, std::move (fields))) {
      std::fprintf (stderr, "facade surface Drift registration failed\n");
      return false;
    }
    runtime.component_registry ().register_component_type_info (
        "facade_surface::Drift const", drift_component_id,
        wsl::reg::ComponentKind::DAS_SCRIPT,
        static_cast<std::size_t> (drift_info.size_of));
  }

  return engine.call_void_function_safe (script.string (), "surface_test");
}

TEST_CASE (
    "Weasel facade surface: one spelling, hierarchy, live views, typed filters")
{
  wsl::das::das_engine::initialize_global ();
  bool ok = false;
  std::thread worker ([&] { ok = run_facade_surface (); });
  worker.join ();
  CHECK (ok);
}

// AOT emission for the `weasel` facade: query macros, generic typed access,
// and unsafe reinterpret casts must survive AOT compilation, and the emitted
// module must reference the real native bindings (not stubs). The script is
// deliberately module-less: explicit `module` scripts crash the AOT compiler
// (upstream daslang issue, still to be isolated), while module-less scripts
// compile cleanly.
static const char *const k_aot_weasel_das = R"DAS(
options gen2
require weasel

def aot_weasel_smoke() {
    var world = current_world()
    var e = world.create_entity()
    var added = add(world, e, Transform())
    assert(added)
    var moved = set_position(world, e, float3(1.0, 2.0, 3.0))
    assert(moved)
    var got = get_or_default(world, e, Transform())
    assert(got.position.x > 0.99 && got.position.x < 1.01)
    var probe = Transform()
    var found = try_get(world, e, Transform(), probe)
    assert(found)
    assert(probe.position.y > 1.99 && probe.position.y < 2.01)
    query(world) $(t : Transform&) {
        t.position.y += 1.0
    }
    query(world, Sprite2D()) $(t : Transform&) {
        t.position.x += 10.0
    }
    var fin = get(world, e, Transform())
    assert(fin.position.x > 10.99 && fin.position.x < 11.01)
    var gone = world.destroy(e)
    assert(gone)
}
)DAS";

static bool
run_aot_weasel_smoke ()
{
  auto script
      = std::filesystem::temp_directory_path () / "weasel_facade_aot.das";
  auto out
      = std::filesystem::temp_directory_path () / "weasel_facade_aot.das.cpp";
  {
    std::ofstream o (script);
    o << k_aot_weasel_das;
  }

  // Interpreted run first (same worker-thread discipline as the other
  // engine tests): proves the facade script means what the asserts say.
  wsl::das::das_engine engine;
  if (!engine.initialize ()) {
    return false;
  }
  entt::registry reg;
  const entt::entity e0 = reg.create ();
  reg.emplace<wsl::comp::transform> (e0);
  wsl::das::wsl_api_set_active_registry (&reg);

  if (!engine.execute_file (script.string ())) {
    std::fprintf (stderr, "weasel facade execute failed: %s\n",
                  engine.last_error ().c_str ());
    return false;
  }
  if (!engine.call_void_function_safe (script.string (), "aot_weasel_smoke")) {
    std::fprintf (stderr, "weasel facade call failed: %s\n",
                  engine.last_error ().c_str ());
    return false;
  }
  if (std::fabs (reg.get<wsl::comp::transform> (e0).position.x () - 10.0F)
          > 1e-5F
      || std::fabs (reg.get<wsl::comp::transform> (e0).position.y () - 1.0F)
             > 1e-5F) {
    std::fprintf (stderr, "weasel facade interpreted run mismatch\n");
    return false;
  }

  // Now AOT-compile the same script: the weasel query macros, generic typed
  // accessors, and unsafe blocks must survive emission against the real
  // native module.
  std::string error;
  bool ok = false;
  std::thread worker ([&] {
    ok = wsl::das::aot_compile_file (script.string (), out.string (), error);
  });
  worker.join ();

  if (!ok) {
    std::fprintf (stderr, "weasel facade aot_compile_file failed: %s\n",
                  error.c_str ());
    return false;
  }

  std::ifstream in (out);
  std::string body ((std::istreambuf_iterator<char> (in)),
                    std::istreambuf_iterator<char> ());
  if (body.find ("#include \"wsl/das/wsl_api_component_accessors.hpp\"")
      == std::string::npos) {
    std::fprintf (stderr, "weasel facade AOT is missing accessor includes\n");
    return false;
  }
  if (body.find ("registerAotFunctions") == std::string::npos) {
    std::fprintf (stderr, "weasel facade AOT is missing module scaffolding\n");
    return false;
  }
  // The weasel facade module made it into the AOT unit (its struct
  // forward declarations are emitted).
  if (body.find ("namespace weasel { struct World; }") == std::string::npos) {
    std::fprintf (stderr, "weasel facade AOT is missing the weasel module\n");
    return false;
  }
  return true;
}

TEST_CASE ("Weasel facade AOT emission covers query macros")
{
  wsl::das::das_engine::initialize_global ();
  bool ok = false;
  std::thread worker ([&] { ok = run_aot_weasel_smoke (); });
  worker.join ();
  CHECK (ok);
}

// Live script system: on_start/update delivery, runtime event subscription
// with disposal, and synchronous emit-to-handler dispatch through a real
// scene update. The script subscribes in on_start, moves a C++-created
// entity every frame, emits on frame one, disposes on frame two, and emits
// again (which must not be delivered).
static const char *const k_live_system_das = R"DAS(
options gen2
module live_probe
require weasel

struct Ping {
    seq : int = 0
}

class ProbeSystem : GameSystem {
    sub : Subscription = Subscription()
    frame : int = 0
    fixed_calls : int = 0
    late_calls : int = 0
    seq : int = 0
    fe_prev : float = -1.0

    def override on_start(var ctx : SystemContext) {
        sub = subscribe_event("Ping", "on_ping")
        var subscribed = sub.is_valid()
        assert(subscribed)
        var world = ctx.world
        var cam = world.create_entity()
        var cam_named = world.set_name(cam, "MainCam")
        assert(cam_named)
        var cam_set = set_active_camera(world, cam)
        assert(cam_set)
    }

    def override update(var ctx : SystemContext; dt : float) {
        frame = frame + 1
        seq = seq * 10 + 2
        var ud = ctx.time.unscaled_delta
        assert(ud > 0.0)
        var sd = ctx.time.delta
        var paused_now = ctx.time.paused()
        if (paused_now) {
            assert(sd == 0.0)
        } else {
            assert(sd == ud)
        }
        var fd = ctx.time.fixed_delta
        assert(fd > 0.0166 && fd < 0.0167)
        var still_paused = ctx.time.paused()
        if (paused_now) {
            assert(still_paused)
        } else {
            assert(!still_paused)
        }
        var live_scale = ctx.time.time_scale()
        assert(live_scale > 0.999 && live_scale < 1.001)
        query(ctx.world) $(t : Transform&) {
            t.position.x += 1.0
        }
        if (frame == 1) {
            var ping = Ping()
            ping.seq = 7
            event_emit("Ping", ping)
        }
        if (frame == 2) {
            var disposed = sub.dispose()
            assert(disposed)
            var ping2 = Ping()
            ping2.seq = 8
            event_emit("Ping", ping2)
        }
    }

    def override fixed_update(var ctx : SystemContext; fixed_dt : float) {
        fixed_calls = fixed_calls + 1
        seq = seq * 10 + 1
        assert(fixed_dt > 0.0166 && fixed_dt < 0.0167)
        var fe = ctx.time.fixed_elapsed
        if (fe_prev >= 0.0) {
            var step = fe - fe_prev
            assert(step > 0.0166 && step < 0.0167)
        }
        fe_prev = fe
    }

    def override late_update(var ctx : SystemContext; dt : float) {
        late_calls = late_calls + 1
        seq = seq * 10 + 3
        assert(dt > 0.0166 && dt < 0.0167)
    }

    def on_ping(ping : Ping) {
        var world = current_world()
        var e = world.create_entity()
        if (ping.seq == 7) {
            var named = world.set_name(e, "Ping1")
            assert(named)
        } else {
            var named = world.set_name(e, "Ping2")
            assert(named)
        }
    }

    def override on_stop(var ctx : SystemContext) {
    }
}

def probe_pause() : void {
    var t = Time()
    t.set_paused(true)
    var p = t.paused()
    assert(p)
}

def probe_unpause() : void {
    var t = Time()
    t.set_paused(false)
    var p = t.paused()
    assert(!p)
}
)DAS";

static bool
run_live_system ()
{
  wsl::das::das_engine engine;
  if (!engine.initialize ()) {
    return false;
  }

  wsl::log::init ();
  wsl::comp::singl::runtime_context runtime ("LiveTest", 0, 0, "", true);
  // Dispatch resolves the active scene through the scene manager, so the
  // test scene must be manager-owned and active (a standalone scene never
  // receives dispatches). Playback follows is_running().
  runtime.set_running (true);
  wsl::rsc::scene &scene
      = runtime.scene_manager ().create_scene ("DasLiveSystemTest", true);
  entt::registry &reg = scene.get_registry ();
  const entt::entity mover = reg.create ();
  reg.emplace<wsl::comp::transform> (mover);
  wsl::das::wsl_api_set_active_registry (&reg);

  const auto script
      = std::filesystem::temp_directory_path () / "weasel_live_system.das";
  {
    std::ofstream out (script);
    out << k_live_system_das;
  }
  if (!engine.execute_file (script.string ())) {
    std::fprintf (stderr, "live system execute failed: %s\n",
                  engine.last_error ().c_str ());
    return false;
  }

  std::string error;
  auto instance = engine.instantiate_class (script, "ProbeSystem", error);
  if (instance.ptr == nullptr || instance.info == nullptr
      || instance.ctx == nullptr) {
    std::fprintf (stderr, "live system instantiate failed: %s\n",
                  error.c_str ());
    return false;
  }
  const entt::id_type probe_id = entt::hashed_string{ "ProbeSystem" }.value ();
  auto adapter = std::make_unique<wsl::das::das_system_adapter> (
      "Probe", script.string (), engine, probe_id, instance.ptr, instance.info,
      instance.ctx);
  wsl::sys::ecs_system &sysref
      = scene.add_system_instance (std::move (adapter));

  scene.resume ();
  if (!sysref.is_active () || sysref.has_failed ()) {
    std::fprintf (stderr, "live system failed to activate\n");
    return false;
  }
  scene.update (1.0 / 60.0);
  scene.update (1.0 / 60.0);

  // Two frames ran: the mover advanced twice and the first ping was
  // delivered; the post-dispose ping must not have been.
  if (std::fabs (reg.get<wsl::comp::transform> (mover).position.x () - 2.0F)
      > 1e-5F) {
    std::fprintf (stderr, "live system lifecycle did not run updates\n");
    return false;
  }
  bool ping1 = false;
  bool ping2 = false;
  for (const auto &[entity, name] : scene.get_entity_names ()) {
    if (name == "Ping1" && reg.valid (entity)) {
      ping1 = true;
    }
    if (name == "Ping2" && reg.valid (entity)) {
      ping2 = true;
    }
  }
  if (!ping1 || ping2) {
    std::fprintf (stderr,
                  "live system event delivery mismatch: ping1=%d "
                  "ping2=%d\n",
                  (int)ping1, (int)ping2);
    return false;
  }
  // The script-selected camera must be the scene's active camera.
  bool main_cam = false;
  for (const auto &[entity, name] : scene.get_entity_names ()) {
    if (name == "MainCam" && reg.valid (entity) && scene.camera == entity) {
      main_cam = true;
    }
  }
  if (!main_cam) {
    std::fprintf (stderr, "live system active camera was not set\n");
    return false;
  }
  // Fixed/late phases ran around each update, in order: two frames of
  // fixed(1) -> update(2) -> late(3) encode seq == 123123.
  auto probe_info = engine.get_struct_info (script, "ProbeSystem");
  auto read_int_field = [&] (const char *name, int &out) {
    for (const auto &field : probe_info.fields) {
      if (field.name == name && field.size == sizeof (int)) {
        std::memcpy (&out,
                     static_cast<const char *> (instance.ptr) + field.offset,
                     sizeof (int));
        return true;
      }
    }
    return false;
  };
  int fixed_calls = -1;
  int late_calls = -1;
  int seq = -1;
  int frames = -1;
  if (!read_int_field ("fixed_calls", fixed_calls)
      || !read_int_field ("late_calls", late_calls)
      || !read_int_field ("seq", seq) || !read_int_field ("frame", frames)) {
    std::fprintf (stderr, "live system probe fields are unreadable\n");
    return false;
  }
  if (fixed_calls != 2 || late_calls != 2 || seq != 123123) {
    std::fprintf (stderr,
                  "live system phase mismatch: fixed=%d late=%d seq=%d\n",
                  fixed_calls, late_calls, seq);
    return false;
  }
  // Pausing freezes fixed steps (no hook ticks, no fixed_update calls)
  // while variable updates keep running: the mover advances but the
  // fixed count, fixed clock, and phase order hold still.
  if (!engine.call_void_function_safe (script.string (), "probe_pause")) {
    std::fprintf (stderr, "live system pause failed: %s\n",
                  engine.last_error ().c_str ());
    return false;
  }
  scene.update (1.0 / 60.0);
  int fixed_paused = -1;
  int late_paused = -1;
  float fe_paused = -1.0F;
  if (!read_int_field ("fixed_calls", fixed_paused)
      || !read_int_field ("late_calls", late_paused)) {
    std::fprintf (stderr, "live system paused fields are unreadable\n");
    return false;
  }
  for (const auto &field : probe_info.fields) {
    if (field.name == "fe_prev" && field.size == sizeof (float)) {
      std::memcpy (&fe_paused,
                   static_cast<const char *> (instance.ptr) + field.offset,
                   sizeof (float));
    }
  }
  // fe_prev holds the clock value seen during the last fixed phase
  // (before that step's tick), so after two frames it reads one step.
  if (fixed_paused != 2 || late_paused != 3
      || std::fabs (reg.get<wsl::comp::transform> (mover).position.x () - 3.0F)
             > 1e-5F
      || fe_paused < 0.0166F || fe_paused > 0.0168F) {
    std::fprintf (stderr,
                  "live system pause mismatch: fixed=%d late=%d fe=%f\n",
                  fixed_paused, late_paused, (double)fe_paused);
    return false;
  }
  if (!engine.call_void_function_safe (script.string (), "probe_unpause")) {
    std::fprintf (stderr, "live system unpause failed: %s\n",
                  engine.last_error ().c_str ());
    return false;
  }
  scene.update (1.0 / 60.0);
  int fixed_resumed = -1;
  int late_resumed = -1;
  if (!read_int_field ("fixed_calls", fixed_resumed)
      || !read_int_field ("late_calls", late_resumed)) {
    std::fprintf (stderr, "live system resumed fields are unreadable\n");
    return false;
  }
  if (fixed_resumed != 3 || late_resumed != 4
      || std::fabs (reg.get<wsl::comp::transform> (mover).position.x () - 4.0F)
             > 1e-5F) {
    std::fprintf (stderr, "live system resume mismatch: fixed=%d late=%d\n",
                  fixed_resumed, late_resumed);
    return false;
  }

  wsl::das::wsl_api_set_active_registry (nullptr);
  return true;
}

TEST_CASE ("Live script system runs lifecycle and event subscriptions")
{
  wsl::das::das_engine::initialize_global ();
  bool ok = false;
  std::thread worker ([&] { ok = run_live_system (); });
  worker.join ();
  CHECK (ok);
}

// Warmed query-throughput baseline (see the performance requirements in
// DASLANG_API_OVERHAUL_PLAN.md). Measures interpreted `query(world)` over
// small, medium, and large entity counts; the numbers are informational
// (printed, not asserted) so CI stays deterministic.
static const char *const k_bench_das = R"DAS(
options gen2
require weasel

def bench_single() {
    var count = 0
    query(current_world()) $(t : Transform&) {
        count += 1
        t.position.x += 0.0
    }
}

def bench_double() {
    var count = 0
    query(current_world()) $(t : Transform&; s : Sprite2D&) {
        count += 1
        t.position.x += 0.0
    }
}
)DAS";

static bool
run_query_benchmark ()
{
  wsl::das::das_engine engine;
  if (!engine.initialize ()) {
    return false;
  }
  wsl::log::init ();
  const char *sizes[3] = { "small", "medium", "large" };
  const int counts[3] = { 100, 10000, 100000 };
  const int iters[3] = { 200, 20, 2 };
  const auto script
      = std::filesystem::temp_directory_path () / "weasel_query_bench.das";
  {
    std::ofstream out (script);
    out << k_bench_das;
  }
  if (!engine.execute_file (script.string ())) {
    std::fprintf (stderr, "bench execute failed: %s\n",
                  engine.last_error ().c_str ());
    return false;
  }
  // Heap bytes currently in use, or -1 where unmeasurable. Used to check
  // for steady-state heap growth across warmed queries (mallinfo2 arenas
  // make this approximate, not exact).
  auto heap_in_use_bytes = [] () -> long long {
#ifdef __linux__
    return static_cast<long long> (mallinfo2 ().uordblks);
#else
    return -1;
#endif
  };
  volatile double sink = 0.0;
  for (int s = 0; s < 3; ++s) {
    wsl::comp::singl::runtime_context runtime ("Bench", 0, 0, "", true);
    entt::registry reg;
    reg.ctx ().emplace<wsl::comp::singl::runtime_context *> (&runtime);
    for (int i = 0; i < counts[s]; ++i) {
      const entt::entity e = reg.create ();
      reg.emplace<wsl::comp::transform> (e);
      if ((i & 1) == 0) {
        reg.emplace<wsl::comp::sprite_2d> (e);
      }
    }
    wsl::das::wsl_api_set_active_registry (&reg);
    // Warm up before timing.
    if (!engine.call_void_function_safe (script.string (), "bench_single")
        || !engine.call_void_function_safe (script.string (), "bench_double")) {
      return false;
    }
    const auto bench_one = [&] (const char *name) {
      const auto start = std::chrono::steady_clock::now ();
      for (int i = 0; i < iters[s]; ++i) {
        if (!engine.call_void_function_safe (script.string (), name)) {
          return -1.0;
        }
      }
      const auto end = std::chrono::steady_clock::now ();
      return std::chrono::duration<double> (end - start).count ()
             / (double)iters[s];
    };
    const double single = bench_one ("bench_single");
    const double dbl = bench_one ("bench_double");
    if (single < 0.0 || dbl < 0.0) {
      return false;
    }
    // Direct C++ EnTT baseline: same populations, same logical work
    // (read + write back position.x per match). The read feeds a volatile
    // sink so the loop is not optimized away.
    const auto bench_entt = [&] (bool two_comp) {
      const auto start = std::chrono::steady_clock::now ();
      for (int i = 0; i < iters[s]; ++i) {
        if (!two_comp) {
          for (const auto e : reg.view<wsl::comp::transform> ()) {
            auto &t = reg.get<wsl::comp::transform> (e);
            sink += t.position.x ();
            t.position.x () += 0.0F;
          }
        } else {
          for (const auto e :
               reg.view<wsl::comp::transform, wsl::comp::sprite_2d> ()) {
            auto &t = reg.get<wsl::comp::transform> (e);
            sink += t.position.x ();
            t.position.x () += 0.0F;
          }
        }
      }
      const auto end = std::chrono::steady_clock::now ();
      return std::chrono::duration<double> (end - start).count ()
             / (double)iters[s];
    };
    const double entt_single = bench_entt (false);
    const double entt_double = bench_entt (true);
    // Steady-state heap growth across warmed das queries. A clean hot path
    // shows no growth; interpreter call overhead may show a small constant.
    const long long heap_before = heap_in_use_bytes ();
    for (int i = 0; i < iters[s]; ++i) {
      if (!engine.call_void_function_safe (script.string (), "bench_single")) {
        return false;
      }
    }
    const long long heap_after = heap_in_use_bytes ();
    const double heap_per_call
        = heap_before < 0 || heap_after < 0
              ? -1.0
              : (double)(heap_after - heap_before) / (double)iters[s];
    std::fprintf (stderr,
                  "[bench] query %s n=%d: single=%.3fms (%.0f ent/s) "
                  "two-comp=%.3fms (%.0f ent/s)\n",
                  sizes[s], counts[s], single * 1e3, (double)counts[s] / single,
                  dbl * 1e3, (double)counts[s] / dbl);
    std::fprintf (stderr,
                  "[bench] entt %s n=%d: single=%.3fms (%.0f ent/s) "
                  "two-comp=%.3fms (%.0f ent/s)\n",
                  sizes[s], counts[s], entt_single * 1e3,
                  (double)counts[s] / entt_single, entt_double * 1e3,
                  (double)counts[s] / entt_double);
    if (heap_per_call < 0.0) {
      std::fprintf (stderr, "[bench] heap %s: unmeasurable on this platform\n",
                    sizes[s]);
    } else {
      std::fprintf (stderr, "[bench] heap %s: %+.1f bytes per warmed query\n",
                    sizes[s], heap_per_call);
    }
    // The hot path must not grow the heap once warmed: any per-query
    // allocation shows up immediately at n=100 over 200 iterations.
    if (heap_per_call >= 0.0 && s == 0 && heap_per_call != 0.0) {
      std::fprintf (stderr, "[bench] heap growth detected: %.1f bytes/query\n",
                    heap_per_call);
      wsl::das::wsl_api_set_active_registry (nullptr);
      return false;
    }
  }
  // Per-query-call overhead in isolation: empty registry, so the measured
  // time is type-id resolution + view setup + block dispatch only.
  {
    wsl::comp::singl::runtime_context runtime ("BenchEmpty", 0, 0, "", true);
    entt::registry reg;
    reg.ctx ().emplace<wsl::comp::singl::runtime_context *> (&runtime);
    wsl::das::wsl_api_set_active_registry (&reg);
    const int empty_iters = 1000;
    const auto start = std::chrono::steady_clock::now ();
    for (int i = 0; i < empty_iters; ++i) {
      if (!engine.call_void_function_safe (script.string (), "bench_single")) {
        return false;
      }
    }
    const auto end = std::chrono::steady_clock::now ();
    const double per_call_us
        = std::chrono::duration<double> (end - start).count ()
          / (double)empty_iters * 1e6;
    std::fprintf (stderr, "[bench] query-call overhead (empty): %.2fus/call\n",
                  per_call_us);
  }
  wsl::das::wsl_api_set_active_registry (nullptr);
  (void)sink;
  return true;
}

TEST_CASE ("Query throughput baseline (informational)")
{
  wsl::das::das_engine::initialize_global ();
  bool ok = false;
  std::thread worker ([&] { ok = run_query_benchmark (); });
  worker.join ();
  CHECK (ok);
}

// Named input actions over the unified facade: keyboard/mouse bindings,
// held state, per-frame just_pressed / just_released edges scanned from
// the published message bus, axis composition, and runtime rebinding.
static const char *const k_input_actions_das = R"DAS(
options gen2
module input_probe
require weasel

// SDL scancodes: SPACE == 44, "1" == 30, LEFT == 80, RIGHT == 79.
// Mouse buttons: 1 = left.
def input_bind() : void {
    var input = Input()
    var ok_jump = input.bind_key("jump", 44)
    assert(ok_jump)
    var ok_fire = input.bind_mouse("fire", 1)
    assert(ok_fire)
    var ok_left = input.bind_key("left", 80)
    assert(ok_left)
    var ok_right = input.bind_key("right", 79)
    assert(ok_right)
    var held = input.pressed("jump")
    assert(!held)
    var no_press = input.just_pressed("jump")
    assert(!no_press)
    var no_release = input.just_released("jump")
    assert(!no_release)
    var axis_none = input.value("left", "right")
    assert(axis_none == 0.0)
    var vec = input.value2("left", "right", "up", "down")
    var vec_zero = vec.x == 0.0 && vec.y == 0.0
    assert(vec_zero)
}

def input_pressed_edges() : void {
    var input = Input()
    var jp = input.just_pressed("jump")
    assert(jp)
    var jr = input.just_released("jump")
    assert(!jr)
    var fp = input.just_pressed("fire")
    assert(fp)
}

def input_repeat_ignored() : void {
    var input = Input()
    var jp = input.just_pressed("jump")
    assert(!jp)
}

def input_released_edges() : void {
    var input = Input()
    var jp = input.just_pressed("jump")
    assert(!jp)
    var jr = input.just_released("jump")
    assert(jr)
    var fr = input.just_released("fire")
    assert(fr)
}

def input_cleared() : void {
    var input = Input()
    var jp = input.just_pressed("jump")
    assert(!jp)
    var jr = input.just_released("jump")
    assert(!jr)
    var fp = input.just_pressed("fire")
    assert(!fp)
    var fr = input.just_released("fire")
    assert(!fr)
}

def input_rebind() : void {
    var input = Input()
    var gone = input.unbind("jump")
    assert(gone)
    var gone_again = input.unbind("jump")
    assert(!gone_again)
    var rebound = input.bind_key("jump", 30)
    assert(rebound)
    var held = input.pressed("jump")
    assert(!held)
}

def input_rebound() : void {
    var input = Input()
    var jp = input.just_pressed("jump")
    assert(jp)
}
)DAS";

static bool
run_input_actions ()
{
  wsl::das::das_engine engine;
  if (!engine.initialize ()) {
    return false;
  }
  wsl::log::init ();
  wsl::comp::singl::runtime_context runtime ("InputActionsTest", 0, 0, "",
                                             true);
  wsl::rsc::scene scene (&runtime, nullptr, "DasInputActionsTest");
  entt::registry &reg = scene.get_registry ();
  reg.ctx ().emplace<wsl::comp::singl::runtime_context *> (&runtime);
  wsl::das::wsl_api_set_active_registry (&reg);

  const auto script
      = std::filesystem::temp_directory_path () / "weasel_input_actions.das";
  {
    std::ofstream out (script);
    out << k_input_actions_das;
  }
  if (!engine.execute_file (script.string ())) {
    std::fprintf (stderr, "input actions execute failed: %s\n",
                  engine.last_error ().c_str ());
    return false;
  }

  auto &bus = runtime.message_bus ();
  auto call = [&] (const char *func) {
    return engine.call_void_function_safe (script.string (), func);
  };

  if (!call ("input_bind")) {
    std::fprintf (stderr, "input actions bind failed: %s\n",
                  engine.last_error ().c_str ());
    return false;
  }

  // Publish a fresh press and a mouse-button down, then drain into the
  // per-frame published buffers the natives read.
  wsl::input::key_pressed press{};
  press.scancode = SDL_SCANCODE_SPACE;
  press.repeat = false;
  bus.post (press);
  wsl::input::mouse_button mdown{};
  mdown.button = 1;
  mdown.down = true;
  bus.post (mdown);
  bus.drain (reg);
  if (!call ("input_pressed_edges")) {
    std::fprintf (stderr, "input actions pressed edges failed: %s\n",
                  engine.last_error ().c_str ());
    return false;
  }

  // Repeats must not count as fresh presses.
  wsl::input::key_pressed held{};
  held.scancode = SDL_SCANCODE_SPACE;
  held.repeat = true;
  bus.post (held);
  bus.drain (reg);
  if (!call ("input_repeat_ignored")) {
    std::fprintf (stderr, "input actions repeat guard failed: %s\n",
                  engine.last_error ().c_str ());
    return false;
  }

  wsl::input::key_released release{};
  release.scancode = SDL_SCANCODE_SPACE;
  bus.post (release);
  wsl::input::mouse_button mup{};
  mup.button = 1;
  mup.down = false;
  bus.post (mup);
  bus.drain (reg);
  if (!call ("input_released_edges")) {
    std::fprintf (stderr, "input actions released edges failed: %s\n",
                  engine.last_error ().c_str ());
    return false;
  }

  // No new messages: all edges must be quiet.
  bus.drain (reg);
  if (!call ("input_cleared")) {
    std::fprintf (stderr, "input actions cleared failed: %s\n",
                  engine.last_error ().c_str ());
    return false;
  }

  if (!call ("input_rebind")) {
    std::fprintf (stderr, "input actions rebind failed: %s\n",
                  engine.last_error ().c_str ());
    return false;
  }

  // The rebinding moved jump to a new key: only that key triggers now.
  wsl::input::key_pressed press_new{};
  press_new.scancode = SDL_SCANCODE_1;
  bus.post (press_new);
  bus.drain (reg);
  if (!call ("input_rebound")) {
    std::fprintf (stderr, "input actions rebound failed: %s\n",
                  engine.last_error ().c_str ());
    return false;
  }

  wsl::input::key_pressed press_old{};
  press_old.scancode = SDL_SCANCODE_SPACE;
  bus.post (press_old);
  bus.drain (reg);
  if (!call ("input_cleared")) {
    std::fprintf (stderr, "input actions stale binding fired: %s\n",
                  engine.last_error ().c_str ());
    return false;
  }

  wsl::das::wsl_api_set_active_registry (nullptr);
  return true;
}

TEST_CASE ("Named input actions: bind, held, edges, axes, rebind")
{
  wsl::das::das_engine::initialize_global ();
  bool ok = false;
  std::thread worker ([&] { ok = run_input_actions (); });
  worker.join ();
  CHECK (ok);
}

// Scene system control over the unified facade: scheduling declarations in
// on_start (stage/dependency/conflict recorded on the adapter), plus runtime
// enable/disable by engine display name with activation-gated updates.
static const char *const k_system_control_das = R"DAS(
options gen2
module counter_probe
require weasel

class CounterSystem : GameSystem {
    def override on_start(var ctx : SystemContext) {
        set_stage("logic")
        depends_on("CounterDep")
        conflicts_with("CounterConflict")
    }

    def override update(var ctx : SystemContext; dt : float) {
        query(ctx.world) $(t : Transform&) {
            t.position.x += 1.0
        }
    }

    def override on_stop(var ctx : SystemContext) {
    }
}

def counter_disable() : void {
    var off = disable_system("Counter")
    assert(off)
    var gone = disable_system("NoSuchSystem")
    assert(!gone)
    var still = system_enabled("Counter")
    assert(!still)
    var missing = system_enabled("NoSuchSystem")
    assert(!missing)
}

def counter_enable() : void {
    var on = enable_system("Counter")
    assert(on)
    var back = system_enabled("Counter")
    assert(back)
}
)DAS";

static bool
run_system_control ()
{
  wsl::das::das_engine engine;
  if (!engine.initialize ()) {
    return false;
  }

  wsl::log::init ();
  wsl::comp::singl::runtime_context runtime ("SysControlTest", 0, 0, "", true);
  runtime.set_running (true);
  wsl::rsc::scene &scene
      = runtime.scene_manager ().create_scene ("DasSysControlTest", true);
  entt::registry &reg = scene.get_registry ();
  const entt::entity mover = reg.create ();
  reg.emplace<wsl::comp::transform> (mover);
  wsl::das::wsl_api_set_active_registry (&reg);

  const auto script
      = std::filesystem::temp_directory_path () / "weasel_system_control.das";
  {
    std::ofstream out (script);
    out << k_system_control_das;
  }
  if (!engine.execute_file (script.string ())) {
    std::fprintf (stderr, "system control execute failed: %s\n",
                  engine.last_error ().c_str ());
    return false;
  }

  std::string error;
  auto instance = engine.instantiate_class (script, "CounterSystem", error);
  if (instance.ptr == nullptr || instance.info == nullptr
      || instance.ctx == nullptr) {
    std::fprintf (stderr, "system control instantiate failed: %s\n",
                  error.c_str ());
    return false;
  }
  const entt::id_type counter_id
      = entt::hashed_string{ "CounterSystem" }.value ();
  auto adapter = std::make_unique<wsl::das::das_system_adapter> (
      "Counter", script.string (), engine, counter_id, instance.ptr,
      instance.info, instance.ctx);
  wsl::sys::ecs_system &sysref
      = scene.add_system_instance (std::move (adapter));

  scene.resume ();
  if (!sysref.is_active () || sysref.has_failed ()) {
    std::fprintf (stderr, "system control failed to activate\n");
    return false;
  }
  // Scheduling declarations from on_start landed on the adapter.
  auto *das_adapter = dynamic_cast<wsl::das::das_system_adapter *> (&sysref);
  if (das_adapter == nullptr || das_adapter->get_stage () != "logic") {
    std::fprintf (stderr, "system control stage was not recorded\n");
    return false;
  }
  const auto &deps = das_adapter->get_dependencies ();
  if (std::find (deps.begin (), deps.end (), "CounterDep") == deps.end ()) {
    std::fprintf (stderr, "system control dependency was not recorded\n");
    return false;
  }
  const auto &conflicts = das_adapter->get_conflicts ();
  if (std::find (conflicts.begin (), conflicts.end (), "CounterConflict")
      == conflicts.end ()) {
    std::fprintf (stderr, "system control conflict was not recorded\n");
    return false;
  }
  if (!engine.call_void_function_safe (script.string (), "counter_enable")) {
    std::fprintf (stderr, "system control enable probe failed: %s\n",
                  engine.last_error ().c_str ());
    return false;
  }

  scene.update (1.0 / 60.0);
  if (std::fabs (reg.get<wsl::comp::transform> (mover).position.x () - 1.0F)
      > 1e-5F) {
    std::fprintf (stderr, "system control first update did not run\n");
    return false;
  }
  if (!engine.call_void_function_safe (script.string (), "counter_disable")) {
    std::fprintf (stderr, "system control disable failed: %s\n",
                  engine.last_error ().c_str ());
    return false;
  }
  scene.update (1.0 / 60.0);
  if (std::fabs (reg.get<wsl::comp::transform> (mover).position.x () - 1.0F)
      > 1e-5F) {
    std::fprintf (stderr, "disabled system still updated\n");
    return false;
  }
  if (!engine.call_void_function_safe (script.string (), "counter_enable")) {
    std::fprintf (stderr, "system control re-enable failed: %s\n",
                  engine.last_error ().c_str ());
    return false;
  }
  scene.update (1.0 / 60.0);
  if (std::fabs (reg.get<wsl::comp::transform> (mover).position.x () - 2.0F)
      > 1e-5F) {
    std::fprintf (stderr, "re-enabled system did not resume\n");
    return false;
  }

  wsl::das::wsl_api_set_active_registry (nullptr);
  return true;
}

TEST_CASE ("Scene system enable/disable and scheduling declarations")
{
  wsl::das::das_engine::initialize_global ();
  bool ok = false;
  std::thread worker ([&] { ok = run_system_control (); });
  worker.join ();
  CHECK (ok);
}

// The go-demo gameplay scripts migrated to `require weasel`: compile the
// real game files headless, run their lifecycle, and verify they behave.
// Cameras exist by name, the goban camera is active, and no input is posted,
// so placement paths stay dormant while init/update/stop run for real.
static bool
run_go_demo_gameplay ()
{
  wsl::das::das_engine engine;
  if (!engine.initialize ()) {
    return false;
  }

  wsl::log::init ();
  wsl::comp::singl::runtime_context runtime ("GoDemoTest", 0, 0, "", true);
  wsl::comp::for_each_type<wsl::comp::component_types>::apply (
      [&]<typename T> () {
        runtime.component_registry ().register_world_component<T> ();
      });

#ifdef WSL_SOURCE_DIR
  const std::filesystem::path go_demo
      = std::filesystem::path (WSL_SOURCE_DIR) / "examples" / "go-demo";
#else
  const std::filesystem::path go_demo
      = std::filesystem::path ("examples") / "go-demo";
#endif
  engine.addFsRoot ("gocomponents", (go_demo / "src" / "components").string ());

  // Register the das-authored MouseRotate component from the real file.
  const auto mouse_rotate_path
      = go_demo / "src" / "components" / "mouse_rotate.das";
  if (!engine.execute_file (mouse_rotate_path)) {
    std::fprintf (stderr, "go-demo MouseRotate execute failed: %s\n",
                  engine.last_error ().c_str ());
    return false;
  }
  auto mr_info = engine.get_struct_info (mouse_rotate_path, "MouseRotate");
  if (mr_info.size_of <= 0 || mr_info.fields.empty ()) {
    std::fprintf (stderr, "go-demo MouseRotate has no reflected fields\n");
    return false;
  }
  const entt::id_type mr_type = entt::hashed_string{ "mouse_rotate" }.value ();
  std::vector<wsl::reg::component_registry::descriptor::das_field> mr_fields;
  for (const auto &field : mr_info.fields) {
    mr_fields.push_back ({ field.name, field.type_name,
                           static_cast<std::uint32_t> (field.offset),
                           static_cast<std::uint32_t> (field.size), field.kind,
                           field.default_value });
  }
  runtime.component_registry ().register_cached_runtime_world_component (
      mr_type, "mouse_rotate", "Mouse Rotate", mr_info.size_of,
      std::move (mr_fields));
  runtime.component_registry ().register_component_type_info (
      "mouse_rotate::MouseRotate const", mr_type,
      wsl::reg::ComponentKind::DAS_SCRIPT,
      static_cast<std::size_t> (mr_info.size_of));

  runtime.set_running (true);
  wsl::rsc::scene &scene
      = runtime.scene_manager ().create_scene ("GoDemoTest", true);
  entt::registry &reg = scene.get_registry ();
  const entt::entity fp_cam = reg.create ();
  reg.emplace<wsl::comp::transform> (fp_cam);
  reg.emplace<wsl::comp::camera> (fp_cam);
  reg.emplace<wsl::comp::world_transform> (fp_cam);
  scene.set_entity_name (fp_cam, "FP Camera");
  const entt::entity goban_cam = reg.create ();
  reg.emplace<wsl::comp::transform> (goban_cam);
  reg.emplace<wsl::comp::camera> (goban_cam);
  reg.emplace<wsl::comp::world_transform> (goban_cam);
  scene.set_entity_name (goban_cam, "Goban Camera");
  const entt::entity player = reg.create ();
  reg.emplace<wsl::comp::transform> (player);
  if (!runtime.component_registry ().das_component_add (reg, mr_type, player)) {
    std::fprintf (stderr, "go-demo MouseRotate attach failed\n");
    return false;
  }
  scene.set_entity_name (player, "Player");
  scene.camera = goban_cam;
  wsl::das::wsl_api_set_active_registry (&reg);

  const auto goban_path = go_demo / "src" / "systems" / "goban_system.das";
  const auto rotate_path
      = go_demo / "src" / "systems" / "mouse_rotate_system.das";
  if (!engine.execute_file (goban_path)) {
    std::fprintf (stderr, "go-demo goban execute failed: %s\n",
                  engine.last_error ().c_str ());
    return false;
  }
  if (!engine.execute_file (rotate_path)) {
    std::fprintf (stderr, "go-demo mouse-rotate execute failed: %s\n",
                  engine.last_error ().c_str ());
    return false;
  }

  std::string error;
  const auto goban_inst
      = engine.instantiate_class (goban_path, "System", error);
  if (goban_inst.ptr == nullptr || goban_inst.info == nullptr
      || goban_inst.ctx == nullptr) {
    std::fprintf (stderr, "go-demo goban instantiate failed: %s\n",
                  error.c_str ());
    return false;
  }
  const auto rotate_inst
      = engine.instantiate_class (rotate_path, "System", error);
  if (rotate_inst.ptr == nullptr || rotate_inst.info == nullptr
      || rotate_inst.ctx == nullptr) {
    std::fprintf (stderr, "go-demo mouse-rotate instantiate failed: %s\n",
                  error.c_str ());
    return false;
  }
  auto goban_adapter = std::make_unique<wsl::das::das_system_adapter> (
      "Goban System", goban_path.string (), engine,
      entt::hashed_string{ "GobanSystem" }.value (), goban_inst.ptr,
      goban_inst.info, goban_inst.ctx);
  auto rotate_adapter = std::make_unique<wsl::das::das_system_adapter> (
      "Mouse Rotate System", rotate_path.string (), engine,
      entt::hashed_string{ "MouseRotateSystem" }.value (), rotate_inst.ptr,
      rotate_inst.info, rotate_inst.ctx);
  wsl::sys::ecs_system &goban_ref
      = scene.add_system_instance (std::move (goban_adapter));
  wsl::sys::ecs_system &rotate_ref
      = scene.add_system_instance (std::move (rotate_adapter));

  scene.resume ();
  if (!goban_ref.is_active () || goban_ref.has_failed ()
      || !rotate_ref.is_active () || rotate_ref.has_failed ()) {
    std::fprintf (stderr, "go-demo systems failed to activate\n");
    return false;
  }
  for (int i = 0; i < 3; ++i) {
    scene.update (1.0 / 60.0);
  }
  if (goban_ref.has_failed () || rotate_ref.has_failed ()) {
    std::fprintf (stderr, "go-demo systems failed during update\n");
    return false;
  }

  wsl::das::wsl_api_set_active_registry (nullptr);
  return true;
}

TEST_CASE ("go-demo gameplay scripts run headless on the new facade")
{
  wsl::das::das_engine::initialize_global ();
  bool ok = false;
  std::thread worker ([&] { ok = run_go_demo_gameplay (); });
  worker.join ();
  CHECK (ok);
}

// Queued `event_post` delivery: posts queue instead of dispatching, the
// pump delivers them FIFO at the next update, posts made during a pump
// roll to the following pump, and engine-owned payloads (Area3D
// `entered`) flow through the same path to `connect_area_entered`
// handlers.
static const char *const k_event_post_das = R"DAS(
options gen2
module post_probe
require weasel

struct Ping {
    seq : int = 0
}

class PostSystem : GameSystem {
    sub : Subscription = Subscription()
    area_sub : Subscription = Subscription()
    frame : int = 0

    def override on_start(var ctx : SystemContext) {
        sub = subscribe_event("Ping", "on_ping")
        var subscribed = sub.is_valid()
        assert(subscribed)
        area_sub = subscribe_area_entered("on_entered")
        var area_ok = area_sub.is_valid()
        assert(area_ok)
    }

    def override update(var ctx : SystemContext; dt : float) {
        frame = frame + 1
        if (frame == 3) {
            var tgt = ctx.world.find_by_name("Target")
            var alive = ctx.world.is_alive(tgt)
            assert(alive)
            var overlap = sensor_overlap(tgt, tgt)
            event_post("entered", overlap)
        }
    }

    def override on_stop(var ctx : SystemContext) {
    }

    def on_ping(ping : Ping) {
        var world = current_world()
        var e = world.create_entity()
        var named = world.set_name(e, "PostedPing{ping.seq}")
        assert(named)
        if (ping.seq == 1) {
            var follow = Ping()
            follow.seq = 2
            event_post("Ping", follow)
        }
    }

    def on_entered(overlap : SensorOverlap) {
        var world = current_world()
        var other = world.sensor_other(overlap)
        if (world.is_alive(other)) {
            var e = world.create_entity()
            var named = world.set_name(e, "PostedOverlap")
            assert(named)
        }
    }
}

def schedule_ping() : void {
    var ping = Ping()
    ping.seq = 1
    event_post("Ping", ping)
}
)DAS";

static bool
has_entity_named (wsl::rsc::scene &scene, const char *name)
{
  for (const auto &[entity, entity_name] : scene.get_entity_names ()) {
    if (entity_name == name) {
      return true;
    }
  }
  return false;
}

static bool
run_event_post ()
{
  wsl::das::das_engine engine;
  if (!engine.initialize ()) {
    return false;
  }

  wsl::log::init ();
  wsl::comp::singl::runtime_context runtime ("PostTest", 0, 0, "", true);
  runtime.set_running (true);
  wsl::rsc::scene &scene
      = runtime.scene_manager ().create_scene ("DasEventPostTest", true);
  entt::registry &reg = scene.get_registry ();
  const entt::entity target = reg.create ();
  scene.set_entity_name (target, "Target");
  wsl::das::wsl_api_set_active_registry (&reg);

  const auto script
      = std::filesystem::temp_directory_path () / "weasel_event_post.das";
  {
    std::ofstream out (script);
    out << k_event_post_das;
  }
  if (!engine.execute_file (script.string ())) {
    std::fprintf (stderr, "event post execute failed: %s\n",
                  engine.last_error ().c_str ());
    return false;
  }

  std::string error;
  auto instance = engine.instantiate_class (script, "PostSystem", error);
  if (instance.ptr == nullptr || instance.info == nullptr
      || instance.ctx == nullptr) {
    std::fprintf (stderr, "event post instantiate failed: %s\n",
                  error.c_str ());
    return false;
  }
  const entt::id_type post_id = entt::hashed_string{ "PostSystem" }.value ();
  auto adapter = std::make_unique<wsl::das::das_system_adapter> (
      "Post", script.string (), engine, post_id, instance.ptr, instance.info,
      instance.ctx);
  wsl::sys::ecs_system &sysref
      = scene.add_system_instance (std::move (adapter));

  scene.resume ();
  if (!sysref.is_active () || sysref.has_failed ()) {
    std::fprintf (stderr, "event post failed to activate\n");
    return false;
  }
  // Queue a ping outside any update: nothing may run synchronously.
  if (!engine.call_void_function_safe (script.string (), "schedule_ping")) {
    std::fprintf (stderr, "event post schedule failed: %s\n",
                  engine.last_error ().c_str ());
    return false;
  }
  if (has_entity_named (scene, "PostedPing1")) {
    std::fprintf (stderr, "event post delivered synchronously\n");
    return false;
  }
  scene.update (1.0 / 60.0);
  if (!has_entity_named (scene, "PostedPing1")) {
    std::fprintf (stderr, "event post was not delivered at the pump\n");
    return false;
  }
  if (has_entity_named (scene, "PostedPing2")) {
    std::fprintf (stderr, "nested post was not deferred to the next pump\n");
    return false;
  }
  scene.update (1.0 / 60.0);
  if (!has_entity_named (scene, "PostedPing2")) {
    std::fprintf (stderr, "nested post was never delivered\n");
    return false;
  }
  // Frame 3 posts an engine-owned overlap from inside update; frame 4
  // delivers it to the connect_area_entered handler.
  scene.update (1.0 / 60.0);
  if (has_entity_named (scene, "PostedOverlap")) {
    std::fprintf (stderr, "overlap post delivered too early\n");
    return false;
  }
  scene.update (1.0 / 60.0);
  if (!has_entity_named (scene, "PostedOverlap")) {
    std::fprintf (stderr, "overlap post was not delivered\n");
    return false;
  }
  {
    auto post_info = engine.get_struct_info (script, "PostSystem");
    int frames = -1;
    for (const auto &field : post_info.fields) {
      if (field.name == "frame" && field.size == sizeof (int)) {
        std::memcpy (&frames,
                     static_cast<const char *> (instance.ptr) + field.offset,
                     sizeof (int));
      }
    }
    if (frames != 4) {
      std::fprintf (stderr, "event post updates did not run: frame=%d\n",
                    frames);
      return false;
    }
  }
  if (sysref.has_failed ()) {
    std::fprintf (stderr, "event post system failed\n");
    return false;
  }

  wsl::das::wsl_api_set_active_registry (nullptr);
  return true;
}

TEST_CASE ("Queued event post delivers at the next pump")
{
  wsl::das::das_engine::initialize_global ();
  bool ok = false;
  std::thread worker ([&] { ok = run_event_post (); });
  worker.join ();
  CHECK (ok);
}

// Showcase example project: the real game files under
// examples/showcase/src/systems run headless against a manually built
// scene (anim/audio state, sensor subscription + synthetic overlap,
// menu bind degradation, transition graceful failures), and the shipped
// showcase.wscn scene file must load through the snapshot serializer.
static const char *const k_showcase_files[] = {
  "anim_demo_system.das",       "audio_demo_system.das",
  "sensor_demo_system.das",     "menu_demo_system.das",
  "transition_demo_system.das",
};
static const char *const k_showcase_names[] = {
  "Anim Demo System", "Audio Demo System",      "Sensor Demo System",
  "Menu Demo System", "Transition Demo System",
};

static bool
run_showcase_gameplay ()
{
  wsl::das::das_engine engine;
  if (!engine.initialize ()) {
    return false;
  }

  wsl::log::init ();
  wsl::comp::singl::runtime_context runtime ("ShowcaseTest", 0, 0, "", true);
  wsl::comp::for_each_type<wsl::comp::component_types>::apply (
      [&]<typename T> () {
        runtime.component_registry ().register_world_component<T> ();
      });

#ifdef WSL_SOURCE_DIR
  const std::filesystem::path showcase
      = std::filesystem::path (WSL_SOURCE_DIR) / "examples" / "showcase";
#else
  const std::filesystem::path showcase
      = std::filesystem::path ("examples") / "showcase";
#endif

  runtime.set_running (true);
  wsl::rsc::scene &scene
      = runtime.scene_manager ().create_scene ("DasShowcaseTest", true);
  entt::registry &reg = scene.get_registry ();
  const entt::entity actor = reg.create ();
  reg.emplace<wsl::comp::transform> (actor);
  reg.emplace<wsl::comp::animator> (actor);
  scene.set_entity_name (actor, "Actor");
  const entt::entity speaker = reg.create ();
  reg.emplace<wsl::comp::transform> (speaker);
  reg.emplace<wsl::comp::audio> (speaker);
  scene.set_entity_name (speaker, "Speaker");
  const entt::entity target = reg.create ();
  scene.set_entity_name (target, "Target");
  wsl::das::wsl_api_set_active_registry (&reg);

  std::vector<wsl::sys::ecs_system *> systems;
  for (int i = 0; i < 5; ++i) {
    const auto path = showcase / "src" / "systems" / k_showcase_files[i];
    if (!engine.execute_file (path.string ())) {
      std::fprintf (stderr, "showcase %s execute failed: %s\n",
                    k_showcase_files[i], engine.last_error ().c_str ());
      return false;
    }
    std::string error;
    auto instance = engine.instantiate_class (path, "System", error);
    if (instance.ptr == nullptr || instance.info == nullptr
        || instance.ctx == nullptr) {
      std::fprintf (stderr, "showcase %s instantiate failed: %s\n",
                    k_showcase_files[i], error.c_str ());
      return false;
    }
    const entt::id_type type_id
        = entt::hashed_string{ k_showcase_files[i] }.value ();
    auto adapter = std::make_unique<wsl::das::das_system_adapter> (
        k_showcase_names[i], path.string (), engine, type_id, instance.ptr,
        instance.info, instance.ctx);
    systems.push_back (&scene.add_system_instance (std::move (adapter)));
  }

  scene.resume ();
  for (auto *sys : systems) {
    if (!sys->is_active () || sys->has_failed ()) {
      std::fprintf (stderr, "showcase system failed to activate\n");
      return false;
    }
  }
  // Sensor demo: queue a synthetic overlap for Target, like the event-post
  // test does; real overlaps arrive from physics in live play.
  {
    const auto path = showcase / "src" / "systems" / "sensor_demo_system.das";
    if (!engine.call_void_function_safe (path.string (),
                                         "debug_post_overlap")) {
      std::fprintf (stderr, "showcase sensor post failed: %s\n",
                    engine.last_error ().c_str ());
      return false;
    }
  }
  for (int i = 0; i < 4; ++i) {
    scene.update (1.0 / 60.0);
  }
  for (auto *sys : systems) {
    if (sys->has_failed ()) {
      std::fprintf (stderr, "showcase system failed during update\n");
      return false;
    }
  }
  bool touched = false;
  bool untouched = false;
  for (const auto &[entity, name] : scene.get_entity_names ()) {
    if (name == "Touched") {
      touched = true;
    }
    if (name == "Untouched") {
      untouched = true;
    }
  }
  if (!touched || untouched) {
    std::fprintf (stderr, "showcase sensor mismatch: touched=%d untouched=%d\n",
                  (int)touched, (int)untouched);
    return false;
  }
  const auto &anim = reg.get<wsl::comp::animator> (actor);
  if (anim.clip_path != "res://rsc/models/Fox_Run.anim.ozz" || anim.playing) {
    std::fprintf (stderr, "showcase animation mismatch: clip='%s' playing=%d\n",
                  anim.clip_path.c_str (), (int)anim.playing);
    return false;
  }
  const auto &audio = reg.get<wsl::comp::audio> (speaker);
  // Volume rides an audio-system event: without the audio system running
  // (headless scene updates drive script systems only) the component keeps
  // its default. The demo asserts the calls succeed; real mixing needs the
  // audio system and an SDL mixer.
  (void)audio;

  // The shipped scene file must load through the real loader.
  {
    wsl::rsc::scene &file_scene
        = runtime.scene_manager ().create_scene ("ShowcaseFile", false);
    wsl::rsc::io::scene_snapshot_serializer loader (&runtime, file_scene);
    if (!loader.load_json (
            (showcase / "rsc" / "scenes" / "showcase.wscn").string ())) {
      std::fprintf (stderr, "showcase.wscn failed to load\n");
      return false;
    }
    bool has_actor = false;
    bool has_animator = false;
    bool has_audio = false;
    for (const auto &[entity, name] : file_scene.get_entity_names ()) {
      if (name == "Actor") {
        has_actor = true;
        if (auto *anim
            = file_scene.get_registry ().try_get<wsl::comp::animator> (
                entity)) {
          has_animator
              = anim->clip_path == "res://rsc/models/Fox_Walk.anim.ozz";
        }
      }
      if (name == "Speaker") {
        if (file_scene.get_registry ().try_get<wsl::comp::audio> (entity)
            != nullptr) {
          has_audio = true;
        }
      }
    }
    if (!has_actor || !has_animator || !has_audio) {
      std::fprintf (
          stderr, "showcase.wscn content mismatch: actor=%d anim=%d audio=%d\n",
          (int)has_actor, (int)has_animator, (int)has_audio);
      return false;
    }
  }

  wsl::das::wsl_api_set_active_registry (nullptr);
  return true;
}

TEST_CASE ("showcase example project runs headless on the new facade")
{
  wsl::das::das_engine::initialize_global ();
  bool ok = false;
  std::thread worker ([&] { ok = run_showcase_gameplay (); });
  worker.join ();
  CHECK (ok);
}

#else  // !WEASEL_HAS_DASLANG

TEST_CASE ("Component accessors (daslang disabled)")
{
  MESSAGE ("daslang support not enabled; skipping component accessors test.");
  CHECK (true);
}

// ---------------------------------------------------------------------------
#endif // WEASEL_HAS_DASLANG
