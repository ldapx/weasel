#include "mcp_server_app.hpp"

#include "wsl/log/log.hpp"

#include <CLI/CLI.hpp>
#include <cstdlib>
#include <iostream>
#include <spdlog/spdlog.h>

int
main (int argc, char **argv)
{
  CLI::App app{ "Weasel Engine MCP Server" };

  // Historical HTTP options are kept for compatibility but ignored: the
  // server now speaks stdio only via agentsdk::mcp::mcp_server.
  std::string host = "localhost";
  int port = 8080;
  bool stdio_mode = true;
  app.add_option ("--host", host, "Deprecated: ignored (stdio only)");
  app.add_option ("--port", port, "Deprecated: ignored (stdio only)");
  app.add_flag ("--stdio", stdio_mode, "Use stdio transport (default, only)");

  try {
    app.parse (argc, argv);
  } catch (const CLI::ParseError &e) {
    return app.exit (e);
  }

  // Initialise the engine logging system so that engine code (e.g. component
  // registry registration) does not crash on a null logger pointer.
  // Suppress all output so that log messages do not corrupt the JSON-RPC
  // protocol carried over stdout.
  wsl::log::init ();
  spdlog::set_level (spdlog::level::off);

  wsl::mcp_server::mcp_server_app server;
  server.run ();

  return 0;
}
