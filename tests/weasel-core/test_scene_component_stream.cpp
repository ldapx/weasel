// Part of weasel_core_tests; the doctest main lives in test_event_bus.cpp.
//
// A scene file is a single JSON document whose components are keyed by type
// name. That shape is what these tests pin down:
//
//   * the file is one parseable JSON value that any tool can read,
//   * every component round-trips through it,
//   * a component the current build does not know is skipped without
//     disturbing the ones around it, and
//   * a file in the retired newline-delimited format is rejected rather than
//     silently half-loaded.

#include "doctest.h"

#include "wsl/comp/components.hpp"
#include "wsl/comp/singl/runtime_context.hpp"
#include "wsl/reg/component_registry.hpp"
#include "wsl/reg/singleton_registry.hpp"
#include "wsl/rsc/scene.hpp"
#include "wsl/rsc/project_loader.hpp"
#include "wsl/rsc/scene_snapshot_serializer.hpp"
#include "wsl/serialize/serialize.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <entt/entity/fwd.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace
{

namespace json_document = wsl::rsc::json_document;

void
register_world_components (wsl::comp::singl::runtime_context &rtc)
{
  using namespace wsl::comp;

  register_component_meta<
      hierarchy, world_transform, transform, model_instance_3d, camera,
      camera_2d, point_light, spot_light, directional_light, rigid_body, area,
      character_body, audio, prefab_instance, sprite_2d, subviewport,
      transform_2d, animator, skeleton_pose> ();

  for_each_type<component_types>::apply ([&rtc]<typename T> () {
    rtc.component_registry ().register_world_component<T> ();
  });
}

/** Fills the scene with data spread across the component block. */
entt::entity
populate (wsl::rsc::scene &scene)
{
  entt::registry &registry = scene.get_registry ();
  entt::entity entity = registry.create ();
  scene.set_entity_name (entity, "Subject");

  registry.emplace<wsl::comp::hierarchy> (entity);
  registry.emplace<wsl::comp::world_transform> (entity);

  wsl::comp::transform transform;
  transform.position = { 1.0F, 2.0F, 3.0F };
  registry.emplace<wsl::comp::transform> (entity, transform);

  wsl::comp::model_instance_3d model_instance;
  model_instance.scene_index = 2;
  registry.emplace<wsl::comp::model_instance_3d> (entity, model_instance);

  wsl::comp::camera camera;
  camera.fov () = 0.5F;
  registry.emplace<wsl::comp::camera> (entity, camera);

  return entity;
}

std::string
read_file (const std::string &path)
{
  std::ifstream file (path);
  return std::string ((std::istreambuf_iterator<char> (file)),
                      std::istreambuf_iterator<char> ());
}

std::filesystem::path
write_temp (const std::string &contents)
{
  std::filesystem::path const path
      = std::filesystem::temp_directory_path ()
        / "weasel_scene_component_stream_test.wscn";
  std::ofstream file (path);
  file << contents;
  file.close ();
  return path;
}

/** Asserts the entity came back with every component the stream described. */
void
check_loaded (wsl::rsc::scene &scene, entt::entity entity)
{
  entt::registry &registry = scene.get_registry ();

  REQUIRE (registry.valid (entity));
  CHECK (scene.get_entity_name (entity) == "Subject");
  CHECK (registry.all_of<wsl::comp::transform> (entity));
  CHECK (registry.all_of<wsl::comp::model_instance_3d> (entity));
  CHECK (registry.all_of<wsl::comp::camera> (entity));
  CHECK (registry.all_of<wsl::comp::hierarchy> (entity));

  const wsl::comp::transform &transform
      = registry.get<wsl::comp::transform> (entity);
  CHECK (transform.position.x () == doctest::Approx (1.0F));
  CHECK (transform.position.z () == doctest::Approx (3.0F));

  // Values that sit in different slots of the component block prove the
  // documents were consumed by the right component types.
  CHECK (registry.get<wsl::comp::model_instance_3d> (entity).scene_index == 2);
  CHECK (registry.get<wsl::comp::camera> (entity).fov ()
         == doctest::Approx (0.5F));
}

} // namespace

