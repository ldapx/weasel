#pragma once

#include "animation_loader.hpp"
#include "cubemap_loader.hpp"
#include "image_loader.hpp"
#include "model_loader.hpp"
#include "project_loader.hpp"
#include "resource_ids.hpp"
#include "resource_ref.hpp"
#include "scene_loader.hpp"
#include "skeleton_loader.hpp"
#include "../comp/component_meta.hpp"
#include "shader_loader.hpp"
#include "wsl/gfx/material_asset.hpp"
#include "wsl/gfx/shader_program.hpp"
#include <SDL3_mixer/SDL_mixer.h>

#ifndef IN_MODULE_INTERFACE
#include <entt/entt.hpp>
#endif
#ifndef IN_MODULE_INTERFACE
#include <entt/resource/cache.hpp>
#endif
#ifndef IN_MODULE_INTERFACE
#include <entt/resource/resource.hpp>
#endif
#ifndef IN_MODULE_INTERFACE
#include <future>
#endif
#ifndef IN_MODULE_INTERFACE
#include <optional>
#endif
#ifndef IN_MODULE_INTERFACE
#include <string>
#endif
#ifndef IN_MODULE_INTERFACE
#include <unordered_map>
#endif
#ifndef IN_MODULE_INTERFACE
#include <unordered_set>
#endif
#ifndef IN_MODULE_INTERFACE
#include <vector>
#endif
namespace wsl
{

namespace comp::singl
{
class runtime_context;
class editor_context;
}

/**
 * Contains types and classes for managing engine resources, scenes, and
 * project metadata.
 */
namespace rsc
{

/** Internal implementation details for resource management. */
namespace detail
{
}

/**
 * Core structures for raw, CPU-side asset representation before GPU
 * upload.
 */
namespace raw
{
}

/**
 * Input/Output and serialization logic for scenes and resource
 * references.
 */
namespace io
{
}

/**
 * Tools for interacting with the xmake build system and project
 * configuration.
 */
namespace xmake
{
}

namespace detail
{
/**
 * constexpr hash of the procedural skybox builtin. Keeping the entt
 * literal in a function scope avoids a file-scope
 * `using namespace entt::literals;`, which used to leak into every
 * translation unit that includes this header.
 */
constexpr entt::id_type
skybox_procedural_id ()
{
  using namespace entt::literals;
  return "builtin/skybox_procedural"_hs;
}
} // namespace detail

constexpr entt::id_type builtin_skybox_procedural
    = detail::skybox_procedural_id ();

/** Represents the current loading state of a 3D model. */
enum class model_state
{
  not_loaded,
  loading_cpu,
  preparing_gpu,
  uploading_gpu,
  loaded
};
/** Represents the current loading state of an image. */
enum class image_state
{
  not_loaded,
  loading,
  loaded
};
/** Represents the current loading state of a cubemap. */
enum class cubemap_state
{
  not_loaded,
  loading,
  loaded
};
/** Represents the current loading state of a scene. */
enum class scene_state
{
  not_loaded,
  loading,
  loaded
};

/** Represents the current loading state of an audio asset. */
enum class audio_state
{
  not_loaded,
  loading,
  loaded
};

/** Represents the current loading state of a UI layout. */
enum class ui_layout_state
{
  not_loaded,
  loaded
};

/** Represents the current loading state of a shader module. */
enum class shader_state
{
  not_loaded,
  loading,
  loaded
};

/** Represents the current loading state of a material asset. */
enum class material_state
{
  not_loaded,
  loaded
};

/** Represents the current loading state of a shader program. */
enum class shader_program_state
{
  not_loaded,
  loaded
};

/** Represents the current loading state of a skeleton asset. */
enum class skeleton_state
{
  not_loaded,
  loaded
};

/** Represents the current loading state of an animation clip asset. */
enum class animation_state
{
  not_loaded,
  loaded
};

/** Metadata for a 3D model resource. */
struct model_resource_info
{
  entt::id_type id{};  // Unique identifier.
  std::string path;    // Path to the model file.
  std::string name;    // Human-readable name.
  model_state state{}; // Current loading state.
  bool preview_owned
      = false; // Whether the model is currently used as a preview.
  bool lowest_lod_only = false; // Whether only the lowest LOD is loaded.
};

/** Metadata for an image resource. */
struct image_resource_info
{
  entt::id_type id{};  // Unique identifier.
  std::string path;    // Path to the image file.
  std::string name;    // Human-readable name.
  image_state state{}; // Current loading state.
};

/** Metadata for a cubemap resource. */
struct cubemap_resource_info
{
  entt::id_type id{};    // Unique identifier.
  std::string path;      // Path to the cubemap file.
  std::string name;      // Human-readable name.
  cubemap_state state{}; // Current loading state.
};

/** Metadata for a scene resource. */
struct scene_resource_info
{
  entt::id_type id{};     // Unique identifier.
  std::string path;       // Path to the scene file.
  std::string name;       // Human-readable name.
  scene_state state{};    // Current loading state.
  bool is_prefab = false; // Whether the scene is a prefab.
};

/** Metadata for an audio resource. */
struct audio_resource_info
{
  entt::id_type id{};  // Unique identifier.
  std::string path;    // Path to the audio file.
  std::string name;    // Human-readable name.
  audio_state state{}; // Current loading state.
};

/** Metadata for a UI layout resource. */
struct ui_layout_resource_info
{
  entt::id_type id{};      // Unique identifier.
  std::string path;        // Path to the UI layout file.
  std::string name;        // Human-readable name.
  ui_layout_state state{}; // Current loading state.
};

/** Metadata for a font resource. */
struct font_resource_info
{
  entt::id_type id{}; // Unique identifier.
  std::string path;   // Path to the font file.
  std::string name;   // Human-readable name.
};

/** Metadata for a shader resource. */
struct shader_resource_info
{
  entt::id_type id{};   // Unique identifier.
  std::string path;     // Path to the shader file.
  std::string name;     // Human-readable name.
  shader_state state{}; // Current loading state.
};

/** Metadata for a material asset resource. */
struct material_resource_info
{
  entt::id_type id{};                   // Unique identifier.
  std::string path;                     // Path to the material file.
  std::string name;                     // Human-readable name.
  material_state state{};               // Current loading state.
  entt::id_type shader_program_id{ 0 }; // Referenced shader program.
};

/** Metadata for a shader program resource. */
struct shader_program_resource_info
{
  entt::id_type id{};           // Unique identifier.
  std::string path;             // Optional path (empty for runtime generated).
  std::string name;             // Human-readable name.
  shader_program_state state{}; // Current loading state.
};

/** Metadata for a skeleton resource (`.skel.ozz`). */
struct skeleton_resource_info
{
  entt::id_type id{};     // Unique identifier.
  std::string path;       // Path to the .skel.ozz file.
  std::string name;       // Human-readable name.
  skeleton_state state{}; // Current loading state.
};

/** Metadata for an animation clip resource (`.anim.ozz`). */
struct animation_resource_info
{
  entt::id_type id{};      // Unique identifier.
  std::string path;        // Path to the .anim.ozz file.
  std::string name;        // Human-readable name.
  animation_state state{}; // Current loading state.
};

namespace detail
{

/** Internal record tracking the state and data of a model resource. */
struct model_record
{
  std::string path;                                   // Path to the model file.
  std::string name;                                   // Human-readable name.
  model_state state = model_state::not_loaded;        // Current loading state.
  std::future<std::shared_ptr<raw::cpu_model>> job{}; // Async CPU loading job.
  std::shared_ptr<raw::cpu_model> cpu_data{};         // Loaded CPU data.
  std::unique_ptr<model_loader::upload_session>
      upload{}; // Active GPU upload session.
};

/** Internal record tracking the state and data of an image resource. */
struct image_record
{
  std::string path;                                   // Path to the image file.
  std::string name;                                   // Human-readable name.
  image_state state = image_state::not_loaded;        // Current loading state.
  std::future<std::shared_ptr<raw::image_cpu>> job{}; // Async loading job.
};

/** Internal record tracking the state and data of a cubemap resource. */
struct cubemap_record
{
  std::string path;                                 // Path to the cubemap file.
  std::string name;                                 // Human-readable name.
  cubemap_state state = cubemap_state::not_loaded;  // Current loading state.
  std::future<std::shared_ptr<gfx::cubemap>> job{}; // Async loading job.
};

/** Internal record tracking the state and data of a scene resource. */
struct scene_record
{
  std::string path;                            // Path to the scene file.
  std::string name;                            // Human-readable name.
  scene_state state = scene_state::not_loaded; // Current loading state.
  bool is_prefab = false;                      // Whether the scene is a prefab.
  std::future<std::shared_ptr<rsc::scene>> job{}; // Async loading job.
};

/** Internal record tracking the state and data of an audio resource. */
struct audio_record
{
  std::string path;                            // Path to the audio file.
  std::string name;                            // Human-readable name.
  audio_state state = audio_state::not_loaded; // Current loading state.
  MIX_Audio *audio = nullptr;                  // Pointer to loaded audio data.
};

/** Internal record tracking the state of a UI layout resource. */
struct ui_layout_record
{
  std::string path; // Path to the UI layout file.
  std::string name; // Human-readable name.
  ui_layout_state state = ui_layout_state::not_loaded; // Current state.
};

/** Internal record tracking the location of a font resource. */
struct font_record
{
  std::string path; // Path to the font file.
  std::string name; // Human-readable name.
};

/** Internal record tracking the state of a shader resource. */
struct shader_record
{
  std::string path;                              // Path to the shader file.
  std::string name;                              // Human-readable name.
  shader_state state = shader_state::not_loaded; // Current loading state.
};

/** Internal record tracking the state of a material asset. */
struct material_record
{
  std::string path; // Path to the material file.
  std::string name; // Human-readable name.
  material_state state = material_state::not_loaded; // Current loading state.
  entt::id_type shader_program_id{ 0 }; // Referenced shader program.
};

/** Internal record tracking the state of a shader program. */
struct shader_program_record
{
  std::string path; // Optional path.
  std::string name; // Human-readable name.
  shader_program_state state
      = shader_program_state::not_loaded; // Current loading state.
};

/** Internal record tracking the state and data of a skeleton resource. */
struct skeleton_record
{
  std::string path; // Path to the .skel.ozz file.
  std::string name; // Human-readable name.
  skeleton_state state = skeleton_state::not_loaded; // Loading state.
  /** Loaded runtime skeleton; owns its ozz-allocated memory. */
  ozz::unique_ptr<ozz::animation::Skeleton> skeleton{};
};

/** Internal record tracking the state and data of an animation resource. */
struct animation_record
{
  std::string path; // Path to the .anim.ozz file.
  std::string name; // Human-readable name.
  animation_state state = animation_state::not_loaded; // Loading state.
  /** Loaded runtime animation clip; owns its ozz-allocated memory. */
  ozz::unique_ptr<ozz::animation::Animation> animation{};
};

} // namespace detail

class resource_manager
{
public:
  using model_handle = entt::resource<gfx::model_3d>;
  using image_handle = entt::resource<gfx::image>;
  using cubemap_handle = entt::resource<gfx::cubemap>;
  using scene_handle = entt::resource<rsc::scene>;
  using shader_handle = entt::resource<gfx::shader_module>;
  using material_handle = std::shared_ptr<gfx::material_asset>;
  using shader_program_handle = std::shared_ptr<gfx::shader_program>;

