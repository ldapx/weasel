#pragma once

#include <agentsdk/mcp/mcp_server.hpp>

#include <string>

namespace wsl::mcp_server
{

/** Encapsulates the MCP server that exposes weasel-cli documentation. */
class mcp_server_app
{
public:
  /** Constructs the stdio MCP server. */
  mcp_server_app ();

  /** Starts the server and blocks until stdin closes. */
  void run ();

private:
  void register_tools ();

  agentsdk::mcp::mcp_server m_server;
};

} // namespace wsl::mcp_server
