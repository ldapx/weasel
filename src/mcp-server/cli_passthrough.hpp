#pragma once

#include <agentsdk/common/result.hpp>
#include <agentsdk/mcp/mcp_types.hpp>

#include <string>
#include <vector>

namespace wsl::mcp_server
{

namespace mcp = agentsdk::mcp;

/**
 * Resolves the weasel-cli executable used for command passthrough.
 *
 * Order: WEASEL_CLI_PATH environment variable, a "weasel-cli" binary next to
 * this server's own executable, then bare "weasel-cli" (PATH lookup).
 */
std::string resolve_weasel_cli ();

/** Outcome of running a weasel-cli command in a child process. */
struct cli_result
{
  std::vector<std::string> command;
  int exit_code = -1;
  std::string stdout_data;
  std::string stderr_data;
  bool timed_out = false;
  std::string cli_path;
};

/**
 * Runs a weasel-cli command in a child process and returns its outcome.
 *
 * The command string is tokenized with shell-style quoting rules but is NOT
 * passed through a shell, so no shell metacharacters are interpreted.
 */
cli_result execute_cli_command (const std::string &command, bool attach,
                                const std::string &project,
                                int timeout_seconds);

/** MCP handler for the ``script_reload`` tool (no confirmation needed). */
agentsdk::result<mcp::tool_result, mcp::mcp_error>
handle_script_reload (const std::string &args_json);

/**
 * MCP handler for the ``run_cli_command`` passthrough tool.
 *
 * Requires ``confirm == true`` so callers state intent before mutating.
 */
agentsdk::result<mcp::tool_result, mcp::mcp_error>
handle_run_cli_command (const std::string &args_json);

/**
 * Structured mutation tools (Phase 4 / E3 tier 2). Each maps to a single
 * weasel-cli command with validated, typed inputs instead of a free-form
 * command line, and each requires ``confirm == true``.
 */
agentsdk::result<mcp::tool_result, mcp::mcp_error>
handle_scene_save (const std::string &args_json);
agentsdk::result<mcp::tool_result, mcp::mcp_error>
handle_entity_add (const std::string &args_json);
agentsdk::result<mcp::tool_result, mcp::mcp_error>
handle_entity_remove (const std::string &args_json);
agentsdk::result<mcp::tool_result, mcp::mcp_error>
handle_component_set (const std::string &args_json);

} // namespace wsl::mcp_server
