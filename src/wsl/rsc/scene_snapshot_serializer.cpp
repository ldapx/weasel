#include "scene_snapshot_serializer.hpp"

#include "../log/log.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <entt/core/fwd.hpp>
#include <unordered_set>
#include <entt/entity/entity.hpp>
#include <entt/entity/fwd.hpp>
#include <exception>
#include <fstream>
#include <glm/ext/vector_float3.hpp>
#include <ios>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "../comp/transform.hpp"
#include "../comp/singl/physics_manager.hpp"
#include "../comp/singl/runtime_context.hpp"
#include "../comp/area3d.hpp"
#include "../comp/character_body.hpp"
#include "../comp/rigid_body.hpp"
#include "../comp/world_transform.hpp"
#include "../phys/physics_engine.hpp"
#include "../reg/component_registry.hpp"
#include "resource_manager.hpp"
#include "resource_ref.hpp"
#include "scene.hpp"
#include "../reg/singleton_registry.hpp"
#include "../reg/system_factory_registry.hpp"
#include "../serialize/serialize.hpp"
#include "../sys/system.hpp"

namespace wsl
{

namespace rsc
{

namespace io
{

namespace
{

/** Version stamped on the scene format written by `save`. */
constexpr uint32_t scene_document_format_version = 2;

/** Escapes \p text for use as a JSON string, quotes included. */
std::string
json_quote (std::string_view text)
{
  std::string out;
  out.reserve (text.size () + 2);
  out += '"';
  for (const char c : text) {
    switch (c) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      if (static_cast<unsigned char> (c) < 0x20) {
        static constexpr char digits[] = "0123456789abcdef";
        out += "\\u00";
        out += digits[(c >> 4) & 0x0f];
        out += digits[c & 0x0f];
      } else {
        out += c;
      }
      break;
    }
  }
  out += '"';
  return out;
}

/**
 * Serializes one component and returns its raw JSON text.
 *
 * Components without a serializer produce `null`, which is also how the loader
 * spells "this build does not implement that component".
 */
std::string
component_document (reg::component_registry &components,
                    entt::registry &registry, entt::id_type type_id)
{
  serialize::json_writer component_writer;
  if (components.save_world_component_json (component_writer, registry, type_id)
      && !component_writer.json.empty ()) {
    return std::move (component_writer.json);
  }
  return "null";
}

/** Appends `"key": <payload>` to an object body, comma-separated. */
void
append_member (std::string &object, bool &first, std::string_view key,
               std::string_view payload)
{
  if (!first) {
    object += ',';
  }
  first = false;
  object += json_quote (key);
  object += ':';
  object += payload;
}

} // namespace

scene_snapshot_serializer::scene_snapshot_serializer (
    comp::singl::runtime_context *runtime_ctx, scene &scene)
    : scene_ref (scene), runtime_ctx (runtime_ctx)
{
}

void
scene_snapshot_serializer::save (serialize::json_writer &writer) const
{
  resource_manager::serialization_context::get ()
      = &runtime_ctx->resource_manager ();

  scene_header header;
  header.scene_name = scene_ref.get_name ();
  header.is_prefab = is_prefab;

  for (const std::unique_ptr<sys::ecs_system> &sys : scene_ref.systems) {
    header.systems.push_back (sys->get_name ());
  }

  for (auto it = scene_ref.get_entity_names ().begin ();
       it != scene_ref.get_entity_names ().end ();) {
    if (!scene_ref.get_registry ().valid (it->first))
      it = scene_ref.get_entity_names ().erase (it);
    else
      ++it;
  }

  for (const std::pair<const entt::entity, std::string> &entry :
       scene_ref.get_entity_names ()) {
    header.entity_names.emplace_back (
        static_cast<uint32_t> (entt::to_integral (entry.first)), entry.second);
  }

  // Only persist connections for systems in this scene (plus global
  // non-scene-system handlers such as runtime_context).  Saving all
  // global connections causes spurious warnings when loading scenes
  // that do not contain the referenced systems.
  std::unordered_set<entt::id_type> scene_system_ids;
  for (const auto &sys : scene_ref.systems) {
    scene_system_ids.insert (sys->get_type_id ());
  }
  header.connections = runtime_ctx->event_hub ().get_connections_for_systems (
      scene_system_ids);

  for (const resource_ref &ref : scene_ref.get_load_list ()) {
    std::string path = runtime_ctx->resource_manager ().get_path (ref);
    header.autoload.push_back ({ ref.type, path });
  }

  header.camera = static_cast<uint32_t> (entt::to_integral (scene_ref.camera));

  entt::registry &registry = scene_ref.get_registry ();

  struct scene_data
  {
    scene_header header;
    std::vector<entt::entity> entities;
  };

  scene_data data;
  data.header = std::move (header);

  auto &storage = registry.storage<entt::entity> ();
  data.entities.reserve (storage.size ());
  for (auto it = storage.rbegin (), last = storage.rend (); it != last; ++it) {
    data.entities.push_back (*it);
  }

  // Assemble a single JSON object. Keying each component by its type name means
  // the file is valid JSON that any tool can read, and the loader no longer has
  // to consume a positional document stream to stay aligned.
  const std::vector<const reg::component_registry::descriptor *>
      world_components
      = runtime_ctx->component_registry ().get_world_components (
          reg::world_component_order::type_id);

  reg::component_registry &components = runtime_ctx->component_registry ();

  std::string components_object;
  bool first = true;
  for (const reg::component_registry::descriptor *desc : world_components) {
    if (desc == nullptr) {
      continue;
    }
    // Daslang components are type-erased: their payload is written as one
    // block keyed by numeric type id, not one document per type.
    if (desc->is_das_component) {
      continue;
    }
    append_member (components_object, first, desc->type_name,
                   component_document (components, registry, desc->type_id));
  }

  serialize::json_writer das_writer;
  components.save_das_components_json (das_writer, registry);
  const std::string das_payload
      = das_writer.json.empty () ? "[]" : das_writer.json;

  std::string singletons_object;
  bool first_singleton = true;
  for (const reg::singleton_registry::descriptor *desc :
       runtime_ctx->singleton_registry ().get_singleton_components (
           reg::singleton_component_order::type_id)) {
    if (!desc || !desc->serialize_with_scene) {
      continue;
    }

    reg::singleton_registry &singletons = runtime_ctx->singleton_registry ();
    const bool has
        = desc->is_das_singleton
              ? singletons.das_singleton_contains (registry, desc->type_id)
              : (desc->contains ? desc->contains (registry) : false);
    if (!has || data.header.is_prefab) {
      continue;
    }

    if (desc->is_das_singleton) {
      append_member (
          singletons_object, first_singleton, desc->type_name,
          singletons.das_singleton_json (registry, desc->type_id));
      continue;
    }

    serialize::json_writer singleton_writer;
    singletons.save_singleton_json (singleton_writer, registry, desc->type_id);
    append_member (singletons_object, first_singleton, desc->type_name,
                   singleton_writer.json.empty () ? "null"
                                                  : singleton_writer.json);
  }

  std::string document;
  document += '{';
  // The top-level members need their own separator state: `first` above
  // belongs to the per-component object, not to this one.
  bool document_first = true;
  append_member (document, document_first, "format_version",
                 std::to_string (scene_document_format_version));
  append_member (document, document_first, "header",
                 wsl::serialize::json_write (data.header));
  append_member (document, document_first, "entities",
                 wsl::serialize::json_write (data.entities));
  append_member (document, document_first, "components",
                 '{' + components_object + '}');
  append_member (document, document_first, "das_components", das_payload);
  append_member (document, document_first, "singletons",
                 '{' + singletons_object + '}');
  document += '}';

  writer.json = std::move (document);

  resource_manager::serialization_context::get () = nullptr;
}

bool
scene_snapshot_serializer::load (serialize::json_reader &reader)
{
  resource_manager::serialization_context::get ()
      = &runtime_ctx->resource_manager ();

  wsl::log::rsc ()->trace ("Loading scene");
  scene_ref.stop_and_clear ();
  runtime_ctx->event_hub ().clear_connections ();

  struct scene_data
  {
    scene_header header;
    std::vector<entt::entity> entities;
  };

  const std::string document (reader.peek ());
  const std::vector<json_document::member> members
      = json_document::object_members (document);

  // Reject the retired stream format outright. Its first document also carries
  // "header" and "entities", so a partial parse would look like a successful
  // load of an empty scene; bailing out is the honest outcome.
  const json_document::member *format_member
      = json_document::find (members, "format_version");
  if (format_member == nullptr) {
    wsl::log::rsc ()->error (
        "Scene file is not in the current format: no top-level "
        "\"format_version\" member. Re-save the scene with this engine "
        "version.");
    resource_manager::serialization_context::get () = nullptr;
    return false;
  }

  uint32_t format_version = 0;
  wsl::serialize::json_read (format_member->value, format_version);
  if (format_version != scene_document_format_version) {
    wsl::log::rsc ()->error (
        "Unsupported scene format version {} (this engine writes {}).",
        format_version, scene_document_format_version);
    resource_manager::serialization_context::get () = nullptr;
    return false;
  }

  scene_data data;
  if (const json_document::member *header_member
      = json_document::find (members, "header")) {
    wsl::serialize::json_read (header_member->value, data.header);
  }
  if (const json_document::member *entities_member
      = json_document::find (members, "entities")) {
    wsl::serialize::json_read (entities_member->value, data.entities);
  }

  scene_header &header = data.header;

  scene_ref.set_name (header.scene_name);
  scene_ref.camera = entt::entity{ static_cast<entt::id_type> (header.camera) };

  wsl::log::rsc ()->trace ("Restoring {} systems", header.systems.size ());
  scene_ref.systems.clear ();

  reg::system_factory_registry &factory
      = runtime_ctx->system_factory_registry ();

  for (const std::string &sys_name : header.systems) {
    if (std::unique_ptr<sys::ecs_system> sys
        = factory.create (sys_name, scene_ref)) {
      scene_ref.add_system_instance (std::move (sys), false);
    } else {
      wsl::log::rsc ()->warn ("Unknown system in scene: {}", sys_name);
    }
  }

  wsl::log::rsc ()->trace ("Loading entities");
  entt::registry &registry = scene_ref.get_registry ();

  auto &storage = registry.storage<entt::entity> ();
  storage.reserve (data.entities.size ());
  entt::entity placeholder{};
  for (entt::entity e : data.entities) {
    storage.generate (e);
    placeholder = (e > placeholder) ? e : placeholder;
  }
  entt::entity next_after_last = entt::entity{ static_cast<entt::id_type> (
      entt::to_integral (placeholder) + 1U) };
  storage.start_from (next_after_last);

  wsl::log::rsc ()->trace ("Loading components");
  {
    // Each component is keyed by type name, so a component this build no longer
    // registers is simply absent and the rest load unaffected.
    reg::component_registry &components = runtime_ctx->component_registry ();

    const json_document::member *components_member
        = json_document::find (members, "components");
    if (components_member != nullptr) {
      for (const json_document::member &member :
           json_document::object_members (components_member->value)) {
        const reg::component_registry::descriptor *desc
            = components.find_world_component (member.key);
        if (desc == nullptr || desc->is_das_component) {
          wsl::log::rsc ()->warn ("Unknown component in scene: {}", member.key);
          continue;
        }
        if (member.value == "null") {
          continue;
        }

        serialize::json_reader component_reader{ member.value };
        try {
          components.load_world_component_json (component_reader, registry,
                                                desc->type_id);
        } catch (const std::exception &error) {
          wsl::log::rsc ()->warn ("Could not restore component '{}': {}",
                                  member.key, error.what ());
        }
      }
    }

    wsl::log::rsc ()->trace ("Loading das components");
    if (const json_document::member *das_member
        = json_document::find (members, "das_components")) {
      serialize::json_reader das_reader{ das_member->value };
      try {
        components.load_das_components_json (das_reader, registry);
      } catch (const std::exception &error) {
        wsl::log::rsc ()->warn ("Could not restore Daslang components: {}",
                                error.what ());
      }
    }
  }
  entt::registry &scene_registry = scene_ref.get_registry ();

  // Singletons are keyed by type name for the same reason as components.
  const json_document::member *singletons_member
      = json_document::find (members, "singletons");
  if (singletons_member != nullptr && !header.is_prefab) {
    for (const json_document::member &member :
         json_document::object_members (singletons_member->value)) {
      const reg::singleton_registry::descriptor *desc
          = runtime_ctx->singleton_registry ().find_singleton_component (
              member.key);
      if (desc == nullptr || !desc->serialize_with_scene
          || member.value == "null") {
        continue;
      }

      reg::singleton_registry &singletons = runtime_ctx->singleton_registry ();
      if (desc->is_das_singleton) {
        bool restored = false;
        if (!member.value.empty () && member.value.front () == '{') {
          restored = singletons.das_singleton_load_json (
              scene_registry, desc->type_id, std::string (member.value));
        } else {
          // Hex arrives as a JSON string; strip the quotes before decoding.
          std::string_view hex = member.value;
          if ((hex.size () >= 2) && (hex.front () == '"')
              && (hex.back () == '"')) {
            hex = hex.substr (1, hex.size () - 2);
          }
          restored = singletons.das_singleton_load_hex (scene_registry,
                                                        desc->type_id, hex);
        }
        if (!restored) {
          wsl::log::rsc ()->warn ("Could not restore das singleton: {}",
                                  member.key);
        }
        continue;
      }

      serialize::json_reader singleton_reader{ member.value };
      try {
        singletons.load_singleton_json (singleton_reader, scene_registry,
                                        desc->type_id);
      } catch (const std::exception &error) {
        wsl::log::rsc ()->warn ("Could not restore singleton '{}': {}",
                                member.key, error.what ());
      }
    }
  }

  wsl::log::rsc ()->trace ("Restoring {} names", header.entity_names.size ());
  for (const std::pair<uint32_t, std::string> &entry : header.entity_names) {
    entt::entity const e{ static_cast<entt::entity> (entry.first) };
    if (scene_ref.get_registry ().valid (e)) {
      scene_ref.set_entity_name (e, entry.second);
    }
  }

  wsl::log::rsc ()->trace ("Restoring {} connections",
                           header.connections.size ());
  for (const auto &conn : header.connections) {
    if (!runtime_ctx->event_hub ().connect (
            conn.event_type_id, conn.system_type_id, conn.handler_name)) {
      wsl::log::rsc ()->warn (
          "Failed to connect signal {} to system {} handler {} "
          "during scene load",
          conn.event_type_id, conn.system_type_id, conn.handler_name);
    }
  }

  for (const resource_ref_serialized &res : header.autoload) {
    entt::id_type const id = entt::hashed_string{ res.path.c_str () };
    wsl::log::rsc ()->trace ("Autoload resource type={} path={}", (int)res.type,
                             res.path);
    scene_ref.add_resource (res.type, id);
  }

  resource_manager::serialization_context::get () = nullptr;

  wsl::log::rsc ()->debug ("Recreating physics objects");
  if (!scene_registry.ctx ().contains<comp::singl::physics_manager> ()) {
    wsl::log::rsc ()->warn (
        "scene_snapshot_serializer: loaded scene is missing its "
        "physics manager; skipping physics object recreation");
    return true;
  }

  if (runtime_ctx != nullptr && runtime_ctx->is_headless ()) {
    wsl::log::rsc ()->debug (
        "Headless mode, skipping physics object recreation");
    return true;
  }

  comp::singl::physics_manager &physics
      = scene_registry.ctx ().get<comp::singl::physics_manager> ();
  phys::engine &engine = physics.ensure_engine ();

  engine.clear ();

  {
    auto view = scene_registry.view<comp::rigid_body> ();
    wsl::log::rsc ()->debug ("Recreating {} rigid bodies",
                             std::distance (view.begin (), view.end ()));
    for (entt::entity e : view) {
      comp::rigid_body &rb = view.get<comp::rigid_body> (e);
      wsl::log::rsc ()->trace ("Creating rigid body for entity {}",
                               (uint32_t)e);

      glm::vec3 world_pos{ 0.0F, 0.0F, 0.0F };
      glm::quat world_rot{ 1.0F, 0.0F, 0.0F, 0.0F };
      glm::vec3 scale{ 1.0F, 1.0F, 1.0F };
      if (auto *wt = scene_registry.try_get<comp::world_transform> (e); wt) {
        glm::mat4 const &wm = wt->value ();
        world_pos = glm::vec3 (wm[3]);
        world_rot = glm::quat_cast (wm);
        scale = glm::vec3 (glm::length (glm::vec3 (wm[0])),
                           glm::length (glm::vec3 (wm[1])),
                           glm::length (glm::vec3 (wm[2])));
      } else if (auto *t = scene_registry.try_get<comp::transform> (e); t) {
        world_pos = (glm::vec3)t->position;
        world_rot = (glm::quat)t->rotation;
        scale = glm::vec3 (t->scale.x (), t->scale.y (), t->scale.z ());
      }

      world_pos = world_pos + world_rot * (glm::vec3)rb.position;
      world_rot = world_rot * (glm::quat)rb.rotation;

      rb.create_body (engine, world_pos, world_rot, scale);
    }
  }

  {
    auto view
        = scene_registry.view<comp::character_body, comp::world_transform> ();
    wsl::log::rsc ()->debug ("Recreating {} characters",
                             std::distance (view.begin (), view.end ()));
    for (entt::entity e : view) {
      comp::character_body &cb = view.get<comp::character_body> (e);
      comp::world_transform &wt = view.get<comp::world_transform> (e);

      wsl::log::rsc ()->trace ("Creating character for entity {}", (uint32_t)e);
      glm::vec3 const pos = glm::vec3 (static_cast<glm::mat4> (wt.value ())[3]);
      cb.recreate (engine, (math::vec3f)pos);
    }
  }

  {
    auto view = scene_registry.view<comp::area> ();
    wsl::log::rsc ()->debug ("Recreating {} area sensors",
                             std::distance (view.begin (), view.end ()));
    for (entt::entity e : view) {
      comp::area &area = view.get<comp::area> (e);
      wsl::log::rsc ()->trace ("Creating area for entity {}", (uint32_t)e);

      glm::vec3 world_pos{ 0.0F, 0.0F, 0.0F };
      glm::quat world_rot{ 1.0F, 0.0F, 0.0F, 0.0F };
      glm::vec3 scale{ 1.0F, 1.0F, 1.0F };
      if (auto *wt = scene_registry.try_get<comp::world_transform> (e); wt) {
        glm::mat4 const &wm = wt->value ();
        world_pos = glm::vec3 (wm[3]);
        world_rot = glm::quat_cast (wm);
        scale = glm::vec3 (glm::length (glm::vec3 (wm[0])),
                           glm::length (glm::vec3 (wm[1])),
                           glm::length (glm::vec3 (wm[2])));
      } else if (auto *t = scene_registry.try_get<comp::transform> (e); t) {
        world_pos = (glm::vec3)t->position;
        world_rot = (glm::quat)t->rotation;
        scale = glm::vec3 (t->scale.x (), t->scale.y (), t->scale.z ());
      }

      world_pos = world_pos + world_rot * (glm::vec3)area.position;
      world_rot = world_rot * (glm::quat)area.rotation;

      area.create_body (engine, world_pos, world_rot, scale);
    }
  }

  scene_ref.init ();

  wsl::log::rsc ()->trace ("Scene load finished");
  return true;
}

void
scene_snapshot_serializer::save_binary (serialize::binary_writer &writer) const
{
  resource_manager::serialization_context::get ()
      = &runtime_ctx->resource_manager ();

  scene_header header;
  header.scene_name = scene_ref.get_name ();
  header.is_prefab = is_prefab;

  for (const std::unique_ptr<sys::ecs_system> &sys : scene_ref.systems) {
    header.systems.push_back (sys->get_name ());
  }

  for (auto it = scene_ref.get_entity_names ().begin ();
       it != scene_ref.get_entity_names ().end ();) {
    if (!scene_ref.get_registry ().valid (it->first))
      it = scene_ref.get_entity_names ().erase (it);
    else
      ++it;
  }

  for (const std::pair<const entt::entity, std::string> &entry :
       scene_ref.get_entity_names ()) {
    header.entity_names.emplace_back (
        static_cast<uint32_t> (entt::to_integral (entry.first)), entry.second);
  }

  // Only persist connections for systems in this scene (plus global
  // non-scene-system handlers such as runtime_context).  See save() for
  // rationale.
  std::unordered_set<entt::id_type> scene_system_ids;
  for (const auto &sys : scene_ref.systems) {
    scene_system_ids.insert (sys->get_type_id ());
  }
  header.connections = runtime_ctx->event_hub ().get_connections_for_systems (
      scene_system_ids);

  for (const resource_ref &ref : scene_ref.get_load_list ()) {
    std::string path = runtime_ctx->resource_manager ().get_path (ref);
    header.autoload.push_back ({ ref.type, path });
  }

  header.camera = static_cast<uint32_t> (entt::to_integral (scene_ref.camera));

  entt::registry &registry = scene_ref.get_registry ();

  struct scene_data
  {
    scene_header header;
    std::vector<entt::entity> entities;
  };

  scene_data data;
  data.header = std::move (header);

  auto &storage = registry.storage<entt::entity> ();
  data.entities.reserve (storage.size ());
  for (auto it = storage.rbegin (), last = storage.rend (); it != last; ++it) {
    data.entities.push_back (*it);
  }

  writer.write (data);

  // The binary format keeps one msgpack document per component in registration
  // order. It is only ever consumed in-process (save_to_binary_string ->
  // load_from_binary_string), never from a file on disk, so unlike the JSON
  // format there is no need to describe the layout or guard against a build
  // that registers a different set of components.
  for (const reg::component_registry::descriptor *desc :
       runtime_ctx->component_registry ().get_world_components (
           reg::world_component_order::type_id)) {
    if (!desc) {
      continue;
    }
    runtime_ctx->component_registry ().save_world_component_binary (
        writer, registry, desc->type_id);
  }

  runtime_ctx->component_registry ().save_das_components_binary (writer,
                                                                 registry);

  for (const reg::singleton_registry::descriptor *desc :
       runtime_ctx->singleton_registry ().get_singleton_components (
           reg::singleton_component_order::type_id)) {
    if (!desc || !desc->serialize_with_scene) {
      continue;
    }

    const bool has = desc && desc->contains ? desc->contains (registry) : false;
    if (has && !data.header.is_prefab) {
      runtime_ctx->singleton_registry ().save_singleton_binary (
          writer, registry, desc->type_id);
    }
  }

  resource_manager::serialization_context::get () = nullptr;
}

bool
scene_snapshot_serializer::load_binary (serialize::binary_reader &reader)
{
  resource_manager::serialization_context::get ()
      = &runtime_ctx->resource_manager ();

  wsl::log::rsc ()->trace ("Loading scene");
  scene_ref.stop_and_clear ();
  runtime_ctx->event_hub ().clear_connections ();

  struct scene_data
  {
    scene_header header;
    std::vector<entt::entity> entities;
  };

  scene_data data;
  if (!reader.read (data)) {
    // Decoding used to be ignored here, so a payload in the wrong format
    // (a JSON scene fed to this reader) became a nameless, entity-less scene
    // that loaded "successfully". Report it instead.
    wsl::log::rsc ()->error (
        "Scene payload could not be decoded as a binary snapshot");
    resource_manager::serialization_context::get () = nullptr;
    return false;
  }

  scene_header &header = data.header;

  scene_ref.set_name (header.scene_name);
  scene_ref.camera = entt::entity{ static_cast<entt::id_type> (header.camera) };

  wsl::log::rsc ()->trace ("Restoring {} systems", header.systems.size ());
  scene_ref.systems.clear ();

  reg::system_factory_registry &factory
      = runtime_ctx->system_factory_registry ();

  for (const std::string &sys_name : header.systems) {
    if (std::unique_ptr<sys::ecs_system> sys
        = factory.create (sys_name, scene_ref)) {
      scene_ref.add_system_instance (std::move (sys), false);
    } else {
      wsl::log::rsc ()->warn ("Unknown system in scene: {}", sys_name);
    }
  }

  wsl::log::rsc ()->trace ("Loading entities");
  entt::registry &registry = scene_ref.get_registry ();

  auto &storage = registry.storage<entt::entity> ();
  storage.reserve (data.entities.size ());
  entt::entity placeholder{};
  for (entt::entity e : data.entities) {
    storage.generate (e);
    placeholder = (e > placeholder) ? e : placeholder;
  }
  entt::entity next_after_last = entt::entity{ static_cast<entt::id_type> (
      entt::to_integral (placeholder) + 1U) };
  storage.start_from (next_after_last);

  wsl::log::rsc ()->trace ("Loading components");
  {
    reg::component_registry &components = runtime_ctx->component_registry ();

    // Registration order, matching the writer above. The binary format is only
    // ever consumed in-process, so both sides agree by construction.
    for (const reg::component_registry::descriptor *desc :
         components.get_world_components (
             reg::world_component_order::type_id)) {
      if (!desc) {
        continue;
      }

      try {
        components.load_world_component_binary (reader, registry,
                                                desc->type_id);
      } catch (const std::exception &) {
        throw;
      }
    }
  }

  wsl::log::rsc ()->trace ("Loading das components");
  try {
    runtime_ctx->component_registry ().load_das_components_binary (reader,
                                                                   registry);
  } catch (const std::exception &) {
    throw;
  }

  wsl::log::rsc ()->trace ("Loading singletons");
  entt::registry &scene_registry = scene_ref.get_registry ();
  for (const reg::singleton_registry::descriptor *desc :
       runtime_ctx->singleton_registry ().get_singleton_components (
           reg::singleton_component_order::type_id)) {
    if (!desc || !desc->serialize_with_scene) {
      continue;
    }

    try {
      if (!header.is_prefab) {
        runtime_ctx->singleton_registry ().load_singleton_binary (
            reader, scene_registry, desc->type_id);
      }
    } catch (const std::exception &) {
      throw;
    }
  }

  wsl::log::rsc ()->trace ("Restoring {} names", header.entity_names.size ());
  for (const std::pair<uint32_t, std::string> &entry : header.entity_names) {
    entt::entity const e{ static_cast<entt::entity> (entry.first) };
    if (scene_ref.get_registry ().valid (e)) {
      scene_ref.set_entity_name (e, entry.second);
    }
  }

  wsl::log::rsc ()->trace ("Restoring {} connections",
                           header.connections.size ());
  for (const auto &conn : header.connections) {
    if (!runtime_ctx->event_hub ().connect (
            conn.event_type_id, conn.system_type_id, conn.handler_name)) {
      wsl::log::rsc ()->warn (
          "Failed to connect signal {} to system {} handler {} "
          "during scene load",
          conn.event_type_id, conn.system_type_id, conn.handler_name);
    }
  }

  for (const resource_ref_serialized &res : header.autoload) {
    entt::id_type const id = entt::hashed_string{ res.path.c_str () };
    wsl::log::rsc ()->trace ("Autoload resource type={} path={}", (int)res.type,
                             res.path);
    scene_ref.add_resource (res.type, id);
  }

  resource_manager::serialization_context::get () = nullptr;

  wsl::log::rsc ()->debug ("Recreating physics objects");
  if (!scene_registry.ctx ().contains<comp::singl::physics_manager> ()) {
    wsl::log::rsc ()->warn (
        "scene_snapshot_serializer: loaded scene is missing its "
        "physics manager; skipping physics object recreation");
    return true;
  }

  if (runtime_ctx != nullptr && runtime_ctx->is_headless ()) {
    wsl::log::rsc ()->debug (
        "Headless mode, skipping physics object recreation");
    return true;
  }

  comp::singl::physics_manager &physics
      = scene_registry.ctx ().get<comp::singl::physics_manager> ();
  phys::engine &engine = physics.ensure_engine ();

  engine.clear ();

  {
    auto view = scene_registry.view<comp::rigid_body> ();
    wsl::log::rsc ()->debug ("Recreating {} rigid bodies",
                             std::distance (view.begin (), view.end ()));
    for (entt::entity e : view) {
      comp::rigid_body &rb = view.get<comp::rigid_body> (e);
      wsl::log::rsc ()->trace ("Creating rigid body for entity {}",
                               (uint32_t)e);

      glm::vec3 world_pos{ 0.0F, 0.0F, 0.0F };
      glm::quat world_rot{ 1.0F, 0.0F, 0.0F, 0.0F };
      glm::vec3 scale{ 1.0F, 1.0F, 1.0F };
      if (auto *wt = scene_registry.try_get<comp::world_transform> (e); wt) {
        glm::mat4 const &wm = wt->value ();
        world_pos = glm::vec3 (wm[3]);
        world_rot = glm::quat_cast (wm);
        scale = glm::vec3 (glm::length (glm::vec3 (wm[0])),
                           glm::length (glm::vec3 (wm[1])),
                           glm::length (glm::vec3 (wm[2])));
      } else if (auto *t = scene_registry.try_get<comp::transform> (e); t) {
        world_pos = (glm::vec3)t->position;
        world_rot = (glm::quat)t->rotation;
        scale = glm::vec3 (t->scale.x (), t->scale.y (), t->scale.z ());
      }

      world_pos = world_pos + world_rot * (glm::vec3)rb.position;
      world_rot = world_rot * (glm::quat)rb.rotation;

      rb.create_body (engine, world_pos, world_rot, scale);
    }
  }

  {
    auto view
        = scene_registry.view<comp::character_body, comp::world_transform> ();
    wsl::log::rsc ()->debug ("Recreating {} characters",
                             std::distance (view.begin (), view.end ()));
    for (entt::entity e : view) {
      comp::character_body &cb = view.get<comp::character_body> (e);
      comp::world_transform &wt = view.get<comp::world_transform> (e);

      wsl::log::rsc ()->trace ("Creating character for entity {}", (uint32_t)e);
      glm::vec3 const pos = glm::vec3 (static_cast<glm::mat4> (wt.value ())[3]);
      cb.recreate (engine, (math::vec3f)pos);
    }
  }

  {
    auto view = scene_registry.view<comp::area> ();
    wsl::log::rsc ()->debug ("Recreating {} area sensors",
                             std::distance (view.begin (), view.end ()));
    for (entt::entity e : view) {
      comp::area &area = view.get<comp::area> (e);
      wsl::log::rsc ()->trace ("Creating area for entity {}", (uint32_t)e);

      glm::vec3 world_pos{ 0.0F, 0.0F, 0.0F };
      glm::quat world_rot{ 1.0F, 0.0F, 0.0F, 0.0F };
      glm::vec3 scale{ 1.0F, 1.0F, 1.0F };
      if (auto *wt = scene_registry.try_get<comp::world_transform> (e); wt) {
        glm::mat4 const &wm = wt->value ();
        world_pos = glm::vec3 (wm[3]);
        world_rot = glm::quat_cast (wm);
        scale = glm::vec3 (glm::length (glm::vec3 (wm[0])),
                           glm::length (glm::vec3 (wm[1])),
                           glm::length (glm::vec3 (wm[2])));
      } else if (auto *t = scene_registry.try_get<comp::transform> (e); t) {
        world_pos = (glm::vec3)t->position;
        world_rot = (glm::quat)t->rotation;
        scale = glm::vec3 (t->scale.x (), t->scale.y (), t->scale.z ());
      }

      world_pos = world_pos + world_rot * (glm::vec3)area.position;
      world_rot = world_rot * (glm::quat)area.rotation;

      area.create_body (engine, world_pos, world_rot, scale);
    }
  }

  scene_ref.init ();

  wsl::log::rsc ()->trace ("Scene load finished");
  return true;
}

bool
scene_snapshot_serializer::save_binary (const std::string &path) const
{
  serialize::binary_writer writer;
  save_binary (writer);
  std::ofstream file (path, std::ios::binary);
  if (!file) {
    return false;
  }
  file.write (reinterpret_cast<const char *> (writer.bytes.data ()),
              static_cast<std::streamsize> (writer.bytes.size ()));
  return true;
}

bool
scene_snapshot_serializer::load_binary (const std::string &path)
{
  wsl::log::rsc ()->trace ("Loading binary scene: {}", path);
  std::ifstream file (path, std::ios::binary);
  if (!file) {
    return false;
  }
  file.seekg (0, std::ios::end);
  auto size = file.tellg ();
  file.seekg (0, std::ios::beg);
  std::vector<std::uint8_t> bytes (static_cast<size_t> (size));
  file.read (reinterpret_cast<char *> (bytes.data ()), size);
  serialize::binary_reader reader{ bytes };
  return load_binary (reader);
}

bool
scene_snapshot_serializer::save_json (const std::string &path) const
{
  serialize::json_writer writer;
  save (writer);
  std::ofstream file (path);
  if (!file) {
    return false;
  }
  file << writer.json;
  return true;
}

bool
scene_snapshot_serializer::load_json (const std::string &path)
{
  wsl::log::rsc ()->trace ("Loading JSON scene: {}", path);
  std::ifstream file (path);
  if (!file) {
    return false;
  }
  std::stringstream ss;
  ss << file.rdbuf ();
  // Hold the buffer in a named string: json_reader stores a view into it
  // for the whole load, so a temporary would dangle.
  const std::string content = ss.str ();
  serialize::json_reader reader{ content };
  return load (reader);
}

bool
scene_snapshot_serializer::save_to_binary_string (std::string &out) const
{
  serialize::binary_writer writer;
  save_binary (writer);
  out.assign (reinterpret_cast<const char *> (writer.bytes.data ()),
              writer.bytes.size ());
  return true;
}

bool
scene_snapshot_serializer::load_from_binary_string (const std::string &in)
{
  std::vector<std::uint8_t> bytes (in.begin (), in.end ());
  serialize::binary_reader reader{ bytes };
  return load_binary (reader);
}

} // namespace io

} // namespace rsc

} // namespace wsl
