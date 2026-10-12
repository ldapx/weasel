#include "cli_passthrough.hpp"
#include "mcp_helpers.hpp"

#include <algorithm>
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_process.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_timer.h>
#ifdef _WIN32
// Kept for GetModuleFileNameA in resolve_weasel_cli; process and pipe
// handling below is SDL-only on every platform.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <chrono>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace wsl::mcp_server
{

namespace
{

// Shell-style tokenizer: whitespace splits, quotes group, backslash escapes.
std::vector<std::string>
tokenize_command (const std::string &line)
{
  std::vector<std::string> tokens;
  std::string current;
  bool in_single = false;
  bool in_double = false;
  bool escape_next = false;

  for (char ch : line) {
    if (escape_next) {
      current.push_back (ch);
      escape_next = false;
      continue;
    }
    if (ch == '\\' && !in_single) {
      escape_next = true;
      continue;
    }
    if (ch == '\'' && !in_double) {
      in_single = !in_single;
      continue;
    }
    if (ch == '"' && !in_single) {
      in_double = !in_double;
      continue;
    }
    if (std::isspace (static_cast<unsigned char> (ch)) && !in_single
        && !in_double) {
      if (!current.empty ()) {
        tokens.push_back (current);
        current.clear ();
      }
      continue;
    }
    current.push_back (ch);
  }
  if (!current.empty ())
    tokens.push_back (current);
  return tokens;
}

// Wrap a value in double quotes when it contains whitespace so the downstream
// command tokenizer keeps it as a single argument. Empty strings are left
// empty (the caller decides whether to emit the token at all).
std::string
quote_if_needed (const std::string &value)
{
  if (value.empty ())
    return value;
  for (char c : value) {
    if (std::isspace (static_cast<unsigned char> (c)))
      return "\"" + value + "\"";
  }
  return value;
}

std::string
format_cli_result (const cli_result &r, bool success)
{
  std::ostringstream oss;
  oss << "command:";
  for (const auto &a : r.command) {
    oss << " " << a;
  }
  oss << "\nexit_code: " << r.exit_code << "\n";
  oss << "timed_out: " << (r.timed_out ? "true" : "false") << "\n";
  oss << "success: " << (success ? "true" : "false") << "\n";
  oss << "--- stdout ---\n" << r.stdout_data << "\n";
  oss << "--- stderr ---\n" << r.stderr_data << "\n";
  return oss.str ();
}

// Shared gate for the structured mutation tools: require confirm=true.
bool
require_confirm (const std::string &args_json, mcp::mcp_error &error)
{
  if (!get_bool_arg (args_json, "confirm", false)) {
    error = param_error (
        "This tool mutates engine state: pass confirm=true to proceed");
    return false;
  }
  return true;
}

// Resolve the common attach/project/timeout options used by every tool.
bool
resolve_attach (const std::string &args_json)
{
  if (!has_arg (args_json, "attach")) {
    return true;
  }
  return get_bool_arg (args_json, "attach", true);
}

std::string
resolve_project (const std::string &args_json)
{
  std::string project;
  get_optional_string_arg (args_json, "project", project);
  return project;
}

int
resolve_timeout (const std::string &args_json)
{
  return get_int_arg (args_json, "timeout_seconds", 120);
}

} // namespace

std::string
resolve_weasel_cli ()
{
  if (const char *env = std::getenv ("WEASEL_CLI_PATH")) {
    if (*env != '\0')
      return env;
  }

  std::error_code ec;
#ifdef _WIN32
  // Sibling weasel-cli.exe next to this binary.
  char self_buf[MAX_PATH];
  const DWORD self_len = GetModuleFileNameA (nullptr, self_buf, MAX_PATH);
  if (self_len != 0 && self_len < MAX_PATH) {
    const std::filesystem::path candidate
        = std::filesystem::path (std::string (self_buf, self_len))
              .parent_path ()
          / "weasel-cli.exe";
    if (std::filesystem::exists (candidate, ec))
      return candidate.string ();
  }
#else
  std::filesystem::path self
      = std::filesystem::read_symlink ("/proc/self/exe", ec);
  if (!ec) {
    const std::filesystem::path candidate = self.parent_path () / "weasel-cli";
    if (std::filesystem::exists (candidate, ec))
      return candidate.string ();
  }
#endif

  return "weasel-cli";
}

// Run weasel-cli and capture its output. Single implementation for every
// platform on top of SDL processes: stdout/stderr travel on separate pipes
// (preserved independently in the result, as with fork), stdin is inherited,
// and the deadline loop mirrors the old poll() version.
static cli_result
run_sdl (const std::vector<std::string> &full_argv, int timeout_seconds,
         const std::string &cli_path)
{
  // argv for SDL (null-terminated array of pointers into full_argv, which
  // outlives the call; SDL copies what it needs during creation).
  std::vector<const char *> argv;
  argv.reserve (full_argv.size () + 1);
  for (const std::string &arg : full_argv) {
    argv.push_back (arg.c_str ());
  }
  argv.push_back (nullptr);

  SDL_PropertiesID props = SDL_CreateProperties ();
  SDL_SetPointerProperty (props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER,
                          argv.data ());
  SDL_SetNumberProperty (props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER,
                         SDL_PROCESS_STDIO_INHERITED);
  SDL_SetNumberProperty (props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,
                         SDL_PROCESS_STDIO_APP);
  SDL_SetNumberProperty (props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER,
                         SDL_PROCESS_STDIO_APP);
  SDL_Process *proc = SDL_CreateProcessWithProperties (props);
  SDL_DestroyProperties (props);
  if (!proc) {
    throw std::runtime_error (std::string ("SDL_CreateProcess failed: ")
                              + SDL_GetError ());
  }
  SDL_PropertiesID pprops = SDL_GetProcessProperties (proc);
  SDL_IOStream *out = (SDL_IOStream *)SDL_GetPointerProperty (
      pprops, SDL_PROP_PROCESS_STDOUT_POINTER, nullptr);
  SDL_IOStream *err = (SDL_IOStream *)SDL_GetPointerProperty (
      pprops, SDL_PROP_PROCESS_STDERR_POINTER, nullptr);
  if (!out || !err) {
    SDL_DestroyProcess (proc);
    throw std::runtime_error ("SDL process missing stdio streams");
  }

  const auto deadline = std::chrono::steady_clock::now ()
                        + std::chrono::seconds (timeout_seconds);
  bool timed_out = false;
  bool out_open = true, err_open = true;
  std::string stdout_data;
  std::string stderr_data;

  // Pump one stream; returns true on progress. Mirrors the old per-fd
  // handling: data appends, EOF/error closes that side, NOT_READY waits.
  auto pump = [&] (SDL_IOStream *s, std::string &into, bool &open) -> bool {
    if (!open) {
      return false;
    }
    char buffer[4096];
    const size_t n = SDL_ReadIO (s, buffer, sizeof (buffer));
    if (n > 0) {
      into.append (buffer, n);
      return true;
    }
    const SDL_IOStatus st = SDL_GetIOStatus (s);
    if (st == SDL_IO_STATUS_EOF || st == SDL_IO_STATUS_ERROR) {
      open = false;
    }
    return false;
  };

  while (out_open || err_open) {
    if (std::chrono::steady_clock::now () > deadline) {
      timed_out = true;
      SDL_KillProcess (proc, true);
      break;
    }
    bool progress = pump (out, stdout_data, out_open);
    progress = pump (err, stderr_data, err_open) || progress;
    if (!progress) {
      SDL_Delay (1);
    }
  }

  int exit_code = -1;
  SDL_WaitProcess (proc, true, &exit_code);
  // SDL reports fatal signals negated; match the old 128+signal mapping.
  if (exit_code < 0) {
    exit_code = 128 - exit_code;
  }
  SDL_CloseIO (out);
  SDL_CloseIO (err);
  SDL_DestroyProcess (proc);

  cli_result result;
  result.command = full_argv;
  result.exit_code = exit_code;
  result.stdout_data = std::move (stdout_data);
  result.stderr_data = std::move (stderr_data);
  result.timed_out = timed_out;
  result.cli_path = cli_path;
  return result;
}

cli_result
execute_cli_command (const std::string &command, bool attach,
                     const std::string &project, int timeout_seconds)
{
  std::vector<std::string> argv = tokenize_command (command);
  if (!argv.empty () && argv[0] == "weasel-cli")
    argv.erase (argv.begin ());
  if (argv.empty ()) {
    throw std::invalid_argument ("Missing command to run");
  }
  if (timeout_seconds <= 0)
    timeout_seconds = 120;
  timeout_seconds = std::min (timeout_seconds, 600);

  std::string cli_path = resolve_weasel_cli ();
  std::vector<std::string> full_argv;
  full_argv.push_back (cli_path);
  if (attach)
    full_argv.push_back ("-a");
  if (!project.empty ()) {
    full_argv.push_back ("--project");
    full_argv.push_back (project);
  }
  for (const std::string &arg : argv)
    full_argv.push_back (arg);

  return run_sdl (full_argv, timeout_seconds, cli_path);
}

agentsdk::result<mcp::tool_result, mcp::mcp_error>
handle_script_reload (const std::string & /*args_json*/)
{
  cli_result r;
  try {
    r = execute_cli_command ("script reload", true, "", 300);
  } catch (const std::exception &e) {
    mcp::tool_result tr
        = text_result (std::string ("script reload failed: ") + e.what ());
    tr.is_error = true;
    return tr;
  }

  bool success = r.exit_code == 0 && !r.timed_out;
  mcp::tool_result tr = text_result (format_cli_result (r, success));
  tr.is_error = !success;
  return tr;
}

agentsdk::result<mcp::tool_result, mcp::mcp_error>
handle_run_cli_command (const std::string &args_json)
{
  std::string command;
  std::string error;
  if (!get_string_arg (args_json, "command", command, error)) {
    return param_error (error);
  }
  mcp::mcp_error confirm_error{ 0, "", std::nullopt };
  if (!require_confirm (args_json, confirm_error)) {
    return confirm_error;
  }

  const bool attach = resolve_attach (args_json);
  const std::string project = resolve_project (args_json);
  int timeout = resolve_timeout (args_json);

  cli_result r;
  try {
    r = execute_cli_command (command, attach, project, timeout);
  } catch (const std::exception &e) {
    mcp::tool_result tr
        = text_result (std::string ("run_cli_command failed: ") + e.what ());
    tr.is_error = true;
    return tr;
  }
  bool success = r.exit_code == 0 && !r.timed_out;
  mcp::tool_result tr = text_result (format_cli_result (r, success));
  tr.is_error = !success;
  return tr;
}

agentsdk::result<mcp::tool_result, mcp::mcp_error>
handle_scene_save (const std::string &args_json)
{
  mcp::mcp_error confirm_error{ 0, "", std::nullopt };
  if (!require_confirm (args_json, confirm_error)) {
    return confirm_error;
  }
  if (has_arg (args_json, "path")) {
    std::string path;
    std::string error;
    if (!get_string_arg (args_json, "path", path, error)) {
      return param_error ("'path' must be a string");
    }
    if (path.empty ()) {
      return param_error ("'path' must not be empty");
    }
  }

  std::string command = "scene save";
  std::string path;
  get_optional_string_arg (args_json, "path", path);
  if (!path.empty ()) {
    command += " " + quote_if_needed (path);
  }
  if (get_bool_arg (args_json, "as_default", false)) {
    command += " --as-default";
  }

  cli_result r;
  try {
    r = execute_cli_command (command, resolve_attach (args_json),
                             resolve_project (args_json),
                             resolve_timeout (args_json));
  } catch (const std::exception &e) {
    mcp::tool_result tr
        = text_result (std::string ("scene save failed: ") + e.what ());
    tr.is_error = true;
    return tr;
  }
  bool success = r.exit_code == 0 && !r.timed_out;
  mcp::tool_result tr = text_result (format_cli_result (r, success));
  tr.is_error = !success;
  return tr;
}

agentsdk::result<mcp::tool_result, mcp::mcp_error>
handle_entity_add (const std::string &args_json)
{
  mcp::mcp_error confirm_error{ 0, "", std::nullopt };
  if (!require_confirm (args_json, confirm_error)) {
    return confirm_error;
  }
  std::string name;
  std::string error;
  if (!get_string_arg (args_json, "name", name, error) || name.empty ()) {
    return param_error ("Missing 'name' string parameter");
  }

  std::string command = "ent new";
  if (get_bool_arg (args_json, "empty", false)) {
    command += " --empty";
  }
  command += " " + quote_if_needed (name);

  cli_result r;
  try {
    r = execute_cli_command (command, resolve_attach (args_json),
                             resolve_project (args_json),
                             resolve_timeout (args_json));
  } catch (const std::exception &e) {
    mcp::tool_result tr
        = text_result (std::string ("entity add failed: ") + e.what ());
    tr.is_error = true;
    return tr;
  }
  bool success = r.exit_code == 0 && !r.timed_out;
  mcp::tool_result tr = text_result (format_cli_result (r, success));
  tr.is_error = !success;
  return tr;
}

agentsdk::result<mcp::tool_result, mcp::mcp_error>
handle_entity_remove (const std::string &args_json)
{
  mcp::mcp_error confirm_error{ 0, "", std::nullopt };
  if (!require_confirm (args_json, confirm_error)) {
    return confirm_error;
  }
  std::string id;
  std::string error;
  if (!get_string_arg (args_json, "id", id, error) || id.empty ()) {
    return param_error ("Missing 'id' string parameter (entity id or name)");
  }

  const std::string command = "ent rm " + quote_if_needed (id);

  cli_result r;
  try {
    r = execute_cli_command (command, resolve_attach (args_json),
                             resolve_project (args_json),
                             resolve_timeout (args_json));
  } catch (const std::exception &e) {
    mcp::tool_result tr
        = text_result (std::string ("entity remove failed: ") + e.what ());
    tr.is_error = true;
    return tr;
  }
  bool success = r.exit_code == 0 && !r.timed_out;
  mcp::tool_result tr = text_result (format_cli_result (r, success));
  tr.is_error = !success;
  return tr;
}

agentsdk::result<mcp::tool_result, mcp::mcp_error>
handle_component_set (const std::string &args_json)
{
  mcp::mcp_error confirm_error{ 0, "", std::nullopt };
  if (!require_confirm (args_json, confirm_error)) {
    return confirm_error;
  }
  for (const char *field : { "entity", "component", "property", "value" }) {
    std::string value;
    std::string error;
    if (!get_string_arg (args_json, field, value, error) || value.empty ()) {
      return param_error (std::string ("Missing '") + field
                          + "' string parameter");
    }
  }

  std::string entity;
  std::string component;
  std::string property;
  std::string value;
  std::string dummy;
  get_string_arg (args_json, "entity", entity, dummy);
  get_string_arg (args_json, "component", component, dummy);
  get_string_arg (args_json, "property", property, dummy);
  get_string_arg (args_json, "value", value, dummy);

  const std::string command = "comp set " + quote_if_needed (entity) + " "
                              + quote_if_needed (component) + " "
                              + quote_if_needed (property) + " "
                              + quote_if_needed (value);

  cli_result r;
  try {
    r = execute_cli_command (command, resolve_attach (args_json),
                             resolve_project (args_json),
                             resolve_timeout (args_json));
  } catch (const std::exception &e) {
    mcp::tool_result tr
        = text_result (std::string ("component set failed: ") + e.what ());
    tr.is_error = true;
    return tr;
  }
  bool success = r.exit_code == 0 && !r.timed_out;
  mcp::tool_result tr = text_result (format_cli_result (r, success));
  tr.is_error = !success;
  return tr;
}

} // namespace wsl::mcp_server
