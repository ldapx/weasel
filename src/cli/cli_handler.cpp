#include "cli_handler.hpp"
#include "wsl/log/log.hpp"

#include <SDL3/SDL_filesystem.h>

#include "comp/area3d.hpp"
#include "comp/camera.hpp"
#include "comp/character_body.hpp"
#include "comp/component_meta.hpp"
#include "comp/directional_light.hpp"
#include "comp/hierarchy.hpp"
#include "comp/model_instance_3d.hpp"
#include "comp/point_light.hpp"
#include "comp/rigid_body.hpp"
#include "comp/singl/physics_manager.hpp"
#include "comp/singl/rendering_manager.hpp"
#include "comp/spot_light.hpp"
#include "reg/component_registry.hpp"
#include "reg/singleton_registry.hpp"
#include "reg/system_factory_registry.hpp"
#include "comp/transform.hpp"
#include "comp/world_transform.hpp"
#include "math/vector.hpp"
#include "rsc/animation_importer.hpp"
#include "rsc/resource_ids.hpp"
#include "rsc/resource_manager.hpp"
#include "wsl/rsc/project_loader.hpp"
#include "wsl/rsc/project.hpp"
#include "wsl/rsc/scene.hpp"
#include "wsl/rsc/scene_snapshot_serializer.hpp"
#include "wsl/comp/singl/runtime_context.hpp"
#include "wsl/comp/singl/editor_context.hpp"
#include "wsl/comp/singl/ui_manager.hpp"
#include "wsl/sys/system.hpp"
#include "wsl/comp/components.hpp"

