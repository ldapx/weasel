// Viewport resolution-chain and aspect-policy math (spec §§2–4, TODO T1).
//
// Pure functions: no GPU, no registry. Exercises the engine-level resolve
// helper that the rendering backend consumes (B1).

#define DOCTEST_CONFIG_USE_STD_HEADERS

#include <doctest/doctest.h>

#include "wsl/comp/viewport_settings.hpp"
#include "wsl/gfx/viewport_resolve.hpp"

using wsl::comp::viewport_aspect_policy;
using wsl::comp::viewport_scale_mode;
using wsl::comp::viewport_settings;
using wsl::gfx::pixel_rect;
using wsl::gfx::resolved_viewport;

namespace
{

viewport_settings
settings (viewport_scale_mode mode, viewport_aspect_policy aspect,
          float render_scale = 1.0F)
{
  viewport_settings s;
  s.scale_mode = mode;
  s.aspect_policy = aspect;
  s.render_scale = render_scale;
  return s;
}

} // namespace

TEST_CASE ("viewport resolve: disabled + ignore reproduces fullscreen")
{
  // D12: fresh-scene root defaults. One virtual unit maps to one output
  // pixel; the visible virtual area equals the container.
  auto const r = wsl::gfx::resolve_viewport (
      settings (viewport_scale_mode::disabled, viewport_aspect_policy::ignore),
      wsl::math::vec2f{ 1600.0F, 900.0F }, wsl::math::vec2f{ 1280.0F, 720.0F });

  CHECK (r.container_size.x () == 1600.0F);
  CHECK (r.container_size.y () == 900.0F);
  CHECK (r.render_size.x () == 1600.0F);
  CHECK (r.render_size.y () == 900.0F);
  CHECK (r.virtual_size.x () == 1600.0F);
  CHECK (r.virtual_size.y () == 900.0F);
  CHECK (r.scale.x () == 1.0F);
  CHECK (r.scale.y () == 1.0F);
  CHECK (r.output_rect == (pixel_rect{ 0, 0, 1600, 900 }));
  CHECK (r.aspect_ratio () == doctest::Approx (1600.0F / 900.0F));
}

TEST_CASE ("viewport resolve: viewport scaling worked example (R2)")
{
  // Base 1280x720 in a 1920x1080 window: render target stays 1280x720,
  // presented scaled by 1.5 from the full output rect.
  auto const r = wsl::gfx::resolve_viewport (
      settings (viewport_scale_mode::viewport, viewport_aspect_policy::ignore),
      wsl::math::vec2f{ 1920.0F, 1080.0F },
      wsl::math::vec2f{ 1280.0F, 720.0F });

  CHECK (r.render_size.x () == 1280.0F);
  CHECK (r.render_size.y () == 720.0F);
  CHECK (r.virtual_size.x () == 1280.0F);
  CHECK (r.virtual_size.y () == 720.0F);
  CHECK (r.output_rect == (pixel_rect{ 0, 0, 1920, 1080 }));
  CHECK (r.scale.x () == doctest::Approx (1.5F));
  CHECK (r.scale.y () == doctest::Approx (1.5F));
  CHECK (r.renders_offscreen ());
  CHECK (r.full_output ());
  CHECK (r.target_px_per_virtual ().x () == doctest::Approx (1.0F));
}

TEST_CASE ("viewport resolve: keep letterboxes and centres")
{
  // 4:3 base in a 16:9 container: uniform scale = 1.0 (height-limited
  // would be 1080/720 = 1.5 but width gives 1920/1280 = 1.5; use a case
  // where the axes differ: base 1280x720 in 1280x800).
  auto const r = wsl::gfx::resolve_viewport (
      settings (viewport_scale_mode::viewport, viewport_aspect_policy::keep),
      wsl::math::vec2f{ 1280.0F, 800.0F }, wsl::math::vec2f{ 1280.0F, 720.0F });

  // scale = min(1280/1280, 800/720) = 1.0 -> 1280x720 centred vertically.
  CHECK (r.render_size.x () == 1280.0F);
  CHECK (r.render_size.y () == 720.0F);
  CHECK (r.output_rect == (pixel_rect{ 0, 40, 1280, 720 }));
  CHECK (!r.full_output ());
  CHECK (r.scale.x () == doctest::Approx (1.0F));
}

