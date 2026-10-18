-- Weasel Engine — xmake build
-- The sole build system for Weasel (replaces the removed CMake build).
-- Layout: legacy header build by default; `with_modules` selects the C++20
-- module build (Phase C) where enabled.
-- Toolchain: xmake 3.1.1 + Clang 22 (GCC 16 also works)
set_project("weasel")
set_version("0.1.0")
set_xmakever("3.0.0")
set_languages("c++20")
set_policy("build.warning", true)
set_policy("build.optimization.lto", false)

add_rules("mode.debug", "mode.release")

-- WEASEL_SOURCE_DIR-style defines embed the path in C string literals, where
-- a native Windows backslash path corrupts them (unknown escapes like \w,
-- real ones like \a). Normalize once; use projectdir_fwd in every define.
local projectdir_fwd = (os.projectdir ():gsub ("\\", "/"))

-- ---------------------------------------------------------------------------
-- Options (mirror CMake options)
-- ---------------------------------------------------------------------------
option("weasel_enable_multiplayer", function ()
    set_default(false)
    set_showmenu(true)
    set_description("Enable multiplayer via GameNetworkingSockets")
end)

option("weasel_enable_renderdoc", function ()
    set_default(false)
    set_showmenu(true)
    set_description("Enable RenderDoc in-application API")
end)



-- ---------------------------------------------------------------------------
-- Custom packages for forks / packages not in xrepo
-- These override or supplement xrepo packages.
-- ---------------------------------------------------------------------------

-- agentsdk-cpp (ldapx/agentsdk-cpp) — A2A + ACP + MCP agent SDK (not in xrepo)
package("agentsdk")
    set_kind("library")
    set_homepage("https://github.com/ldapx/agentsdk-cpp")
    set_description("AgentSDK C++ — A2A, ACP and MCP SDK")
    set_license("MIT")
    add_urls("https://github.com/ldapx/agentsdk-cpp.git")
    add_versions("dev", "dev")
    on_install(function (package)
        io.writefile("xmake.lua", [[
            add_rules("mode.debug", "mode.release")
            set_languages("c++20")
            add_requires("spdlog", {configs = {header_only = false}}, "libcurl", "simdjson", "libsdl3")
            target("agentsdk")
                set_kind("static")
                add_files("src/agentsdk/**.cpp")
                -- Compiled spdlog (not header-only): otherwise every object
                -- embeds spdlog/fmt code that collides with the outer
                -- compiled libspdlog/libfmt at link time (MSVC LNK2005;
                -- ELF tolerates it, which is why Linux never noticed).
                -- No fmt_external: the bundled fmt headers suffice, and
                -- skipping a second fmt build avoids a source-tree race.
                add_defines("SPDLOG_COMPILED_LIB")
                -- Windows: the A2A HTTP server transports have no support
                -- upstream (BSD sockets in http_listener). weasel only links
                -- the ACP/MCP client side plus the MCP stdio server, which
                -- are portable; nothing references these objects, so they
                -- are dropped from the build. mcp_server.cpp stays: its
                -- stdin pump is portable (background reader thread with
                -- unbuffered reads).
                if is_plat("windows") then
                    remove_files("src/agentsdk/a2a/http/http_listener.cpp",
                                 "src/agentsdk/a2a/http/http_server.cpp",
                                 "src/agentsdk/a2a/http/json_rpc_server.cpp",
                                 "src/agentsdk/a2a/server.cpp")
                end
                add_includedirs("src", {public = true})
                -- simdjson and curl types appear in the public headers
                -- (a2a/json_util.hpp, a2a/http/http_client.hpp).
                add_packages("spdlog", "libcurl", "simdjson", {public = true})
                -- libsdl3 stays private: the public headers only name SDL
                -- types behind void* (stdio transport handles).
                add_packages("libsdl3")
                if is_plat("linux") then
                    add_syslinks("pthread")
                elseif is_plat("windows") then
                    add_syslinks("ws2_32", "wsock32")
                end
        ]])
        import("package.tools.xmake").install(package)
        -- Preserve the src/agentsdk/... layout under include/ so that
        -- consumers keep using <agentsdk/a2a/...>. add_headerfiles would
        -- flatten every header into a single include/ directory.
        os.cp("src/agentsdk", package:installdir("include"))
    end)
    on_load(function (package)
        package:add("includedirs", "include")
        package:add("defines", "SPDLOG_COMPILED_LIB")
        package:add("deps", "libcurl")
    end)
package_end()

-- ImSearch (Mario1159/imsearch) — static lib (needs imgui)
package("imsearch")
    set_kind("library")
    set_homepage("https://github.com/Mario1159/imsearch")
    set_description("ImSearch — ImGui search widget")
    add_urls("https://github.com/Mario1159/imsearch.git")
    add_versions("main", "main")
    on_install(function (package)
        io.writefile("xmake.lua", [[
            add_rules("mode.debug", "mode.release")
            set_languages("c++20")
            add_requires("imgui v1.92.9-docking", {configs = {sdl3 = true, sdl3_renderer = true, sdl3_gpu = true, shared = false}})
            target("imsearch")
                set_kind("$(kind)")
                add_files("imsearch.cpp")
                add_headerfiles("imsearch.h", "imsearch_internal.h")
                add_includedirs(".", {public = true})
                add_packages("imgui")
                if is_kind("shared") then
                    add_rules("utils.symbols.export_all")
                end
        ]])
        import("package.tools.xmake").install(package)
    end)
    on_load(function (package)
        package:add("deps", "imgui v1.92.9-docking", {configs = {sdl3 = true, sdl3_renderer = true, sdl3_gpu = true, shared = false}})
    end)
package_end()

-- ImGizmo2D (Miisan-png/ImGizmo2D) — header-only
package("imgizmo2d")
    set_kind("library", {headeronly = true})
    set_homepage("https://github.com/Miisan-png/ImGizmo2D")
    add_urls("https://github.com/Miisan-png/ImGizmo2D.git")
    add_versions("main", "main")
    on_install(function (package)
        os.cp("*.h", package:installdir("include"))
        os.cp("*.hpp", package:installdir("include"))
        -- Patch AddRect signature for imgui >= 1.92.8 (thickness and flags swapped)
        io.replace(path.join(package:installdir("include"), "ImGizmo2D.h"),
                   [[ctx.drawList->AddRect(tl, br, ctx.colLine, 0, 0, ctx.lineThickness);]],
                   [[ctx.drawList->AddRect(tl, br, ctx.colLine, 0, ctx.lineThickness, 0);]],
                   {plain = true})
    end)
package_end()

-- ImNodeFlow (Fattorino/ImNodeFlow) — static lib
package("imnodeflow")
    set_kind("library")
    set_homepage("https://github.com/Fattorino/ImNodeFlow")
    add_urls("https://github.com/Fattorino/ImNodeFlow.git")
    add_versions("master", "master")
    on_install(function (package)
        io.writefile("xmake.lua", [[
            add_rules("mode.debug", "mode.release")
            set_languages("c++20")
            add_requires("imgui v1.92.9-docking", {configs = {sdl3 = true, sdl3_renderer = true, sdl3_gpu = true, shared = false}})
            target("imnodeflow")
                set_kind("$(kind)")
                add_files("src/ImNodeFlow.cpp")
                add_headerfiles("include/ImNodeFlow.h")
                add_headerfiles("src/imgui_bezier_math.h", "src/imgui_bezier_math.inl", "src/imgui_extra_math.h", "src/imgui_extra_math.inl")
                add_includedirs("include", {public = true})
                add_includedirs("src")
                add_packages("imgui")
                add_defines("IMGUI_DEFINE_MATH_OPERATORS")
        ]])
        import("package.tools.xmake").install(package)
        -- Preserve relative include "../src/imgui_bezier_math.h" expected by ImNodeFlow.h
        os.cp("src", package:installdir())
    end)
    on_load(function (package)
        package:add("deps", "imgui v1.92.9-docking", {configs = {sdl3 = true, sdl3_renderer = true, sdl3_gpu = true, shared = false}})
        package:add("includedirs", "include")
    end)
package_end()

