#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "mcp-server/cli_reference.hpp"
#include "mcp-server/component_info.hpp"
#include "mcp-server/das_api_info.hpp"
#include "mcp-server/mcp_server_handlers.hpp"
#include "mcp-server/namespace_info.hpp"

#include "wsl/log/log.hpp"

#include <agentsdk/mcp/mcp_tool_builder.hpp>

#include <string>
#include <sstream>

#include <fstream>
#include <filesystem>
#include <regex>
#include <set>
#include <variant>

using namespace wsl::mcp_server;
namespace mcp = agentsdk::mcp;

// Component handlers touch the engine registries, which log through
// wsl::log::sys(). Mirror main.cpp: initialise logging before any test.
struct log_init_fixture
{
  log_init_fixture () { wsl::log::init (); }
};
static log_init_fixture g_log_init;

// Helper: extract text from a tool_result
static std::string
get_text (const agentsdk::result<mcp::tool_result, mcp::mcp_error> &r)
{
  REQUIRE (r.has_value ());
  const auto &result = r.value ();
  REQUIRE (result.content.size () == 1);
  const auto *t = std::get_if<mcp::text_content> (&result.content[0]);
  REQUIRE (t != nullptr);
  return t->text;
}

// ============================================================================
// cli_reference
// ============================================================================

TEST_CASE ("get_cli_reference returns all commands")
{
  const auto &ref = get_cli_reference ();
  CHECK (ref.size () > 30);

  bool has_proj_new = false;
  bool has_ent_new = false;
  bool has_help = false;
  for (const auto &cmd : ref) {
    if (cmd.name == "proj new")
      has_proj_new = true;
    if (cmd.name == "ent new")
      has_ent_new = true;
    if (cmd.name == "help")
      has_help = true;
  }
  CHECK (has_proj_new);
  CHECK (has_ent_new);
  CHECK (has_help);
}

TEST_CASE ("commands have required fields")
{
  const auto &ref = get_cli_reference ();
  for (const auto &cmd : ref) {
    CHECK (!cmd.name.empty ());
    CHECK (!cmd.category.empty ());
    CHECK ((cmd.category == "cli" || cmd.category == "repl"));
    CHECK (!cmd.description.empty ());
    CHECK (!cmd.syntax.empty ());
  }
}

TEST_CASE ("find_command finds existing commands")
{
  const cli_command_info *cmd = find_command ("proj new");
  REQUIRE (cmd != nullptr);
  CHECK (cmd->name == "proj new");
  CHECK (cmd->category == "repl");

  cmd = find_command ("ent new");
  REQUIRE (cmd != nullptr);
  CHECK (cmd->name == "ent new");
}

TEST_CASE ("find_command returns null for unknown")
{
  CHECK (find_command ("nonexistent") == nullptr);
  CHECK (find_command ("") == nullptr);
}

TEST_CASE ("find_command finds cli flags")
{
  const cli_command_info *cmd = find_command ("--project");
  REQUIRE (cmd != nullptr);
  CHECK (cmd->category == "cli");
  CHECK (cmd->syntax.find ("--project") != std::string::npos);
}

// ============================================================================
// namespace_info
// ============================================================================

TEST_CASE ("handle_list_namespaces lists all 7 namespaces")
{
  auto result = handle_list_namespaces ("{}");
  std::string text = get_text (result);

  CHECK (text.find ("comp") != std::string::npos);
  CHECK (text.find ("sys") != std::string::npos);
  CHECK (text.find ("rsc") != std::string::npos);
  CHECK (text.find ("phys") != std::string::npos);
  CHECK (text.find ("gfx") != std::string::npos);
  CHECK (text.find ("math") != std::string::npos);
  CHECK (text.find ("reg") != std::string::npos);
}

TEST_CASE ("handle_describe_namespace returns detail for known ns")
{
  auto result = handle_describe_namespace (R"({"name":"comp"})");
  std::string text = get_text (result);

  CHECK (text.find ("wsl::comp") != std::string::npos);
  CHECK (text.find ("transform") != std::string::npos);
  CHECK (text.find ("camera") != std::string::npos);
}

