#define GLM_ENABLE_EXPERIMENTAL
#include "repl_handler.hpp"
#include "wsl/log/log.hpp"
#include "wsl/das/das_engine.hpp"
#include <cstdint>

#include "comp/area3d.hpp"
#include "comp/camera.hpp"
#include "comp/character_body.hpp"
#include "comp/component_meta.hpp"
#include "comp/directional_light.hpp"
#include "comp/hierarchy.hpp"
#include "comp/model_instance_3d.hpp"
#include "comp/point_light.hpp"
#include "comp/rigid_body.hpp"
#include "comp/world_transform.hpp"
#include "comp/singl/physics_manager.hpp"
#include "comp/spot_light.hpp"
#include "comp/transform.hpp"
#include "math/vector.hpp"
#include "reg/component_registry.hpp"
#include "reg/singleton_registry.hpp"
#include "reg/system_factory_registry.hpp"
#include "rsc/resource_ids.hpp"
#include "rsc/resource_manager.hpp"
#include "rsc/project_loader.hpp"
#include "sys/core_systems.hpp"
#include "wsl/rsc/project_loader.hpp"
#include "wsl/rsc/project.hpp"
#include "wsl/rsc/scene.hpp"
#include "wsl/rsc/scene_snapshot_serializer.hpp"
#include "wsl/comp/singl/runtime_context.hpp"
#include "wsl/comp/singl/editor_context.hpp"
#include "wsl/comp/singl/ui_manager.hpp"
#include "wsl/sys/system.hpp"
#include "wsl/comp/components.hpp"
#include "wsl/das/das_api_catalog.gen.hpp"

#include "wsl/serialize/serialize.hpp"
#include <entt/entt.hpp>
#include <nlohmann/json.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>
#include <thread>
#include <algorithm>
#include <cctype>
#include <iterator>
#include <filesystem>
#include <fstream>

using namespace entt::literals;
using namespace wsl::rsc;