-- ImGuiTextSelect (Mario1159/ImGuiTextSelect) — static lib
package("imguitextselect")
    set_kind("library")
    set_homepage("https://github.com/Mario1159/ImGuiTextSelect")
    add_urls("https://github.com/Mario1159/ImGuiTextSelect.git")
    add_versions("main", "main")
    on_install(function (package)
        io.writefile("xmake.lua", [[
            add_rules("mode.debug", "mode.release")
            set_languages("c++20")
            add_requires("imgui v1.92.9-docking", {configs = {sdl3 = true, sdl3_renderer = true, sdl3_gpu = true, shared = false}})
            target("imguitextselect")
                set_kind("$(kind)")
                add_files("textselect.cpp")
                add_headerfiles("textselect.hpp")
                add_includedirs(".", {public = true})
                add_packages("imgui")
        ]])
        import("package.tools.xmake").install(package)
    end)
    on_load(function (package)
        package:add("deps", "imgui v1.92.9-docking", {configs = {sdl3 = true, sdl3_renderer = true, sdl3_gpu = true, shared = false}})
    end)
package_end()

-- ImGuiColorTextEdit (goossens/ImGuiColorTextEdit) — static lib
package("imguicolortextedit")
    set_kind("library")
    set_homepage("https://github.com/goossens/ImGuiColorTextEdit")
    add_urls("https://github.com/goossens/ImGuiColorTextEdit.git")
    add_versions("future", "future")
    on_install(function (package)
        io.writefile("xmake.lua", [[
            add_rules("mode.debug", "mode.release")
            set_languages("c++20")
            add_requires("imgui v1.92.9-docking", {configs = {sdl3 = true, sdl3_renderer = true, sdl3_gpu = true, shared = false}})
            target("imguicolortextedit")
                set_kind("$(kind)")
                add_files("TextEditor.cpp", "TextDiff.cpp")
                add_headerfiles("TextEditor.h", "TextDiff.h", "dtl.h")
                add_includedirs(".", {public = true})
                add_packages("imgui")
        ]])
        import("package.tools.xmake").install(package)
    end)
    on_load(function (package)
        package:add("deps", "imgui v1.92.9-docking", {configs = {sdl3 = true, sdl3_renderer = true, sdl3_gpu = true, shared = false}})
    end)
package_end()

-- daslang — build from source like CMake does (Gaijin/daScript v0.6.3-RC3)
-- xrepo's dascript is a binary bundle; we need libDaScript static lib
package("daslang")
    set_kind("library")
    set_homepage("https://github.com/GaijinEntertainment/daScript")
    set_description("daScript — daslang scripting language (source build)")
    add_urls("https://github.com/GaijinEntertainment/daScript.git")
    add_versions("v0.6.3-RC3", "v0.6.3-RC3")
    add_deps("cmake")
    add_includedirs("include", {public = true})
    add_linkdirs("lib", {public = true})
    -- daScript's CMake does not strip the auto "lib" prefix, so the
    -- archive on disk is liblibDaScript.a -> link name "libDaScript".
    -- libDaScript is a thin wrapper over libDaScript_runtime (Context,
    -- simulator, annotations...) plus libUriParser; link them after.
    add_links("libDaScript", "libDaScript_runtime", "libUriParser",
              {public = true})
    add_defines("WEASEL_HAS_DASLANG=1", {public = true})
    on_install(function (package)
        -- GCC 16 hard-errors on daslang's deprecated wstring_convert under
        -- its own -Werror; the engine does not use the unit-test module.
        -- Also re-enable RTTI: daslang builds -fno-rtti, which breaks the
        -- link against the RTTI-enabled engine when using clang.
        -- NB: io.replace gsubs by default; {plain = true} is required or
        -- "-fno-rtti" (a malformed Lua pattern) silently matches nothing.
        local common = "CMakeCommon.txt"  -- cwd is the package sourcedir
        io.replace(common, "-fno-rtti", "-frtti", {plain = true})
        assert(io.readfile(common):find("-frtti", 1, true),
               "daslang: failed to patch CMakeCommon.txt for RTTI")
        -- daslang turns warnings into errors on every target
        -- (DAS_APPLY_STRICT_WARNINGS adds -Werror). The GCC-only
        -- -Werror=<group> downgrades below are rejected outright by
        -- clang ("unknown warning option", fatal under -Werror), so
        -- strip -Werror instead: compiler-agnostic and future-proof.
        io.replace(common, "-Wnon-virtual-dtor;-Werror", "-Wnon-virtual-dtor", {plain = true})
        assert(not io.readfile(common):find("-Wnon-virtual-dtor;-Werror", 1, true),
               "daslang: failed to strip -Werror from CMakeCommon.txt")
        local configs = {}
        table.insert(configs, "-DCMAKE_BUILD_TYPE=" .. (package:is_debug() and "Debug" or "Release"))
        -- NB: -Wstringop-overflow is GCC-only; passing it as -Werror= to
        -- clang is itself a (fatal) unknown-warning error, so it is
        -- deliberately omitted. The remaining groups exist on both GCC
        -- and clang; they only matter if some other target re-adds
        -- -Werror, since the macro-level -Werror is stripped above.
        table.insert(configs, "-DCMAKE_CXX_FLAGS=-Wno-error=deprecated-declarations -Wno-error=uninitialized -Wno-error=array-bounds")
        table.insert(configs, "-DBUILD_TESTING=OFF")
        table.insert(configs, "-DDAS_UNIT_TEST_DISABLED=ON")
        table.insert(configs, "-DDAS_GLFW_DISABLED=ON")
        table.insert(configs, "-DDAS_CLANG_BIND_DISABLED=ON")
        table.insert(configs, "-DDAS_LLVM_DISABLED=ON")
        table.insert(configs, "-DDAS_SMT_DISABLED=ON")
        table.insert(configs, "-DDAS_SQLITE_DISABLED=ON")
        table.insert(configs, "-DDAS_HV_DISABLED=ON")
        table.insert(configs, "-DDAS_AUDIO_DISABLED=ON")
        table.insert(configs, "-DDAS_STDDLG_DISABLED=ON")
        table.insert(configs, "-DDAS_STBIMAGE_DISABLED=ON")
        table.insert(configs, "-DDAS_TOOLS_DISABLED=ON")
        table.insert(configs, "-DDAS_AOT_EXAMPLES_DISABLED=ON")
        table.insert(configs, "-DDAS_TUTORIAL_DISABLED=ON")
        table.insert(configs, "-DDAS_TESTS_DISABLED=ON")
        table.insert(configs, "-DDAS_BUILD_DOCUMENTATION=OFF")
        import("package.tools.cmake").install(package, configs)
        -- daslib scripts (incl. aot_cpp.das) for runtime `require` and AOT
        os.cp("daslib", package:installdir())
    end)
package_end()

-- ozz-animation (guillaumeblanc/ozz-animation) — skeletal animation runtime,
-- offline builders, and the gltf2ozz importer (not in xrepo; M1 of
-- OZZ_ANIMATION_PLAN.md).
package("ozz-animation")
    set_kind("library")
    set_homepage("https://github.com/guillaumeblanc/ozz-animation")
    set_description("ozz-animation — skeletal animation runtime & offline libraries")
    set_license("MIT")
    add_urls("https://github.com/guillaumeblanc/ozz-animation.git")
    add_versions("0.17.0", "744eb9d99f606eda849acb0b1204f7a3dc20bca1")
    add_deps("cmake")
    add_includedirs("include", {public = true})
    -- Dependents before dependencies for static link order. gltf2ozz itself
    -- installs as a standalone executable at bin/tools/gltf2ozz.
    add_links("ozz_animation_offline", "ozz_animation", "ozz_base", {public = true})
    on_install(function (package)
        local configs = {}
        table.insert(configs, "-DCMAKE_BUILD_TYPE=" .. (package:is_debug() and "Debug" or "Release"))
        table.insert(configs, "-DBUILD_SHARED_LIBS=OFF")
        -- Tools stay ON for gltf2ozz; FBX SDK is absent so the fbx pipeline
        -- is disabled explicitly. Samples/howtos/tests only add build time.
        table.insert(configs, "-Dozz_build_tools=ON")
        table.insert(configs, "-Dozz_build_gltf=ON")
        table.insert(configs, "-Dozz_build_fbx=OFF")
        table.insert(configs, "-Dozz_build_samples=OFF")
        table.insert(configs, "-Dozz_build_howtos=OFF")
        table.insert(configs, "-Dozz_build_tests=OFF")
        table.insert(configs, "-Dozz_build_data=OFF")
        -- The default postfix would rename release archives to
        -- libozz_*_r.a, which breaks add_links("ozz_*").
        table.insert(configs, "-Dozz_build_postfix=OFF")
        -- ozz builds with -Werror, and MSVC's UCRT marks fopen etc.
        -- deprecated, which is fatal under clang-windows. The macro is a
        -- no-op elsewhere. (opt.cxflags appends flags through xmake's
        -- cmake helper instead of clobbering CMAKE_CXX_FLAGS.)
        import("package.tools.cmake").install(package, configs,
                                              {cxflags = {"-D_CRT_SECURE_NO_WARNINGS"}})
    end)
    on_test(function (package)
        assert(package:check_cxxsnippets({test = [[
            #include <ozz/animation/offline/raw_skeleton.h>
            #include <ozz/animation/offline/skeleton_builder.h>
            #include <ozz/animation/runtime/skeleton.h>
            void test() {
                ozz::animation::offline::RawSkeleton raw;
                raw.roots.resize(1);
                ozz::animation::offline::SkeletonBuilder builder;
                ozz::unique_ptr<ozz::animation::Skeleton> skel = builder(raw);
                (void)skel;
            }
        ]]}, {configs = {languages = "c++17"}}))
    end)