TEST_CASE ("handle_describe_namespace errors for unknown ns")
{
  auto result = handle_describe_namespace (R"({"name":"foobar"})");
  CHECK (!result.has_value ());
  CHECK (result.error ().code == mcp::INVALID_PARAMS);
}

TEST_CASE ("handle_describe_namespace errors when name missing")
{
  auto result = handle_describe_namespace ("{}");
  CHECK (!result.has_value ());
  CHECK (result.error ().code == mcp::INVALID_PARAMS);
}

TEST_CASE ("handle_describe_namespace is case-insensitive")
{
  auto result = handle_describe_namespace (R"({"name":"COMP"})");
  CHECK (get_text (result).find ("wsl::comp") != std::string::npos);
}

TEST_CASE ("handle_describe_namespace for sys")
{
  auto result = handle_describe_namespace (R"({"name":"sys"})");
  CHECK (get_text (result).find ("EcsSystem") != std::string::npos);
}

// ============================================================================
// mcp_server_handlers
// ============================================================================

TEST_CASE ("handle_list_commands returns categorized list")
{
  auto result = handle_list_commands ("{}");
  std::string text = get_text (result);

  CHECK (text.find ("CLI Subcommands") != std::string::npos);
  CHECK (text.find ("REPL Commands") != std::string::npos);
  CHECK (text.find ("--project") != std::string::npos);
  CHECK (text.find ("proj new") != std::string::npos);
  CHECK (text.find ("comp avail") != std::string::npos);
}

TEST_CASE ("handle_describe_command returns command details")
{
  auto result = handle_describe_command (R"({"name":"proj new"})");
  std::string text = get_text (result);

  CHECK (text.find ("Command: proj new") != std::string::npos);
  CHECK (text.find ("Category: repl") != std::string::npos);
  CHECK (text.find ("Syntax:") != std::string::npos);
}

TEST_CASE ("handle_describe_command errors for unknown command")
{
  auto result = handle_describe_command (R"({"name":"does_not_exist"})");
  CHECK (!result.has_value ());
  CHECK (result.error ().code == mcp::INVALID_PARAMS);
}

TEST_CASE ("handle_describe_command errors when name missing")
{
  auto result = handle_describe_command ("{}");
  CHECK (!result.has_value ());
  CHECK (result.error ().code == mcp::INVALID_PARAMS);
}

TEST_CASE ("handle_describe_command shows parameters and examples")
{
  auto result = handle_describe_command (R"({"name":"proj new"})");
  std::string text = get_text (result);

  CHECK (text.find ("Parameters:") != std::string::npos);
  CHECK (text.find ("Examples:") != std::string::npos);
}

TEST_CASE ("handle_get_quick_start returns guide")
{
  auto result = handle_get_quick_start ("{}");
  std::string text = get_text (result);

  CHECK (text.find ("Quick Start") != std::string::npos);
  CHECK (text.find ("create-project") != std::string::npos);
  CHECK (text.find ("REPL") != std::string::npos);
}

TEST_CASE ("handle_cli_capabilities returns manifest")
{
  auto result = handle_cli_capabilities ("{}");
  std::string text = get_text (result);

  CHECK (text.find ("Fully Implemented") != std::string::npos);
  CHECK (text.find ("NOT Implemented") != std::string::npos);
  CHECK (text.find ("proj new") != std::string::npos);
  CHECK (text.find ("sig") != std::string::npos);
}

TEST_CASE ("handle_list_components returns registered components")
{
  auto result = handle_list_components ("{}");
  std::string text = get_text (result);
  CHECK (text.find ("Registered Components") != std::string::npos);
  CHECK (text.find ("Transform") != std::string::npos);
}

TEST_CASE ("handle_describe_component errors for unknown")
{
  auto result = handle_describe_component (R"({"name":"nope"})");
  CHECK (!result.has_value ());
  CHECK (result.error ().code == mcp::INVALID_PARAMS);
}

TEST_CASE ("handle_describe_das_api lists functions without name")
{
  auto result = handle_describe_das_api ("{}");
  std::string text = get_text (result);
  CHECK (text.find ("Weasel Daslang API") != std::string::npos);
}

