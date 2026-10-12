#include "component_info.hpp"
#include "mcp_helpers.hpp"

#include "wsl/comp/components.hpp"
#include "wsl/comp/component_meta.hpp"
#include "wsl/reg/component_registry.hpp"

#include <algorithm>
#include <cctype>
#include <entt/entt.hpp>
#include <sstream>
#include <string>
#include <vector>

namespace wsl::mcp_server
{

// --- lazy initialization ---------------------------------------------------

reg::component_registry &
ensure_registry ()
{
  static reg::component_registry registry;
  static bool initialized = false;
  if (!initialized) {
    initialized = true;
    using namespace wsl::comp;

    register_component_meta<hierarchy, world_transform, transform,
                            model_instance_3d, camera, point_light, spot_light,
                            directional_light, rigid_body, area, character_body,
                            audio, prefab_instance> ();

    for_each_type<component_types>::apply ([]<typename T> () {
      registry.template register_world_component<T> ();
    });
  }
  return registry;
}

// --- helpers ---------------------------------------------------------------

// Behavior notes surfaced by describe_component. Single source of truth for the
// hidden engine rules agents previously had to reverse-engineer (see plan
// Workstream D). Keyed by the component display name.
const std::map<std::string, std::vector<std::string>> &
behavior_notes ()
{
  static const std::map<std::string, std::vector<std::string>> notes = {
    { "Rigid Body",
      { "Ownership model: once created, the physics engine (Box3D) owns the "
        "collision body; the engine copies the simulation result back into "
        "the transform/scale every step.",
        "Dynamic bodies: writing transform.position/rotation/scale directly "
        "mid-simulation is IGNORED. The transform→body reset skips dynamic "
        "bodies (the editor-only force_all flag is the only escape hatch). "
        "To move a dynamic body, apply forces/impulses (apply_force / "
        "apply_impulse) or remove + re-add the rigid_body component to "
        "teleport.",
        "Kinematic and static bodies DO accept transform writes.",
        "Mass is derived from the default density (1000 kg/m^3). A "
        "builtin://sphere (radius 0.5) masses ~524 kg, so apply_force "
        "magnitudes must be in the thousands (≈6500 N yields ≈g lift for "
        "that sphere). Use per-body mass/scale to size your forces.",
        "Scale changes rebuild the collider but, like position writes, skip "
        "dynamic bodies." } },
    { "Model Instance 3D",
      { "Built-in primitive dimensions (constant; not affected by the "
        "entity transform unless you set scale):",
        "  builtin://sphere   — radius 0.5",
        "  builtin://cube     — half-extent 0.5 (1×1×1 box)",
        "  builtin://cylinder — radius 0.5, height 1",
        "  builtin://prism    — triangular prism",
        "  builtin://quad     — 1×1 quad in the XY plane",
        "Reference these via the model_id property (e.g. `comp set <id> "
        "model_instance_3d model_id builtin://cube`)." } },
  };
  return notes;
}

std::string
type_name_str (const entt::meta_type &meta)
{
  if (!meta)
    return "<unregistered>";
  std::string_view name = meta.info ().name ();
  if (!name.empty ())
    return std::string (name);
  return "<unknown>";
}

// Format an enum value: try to get int, fall back to display name
std::string
format_enum_value (const entt::meta_data &data)
{
  // meta_custom stores a value of type const char*,
  // so the safe pointer conversion yields const char**
  std::string label = "<unnamed>";
  if (const char *const *p = data.custom ();
      (p != nullptr) && ((*p) != nullptr)) {
    label = *p;
  }

  entt::meta_any val = data.get ({});
  if (!val)
    return label + " = <?";

  std::string result = label;

  // Show the EnTT hashed-string name used by `comp set` for enum matching
  if (const char *const *p = data.custom ();
      (p != nullptr) && ((*p) != nullptr)) {
    result += " [match: " + std::string (*p) + "]";
  }

  if (val.allow_cast<int> ()) {
    result += " = " + std::to_string (val.cast<int> ());
  }

  return result;
}

// Describe a meta type's data members, recursing into nested types that
// are neither enums nor built-in arithmetic types.
void
describe_fields (std::ostringstream &oss, const entt::meta_type &meta,
                 const std::string &indent, int depth)
{
  if (!meta || depth > 5)
    return;

  for (auto &&[id, data] : meta.data ()) {

    auto field_type = data.type ();

    std::string display_name;
    std::string description;
    if (auto mi = comp::get_meta_info (data)) {
      display_name = mi->display_name;
      description = mi->description;
    }
    const bool named = !display_name.empty ();
    if (!named) {
      display_name = "<unnamed>";
    }

    // Derive the meta property name from the display name convention:
    // lowercase, spaces→underscores, strip trailing punctuation.
    std::string prop_name = display_name;
    for (auto &c : prop_name)
      c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
    for (auto &c : prop_name)
      if (c == ' ')
        c = '_';
    while (!prop_name.empty () && prop_name.back () == '.')
      prop_name.pop_back ();

    oss << indent << "  " << display_name << " [key: " << prop_name << "]";

    if (field_type) {
      oss << " (" << type_name_str (field_type) << ")";
    } else {
      oss << " (<unregistered>)";
    }

    if (!description.empty ()) {
      oss << " - " << description;
    }
    oss << "\n";

    // Show CLI example for "comp set" property name usage. Only meaningful
    // when the field actually has a name -- "<unnamed>" is not a valid
    // property argument.
    if (named) {
      oss << indent << "    CLI: comp set <id> <type> " << prop_name
          << " <value>\n";
    } else {
      oss << indent
          << "    No display name registered, so no usable comp set property "
             "path.\n";
    }

    // Enum type: list possible values
    if (field_type && field_type.is_enum ()) {
      // An enum with no registered names is a known shape (bitmask enums that
      // were never added to entt). Emitting the header with nothing under it
      // reads as "there are no values", which is worse than saying so.
      bool has_names = false;
      for (auto &&[_, __] : field_type.data ()) {
        (void)_;
        (void)__;
        has_names = true;
        break;
      }
      if (has_names) {
        oss << indent
            << "    Values (comp set matches against the bracketed label):\n";
        for (auto &&[ev_id, ev_data] : field_type.data ()) {
          (void)ev_id;
          oss << indent << "      " << format_enum_value (ev_data) << "\n";
        }
      } else {
        oss << indent
            << "    No named values registered; pass the raw integer.\n";
      }
      continue;
    }

    // Nested class type (not arithmetic, not enum): recurse
    if (field_type && field_type.is_class () && !field_type.is_arithmetic ()) {
      bool has_sub = false;
      for (auto &&[_, sub] : field_type.data ()) {
        (void)_;
        (void)sub;
        has_sub = true;
        break;
      }
      if (has_sub) {
        describe_fields (oss, field_type, indent + "    ", depth + 1);
      }
    }
  }
}

// Search for a component by various lookup strategies
const reg::component_registry::descriptor *
find_component (const std::string &name)
{
  const auto &registry = ensure_registry ();
  auto components = registry.get_world_components ();

  // 1. Exact display name match
  for (auto *desc : components) {
    if (desc && desc->display_name == name)
      return desc;
  }

  // 2. Exact type name match
  for (auto *desc : components) {
    if (desc && desc->type_name == name)
      return desc;
  }

  // 3. Case-insensitive display name
  std::string lower_name = name;
  for (auto &c : lower_name)
    c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));

  for (auto *desc : components) {
    if (!desc)
      continue;
    std::string dn_lower = desc->display_name;
    for (auto &c : dn_lower)
      c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
    if (dn_lower == lower_name)
      return desc;
  }

  // 4. Suffix match on type name (e.g. "rigid_body" matches
  // "wsl::comp::rigid_body")
  for (auto *desc : components) {
    if (!desc)
      continue;
    auto pos = desc->type_name.rfind (name);
    if (pos != std::string::npos
        && pos + name.size () == desc->type_name.size ()) {
      return desc;
    }
  }

  return nullptr;
}

