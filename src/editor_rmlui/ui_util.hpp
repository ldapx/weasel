#pragma once

// Shared formatting and parsing helpers for the RmlUi editor layers.
// Header-only: every function is inline so model, view and controller can
// share them without a dedicated translation unit.

#include "editor_ui.hpp"

#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>
#include <string_view>

#include <entt/entt.hpp>

#include <filesystem>

#include "wsl/math/vector.hpp"

namespace editor_rml
{

/** File interface resolving RmlUi paths against a base directory. */
inline std::string
editor_document_root (const editor_ui::services &svc)
{
  if (!svc.source_root.empty ()) {
    const std::string source_dir = svc.source_root + "/rsc/editor_rml";
    std::error_code ec;
    if (std::filesystem::exists (source_dir + "/editor.rml", ec)) {
      return source_dir;
    }
  }
  return svc.asset_root + "/editor_rml";
}

inline std::string
html_escape (const std::string &text)
{
  std::string out;
  out.reserve (text.size () + 16);
  for (const char c : text) {
    switch (c) {
    case '&':
      out += "&amp;";
      break;
    case '<':
      out += "&lt;";
      break;
    case '>':
      out += "&gt;";
      break;
    case '"':
      out += "&quot;";
      break;
    case '\'':
      out += "&#39;";
      break;
    default:
      out += c;
      break;
    }
  }
  return out;
}

/** Editor trait convention mirrored from the ImGui inspector. */
enum class meta_editor_trait : std::uint16_t
{
  hidden = 1 << 1
};

inline bool
field_hidden (const entt::meta_data &data)
{
  const auto flags
      = static_cast<std::uint16_t> (data.traits<meta_editor_trait> ());
  return (flags & static_cast<std::uint16_t> (meta_editor_trait::hidden)) != 0;
}

inline std::string
format_vec (const float *values, int count)
{
  char buffer[160];
  int written = 0;
  for (int i = 0; i < count; ++i) {
    const int n = std::snprintf (
        buffer + written, sizeof (buffer) - static_cast<size_t> (written),
        i == 0 ? "%.3f" : ", %.3f", static_cast<double> (values[i]));
    if (n <= 0) {
      break;
    }
    written += n;
  }
  return buffer;
}

inline std::string
format_meta_value (entt::meta_any value)
{
  if (!value) {
    return "-";
  }

  const entt::meta_type type = value.type ();

  if (type == entt::resolve<bool> ()) {
    return (*value.try_cast<bool> ()) ? "true" : "false";
  }
  if (type == entt::resolve<int> ()) {
    return std::to_string (*value.try_cast<int> ());
  }
  if (type == entt::resolve<std::uint32_t> ()) {
    return std::to_string (*value.try_cast<std::uint32_t> ());
  }
  if (type == entt::resolve<float> ()) {
    char buffer[32];
    std::snprintf (buffer, sizeof (buffer), "%.3f",
                   static_cast<double> (*value.try_cast<float> ()));
    return buffer;
  }
  if (type == entt::resolve<double> ()) {
    char buffer[32];
    std::snprintf (buffer, sizeof (buffer), "%.3f", *value.try_cast<double> ());
    return buffer;
  }
  if (type == entt::resolve<std::string> ()) {
    return *value.try_cast<std::string> ();
  }
  if (type == entt::resolve<entt::entity> ()) {
    return std::to_string (
        static_cast<std::uint32_t> (*value.try_cast<entt::entity> ()));
  }
  if (type == entt::resolve<wsl::math::vec2f> ()) {
    const auto *v = value.try_cast<wsl::math::vec2f> ();
    const float f[2]{ v->x (), v->y () };
    return format_vec (f, 2);
  }
  if (type == entt::resolve<wsl::math::vec3f> ()) {
    const auto *v = value.try_cast<wsl::math::vec3f> ();
    const float f[3]{ v->x (), v->y (), v->z () };
    return format_vec (f, 3);
  }
  if (type == entt::resolve<wsl::math::vec4f> ()) {
    const auto *v = value.try_cast<wsl::math::vec4f> ();
    const float f[4]{ v->x (), v->y (), v->z (), v->w () };
    return format_vec (f, 4);
  }
  if (type == entt::resolve<wsl::math::quatf> ()) {
    const auto *v = value.try_cast<wsl::math::quatf> ();
    const float f[4]{ v->x (), v->y (), v->z (), v->w () };
    return format_vec (f, 4);
  }

  if (type) {
    std::string_view name = type.info ().name ();
    if (const std::size_t pos = name.rfind ("::");
        pos != std::string_view::npos) {
      name = name.substr (pos + 2);
    }
    if (!name.empty ()) {
      return std::string ("<") + std::string (name) + ">";
    }
  }
  return "<value>";
}

/**
 * Returns ``true`` when a reflected value type gets an editor widget (bool
 * toggle, text input) instead of read-only text.
 */
inline bool
field_kind_editable (const entt::meta_type &type)
{
  return type == entt::resolve<bool> () || type == entt::resolve<int> ()
         || type == entt::resolve<std::uint32_t> ()
         || type == entt::resolve<float> () || type == entt::resolve<double> ()
         || type == entt::resolve<std::string> ()
         || type == entt::resolve<entt::entity> ()
         || type == entt::resolve<wsl::math::vec2f> ()
         || type == entt::resolve<wsl::math::vec3f> ()
         || type == entt::resolve<wsl::math::vec4f> ()
         || type == entt::resolve<wsl::math::quatf> ();
}

/**
 * Parses up to *expect* comma/space-separated floats from *text*.
 * Returns ``true`` only when exactly *expect* values were found.
 */
inline bool
parse_float_list (const std::string &text, int expect, float *out)
{
  std::string scratch = text;
  for (char &c : scratch) {
    if (c == ',') {
      c = ' ';
    }
  }
  std::istringstream stream (scratch);
  int count = 0;
  float value = 0.0F;
  while (count < expect && (stream >> value)) {
    out[count++] = value;
  }
  std::string rest;
  return count == expect && !(stream >> rest);
}

/** Parses one scalar of arithmetic type; returns false on any trailing junk. */
template <typename T>
inline bool
parse_scalar (const std::string &text, T &out)
{
  std::istringstream stream (text);
  stream >> out;
  if (stream.fail ()) {
    return false;
  }
  std::string rest;
  return !(stream >> rest);
}

} // namespace editor_rml
