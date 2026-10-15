#pragma once

#include "../rsc/resource_ids.hpp"
#include "component_meta.hpp"

#ifndef IN_MODULE_INTERFACE
#include <entt/entt.hpp>
#endif
#ifndef IN_MODULE_INTERFACE
#include <string>
#endif
#ifndef IN_MODULE_INTERFACE
#include <vector>
#endif

namespace wsl
{

namespace comp::singl
{
class runtime_context;
}

namespace comp
{

/**
 * Animation playback settings for a skinned model instance.
 *
 * The path fields are the serialized/rest representation. Runtime resource
 * ids and playback transition state are kept in the component as transient
 * fields and are rebuilt by the animation system when needed.
 */
struct animator : world_component
{
  /** Path to the ozz animation clip (`.anim.ozz`). */
  std::string clip_path = "None";

  /**
   * Path to the matching ozz skeleton (`.skel.ozz`).
   *
   * When left as `None`, the animation system derives the conventional
   * `<model>_<clip>.anim.ozz` -> `<model>.skel.ozz` path.
   */
  std::string skeleton_path = "None";

  /** Index of the model skin to drive. */
  int skin_index = 0;

  /** Playback rate multiplier. */
  float speed = 1.0F;

  /** Whether playback wraps at the end of the clip. */
  bool loop = true;

  /** Duration in seconds used when crossfading to a new clip. */
  float crossfade_duration = 0.25F;

  /** Whether the animator is logically playing. */
  bool playing = true;

  /** Current playback time in seconds. */
  float time = 0.0F;

  // ---- Transient runtime state (not serialized) ----

  /** Resource id of the currently sampled clip. */
  rsc::animation_id current_animation{};

  /** Resource id of the currently loaded skeleton. */
  rsc::skeleton_id current_skeleton{};

  /** Resource id of the outgoing clip during a crossfade. */
  rsc::animation_id previous_animation{};

  /** Playback position of the outgoing clip. */
  float previous_time = 0.0F;

  /** Current crossfade weight in the range [0, 1]. */
  float blend_weight = 1.0F;

  /** Runtime pause state; the serialized `playing` flag is the authored state.
   */
  bool paused = false;

  /** Whether a non-looping clip has reached its end. */
  bool finished = false;

  /** Loop mode of the outgoing clip during a crossfade. */
  bool previous_loop = true;

  /** A clip the owning model declares, as reported by its import sidecar. */
  struct clip_ref
  {
    /** Clip name as declared in the source glTF ("Walk"). */
    std::string name;
    /** Resolved `.anim.ozz` path for that clip. */
    std::string path;
  };

  /**
   * Clips available for the model this entity draws.
   *
   * Populated at runtime by the animation system from the model's import
   * sidecar (see `animation_importer::read_manifest`), so the inspector can
   * offer a per-model dropdown without re-running the conversion. Transient:
   * it is rebuilt from the model, never serialized.
   */
  std::vector<clip_ref> available_clips;

  /** Duration of the currently assigned clip in seconds (0 when unknown). */
  float clip_duration = 0.0F;

  /** Clears runtime-only state when playback is reconfigured. */
  void
  reset_runtime_state ()
  {
    current_animation = {};
    current_skeleton = {};
    previous_animation = {};
    previous_time = 0.0F;
    blend_weight = 1.0F;
    paused = false;
    finished = false;
    previous_loop = true;
  }

  /**
   * Editor-facing UI: a per-model clip dropdown, transport controls and a
   * scrub bar. Defined in editor_support (inspectors_components.cpp).
   * @return true when a field changed and the entity should be marked dirty.
   */
  bool custom_inspect (const char *label,
                       comp::singl::runtime_context *runtime);

  static void register_meta ();

private:
  static void
  register_meta_impl ()
  {
    using namespace entt::literals;

    entt::meta_factory<comp::animator> ()
        .type (entt::type_hash<comp::animator>::value ())
        .custom<comp::meta_info> (meta_info{
            "Animator", "Controls ozz skeletal animation playback", "" })

        .data<&comp::animator::clip_path> ("clip_path"_hs)
        .custom<comp::meta_info> (meta_info{
            "Animation Clip", "Path to the .anim.ozz clip to play", "" })

        .data<&comp::animator::skeleton_path> ("skeleton_path"_hs)
        .custom<comp::meta_info> (meta_info{
            "Skeleton", "Optional path to the .skel.ozz skeleton", "" })

        .data<&comp::animator::skin_index> ("skin_index"_hs)
        .custom<comp::meta_info> (
            meta_info{ "Skin Index", "Index of the model skin to animate", "" })

        .data<&comp::animator::speed> ("speed"_hs)
        .custom<comp::meta_info> (
            meta_info{ "Speed", "Playback rate multiplier", "" })

        .data<&comp::animator::loop> ("loop"_hs)
        .custom<comp::meta_info> (meta_info{
            "Loop", "Whether playback restarts at the clip end", "" })

        .data<&comp::animator::crossfade_duration> ("crossfade_duration"_hs)
        .custom<comp::meta_info> (
            meta_info{ "Crossfade", "Transition duration in seconds", "" })

        .data<&comp::animator::playing> ("playing"_hs)
        .custom<comp::meta_info> (
            meta_info{ "Playing", "Whether this animator is enabled", "" })

        .data<&comp::animator::time> ("time"_hs)
        .custom<comp::meta_info> (
            meta_info{ "Time", "Current playback time in seconds", "" });
  }
};

} // namespace comp

} // namespace wsl
