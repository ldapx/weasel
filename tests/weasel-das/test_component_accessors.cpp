#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#ifdef WEASEL_HAS_DASLANG

#include "wsl/das/das_engine.hpp"
#include "wsl/das/wsl_api_module.hpp"
#include "wsl/comp/animator.hpp"
#include "wsl/comp/camera_2d.hpp"
#include "wsl/comp/directional_light.hpp"
#include "wsl/comp/point_light.hpp"
#include "wsl/comp/rigid_body.hpp"
#include "wsl/comp/spot_light.hpp"
#include "wsl/comp/sprite_2d.hpp"
#include "wsl/comp/transform.hpp"
#include "wsl/comp/transform_2d.hpp"
#include "wsl/comp/singl/runtime_context.hpp"
#include "wsl/reg/component_registry.hpp"
#include "wsl/math/vector.hpp"
#include "wsl/log/log.hpp"
#include "wsl/rsc/scene.hpp"
#include "wsl/rsc/scene_snapshot_serializer.hpp"

#include <cmath>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>

namespace
{

const char *const k_smoke_das = R"DAS(
options gen2
require weasel_api
require weasel_helpers

def smoke_test() {
    var e : uint = 0u

    var t = get_component_or(e, Transform())
    t.scale.x = 2.0
    t.position.y = 3.5
    t.position.z = -1.0
    t.scale = float3(4.0, 5.0, 6.0)
    t.position = float3(1.0, 2.0, 3.0)

    var t2 = get_component_or(e, Transform2D())
    t2.rotation = 0.5
    t2.scale = float2(2.0, 3.0)

    var cam = get_component_or(e, Camera2D())
    cam.zoom = 2.25

    var spr = get_component_or(e, Sprite2D())
    spr.color.w = 1.0
    spr.size.x = 10.0
    spr.size.y = 20.0
    spr.color = float4(0.25, 0.5, 0.75, 1.0)

    var pl = get_component_or(e, PointLight())
    pl.intensity = 5.0
    pl.color = float3(0.1, 0.2, 0.3)

    var dl = get_component_or(e, DirectionalLight())
    dl.intensity = 3.0

    var sl = get_component_or(e, SpotLight())
    sl.intensity = 4.0

    var missing = get_component_or(1u, Transform())
    missing.position.x = 99.0

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
require weasel_api
require weasel_helpers

def main() {
    var e : uint = 0u
    var t = get_component_or(e, Transform())
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
require weasel_api
require weasel_helpers

def main() {
    var e : uint = 0u
    var t = get_component_or(e, Transform())
    t.scale.x = 2.0
    t.scale.y = 2.0
    t.scale.z = 3.0
}

def smoke_test() {
    var e : uint = 0u
    var t = get_component_or(e, Transform())
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

// `query() $(t : Transform&) { ... }` — DECS-style iteration over
// the entt registry via the `query` call macro. Exercises the macro
// expansion (block params -> live proxies) AND the `each_entity_id_with`
// primitive (only entities owning ALL listed components are visited).
static const char *const k_query_das = R"DAS(
options gen2
require weasel_api
require weasel_helpers

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
require weasel_api
require weasel_helpers

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
    fields.push_back ({
        field.name, field.type_name,
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

// Generic Daslang-component accessor (`get_component_or` value copy),
// `has_component` / `add_component` / `remove_component` typed overloads.
static const char *const k_generic_das_das = R"DAS(
options gen2
module mouse_rotate
require weasel_api
require weasel_helpers

struct MouseRotate {
    yaw : float = 11.0
}

def accessor_test() : void {
    // Present: the C++ harness wrote 42.0 into the live pool bytes, so the
    // accessor must return the pooled value, not the daslang default (11.0).
    var p = get_component_or(0u, MouseRotate())
    assert(p.yaw == 42.0)
    // Absent: must return the daslang default without inserting a component.
    var a = get_component_or(1u, MouseRotate())
    assert(a.yaw == 11.0)
    let hp = has_component(0u, MouseRotate())
    assert(hp)
    let ha = has_component(1u, MouseRotate())
    assert(!ha)
    let added = add_component(2u, MouseRotate())
    assert(added)
    let h2 = has_component(2u, MouseRotate())
    assert(h2)
    let removed = remove_component(2u, MouseRotate())
    assert(removed)
    let h3 = has_component(2u, MouseRotate())
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
    fields.push_back ({
        field.name, field.type_name,
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
require weasel_api
require weasel_helpers

def mutate_in_query() : void {
    query() $(t : Transform&) {
        entity_destroy(e)
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
    fields.push_back ({
        field.name, field.type_name,
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
require weasel_api
require weasel_helpers

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
require weasel_api
require weasel_helpers

struct MouseRotate { yaw : float = 0.0 }

def iso_test() : void {
    var p = get_component_or(0u, MouseRotate())
    assert(p.yaw == 7.0)
    var none = get_component_or(1u, MouseRotate())
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
require weasel_api
require weasel_helpers

struct MouseRotate { yaw : float = 0.0 }

def recycle_test() : void {
    var p = get_component_or(0u, MouseRotate())
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
require weasel_api
require weasel_helpers

struct MouseRotate { yaw : float = 0.0 }

def diag_test() : void {
    let h = has_component(99999u, MouseRotate())
    assert(!h)
    var d = get_component_or(99999u, MouseRotate())
    assert(d.yaw == 0.0)
    var count = 0
    query() $(t : Transform&) { count += 1 }
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
require weasel_api
require weasel_helpers

def builtin_view_test() : void {
    var rb = get_component_or(0u, RigidBody())
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
require weasel_api
require weasel_helpers

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
require weasel_api

def run_anim() {
    // Fresh registry: the first entity is id 0 (asserted below).
    let entity = 0u

    anim_play(entity, "res://rsc/models/Fox_Walk.anim.ozz")
    anim_set_speed(entity, 1.5)
    anim_set_time(entity, 0.75)
    anim_set_loop(entity, false)
    anim_crossfade(entity, "res://rsc/models/Fox_Run.anim.ozz", 0.5)
    anim_set_time(entity, -3.0)
    anim_crossfade(entity, "res://rsc/models/Fox_Survey.anim.ozz", -1.0)
    // No-ops: that entity has no Animator, and this id does not exist.
    anim_play(entity + 1000, "res://rsc/models/Fox_Walk.anim.ozz")
    anim_set_speed(entity + 1000, 9.0)
    anim_stop(entity)
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

#else  // !WEASEL_HAS_DASLANG

TEST_CASE ("Component accessors (daslang disabled)")
{
  MESSAGE ("daslang support not enabled; skipping component accessors test.");
  CHECK (true);
}

// ---------------------------------------------------------------------------
#endif // WEASEL_HAS_DASLANG