TEST_CASE ("scene headers stay readable when fields are added later")
{
  // Regression: rfl treats plain struct fields as *required*, so adding a
  // field to scene_header would make previously saved scenes fail to parse,
  // silently producing scenes with no entities at all. The header contract is
  // therefore frozen.
  std::string const legacy_header
      = R"({"header":{"scene_name":"Main Scene","is_prefab":false,"systems":[],"entity_names":[[0,"Cube"]],"connections":[],"autoload":[],"camera":0},"entities":[{"id":0}]})";

  struct scene_data
  {
    wsl::rsc::io::scene_header header;
    std::vector<entt::entity> entities;
  };

  scene_data data;
  std::string error;
  REQUIRE (wsl::serialize::json_read (legacy_header, data, &error));
  CHECK (error.empty ());
  CHECK (data.header.scene_name == "Main Scene");
  REQUIRE (data.header.entity_names.size () == 1);
  CHECK (data.header.entity_names[0].second == "Cube");
  REQUIRE (data.entities.size () == 1);
}

TEST_CASE ("saved scenes are a single valid JSON document")
{
  // Scenes used to be a newline-delimited *stream* of JSON documents, which
  // no JSON tool could parse: `jq`, `json.tool` and every diff view failed on
  // a scene file. The format is now one self-describing object whose
  // components are keyed by type name.
  wsl::comp::singl::runtime_context rtc ("SceneJson", 0, 0, "", true);
  register_world_components (rtc);

  wsl::rsc::scene scene (&rtc, nullptr, "Json Test");
  entt::entity const entity = populate (scene);
  scene.get_registry ().emplace<wsl::comp::animator> (entity);

  std::filesystem::path const path = std::filesystem::temp_directory_path ()
                                     / "weasel_scene_single_document.wscn";
  {
    wsl::rsc::io::scene_snapshot_serializer saver (&rtc, scene);
    REQUIRE (saver.save_json (path.string ()));
  }

  const std::string content = read_file (path.string ());

  // A single JSON value, and nothing after it.
  std::vector<json_document::member> members;
  REQUIRE_FALSE (json_document::object_members (content).empty ());
  REQUIRE (json_document::value_end (content, 0) == content.size ());

  // Parses as one document, and describes itself.
  struct scene_envelope
  {
    uint32_t format_version;
    wsl::rsc::io::scene_header header;
    std::vector<entt::entity> entities;
  };
  scene_envelope parsed;
  std::string error;
  REQUIRE (wsl::serialize::json_read (content, parsed, &error));
  CHECK (error.empty ());
  CHECK (parsed.format_version >= 2);
  CHECK (parsed.header.scene_name == "Json Test");
  CHECK (parsed.entities.size () == 1);

  // Components are keyed by type name, so a reader can find one directly
  // instead of replaying a positional document stream.
  const std::vector<json_document::member> top
      = json_document::object_members (content);
  const json_document::member *components_member = nullptr;
  for (const json_document::member &member : top) {
    if (member.key == "components") {
      components_member = &member;
    }
  }
  REQUIRE (components_member != nullptr);
  const std::vector<json_document::member> components
      = json_document::object_members (components_member->value);

  bool saw_transform = false;
  for (const json_document::member &member : components) {
    if (member.key == "wsl::comp::transform") {
      saw_transform = true;
      // Component documents wrap each instance in an {entity_id, data} pair,
      // so the value is that wrapper list rather than a bare component array.
      struct transform_entry
      {
        entt::entity entity_id;
        wsl::comp::transform data;
      };
      std::vector<transform_entry> entries;
      REQUIRE (wsl::serialize::json_read (member.value, entries));
      REQUIRE (entries.size () == 1);
      CHECK (entries[0].data.position.x () == doctest::Approx (1.0F));
      CHECK (entries[0].data.position.z () == doctest::Approx (3.0F));
    }
  }
  CHECK (saw_transform);

  // And it round-trips.
  wsl::rsc::io::scene_snapshot_serializer loader (&rtc, scene);
  REQUIRE (loader.load_json (path.string ()));
  std::filesystem::remove (path);

  check_loaded (scene, entity);
  CHECK (scene.get_registry ().all_of<wsl::comp::animator> (entity));
}

