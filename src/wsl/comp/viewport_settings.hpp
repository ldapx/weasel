#pragma once

#include "../math/vector.hpp"
#include "../rsc/resource_ids.hpp"
#include "component_meta.hpp"

#ifndef IN_MODULE_INTERFACE
#include <cstdint>
#endif

namespace wsl::comp
{

namespace singl
{
class runtime_context;
}

/**
 * How a viewport maps its rendered content onto the available output area.
 *
 * See the viewport redesign spec, section 3.
 */
enum class viewport_scale_mode : std::uint8_t
{
  /** No automatic scaling; one virtual unit maps to one output pixel. */
  disabled = 0,
  /**
   * 2D content is rendered directly at the target rendering resolution
   * instead of being rendered into a fixed-size viewport texture and
   * upscaled. The base resolution supplies the reference dimensions for 2D
   * layout, the target resolution supplies the pixel density.
   */
  scale_2d = 1,
  /**
   * The viewport renders into a render target sized from its virtual size
   * (the configured base resolution); the resulting texture is scaled to
   * fit the output area subject to the aspect ratio policy.
   */
  viewport = 2,
};

/** How the virtual framing is fitted into the container. See spec section 4. */
enum class viewport_aspect_policy : std::uint8_t
{
  /** Fill the whole output area, ignoring the base aspect ratio. */
  ignore = 0,
  /** Preserve the aspect ratio; letterbox/pillarbox the unused border. */
  keep = 1,
  /** Fix the virtual width to the base width; reveal/trim vertical area. */
  keep_width = 2,
  /** Fix the virtual height to the base height; reveal/trim horizontal area. */
  keep_height = 3,
  /** Preserve the aspect ratio while expanding the visible virtual area. */
  expand = 4,
};

/** Texture filtering applied when a viewport texture is presented. */
enum class viewport_filter : std::uint8_t
{
  nearest = 0,
  linear = 1,
};

/** Per-viewport update cadence. See spec section 6.3. */
enum class viewport_update_mode : std::uint8_t
{
  /** Skipped entirely. */
  disabled = 0,
  /** Renders one frame, then switches to `disabled`. */
  once = 1,
  /** Renders only while the viewport is inside its parent's visible area. */
  when_visible = 2,
  /** Renders only while its parent viewport also rendered this frame. */
  when_parent_visible = 3,
  /** Updates every frame. */
  always = 4,
};

/** Per-viewport clear cadence. */
enum class viewport_clear_mode : std::uint8_t
{
  /** Clear the render target every frame. */
  always = 0,
  /** Clear the render target on the next frame only. */
  next_frame = 1,
  /** Never clear; keep the previous frame's contents. */
  never = 2,
};

/**
 * Per-viewport world environment.
 *
 * Every knob has a `use_global` companion. When true, the value is taken
 * live from the scene's global environment (`comp::singl::rendering_manager`);
 * when false, the stored override is used. Globally shared in v1 (see spec
 * section 6.4): scene lights, procedural sun, shadow maps and the clustered
 * lighting grid are never duplicated per viewport.
 */
struct viewport_environment
{
  // ---- Skybox ----
  bool skybox_use_global = true;
  rsc::cubemap_id skybox{};
  bool skybox_rotation_use_global = true;
  math::quatf skybox_rotation{ 0.0F, 0.0F, 0.0F, 1.0F };

  // ---- Ambient ----
  bool ambient_use_global = true;
  math::vec3f ambient_color{ 1.0F, 1.0F, 1.0F };
  float ambient_intensity = 0.01F;

  // ---- Image based lighting ----
  bool ibl_use_global = true;
  float ibl_intensity = 1.0F;

  // ---- Exposure / tonemap ----
  bool exposure_use_global = true;
  float exposure = 1.0F;

  // ---- Bloom ----
  bool bloom_use_global = true;
  /** Allocates the bloom target and runs the bloom pass when enabled. */
  bool bloom_enabled = false;
  float bloom_threshold = 1.0F;
  float bloom_knee = 0.5F;
  float bloom_intensity = 1.0F;

  // ---- SSAO ----
  bool ssao_use_global = true;
  bool ssao_enabled = true;
  float ssao_radius = 0.6F;
  float ssao_bias = 0.025F;
  float ssao_power = 1.25F;
  float ssao_intensity = 1.0F;

  // ---- Clear ----
  /** Whether the colour target is cleared before this viewport renders. */
  bool clear_color_enabled = true;
  /** When true the scene clear colour is used; otherwise `clear_color`. */
  bool clear_color_use_global = true;
  math::vec4f clear_color{ 0.0F, 0.0F, 0.0F, 1.0F };
  /** Whether the depth target is cleared before this viewport renders. */
  bool clear_depth = true;

