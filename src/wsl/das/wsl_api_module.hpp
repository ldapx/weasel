#pragma once

#ifndef IN_MODULE_INTERFACE
#include <entt/entt.hpp>
#endif
#ifndef IN_MODULE_INTERFACE
#include <cstdint>
#endif
#ifndef IN_MODULE_INTERFACE
#include <string>
#endif

namespace das
{
class Module;
class ModuleGroup;
class Context;
struct LineInfoArg;
template <typename TT> struct TArray;
template <typename Result, typename... Args> struct TBlock;
}

namespace wsl::event
{
class message_bus;
}

namespace wsl::comp::singl
{
class runtime_context;
}

namespace wsl::das
{

/** Entity handle returned to daslang scripts. */
struct Entity
{
  uint32_t id;
};

constexpr uint32_t NULL_ENTITY_ID = 0xFFFFFFFFu;

/** Returns the active runtime context, or nullptr when unavailable. */
comp::singl::runtime_context *try_get_runtime_context ();

/** Registers the Weasel API module with the daslang engine. */
void register_wsl_api_module (::das::ModuleGroup &module_group);

::das::Module *get_wsl_api_module ();

::das::Module *create_worker_weasel_api_module ();

/** Sets the active registry for the Weasel API module. */
entt::registry *wsl_api_set_active_registry (entt::registry *registry);

/** Current query nesting, used to repair state after a daScript longjmp. */
int wsl_api_get_query_depth () noexcept;
/** Restores query nesting after daScript bypasses C++ destructors. */
void wsl_api_restore_query_depth (int depth) noexcept;

/** Returns the message bus of the active runtime context, or nullptr. */
wsl::event::message_bus *wsl_api_get_active_message_bus ();

void wsl_log_info (const char *msg);
void wsl_log_debug (const char *msg);
void wsl_log_warn (const char *msg);
void wsl_log_error (const char *msg);

/**
 * Returns the stable type ID for a world component by display name.
 * :return: The type ID, or 0 if not found.
 */
uint32_t wsl_get_component_type_id (const char *display_name);

uint32_t wsl_get_active_camera ();

/** Iterates entities owning ALL of the listed component types and invokes `blk`
 *  with each matching entity id. `type_ids` are stable component type ids (see
 *  `_get_component_type_id_by_name`). Backs the daslang `query` macro.
 */
void each_entity_id_with (const ::das::TArray<uint32_t> &type_ids,
                          const ::das::TBlock<void, uint32_t> &blk,
                          ::das::Context *context, ::das::LineInfoArg *at);
float wsl_get_event_mouse_dx ();
float wsl_get_event_mouse_dy ();
bool wsl_has_component (uint32_t type_id, uint32_t entity);
bool wsl_set_relative_mouse_mode (bool enabled);

// ── Generic component type lookup ──

/**
 * Looks up component type ID by daScript type name.
 * :param type_name: The daScript-visible type name (e.g. "MouseRotate").
 * :return: The stable type ID, or 0 if not found.
 */
uint32_t wsl_get_component_type_id_by_name (const char *type_name);

/** Returns a pointer to an existing Daslang component payload, or nullptr. */
void *wsl_get_component_data (uint32_t entity, uint32_t type_id);

// ── Animation ──
//
// These mutate comp::animator directly. The animation system diffs clip_path
// every frame, so assigning a new path is what starts a crossfade. All of them
// are no-ops for entities without an Animator or invalid ids, so a script
// cannot fault the runtime.

/** Selects `clip` and rewinds playback to the start. */
void wsl_anim_play (uint32_t entity, const char *clip);

/** Selects `clip` and crossfades into it over `duration` seconds. */
void wsl_anim_crossfade (uint32_t entity, const char *clip, float duration);

/** Stops playback and rewinds. */
void wsl_anim_stop (uint32_t entity);

/** Sets the playback rate multiplier. */
void wsl_anim_set_speed (uint32_t entity, float speed);

/** Scrubs to `time` seconds; negative values clamp to zero. */
void wsl_anim_set_time (uint32_t entity, float time);

/** Enables or disables looping. */
void wsl_anim_set_loop (uint32_t entity, bool loop);

} // namespace wsl::das
