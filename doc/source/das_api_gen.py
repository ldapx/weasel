#!/usr/bin/env python3
"""
das_api_gen.py -- Generate Sphinx .rst documentation from Weasel daslang bindings.

Parses wsl_api_module.cpp to extract addExtern<DAS_BIND_FUN(...)> registrations
and generates .rst files using the das: domain directives.

Usage:
    python3 das_api_gen.py --source <src_dir> --output <rst_output_dir>
"""

import argparse
import os
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional


@dataclass
class BoundFunction:
    """A single function bound to daslang via addExtern."""

    cpp_name: str  # C++ function name (e.g. wsl_entity_create)
    das_name: str  # daslang-visible name (e.g. entity_create)
    args: list[str] = field(default_factory=list)
    return_type: str = ""
    description: str = ""
    section: str = ""
    side_effects: str = ""
    cpp_signature: str = ""


@dataclass
class BoundConstant:
    """A constant bound via addConstant."""

    das_name: str
    cpp_type: str
    value: str
    description: str = ""


@dataclass
class BindingModule:
    """A complete daslang module extracted from C++ source."""

    name: str
    description: str = ""
    functions: list[BoundFunction] = field(default_factory=list)
    constants: list[BoundConstant] = field(default_factory=list)


# -- Section descriptions (curated) -------------------------------------------

SECTION_DOCS = {
    "Entity operations": "Functions for creating, destroying, and querying entities.",
    "Per-component add/remove": "Add or remove specific components from entities.",
    "Generic component queries": "Generic queries that work with any component type.",
    "Transform operations": "Get and set entity positions, rotations, and scales.",
    "Scene operations": "Query and modify the active scene (find entities, cameras).",
    "Component type ID constants": "Stable type IDs for built-in component types.",
    "Event query functions": "Query input events (mouse motion, button clicks).",
    "SDL window operations": "Window management: cursor, mouse mode, window size.",
    "Window size": "Get the current window dimensions.",
    "Entity iteration": "Iterate over entities with specific components.",
    "Entity iteration by component": "Iterate over entities that own a given component type.",
    "Component type lookup": "Look up component type IDs at runtime.",
    "Component field access": "Read and write component fields by byte offset.",
    "Raycasting": "Cast rays from camera through mouse position and intersect with planes.",
    "Logging": "Log messages at various severity levels.",
    "Time": "Monotonic clock accessible from any system callback.",
    "Physics": "Drive rigid bodies and character controllers via the Jolt physics engine.",
    "Audio": "Control playback of entity audio components.",
    "2D transform": "Get and set 2D local transforms.",
    "2D camera": "Control 2D orthographic cameras.",
    "2D sprite": "Control 2D sprite appearance (color, size, flip).",
    "Lights": "Control light color and intensity at runtime.",
    "Model instance": "Swap models and material overrides on entities.",
}

# -- Per-function descriptions (curated) --------------------------------------

