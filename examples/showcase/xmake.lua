set_project("showcase")
set_version("0.1.0")
set_xmakever("3.0.0")
set_languages("c++20")
add_rules("mode.debug", "mode.release")

-- Weasel Engine dependency
-- Configure with: xmake f --weasel_dir=/path/to/weasel (or set $WEASEL_DIR)
option("weasel_dir", {showmenu = true, default = os.getenv("WEASEL_DIR") or "", description = "Path to the Weasel Engine source tree"})

target("showcase")
    set_kind("binary")
    add_files("src/*.cpp", "src/components/*.cpp", "src/systems/*.cpp", "src/singletons/*.cpp")
    -- TODO: AOT-compile .das files (weasel_aot_das equivalent)
    add_includedirs("$(weasel_dir)/src", "$(weasel_dir)/src/wsl")
    add_linkdirs("$(weasel_dir)/build/$(plat)/$(arch)/$(mode)")
    add_links("wsl")
    add_defines("WSL_RESOURCE_PATH=\"$(weasel_dir)\"")

-- --- Installation and Packaging ---
add_installfiles("src", {prefixdir = "share/showcase"})