  /**
   * Construct a resource manager for the runtime.
   * :param runtime_ctx: Pointer to the runtime context (non-owning).
   * :param engine_res_path: Path to engine-provided resources used as a base
   * for resolution.
   */
  explicit resource_manager (comp::singl::runtime_context *runtime_ctx,
                             const std::string &engine_res_path = ".",
                             bool manages_runtime_state = true);
  /** Destroy the resource manager and release owned resources. */
  ~resource_manager ();

  /** Final shutdown used when the owning runtime is being destroyed. */
  void shutdown ();

  /**
   * Assign an editor context for editor-only behaviors (non-owning).
   * :param editor_ctx: Editor context pointer, or nullptr to clear.
   */
  void set_editor_context (comp::singl::editor_context *editor_ctx);

  /**
   * Poll and finalize any asynchronous upload jobs started for GPU
   * resources. This should be called from the main thread to move resources
   * from CPU-side jobs into GPU upload sessions when ready.
   */
  void update_async_uploads ();

  /** Register a model path and return its id without loading. */
  [[nodiscard]] model_id register_model (const std::string &path);

  /**
   * Import a model into the project and optionally request loading.
   * :param path: Filesystem path to the model asset.
   * :param request_load: If true, start asynchronous loading immediately.
   * :return: The assigned model id.
   */
  [[nodiscard]] model_id import_model (const std::string &path,
                                       bool request_load = true);