FUNCTION_DOCS = {
    "log_info": "Logs a message at info level.",
    "log_debug": "Logs a message at debug level.",
    "log_warn": "Logs a message at warning level.",
    "log_error": "Logs a message at error level.",
    "get_time": "Returns the elapsed time in seconds since the module was first queried.",
    "get_elapsed_time": "Returns the elapsed time in seconds since the module was first queried.",
    "set_rigid_body_linear_velocity": "Sets the rigid body linear velocity (world space).",
    "get_rigid_body_linear_velocity_x": "Gets the rigid body linear velocity X (world space).",
    "get_rigid_body_linear_velocity_y": "Gets the rigid body linear velocity Y (world space).",
    "get_rigid_body_linear_velocity_z": "Gets the rigid body linear velocity Z (world space).",
    "set_rigid_body_angular_velocity": "Sets the rigid body angular velocity.",
    "get_rigid_body_angular_velocity_x": "Gets the rigid body angular velocity X.",
    "get_rigid_body_angular_velocity_y": "Gets the rigid body angular velocity Y.",
    "get_rigid_body_angular_velocity_z": "Gets the rigid body angular velocity Z.",
    "apply_impulse": "Applies an impulse to the rigid body.",
    "apply_force": "Applies a continuous force to the rigid body.",
    "get_body_position_x": "Gets the rigid body world position X.",
    "get_body_position_y": "Gets the rigid body world position Y.",
    "get_body_position_z": "Gets the rigid body world position Z.",
    "set_character_body_velocity": "Sets the desired velocity of a character body (kinematic controller).",
    "get_character_body_velocity_x": "Gets the character body desired velocity X.",
    "get_character_body_velocity_y": "Gets the character body desired velocity Y.",
    "get_character_body_velocity_z": "Gets the character body desired velocity Z.",
    "audio_play": "Starts playback of the entity's audio component.",
    "audio_stop": "Stops playback of the entity's audio component.",
    "audio_pause": "Pauses playback of the entity's audio component.",
    "audio_resume": "Resumes playback of the entity's audio component.",
    "audio_set_volume": "Sets the playback volume (0.0 to 1.0) of the entity's audio component.",
    "set_position_2d": "Sets the 2D local position.",
    "get_position_2d_x": "Gets the 2D local position X.",
    "get_position_2d_y": "Gets the 2D local position Y.",
    "set_rotation_2d": "Sets the 2D Z rotation in degrees.",
    "get_rotation_2d": "Gets the 2D Z rotation in degrees.",
    "set_scale_2d": "Sets the 2D local scale.",
    "get_scale_2d_x": "Gets the 2D local scale X.",
    "get_scale_2d_y": "Gets the 2D local scale Y.",
    "set_camera_2d_zoom": "Sets the 2D camera zoom.",
    "get_camera_2d_zoom": "Gets the 2D camera zoom.",
    "set_camera_2d_layer": "Sets the 2D camera layer (render order).",
    "get_camera_2d_layer": "Gets the 2D camera layer (render order).",
    "set_sprite_color": "Sets the sprite tint color (RGBA, 0.0 to 1.0).",
    "get_sprite_color_r": "Gets the sprite tint color R.",
    "get_sprite_color_g": "Gets the sprite tint color G.",
    "get_sprite_color_b": "Gets the sprite tint color B.",
    "get_sprite_color_a": "Gets the sprite tint color A.",
    "set_sprite_size": "Sets the sprite size in pixels.",
    "get_sprite_size_x": "Gets the sprite size X in pixels.",
    "get_sprite_size_y": "Gets the sprite size Y in pixels.",
    "set_sprite_flip_h": "Sets the sprite horizontal flip.",
    "get_sprite_flip_h": "Gets the sprite horizontal flip.",
    "set_sprite_flip_v": "Sets the sprite vertical flip.",
    "get_sprite_flip_v": "Gets the sprite vertical flip.",
    "set_point_light_color": "Sets the point light color (RGB, 0.0 to 1.0).",
    "get_point_light_color_r": "Gets the point light color R.",
    "get_point_light_color_g": "Gets the point light color G.",
    "get_point_light_color_b": "Gets the point light color B.",
    "set_point_light_intensity": "Sets the point light intensity.",
    "get_point_light_intensity": "Gets the point light intensity.",
    "set_directional_light_color": "Sets the directional light color (RGB, 0.0 to 1.0).",
    "get_directional_light_color_r": "Gets the directional light color R.",
    "get_directional_light_color_g": "Gets the directional light color G.",
    "get_directional_light_color_b": "Gets the directional light color B.",
    "set_directional_light_intensity": "Sets the directional light intensity.",
    "get_directional_light_intensity": "Gets the directional light intensity.",
    "set_spot_light_color": "Sets the spot light color (RGB, 0.0 to 1.0).",
    "get_spot_light_color_r": "Gets the spot light color R.",
    "get_spot_light_color_g": "Gets the spot light color G.",
    "get_spot_light_color_b": "Gets the spot light color B.",
    "set_spot_light_intensity": "Sets the spot light intensity.",
    "get_spot_light_intensity": "Gets the spot light intensity.",
    "set_model": 'Sets the model on the entity from a resource path (e.g. "res://...").',
    "set_model_material_override": "Sets a per-instance material override from a resource path.",
    "set_model_visibility_range": "Sets the model max draw distance in world units (0 = unlimited).",
    "entity_create": "Creates a new empty entity and returns its ID.",
    "entity_destroy": "Destroys an entity and all of its components.",
    "entity_valid": "Returns ``true`` if the entity ID refers to a live entity.",
    "entity_is_null": "Returns ``true`` if the entity ID is the null sentinel.",
    "null_entity": "Returns the null entity sentinel value.",
    "has_component": "Returns ``true`` if the entity owns a component with the given type ID.",
    "add_transform": "Adds a Transform component to the entity.",
    "remove_transform": "Removes the Transform component from the entity.",
    "add_camera": "Adds a Camera component to the entity.",
    "remove_camera": "Removes the Camera component from the entity.",
    "add_hierarchy": "Adds a Hierarchy component to the entity.",
    "remove_hierarchy": "Removes the Hierarchy component from the entity.",
    "add_world_transform": "Adds a World Transform component to the entity.",
    "remove_world_transform": "Removes the World Transform component from the entity.",
    "get_position": "Reads the entity's local position. Use ``get_transform_x/y/z`` to extract components.",
    "set_position": "Sets the entity's local position.",
    "get_rotation": "Reads the entity's rotation as Euler angles (pitch, yaw, roll) in degrees.",
    "set_rotation": "Sets the entity's rotation from Euler angles in degrees.",
    "get_scale": "Reads the entity's local scale. Use ``get_transform_x/y/z`` to extract components.",
    "set_scale": "Sets the entity's local scale.",
    "get_transform_x": "Returns the X component of the last ``get_position``/``get_scale`` call.",
    "get_transform_y": "Returns the Y component of the last ``get_position``/``get_scale`` call.",
    "get_transform_z": "Returns the Z component of the last ``get_position``/``get_scale`` call.",
    "find_entity_by_name": "Finds an entity by its scene name. Returns null if not found.",
    "get_active_camera": "Returns the entity ID of the active camera.",
    "set_active_camera": "Sets the active camera to the given entity.",
    "TYPE_TRANSFORM": "Stable type ID for the Transform component.",
    "TYPE_CAMERA": "Stable type ID for the Camera component.",
    "TYPE_HIERARCHY": "Stable type ID for the Hierarchy component.",
    "TYPE_WORLD_TRANSFORM": "Stable type ID for the World Transform component.",
    "get_event_kind": "Returns the type of the current event (see EVENT_* constants).",
    "get_event_mouse_dx": "Returns the horizontal mouse delta for mouse motion events.",
    "get_event_mouse_dy": "Returns the vertical mouse delta for mouse motion events.",
    "get_event_mouse_x": "Returns the mouse X position for button events.",
    "get_event_mouse_y": "Returns the mouse Y position for button events.",
    "get_event_mouse_button": "Returns which mouse button was pressed/released.",
    "set_relative_mouse_mode": "Enables or disables relative mouse mode (FPS-style mouse).",
    "cursor_visible": "Returns ``true`` if the cursor is currently visible.",
    "show_cursor": "Shows the system cursor.",
    "hide_cursor": "Hides the system cursor.",
    "refresh_window_size": "Queries the OS for the current window size (must call before get_window_width/Height).",
    "get_window_width": "Returns the window width. Call ``refresh_window_size`` first.",
    "get_window_height": "Returns the window height. Call ``refresh_window_size`` first.",
    "refresh_entities_with_transform": "Populates the entity buffer with all entities that have a Transform.",
    "get_entity_count": "Returns the number of entities in the current iteration buffer.",
    "get_entity_at": "Returns the entity ID at the given index in the iteration buffer.",
    "get_component_type_id": 'Looks up a component type ID by its display name (e.g. ``"Transform"``).',
    "refresh_entities_with_component": "Populates the entity buffer with all entities owning the given component type.",
    "get_component_field_f": "Reads a float field from a component at the given byte offset.",
    "set_component_field_f": "Writes a float field to a component at the given byte offset.",
    "make_pick_ray": "Casts a ray from the camera through the given screen coordinates.",
    "get_ray_origin_x": "Returns the X origin of the last pick ray.",
    "get_ray_origin_y": "Returns the Y origin of the last pick ray.",
    "get_ray_origin_z": "Returns the Z origin of the last pick ray.",
    "get_ray_dir_x": "Returns the X direction of the last pick ray.",
    "get_ray_dir_y": "Returns the Y direction of the last pick ray.",
    "get_ray_dir_z": "Returns the Z direction of the last pick ray.",
    "ray_plane_intersect": "Intersects the last pick ray with a plane. Returns true if hit.",
    "get_hit_x": "Returns the X coordinate of the last ray-plane intersection.",
    "get_hit_y": "Returns the Y coordinate of the last ray-plane intersection.",
    "get_hit_z": "Returns the Z coordinate of the last ray-plane intersection.",
}

