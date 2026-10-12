import sys
import os
import time

sys.path.insert(0, os.path.abspath("."))
sys.path.insert(0, os.path.abspath("_themes"))
extensions = [
    "hawkmoth",
    "daslang",
]

# -- Hawkmoth configuration -------------------------------------------------
# Hawkmoth parses C/C++ source directly with libclang (no Doxygen needed).
_this_dir = os.path.dirname(os.path.abspath(__file__))
_project_root = os.path.normpath(os.path.join(_this_dir, "..", ".."))

hawkmoth_root = os.path.join(_project_root, "src")


def _first_glob(pattern):
    """Return the first matching path, or '' if none (keeps clang flags valid)."""
    import glob

    matches = sorted(glob.glob(pattern))
    return matches[0] if matches else ""


# Resolve toolchain include paths by version glob instead of hardcoding them,
# so a GCC/Clang version bump doesn't silently break header resolution.
_gcc_include = _first_glob("/usr/lib/gcc/x86_64-pc-linux-gnu/*/include")
_cxx_include = _first_glob("/usr/include/c++/[0-9]*")
_clang_include = _first_glob("/usr/lib/clang/*/include")

hawkmoth_clang = [
    f"-I{os.path.join(_project_root, 'src')}",
    f"-I{os.path.join(_project_root, 'src', 'wsl')}",
    f"-I{os.path.join(_project_root, 'build', '_deps', 'entt-src', 'single_include')}",
    f"-I{os.path.join(_project_root, 'build', '_deps', 'daslang-src', 'include')}",
    f"-I{os.path.join(_project_root, 'build', '_deps', 'daslang-build', 'include')}",
    f"-I{os.path.join(_project_root, 'build', '_deps', 'ai-sdk-cpp-src', 'third_party', 'nlohmann_json_patched', 'include')}",
    f"-I{os.path.join(_project_root, 'build', '_deps', 'ai-sdk-cpp-src', 'third_party', 'nlohmann_json_patched', 'include', 'nlohmann')}",
    "-std=c++20",
    "-D__clang__",
    "-Wno-everything",
    "-D__cpp_exceptions=1",
    # RmlUi's SDL platform header requires the linked SDL major version.
    "-DRMLUI_SDL_VERSION_MAJOR=3",
    # Note: we intentionally do NOT add GCC's internal include path
    # (/usr/lib/gcc/.../include). It pulls in GCC's ia32intrin.h/xmmintrin.h
    # which redefine clang builtins and fail to parse. clang provides its own
    # stddef.h/float.h via its resource dir (the clang include below).
    # System C++ standard library paths
    f"-isystem{_cxx_include}" if _cxx_include else "",
    f"-isystem{os.path.join(_cxx_include, 'x86_64-pc-linux-gnu')}"
    if _cxx_include
    else "",
    f"-isystem{os.path.join(_cxx_include, 'backward')}" if _cxx_include else "",
    f"-isystem{_clang_include}" if _clang_include else "",
    "-isystem/usr/local/include",
    "-isystem/usr/include",
]

templates_path = ["_templates"]
suppress_warnings = ["toctree.not_included"]
source_suffix = ".rst"
master_doc = "index"

project = "Weasel Engine"
copyright = "2024-%s" % time.strftime("%Y")
author = "Weasel Contributors"

version = "0.1.0"
release = "0.1.0"

language = "en"
exclude_patterns = ["_build"]
pygments_style = "pygments_weasel.WeaselStyle"
highlight_language = "cpp"
primary_domain = "cpp"
todo_include_todos = False

html_theme = "weasel"
html_theme_path = [os.path.abspath("_themes")]
html_theme_options = {}
html_logo = "_static/logo.svg"
html_favicon = "_static/logo.svg"
html_static_path = ["_static"]
html_js_files = ["toc.js"]
html_sidebars = {
    "**": ["logo.html", "searchbox.html", "globaltoc.html", "sourcelink.html"]
}
htmlhelp_basename = "weasel_doc"

latex_documents = [
    (master_doc, "weasel.tex", "Weasel Engine Documentation", author, "manual"),
]