TEST_CASE ("a single-document scene ignores components the build does not know")
{
  // Self-describing keys mean an unknown component is a no-op rather than a
  // stream misalignment: the entries after it must still load.
  wsl::comp::singl::runtime_context rtc ("SceneJson", 0, 0, "", true);
  register_world_components (rtc);

  wsl::rsc::scene scene (&rtc, nullptr, "Json Test");
  entt::entity const entity = populate (scene);

  std::string document
      = R"({"format_version":2,"header":{"scene_name":"Json Test",)"
        R"("is_prefab":false,"systems":[],"entity_names":[[0,"Subject"]],)"
        R"("connections":[],"autoload":[],"camera":4294967295},)"
        R"("entities":[{"id":0}],"components":{)"
        R"("wsl::comp::a_component_from_the_future":[],)"
        R"("wsl::comp::transform":[{"entity_id":{"id":0},)"
        R"("data":{"position":{"x":1.0,"y":2.0,"z":3.0},)"
        R"("rotation":{"w":1.0,"x":0.0,"y":0.0,"z":0.0},)"
        R"("scale":{"x":1.0,"y":1.0,"z":1.0}}}]},)"
        R"("das_components":[],"singletons":{}})";

  std::filesystem::path const path = std::filesystem::temp_directory_path ()
                                     / "weasel_scene_unknown_component.wscn";
  std::ofstream file (path);
  file << document;
  file.close ();

  wsl::rsc::io::scene_snapshot_serializer loader (&rtc, scene);
  REQUIRE (loader.load_json (path.string ()));
  std::filesystem::remove (path);

  // `transform` came *after* the unknown component and still loaded correctly.
  CHECK (scene.get_registry ().valid (entity));
  REQUIRE (scene.get_registry ().all_of<wsl::comp::transform> (entity));
  CHECK (scene.get_registry ().get<wsl::comp::transform> (entity).position.z ()
         == doctest::Approx (3.0F));
}

TEST_CASE ("scene file names normalize to .wscn")
{
  namespace scene_file = wsl::rsc::scene_file;

  CHECK (scene_file::strip_extension ("main.wscn") == "main");
  CHECK (scene_file::strip_extension ("level1") == "level1");
  CHECK (scene_file::with_extension ("level1") == "level1.wscn");
  CHECK (scene_file::with_extension ("level1.wscn") == "level1.wscn");
  // Only `.wscn` is recognized; any other suffix is part of the stem.
  CHECK (scene_file::strip_extension ("level1.json") == "level1.json");
}

TEST_CASE ("project root paths normalize")
{
  namespace fs = std::filesystem;

  // The bug this pins down: `fs::absolute()` makes a path absolute but leaves
  // `.` and `..` in it, so a manifest created from `./orbhunt/OrbHunt` recorded
  // `.../examples/./orbhunt/OrbHunt`. That string then compared unequal to the
  // same directory reached another way.
  const fs::path base = fs::temp_directory_path ();

  // Existing directory, spelled messily.
  const fs::path existing = base / "weasel_normalize_probe";
  fs::create_directories (existing);
  const std::string messy
      = wsl::rsc::project_path::normalize (existing.string () + "/./sub/../");
  CHECK (messy == wsl::rsc::project_path::normalize (existing.string ()));
  CHECK (messy.find ("/./") == std::string::npos);
  CHECK (messy.find ("/../") == std::string::npos);
  // No trailing separator, so it compares equal to the plain directory string.
  CHECK (messy == existing.string ());
  fs::remove_all (existing);

  // A path that does not exist yet must still normalize -- `fs::canonical`
  // would fail, and this is called before the project directory is created.
  const fs::path absent = base / "weasel_normalize_absent" / "a" / ".." / "b";
  const std::string absent_normalized
      = wsl::rsc::project_path::normalize (absent);
  CHECK (absent_normalized
         == (base / "weasel_normalize_absent" / "b").string ());

  // Relative input becomes absolute rather than staying relative.
  const std::string cwd_relative
      = wsl::rsc::project_path::normalize (fs::current_path () / "." / "x");
  CHECK (fs::path (cwd_relative).is_absolute ());
  CHECK (cwd_relative.find ("/./") == std::string::npos);

  // A trailing separator never survives. `weakly_canonical` normalises an
  // interior `.` to `/./` on its own, so a bare "." tail has to be stripped
  // too -- that is the exact shape the original `root_path` had
  // (`.../OrbHunt/.`).
  CHECK (wsl::rsc::project_path::normalize (base.string () + "/")
         == wsl::rsc::project_path::normalize (base.string ()));
  CHECK (wsl::rsc::project_path::normalize (base.string () + "/.")
         == wsl::rsc::project_path::normalize (base.string ()));
  CHECK (wsl::rsc::project_path::normalize (existing.string () + "/./")
         == existing.string ());
}

