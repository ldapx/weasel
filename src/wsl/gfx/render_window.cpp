// window.cpp - HDR + Bloom + Tonemap (SDL_gpu3)
// NOTE: Requires these shaders compiled:
//   compiled_shaders/fullscreen.vert.slang.spv
//   compiled_shaders/bloom_downsample.frag.slang.spv
//   compiled_shaders/bloom_blur.frag.slang.spv
//   compiled_shaders/composite_tonemap.frag.slang.spv
//
// And your 3D pipelines (PBR/skybox/unlit) must render into 2 HDR targets
// (SV_Target0 scene, SV_Target1 bloom).

#include "render_window.hpp"
#ifdef WEASEL_ENABLE_RENDERDOC
#include "renderdoc.hpp"
#endif
#include "tracy_gpu_mem.hpp"
#include "wsl/log/log.hpp"

#include "wsl/gfx/shader.hpp" // Shader loader, also used in scene_renderer.cpp.
#include "wsl/rsc/resource_manager.hpp"

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_pixels.h>

#include <SDL3/SDL_stdinc.h>

#include <tracy/Tracy.hpp>
#include <SDL3/SDL_video.h>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <sys/types.h>
#include <vector>

namespace wsl
{

namespace gfx
{

SDL_GPUSampler *
render_window::ensure_linear_sampler ()
{
  if (!m_linear_sampler) {
    SDL_GPUSamplerCreateInfo si{};
    si.min_filter = SDL_GPU_FILTER_LINEAR;
    si.mag_filter = SDL_GPU_FILTER_LINEAR;
    si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    m_linear_sampler = gpu_sampler (m_ctx->gpu_device, si);
  }
  return m_linear_sampler.get ();
}

void
render_window::destroy_texture (gpu_texture &texture) const
{
  texture.reset ();
}

SDL_GPUGraphicsPipeline *
render_window::create_fullscreen_pipe (const char *frag_shader_path,
                                       SDL_GPUTextureFormat out_format,
                                       int num_uniform_buffers,
                                       int num_samplers)
{
  // Fullscreen triangle VS, no vertex buffers.
  auto vert_id = m_res_mgr->register_shader (
      "engine://compiled_shaders/fullscreen.vert.slang.spv");
  SDL_GPUShader *vert = wsl::gfx::shader::load_from_manager (
      m_ctx->gpu_device, m_res_mgr, vert_id, SDL_GPU_SHADERSTAGE_VERTEX,
      /*num_uniform_buffers=*/0,
      /*num_samplers=*/0);

  auto frag_id = m_res_mgr->register_shader (frag_shader_path);
  SDL_GPUShader *frag = wsl::gfx::shader::load_from_manager (
      m_ctx->gpu_device, m_res_mgr, frag_id, SDL_GPU_SHADERSTAGE_FRAGMENT,
      /*num_uniform_buffers=*/num_uniform_buffers,
      /*num_samplers=*/num_samplers);

  if ((vert == nullptr) || (frag == nullptr)) {
    if (vert != nullptr) {
      SDL_ReleaseGPUShader (m_ctx->gpu_device, vert);
    }
    if (frag != nullptr) {
      SDL_ReleaseGPUShader (m_ctx->gpu_device, frag);
    }
    return nullptr;
  }

  SDL_GPUGraphicsPipelineCreateInfo pipe{};
  SDL_zero (pipe);

  pipe.vertex_shader = vert;
  pipe.fragment_shader = frag;
  pipe.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

  pipe.depth_stencil_state.enable_depth_test = false;
  pipe.depth_stencil_state.enable_depth_write = false;
  pipe.depth_stencil_state.enable_stencil_test = false;

  pipe.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
  pipe.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;

  pipe.vertex_input_state.num_vertex_buffers = 0;
  pipe.vertex_input_state.vertex_buffer_descriptions = nullptr;
  pipe.vertex_input_state.num_vertex_attributes = 0;
  pipe.vertex_input_state.vertex_attributes = nullptr;

  SDL_GPUColorTargetDescription ctd{};
  SDL_zero (ctd);
  ctd.format = out_format;

  pipe.target_info.num_color_targets = 1;
  pipe.target_info.color_target_descriptions = &ctd;

  // no depth attachment in these passes
  pipe.target_info.has_depth_stencil_target = false;

  pipe.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
  pipe.multisample_state.sample_mask = 0;
  pipe.multisample_state.enable_mask = false;

  SDL_GPUGraphicsPipeline *out
      = SDL_CreateGPUGraphicsPipeline (m_ctx->gpu_device, &pipe);
  wsl::gfx::tracy_alloc_pipeline (out);

  SDL_ReleaseGPUShader (m_ctx->gpu_device, vert);
  SDL_ReleaseGPUShader (m_ctx->gpu_device, frag);

  return out;
}

gpu_graphics_pipeline
render_window::create_composite_pipe ()
{
  // composite outputs to m_swapchain format (LDR)
  SDL_GPUTextureFormat const sc_fmt
      = SDL_GetGPUSwapchainTextureFormat (m_ctx->gpu_device, m_handler);

  return gpu_graphics_pipeline::adopt (
      m_ctx->gpu_device,
      create_fullscreen_pipe (
          "engine://compiled_shaders/composite_tonemap.frag.slang.spv", sc_fmt,
          /*num_uniform_buffers=*/1, /*Composite cbuffer*/
          /*num_samplers=*/2 /*scene + bloom*/));
}

gpu_graphics_pipeline
render_window::create_downsample_pipe ()
{
  return gpu_graphics_pipeline::adopt (
      m_ctx->gpu_device,
      create_fullscreen_pipe (
          "engine://compiled_shaders/bloom_downsample.frag.slang.spv",
          SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,
          /*num_uniform_buffers=*/1, /*Downsample cbuffer*/
          /*num_samplers=*/1));
}

gpu_graphics_pipeline
render_window::create_blur_pipe ()
{
  return gpu_graphics_pipeline::adopt (
      m_ctx->gpu_device,
      create_fullscreen_pipe (
          "engine://compiled_shaders/bloom_blur.frag.slang.spv",
          SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,
          /*num_uniform_buffers=*/1, /*Blur cbuffer*/
          /*num_samplers=*/1));
}

render_window::render_window (const char *name, int width, int height,
                              wsl::gfx::render_context *m_ctx,
                              wsl::rsc::resource_manager *res_mgr,
                              bool headless, bool try_disable_vsync_on_startup)
    : m_ctx (m_ctx), m_res_mgr (res_mgr)
{
  if (headless) {
    wsl::log::gfx ()->debug ("Headless mode, skipping window creation");
    return;
  }

  m_handler = SDL_CreateWindow (name, width, height, SDL_WINDOW_RESIZABLE);
  SDL_ShowWindow (m_handler);
  SDL_ClaimWindowForGPUDevice (m_ctx->gpu_device, m_handler);
  m_swapchain_format
      = SDL_GetGPUSwapchainTextureFormat (m_ctx->gpu_device, m_handler);

  // Default behavior: try to disable vsync so the triple-buffered
  // pipeline can actually overlap frames. The change is wrapped in
  // `set_vsync`, which spdlog-logs and bails out (keeping VSYNC) if
  // the backend refuses. The known AMDVK / Mesa driver-side bug —
  // where `SDL_WindowSupportsGPUPresentMode` and
  // `SDL_SetGPUSwapchainParameters` both return success for
  // IMMEDIATE/MAILBOX, but the next compute dispatch segfaults —
  // is documented on `set_vsync`. AMDVK users should pass
  // `try_disable_vsync_on_startup = false` to skip this attempt, or
  // call `set_vsync(true)` immediately after construction.
  if (try_disable_vsync_on_startup) {
    if (!set_vsync (false)) {
      wsl::log::gfx ()->error (
          "render_window: failed to disable vsync on startup, "
          "m_swapchain remains vsync-paced (VSYNC).");
    }
  }

  wsl::log::gfx ()->debug ("Window: {} ({}x{}), m_swapchain format={:#x}", name,
                           width, height,
                           static_cast<unsigned> (m_swapchain_format));

  // Allocate initial resources based on current size.
  on_resize ();

  // Create post-process pipelines (once).
  m_pipe_downsample = create_downsample_pipe ();
  m_pipe_blur = create_blur_pipe ();
  m_pipe_composite = create_composite_pipe ();

  // Tracy frame image capture. Target size must be divisible by 4
  // (Tracy requirement). 320x180 is the recommended thumbnail size
  // from the Tracy manual.
  frame_image_init (320, 180);
}

bool
render_window::set_vsync (bool enabled)
{
  if (m_handler == nullptr || m_ctx->gpu_device == nullptr) {
    wsl::log::gfx ()->error ("render_window::set_vsync: no window or device");
    return false;
  }

  // Pick the requested present mode based on the desired vsync state
  // and what the backend actually supports. We re-query support on
  // every call because the user may have moved the window to a
  // different monitor with different capabilities between toggles.
  SDL_GPUPresentMode requested;
  if (enabled) {
    // vsync ON: prefer MAILBOX (lower-latency vsync, drops pending
    // images instead of queueing them) when available, fall back to
    // plain VSYNC otherwise.
    if (SDL_WindowSupportsGPUPresentMode (m_ctx->gpu_device, m_handler,
                                          SDL_GPU_PRESENTMODE_MAILBOX)) {
      requested = SDL_GPU_PRESENTMODE_MAILBOX;
    } else {
      requested = SDL_GPU_PRESENTMODE_VSYNC;
    }
  } else {
    // vsync OFF: IMMEDIATE (no vblank sync). Bail with a spdlog error
    // if the backend doesn't advertise it — we'd rather keep the
    // previous mode than call `SDL_SetGPUSwapchainParameters` with a
    // mode that's known to fail.
    if (!SDL_WindowSupportsGPUPresentMode (m_ctx->gpu_device, m_handler,
                                           SDL_GPU_PRESENTMODE_IMMEDIATE)) {
      wsl::log::gfx ()->error (
          "render_window::set_vsync: IMMEDIATE present mode not "
          "supported by the backend, vsync stays enabled");
      return false;
    }
    requested = SDL_GPU_PRESENTMODE_IMMEDIATE;
  }

  if (!SDL_SetGPUSwapchainParameters (m_ctx->gpu_device, m_handler,
                                      m_swapchain_composition, requested)) {
    wsl::log::gfx ()->error (
        "render_window::set_vsync: SDL_SetGPUSwapchainParameters "
        "rejected the request ({})",
        SDL_GetError ());
    return false;
  }

  m_vsync = enabled;
  char const *mode_name
      = (requested == SDL_GPU_PRESENTMODE_VSYNC)       ? "VSYNC"
        : (requested == SDL_GPU_PRESENTMODE_MAILBOX)   ? "MAILBOX"
        : (requested == SDL_GPU_PRESENTMODE_IMMEDIATE) ? "IMMEDIATE"
                                                       : "UNKNOWN";
  wsl::log::gfx ()->info ("render_window::set_vsync: vsync {} (mode={})",
                          enabled ? "ON" : "OFF", mode_name);
  return true;
}

render_window::~render_window ()
{
  if (m_ctx->gpu_device == nullptr) {
    if (m_handler != nullptr)
      SDL_DestroyWindow (m_handler);
    return;
  }

  frame_image_shutdown ();

  SDL_WaitForGPUIdle (m_ctx->gpu_device);

  m_pipe_downsample.reset ();
  m_pipe_blur.reset ();
  m_pipe_composite.reset ();

  m_linear_sampler.reset ();

  m_bloom_a.reset ();
  m_bloom_b.reset ();
  m_hdr_scene.reset ();
  m_hdr_bloom_src.reset ();
  m_msaa_hdr_scene.reset ();
  m_msaa_hdr_bloom.reset ();

  m_depth_texture.reset ();
  // m_present_tex.texture_data is a raw pointer (from
  // wsl::gfx::texture). Free it manually with Tracy bookkeeping
  // to match the tracy_alloc_texture call in create_hdr_target().
  if (m_present_tex.texture_data != nullptr) {
    wsl::gfx::tracy_free_texture (m_present_tex.texture_data);
    SDL_ReleaseGPUTexture (m_ctx->gpu_device, m_present_tex.texture_data);
    m_present_tex.texture_data = nullptr;
  }

  SDL_WaitForGPUIdle (m_ctx->gpu_device);

  SDL_ReleaseWindowFromGPUDevice (m_ctx->gpu_device, m_handler);
  SDL_DestroyWindow (m_handler);
  SDL_PumpEvents ();
}

void
render_window::get_size (uint32_t &width, uint32_t &height) const
{
  if (m_handler == nullptr) {
    width = 0;
    height = 0;
    return;
  }

  int w;
  int h;
  SDL_GetWindowSize (m_handler, &w, &h);
  width = (uint32_t)w;
  height = (uint32_t)h;
}

void
render_window::get_size (int &width, int &height) const
{
  if (m_handler == nullptr) {
    width = 0;
    height = 0;
    return;
  }
  SDL_GetWindowSize (m_handler, &width, &height);
}

void
render_window::create_depth_texture ()
{
  if (m_ctx->gpu_device == nullptr || m_handler == nullptr)
    return;

  destroy_texture (m_depth_texture);

  int w;
  int h;
  SDL_GetWindowSizeInPixels (m_handler, &w, &h);

  SDL_GPUTextureCreateInfo info{};
  info.type = SDL_GPU_TEXTURETYPE_2D;
  info.format = m_depth_format;
  info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
  info.width = (uint32_t)w;
  info.height = (uint32_t)h;
  info.layer_count_or_depth = 1;
  info.num_levels = 1;
  info.sample_count = SDL_GPU_SAMPLECOUNT_4; // MSAA matches MRT pass

  m_depth_texture = gpu_texture (m_ctx->gpu_device, info);
  if (!m_depth_texture) {
    wsl::log::gfx ()->error ("Failed to create depth texture: {}",
                             SDL_GetError ());
  } else {
    // Resource name (vkSetDebugUtilsObjectNameEXT under the hood);
    // shows up in RenderDoc's Resource Inspector and Texture Viewer.
    SDL_SetGPUTextureName (m_ctx->gpu_device, m_depth_texture.get (),
                           "Depth Buffer");
  }
}

void
render_window::begin_3d_pass (bool clear_color, bool clear_depth,
                              const char *label) const
{
  ZoneScoped;
  if ((!m_msaa_hdr_scene) || (!m_msaa_hdr_bloom) || (!m_hdr_scene)
      || (!m_hdr_bloom_src) || (!m_depth_texture)) {
    wsl::log::gfx ()->warn (
        "begin_3d_pass: null render target texture(s), skipping");
    return;
  }

#ifdef WEASEL_ENABLE_RENDERDOC
  wsl::gfx::rdoc::annotate_command (m_ctx->main_cmd, "pass.3d", "main");
#endif
  // Visible as a coloured region in RenderDoc's Event Browser (via
  // VK_EXT_debug_utils / ID3DUserDefinedAnnotation under the hood).
  SDL_PushGPUDebugGroup (m_ctx->main_cmd, label);

  SDL_GPUColorTargetInfo ct[2]{};

  // Scene HDR
  ct[0].texture = m_msaa_hdr_scene.get ();
  ct[0].load_op = clear_color ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
  ct[0].store_op = SDL_GPU_STOREOP_RESOLVE;
  ct[0].clear_color = m_scene_clear_color;
  ct[0].resolve_texture = m_hdr_scene.get ();

  // Bloom source HDR
  ct[1].texture = m_msaa_hdr_bloom.get ();
  ct[1].load_op = clear_color ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
  ct[1].store_op = SDL_GPU_STOREOP_RESOLVE;
  ct[1].clear_color = { 0.0F, 0.0F, 0.0F, 1.0F };
  ct[1].resolve_texture = m_hdr_bloom_src.get ();

  SDL_GPUDepthStencilTargetInfo ds{};
  SDL_zero (ds);
  ds.texture = m_depth_texture.get ();
  ds.clear_depth = 1.0F;
  ds.load_op = clear_depth ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
  ds.store_op = SDL_GPU_STOREOP_STORE;

  m_ctx->begin_main_render_pass (ct, 2, &ds);

  // If a viewport is active, set it on the render pass.
  if (has_active_viewport ()) {
    auto vp = current_viewport ();
    int w;
    int h;
    SDL_GetWindowSizeInPixels (m_handler, &w, &h);
    auto pix
        = vp.to_pixels (static_cast<uint32_t> (w), static_cast<uint32_t> (h));

    SDL_GPUViewport gpu_vp{};
    gpu_vp.x = static_cast<float> (pix.x);
    gpu_vp.y = static_cast<float> (pix.y);
    gpu_vp.w = static_cast<float> (pix.width);
    gpu_vp.h = static_cast<float> (pix.height);
    gpu_vp.min_depth = vp.min_depth;
    gpu_vp.max_depth = vp.max_depth;
    m_ctx->set_viewport (gpu_vp);

    SDL_Rect scissor{};
    scissor.x = pix.x;
    scissor.y = pix.y;
    scissor.w = pix.width;
    scissor.h = pix.height;
    m_ctx->set_scissor_rect (scissor);
  }
}

void
render_window::end_3d_pass (bool run_postprocess)
{
  ZoneScoped;
  if (m_ctx->has_main_render_pass ()) {
    m_ctx->end_main_render_pass ();
  }

  // Close the "Main 3D Pass" debug group opened in begin_3d_pass
  // (the postprocess pass opens its own group below).
  if (m_ctx->main_cmd != nullptr) {
    SDL_PopGPUDebugGroup (m_ctx->main_cmd);
  }

  if (run_postprocess) {
    // Always build m_present_tex (Game View samples this).
    // Only also write to m_swapchain if present_to_swapchain is true.
    postprocess_hdr_bloom ();
  }
}

void
render_window::begin_subviewport_pass (const subviewport_target &target,
                                       bool clear_color, bool clear_depth,
                                       const char *label) const
{
  ZoneScoped;
  if ((!target.color_msaa) || (!target.color_resolve) || (!target.depth)) {
    wsl::log::gfx ()->warn (
        "begin_subviewport_pass: null render target texture(s), skipping");
    return;
  }

#ifdef WEASEL_ENABLE_RENDERDOC
  wsl::gfx::rdoc::annotate_command (m_ctx->main_cmd, "pass.3d", "subviewport");
#endif
  SDL_PushGPUDebugGroup (m_ctx->main_cmd, label);

  SDL_GPUColorTargetInfo ct[2]{};

  ct[0].texture = target.color_msaa.get ();
  ct[0].load_op = clear_color ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
  ct[0].store_op = SDL_GPU_STOREOP_RESOLVE;
  ct[0].clear_color = m_scene_clear_color;
  ct[0].resolve_texture = target.color_resolve.get ();

  ct[1].texture = target.bloom_msaa.get ();
  ct[1].load_op = clear_color ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
  ct[1].store_op = SDL_GPU_STOREOP_RESOLVE;
  ct[1].clear_color = { 0.0F, 0.0F, 0.0F, 1.0F };
  ct[1].resolve_texture = target.bloom_resolve.get ();

  SDL_GPUDepthStencilTargetInfo ds{};
  SDL_zero (ds);
  ds.texture = target.depth.get ();
  ds.clear_depth = 1.0F;
  ds.load_op = clear_depth ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
  ds.store_op = SDL_GPU_STOREOP_STORE;

  m_ctx->begin_main_render_pass (ct, 2, &ds);

  // Full target size viewport
  m_ctx->reset_viewport (target.width, target.height);
  m_ctx->reset_scissor_rect (target.width, target.height);
}

void
render_window::end_subviewport_pass ()
{
  ZoneScoped;
  if (m_ctx->has_main_render_pass ()) {
    m_ctx->end_main_render_pass ();
  }
  if (m_ctx->main_cmd != nullptr) {
    SDL_PopGPUDebugGroup (m_ctx->main_cmd);
  }
}

void
render_window::begin_ui_pass () const
{
  ZoneScoped;
#ifdef WEASEL_ENABLE_RENDERDOC
  wsl::gfx::rdoc::annotate_command (m_ctx->main_cmd, "pass.ui", "ui");
#endif
  if (m_ctx->main_cmd != nullptr) {
    SDL_PushGPUDebugGroup (m_ctx->main_cmd, "UI Pass");
  }
  m_ctx->begin_ui_render_pass (m_swapchain.texture_data);
}

void
render_window::end_ui_pass () const
{
  ZoneScoped;
  m_ctx->end_ui_render_pass ();
  if (m_ctx->main_cmd != nullptr) {
    SDL_PopGPUDebugGroup (m_ctx->main_cmd);
  }
}

void
render_window::push_viewport (const gfx::viewport &vp)
{
  m_viewport_stack.push_back (vp);
}

void
render_window::pop_viewport ()
{
  if (!m_viewport_stack.empty ()) {
    m_viewport_stack.pop_back ();
  }
}

void
render_window::reset_viewports ()
{
  m_viewport_stack.clear ();
}

bool
render_window::has_active_viewport () const
{
  return !m_viewport_stack.empty ();
}

size_t
render_window::viewport_count () const
{
  return m_viewport_stack.size ();
}

gfx::viewport
render_window::current_viewport () const
{
  if (!m_viewport_stack.empty ()) {
    return m_viewport_stack.back ();
  }
  // Default full-screen viewport.
  return gfx::viewport{};
}

void
render_window::apply_viewport (const gfx::viewport &vp) const
{
  int w;
  int h;
  SDL_GetWindowSizeInPixels (m_handler, &w, &h);
  auto pix
      = vp.to_pixels (static_cast<uint32_t> (w), static_cast<uint32_t> (h));

  SDL_GPUViewport gpu_vp{};
  gpu_vp.x = static_cast<float> (pix.x);
  gpu_vp.y = static_cast<float> (pix.y);
  gpu_vp.w = static_cast<float> (pix.width);
  gpu_vp.h = static_cast<float> (pix.height);
  gpu_vp.min_depth = vp.min_depth;
  gpu_vp.max_depth = vp.max_depth;
  m_ctx->set_viewport (gpu_vp);

  SDL_Rect scissor{};
  scissor.x = pix.x;
  scissor.y = pix.y;
  scissor.w = pix.width;
  scissor.h = pix.height;
  m_ctx->set_scissor_rect (scissor);
}

void
render_window::new_swapchain ()
{
  ZoneScoped;
  // Try the non-blocking acquire first. With max-3 frames in flight and
  // IMMEDIATE present mode this is essentially always successful, and
  // when it is the CPU never blocks on the m_swapchain.
  //
  // The catch: per the SDL docs, when too many frames are in flight
  // `SDL_AcquireGPUSwapchainTexture` returns `true` with
  // `swapchain_texture = NULL` as an "indication to wait". Recording
  // GPU work on a cmd buffer that holds a NULL m_swapchain acquire is
  // legal but the AMDVK driver is observably fragile about it — the
  // next `SDL_DispatchGPUCompute` (or even the copy pass) segfaults
  // because the cmd buffer's submit-side present is in an
  // inconsistent state. This was the source of the intermittent
  // crashes after the IMMEDIATE change.
  //
  // Critical: we must NOT call the blocking fallback
  // `SDL_WaitAndAcquireGPUSwapchainTexture` on the same `m_ctx->main_cmd`
  // that already has the NULL acquire recorded — that would leave the
  // command buffer with TWO m_swapchain acquires (one NULL, one valid)
  // and AMDVK uses the NULL one. The fallback MUST use a fresh command
  // buffer. As a bonus, this also gives us a clean command buffer
  // with exactly one valid m_swapchain acquire for the rest of the frame.
  constexpr int kSpinAttempts = 4;
  {
    ZoneScopedN ("new_swapchain::non_blocking_spin");
    for (int attempt = 0; attempt < kSpinAttempts; ++attempt) {
      bool const ok = SDL_AcquireGPUSwapchainTexture (
          m_ctx->main_cmd, m_handler, &m_swapchain.texture_data,
          &m_swapchain.width, &m_swapchain.height);
      if (ok && m_swapchain.texture_data != nullptr) {
        return;
      }
      if (ok) {
        // Got `true` but the texture is NULL — too many frames in flight.
        // The non-blocking acquire already left a NULL acquire on
        // `m_ctx->main_cmd`, so we cannot reuse it for the blocking
        // fallback. Submit the poisoned cmd buffer as a no-op (it has
        // no recorded draws yet — `new_swapchain` is the first call
        // before any pass) and get a fresh one for the blocking acquire.
        {
          ZoneScopedN ("new_swapchain::recover_poison");
          SDL_GPUFence *poison_fence
              = SDL_SubmitGPUCommandBufferAndAcquireFence (m_ctx->main_cmd);
          if (poison_fence != nullptr) {
            SDL_WaitForGPUFences (m_ctx->gpu_device, true, &poison_fence, 1);
            SDL_ReleaseGPUFence (m_ctx->gpu_device, poison_fence);
          }
          m_ctx->main_cmd = SDL_AcquireGPUCommandBuffer (m_ctx->gpu_device);
          if (m_ctx->main_cmd == nullptr) {
            wsl::log::gfx ()->error (
                "new_swapchain: failed to acquire fresh cmd buffer: {}",
                SDL_GetError ());
            return;
          }
        }
        break;
      }
      // `false` is a hard error. Bail out and let the frame render to
      // m_present_tex only.
      wsl::log::gfx ()->debug (
          "new_swapchain: non-blocking acquire returned false ({}), "
          "falling back to blocking acquire",
          SDL_GetError ());
      break;
    }
  }

  {
    ZoneScopedN ("new_swapchain::blocking_acquire");
    bool const ok = SDL_WaitAndAcquireGPUSwapchainTexture (
        m_ctx->main_cmd, m_handler, &m_swapchain.texture_data,
        &m_swapchain.width, &m_swapchain.height);
    if (!ok) {
      wsl::log::gfx ()->error (
          "new_swapchain: blocking acquire also failed: {}", SDL_GetError ());
      m_swapchain.texture_data = nullptr;
      m_swapchain.width = 0;
      m_swapchain.height = 0;
    }
  }
}

void
render_window::on_resize ()
{
  int w;
  int h;
  SDL_GetWindowSizeInPixels (m_handler, &w, &h);

  if (w <= 0 || h <= 0) {
    return;
  }

  wsl::log::gfx ()->debug ("Resize: {}x{}", w, h);

  SDL_WaitForGPUIdle (m_ctx->gpu_device);

  // Depth
  create_depth_texture ();

  // Destroy HDR/bloom targets
  destroy_texture (m_msaa_hdr_scene);
  destroy_texture (m_msaa_hdr_bloom);
  destroy_texture (m_hdr_scene);
  destroy_texture (m_hdr_bloom_src);
  destroy_texture (m_bloom_a);
  destroy_texture (m_bloom_b);
  // m_present_tex.texture_data is a raw pointer; free manually with
  // Tracy bookkeeping so the wsl.gfx.textures pool stays consistent.
  if (m_present_tex.texture_data != nullptr) {
    wsl::gfx::tracy_free_texture (m_present_tex.texture_data);
    SDL_ReleaseGPUTexture (m_ctx->gpu_device, m_present_tex.texture_data);
    m_present_tex.texture_data = nullptr;
  }

  // MSAA HDR targets (two MRT textures)
  SDL_GPUTextureCreateInfo msaa{};
  SDL_zero (msaa);
  msaa.type = SDL_GPU_TEXTURETYPE_2D;
  msaa.width = (uint32_t)w;
  msaa.height = (uint32_t)h;
  msaa.layer_count_or_depth = 1;
  msaa.num_levels = 1;
  msaa.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
  msaa.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
  msaa.sample_count = SDL_GPU_SAMPLECOUNT_4;

  auto create_hdr_target = [&] (const SDL_GPUTextureCreateInfo &ci) {
    SDL_GPUTexture *tex = SDL_CreateGPUTexture (m_ctx->gpu_device, &ci);
    if (tex == nullptr) {
      wsl::log::gfx ()->error ("Failed to create HDR render target ({}x{}): {}",
                               ci.width, ci.height, SDL_GetError ());
      return tex;
    }
    wsl::gfx::tracy_alloc_texture (tex, ci);
    return tex;
  };

  m_msaa_hdr_scene
      = gpu_texture::adopt (m_ctx->gpu_device, create_hdr_target (msaa));
  m_msaa_hdr_bloom
      = gpu_texture::adopt (m_ctx->gpu_device, create_hdr_target (msaa));
  if (m_msaa_hdr_scene)
    SDL_SetGPUTextureName (m_ctx->gpu_device, m_msaa_hdr_scene.get (),
                           "MSAA HDR Scene (4x)");
  if (m_msaa_hdr_bloom)
    SDL_SetGPUTextureName (m_ctx->gpu_device, m_msaa_hdr_bloom.get (),
                           "MSAA HDR Bloom (4x)");

  // Resolved HDR targets must be sampler + color target
  SDL_GPUTextureCreateInfo res = msaa;
  res.sample_count = SDL_GPU_SAMPLECOUNT_1;
  res.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;

  m_hdr_scene = gpu_texture::adopt (m_ctx->gpu_device, create_hdr_target (res));
  m_hdr_bloom_src
      = gpu_texture::adopt (m_ctx->gpu_device, create_hdr_target (res));
  if (m_hdr_scene)
    SDL_SetGPUTextureName (m_ctx->gpu_device, m_hdr_scene.get (), "HDR Scene");
  if (m_hdr_bloom_src)
    SDL_SetGPUTextureName (m_ctx->gpu_device, m_hdr_bloom_src.get (),
                           "HDR Bloom Source");

  // Half-res bloom ping-pong
  SDL_GPUTextureCreateInfo half = res;
  half.width = (uint32_t)std::max (1, w / 2);
  half.height = (uint32_t)std::max (1, h / 2);

  m_bloom_a = gpu_texture::adopt (m_ctx->gpu_device, create_hdr_target (half));
  m_bloom_b = gpu_texture::adopt (m_ctx->gpu_device, create_hdr_target (half));
  if (m_bloom_a)
    SDL_SetGPUTextureName (m_ctx->gpu_device, m_bloom_a.get (),
                           "Bloom A (half-res)");
  if (m_bloom_b)
    SDL_SetGPUTextureName (m_ctx->gpu_device, m_bloom_b.get (),
                           "Bloom B (half-res)");

  // Create LDR output texture (same format as m_swapchain) so UI can sample it
  SDL_GPUTextureCreateInfo out{};
  SDL_zero (out);
  out.type = SDL_GPU_TEXTURETYPE_2D;
  out.width = (uint32_t)w;
  out.height = (uint32_t)h;
  out.layer_count_or_depth = 1;
  out.num_levels = 1;
  out.sample_count = SDL_GPU_SAMPLECOUNT_1;
  out.format = m_swapchain_format;
  out.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;

  m_present_tex.texture_data = create_hdr_target (out);
  if (m_present_tex.texture_data != nullptr)
    SDL_SetGPUTextureName (m_ctx->gpu_device, m_present_tex.texture_data,
                           "Present Tex (sampleable LDR)");
  m_present_tex.width = (uint32_t)w;
  m_present_tex.height = (uint32_t)h;

  // Re-size the Tracy frame-image staging buffer to match the new
  // m_present_tex. The GPU is already idle (SDL_WaitForGPUIdle at the
  // top of on_resize), so the existing transfer buffer is safe to
  // release. frame_image_resize() is a no-op if the dimensions
  // haven't changed, so repeated calls during a drag-resize are
  // cheap.
  frame_image_resize (m_present_tex.width, m_present_tex.height);
}

void
render_window::postprocess_hdr_bloom ()
{
  ZoneScoped;
  if ((!m_hdr_bloom_src) || (!m_hdr_scene) || (!m_bloom_a) || (!m_bloom_b)
      || (m_present_tex.texture_data == nullptr)) {
    return;
  }

#ifdef WEASEL_ENABLE_RENDERDOC
  wsl::gfx::rdoc::annotate_command (m_ctx->main_cmd, "pass.postprocess",
                                    "bloom_tonemap");
#endif
  // Marker region in the Event Browser; closes at the end of this
  // function. The sub-passes below push their own nested groups so the
  // timeline reads as Postprocess > Bloom Downsample / Blur H / Blur V /
  // Tonemap Swapchain / Tonemap PresentTex.
  SDL_PushGPUDebugGroup (m_ctx->main_cmd, "Postprocess");

  ensure_linear_sampler ();

  // Pipelines must exist (created in ctor). If shader compilation failed,
  // skip post to avoid crashing. The Postprocess debug group was pushed
  // above, so pop it before returning to keep the GPU debug-group stack
  // balanced for subsequent passes (begin_ui_pass would otherwise be
  // nesting inside the unclosed Postprocess group).
  if ((!m_pipe_downsample) || (!m_pipe_blur) || (!m_pipe_composite)) {
    SDL_PopGPUDebugGroup (m_ctx->main_cmd);
    return;
  }

  int ww;
  int hh;
  SDL_GetWindowSizeInPixels (m_handler, &ww, &hh);

  // ---------- (1) Downsample bloom_src -> m_bloom_a ----------
  {
    ZoneScopedN ("postprocess::downsample");
#ifdef WEASEL_ENABLE_RENDERDOC
    wsl::gfx::rdoc::annotate_command (m_ctx->main_cmd, "pass.postprocess.bloom",
                                      "downsample");
#endif
    SDL_PushGPUDebugGroup (m_ctx->main_cmd, "Bloom Downsample");
    SDL_GPUColorTargetInfo ct{};
    SDL_zero (ct);
    ct.texture = m_bloom_a.get ();
    ct.load_op = SDL_GPU_LOADOP_CLEAR;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    ct.clear_color = { 0, 0, 0, 1 };

    SDL_GPURenderPass *pass
        = SDL_BeginGPURenderPass (m_ctx->main_cmd, &ct, 1, nullptr);
    if (pass != nullptr) {
      SDL_BindGPUGraphicsPipeline (pass, m_pipe_downsample.get ());

      struct alignas (16) down
      {
        float texel[2];
        float pad[2];
      } p{};
      p.texel[0] = 1.0F / float (ww);
      p.texel[1] = 1.0F / float (hh);

      SDL_PushGPUFragmentUniformData (m_ctx->main_cmd, 0, &p, sizeof (p));

      SDL_GPUTextureSamplerBinding b{};
      SDL_zero (b);
      b.texture = m_hdr_bloom_src.get ();
      b.sampler = m_linear_sampler.get ();
      SDL_BindGPUFragmentSamplers (pass, 0, &b, 1);

      SDL_DrawGPUPrimitives (pass, 3, 1, 0, 0);
      SDL_EndGPURenderPass (pass);
    }
    SDL_PopGPUDebugGroup (m_ctx->main_cmd);
  }

  // bloom texel size (half res)
  uint32_t const bw = std::max (1U, (uint32_t)ww / 2);
  uint32_t const bh = std::max (1U, (uint32_t)hh / 2);
  float const bloom_texel[2] = { 1.0F / float (bw), 1.0F / float (bh) };

  // ---------- (2) Blur H: m_bloom_a -> m_bloom_b ----------
  {
    ZoneScopedN ("postprocess::blur_h");
#ifdef WEASEL_ENABLE_RENDERDOC
    wsl::gfx::rdoc::annotate_command (m_ctx->main_cmd, "pass.postprocess.bloom",
                                      "blur_h");
#endif
    SDL_PushGPUDebugGroup (m_ctx->main_cmd, "Bloom Blur H");
    SDL_GPUColorTargetInfo ct{};
    SDL_zero (ct);
    ct.texture = m_bloom_b.get ();
    ct.load_op = SDL_GPU_LOADOP_CLEAR;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    ct.clear_color = { 0, 0, 0, 1 };

    SDL_GPURenderPass *pass
        = SDL_BeginGPURenderPass (m_ctx->main_cmd, &ct, 1, nullptr);
    if (pass != nullptr) {
      SDL_BindGPUGraphicsPipeline (pass, m_pipe_blur.get ());

      struct alignas (16) blur
      {
        float texel[2];
        float dir[2];
      } p{};
      p.texel[0] = bloom_texel[0];
      p.texel[1] = bloom_texel[1];
      p.dir[0] = 1.0F;
      p.dir[1] = 0.0F;

      SDL_PushGPUFragmentUniformData (m_ctx->main_cmd, 0, &p, sizeof (p));

      SDL_GPUTextureSamplerBinding b{};
      SDL_zero (b);
      b.texture = m_bloom_a.get ();
      b.sampler = m_linear_sampler.get ();
      SDL_BindGPUFragmentSamplers (pass, 0, &b, 1);

      SDL_DrawGPUPrimitives (pass, 3, 1, 0, 0);
      SDL_EndGPURenderPass (pass);
    }
    SDL_PopGPUDebugGroup (m_ctx->main_cmd);
  }

  // ---------- (3) Blur V: m_bloom_b -> m_bloom_a ----------
  {
    ZoneScopedN ("postprocess::blur_v");
#ifdef WEASEL_ENABLE_RENDERDOC
    wsl::gfx::rdoc::annotate_command (m_ctx->main_cmd, "pass.postprocess.bloom",
                                      "blur_v");
#endif
    SDL_PushGPUDebugGroup (m_ctx->main_cmd, "Bloom Blur V");
    SDL_GPUColorTargetInfo ct{};
    SDL_zero (ct);
    ct.texture = m_bloom_a.get ();
    ct.load_op = SDL_GPU_LOADOP_CLEAR;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    ct.clear_color = { 0, 0, 0, 1 };

    SDL_GPURenderPass *pass
        = SDL_BeginGPURenderPass (m_ctx->main_cmd, &ct, 1, nullptr);
    if (pass != nullptr) {
      SDL_BindGPUGraphicsPipeline (pass, m_pipe_blur.get ());

      struct alignas (16) blur
      {
        float texel[2];
        float dir[2];
      } p{};
      p.texel[0] = bloom_texel[0];
      p.texel[1] = bloom_texel[1];
      p.dir[0] = 0.0F;
      p.dir[1] = 1.0F;

      SDL_PushGPUFragmentUniformData (m_ctx->main_cmd, 0, &p, sizeof (p));

      SDL_GPUTextureSamplerBinding b{};
      SDL_zero (b);
      b.texture = m_bloom_b.get ();
      b.sampler = m_linear_sampler.get ();
      SDL_BindGPUFragmentSamplers (pass, 0, &b, 1);

      SDL_DrawGPUPrimitives (pass, 3, 1, 0, 0);
      SDL_EndGPURenderPass (pass);
    }
    SDL_PopGPUDebugGroup (m_ctx->main_cmd);
  }

  // ---------- (4) Composite + tonemap: m_hdr_scene + m_bloom_a -> m_swapchain
  // ----------
  // Skipped when the app renders the scene itself (editor front-ends that
  // blit present_tex into their UI): the swapchain then only carries the UI,
  // and the scene does not get composited (and cleared) twice per frame.
  if (m_present_to_swapchain && (m_swapchain.texture_data != nullptr)) {
    ZoneScopedN ("postprocess::composite_swapchain");
#ifdef WEASEL_ENABLE_RENDERDOC
    wsl::gfx::rdoc::annotate_command (
        m_ctx->main_cmd, "pass.postprocess.tonemap", "m_swapchain");
#endif
    SDL_PushGPUDebugGroup (m_ctx->main_cmd, "Tonemap Swapchain");
    SDL_GPUColorTargetInfo ct{};
    SDL_zero (ct);
    ct.texture = m_swapchain.texture_data;
    ct.load_op = SDL_GPU_LOADOP_CLEAR;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    ct.clear_color = { 0, 0, 0, 1 };

    SDL_GPURenderPass *pass
        = SDL_BeginGPURenderPass (m_ctx->main_cmd, &ct, 1, nullptr);
    if (pass != nullptr) {
      SDL_BindGPUGraphicsPipeline (pass, m_pipe_composite.get ());

      struct alignas (16) comp
      {
        float m_exposure;
        float bloom_int;
        float pad[2];
      } c{};
      c.m_exposure = m_exposure;
      c.bloom_int = m_bloom_intensity;

      SDL_PushGPUFragmentUniformData (m_ctx->main_cmd, 0, &c, sizeof (c));

      SDL_GPUTextureSamplerBinding b[2]{};
      SDL_zero (b);
      b[0].texture = m_hdr_scene.get ();
      b[0].sampler = m_linear_sampler.get ();
      b[1].texture = m_bloom_a.get ();
      b[1].sampler = m_linear_sampler.get ();

      SDL_BindGPUFragmentSamplers (pass, 0, b, 2);

      SDL_DrawGPUPrimitives (pass, 3, 1, 0, 0);
      SDL_EndGPURenderPass (pass);
    }
    // Close the "Tonemap Swapchain" group before the next composite
    // opens its own. Without this the Present Tex group is nested
    // inside the Swapchain group.
    SDL_PopGPUDebugGroup (m_ctx->main_cmd);
  }

  // ---------- (5) Composite + tonemap also into m_present_tex (sampleable)
  // ----------
  if (m_present_tex.texture_data != nullptr) {
    ZoneScopedN ("postprocess::composite_present_tex");
#ifdef WEASEL_ENABLE_RENDERDOC
    wsl::gfx::rdoc::annotate_command (
        m_ctx->main_cmd, "pass.postprocess.tonemap", "m_present_tex");
#endif
    SDL_PushGPUDebugGroup (m_ctx->main_cmd, "Tonemap Present Tex");
    SDL_GPUColorTargetInfo ct{};
    SDL_zero (ct);
    ct.texture = m_present_tex.texture_data;
    ct.load_op = SDL_GPU_LOADOP_CLEAR;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    ct.clear_color = { 0, 0, 0, 1 };

    SDL_GPURenderPass *pass
        = SDL_BeginGPURenderPass (m_ctx->main_cmd, &ct, 1, nullptr);
    if (pass != nullptr) {
      SDL_BindGPUGraphicsPipeline (pass, m_pipe_composite.get ());

      struct alignas (16) comp
      {
        float m_exposure;
        float bloom_int;
        float pad[2];
      } c{};
      c.m_exposure = m_exposure;
      c.bloom_int = m_bloom_intensity;

      SDL_PushGPUFragmentUniformData (m_ctx->main_cmd, 0, &c, sizeof (c));

      SDL_GPUTextureSamplerBinding b[2]{};
      SDL_zero (b);
      b[0].texture = m_hdr_scene.get ();
      b[0].sampler = m_linear_sampler.get ();
      b[1].texture = m_bloom_a.get ();
      b[1].sampler = m_linear_sampler.get ();

      SDL_BindGPUFragmentSamplers (pass, 0, b, 2);

      SDL_DrawGPUPrimitives (pass, 3, 1, 0, 0);
      SDL_EndGPURenderPass (pass);
    }
    SDL_PopGPUDebugGroup (m_ctx->main_cmd);
  }

  // Close the "Postprocess" debug group opened at the top of this
  // function.
  SDL_PopGPUDebugGroup (m_ctx->main_cmd);

  // Tracy frame image: read back the final composited m_present_tex
  // into a staging transfer buffer. Done as the very last GPU
  // command of the frame so the pixels cover everything drawn this
  // frame (postprocess + UI). The fence / wait / FrameImage call
  // happens in frame_image_submit() after end_cmd.
  frame_image_issue_copy ();
}

// ---------------------------------------------------------------------
// Tracy frame image capture
// ---------------------------------------------------------------------

void
render_window::frame_image_init (uint32_t target_w, uint32_t target_h)
{
  // The downsample target is fixed at construction; the staging
  // transfer buffer is allocated separately by frame_image_resize()
  // once the m_present_tex dimensions are known. The target size must
  // be divisible by 4 (Tracy FrameImage requirement); 320x180 is the
  // recommended thumbnail size from the Tracy manual.
  m_fi_dst_w = target_w;
  m_fi_dst_h = target_h;
  m_fi_dst_pitch = target_w * 4;

  // m_present_tex is in the platform's m_swapchain format. On every
  // desktop backend SDL3 GPU exposes B8G8R8A8 (see
  // SDL_GetGPUSwapchainTextureFormat). Mark the source as BGRA so
  // the downsample step can swap channels on the way out; an
  // R8G8B8A8 m_swapchain (rare on desktops) would set this false.
  m_fi_src_is_bgra = true;

  wsl::log::gfx ()->debug (
      "render_window: Tracy frame-image capture initialised "
      "({}x{} downsample target)",
      m_fi_dst_w, m_fi_dst_h);
}
void
render_window::frame_image_shutdown ()
{
  m_fi_transfer.reset ();
  m_fi_alloc_w = 0;
  m_fi_alloc_h = 0;
}

void
render_window::frame_image_resize (uint32_t src_w, uint32_t src_h)
{
  if (m_ctx->gpu_device == nullptr || src_w == 0 || src_h == 0) {
    return;
  }
  // Skip the (re)allocation if the staging buffer already covers
  // the requested source size. This keeps on_resize() cheap on
  // repeated calls (e.g. drag-resize) and avoids GPU buffer churn.
  if (m_fi_transfer && m_fi_alloc_w == src_w && m_fi_alloc_h == src_h) {
    return;
  }

  // Must be GPU-idle before releasing a transfer buffer that may
  // still be referenced by an in-flight command buffer. The caller
  // (on_resize) already does SDL_WaitForGPUIdle() above, so this
  // call is safe in that path. Standalone callers (tests) must do
  // the same.
  m_fi_transfer.reset ();

  // The staging buffer must hold the *full* source texture, not
  // the downscaled target. The downsample happens on the CPU after
  // the GPU fence signals; doing it on the GPU would require a
  // compute pipeline we don't need for anything else. Allocating
  // for src_w*src_h*4 (BGRA8) gives us a buffer large enough for
  // any m_swapchain size up to 4K (~33 MB).
  size_t const size = static_cast<size_t> (src_w) * src_h * 4;

  SDL_GPUTransferBufferCreateInfo info{};
  info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
  info.size = static_cast<uint32_t> (size);

  m_fi_transfer = gpu_transfer_buffer (m_ctx->gpu_device, info);
  if (!m_fi_transfer) {
    wsl::log::rsc ()->warn (
        "render_window: failed to allocate Tracy frame-image "
        "staging buffer ({}x{} = {}B): {}",
        src_w, src_h, size, SDL_GetError ());
    m_fi_alloc_w = 0;
    m_fi_alloc_h = 0;
    return;
  }
  m_fi_alloc_w = src_w;
  m_fi_alloc_h = src_h;

  wsl::log::gfx ()->debug (
      "render_window: Tracy frame-image staging buffer reallocated "
      "({}x{} src, {}B)",
      src_w, src_h, size);
}

void
render_window::frame_image_issue_copy ()
{
  if (!m_fi_transfer || m_ctx->main_cmd == nullptr) {
    return;
  }
  if (m_present_tex.texture_data == nullptr) {
    return;
  }
  if (m_present_tex.width == 0 || m_present_tex.height == 0) {
    return;
  }
  // Skip the GPU copy pass entirely when no profiler is attached.
  // Without this guard we'd still pay the GPU bandwidth and CPU
  // downsample cost on every frame.
  if (!TracyIsConnected) {
    return;
  }

  // Defensive resize: if the m_present_tex dimensions changed (e.g.
  // an external resize path that didn't go through on_resize) but
  // the staging buffer wasn't reallocated, do it now. This branch
  // is unreachable in the normal flow because on_resize() calls
  // frame_image_resize() — it exists to prevent a buffer overflow
  // if that contract is ever broken.
  if (m_fi_alloc_w != m_present_tex.width
      || m_fi_alloc_h != m_present_tex.height) {
    SDL_WaitForGPUIdle (m_ctx->gpu_device);
    frame_image_resize (m_present_tex.width, m_present_tex.height);
    if (!m_fi_transfer) {
      return;
    }
  }

  m_fi_src_w = m_present_tex.width;
  m_fi_src_h = m_present_tex.height;

  // Note: the full m_present_tex is copied (not yet downscaled). The
  // downscale to 320x180 happens on the CPU after the GPU fence
  // signals; doing it on the GPU would require a compute pipeline
  // we don't need for anything else.
  SDL_GPUTextureRegion src_region{};
  src_region.texture = m_present_tex.texture_data;
  src_region.mip_level = 0;
  src_region.layer = 0;
  src_region.x = 0;
  src_region.y = 0;
  src_region.z = 0;
  src_region.w = m_fi_src_w;
  src_region.h = m_fi_src_h;
  src_region.d = 1;

  SDL_GPUTextureTransferInfo dst_info{};
  dst_info.transfer_buffer = m_fi_transfer.get ();
  dst_info.offset = 0;
  // Pitch must match the row width of the destination texture
  // region; we use 4 bytes-per-pixel because the transfer buffer
  // holds BGRA8 / RGBA8 (no compressed formats here).
  dst_info.pixels_per_row = m_fi_src_w;
  dst_info.rows_per_layer = m_fi_src_h;

  SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass (m_ctx->main_cmd);
  if (copy == nullptr) {
    // Beginning a copy pass can fail if the command buffer is in a
    // bad state (e.g. the m_swapchain acquire failed earlier). Skip
    // silently — no Tracy image this frame.
    return;
  }
  SDL_DownloadFromGPUTexture (copy, &src_region, &dst_info);
  SDL_EndGPUCopyPass (copy);
}

void
render_window::frame_image_submit (SDL_GPUFence *fence)
{
  if (!m_fi_transfer) {
    return;
  }
  if (fence == nullptr) {
    return;
  }
  if (m_fi_src_w == 0 || m_fi_src_h == 0) {
    return;
  }
  // Sanity: if the staging buffer is smaller than the source we
  // recorded in issue_copy, something resized the texture out from
  // under us. Bail rather than overflow the buffer in the downsample
  // loop below.
  if (m_fi_alloc_w < m_fi_src_w || m_fi_alloc_h < m_fi_src_h) {
    return;
  }

  // Skip the entire readback path when no profiler is attached.
  // TracyIsConnected is a no-op returning `false` when TRACY_ENABLE
  // is not defined, so this also short-circuits cleanly in builds
  // that don't enable Tracy macros.
  if (!TracyIsConnected) {
    return;
  }

  // Block until the GPU has finished the download. Single-frame
  // latency; Tracy runs the per-frame image compression on a
  // background thread, so this doesn't stall the client.
  //
  // IMPORTANT: do NOT call SDL_ReleaseGPUFence here. The fence is
  // owned by the render_context slot that submitted the command
  // buffer; the slot recycles after kMaxFramesInFlight frames and
  // begin_frame() will wait + release the same fence then.
  // Releasing it here is a use-after-free / double-free crash.
  if (!SDL_WaitForGPUFences (m_ctx->gpu_device, true, &fence, 1)) {
    // Fence wait failed (driver error, device lost, etc.). Skip
    // this frame's image — better to lose one frame than to
    // unmap corrupt data and crash Tracy's background thread.
    wsl::log::gfx ()->warn (
        "render_window: SDL_WaitForGPUFences failed for Tracy "
        "frame-image readback: {}",
        SDL_GetError ());
    return;
  }

  // Map the transfer buffer, downsample, hand to Tracy, unmap.
  void *mapped = SDL_MapGPUTransferBuffer (m_ctx->gpu_device,
                                           m_fi_transfer.get (), false);
  if (mapped == nullptr) {
    wsl::log::gfx ()->warn (
        "render_window: SDL_MapGPUTransferBuffer failed for Tracy "
        "frame-image readback: {}",
        SDL_GetError ());
    return;
  }
  const uint8_t *src = static_cast<const uint8_t *> (mapped);

  // Box-filter downsample. Each dst pixel averages the src block
  // it covers. For integer src_w/src_dst_w ratios the block is
  // exact (every block has the same size); for arbitrary sizes
  // some blocks are 1px wider on the right / bottom edge.
  std::vector<uint8_t> downscaled (static_cast<size_t> (m_fi_dst_pitch)
                                   * m_fi_dst_h);
  const uint32_t src_w = m_fi_src_w;
  const uint32_t src_h = m_fi_src_h;
  const uint32_t dst_w = m_fi_dst_w;
  const uint32_t dst_h = m_fi_dst_h;
  for (uint32_t dy = 0; dy < dst_h; ++dy) {
    uint32_t const sy0 = (uint64_t)dy * src_h / dst_h;
    uint32_t const sy1
        = std::max (sy0 + 1, (uint32_t)((uint64_t)(dy + 1) * src_h / dst_h));
    for (uint32_t dx = 0; dx < dst_w; ++dx) {
      uint32_t const sx0 = (uint64_t)dx * src_w / dst_w;
      uint32_t const sx1
          = std::max (sx0 + 1, (uint32_t)((uint64_t)(dx + 1) * src_w / dst_w));
      uint64_t br = 0;
      uint64_t bg = 0;
      uint64_t bb = 0;
      uint32_t count = 0;
      for (uint32_t sy = sy0; sy < sy1; ++sy) {
        const uint8_t *row = src + ((size_t)sy * src_w * 4);
        for (uint32_t sx = sx0; sx < sx1; ++sx) {
          // SDL3 GPU m_swapchain format is B8G8R8A8 on desktop. Swap
          // R/B when reading so the output is RGBA.
          if (m_fi_src_is_bgra) {
            bb += row[(sx * 4) + 0];
            bg += row[(sx * 4) + 1];
            br += row[(sx * 4) + 2];
          } else {
            br += row[(sx * 4) + 0];
            bg += row[(sx * 4) + 1];
            bb += row[(sx * 4) + 2];
          }
          ++count;
        }
      }
      uint8_t *dst
          = downscaled.data () + ((size_t)dy * m_fi_dst_pitch) + (dx * 4);
      dst[0] = (uint8_t)(br / count);
      dst[1] = (uint8_t)(bg / count);
      dst[2] = (uint8_t)(bb / count);
      dst[3] = 0xFF; // alpha: force opaque
    }
  }

  // TracyFrameImage (via FrameImage) takes ownership of the pixel
  // buffer; the data must outlive the call, which is why the
  // downscaled vector lives in this scope and is not freed until
  // after the call returns. Tracy copies internally.
  FrameImage (downscaled.data (), static_cast<uint16_t> (dst_w),
              static_cast<uint16_t> (dst_h), /*offset=*/0,
              /*flip=*/false);

  SDL_UnmapGPUTransferBuffer (m_ctx->gpu_device, m_fi_transfer.get ());
}

} // namespace gfx

} // namespace wsl
