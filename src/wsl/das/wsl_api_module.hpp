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
void wsl_log_trace (const char *msg);
/** Activates a scene system by engine display name. Unknown names fail. */
bool wsl_enable_system (const char *name);
/** Deactivates a scene system by engine display name. Unknown names fail. */
bool wsl_disable_system (const char *name);
/** True when the named scene system exists and is active. */
bool wsl_system_enabled (const char *name);

/** Wall-clock seconds since process start; unaffected by pause/scale. */
float wsl_get_elapsed_time ();
/** Simulated fixed-step seconds; advances once per completed fixed step. */
float wsl_get_fixed_elapsed ();
/** Advances the fixed-step clock. Installed as the runtime hook by das. */
void wsl_api_notify_fixed_step (double fixed_dt);
/** Fixed physics step in seconds (engine value, 1/60 fallback). */
float wsl_get_fixed_delta ();
/** Renderer frame index (0 when no runtime context is active). */
uint64_t wsl_get_frame_index ();
/** Gameplay time scale applied to script `delta` (default 1). */
float wsl_get_time_scale ();
void wsl_set_time_scale (float scale);
bool wsl_is_time_paused ();
void wsl_set_time_paused (bool paused);

/**
 * Returns the stable type ID for a world component by display name.
 * :return: The type ID, or 0 if not found.
 */
uint32_t wsl_get_component_type_id (const char *display_name);

uint32_t wsl_get_active_camera ();

/** Typed world/entity facade used by the canonical `weasel` module. */
uint64_t wsl_current_world ();
uint32_t wsl_world_create_entity (uint64_t world);
bool wsl_world_destroy_entity (uint64_t world, uint64_t entity_world,
                               uint32_t entity_id, ::das::Context *context,
                               ::das::LineInfoArg *at);
bool wsl_world_entity_is_alive (uint64_t world, uint64_t entity_world,
                                uint32_t entity_id);
bool wsl_world_entity_is_null (uint64_t entity_world, uint32_t entity_id);
bool wsl_ui_bind_button (const char *element_id, const char *method_name);
uint32_t wsl_world_find_entity_by_name (uint64_t world, const char *name);
bool wsl_world_set_entity_name (uint64_t world, uint64_t entity_world,
                                uint32_t entity_id, const char *name);
bool wsl_world_set_parent (uint64_t world, uint64_t child_world,
                           uint32_t child_id, uint64_t parent_world,
                           uint32_t parent_id, bool keep_world,
                           ::das::Context *context, ::das::LineInfoArg *at);
uint32_t wsl_world_get_parent (uint64_t world, uint64_t entity_world,
                               uint32_t entity_id);
uint32_t wsl_world_get_child_count (uint64_t world, uint64_t entity_world,
                                    uint32_t entity_id);
uint32_t wsl_world_get_child (uint64_t world, uint64_t entity_world,
                              uint32_t entity_id, uint32_t index);
bool wsl_world_change_scene (uint64_t world, const char *scene_name);
uint32_t wsl_world_component_type_id (uint64_t world, const char *type_name);
uint32_t wsl_world_entity_id (uint64_t world, uint64_t entity_world,
                              uint32_t entity_id);
uint32_t wsl_world_entity_from_id (uint64_t world, uint32_t entity_id);
bool wsl_world_has_component (uint64_t world, uint64_t entity_world,
                              uint32_t entity_id, uint32_t type_id);
bool wsl_world_has_animator (uint64_t world, uint64_t entity_world,
                             uint32_t entity_id);
bool wsl_world_add_component (uint64_t world, uint64_t entity_world,
                              uint32_t entity_id, uint32_t type_id,
                              ::das::Context *context, ::das::LineInfoArg *at);
bool wsl_world_remove_component (uint64_t world, uint64_t entity_world,
                                 uint32_t entity_id, uint32_t type_id,
                                 ::das::Context *context,
                                 ::das::LineInfoArg *at);
bool wsl_world_defer_destroy (uint64_t world, uint64_t entity_world,
                              uint32_t entity_id);
bool wsl_world_defer_add_component (uint64_t world, uint64_t entity_world,
                                    uint32_t entity_id, uint32_t type_id);
bool wsl_world_defer_remove_component (uint64_t world, uint64_t entity_world,
                                       uint32_t entity_id, uint32_t type_id);