  /**
   * Load a model resource handle (may start loading if not ready).
   * :param id: Model id to load.
   * :return: A handle to the model resource.
   */
  [[nodiscard]] model_handle load (model_id id);

  /** Load a resource given a generic resource_ref. */
  void load (io::resource_ref ref);

  /**
   * Unload a previously registered model id.
   * :param id: Model id to unload.
   */
  void unload (model_id id);

  /** Unload a resource referenced by a generic resource_ref. */
  void unload (io::resource_ref ref);

  /**
   * Get a model handle for the given id.
   * :return: An entt::resource handle; if the resource is not loaded the
   *         returned handle may be empty or trigger a load when accessed.
   */
  [[nodiscard]] model_handle get (model_id id);

  /** Query the current loading state of a model id. */
  [[nodiscard]] model_state state (model_id id) const;

  /** Check whether the manager knows about the given model id. */
  [[nodiscard]] bool contains (model_id id) const;

  /** Find a registered model id by the original path if present. */
  [[nodiscard]] std::optional<model_id>
  find_model_by_path (const std::string &path) const;

  /** Retrieve metadata about a registered model id. */
  [[nodiscard]] std::optional<model_resource_info> info (model_id id) const;

  /** List metadata for all known models. */
  [[nodiscard]] std::vector<model_resource_info> list_models () const;