TEST_CASE ("viewport resolve: keep_width and keep_height expand the area")
{
  // Tall container (720x1280, aspect 0.5625) vs base 1280x720 (1.7778).
  {
    auto const r = wsl::gfx::resolve_viewport (
        settings (viewport_scale_mode::viewport,
                  viewport_aspect_policy::keep_width),
        wsl::math::vec2f{ 720.0F, 1280.0F },
        wsl::math::vec2f{ 1280.0F, 720.0F });
    // Virtual height = 1280 / (720/1280) = 2275.56 (rounded render h).
    CHECK (r.scale.x () == doctest::Approx (720.0F / 1280.0F));
    CHECK (r.virtual_size.y ()
           == doctest::Approx (1280.0F / (720.0F / 1280.0F)));
    CHECK (r.render_size.x () == 1280.0F);
    CHECK (r.render_size.y ()
           == doctest::Approx (1280.0F / (720.0F / 1280.0F)).epsilon (0.01));
    CHECK (r.full_output ());
  }

  // Wide container (2560x1080, aspect 2.370) vs base 1280x720.
  {
    auto const r = wsl::gfx::resolve_viewport (
        settings (viewport_scale_mode::viewport,
                  viewport_aspect_policy::keep_height),
        wsl::math::vec2f{ 2560.0F, 1080.0F },
        wsl::math::vec2f{ 1280.0F, 720.0F });
    CHECK (r.virtual_size.y () == 720.0F);
    CHECK (r.virtual_size.x ()
           == doctest::Approx (720.0F * (2560.0F / 1080.0F)));
    CHECK (r.scale.x () == doctest::Approx (1080.0F / 720.0F));
    CHECK (r.full_output ());
  }
}

TEST_CASE ("viewport resolve: expand preserves framing without bars")
{
  // Wider container: height constrained, width expands.
  {
    auto const r
        = wsl::gfx::resolve_viewport (settings (viewport_scale_mode::viewport,
                                                viewport_aspect_policy::expand),
                                      wsl::math::vec2f{ 1920.0F, 1080.0F },
                                      wsl::math::vec2f{ 1280.0F, 720.0F });
    CHECK (r.virtual_size.y () == 720.0F);
    CHECK (r.virtual_size.x () == doctest::Approx (720.0F * (16.0F / 9.0F)));
    CHECK (r.full_output ());
  }

  // Taller container: width constrained, height expands.
  {
    auto const r
        = wsl::gfx::resolve_viewport (settings (viewport_scale_mode::viewport,
                                                viewport_aspect_policy::expand),
                                      wsl::math::vec2f{ 720.0F, 1280.0F },
                                      wsl::math::vec2f{ 1280.0F, 720.0F });
    CHECK (r.virtual_size.x () == 1280.0F);
    CHECK (r.virtual_size.y ()
           == doctest::Approx (1280.0F / (720.0F / 1280.0F)));
    CHECK (r.full_output ());
  }
}

TEST_CASE ("viewport resolve: scale_2d renders at the target resolution")
{
  auto const r = wsl::gfx::resolve_viewport (
      settings (viewport_scale_mode::scale_2d, viewport_aspect_policy::ignore),
      wsl::math::vec2f{ 1600.0F, 900.0F }, wsl::math::vec2f{ 1280.0F, 720.0F });

  CHECK (r.render_size.x () == 1600.0F);
  CHECK (r.render_size.y () == 900.0F);
  CHECK (r.scale.x () == doctest::Approx (1600.0F / 1280.0F));
  CHECK (r.scale.y () == doctest::Approx (900.0F / 720.0F));
  CHECK (r.full_output ());
}

TEST_CASE ("viewport resolve: render scale multiplies the render target")
{
  auto const r = wsl::gfx::resolve_viewport (
      settings (viewport_scale_mode::viewport, viewport_aspect_policy::ignore,
                0.5F),
      wsl::math::vec2f{ 1920.0F, 1080.0F }, wsl::math::vec2f{ 1280.0F, 720.0F },
      0.5F);

  CHECK (r.render_size.x () == 640.0F);
  CHECK (r.render_size.y () == 360.0F);
  CHECK (r.render_rect == (pixel_rect{ 0, 0, 640, 360 }));
  CHECK (r.virtual_size.x () == 1280.0F);
}

