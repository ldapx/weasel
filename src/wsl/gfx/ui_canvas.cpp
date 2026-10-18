#include "ui_canvas.hpp"

#include "render_context.hpp"
#include "render_window.hpp"
#include "shader.hpp"
#include "wsl/log/log.hpp"
#include "wsl/rsc/resource_manager.hpp"

#include <SDL3/SDL_gpu.h>

namespace wsl::gfx
{

ui_canvas::~ui_canvas () { destroy (); }

void
ui_canvas::ensure (render_window &window, render_context &ctx, uint32_t width,
                   uint32_t height)
{
  if (ctx.gpu_device == nullptr || width == 0 || height == 0) {
    return;
  }
  if (m_canvas && m_width == width && m_height == height) {
    return;
  }

  // The previous canvas may still be referenced by frames in flight.
  SDL_WaitForGPUIdle (ctx.gpu_device);
  m_canvas.reset ();

  SDL_GPUTextureCreateInfo info{};
  SDL_zero (info);
  info.type = SDL_GPU_TEXTURETYPE_2D;
  info.format = window.swapchain_format ();
  info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
  info.width = width;
  info.height = height;
  info.layer_count_or_depth = 1;
  info.num_levels = 1;
  info.sample_count = SDL_GPU_SAMPLECOUNT_1;

  m_canvas = gpu_texture (ctx.gpu_device, info);
  if (!m_canvas) {
    wsl::log::gfx ()->error ("Failed to create the UI virtual canvas: {}",
                             SDL_GetError ());
    return;
  }
  SDL_SetGPUTextureName (ctx.gpu_device, m_canvas.get (), "UI Virtual Canvas");
  m_width = width;
  m_height = height;

  ensure_pipeline (window, ctx);
}

void
ui_canvas::ensure_pipeline (render_window &window, render_context &ctx)
{
  if (m_pipeline || m_sampler) {
    return;
  }
  auto *res_mgr = window.resource_manager ();
  if (res_mgr == nullptr) {
    return;
  }

  auto const vs_id = res_mgr->register_shader (
      "engine://compiled_shaders/editor_blit.vert.slang.spv");
  auto const fs_id = res_mgr->register_shader (
      "engine://compiled_shaders/editor_blit.frag.slang.spv");

  SDL_GPUShader *vs = shader::load_from_manager (
      ctx.gpu_device, res_mgr, vs_id, SDL_GPU_SHADERSTAGE_VERTEX, 0, 0);
  SDL_GPUShader *fs = shader::load_from_manager (
      ctx.gpu_device, res_mgr, fs_id, SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 1);
  if (vs == nullptr || fs == nullptr) {
    wsl::log::gfx ()->error (
        "UI virtual-canvas blit shaders unavailable; UI compositing disabled");
    if (vs != nullptr) {
      SDL_ReleaseGPUShader (ctx.gpu_device, vs);
    }
    if (fs != nullptr) {
      SDL_ReleaseGPUShader (ctx.gpu_device, fs);
    }
    return;
  }

  SDL_GPUGraphicsPipelineCreateInfo pipe{};
  SDL_zero (pipe);
  pipe.vertex_shader = vs;
  pipe.fragment_shader = fs;
  pipe.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  pipe.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
  pipe.depth_stencil_state.enable_depth_test = false;
  pipe.depth_stencil_state.enable_depth_write = false;

  SDL_GPUColorTargetDescription ct{};
  ct.format = window.swapchain_format ();
  // RmlUi writes premultiplied alpha; match that when compositing over the
  // scene so partly transparent UI blends correctly.
  ct.blend_state.enable_blend = true;
  ct.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
  ct.blend_state.dst_color_blendfactor
      = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  ct.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
  ct.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
  ct.blend_state.dst_alpha_blendfactor
      = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  ct.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
  ct.blend_state.color_write_mask
      = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G
        | SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;

  pipe.target_info.num_color_targets = 1;
  pipe.target_info.color_target_descriptions = &ct;

  m_pipeline = gpu_graphics_pipeline (ctx.gpu_device, pipe);

  SDL_ReleaseGPUShader (ctx.gpu_device, vs);
  SDL_ReleaseGPUShader (ctx.gpu_device, fs);

  SDL_GPUSamplerCreateInfo sinfo{};
  SDL_zero (sinfo);
  sinfo.min_filter = SDL_GPU_FILTER_LINEAR;
  sinfo.mag_filter = SDL_GPU_FILTER_LINEAR;
  sinfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
  sinfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  sinfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  sinfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  m_sampler = gpu_sampler (ctx.gpu_device, sinfo);
}

void
ui_canvas::begin_frame (render_context &ctx) const
{
  if (!m_canvas || ctx.main_cmd == nullptr) {
    return;
  }

  SDL_GPUColorTargetInfo ct{};
  SDL_zero (ct);
  ct.texture = m_canvas.get ();
  ct.load_op = SDL_GPU_LOADOP_CLEAR;
  ct.store_op = SDL_GPU_STOREOP_STORE;
  ct.clear_color = { 0.0F, 0.0F, 0.0F, 0.0F };

  SDL_GPURenderPass *pass
      = SDL_BeginGPURenderPass (ctx.main_cmd, &ct, 1, nullptr);
  if (pass != nullptr) {
    SDL_EndGPURenderPass (pass);
  }
}

void
ui_canvas::composite (render_window &window, pixel_rect output_rect,
                      bool include_swapchain)
{
  render_context *ctx = window.ctx ();
  if (!m_canvas || !m_pipeline || !m_sampler || ctx == nullptr
      || ctx->main_cmd == nullptr) {
    return;
  }

  auto draw_into = [&] (SDL_GPUTexture *target, uint32_t target_w,
                        uint32_t target_h) {
    if (target == nullptr || target_w == 0 || target_h == 0) {
      return;
    }

    SDL_GPUColorTargetInfo ct{};
    SDL_zero (ct);
    ct.texture = target;
    ct.load_op = SDL_GPU_LOADOP_LOAD;
    ct.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass *pass
        = SDL_BeginGPURenderPass (ctx->main_cmd, &ct, 1, nullptr);
    if (pass == nullptr) {
      return;
    }

    pixel_rect rect = output_rect;
    if (rect.empty ()) {
      rect = pixel_rect{ 0, 0, static_cast<int> (target_w),
                         static_cast<int> (target_h) };
    }
    // Clamp to the target to keep the GPU viewport valid.
    if (rect.x < 0) {
      rect.width += rect.x;
      rect.x = 0;
    }
    if (rect.y < 0) {
      rect.height += rect.y;
      rect.y = 0;
    }
    if (rect.x + rect.width > static_cast<int> (target_w)) {
      rect.width = static_cast<int> (target_w) - rect.x;
    }
    if (rect.y + rect.height > static_cast<int> (target_h)) {
      rect.height = static_cast<int> (target_h) - rect.y;
    }
    if (rect.width <= 0 || rect.height <= 0) {
      SDL_EndGPURenderPass (pass);
      return;
    }

    SDL_GPUViewport vp{};
    vp.x = static_cast<float> (rect.x);
    vp.y = static_cast<float> (rect.y);
    vp.w = static_cast<float> (rect.width);
    vp.h = static_cast<float> (rect.height);
    vp.min_depth = 0.0F;
    vp.max_depth = 1.0F;
    SDL_SetGPUViewport (pass, &vp);

    SDL_Rect scissor{ rect.x, rect.y, rect.width, rect.height };
    SDL_SetGPUScissor (pass, &scissor);

    SDL_BindGPUGraphicsPipeline (pass, m_pipeline.get ());
    SDL_GPUTextureSamplerBinding binding{ m_canvas.get (), m_sampler.get () };
    SDL_BindGPUFragmentSamplers (pass, 0, &binding, 1);
    SDL_DrawGPUPrimitives (pass, 3, 1, 0, 0);

    SDL_EndGPURenderPass (pass);
  };

  auto &present = window.present_tex ();
  draw_into (present.texture_data, present.width, present.height);

  if (include_swapchain && window.present_to_swapchain ()) {
    auto &swapchain = window.swapchain ();
    draw_into (swapchain.texture_data, swapchain.width, swapchain.height);
  }
}

void
ui_canvas::destroy ()
{
  m_pipeline.reset ();
  m_sampler.reset ();
  m_canvas.reset ();
  m_width = 0;
  m_height = 0;
}

} // namespace wsl::gfx
