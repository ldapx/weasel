#include "resource_manager.hpp"

#include "../comp/component_meta.hpp"
#include "../comp/hierarchy.hpp"
#include "../comp/singl/editor_context.hpp"
#include "../comp/singl/runtime_context.hpp"
#include "../comp/singl/rendering_manager.hpp"
#include "../sys/core_systems.hpp"
#include "gfx/cubemap.hpp"
#include "gfx/image.hpp"
#include "gfx/model_3d.hpp"
#include "rsc/cpu_model.hpp"
#include "rsc/image_loader.hpp"
#include "rsc/model_loader.hpp"
#include "rsc/project.hpp"
#include "rsc/project_loader.hpp"
#include "rsc/resource_ids.hpp"
#include "rsc/resource_ref.hpp"
#include "rsc/shader_loader.hpp"
#include "wsl/log/log.hpp"

#include "../serialize/serialize.hpp"

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <fstream>
#include <cstdint>
#include <cstdio>
#include <entt/core/fwd.hpp>
#include <entt/core/hashed_string.hpp>
#include <entt/core/type_info.hpp>
#include <entt/entity/entity.hpp>
#include <entt/entity/fwd.hpp>
#include <entt/meta/factory.hpp>
#include <exception>
#include <filesystem>
#include <future>
#include <nlohmann/json.hpp>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace wsl
{

namespace fs = std::filesystem;

namespace
{

template <typename T>
std::string
variant_tag_name ()
{
  return rfl::type_name_t<T>{}.str ();
}

std::string
migrate_legacy_material_variant_tags (const std::string &content)
{
  nlohmann::json root = nlohmann::json::parse (content, nullptr, false);
  if (root.is_discarded () || !root.contains ("default_parameters")
      || !root["default_parameters"].is_object ()) {
    return content;
  }

  static const std::unordered_map<std::string, std::string> legacy_tags = {
    { "vec<2, float>", variant_tag_name<glm::vec2> () },
    { "vec<3, float>", variant_tag_name<glm::vec3> () },
    { "vec<4, float>", variant_tag_name<glm::vec4> () },
    { "image_id", variant_tag_name<rsc::image_id> () },
    { "cubemap_id", variant_tag_name<rsc::cubemap_id> () },
  };

  bool changed = false;
  for (auto &parameter : root["default_parameters"].items ()) {
    if (!parameter.value ().is_object ()) {
      continue;
    }

    auto value_it = parameter.value ().find ("value");
    if (value_it == parameter.value ().end () || !value_it->is_object ()
        || (value_it->size () != 1)) {
      continue;
    }

    auto alternative = value_it->begin ();
    auto legacy = legacy_tags.find (alternative.key ());
    if (legacy == legacy_tags.end ()) {
      continue;
    }

    nlohmann::json migrated = nlohmann::json::object ();
    migrated[legacy->second] = alternative.value ();
    parameter.value ()["value"] = std::move (migrated);
    changed = true;
  }

  return changed ? root.dump () : content;
}

std::string
basename_no_ext (const std::string &path)
{
  std::filesystem::path const p (path);
  return p.stem ().string ();
}

std::string
display_name_for_model_path (const std::string &path)
{
  const std::string_view builtin_name
      = rsc::model_loader::primitive_display_name (path);
  if (!builtin_name.empty ()) {
    return std::string (builtin_name);
  }

  return basename_no_ext (path);
}

template <typename Table>
auto
find_record (Table &table, entt::id_type id) -> Table::mapped_type *
{
  auto it = table.find (id);
  if (it == table.end ()) {
    return nullptr;
  }
  return &it->second;
}

template <typename Table>
auto
find_record (const Table &table, entt::id_type id) -> const Table::mapped_type *
{
  auto it = table.find (id);
  if (it == table.end ()) {
    return nullptr;
  }
  return &it->second;
}

template <typename Table, typename Record, typename State>
entt::id_type
register_resource (Table &table, const std::string &path, State initial_state)
{
  const entt::id_type id = entt::hashed_string{ path.c_str () };
  table.try_emplace (id, Record{ .path = path,
                                 .name = basename_no_ext (path),
                                 .state = initial_state });
  return id;
}

template <typename Info>
void
sort_infos (std::vector<Info> &infos)
{
  std::sort (infos.begin (), infos.end (),
             [] (const Info &lhs, const Info &rhs) {
               if (lhs.name == rhs.name) {
                 return lhs.id < rhs.id;
               }
               return lhs.name < rhs.name;
             });
}

static bool
path_is_inside_project (const fs::path &file, const fs::path &root)
{
  fs::path const abs_file = fs::absolute (file);
  fs::path const abs_root = fs::absolute (root);
  std::string f = abs_file.string ();
  std::string r = abs_root.string ();
  if (!r.empty () && r.back () != fs::path::preferred_separator) {
    r += fs::path::preferred_separator;
  }
  return f.size () > r.size () && f.compare (0, r.size (), r) == 0;
}

static std::optional<std::string>
copy_import_to_project (const std::string &source_path,
                        const std::string &project_subdir,
                        const std::string &project_root)
{
  fs::path const src (source_path);
  if (!fs::exists (src)) {
    wsl::log::rsc ()->warn ("Import source does not exist: {}", source_path);
    return std::nullopt;
  }

  fs::path const root (project_root);
  fs::path const abs_root = fs::absolute (root);
  fs::path const abs_src = fs::absolute (src);

  if (path_is_inside_project (abs_src, abs_root)) {
    return "res://" + fs::relative (abs_src, abs_root).generic_string ();
  }

  fs::path dest_dir = abs_root / project_subdir;
  try {
    fs::create_directories (dest_dir);
  } catch (const std::exception &e) {
    wsl::log::rsc ()->error ("Failed to create import directory {}: {}",
                             dest_dir.string (), e.what ());
    return std::nullopt;
  }

  fs::path dest = dest_dir / src.filename ();
  try {
    fs::copy_file (src, dest, fs::copy_options::overwrite_existing);
  } catch (const std::exception &e) {
    wsl::log::rsc ()->error ("Failed to copy imported file from {} to {}: {}",
                             source_path, dest.string (), e.what ());
    return std::nullopt;
  }

  return "res://" + fs::relative (dest, abs_root).generic_string ();
}

} // namespace

rsc::resource_manager::resource_manager (
    comp::singl::runtime_context *m_runtime_ctx,
    const std::string &engine_res_path, bool manages_runtime_state)
    : m_runtime_ctx (m_runtime_ctx), m_scenes (scene_loader{}),
      m_models (model_loader{ &m_runtime_ctx->render_ctx () }),
      m_cubemaps (cubemap_loader{ &m_runtime_ctx->render_ctx () }),
      m_images (image_loader{}), m_shaders (shader_loader{}),
      m_project_loader (m_runtime_ctx), m_wsl_resource_path (engine_res_path),
      m_manages_runtime_state (manages_runtime_state)
{
  if ((SDL_WasInit (SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0) {
    wsl::log::rsc ()->debug (
        "resource_manager: SDL audio is not initialized, skipping "
        "mixer creation");
    return;
  }

  if (!MIX_Init ()) {
    wsl::log::rsc ()->error ("Failed to initialize SDL_mixer: {}",
                             SDL_GetError ());
  }

  m_mixer = MIX_CreateMixerDevice (SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
  if (m_mixer == nullptr) {
    wsl::log::rsc ()->error ("Failed to create SDL_mixer mixer: {}",
                             SDL_GetError ());
  }
}

void
rsc::resource_manager_view::register_meta ()
{
  using namespace entt::literals;

  auto &&factory_v
      = entt::meta_factory<rsc::resource_manager_view> ()
            .type (entt::type_hash<rsc::resource_manager_view>::value ())
            .custom<comp::meta_info> (comp::meta_info{
                "Resource Manager",
                "Owns project resources and background loading for "
                "the current runtime.",
                "" });

  rsc::model_id::register_meta ();
  rsc::audio_id::register_meta ();
  rsc::material_id::register_meta ();
}

rsc::resource_manager::model_handle
rsc::resource_manager::load (model_id id)
{
  detail::model_record *rec = find_record (m_model_table, id.value);
  if (rec == nullptr) {
    return {};
  }

  if (rec->state == model_state::loaded) {
    return m_models[id.value];
  }

  if (rec->state != model_state::not_loaded) {
    return {};
  }

  std::string const resolved = resolve_path (rec->path);

  if (resolved.rfind ("builtin://", 0) == 0) {
    model_loader const loader{ &m_runtime_ctx->render_ctx () };
    std::shared_ptr<gfx::model_3d> const ready = loader (resolved);
    if (ready) {
      m_models.force_load (id.value, std::move (*ready));
      rec->state = model_state::loaded;
      return m_models[id.value];
    }
    return {};
  }

  rec->state = model_state::loading_cpu;
  rec->job = std::async (std::launch::async, [this, resolved] () {
    model_loader const loader{ &m_runtime_ctx->render_ctx () };
    return loader.load_cpu (resolved);
  });
  return {};
}

void
rsc::resource_manager::load (io::resource_ref ref)
{
  switch (ref.type) {
  case io::resource_type::model:
    static_cast<void> (load (model_id{ ref.id }));
    break;
  case io::resource_type::image:
    static_cast<void> (load (image_id{ ref.id }));
    break;
  case io::resource_type::cubemap:
    static_cast<void> (load (cubemap_id{ ref.id }));
    break;
  case io::resource_type::scene:
    static_cast<void> (load (scene_id{ ref.id }));
    break;
  case io::resource_type::audio:
    static_cast<void> (load (audio_id{ ref.id }));
    break;
  case io::resource_type::material:
    static_cast<void> (load (material_id{ ref.id }));
    break;
  }
}

rsc::model_id
rsc::resource_manager::register_model (const std::string &path)
{
  std::string normalized = path;

  // Convert absolute project paths to res://
  if (m_active_project && path.rfind ("res://", 0) != 0
      && path.rfind ("builtin://", 0) != 0) {
    std::filesystem::path const root (m_active_project->root_path);
    std::filesystem::path const p (path);
    if (path.find (m_active_project->root_path) == 0) {
      normalized
          = "res://" + std::filesystem::relative (p, root).generic_string ();
    }
  }

  if (const std::unordered_map<std::string, entt::id_type>::const_iterator it
      = m_model_ids_by_path.find (normalized);
      it != m_model_ids_by_path.end ()) {
    return model_id{ it->second };
  }

  const entt::id_type id = entt::hashed_string{ normalized.c_str () };
  m_model_table.try_emplace (
      id,
      detail::model_record{ .path = normalized,
                            .name = display_name_for_model_path (normalized),
                            .state = model_state::not_loaded });
  m_model_ids_by_path[normalized] = id;
  return model_id{ id };
}

rsc::model_id
rsc::resource_manager::import_model (const std::string &path, bool request_load)
{
  std::string import_path = path;
  if (m_active_project) {
    if (auto copied = copy_import_to_project (
            path, m_active_project->models_path, m_active_project->root_path)) {
      import_path = *copied;
    }
  }
  model_id id = register_model (import_path);
  if (request_load) {
    static_cast<void> (load (id));
  }
  return id;
}

rsc::model_state
rsc::resource_manager::state (model_id id) const
{
  if (const detail::model_record *rec = find_record (m_model_table, id.value)) {
    return rec->state;
  }
  return model_state::not_loaded;
}

bool
rsc::resource_manager::contains (model_id id) const
{
  return find_record (m_model_table, id.value) != nullptr;
}

std::optional<rsc::model_id>
rsc::resource_manager::find_model_by_path (const std::string &path) const
{
  const std::unordered_map<std::string, entt::id_type>::const_iterator it
      = m_model_ids_by_path.find (path);
  if (it == m_model_ids_by_path.end ()) {
    return std::nullopt;
  }

  return model_id{ it->second };
}

std::optional<rsc::model_resource_info>
rsc::resource_manager::info (model_id id) const
{
  const detail::model_record *rec = find_record (m_model_table, id.value);
  if (rec == nullptr) {
    return std::nullopt;
  }

  return model_resource_info{ .id = id.value,
                              .path = rec->path,
                              .name = rec->name,
                              .state = rec->state,
                              .preview_owned = (m_preview_model_id == id.value),
                              .lowest_lod_only
                              = m_low_lod_only_models.contains (id.value) };
}

std::vector<rsc::model_resource_info>
rsc::resource_manager::list_models () const
{
  std::vector<model_resource_info> infos;
  infos.reserve (m_model_table.size ());

  for (const auto &[id, rec] : m_model_table) {
    infos.push_back (model_resource_info{
        .id = id,
        .path = rec.path,
        .name = rec.name,
        .state = rec.state,
        .preview_owned = (m_preview_model_id == id),
        .lowest_lod_only = m_low_lod_only_models.contains (id) });
  }

  sort_infos (infos);
  return infos;
}

rsc::resource_manager::model_handle
rsc::resource_manager::get (model_id id)
{
  return state (id) == model_state::loaded ? m_models[id.value]
                                           : model_handle{};
}

rsc::image_id
rsc::resource_manager::register_image (const std::string &path)
{
  std::string normalized = path;
  if (m_active_project && path.rfind ("res://", 0) != 0
      && path.rfind ("builtin://", 0) != 0
      && path.rfind ("engine://", 0) != 0) {
    std::filesystem::path const root (m_active_project->root_path);
    std::filesystem::path const p (path);
    if (path.find (m_active_project->root_path) == 0) {
      normalized
          = "res://" + std::filesystem::relative (p, root).generic_string ();
    }
  }

  return image_id{
    register_resource<std::unordered_map<entt::id_type, detail::image_record>,
                      detail::image_record> (m_image_table, normalized,
                                             image_state::not_loaded)
  };
}

rsc::image_id
rsc::resource_manager::import_image (const std::string &path, bool request_load)
{
  std::string import_path = path;
  if (m_active_project) {
    if (auto copied = copy_import_to_project (
            path, m_active_project->images_path, m_active_project->root_path)) {
      import_path = *copied;
    }
  }
  image_id id = register_image (import_path);
  if (request_load) {
    static_cast<void> (load (id));
  }
  return id;
}

rsc::resource_manager::image_handle
rsc::resource_manager::get (image_id id)
{
  return state (id) == image_state::loaded ? m_images[id.value]
                                           : image_handle{};
}

rsc::resource_manager::image_handle
rsc::resource_manager::load (image_id id)
{
  detail::image_record *rec = find_record (m_image_table, id.value);
  if (rec == nullptr) {
    return {};
  }

  if (rec->state == image_state::loaded) {
    return m_images[id.value];
  }

  if (rec->state != image_state::not_loaded) {
    return {};
  }

  rec->state = image_state::loading;
  std::string const resolved = resolve_path (rec->path);
  rec->job = std::async (std::launch::async, [resolved] () {
    image_loader const loader;
    return loader.load_cpu (resolved);
  });
  return {};
}

rsc::image_state
rsc::resource_manager::state (image_id id) const
{
  if (const detail::image_record *rec = find_record (m_image_table, id.value)) {
    return rec->state;
  }
  return image_state::not_loaded;
}

bool
rsc::resource_manager::contains (image_id id) const
{
  return find_record (m_image_table, id.value) != nullptr;
}

std::optional<rsc::image_resource_info>
rsc::resource_manager::info (image_id id) const
{
  const detail::image_record *rec = find_record (m_image_table, id.value);
  if (rec == nullptr) {
    return std::nullopt;
  }

  return image_resource_info{
    .id = id.value, .path = rec->path, .name = rec->name, .state = rec->state
  };
}

std::vector<rsc::image_resource_info>
rsc::resource_manager::list_images () const
{
  std::vector<image_resource_info> infos;
  infos.reserve (m_image_table.size ());

  for (const auto &[id, rec] : m_image_table) {
    infos.push_back (image_resource_info{
        .id = id, .path = rec.path, .name = rec.name, .state = rec.state });
  }

  sort_infos (infos);
  return infos;
}

rsc::cubemap_id
rsc::resource_manager::register_cubemap (const std::string &path)
{
  std::string normalized = path;
  if (m_active_project && path.rfind ("res://", 0) != 0
      && path.rfind ("builtin/", 0) != 0 && path.rfind ("builtin://", 0) != 0
      && path.rfind ("engine://", 0) != 0) {
    std::filesystem::path const root (m_active_project->root_path);
    std::filesystem::path const p (path);
    if (path.find (m_active_project->root_path) == 0) {
      normalized
          = "res://" + std::filesystem::relative (p, root).generic_string ();
    }
  }

  return cubemap_id{
    register_resource<std::unordered_map<entt::id_type, detail::cubemap_record>,
                      detail::cubemap_record> (m_cubemap_table, normalized,
                                               cubemap_state::not_loaded)
  };
}

rsc::cubemap_id
rsc::resource_manager::import_cubemap (const std::string &path,
                                       bool request_load)
{
  std::string import_path = path;
  if (m_active_project) {
    if (auto copied
        = copy_import_to_project (path, m_active_project->cubemaps_path,
                                  m_active_project->root_path)) {
      import_path = *copied;
    }
  }
  cubemap_id id = register_cubemap (import_path);
  if (request_load) {
    static_cast<void> (load (id));
  }
  return id;
}

rsc::resource_manager::cubemap_handle
rsc::resource_manager::load (cubemap_id id)
{
  detail::cubemap_record *rec = find_record (m_cubemap_table, id.value);
  if (rec == nullptr) {
    return {};
  }

  if (rec->state == cubemap_state::loaded) {
    return m_cubemaps[id.value];
  }

  if (rec->state != cubemap_state::not_loaded) {
    return {};
  }

  rec->state = cubemap_state::loading;
  std::string const resolved = resolve_path (rec->path);
  rec->job = std::async (std::launch::async, [this, resolved] () {
    cubemap_loader const loader{ &m_runtime_ctx->render_ctx () };
    return loader (resolved);
  });
  return {};
}

rsc::resource_manager::cubemap_handle
rsc::resource_manager::get (cubemap_id id)
{
  return state (id) == cubemap_state::loaded ? m_cubemaps[id.value]
                                             : cubemap_handle{};
}

rsc::cubemap_state
rsc::resource_manager::state (cubemap_id id) const
{
  if (const detail::cubemap_record *rec
      = find_record (m_cubemap_table, id.value)) {
    return rec->state;
  }
  return cubemap_state::not_loaded;
}

bool
rsc::resource_manager::contains (cubemap_id id) const
{
  return find_record (m_cubemap_table, id.value) != nullptr;
}

std::optional<rsc::cubemap_resource_info>
rsc::resource_manager::info (cubemap_id id) const
{
  const detail::cubemap_record *rec = find_record (m_cubemap_table, id.value);
  if (rec == nullptr) {
    return std::nullopt;
  }

  return cubemap_resource_info{
    .id = id.value, .path = rec->path, .name = rec->name, .state = rec->state
  };
}

std::vector<rsc::cubemap_resource_info>
rsc::resource_manager::list_cubemaps () const
{
  std::vector<cubemap_resource_info> infos;
  infos.reserve (m_cubemap_table.size ());

  for (const auto &[id, rec] : m_cubemap_table) {
    infos.push_back (cubemap_resource_info{
        .id = id, .path = rec.path, .name = rec.name, .state = rec.state });
  }

  sort_infos (infos);
  return infos;
}

rsc::scene_id
rsc::resource_manager::register_scene (const std::string &path)
{
  std::string normalized = path;
  if (m_active_project && path.rfind ("res://", 0) != 0
      && path.rfind ("builtin://", 0) != 0
      && path.rfind ("engine://", 0) != 0) {
    std::filesystem::path const root (m_active_project->root_path);
    std::filesystem::path const p (path);
    if (path.find (m_active_project->root_path) == 0) {
      normalized
          = "res://" + std::filesystem::relative (p, root).generic_string ();
    }
  }

  const entt::id_type id = entt::hashed_string{ normalized.c_str () };
  std::filesystem::path const p (normalized);
  bool const is_prefab = p.extension () == ".prefab";

  m_scene_table.try_emplace (
      id, detail::scene_record{ .path = normalized,
                                .name = basename_no_ext (normalized),
                                .state = scene_state::not_loaded,
                                .is_prefab = is_prefab });
  return scene_id{ id };
}

rsc::scene_id
rsc::resource_manager::import_scene (const std::string &path, bool request_load)
{
  std::string import_path = path;
  if (m_active_project) {
    if (auto copied = copy_import_to_project (
            path, m_active_project->scenes_path, m_active_project->root_path)) {
      import_path = *copied;
    }
  }
  scene_id id = register_scene (import_path);
  if (request_load) {
    static_cast<void> (load (id));
  }
  return id;
}

rsc::resource_manager::scene_handle
rsc::resource_manager::load (scene_id id)
{
  detail::scene_record *rec = find_record (m_scene_table, id.value);
  if (rec == nullptr) {
    return {};
  }

  if (rec->state == scene_state::loaded) {
    return m_scenes.contains (id.value) ? m_scenes[id.value] : scene_handle{};
  }

  if (rec->state != scene_state::not_loaded) {
    return {};
  }

  rec->state = scene_state::loading;
  std::string const resolved = resolve_path (rec->path);
  rec->job = std::async (std::launch::async, [this, resolved] () {
    scene_loader const loader{};
    return loader (m_runtime_ctx, m_editor_ctx, resolved);
  });
  return {};
}

rsc::resource_manager::scene_handle
rsc::resource_manager::get (scene_id id)
{
  if (state (id) != scene_state::loaded || !m_scenes.contains (id.value)) {
    return scene_handle{};
  }

  return m_scenes[id.value];
}

rsc::scene *
rsc::resource_manager::find_loaded_scene (scene_id id) const
{
  if (const auto it = m_loaded_scene_instances.find (id.value);
      it != m_loaded_scene_instances.end ()) {
    return it->second;
  }

  return nullptr;
}

bool
rsc::resource_manager::activate_scene (scene_id id)
{
  if (scene *scene = find_loaded_scene (id)) {
    m_runtime_ctx->scene_manager ().set_active (scene);
    return true;
  }

  return false;
}

entt::entity
rsc::resource_manager::instantiate_prefab (scene_id id, entt::entity parent)
{
  scene *active_scene = m_runtime_ctx->scene_manager ().get_active ();
  if (active_scene == nullptr) {
    return entt::null;
  }

  detail::scene_record *rec = find_record (m_scene_table, id.value);
  if (rec == nullptr) {
    return entt::null;
  }

  // Ensure it is loaded.
  if (rec->state == scene_state::not_loaded) {
    static_cast<void> (load (id));
  }

  // If it's loading, wait for it synchronously for instantiation
  if (rec->state == scene_state::loading) {
    if (rec->job.valid ()) {
      try {
        std::shared_ptr<rsc::scene> const scn = rec->job.get ();
        if (scn) {
          rsc::scene &world_scene
              = m_runtime_ctx->scene_manager ().get_world ().add_scene (
                  std::move (*scn));
          m_loaded_scene_instances[id.value] = &world_scene;
          rec->state = scene_state::loaded;

          for (const io::resource_ref &r : world_scene.get_load_list ()) {
            load (r);
          }
        } else {
          rec->state = scene_state::not_loaded;
          return entt::null;
        }
      } catch (const std::exception &e) {
        wsl::log::rsc ()->error ("Scene load job for '{}' threw: {}", id.value,
                                 e.what ());
        rec->state = scene_state::not_loaded;
        return entt::null;
      } catch (...) {
        wsl::log::rsc ()->error (
            "Scene load job for '{}' threw an unknown exception", id.value);
        rec->state = scene_state::not_loaded;
        return entt::null;
      }
    }
  }

  if (rec->state != scene_state::loaded) {
    return entt::null;
  }

  scene *prefab_scene = find_loaded_scene (id);
  if (prefab_scene == nullptr) {
    return entt::null;
  }

  entt::registry &prefab_reg = prefab_scene->get_registry ();

  // Find root entities in prefab (entities with comp::hierarchy but no parent,
  // or no comp::hierarchy)
  std::vector<entt::entity> roots;
  prefab_reg.view<entt::entity> ().each ([&] (entt::entity e) {
    if (prefab_reg.all_of<comp::hierarchy> (e)) {
      if (prefab_reg.get<comp::hierarchy> (e).parent == entt::null) {
        roots.push_back (e);
      }
    } else {
      // If no hierarchy component, it is also a root
      roots.push_back (e);
    }
  });

  entt::entity first_root = entt::null;
  for (entt::entity const root : roots) {
    entt::entity copied
        = active_scene->copy_entity (*prefab_scene, root, parent, true, id);
    if (first_root == entt::null) {
      first_root = copied;
    }
  }
  return first_root;
}

rsc::scene_state
rsc::resource_manager::state (scene_id id) const
{
  if (const detail::scene_record *rec = find_record (m_scene_table, id.value)) {
    return rec->state;
  }
  return scene_state::not_loaded;
}

bool
rsc::resource_manager::contains (scene_id id) const
{
  return find_record (m_scene_table, id.value) != nullptr;
}

std::optional<rsc::scene_resource_info>
rsc::resource_manager::info (scene_id id) const
{
  const detail::scene_record *rec = find_record (m_scene_table, id.value);
  if (rec == nullptr) {
    return std::nullopt;
  }

  return scene_resource_info{ .id = id.value,
                              .path = rec->path,
                              .name = rec->name,
                              .state = rec->state,
                              .is_prefab = rec->is_prefab };
}

std::vector<rsc::scene_resource_info>
rsc::resource_manager::list_scenes () const
{
  std::vector<scene_resource_info> infos;
  infos.reserve (m_scene_table.size ());

  for (const auto &[id, rec] : m_scene_table) {
    infos.push_back (scene_resource_info{ .id = id,
                                          .path = rec.path,
                                          .name = rec.name,
                                          .state = rec.state,
                                          .is_prefab = rec.is_prefab });
  }

  sort_infos (infos);
  return infos;
}

bool
rsc::resource_manager::save_scene (const rsc::scene &scene,
                                   const std::string &path, bool is_prefab)
{
  scene_loader const loader;
  return loader.save (m_runtime_ctx, scene, path, is_prefab);
}

void
rsc::resource_manager::set_editor_context (
    comp::singl::editor_context *editor_ctx)
{
  m_editor_ctx = editor_ctx;
}

rsc::audio_id
rsc::resource_manager::register_audio (const std::string &path)
{
  std::string normalized = path;
  if (m_active_project && path.rfind ("res://", 0) != 0) {
    std::filesystem::path const root (m_active_project->root_path);
    std::filesystem::path const p (path);
    if (path.find (m_active_project->root_path) == 0) {
      normalized
          = "res://" + std::filesystem::relative (p, root).generic_string ();
    }
  }

  if (auto it = m_audio_ids_by_path.find (normalized);
      it != m_audio_ids_by_path.end ()) {
    return audio_id{ it->second };
  }

  const entt::id_type id = entt::hashed_string{ normalized.c_str () };
  m_audio_table.try_emplace (
      id, detail::audio_record{ .path = normalized,
                                .name = basename_no_ext (normalized),
                                .state = audio_state::not_loaded });
  m_audio_ids_by_path[normalized] = id;
  return audio_id{ id };
}

rsc::audio_id
rsc::resource_manager::import_audio (const std::string &path, bool request_load)
{
  std::string import_path = path;
  if (m_active_project) {
    if (auto copied = copy_import_to_project (
            path, m_active_project->audio_path, m_active_project->root_path)) {
      import_path = *copied;
    }
  }
  audio_id id = register_audio (import_path);
  if (request_load) {
    static_cast<void> (load (id));
  }
  return id;
}

MIX_Audio *
rsc::resource_manager::load (audio_id id)
{
  detail::audio_record *rec = find_record (m_audio_table, id.value);
  if (rec == nullptr) {
    return nullptr;
  }

  if (rec->state == audio_state::loaded) {
    return rec->audio;
  }

  std::string resolved = resolve_path (rec->path);
  rec->audio = MIX_LoadAudio (m_mixer, resolved.c_str (), false);
  if (rec->audio != nullptr) {
    rec->state = audio_state::loaded;
  } else {
    wsl::log::rsc ()->error ("Failed to load audio {}: {}", resolved,
                             SDL_GetError ());
    rec->state = audio_state::not_loaded;
  }

  return rec->audio;
}

void
rsc::resource_manager::unload (audio_id id)
{
  detail::audio_record *rec = find_record (m_audio_table, id.value);
  if ((rec != nullptr) && (rec->audio != nullptr)) {
    MIX_DestroyAudio (rec->audio);
    rec->audio = nullptr;
    rec->state = audio_state::not_loaded;
  }
}

MIX_Audio *
rsc::resource_manager::get (audio_id id)
{
  detail::audio_record const *rec = find_record (m_audio_table, id.value);
  return (rec != nullptr) ? rec->audio : nullptr;
}

rsc::audio_state
rsc::resource_manager::state (audio_id id) const
{
  if (const detail::audio_record *rec = find_record (m_audio_table, id.value)) {
    return rec->state;
  }
  return audio_state::not_loaded;
}

bool
rsc::resource_manager::contains (audio_id id) const
{
  return m_audio_table.contains (id.value);
}

std::optional<rsc::audio_resource_info>
rsc::resource_manager::info (audio_id id) const
{
  if (const detail::audio_record *rec = find_record (m_audio_table, id.value)) {
    return audio_resource_info{
      .id = id.value, .path = rec->path, .name = rec->name, .state = rec->state
    };
  }
  return std::nullopt;
}

std::vector<rsc::audio_resource_info>
rsc::resource_manager::list_audio () const
{
  std::vector<audio_resource_info> infos;
  infos.reserve (m_audio_table.size ());
  for (const auto &[id, rec] : m_audio_table) {
    infos.push_back (audio_resource_info{
        .id = id, .path = rec.path, .name = rec.name, .state = rec.state });
  }
  sort_infos (infos);
  return infos;
}

rsc::skeleton_id
rsc::resource_manager::register_skeleton (const std::string &path)
{
  std::string normalized = path;
  if (m_active_project && path.rfind ("res://", 0) != 0) {
    std::filesystem::path const root (m_active_project->root_path);
    std::filesystem::path const p (path);
    if (path.find (m_active_project->root_path) == 0) {
      normalized
          = "res://" + std::filesystem::relative (p, root).generic_string ();
    }
  }

  if (auto it = m_skeleton_ids_by_path.find (normalized);
      it != m_skeleton_ids_by_path.end ()) {
    return skeleton_id{ it->second };
  }

  const entt::id_type id = entt::hashed_string{ normalized.c_str () };
  m_skeleton_table.try_emplace (
      id, detail::skeleton_record{ .path = normalized,
                                   .name = basename_no_ext (normalized),
                                   .state = skeleton_state::not_loaded });
  m_skeleton_ids_by_path[normalized] = id;
  return skeleton_id{ id };
}

ozz::animation::Skeleton *
rsc::resource_manager::load (skeleton_id id)
{
  detail::skeleton_record *rec = find_record (m_skeleton_table, id.value);
  if (rec == nullptr) {
    return nullptr;
  }
  if (rec->state == skeleton_state::loaded) {
    return rec->skeleton.get ();
  }

  std::string const resolved = resolve_path (rec->path);
  rec->skeleton = skeleton_loader::load (resolved);
  rec->state = rec->skeleton != nullptr ? skeleton_state::loaded
                                        : skeleton_state::not_loaded;
  return rec->skeleton.get ();
}

void
rsc::resource_manager::unload (skeleton_id id)
{
  detail::skeleton_record *rec = find_record (m_skeleton_table, id.value);
  if (rec != nullptr) {
    rec->skeleton.reset ();
    rec->state = skeleton_state::not_loaded;
  }
}

ozz::animation::Skeleton *
rsc::resource_manager::get (skeleton_id id)
{
  detail::skeleton_record const *rec = find_record (m_skeleton_table, id.value);
  return (rec != nullptr) ? rec->skeleton.get () : nullptr;
}

rsc::skeleton_state
rsc::resource_manager::state (skeleton_id id) const
{
  if (const detail::skeleton_record *rec
      = find_record (m_skeleton_table, id.value)) {
    return rec->state;
  }
  return skeleton_state::not_loaded;
}

bool
rsc::resource_manager::contains (skeleton_id id) const
{
  return m_skeleton_table.contains (id.value);
}

std::optional<rsc::skeleton_resource_info>
rsc::resource_manager::info (skeleton_id id) const
{
  if (const detail::skeleton_record *rec
      = find_record (m_skeleton_table, id.value)) {
    return skeleton_resource_info{
      .id = id.value, .path = rec->path, .name = rec->name, .state = rec->state
    };
  }
  return std::nullopt;
}

std::vector<rsc::skeleton_resource_info>
rsc::resource_manager::list_skeletons () const
{
  std::vector<skeleton_resource_info> infos;
  infos.reserve (m_skeleton_table.size ());
  for (const auto &[id, rec] : m_skeleton_table) {
    infos.push_back (skeleton_resource_info{
        .id = id, .path = rec.path, .name = rec.name, .state = rec.state });
  }
  sort_infos (infos);
  return infos;
}

rsc::animation_id
rsc::resource_manager::register_animation (const std::string &path)
{
  std::string normalized = path;
  if (m_active_project && path.rfind ("res://", 0) != 0) {
    std::filesystem::path const root (m_active_project->root_path);
    std::filesystem::path const p (path);
    if (path.find (m_active_project->root_path) == 0) {
      normalized
          = "res://" + std::filesystem::relative (p, root).generic_string ();
    }
  }

  if (auto it = m_animation_ids_by_path.find (normalized);
      it != m_animation_ids_by_path.end ()) {
    return animation_id{ it->second };
  }

  const entt::id_type id = entt::hashed_string{ normalized.c_str () };
  m_animation_table.try_emplace (
      id, detail::animation_record{ .path = normalized,
                                    .name = basename_no_ext (normalized),
                                    .state = animation_state::not_loaded });
  m_animation_ids_by_path[normalized] = id;
  return animation_id{ id };
}

ozz::animation::Animation *
rsc::resource_manager::load (animation_id id)
{
  detail::animation_record *rec = find_record (m_animation_table, id.value);
  if (rec == nullptr) {
    return nullptr;
  }
  if (rec->state == animation_state::loaded) {
    return rec->animation.get ();
  }

  std::string const resolved = resolve_path (rec->path);
  rec->animation = animation_loader::load (resolved);
  rec->state = rec->animation != nullptr ? animation_state::loaded
                                         : animation_state::not_loaded;
  return rec->animation.get ();
}

void
rsc::resource_manager::unload (animation_id id)
{
  detail::animation_record *rec = find_record (m_animation_table, id.value);
  if (rec != nullptr) {
    rec->animation.reset ();
    rec->state = animation_state::not_loaded;
  }
}

ozz::animation::Animation *
rsc::resource_manager::get (animation_id id)
{
  detail::animation_record const *rec
      = find_record (m_animation_table, id.value);
  return (rec != nullptr) ? rec->animation.get () : nullptr;
}

rsc::animation_state
rsc::resource_manager::state (animation_id id) const
{
  if (const detail::animation_record *rec
      = find_record (m_animation_table, id.value)) {
    return rec->state;
  }
  return animation_state::not_loaded;
}

bool
rsc::resource_manager::contains (animation_id id) const
{
  return m_animation_table.contains (id.value);
}

std::optional<rsc::animation_resource_info>
rsc::resource_manager::info (animation_id id) const
{
  if (const detail::animation_record *rec
      = find_record (m_animation_table, id.value)) {
    return animation_resource_info{
      .id = id.value, .path = rec->path, .name = rec->name, .state = rec->state
    };
  }
  return std::nullopt;
}

std::vector<rsc::animation_resource_info>
rsc::resource_manager::list_animations () const
{
  std::vector<animation_resource_info> infos;
  infos.reserve (m_animation_table.size ());
  for (const auto &[id, rec] : m_animation_table) {
    infos.push_back (animation_resource_info{
        .id = id, .path = rec.path, .name = rec.name, .state = rec.state });
  }
  sort_infos (infos);
  return infos;
}

rsc::ui_layout_id
rsc::resource_manager::register_ui_layout (const std::string &path)
{
  std::string normalized = path;
  if (m_active_project && path.rfind ("res://", 0) != 0) {
    std::filesystem::path const root (m_active_project->root_path);
    std::filesystem::path const p (path);
    if (path.find (m_active_project->root_path) == 0) {
      normalized
          = "res://" + std::filesystem::relative (p, root).generic_string ();
    }
  }

  const entt::id_type id = entt::hashed_string{ normalized.c_str () };
  m_ui_layout_table.try_emplace (
      id, detail::ui_layout_record{ .path = normalized,
                                    .name = basename_no_ext (normalized),
                                    .state = ui_layout_state::loaded });
  return ui_layout_id{ id };
}

rsc::font_id
rsc::resource_manager::register_font (const std::string &path)
{
  std::string normalized = path;
  if (m_active_project && path.rfind ("res://", 0) != 0) {
    std::filesystem::path const root (m_active_project->root_path);
    std::filesystem::path const p (path);
    if (path.find (m_active_project->root_path) == 0) {
      normalized
          = "res://" + std::filesystem::relative (p, root).generic_string ();
    }
  }

  const entt::id_type id = entt::hashed_string{ normalized.c_str () };
  m_font_table.try_emplace (
      id, detail::font_record{ .path = normalized,
                               .name = basename_no_ext (normalized) });
  return font_id{ id };
}

std::optional<rsc::ui_layout_resource_info>
rsc::resource_manager::info (ui_layout_id id) const
{
  if (const detail::ui_layout_record *rec
      = find_record (m_ui_layout_table, id.value)) {
    return ui_layout_resource_info{
      .id = id.value, .path = rec->path, .name = rec->name, .state = rec->state
    };
  }
  return std::nullopt;
}

std::vector<rsc::ui_layout_resource_info>
rsc::resource_manager::list_ui_layouts () const
{
  std::vector<ui_layout_resource_info> infos;
  infos.reserve (m_ui_layout_table.size ());
  for (const auto &[id, rec] : m_ui_layout_table) {
    infos.push_back (ui_layout_resource_info{
        .id = id, .path = rec.path, .name = rec.name, .state = rec.state });
  }
  sort_infos (infos);
  return infos;
}

std::optional<rsc::font_resource_info>
rsc::resource_manager::info (font_id id) const
{
  if (const detail::font_record *rec = find_record (m_font_table, id.value)) {
    return font_resource_info{ .id = id.value,
                               .path = rec->path,
                               .name = rec->name };
  }
  return std::nullopt;
}

std::vector<rsc::font_resource_info>
rsc::resource_manager::list_fonts () const
{
  std::vector<font_resource_info> infos;
  infos.reserve (m_font_table.size ());
  for (const auto &[id, rec] : m_font_table) {
    infos.push_back (
        font_resource_info{ .id = id, .path = rec.path, .name = rec.name });
  }
  sort_infos (infos);
  return infos;
}

rsc::resource_manager::~resource_manager () { shutdown (); }

void
rsc::resource_manager::shutdown ()
{
  if (m_shutdown) {
    return;
  }

  clear_all_resources (false);
  if (m_mixer != nullptr) {
    MIX_DestroyMixer (m_mixer);
    m_mixer = nullptr;
  }
  MIX_Quit ();

  m_editor_ctx = nullptr;
  m_runtime_ctx = nullptr;
  m_shutdown = true;
}

void
rsc::resource_manager::clear_all_resources (bool restore_builtin_defaults)
{
  if (m_clearing) {
    return;
  }
  m_clearing = true;

  // Wait for the GPU first so we do not destroy in-flight resources.
  if ((m_runtime_ctx != nullptr)
      && (m_runtime_ctx->render_ctx ().gpu_device != nullptr)) {
    // Only wait if SDL is still initialized and the device is valid
    SDL_WaitForGPUIdle (m_runtime_ctx->render_ctx ().gpu_device);
  }

  for (auto &[id, rec] : m_audio_table) {
    if (rec.audio != nullptr) {
      MIX_DestroyAudio (rec.audio);
    }
  }
  m_audio_table.clear ();
  m_audio_ids_by_path.clear ();

  m_skeleton_table.clear ();
  m_animation_table.clear ();
  m_skeleton_ids_by_path.clear ();
  m_animation_ids_by_path.clear ();

  m_model_table.clear ();
  m_image_table.clear ();
  m_cubemap_table.clear ();
  m_scene_table.clear ();
  m_ui_layout_table.clear ();
  m_font_table.clear ();
  m_shader_table.clear ();
  m_model_ids_by_path.clear ();

  m_models.clear ();
  m_images.clear ();
  m_cubemaps.clear ();
  m_shaders.clear ();
  m_scenes.clear ();
  m_loaded_scene_instances.clear ();
  m_preferred_default_scene_id = entt::null;
  m_waiting_for_preferred_default_scene = false;

  m_preview_model_id = entt::null;
  m_low_lod_only_models.clear ();
  m_cancel_models.clear ();
  m_cancel_images.clear ();
  m_cancel_cubemaps.clear ();
  m_cancel_scenes.clear ();
  m_active_project.reset ();

  // Shut down systems properly before destroying scenes to avoid stale context
  // access. Auxiliary managers (like editor-only engine resources) must not
  // tear down the shared runtime world.
  if (m_manages_runtime_state && (m_runtime_ctx != nullptr)) {
    for (auto &scene : m_runtime_ctx->world ().get_scenes ()) {
      if (scene) {
        scene->stop_and_clear ();
      }
    }

    m_runtime_ctx->scene_manager ().set_active (nullptr);

    // Sync core systems while the old scene's registry is still valid
    if (m_runtime_ctx->core_systems ()) {
      m_runtime_ctx->core_systems ()->sync_activation ();
    }

    m_runtime_ctx->world ().clear ();
    m_runtime_ctx->event_hub ().clear_connections ();
  }

  if (restore_builtin_defaults) {
    register_builtin_models ();
    register_builtin_cubemaps ();
  }
  m_clearing = false;
}

bool
rsc::resource_manager::new_project (const rsc::project &proj)
{
  wsl::log::rsc ()->info ("Creating new project: {}", proj.name);
  if (!m_project_loader.create (proj)) {
    wsl::log::rsc ()->error ("Failed to create project");
    return false;
  }

  const std::string project_file
      = (fs::path (proj.root_path) / project_loader::manifest_file).string ();
  return load_project (project_file);
}

bool
rsc::resource_manager::load_project (const std::string &path)
{
  if (m_active_project_load && m_active_project_load->assets_job.valid ()) {
    wsl::log::rsc ()->warn (
        "Project load already in progress, ignoring duplicate request for {}",
        path);
    return true;
  }

  std::string actual_path = path;
  if (!fs::exists (actual_path) && fs::path (path).filename () == path) {
    fs::path p = fs::current_path ();
    while (p.has_parent_path ()) {
      if (fs::exists (p / path)) {
        actual_path = (p / path).string ();
        break;
      }
      p = p.parent_path ();
    }
  }

  std::shared_ptr<rsc::project> const proj
      = m_project_loader.load (actual_path);
  if (!proj) {
    wsl::log::rsc ()->error ("Project manifest loading failed for {}", path);
    return false;
  }

  if (m_editor_ctx != nullptr) {
    m_editor_ctx->is_loading_project (true);
  }

  const bool has_runtime_code
      = std::filesystem::exists (std::filesystem::path (proj->root_path)
                                 / proj->systems_path)
        || std::filesystem::exists (std::filesystem::path (proj->root_path)
                                    / proj->components_path)
        || std::filesystem::exists (std::filesystem::path (proj->root_path)
                                    / proj->singletons_path);

  m_active_project_load = project_load_job{
    .project_data = proj,
    .assets_job = std::async (
        std::launch::async,
        [this, proj] () { return m_project_loader.scan_assets (*proj); }),
    .has_runtime_code = has_runtime_code
  };

  return true;
}

std::shared_ptr<rsc::project>
rsc::resource_manager::current_project () const
{
  return m_active_project;
}

void
rsc::resource_manager::register_builtin_models ()
{
  for (const auto &primitive : model_loader::builtin_primitives ()) {
    static_cast<void> (register_model (std::string (primitive.path)));
  }
}

void
rsc::resource_manager::register_builtin_cubemaps ()
{
  m_cubemap_table.try_emplace (
      builtin_skybox_procedural,
      detail::cubemap_record{ .path = "builtin/skybox_procedural",
                              .name = "Procedural Skybox",
                              .state = cubemap_state::not_loaded });
}

void
rsc::resource_manager::update_async_uploads ()
{
  const auto activate_loaded_fallback_scene = [this] () {
    if (m_runtime_ctx->scene_manager ().get_active ()) {
      return;
    }

    for (const auto &[id, scene] : m_loaded_scene_instances) {
      (void)id;
      if (scene) {
        m_runtime_ctx->scene_manager ().set_active (scene);
        return;
      }
    }
  };

  if (m_active_project_load && m_active_project_load->assets_job.valid ()
      && m_active_project_load->assets_job.wait_for (std::chrono::seconds (0))
             == std::future_status::ready) {
    project_assets assets = m_active_project_load->assets_job.get ();
    std::shared_ptr<rsc::project> const proj
        = m_active_project_load->project_data;

    if (m_active_project_load->has_runtime_code) {
      auto &runtime_module = m_runtime_ctx->runtime_project_module ();
      if (runtime_module.compile_and_load (*proj)) {
        if (!runtime_module.finalize_load ()) {
          wsl::log::rsc ()->warn (
              "Project runtime module failed to register: {}",
              runtime_module.last_error ());
        }
      } else {
        wsl::log::rsc ()->warn (
            "Project runtime module failed to compile/load: {}",
            runtime_module.last_status ());
      }
    }

    clear_all_resources ();
    m_active_project = proj;

    for (const std::string &p : assets.models) {
      static_cast<void> (register_model (p));
    }

    for (const std::string &p : assets.images) {
      static_cast<void> (register_image (p));
    }

    for (const std::string &p : assets.cubemaps) {
      static_cast<void> (register_cubemap (p));
    }

    m_preferred_default_scene_id = entt::null;
    m_waiting_for_preferred_default_scene = false;

    const std::string default_scene_full_path
        = !proj->default_scene_path.empty ()
              ? (fs::path (proj->root_path) / proj->default_scene_path)
                    .string ()
              : "";

    for (std::size_t i = 0; i < assets.scenes.size (); ++i) {
      const std::string &p = assets.scenes[i];
      const scene_id id = register_scene (p);
      if (!default_scene_full_path.empty ()
          && fs::exists (default_scene_full_path)
          && fs::equivalent (p, default_scene_full_path)) {
        m_preferred_default_scene_id = id.value;
        m_waiting_for_preferred_default_scene = true;
      }
      static_cast<void> (load (id));
    }

    if (m_preferred_default_scene_id == entt::null) {
      m_waiting_for_preferred_default_scene = false;
    }

    for (const std::string &p : assets.audio) {
      static_cast<void> (register_audio (p));
    }

    for (const std::string &p : assets.skeletons) {
      static_cast<void> (register_skeleton (p));
    }

    for (const std::string &p : assets.animations) {
      static_cast<void> (register_animation (p));
    }

    for (const std::string &p : assets.ui_layouts) {
      static_cast<void> (register_ui_layout (p));
    }

    for (const std::string &p : assets.fonts) {
      static_cast<void> (register_font (p));
    }

    for (const std::string &p : assets.shaders) {
      static_cast<void> (register_shader (p));
    }

    wsl::log::rsc ()->info (
        "Project '{}' fully loaded through resource_manager", proj->name);

    if (m_editor_ctx != nullptr) {
      m_editor_ctx->is_loading_project (false);
      m_editor_ctx->re_register_editor_resources ();
    }
    m_active_project_load.reset ();
  }

  for (auto &[id, rec] : m_model_table) {
    if (rec.state != model_state::loading_cpu) {
      continue;
    }

    if (!rec.job.valid ()) {
      continue;
    }

    if (rec.job.wait_for (std::chrono::seconds (0))
        == std::future_status::ready) {

      std::shared_ptr<raw::cpu_model> const cpu_model = rec.job.get ();

      // If cancelled while loading, discard and reset
      if (m_cancel_models.contains (id)) {
        rec.cpu_data.reset ();
        rec.upload.reset ();
        rec.state = model_state::not_loaded;
        m_cancel_models.erase (id);
        continue;
      }

      if (cpu_model) {
        rec.cpu_data = cpu_model;
        rec.state = model_state::preparing_gpu;
      } else {
        rec.state = model_state::not_loaded;
      }
    }
  }

  for (auto &[id, rec] : m_model_table) {
    if (rec.state != model_state::preparing_gpu) {
      continue;
    }

    if (m_cancel_models.contains (id)) {
      rec.cpu_data.reset ();
      rec.upload.reset ();
      rec.state = model_state::not_loaded;
      m_cancel_models.erase (id);
      continue;
    }

    model_loader const loader{ &m_runtime_ctx->render_ctx () };
    rec.upload
        = std::make_unique<model_loader::upload_session> (loader.begin_upload (
            *rec.cpu_data, model_loader::upload_options{
                               m_low_lod_only_models.contains (id) }));
    rec.state = model_state::uploading_gpu;
  }

  constexpr size_t max_tasks_per_frame = 2;

  for (auto &[id, rec] : m_model_table) {
    if (rec.state != model_state::uploading_gpu) {
      continue;
    }

    if (m_cancel_models.contains (id)) {
      SDL_WaitForGPUIdle (m_runtime_ctx->render_ctx ().gpu_device);

      rec.upload.reset ();
      rec.cpu_data.reset ();
      m_models.erase (id);

      rec.state = model_state::not_loaded;
      m_cancel_models.erase (id);
      continue;
    }

    model_loader const loader{ &m_runtime_ctx->render_ctx () };
    if (!rec.upload) {
      rec.state = model_state::not_loaded;
      continue;
    }

    loader.upload_next_batch (*rec.upload, *rec.cpu_data, max_tasks_per_frame);

    if (loader.is_upload_complete (*rec.upload)) {
      gfx::model_3d ready_model
          = loader.finish_upload (*rec.upload, *rec.cpu_data);

      m_models.force_load (id, std::move (ready_model));

      rec.upload.reset ();
      rec.cpu_data.reset ();
      rec.state = model_state::loaded;
    }
  }

  for (auto it = m_image_table.begin (); it != m_image_table.end (); ++it) {
    auto &rec = it->second;

    if (rec.state == image_state::loading
        && rec.job.wait_for (std::chrono::seconds (0))
               == std::future_status::ready) {
      std::shared_ptr<raw::image_cpu> const cpu_img = rec.job.get ();

      if (m_cancel_images.contains (it->first)) {
        rec.state = image_state::not_loaded;
        m_cancel_images.erase (it->first);
        continue;
      }

      if (cpu_img) {
        image_loader const loader;
        gfx::image gpu_img = loader.upload_gpu (
            m_runtime_ctx->render_ctx ().gpu_device, *cpu_img);
        if (gpu_img.texture) {
          m_images.force_load (it->first, std::move (gpu_img));
          rec.state = image_state::loaded;
        } else {
          rec.state = image_state::not_loaded;
        }
      } else {
        rec.state = image_state::not_loaded;
      }
    }
  }

  for (auto it = m_cubemap_table.begin (); it != m_cubemap_table.end (); ++it) {
    auto &rec = it->second;

    if (rec.state == cubemap_state::loading
        && rec.job.wait_for (std::chrono::seconds (0))
               == std::future_status::ready) {

      std::shared_ptr<gfx::cubemap> const cube = rec.job.get ();

      if (m_cancel_cubemaps.contains (it->first)) {
        rec.state = cubemap_state::not_loaded;
        m_cancel_cubemaps.erase (it->first);
        continue;
      }

      if (cube) {
        if (auto *rendering = m_runtime_ctx->get_active_rendering_manager ()) {
          auto &renderer = rendering->ensure_renderer (
              m_runtime_ctx->window (), m_runtime_ctx->render_ctx (), this);
          if (cube->equirect_to_bake != nullptr) {
            renderer.bake_equirect_to_cube (*cube, cube->equirect_to_bake);
            // We can release equirect_to_bake now as it is no longer needed
            SDL_ReleaseGPUTexture (m_runtime_ctx->render_ctx ().gpu_device,
                                   cube->equirect_to_bake);
            cube->equirect_to_bake = nullptr;
          }
          renderer.bake_ibl (*cube);
        }
        m_cubemaps.force_load (it->first, std::move (*cube));
        rec.state = cubemap_state::loaded;
      } else {
        rec.state = cubemap_state::not_loaded;
      }
    }
  }

  for (auto it = m_scene_table.begin (); it != m_scene_table.end (); ++it) {
    auto &rec = it->second;

    if (rec.state == scene_state::loading
        && rec.job.wait_for (std::chrono::seconds (0))
               == std::future_status::ready) {
      std::shared_ptr<rsc::scene> scn;
      try {
        scn = rec.job.get ();
      } catch (const std::exception &e) {
        wsl::log::rsc ()->error ("Scene load job failed for '{}': {}", rec.path,
                                 e.what ());
        rec.state = scene_state::not_loaded;
        if (m_waiting_for_preferred_default_scene
            && it->first == m_preferred_default_scene_id) {
          m_waiting_for_preferred_default_scene = false;
          activate_loaded_fallback_scene ();
        }
        continue;
      } catch (...) {
        wsl::log::rsc ()->error (
            "Scene load job failed for '{}': unknown exception", rec.path);
        rec.state = scene_state::not_loaded;
        if (m_waiting_for_preferred_default_scene
            && it->first == m_preferred_default_scene_id) {
          m_waiting_for_preferred_default_scene = false;
          activate_loaded_fallback_scene ();
        }
        continue;
      }

      if (m_cancel_scenes.contains (it->first)) {
        rec.state = scene_state::not_loaded;
        m_cancel_scenes.erase (it->first);
        if (m_waiting_for_preferred_default_scene
            && it->first == m_preferred_default_scene_id) {
          m_waiting_for_preferred_default_scene = false;
          activate_loaded_fallback_scene ();
        }
        continue;
      }

      if (scn) {
        rsc::scene &world_scene
            = m_runtime_ctx->scene_manager ().get_world ().add_scene (
                std::move (*scn));
        m_loaded_scene_instances[it->first] = &world_scene;
        rec.state = scene_state::loaded;

        for (const io::resource_ref &r : world_scene.get_load_list ()) {
          load (r);
        }

        if (m_waiting_for_preferred_default_scene
            && it->first == m_preferred_default_scene_id) {
          m_runtime_ctx->scene_manager ().set_active (&world_scene);
          m_waiting_for_preferred_default_scene = false;
        } else if ((m_runtime_ctx->scene_manager ().get_active () == nullptr)
                   && !m_waiting_for_preferred_default_scene) {
          m_runtime_ctx->scene_manager ().set_active (&world_scene);
        }
      } else {
        rec.state = scene_state::not_loaded;
        if (m_waiting_for_preferred_default_scene
            && it->first == m_preferred_default_scene_id) {
          m_waiting_for_preferred_default_scene = false;
          activate_loaded_fallback_scene ();
        }
      }
    }
  }
}

void
rsc::resource_manager::unload (model_id id)
{
  detail::model_record *rec = find_record (m_model_table, id.value);
  if (rec == nullptr) {
    return;
  }

  // If currently loading/preparing/uploading: mark cancelled and let
  // update_async_uploads discard it.
  if (rec->state != model_state::not_loaded
      && rec->state != model_state::loaded) {
    m_cancel_models.insert (id.value);
    // don't touch rec.job here (avoids blocking future destructor)
    return;
  }

  if (rec->state == model_state::loaded) {
    SDL_WaitForGPUIdle (m_runtime_ctx->render_ctx ().gpu_device);
    m_models.erase (id.value);
    rec->state = model_state::not_loaded;
  }
}

void
rsc::resource_manager::unload (image_id id)
{
  detail::image_record *rec = find_record (m_image_table, id.value);
  if (rec == nullptr) {
    return;
  }

  if (rec->state == image_state::loading) {
    m_cancel_images.insert (id.value);
    return;
  }

  if (rec->state == image_state::loaded) {
    SDL_WaitForGPUIdle (m_runtime_ctx->render_ctx ().gpu_device);
    m_images.erase (id.value);
    rec->state = image_state::not_loaded;
  }
}

void
rsc::resource_manager::unload (cubemap_id id)
{
  detail::cubemap_record *rec = find_record (m_cubemap_table, id.value);
  if (rec == nullptr) {
    return;
  }

  if (rec->state == cubemap_state::loading) {
    m_cancel_cubemaps.insert (id.value);
    return;
  }

  if (rec->state == cubemap_state::loaded) {
    SDL_WaitForGPUIdle (m_runtime_ctx->render_ctx ().gpu_device);
    m_cubemaps.erase (id.value);
    rec->state = cubemap_state::not_loaded;
  }
}

void
rsc::resource_manager::unload (scene_id id)
{
  detail::scene_record *rec = find_record (m_scene_table, id.value);
  if (rec == nullptr) {
    return;
  }

  if (rec->state == scene_state::loading) {
    m_cancel_scenes.insert (id.value);
    return;
  }

  if (rec->state == scene_state::loaded) {
    SDL_WaitForGPUIdle (m_runtime_ctx->render_ctx ().gpu_device);
    if (scene *scene = find_loaded_scene (id)) {
      m_runtime_ctx->scene_manager ().destroy_scene (scene);
      m_loaded_scene_instances.erase (id.value);
    }

    if (m_scenes.contains (id.value)) {
      m_scenes.erase (id.value);
    }

    rec->state = scene_state::not_loaded;
  }
}

void
rsc::resource_manager::unload (io::resource_ref ref)
{
  switch (ref.type) {
  case io::resource_type::model:
    unload (model_id{ ref.id });
    break;
  case io::resource_type::image:
    unload (image_id{ ref.id });
    break;
  case io::resource_type::cubemap:
    unload (cubemap_id{ ref.id });
    break;
  case io::resource_type::scene:
    unload (scene_id{ ref.id });
    break;
  case io::resource_type::audio:
    unload (audio_id{ ref.id });
    break;
  case io::resource_type::material:
    unload (material_id{ ref.id });
    break;
  }
}

void
rsc::resource_manager::load_preview_model_low_lod (model_id id)
{
  // if same, keep it
  if (m_preview_model_id == id.value) {
    return;
  }

  // unload previous preview model (only the preview one)
  unload_preview_model ();

  m_preview_model_id = id.value;
  m_low_lod_only_models.insert (id.value);

  // request load
  static_cast<void> (load (id));
}

void
rsc::resource_manager::unload_preview_model ()
{
  if (m_preview_model_id == entt::null) {
    return;
  }

  // clear low-lod policy
  m_low_lod_only_models.erase (m_preview_model_id);

  // unload the resource itself
  unload (model_id{ m_preview_model_id });

  m_preview_model_id = entt::null;
}

rsc::model_id
rsc::resource_manager::current_preview_model () const
{
  return model_id{ m_preview_model_id };
}

void
rsc::resource_manager::release_preview_ownership_if_matches (model_id id)
{
  if (m_preview_model_id == id.value) {
    m_low_lod_only_models.erase (m_preview_model_id);
    m_preview_model_id = entt::null;
  }
}

bool
rsc::resource_manager::is_preview_owned (model_id id) const
{
  return m_preview_model_id == id.value;
}

std::string
rsc::resource_manager::resolve_path (const std::string &path) const
{
  if (path.rfind ("builtin://", 0) == 0) {
    return path;
  }

  if (path.rfind ("res://", 0) == 0) {
    if (!m_active_project) {
      return path.substr (6);
    }
    return (std::filesystem::path (m_active_project->root_path)
            / path.substr (6))
        .string ();
  }

  if (path.rfind ("engine://", 0) == 0) {
    const std::string sub_path = path.substr (9);

    // Try the installed/ packaged layout first (share/weasel/...)
    std::filesystem::path installed_path
        = std::filesystem::path (m_wsl_resource_path) / "share/weasel"
          / sub_path;
    if (std::filesystem::exists (installed_path)) {
      return installed_path.string ();
    }

    // Fall back to the legacy / development layout directly under the resource
    // path
    std::filesystem::path base_path
        = std::filesystem::path (m_wsl_resource_path) / sub_path;

    // Apply shader extension replacement for known shader bytecode extensions,
    // swapping to the platform-native format (.spv -> .metal on macOS, etc.)
    std::string ext = base_path.extension ().string ();
    bool has_non_shader_ext
        = (!ext.empty () && ext != ".hlsl" && ext != ".HLSL" && ext != ".spv"
           && ext != ".SPV" && ext != ".dxil" && ext != ".DXIL"
           && ext != ".metal" && ext != ".METAL");

    if (!has_non_shader_ext) {
#if defined(_WIN32)
      std::filesystem::path dxil_path = base_path;
      dxil_path.replace_extension (".dxil");
      if (std::filesystem::exists (dxil_path)) {
        return dxil_path.string ();
      }
#elif defined(__APPLE__)
      std::filesystem::path metal_path = base_path;
      metal_path.replace_extension (".metal");
      if (std::filesystem::exists (metal_path)) {
        return metal_path.string ();
      }
#else
      std::filesystem::path spv_path = base_path;
      spv_path.replace_extension (".spv");
      if (std::filesystem::exists (spv_path)) {
        return spv_path.string ();
      }
#endif
    }

    return base_path.string ();
  }

  return path;
}

std::string
rsc::resource_manager::get_resource_path (model_id id) const
{
  if (const detail::model_record *rec = find_record (m_model_table, id.value)) {
    // If it's builtin:// or res:// already, return as is.
    if (rec->path.rfind ("builtin://", 0) == 0
        || rec->path.rfind ("res://", 0) == 0) {
      return rec->path;
    }

    // Otherwise try to make it res:// relative if possible.
    if (m_active_project) {
      std::filesystem::path const root (m_active_project->root_path);
      std::filesystem::path const p (rec->path);
      if (rec->path.find (m_active_project->root_path) == 0) {
        return "res://" + std::filesystem::relative (p, root).generic_string ();
      }
    }
    return rec->path;
  }
  return "None";
}

std::string
rsc::resource_manager::get_resource_path (cubemap_id id) const
{
  if (const detail::cubemap_record *rec
      = find_record (m_cubemap_table, id.value)) {
    return rec->path;
  }
  return "None";
}

std::string
rsc::resource_manager::get_resource_path (audio_id id) const
{
  if (const detail::audio_record *rec = find_record (m_audio_table, id.value)) {
    // If it's res:// already, return as is.
    if (rec->path.rfind ("res://", 0) == 0) {
      return rec->path;
    }

    // Otherwise try to make it res:// relative if possible.
    if (m_active_project) {
      std::filesystem::path const root (m_active_project->root_path);
      std::filesystem::path const p (rec->path);
      if (rec->path.find (m_active_project->root_path) == 0) {
        return "res://" + std::filesystem::relative (p, root).generic_string ();
      }
    }
    return rec->path;
  }
  return "None";
}

std::string
rsc::resource_manager::get_resource_path (skeleton_id id) const
{
  if (const detail::skeleton_record *rec
      = find_record (m_skeleton_table, id.value)) {
    if (rec->path.rfind ("res://", 0) == 0) {
      return rec->path;
    }

    if (m_active_project) {
      std::filesystem::path const root (m_active_project->root_path);
      std::filesystem::path const p (rec->path);
      if (rec->path.find (m_active_project->root_path) == 0) {
        return "res://" + std::filesystem::relative (p, root).generic_string ();
      }
    }
    return rec->path;
  }
  return "None";
}

std::string
rsc::resource_manager::get_resource_path (animation_id id) const
{
  if (const detail::animation_record *rec
      = find_record (m_animation_table, id.value)) {
    if (rec->path.rfind ("res://", 0) == 0) {
      return rec->path;
    }

    if (m_active_project) {
      std::filesystem::path const root (m_active_project->root_path);
      std::filesystem::path const p (rec->path);
      if (rec->path.find (m_active_project->root_path) == 0) {
        return "res://" + std::filesystem::relative (p, root).generic_string ();
      }
    }
    return rec->path;
  }
  return "None";
}

std::string
rsc::resource_manager::get_resource_path (material_id id) const
{
  if (std::optional<material_resource_info> rec = info (id)) {
    // If it's res:// already, return as is.
    if (rec->path.rfind ("res://", 0) == 0) {
      return rec->path;
    }

    // Otherwise try to make it res:// relative if possible.
    if (m_active_project) {
      std::filesystem::path const root (m_active_project->root_path);
      std::filesystem::path const p (rec->path);
      if (rec->path.find (m_active_project->root_path) == 0) {
        return "res://" + std::filesystem::relative (p, root).generic_string ();
      }
    }
    return rec->path;
  }
  return "None";
}

std::string
rsc::resource_manager::get_path (io::resource_ref ref) const
{
  switch (ref.type) {
  case io::resource_type::model:
    if (auto i = info (model_id{ ref.id })) {
      return i->path;
    }
    break;
  case io::resource_type::image:
    if (auto i = info (image_id{ ref.id })) {
      return i->path;
    }
    break;
  case io::resource_type::cubemap:
    if (auto i = info (cubemap_id{ ref.id })) {
      return i->path;
    }
    break;
  case io::resource_type::scene:
    if (auto i = info (scene_id{ ref.id })) {
      return i->path;
    }
    break;
  case io::resource_type::audio:
    if (auto i = info (audio_id{ ref.id })) {
      return i->path;
    }
    break;
  case io::resource_type::material:
    if (auto i = info (material_id{ ref.id })) {
      return i->path;
    }
    break;
  }
  return "None";
}

void
rsc::model_id::register_meta ()
{
  using namespace entt::literals;

  auto &&factory_mi = entt::meta_factory<rsc::model_id> ().type (
      entt::type_hash<rsc::model_id>::value ());

  // IMPORTANT: register the hook
  // Signature: bool model_id::custom_inspect(const char*,
  // runtime_context*)

  (factory_mi.data<&rsc::model_id::value>)("value"_hs);
}

void
rsc::audio_id::register_meta ()
{
  using namespace entt::literals;

  auto &&factory_ai = entt::meta_factory<rsc::audio_id> ().type (
      entt::type_hash<rsc::audio_id>::value ());
  (factory_ai.data<&rsc::audio_id::value>)("value"_hs);
}

void
rsc::material_id::register_meta ()
{
  using namespace entt::literals;

  auto &&factory_mati = entt::meta_factory<rsc::material_id> ().type (
      entt::type_hash<rsc::material_id>::value ());
  (factory_mati.data<&rsc::material_id::value>)("value"_hs);
}

void
rsc::image_id::register_meta ()
{
  using namespace entt::literals;

  auto &&factory_ii = entt::meta_factory<rsc::image_id> ().type (
      entt::type_hash<rsc::image_id>::value ());
  (factory_ii.data<&rsc::image_id::value>)("value"_hs);
}

rsc::shader_id
rsc::resource_manager::register_shader (const std::string &path)
{
  return shader_id{
    register_resource<std::unordered_map<entt::id_type, detail::shader_record>,
                      detail::shader_record> (m_shader_table, path,
                                              shader_state::not_loaded)
  };
}

rsc::resource_manager::shader_handle
rsc::resource_manager::load (shader_id id)
{
  detail::shader_record *rec = find_record (m_shader_table, id.value);
  if (rec == nullptr) {
    return {};
  }

  if (rec->state == shader_state::loaded) {
    return m_shaders[id.value];
  }

  std::string const resolved = resolve_path (rec->path);
  auto module = m_shaders.load (id.value, resolved);
  if (module.second) {
    rec->state = shader_state::loaded;
    return m_shaders[id.value];
  }

  return {};
}

void
rsc::resource_manager::unload (shader_id id)
{
  if (detail::shader_record *rec = find_record (m_shader_table, id.value)) {
    m_shaders.erase (id.value);
    rec->state = shader_state::not_loaded;
  }
}

rsc::resource_manager::shader_handle
rsc::resource_manager::get (shader_id id)
{
  return state (id) == shader_state::loaded ? m_shaders[id.value]
                                            : shader_handle{};
}

rsc::shader_state
rsc::resource_manager::state (shader_id id) const
{
  if (const detail::shader_record *rec
      = find_record (m_shader_table, id.value)) {
    return rec->state;
  }
  return shader_state::not_loaded;
}

bool
rsc::resource_manager::contains (shader_id id) const
{
  return find_record (m_shader_table, id.value) != nullptr;
}

std::optional<rsc::shader_resource_info>
rsc::resource_manager::info (shader_id id) const
{
  const detail::shader_record *rec = find_record (m_shader_table, id.value);
  if (rec == nullptr) {
    return std::nullopt;
  }

  return shader_resource_info{
    .id = id.value, .path = rec->path, .name = rec->name, .state = rec->state
  };
}

std::vector<rsc::shader_resource_info>
rsc::resource_manager::list_shaders () const
{
  std::vector<shader_resource_info> infos;
  infos.reserve (m_shader_table.size ());

  for (const auto &[id, rec] : m_shader_table) {
    infos.push_back (shader_resource_info{
        .id = id, .path = rec.path, .name = rec.name, .state = rec.state });
  }

  sort_infos (infos);
  return infos;
}

void
rsc::resource_manager::set_engine_resource_path (const std::string &path)
{
  m_wsl_resource_path = path;
}

// ---- Materials ----

rsc::material_id
rsc::resource_manager::register_material (const std::string &path)
{
  return material_id{ register_resource<
      std::unordered_map<entt::id_type, detail::material_record>,
      detail::material_record> (m_material_table, path,
                                material_state::not_loaded) };
}

rsc::material_id
rsc::resource_manager::import_material (const std::string &path,
                                        bool /*request_load*/)
{
  if (m_active_project == nullptr) {
    wsl::log::rsc ()->warn ("Cannot import material without active project");
    return material_id{};
  }

  fs::path const src (path);
  fs::path const dst_dir = fs::path (m_active_project->root_path)
                           / m_active_project->materials_path;
  fs::create_directories (dst_dir);
  fs::path const dst = dst_dir / src.filename ();

  try {
    fs::copy_file (src, dst, fs::copy_options::overwrite_existing);
  } catch (const std::exception &e) {
    wsl::log::rsc ()->error ("Failed to copy material: {}", e.what ());
    return material_id{};
  }

  return register_material (dst.string ());
}

rsc::resource_manager::material_handle
rsc::resource_manager::load (material_id id)
{
  detail::material_record *rec = find_record (m_material_table, id.value);
  if (rec == nullptr) {
    return nullptr;
  }

  auto it = m_materials.find (id.value);
  if (it != m_materials.end ()) {
    rec->state = material_state::loaded;
    return it->second;
  }

  std::string const resolved = resolve_path (rec->path);
  std::ifstream file (resolved);
  if (!file) {
    wsl::log::rsc ()->error ("Failed to open material file: {}", resolved);
    return nullptr;
  }

  try {
    std::string json_content ((std::istreambuf_iterator<char> (file)),
                              std::istreambuf_iterator<char> ());
    auto asset = std::make_shared<gfx::material_asset> ();
    // Materials are written with tagged variants (see
    // shader_graph_editor); mirror that dialect here so the variant
    // inside material_parameter round-trips without type drift.
    std::string read_error;
    bool parsed = serialize::json_read_p<gfx::material_asset,
                                         rfl::AddNamespacedTagsToVariants> (
        json_content, *asset, &read_error);
    if (!parsed) {
      // Older Clang builds wrote the unqualified reflect-cpp tags (for
      // example, "vec<3, float>"). Normalize those before giving up.
      const std::string migrated_json
          = migrate_legacy_material_variant_tags (json_content);
      if (migrated_json != json_content) {
        auto legacy_asset = std::make_shared<gfx::material_asset> ();
        std::string legacy_error;
        parsed = serialize::json_read_p<gfx::material_asset,
                                        rfl::AddNamespacedTagsToVariants> (
            migrated_json, *legacy_asset, &legacy_error);
        if (parsed) {
          *asset = std::move (*legacy_asset);
          read_error = std::move (legacy_error);
        }
      }
    }
    if (!parsed) {
      wsl::log::rsc ()->error ("Failed to parse material '{}': {}", resolved,
                               read_error);
      return nullptr;
    }
    asset->id = id;
    asset->path = rec->path;
    if (asset->name.empty ()) {
      asset->name = rec->name;
    }
    rec->shader_program_id = asset->shader_program.value;
    m_materials[id.value] = asset;
    rec->state = material_state::loaded;
    return asset;
  } catch (const std::exception &e) {
    wsl::log::rsc ()->error ("Failed to parse material '{}': {}", resolved,
                             e.what ());
    return nullptr;
  }
}

void
rsc::resource_manager::unload (material_id id)
{
  if (detail::material_record *rec = find_record (m_material_table, id.value)) {
    m_materials.erase (id.value);
    rec->state = material_state::not_loaded;
  }
}

rsc::resource_manager::material_handle
rsc::resource_manager::get (material_id id)
{
  auto it = m_materials.find (id.value);
  if (it != m_materials.end ()) {
    return it->second;
  }
  return load (id);
}

rsc::material_state
rsc::resource_manager::state (material_id id) const
{
  if (const detail::material_record *rec
      = find_record (m_material_table, id.value)) {
    return rec->state;
  }
  return material_state::not_loaded;
}

bool
rsc::resource_manager::contains (material_id id) const
{
  return find_record (m_material_table, id.value) != nullptr;
}

std::optional<rsc::material_resource_info>
rsc::resource_manager::info (material_id id) const
{
  const detail::material_record *rec = find_record (m_material_table, id.value);
  if (rec == nullptr) {
    return std::nullopt;
  }

  return material_resource_info{ .id = id.value,
                                 .path = rec->path,
                                 .name = rec->name,
                                 .state = rec->state,
                                 .shader_program_id = rec->shader_program_id };
}

std::vector<rsc::material_resource_info>
rsc::resource_manager::list_materials () const
{
  std::vector<material_resource_info> infos;
  infos.reserve (m_material_table.size ());

  for (const auto &[id, rec] : m_material_table) {
    infos.push_back (
        material_resource_info{ .id = id,
                                .path = rec.path,
                                .name = rec.name,
                                .state = rec.state,
                                .shader_program_id = rec.shader_program_id });
  }

  sort_infos (infos);
  return infos;
}

// ---- Shader Programs ----

rsc::shader_program_id
rsc::resource_manager::register_shader_program (const std::string &path)
{
  return shader_program_id{ register_resource<
      std::unordered_map<entt::id_type, detail::shader_program_record>,
      detail::shader_program_record> (m_shader_program_table, path,
                                      shader_program_state::not_loaded) };
}

rsc::shader_program_id
rsc::resource_manager::register_shader_program (
    std::shared_ptr<gfx::shader_program> prog, const std::string &name)
{
  entt::id_type id = entt::hashed_string{ name.c_str () };
  m_shader_program_table[id] = detail::shader_program_record{
    .path = "", .name = name, .state = shader_program_state::loaded
  };
  m_shader_programs[id] = std::move (prog);
  return shader_program_id{ id };
}

rsc::resource_manager::shader_program_handle
rsc::resource_manager::get (shader_program_id id)
{
  auto it = m_shader_programs.find (id.value);
  if (it != m_shader_programs.end ()) {
    return it->second;
  }
  return nullptr;
}

rsc::shader_program_state
rsc::resource_manager::state (shader_program_id id) const
{
  if (const detail::shader_program_record *rec
      = find_record (m_shader_program_table, id.value)) {
    return rec->state;
  }
  return shader_program_state::not_loaded;
}

bool
rsc::resource_manager::contains (shader_program_id id) const
{
  return find_record (m_shader_program_table, id.value) != nullptr;
}

std::optional<rsc::shader_program_resource_info>
rsc::resource_manager::info (shader_program_id id) const
{
  const detail::shader_program_record *rec
      = find_record (m_shader_program_table, id.value);
  if (rec == nullptr) {
    return std::nullopt;
  }

  return shader_program_resource_info{
    .id = id.value, .path = rec->path, .name = rec->name, .state = rec->state
  };
}

std::vector<rsc::shader_program_resource_info>
rsc::resource_manager::list_shader_programs () const
{
  std::vector<shader_program_resource_info> infos;
  infos.reserve (m_shader_program_table.size ());

  for (const auto &[id, rec] : m_shader_program_table) {
    infos.push_back (shader_program_resource_info{
        .id = id, .path = rec.path, .name = rec.name, .state = rec.state });
  }

  sort_infos (infos);
  return infos;
}

} // namespace wsl
