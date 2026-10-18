#include "viewport_resolve.hpp"

#include <cmath>

namespace wsl::gfx
{

namespace
{

int
round_px (float value)
{
  return static_cast<int> (std::lround (value));
}

int
at_least_1 (float value)
{
  return std::max (1, round_px (value));
}

float
safe_div (float numerator, float denominator, float fallback)
{
  return denominator > 0.0F ? numerator / denominator : fallback;
}

} // namespace

resolved_viewport
resolve_viewport (const comp::viewport_settings &settings,
                  math::vec2f container_size, math::vec2f base_size,
                  float resolved_render_scale)
{
  resolved_viewport out;

  float const container_w = std::max (container_size.x (), 1.0F);
  float const container_h = std::max (container_size.y (), 1.0F);
  float const base_w = std::max (base_size.x (), 1.0F);
  float const base_h = std::max (base_size.y (), 1.0F);
  float const render_scale = std::clamp (resolved_render_scale, 0.05F, 8.0F);

  float const container_aspect = container_w / container_h;
  float const base_aspect = base_w / base_h;

  out.base_size = math::vec2f{ base_w, base_h };
  out.container_size = math::vec2f{ container_w, container_h };
  out.render_scale = render_scale;

  // ---- Aspect ratio policy: virtual framing, scale and content region ----
  float virt_w = base_w;
  float virt_h = base_h;
  float scale_x = 1.0F;
  float scale_y = 1.0F;
  bool uniform = true;
  pixel_rect region{ 0, 0, static_cast<int> (container_w),
                     static_cast<int> (container_h) };

  switch (settings.aspect_policy) {
  case comp::viewport_aspect_policy::ignore:
    scale_x = container_w / base_w;
    scale_y = container_h / base_h;
    uniform = std::abs (scale_x - scale_y) <= 1e-4F;
    break;

  case comp::viewport_aspect_policy::keep: {
    float const scale = std::min (container_w / base_w, container_h / base_h);
    scale_x = scale;
    scale_y = scale;
    int const disp_w = at_least_1 (base_w * scale);
    int const disp_h = at_least_1 (base_h * scale);
    region = pixel_rect{
      static_cast<int> (std::floor ((container_w - float (disp_w)) * 0.5F)),
      static_cast<int> (std::floor ((container_h - float (disp_h)) * 0.5F)),
      disp_w,
      disp_h,
    };
    break;
  }

  case comp::viewport_aspect_policy::keep_width:
    // Fixed virtual width; vertical visible area follows the container.
    virt_h = base_w / container_aspect;
    scale_x = scale_y = container_w / base_w;
    break;

  case comp::viewport_aspect_policy::keep_height:
    // Fixed virtual height; horizontal visible area follows the container.
    virt_w = base_h * container_aspect;
    scale_x = scale_y = container_h / base_h;
    break;

  case comp::viewport_aspect_policy::expand:
    if (container_aspect >= base_aspect) {
      virt_w = base_h * container_aspect;
      virt_h = base_h;
      scale_x = scale_y = container_h / base_h;
    } else {
      virt_w = base_w;
      virt_h = base_w / container_aspect;
      scale_x = scale_y = container_w / base_w;
    }
    break;
  }

  // ---- Scale mode: render target allocation + content mapping ----
  switch (settings.scale_mode) {
  case comp::viewport_scale_mode::disabled: {
    // No scaling: one virtual unit == one output pixel (the render scale
    // knob renders at a higher/lower density, but the presented size stays
    // 1:1). The aspect policy positions a fixed reference frame inside the
    // container; resizing reveals area or adds unused space.
    int virt_px_w = static_cast<int> (container_w);
    int virt_px_h = static_cast<int> (container_h);
    int offset_x = 0;
    int offset_y = 0;

    switch (settings.aspect_policy) {
    case comp::viewport_aspect_policy::keep:
      virt_px_w = std::min (static_cast<int> (base_w),
                            static_cast<int> (container_w));
      virt_px_h = std::min (static_cast<int> (base_h),
                            static_cast<int> (container_h));
      offset_x
          = static_cast<int> (std::floor ((container_w - virt_px_w) * 0.5F));
      offset_y
          = static_cast<int> (std::floor ((container_h - virt_px_h) * 0.5F));
      break;
    case comp::viewport_aspect_policy::keep_width:
      virt_px_w = std::min (static_cast<int> (base_w),
                            static_cast<int> (container_w));
      virt_px_h = static_cast<int> (container_h);
      offset_x
          = static_cast<int> (std::floor ((container_w - virt_px_w) * 0.5F));
      break;
    case comp::viewport_aspect_policy::keep_height:
      virt_px_w = static_cast<int> (container_w);
      virt_px_h = std::min (static_cast<int> (base_h),
                            static_cast<int> (container_h));
      offset_y
          = static_cast<int> (std::floor ((container_h - virt_px_h) * 0.5F));
      break;
    case comp::viewport_aspect_policy::ignore:
    case comp::viewport_aspect_policy::expand:
    default:
      break;
    }

    out.virtual_size = math::vec2f{ static_cast<float> (virt_px_w),
                                    static_cast<float> (virt_px_h) };
    out.scale = math::vec2f{ 1.0F, 1.0F };
    out.uniform_scale = true;
    out.render_size
        = math::vec2f{ container_w * render_scale, container_h * render_scale };
    out.output_rect = pixel_rect{ offset_x, offset_y, virt_px_w, virt_px_h };
    out.render_rect = pixel_rect{
      round_px (float (offset_x) * render_scale),
      round_px (float (offset_y) * render_scale),
      at_least_1 (float (virt_px_w) * render_scale),
      at_least_1 (float (virt_px_h) * render_scale),
    };
    return out;
  }

  case comp::viewport_scale_mode::scale_2d: {
    // 2D content rendered directly at the target rendering resolution; no
    // low-resolution viewport stage and no upscaling.
    out.virtual_size = math::vec2f{ virt_w, virt_h };
    out.scale = math::vec2f{ scale_x, scale_y };
    out.uniform_scale = uniform;
    out.render_size
        = math::vec2f{ container_w * render_scale, container_h * render_scale };
    out.output_rect = region;
    out.render_rect = pixel_rect{
      round_px (float (region.x) * render_scale),
      round_px (float (region.y) * render_scale),
      at_least_1 (float (region.width) * render_scale),
      at_least_1 (float (region.height) * render_scale),
    };
    return out;
  }

  case comp::viewport_scale_mode::viewport:
  default: {
    // Render into a target sized from the virtual size, then present it
    // scaled to the output area subject to the aspect policy.
    out.virtual_size = math::vec2f{ virt_w, virt_h };
    out.scale = math::vec2f{ scale_x, scale_y };
    out.uniform_scale = uniform;
    int const render_w = at_least_1 (virt_w * render_scale);
    int const render_h = at_least_1 (virt_h * render_scale);
    out.render_size = math::vec2f{ static_cast<float> (render_w),
                                   static_cast<float> (render_h) };
    out.output_rect = region;
    out.render_rect = pixel_rect{ 0, 0, render_w, render_h };
    return out;
  }
  }
}

pixel_rect
compose_2d_rect (math::vec2f position, math::vec2f size,
                 math::vec2f parent_target_scale,
                 const resolved_viewport &child)
{
  int const px = round_px (position.x () * parent_target_scale.x ());
  int const py = round_px (position.y () * parent_target_scale.y ());
  int const pw = at_least_1 (size.x () * parent_target_scale.x ());
  int const ph = at_least_1 (size.y () * parent_target_scale.y ());

  if (child.output_rect.empty ()) {
    return pixel_rect{ px, py, pw, ph };
  }
  return pixel_rect{ px + child.output_rect.x, py + child.output_rect.y,
                     child.output_rect.width, child.output_rect.height };
}

pixel_rect
intersect_rect (const pixel_rect &a, const pixel_rect &b)
{
  int const x0 = std::max (a.x, b.x);
  int const y0 = std::max (a.y, b.y);
  int const x1 = std::min (a.x + a.width, b.x + b.width);
  int const y1 = std::min (a.y + a.height, b.y + b.height);
  if (x1 <= x0 || y1 <= y0) {
    return pixel_rect{ 0, 0, 0, 0 };
  }
  return pixel_rect{ x0, y0, x1 - x0, y1 - y0 };
}

bool
map_output_point_to_virtual (math::vec2f point,
                             const resolved_viewport &resolved,
                             math::vec2f &out_virtual)
{
  pixel_rect const &rect = resolved.output_rect;
  if (rect.empty () || point.x () < static_cast<float> (rect.x)
      || point.y () < static_cast<float> (rect.y)
      || point.x () >= static_cast<float> (rect.x + rect.width)
      || point.y () >= static_cast<float> (rect.y + rect.height)) {
    return false;
  }

  float const u = (point.x () - static_cast<float> (rect.x))
                  / static_cast<float> (rect.width);
  float const v = (point.y () - static_cast<float> (rect.y))
                  / static_cast<float> (rect.height);
  out_virtual = math::vec2f{ u * resolved.virtual_size.x (),
                             v * resolved.virtual_size.y () };
  return true;
}

bool
map_output_point_to_target (math::vec2f point,
                            const resolved_viewport &resolved,
                            math::vec2f &out_target_px)
{
  pixel_rect const &rect = resolved.output_rect;
  if (rect.empty () || point.x () < static_cast<float> (rect.x)
      || point.y () < static_cast<float> (rect.y)
      || point.x () >= static_cast<float> (rect.x + rect.width)
      || point.y () >= static_cast<float> (rect.y + rect.height)) {
    return false;
  }

  float const u = (point.x () - static_cast<float> (rect.x))
                  / static_cast<float> (rect.width);
  float const v = (point.y () - static_cast<float> (rect.y))
                  / static_cast<float> (rect.height);
  out_target_px = math::vec2f{ u * resolved.render_size.x (),
                               v * resolved.render_size.y () };
  return true;
}

} // namespace wsl::gfx