package_end()

-- rapidjson override: upstream xmake-repo pins the install check to
-- c++11, which MSVC 14.44+ STL rejects (deduced `auto` returns need
-- C++14; cl offers no C++11 mode at all). rapidjson itself is plain
-- C++11-compatible code and works on Windows — only the recipe's check
-- standard is wrong. Checking as c++17 is strictly stronger and valid
-- on every toolchain. (Project-shadows-repo package override.)
package("rapidjson")
    set_kind("library", {headeronly = true})
    set_homepage("https://github.com/Tencent/rapidjson")
    set_description("RapidJSON is a JSON parser and generator for C++.")
    set_license("MIT")
    add_urls("https://github.com/Tencent/rapidjson/archive/refs/tags/$(version).zip",
             "https://github.com/Tencent/rapidjson.git", {submodules = false})
    add_versions("2025.02.05", "24b5e7a8b27f42fa16b96fc70aade9106cf7102f")
    add_configs("cmake", {description = "Use cmake build system", default = true, type = "boolean"})
    on_load(function (package)
        if package:config("cmake") then
            package:add("deps", "cmake")
        end
        if package:is_plat("windows") and package:is_arch("arm.*") then
            package:add("defines", "RAPIDJSON_ENDIAN=RAPIDJSON_LITTLEENDIAN")
        end
    end)
    on_install(function (package)
        if package:config("cmake") then
            local configs = {
                "-DRAPIDJSON_BUILD_DOC=OFF",
                "-DRAPIDJSON_BUILD_EXAMPLES=OFF",
                "-DRAPIDJSON_BUILD_TESTS=OFF",
            }
            table.insert(configs, "-DCMAKE_BUILD_TYPE=" .. (package:is_debug() and "Debug" or "Release"))
            import("package.tools.cmake").install(package, configs)
        else
            os.cp("include/*", package:installdir("include"))
        end
    end)
    on_test(function (package)
        assert(package:check_cxxsnippets({test = [[
            void test()
            {
                const char* json = "{\"project\":\"rapidjson\",\"stars\":10}";
                rapidjson::Document d;
                d.Parse(json);

                rapidjson::StringBuffer buffer;
                rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
                d.Accept(writer);
            }
        ]]}, {configs = {languages = "c++17"}, includes = { "rapidjson/document.h", "rapidjson/stringbuffer.h", "rapidjson/writer.h"} }))
    end)
package_end()

-- cli11 override: same story as rapidjson — upstream xmake-repo pins the
-- install check to cxx11, which MSVC 14.44+ STL rejects (CLI.hpp pulls in
-- <codecvt>/<locale>, which use C++14-only constructs). CLI11 itself is
-- C++11-and-beyond code that works on Windows; only the check standard
-- is wrong. c++17 is valid on every toolchain here.
package("cli11")
    set_kind("library", {headeronly = true})
    set_homepage("https://github.com/CLIUtils/CLI11")
    set_description("CLI11 is a command line parser for C++11 and beyond that provides a rich feature set with a simple and intuitive interface.")
    set_license("BSD")
    add_urls("https://github.com/CLIUtils/CLI11/archive/refs/tags/$(version).tar.gz",
             "https://github.com/CLIUtils/CLI11.git")
    add_versions("v2.6.2", "c6ea6b2e5608b3ea8617999bd5f47420c71b2ebdb8dc4767c1034d1da5785711")
    add_configs("cmake", {description = "Use cmake build system", default = true, type = "boolean"})
    if is_plat("windows", "mingw") then
        add_syslinks("shell32")
    end
    on_load(function (package)
        if package:config("cmake") then
            package:add("deps", "cmake")
        end
    end)
    on_install(function (package)
        if package:config("cmake") then
            import("package.tools.cmake").install(package, {
                "-DBUILD_TESTING=OFF",
                "-DCLI11_BUILD_EXAMPLES=OFF",
                "-DCLI11_INSTALL=ON",
            })
        else
            os.cp("include", package:installdir())
        end
    end)
    on_test(function (package)
        assert(package:check_cxxsnippets({test = [[
            CLI::App app{"Test", "test"};
        ]]}, {configs = {languages = "c++17"}, includes = "CLI/CLI.hpp"}))
    end)
package_end()

-- libarchive override: upstream xmake-repo disables the CAT/TAR/CPIO
-- CLI tools but not the newer bsdunzip tool, whose bsdunzip.c uses
-- POSIX write/open/close/isatty without the Windows <io.h> spellings
-- (hard error under clang-windows). We only ever link the library, so
-- disable it like its siblings. Applies everywhere: less to build,
-- same archive_* API.
package("libarchive")
    set_homepage("https://libarchive.org/")
    set_description("Multi-format archive and compression library")
    set_license("BSD-2-Clause")
    add_urls("https://libarchive.org/downloads/libarchive-$(version).tar.gz")
    add_urls("https://github.com/libarchive/libarchive/releases/download/v$(version)/libarchive-$(version).tar.gz")
    add_versions("3.8.9", "f5a6539059cf5e597dbeda37bfa4874b1e8dea063c8d93bf85a2b44af90a5bd4")
    add_deps("cmake")
    add_configs("openssl3", {description = "Enable use of OpenSSL.", default = true, type = "boolean"})
    add_configs("xz",       {description = "Enable use of XZ/LZMA.", default = true, type = "boolean"})
    add_configs("zlib",     {description = "Enable use of GZIP/ZIP.",default = true, type = "boolean"})
    add_configs("bzip2",    {description = "Enable use of BZIP2.",   default = true, type = "boolean"})
    add_configs("lz4",      {description = "Enable use of LZ4.",     default = true, type = "boolean"})
    add_configs("lzo",      {description = "Enable use of LZO.",     default = true, type = "boolean"})
    add_configs("zstd",     {description = "Enable use of ZSTD.",    default = true, type = "boolean"})
    if is_plat("windows") then
        add_syslinks("advapi32", "bcrypt", "ws2_32", "shlwapi", "user32", "crypt32")
    end
    on_load(function (package)
        if package:config("openssl3") then
            package:add("deps", "openssl3", {configs = {shared = package:config("shared")}})
        end
        if package:config("xz") then
            package:add("deps", "xz")
        end
        if package:config("zlib") then
            package:add("deps", "zlib")
        end
        if package:config("bzip2") then
            package:add("deps", "bzip2")
        end
        if package:config("lz4") then
            package:add("deps", "lz4")
        end
        if package:config("lzo") then
            package:add("deps", "lzo")
        end
        if package:config("zstd") then
            package:add("deps", "zstd")
        end
    end)
    on_install("windows", "linux", "macosx", function (package)
        local configs = {"-DENABLE_TEST=OFF",
                         "-DENABLE_CAT=OFF",
                         "-DENABLE_TAR=OFF",
                         "-DENABLE_CPIO=OFF",
                         "-DENABLE_UNZIP=OFF",
                         "-DENABLE_PCREPOSIX=OFF",
                         "-DENABLE_LibGCC=OFF",
                         "-DENABLE_LIBGCC=OFF",
                         "-DENABLE_ICONV=OFF",
                         "-DENABLE_ACL=OFF",
                         "-DENABLE_EXPAT=OFF",
                         "-DENABLE_LIBXML2=OFF",
                         "-DENABLE_LIBB2=OFF"}
        table.insert(configs, "-DENABLE_OPENSSL=" .. (package:config("openssl3") and "ON" or "OFF"))
        table.insert(configs, "-DENABLE_LZMA="    .. (package:config("xz")       and "ON" or "OFF"))
        table.insert(configs, "-DENABLE_ZLIB="    .. (package:config("zlib")      and "ON" or "OFF"))
        table.insert(configs, "-DENABLE_BZip2="   .. (package:config("bzip2")    and "ON" or "OFF"))
        table.insert(configs, "-DENABLE_LZ4="     .. (package:config("lz4")      and "ON" or "OFF"))
        table.insert(configs, "-DENABLE_LZO="     .. (package:config("lzo")      and "ON" or "OFF"))
        table.insert(configs, "-DENABLE_ZSTD="    .. (package:config("zstd")     and "ON" or "OFF"))
        table.insert(configs, "-DENABLE_CNG="     .. (package:is_plat("windows") and "ON" or "OFF"))
        table.insert(configs, "-DCMAKE_BUILD_TYPE="  .. (package:debug() and "Debug" or "Release"))
        table.insert(configs, "-DBUILD_SHARED_LIBS=" .. (package:config("shared") and "ON" or "OFF"))
        if package:is_plat("windows") then
            table.insert(configs, "-DPOSIX_REGEX_LIB=NONE")
        end
        if not package:config("shared") then
            package:add("defines", "LIBARCHIVE_STATIC")
        end
        import("package.tools.cmake").install(package, configs)
    end)
    on_test(function (package)
        assert(package:has_cfuncs("archive_version_number", {includes = "archive.h"}))
    end)
