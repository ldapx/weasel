#include "viewport_settings.hpp"

#include <entt/meta/factory.hpp>

namespace wsl::comp
{

void
register_viewport_settings_meta ()
{
  using namespace entt::literals;

  entt::meta_factory<comp::viewport_scale_mode> ()
      .type (entt::type_hash<comp::viewport_scale_mode>::value ())
      .conv<int> ()
      .data<comp::viewport_scale_mode::disabled> ("disabled"_hs)
      .custom<const char *> ("Disabled")
      .data<comp::viewport_scale_mode::scale_2d> ("scale_2d"_hs)
      .custom<const char *> ("2D Scaling")
      .data<comp::viewport_scale_mode::viewport> ("viewport"_hs)
      .custom<const char *> ("Viewport Scaling");

  entt::meta_factory<comp::viewport_aspect_policy> ()
      .type (entt::type_hash<comp::viewport_aspect_policy>::value ())
      .conv<int> ()
      .data<comp::viewport_aspect_policy::ignore> ("ignore"_hs)
      .custom<const char *> ("Ignore")
      .data<comp::viewport_aspect_policy::keep> ("keep"_hs)
      .custom<const char *> ("Keep")
      .data<comp::viewport_aspect_policy::keep_width> ("keep_width"_hs)
      .custom<const char *> ("Keep Width")
      .data<comp::viewport_aspect_policy::keep_height> ("keep_height"_hs)
      .custom<const char *> ("Keep Height")
      .data<comp::viewport_aspect_policy::expand> ("expand"_hs)
      .custom<const char *> ("Expand");

  entt::meta_factory<comp::viewport_filter> ()
      .type (entt::type_hash<comp::viewport_filter>::value ())
      .conv<int> ()
      .data<comp::viewport_filter::nearest> ("nearest"_hs)
      .custom<const char *> ("Nearest")
      .data<comp::viewport_filter::linear> ("linear"_hs)
      .custom<const char *> ("Linear");

  entt::meta_factory<comp::viewport_update_mode> ()
      .type (entt::type_hash<comp::viewport_update_mode>::value ())
      .conv<int> ()
      .data<comp::viewport_update_mode::disabled> ("disabled"_hs)
      .custom<const char *> ("Disabled")
      .data<comp::viewport_update_mode::once> ("once"_hs)
      .custom<const char *> ("Once")
      .data<comp::viewport_update_mode::when_visible> ("when_visible"_hs)
      .custom<const char *> ("When Visible")
      .data<comp::viewport_update_mode::when_parent_visible> (
          "when_parent_visible"_hs)
      .custom<const char *> ("When Parent Visible")
      .data<comp::viewport_update_mode::always> ("always"_hs)
      .custom<const char *> ("Always");

  entt::meta_factory<comp::viewport_clear_mode> ()
      .type (entt::type_hash<comp::viewport_clear_mode>::value ())
      .conv<int> ()
      .data<comp::viewport_clear_mode::always> ("always"_hs)
      .custom<const char *> ("Always")
      .data<comp::viewport_clear_mode::next_frame> ("next_frame"_hs)
      .custom<const char *> ("Next Frame")
      .data<comp::viewport_clear_mode::never> ("never"_hs)
      .custom<const char *> ("Never");

  entt::meta_factory<comp::viewport_environment> ()
      .type (entt::type_hash<comp::viewport_environment>::value ())
      .custom<meta_info> (
          meta_info{ "Environment", "Per-viewport world environment.", "" })
      .data<&comp::viewport_environment::skybox_use_global> (
          "skybox_use_global"_hs)
      .custom<meta_info> (
          meta_info{ "Skybox Use Global", "Follow the scene skybox.", "" })
      .data<&comp::viewport_environment::skybox> ("skybox"_hs)
      .custom<meta_info> (
          meta_info{ "Skybox", "Viewport skybox cubemap override.", "" })
      .data<&comp::viewport_environment::skybox_rotation_use_global> (
          "skybox_rotation_use_global"_hs)
      .custom<meta_info> (meta_info{ "Skybox Rotation Use Global",
                                     "Follow the scene skybox rotation.", "" })
      .data<&comp::viewport_environment::skybox_rotation> ("skybox_rotation"_hs)
      .custom<meta_info> (
          meta_info{ "Skybox Rotation", "Skybox rotation override.", "" })
      .data<&comp::viewport_environment::ambient_use_global> (
          "ambient_use_global"_hs)
      .custom<meta_info> (meta_info{ "Ambient Use Global",
                                     "Follow the scene ambient light.", "" })
      .data<&comp::viewport_environment::ambient_color> ("ambient_color"_hs)
      .custom<meta_info> (
          meta_info{ "Ambient Color", "Ambient tint override.", "" })
      .data<&comp::viewport_environment::ambient_intensity> (
          "ambient_intensity"_hs)
      .custom<meta_info> (
          meta_info{ "Ambient Intensity", "Ambient strength override.", "" })
      .data<&comp::viewport_environment::ibl_use_global> ("ibl_use_global"_hs)
      .custom<meta_info> (
          meta_info{ "IBL Use Global", "Follow the scene IBL strength.", "" })
      .data<&comp::viewport_environment::ibl_intensity> ("ibl_intensity"_hs)
      .custom<meta_info> (
          meta_info{ "IBL Intensity", "IBL strength override.", "" })
      .data<&comp::viewport_environment::exposure_use_global> (
          "exposure_use_global"_hs)
      .custom<meta_info> (
          meta_info{ "Exposure Use Global", "Follow the scene exposure.", "" })
      .data<&comp::viewport_environment::exposure> ("exposure"_hs)
      .custom<meta_info> (
          meta_info{ "Exposure", "Tonemap exposure override.", "" })
      .data<&comp::viewport_environment::bloom_use_global> (
          "bloom_use_global"_hs)
      .custom<meta_info> (
          meta_info{ "Bloom Use Global", "Follow the scene bloom.", "" })
      .data<&comp::viewport_environment::bloom_enabled> ("bloom_enabled"_hs)
      .custom<meta_info> (
          meta_info{ "Bloom Enabled",
                     "Allocate the bloom target and run bloom for this "
                     "viewport.",
                     "" })
      .data<&comp::viewport_environment::bloom_threshold> ("bloom_threshold"_hs)
      .custom<meta_info> (
          meta_info{ "Bloom Threshold", "Bloom threshold override.", "" })
      .data<&comp::viewport_environment::bloom_knee> ("bloom_knee"_hs)
      .custom<meta_info> (meta_info{ "Bloom Knee", "Bloom knee override.", "" })
      .data<&comp::viewport_environment::bloom_intensity> ("bloom_intensity"_hs)
      .custom<meta_info> (
          meta_info{ "Bloom Intensity", "Bloom intensity override.", "" })
      .data<&comp::viewport_environment::ssao_use_global> ("ssao_use_global"_hs)
      .custom<meta_info> (
          meta_info{ "SSAO Use Global", "Follow the scene SSAO.", "" })
      .data<&comp::viewport_environment::ssao_enabled> ("ssao_enabled"_hs)
      .custom<meta_info> (
          meta_info{ "SSAO Enabled", "Enable SSAO for this viewport.", "" })
      .data<&comp::viewport_environment::ssao_radius> ("ssao_radius"_hs)
      .custom<meta_info> (
          meta_info{ "SSAO Radius", "SSAO radius override.", "" })
      .data<&comp::viewport_environment::ssao_bias> ("ssao_bias"_hs)
      .custom<meta_info> (meta_info{ "SSAO Bias", "SSAO bias override.", "" })
      .data<&comp::viewport_environment::ssao_power> ("ssao_power"_hs)
      .custom<meta_info> (meta_info{ "SSAO Power", "SSAO power override.", "" })
      .data<&comp::viewport_environment::ssao_intensity> ("ssao_intensity"_hs)
      .custom<meta_info> (
          meta_info{ "SSAO Intensity", "SSAO intensity override.", "" })
      .data<&comp::viewport_environment::clear_color_enabled> (
          "clear_color_enabled"_hs)
      .custom<meta_info> (
          meta_info{ "Clear Color Enabled", "Clear the colour target.", "" })
      .data<&comp::viewport_environment::clear_color_use_global> (
          "clear_color_use_global"_hs)
      .custom<meta_info> (meta_info{ "Clear Color Use Global",
                                     "Use the scene clear colour.", "" })
      .data<&comp::viewport_environment::clear_color> ("clear_color"_hs)
      .custom<meta_info> (
          meta_info{ "Clear Color", "Clear colour override (RGBA).", "" })
      .data<&comp::viewport_environment::clear_depth> ("clear_depth"_hs)
      .custom<meta_info> (
          meta_info{ "Clear Depth", "Clear the depth target.", "" });

  entt::meta_factory<comp::viewport_settings> ()
      .type (entt::type_hash<comp::viewport_settings>::value ())
      .custom<meta_info> (meta_info{ "Viewport Settings",
                                     "Shared root/sub viewport settings.", "" })
      .data<&comp::viewport_settings::scale_mode> ("scale_mode"_hs)
      .custom<meta_info> (
          meta_info{ "Scale Mode", "How content maps to the output area.", "" })
      .data<&comp::viewport_settings::aspect_policy> ("aspect_policy"_hs)
      .custom<meta_info> (
          meta_info{ "Aspect Ratio", "Aspect ratio fitting policy.", "" })
      .data<&comp::viewport_settings::filter> ("filter"_hs)
      .custom<meta_info> (meta_info{
          "Filter", "Scaling filter used when presenting the viewport.", "" })
      .data<&comp::viewport_settings::filter_use_global> (
          "filter_use_global"_hs)
      .custom<meta_info> (meta_info{ "Filter Use Global",
                                     "Follow the project filter default.", "" })
      .data<&comp::viewport_settings::msaa> ("msaa"_hs)
      .custom<meta_info> (
          meta_info{ "MSAA", "Antialiasing sample count (1, 2 or 4).", "" })
      .data<&comp::viewport_settings::msaa_use_global> ("msaa_use_global"_hs)
      .custom<meta_info> (meta_info{
          "MSAA Use Global", "Follow the project antialiasing default.", "" })
      .data<&comp::viewport_settings::render_scale> ("render_scale"_hs)
      .custom<meta_info> (meta_info{
          "Render Scale", "Additional rendering-resolution multiplier.", "" })
      .data<&comp::viewport_settings::render_scale_use_global> (
          "render_scale_use_global"_hs)
      .custom<meta_info> (meta_info{ "Render Scale Use Global",
                                     "Follow the project render scale default.",
                                     "" })
      .data<&comp::viewport_settings::env> ("env"_hs)
      .custom<meta_info> (
          meta_info{ "Environment", "Per-viewport environment.", "" })
      .data<&comp::viewport_settings::update_mode> ("update_mode"_hs)
      .custom<meta_info> (
          meta_info{ "Update Mode", "When this viewport updates.", "" })
      .data<&comp::viewport_settings::clear_mode> ("clear_mode"_hs)
      .custom<meta_info> (
          meta_info{ "Clear Mode", "When this viewport clears.", "" })
      .data<&comp::viewport_settings::shadow_caster> ("shadow_caster"_hs)
      .custom<meta_info> (meta_info{
          "Shadow Caster", "Include this subtree in shadow passes.", "" })
      .data<&comp::viewport_settings::cull_mask> ("cull_mask"_hs)
      .custom<meta_info> (
          meta_info{ "Cull Mask", "Render layer mask (reserved).", "" });

  entt::meta_factory<comp::viewport_quality_defaults> ()
      .type (entt::type_hash<comp::viewport_quality_defaults>::value ())
      .custom<meta_info> (meta_info{
          "Viewport Quality Defaults",
          "Project-wide defaults followed by `use_global` viewports.", "" })
      .data<&comp::viewport_quality_defaults::filter> ("filter"_hs)
      .custom<meta_info> (
          meta_info{ "Filter", "Default scaling filter for viewports.", "" })
      .data<&comp::viewport_quality_defaults::msaa> ("msaa"_hs)
      .custom<meta_info> (meta_info{
          "MSAA", "Default antialiasing sample count (1, 2 or 4).", "" })
      .data<&comp::viewport_quality_defaults::render_scale> ("render_scale"_hs)
      .custom<meta_info> (meta_info{
          "Render Scale", "Default rendering-resolution multiplier.", "" });
}

} // namespace wsl::comp