# -- Argument type mapping (C++ -> das) ---------------------------------------

CPP_TO_DAS_TYPE = {
    "uint32_t": "uint",
    "int": "int",
    "float": "float",
    "double": "float",
    "bool": "bool",
    "const char *": "string",
    "const char*": "string",
    "void": "void",
}

# Canonical section order for the generated docs
SECTION_ORDER = [
    "Component accessors",
    "Logging",
    "Entity operations",
    "Generic component queries",
    "Per-component add/remove",
    "Transform operations",
    "Scene operations",
    "Component type ID constants",
    "Event query functions",
    "SDL window operations",
    "Window size",
    "Entity iteration",
    "Entity iteration by component",
    "Component type lookup",
    "Component field access",
    "Raycasting",
]

# Curated docs for the component accessor proxy API.
#
# The accessor externs (get_transform_accessor, ...) are internal C++
# bindings; the user-facing entry points are the world-scoped `get`/`has`
# typed overloads in the `weasel` facade module.
COMPONENT_ACCESSOR_DOCS = """\
Component accessor proxies
--------------------------

Component data is read and written through live proxy values created with
``get_component(entity, Tag)``.  The proxy captures the component pointer
once at call time; every later property access reads or writes the actual
entity component in place.  Example::

    def on_update (entity : uint) {
        let t = get_component(entity, transform())
        t.scale.x = 2.0                    // chained leaf write
        t.scale = float3(1.0, 2.0, 3.0)    // full-struct write
        t.position.y = 3.5
        let sx = t.scale.x                 // chained read
    }

The available tags and their properties:

.. list-table::
   :header-rows: 1

   * - Tag function
     - Proxy type
     - Properties
   * - ``transform()``
     - ``TransformAccessor``
     - ``position.x/y/z``, ``scale.x/y/z``
   * - ``transform_2d()``
     - ``Transform2DAccessor``
     - ``position.x/y``, ``scale.x/y``, ``rotation``
   * - ``camera_2d()``
     - ``Camera2DAccessor``
     - ``zoom``
   * - ``sprite_2d()``
     - ``Sprite2DAccessor``
     - ``color.x/y/z/w``, ``size.x/y``
   * - ``point_light()``
     - ``PointLightAccessor``
     - ``color.x/y/z``, ``intensity``
   * - ``directional_light()``
     - ``DirectionalLightAccessor``
     - ``color.x/y/z``, ``intensity``
   * - ``spot_light()``
     - ``SpotLightAccessor``
     - ``color.x/y/z``, ``intensity``

A proxy stays valid only while the entity is alive and still has the
component.  Re-call ``get_component`` after structural changes to the
entity.
"""