TEST_CASE ("a scene in the retired stream format is rejected")
{
  // The old format was a stream of newline-delimited documents. It is no longer
  // read: guessing which document maps to which component is exactly the
  // fragility the keyed format removed, so a stale file must fail loudly rather
  // than load as an empty scene.
  wsl::comp::singl::runtime_context rtc ("SceneStream", 0, 0, "", true);
  register_world_components (rtc);

  // Named differently from the retired file so the load's effect (or lack of
  // one) on the scene name is observable.
  wsl::rsc::scene scene (&rtc, nullptr, "Target");
  entt::entity const entity = populate (scene);

  const std::string retired
      = "{\"header\":{\"scene_name\":\"Stream Test\",\"is_prefab\":false,"
        "\"systems\":[],\"entity_names\":[[0,\"Subject\"]],\"connections\":[],"
        "\"autoload\":[],\"camera\":4294967295},\"entities\":[{\"id\":0}]}\n"
        "[{\"entity_id\":{\"id\":0},\"data\":{\"position\":{\"x\":1.0,"
        "\"y\":2.0,\"z\":3.0},\"rotation\":{\"w\":1.0,\"x\":0.0,\"y\":0.0,"
        "\"z\":0.0},\"scale\":{\"x\":1.0,\"y\":1.0,\"z\":1.0}}}]";

  std::filesystem::path const path = write_temp (retired);
  wsl::rsc::io::scene_snapshot_serializer loader (&rtc, scene);
  REQUIRE (loader.load_json (path.string ()) == false);
  std::filesystem::remove (path);

  // Nothing was loaded: the header was ignored (the scene was not renamed to
  // "Stream Test") and the entity has no transform.
  CHECK (scene.get_name () != "Stream Test");
  CHECK (scene.get_registry ().all_of<wsl::comp::transform> (entity) == false);
}

TEST_CASE ("saved scenes reload every component")
{
  wsl::comp::singl::runtime_context rtc ("SceneStream", 0, 0, "", true);
  register_world_components (rtc);

  wsl::rsc::scene scene (&rtc, nullptr, "Stream Test");
  entt::entity const entity = populate (scene);
  scene.get_registry ().emplace<wsl::comp::animator> (entity);

  std::filesystem::path const path
      = std::filesystem::temp_directory_path () / "weasel_scene_roundtrip.wscn";

  {
    wsl::rsc::io::scene_snapshot_serializer saver (&rtc, scene);
    REQUIRE (saver.save_json (path.string ()));
  }

  wsl::rsc::io::scene_snapshot_serializer loader (&rtc, scene);
  REQUIRE (loader.load_json (path.string ()));
  std::filesystem::remove (path);

  check_loaded (scene, entity);
  CHECK (scene.get_registry ().all_of<wsl::comp::animator> (entity));
}

// ── Daslang singletons ──
//
// A Daslang singleton has no C++ type, so its bytes live in a type-erased
// storage attached to the registry context. These tests pin the behaviour the
// CLI depends on: construction from declared initialisers, presence checks,
// removal, and a round trip through the scene file.

namespace
{

constexpr entt::id_type das_singleton_type_id = 0x00DA5601U;

/** Registers a fake daslang singleton with a 9-byte layout. */
wsl::reg::singleton_registry::descriptor
register_fake_das_singleton (wsl::reg::singleton_registry &registry)
{
  std::vector<wsl::reg::singleton_registry::descriptor::das_field> fields;
  // int at 0, float at 4, bool at 8.
  fields.push_back ({ "score",
                      "int",
                      0,
                      4,
                      wsl::das::das_engine::field_type_kind::integer,
                      { 0x2A, 0x00, 0x00, 0x00 } });
  const float one_point_five = 1.5F;
  std::vector<uint8_t> float_bytes (sizeof (float));
  std::memcpy (float_bytes.data (), &one_point_five, sizeof (float));
  fields.push_back ({ "ratio", "float", 4, 4,
                      wsl::das::das_engine::field_type_kind::floating,
                      float_bytes });
  fields.push_back ({ "running",
                      "bool",
                      8,
                      1,
                      wsl::das::das_engine::field_type_kind::boolean,
                      { 1 } });

  registry.register_cached_runtime_singleton_component (
      das_singleton_type_id, "game_state", "Game State", 9, std::move (fields));
  return *registry.find_singleton_component (das_singleton_type_id);
}

} // namespace