  /**
   * Load a preview model optimized to the lowest LOD for quick display.
   * :param id: Model id to load as preview.
   */
  void load_preview_model_low_lod (model_id id);

  /** Get the currently active preview model id, or entt::null. */
  model_id current_preview_model () const;

  /** Unload the current preview model if one is active. */
  void unload_preview_model ();

  /**
   * Release preview ownership for a model if the provided id matches.
   * This clears internal preview ownership flags without unloading the model.
   */
  void release_preview_ownership_if_matches (model_id id);

  /** Query whether the given model id is currently owned as a preview. */
  bool is_preview_owned (model_id id) const;

  /** Register an image asset path without loading. */
  [[nodiscard]] image_id register_image (const std::string &path);

  /**
   * Import an image into the project and optionally request loading.
   * :param path: Path to the image file.
   * :param request_load: If true, start loading immediately.
   */
  [[nodiscard]] image_id import_image (const std::string &path,
                                       bool request_load = true);

  /** Load an image handle for use by the renderer. */
  [[nodiscard]] image_handle load (image_id id);

  /** Unload the image resource referenced by id. */
  void unload (image_id id);

  /** Get an image handle for the given id. */
  [[nodiscard]] image_handle get (image_id id);

  /** Query the loading state of an image id. */
  [[nodiscard]] image_state state (image_id id) const;

  /** Check if an image id is known to the manager. */
  [[nodiscard]] bool contains (image_id id) const;

  /** Retrieve metadata for an image id. */
  [[nodiscard]] std::optional<image_resource_info> info (image_id id) const;

  /** List all registered images and their metadata. */
  [[nodiscard]] std::vector<image_resource_info> list_images () const;

  /** Register a cubemap asset path. */
  [[nodiscard]] cubemap_id register_cubemap (const std::string &path);

  /** Import a cubemap and optionally request loading. */
  [[nodiscard]] cubemap_id import_cubemap (const std::string &path,
                                           bool request_load = true);