def parse_cpp_function_signatures(source_dir: Path) -> dict[str, tuple[str, str]]:
    """Parse C++ source files to extract function signatures and doc comments.

    Returns dict mapping C++ function name -> (return_type, signature_string).
    """
    signatures = {}
    api_file = source_dir / "wsl" / "das" / "wsl_api_module.cpp"
    if not api_file.exists():
        return signatures

    content = api_file.read_text()

    # Match function definitions: return_type\nfunc_name (args)
    pattern = re.compile(
        r"^(\w[\w:*&<> ]*)\s*\n"  # return type on previous line
        r"(wsl_\w+)\s*\("  # function name
        r"([^)]*)\)",  # arguments
        re.MULTILINE,
    )

    for match in pattern.finditer(content):
        ret_type = match.group(1).strip()
        func_name = match.group(2).strip()
        args_str = match.group(3).strip()

        # Parse individual arguments
        args = []
        if args_str:
            for arg in args_str.split(","):
                arg = arg.strip()
                # Split on last space to get type and name
                parts = arg.rsplit(None, 1)
                if len(parts) == 2:
                    args.append((parts[0].strip(), parts[1].strip()))
                elif len(parts) == 1:
                    args.append(("", parts[0].strip()))

        signatures[func_name] = (ret_type, args)

    return signatures


