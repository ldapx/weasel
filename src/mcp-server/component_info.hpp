#pragma once

#include <agentsdk/common/result.hpp>
#include <agentsdk/mcp/mcp_types.hpp>

#include <string>

namespace wsl::mcp_server
{

namespace mcp = agentsdk::mcp;

agentsdk::result<mcp::tool_result, mcp::mcp_error>
handle_list_components (const std::string &args_json);
agentsdk::result<mcp::tool_result, mcp::mcp_error>
handle_describe_component (const std::string &args_json);

} // namespace wsl::mcp_server
