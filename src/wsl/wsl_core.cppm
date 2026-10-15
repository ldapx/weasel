module;
// ---------------------------------------------------------------------------
// wsl.core — the strongly-coupled engine cluster (rsc/gfx/sys/reg/das/comp/ai
// + top-level glue) as ONE named module. Its internal header cycles are
// legal inside a single module purview; cross-module deps (math/event/phys)
// arrive as imports below.
//
// The global module fragment provides every third-party + STL header the
// purview headers skip behind WSL_MODULE_BUILD. Those entities stay attached
// to the global module, shared with legacy TUs and the other modules.
// ---------------------------------------------------------------------------

#define WSL_MODULE_BUILD
#define RMLUI_SDL_VERSION_MAJOR 3
#define CPP_RTTI_ENABLED

// STL (the old stl_all.hpp set)
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <set>
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <ctime>
#include <csetjmp>
#include <cstdarg>
#include <cctype>
#include <cassert>
#include <cfloat>
#include <climits>
#include <cerrno>
#include <csignal>
#include <cwchar>
#include <type_traits>
#include <utility>
#include <optional>
#include <variant>
#include <array>
#include <span>
#include <filesystem>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <ios>
#include <ostream>
#include <istream>
#include <tuple>
#include <numeric>
#include <limits>
#include <initializer_list>
#include <any>
#include <queue>
#include <list>
#include <stack>
#include <deque>
#include <bitset>
#include <future>
#include <iterator>
#include <ranges>
#include <regex>
#include <concepts>
#include <exception>
#include <stdexcept>
#include <system_error>
#include <typeinfo>

// C system headers used directly
#include <setjmp.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>

// Third-party
#include <entt/entt.hpp>
#include "comp/component_meta.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_timer.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <RmlUi/Core.h>
#include <RmlUi/Config/Config.h>
#include <RmlUi_Platform_SDL.h>
// See ui_manager.hpp: RmlUi_Renderer_SDL_GPU.h uses `interface` as a
// parameter name, colliding with the COM macro from windows.h.
#ifdef interface
#pragma push_macro ("interface")
#undef interface
#define WSL_POP_RMLUI_INTERFACE
#endif
#include <RmlUi_Renderer_SDL_GPU.h>
#ifdef WSL_POP_RMLUI_INTERFACE
#pragma pop_macro ("interface")
#undef WSL_POP_RMLUI_INTERFACE
#endif
#include <imgui.h>
#include <imgui_internal.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/mat4x4.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include "das/daScript_all.hpp"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <tracy/Tracy.hpp>
#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/util.hpp>
#include <fastgltf/math.hpp>
#include <fastgltf/tools.hpp>
#include <archive.h>
#include <archive_entry.h>
#include <nlohmann/json.hpp>
#include <meshoptimizer.h>

export module wsl.core;

// Cross-module dependencies (imported after the module declaration, before
// any purview declaration).
import wsl.math;
import wsl.event;
import wsl.phys;

export {
#include "log/log.hpp"
#include "log/tracy_sink.hpp"
#include "event.hpp"
#include "input.hpp"
#include "ray.hpp"
#include "gfx/ui_system_interface.hpp"
#include "das/das_api_catalog.gen.hpp"
#include "das/wsl_api_module.hpp"
#include "das/das_engine.hpp"
#include "das/wsl_event_binds.hpp"
#include "das/das_ecs_binds.hpp"
#include "rsc/resource_ids.hpp"
#include "rsc/cubemap_loader.hpp"
#include "rsc/image_loader.hpp"
#include "rsc/resource_ref.hpp"
#include "rsc/project.hpp"
#include "rsc/shader_loader.hpp"
#include "rsc/cpu_model.hpp"
#include "comp/prefab_instance.hpp"
#include "comp/directional_light.hpp"
#include "comp/sprite_2d.hpp"
#include "comp/transform_2d.hpp"
#include "comp/world_transform.hpp"
#include "comp/camera.hpp"
#include "comp/area3d.hpp"
#include "comp/spot_light.hpp"
#include "comp/subviewport.hpp"
#include "comp/transform.hpp"
#include "comp/camera_2d.hpp"
#include "comp/hierarchy.hpp"
#include "comp/point_light.hpp"
#include "gfx/lighting.hpp"
#include "gfx/renderer.hpp"
#include "gfx/shader_program.hpp"
#include "gfx/tracy_gpu_mem.hpp"
#include "gfx/pipeline_cache.hpp"
#include "gfx/viewport.hpp"
#include "gfx/material.hpp"
#include "gfx/shader.hpp"
#include "gfx/cubemap.hpp"
#include "gfx/renderdoc.hpp"
#include "gfx/gpu_resources.hpp"
#include "gfx/shader_compiler.hpp"
#include "gfx/batch_renderer_2d.hpp"
#include "gfx/shader_graph.hpp"
#include "gfx/material_asset.hpp"
#include "net/command_protocol.hpp"
#include "sys/stage_registry.hpp"
#include "sys/system.hpp"
#include "sys/task_pool.hpp"
#include "sys/tracy_telemetry.hpp"
#include "sys/shadow_system.hpp"
#include "sys/physics_system.hpp"
#include "sys/render_3d_system.hpp"
#include "sys/skybox_system.hpp"
#include "sys/render_ui_system.hpp"
#include "reg/system_factory_registry.hpp"
#include "reg/runtime_project_module.hpp"
#include "reg/detail/registry_helpers.hpp"
#include "reg/registry_handle.hpp"
#include "debug/debug_renderer.hpp"
#include "das/das_interop.hpp"
#include "das/das_system_adapter.hpp"
#include "rsc/model_loader.hpp"
#include "rsc/project_loader.hpp"
#include "rsc/runtime_project_module.hpp"
#include "rsc/scene.hpp"
#include "comp/singl/physics_manager.hpp"
#include "gfx/render_context.hpp"
#include "gfx/image.hpp"
#include "gfx/subviewport_target.hpp"
#include "gfx/mesh.hpp"
#include "gfx/shader_graph_codegen.hpp"
#include "sys/render_2d_system.hpp"
#include "sys/lighting_system.hpp"
#include "sys/system_dependency_graph.hpp"
#include "sys/transform_system.hpp"
#include "sys/system_scheduler.hpp"
#include "reg/singleton_registry.hpp"
#include "reg/component_registry.hpp"
#include "reg/registry_queries.hpp"
#include "events.hpp"
#include "rsc/scene_loader.hpp"
#include "rsc/resource_manager.hpp"
#include "rsc/world.hpp"
#include "rsc/scene_snapshot_serializer.hpp"
#include "rsc/cmake_file_api.hpp"
#include "rsc/scene_manager.hpp"
#include "comp/singl/skybox_instance_3d.hpp"
#include "comp/singl/engine_resources.hpp"
#include "comp/audio.hpp"
#include "gfx/clustered_lighting.hpp"
#include "gfx/render_window.hpp"
#include "gfx/model_3d.hpp"
#include "gfx/ui_render_interface.hpp"
#include "sys/audio_system.hpp"
#include "sys/core_systems.hpp"
#include "comp/singl/ui_manager.hpp"
#include "gfx/scene_renderer.hpp"
#include "sys/render_frame.hpp"
#include "comp/singl/rendering_manager.hpp"
#include "comp/singl/runtime_context.hpp"
#include "comp/rigid_body.hpp"
#include "comp/model_instance_3d.hpp"
#include "comp/character_body.hpp"
#include "app.hpp"
#include "das/wsl_api_component_accessors.hpp"
#include "comp/singl/editor_context.hpp"
#include "comp/components.hpp"
}