def parse_binding_registrations(source_dir: Path) -> BindingModule:
    """Parse wsl_api_module.cpp to extract all addExtern binding registrations."""
    module = BindingModule(name="weasel_api")

    api_file = source_dir / "wsl" / "das" / "wsl_api_module.cpp"
    if not api_file.exists():
        print(f"Warning: {api_file} not found", file=sys.stderr)
        return module

    content = api_file.read_text()

    # Extract C++ function signatures for type info
    cpp_sigs = parse_cpp_function_signatures(source_dir)

    # Parse section comments from the wrapper function area.
    # Section comments (// ── Name ──) appear before the wrapper functions,
    # so we map each wrapper function name to its enclosing section.
    wrapper_sections: dict[str, str] = {}  # cpp_func_name -> section_name
    current_section = ""
    section_pattern = re.compile(r"//\s*─+\s*(.+?)\s*─+")

    lines = content.split("\n")
    for line in lines:
        sm = section_pattern.search(line)
        if sm:
            current_section = sm.group(1).strip()
        # Match wrapper function definitions (type on own line, name on next)
        func_start = re.match(r"^(wsl_\w+)\s*\(", line)
        if func_start and current_section:
            wrapper_sections[func_start.group(1)] = current_section

    # Find all addExtern registrations (including two-template-argument
    # forms, e.g. proxy accessors bound with an explicit SimNode type).
    # The module argument is `*this` in Module_WeaselApi but `mod` in the
    # shared register_component_accessors helper.
    extern_pattern = re.compile(
        r"addExtern\s*<\s*DAS_BIND_FUN\s*\(\s*(\w+)\s*\)"
        r"(?:\s*,\s*[^>]+)?\s*>\s*\(\s*(?:\*this|mod)\s*,\s*lib\s*,"
        r"\s*\"(\w+)\"(?:\s*,\s*::das::SideEffects::(\w+))?"
    )

    # Find addInterop registrations (e.g. the event_emit bridge), which use
    # the same (*this, lib, "name", side-effects, ...) call convention.
    interop_pattern = re.compile(
        r"addInterop\s*<\s*(\w+)\s*,[^>]*>\s*\(\s*\*this\s*,\s*lib\s*,"
        r"\s*\"(\w+)\"(?:\s*,\s*::das::SideEffects::(\w+))?"
    )

    # Find addConstant registrations
    constant_pattern = re.compile(
        r"addConstant\s*<\s*(\w[\w<>: ]*)\s*>\s*"
        r'\(\s*\*this\s*,\s*"(\w+)"\s*,\s*(.+?)\s*\)'
    )

    # Map event_kind values to readable names
    EVENT_KIND_MAP = {
        "event_kind::mouse_motion": "2 (mouse_motion)",
        "event_kind::mouse_button_down": "3 (mouse_button_down)",
        "event_kind::mouse_button_up": "4 (mouse_button_up)",
        "event_kind::quit": "0 (quit)",
    }

    for match in sorted(
        list(extern_pattern.finditer(content))
        + list(interop_pattern.finditer(content)),
        key=lambda m: m.start(),
    ):
        cpp_name = match.group(1)
        das_name = match.group(2)
        side_effects = match.group(3) or "none"

        # Look up section from wrapper function mapping
        section = wrapper_sections.get(cpp_name, "")

        # Get description from curated docs
        description = FUNCTION_DOCS.get(das_name, "")

        # Get argument names from the registration
        # Look for ->arg("name") or ->args({"name1", "name2"})
        remaining = content[match.end() : match.end() + 500]
        arg_names = []

        # Match ->arg("name")
        arg_match = re.search(r'->arg\s*\(\s*"(\w+)"\s*\)', remaining)
        if arg_match:
            arg_names.append(arg_match.group(1))

        # Match ->args({"name1", "name2", ...})
        args_match = re.search(r"->args\s*\(\s*\{([^}]+)\}\s*\)", remaining)
        if args_match:
            arg_names = [a.strip().strip('"') for a in args_match.group(1).split(",")]

        # Get C++ signature for type info
        cpp_ret, cpp_args = cpp_sigs.get(cpp_name, ("", []))

        # Build typed arg list
        typed_args = []
        for i, (arg_type, _) in enumerate(cpp_args):
            if i < len(arg_names):
                # Strip const/ref qualifiers for das type
                clean_type = (
                    arg_type.replace("const ", "")
                    .replace(" *", "*")
                    .replace("&", "")
                    .strip()
                )
                das_type = CPP_TO_DAS_TYPE.get(
                    clean_type, CPP_TO_DAS_TYPE.get(arg_type, "var")
                )
                typed_args.append(f"{arg_names[i]} : {das_type}")

        func = BoundFunction(
            cpp_name=cpp_name,
            das_name=das_name,
            args=typed_args,
            return_type=CPP_TO_DAS_TYPE.get(cpp_ret, "void"),
            description=description,
            section=section,
            side_effects=side_effects,
        )
        module.functions.append(func)

    # Find constants
    for match in constant_pattern.finditer(content):
        cpp_type = match.group(1).strip()
        das_name = match.group(2)
        value = match.group(3).strip()

        # Clean up value: resolve event_kind references
        clean_value = value
        for event_ref, readable in EVENT_KIND_MAP.items():
            if event_ref in value:
                clean_value = readable
                break
        # Remove trailing ')' and extra parentheses from static_cast
        while clean_value.endswith(")"):
            clean_value = clean_value[:-1]

        module.constants.append(
            BoundConstant(
                das_name=das_name,
                cpp_type=cpp_type,
                value=clean_value,
            )
        )

    # Also parse let constants from weasel_api.das
    das_file = source_dir / "wsl" / "das" / "modules" / "weasel_api" / "weasel_api.das"
    if das_file.exists():
        das_content = das_file.read_text()
        let_pattern = re.compile(r"let\s+(\w+)\s*:\s*(\w+)\s*=\s*(.+)")
        for match in let_pattern.finditer(das_content):
            das_name = match.group(1)
            # Skip if already added from C++
            if not any(c.das_name == das_name for c in module.constants):
                module.constants.append(
                    BoundConstant(
                        das_name=das_name,
                        cpp_type=match.group(2),
                        value=match.group(3).strip(),
                    )
                )

    return module