namespace wsl::cli
{

namespace
{

void
register_repl_types (wsl::comp::singl::runtime_context &rtc)
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

template <typename Descriptor>
void
write_registered_entry (std::ostringstream &output,
                        const Descriptor &descriptor)
{
  output << " - " << descriptor.display_name;
  if (!descriptor.type_name.empty ()
      && descriptor.type_name != descriptor.display_name) {
    output << " [" << descriptor.type_name << "]";
  }
  if (descriptor.runtime_registered) {
    output << " [runtime]";
  }
  output << "\n";
}

// Find the closest registered system name to a (possibly mis-typed) query,
// using Levenshtein distance, for self-diagnosing `sys add` errors.
static std::string
suggest_system_name (const std::string &query,
                     const wsl::reg::system_factory_registry &registry)
{
  auto levenshtein = [] (const std::string &a, const std::string &b) -> int {
    const std::size_t n = a.size (), m = b.size ();
    std::vector<std::vector<int>> d (n + 1, std::vector<int> (m + 1, 0));
    for (std::size_t i = 0; i <= n; ++i)
      d[i][0] = static_cast<int> (i);
    for (std::size_t j = 0; j <= m; ++j)
      d[0][j] = static_cast<int> (j);
    for (std::size_t i = 1; i <= n; ++i) {
      for (std::size_t j = 1; j <= m; ++j) {
        const int cost = a[i - 1] == b[j - 1] ? 0 : 1;
        d[i][j] = std::min (
            { d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + cost });
      }
    }
    return d[n][m];
  };

  auto to_lower_str = [] (const std::string &s) {
    std::string out = s;
    for (auto &c : out)
      c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
    return out;
  };

  const std::string qlow = to_lower_str (query);
  std::string best;
  int best_dist = std::numeric_limits<int>::max ();
  for (const auto *system : registry.get_systems ()) {
    if (!system)
      continue;
    const int dist = levenshtein (qlow, to_lower_str (system->display_name));
    // Only suggest reasonably close names.
    if (dist <= static_cast<int> (system->display_name.size () / 2) + 2
        && dist < best_dist) {
      best_dist = dist;
      best = system->display_name;
    }
  }
  return best;
}

std::string
quote_repl_arg (std::string_view arg)
{
  const bool needs_quotes
      = arg.empty ()
        || std::any_of (arg.begin (), arg.end (), [] (unsigned char ch) {
             return std::isspace (ch) || ch == '"' || ch == '\'';
           });
  if (!needs_quotes) {
    return std::string (arg);
  }

  std::string out;
  out.reserve (arg.size () + 2);
  out.push_back ('"');
  for (char ch : arg) {
    if (ch == '"' || ch == '\\') {
      out.push_back ('\\');
    }
    out.push_back (ch);
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

bool
command_failed (std::string_view output)
{
  return output.find ("Failed") != std::string_view::npos
         || output.find ("Error:") != std::string_view::npos;
}

// Set a meta_any value from a parsed nlohmann::json value.
// Modifies the field in-place via its void pointer.
// The optional resource_manager is used to resolve resource path strings
// (e.g. "builtin://sphere") into typed resource handles such as model_id.
// Find meta_data by hashed name or by lowercased display name.
entt::meta_data
find_meta_data (const entt::meta_type &meta, const std::string &name)
{
  entt::id_type hash = entt::hashed_string::value (name.c_str (), name.size ());
  auto data = meta.data (hash);
  if (data)
    return data;

  // Fallback: match by lowercased display name
  std::string lower;
  for (unsigned char c : name)
    lower.push_back (static_cast<char> (std::tolower (c)));
  for (auto [fid, fd] : meta.data ()) {
    (void)fid;
    auto mi = wsl::comp::get_meta_info (fd);
    if (mi) {
      std::string dl;
      for (unsigned char c : mi->display_name)
        dl.push_back (static_cast<char> (std::tolower (c)));
      if (dl == lower)
        return fd;
    }
  }
  return {};
}

bool
set_meta_from_json (entt::meta_any &field_inst, const nlohmann::json &j,
                    wsl::rsc::resource_manager *res_mgr = nullptr)
{
  auto meta = field_inst.type ();
  if (!meta)
    return false;
  void *ptr = const_cast<void *> (field_inst.base ().data ());
  if (!ptr)
    return false;

  auto type_id = meta.alias ();

  // ── model_id: accept a string resource path ──
  if (type_id == entt::type_hash<wsl::rsc::model_id>::value ()) {
    if (j.is_string () && res_mgr) {
      std::string path = j.get<std::string> ();
      *static_cast<wsl::rsc::model_id *> (ptr) = res_mgr->register_model (path);
      return true;
    }
    return false;
  }

  // Arithmetic types
  if (meta.is_arithmetic ()) {
    if (type_id == entt::type_hash<float>::value ()) {
      *static_cast<float *> (ptr) = j.get<float> ();
      return true;
    }
    if (type_id == entt::type_hash<int>::value ()) {
      *static_cast<int *> (ptr) = j.get<int> ();
      return true;
    }
    if (type_id == entt::type_hash<bool>::value ()) {
      *static_cast<bool *> (ptr) = j.get<bool> ();
      return true;
    }
    if (type_id == entt::type_hash<uint32_t>::value ()) {
      *static_cast<uint32_t *> (ptr) = j.get<uint32_t> ();
      return true;
    }
    if (type_id == entt::type_hash<double>::value ()) {
      *static_cast<double *> (ptr) = j.get<double> ();
      return true;
    }
    return false;
  }

  // Enum types
  if (meta.is_enum ()) {
    if (j.is_string ()) {
      std::string name = j.get<std::string> ();
      for (auto [ev_id, ev_data] : meta.data ()) {
        (void)ev_id;
        if (const char *const *p = ev_data.custom (); p && *p) {
          if (name == *p) {
            entt::meta_any enum_val = ev_data.get ({});
            if (enum_val && enum_val.allow_cast<int> ()) {
              std::memcpy (ptr, enum_val.base ().data (), meta.size_of ());
              return true;
            }
          }
        }
      }
      return false;
    }
    if (j.is_number_integer ()) {
      int val = j.get<int> ();
      std::memcpy (ptr, &val, meta.size_of ());
      return true;
    }
    return false;
  }

  // String type
  if (type_id == entt::type_hash<std::string>::value ()) {
    *static_cast<std::string *> (ptr) = j.get<std::string> ();
    return true;
  }

  // Compound type with registered sub-fields
  if (meta.is_class ()) {
    if (j.is_array ()) {
      size_t idx = 0;
      for (auto [fid, fdata] : meta.data ()) {
        (void)fid;
        if (idx >= j.size ())
          break;
        entt::meta_any sub_inst = fdata.get (field_inst);
        if (sub_inst && set_meta_from_json (sub_inst, j[idx], res_mgr)) {
          fdata.set (field_inst, sub_inst);
        }
        ++idx;
      }
      return true;
    }
    if (j.is_object ()) {
      for (auto [fid, fdata] : meta.data ()) {
        (void)fid;
        auto mi = wsl::comp::get_meta_info (fdata);
        std::string fn = mi ? mi->display_name : "";
        if (fn.empty ())
          continue;
        std::string key;
        for (char c : fn)
          key.push_back (static_cast<char> (
              std::tolower (static_cast<unsigned char> (c))));
        if (j.contains (key)) {
          entt::meta_any sub_inst = fdata.get (field_inst);
          if (sub_inst && set_meta_from_json (sub_inst, j[key], res_mgr)) {
            fdata.set (field_inst, sub_inst);
          }
        }
      }
      return true;
    }
    // Single value fallback: try each sub-field
    if (!j.is_object () && !j.is_array ()) {
      for (auto [fid, fdata] : meta.data ()) {
        (void)fid;
        entt::meta_any sub_inst = fdata.get (field_inst);
        if (sub_inst && sub_inst.type ().is_arithmetic ()) {
          if (set_meta_from_json (sub_inst, j, res_mgr)) {
            fdata.set (field_inst, sub_inst);
            return true;
          }
        }
      }
    }
  }

  return false;
}

// Try to set a component property from a string value.
// Returns true on success and writes a description to out_msg.
// The optional resource_manager is forwarded to set_meta_from_json for
// resolving resource paths (model_id, etc.).
bool
set_component_property (entt::meta_any &instance, entt::meta_data prop_data,
                        const std::string &value_str, std::string &out_msg,
                        wsl::rsc::resource_manager *res_mgr = nullptr)
{
  auto field_type = prop_data.type ();
  if (!field_type) {
    out_msg = "Property has no reflected type";
    return false;
  }

  nlohmann::json j;
  try {
    j = nlohmann::json::parse (value_str);
  } catch (...) {
    j = value_str;
  }

  // Get current field value to modify in-place (always use this path
  // for reliability — avoids potential issues with meta_data::set and
  // from_void instances, and also handles enum types correctly).
  entt::meta_any field_inst = prop_data.get (instance);
  if (!field_inst) {
    out_msg = "Failed to access property value";
    return false;
  }

  // For enum types with string values, build the correct integer via enum
  // data lookup, then inject into the get-modify-set flow.
  if (field_type.is_enum () && j.is_string ()) {
    std::string name = j.get<std::string> ();
    for (auto [ev_id, ev_data] : field_type.data ()) {
      (void)ev_id;
      if (const char *const *p = ev_data.custom (); p && *p && *p == name) {
        entt::meta_any enum_val = ev_data.get ({});
        if (enum_val && enum_val.base ().data ()) {
          void *fp = const_cast<void *> (field_inst.base ().data ());
          if (fp) {
            std::memcpy (fp, enum_val.base ().data (), field_type.size_of ());
            prop_data.set (instance, field_inst);
            out_msg = "set to " + name;
            return true;
          }
        }
      }
    }
  }

  // Auto-descend into single-field wrapper structs (e.g. motion_type_ui →
  // value)
  if (field_type.is_class () && !field_type.is_enum () && j.is_string ()) {
    // Try the parent type first — resource handles (model_id, audio_id, etc.)
    // have their own handlers in set_meta_from_json that should take priority
    // over descending into the wrapper.
    if (set_meta_from_json (field_inst, j, res_mgr)) {
      prop_data.set (instance, field_inst);
      out_msg = "set";
      return true;
    }

    auto data_range = field_type.data ();
    auto it = data_range.begin ();
    if (it != data_range.end ()) {
      auto sub_data = it->second;
      ++it;
      if (it == data_range.end ()) {
        entt::meta_any sub_inst = sub_data.get (field_inst);
        if (sub_inst && set_meta_from_json (sub_inst, j, res_mgr)) {
          sub_data.set (field_inst, sub_inst);
          prop_data.set (instance, field_inst);
          out_msg = "set";
          return true;
        }
      }
    }
  }

  if (set_meta_from_json (field_inst, j, res_mgr)) {
    prop_data.set (instance, field_inst);
    out_msg = "set";
    return true;
  }

  // Fallback: try allow_cast from a basic meta_any
  entt::meta_any val;
  if (j.is_number_float ())
    val = j.get<float> ();
  else if (j.is_number_integer ())
    val = j.get<int> ();
  else if (j.is_boolean ())
    val = j.get<bool> ();
  else
    val = j.get<std::string> ();

  entt::meta_any converted = val.allow_cast (field_type);
  if (converted) {
    prop_data.set (instance, converted);
    out_msg = "set";
    return true;
  }

  out_msg = "Unsupported value type or conversion failed";
  return false;
}

// ── Value formatting for read-back ──

// Read back a daslang component/singleton field from its raw bytes. Daslang
// values have no C++ type to reflect over, so the value has to be decoded from
// the stored block using the field kind recorded at registration. Templated
// because the component and singleton registries declare the field struct
// separately, but identically.
template <typename DasField>
static void
format_das_field_value (std::ostringstream &output, const DasField &field,
                        const uint8_t *field_ptr)
{
  if (field_ptr == nullptr) {
    output << "<?>";
    return;
  }

  switch (field.kind) {
  case wsl::das::das_engine::field_type_kind::boolean: {
    uint8_t v = 0;
    std::memcpy (&v, field_ptr, std::min<int> (field.size, 1));
    output << (v != 0 ? "true" : "false");
    return;
  }
  case wsl::das::das_engine::field_type_kind::integer: {
    if (field.size >= static_cast<int> (sizeof (int64_t))) {
      int64_t v = 0;
      std::memcpy (&v, field_ptr, sizeof (v));
      output << v;
      return;
    }
    if (field.size >= static_cast<int> (sizeof (int32_t))) {
      int32_t v = 0;
      std::memcpy (&v, field_ptr, sizeof (v));
      output << v;
      return;
    }
    if (field.size >= static_cast<int> (sizeof (int16_t))) {
      int16_t v = 0;
      std::memcpy (&v, field_ptr, sizeof (v));
      output << v;
      return;
    }
    int8_t v = 0;
    std::memcpy (&v, field_ptr, 1);
    output << static_cast<int> (v);
    return;
  }
  case wsl::das::das_engine::field_type_kind::unsigned_integer: {
    if (field.size >= static_cast<int> (sizeof (uint64_t))) {
      uint64_t v = 0;
      std::memcpy (&v, field_ptr, sizeof (v));
      output << v;
      return;
    }
    if (field.size >= static_cast<int> (sizeof (uint32_t))) {
      uint32_t v = 0;
      std::memcpy (&v, field_ptr, sizeof (v));
      output << v;
      return;
    }
    if (field.size >= static_cast<int> (sizeof (uint16_t))) {
      uint16_t v = 0;
      std::memcpy (&v, field_ptr, sizeof (v));
      output << v;
      return;
    }
    uint8_t v = 0;
    std::memcpy (&v, field_ptr, 1);
    output << static_cast<unsigned> (v);
    return;
  }
  case wsl::das::das_engine::field_type_kind::floating: {
    if (field.size >= static_cast<int> (sizeof (double))) {
      double v = 0.0;
      std::memcpy (&v, field_ptr, sizeof (v));
      output << v;
      return;
    }
    float v = 0.0F;
    std::memcpy (&v, field_ptr, sizeof (v));
    output << v;
    return;
  }
  case wsl::das::das_engine::field_type_kind::floating_vector: {
    output << '[';
    const std::size_t count = field.size / sizeof (float);
    for (std::size_t i = 0; i < count; ++i) {
      float lane = 0.0F;
      std::memcpy (&lane, field_ptr + i * sizeof (lane), sizeof (lane));
      if (i != 0) {
        output << ", ";
      }
      output << lane;
    }
    output << ']';
    return;
  }
  case wsl::das::das_engine::field_type_kind::integer_vector: {
    output << '[';
    const std::size_t count = field.size / sizeof (std::int32_t);
    for (std::size_t i = 0; i < count; ++i) {
      std::int32_t lane = 0;
      std::memcpy (&lane, field_ptr + i * sizeof (lane), sizeof (lane));
      if (i != 0) {
        output << ", ";
      }
      output << lane;
    }
    output << ']';
    return;
  }
  case wsl::das::das_engine::field_type_kind::string: {
    // The in-memory layout of a daslang string (pointer plus length, or a
    // header block) is not exposed through the field metadata, so do not guess
    // at it -- decoding the wrong offset would print garbage.
    output << "(string: " << field.type_name << ")";
    return;
  }
  case wsl::das::das_engine::field_type_kind::unsupported:
    break;
  }

  output << "(unsupported type: " << field.type_name << ")";
}

// Write a daslang component/singleton field from a CLI value string. The value
// is parsed as JSON first so numbers, booleans and quoted strings all work;
// anything else is a hard error rather than a silent no-op.

// Whether `value` is representable in a signed field of `size` bytes.
static bool
value_fits_signed (int64_t value, int size)
{
  switch (size) {
  case 1:
    return value >= -128 && value <= 127;
  case 2:
    return value >= -32768 && value <= 32767;
  case 4:
    return value >= -2147483648LL && value <= 2147483647LL;
  default:
    return true;
  }
}

// Whether `value` is representable in an unsigned field of `size` bytes.
static bool
value_fits_unsigned (uint64_t value, int size)
{
  switch (size) {
  case 1:
    return value <= 255ULL;
  case 2:
    return value <= 65535ULL;
  case 4:
    return value <= 4294967295ULL;
  default:
    return true;
  }
}

template <typename DasField>
static bool
set_das_field_value (uint8_t *field_ptr, const DasField &field,
                     const std::string &value_str, std::string &out_msg)
{
  if (field_ptr == nullptr) {
    out_msg = "no storage";
    return false;
  }

  nlohmann::json j;
  try {
    j = nlohmann::json::parse (value_str);
  } catch (...) {
    j = value_str;
  }

  switch (field.kind) {
  case wsl::das::das_engine::field_type_kind::boolean: {
    if (!j.is_boolean ()) {
      out_msg = "expected true or false";
      return false;
    }
    const uint8_t v = j.get<bool> () ? 1 : 0;
    std::memcpy (field_ptr, &v, 1);
    out_msg = j.get<bool> () ? "true" : "false";
    return true;
  }
  case wsl::das::das_engine::field_type_kind::integer: {
    if (!j.is_number_integer ()) {
      out_msg = "expected an integer";
      return false;
    }
    const int64_t v = j.get<int64_t> ();
    // Reject rather than truncate: silently keeping the low bytes of an
    // out-of-range value is how "set" ends up lying.
    if (!value_fits_signed (v, field.size)) {
      out_msg = "value does not fit the " + std::to_string (field.size)
                + "-byte field";
      return false;
    }
    std::memcpy (field_ptr, &v, std::min<std::size_t> (field.size, sizeof (v)));
    out_msg = std::to_string (v);
    return true;
  }
  case wsl::das::das_engine::field_type_kind::unsigned_integer: {
    if (!j.is_number_unsigned ()) {
      out_msg = "expected a non-negative integer";
      return false;
    }
    const uint64_t v = j.get<uint64_t> ();
    if (!value_fits_unsigned (v, field.size)) {
      out_msg = "value does not fit the " + std::to_string (field.size)
                + "-byte field";
      return false;
    }
    std::memcpy (field_ptr, &v, std::min<std::size_t> (field.size, sizeof (v)));
    out_msg = std::to_string (v);
    return true;
  }
  case wsl::das::das_engine::field_type_kind::floating: {
    if (!j.is_number ()) {
      out_msg = "expected a number";
      return false;
    }
    const double wide = j.get<double> ();
    if (field.size >= static_cast<int> (sizeof (double))) {
      std::memcpy (field_ptr, &wide, sizeof (wide));
    } else {
      const float narrow = static_cast<float> (wide);
      std::memcpy (field_ptr, &narrow, sizeof (narrow));
    }
    out_msg = std::to_string (wide);
    return true;
  }
  case wsl::das::das_engine::field_type_kind::floating_vector: {
    const std::size_t count = field.size / sizeof (float);
    if (!j.is_array () || count == 0 || j.size () != count) {
      out_msg = "expected an array with " + std::to_string (count)
                + " numeric lanes";
      return false;
    }
    std::vector<float> lanes;
    lanes.reserve (count);
    for (const auto &lane : j) {
      if (!lane.is_number ()) {
        out_msg = "expected an array of numbers";
        return false;
      }
      const double value = lane.get<double> ();
      if (!std::isfinite (value)
          || std::abs (value) > std::numeric_limits<float>::max ()) {
        out_msg = "vector lane is outside the float range";
        return false;
      }
      lanes.push_back (static_cast<float> (value));
    }
    std::memcpy (field_ptr, lanes.data (), field.size);
    out_msg = j.dump ();
    return true;
  }
  case wsl::das::das_engine::field_type_kind::integer_vector: {
    const std::size_t count = field.size / sizeof (std::int32_t);
    if (!j.is_array () || count == 0 || j.size () != count) {
      out_msg = "expected an array with " + std::to_string (count)
                + " integer lanes";
      return false;
    }
    std::vector<std::int32_t> lanes;
    lanes.reserve (count);
    for (const auto &lane : j) {
      if (lane.is_number_unsigned ()) {
        const auto value = lane.get<std::uint64_t> ();
        if (value > static_cast<std::uint64_t> (
                        std::numeric_limits<std::int32_t>::max ())) {
          out_msg = "vector lane is outside the int32 range";
          return false;
        }
        lanes.push_back (static_cast<std::int32_t> (value));
      } else if (lane.is_number_integer ()) {
        const auto value = lane.get<std::int64_t> ();
        if (value < std::numeric_limits<std::int32_t>::min ()
            || value > std::numeric_limits<std::int32_t>::max ()) {
          out_msg = "vector lane is outside the int32 range";
          return false;
        }
        lanes.push_back (static_cast<std::int32_t> (value));
      } else {
        out_msg = "expected an array of integers";
        return false;
      }
    }
    std::memcpy (field_ptr, lanes.data (), field.size);
    out_msg = j.dump ();
    return true;
  }
  case wsl::das::das_engine::field_type_kind::string:
    out_msg = "string fields cannot be set from the CLI (the daslang string "
              "layout is not exposed)";
    return false;
  case wsl::das::das_engine::field_type_kind::unsupported:
    break;
  }

  out_msg = "unsupported field type: " + field.type_name;
  return false;
}

void
format_meta_value (std::ostringstream &output, const entt::meta_any &field_inst,
                   const std::string &indent,
                   const rsc::resource_manager *res_mgr = nullptr)
{
  auto type = field_inst.type ();
  if (!type) {
    output << "<?>";
    return;
  }
  const void *ptr = field_inst.base ().data ();
  if (!ptr) {
    output << "<?>";
    return;
  }

  auto type_id = type.alias ();

  // arithmetic
  if (type.is_arithmetic ()) {
    if (type_id == entt::type_hash<float>::value ())
      output << *static_cast<const float *> (ptr);
    else if (type_id == entt::type_hash<double>::value ())
      output << *static_cast<const double *> (ptr);
    else if (type_id == entt::type_hash<bool>::value ())
      output << (*static_cast<const bool *> (ptr) ? "true" : "false");
    else if (type_id == entt::type_hash<int>::value ())
      output << *static_cast<const int *> (ptr);
    else if (type_id == entt::type_hash<unsigned int>::value ())
      output << *static_cast<const unsigned int *> (ptr);
    else if (type_id == entt::type_hash<int16_t>::value ())
      output << *static_cast<const int16_t *> (ptr);
    else if (type_id == entt::type_hash<uint16_t>::value ())
      output << *static_cast<const uint16_t *> (ptr);
    else if (type_id == entt::type_hash<int32_t>::value ())
      output << *static_cast<const int32_t *> (ptr);
    else if (type_id == entt::type_hash<uint32_t>::value ())
      output << *static_cast<const uint32_t *> (ptr);
    else if (type_id == entt::type_hash<int64_t>::value ())
      output << *static_cast<const int64_t *> (ptr);
    else if (type_id == entt::type_hash<uint64_t>::value ())
      output << *static_cast<const uint64_t *> (ptr);
    else if (type_id == entt::type_hash<uint8_t>::value ())
      output << static_cast<unsigned int> (*static_cast<const uint8_t *> (ptr));
    else if (type_id == entt::type_hash<int8_t>::value ())
      output << static_cast<int> (*static_cast<const int8_t *> (ptr));
    else
      output << "<?>";
    return;
  }

  // string
  if (type_id == entt::type_hash<std::string>::value ()) {
    output << "\"" << *static_cast<const std::string *> (ptr) << "\"";
    return;
  }

  // model_id
  if (type_id == entt::type_hash<wsl::rsc::model_id>::value ()) {
    auto id = *static_cast<const wsl::rsc::model_id *> (ptr);
    if (res_mgr) {
      auto info = res_mgr->info (id);
      if (info)
        output << info->path;
      else
        output << "model#" << id.value;
    } else {
      output << "model#" << id.value;
    }
    return;
  }

  // enum
  if (type.is_enum ()) {
    for (auto [ev_id, ev_data] : type.data ()) {
      (void)ev_id;
      entt::meta_any ev = ev_data.get ({});
      if (ev && ev.base ().data ()
          && std::memcmp (ev.base ().data (), ptr, type.size_of ()) == 0) {
        if (const char *const *p = ev_data.custom (); p && *p) {
          output << *p;
          return;
        }
      }
    }
    // fallback: show integer
    int iv = 0;
    std::memcpy (&iv, ptr, (std::min)(type.size_of (), sizeof (int)));
    output << iv;
    return;
  }

  // compound with sub-fields
  if (type.is_class ()) {
    auto data_range = type.data ();
    auto data_begin = data_range.begin ();
    auto data_end = data_range.end ();
    if (data_begin != data_end) {
      // Flatten wrapper types: if a single unnamed sub-field, show value
      // directly
      auto next = data_begin;
      ++next;
      if (next == data_end) {
        auto &fdata = data_begin->second;
        auto mi = wsl::comp::get_meta_info (fdata);
        if (!mi) {
          entt::meta_any sub = fdata.get (field_inst);
          if (sub) {
            format_meta_value (output, sub, indent, res_mgr);
            return;
          }
        }
      }
      output << "{";
      for (auto [fid, fdata] : type.data ()) {
        (void)fid;
        auto mi = wsl::comp::get_meta_info (fdata);
        std::string dn = mi ? mi->display_name : "?";
        entt::meta_any sub = fdata.get (field_inst);
        output << "\n" << indent << "  " << dn << " = ";
        if (sub)
          format_meta_value (output, sub, indent + "  ", res_mgr);
        else
          output << "<?>";
      }
      output << "\n" << indent << "}";
      return;
    }
  }

  output << "<?>";
}

void
format_component_properties (std::ostringstream &output,
                             const entt::meta_type &meta, void *instance,
                             const std::string &indent,
                             const rsc::resource_manager *res_mgr = nullptr)
{
  entt::meta_any comp_any = meta.from_void (instance);
  if (!comp_any)
    return;
  bool first = true;
  for (auto [fid, fdata] : meta.data ()) {
    (void)fid;
    if (!first)
      output << "\n";
    first = false;
    auto mi = wsl::comp::get_meta_info (fdata);
    std::string dn = mi ? mi->display_name : "?";
    entt::meta_any sub = fdata.get (comp_any);
    output << indent << "  " << dn << " = ";
    if (sub)
      format_meta_value (output, sub, indent + "  ", res_mgr);
    else
      output << "<?>";
  }
  if (!first)
    output << "\n";
}

void
format_component_info (std::ostringstream &output, const entt::meta_type &meta,
                       const std::string &indent)
{
  output << indent;
  // Component title with display name and C++ type
  auto comp_mi = wsl::comp::get_meta_info (meta);
  if (comp_mi && !comp_mi->display_name.empty ())
    output << comp_mi->display_name;
  else
    output << meta.info ().name ();
  output << " [" << meta.info ().name () << "]";

  if (comp_mi && !comp_mi->description.empty ())
    output << "\n" << indent << "  Description: " << comp_mi->description;

  output << "\n" << indent << "  Fields:";

  bool any_field = false;
  for (auto [fid, fdata] : meta.data ()) {
    (void)fid;
    any_field = true;
    auto mi = wsl::comp::get_meta_info (fdata);
    std::string dn = mi ? mi->display_name : "?";
    output << "\n" << indent << "    - " << dn;

    if (fdata.type ()) {
      std::string_view type_name = fdata.type ().info ().name ();
      // Strip "struct " / "class " prefix if present
      if (type_name.starts_with ("struct "))
        type_name.remove_prefix (7);
      else if (type_name.starts_with ("class "))
        type_name.remove_prefix (6);
      output << " [" << type_name << "]";
    }

    if (mi && !mi->description.empty ())
      output << "\n" << indent << "        " << mi->description;
  }

  if (!any_field)
    output << "\n" << indent << "    (none)";

  output << "\n";
}

} // namespace

namespace
{

// ── name conversion helpers (mirrors editor/file_list.cpp) ──

std::vector<std::string>
tokenize_name (std::string_view raw_name)
{
  std::vector<std::string> tokens;
  std::string current;
  char previous = '\0';

  auto flush = [&] () {
    if (!current.empty ()) {
      tokens.push_back (current);
      current.clear ();
    }
  };

  for (char const ch : raw_name) {
    const unsigned char uch = static_cast<unsigned char> (ch);
    if (std::isalnum (uch) == 0) {
      flush ();
      previous = '\0';
      continue;
    }

    if ((std::isupper (uch) != 0) && !current.empty ()
        && (std::islower (static_cast<unsigned char> (previous)) != 0)) {
      flush ();
    }

    current.push_back ((char)std::tolower (uch));
    previous = ch;
  }

  flush ();
  return tokens;
}

std::string
to_snake_case (const std::vector<std::string> &tokens)
{
  std::string out;
  for (std::size_t i = 0; i < tokens.size (); ++i) {
    if (i != 0)
      out += '_';
    out += tokens[i];
  }
  return out;
}

std::string
to_pascal_case (const std::vector<std::string> &tokens)
{
  std::string out;
  for (const std::string &token : tokens) {
    if (token.empty ())
      continue;
    std::string piece = token;
    piece[0] = (char)std::toupper (static_cast<unsigned char> (piece[0]));
    out += piece;
  }
  return out;
}

std::string
to_title_case (const std::vector<std::string> &tokens)
{
  std::string out;
  for (std::size_t i = 0; i < tokens.size (); ++i) {
    if (i != 0)
      out += ' ';
    std::string piece = tokens[i];
    if (!piece.empty ())
      piece[0] = (char)std::toupper (static_cast<unsigned char> (piece[0]));
    out += piece;
  }
  return out;
}

bool
write_text_file (const std::filesystem::path &path, std::string_view text)
{
  std::ofstream output (path, std::ios::binary | std::ios::trunc);
  if (!output)
    return false;
  output.write (text.data (), (std::streamsize)text.size ());
  return output.good ();
}

} // namespace

// -------- command_executor implementation --------

command_executor::command_executor (wsl::comp::singl::runtime_context &rtc)
    : m_rtc (rtc)
{
}

void
command_executor::set_current_project (std::shared_ptr<wsl::rsc::project> proj)
{
  m_current_project = std::move (proj);
}

bool
command_executor::ensure_runtime (runtime_load_mode mode)
{
  auto &module = m_rtc.runtime_project_module ();
  using wsl::reg::runtime::load_state_t;

  if (!m_current_project)
    return true;

  if (module.get_load_state () != load_state_t::unloaded) {
    if (mode != runtime_load_mode::force_full) {
      return module.get_load_state () == load_state_t::loaded;
    }
    // force_full asks for a real compile.  Bail out of the metadata-cache state
    // so the slow path below actually runs; otherwise commands like
    // `comp add` silently keep running with placeholder-only metadata.
    if (module.get_load_state () == load_state_t::loaded) {
      return true;
    }
    module.clear_cached_metadata ();
  }

  // Only projects with runtime source directories can have a module.
  std::filesystem::path const root (m_current_project->root_path);
  bool has_runtime = false;
  for (auto const &sub :
       { m_current_project->systems_path, m_current_project->components_path,
         m_current_project->singletons_path }) {
    if (std::filesystem::exists (root / sub)) {
      has_runtime = true;
      break;
    }
  }
  if (!has_runtime)
    return true;

  wsl::das::das_engine::initialize_global ();

  // Fast path: restore registration names from the metadata cache.
  // load_cached_metadata refuses empty/stale caches, so this escalates to a
  // full compile automatically whenever the cache cannot be trusted.
  if (mode == runtime_load_mode::auto_mode
      && module.load_cached_metadata (*m_current_project)) {
    wsl::log::cli ()->info ("Runtime registry source: metadata cache.");
    m_output << "Registry source: metadata cache.\n";
    return true;
  }

  // Slow path: compile the runtime sources.
  m_output << "Compiling and loading runtime module...\n";
  if (!module.compile_and_load (*m_current_project)) {
    m_output << "Warning: runtime module compilation failed:\n"
             << "  " << module.last_error () << "\n"
             << "  Run 'script status' for details. Continuing without "
                "user-defined types.\n";
    return false;
  }
  if (!module.finalize_load ()) {
    m_output << "Runtime module registration failed:\n  "
             << module.last_error () << "\n";
    return false;
  }
  m_output << "Registry source: full compile.\n";
  wsl::log::cli ()->info ("Runtime module loaded successfully.");
  return true;
}

void
command_executor::auto_save_scene (bool verbose)
{
  if (!m_auto_save)
    return;

  auto *scene = get_active_scene ();
  if (!scene) {
    if (verbose)
      m_output << "Auto-save skipped: no active scene.\n";
    return;
  }
  if (!m_current_project) {
    if (verbose)
      m_output << "Auto-save skipped: no project loaded.\n";
    return;
  }

  std::string save_path;

  // Prefer the original source path when the scene was loaded from a file,
  // so auto-save overwrites the original file rather than creating a new one
  // derived from the scene display name.
  if (!m_active_scene_source_path.empty ()) {
    save_path = m_active_scene_source_path;
  } else {
    std::filesystem::path scenes_dir (m_current_project->root_path);
    scenes_dir /= m_current_project->scenes_path;
    std::string sname = rsc::scene_file::strip_extension (
        std::filesystem::path (scene->get_name ()).filename ().string ());
    save_path
        = (scenes_dir / rsc::scene_file::with_extension (sname)).string ();
  }

  std::error_code ec;
  std::filesystem::path parent_dir
      = std::filesystem::path (save_path).parent_path ();
  std::filesystem::create_directories (parent_dir, ec);

  wsl::rsc::io::scene_snapshot_serializer serializer (&m_rtc, *scene);
  if (serializer.save_json (save_path)) {
    if (verbose)
      m_output << "Auto-saved scene to " << save_path << "\n";
  } else {
    m_output << "Auto-save failed for scene to " << save_path << "\n";
  }
}

void
command_executor::auto_save_project ()
{
  if (!m_current_project) {
    m_output << "Auto-save skipped: no project loaded.\n";
    return;
  }

  std::filesystem::path manifest
      = std::filesystem::path (m_current_project->root_path)
        / wsl::rsc::project_loader::manifest_file;
  std::ofstream file (manifest);
  if (!file) {
    m_output << "Auto-save failed for project.\n";
    return;
  }
  file << serialize::json_write (*m_current_project);
}

std::string
command_executor::execute (const std::string &line)
{
  m_output.str ("");
  m_output.clear ();

  // Strip inline comments: remove everything from the first unquoted '#'.
  std::string cleaned;
  cleaned.reserve (line.size ());
  bool in_sq = false;
  bool in_dq = false;
  for (char ch : line) {
    if (ch == '\'' && !in_dq) {
      in_sq = !in_sq;
      cleaned.push_back (ch);
      continue;
    }
    if (ch == '"' && !in_sq) {
      in_dq = !in_dq;
      cleaned.push_back (ch);
      continue;
    }
    if (ch == '#' && !in_sq && !in_dq)
      break; // strip rest of line
    cleaned.push_back (ch);
  }

  auto tokens = tokenize (cleaned);
  if (tokens.empty ())
    return "";

  const std::string &family = tokens[0];
  bool handled = false;
  if (family == "proj") {
    cmd_proj (tokens);
    handled = true;
  }
  if (!handled && family == "scene") {
    cmd_scene (tokens);
    handled = true;
  }
  if (!handled && family == "ent") {
    cmd_ent (tokens);
    handled = true;
  }
  if (!handled && family == "comp") {
    cmd_comp (tokens);
    handled = true;
  }
  if (!handled && family == "singl") {
    cmd_singl (tokens);
    handled = true;
  }
  if (!handled && family == "sig") {
    cmd_sig (tokens);
    handled = true;
  }
  if (!handled && family == "sys") {
    cmd_sys (tokens);
    handled = true;
  }
  if (!handled && family == "check") {
    cmd_check (tokens);
    handled = true;
  }
  if (!handled && family == "rsc") {
    cmd_rsc (tokens);
    handled = true;
  }
  if (!handled && family == "prefab") {
    cmd_prefab (tokens);
    handled = true;
  }
  if (!handled && family == "script") {
    cmd_script (tokens);
    handled = true;
  }
  if (!handled && family == "das") {
    cmd_das (tokens);
    handled = true;
  }
  if (!handled && family == "play") {
    cmd_play (tokens);
    handled = true;
  }
  if (!handled && family == "help") {
    cmd_help ();
    handled = true;
  }
  if (!handled && (family == "exit" || family == "quit")) {
    m_output << "exit\n";
    handled = true;
  }
  if (!handled && family == "cls") {
    m_output << "\033[2J\033[1;1H";
    handled = true;
  }
  if (!handled) {
    m_output << "Unknown command family: " << family
             << ". Type 'help' for usage.\n";
  }

  return m_output.str ();
}

std::vector<std::string>
command_executor::tokenize (const std::string &line)
{
  std::vector<std::string> tokens;
  std::string current;
  bool in_single_quotes = false;
  bool in_double_quotes = false;
  bool escape_next = false;

  for (char ch : line) {
    if (escape_next) {
      current.push_back (ch);
      escape_next = false;
      continue;
    }

    if (in_double_quotes && ch == '\\') {
      escape_next = true;
      continue;
    }

    if (!in_double_quotes && ch == '\'') {
      in_single_quotes = !in_single_quotes;
      continue;
    }

    if (!in_single_quotes && ch == '"') {
      in_double_quotes = !in_double_quotes;
      continue;
    }

    if (!in_single_quotes && !in_double_quotes
        && std::isspace (static_cast<unsigned char> (ch))) {
      if (!current.empty ()) {
        tokens.push_back (std::move (current));
        current.clear ();
      }
      continue;
    }

    current.push_back (ch);
  }

  if (escape_next) {
    current.push_back ('\\');
  }

  if (!current.empty ()) {
    tokens.push_back (std::move (current));
  }

  return tokens;
}

wsl::rsc::scene *
command_executor::get_active_scene ()
{
  return m_rtc.scene_manager ().get_active ();
}

entt::entity
command_executor::resolve_entity_token (const std::string &token,
                                        wsl::rsc::scene *scene)
{
  entt::entity e = entt::null;
  try {
    e = (entt::entity)std::stoul (token);
  } catch (const std::exception &) {
    if (scene) {
      for (const auto &kv : scene->get_entity_names ()) {
        if (kv.second == token) {
          e = kv.first;
          break;
        }
      }
    }
  }
  return e;
}

void
command_executor::cmd_proj (const std::vector<std::string> &tokens)
{
  if (tokens.size () < 2) {
    m_output << "Usage: proj <new|load|info|save|set> [args...]\n";
    return;
  }
  const std::string &action = tokens[1];
  if (action == "new") {
    if (tokens.size () < 4) {
      m_output << "Usage: proj new <path> <name>\n";
      return;
    }
    wsl::rsc::project proj;
    proj.name = tokens[3];
    proj.root_path = wsl::rsc::project_path::normalize (tokens[2]);
    proj.systems_path = "src/systems";
    proj.components_path = "src/components";
    proj.singletons_path = "src/singletons";
    proj.scenes_path = "rsc/scenes";
    proj.models_path = "rsc/models";
    proj.images_path = "rsc/textures";
    proj.cubemaps_path = "rsc/textures/cubemaps";
    proj.audio_path = "rsc/audio";
    proj.fonts_path = "rsc/fonts";
    proj.materials_path = "rsc/materials";
    proj.ui_layouts_path = "src/ui";
    proj.shaders_path = "rsc/shaders";

    wsl::rsc::project_loader loader (&m_rtc);
    if (loader.create (proj)) {
      m_current_project = std::make_shared<wsl::rsc::project> (proj);
      m_output << "Project '" << proj.name << "' created and loaded.\n";
    } else
      m_output << "Failed to create project.\n";
  } else if (action == "load") {
    if (tokens.size () < 3) {
      m_output << "Usage: proj load <path>\n";
      return;
    }
    std::filesystem::path load_path (tokens[2]);
    if (std::filesystem::is_directory (load_path)) {
      load_path /= wsl::rsc::project_loader::manifest_file;
    }
    if (!std::filesystem::exists (load_path)) {
      m_output << "Failed to find project manifest: " << load_path.string ()
               << "\n";
      return;
    }
    m_current_project = wsl::rsc::project_loader::load (load_path.string ());
    if (m_current_project) {
      m_output << "Project '" << m_current_project->name << "' loaded.\n";

      // A5: make `proj load` honest — also load the runtime scripts and
      // report the outcome instead of leaving an empty registry silently.
      const bool strict
          = std::find (tokens.begin (), tokens.end (), "--strict-runtime")
            != tokens.end ();
      auto &module = m_rtc.runtime_project_module ();
      ensure_runtime (runtime_load_mode::auto_mode);
      if (module.has_loaded_module ()) {
        m_output << "Runtime: compiled " << module.das_component_count ()
                 << " components / " << module.das_system_count ()
                 << " systems.\n";
      } else {
        m_output << "Runtime: FAILED — see 'script status'; continuing without "
                    "user scripts.\n"
                 << "  Last error: " << module.last_error () << "\n";
        if (strict) {
          m_exit_code = 1;
        }
      }
    } else
      m_output << "Failed to load project.\n";
  } else if (action == "info") {
    if (!m_current_project) {
      m_output << "No project loaded.\n";
      return;
    }
    m_output << "Project: " << m_current_project->name
             << "\n  Author: " << m_current_project->author
             << "\n  Root: " << m_current_project->root_path
             << "\n  Default Scene: " << m_current_project->default_scene_path
             << "\n  Systems: " << m_current_project->systems_path
             << "\n  Components: " << m_current_project->components_path
             << "\n  Singletons: " << m_current_project->singletons_path
             << "\n  Scenes: " << m_current_project->scenes_path
             << "\n  Models: " << m_current_project->models_path
             << "\n  Images: " << m_current_project->images_path
             << "\n  Cubemaps: " << m_current_project->cubemaps_path
             << "\n  Audio: " << m_current_project->audio_path
             << "\n  UI Layouts: " << m_current_project->ui_layouts_path
             << "\n  Fonts: " << m_current_project->fonts_path
             << "\n  Shaders: " << m_current_project->shaders_path << "\n";
  } else if (action == "set") {
    if (tokens.size () < 4) {
      m_output << "Usage: proj set <field> <value>\n";
      return;
    }
    if (!m_current_project) {
      m_output << "No project loaded.\n";
      return;
    }
    const std::string &field = tokens[2];
    const std::string &value = tokens[3];
    bool found = false;
    if (field == "name") {
      m_current_project->name = value;
      found = true;
    }
    if (!found && field == "author") {
      m_current_project->author = value;
      found = true;
    }
    if (!found && field == "default_scene_path") {
      m_current_project->default_scene_path = value;
      found = true;
    }
    if (!found && field == "systems_path") {
      m_current_project->systems_path = value;
      found = true;
    }
    if (!found && field == "components_path") {
      m_current_project->components_path = value;
      found = true;
    }
    if (!found && field == "singletons_path") {
      m_current_project->singletons_path = value;
      found = true;
    }
    if (!found && field == "scenes_path") {
      m_current_project->scenes_path = value;
      found = true;
    }
    if (!found && field == "models_path") {
      m_current_project->models_path = value;
      found = true;
    }
    if (!found && field == "images_path") {
      m_current_project->images_path = value;
      found = true;
    }
    if (!found && field == "cubemaps_path") {
      m_current_project->cubemaps_path = value;
      found = true;
    }
    if (!found && field == "audio_path") {
      m_current_project->audio_path = value;
      found = true;
    }
    if (!found && field == "ui_layouts_path") {
      m_current_project->ui_layouts_path = value;
      found = true;
    }
    if (!found && field == "fonts_path") {
      m_current_project->fonts_path = value;
      found = true;
    }
    if (!found && field == "shaders_path") {
      m_current_project->shaders_path = value;
      found = true;
    }
    if (!found) {
      m_output
          << "Unknown project field: " << field
          << ". Supported: name, author, default_scene_path, systems_path, "
             "components_path, singletons_path, scenes_path, models_path, "
             "images_path, cubemaps_path, audio_path, ui_layouts_path, "
             "fonts_path, shaders_path\n";
    }
    if (found) {
      m_output << "Project field '" << field << "' set.\n";
      auto_save_project ();
    }
  } else if (action == "save") {
    if (!m_current_project) {
      m_output << "No project loaded.\n";
      return;
    }
    auto_save_project ();
    m_output << "Project saved.\n";
    return;
  }

  // ── scene subcommand ──
}

void
command_executor::cmd_scene (const std::vector<std::string> &tokens)
{
  if (tokens.size () < 2) {
    m_output << "Usage: scene <new|load|save|ls|status> [args...]\n";
    return;
  }
  const std::string &action = tokens[1];
  if (action == "new") {
    if (tokens.size () < 3) {
      m_output << "Usage: scene new <name>\n";
      return;
    }
    m_active_scene_source_path.clear ();
    auto &scene = m_rtc.scene_manager ().create_default_scene (tokens[2], true);
    m_output << "Scene '" << tokens[2] << "' created and set as active.\n";
    auto_save_scene ();
  } else if (action == "load") {
    if (tokens.size () < 3) {
      m_output << "Usage: scene load <path>\n";
      return;
    }
    if (!m_current_project) {
      m_output << "Error: Load a project first.\n";
      return;
    }
    std::string load_path = tokens[2];

    // Resolve relative paths against the project root
    if (!std::filesystem::path (load_path).is_absolute ()) {
      std::string rooted
          = (std::filesystem::path (m_current_project->root_path) / load_path)
                .string ();
      if (std::filesystem::exists (rooted)) {
        load_path = rooted;
      }
    }

    // Try to resolve by scene name if the resolved path doesn't exist
    if (!std::filesystem::exists (load_path)) {
      std::filesystem::path scenes_dir (m_current_project->root_path);
      scenes_dir /= m_current_project->scenes_path;
      // Try the exact path, then the same name with the scene extension,
      // then the bare name (which may already carry an extension).
      for (const std::string &candidate :
           { (scenes_dir / rsc::scene_file::with_extension (load_path))
                 .string (),
             (scenes_dir / load_path).string () }) {
        if (std::filesystem::exists (candidate)) {
          load_path = candidate;
          break;
        }
      }
    }

    if (!std::filesystem::exists (load_path)) {
      m_output << "Failed to load scene: file not found '" << tokens[2]
               << "'.\n";
      return;
    }

    // Ensure runtime component types are registered before loading
    ensure_runtime (runtime_load_mode::force_full);

    // Derive the scene name from the path basename (strip the extension)
    std::string scene_name = rsc::scene_file::strip_extension (
        std::filesystem::path (load_path).filename ().string ());

    try {
      auto &scene = m_rtc.scene_manager ().create_scene (scene_name, true);
      wsl::rsc::io::scene_snapshot_serializer serializer (&m_rtc, scene);
      if (serializer.load_json (load_path)) {
        m_output << "Scene loaded from " << load_path << "\n";
        m_active_scene_source_path = load_path;
      } else {
        m_output << "Failed to load scene from '" << load_path
                 << "' (could not open file).\n";
      }
    } catch (const std::exception &e) {
      m_output << "Failed to load scene from '" << load_path
               << "': " << e.what () << "\n";
    } catch (...) {
      m_output << "Failed to load scene from '" << load_path
               << "': unknown error.\n";
    }
  } else if (action == "save") {
    auto *scene = get_active_scene ();
    if (!scene) {
      m_output << "No active scene.\n";
      return;
    }
    bool as_default = false;
    std::string explicit_path;
    for (std::size_t i = 2; i < tokens.size (); ++i) {
      if (tokens[i] == "--as-default") {
        as_default = true;
      } else if (explicit_path.empty ()) {
        explicit_path = tokens[i];
      } else {
        explicit_path += ' ' + tokens[i];
      }
    }
    std::string path;
    if (!explicit_path.empty ()) {
      path = explicit_path;
      // Resolve relative paths against the project root
      if (m_current_project && !std::filesystem::path (path).is_absolute ()) {
        path = (std::filesystem::path (m_current_project->root_path) / path)
                   .string ();
      }
    } else {
      // Default save target resolution (E1):
      //   1. The path the scene was loaded from (keeps edits in place).
      //   2. <root>/<scenes_path>/<default_scene_path> when the active scene
      //      name matches the default scene's stem.
      //   3. <root>/<scenes_path>/<scene_name>.wscn as a last resort.
      if (m_current_project && !m_active_scene_source_path.empty ()) {
        path = m_active_scene_source_path;
      } else if (m_current_project) {
        std::filesystem::path scenes_dir (m_current_project->root_path);
        scenes_dir /= m_current_project->scenes_path;
        std::string sname = rsc::scene_file::strip_extension (
            std::filesystem::path (scene->get_name ()).filename ().string ());
        bool matches_default = false;
        if (!m_current_project->default_scene_path.empty ()) {
          matches_default = (rsc::scene_file::strip_extension (
                                 std::filesystem::path (
                                     m_current_project->default_scene_path)
                                     .filename ()
                                     .string ())
                             == sname);
        }
        if (matches_default) {
          path
              = (scenes_dir
                 / std::filesystem::path (m_current_project->default_scene_path)
                       .filename ())
                    .string ();
        } else {
          path = (scenes_dir / rsc::scene_file::with_extension (sname))
                     .string ();
        }
      } else {
        path = rsc::scene_file::with_extension (
            std::filesystem::path (scene->get_name ()).filename ().string ());
      }
    }
    wsl::rsc::io::scene_snapshot_serializer serializer (&m_rtc, *scene);
    std::filesystem::path save_path (path);
    std::error_code ec;
    std::filesystem::create_directories (save_path.parent_path (), ec);
    if (serializer.save_json (save_path.string ())) {
      m_output << "Scene saved to " << path << "\n";
      if (m_current_project
          && !m_current_project->default_scene_path.empty ()) {
        const std::string saved_name
            = std::filesystem::path (path).filename ().string ();
        const std::string default_name
            = std::filesystem::path (m_current_project->default_scene_path)
                  .filename ()
                  .string ();
        if (saved_name != default_name) {
          m_output << "Warning: saved to '" << saved_name
                   << "' but project default_scene_path points at '"
                   << default_name << "'.\n"
                   << "  The project will open the stale scene on load. Run "
                      "'proj set default_scene_path "
                   << saved_name
                   << "' (then 'proj save'), or use "
                      "'scene save --as-default'.\n";
        }
      }
      if (as_default && m_current_project) {
        m_current_project->default_scene_path
            = std::filesystem::path (path).filename ().string ();
        auto_save_project ();
        m_output << "Project default_scene_path set to '"
                 << m_current_project->default_scene_path << "'.\n";
      }
    } else
      m_output << "Failed to save scene.\n";
  } else if (action == "ls") {
    if (!m_current_project) {
      m_output << "No project loaded.\n";
      return;
    }
    auto assets = wsl::rsc::project_loader::scan_assets (*m_current_project);
    m_output << "Scenes in project:\n";
    for (const auto &s : assets.scenes) {
      std::string header_name;
      try {
        std::ifstream sf (s);
        if (sf) {
          nlohmann::json j;
          sf >> j;
          if (j.contains ("header") && j["header"].contains ("scene_name"))
            header_name = j["header"]["scene_name"].get<std::string> ();
        }
      } catch (...) {
      }

      std::filesystem::path p (s);
      std::string filename = p.filename ().string ();
      if (!header_name.empty ())
        m_output << " - " << header_name << " (" << filename << ")\n";
      else
        m_output << " - " << filename << "\n";
    }
  } else if (action == "status") {
    auto *scene = get_active_scene ();
    if (!scene) {
      m_output << "No active scene.\n";
      return;
    }
    auto &reg = scene->get_registry ();
    m_output << "Active Scene: " << scene->get_name () << "\n";
    size_t entity_count = reg.storage<entt::entity> ().size ();
    m_output << "Entities: " << entity_count << "\n";
    m_output << "Systems: " << scene->systems.size () << "\n";
    for (const auto &sys : scene->systems) {
      if (sys)
        m_output << "  - " << sys->get_name () << "\n";
    }
    // Count how many entities have each component type
    m_output << "Components:\n";
    auto components = m_rtc.component_registry ().get_world_components ();
    for (const auto *desc : components) {
      if (!desc)
        continue;
      size_t count = 0;
      for (auto [sid, storage] : reg.storage ()) {
        entt::id_type stable = m_rtc.component_registry ().to_stable_id (sid);
        if (stable == desc->type_id) {
          count = storage.size ();
          break;
        }
      }
      if (count > 0) {
        m_output << "  - " << desc->display_name << ": " << count << "\n";
      }
    }
    // Daslang components live outside entt storage, so the count above is
    // always 0 for them.
    for (const auto *desc : components) {
      if (desc == nullptr || !desc->is_das_component) {
        continue;
      }
      const auto *pool
          = m_rtc.component_registry ().das_component_pool (reg, desc->type_id);
      if (pool != nullptr && pool->size () != 0) {
        m_output << "  - " << desc->display_name << ": "
                 << pool->size () << "\n";
      }
    }
  }
}

void
command_executor::cmd_ent (const std::vector<std::string> &tokens)
{
  auto *scene = get_active_scene ();
  if (!scene) {
    m_output << "No active scene.\n";
    return;
  }

  if (tokens.size () < 2) {
    m_output << "Usage: ent <new|ls|rm|ren|inspect> [args...]\n";
    return;
  }

  const std::string &action = tokens[1];
  if (action == "new") {
    auto &reg = scene->get_registry ();
    auto e = reg.create ();

    std::size_t name_idx = 2;
    bool empty_entity = false;

    if (tokens.size () > 2 && tokens[2] == "--empty") {
      empty_entity = true;
      name_idx = 3;
    }

    if (!empty_entity) {
      reg.emplace<wsl::comp::transform> (e);
      reg.emplace<wsl::comp::world_transform> (e);
      reg.emplace<wsl::comp::hierarchy> (e);
    }

    bool has_name = tokens.size () > name_idx;
    if (has_name)
      scene->set_entity_name (e, tokens[name_idx]);

    if (empty_entity) {
      m_output << "Entity " << (uint32_t)e;
      if (has_name)
        m_output << " (" << tokens[name_idx] << ")";
      m_output << " created (empty).\n";
    } else {
      m_output << "Entity " << (uint32_t)e;
      if (has_name)
        m_output << " (" << tokens[name_idx] << ")";
      m_output << " created with Transform, WorldTransform, Hierarchy.\n";
    }
    auto_save_scene ();
  } else if (action == "ls") {
    for (auto [e] : scene->get_registry ().storage<entt::entity> ().each ()) {
      m_output << "ID: " << (uint32_t)e
               << " | Name: " << scene->get_entity_name (e) << "\n";
    }
  } else if (action == "rm") {
    if (tokens.size () < 3)
      return;
    entt::entity e = resolve_entity_token (tokens[2], scene);
    if (e == entt::null) {
      m_output << "Unknown entity: " << tokens[2] << "\n";
      return;
    }
    scene->destroy_entity_subtree (e);
    m_output << "Entity " << tokens[2] << " destroyed.\n";
    auto_save_scene ();
  } else if (action == "ren") {
    if (tokens.size () < 4)
      return;
    entt::entity e = resolve_entity_token (tokens[2], scene);
    if (e == entt::null) {
      m_output << "Unknown entity: " << tokens[2] << "\n";
      return;
    }
    scene->set_entity_name (e, tokens[3]);
    m_output << "Entity " << tokens[2] << " renamed to '" << tokens[3]
             << "'.\n";
    auto_save_scene ();
  } else if (action == "inspect") {
    if (tokens.size () < 3)
      return;
    entt::entity e = resolve_entity_token (tokens[2], scene);
    if (e == entt::null) {
      m_output << "Unknown entity: " << tokens[2] << "\n";
      return;
    }
    m_output << "Inspecting Entity " << tokens[2] << ":\n";
    m_output << " Name: " << scene->get_entity_name (e) << "\n";
    m_output << " Components:\n";
    cmd_comp ({ "comp", "ls", std::to_string ((uint32_t)e) });
  }
}

void
command_executor::cmd_comp (const std::vector<std::string> &tokens)
{
  if (tokens.size () < 2) {
    m_output << "Usage: comp <ls|avail|info|add|rm|set> [ent_id] [args...]\n";
    return;
  }
  const std::string &action = tokens[1];

  if ((action == "ls" || action == "avail") && tokens.size () == 2) {
    ensure_runtime (runtime_load_mode::auto_mode);
    auto components = m_rtc.component_registry ().get_world_components ();
    m_output << "Registered Components (" << components.size () << "):\n";
    for (const auto *component : components) {
      if (component == nullptr) {
        continue;
      }
      write_registered_entry (m_output, *component);
    }
    return;
  }

  if (action == "create") {
    if (tokens.size () < 3) {
      m_output << "Usage: comp create <name> [--source]\n";
      return;
    }
    if (!m_current_project) {
      m_output << "No project loaded.\n";
      return;
    }
    bool header_only = true;
    size_t name_idx = 2;
    if (tokens.size () > 3 && tokens[3] == "--source") {
      header_only = false;
    } else if (tokens.size () > 3 && tokens[2] == "--source") {
      header_only = false;
      name_idx = 3;
    }
    const std::vector<std::string> raw_tokens
        = tokenize_name (tokens[name_idx]);
    if (raw_tokens.empty ()) {
      m_output << "Invalid name: use letters or numbers.\n";
      return;
    }
    const std::string file_stem = to_snake_case (raw_tokens);
    const std::string class_name = to_pascal_case (raw_tokens);
    const std::string display_name = to_title_case (raw_tokens);
    if (class_name.empty ()
        || (std::isdigit (static_cast<unsigned char> (class_name.front ()))
            != 0)) {
      m_output << "Class name must start with a letter.\n";
      return;
    }
    std::filesystem::path base_dir
        = std::filesystem::path (m_current_project->root_path)
          / m_current_project->components_path;
    std::filesystem::path das_path = base_dir / (file_stem + ".das");

    std::error_code ec;
    std::filesystem::create_directories (base_dir, ec);
    if (ec) {
      m_output << "Could not create target directory.\n";
      return;
    }
    if (std::filesystem::exists (das_path)) {
      m_output << "File already exists: " << das_path.filename ().string ()
               << "\n";
      return;
    }

    std::ostringstream das_text;
    das_text << "options gen2\nmodule " << file_stem << "\n\n"
             << "struct " << class_name << " {\n"
             << "    value : float = 1.0\n"
             << "}\n";
    if (!write_text_file (das_path, das_text.str ())) {
      m_output << "Failed to write Daslang component.\n";
      return;
    }
    m_output << "Created: " << das_path.string () << "\n";
    return;
  }

  // ── comp info <name> ──
  if (action == "info") {
    if (tokens.size () < 3) {
      m_output << "Usage: comp info <name>\n";
      return;
    }
    ensure_runtime (runtime_load_mode::auto_mode);
    const auto *descriptor
        = m_rtc.component_registry ().find_world_component (tokens[2]);
    if (!descriptor) {
      m_output << "Unknown component: " << tokens[2] << "\n";
      return;
    }
    entt::meta_type meta = entt::resolve (descriptor->type_id);
    if (!meta) {
      m_output << "No reflection metadata for " << descriptor->display_name
               << "\n";
      return;
    }
    format_component_info (m_output, meta, "");
    return;
  }

  auto *scene = get_active_scene ();
  if (!scene) {
    m_output << "No active scene.\n";
    return;
  }

  if (tokens.size () < 3) {
    m_output << "Usage: comp <ls|add|rm|set> <ent_id> [args...]\n";
    return;
  }

  entt::entity e = resolve_entity_token (tokens[2], scene);
  if (e == entt::null) {
    m_output << "Unknown entity: " << tokens[2] << "\n";
    return;
  }

  if (action == "ls") {
    ensure_runtime (runtime_load_mode::force_full);
    for (auto [id, storage] : scene->get_registry ().storage ()) {
      if (storage.contains (e)) {
        if (const auto *descriptor
            = m_rtc.component_registry ().find_world_component (id)) {
          write_registered_entry (m_output, *descriptor);
          // Show property values via reflection
          entt::meta_type meta = entt::resolve (descriptor->type_id);
          if (meta && storage.value (e)) {
            format_component_properties (m_output, meta, storage.value (e),
                                         "    ", &m_rtc.resource_manager ());
          }
        } else {
          m_output << " - Unknown Component (Hash: " << id << ")\n";
        }
      }
    }

    // Daslang components are not in entt storage, so the loop above never sees
    // them. Without this pass `comp add <das component>` looks like a no-op:
    // the write succeeds but no listing mentions it.
    auto &component_registry = m_rtc.component_registry ();
    for (const auto *desc : component_registry.get_world_components ()) {
      if (desc == nullptr || !desc->is_das_component) {
        continue;
      }
      if (!component_registry.das_component_contains (scene->get_registry (),
                                                      desc->type_id, e)) {
        continue;
      }
      write_registered_entry (m_output, *desc);
      const uint8_t *data = component_registry.das_component_data (
          scene->get_registry (), desc->type_id, e);
      if (desc->das_fields.empty ()) {
        m_output << "    (no fields)\n";
        continue;
      }
      for (const auto &field : desc->das_fields) {
        m_output << "    " << field.name << " = ";
        format_das_field_value (
            m_output, field, data != nullptr ? data + field.offset : nullptr);
        m_output << " (" << field.type_name << ")\n";
      }
    }
  } else if (action == "add") {
    if (tokens.size () < 4)
      return;
    ensure_runtime (runtime_load_mode::force_full);
    auto &component_registry = m_rtc.component_registry ();
    const auto *descriptor
        = component_registry.find_world_component (tokens[3]);
    if (descriptor && descriptor->is_das_component) {
      // Daslang components live in a type-erased storage, so their descriptor
      // callbacks are null; use the dedicated helpers.
      if (component_registry.das_component_add (scene->get_registry (),
                                                descriptor->type_id, e)) {
        m_output << "Added " << tokens[3] << " to " << tokens[2] << "\n";
        auto_save_scene ();
      } else {
        m_output << "Failed to add " << tokens[3] << " to " << tokens[2]
                 << " (already has it or entity invalid)\n";
      }
    } else if (descriptor && descriptor->emplace_default) {
      if (descriptor->emplace_default (scene->get_registry (), e)) {
        m_output << "Added " << tokens[3] << " to " << tokens[2] << "\n";
        auto_save_scene ();
      } else {
        m_output << "Failed to add " << tokens[3] << " to " << tokens[2]
                 << " (already has it or entity invalid)\n";
      }
    } else {
      m_output << "Unknown component type: " << tokens[3] << "\n";
    }
  } else if (action == "rm") {
    if (tokens.size () < 4)
      return;
    ensure_runtime (runtime_load_mode::force_full);
    auto &component_registry = m_rtc.component_registry ();
    const auto *descriptor
        = component_registry.find_world_component (tokens[3]);
    if (descriptor && descriptor->is_das_component) {
      if (component_registry.das_component_remove (scene->get_registry (),
                                                   descriptor->type_id, e)) {
        m_output << "Removed " << tokens[3] << " from " << tokens[2] << "\n";
        auto_save_scene ();
      } else {
        m_output << "Failed to remove " << tokens[3] << " from " << tokens[2]
                 << "\n";
      }
    } else if (descriptor && descriptor->remove) {
      if (descriptor->remove (scene->get_registry (), e)) {
        m_output << "Removed " << tokens[3] << " from " << tokens[2] << "\n";
        auto_save_scene ();
      } else {
        m_output << "Failed to remove " << tokens[3] << " from " << tokens[2]
                 << " (entity doesn't have this component or entity invalid)\n";
      }
    } else {
      m_output << "Unknown component type: " << tokens[3] << "\n";
    }
  } else if (action == "set") {
    if (tokens.size () < 6) {
      m_output << "Usage: comp set <id> <type> <property> <value>\n";
      return;
    }

    ensure_runtime (runtime_load_mode::force_full);
    const auto *descriptor
        = m_rtc.component_registry ().find_world_component (tokens[3]);
    if (!descriptor) {
      m_output << "Unknown component type: " << tokens[3] << "\n";
      return;
    }

    entt::meta_type meta = entt::resolve (descriptor->type_id);
    auto &component_registry = m_rtc.component_registry ();
    auto &registry = scene->get_registry ();

    if (!registry.valid (e)) {
      m_output << "Invalid entity: " << tokens[2] << "\n";
      return;
    }

    // A daslang component has no entt reflection, so the property name is
    // matched against its recorded field layout instead.
    if (descriptor->is_das_component) {
      if (!component_registry.das_component_contains (registry,
                                                      descriptor->type_id, e)) {
        m_output << "Entity " << tokens[2] << " does not have component "
                 << tokens[3] << "\n";
        return;
      }
      const std::string &field_name = tokens[4];
      const std::vector<wsl::reg::component_registry::descriptor::das_field>
          &das_fields = descriptor->das_fields;
      const auto field = std::find_if (
          das_fields.begin (), das_fields.end (),
          [&field_name] (
              const wsl::reg::component_registry::descriptor::das_field &f) {
            return f.name == field_name;
          });
      if (field == das_fields.end ()) {
        m_output << "Unknown field: " << field_name << "\n";
        if (!das_fields.empty ()) {
          m_output << "  Fields: ";
          for (std::size_t i = 0; i < das_fields.size (); ++i) {
            m_output << (i != 0 ? ", " : "") << das_fields[i].name;
          }
          m_output << "\n";
        }
        return;
      }
      uint8_t *das_ptr = component_registry.das_component_data (
          registry, descriptor->type_id, e);
      if (das_ptr == nullptr) {
        m_output << "Could not locate component data for " << tokens[3]
                 << " on entity " << tokens[2] << "\n";
        return;
      }

      std::string set_msg;
      if (!set_das_field_value (das_ptr + field->offset, *field, tokens[5],
                                set_msg)) {
        m_output << "Failed to set " << tokens[3] << "." << field->name << ": "
                 << set_msg << "\n";
        return;
      }
      m_output << "Set " << tokens[3] << "." << field->name << " = "
               << tokens[5] << " (" << set_msg << ")\n";
      auto_save_scene ();
      return;
    }

    if (!meta) {
      m_output << "No reflection metadata for " << tokens[3] << "\n";
      return;
    }

    if (descriptor->contains && !descriptor->contains (registry, e)) {
      m_output << "Entity " << tokens[2] << " does not have component "
               << tokens[3] << "\n";
      return;
    }

    // Find component storage and get void pointer
    void *comp_ptr = nullptr;
    for (auto [sid, storage] : registry.storage ()) {
      if (storage.contains (e)) {
        entt::id_type stable = m_rtc.component_registry ().to_stable_id (sid);
        if (stable == descriptor->type_id) {
          comp_ptr = storage.value (e);
          break;
        }
      }
    }

    if (!comp_ptr) {
      m_output << "Could not locate component data for " << tokens[3]
               << " on entity " << tokens[2] << "\n";
      return;
    }

    // Split property path on '.' for nested traversal
    std::string prop_path = tokens[4];
    std::vector<std::string> segments;
    size_t start = 0;
    size_t dot;
    while ((dot = prop_path.find ('.', start)) != std::string::npos) {
      segments.push_back (prop_path.substr (start, dot - start));
      start = dot + 1;
    }
    segments.push_back (prop_path.substr (start));

    // ── Nested property traversal ──
    // meta_data::get/set internally use try_cast which fails on
    // reference-typed meta_any (from from_void or field accessors).
    // Solution: for the innermost parent, create a VALUE copy, set the
    // property on the VALUE copy (try_cast works), then write the
    // entire modified copy back to the registry component via memcpy.
    // The nested path is walked using the original comp_ptr and
    // from_void; each level's data() gives the field address which we
    // store so we know where to memcpy the modified parent back.
    struct cl
    {
      void *parent_ptr;
      entt::meta_type parent_type;
      entt::id_type acc_hash;
    };
    std::vector<cl> chain;

    void *cur = comp_ptr;
    entt::meta_type cur_meta = meta;
    for (size_t i = 0; i + 1 < segments.size (); ++i) {
      entt::id_type seg_id = entt::hashed_string::value (segments[i].c_str (),
                                                         segments[i].size ());
      auto data = find_meta_data (cur_meta, segments[i]);
      if (!data) {
        m_output << "Unknown property: " << segments[i] << " in path "
                 << tokens[4] << "\n";
        return;
      }
      chain.push_back ({ cur, cur_meta, seg_id });
      entt::meta_any p = cur_meta.from_void (cur);
      if (!p) {
        m_output << "Cannot access parent at " << segments[i] << "\n";
        return;
      }
      entt::meta_any s = data.get (p);
      if (!s) {
        m_output << "Cannot access " << segments[i] << " (null or unset)\n";
        return;
      }
      void *np = const_cast<void *> (s.base ().data ());
      if (!np) {
        m_output << "Cannot resolve field " << segments[i] << "\n";
        return;
      }
      cur = np;
      cur_meta = data.type ();
    }

    // Create a VALUE copy of the innermost parent
    size_t parent_sz = cur_meta.size_of ();
    void *parent_buf = std::malloc (parent_sz);
    std::memcpy (parent_buf, cur, parent_sz);
    entt::meta_any parent_val = cur_meta.from_void (parent_buf, true);

    // Set the leaf property on the VALUE copy
    auto prop_data = find_meta_data (cur_meta, segments.back ());
    if (!prop_data) {
      m_output << "Unknown property: " << tokens[4] << "\n";
      return;
    }

    std::string set_msg;
    if (set_component_property (parent_val, prop_data, tokens[5], set_msg,
                                &m_rtc.resource_manager ())) {
      // Write the modified innermost parent back to wherever cur points
      std::memcpy (cur, parent_buf, parent_sz);

      // Walk the chain in reverse to propagate changes up to the
      // original component.  Each level makes a value copy of the
      // parent, calls meta_data::set() to write the modified child
      // into the copy, then memcpy's the copy back to the original
      // parent_ptr (which points into the ECS registry storage).
      void *modified = parent_buf;
      size_t modified_sz = parent_sz;
      for (size_t j = chain.size (); j-- > 0;) {
        auto &cl = chain[j];

        size_t psz = cl.parent_type.size_of ();
        void *pcopy = std::malloc (psz);
        std::memcpy (pcopy, cl.parent_ptr, psz);
        entt::meta_any pv = cl.parent_type.from_void (pcopy, false);

        auto cd = cl.parent_type.data (cl.acc_hash);
        if (!cd)
          break;
        entt::meta_any cv = cd.type ().from_void (modified, false);
        cd.set (pv, cv);

        std::memcpy (cl.parent_ptr, pcopy, psz);
        modified = cl.parent_ptr;
        modified_sz = psz;
        std::free (pcopy);
      }

      m_output << "Set " << tokens[3] << "." << tokens[4] << " = " << tokens[5]
               << " (" << set_msg << ")\n";
      auto_save_scene ();
    } else {
      m_output << "Failed to set " << tokens[3] << "." << tokens[4] << ": "
               << set_msg << "\n";
    }
  }
}

void
command_executor::cmd_singl (const std::vector<std::string> &tokens)
{
  if (tokens.size () < 2) {
    m_output << "Usage: singl <ls|info|add|rm|create|set> [args...]\n";
    return;
  }
  const std::string &action = tokens[1];

  // ── singl ls ──
  if (action == "ls") {
    ensure_runtime (runtime_load_mode::auto_mode);
    auto singletons = m_rtc.singleton_registry ().get_singleton_components ();
    auto *scene = get_active_scene ();
    auto &singleton_registry = m_rtc.singleton_registry ();
    m_output << "Singleton Components (" << singletons.size () << "):\n";
    for (const auto *s : singletons) {
      if (!s)
        continue;
      m_output << " - " << s->display_name;
      if (!s->type_name.empty () && s->type_name != s->display_name)
        m_output << " [" << s->type_name << "]";
      if (s->core)
        m_output << " [core]";

      // A daslang singleton carries no `contains` callback, so testing the
      // descriptor alone reported every user singleton as [absent] and made
      // `singl create` followed by `singl add` look like a dead end.
      const bool present
          = (scene != nullptr)
            && (s->is_das_singleton
                    ? singleton_registry.das_singleton_contains (
                          scene->get_registry (), s->type_id)
                    : (s->contains ? s->contains (scene->get_registry ())
                                   : false));
      if (scene)
        m_output << (present ? " [present]" : " [absent]");
      m_output << "\n";

      if (!scene || !present) {
        continue;
      }

      if (s->is_das_singleton) {
        const uint8_t *das_ptr = singleton_registry.das_singleton_data (
            scene->get_registry (), s->type_id);
        if (s->das_fields.empty ()) {
          m_output << "    (no fields)\n";
          continue;
        }
        for (const auto &field : s->das_fields) {
          m_output << "    " << field.name << " = ";
          format_das_field_value (m_output, field,
                                  das_ptr != nullptr ? das_ptr + field.offset
                                                     : nullptr);
          m_output << " (" << field.type_name << ")\n";
        }
        continue;
      }

      if (s->contains && s->get_ptr) {
        void *ptr = s->get_ptr (scene->get_registry ());
        if (ptr) {
          entt::meta_type meta = entt::resolve (s->type_id);
          if (meta)
            format_component_properties (m_output, meta, ptr, "    ",
                                         &m_rtc.resource_manager ());
        }
      }
    }
    return;
  }

  // ── singl info <name> ──
  if (action == "info") {
    if (tokens.size () < 3) {
      m_output << "Usage: singl info <name>\n";
      return;
    }
    ensure_runtime (runtime_load_mode::auto_mode);
    const auto *descriptor
        = m_rtc.singleton_registry ().find_singleton_component (tokens[2]);
    if (!descriptor) {
      m_output << "Unknown singleton: " << tokens[2] << "\n";
      return;
    }
    entt::meta_type meta = entt::resolve (descriptor->type_id);
    if (!meta) {
      m_output << "No reflection metadata for " << descriptor->display_name
               << "\n";
      if (descriptor->is_das_singleton) {
        m_output << "Daslang singleton fields ("
                 << descriptor->das_fields.size () << "):\n";
        for (const auto &field : descriptor->das_fields) {
          m_output << " - " << field.name << " (" << field.type_name
                   << ") at offset " << field.offset << " size " << field.size
                   << "\n";
        }
        m_output << "CLI: singl set " << descriptor->type_name
                 << " <field> <value>\n";
      }
      return;
    }
    format_component_info (m_output, meta, "");
    return;
  }

  // ── singl add <name> ──
  if (action == "add") {
    if (tokens.size () < 3) {
      m_output << "Usage: singl add <name>\n";
      return;
    }
    auto *scene = get_active_scene ();
    if (!scene) {
      m_output << "No active scene.\n";
      return;
    }
    ensure_runtime (runtime_load_mode::force_full);
    auto &singleton_registry = m_rtc.singleton_registry ();
    const auto *desc = singleton_registry.find_singleton_component (tokens[2]);
    if (!desc) {
      m_output << "Unknown singleton component: " << tokens[2] << "\n";
      return;
    }
    if (desc->is_das_singleton) {
      if (desc->das_struct_size <= 0) {
        m_output << "Layout for " << desc->display_name
                 << " is unknown; run 'script reload'.\n";
        return;
      }
      if (singleton_registry.das_singleton_contains (scene->get_registry (),
                                                     desc->type_id)) {
        m_output << desc->display_name << " is already present.\n";
        return;
      }
      if (singleton_registry.das_singleton_add (scene->get_registry (),
                                                desc->type_id)) {
        m_output << "Added singleton: " << desc->display_name << "\n";
        auto_save_scene ();
      } else {
        m_output << "Failed to add singleton: " << desc->display_name << "\n";
      }
      return;
    }
    if (!desc->emplace_default) {
      // Not "bound": a runtime-registered daslang singleton without a cached
      // layout simply has no constructor available.
      m_output << desc->display_name
               << " cannot be added: it is a bound singleton"
               << (desc->runtime_registered ? " or its layout is unknown" : "")
               << ".\n";
      return;
    }
    if (desc->contains (scene->get_registry ())) {
      m_output << desc->display_name << " is already present.\n";
      return;
    }
    desc->emplace_default (scene->get_registry ());
    m_output << "Added singleton: " << desc->display_name << "\n";
    auto_save_scene ();
    return;
  }

  // ── singl rm <name> ──
  if (action == "rm") {
    if (tokens.size () < 3) {
      m_output << "Usage: singl rm <name>\n";
      return;
    }
    auto *scene = get_active_scene ();
    if (!scene) {
      m_output << "No active scene.\n";
      return;
    }
    ensure_runtime (runtime_load_mode::force_full);
    auto &singleton_registry = m_rtc.singleton_registry ();
    const auto *desc = singleton_registry.find_singleton_component (tokens[2]);
    if (!desc) {
      m_output << "Unknown singleton component: " << tokens[2] << "\n";
      return;
    }
    if (desc->is_das_singleton) {
      if (singleton_registry.das_singleton_remove (scene->get_registry (),
                                                   desc->type_id)) {
        m_output << "Removed singleton: " << desc->display_name << "\n";
        auto_save_scene ();
      } else {
        m_output << desc->display_name << " is not present.\n";
      }
      return;
    }
    if (desc->core) {
      m_output << desc->display_name
               << " is a core engine singleton and cannot be removed.\n";
      return;
    }
    if (!desc->remove) {
      m_output << desc->display_name << " cannot be removed via CLI.\n";
      return;
    }
    if (desc->remove (scene->get_registry ())) {
      m_output << "Removed singleton: " << desc->display_name << "\n";
      auto_save_scene ();
    } else {
      m_output << desc->display_name << " is not present.\n";
    }
    return;
  }

  // ── singl create <name> [--source] ──
  if (action == "create") {
    if (tokens.size () < 3) {
      m_output << "Usage: singl create <name> [--source]\n";
      return;
    }
    if (!m_current_project) {
      m_output << "No project loaded.\n";
      return;
    }
    bool header_only = true;
    size_t name_idx = 2;
    if (tokens.size () > 3 && tokens[3] == "--source") {
      header_only = false;
    } else if (tokens.size () > 3 && tokens[2] == "--source") {
      header_only = false;
      name_idx = 3;
    }
    const std::vector<std::string> raw_tokens
        = tokenize_name (tokens[name_idx]);
    if (raw_tokens.empty ()) {
      m_output << "Invalid name: use letters or numbers.\n";
      return;
    }
    const std::string file_stem = to_snake_case (raw_tokens);
    const std::string class_name = to_pascal_case (raw_tokens);
    const std::string display_name = to_title_case (raw_tokens);
    if (class_name.empty ()
        || (std::isdigit (static_cast<unsigned char> (class_name.front ()))
            != 0)) {
      m_output << "Class name must start with a letter.\n";
      return;
    }
    std::filesystem::path base_dir
        = std::filesystem::path (m_current_project->root_path)
          / m_current_project->singletons_path;
    std::filesystem::path das_path = base_dir / (file_stem + ".das");

    std::error_code ec;
    std::filesystem::create_directories (base_dir, ec);
    if (ec) {
      m_output << "Could not create target directory.\n";
      return;
    }
    if (std::filesystem::exists (das_path)) {
      m_output << "File already exists: " << das_path.filename ().string ()
               << "\n";
      return;
    }

    std::ostringstream das_text;
    das_text << "options gen2\nmodule " << file_stem << "\n\n"
             << "struct " << class_name << " {\n"
             << "    value : float = 1.0\n"
             << "}\n";
    if (!write_text_file (das_path, das_text.str ())) {
      m_output << "Failed to write Daslang singleton.\n";
      return;
    }
    m_output << "Created: " << das_path.string () << "\n";
    return;
  }

  // ── singl set <name> <property> <value> ──
  if (action == "set") {
    if (tokens.size () < 5) {
      m_output << "Usage: singl set <name> <property> <value>\n";
      return;
    }
    auto *scene = get_active_scene ();
    if (!scene) {
      m_output << "No active scene.\n";
      return;
    }
    ensure_runtime (runtime_load_mode::force_full);
    auto &singleton_registry = m_rtc.singleton_registry ();
    const auto *desc = singleton_registry.find_singleton_component (tokens[2]);
    if (!desc) {
      m_output << "Unknown singleton component: " << tokens[2] << "\n";
      return;
    }
    if (desc->is_das_singleton) {
      // Daslang singletons have no entt reflection, so the property name is
      // matched against the recorded field layout instead.
      if (!singleton_registry.das_singleton_contains (scene->get_registry (),
                                                      desc->type_id)) {
        m_output << desc->display_name
                 << " is not present in the active scene.\n";
        return;
      }
      const std::string &field_name = tokens[3];
      const std::vector<wsl::reg::singleton_registry::descriptor::das_field>
          &das_fields = desc->das_fields;
      const auto field = std::find_if (
          das_fields.begin (), das_fields.end (),
          [&field_name] (
              const wsl::reg::singleton_registry::descriptor::das_field &f) {
            return f.name == field_name;
          });
      if (field == das_fields.end ()) {
        m_output << "Unknown field: " << field_name << "\n";
        if (!das_fields.empty ()) {
          m_output << "  Fields: ";
          for (std::size_t i = 0; i < das_fields.size (); ++i) {
            m_output << (i != 0 ? ", " : "") << das_fields[i].name;
          }
          m_output << "\n";
        }
        return;
      }
      uint8_t *das_ptr = singleton_registry.das_singleton_data (
          scene->get_registry (), desc->type_id);
      if (das_ptr == nullptr) {
        m_output << "Failed to access singleton data.\n";
        return;
      }

      std::string set_msg;
      if (!set_das_field_value (das_ptr + field->offset, *field, tokens[4],
                                set_msg)) {
        m_output << "Failed to set " << desc->display_name << "." << field->name
                 << ": " << set_msg << "\n";
        return;
      }
      m_output << "Set " << desc->display_name << "." << field->name << " = "
               << tokens[4] << " (" << set_msg << ")\n";
      auto_save_scene ();
      return;
    }
    if (!desc->contains (scene->get_registry ())) {
      m_output << desc->display_name
               << " is not present in the active scene.\n";
      return;
    }
    void *ptr = desc->get_ptr (scene->get_registry ());
    if (!ptr) {
      m_output << "Failed to access singleton data.\n";
      return;
    }
    entt::meta_type meta = entt::resolve (desc->type_id);
    if (!meta) {
      m_output << "No reflection metadata for " << desc->display_name << "\n";
      return;
    }

    std::string prop_path = tokens[3];
    std::vector<std::string> segments;
    size_t start = 0;
    size_t dot;
    while ((dot = prop_path.find ('.', start)) != std::string::npos) {
      segments.push_back (prop_path.substr (start, dot - start));
      start = dot + 1;
    }
    segments.push_back (prop_path.substr (start));

    struct cl
    {
      void *parent_ptr;
      entt::meta_type parent_type;
      entt::id_type acc_hash;
    };
    std::vector<cl> chain;

    void *cur = ptr;
    entt::meta_type cur_meta = meta;
    for (size_t i = 0; i + 1 < segments.size (); ++i) {
      auto data = find_meta_data (cur_meta, segments[i]);
      if (!data) {
        m_output << "Unknown property: " << segments[i] << " in path "
                 << prop_path << "\n";
        return;
      }
      chain.push_back ({ cur, cur_meta,
                         entt::hashed_string::value (segments[i].c_str (),
                                                     segments[i].size ()) });
      entt::meta_any p = cur_meta.from_void (cur);
      if (!p) {
        m_output << "Cannot access parent at " << segments[i] << "\n";
        return;
      }
      entt::meta_any s = data.get (p);
      if (!s) {
        m_output << "Cannot access " << segments[i] << " (null or unset)\n";
        return;
      }
      void *np = const_cast<void *> (s.base ().data ());
      if (!np) {
        m_output << "Cannot resolve field " << segments[i] << "\n";
        return;
      }
      cur = np;
      cur_meta = data.type ();
    }

    size_t parent_sz = cur_meta.size_of ();
    void *parent_buf = std::malloc (parent_sz);
    std::memcpy (parent_buf, cur, parent_sz);
    entt::meta_any parent_val = cur_meta.from_void (parent_buf, true);

    auto prop_data = find_meta_data (cur_meta, segments.back ());
    if (!prop_data) {
      m_output << "Unknown property: " << prop_path << "\n";
      std::free (parent_buf);
      return;
    }

    std::string set_msg;
    if (set_component_property (parent_val, prop_data, tokens[4], set_msg,
                                &m_rtc.resource_manager ())) {
      std::memcpy (cur, parent_buf, parent_sz);

      void *modified = parent_buf;
      size_t modified_sz = parent_sz;
      for (size_t j = chain.size (); j-- > 0;) {
        auto &cl = chain[j];

        size_t psz = cl.parent_type.size_of ();
        void *pcopy = std::malloc (psz);
        std::memcpy (pcopy, cl.parent_ptr, psz);
        entt::meta_any pv = cl.parent_type.from_void (pcopy, false);

        auto cd = cl.parent_type.data (cl.acc_hash);
        if (!cd)
          break;
        entt::meta_any cv = cd.type ().from_void (modified, false);
        cd.set (pv, cv);

        std::memcpy (cl.parent_ptr, pcopy, psz);
        modified = cl.parent_ptr;
        modified_sz = psz;
        std::free (pcopy);
      }

      m_output << "Set " << desc->display_name << "." << tokens[3] << " = "
               << tokens[4] << " (" << set_msg << ")\n";
      auto_save_scene ();
    } else {
      m_output << "Failed to set " << desc->display_name << "." << tokens[3]
               << ": " << set_msg << "\n";
    }
    // parent_val (entt::meta_any with transfer_ownership=true) owns
    // parent_buf; do NOT free it explicitly to avoid double-free.
    return;
  }

  m_output << "Usage: singl <ls|add|create|set> [args...]\n";
}

void
command_executor::cmd_sig (const std::vector<std::string> &tokens)
{
  if (tokens.size () < 2) {
    m_output << "Usage: sig <ls|handlers|connections|connect|disconnect> "
                "[args...]\n";
    return;
  }

  const std::string &sub = tokens[1];

  if (sub == "ls") {
    if (m_rtc.event_db ().entries.empty ()) {
      m_output << "No events declared.\n";
      return;
    }
    for (const auto &kv : m_rtc.event_db ().entries) {
      const auto &entry = kv.second;
      m_output << entry.type_name << " (owner=" << entry.owner_system_type_name
               << ") listeners=" << entry.listener_count
               << " emits=" << entry.emit_count << "\n";
    }
    return;
  }

  if (sub == "handlers") {
    if (m_rtc.event_db ().event_sinks.empty ()) {
      m_output << "No connectable handlers declared.\n";
      return;
    }
    for (const auto &h : m_rtc.event_db ().event_sinks) {
      m_output << h.event_type_name << " -> " << h.system_type_name
               << "::" << h.handler_name << "\n";
    }
    return;
  }

  if (sub == "connections") {
    auto conns = m_rtc.event_hub ().get_all_connections ();
    if (conns.empty ()) {
      m_output << "No event connections.\n";
      return;
    }
    for (const auto &c : conns) {
      std::string signal_name = std::to_string (c.event_type_id);
      auto it = m_rtc.event_db ().entries.find (c.event_type_id);
      if (it != m_rtc.event_db ().entries.end ())
        signal_name = it->second.type_name;

      std::string system_name = "<unknown>";
      if (auto sd
          = m_rtc.system_factory_registry ().find_system (c.system_type_id))
        system_name = sd->display_name;

      m_output << signal_name << " -> " << system_name << "::" << c.handler_name
               << "\n";
    }
    return;
  }

  if (sub == "connect" || sub == "disconnect") {
    // Usage: sig connect <event> <system> <handler>
    if (tokens.size () < 5) {
      m_output << "Usage: sig " << sub << " <signal> <system> <handler>\n";
      return;
    }

    const std::string &signal_arg = tokens[2];
    const std::string &system_arg = tokens[3];
    const std::string &handler_arg = tokens[4];

    // Resolve event type id by full or simple name
    entt::id_type signal_id = 0;
    for (const auto &kv : m_rtc.event_db ().entries) {
      const auto &entry = kv.second;
      if (entry.type_name == signal_arg) {
        signal_id = kv.first;
        break;
      }
      // match simple name (after last ::)
      auto pos = entry.type_name.rfind ("::");
      std::string simple = (pos == std::string::npos)
                               ? entry.type_name
                               : entry.type_name.substr (pos + 2);
      if (simple == signal_arg) {
        signal_id = kv.first;
        break;
      }
    }
    if (signal_id == 0) {
      m_output << "Unknown event: " << signal_arg << "\n";
      return;
    }

    // Resolve system type id by display name or type name
    entt::id_type system_id = 0;
    if (auto sd = m_rtc.system_factory_registry ().find_system (system_arg)) {
      system_id = sd->type_id;
    } else {
      // try match by type name
      if (auto sd2
          = m_rtc.system_factory_registry ().find_system (system_arg)) {
        system_id = sd2->type_id;
      }
    }
    if (system_id == 0) {
      m_output << "Unknown system: " << system_arg << "\n";
      return;
    }

    bool ok = false;
    if (sub == "connect") {
      ok = m_rtc.event_hub ().connect (signal_id, system_id, handler_arg);
      m_output << (ok ? "Connected." : "Failed to connect.") << "\n";
    } else {
      ok = m_rtc.event_hub ().disconnect (signal_id, system_id, handler_arg);
      m_output << (ok ? "Disconnected." : "Failed to disconnect.") << "\n";
    }
    return;
  }

  m_output << "Unknown sig subcommand: " << sub << "\n";
}

void
command_executor::cmd_sys (const std::vector<std::string> &tokens)
{
  if (tokens.size () < 2) {
    m_output << "Usage: sys <ls|avail|add>\n";
    return;
  }

  if (tokens[1] == "ls") {
    auto *scene = get_active_scene ();
    if (!scene) {
      m_output << "No active scene.\n";
      return;
    }

    // Collect core systems (always present in every scene).
    std::vector<std::string> core_names;
    if (m_rtc.core_systems ()) {
      for (sys::ecs_system *sys : m_rtc.core_systems ()->to_vec ()) {
        if (sys)
          core_names.push_back (sys->get_name ());
      }
    } else {
      // Headless mode: list core system display names from the factory.
      auto all = m_rtc.system_factory_registry ().get_systems ();
      for (const auto *desc : all) {
        if (desc && !desc->runtime_registered)
          core_names.push_back (desc->display_name);
      }
    }

    // Collect per-scene (user-defined) systems.
    auto user_instances = scene->get_systems ();
    std::vector<std::string> user_names;
    user_names.reserve (user_instances.size ());
    for (const auto *sys : user_instances) {
      if (sys)
        user_names.push_back (sys->get_name ());
    }

    m_output << "Scene systems (" << (core_names.size () + user_names.size ())
             << "):\n";
    for (const auto &name : core_names) {
      m_output << "  " << name << " (core)\n";
    }
    for (const auto &name : user_names) {
      m_output << "  " << name << "\n";
    }
    if (core_names.empty () && user_names.empty ()) {
      m_output << "  (none)\n";
    }
  } else if (tokens[1] == "avail") {
    ensure_runtime (runtime_load_mode::auto_mode);
    auto systems = m_rtc.system_factory_registry ().get_systems ();

    std::vector<const reg::system_factory_registry::system_descriptor *>
        user_systems;
    for (const auto *system : systems) {
      if (system && system->runtime_registered) {
        user_systems.push_back (system);
      }
    }

    if (user_systems.empty ()) {
      m_output << "No user-defined system types registered.\n"
               << "  Use `sys create <name>` to create a custom system, then\n"
               << "  `sys add <name>` to attach it to the active scene.\n";
      return;
    }

    m_output << "User-defined system types (" << user_systems.size () << "):\n";
    for (const auto *system : user_systems) {
      if (system == nullptr) {
        continue;
      }
      write_registered_entry (m_output, *system);
    }
    if (!user_systems.empty ()) {
      m_output << "\nEach system lives at src/systems/<file_stem>.das where "
                  "<file_stem> is the display name lower-cased, spaces turned "
                  "into underscores, and suffixed with '_system'\n"
                  "(e.g. 'Ball Controller System' → "
                  "src/systems/ball_controller_system.das).\n";
    }
  } else if (tokens[1] == "create") {
    if (tokens.size () < 3) {
      m_output << "Usage: sys create <name> [--source]\n";
      return;
    }
    if (!m_current_project) {
      m_output << "No project loaded.\n";
      return;
    }
    bool header_only = true;
    size_t name_idx = 2;
    if (tokens.size () > 3 && tokens[3] == "--source") {
      header_only = false;
    } else if (tokens.size () > 3 && tokens[2] == "--source") {
      header_only = false;
      name_idx = 3;
    }
    std::vector<std::string> raw_tokens = tokenize_name (tokens[name_idx]);
    if (raw_tokens.empty ()) {
      m_output << "Invalid name: use letters or numbers.\n";
      return;
    }
    // Append "system" if not already present (matching editor behavior)
    if (raw_tokens.back () != "system") {
      raw_tokens.push_back ("system");
    }
    const std::string file_stem = to_snake_case (raw_tokens);
    const std::string class_name = to_pascal_case (raw_tokens);
    const std::string display_name = to_title_case (raw_tokens);
    if (class_name.empty ()
        || (std::isdigit (static_cast<unsigned char> (class_name.front ()))
            != 0)) {
      m_output << "Class name must start with a letter.\n";
      return;
    }
    std::filesystem::path base_dir
        = std::filesystem::path (m_current_project->root_path)
          / m_current_project->systems_path;
    std::filesystem::path das_path = base_dir / (file_stem + ".das");

    std::error_code ec;
    std::filesystem::create_directories (base_dir, ec);
    if (ec) {
      m_output << "Could not create target directory.\n";
      return;
    }
    if (std::filesystem::exists (das_path)) {
      m_output << "File already exists: " << das_path.filename ().string ()
               << "\n";
      return;
    }

    std::ostringstream das_text;
    // The `module` declaration is mandatory, not cosmetic: a runtime source
    // without one ends up attached to daScript's shared global module, and
    // destroying its Program at process exit corrupts the heap ("free():
    // invalid size", "double free or corruption", "munmap_chunk(): invalid
    // pointer", exit 134).  comp create / singl create already emit it.
    das_text << "options gen2\nmodule " << file_stem << "\n"
             << "require weasel_ecs\nrequire weasel_helpers\n\n"
             << "class System : EcsSystem {\n"
             << "    def override on_update(dt : float) : void {\n"
             << "    }\n"
             << "}\n";
    if (!write_text_file (das_path, das_text.str ())) {
      m_output << "Failed to write Daslang system.\n";
      return;
    }
    m_output << "Created: " << das_path.string () << "\n";
  } else if (tokens[1] == "add") {
    if (tokens.size () < 3) {
      m_output << "Usage: sys add <name>\n";
      return;
    }
    ensure_runtime (runtime_load_mode::auto_mode);
    std::string sys_name = tokens[2];
    for (size_t i = 3; i < tokens.size (); ++i) {
      sys_name += ' ' + tokens[i];
    }
    auto *scene = get_active_scene ();
    if (!scene) {
      m_output << "No active scene.\n";
      return;
    }
    auto *desc = m_rtc.system_factory_registry ().find_system (sys_name);
    if (desc == nullptr) {
      m_output << "Unknown system: '" << sys_name << "'\n";
      const std::string suggestion
          = suggest_system_name (sys_name, m_rtc.system_factory_registry ());
      if (!suggestion.empty ()) {
        m_output << "  Did you mean: '" << suggestion << "'?\n";
      }
      m_output << "  Use quotes for multi-word names: sys add \"" << sys_name
               << "\"\n"
               << "  List available systems with: sys avail\n";
    } else if (!desc->runtime_registered) {
      m_output << "'" << sys_name
               << "' is a core engine system and is always present.\n"
               << "  Use `sys add` only for custom systems created via `sys "
                  "create`.\n";
    } else {
      auto sys = m_rtc.system_factory_registry ().create (sys_name, *scene);
      if (sys) {
        scene->add_system_instance (std::move (sys));
        m_output << "Added system '" << sys_name << "' to active scene.\n";
        auto_save_scene ();
      } else {
        // Never fail silently: an empty response here reads as success.
        m_output << "Found system '" << sys_name
                 << "' but could not instantiate it.\n"
                 << "  Its script probably failed to compile -- run 'script "
                    "reload' for the compiler error.\n";
      }
    }
  } else {
    m_output << "Usage: sys <ls|avail|add>\n";
  }
}

void
command_executor::cmd_check (const std::vector<std::string> &tokens)
{
  if (tokens.size () < 2)
    return;
  m_output << "Validation placeholder for " << tokens[1] << "\n";
}

static std::string
resource_state_str (wsl::rsc::model_state s)
{
  switch (s) {
  case wsl::rsc::model_state::not_loaded:
    return "not_loaded";
  case wsl::rsc::model_state::loading_cpu:
    return "loading_cpu";
  case wsl::rsc::model_state::preparing_gpu:
    return "preparing_gpu";
  case wsl::rsc::model_state::uploading_gpu:
    return "uploading_gpu";
  case wsl::rsc::model_state::loaded:
    return "loaded";
  }
  return "unknown";
}

static std::string
resource_state_str (wsl::rsc::image_state s)
{
  switch (s) {
  case wsl::rsc::image_state::not_loaded:
    return "not_loaded";
  case wsl::rsc::image_state::loading:
    return "loading";
  case wsl::rsc::image_state::loaded:
    return "loaded";
  }
  return "unknown";
}

static std::string
resource_state_str (wsl::rsc::cubemap_state s)
{
  switch (s) {
  case wsl::rsc::cubemap_state::not_loaded:
    return "not_loaded";
  case wsl::rsc::cubemap_state::loading:
    return "loading";
  case wsl::rsc::cubemap_state::loaded:
    return "loaded";
  }
  return "unknown";
}

static std::string
resource_state_str (wsl::rsc::scene_state s)
{
  switch (s) {
  case wsl::rsc::scene_state::not_loaded:
    return "not_loaded";
  case wsl::rsc::scene_state::loading:
    return "loading";
  case wsl::rsc::scene_state::loaded:
    return "loaded";
  }
  return "unknown";
}

static std::string
resource_state_str (wsl::rsc::audio_state s)
{
  switch (s) {
  case wsl::rsc::audio_state::not_loaded:
    return "not_loaded";
  case wsl::rsc::audio_state::loading:
    return "loading";
  case wsl::rsc::audio_state::loaded:
    return "loaded";
  }
  return "unknown";
}

static std::string
resource_state_str (wsl::rsc::ui_layout_state s)
{
  switch (s) {
  case wsl::rsc::ui_layout_state::not_loaded:
    return "not_loaded";
  case wsl::rsc::ui_layout_state::loaded:
    return "loaded";
  }
  return "unknown";
}

static std::string
resource_state_str (wsl::rsc::shader_state s)
{
  switch (s) {
  case wsl::rsc::shader_state::not_loaded:
    return "not_loaded";
  case wsl::rsc::shader_state::loading:
    return "loading";
  case wsl::rsc::shader_state::loaded:
    return "loaded";
  }
  return "unknown";
}

void
command_executor::cmd_rsc (const std::vector<std::string> &tokens)
{
  auto &mgr = m_rtc.resource_manager ();

  auto list_type = [&] (std::string_view label, auto &&list_fn) {
    auto items = list_fn ();
    m_output << "  " << label << " (" << items.size () << "):\n";
    for (auto &item : items) {
      m_output << "    " << item.name << "  [" << item.path << "]\n";
    }
  };

  auto list_type_with_state = [&] (std::string_view label, auto &&list_fn) {
    auto items = list_fn ();
    m_output << "  " << label << " (" << items.size () << "):\n";
    for (auto &item : items) {
      m_output << "    " << item.name << "  [" << item.path << "]  ("
               << resource_state_str (item.state) << ")\n";
    }
  };

  auto show_info = [&] (const std::string &label, auto id, auto &&info_fn) {
    auto item = info_fn (id);
    if (item) {
      m_output << "Type: " << label << "\n";
      m_output << "  Name: " << item->name << "\n";
      m_output << "  Path: " << item->path << "\n";
      if constexpr (requires { item->state; }) {
        m_output << "  State: " << resource_state_str (item->state) << "\n";
      }
    } else {
      m_output << label << " not found.\n";
    }
  };

  // ── Resolve a name or path to a resource ID ──
  auto find_model = [&] (const std::string &input) -> std::optional<model_id> {
    if (auto id = mgr.find_model_by_path (input))
      return id;
    for (auto &info : mgr.list_models ())
      if (info.name == input)
        return model_id{ info.id };
    return std::nullopt;
  };

  auto find_image = [&] (const std::string &input) -> std::optional<image_id> {
    image_id id{ entt::hashed_string::value (input.c_str (), input.size ()) };
    if (mgr.contains (id))
      return id;
    for (auto &info : mgr.list_images ())
      if (info.name == input)
        return image_id{ info.id };
    return std::nullopt;
  };

  auto find_cubemap
      = [&] (const std::string &input) -> std::optional<cubemap_id> {
    cubemap_id id{ entt::hashed_string::value (input.c_str (), input.size ()) };
    if (mgr.contains (id))
      return id;
    for (auto &info : mgr.list_cubemaps ())
      if (info.name == input)
        return cubemap_id{ info.id };
    return std::nullopt;
  };

  auto find_scene = [&] (const std::string &input) -> std::optional<scene_id> {
    scene_id id{ entt::hashed_string::value (input.c_str (), input.size ()) };
    if (mgr.contains (id))
      return id;
    for (auto &info : mgr.list_scenes ())
      if (info.name == input)
        return scene_id{ info.id };
    return std::nullopt;
  };

  auto find_audio = [&] (const std::string &input) -> std::optional<audio_id> {
    audio_id id{ entt::hashed_string::value (input.c_str (), input.size ()) };
    if (mgr.contains (id))
      return id;
    for (auto &info : mgr.list_audio ())
      if (info.name == input)
        return audio_id{ info.id };
    return std::nullopt;
  };

  auto find_shader
      = [&] (const std::string &input) -> std::optional<shader_id> {
    shader_id id{ entt::hashed_string::value (input.c_str (), input.size ()) };
    if (mgr.contains (id))
      return id;
    for (auto &info : mgr.list_shaders ())
      if (info.name == input)
        return shader_id{ info.id };
    return std::nullopt;
  };

  auto find_layout
      = [&] (const std::string &input) -> std::optional<ui_layout_id> {
    for (auto &info : mgr.list_ui_layouts ())
      if (info.name == input)
        return ui_layout_id{ info.id };
    return std::nullopt;
  };

  auto find_font = [&] (const std::string &input) -> std::optional<font_id> {
    for (auto &info : mgr.list_fonts ())
      if (info.name == input)
        return font_id{ info.id };
    return std::nullopt;
  };

  if (tokens.size () < 2 || tokens[1] == "ls") {
    if (tokens.size () < 3) {
      m_output << "Registered resources:\n";
      list_type_with_state ("models", [&] { return mgr.list_models (); });
      list_type_with_state ("images", [&] { return mgr.list_images (); });
      list_type_with_state ("cubemaps", [&] { return mgr.list_cubemaps (); });
      list_type_with_state ("scenes", [&] { return mgr.list_scenes (); });
      list_type_with_state ("audio", [&] { return mgr.list_audio (); });
      list_type_with_state ("layouts", [&] { return mgr.list_ui_layouts (); });
      list_type ("fonts", [&] { return mgr.list_fonts (); });
      list_type_with_state ("shaders", [&] { return mgr.list_shaders (); });
    } else {
      const std::string &type = tokens[2];
      if (type == "models" || type == "model")
        list_type_with_state ("models", [&] { return mgr.list_models (); });
      else if (type == "images" || type == "image")
        list_type_with_state ("images", [&] { return mgr.list_images (); });
      else if (type == "cubemaps" || type == "cubemap")
        list_type_with_state ("cubemaps", [&] { return mgr.list_cubemaps (); });
      else if (type == "scenes" || type == "scene")
        list_type_with_state ("scenes", [&] { return mgr.list_scenes (); });
      else if (type == "audio" || type == "audios")
        list_type_with_state ("audio", [&] { return mgr.list_audio (); });
      else if (type == "layouts" || type == "layout")
        list_type_with_state ("layouts",
                              [&] { return mgr.list_ui_layouts (); });
      else if (type == "fonts" || type == "font")
        list_type ("fonts", [&] { return mgr.list_fonts (); });
      else if (type == "shaders" || type == "shader")
        list_type_with_state ("shaders", [&] { return mgr.list_shaders (); });
      else
        m_output << "Unknown resource type: " << type << "\n";
    }
    return;
  }

  const std::string &action = tokens[1];

  // ── rsc add <type> <path> [--load] ──
  if (action == "add") {
    if (tokens.size () < 4) {
      m_output << "Usage: rsc add <type> <path> [--load]\n";
      return;
    }
    const std::string &type = tokens[2];
    const std::string &path = tokens[3];
    bool do_load = tokens.size () > 4 && tokens[4] == "--load";

    auto add_and_maybe_load
        = [&] (auto id, auto register_fn, auto load_fn, const char *label) {
            id = register_fn (path);
            m_output << "Registered " << label << ": " << path << "\n";
            if (do_load) {
              static_cast<void> (load_fn (id));
              m_output << "Loaded " << label << ": " << path << "\n";
            }
          };

    if (type == "model" || type == "models") {
      add_and_maybe_load (
          model_id{},
          [&] (const std::string &p) { return mgr.register_model (p); },
          [&] (model_id id) { static_cast<void> (mgr.load (id)); }, "model");
    } else if (type == "image" || type == "images") {
      add_and_maybe_load (
          image_id{},
          [&] (const std::string &p) { return mgr.register_image (p); },
          [&] (image_id id) { static_cast<void> (mgr.load (id)); }, "image");
    } else if (type == "cubemap" || type == "cubemaps") {
      add_and_maybe_load (
          cubemap_id{},
          [&] (const std::string &p) { return mgr.register_cubemap (p); },
          [&] (cubemap_id id) { static_cast<void> (mgr.load (id)); },
          "cubemap");
    } else if (type == "scene" || type == "scenes") {
      add_and_maybe_load (
          scene_id{},
          [&] (const std::string &p) { return mgr.register_scene (p); },
          [&] (scene_id id) { static_cast<void> (mgr.load (id)); }, "scene");
    } else if (type == "audio" || type == "audios") {
      add_and_maybe_load (
          audio_id{},
          [&] (const std::string &p) { return mgr.register_audio (p); },
          [&] (audio_id id) { static_cast<void> (mgr.load (id)); }, "audio");
    } else if (type == "font" || type == "fonts") {
      static_cast<void> (mgr.register_font (path));
      m_output << "Registered font: " << path << "\n";
    } else if (type == "shader" || type == "shaders") {
      const shader_id id = mgr.register_shader (path);
      m_output << "Registered shader: " << path << "\n";
      if (do_load) {
        static_cast<void> (mgr.load (id));
        m_output << "Loaded shader: " << path << "\n";
      }
    } else if (type == "layout" || type == "layouts") {
      static_cast<void> (mgr.register_ui_layout (path));
      m_output << "Registered UI layout: " << path << "\n";
    } else {
      m_output << "Unknown resource type: " << type << "\n";
    }
    return;
  }

  // ── rsc rm <type> <name/path> ──
  if (action == "rm") {
    if (tokens.size () < 4) {
      m_output << "Usage: rsc rm <type> <name|path>\n";
      return;
    }
    const std::string &type = tokens[2];
    const std::string &name = tokens[3];

    auto do_rm = [&] (auto find_fn, const char *label) {
      auto id = find_fn (name);
      if (id) {
        mgr.unload (*id);
        m_output << "Removed " << label << ": " << name << "\n";
      } else {
        m_output << label << " not found: " << name << "\n";
      }
    };

    if (type == "model" || type == "models")
      do_rm (find_model, "Model");
    else if (type == "image" || type == "images")
      do_rm (find_image, "Image");
    else if (type == "cubemap" || type == "cubemaps")
      do_rm (find_cubemap, "Cubemap");
    else if (type == "scene" || type == "scenes")
      do_rm (find_scene, "Scene");
    else if (type == "audio" || type == "audios")
      do_rm (find_audio, "Audio");
    else if (type == "font" || type == "fonts")
      m_output << "Fonts are metadata-only and cannot be removed.\n";
    else if (type == "shader" || type == "shaders")
      do_rm (find_shader, "Shader");
    else if (type == "layout" || type == "layouts")
      m_output << "UI layouts are metadata-only and cannot be removed.\n";
    else
      m_output << "Unknown resource type: " << type << "\n";
    return;
  }

  // ── rsc load <type> <name/path> ──
  if (action == "load") {
    if (tokens.size () < 4) {
      m_output << "Usage: rsc load <type> <name|path>\n";
      return;
    }
    const std::string &type = tokens[2];
    const std::string &name = tokens[3];

    if (type == "model" || type == "models") {
      auto id = find_model (name);
      if (id) {
        static_cast<void> (mgr.load (*id));
        m_output << "Loaded model: " << name << "\n";
      } else {
        m_output << "Model not found: " << name << "\n";
      }
    } else if (type == "image" || type == "images") {
      auto id = find_image (name);
      if (id) {
        static_cast<void> (mgr.load (*id));
        m_output << "Loaded image: " << name << "\n";
      } else {
        m_output << "Image not found: " << name << "\n";
      }
    } else if (type == "cubemap" || type == "cubemaps") {
      auto id = find_cubemap (name);
      if (id) {
        static_cast<void> (mgr.load (*id));
        m_output << "Loaded cubemap: " << name << "\n";
      } else {
        m_output << "Cubemap not found: " << name << "\n";
      }
    } else if (type == "scene" || type == "scenes") {
      auto id = find_scene (name);
      if (id) {
        static_cast<void> (mgr.load (*id));
        m_output << "Loaded scene: " << name << "\n";
      } else {
        m_output << "Scene not found: " << name << "\n";
      }
    } else if (type == "audio" || type == "audios") {
      auto id = find_audio (name);
      if (id) {
        static_cast<void> (mgr.load (*id));
        m_output << "Loaded audio: " << name << "\n";
      } else {
        m_output << "Audio not found: " << name << "\n";
      }
    } else if (type == "font" || type == "fonts") {
      m_output << "Fonts are metadata-only and cannot be loaded.\n";
    } else if (type == "shader" || type == "shaders") {
      auto id = find_shader (name);
      if (id) {
        static_cast<void> (mgr.load (*id));
        m_output << "Loaded shader: " << name << "\n";
      } else {
        m_output << "Shader not found: " << name << "\n";
      }
    } else if (type == "layout" || type == "layouts") {
      m_output << "UI layouts are metadata-only and cannot be loaded.\n";
    } else {
      m_output << "Unknown resource type: " << type << "\n";
    }
    return;
  }

  // ── rsc unload <type> <name/path> ──
  if (action == "unload") {
    if (tokens.size () < 4) {
      m_output << "Usage: rsc unload <type> <name|path>\n";
      return;
    }
    const std::string &type = tokens[2];
    const std::string &name = tokens[3];

    if (type == "model" || type == "models") {
      auto id = find_model (name);
      if (id) {
        mgr.unload (*id);
        m_output << "Unloaded model: " << name << "\n";
      } else {
        m_output << "Model not found: " << name << "\n";
      }
    } else if (type == "image" || type == "images") {
      auto id = find_image (name);
      if (id) {
        mgr.unload (*id);
        m_output << "Unloaded image: " << name << "\n";
      } else {
        m_output << "Image not found: " << name << "\n";
      }
    } else if (type == "cubemap" || type == "cubemaps") {
      auto id = find_cubemap (name);
      if (id) {
        mgr.unload (*id);
        m_output << "Unloaded cubemap: " << name << "\n";
      } else {
        m_output << "Cubemap not found: " << name << "\n";
      }
    } else if (type == "scene" || type == "scenes") {
      auto id = find_scene (name);
      if (id) {
        mgr.unload (*id);
        m_output << "Unloaded scene: " << name << "\n";
      } else {
        m_output << "Scene not found: " << name << "\n";
      }
    } else if (type == "audio" || type == "audios") {
      auto id = find_audio (name);
      if (id) {
        mgr.unload (*id);
        m_output << "Unloaded audio: " << name << "\n";
      } else {
        m_output << "Audio not found: " << name << "\n";
      }
    } else if (type == "font" || type == "fonts") {
      m_output << "Fonts are metadata-only and cannot be unloaded.\n";
    } else if (type == "shader" || type == "shaders") {
      auto id = find_shader (name);
      if (id) {
        mgr.unload (*id);
        m_output << "Unloaded shader: " << name << "\n";
      } else {
        m_output << "Shader not found: " << name << "\n";
      }
    } else if (type == "layout" || type == "layouts") {
      m_output << "UI layouts are metadata-only and cannot be unloaded.\n";
    } else {
      m_output << "Unknown resource type: " << type << "\n";
    }
    return;
  }

  // ── rsc info <type> <name/path> ──
  if (action == "info") {
    if (tokens.size () < 4) {
      m_output << "Usage: rsc info <type> <name|path>\n";
      return;
    }
    const std::string &type = tokens[2];
    const std::string &name = tokens[3];

    if (type == "model" || type == "models") {
      auto id = find_model (name);
      if (id)
        show_info ("model", *id, [&] (model_id i) { return mgr.info (i); });
      else
        m_output << "Model not found: " << name << "\n";
    } else if (type == "image" || type == "images") {
      auto id = find_image (name);
      if (id)
        show_info ("image", *id, [&] (image_id i) { return mgr.info (i); });
      else
        m_output << "Image not found: " << name << "\n";
    } else if (type == "cubemap" || type == "cubemaps") {
      auto id = find_cubemap (name);
      if (id)
        show_info ("cubemap", *id, [&] (cubemap_id i) { return mgr.info (i); });
      else
        m_output << "Cubemap not found: " << name << "\n";
    } else if (type == "scene" || type == "scenes") {
      auto id = find_scene (name);
      if (id)
        show_info ("scene", *id, [&] (scene_id i) { return mgr.info (i); });
      else
        m_output << "Scene not found: " << name << "\n";
    } else if (type == "audio" || type == "audios") {
      auto id = find_audio (name);
      if (id)
        show_info ("audio", *id, [&] (audio_id i) { return mgr.info (i); });
      else
        m_output << "Audio not found: " << name << "\n";
    } else if (type == "font" || type == "fonts") {
      auto id = find_font (name);
      if (id)
        show_info ("font", *id, [&] (font_id i) { return mgr.info (i); });
      else
        m_output << "Font not found: " << name << "\n";
    } else if (type == "shader" || type == "shaders") {
      auto id = find_shader (name);
      if (id)
        show_info ("shader", *id, [&] (shader_id i) { return mgr.info (i); });
      else
        m_output << "Shader not found: " << name << "\n";
    } else if (type == "layout" || type == "layouts") {
      auto id = find_layout (name);
      if (id)
        show_info ("layout", *id,
                   [&] (ui_layout_id i) { return mgr.info (i); });
      else
        m_output << "Layout not found: " << name << "\n";
    } else {
      m_output << "Unknown resource type: " << type << "\n";
    }
    return;
  }

  m_output << "Usage: rsc <ls|add|rm|load|unload|info> [args...]\n";
}

void
command_executor::cmd_prefab (const std::vector<std::string> &tokens)
{
  if (tokens.size () < 2) {
    m_output << "Usage: prefab <ls|save|load|instantiate> [args...]\n";
    return;
  }

  const std::string &action = tokens[1];

  if (action == "ls") {
    if (!m_current_project) {
      m_output << "No project loaded.\n";
      return;
    }
    auto assets = wsl::rsc::project_loader::scan_assets (*m_current_project);
    m_output << "Prefabs in project:\n";
    bool found_any = false;
    for (const auto &s : assets.scenes) {
      if (std::filesystem::path (s).extension () == ".prefab") {
        found_any = true;
        std::string header_name;
        try {
          std::ifstream sf (s);
          if (sf) {
            nlohmann::json j;
            sf >> j;
            if (j.contains ("header") && j["header"].contains ("scene_name"))
              header_name = j["header"]["scene_name"].get<std::string> ();
          }
        } catch (...) {
        }
        std::filesystem::path p (s);
        std::string filename = p.filename ().string ();
        if (!header_name.empty ())
          m_output << " - " << header_name << " (" << filename << ")\n";
        else
          m_output << " - " << filename << "\n";
      }
    }
    if (!found_any)
      m_output << " (none)\n";
    return;
  }

  if (action == "save") {
    auto *scene = get_active_scene ();
    if (!scene) {
      m_output << "No active scene.\n";
      return;
    }
    std::string path;
    if (tokens.size () > 2) {
      path = tokens[2];
      if (m_current_project && !std::filesystem::path (path).is_absolute ()) {
        path = (std::filesystem::path (m_current_project->root_path) / path)
                   .string ();
      }
    } else {
      if (m_current_project && !m_active_scene_source_path.empty ()
          && m_active_scene_source_path.ends_with (".prefab")) {
        // Prefer the path the prefab was loaded from.
        path = m_active_scene_source_path;
      } else if (m_current_project) {
        std::filesystem::path scenes_dir (m_current_project->root_path);
        scenes_dir /= m_current_project->scenes_path;
        std::string sname = rsc::scene_file::strip_extension (
            std::filesystem::path (scene->get_name ()).filename ().string ());
        if (sname.ends_with (".prefab")) {
          sname.resize (sname.size () - 7);
        }
        path = (scenes_dir / (sname + ".prefab")).string ();
      } else {
        std::string sname = rsc::scene_file::strip_extension (
            std::filesystem::path (scene->get_name ()).filename ().string ());
        if (sname.ends_with (".prefab")) {
          sname.resize (sname.size () - 7);
        }
        path = sname + ".prefab";
      }
    }
    if (!path.ends_with (".prefab"))
      path += ".prefab";
    std::error_code ec;
    std::filesystem::create_directories (
        std::filesystem::path (path).parent_path (), ec);
    if (m_rtc.resource_manager ().save_scene (*scene, path, true)) {
      m_output << "Prefab saved to " << path << "\n";
    } else {
      m_output << "Failed to save prefab.\n";
    }
    return;
  }

  if (action == "load") {
    if (tokens.size () < 3) {
      m_output << "Usage: prefab load <path>\n";
      return;
    }
    if (!m_current_project) {
      m_output << "Error: Load a project first.\n";
      return;
    }
    std::string load_path = tokens[2];
    if (!std::filesystem::path (load_path).is_absolute ()) {
      std::string rooted
          = (std::filesystem::path (m_current_project->root_path) / load_path)
                .string ();
      if (std::filesystem::exists (rooted))
        load_path = rooted;
    }
    if (!std::filesystem::exists (load_path)) {
      std::filesystem::path scenes_dir (m_current_project->root_path);
      scenes_dir /= m_current_project->scenes_path;
      std::string candidate = (scenes_dir / (load_path + ".prefab")).string ();
      if (std::filesystem::exists (candidate)) {
        load_path = candidate;
      } else {
        candidate = (scenes_dir / load_path).string ();
        if (std::filesystem::exists (candidate))
          load_path = candidate;
      }
    }
    if (!std::filesystem::exists (load_path)) {
      m_output << "Failed to load prefab: file not found '" << tokens[2]
               << "'.\n";
      return;
    }
    ensure_runtime (runtime_load_mode::force_full);
    std::string scene_name
        = std::filesystem::path (load_path).stem ().stem ().string ();
    try {
      auto &scene = m_rtc.scene_manager ().create_scene (scene_name, true);
      wsl::rsc::io::scene_snapshot_serializer serializer (&m_rtc, scene);
      if (serializer.load_json (load_path)) {
        m_output << "Prefab loaded from " << load_path << "\n";
        m_active_scene_source_path = load_path;
      } else {
        m_output << "Failed to load prefab from '" << load_path
                 << "' (could not open file).\n";
      }
    } catch (const std::exception &e) {
      m_output << "Failed to load prefab from '" << load_path
               << "': " << e.what () << "\n";
    } catch (...) {
      m_output << "Failed to load prefab from '" << load_path
               << "': unknown error.\n";
    }
    return;
  }

  if (action == "instantiate") {
    if (tokens.size () < 3) {
      m_output << "Usage: prefab instantiate <name|path> [parent_id]\n";
      return;
    }
    auto *scene = get_active_scene ();
    if (!scene) {
      m_output << "No active scene.\n";
      return;
    }
    auto &mgr = m_rtc.resource_manager ();
    const std::string &name_or_path = tokens[2];

    std::optional<wsl::rsc::scene_id> prefab_id;
    wsl::rsc::scene_id id{ entt::hashed_string::value (name_or_path.c_str (),
                                                       name_or_path.size ()) };
    if (mgr.contains (id)) {
      auto info = mgr.info (id);
      if (info && info->is_prefab)
        prefab_id = id;
    }
    if (!prefab_id) {
      for (auto &info : mgr.list_scenes ()) {
        if (info.is_prefab
            && (info.name == name_or_path || info.path == name_or_path)) {
          prefab_id = wsl::rsc::scene_id{ info.id };
          break;
        }
      }
    }
    if (!prefab_id && m_current_project) {
      std::filesystem::path scenes_dir (m_current_project->root_path);
      scenes_dir /= m_current_project->scenes_path;
      std::string candidate
          = (scenes_dir / (name_or_path + ".prefab")).string ();
      if (!std::filesystem::exists (candidate))
        candidate = (scenes_dir / name_or_path).string ();
      if (std::filesystem::exists (candidate)) {
        id = mgr.import_scene (candidate, true);
        if (mgr.contains (id)) {
          auto info = mgr.info (id);
          if (info && info->is_prefab)
            prefab_id = id;
        }
      }
    }
    if (!prefab_id) {
      m_output << "Prefab not found: " << name_or_path << "\n";
      return;
    }

    entt::entity parent = entt::null;
    if (tokens.size () > 3) {
      parent = resolve_entity_token (tokens[3], scene);
      if (parent == entt::null) {
        m_output << "Unknown parent entity: " << tokens[3] << "\n";
        return;
      }
    }

    static_cast<void> (mgr.instantiate_prefab (*prefab_id, parent));
    m_output << "Prefab instantiated";
    if (parent != entt::null)
      m_output << " under entity " << tokens[3];
    m_output << ".\n";
    auto_save_scene ();
    return;
  }

  m_output << "Unknown prefab action: " << action << "\n";
}

void
command_executor::cmd_script (const std::vector<std::string> &tokens)
{
  auto &module = m_rtc.runtime_project_module ();
  using wsl::reg::runtime::load_state_t;

  const std::string action = tokens.size () > 1 ? tokens[1] : std::string ();

  if (action == "status") {
    m_output << "Runtime module status:\n";
    switch (module.get_load_state ()) {
    case load_state_t::unloaded:
      m_output << "  State: unloaded\n";
      break;
    case load_state_t::metadata_cache:
      m_output << "  State: metadata-cache (placeholders only)\n";
      break;
    case load_state_t::loaded:
      m_output << "  State: loaded (full compile)\n";
      break;
    }
    if (!m_current_project) {
      m_output << "  Project: (none)\n";
    } else {
      m_output << "  Project: " << m_current_project->name << " ("
               << m_current_project->root_path << ")\n";
    }
    if (!module.loaded_project_root ().empty ()) {
      m_output << "  Loaded root: " << module.loaded_project_root ().string ()
               << "\n"
               << "  Source hash: " << module.source_hash () << "\n";
    }
    m_output << "  Registrations: " << module.das_component_count ()
             << " components, " << module.das_singleton_count ()
             << " singletons, " << module.das_system_count () << " systems\n";
    if (m_current_project) {
      m_output << "  Cache file: "
               << module
                      .registration_cache_path (
                          std::filesystem::path (m_current_project->root_path))
                      .string ()
               << "\n";
    }
    m_output << "  Last status: " << module.last_status () << "\n";
    if (!module.last_error ().empty ())
      m_output << "  Last error: " << module.last_error () << "\n";
    return;
  }

  if (action == "reload") {
    if (!m_current_project) {
      m_output << "Error: Load a project first.\n";
      return;
    }
    // Compile synchronously on the calling thread.  The CLI blocks until the
    // outcome is known anyway, and the engine's other CLI entry points
    // (command_executor::ensure_runtime) already use the synchronous
    // compile_and_load().  Going through compile_and_load_async() spun up a
    // std::async worker thread which built its own thread-local daScript
    // modules; Module_WeaselApi / Module_Ecs were therefore constructed twice
    // in one process and the second construction clobbered the global module
    // pointers, which produced glibc heap corruption ("double free or
    // corruption (!prev)", "munmap_chunk(): invalid pointer", exit 134/139)
    // during Module::shutdownInternal() at process exit.
    wsl::log::cli ()->info ("Reloading runtime scripts...");
    m_output << "Reloading runtime scripts...\n";
    if (!module.compile_and_load (*m_current_project)) {
      m_output << "Reload FAILED:\n  " << module.last_error () << "\n"
               << "  Run 'script status' for details.\n";
      return;
    }
    if (!module.finalize_load ()) {
      m_output << "Reload FAILED:\n  " << module.last_error () << "\n"
               << "  Run 'script status' for details.\n";
      return;
    }
    if (!module.has_loaded_module ()) {
      m_output << "Reload FAILED:\n  " << module.last_error () << "\n"
               << "  Run 'script status' for details.\n";
      return;
    }
    m_output << "Reload complete: " << module.das_component_count ()
             << " components, " << module.das_singleton_count ()
             << " singletons, " << module.das_system_count () << " systems.\n  "
             << module.last_status () << "\n";
    return;
  }

  if (action == "invalidate") {
    module.invalidate (m_current_project.get ());
    m_output << "Runtime state invalidated (in-memory registrations cleared,"
                " cache file deleted).\n"
                "Run 'script reload' to rebuild.\n";
    return;
  }

  m_output << "Usage: script <status|reload|invalidate>\n";
}

void
command_executor::cmd_das (const std::vector<std::string> &tokens)
{
  const auto &catalog = wsl::mcp_server::das_api_catalog ();
  const std::string name = tokens.size () > 1 ? tokens[1] : std::string ();

  if (name.empty ()) {
    m_output << "Weasel Daslang API (" << catalog.size ()
             << " functions)\n"
                "Generated from the runtime registration site "
                "(wsl_api_module.cpp).\n\n";
    std::map<std::string, std::vector<const wsl::mcp_server::das_api_entry *>>
        by_cat;
    for (const auto &e : catalog)
      by_cat[e.category].push_back (&e);
    const char *order[] = { "query", "mutation", "action" };
    for (const char *cat : order) {
      auto it = by_cat.find (cat);
      if (it == by_cat.end ())
        continue;
      m_output << cat << " (" << it->second.size () << "):\n";
      for (const auto *e : it->second) {
        m_output << "  - " << e->name;
        if (!e->args.empty ()) {
          m_output << "(";
          for (std::size_t i = 0; i < e->args.size (); ++i) {
            if (i > 0)
              m_output << ", ";
            m_output << e->args[i];
          }
          m_output << ")";
        }
        m_output << "\n";
      }
      m_output << "\n";
    }
    m_output
        << "Use 'das <name>' for a full entry (signature, side effects).\n";
    return;
  }

  const wsl::mcp_server::das_api_entry *found = nullptr;
  for (const auto &e : catalog) {
    if (e.name == name) {
      found = &e;
      break;
    }
  }
  if (found == nullptr) {
    m_output << "Unknown API function: " << name << "\n";
    return;
  }
  m_output << "Function: " << found->name << "\n"
           << "  C++ symbol: " << found->cpp << "\n"
           << "  Side effects: " << found->side_effect << " (" << found->effect
           << ")\n"
           << "  Category: " << found->category << "\n"
           << "  Signature: " << found->name << "(";
  for (std::size_t i = 0; i < found->args.size (); ++i) {
    if (i > 0)
      m_output << ", ";
    m_output << found->args[i];
  }
  m_output << ")\n";
}

void
command_executor::cmd_play (const std::vector<std::string> &tokens)
{
  auto *scene = get_active_scene ();
  if (!scene) {
    m_output << "No active scene. Load a scene first (scene load <path>).\n";
    return;
  }
  if (!scene->is_initialized ()) {
    scene->init ();
  }

  int frames = 60;
  double dt = 1.0 / 60.0;
  bool json = false;
  std::string inspect_token;
  bool has_inspect = false;

  for (std::size_t i = 1; i < tokens.size (); ++i) {
    if (tokens[i] == "--frames" && i + 1 < tokens.size ()) {
      try {
        frames = std::stoi (tokens[++i]);
      } catch (const std::exception &) {
        m_output << "Invalid --frames value.\n";
        return;
      }
    } else if (tokens[i] == "--dt" && i + 1 < tokens.size ()) {
      try {
        dt = std::stod (tokens[++i]);
      } catch (const std::exception &) {
        m_output << "Invalid --dt value.\n";
        return;
      }
    } else if (tokens[i] == "--json") {
      json = true;
    } else if (tokens[i] == "--inspect" && i + 1 < tokens.size ()) {
      inspect_token = tokens[++i];
      has_inspect = true;
    }
  }
  if (frames <= 0) {
    m_output << "Nothing to do (frames <= 0).\n";
    return;
  }

  // Headless tick: run the simulation without rendering. We tick the physics
  // and transform core systems plus any user systems attached to the scene.
  // Render/audio/UI/skybox/shadow/lighting systems are intentionally skipped
  // because they require a GPU/render-window context.
  entt::registry &registry = scene->get_registry ();
  auto &core = m_rtc.core_systems ();

  // ecs_system::update() is a no-op while the system is inactive, and the
  // headless runtime never enters "playing" state, so scene activation has to
  // be forced here. Without this `play` reported N frames simulated while none
  // of the attached systems (nor the core Transform/Physics systems) ever ran.
  auto activate_system = [&registry] (sys::ecs_system *system) {
    if (system != nullptr) {
      system->set_active (true, &registry);
    }
  };

  const auto step = [&] () {
    if (core) {
      for (sys::ecs_system *sys : core->to_vec ()) {
        if (sys == nullptr)
          continue;
        const std::string name = sys->get_name ();
        if (name == "Transform" || name == "Physics") {
          sys->update (&registry, dt);
          sys->editor_update (&registry, dt);
        }
      }
    }
    for (auto &sys : scene->systems) {
      if (sys) {
        sys->update (&registry, dt);
        sys->editor_update (&registry, dt);
      }
    }
  };

  if (core) {
    for (sys::ecs_system *sys : core->to_vec ()) {
      if (sys == nullptr)
        continue;
      const std::string name = sys->get_name ();
      if (name == "Transform" || name == "Physics") {
        activate_system (sys);
      }
    }
  }
  for (auto &sys : scene->systems) {
    activate_system (sys.get ());
  }

  for (int f = 0; f < frames; ++f) {
    step ();
  }

  // Leave the runtime in the state we found it in.
  for (auto &sys : scene->systems) {
    if (sys) {
      sys->set_active (false, &registry);
    }
  }
  if (core) {
    for (sys::ecs_system *sys : core->to_vec ()) {
      if (sys == nullptr)
        continue;
      const std::string name = sys->get_name ();
      if (name == "Transform" || name == "Physics") {
        sys->set_active (false, &registry);
      }
    }
  }

  const double simulated = frames * dt;
  m_output << "Played " << frames << " frames (" << simulated
           << "s simulated at dt=" << dt << ").\n";

  if (has_inspect) {
    entt::entity e = entt::null;
    try {
      e = static_cast<entt::entity> (std::stoul (inspect_token));
    } catch (const std::exception &) {
      // treat as a name
      for (const auto &kv : scene->get_entity_names ()) {
        if (kv.second == inspect_token) {
          e = kv.first;
          break;
        }
      }
    }
    if (e == entt::null || !registry.valid (e)) {
      m_output << "Inspect target not found: " << inspect_token << "\n";
      return;
    }

    std::string entity_name;
    for (const auto &kv : scene->get_entity_names ()) {
      if (kv.first == e) {
        entity_name = kv.second;
        break;
      }
    }

    nlohmann::json snap;
    snap["entity"] = static_cast<uint32_t> (e);
    snap["name"] = entity_name;

    if (auto *wt = registry.try_get<wsl::comp::world_transform> (e)) {
      const glm::mat4 m = wt->value ();
      glm::vec3 scale, translation;
      glm::quat rotation;
      glm::vec3 skew;
      glm::vec4 perspective;
      glm::decompose (m, scale, rotation, translation, skew, perspective);
      nlohmann::json t;
      t["x"] = translation.x;
      t["y"] = translation.y;
      t["z"] = translation.z;
      snap["position"] = t;
      nlohmann::json r;
      r["x"] = rotation.x;
      r["y"] = rotation.y;
      r["z"] = rotation.z;
      r["w"] = rotation.w;
      snap["rotation"] = r;
      nlohmann::json s;
      s["x"] = scale.x;
      s["y"] = scale.y;
      s["z"] = scale.z;
      snap["scale"] = s;
    }
    snap["has_rigid_body"] = registry.any_of<wsl::comp::rigid_body> (e);
    snap["has_transform"] = registry.any_of<wsl::comp::transform> (e);

    if (json) {
      m_output << snap.dump (2) << "\n";
    } else {
      m_output << "Entity " << static_cast<uint32_t> (e)
               << (entity_name.empty () ? "" : " (" + entity_name + ")")
               << ":\n";
      if (snap.contains ("position")) {
        m_output << "  position: (" << snap["position"]["x"].get<double> ()
                 << ", " << snap["position"]["y"].get<double> () << ", "
                 << snap["position"]["z"].get<double> () << ")\n";
        m_output << "  rotation: (" << snap["rotation"]["x"].get<double> ()
                 << ", " << snap["rotation"]["y"].get<double> () << ", "
                 << snap["rotation"]["z"].get<double> () << ", "
                 << snap["rotation"]["w"].get<double> () << ")\n";
        m_output << "  scale: (" << snap["scale"]["x"].get<double> () << ", "
                 << snap["scale"]["y"].get<double> () << ", "
                 << snap["scale"]["z"].get<double> () << ")\n";
      }
      m_output << "  has_rigid_body: "
               << (snap["has_rigid_body"].get<bool> () ? "true" : "false")
               << "\n";
    }
  }
}

void
command_executor::cmd_help ()
{
  m_output
      << "Weasel Engine REPL - Available Commands\n"
      << "======================================\n\n"
      << "Save policy:\n"
      << "  One-shot mode (no -i/-a): changes auto-save to disk after each\n"
      << "  mutation command. Interactive/attach mode: save manually via\n"
      << "  scene save / proj save.\n\n"
      << "Project:\n"
      << "  proj new <path> <name>     Create a new project at path with name\n"
      << "  proj load <path>           Load an existing project\n"
      << "  proj info                  Show current project metadata\n"
      << "  proj set <field> <value>   Set project metadata (name, author, "
         "default_scene_path)\n"
      << "  proj save                  Save current project metadata to "
         "disk\n\n"
      << "Scene:\n"
      << "  scene new <name>           Create a new empty scene and set it "
         "active\n"
      << "  scene load <path|name>     Load a .wscn scene (path or scene "
         "name)\n"
      << "  scene save [path]          Save active scene (default: project "
         "scenes dir)\n"
      << "  scene ls                   List all scene assets in the project\n"
      << "  scene status               Show active scene statistics\n\n"
      << "Entity:\n"
      << "  ent new [name]             Create a new entity (optional name)\n"
      << "  ent ls                     List all entities in the active scene\n"
      << "  ent rm <id>                Destroy entity by numeric ID\n"
      << "  ent ren <id> <name>        Rename an entity\n"
      << "  ent inspect <id>           Show entity details and its "
         "components\n\n"
      << "Component:\n"
      << "  comp avail                 List all registered component types\n"
      << "  comp ls [id]               List components on an entity, or all "
         "types\n"
      << "  comp add <id> <type>       Add a component to entity by type name\n"
      << "  comp rm <id> <type>        Remove a component from entity\n"
      << "  comp set <id> <type> <prop> <val>   Set a component property\n"
      << "  comp create <name> [--source]  Generate a world component "
         "template\n"
      << "    Type name: short name (transform), display name (Transform),\n"
      << "              or fully qualified (wsl::comp::transform)\n"
      << "    Value is JSON: numbers, strings, booleans, arrays, objects\n"
      << "    Enum names: box/sphere, Static/Kinematic/Dynamic, etc.\n"
      << "    Resource paths: builtin://cube, builtin://sphere, etc.\n"
      << "    Nested paths: motion_type.value, collision_layer.value\n"
      << "    Examples:\n"
      << "      comp set 42 transform position '[1,2,3]'\n"
      << "      comp set 42 rigid_body shape sphere\n"
      << "      comp set 42 rigid_body radius 0.5\n"
      << "      comp set 42 rigid_body motion_type.value 2\n"
      << "      comp set 42 model_instance_3d model_id builtin://cube\n\n"
      << "Singleton:\n"
      << "  singl ls                   List singleton components in the scene\n"
      << "  singl add <name>           Add a value-owned singleton to the "
         "active scene\n"
      << "  singl create <name> [--source]  Generate a singleton template\n"
      << "  singl set <name> <prop> <val>   Set a singleton property\n\n"
      << "System:\n"
      << "  sys ls                     List all systems in the active scene\n"
      << "                             (core + user-defined)\n"
      << "  sys avail                  List user-defined system types "
         "available\n"
      << "                             via 'sys add'\n"
      << "  sys add <name>             Add a user-defined system to the scene\n"
      << "                             (core engine systems are always "
         "present)\n"
      << "  sys create <name> [--source]  Generate a system template\n\n"
      << "Scripts (user Daslang runtime code):\n"
      << "  script status              Show runtime load state, registry "
         "counts,\n"
      << "                             cache path, and last error\n"
      << "  script reload              Recompile and reload runtime scripts "
         "(same as\n"
      << "                             the editor's Reload Scripts button)\n"
      << "  script invalidate          Clear in-memory state and delete the "
         "registration\n"
      << "                             cache file (use when caches confuse "
         "you)\n\n"
      << "Daslang API reference:\n"
      << "  das                         List all functions exposed to .das "
         "scripts (grouped)\n"
      << "  das <name>                 Show a function's signature and side "
         "effects\n"
      << "                             (generated from the runtime "
         "registration "
         "site)\n\n"
      << "Headless simulation:\n"
      << "  play --frames N [--dt S] [--inspect <id|name>] [--json]\n"
      << "                             Step the active scene N frames without "
         "rendering\n"
      << "                             and (optionally) snapshot an entity\n\n"
      << "Resources:\n"
      << "  rsc ls [type]              List registered resources\n"
      << "  rsc add <type> <path> [--load]  Register a resource (name = "
         "filename without extension)\n"
      << "  rsc rm <type> <name>       Remove a registered resource\n"
      << "  rsc load <type> <name>     Load a registered resource\n"
      << "  rsc unload <type> <name>   Unload a registered resource\n"
      << "  rsc info <type> <name>     Show resource metadata\n"
      << "    Types: model, image, cubemap, scene, audio, font, shader, "
         "layout\n\n"
      << "Prefab:\n"
      << "  prefab ls                  List prefab assets in the project\n"
      << "  prefab save [path]         Save active scene as a prefab\n"
      << "  prefab load <path>         Load a prefab as the active scene\n"
      << "  prefab instantiate <name> [parent_id]  Spawn prefab into active "
         "scene\n\n"
      << "Other:\n"
      << "  help                       Show this help message\n"
      << "  cls                        Clear the terminal screen\n"
      << "  exit | quit                Exit the REPL\n"
      << "  # ...                      Lines starting with # are ignored "
         "(comments)\n";
}

// -------- repl_handler implementation --------

repl_handler::repl_handler (const std::string &engine_res_path, bool attach)
    : m_engine_res_path (engine_res_path), m_attach (attach)
{
}

repl_handler::~repl_handler () = default;

void
repl_handler::set_auto_save (bool enabled)
{
  ensure_local_executor ();
  if (m_local_executor) {
    m_local_executor->set_auto_save (enabled);
  }
}

void
repl_handler::ensure_local_executor ()
{
  // Created lazily on first use, including in attach mode that fell back to
  // standalone when no editor instance was found.
  if (m_local_executor) {
    return;
  }

  m_rtc = std::make_unique<wsl::comp::singl::runtime_context> (
      "Weasel REPL", 0, 0, m_engine_res_path, true);
  m_rtc->set_editor_ctx (nullptr);
  register_repl_types (*m_rtc);
  m_local_executor = std::make_unique<command_executor> (*m_rtc);
}

bool
repl_handler::prepare (std::optional<std::string> initial_project,
                       std::optional<std::string> initial_scene)
{
  if (m_attach) {
    if (!m_editor_client.is_connected ()) {
      std::optional<std::string> target_project;

      if (initial_project) {
        std::filesystem::path proj_path (*initial_project);
        wsl::log::cli ()->info ("Initial project path: {}",
                                proj_path.string ());
        if (proj_path.filename ().string () == "wslpro.json") {
          proj_path = proj_path.parent_path ();
          wsl::log::cli ()->info ("Detected wslpro.json, using parent: {}",
                                  proj_path.string ());
        }

        target_project
            = std::filesystem::weakly_canonical (proj_path).string ();
        wsl::log::cli ()->info ("Attaching to project at: {}", *target_project);
      } else {
        wsl::log::cli ()->info (
            "No --project given, looking for a running editor instance...");
      }

      if (!m_editor_client.connect (target_project)) {
        // Graceful degradation: attach is best-effort. Warn and continue
        // standalone so commands still run against a local runtime context.
        if (target_project) {
          wsl::log::cli ()->warn (
              "No running editor found for project '{}' — continuing "
              "standalone",
              *target_project);
        } else {
          wsl::log::cli ()->warn (
              "No running editor instance found — continuing standalone");
        }
      } else {
        wsl::log::cli ()->info ("Connected to editor server for project: {}",
                                m_editor_client.connected_project ());
      }
    }

    if (m_editor_client.is_connected ()) {
      if (initial_scene) {
        auto response = m_editor_client.execute_command (
            build_repl_command ({ "scene", "load", *initial_scene }));
        if (!response || command_failed (*response)) {
          if (response && !response->empty ()) {
            wsl::log::cli ()->error ("{}", *response);
            if (response->back () != '\n') {
              std::cerr << '\n';
            }
          }
          return false;
        }
      }

      return true;
    }
    // Not connected: fall through to the standalone path below.
  }

  ensure_local_executor ();
  if (initial_project) {
    std::filesystem::path proj_path (*initial_project);
    std::filesystem::path manifest_path = proj_path;
    // If user passed a project root directory, look for the manifest file
    // inside it
    if (std::filesystem::is_directory (proj_path)) {
      manifest_path = proj_path / wsl::rsc::project_loader::manifest_file;
    }
    if (!std::filesystem::exists (manifest_path)) {
      std::cerr << "Failed to find project manifest: "
                << manifest_path.string () << "\n";
      return false;
    }

    std::string const output = m_local_executor->execute (
        build_repl_command ({ "proj", "load", manifest_path.string () }));
    if (command_failed (output)) {
      std::cerr << output;
      return false;
    }

    // If no scene was explicitly requested, attempt to load the project's
    // default_scene_path (if present). This makes one-shot CLI commands that
    // provide --project behave on the project's default scene.
    if (!initial_scene) {
      auto proj_ptr = wsl::rsc::project_loader::load (manifest_path.string ());
      if (proj_ptr && !proj_ptr->default_scene_path.empty ()) {
        std::string const ds_output
            = m_local_executor->execute (build_repl_command (
                { "scene", "load", proj_ptr->default_scene_path }));
        if (command_failed (ds_output)) {
          std::cerr << ds_output;
          return false;
        }
      }
    }
  }
  if (initial_scene) {
    std::string const output = m_local_executor->execute (
        build_repl_command ({ "scene", "load", *initial_scene }));
    if (command_failed (output)) {
      std::cerr << output;
      return false;
    }
  }

  return true;
}

void
repl_handler::run (std::optional<std::string> initial_project,
                   std::optional<std::string> initial_scene)
{
  wsl::log::cli ()->trace ("Run called, m_attach={}", m_attach);
  if (!prepare (initial_project, initial_scene)) {
    return;
  }

  std::string line;
  wsl::log::cli ()->info ("Weasel Engine REPL\nType 'help' for commands.");
  while (m_running && std::cout << "wsl> " && std::getline (std::cin, line)) {
    if (line.empty ())
      continue;
    execute_command (line);
  }
}

static bool
is_exit_command (const std::string &line)
{
  // Tokenize the first word to detect exit/quit
  std::size_t end = 0;
  while (end < line.size ()
         && !std::isspace (static_cast<unsigned char> (line[end])))
    ++end;
  std::string first = line.substr (0, end);
  return first == "exit" || first == "quit";
}

int
repl_handler::execute_command (const std::string &line)
{
  // Check for exit/quit before doing anything else
  if (is_exit_command (line)) {
    m_running = false;
    return 0;
  }

  // If attached to editor server, forward command remotely
  if (m_attach && m_editor_client.is_connected ()) {
    auto response = m_editor_client.execute_command (line);
    if (response) {
      wsl::log::cli ()->info ("{}", *response);
    } else {
      wsl::log::cli ()->error ("Lost connection to editor server");
      m_running = false;
    }
    return 0;
  }

  // Otherwise, execute locally
  ensure_local_executor ();
  if (m_local_executor) {
    std::string output = m_local_executor->execute (line);
    std::cout << output;
    return m_local_executor->exit_code ();
  }
  return 0;
}

} // namespace wsl::cli