TEST_CASE ("tool_builder emits input schemas")
{
  mcp::tool t = mcp::tool_builder ("describe_command")
                    .with_description ("desc")
                    .with_string_param ("name", "Name.")
                    .build ();
  CHECK (t.name == "describe_command");
  CHECK (t.input_schema.find ("\"name\"") != std::string::npos);
  CHECK (t.input_schema.find ("\"required\"") != std::string::npos);

  mcp::tool no_params
      = mcp::tool_builder ("list_commands").with_description ("d").build ();
  CHECK (no_params.input_schema.find ("properties") != std::string::npos);
}

// ============================================================================
// doc-drift guard: the generated Daslang API ledger
// (src/wsl/das/das_api_catalog.gen.hpp) must stay in sync with the real
// registration site (addExtern calls in src/wsl/das/wsl_api_module.cpp
// plus src/wsl/das/wsl_event_binds.cpp).
// ============================================================================

#ifndef WSL_SOURCE_DIR
#define WSL_SOURCE_DIR "."
#endif

TEST_CASE ("das_api_catalog stays in sync with wsl_api_module registrations")
{
  const std::filesystem::path module_src
      = std::filesystem::path (WSL_SOURCE_DIR)
        / "src/wsl/das/wsl_api_module.cpp";
  const std::filesystem::path binds_src = std::filesystem::path (WSL_SOURCE_DIR)
                                          / "src/wsl/das/wsl_event_binds.cpp";
  const std::filesystem::path catalog = std::filesystem::path (WSL_SOURCE_DIR)
                                        / "src/wsl/das/das_api_catalog.gen.hpp";

  std::ifstream mod (module_src);
  REQUIRE_MESSAGE (mod.good (), "could not open wsl_api_module.cpp");
  std::ifstream ebd (binds_src);
  REQUIRE_MESSAGE (ebd.good (), "could not open wsl_event_binds.cpp");
  std::ifstream cat (catalog);
  REQUIRE_MESSAGE (cat.good (), "could not open das_api_catalog.gen.hpp");

  std::string src_text ((std::istreambuf_iterator<char> (mod)),
                        std::istreambuf_iterator<char> ());
  {
    std::string binds_text ((std::istreambuf_iterator<char> (ebd)),
                            std::istreambuf_iterator<char> ());
    src_text += "\n";
    src_text += binds_text;
  }
  std::string cat_text ((std::istreambuf_iterator<char> (cat)),
                        std::istreambuf_iterator<char> ());

  // Mirror the generator's regex for the C++ registration name.
  const std::regex extern_re (
      R"PAT(addExtern\s*<\s*DAS_BIND_FUN\s*\(\s*([A-Za-z_][\w]*)\s*\)\s*>\s*\()PAT");
  std::set<std::string> registered;
  for (auto it
       = std::sregex_iterator (src_text.begin (), src_text.end (), extern_re);
       it != std::sregex_iterator (); ++it) {
    registered.insert ((*it)[1].str ());
  }
  REQUIRE_MESSAGE (!registered.empty (),
                   "no addExtern registrations found in source");

  // Catalog entries store the C++ name as the second array element.
  const std::regex cat_re (R"PAT(\{\s*"([^"]+)",\s*"([^"]+)",)PAT");
  std::set<std::string> cataloged;
  for (auto it
       = std::sregex_iterator (cat_text.begin (), cat_text.end (), cat_re);
       it != std::sregex_iterator (); ++it) {
    cataloged.insert ((*it)[2].str ());
  }
  REQUIRE_MESSAGE (!cataloged.empty (), "no entries found in catalog");

  for (const auto &name : registered) {
    INFO ("registered function '" << name
                                  << "' missing from generated catalog");
    CHECK (cataloged.count (name) == 1);
  }
}

// ============================================================================
// doc-drift guard: the gameplay facade reference
// (doc/source/stdlib/weasel.rst) must cover the public surface of
// src/wsl/das/modules/weasel.das. Regenerate with
// src/wsl/das/generate_facade_docs.py after editing the facade.
// ============================================================================