def parse_das_stubs(source_dir: Path) -> dict[str, str]:
    """Parse weasel_api.das to extract doc comments for functions."""
    docs = {}
    das_file = source_dir / "wsl" / "das" / "modules" / "weasel_api" / "weasel_api.das"
    if not das_file.exists():
        return docs

    lines = das_file.read_text().split("\n")
    pending_comment = ""

    for line in lines:
        stripped = line.strip()
        if stripped.startswith("//"):
            pending_comment = stripped.lstrip("/ ").strip()
        elif stripped.startswith("def ") or stripped.startswith("let "):
            # Extract name
            parts = stripped.split()
            if len(parts) >= 2:
                name = parts[1].split("(")[0].split(":")[0]
                if pending_comment:
                    docs[name] = pending_comment
            pending_comment = ""
        else:
            if (
                not stripped
                or stripped.startswith("options")
                or stripped.startswith("module")
            ):
                pending_comment = ""

    return docs


def generate_rst(module: BindingModule, output_dir: Path):
    """Generate .rst files for a binding module."""
    output_dir.mkdir(parents=True, exist_ok=True)

    # Group functions by section
    sections: dict[str, list[BoundFunction]] = {}
    for func in module.functions:
        section = func.section or "Other"
        if section not in sections:
            sections[section] = []
        sections[section].append(func)

    # Sort sections by canonical order, unknown sections at the end
    def section_sort_key(name: str) -> int:
        try:
            return SECTION_ORDER.index(name)
        except ValueError:
            return len(SECTION_ORDER)

    sorted_sections = sorted(sections.keys(), key=section_sort_key)

    # Generate weasel_api.rst
    rst_path = output_dir / f"{module.name}.rst"
    with open(rst_path, "w") as f:
        f.write(f"{module.name}\n")
        f.write(f"{'=' * len(module.name)}\n\n")

        if module.description:
            f.write(f"{module.description}\n\n")

        f.write(f".. das:module:: {module.name}\n\n")

        # Write constants
        if module.constants:
            f.write("Constants\n")
            f.write("-" * 10 + "\n\n")
            for const in module.constants:
                f.write(f".. das:data:: {const.das_name}\n\n")
                f.write(f"   :type: {const.cpp_type}\n\n")
                f.write(f"   Value: ``{const.value}``\n\n")

        # Write curated component accessor proxy docs
        f.write(COMPONENT_ACCESSOR_DOCS)
        f.write("\n")

        # Write functions grouped by section
        for section_name in sorted_sections:
            funcs = sections[section_name]
            f.write(f"{section_name}\n")
            f.write("-" * len(section_name) + "\n\n")

            section_desc = SECTION_DOCS.get(section_name, "")
            if section_desc:
                f.write(f"{section_desc}\n\n")

            for func in funcs:
                # Build signature string
                args_str = ", ".join(func.args)
                if func.return_type and func.return_type != "void":
                    sig = f"{func.das_name}({args_str}) : {func.return_type}"
                else:
                    sig = f"{func.das_name}({args_str})"

                f.write(f".. das:function:: {sig}\n\n")

                if func.description:
                    f.write(f"   {func.description}\n\n")

                # Write arguments
                if func.args:
                    f.write("   :param:\n")
                    for arg in func.args:
                        parts = arg.split(" : ", 1)
                        if len(parts) == 2:
                            f.write(f"      {parts[0]} ({parts[1]})\n")
                        else:
                            f.write(f"      {parts[0]}\n")
                    f.write("\n")

                # Write return type
                if func.return_type and func.return_type != "void":
                    f.write(f"   :returns: {func.return_type}\n\n")

    print(f"  Generated {rst_path}")


