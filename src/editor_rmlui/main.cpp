#include "editor_app.hpp"
#include <CLI/CLI.hpp>
#include <SDL3/SDL_filesystem.h>
#include <filesystem>
#include <string>

namespace
{
std::string
default_engine_resource_path ()
{
  // Try to locate resources relative to the executable at runtime.
  // This allows the binary to work from any install prefix (e.g. /usr/local,
  // ~/.local, or an extracted archive) without recompilation.
  const char *base_path = SDL_GetBasePath ();
  if (base_path != nullptr) {
    std::filesystem::path exe_dir (base_path);

    // On macOS the editor runs as a .app bundle and resources are
    // installed under <bundle>/Contents/Resources/share/weasel/.
    // Everywhere else they sit at <prefix>/share/weasel/ directly
    // above the executable.
#ifdef __APPLE__
    std::filesystem::path share_dir
        = exe_dir / ".." / "Resources" / "share" / "weasel";
#else
    std::filesystem::path share_dir = exe_dir / ".." / "share" / "weasel";
#endif
    if (std::filesystem::exists (share_dir / "compiled_shaders"))
      return share_dir.string ();
  }

  // Fallback to compile-time paths for development builds.
#ifdef WEASEL_BUILD_DIR
  return WEASEL_BUILD_DIR;
#elif defined(WEASEL_SOURCE_DIR)
  return WEASEL_SOURCE_DIR;
#else
  return ".";
#endif
}
} // namespace

int
main (int argc, char **argv)
{
  CLI::App app{ "Weasel Engine Editor (RmlUi)" };
  app.get_formatter ()->column_width (42);

  std::string project_to_load;
  auto *project_opt = app.add_option ("--project", project_to_load,
                                      "Path to the project to load");

  std::string scene_to_load;
  auto *scene_opt = app.add_option ("--scene", scene_to_load,
                                    "Path to the scene to open at startup");
  scene_opt->needs (project_opt);

  try {
    app.parse (argc, argv);
  } catch (const CLI::ParseError &e) {
    return app.exit (e);
  }

  editor_rml::editor_app g ("Incantation (RmlUi)", 1280, 720,
                            default_engine_resource_path ());

  if (!project_to_load.empty () && !g.set_project_path (project_to_load)) {
    return 1;
  }

  if (!scene_to_load.empty ()) {
    g.set_scene_path (scene_to_load);
  }

  return g.run ();
}