package_end()
package("rmlui")
    set_kind("library")
    set_homepage("https://github.com/Mario1159/RmlUi")
    set_description("RmlUi with SDL_GPU renderer backend")
    set_license("MIT")
    add_urls("https://github.com/Mario1159/RmlUi.git")
    add_versions("master", "master")
    add_deps("cmake", "libsdl3", "freetype", "lunasvg")
    on_install(function (package)
        local configs = {}
        table.insert(configs, "-DCMAKE_BUILD_TYPE=" .. (package:is_debug() and "Debug" or "Release"))
        table.insert(configs, "-DBUILD_SHARED_LIBS=" .. (package:config("shared") and "ON" or "OFF"))
        table.insert(configs, "-DRMLUI_SVG_PLUGIN=ON")
        table.insert(configs, "-DRMLUI_FONT_ENGINE=freetype")
        table.insert(configs, "-DRMLUI_SDL_VERSION_MAJOR=3")
        table.insert(configs, "-DBUILD_SAMPLES=OFF")
        table.insert(configs, "-DBUILD_TESTS=OFF")
        import("package.tools.cmake").install(package, configs)
        -- Install Backends headers and sources so wsl can compile them
        if os.isdir("Backends") then
            os.cp("Backends", package:installdir())
            os.cp("Backends/*.h", package:installdir("include"))
            os.cp("Backends/*.hpp", package:installdir("include"))
        end
    end)
    on_load(function (package)
        package:add("includedirs", "include")
        local bdir = path.join(package:installdir(), "Backends")
        if os.isdir(bdir) then
            package:add("includedirs", "Backends")
        end
    end)
package_end()