  /** Load a cubemap handle for rendering. */
  [[nodiscard]] cubemap_handle load (cubemap_id id);

  /** Unload a cubemap by id. */
  void unload (cubemap_id id);

  /** Get a cubemap handle for the given id. */
  [[nodiscard]] cubemap_handle get (cubemap_id id);

  /** Query the load state of a cubemap id. */
  [[nodiscard]] cubemap_state state (cubemap_id id) const;

  /** Check presence of cubemap id in manager. */
  [[nodiscard]] bool contains (cubemap_id id) const;

  /** Get metadata for a cubemap id. */
  [[nodiscard]] std::optional<cubemap_resource_info> info (cubemap_id id) const;

  /** List all registered cubemaps. */
  [[nodiscard]] std::vector<cubemap_resource_info> list_cubemaps () const;

  /** Register a scene asset path. */
  [[nodiscard]] scene_id register_scene (const std::string &path);

  /**
   * Import a scene (or prefab) and optionally start loading.
   * :param path: Filesystem path to the scene or prefab.
   * :param request_load: Start asynchronous load if true.
   */
  [[nodiscard]] scene_id import_scene (const std::string &path,
                                       bool request_load = true);

  /** Load a scene resource handle. */
  [[nodiscard]] scene_handle load (scene_id id);

  /** Unload a scene resource. */
  void unload (scene_id id);

  /** Get a scene resource handle for the given id. */
  [[nodiscard]] scene_handle get (scene_id id);

  /**
   * Return a raw pointer to a loaded scene instance or nullptr if not
   * loaded. The returned pointer is non-owning and valid until the scene is
   * unloaded.
   */
  [[nodiscard]] scene *find_loaded_scene (scene_id id) const;

  /**
   * Activate the scene with the provided id as the active scene.
   * :return: True if activation succeeded.
   */
  [[nodiscard]] bool activate_scene (scene_id id);

  /** Instantiate a prefab scene under the optional parent entity.
   *  :return: The root entity of the first instantiated copy, or entt::null on
   * failure. */
  [[nodiscard]] entt::entity
  instantiate_prefab (scene_id id, entt::entity parent = entt::null);

  /** Query the loading state of a scene id. */
  [[nodiscard]] scene_state state (scene_id id) const;

  /** Check whether the manager knows about the provided scene id. */
  [[nodiscard]] bool contains (scene_id id) const;

  /** Retrieve metadata for a scene id. */
  [[nodiscard]] std::optional<scene_resource_info> info (scene_id id) const;

  /** List metadata for all registered scenes. */
  [[nodiscard]] std::vector<scene_resource_info> list_scenes () const;

  /**
   * Save a scene to disk.
   *
   * :param scene: Scene to save.
   * :param path: Destination path.
   * :param is_prefab: Mark whether the saved scene is a prefab.
   * :return: True on success.
   */
  bool save_scene (const rsc::scene &scene, const std::string &path,
                   bool is_prefab = false);

  /** Register an audio asset path. */
  [[nodiscard]] audio_id register_audio (const std::string &path);

  /** Import audio and optionally request loading. */
  [[nodiscard]] audio_id import_audio (const std::string &path,
                                       bool request_load = true);

  /**
   * Load audio and return a raw MIX_Audio pointer owned by the mixer.
   * The pointer is non-owning; do not free it manually.
   */
  [[nodiscard]] MIX_Audio *load (audio_id id);

  /** Unload an audio asset by id. */
  void unload (audio_id id);

  /** Get the raw MIX_Audio pointer for the given id. */
  [[nodiscard]] MIX_Audio *get (audio_id id);

  /** Query the state of an audio id. */
  [[nodiscard]] audio_state state (audio_id id) const;

  /** Check presence of audio id. */
  [[nodiscard]] bool contains (audio_id id) const;

  /** Get metadata for an audio id. */
  [[nodiscard]] std::optional<audio_resource_info> info (audio_id id) const;

  /** List all registered audio assets. */
  [[nodiscard]] std::vector<audio_resource_info> list_audio () const;