def generate_components_rst(source_dir: Path, output_dir: Path):
    """Generate component documentation from component_meta registrations."""
    output_dir.mkdir(parents=True, exist_ok=True)

    rst_path = output_dir / "components.rst"
    with open(rst_path, "w") as f:
        f.write("Components\n")
        f.write("==========\n\n")
        f.write(".. das:module:: weasel_components\n\n")

        # Scan component headers for register_meta()
        comp_dir = source_dir / "wsl" / "comp"
        if not comp_dir.exists():
            f.write("No component directory found.\n")
            return

        for header in sorted(comp_dir.glob("*.hpp")):
            content = header.read_text()

            # Skip non-component files
            if "register_meta" not in content:
                continue

            # Extract component name from meta_info
            name_match = re.search(
                r'meta_info\s*\{\s*"([^"]+)"\s*,\s*"([^"]*)"', content
            )
            if not name_match:
                continue

            display_name = name_match.group(1)
            description = name_match.group(2)

            # Extract field names and descriptions
            fields = []
            field_pattern = re.compile(
                r"\.data\s*<\s*&\s*\w+::comp::\w+::(\w+)\s*>\s*"
                r'\s*\(\s*"[^"]*"_hs\s*\)\s*'
                r"\.custom\s*<\s*comp::meta_info\s*>\s*"
                r'\(\s*meta_info\s*\{\s*"([^"]*)"\s*,\s*"([^"]*)"',
                re.MULTILINE,
            )

            for fm in field_pattern.finditer(content):
                field_name = fm.group(1)
                field_display = fm.group(2)
                field_desc = fm.group(3)
                fields.append((field_name, field_display, field_desc))

            # Determine if it's a singleton
            is_singleton = "singleton_component" in content

            # Write RST
            f.write(f".. das:class:: {display_name}\n\n")
            if description:
                f.write(f"   {description}\n\n")

            if is_singleton:
                f.write("   :singleton: yes\n\n")

            if fields:
                f.write("   **Properties:**\n\n")
                for field_name, field_display, field_desc in fields:
                    f.write(f"   - **{field_display}** (``{field_name}``)")
                    if field_desc:
                        f.write(f" -- {field_desc}")
                    f.write("\n")
                f.write("\n")

    print(f"  Generated {rst_path}")