-- ---------------------------------------------------------------------------
-- Third-party packages (xrepo)
-- ---------------------------------------------------------------------------
-- Core engine deps — versions mirror CMakeLists.txt where possible
add_requires("entt 4.0.0")
add_requires("glm")
-- NB: xmake-repo defaults spdlog to header_only=true (a no-op `shared`
-- build that emits no library), but wsl compiles everything with
-- SPDLOG_COMPILED_LIB, which needs real compiled symbols to link against.
-- header_only=false builds libspdlog (static: no runtime .so to ship in
-- CI artifacts); fmt_external=true avoids duplicate fmt symbols between
-- spdlog's bundled fmt and our own `fmt` package (single fmt from libfmt).
add_requires("spdlog v1.17.0", {configs = {header_only = false, fmt_external = true}})
-- NB: header_only=false is spelled out (not just defaulted) so this
-- instance merges with spdlog's fmt dep, which requests the same config
-- explicitly. Two same-version instances (fmt + fmt#1) share one source
-- tree and race each other on Windows (concurrent extract vs. install
-- -> transiently missing headers like ostream.h at install time).
add_requires("fmt", {configs = {header_only = false}})
add_requires("rapidjson 2025.02.05", {configs = {cmake = false}})
add_requires("reflect-cpp v0.25.0", {configs = {yyjson = true, msgpack = true}})
add_requires("box3d v0.1.0", {configs = {simd = false, double_precision = false}})
add_requires("rmlui", {configs = {shared = false}})
add_requires("fastgltf", {configs = {}})
add_requires("meshoptimizer v0.24")
add_requires("simdjson v4.6.2")
add_requires("libsdl3 3.4.12", {configs = {shared = false}})
add_requires("libsdl3_image 3.4.0", {configs = {shared = false}})
add_requires("libsdl3_mixer 3.2.4", {configs = {shared = false}})
add_requires("tracy v0.13.1", {configs = {on_demand = true, tracy_enable = true, shared = false}})
add_requires("imgui v1.92.9-docking", {configs = {shared = false, sdl3 = true, sdl3_renderer = true, sdl3_gpu = true}})
add_requires("stb")
add_requires("cli11 v2.6.2")
add_requires("nlohmann_json v3.12.0")
add_requires("libarchive")
add_requires("ozz-animation")
-- libcurl: agentsdk's discovery TU links it, but our agentsdk package
-- never exported it, so MSVC finds no provider (Linux happened to work).
-- Required here so every consumer links exactly one instance.
add_requires("libcurl")
-- A2A/ACP agent SDK — supplies <agentsdk/...> headers and exports
-- libcurl transitively (only the SDK's HTTP transport uses it now).
add_requires("agentsdk")
-- slang shader compiler (slangc) — enabled by default for shader compilation
option("with_slang", {default = false, showmenu = true, description = "Enable slang shader compiler (slangc)"})
if has_config("with_slang") then
    add_requires("slang v2026.16", {configs = {shared = true, slangc = true, slang_glslang = true}})
end

-- Optional multiplayer — disabled by default in xmake until OpenSSL/Abseil fixed
-- Enable with: xmake f --weasel_enable_multiplayer=y
if is_config("weasel_enable_multiplayer", true) then
    add_requires("gamenetworkingsockets v1.6.0", {optional = true})
end

-- Editor-only custom packages — mark optional so config can succeed even if they fail
add_requires("imsearch", {optional = true})
add_requires("imgizmo2d", {optional = true})
add_requires("imnodeflow", {optional = true})
add_requires("imguitextselect", {optional = true})
add_requires("imguicolortextedit", {optional = true})

-- daslang — optional for now (binary bundle vs source build mismatch)
option("with_daslang", {default = true, showmenu = true, description = "Enable daslang scripting"})
if has_config("with_daslang") then
    add_requires("daslang")
end

-- C++20 modules — Phase C (ABI-breaking, single wsl.core)
option("with_modules", {default = false, showmenu = true, description = "Enable C++20 modules (wsl.core)"})

-- Editor support: builds the `editor_support` library (ImGui editor
-- framework + inspector widgets) plus the editor binaries (`weasel` ImGui
-- editor and `weasel-rmlui` RmlUi editor). The core library (`libwsl`) is
-- ImGui-free in every configuration; disable this for runtime-only builds
-- (cli/mcp/custom front-ends).
option("weasel_editor_support", {default = true, showmenu = true, description = "Build the editor support library and editor binaries"})

-- always need doctest for tests
add_requires("doctest")

-- ---------------------------------------------------------------------------
-- Helpers
-- ---------------------------------------------------------------------------
-- shader sources (mirrors CMake)
local vertex_shaders = {
    "rsc/shaders/cube.vert.slang",
    "rsc/shaders/flat.vert.slang",
    "rsc/shaders/ui.vert.slang",
    "rsc/shaders/fullscreen.vert.slang",
    "rsc/shaders/skybox.vert.slang",
    "rsc/shaders/shadow_depth.vert.slang",
    "rsc/shaders/point_shadow.vert.slang",
    "rsc/shaders/ssao_prepass.vert.slang",
    "rsc/shaders/outline.vert.slang",
    "rsc/shaders/grid.vert.slang",
    "rsc/shaders/sprite_2d.vert.slang",
    "rsc/shaders/editor_blit.vert.slang",
}
local fragment_shaders = {
    "rsc/shaders/cube.frag.slang",
    "rsc/shaders/unlit.frag.slang",
    "rsc/shaders/preview_bg.frag.slang",
    "rsc/shaders/flat.frag.slang",
    "rsc/shaders/ui.frag.slang",
    "rsc/shaders/ui_texture.frag.slang",
    "rsc/shaders/sprite_2d.frag.slang",
    "rsc/shaders/skybox.frag.slang",
    "rsc/shaders/procedural_skybox.frag.slang",
    "rsc/shaders/brdf_lut.frag.slang",
    "rsc/shaders/ibl_prefilter.frag.slang",
    "rsc/shaders/ibl_irradiance.frag.slang",
    "rsc/shaders/bloom_blur.frag.slang",
    "rsc/shaders/bloom_downsample.frag.slang",
    "rsc/shaders/composite_tonemap.frag.slang",
    "rsc/shaders/shadow_depth.frag.slang",
    "rsc/shaders/point_shadow.frag.slang",
    "rsc/shaders/ssao_prepass.frag.slang",
    "rsc/shaders/ssao.frag.slang",
    "rsc/shaders/ssao_blur.frag.slang",
    "rsc/shaders/outline.frag.slang",
    "rsc/shaders/equirect_to_cube.frag.slang",
    "rsc/shaders/grid.frag.slang",
    "rsc/shaders/editor_blit.frag.slang",
}
local compute_shaders = {
    "rsc/shaders/cluster_build.slang",
    "rsc/shaders/light_cull.slang",
}

-- ---------------------------------------------------------------------------
-- wsl — core engine shared library
-- ---------------------------------------------------------------------------
target("wsl")
    set_kind("static")
    set_languages("c++20")
    -- PIC required for shared libs that may link against this static lib
    add_cxflags("-fPIC")

    -- sources — mirror src/wsl/CMakeLists.txt GLOB_RECURSE
    if has_config("with_modules") then
        add_files("src/wsl/core/core.cppm", {public = true})
        -- Phase C: all converted implementation units
        for _, f in ipairs(os.files(path.join(os.projectdir(), "src/wsl/**_module.cpp"))) do
            if not f:find("das/") and not f:find("ai/") and not f:find("runtime_project_module") then
                add_files(f)
            end
        end
    else
        add_files("src/wsl/**.cpp")
        -- Remove all *_module.cpp from legacy build (except runtime_project_module which is NOT a module unit)
        -- and das/wsl_api_module.cpp — a regular TU that merely has "_module" in its name.
        -- NB: compare basenames, not substrings: os.files returns native
        -- paths, so a "das/wsl_api_module" substring never matches on
        -- Windows (backslashes) and the file was silently dropped there.
        for _, f in ipairs(os.files(path.join(os.projectdir(), "src/wsl/**_module.cpp"))) do
            local name = path.filename(f)
            if name ~= "runtime_project_module.cpp" and name ~= "wsl_api_module.cpp" then
                remove_files(f)
            end
        end
        -- RenderDoc opt-out
        if not has_config("weasel_enable_renderdoc") then
            remove_files("src/wsl/gfx/renderdoc.cpp")
            remove_files("src/wsl/gfx/renderdoc_load_apple.cpp")
            remove_files("src/wsl/gfx/renderdoc_load_linux.cpp")
            remove_files("src/wsl/gfx/renderdoc_load_windows.cpp")
        end
        add_files("cmake/stb/stb_image_impl.c")
    end

    -- handle renderdoc opt-out and daslang opt-out (keep das_engine.cpp stub)
    on_load(function (target)
        if is_config("weasel_enable_renderdoc", false) then
            target:remove("files", "src/wsl/gfx/renderdoc*.cpp")
        end
        if not has_config("with_daslang") then
            target:remove("files", "src/wsl/das/das_ecs_binds.cpp", "src/wsl/das/das_interop.cpp", "src/wsl/das/das_system_adapter.cpp", "src/wsl/das/wsl_api_module.cpp", "src/wsl/das/wsl_event_binds.cpp")
        end
    end)

    if has_config("with_modules") then
        set_languages("c++20")
        set_policy("build.c++.modules", true)
        add_defines("WSL_MODULE_BUILD")
        add_packages("meshoptimizer", {public = true})
        add_packages("fastgltf", {public = true})
        add_packages("simdjson", {public = true})
        add_packages("libarchive", {public = true})
        -- 32 impl units validated in module build; 48 remaining need manual GMF tuning
    end

    add_includedirs("src", {public = true})
    add_includedirs("src/wsl", {public = true})
    -- RmlUi backend headers (RmlUi/Backends is not in standard include path)
    -- we handle via add_packages(rmlui) include + manual add_includedirs after package install is tricky
    -- but we add backend sources directly:
    -- they will be added via after_load hook that locates rmlui package installdir

    add_defines("CPP_RTTI_ENABLED", "RMLUI_SDL_VERSION_MAJOR=3", "RMLUI_STATIC_LIB", "RMLUI_NUM_MSAA_SAMPLES=4", "SPDLOG_COMPILED_LIB", "TRACY_ENABLE", "TRACY_ON_DEMAND", "TRACY_HAS_CALLGRIND=0", {public = true})
    add_defines("WSL_HAS_BOX3D=1")
    add_defines("WSL_PHYSICS_BACKEND_BOX3D=1", {public = true})
    add_packages("box3d")
    -- NOTE: libwsl is ImGui-free. Editor UI code (framework + inspector
    -- widgets) lives in the editor_support target.
    if is_config("weasel_enable_multiplayer", true) then
        add_defines("WEASEL_ENABLE_MULTIPLAYER", {public = true})
    end
    if has_config("weasel_enable_renderdoc") then
        add_defines("WEASEL_ENABLE_RENDERDOC")
        add_includedirs("cmake/renderdoc")
    end
    add_defines("WEASEL_SOURCE_DIR=\"" .. projectdir_fwd .. "\"")
    add_defines("WEASEL_BUILD_DIR=\"" .. projectdir_fwd .. "/build\"")
    -- compiler exe path for DAS AOT cache (like CMake's WEASEL_CXX_COMPILER)
    add_defines("WEASEL_CXX_COMPILER=\"clang++\"")

    add_cxxflags("-Wall", "-Wextra", "-Wpedantic", "-rdynamic", {force = true})

    -- packages — core deps (slang/gns/daslang conditional)
    add_packages("entt", "glm", "spdlog", "fmt", "reflect-cpp", "box3d", "rmlui", "fastgltf", "meshoptimizer", "simdjson", "libsdl3", "libsdl3_image", "libsdl3_mixer", "tracy", "stb", "nlohmann_json", "libarchive", "rapidjson", {public = false})
    add_defines("REFLECT_CPP_C_ARRAYS_OR_INHERITANCE", {public = true})
    if has_config("with_slang") then add_packages("slang") end
    -- Public packages exported to downstream targets (editor, cli, mcp-server, tests)
    add_packages("tracy", {public = true})
    add_packages("entt", {public = true})
    add_packages("box3d", {public = true})
    add_packages("glm", {public = true})
    add_packages("spdlog", {public = true})
    add_packages("fmt", {public = true})
    add_packages("reflect-cpp", {public = true})
    add_packages("rmlui", {public = true})
    add_packages("fastgltf", {public = true})
    add_packages("meshoptimizer", {public = true})
    add_packages("simdjson", {public = true})
    add_packages("libarchive", {public = true})
    add_packages("libsdl3", {public = true})
    add_packages("libsdl3_image", {public = true})
    add_packages("libsdl3_mixer", {public = true})
    add_packages("stb", {public = true})
    add_packages("nlohmann_json", {public = true})
    add_packages("ozz-animation", {public = true})

    if is_config("weasel_enable_multiplayer", true) then
        add_packages("gamenetworkingsockets", {optional = true})
    end

    -- daslang — if available
    if is_config("with_daslang", true) then
        add_packages("daslang", {public = true})
    add_defines("WEASEL_HAS_DASLANG=1", {public = true})
    else
        add_defines("WEASEL_HAS_DASLANG=0", {public = true})
    end

    -- daslang daslib root: resolved at RUNTIME by das_engine.cpp
    -- (WEASEL_DASLANG_ROOT env var, then the newest xmake package install).
    -- Target hooks (on_load/after_load/on_config) proved unreliable across
    -- xmake runs for injecting the package installdir as a define.

    -- link system libs (AF_UNIX sockets need ws2_32 on Windows)
    if is_plat("windows") then
        add_syslinks("ws2_32")
    else
        add_syslinks("dl", "pthread")
    end

    -- RmlUi backend sources — compile them as part of wsl
    -- They live in the rmlui package's Backends dir (cached source, not installdir)
    on_load(function (target)
        local rmlui = target:pkg("rmlui")
        if rmlui then
            local backend_dir = path.join(rmlui:installdir(), "Backends")
            if os.isdir(backend_dir) then
                target:add("files", path.join(backend_dir, "RmlUi_Platform_SDL.cpp"))
                target:add("files", path.join(backend_dir, "RmlUi_Renderer_SDL_GPU.cpp"))
                target:add("includedirs", backend_dir)
            end
        end
    end)

    -- slangc path define — if slang package provides binary, set WEASEL_SLANGC_PATH
    -- NB: inert in practice (this hook does not run on every xmake invocation);
    -- shader_compiler.cpp falls back to WEASEL_SLANGC env or the slangc that
    -- after_build deploys next to the executable.
    on_config(function (target)
        local box3d = target:pkg("box3d")
        if box3d then
            -- Module dependency scanning does not reliably consume package
            -- -isystem paths for this C header, so provide a direct -I path.
            target:add("includedirs", path.join(box3d:installdir(), "include"))
        end
        if is_config("with_slang", true) then
            local slang = target:pkg("slang")
            if slang then
                local slangc = path.join(slang:installdir(), "bin", "slangc")
                if os.isfile(slangc) then
                    target:add("defines", "WEASEL_SLANGC_PATH=\"" .. slangc .. "\"")
                else
                    target:add("defines", "WEASEL_SLANGC_PATH=\"$(builddir)/slangc\"")
                end
            end
        else
            target:add("defines", "WEASEL_SLANGC_PATH=\"slangc\"")
        end
    end)

    -- Deploy slang runtime plugins & slangc next to build dir (mirror CMake POST_BUILD)
    after_build(function (target)
        if is_config("with_slang", true) then
            local slang = target:pkg("slang")
            if slang then
                local bindir = slang:installdir("bin")
                if os.isdir(bindir) then
                    for _, name in ipairs({"libslang-glslang.so", "libslang-glsl-module.so", "slangc"}) do
                        local src = path.join(bindir, name)
                        if os.isfile(src) then
                            os.cp(src, target:targetdir())
                        end
                    end
                end
            end
        end
        -- copy otf, icons to both targetdir and builddir (engine looks in builddir)
        local buildir = target:targetdir()
        local project_buildir = path.join(os.projectdir(), "build")
        for _, d in ipairs({buildir, project_buildir}) do
            os.rm(path.join(d, "otf"))
            os.rm(path.join(d, "icons"))
            os.cp(path.join(os.projectdir(), "rsc/otf"), path.join(d, "otf"))
            os.cp(path.join(os.projectdir(), "rsc/icons"), path.join(d, "icons"))
        end
        -- compile_shaders output will be in $(buildir)/compiled_shaders
        -- daslib copy: engine modules (weasel_ecs, weasel_helpers) must be
        -- visible under <das-root>/daslib so `require daslib/weasel_ecs` works
        local daslib_src = path.join(os.projectdir(), "src/wsl/das/modules/weasel_ecs.das")
        if os.isfile(daslib_src) then
            -- try to locate daslang sourcedir
            local das = target:pkg("daslang")
            if das then
                local daslib_dst = path.join(das:installdir(), "daslib")
                os.cp(daslib_src, daslib_dst)
                os.cp(path.join(os.projectdir(), "src/wsl/das/modules/weasel_helpers.das"), daslib_dst)
                -- also copy to build daslib for dev
                os.cp(daslib_src, path.join(os.projectdir(), "build/daslib"))
                os.cp(path.join(os.projectdir(), "src/wsl/das/modules/weasel_helpers.das"), path.join(os.projectdir(), "build/daslib"))
            end
        end
    end)

-- ---------------------------------------------------------------------------
-- compile_shaders — shader compilation via slangc
-- ---------------------------------------------------------------------------
target("compile_shaders")
    set_kind("phony")
    on_build(function (target)
        -- locate slangc
        local slangc = nil
        local slang_libdir = nil
        -- 1. xmake slang package
        local slang = target:pkg("slang")
        if slang then
            local cand = path.join(slang:installdir(), "bin", "slangc")
            if os.isfile(cand) then slangc = cand; slang_libdir = path.join(slang:installdir(), "lib") end
        end
        -- 2. ~/.local/bin (pre-built binary)
        if not slangc then
            local home = os.getenv("HOME") or "~"
            local cand = path.join(home, ".local", "bin", "slangc")
            if os.isfile(cand) then
                slangc = cand
                slang_libdir = path.join(home, ".local", "lib")
            end
        end
        -- 3. xmake package cache (search all hash dirs)
        if not slangc then
            local home = os.getenv("HOME") or "~"
            local dirs = os.dirs(path.join(home, ".xmake", "packages", "s", "slang", "*", "*"))
            if dirs then
                for _, d in ipairs(dirs) do
                    local cand = path.join(d, "bin", "slangc")
                    if os.isfile(cand) then slangc = cand; slang_libdir = path.join(d, "lib"); break end
                end
            end
        end
        -- 4. system paths
        if not slangc then
            for _, p in ipairs({"/usr/local/bin/slangc", "/usr/bin/slangc"}) do
                if os.isfile(p) then slangc = p; break end
            end
        end
        if not slangc then
            print("compile_shaders: slangc not found, skipping shader compilation")
            print("  Install slangc or run: xmake f --with_slang=y && xmake require slang")
            return
        end
        -- Set LD_LIBRARY_PATH for slangc shared libs
        if slang_libdir and os.isdir(slang_libdir) then
            os.setenv("LD_LIBRARY_PATH", slang_libdir .. ":" .. (os.getenv("LD_LIBRARY_PATH") or ""))
        end
        local shader_dir = path.join(os.projectdir(), "rsc/shaders")
        local outdir = path.join(os.projectdir(), "build", "compiled_shaders")
        if not os.isdir(outdir) then os.mkdir(outdir) end
        -- copy pbr_common (module/include, not compiled)
        os.cp(path.join(shader_dir, "pbr_common.slang"), path.join(outdir, "pbr_common.slang"))
        -- skinning is an include-only module, but it is also compiled into
        -- per-shader skinned variants below.
        os.cp(path.join(shader_dir, "skinning.slang"), path.join(outdir, "skinning.slang"))
        -- determine target
        local slang_target = "spirv"
        local ext = ".spv"
        local dxc_flags = ""
        local spirv_flags = "-emit-spirv-via-glsl -fvk-use-entrypoint-name"
        if is_plat("windows") then
            slang_target = "dxil"
            ext = ".dxil"
            dxc_flags = "-Xdxc -Vd"
        elseif is_plat("macosx") then
            slang_target = "metal"
            ext = ".metal"
            spirv_flags = ""
        end
        local shift = "-fvk-b-shift 0 3 -fvk-s-shift 0 2 -fvk-t-shift 0 2 -fvk-u-shift 0 2"
        -- Vertex shaders: cbuffers live in set 1 (SDL vertex uniform set),
        -- textures/samplers in set 0. The generic shifts above would
        -- misplace the vertex uniform cbuffer and the GPU would sample
        -- nothing (black game view).
        local vert_shift = "-fvk-b-shift 0 1 -fvk-t-shift 0 0 -fvk-s-shift 0 0"
        -- Compute shaders declare explicit [[vk::binding]] attributes;
        -- passing any -fvk-*-shift flag makes slang ignore them.
        local compute_shift = ""
        -- Include-only modules. A vertex shader's output depends on these, so
        -- editing one must invalidate the artifacts that #include it. The
        -- previous check compared only the entry file's mtime, which made every
        -- edit to skinning.slang a silent no-op: the `_skinned` variants kept
        -- their stale bytecode and the build reported success.
        local shader_includes = {
            path.join(shader_dir, "skinning.slang"),
            path.join(shader_dir, "pbr_common.slang"),
        }
        -- Newest mtime across a shader and its includes, or nil if any is
        -- missing (in which case the shader is simply rebuilt).
        local function newest_input(src)
            local newest = nil
            local function consider(f)
                if not os.isfile(f) then newest = nil; return end
                local m = os.mtime(f)
                if newest == nil or m > newest then newest = m end
            end
            consider(src)
            if newest == nil then return nil end
            for _, inc in ipairs(shader_includes) do
                consider(inc)
                if newest == nil then return nil end
            end
            return newest
        end
        local function compile_one(src, entry, profile, shifts, defines, outname, force)
            local base = path.filename(src)
            local out = path.join(outdir, (outname or base) .. ext)
            -- only compile if src and every include are older than out
            local input_mtime = newest_input(src)
            if not force and input_mtime and os.isfile(out) and os.mtime(out) > input_mtime then return end
            local argv = {src, "-target", slang_target}
            if profile ~= "" then table.insert(argv, "-profile"); table.insert(argv, profile) end
            table.insert(argv, "-entry"); table.insert(argv, entry)
            if defines then for _, d in ipairs(defines) do table.insert(argv, "-D" .. d) end end
            table.insert(argv, "-o"); table.insert(argv, out)
            for _, s in ipairs(shifts:split("%s")) do if s ~= "" then table.insert(argv, s) end end
            for _, f in ipairs(dxc_flags:split("%s")) do if f ~= "" then table.insert(argv, f) end end
            for _, f in ipairs(spirv_flags:split("%s")) do if f ~= "" then table.insert(argv, f) end end
            print("Compiling shader: " .. path.filename(out))
            os.execv(slangc, argv)
        end
        -- Vertex shaders that draw skinned meshes also get a SKINNING build.
        -- The plain build stays free of the palette cbuffer, so meshes without
        -- animation neither bind nor pay for it.
        --
        -- Output naming: `cube.vert.slang` -> `cube_skinned.vert.slang`. The
        -- marker goes *before* the stage so the produced name matches what the
        -- renderer asks for; see the cross-check below.
        local skinned_vertex_shaders = {
            ["cube.vert.slang"] = true,
            ["shadow_depth.vert.slang"] = true,
            ["point_shadow.vert.slang"] = true,
            ["ssao_prepass.vert.slang"] = true,
            ["outline.vert.slang"] = true,
        }
        -- Compile all shaders
        local shaders = os.files(path.join(shader_dir, "*.slang"))
        for _, src in ipairs(shaders) do
            -- `name` drops the ".slang" suffix (the patterns below match on
            -- it); `filename` keeps the full file name for the skinned
            -- variant lookup and its output name.
            local filename = path.filename(src)
            local name = path.basename(src)
            -- pbr_common.slang / skinning.slang are modules/includes, never
            -- compiled standalone.
            if filename == "pbr_common.slang" or filename == "skinning.slang" then
            elseif name:find("%.vert$") then
                compile_one(src, "vsMain", "vs_6_0", vert_shift)
                if skinned_vertex_shaders[filename] then
                    local skinned_name = filename:gsub("%.vert%.slang$",
                                                       "_skinned.vert.slang")
                    compile_one(src, "vsMain", "vs_6_0", vert_shift,
                                {"SKINNING"}, skinned_name)
                end
            elseif name:find("%.frag$") then
                compile_one(src, "fsMain", "ps_6_0", shift)
            elseif name == "cluster_build" or name == "light_cull" then
                compile_one(src, "csMain", "cs_6_0", compute_shift)
            else
                print("Skipping unknown shader: " .. path.filename(src))
            end
        end
        -- Cross-check: every `compiled_shaders/*_skinned*.spv` path the
        -- renderer asks for must correspond to a file this step produces.
        -- A silent naming drift here is invisible until runtime, where the
        -- skinned pipelines simply fail to load and animated meshes render
        -- undeformed, so fail the build instead.
        local produced = {}
        for _, f in ipairs(os.files(path.join(outdir, "*.spv"))) do
            produced[path.filename(f)] = true
        end
        local missing = {}
        for _, f in ipairs(os.files(path.join(os.projectdir(), "src/wsl/**/*.cpp"))) do
            local text = io.open(f, "r")
            if text then
                local content = text:read("*a")
                text:close()
                for name in content:gmatch("compiled_shaders/([%w_]+skinned[%w_.]*)%.spv") do
                    if not produced[name .. ".spv"] then
                        table.insert(missing, name .. ".spv")
                    end
                end
            end
        end
        if #missing > 0 then
            table.sort(missing)
            local list = table.concat(missing, ", ")
            raise("compile_shaders: the renderer requests skinned shaders this step does not produce: " .. list)
        end

        print("compile_shaders: done (" .. #shaders .. " shaders processed)")
    end)

-- ---------------------------------------------------------------------------
-- docs — sphinx + hawkmoth + das_api_gen (mirrors the old CMake docs target)
-- ---------------------------------------------------------------------------
target("docs")
    set_kind("phony")
    -- Docs require sphinx + hawkmoth (only installed in the docs CI job),
    -- so never build them as part of a plain `xmake build`.
    set_default(false)
    on_build(function (target)
        import("lib.detect.find_tool")
        local projectdir = os.projectdir()
        local py = find_tool("python3") and "python3" or "python"
        os.execv(py, {
            path.join(projectdir, "doc/source/das_api_gen.py"),
            "--source", path.join(projectdir, "src"),
            "--output", path.join(projectdir, "doc/source/stdlib")
        })
        os.execv("sphinx-build", {
            "--keep-going", "-b", "html",
            path.join(projectdir, "doc/source"),
            path.join(projectdir, "build/docs")
        })
    end)

-- ---------------------------------------------------------------------------
-- ---------------------------------------------------------------------------
-- editor_support — ImGui editor framework + inspector widgets (not in core)
-- ---------------------------------------------------------------------------
target("editor_support")
    set_kind("static")
    set_languages("c++20")
    set_enabled(is_config("weasel_editor_support", true))
    add_files("src/editor_support/*.cpp")
    add_includedirs("src", {public = true})
    add_deps("wsl")
    add_packages("imgui", {public = true})
    add_defines("CPP_RTTI_ENABLED", "IMGUI_DEFINE_MATH_OPERATORS")
    add_cxxflags("-Wall", "-Wextra", "-Wpedantic")

-- ---------------------------------------------------------------------------
-- weasel — editor binary
-- ---------------------------------------------------------------------------
target("weasel")
    set_kind("binary")
    set_languages("c++20")
    set_enabled(is_config("weasel_editor_support", true))
    add_files("src/editor/**.cpp")
    add_includedirs("src", {public = true})
    add_deps("wsl", "editor_support", "cli")
    -- Packages already exported public by wsl are resolved transitively.
    -- imgui is NOT public on wsl anymore (deeper decoupling), so the editor
    -- lists it explicitly for its panels and the ImGui backend.
    -- DO NOT re-list wsl's public packages here — xmake links them
    -- BEFORE -lwsl, causing unresolved symbols from cross-archive deps.
    add_packages("imgui")
    add_packages("cli11")
    -- chat_panel is the only A2A/ACP consumer left in the engine.
    add_packages("agentsdk")
    add_packages("imguitextselect", {optional = true})
    add_packages("imsearch", {optional = true})
    add_packages("imguicolortextedit", {optional = true})
    add_packages("imgizmo2d", {optional = true})
    add_packages("imnodeflow", {optional = true})
    if has_config("with_slang") then add_packages("slang") end
    add_defines("CPP_RTTI_ENABLED", "IMGUI_DEFINE_MATH_OPERATORS")
    if is_config("weasel_enable_multiplayer", true) then add_defines("WEASEL_ENABLE_MULTIPLAYER") end
    add_defines("WEASEL_SOURCE_DIR=\"" .. projectdir_fwd .. "\"", "WEASEL_BUILD_DIR=\"" .. projectdir_fwd .. "/build\"", "TRACY_ENABLE")
    if has_config("weasel_enable_renderdoc") then
        add_defines("WEASEL_ENABLE_RENDERDOC")
        add_includedirs("cmake/renderdoc")
    end
    add_cxxflags("-Wall", "-Wextra", "-Wpedantic", "-rdynamic")
    if is_plat("windows") then
        add_syslinks("ws2_32")
    else
        add_syslinks("dl", "pthread")
    end
    add_deps("compile_shaders")
    -- install rules
    set_installdir("$(bindir)")
    set_prefixdir("$(install_prefix)")
    add_installfiles("$(builddir)/$(arch)/$(mode)/weasel", {prefixdir = "bin"})
    add_installfiles(path.join(os.projectdir(), "build", "compiled_shaders", "*"), {prefixdir = "share/weasel/compiled_shaders"})
    add_installfiles(path.join(os.projectdir(), "rsc/otf", "*"), {prefixdir = "share/weasel/otf"})
    add_installfiles(path.join(os.projectdir(), "rsc/icons", "*"), {prefixdir = "share/weasel/icons"})
    after_build(function (target)
        -- bundle handling for macOS omitted for brevity — will add later
        if is_config("with_slang", true) then
            local slang = target:pkg("slang")
            if slang then
                local bindir = slang:installdir("bin")
                if os.isdir(bindir) then
                    for _, name in ipairs({"libslang-glslang.so", "libslang-glsl-module.so", "slangc"}) do
                        local src = path.join(bindir, name)
                        if os.isfile(src) then os.cp(src, target:targetdir()) end
                    end
                end
            end
        end
    end)

-- ---------------------------------------------------------------------------
-- weasel-rmlui — next-generation editor with an RmlUi (HTML/CSS) UI
-- ---------------------------------------------------------------------------
target("weasel-rmlui")
    set_kind("binary")
    set_languages("c++20")
    set_enabled(is_config("weasel_editor_support", true))
    add_files("src/editor_rmlui/**.cpp")
    add_includedirs("src", {public = true})
    add_deps("wsl", "cli")
    -- rmlui is public on wsl; adding it here lets the on_load hook resolve the
    -- Backends include dir (RmlUi_Platform_SDL.h / RmlUi_Renderer_SDL_GPU.h).
    add_packages("rmlui")
    add_packages("cli11")
    -- Agent tab (ACP chat) needs the agent SDK, like chat_panel in weasel.
    add_packages("agentsdk")
    if has_config("with_slang") then add_packages("slang") end
    add_defines("CPP_RTTI_ENABLED")
    add_defines("WEASEL_SOURCE_DIR=\"" .. projectdir_fwd .. "\"", "WEASEL_BUILD_DIR=\"" .. projectdir_fwd .. "/build\"", "TRACY_ENABLE")
    if is_config("weasel_enable_multiplayer", true) then add_defines("WEASEL_ENABLE_MULTIPLAYER") end
    add_cxxflags("-Wall", "-Wextra", "-Wpedantic", "-rdynamic")
    -- Debug symbols for the target's own objects (cheap; the core lib stays
    -- release) so crashes in the editor layer are diagnosable.
    set_symbols("debug")
    if is_plat("windows") then
        add_syslinks("ws2_32")
    else
        add_syslinks("dl", "pthread")
    end
    add_deps("compile_shaders")
    -- RmlUi backend headers (compiled into wsl, headers needed here)
    on_load(function (target)
        local rmlui = target:pkg("rmlui")
        if rmlui then
            local backend_dir = path.join(rmlui:installdir(), "Backends")
            if os.isdir(backend_dir) then
                target:add("includedirs", backend_dir)
            end
        end
    end)
    -- install rules
    set_installdir("$(bindir)")
    set_prefixdir("$(install_prefix)")
    add_installfiles("$(builddir)/$(arch)/$(mode)/weasel-rmlui", {prefixdir = "bin"})
    add_installfiles(path.join(os.projectdir(), "build", "compiled_shaders", "*"), {prefixdir = "share/weasel/compiled_shaders"})
    add_installfiles(path.join(os.projectdir(), "rsc/otf", "*"), {prefixdir = "share/weasel/otf"})
    add_installfiles(path.join(os.projectdir(), "rsc/icons", "*"), {prefixdir = "share/weasel/icons"})
    add_installfiles(path.join(os.projectdir(), "rsc/editor_rml", "*"), {prefixdir = "share/weasel/editor_rml"})

-- ---------------------------------------------------------------------------
-- cli — static lib + weasel-cli binary
-- ---------------------------------------------------------------------------
target("cli")
    set_kind("static")
    set_languages("c++20")
    add_files("src/cli/*.cpp")
    remove_files("src/cli/main.cpp")
    add_includedirs("src")
    add_deps("wsl")
    add_packages("cli11")

target("weasel-cli")
    set_kind("binary")
    set_languages("c++20")
    add_files("src/cli/main.cpp")
    add_deps("cli", "wsl")
    add_packages("cli11", "reflect-cpp")
    add_includedirs("src")
    add_cxxflags("-Wall", "-Wextra", "-Wpedantic")
    if is_plat("windows") then
        add_syslinks("ws2_32")
    end
    add_installfiles("$(builddir)/$(arch)/$(mode)/weasel-cli", {prefixdir = "bin"})

-- ---------------------------------------------------------------------------
-- mcp-server — static lib + binary
-- ---------------------------------------------------------------------------
target("mcp_server_lib")
    set_kind("static")
    set_languages("c++20")
    add_files("src/mcp-server/*.cpp")
    remove_files("src/mcp-server/main.cpp")
    add_includedirs("src")
    add_deps("wsl")
    add_packages("cli11", "agentsdk", "simdjson")

target("weasel-mcp-server")
    set_kind("binary")
    set_languages("c++20")
    add_files("src/mcp-server/main.cpp")
    add_deps("mcp_server_lib", "wsl")
    add_packages("cli11", "agentsdk", "simdjson")
    add_includedirs("src")
    add_cxxflags("-Wall", "-Wextra", "-Wpedantic")
    if is_plat("windows") then
        add_syslinks("ws2_32")
    end
    add_installfiles("$(builddir)/$(arch)/$(mode)/weasel-mcp-server", {prefixdir = "bin"})

-- ---------------------------------------------------------------------------
-- Tests — mirrors CMake enable_testing()
-- ---------------------------------------------------------------------------
target("weasel_cli_tests")
    set_kind("binary")
    set_languages("c++20")
    add_files("tests/weasel-cli/*.cpp")
    add_deps("cli")
    add_packages("doctest")
    add_includedirs("src")
    add_tests("weasel_cli_tests")

target("weasel_mcp_server_tests")
    set_kind("binary")
    set_languages("c++20")
    add_files("tests/mcp-server/*.cpp")
    add_deps("mcp_server_lib")
    add_packages("doctest", "agentsdk", "simdjson")
    add_includedirs("src")
    add_defines("WSL_SOURCE_DIR=\"" .. projectdir_fwd .. "\"")
    add_tests("weasel_mcp_server_tests")

target("weasel_core_tests")
    set_kind("binary")
    set_languages("c++20")
    add_files("tests/weasel-core/test_event_bus.cpp", "tests/weasel-core/test_resource_ids.cpp", "tests/weasel-core/test_math_module.cpp", "tests/weasel-core/test_serialize_roundtrip.cpp", "tests/weasel-core/test_ozz_smoke.cpp", "tests/weasel-core/test_ozz_loader.cpp", "tests/weasel-core/test_model_skin.cpp", "tests/weasel-core/test_animation_system.cpp", "tests/weasel-core/test_scene_component_stream.cpp", "tests/weasel-core/test_animation_import.cpp", "tests/weasel-core/test_scene_move.cpp", "tests/weasel-core/test_deferred_stop.cpp", "tests/weasel-core/test_physics_debug_draw.cpp", "tests/weasel-core/test_scene_loader.cpp", "tests/weasel-core/test_viewport_resolve.cpp", "tests/weasel-core/test_viewport_environment.cpp")
    add_deps("wsl")
    add_packages("doctest", "simdjson")
    add_includedirs("src")
    add_defines("WEASEL_SOURCE_DIR=\"" .. projectdir_fwd .. "\"")
    add_tests("weasel_core_tests")

target("weasel_das_tests")
    set_kind("binary")
    set_languages("c++20")
    set_enabled(has_config("with_daslang"))
    add_files("tests/weasel-das/test_component_accessors.cpp")
    add_deps("wsl")
    add_packages("doctest")
    add_includedirs("src")
    add_tests("weasel_das_tests")
