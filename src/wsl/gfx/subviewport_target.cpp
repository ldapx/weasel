#include "subviewport_target.hpp"
#include "render_window.hpp"
#include "render_context.hpp"
#include "wsl/log/log.hpp"
#include "tracy_gpu_mem.hpp"

#include <SDL3/SDL_gpu.h>

namespace wsl::gfx
{

namespace
{

SDL_GPUSampleCount
to_sample_count (int samples)
{
  switch (samples) {
  case 2:
    return SDL_GPU_SAMPLECOUNT_2;
  case 4:
    return SDL_GPU_SAMPLECOUNT_4;
  case 1:
  default:
    return SDL_GPU_SAMPLECOUNT_1;
  }
}

SDL_GPUTexture *
create_texture (SDL_GPUDevice *device, const SDL_GPUTextureCreateInfo &ci,
                const char *name)
{
  SDL_GPUTexture *tex = SDL_CreateGPUTexture (device, &ci);
  if (tex == nullptr) {
    wsl::log::gfx ()->error (
        "Failed to create subviewport texture '{}' ({}x{}): {}", name, ci.width,
        ci.height, SDL_GetError ());
    return nullptr;
  }
  wsl::gfx::tracy_alloc_texture (tex, ci);
  SDL_SetGPUTextureName (device, tex, name);
  return tex;
}

} // namespace

subviewport_target
subviewport_target::create (render_window *window, render_context *ctx,
                            const subviewport_target_config &config)
{
  subviewport_target target{};
  target.width = config.width;
  target.height = config.height;
  target.is_2d = config.is_2d;
  target.hdr = config.hdr;
  target.bloom_enabled = config.bloom && !config.is_2d;

  if (config.width == 0 || config.height == 0 || window == nullptr
      || ctx == nullptr) {
    return target;
  }

  SDL_GPUDevice *device = ctx->gpu_device;
  uint32_t const w = config.width;
  uint32_t const h = config.height;

  // 2D subviewports never allocate MSAA or depth targets (S5).
  int const samples = config.is_2d ? 1 : config.sample_count;
  target.sample_count = (samples > 1) ? samples : 1;

  SDL_GPUTextureCreateInfo base{};
  SDL_zero (base);
  base.type = SDL_GPU_TEXTURETYPE_2D;
  base.width = w;
  base.height = h;
  base.layer_count_or_depth = 1;
  base.num_levels = 1;

  // ---- Colour target chain ----
  if (target.sample_count > 1) {
    SDL_GPUTextureCreateInfo msaa = base;
    msaa.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    msaa.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    msaa.sample_count = to_sample_count (target.sample_count);
    target.color_msaa = gpu_texture::adopt (
        device, create_texture (device, msaa, "Subviewport HDR MSAA"));
  }

  SDL_GPUTextureCreateInfo res = base;
  res.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
  res.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
  res.sample_count = SDL_GPU_SAMPLECOUNT_1;
  target.color_resolve = gpu_texture::adopt (
      device, create_texture (device, res, "Subviewport HDR Resolve"));

  // ---- LDR output (3D, hdr == false) ----
  if (!config.is_2d && !config.hdr) {
    SDL_GPUTextureCreateInfo ldr = base;
    ldr.format = window->swapchain_format ();
    ldr.usage
        = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ldr.sample_count = SDL_GPU_SAMPLECOUNT_1;
    target.ldr_color = gpu_texture::adopt (
        device, create_texture (device, ldr, "Subviewport LDR"));
  }

  // ---- Bloom source MRT (part of the 3D pass format) ----
  //
  // 3D subviewports render through the same two-MRT pipelines as the root
  // (scene + bloom source), so the full-res bloom source is part of the
  // pass format and always allocated for 3D. The half-res ping-pong pair
  // is the actual "bloom target" and is only allocated when bloom is
  // explicitly enabled (D12).
  if (!config.is_2d) {
    if (target.sample_count > 1) {
      SDL_GPUTextureCreateInfo msaa = base;
      msaa.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
      msaa.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
      msaa.sample_count = to_sample_count (target.sample_count);
      target.bloom_src_msaa = gpu_texture::adopt (
          device, create_texture (device, msaa, "Subviewport Bloom MSAA"));
    }

    SDL_GPUTextureCreateInfo bsrc = base;
    bsrc.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    bsrc.usage
        = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    bsrc.sample_count = SDL_GPU_SAMPLECOUNT_1;
    target.bloom_src_resolve = gpu_texture::adopt (
        device, create_texture (device, bsrc, "Subviewport Bloom Source"));
  }

  if (target.bloom_enabled) {
    SDL_GPUTextureCreateInfo half = base;
    half.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    half.usage
        = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    half.sample_count = SDL_GPU_SAMPLECOUNT_1;
    half.width = std::max (1U, w / 2);
    half.height = std::max (1U, h / 2);
    target.bloom_a = gpu_texture::adopt (
        device, create_texture (device, half, "Subviewport Bloom A"));
    target.bloom_b = gpu_texture::adopt (
        device, create_texture (device, half, "Subviewport Bloom B"));
  }

  // ---- Depth (3D only) ----
  if (!config.is_2d) {
    SDL_GPUTextureCreateInfo ds = base;
    ds.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    ds.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    ds.sample_count = to_sample_count (target.sample_count);
    target.depth = gpu_texture::adopt (
        device, create_texture (device, ds, "Subviewport Depth"));
  }

  return target;
}

void
subviewport_target::destroy (subviewport_target &target)
{
  target.color_msaa.reset ();
  target.color_resolve.reset ();
  target.ldr_color.reset ();
  target.bloom_src_msaa.reset ();
  target.bloom_src_resolve.reset ();
  target.bloom_a.reset ();
  target.bloom_b.reset ();
  target.depth.reset ();
  target.width = 0;
  target.height = 0;
  target.sample_count = 1;
  target.is_2d = false;
  target.hdr = false;
  target.bloom_enabled = false;
}

} // namespace wsl::gfx