  /** Register a UI layout asset path. */
  [[nodiscard]] ui_layout_id register_ui_layout (const std::string &path);

  /** Get metadata for a UI layout id. */
  [[nodiscard]] std::optional<ui_layout_resource_info>
  info (ui_layout_id id) const;

  /** List all registered UI layouts. */
  [[nodiscard]] std::vector<ui_layout_resource_info> list_ui_layouts () const;

  /** Register a font asset path. */
  [[nodiscard]] font_id register_font (const std::string &path);

  /** Get metadata for a font id. */
  [[nodiscard]] std::optional<font_resource_info> info (font_id id) const;

  /** List all registered fonts. */
  [[nodiscard]] std::vector<font_resource_info> list_fonts () const;

  /** Register a shader asset path. */
  [[nodiscard]] shader_id register_shader (const std::string &path);

  /** Load a shader module handle for the given id. */
  [[nodiscard]] shader_handle load (shader_id id);

  /** Unload a shader module. */
  void unload (shader_id id);

  /** Get a shader handle for the given id. */
  [[nodiscard]] shader_handle get (shader_id id);

  /** Query shader loading state. */
  [[nodiscard]] shader_state state (shader_id id) const;

  /** Check whether a shader id is registered. */
  [[nodiscard]] bool contains (shader_id id) const;

  /** Get metadata for a shader id. */
  [[nodiscard]] std::optional<shader_resource_info> info (shader_id id) const;

  /** List all registered shaders and metadata. */
  [[nodiscard]] std::vector<shader_resource_info> list_shaders () const;

  // ---- Materials ----
  /** Register a material asset path. */
  [[nodiscard]] material_id register_material (const std::string &path);

  /** Import a material asset and optionally request loading. */
  [[nodiscard]] material_id import_material (const std::string &path,
                                             bool request_load = true);

  /** Load a material handle for the given id. */
  [[nodiscard]] material_handle load (material_id id);

  /** Unload a material asset. */
  void unload (material_id id);

  /** Get a material handle for the given id. */
  [[nodiscard]] material_handle get (material_id id);

  /** Query material loading state. */
  [[nodiscard]] material_state state (material_id id) const;

  /** Check whether a material id is registered. */
  [[nodiscard]] bool contains (material_id id) const;

  /** Get metadata for a material id. */
  [[nodiscard]] std::optional<material_resource_info>
  info (material_id id) const;

  /** List all registered materials. */
  [[nodiscard]] std::vector<material_resource_info> list_materials () const;

  // ---- Shader Programs ----
  /** Register a shader program (runtime generated or file path). */
  [[nodiscard]] shader_program_id
  register_shader_program (const std::string &path);

  /** Store a runtime-generated shader program. */
  [[nodiscard]] shader_program_id
  register_shader_program (std::shared_ptr<gfx::shader_program> prog,
                           const std::string &name);

  /** Get a shader program handle for the given id. */
  [[nodiscard]] shader_program_handle get (shader_program_id id);

  /** Query shader program state. */
  [[nodiscard]] shader_program_state state (shader_program_id id) const;

  /** Check whether a shader program id is registered. */
  [[nodiscard]] bool contains (shader_program_id id) const;

  /** Get metadata for a shader program id. */
  [[nodiscard]] std::optional<shader_program_resource_info>
  info (shader_program_id id) const;

  /** List all registered shader programs. */
  [[nodiscard]] std::vector<shader_program_resource_info>
  list_shader_programs () const;

  // ---- Skeletons (`.skel.ozz`) ----
  /** Register a skeleton asset path without loading. */
  [[nodiscard]] skeleton_id register_skeleton (const std::string &path);

  /** Load skeleton data (lazy); returns nullptr on failure. */
  [[nodiscard]] ozz::animation::Skeleton *load (skeleton_id id);

  /** Unload a skeleton by id, releasing its ozz memory. */
  void unload (skeleton_id id);

  /** Get the loaded skeleton without triggering a load. */
  [[nodiscard]] ozz::animation::Skeleton *get (skeleton_id id);