  bool
  operator== (const viewport_environment &o) const
  {
    return skybox_use_global == o.skybox_use_global && skybox == o.skybox
           && skybox_rotation_use_global == o.skybox_rotation_use_global
           && skybox_rotation == o.skybox_rotation
           && ambient_use_global == o.ambient_use_global
           && ambient_color == o.ambient_color
           && ambient_intensity == o.ambient_intensity
           && ibl_use_global == o.ibl_use_global
           && ibl_intensity == o.ibl_intensity
           && exposure_use_global == o.exposure_use_global
           && exposure == o.exposure && bloom_use_global == o.bloom_use_global
           && bloom_enabled == o.bloom_enabled
           && bloom_threshold == o.bloom_threshold && bloom_knee == o.bloom_knee
           && bloom_intensity == o.bloom_intensity
           && ssao_use_global == o.ssao_use_global
           && ssao_enabled == o.ssao_enabled && ssao_radius == o.ssao_radius
           && ssao_bias == o.ssao_bias && ssao_power == o.ssao_power
           && ssao_intensity == o.ssao_intensity
           && clear_color_enabled == o.clear_color_enabled
           && clear_color_use_global == o.clear_color_use_global
           && clear_color == o.clear_color && clear_depth == o.clear_depth;
  }
  bool
  operator!= (const viewport_environment &o) const
  {
    return !(*this == o);
  }
};

/**
 * Shared viewport configuration used by the root viewport (stored in
 * `comp::singl::rendering_manager`) and by every `comp::subviewport`.
 *
 * Defaults reproduce the fresh-scene subviewport behaviour: viewport scaling
 * with an ignored aspect ratio, linear filtering, MSAA off, render scale 1.0,
 * `when_visible` updates and clearing every frame. The root viewport overrides
 * the applicable defaults (scale `disabled`, update `always`).
 */
struct viewport_settings
{
  viewport_scale_mode scale_mode = viewport_scale_mode::viewport;
  viewport_aspect_policy aspect_policy = viewport_aspect_policy::ignore;

  // ---- Quality knobs (`use_global` follows Project Settings, D5) ----
  viewport_filter filter = viewport_filter::linear;
  bool filter_use_global = true;

  /** Antialiasing sample count: 1 (off), 2 or 4. */
  int msaa = 1;
  bool msaa_use_global = true;

  /** Additional rendering scale applied to the resolved target size. */
  float render_scale = 1.0F;
  bool render_scale_use_global = true;

  viewport_environment env{};

  viewport_update_mode update_mode = viewport_update_mode::when_visible;
  viewport_clear_mode clear_mode = viewport_clear_mode::always;

  /** When false the viewport's subtree is excluded from shadow casting. */
  bool shadow_caster = true;
  /** Reserved cull mask (bit set per render layer). */
  std::uint32_t cull_mask = 0xFFFFFFFFU;

  /** Resolved filter after applying the project-quality fallback. */
  [[nodiscard]] viewport_filter
  resolved_filter (viewport_filter project_default) const
  {
    return filter_use_global ? project_default : filter;
  }

  /** Resolved MSAA sample count after applying the project default. */
  [[nodiscard]] int
  resolved_msaa (int project_default) const
  {
    return msaa_use_global ? project_default : msaa;
  }

  /** Resolved render scale after applying the project default. */
  [[nodiscard]] float
  resolved_render_scale (float project_default) const
  {
    return render_scale_use_global ? project_default : render_scale;
  }

  bool
  operator== (const viewport_settings &o) const
  {
    return scale_mode == o.scale_mode && aspect_policy == o.aspect_policy
           && filter == o.filter && filter_use_global == o.filter_use_global
           && msaa == o.msaa && msaa_use_global == o.msaa_use_global
           && render_scale == o.render_scale
           && render_scale_use_global == o.render_scale_use_global
           && env == o.env && update_mode == o.update_mode
           && clear_mode == o.clear_mode && shadow_caster == o.shadow_caster
           && cull_mask == o.cull_mask;
  }
  bool
  operator!= (const viewport_settings &o) const
  {
    return !(*this == o);
  }

  /**
   * ImGui inspector shared by the root viewport (inside the rendering
   * manager) and every subviewport (E1). Implemented by editor_support.
   */
  bool custom_inspect (const char *label,
                       comp::singl::runtime_context *runtime_ctx);
};

/**
 * Global quality defaults owned by Project Settings (D5).
 *
 * Each viewport knob carries a `use_global` flag; when set, the value stored
 * here applies live. Window configuration is not part of this struct (it is
 * owned by the project as well but lives on `rsc::project`).
 */
struct viewport_quality_defaults
{
  viewport_filter filter = viewport_filter::linear;
  int msaa = 1;
  float render_scale = 1.0F;
};

/** Registers the shared viewport enums/settings with the entt reflection. */
void register_viewport_settings_meta ();

} // namespace wsl::comp