TEST_CASE ("a daslang singleton descriptor carries its value layout")
{
  wsl::comp::singl::runtime_context rtc ("SceneStream", 0, 0, "", true);
  wsl::reg::singleton_registry &registry = rtc.singleton_registry ();

  const wsl::reg::singleton_registry::descriptor desc
      = register_fake_das_singleton (registry);

  CHECK (desc.is_das_singleton);
  CHECK (desc.das_struct_size == 9);
  REQUIRE (desc.das_fields.size () == 3);
  CHECK (desc.das_fields[0].name == "score");
  CHECK (desc.das_fields[0].offset == 0);
  CHECK (desc.das_fields[2].name == "running");
  CHECK (desc.das_fields[2].offset == 8);
  // Discovery-only registration: no layout, so not addable.
  CHECK (desc.can_add_default);
}

TEST_CASE ("adding a daslang singleton applies the declared initialisers")
{
  wsl::comp::singl::runtime_context rtc ("SceneStream", 0, 0, "", true);
  wsl::reg::singleton_registry &registry = rtc.singleton_registry ();
  register_fake_das_singleton (registry);

  entt::registry scene_registry;
  CHECK (registry.das_singleton_contains (scene_registry, das_singleton_type_id)
         == false);
  CHECK (registry.das_singleton_add (scene_registry, das_singleton_type_id));

  CHECK (
      registry.das_singleton_contains (scene_registry, das_singleton_type_id));

  const uint8_t *bytes
      = registry.das_singleton_data (scene_registry, das_singleton_type_id);
  REQUIRE (bytes != nullptr);

  int32_t score = 0;
  std::memcpy (&score, bytes, sizeof (score));
  CHECK (score == 42);

  float ratio = 0.0F;
  std::memcpy (&ratio, bytes + 4, sizeof (ratio));
  CHECK (ratio == 1.5F);

  CHECK (bytes[8] == 1);

  // Adding twice is refused rather than re-zeroing the value.
  CHECK (registry.das_singleton_add (scene_registry, das_singleton_type_id)
         == false);

  CHECK (registry.das_singleton_remove (scene_registry, das_singleton_type_id));
  CHECK (registry.das_singleton_contains (scene_registry, das_singleton_type_id)
         == false);
  CHECK (registry.das_singleton_remove (scene_registry, das_singleton_type_id)
         == false);
}

TEST_CASE ("das singleton layout changes preserve matching fields")
{
  wsl::comp::singl::runtime_context rtc ("SceneStream", 0, 0, "", true);
  wsl::reg::singleton_registry &registry = rtc.singleton_registry ();
  register_fake_das_singleton (registry);

  entt::registry scene_registry;
  REQUIRE (registry.das_singleton_add (scene_registry, das_singleton_type_id));
  uint8_t *old_bytes
      = registry.das_singleton_data (scene_registry, das_singleton_type_id);
  REQUIRE (old_bytes != nullptr);
  const int32_t score = 99;
  const float ratio = 2.25F;
  std::memcpy (old_bytes, &score, sizeof (score));
  std::memcpy (old_bytes + 4, &ratio, sizeof (ratio));
  old_bytes[8] = 0;

  const std::int32_t level_default = 7;
  const std::int32_t score_default = 42;
  const float ratio_default = 1.5F;
  std::vector<wsl::reg::singleton_registry::descriptor::das_field> fields;
  fields.push_back ({ "level", "int", 0, 4,
                      wsl::das::das_engine::field_type_kind::integer,
                      std::vector<uint8_t> (sizeof (level_default)) });
  fields.push_back ({ "score", "int", 4, 4,
                      wsl::das::das_engine::field_type_kind::integer,
                      std::vector<uint8_t> (sizeof (score_default)) });
  fields.push_back ({ "ratio", "float", 8, 4,
                      wsl::das::das_engine::field_type_kind::floating,
                      std::vector<uint8_t> (sizeof (ratio_default)) });
  fields.push_back ({ "running", "bool", 12, 1,
                      wsl::das::das_engine::field_type_kind::boolean, { 1 } });
  std::memcpy (fields[0].default_value.data (), &level_default,
               sizeof (level_default));
  std::memcpy (fields[1].default_value.data (), &score_default,
               sizeof (score_default));
  std::memcpy (fields[2].default_value.data (), &ratio_default,
               sizeof (ratio_default));

  REQUIRE (registry.register_cached_runtime_singleton_component (
      das_singleton_type_id, "game_state", "Game State", 13,
      std::move (fields)));
  const uint8_t *migrated
      = registry.das_singleton_data (scene_registry, das_singleton_type_id);
  REQUIRE (migrated != nullptr);

  int32_t level = 0;
  int32_t migrated_score = 0;
  float migrated_ratio = 0.0F;
  std::memcpy (&level, migrated, sizeof (level));
  std::memcpy (&migrated_score, migrated + 4, sizeof (migrated_score));
  std::memcpy (&migrated_ratio, migrated + 8, sizeof (migrated_ratio));
  CHECK (level == level_default);
  CHECK (migrated_score == score);
  CHECK (migrated_ratio == ratio);
  CHECK (migrated[12] == 0);
}

