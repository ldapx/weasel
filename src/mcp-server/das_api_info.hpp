#pragma once

#include <agentsdk/common/result.hpp>
#include <agentsdk/mcp/mcp_types.hpp>

#include <string>

namespace wsl::mcp_server
{

namespace mcp = agentsdk::mcp;

// MCP tool: describe the Daslang API exposed by Module_WeaselApi.
// No `name` -> grouped function listing; `name` -> full entry.
agentsdk::result<mcp::tool_result, mcp::mcp_error>
handle_describe_das_api (const std::string &args_json);

} // namespace wsl::mcp_server