  /** Query the loading state of a skeleton id. */
  [[nodiscard]] skeleton_state state (skeleton_id id) const;

  /** Check presence of skeleton id in manager. */
  [[nodiscard]] bool contains (skeleton_id id) const;

  /** Get metadata for a skeleton id. */
  [[nodiscard]] std::optional<skeleton_resource_info>
  info (skeleton_id id) const;

  /** List all registered skeletons. */
  [[nodiscard]] std::vector<skeleton_resource_info> list_skeletons () const;

  // ---- Animation clips (`.anim.ozz`) ----
  /** Register an animation clip asset path without loading. */
  [[nodiscard]] animation_id register_animation (const std::string &path);

  /** Load an animation clip (lazy); returns nullptr on failure. */
  [[nodiscard]] ozz::animation::Animation *load (animation_id id);

  /** Unload an animation clip by id, releasing its ozz memory. */
  void unload (animation_id id);

  /** Get the loaded animation clip without triggering a load. */
  [[nodiscard]] ozz::animation::Animation *get (animation_id id);

  /** Query the loading state of an animation id. */
  [[nodiscard]] animation_state state (animation_id id) const;

  /** Check presence of animation id in manager. */
  [[nodiscard]] bool contains (animation_id id) const;

  /** Get metadata for an animation id. */
  [[nodiscard]] std::optional<animation_resource_info>
  info (animation_id id) const;

  /** List all registered animation clips. */
  [[nodiscard]] std::vector<animation_resource_info> list_animations () const;

  /**
   * Set the base engine resource path used to resolve engine-provided
   * assets.
   */
  void set_engine_resource_path (const std::string &path);

  /** Get the configured engine resource base path. */
  [[nodiscard]] std::string
  get_engine_resource_path () const
  {
    return m_wsl_resource_path;
  }

  /** Create a new project from the provided project descriptor. */
  [[nodiscard]] bool new_project (const rsc::project &proj);

  /** Load a project file from disk and set it as active. */
  [[nodiscard]] bool load_project (const std::string &path);

  /** Return the currently active project if any. */
  [[nodiscard]] std::shared_ptr<rsc::project> current_project () const;

  /**
   * Unload and clear all managed resources, releasing GPU and CPU
   * memory.
   */
  void clear_all_resources (bool restore_builtin_defaults = true);

  /** Access to the internal audio mixer instance (non-owning). */
  MIX_Mixer *
  mixer () const
  {
    return m_mixer;
  }

  // Path resolution helpers
  /**
   * Resolve a possibly-relative asset path to an absolute
   * engine/project path.
   */
  [[nodiscard]] std::string resolve_path (const std::string &path) const;

  /** Get the filesystem path for a registered model id. */
  [[nodiscard]] std::string get_resource_path (model_id id) const;

  /** Get the filesystem path for a registered cubemap id. */
  [[nodiscard]] std::string get_resource_path (cubemap_id id) const;

  /** Get the filesystem path for a registered audio id. */
  [[nodiscard]] std::string get_resource_path (audio_id id) const;

  /** Get the filesystem path for a registered skeleton id. */
  [[nodiscard]] std::string get_resource_path (skeleton_id id) const;

  /** Get the filesystem path for a registered animation id. */
  [[nodiscard]] std::string get_resource_path (animation_id id) const;

  /** Get the filesystem path for a registered material id. */
  [[nodiscard]] std::string get_resource_path (material_id id) const;

  /** Get the filesystem path for a generic resource reference. */
  [[nodiscard]] std::string get_path (io::resource_ref ref) const;

  /**
   * Helper providing thread-local access to the active resource manager
   * during serialization operations. This is internal and should be set by the
   * manager when performing (de)serialization.
   */
  struct serialization_context
  {
    static resource_manager *&
    get ()
    {
      static thread_local resource_manager *mgr = nullptr;
      return mgr;
    }
  };

private:
  comp::singl::runtime_context *m_runtime_ctx;
  comp::singl::editor_context *m_editor_ctx = nullptr;

  MIX_Mixer *m_mixer = nullptr;

