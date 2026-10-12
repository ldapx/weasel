#include "mcp_server_app.hpp"
#include "cli_passthrough.hpp"
#include "cli_reference.hpp"
#include "das_api_info.hpp"
#include "component_info.hpp"
#include "mcp_helpers.hpp"
#include "mcp_server_handlers.hpp"
#include "namespace_info.hpp"

#include <agentsdk/mcp/mcp_tool_builder.hpp>

#include <functional>
#include <map>
#include <sstream>
#include <utility>
#include <vector>

namespace wsl::mcp_server
{

namespace
{

using tool_handler
    = std::function<agentsdk::result<mcp::tool_result, mcp::mcp_error> (
        const std::string &)>;

struct tool_entry
{
  mcp::tool tool;
  tool_handler handler;
};

} // namespace

agentsdk::result<mcp::tool_result, mcp::mcp_error>
handle_list_commands (const std::string &args_json)
{
  (void)args_json;
  const auto &ref = get_cli_reference ();

  std::ostringstream oss;
  oss << "Available weasel-cli commands:\n\n";

  oss << "== CLI Subcommands ==\n";
  for (const auto &cmd : ref) {
    if (cmd.category == "cli") {
      oss << cmd.name << " - " << cmd.description << "\n";
    }
  }

  oss << "\n== REPL Commands ==\n";
  for (const auto &cmd : ref) {
    if (cmd.category == "repl") {
      oss << cmd.name << " - " << cmd.description << "\n";
    }
  }

  return text_result (oss.str ());
}

agentsdk::result<mcp::tool_result, mcp::mcp_error>
handle_describe_command (const std::string &args_json)
{
  std::string name;
  std::string error;
  if (!get_string_arg (args_json, "name", name, error)) {
    return param_error (error);
  }

  const cli_command_info *info = find_command (name);

  if (!info) {
    return param_error ("Unknown command: " + name);
  }

  std::ostringstream oss;
  oss << "Command: " << info->name << "\n";
  oss << "Category: " << info->category << "\n";
  oss << "Description: " << info->description << "\n";
  oss << "Syntax: " << info->syntax << "\n";

  if (!info->parameters.empty ()) {
    oss << "Parameters:\n";
    for (const auto &p : info->parameters) {
      oss << "  - " << p << "\n";
    }
  }

  if (!info->examples.empty ()) {
    oss << "Examples:\n";
    for (const auto &ex : info->examples) {
      oss << "  " << ex << "\n";
    }
  }

  return text_result (oss.str ());
}

agentsdk::result<mcp::tool_result, mcp::mcp_error>
handle_get_quick_start (const std::string &args_json)
{
  (void)args_json;
  std::ostringstream oss;
  oss << "Weasel Engine CLI Quick Start\n\n";
  oss << "1. Create a project:\n";
  oss << "   weasel-cli create-project ./mygame MyGame\n\n";
  oss << "2. Create a scene:\n";
  oss << "   weasel-cli create-scene ./mygame/rsc/scenes/level1.wscn "
         "Level1\n\n";
  oss << "3. Validate a project:\n";
  oss << "   weasel-cli validate-project ./mygame/wslpro.json\n\n";
  oss << "4. One-shot scripting (PREFERRED form, auto-save):\n";
  oss << "   weasel-cli -a ent new Player\n";
  oss << "   weasel-cli -a comp add 0 rigid_body\n";
  oss << "   Always prefer -a: it attaches to the first running editor\n";
  oss << "   instance and uses its open project. When no editor is running\n";
  oss << "   it warns and falls back to standalone execution; add --project\n";
  oss << "   <path> to select the project for the standalone run:\n";
  oss << "   weasel-cli -a --project ./mygame/wslpro.json --scene Level1 \\\n";
  oss << "       comp set 0 rigid_body shape sphere\n";
  oss << "   Each mutation command auto-saves when running standalone.\n";
  oss << "   When attached to a live editor, saving is done in the editor.\n\n";
  oss << "5. Start the interactive REPL (manual save):\n";
  oss << "   weasel-cli -i -a        # attached to the running editor\n";
  oss << "   weasel-cli --project ./mygame/wslpro.json --interactive\n\n";
  oss << "   Inside the REPL you can manage entities, components, scenes, and "
         "more:\n";
  oss << "     scene new Level1              # Create a new empty scene\n";
  oss << "     (core systems—Transform, Physics, 3D Render, etc.—are\n";
  oss << "     automatically present in every scene and do not need to\n";
  oss << "     be added manually)\n";
  oss << "     ent new Player                # Create an entity named Player\n";
  oss << "     ent new --empty MyBare        # Create entity without default "
         "components\n";
  oss << "     comp add 0 transform          # Add Transform component\n";
  oss << "     comp set 0 transform position '[0,2,0]'  # Set position\n";
  oss << "     comp add 0 rigid_body         # Add RigidBody component\n";
  oss << "     comp set 0 rigid_body shape sphere  # Set shape to sphere\n";
  oss << "     comp set 0 rigid_body radius 0.5      # Set radius\n";
  oss << "     comp rm 0 rigid_body          # Remove component\n";
  oss << "     ent inspect 0                 # Inspect entity\n";
  oss << "     scene save                    # Save scene (manual in REPL)\n";
  oss << "     help                          # Show full command reference\n\n";
  oss << "6. Attach behavior:\n";
  oss << "   weasel-cli -a <command>   # first editor instance, its project\n";
  oss << "   weasel-cli -a --project P # only an editor with project P\n";
  oss << "   No editor found: warns and runs standalone instead.\n\n";
  oss << "For full command reference, use the list_commands and "
         "describe_command tools.\n";
  oss << "For component property details, use describe_component (e.g. Rigid "
         "Body).\n";
  oss << "For implementation status, use cli_capabilities.";

  return text_result (oss.str ());
}

agentsdk::result<mcp::tool_result, mcp::mcp_error>
handle_cli_capabilities (const std::string &args_json)
{
  (void)args_json;
  std::ostringstream oss;
  oss << "Weasel CLI/REPL Capabilities Manifest\n";
  oss << "======================================\n\n";

  oss << "== Save Policy ==\n";
  oss << "  Prefer attach mode: weasel-cli -a <command> drives the running\n";
  oss << "  editor instance (its open project) — changes appear live and are\n";
  oss << "  saved manually in the editor via scene save / proj save.\n";
  oss << "  Standalone one-shot runs (including -a when no editor was "
         "found):\n";
  oss << "  mutation commands auto-save the active scene and project after\n";
  oss << "  each change.\n\n";

  oss << "== Fully Implemented ==\n";
  oss << "  proj new        - Create and load a new project\n";
  oss << "  proj load       - Load an existing project\n";
  oss << "  proj info       - Show project metadata\n";
  oss << "  proj save       - Save project metadata\n";
  oss << "  scene new       - Create a new empty scene\n";
  oss << "  scene load      - Load a scene from disk\n";
  oss << "  scene save      - Save the active scene\n";
  oss << "  scene ls        - List scene assets\n";
  oss << "  scene status    - Show scene statistics\n";
  oss << "  ent new         - Create a new entity\n";
  oss << "  ent ls          - List all entities\n";
  oss << "  ent rm          - Remove (destroy) an entity\n";
  oss << "  ent ren         - Rename an entity\n";
  oss << "  ent inspect     - Show entity details\n";
  oss << "  comp ls         - List components (registered or on an entity)\n";
  oss << "  comp avail      - List registered component types\n";
  oss << "  comp add        - Add a component to an entity\n";
  oss << "  comp rm         - Remove a component from an entity\n";
  oss << "  comp set        - Set a component property\n";
  oss << "  comp create     - Generate a world component template\n";
  oss << "  singl ls        - List singleton components with values\n";
  oss << "  singl add       - Add a value-owned singleton\n";
  oss << "  singl create    - Generate a singleton template\n";
  oss << "  singl set       - Set a singleton property\n";
  oss << "  sys ls          - List all systems in the active scene (core + "
         "user)\n";
  oss << "  sys avail       - List user-defined system types available via "
         "'sys add'\n";
  oss << "  sys add         - Add a user-defined system to the active scene\n";
  oss << "  sys create      - Generate a Daslang system template\n";
  oss << "  das             - List all functions exposed to .das scripts "
         "(grouped)\n";
  oss << "  das <name>      - Show a function's signature and side effects\n";
  oss << "  script status   - Show runtime script load state, registry "
         "counts,\n";
  oss << "                    cache path, last error\n";
  oss << "  script reload   - Recompile and reload runtime scripts (CLI "
         "mirror of\n";
  oss << "                    the editor's Reload Scripts button)\n";
  oss << "  script invalidate - Clear runtime state and delete the "
         "registration\n";
  oss << "                    cache file\n";
  oss << "  check           - Run validation checks\n";
  oss << "  rsc ls          - List resources\n";
  oss << "  rsc add         - Register a resource (--load to load)\n";
  oss << "  rsc rm          - Remove a registered resource\n";
  oss << "  rsc load        - Load a registered resource by name/path\n";
  oss << "  rsc unload      - Unload a registered resource\n";
  oss << "  rsc info        - Show resource metadata\n";
  oss << "  help            - Show help\n";
  oss << "  exit/quit       - Exit REPL\n";
  oss << "  cls             - Clear screen\n";
  oss << "  sig             - Signal management (ls, handlers, connections, "
         "connect, disconnect)\n\n";

  oss << "== Documented but NOT Implemented ==\n";
  oss << "  (none)\n\n";

  oss << "== Previously Fixed Limitations ==\n";
  oss << "  - Component type names now accept short names (transform,\n";
  oss << "    rigid_body), display names (Transform), or fully qualified\n";
  oss << "    C++ names (wsl::comp::rigid_body)\n";
  oss << "  - model_id property on model_instance_3d now accepts\n";
  oss << "    builtin:// paths (e.g. builtin://cube) without crashing\n";
  oss << "  - sys add <name> can attach user-defined systems to scenes "
         "dynamically\n";
  oss << "  - Nested comp set properties (motion_type.value,\n";
  oss << "    collision_layer.value) now persist through scene save\n";
  oss << "  - scene save without a path writes to project's scenes_path\n";
  oss << "    (no double .wscn extension)\n";
  oss << "  - # comments and inline comments are now supported in REPL\n\n";
  oss << "== Scene System Notes ==\n";
  oss << "  - `scene new` creates a scene with no per-scene user systems\n";
  oss << "  - 8 built-in core systems are always present in every scene:\n";
  oss << "    Transform, Physics, 3D Render, Audio, Lighting, Skybox,\n";
  oss << "    Shadow, UI\n";
  oss << "  - Core systems are part of the engine and cannot be added or\n";
  oss << "    removed via `sys add`/`sys rm`\n";
  oss << "  - `sys add` is only for user-defined systems created with\n";
  oss << "    `sys create`\n";
  oss << "  - Core singleton components present in every scene:\n";
  oss << "    Runtime Context, Scene Manager, Resource Manager, UI Manager,\n";
  oss << "    Rendering Manager, Physics Manager\n";
  oss << "  - `comp set` supports primitive types, enums (by name or int),\n";
  oss << "    compound types (vec3, quat) via JSON arrays/objects,\n";
  oss << "    and nested property paths (e.g. motion_type.value)\n";
  oss << "  - Physics components modified via comp set may need manual\n";
  oss << "    re-sync (e.g. rigid_body changes require on_inspector_changed)\n";
  oss << "  - comp set uses EnTT meta property names (e.g. motion_type)\n";
  oss << "    NOT display names (e.g. Motion Type) or serialization names\n";
  oss << "  - comp set only works on components with register_meta() called\n";

  return text_result (oss.str ());
}

mcp_server_app::mcp_server_app ()
    : m_server (mcp::implementation_info{ "Weasel MCP Server", std::nullopt,
                                          "0.1.0" },
                mcp::server_capabilities{ mcp::tools_capability{ false },
                                          std::nullopt, std::nullopt, false,
                                          false })
{
  const char *instructions
      = "This MCP server provides reference documentation for the Weasel "
        "Engine and CLI passthrough tools for mutations. When suggesting "
        "weasel-cli commands, ALWAYS prefer attach mode: 'weasel-cli -a "
        "<command>' attaches to the first running editor instance and uses "
        "its open project; if no editor is running it warns and falls back "
        "to standalone execution. Only add --project when a specific "
        "project is required.\n"
        "Available tool categories:\n"
        "  CLI/REPL: list_commands, describe_command, get_quick_start, "
        "cli_capabilities\n"
        "  Components: list_components, describe_component (incl. behavior "
        "notes)\n"
        "  Engine Namespaces: list_namespaces, describe_namespace — learn how "
        "to write a game using comp/sys/rsc/phys/gfx/math/reg\n"
        "  Daslang API: describe_das_api — authoritative list of every "
        "function "
        "exposed to .das scripts (generated from the runtime registration "
        "site)\n"
        "  Mutations (execute weasel-cli commands directly):\n"
        "    script_reload — recompile/reload the project's runtime scripts\n"
        "    run_cli_command — run any weasel-cli command (requires "
        "confirm=true); prefer this over raw bash for engine mutations\n"
        "    scene_save — save the active scene (structured, confirm=true)\n"
        "    entity_add — create an entity (structured, confirm=true)\n"
        "    entity_remove — destroy an entity by id/name (structured, "
        "confirm=true)\n"
        "    component_set — set a component property (structured, "
        "confirm=true)";
  m_server.set_instructions (instructions);

  register_tools ();
}

void
mcp_server_app::run ()
{
  m_server.run ();
}

void
mcp_server_app::register_tools ()
{
  std::vector<tool_entry> tools;

  tools.push_back ({ mcp::tool_builder ("list_commands")
                         .with_description (
                             "List all available weasel-cli and REPL commands.")
                         .build (),
                     handle_list_commands });

  tools.push_back (
      { mcp::tool_builder ("describe_command")
            .with_description (
                "Get detailed usage information for a specific command.")
            .with_string_param ("name", "Name of the command to describe.")
            .build (),
        handle_describe_command });

  tools.push_back (
      { mcp::tool_builder ("get_quick_start")
            .with_description ("Return a quick-start guide for the Weasel CLI.")
            .build (),
        handle_get_quick_start });

  tools.push_back (
      { mcp::tool_builder ("list_components")
            .with_description (
                "List all registered ECS components with their fully "
                "qualified C++ type names.")
            .build (),
        handle_list_components });

  tools.push_back (
      { mcp::tool_builder ("describe_component")
            .with_description (
                "Get detailed information about a registered component: all "
                "properties, their types, enum values, and descriptions.")
            .with_string_param (
                "name", "Component name (display name like 'Rigid Body' "
                        "or C++ type like 'wsl::comp::rigid_body').")
            .build (),
        handle_describe_component });

  tools.push_back (
      { mcp::tool_builder ("list_namespaces")
            .with_description (
                "List all engine namespaces (comp, sys, rsc, phys, gfx, math, "
                "reg) with brief descriptions. Use describe_namespace for "
                "detailed documentation on how to use each one.")
            .build (),
        handle_list_namespaces });

  tools.push_back (
      { mcp::tool_builder ("describe_namespace")
            .with_description (
                "Get detailed documentation about an engine namespace: what "
                "it contains, key classes, and how to use it to write a game.")
            .with_string_param (
                "name",
                "Namespace name: comp, sys, rsc, phys, gfx, math, or reg.")
            .build (),
        handle_describe_namespace });

  tools.push_back (
      { mcp::tool_builder ("describe_das_api")
            .with_description (
                "Describe the Daslang API exposed to .das scripts by the "
                "engine "
                "(Module_WeaselApi). With no 'name' returns a grouped function "
                "listing; with 'name' returns the full entry (signature, side "
                "effects, category). Generated from the runtime registration "
                "site — always authoritative, never hand-written.")
            .with_string_param (
                "name",
                "Optional function name (e.g. apply_force, is_key_pressed).",
                false)
            .build (),
        handle_describe_das_api });

  tools.push_back (
      { mcp::tool_builder ("cli_capabilities")
            .with_description ("List which CLI/REPL commands are actually "
                               "implemented vs. documented but missing.")
            .build (),
        handle_cli_capabilities });

  tools.push_back (
      { mcp::tool_builder ("script_reload")
            .with_description (
                "Recompile and reload the user Daslang runtime scripts of the "
                "project open in the attached editor (same as the editor's "
                "'Reload Scripts' button or `weasel-cli -a script reload`). "
                "Returns the CLI output including component/system counts, "
                "or the compiler error on failure.")
            .build (),
        handle_script_reload });

  tools.push_back (
      { mcp::tool_builder ("run_cli_command")
            .with_description (
                "Run any weasel-cli command and return its output. Prefers "
                "attach mode (-a): if a Weasel editor instance is running, "
                "the command executes against its open project; otherwise it "
                "runs standalone. Mutates engine state — pass confirm=true. "
                "Examples: 'scene ls', 'sys add \"Ball Controller System\"', "
                "'ent new Ball'.")
            .with_string_param ("command",
                                "The weasel-cli command line, e.g. 'scene "
                                "ls' or 'comp set 42 rigid_body radius 0.5'.")
            .with_boolean_param (
                "confirm", "Must be true to allow mutating commands.", true)
            .with_string_param (
                "project", "Optional project path for standalone runs.", false)
            .with_boolean_param (
                "attach", "Attach to a running editor (default true).", false)
            .with_number_param ("timeout_seconds",
                                "Kill the command after this many seconds "
                                "(default 120, max 600).",
                                false)
            .build (),
        handle_run_cli_command });

  tools.push_back (
      { mcp::tool_builder ("scene_save")
            .with_description (
                "Save the active scene back to disk (default or explicit "
                "path). Structured alternative to `run_cli_command 'scene "
                "save'`. Requires confirm=true.")
            .with_string_param ("path",
                                "Optional explicit scene file path to write "
                                "to (overrides the loaded path).",
                                false)
            .with_boolean_param (
                "as_default",
                "If true, also set this scene as the project's "
                "default_scene_path.",
                false)
            .with_boolean_param ("confirm", "Must be true to mutate.", true)
            .with_string_param ("project",
                                "Optional project path (standalone).", false)
            .with_boolean_param ("attach", "Attach to a running editor.", false)
            .with_number_param ("timeout_seconds", "Command timeout.", false)
            .build (),
        handle_scene_save });

  tools.push_back (
      { mcp::tool_builder ("entity_add")
            .with_description (
                "Create a new entity in the active scene. Structured "
                "alternative to `run_cli_command 'ent new'`. Requires "
                "confirm=true.")
            .with_string_param ("name", "Name of the new entity.", true)
            .with_boolean_param ("empty",
                                 "If true, create an empty entity (no "
                                 "Transform/WorldTransform/Hierarchy).",
                                 false)
            .with_boolean_param ("confirm", "Must be true to mutate.", true)
            .with_string_param ("project",
                                "Optional project path (standalone).", false)
            .with_boolean_param ("attach", "Attach to a running editor.", false)
            .with_number_param ("timeout_seconds", "Command timeout.", false)
            .build (),
        handle_entity_add });

  tools.push_back (
      { mcp::tool_builder ("entity_remove")
            .with_description (
                "Destroy an entity from the active scene by id or name. "
                "Structured alternative to `run_cli_command 'ent rm'`. "
                "Requires confirm=true.")
            .with_string_param ("id", "Entity id (number) or name.", true)
            .with_boolean_param ("confirm", "Must be true to mutate.", true)
            .with_string_param ("project",
                                "Optional project path (standalone).", false)
            .with_boolean_param ("attach", "Attach to a running editor.", false)
            .with_number_param ("timeout_seconds", "Command timeout.", false)
            .build (),
        handle_entity_remove });

  tools.push_back (
      { mcp::tool_builder ("component_set")
            .with_description (
                "Set a property on a component instance of an entity. "
                "Structured alternative to `run_cli_command 'comp set'`. "
                "Requires confirm=true.")
            .with_string_param ("entity", "Entity id (number) or name.", true)
            .with_string_param ("component", "Component type name.", true)
            .with_string_param ("property",
                                "Property name (dot-path for "
                                "nested fields).",
                                true)
            .with_string_param ("value", "New value as a string.", true)
            .with_boolean_param ("confirm", "Must be true to mutate.", true)
            .with_string_param ("project",
                                "Optional project path (standalone).", false)
            .with_boolean_param ("attach", "Attach to a running editor.", false)
            .with_number_param ("timeout_seconds", "Command timeout.", false)
            .build (),
        handle_component_set });

  auto tools_ptr = std::make_shared<std::map<std::string, tool_entry>> ();
  for (auto &t : tools) {
    tools_ptr->emplace (t.tool.name, std::move (t));
  }

  m_server.set_list_tools_handler (
      [tools_ptr] (const std::optional<std::string> &)
          -> agentsdk::result<mcp::tools_list_result, mcp::mcp_error> {
        mcp::tools_list_result r;
        for (const auto &[name, entry] : *tools_ptr) {
          (void)name;
          r.tools.push_back (entry.tool);
        }
        return r;
      });

  m_server.set_call_tool_handler (
      [tools_ptr] (const std::string &name, const std::string &args)
          -> agentsdk::result<mcp::tool_result, mcp::mcp_error> {
        auto it = tools_ptr->find (name);
        if (it == tools_ptr->end ()) {
          return mcp::mcp_error{ mcp::METHOD_NOT_FOUND,
                                 "Tool not found: " + name, std::nullopt };
        }
        try {
          return it->second.handler (args);
        } catch (const std::exception &e) {
          mcp::tool_result tr;
          mcp::text_content t;
          t.text = std::string ("Internal error: ") + e.what ();
          tr.content.push_back (mcp::content_block{ t });
          tr.is_error = true;
          return tr;
        }
      });
}

} // namespace wsl::mcp_server
