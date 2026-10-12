#include "das_api_info.hpp"
#include "mcp_helpers.hpp"

#include "wsl/das/das_api_catalog.gen.hpp"

#include <map>
#include <sstream>
#include <string>

namespace wsl::mcp_server
{

agentsdk::result<mcp::tool_result, mcp::mcp_error>
handle_describe_das_api (const std::string &args_json)
{
  const auto &catalog = das_api_catalog ();

  std::string name;
  get_optional_string_arg (args_json, "name", name);

  if (name.empty ()) {
    // Grouped listing.
    std::map<std::string, std::vector<const das_api_entry *>> by_cat;
    for (const auto &e : catalog)
      by_cat[e.category].push_back (&e);

    std::ostringstream oss;
    oss << "Weasel Daslang API (" << catalog.size ()
        << " functions)\n"
           "Generated from the runtime registration site "
           "(wsl_api_module.cpp).\n\n";
    const char *order[] = { "query", "mutation", "action" };
    for (const char *cat : order) {
      auto it = by_cat.find (cat);
      if (it == by_cat.end ())
        continue;
      oss << cat << " (" << it->second.size () << "):\n";
      for (const auto *e : it->second) {
        oss << "  - " << e->name;
        if (!e->args.empty ()) {
          oss << "(";
          for (std::size_t i = 0; i < e->args.size (); ++i) {
            if (i > 0)
              oss << ", ";
            oss << e->args[i];
          }
          oss << ")";
        }
        oss << "\n";
      }
      oss << "\n";
    }
    oss << "Use describe_das_api with a 'name' argument for a full entry.\n";
    return text_result (oss.str ());
  }

  const das_api_entry *found = nullptr;
  for (const auto &e : catalog) {
    if (e.name == name) {
      found = &e;
      break;
    }
  }
  if (found == nullptr) {
    return param_error ("Unknown API function: " + name);
  }

  std::ostringstream oss;
  oss << "Function: " << found->name << "\n";
  oss << "  C++ symbol: " << found->cpp << "\n";
  oss << "  Side effects: " << found->side_effect << " (" << found->effect
      << ")\n";
  oss << "  Category: " << found->category << "\n";
  oss << "  Signature: " << found->name << "(";
  for (std::size_t i = 0; i < found->args.size (); ++i) {
    if (i > 0)
      oss << ", ";
    oss << found->args[i];
  }
  oss << ")\n";
  oss << "\n";
  oss << "This entry is generated directly from the registration site; it "
         "reflects exactly what is callable from a .das script. No "
         "hand-written "
         "stub is involved.\n";

  return text_result (oss.str ());
}

} // namespace wsl::mcp_server