  std::unordered_map<entt::id_type, detail::image_record> m_image_table;
  std::unordered_map<entt::id_type, detail::cubemap_record> m_cubemap_table;
  std::unordered_map<entt::id_type, detail::scene_record> m_scene_table;
  std::unordered_map<entt::id_type, detail::model_record> m_model_table;
  std::unordered_map<entt::id_type, detail::audio_record> m_audio_table;
  std::unordered_map<entt::id_type, detail::ui_layout_record> m_ui_layout_table;
  std::unordered_map<entt::id_type, detail::font_record> m_font_table;
  std::unordered_map<entt::id_type, detail::shader_record> m_shader_table;
  std::unordered_map<entt::id_type, detail::material_record> m_material_table;
  std::unordered_map<entt::id_type, detail::shader_program_record>
      m_shader_program_table;
  std::unordered_map<entt::id_type, detail::skeleton_record> m_skeleton_table;
  std::unordered_map<entt::id_type, detail::animation_record> m_animation_table;

  entt::resource_cache<rsc::scene, scene_loader> m_scenes;
  std::unordered_map<entt::id_type, rsc::scene *> m_loaded_scene_instances;
  entt::id_type m_preferred_default_scene_id = entt::null;
  bool m_waiting_for_preferred_default_scene = false;

  entt::resource_cache<gfx::model_3d, model_loader> m_models;
  entt::resource_cache<gfx::cubemap, cubemap_loader> m_cubemaps;
  entt::resource_cache<gfx::image, image_loader> m_images;
  entt::resource_cache<gfx::shader_module, shader_loader> m_shaders;

  std::unordered_map<entt::id_type, std::shared_ptr<gfx::material_asset>>
      m_materials;
  std::unordered_map<entt::id_type, std::shared_ptr<gfx::shader_program>>
      m_shader_programs;

  std::shared_ptr<rsc::project> m_active_project;
  rsc::project_loader m_project_loader;
  std::unordered_map<std::string, entt::id_type> m_model_ids_by_path;
  std::unordered_map<std::string, entt::id_type> m_audio_ids_by_path;
  std::unordered_map<std::string, entt::id_type> m_skeleton_ids_by_path;
  std::unordered_map<std::string, entt::id_type> m_animation_ids_by_path;

  // preview temp state
  entt::id_type m_preview_model_id = entt::null;

  // ids that should build/upload only the lowest LOD
  std::unordered_set<entt::id_type> m_low_lod_only_models;

  // cancellation sets (avoid blocking future destructor)
  std::unordered_set<entt::id_type> m_cancel_models;
  std::unordered_set<entt::id_type> m_cancel_images;
  std::unordered_set<entt::id_type> m_cancel_cubemaps;
  std::unordered_set<entt::id_type> m_cancel_scenes;

  std::string m_wsl_resource_path = ".";

  bool m_clearing = false;
  bool m_shutdown = false;
  bool m_manages_runtime_state = true;

  struct project_load_job
  {
    std::shared_ptr<project> project_data;
    std::future<project_assets> assets_job;
    bool has_runtime_code = false;
  };
  std::optional<project_load_job> m_active_project_load;

  /** Register built-in engine models used as placeholders or defaults. */
  void register_builtin_models ();

  /** Register built-in cubemaps used by engine defaults. */
  void register_builtin_cubemaps ();
};

struct resource_manager_view : comp::singleton_component
{
  resource_manager *manager = nullptr;

  resource_manager_view () = default;
  explicit resource_manager_view (resource_manager *manager) : manager (manager)
  {
  }

  /** Register reflection metadata used by the editor for this view. */
  static void register_meta ();

  /**
   * ImGui inspector for the resource manager; implemented in editor_support
   * (inspectors_resources.cpp) and registered by
   * editor::register_editor_inspectors().
   * :param label: Optional UI label.
   * :param runtime_ctx: Runtime context used to resolve runtime resources.
   * :return: True if interactive UI was drawn.
   */
  bool custom_inspect (const char *label,
                       comp::singl::runtime_context *runtime_ctx) const;
};

} // namespace rsc

} // namespace wsl