def generate_ecs_rst(output_dir: Path):
    """Generate GameSystem lifecycle documentation.

    The legacy ``weasel_ecs`` module (``EcsSystem`` base) was folded into
    the ``weasel`` facade; game code declares ``class System : GameSystem``.
    This page is kept as a redirect stub so old links keep resolving.
    """
    output_dir.mkdir(parents=True, exist_ok=True)

    rst_path = output_dir / "weasel_ecs.rst"
    with open(rst_path, "w") as f:
        f.write("weasel_ecs\n")
        f.write("==========\n\n")
        f.write(".. das:module:: weasel_ecs\n\n")

        f.write(".. note::\n")
        f.write("   The ``weasel_ecs`` module no longer exists as a gameplay\n")
        f.write("   import. ``EcsSystem`` now lives in the ``weasel`` facade\n")
        f.write("   and game systems extend ``GameSystem`` (``class System :\n")
        f.write("   GameSystem``).\n\n")

        f.write("   See the migration guide for the lifecycle contract\n")
        f.write("   (``on_start`` / ``update`` / ``fixed_update`` /\n")
        f.write("   ``late_update`` / ``on_stop``).\n")

    print(f"  Generated {rst_path}")


def main():
    parser = argparse.ArgumentParser(
        description="Generate Sphinx .rst docs from Weasel daslang bindings"
    )
    parser.add_argument("--source", required=True, help="Path to the src/ directory")
    parser.add_argument(
        "--output", required=True, help="Output directory for .rst files"
    )
    args = parser.parse_args()

    source_dir = Path(args.source)
    output_dir = Path(args.output)

    if not source_dir.exists():
        print(f"Error: source directory {source_dir} not found", file=sys.stderr)
        sys.exit(1)

    print("Generating Weasel API documentation...")

    # Parse bindings from C++ source
    module = parse_binding_registrations(source_dir)
    print(
        f"  Found {len(module.functions)} bound functions, {len(module.constants)} constants"
    )

    # Enrich with das stub comments
    das_docs = parse_das_stubs(source_dir)
    for func in module.functions:
        if func.das_name in das_docs and not func.description:
            func.description = das_docs[func.das_name]

    # Generate RST files
    generate_rst(module, output_dir)
    generate_ecs_rst(output_dir)
    generate_components_rst(source_dir, output_dir)

    print("Documentation generation complete.")


if __name__ == "__main__":
    main()