TEST_CASE ("weasel facade reference covers the public facade surface")
{
  const std::filesystem::path facade = std::filesystem::path (WSL_SOURCE_DIR)
                                       / "src/wsl/das/modules/weasel.das";
  const std::filesystem::path reference
      = std::filesystem::path (WSL_SOURCE_DIR) / "doc/source/stdlib/weasel.rst";

  std::ifstream das (facade);
  REQUIRE_MESSAGE (das.good (), "could not open weasel.das");
  std::ifstream rst (reference);
  REQUIRE_MESSAGE (rst.good (), "could not open weasel.rst");

  std::string das_text ((std::istreambuf_iterator<char> (das)),
                        std::istreambuf_iterator<char> ());
  std::string rst_text ((std::istreambuf_iterator<char> (rst)),
                        std::istreambuf_iterator<char> ());

  // Public top-level and member definitions: `def name`, `struct Name`,
  // `class Name`, `enum Name`, `typedef ... Alias`. Private (`_`-prefixed),
  // `private` classes, and the query-macro machinery are implementation
  // details and must not appear here.
  std::set<std::string> facade_names;
  {
    // Match line by line (^ anchors the whole string in ECMAScript).
    std::istringstream lines (das_text);
    std::string line;
    const std::regex def_re (
        R"PAT(^[ \t]*def[ \t]+(?:override[ \t]+)?([A-Za-z_][\w]*))PAT");
    const std::regex type_re (
        R"PAT(^[ \t]*(?:struct|class|enum)[ \t]+([A-Za-z_][\w]*))PAT");
    const std::regex alias_re (
        R"PAT(^[ \t]*typedef[ \t]+(?:public[ \t]+)?([A-Za-z_][\w]*))PAT");
    std::smatch m;
    int private_depth = 0;
    auto count_braces = [] (const std::string &s) {
      int d = 0;
      bool in_str = false;
      for (std::size_t k = 0; k < s.size (); ++k) {
        const char c = s[k];
        if (in_str) {
          if (c == '"' && (k == 0 || s[k - 1] != '\\')) {
            in_str = false;
          }
        } else if (c == '"') {
          in_str = true;
        } else if (c == '{') {
          ++d;
        } else if (c == '}') {
          --d;
        }
      }
      return d;
    };
    while (std::getline (lines, line)) {
      // `private` declarations and the query-macro class bodies are
      // implementation details, mirroring generate_facade_docs.py.
      const std::string code = line.substr (0, line.find ("//"));
      if (private_depth > 0) {
        private_depth += count_braces (code);
        continue;
      }
      if (code.find ("private") != std::string::npos) {
        private_depth += count_braces (code);
        continue;
      }
      if (std::regex_search (code, m, def_re)) {
        if (!m[1].str ().empty () && m[1].str ()[0] != '_') {
          facade_names.insert (m[1].str ());
        }
      } else if (std::regex_search (code, m, type_re)) {
        if (!m[1].str ().empty () && m[1].str ()[0] != '_') {
          facade_names.insert (m[1].str ());
        }
      } else if (std::regex_search (code, m, alias_re)) {
        if (!m[1].str ().empty () && m[1].str ()[0] != '_') {
          facade_names.insert (m[1].str ());
        }
      }
    }
  }
  facade_names.erase ("QueryMacro");
  facade_names.erase ("QueryExceptMacro");
  facade_names.erase ("private");
  REQUIRE_MESSAGE (!facade_names.empty (),
                   "no public entries found in weasel.das");

  // Documented names: `.. das:function:: name`, `.. das:method:: name`,
  // `.. das:class:: Name`, `.. das:attribute:: name`, `.. das:data:: Alias`.
  const std::regex doc_re (
      R"PAT(\.\.[ \t]+das:(?:function|method|operator|class|attribute|data)::[ \t]+(?:def[ \t]+)?(?:override[ \t]+)?([A-Za-z_][\w]*))PAT");
  std::set<std::string> documented;
  for (auto it
       = std::sregex_iterator (rst_text.begin (), rst_text.end (), doc_re);
       it != std::sregex_iterator (); ++it) {
    documented.insert ((*it)[1].str ());
  }
  REQUIRE_MESSAGE (!documented.empty (), "no entries found in weasel.rst");

  for (const auto &name : facade_names) {
    INFO ("facade entry '" << name << "' missing from weasel.rst; "
                           << "regenerate with generate_facade_docs.py");
    CHECK (documented.count (name) == 1);
  }
}