#include <CLI/CLI.hpp>
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace wsl::cli
{

namespace
{
class dummy_system : public wsl::sys::ecs_system_t<dummy_system>
{
public:
  using ecs_system_t::ecs_system_t;
};

void
register_all (wsl::comp::singl::runtime_context &rtc)
{
  wsl::comp::singl::runtime_context::register_meta ();
  wsl::comp::singl::editor_context::register_meta ();
  wsl::comp::singl::ui_manager::register_meta ();

  wsl::comp::register_component_meta<
      wsl::comp::hierarchy, wsl::comp::world_transform, wsl::comp::transform,
      wsl::math::vec3f, wsl::math::quatf, wsl::math::mat33f, wsl::math::mat44f,
      wsl::rsc::model_id, wsl::rsc::image_id, wsl::comp::model_instance_3d,
      wsl::comp::camera, wsl::comp::point_light, wsl::comp::spot_light,
      wsl::comp::directional_light, wsl::comp::rigid_body, wsl::comp::area,
      wsl::comp::character_body, wsl::rsc::scene_manager,
      wsl::rsc::resource_manager_view> ();

  wsl::comp::for_each_type<wsl::comp::component_types>::apply (
      [&rtc]<typename T> () {
        rtc.component_registry ().register_world_component<T> ();
      });

  rtc.singleton_registry ()
      .register_bound_singleton_component<wsl::comp::singl::runtime_context> (
          { "Runtime Context", true });
  rtc.singleton_registry ()
      .register_bound_singleton_component<wsl::comp::singl::editor_context> (
          { "Editor Context", true });
  rtc.singleton_registry ()
      .register_bound_singleton_component<wsl::rsc::scene_manager> (
          { "Scene Manager", true });
  rtc.singleton_registry ()
      .register_bound_singleton_component<wsl::rsc::resource_manager_view> (
          { "Resource Manager", true });
  rtc.singleton_registry ()
      .register_bound_singleton_component<wsl::comp::singl::ui_manager> (
          { "UI Manager", true, false, true });
  rtc.singleton_registry ()
      .register_singleton_component<wsl::comp::singl::rendering_manager> (
          { "Rendering Manager", true });
  rtc.singleton_registry ()
      .register_singleton_component<wsl::comp::singl::physics_manager> (
          { "Physics Manager", true });
}

std::string
quote_repl_arg (std::string_view arg)
{
  const bool needs_quotes
      = arg.empty ()
        || std::any_of (arg.begin (), arg.end (), [] (unsigned char c) {
             return std::isspace (c) || c == '"' || c == '\'';
           });
  if (!needs_quotes) {
    return std::string (arg);
  }

  std::string out;
  out.reserve (arg.size () + 2);
  out.push_back ('"');
  for (char chr : arg) {
    if (chr == '"' || chr == '\\') {
      out.push_back ('\\');
    }
    out.push_back (chr);
  }
  out.push_back ('"');
  return out;
}

std::string
build_repl_command (const std::vector<std::string> &args)
{
  std::ostringstream output;
  for (std::size_t i = 0; i < args.size (); ++i) {
    if (i > 0) {
      output << ' ';
    }
    output << quote_repl_arg (args[i]);
  }
  return output.str ();
}
}

cli_handler::result
cli_handler::parse (int argc, char **argv)
{
  CLI::App app{ "Weasel Engine Editor" };
  app.allow_extras ();
  app.get_formatter ()->column_width (42);

  std::string project_to_load;
  auto *project_opt = app.add_option ("--project", project_to_load,
                                      "Path to the project to load");

  std::string scene_to_load;
  auto *scene_opt
      = app.add_option ("--scene", scene_to_load,
                        "Path to the scene to load before running a command");
  scene_opt->needs (project_opt);

  bool interactive = false;
  app.add_flag ("-i,--interactive", interactive, "Start the interactive REPL");

  bool attach = false;
  app.add_flag (
      "-a,--attach", attach,
      "Attach to the first running editor instance and use its open project "
      "(pass --project to pick a specific one; falls back to standalone with "
      "a warning when no editor is running)");

  auto *create_project
      = app.add_subcommand ("create-project", "Create a new project");
  create_project->alias ("--create-project");
  std::string cp_path;
  std::string cp_name;
  bool no_subdir = false;
  create_project->add_option ("path", cp_path, "Project parent path");
  create_project->add_option ("name", cp_name, "Project name");
  create_project->add_flag ("--no-subdir", no_subdir,
                            "Don't create a project-name subdirectory");

  auto *create_scene
      = app.add_subcommand ("create-scene", "Create a new scene");
  create_scene->alias ("--create-scene");
  std::string cs_path;
  std::string cs_name;
  std::vector<std::string> cs_systems;
  create_scene->add_option ("path", cs_path, "Scene path")->required ();
  create_scene->add_option ("name", cs_name, "Scene name")->required ();
  create_scene->add_option ("systems", cs_systems, "Systems to add");

  auto *validate_project
      = app.add_subcommand ("validate-project", "Validate a project");
  validate_project->alias ("--validate-project");
  std::string vp_path;
  validate_project->add_option ("path", vp_path, "Path to wslpro.json")
      ->required ();

  auto *validate_scene
      = app.add_subcommand ("validate-scene", "Validate a scene");
  validate_scene->alias ("--validate-scene");
  std::string vs_scene_path;
  std::string vs_proj_path;
  validate_scene->add_option ("scene_path", vs_scene_path, "Path to scene.wscn")
      ->required ();
  validate_scene->add_option ("proj_path", vs_proj_path, "Path to wslpro.json")
      ->required ();

  auto *proj_cmd = app.add_subcommand ("proj", "Manage project configuration");
  auto *proj_new = proj_cmd->add_subcommand ("new", "Create a project");
  std::string proj_new_path;
  std::string proj_new_name;
  proj_new->add_option ("path", proj_new_path, "Project path")->required ();
  proj_new->add_option ("name", proj_new_name, "Project name")->required ();

  auto *proj_load = proj_cmd->add_subcommand (
      "load", "Load a project into a non-interactive runtime");
  std::string proj_load_path;
  bool proj_load_strict = false;
  proj_load
      ->add_option ("path", proj_load_path,
                    "Path to wslpro.json or project root")
      ->required ();
  proj_load->add_flag (
      "--strict-runtime", proj_load_strict,
      "Exit non-zero if the runtime script compile fails after load");

  auto *proj_info = proj_cmd->add_subcommand (
      "info", "Show information about the loaded project");
  auto *proj_save
      = proj_cmd->add_subcommand ("save", "Save the loaded project");
  auto *proj_set
      = proj_cmd->add_subcommand ("set", "Set a project field value");
  std::string proj_set_field;
  std::string proj_set_value;
  proj_set
      ->add_option ("field", proj_set_field,
                    "Field name (e.g. name, author, default_scene_path)")
      ->required ();
  proj_set->add_option ("value", proj_set_value, "Field value")->required ();

  auto *scene_cmd
      = app.add_subcommand ("scene", "Manage scenes in the loaded project");
  auto *scene_new
      = scene_cmd->add_subcommand ("new", "Create a new empty active scene");
  std::string scene_new_name;
  scene_new->add_option ("name", scene_new_name, "Scene name")->required ();

  auto *scene_load = scene_cmd->add_subcommand (
      "load", "Load a scene into the active runtime");
  std::string scene_load_path;
  scene_load->add_option ("path", scene_load_path, "Path to the scene file")
      ->required ();

  auto *scene_save
      = scene_cmd->add_subcommand ("save", "Save the active scene");
  std::string scene_save_path;
  scene_save->add_option ("path", scene_save_path,
                          "Optional override save path");

  auto *scene_ls
      = scene_cmd->add_subcommand ("ls", "List scenes in the loaded project");
  auto *scene_status
      = scene_cmd->add_subcommand ("status", "Show active scene status");

  auto *ent_cmd
      = app.add_subcommand ("ent", "Manage entities in the active scene");
  auto *ent_new = ent_cmd->add_subcommand (
      "new", "Create a new entity with default components (Transform, "
             "WorldTransform, Hierarchy)");
  std::string ent_new_name;
  bool ent_new_empty = false;
  ent_new->add_option ("name", ent_new_name, "Optional entity name");
  ent_new->add_flag ("--empty", ent_new_empty,
                     "Create an empty entity without default components");

  auto *ent_ls
      = ent_cmd->add_subcommand ("ls", "List entities in the active scene");

  auto *ent_rm = ent_cmd->add_subcommand (
      "rm", "Remove an entity from the active scene");
  std::string ent_rm_id;
  ent_rm->add_option ("id", ent_rm_id, "Entity id")->required ();

  auto *ent_ren
      = ent_cmd->add_subcommand ("ren", "Rename an entity in the active scene");
  std::string ent_ren_id;
  std::string ent_ren_name;
  ent_ren->add_option ("id", ent_ren_id, "Entity id")->required ();
  ent_ren->add_option ("new_name", ent_ren_name, "New entity name")
      ->required ();

  auto *ent_inspect = ent_cmd->add_subcommand (
      "inspect", "Inspect an entity in the active scene");
  std::string ent_inspect_id;
  ent_inspect->add_option ("id", ent_inspect_id, "Entity id")->required ();

  auto *comp_cmd = app.add_subcommand ("comp", "Manage components on entities");
  auto *comp_ls = comp_cmd->add_subcommand (
      "ls", "List registered components or an entity's components");
  std::string comp_ls_entity_id;
  comp_ls->add_option ("entity_id", comp_ls_entity_id, "Optional entity id");

  auto *comp_avail
      = comp_cmd->add_subcommand ("avail", "List registered components");

  auto *comp_add
      = comp_cmd->add_subcommand ("add", "Add a component to an entity");
  std::string comp_add_entity_id;
  std::string comp_add_type;
  comp_add->add_option ("entity_id", comp_add_entity_id, "Entity id")
      ->required ();
  comp_add->add_option ("type", comp_add_type, "Component type name")
      ->required ();

  auto *comp_rm
      = comp_cmd->add_subcommand ("rm", "Remove a component from an entity");
  std::string comp_rm_entity_id;
  std::string comp_rm_type;
  comp_rm->add_option ("entity_id", comp_rm_entity_id, "Entity id")
      ->required ();
  comp_rm->add_option ("type", comp_rm_type, "Component type name")
      ->required ();

  auto *comp_set = comp_cmd->add_subcommand ("set", "Set a component property");
  std::string comp_set_entity_id;
  std::string comp_set_type;
  std::string comp_set_prop;
  std::string comp_set_val;
  comp_set->add_option ("entity_id", comp_set_entity_id, "Entity id")
      ->required ();
  comp_set->add_option ("type", comp_set_type, "Component type name")
      ->required ();
  comp_set->add_option ("property", comp_set_prop, "Property path")
      ->required ();
  comp_set->add_option ("value", comp_set_val, "Value (JSON or plain)")
      ->required ();

  auto *comp_create = comp_cmd->add_subcommand (
      "create", "Generate a world component template");
  std::string comp_create_name;
  bool comp_create_source = false;
  comp_create->add_option ("name", comp_create_name, "Component name")
      ->required ();
  comp_create->add_flag ("--source", comp_create_source,
                         "Also generate a .cpp source file");

  auto *singl_cmd = app.add_subcommand ("singl", "Manage singleton components");
  auto *sig_cmd = app.add_subcommand (
      "sig",
      "Signal management (ls, handlers, connections, connect, disconnect)");
  auto *singl_ls = singl_cmd->add_subcommand (
      "ls", "List singleton components in the scene");
  auto *singl_add = singl_cmd->add_subcommand (
      "add", "Add a singleton to the active scene");
  std::string singl_add_name;
  singl_add->add_option ("name", singl_add_name, "Singleton name")->required ();
  // Registered explicitly: an unregistered subcommand name falls through to
  // CLI11's positional handling and produces a misleading error instead of
  // reaching the REPL handler.
  auto *singl_rm = singl_cmd->add_subcommand (
      "rm", "Remove a singleton from the active scene");
  std::string singl_rm_name;
  singl_rm->add_option ("name", singl_rm_name, "Singleton name")->required ();
  auto *singl_info = singl_cmd->add_subcommand ("info", "Describe a singleton");
  std::string singl_info_name;
  singl_info->add_option ("name", singl_info_name, "Singleton name")
      ->required ();
  auto *singl_create = singl_cmd->add_subcommand (
      "create", "Generate a singleton component template");
  std::string singl_create_name;
  bool singl_create_source = false;
  singl_create->add_option ("name", singl_create_name, "Singleton name")
      ->required ();
  singl_create->add_flag ("--source", singl_create_source,
                          "Also generate a .cpp source file");
  auto *singl_set
      = singl_cmd->add_subcommand ("set", "Set a singleton component property");
  std::string singl_set_name;
  std::string singl_set_prop;
  std::string singl_set_val;
  singl_set->add_option ("name", singl_set_name, "Singleton name")->required ();
  singl_set->add_option ("property", singl_set_prop, "Property path")
      ->required ();
  singl_set->add_option ("value", singl_set_val, "Value (JSON or plain)")
      ->required ();

  auto *sys_cmd = app.add_subcommand (
      "sys", "Manage systems attached to the active scene");
  auto *sys_ls

      = sys_cmd->add_subcommand (
          "ls", "List all systems in the active scene (core + user-defined)");
  auto *sys_avail

      = sys_cmd->add_subcommand ("avail",
                                 "List registered system types (core + user)");
  auto *sys_add

      = sys_cmd->add_subcommand (
          "add", "Add a user-defined system to the active scene");
  std::vector<std::string> sys_add_name;
  sys_add
      ->add_option ("name", sys_add_name,
                    "System name (quotes optional; "
                    "multiple words are joined)")
      ->required ()
      ->expected (-1);

  auto *sys_create
      = sys_cmd->add_subcommand ("create", "Generate a system template");
  std::string sys_create_name;
  bool sys_create_source = false;
  sys_create->add_option ("name", sys_create_name, "System name")->required ();
  sys_create->add_flag ("--source", sys_create_source,
                        "Also generate a .cpp source file");

  auto *rsc_cmd = app.add_subcommand ("rsc", "Manage engine resources");
  auto *rsc_ls = rsc_cmd->add_subcommand ("ls", "List registered resources");
  std::string rsc_ls_type;
  rsc_ls->add_option ("type", rsc_ls_type,
                      "Resource type filter (model, image, cubemap, scene, "
                      "audio, font, shader, layout)");

  auto *rsc_add = rsc_cmd->add_subcommand (
      "add", "Register a resource (and optionally load it with --load)");
  std::string rsc_add_type;
  std::string rsc_add_path;
  bool rsc_add_load = false;
  rsc_add->add_option ("type", rsc_add_type, "Resource type")->required ();
  rsc_add->add_option ("path", rsc_add_path, "Path to the resource file")
      ->required ();
  rsc_add->add_flag ("--load", rsc_add_load,
                     "Load the resource after registration");

  auto *rsc_rm = rsc_cmd->add_subcommand (
      "rm", "Remove a registered resource by name or path");
  std::string rsc_rm_type;
  std::string rsc_rm_name;
  rsc_rm->add_option ("type", rsc_rm_type, "Resource type")->required ();
  rsc_rm->add_option ("name", rsc_rm_name, "Resource name or path")
      ->required ();

  auto *rsc_load = rsc_cmd->add_subcommand (
      "load", "Load an already-registered resource by name or path");
  std::string rsc_load_type;
  std::string rsc_load_name;
  rsc_load->add_option ("type", rsc_load_type, "Resource type")->required ();
  rsc_load->add_option ("name", rsc_load_name, "Resource name or path")
      ->required ();

  auto *rsc_unload = rsc_cmd->add_subcommand (
      "unload", "Unload a registered resource by name or path");
  std::string rsc_unload_type;
  std::string rsc_unload_name;
  rsc_unload->add_option ("type", rsc_unload_type, "Resource type")
      ->required ();
  rsc_unload->add_option ("name", rsc_unload_name, "Resource name or path")
      ->required ();

  auto *rsc_info = rsc_cmd->add_subcommand (
      "info", "Display metadata for a registered resource");
  std::string rsc_info_type;
  std::string rsc_info_name;
  rsc_info->add_option ("type", rsc_info_type, "Resource type")->required ();
  rsc_info->add_option ("name", rsc_info_name, "Resource name or path")
      ->required ();

  auto *import_cmd
      = app.add_subcommand ("import", "Import engine assets from sources");
  auto *import_anim = import_cmd->add_subcommand (
      "anim", "Convert a glTF file to .skel.ozz + .anim.ozz (gltf2ozz)");
  std::string import_anim_file;
  import_anim->add_option ("file", import_anim_file, "Path to .gltf/.glb")
      ->required ();

  auto *prefab_cmd = app.add_subcommand ("prefab", "Manage prefab scenes");
  auto *prefab_ls = prefab_cmd->add_subcommand (
      "ls", "List prefab assets in the loaded project");
  auto *prefab_save = prefab_cmd->add_subcommand (
      "save", "Save the active scene as a prefab");
  std::string prefab_save_path;
  prefab_save->add_option ("path", prefab_save_path,
                           "Optional override save path");
  auto *prefab_load = prefab_cmd->add_subcommand (
      "load", "Load a prefab scene as the active scene");
  std::string prefab_load_path;
  prefab_load->add_option ("path", prefab_load_path, "Path to the prefab file")
      ->required ();
  auto *prefab_instantiate = prefab_cmd->add_subcommand (
      "instantiate", "Instantiate a prefab into the active scene");
  std::string prefab_instantiate_name;
  std::string prefab_instantiate_parent;
  prefab_instantiate
      ->add_option ("name", prefab_instantiate_name, "Prefab name or path")
      ->required ();
  prefab_instantiate->add_option ("parent_id", prefab_instantiate_parent,
                                  "Optional parent entity id");

  auto *aot_cmd = app.add_subcommand (
      "aot", "AOT-compile a .das file using the engine's module environment");
  std::string aot_input;
  std::string aot_output;
  std::vector<std::string> aot_include_dirs;
  aot_cmd->add_option ("input", aot_input, "Path to .das file")->required ();
  aot_cmd->add_option ("output", aot_output, "Output .cpp file path")
      ->required ();
  aot_cmd
      ->add_option ("--include,-I", aot_include_dirs,
                    "Additional directory to search for `require`d modules")
      ->allow_extra_args (false);

  int play_frames = 60;
  double play_dt = 1.0 / 60.0;
  std::string play_inspect;
  bool play_json = false;
  auto *play_cmd
      = app.add_subcommand ("play", "Run the active scene headlessly");
  play_cmd
      ->add_option ("--frames,-f", play_frames,
                    "Number of simulation frames to step")
      ->capture_default_str ();
  play_cmd->add_option ("--dt", play_dt, "Fixed timestep in seconds")
      ->capture_default_str ();
  play_cmd->add_option ("--inspect,-i", play_inspect,
                        "Entity id (number) or name to snapshot after the run");
  play_cmd->add_flag ("--json,-j", play_json,
                      "Emit the inspect snapshot as JSON");

  try {
    app.parse (argc, argv);
  } catch (const CLI::ParseError &e) {
    return { true, app.exit (e), std::nullopt };
  }

  if (*aot_cmd) {
    // AOT-compile in-process with the engine's real module environment
    // (Module_WeaselApi, Module_Ecs) loaded, so the generated C++ references
    // the real proxy types instead of the hand-written stubs.
    std::string error;
    if (!wsl::das::aot_compile_file (aot_input, aot_output, error,
                                     aot_include_dirs)) {
      wsl::log::cli ()->error ("AOT compilation failed: {}", error);
      return { true, 1, std::nullopt };
    }
    wsl::log::cli ()->info ("AOT-compiled {} -> {}", aot_input, aot_output);
    return { true, 0, std::nullopt };
  }

  if (*import_anim) {
    // Offline asset import: convert rigged glTF into ozz runtime data so
    // the engine can register .skel.ozz / .anim.ozz as resources (M2).
    const wsl::rsc::animation_import_result result
        = wsl::rsc::animation_importer::import (import_anim_file);
    if (!result.ok) {
      wsl::log::cli ()->error ("Animation import failed: {}", result.error);
      return { true, 1, std::nullopt };
    }
    for (const std::string &out : result.outputs) {
      wsl::log::cli ()->info ("Imported {}", out);
    }
    return { true, 0, std::nullopt };
  }

  if (*create_project) {
    if (cp_path.empty () && cp_name.empty ()) {
      wsl::log::cli ()->error ("Project name is required");
      return { true, 1, std::nullopt };
    }
    if (!cp_path.empty () && cp_name.empty ()) {
      cp_name = cp_path;
      cp_path.clear ();
    }

    std::filesystem::path project_root;
    if (cp_path.empty ()) {
      if (no_subdir) {
        project_root = std::filesystem::current_path ();
      } else {
        project_root = std::filesystem::current_path () / cp_name;
      }
    } else {
      if (no_subdir) {
        project_root = std::filesystem::path (cp_path);
      } else {
        project_root = std::filesystem::path (cp_path) / cp_name;
      }
    }

    wsl::rsc::project proj;
    proj.name = cp_name;
    proj.author = "Unknown";
    proj.root_path = std::filesystem::absolute (project_root).string ();
    proj.systems_path = "src/systems";
    proj.components_path = "src/components";
    proj.singletons_path = "src/singletons";
    proj.ui_layouts_path = "src/ui";
    proj.scenes_path = "rsc/scenes";
    proj.models_path = "rsc/models";
    proj.images_path = "rsc/textures";
    proj.cubemaps_path = "rsc/textures/cubemaps";
    proj.audio_path = "rsc/audio";
    proj.fonts_path = "rsc/fonts";
    proj.materials_path = "rsc/materials";
    proj.default_scene_path = "";

    wsl::comp::singl::runtime_context rtc{ "Project Generator", 0, 0,
                                           default_engine_resource_path (),
                                           true };
    rtc.set_editor_ctx (nullptr);
    register_all (rtc);

    wsl::rsc::project_loader const loader (&rtc);
    if (loader.create (proj)) {
      // Report the path that was actually written, not the caller's spelling.
      wsl::log::cli ()->info (
          "Project created successfully at {}",
          wsl::rsc::project_path::normalize (proj.root_path));
      return { true, 0, std::nullopt };
    }
    wsl::log::cli ()->error ("Failed to create project");
    return { true, 1, std::nullopt };
  }

  if (*create_scene) {
    // Scene files use the `.wscn` extension.
    if (!cs_path.ends_with (rsc::scene_file::extension)) {
      cs_path += rsc::scene_file::extension;
    }

    wsl::comp::singl::runtime_context rtc{ "Scene Generator", 0, 0,
                                           default_engine_resource_path (),
                                           true };
    rtc.set_editor_ctx (nullptr);
    register_all (rtc);

    wsl::rsc::scene scene{ &rtc, nullptr, cs_name };
    auto &reg = scene.get_registry ();

    // Skybox
    auto &rendering
        = reg.ctx ().emplace<wsl::comp::singl::rendering_manager> ();
    rendering.skybox = { wsl::rsc::builtin_skybox_procedural };

    // Sample cube
    const wsl::rsc::model_id builtin_cube_id{ entt::hashed_string{
        "builtin://cube" } };
    const wsl::rsc::model_id builtin_sphere_id{ entt::hashed_string{
        "builtin://sphere" } };
    const wsl::rsc::cubemap_id builtin_skybox_id{
      wsl::rsc::builtin_skybox_procedural
    };
    scene.add_resource (wsl::rsc::io::resource_type::model,
                        builtin_cube_id.value);
    scene.add_resource (wsl::rsc::io::resource_type::model,
                        builtin_sphere_id.value);
    scene.add_resource (wsl::rsc::io::resource_type::cubemap,
                        builtin_skybox_id.value);

    auto sample_cube = reg.create ();
    scene.set_entity_name (sample_cube, "Sample Cube");
    reg.emplace<wsl::comp::hierarchy> (sample_cube);
    reg.emplace<wsl::comp::transform> (sample_cube,
                                       glm::vec3 (0.0F, 0.0F, 0.0F));
    reg.emplace<wsl::comp::world_transform> (sample_cube);
    auto &cube_model = reg.emplace<wsl::comp::model_instance_3d> (sample_cube);
    cube_model.id = builtin_cube_id;
    cube_model.scene_index = 0;

    // Default camera
    auto cam_entity = reg.create ();
    scene.set_entity_name (cam_entity, "Scene Default Camera");
    reg.emplace<wsl::comp::hierarchy> (cam_entity);
    auto &cam_transform = reg.emplace<wsl::comp::transform> (
        cam_entity, glm::vec3 (0.0F, 0.0F, 5.0F));
    auto &cam_world_transform
        = reg.emplace<wsl::comp::world_transform> (cam_entity);
    cam_world_transform.value () = cam_transform.model ();
    reg.emplace<wsl::comp::camera> (cam_entity);
    scene.camera = cam_entity;

    // Sunlight
    auto sun_entity = reg.create ();
    scene.set_entity_name (sun_entity, "Sunlight");
    reg.emplace<wsl::comp::hierarchy> (sun_entity);
    auto &sun_transform = reg.emplace<wsl::comp::transform> (
        sun_entity, glm::vec3 (0.0F, 5.0F, 0.0F));
    sun_transform.set_rotation_xyz (wsl::math::vec3f{ 145.0F, -45.0F, 180.0F });
    reg.emplace<wsl::comp::world_transform> (sun_entity).value ()
        = sun_transform.model ();
    reg.emplace<wsl::comp::directional_light> (sun_entity);

    for (const auto &sys_name : cs_systems) {
      rtc.system_factory_registry ().register_system (
          sys_name.c_str (), [sys_name] (wsl::rsc::scene &) {
            return std::make_unique<dummy_system> (sys_name);
          });
      if (auto sys = rtc.system_factory_registry ().create (sys_name, scene)) {
        scene.add_system_instance (std::move (sys), false);
      }
    }

    wsl::rsc::io::scene_snapshot_serializer const serializer{ &rtc, scene };
    if (serializer.save_json (cs_path)) {
      wsl::log::cli ()->info ("Scene created successfully at {}", cs_path);
      return { true, 0, std::nullopt };
    }
    wsl::log::cli ()->error ("Failed to create scene");
    return { true, 1, std::nullopt };
  }

  if (*validate_project) {
    try {
      wsl::rsc::project_loader const loader;
      auto proj = loader.load (vp_path);
      if (proj) {
        wsl::log::cli ()->info ("Project is valid: {}", proj->name);
        return { true, 0, std::nullopt };
      }
      wsl::log::cli ()->error ("Failed to load project (returned null)");
      return { true, 1, std::nullopt };
    } catch (const std::exception &e) {
      wsl::log::cli ()->error ("Project validation failed: {}", e.what ());
      return { true, 1, std::nullopt };
    }
  }

  if (*validate_scene) {
    try {
      wsl::rsc::project_loader const proj_loader;
      auto proj = proj_loader.load (vs_proj_path);
      if (!proj) {
        wsl::log::cli ()->error (
            "Failed to load project context for scene validation");
        return { true, 1, std::nullopt };
      }

      wsl::comp::singl::runtime_context rtc{ "Validator", 0, 0,
                                             default_engine_resource_path (),
                                             true };
      register_all (rtc);

      const std::filesystem::path project_root = proj->root_path;
      const bool has_runtime_code
          = std::filesystem::exists (project_root / proj->systems_path)
            || std::filesystem::exists (project_root / proj->components_path)
            || std::filesystem::exists (project_root / proj->singletons_path);
      if (has_runtime_code) {
        wsl::das::das_engine::initialize_global ();
        if (!rtc.runtime_project_module ().compile_and_load (*proj)) {
          wsl::log::cli ()->error (
              "Runtime module validation failed: {}",
              rtc.runtime_project_module ().last_status ());
          return { true, 1, std::nullopt };
        }
        if (!rtc.runtime_project_module ().finalize_load ()) {
          wsl::log::cli ()->error (
              "Runtime module registration failed: {}",
              rtc.runtime_project_module ().last_error ());
          return { true, 1, std::nullopt };
        }
      }

      wsl::rsc::scene scene{ &rtc, nullptr, "ValidationScene" };
      wsl::rsc::io::scene_snapshot_serializer serializer{ &rtc, scene };
      if (serializer.load_json (vs_scene_path)) {
        wsl::log::cli ()->info ("Scene is valid: {}", vs_scene_path);
        return { true, 0, std::nullopt };
      }
      wsl::log::cli ()->error ("Failed to load scene");
      return { true, 1, std::nullopt };
    } catch (const std::exception &e) {
      wsl::log::cli ()->error ("Scene validation failed: {}", e.what ());
      return { true, 1, std::nullopt };
    }
  }

  std::optional<std::string> repl_command;
  if (*proj_new) {
    repl_command
        = build_repl_command ({ "proj", "new", proj_new_path, proj_new_name });
  }
  if (!repl_command && *proj_load) {
    if (proj_load_strict) {
      repl_command = build_repl_command (
          { "proj", "load", proj_load_path, "--strict-runtime" });
    } else {
      repl_command = build_repl_command ({ "proj", "load", proj_load_path });
    }
  }
  if (!repl_command && *proj_info) {
    repl_command = build_repl_command ({ "proj", "info" });
  }
  if (!repl_command && *proj_save) {
    repl_command = build_repl_command ({ "proj", "save" });
  }
  if (!repl_command && *proj_set) {
    repl_command = build_repl_command (
        { "proj", "set", proj_set_field, proj_set_value });
  }
  if (!repl_command && *scene_new) {
    repl_command = build_repl_command ({ "scene", "new", scene_new_name });
  }
  if (!repl_command && *scene_load) {
    repl_command = build_repl_command ({ "scene", "load", scene_load_path });
  }
  if (!repl_command && *scene_save) {
    std::vector<std::string> args{ "scene", "save" };
    if (!scene_save_path.empty ()) {
      args.push_back (scene_save_path);
    }
    repl_command = build_repl_command (args);
  }
  if (!repl_command && *scene_ls) {
    repl_command = build_repl_command ({ "scene", "ls" });
  }
  if (!repl_command && *scene_status) {
    repl_command = build_repl_command ({ "scene", "status" });
  }
  if (!repl_command && *ent_new) {
    std::vector<std::string> args{ "ent", "new" };
    if (ent_new_empty) {
      args.push_back ("--empty");
    }
    if (!ent_new_name.empty ()) {
      args.push_back (ent_new_name);
    }
    repl_command = build_repl_command (args);
  }
  if (!repl_command && *ent_ls) {
    repl_command = build_repl_command ({ "ent", "ls" });
  }
  if (!repl_command && *ent_rm) {
    repl_command = build_repl_command ({ "ent", "rm", ent_rm_id });
  }
  if (!repl_command && *ent_ren) {
    repl_command
        = build_repl_command ({ "ent", "ren", ent_ren_id, ent_ren_name });
  }
  if (!repl_command && *ent_inspect) {
    repl_command = build_repl_command ({ "ent", "inspect", ent_inspect_id });
  }
  if (!repl_command && *comp_ls) {
    std::vector<std::string> args{ "comp", "ls" };
    if (!comp_ls_entity_id.empty ()) {
      args.push_back (comp_ls_entity_id);
    }
    repl_command = build_repl_command (args);
  }
  if (!repl_command && *comp_avail) {
    repl_command = build_repl_command ({ "comp", "avail" });
  }
  if (!repl_command && *comp_add) {
    repl_command = build_repl_command (
        { "comp", "add", comp_add_entity_id, comp_add_type });
  }
  if (!repl_command && *comp_rm) {
    repl_command = build_repl_command (
        { "comp", "rm", comp_rm_entity_id, comp_rm_type });
  }
  if (!repl_command && *comp_set) {
    repl_command
        = build_repl_command ({ "comp", "set", comp_set_entity_id,
                                comp_set_type, comp_set_prop, comp_set_val });
  }
  if (!repl_command && *comp_create) {
    std::vector<std::string> args{ "comp", "create", comp_create_name };
    if (comp_create_source) {
      args.push_back ("--source");
    }
    repl_command = build_repl_command (args);
  }
  if (!repl_command && *singl_ls) {
    repl_command = build_repl_command ({ "singl", "ls" });
  }
  if (!repl_command && *singl_add) {
    repl_command = build_repl_command ({ "singl", "add", singl_add_name });
  }
  if (!repl_command && *singl_rm) {
    repl_command = build_repl_command ({ "singl", "rm", singl_rm_name });
  }
  if (!repl_command && *singl_info) {
    repl_command = build_repl_command ({ "singl", "info", singl_info_name });
  }
  if (!repl_command && *singl_create) {
    std::vector<std::string> args{ "singl", "create", singl_create_name };
    if (singl_create_source) {
      args.push_back ("--source");
    }
    repl_command = build_repl_command (args);
  }
  if (!repl_command && *singl_set) {
    repl_command = build_repl_command (
        { "singl", "set", singl_set_name, singl_set_prop, singl_set_val });
  }
  if (!repl_command && *sys_ls) {
    repl_command = build_repl_command ({ "sys", "ls" });
  }
  if (!repl_command && *sys_avail) {
    repl_command = build_repl_command ({ "sys", "avail" });
  }
  if (!repl_command && *sys_add) {
    std::string joined;
    for (std::size_t i = 0; i < sys_add_name.size (); ++i) {
      if (i > 0)
        joined += ' ';
      joined += sys_add_name[i];
    }
    repl_command = build_repl_command ({ "sys", "add", joined });
  }
  if (!repl_command && *sys_create) {
    std::vector<std::string> args{ "sys", "create", sys_create_name };
    if (sys_create_source) {
      args.push_back ("--source");
    }
    repl_command = build_repl_command (args);
  }
  if (!repl_command && *sig_cmd) {
    // Build a repl command that forwards all remaining args to the REPL 'sig'
    // handler
    std::vector<std::string> args;
    args.push_back ("sig");
    for (const auto &remaining_str : app.remaining ()) {
      args.push_back (remaining_str);
    }
    repl_command = build_repl_command (args);
  }
  if (!repl_command && *rsc_ls) {
    std::vector<std::string> args{ "rsc", "ls" };
    if (!rsc_ls_type.empty ()) {
      args.push_back (rsc_ls_type);
    }
    repl_command = build_repl_command (args);
  }
  if (!repl_command && *rsc_add) {
    std::vector<std::string> args{ "rsc", "add", rsc_add_type, rsc_add_path };
    if (rsc_add_load) {
      args.push_back ("--load");
    }
    repl_command = build_repl_command (args);
  }
  if (!repl_command && *rsc_rm) {
    repl_command
        = build_repl_command ({ "rsc", "rm", rsc_rm_type, rsc_rm_name });
  }
  if (!repl_command && *rsc_load) {
    repl_command
        = build_repl_command ({ "rsc", "load", rsc_load_type, rsc_load_name });
  }
  if (!repl_command && *rsc_unload) {
    repl_command = build_repl_command (
        { "rsc", "unload", rsc_unload_type, rsc_unload_name });
  }
  if (!repl_command && *rsc_info) {
    repl_command
        = build_repl_command ({ "rsc", "info", rsc_info_type, rsc_info_name });
  }
  if (!repl_command && *prefab_ls) {
    repl_command = build_repl_command ({ "prefab", "ls" });
  }
  if (!repl_command && *prefab_save) {
    std::vector<std::string> args{ "prefab", "save" };
    if (!prefab_save_path.empty ()) {
      args.push_back (prefab_save_path);
    }
    repl_command = build_repl_command (args);
  }
  if (!repl_command && *prefab_load) {
    repl_command = build_repl_command ({ "prefab", "load", prefab_load_path });
  }
  if (!repl_command && *prefab_instantiate) {
    std::vector<std::string> args{ "prefab", "instantiate",
                                   prefab_instantiate_name };
    if (!prefab_instantiate_parent.empty ()) {
      args.push_back (prefab_instantiate_parent);
    }
    repl_command = build_repl_command (args);
  }
  if (!repl_command && *play_cmd) {
    std::vector<std::string> args{ "play", "--frames",
                                   std::to_string (play_frames), "--dt",
                                   std::to_string (play_dt) };
    if (!play_inspect.empty ()) {
      args.push_back ("--inspect");
      args.push_back (play_inspect);
    }
    if (play_json) {
      args.push_back ("--json");
    }
    repl_command = build_repl_command (args);
  }

  auto extras = app.remaining ();
  if (repl_command && !extras.empty ()) {
    wsl::log::cli ()->error ("Unexpected extra arguments after CLI subcommand");
    return { true, 1, std::nullopt };
  }

  // note: proj info/save and scene load/save previously required
  // --interactive or --attach but now work standalone (with auto-save
  // in one-shot mode).

  result res;
  if (!project_to_load.empty ()) {
    res.project_to_load = project_to_load;
  }
  if (!scene_to_load.empty ()) {
    res.scene_to_load = scene_to_load;
  }
  res.interactive = interactive;
  res.attach = attach;

  if (repl_command) {
    res.command = *repl_command;
  } else if (!extras.empty ()) {
    res.command = build_repl_command (extras);
  }

  if (res.interactive && res.command) {
    wsl::log::cli ()->error (
        "Choose either --interactive or a non-interactive command");
    return { true, 1, std::nullopt };
  }

  return res;
}

std::string
cli_handler::default_engine_resource_path ()
{
  const char *base_path = SDL_GetBasePath ();
  if (base_path != nullptr) {
    std::filesystem::path const exe_dir (base_path);
    std::filesystem::path const share_dir = exe_dir / ".." / "share" / "weasel";
    if (std::filesystem::exists (share_dir / "compiled_shaders")) {
      return share_dir.string ();
    }
  }

#ifdef WEASEL_BUILD_DIR
  return WEASEL_BUILD_DIR;
#elif defined(WEASEL_SOURCE_DIR)
  return WEASEL_SOURCE_DIR;
#else
  return ".";
#endif
}

} // namespace wsl::cli