TEST_CASE ("viewport compose: D2 worked example")
{
  // Parent base 1280x720, window 1920x1080 (scale 1.5 via scale_2d).
  // A 320x180 sub at (100, 50) occupies (150, 75, 480, 270) in the
  // parent render target.
  auto const parent = wsl::gfx::resolve_viewport (
      settings (viewport_scale_mode::scale_2d, viewport_aspect_policy::ignore),
      wsl::math::vec2f{ 1920.0F, 1080.0F },
      wsl::math::vec2f{ 1280.0F, 720.0F });

  CHECK (parent.target_px_per_virtual ().x () == doctest::Approx (1.5F));

  auto const child = wsl::gfx::resolve_viewport (
      settings (viewport_scale_mode::viewport, viewport_aspect_policy::ignore),
      wsl::math::vec2f{ 480.0F, 270.0F }, wsl::math::vec2f{ 1920.0F, 1080.0F });

  auto const dst = wsl::gfx::compose_2d_rect (
      wsl::math::vec2f{ 100.0F, 50.0F }, wsl::math::vec2f{ 320.0F, 180.0F },
      parent.target_px_per_virtual (), child);

  CHECK (dst == (pixel_rect{ 150, 75, 480, 270 }));
}

TEST_CASE ("viewport compose: keep policy letters inside the container")
{
  // Child with a keep policy inside a 640x480 container whose base is
  // 1280x720: scale = min(0.5, 0.666) = 0.5 -> 640x360 centred.
  auto const child = wsl::gfx::resolve_viewport (
      settings (viewport_scale_mode::viewport, viewport_aspect_policy::keep),
      wsl::math::vec2f{ 640.0F, 480.0F }, wsl::math::vec2f{ 1280.0F, 720.0F });

  CHECK (child.output_rect == (pixel_rect{ 0, 60, 640, 360 }));

  auto const dst = wsl::gfx::compose_2d_rect (
      wsl::math::vec2f{ 10.0F, 20.0F }, wsl::math::vec2f{ 640.0F, 480.0F },
      wsl::math::vec2f{ 1.0F, 1.0F }, child);
  CHECK (dst == (pixel_rect{ 10, 80, 640, 360 }));
}

TEST_CASE ("viewport input mapping: window point to virtual space (I1)")
{
  // Letterboxed keep viewport: 1280x720 base in a 1280x800 window.
  auto const resolved = wsl::gfx::resolve_viewport (
      settings (viewport_scale_mode::viewport, viewport_aspect_policy::keep),
      wsl::math::vec2f{ 1280.0F, 800.0F }, wsl::math::vec2f{ 1280.0F, 720.0F });

  CHECK (resolved.output_rect == (pixel_rect{ 0, 40, 1280, 720 }));

  wsl::math::vec2f out{ 0.0F, 0.0F };
  // A point in the top letterbox bar does not map.
  CHECK (!wsl::gfx::map_output_point_to_virtual (
      wsl::math::vec2f{ 100.0F, 10.0F }, resolved, out));

  // Top-left of the displayed rect maps to (0, 0).
  CHECK (wsl::gfx::map_output_point_to_virtual (wsl::math::vec2f{ 0.0F, 40.0F },
                                                resolved, out));
  CHECK (out.x () == doctest::Approx (0.0F));
  CHECK (out.y () == doctest::Approx (0.0F));

  // Centre maps to the virtual centre.
  CHECK (wsl::gfx::map_output_point_to_virtual (
      wsl::math::vec2f{ 640.0F, 400.0F }, resolved, out));
  CHECK (out.x () == doctest::Approx (640.0F));
  CHECK (out.y () == doctest::Approx (360.0F));

  // The inverse mapping reaches the render target (used for 3D quads).
  wsl::math::vec2f target_px{ 0.0F, 0.0F };
  CHECK (wsl::gfx::map_output_point_to_target (
      wsl::math::vec2f{ 640.0F, 400.0F }, resolved, target_px));
  CHECK (target_px.x () == doctest::Approx (640.0F));
  CHECK (target_px.y () == doctest::Approx (360.0F));
}