TEST_CASE ("das singleton hex round trips and rejects bad input")
{
  wsl::comp::singl::runtime_context rtc ("SceneStream", 0, 0, "", true);
  wsl::reg::singleton_registry &registry = rtc.singleton_registry ();
  register_fake_das_singleton (registry);

  entt::registry source;
  REQUIRE (registry.das_singleton_add (source, das_singleton_type_id));
  uint8_t *bytes = registry.das_singleton_data (source, das_singleton_type_id);
  REQUIRE (bytes != nullptr);
  bytes[0] = 0x63; // 99

  const std::string hex
      = registry.das_singleton_hex (source, das_singleton_type_id);
  CHECK (hex.size () == 18); // 9 bytes

  entt::registry target;
  CHECK (registry.das_singleton_load_hex (target, das_singleton_type_id, hex));
  const uint8_t *restored
      = registry.das_singleton_data (target, das_singleton_type_id);
  REQUIRE (restored != nullptr);
  CHECK (restored[0] == 0x63);
  CHECK (hex == registry.das_singleton_hex (target, das_singleton_type_id));

  // Wrong length, odd length and non-hex are all refused.
  CHECK (registry.das_singleton_load_hex (target, das_singleton_type_id, "")
         == false);
  CHECK (registry.das_singleton_load_hex (target, das_singleton_type_id, "abcd")
         == false);
  CHECK (registry.das_singleton_load_hex (target, das_singleton_type_id,
                                          std::string (18, 'z'))
         == false);
  // An unknown type id never creates storage.
  entt::registry other;
  CHECK (registry.das_singleton_load_hex (other, 0x00BADBADU, hex) == false);
  CHECK (registry.das_singleton_contains (other, 0x00BADBADU) == false);
}

TEST_CASE ("a daslang singleton survives a scene save and reload")
{
  wsl::comp::singl::runtime_context rtc ("SceneStream", 0, 0, "", true);
  register_world_components (rtc);
  wsl::reg::singleton_registry &registry = rtc.singleton_registry ();
  register_fake_das_singleton (registry);

  wsl::rsc::scene scene (&rtc, nullptr, "Das Singleton");
  populate (scene);

  entt::registry &scene_registry = scene.get_registry ();
  REQUIRE (registry.das_singleton_add (scene_registry, das_singleton_type_id));
  uint8_t *bytes
      = registry.das_singleton_data (scene_registry, das_singleton_type_id);
  REQUIRE (bytes != nullptr);
  bytes[0] = 0x07;

  const std::filesystem::path path = std::filesystem::temp_directory_path ()
                                     / "weasel_scene_das_singleton.wscn";

  {
    wsl::rsc::io::scene_snapshot_serializer saver (&rtc, scene);
    REQUIRE (saver.save_json (path.string ()));
  }

  // The payload rides in the `singletons` object as a typed JSON object.
  const std::string content = read_file (path.string ());
  const std::vector<json_document::member> top
      = json_document::object_members (content);
  const json_document::member *singletons
      = json_document::find (top, "singletons");
  REQUIRE (singletons != nullptr);
  const json_document::member *entry = json_document::find (
      json_document::object_members (singletons->value), "game_state");
  REQUIRE (entry != nullptr);
  // Field-by-field JSON (Phase 3), not the old quoted hex payload.
  REQUIRE (!entry->value.empty ());
  CHECK (entry->value.front () == '{');
  CHECK (entry->value.back () == '}');
  CHECK (entry->value.find ("score") != std::string::npos);

  REQUIRE (
      registry.das_singleton_remove (scene_registry, das_singleton_type_id));

  wsl::rsc::io::scene_snapshot_serializer loader (&rtc, scene);
  REQUIRE (loader.load_json (path.string ()));
  std::filesystem::remove (path);

  CHECK (
      registry.das_singleton_contains (scene_registry, das_singleton_type_id));
  const uint8_t *restored
      = registry.das_singleton_data (scene_registry, das_singleton_type_id);
  REQUIRE (restored != nullptr);
  CHECK (restored[0] == 0x07);
  CHECK (restored[8] == 1); // the `running` initialiser
}