void *wsl_world_singleton_data (uint64_t world, uint32_t type_id);
uint32_t wsl_world_singleton_type_id (uint64_t world, const char *type_name);
bool wsl_world_has_singleton (uint64_t world, uint32_t type_id);
bool wsl_world_add_singleton (uint64_t world, uint32_t type_id);
bool wsl_world_remove_singleton (uint64_t world, uint32_t type_id);
void *wsl_world_component_data (uint64_t world, uint64_t entity_world,
                                uint32_t entity_id, uint32_t type_id);
void *wsl_component_default_data (uint32_t type_id);
uint32_t wsl_world_clone_entity (uint64_t world, uint64_t src_world,
                                 uint32_t src_id, uint64_t parent_world,
                                 uint32_t parent_id, ::das::Context *context,
                                 ::das::LineInfoArg *at);
bool wsl_world_destroy_entity_only (uint64_t world, uint64_t entity_world,
                                    uint32_t entity_id, ::das::Context *context,
                                    ::das::LineInfoArg *at);
uint32_t wsl_world_entity_count (uint64_t world);
uint32_t wsl_world_find_all_by_name_count (uint64_t world, const char *name);
uint32_t wsl_world_find_all_by_name_at (uint64_t world, const char *name,
                                        uint32_t index);
bool wsl_world_copy_component (uint64_t world, uint64_t dst_world,
                               uint32_t dst_id, uint64_t src_world,
                               uint32_t src_id, uint32_t type_id,
                               ::das::Context *context, ::das::LineInfoArg *at);
bool wsl_world_defer_destroy_only (uint64_t world, uint64_t entity_world,
                                   uint32_t entity_id);
bool wsl_world_defer_set_parent (uint64_t world, uint64_t child_world,
                                 uint32_t child_id, uint64_t parent_world,
                                 uint32_t parent_id, bool keep_world);
uint32_t wsl_world_defer_create_entity (uint64_t world, const char *name);

void each_world_entity_with (uint64_t world,
                             const ::das::TArray<uint32_t> &type_ids,
                             const ::das::TBlock<void, uint32_t> &blk,
                             ::das::Context *context, ::das::LineInfoArg *at);
void each_world_entity_with_except (uint64_t world,
                                    const ::das::TArray<uint32_t> &type_ids,
                                    const ::das::TArray<uint32_t> &except_ids,
                                    const ::das::TBlock<void, uint32_t> &blk,
                                    ::das::Context *context,
                                    ::das::LineInfoArg *at);
float wsl_get_event_mouse_dx ();
float wsl_get_event_mouse_dy ();
bool wsl_set_relative_mouse_mode (bool enabled);
float wsl_get_mouse_x ();
float wsl_get_mouse_y ();
float wsl_get_mouse_dx ();
float wsl_get_mouse_dy ();
bool wsl_mouse_button_down (int button);

/** Binds a named action to a keyboard scancode in the app input map. */
bool wsl_action_bind_key (const char *action, int scancode);
/** Binds a named action to a mouse button (1 = left) in the app input map. */
bool wsl_action_bind_mouse (const char *action, int button);
/** Removes a named action binding. Returns true when one was removed. */
bool wsl_action_unbind (const char *action);
/** Held state of a named action (keyboard or mouse binding). */
bool wsl_action_pressed (const char *action);
/** True when a named action began this frame (ignores key repeats). */
bool wsl_action_just_pressed (const char *action);
/** True when a named action was released this frame. */
bool wsl_action_just_released (const char *action);
/** Signed axis from two actions: +1, 0, or -1. */
float wsl_action_axis (const char *neg_action, const char *pos_action);

// ── Generic component type lookup ──

/**
 * Looks up component type ID by daScript type name.
 * :param type_name: The daScript-visible type name (e.g. "MouseRotate").
 * :return: The stable type ID, or 0 if not found.
 */
uint32_t wsl_get_component_type_id_by_name (const char *type_name);

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
/** Enables or disables looping. */
void wsl_anim_set_loop (uint32_t entity, bool loop);
void wsl_anim_pause (uint32_t entity);
void wsl_anim_resume (uint32_t entity);
bool wsl_anim_is_playing (uint32_t entity);
bool wsl_anim_is_paused (uint32_t entity);
bool wsl_anim_is_finished (uint32_t entity);
float wsl_anim_get_time (uint32_t entity);
float wsl_anim_get_duration (uint32_t entity);
bool wsl_audio_is_playing (uint32_t entity);

} // namespace wsl::das