// --- handlers --------------------------------------------------------------

agentsdk::result<mcp::tool_result, mcp::mcp_error>
handle_list_components (const std::string &args_json)
{
  (void)args_json;
  const auto &registry = ensure_registry ();
  auto components = registry.get_world_components ();

  std::ostringstream oss;
  oss << "Registered Components (" << components.size () << "):\n\n";

  for (const auto *desc : components) {
    if (!desc)
      continue;
    oss << "  " << desc->display_name << "\n";
    oss << "    Type: " << desc->type_name << "\n";
    oss << "    Type ID: " << desc->type_id << "\n";

    if (desc->can_add_default) {
      oss << "    Can add to entity: yes\n";
    } else {
      oss << "    Can add to entity: no\n";
    }
    oss << "\n";
  }

  return text_result (oss.str ());
}

agentsdk::result<mcp::tool_result, mcp::mcp_error>
handle_describe_component (const std::string &args_json)
{
  std::string name;
  std::string error;
  if (!get_string_arg (args_json, "name", name, error)) {
    return param_error (error);
  }

  const auto *desc = find_component (name);

  if (!desc) {
    return param_error ("Unknown component: " + name);
  }

  std::ostringstream oss;
  oss << "Component: " << desc->display_name << "\n";
  oss << "  Type: " << desc->type_name << "\n";
  oss << "  Type ID: " << desc->type_id << "\n\n";

  // Properties via entt meta reflection
  entt::meta_type meta = entt::resolve (desc->type_id);
  if (!meta) {
    oss << "  (No reflection metadata registered)\n";
    oss << "  NOTE: comp set only works on components with EnTT meta\n";
    oss << "  registration (register_meta() must have been called).\n";
  } else {
    bool has_fields = false;
    for (auto &&[id, data] : meta.data ()) {
      (void)id;
      has_fields = true;
      break;
    }

    if (!has_fields) {
      oss << "  Properties: none exposed via reflection\n";
      oss << "  NOTE: comp set only works on components with EnTT meta\n";
      oss << "  registration (register_meta() must have been called).\n";
    } else {
      oss << "Properties:\n";
      oss << "  NOTE: comp set uses the bracketed hash IDs as property "
             "names,\n";
      oss << "  NOT the display names. Example: use motion_type, not Motion "
             "Type.\n";
      oss << "  Scene serialization uses yet another set of names (e.g. "
             "serializes\n";
      oss << "  as 'motion_type' in JSON, but meta property is "
             "'motion_type').\n\n";
      describe_fields (oss, meta, "", 0);
    }
  }

  // Behavior notes (Workstream D): hidden engine rules made first-class.
  const auto &notes = behavior_notes ();
  auto it = notes.find (desc->display_name);
  if (it != notes.end () && !it->second.empty ()) {
    oss << "\nBehavior notes:\n";
    for (const std::string &note : it->second) {
      oss << "  - " << note << "\n";
    }
  }

  return text_result (oss.str ());
}

} // namespace wsl::mcp_server
