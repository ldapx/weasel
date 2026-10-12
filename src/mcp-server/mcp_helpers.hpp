#pragma once

#include <agentsdk/mcp/mcp_json.hpp>
#include <agentsdk/mcp/mcp_types.hpp>

#include <optional>
#include <string>

namespace wsl::mcp_server
{

namespace mcp = agentsdk::mcp;

/** Build a single-text-block tool result. */
inline mcp::tool_result
text_result (const std::string &text, bool is_error = false)
{
  mcp::tool_result r;
  mcp::text_content t;
  t.text = text;
  r.content.push_back (mcp::content_block{ t });
  r.is_error = is_error;
  return r;
}

/** Missing/invalid parameter error. */
inline mcp::mcp_error
param_error (const std::string &message)
{
  return mcp::mcp_error{ mcp::INVALID_PARAMS, message, std::nullopt };
}

/** Extract a required string field from an arguments JSON object. */
inline bool
get_string_arg (const std::string &args_json, const std::string &field,
                std::string &out, std::string &error)
{
  auto parsed = mcp::parse_json (args_json.empty () ? "{}" : args_json);
  if (!parsed) {
    error = "Invalid arguments JSON";
    return false;
  }
  auto v = parsed.value ()[field].get_string ();
  if (v.error ()) {
    error = "Missing '" + field + "' string parameter";
    return false;
  }
  out = std::string (v.value ());
  return true;
}

/** Extract an optional string field; returns true always (out empty if absent).
 */
inline bool
get_optional_string_arg (const std::string &args_json, const std::string &field,
                         std::string &out)
{
  out.clear ();
  auto parsed = mcp::parse_json (args_json.empty () ? "{}" : args_json);
  if (!parsed) {
    return true;
  }
  auto v = parsed.value ()[field].get_string ();
  if (v.error ()) {
    return true;
  }
  out = std::string (v.value ());
  return true;
}

/** Extract an optional boolean field with a default. */
inline bool
get_bool_arg (const std::string &args_json, const std::string &field, bool dflt)
{
  auto parsed = mcp::parse_json (args_json.empty () ? "{}" : args_json);
  if (!parsed) {
    return dflt;
  }
  auto v = parsed.value ()[field].get_bool ();
  if (v.error ()) {
    return dflt;
  }
  return v.value ();
}

/** Extract an optional integer field with a default. */
inline int
get_int_arg (const std::string &args_json, const std::string &field, int dflt)
{
  auto parsed = mcp::parse_json (args_json.empty () ? "{}" : args_json);
  if (!parsed) {
    return dflt;
  }
  auto v = parsed.value ()[field].get_int64 ();
  if (v.error ()) {
    return dflt;
  }
  return static_cast<int> (v.value ());
}

/** True when the parsed args object contains the field. */
inline bool
has_arg (const std::string &args_json, const std::string &field)
{
  auto parsed = mcp::parse_json (args_json.empty () ? "{}" : args_json);
  if (!parsed) {
    return false;
  }
  return !parsed.value ()[field].error ();
}

} // namespace wsl::mcp_server
