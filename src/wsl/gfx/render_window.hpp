#pragma once

#include "wsl/comp/camera.hpp"
#include "wsl/gfx/gpu_resources.hpp"
#include "wsl/gfx/mesh.hpp"
#include "wsl/gfx/render_context.hpp"
#include "wsl/gfx/subviewport_target.hpp"
#include "wsl/gfx/viewport.hpp"
#include "wsl/rsc/window_settings.hpp"

#ifndef IN_MODULE_INTERFACE
#include <SDL3/SDL.h>
#endif
#ifndef IN_MODULE_INTERFACE
#include <SDL3/SDL_gpu.h>
#endif
#ifndef IN_MODULE_INTERFACE
#include <entt/entt.hpp>
#endif
#ifndef IN_MODULE_INTERFACE
#include <vector>
#endif

namespace wsl
{

namespace rsc
{
class resource_manager;
}

namespace gfx
{

class render_window
{
public:
  /** Construct a render window. */
  //
  /**
   * :param try_disable_vsync_on_startup: When true (default), the
   *   constructor requests `IMMEDIATE` present mode so the GPU's
   *   present doesn't block on vblank. If the call fails, a spdlog
   *   error is logged and the swapchain stays in vsync-paced mode.
   *   Pass false to skip the attempt entirely (e.g. on drivers where
   *   changing the present mode corrupts the compute queue).
   */
  render_window (const char *name, int width, int height,
                 wsl::gfx::render_context *ctx,
                 wsl::rsc::resource_manager *res_mgr, bool headless = false,
                 bool try_disable_vsync_on_startup = true);
  ~render_window ();

  void get_size (uint32_t &width, uint32_t &height) const;
  void get_size (int &width, int &height) const;

  /**
   * Toggle the swapchain present mode. When ``enabled`` is true, requests
   * ``MAILBOX`` if the backend supports it (lower-latency vsync), otherwise
   * ``VSYNC``. When false, requests ``IMMEDIATE`` (no vblank sync).
   *
   * Returns true if the GPU swapchain was actually reconfigured.
   * Returns false (and logs a spdlog error) if the backend refuses the
   * request — the previous mode remains active in that case.
   *
   * .. note::
   *
   *   On the Vulkan backend with AMDVK, both ``IMMEDIATE`` and
   *   ``MAILBOX`` are reported as supported and accepted by
   *   ``SDL_SetGPUSwapchainParameters``, but the next compute dispatch
   *   can segfault on those drivers. If you hit that crash, call
   *   ``set_vsync(true)`` early in startup (or pass
   *   ``try_disable_vsync_on_startup = false`` to the ctor).
   */
  bool set_vsync (bool enabled);

  /**
   * Current vsync state: true means the swapchain is in VSYNC or
   * MAILBOX mode, false means IMMEDIATE.
   */
  [[nodiscard]] bool
  vsync () const
  {
    return m_vsync;
  }

  // SDL window handle
  [[nodiscard]] SDL_Window *
  handler () const
  {
    return m_handler;
  }

  // Swapchain texture
  [[nodiscard]] wsl::gfx::texture &
  swapchain ()
  {
    return m_swapchain;
  }
  [[nodiscard]] const wsl::gfx::texture &
  swapchain () const
  {
    return m_swapchain;
  }

  // Render context
  [[nodiscard]] wsl::gfx::render_context *
  ctx () const
  {
    return m_ctx;
  }

  // Depth texture
  [[nodiscard]] gpu_texture &
  depth_texture ()
  {
    return m_depth_texture;
  }
  [[nodiscard]] const gpu_texture &
  depth_texture () const
  {
    return m_depth_texture;
  }
  [[nodiscard]] SDL_GPUTextureFormat
  depth_format () const
  {
    return m_depth_format;
  }
  void
  depth_format (SDL_GPUTextureFormat fmt)
  {
    m_depth_format = fmt;
  }

  // MSAA HDR textures
  [[nodiscard]] gpu_texture &
  msaa_hdr_scene ()
  {
    return m_msaa_hdr_scene;
  }
  [[nodiscard]] const gpu_texture &
  msaa_hdr_scene () const
  {
    return m_msaa_hdr_scene;
  }
  [[nodiscard]] gpu_texture &
  msaa_hdr_bloom ()
  {
    return m_msaa_hdr_bloom;
  }
  [[nodiscard]] const gpu_texture &
  msaa_hdr_bloom () const
  {
    return m_msaa_hdr_bloom;
  }

  // Resolved HDR textures
  [[nodiscard]] gpu_texture &
  hdr_scene ()
  {
    return m_hdr_scene;
  }
  [[nodiscard]] const gpu_texture &
  hdr_scene () const
  {
    return m_hdr_scene;
  }
  [[nodiscard]] gpu_texture &
  hdr_bloom_src ()
  {
    return m_hdr_bloom_src;
  }
  [[nodiscard]] const gpu_texture &
  hdr_bloom_src () const
  {
    return m_hdr_bloom_src;
  }

  // Bloom ping-pong
  [[nodiscard]] gpu_texture &
  bloom_a ()
  {
    return m_bloom_a;
  }
  [[nodiscard]] const gpu_texture &
  bloom_a () const
  {
    return m_bloom_a;
  }
  [[nodiscard]] gpu_texture &
  bloom_b ()
  {
    return m_bloom_b;
  }
  [[nodiscard]] const gpu_texture &
  bloom_b () const
  {
    return m_bloom_b;
  }

  // Present texture
  [[nodiscard]] wsl::gfx::texture &
  present_tex ()
  {
    return m_present_tex;
  }
  [[nodiscard]] const wsl::gfx::texture &
  present_tex () const
  {
    return m_present_tex;
  }

  // Swapchain format
  [[nodiscard]] SDL_GPUTextureFormat
  swapchain_format () const
  {
    return m_swapchain_format;
  }
  void
  swapchain_format (SDL_GPUTextureFormat fmt)
  {
    m_swapchain_format = fmt;
  }

  // Pipelines
  [[nodiscard]] gpu_graphics_pipeline &
  pipe_downsample ()
  {
    return m_pipe_downsample;
  }
  [[nodiscard]] const gpu_graphics_pipeline &
  pipe_downsample () const
  {
    return m_pipe_downsample;
  }
  [[nodiscard]] gpu_graphics_pipeline &
  pipe_blur ()
  {
    return m_pipe_blur;
  }
  [[nodiscard]] const gpu_graphics_pipeline &
  pipe_blur () const
  {
    return m_pipe_blur;
  }
  [[nodiscard]] gpu_graphics_pipeline &
  pipe_composite ()
  {
    return m_pipe_composite;
  }
  [[nodiscard]] const gpu_graphics_pipeline &
  pipe_composite () const
  {
    return m_pipe_composite;
  }

  // Linear sampler
  [[nodiscard]] gpu_sampler &
  linear_sampler ()
  {
    return m_linear_sampler;
  }
  [[nodiscard]] const gpu_sampler &
  linear_sampler () const
  {
    return m_linear_sampler;
  }

  // Nearest sampler (per-viewport `nearest` scaling filter)
  [[nodiscard]] gpu_sampler &
  nearest_sampler ()
  {
    ensure_nearest_sampler ();
    return m_nearest_sampler;
  }

  /** 1x1 zero HDR texture (black) used as a stand-in bloom texture. */
  [[nodiscard]] SDL_GPUTexture *
  zero_bloom_texture () const
  {
    return m_zero_bloom.get ();
  }

  // Sample count
  [[nodiscard]] int
  current_sample_count () const
  {
    return m_current_sample_count;
  }
  void
  current_sample_count (int count)
  {
    m_current_sample_count = count;
  }

  // Present to swapchain
  [[nodiscard]] bool
  present_to_swapchain () const
  {
    return m_present_to_swapchain;
  }
  void
  present_to_swapchain (bool val)
  {
    m_present_to_swapchain = val;
  }

  // Scene clear color
  [[nodiscard]] SDL_FColor
  scene_clear_color () const
  {
    return m_scene_clear_color;
  }
  void
  scene_clear_color (SDL_FColor color)
  {
    m_scene_clear_color = color;
  }

  // Exposure
  [[nodiscard]] float
  exposure () const
  {
    return m_exposure;
  }
  void
  exposure (float val)
  {
    m_exposure = val;
  }

  // Bloom intensity
  [[nodiscard]] float
  bloom_intensity () const
  {
    return m_bloom_intensity;
  }
  void
  bloom_intensity (float val)
  {
    m_bloom_intensity = val;
  }

  void create_depth_texture (uint32_t width, uint32_t height, int sample_count);

  /**
   * Ensures the root viewport's render targets (MSAA/resolve HDR scene,
   * bloom source, depth, bloom ping-pong) match the requested size and
   * sample count. Lazy: reallocates only when the resolved root viewport
   * size or MSAA setting changes (D13). The LDR present texture always
   * matches the window size.
   */
  void ensure_root_targets (uint32_t width, uint32_t height, int sample_count);

  /** Current root viewport render-target size in pixels. */
  [[nodiscard]] uint32_t
  root_target_width () const
  {
    return m_root_target_w;
  }
  [[nodiscard]] uint32_t
  root_target_height () const
  {
    return m_root_target_h;
  }
  [[nodiscard]] int
  root_sample_count () const
  {
    return m_root_sample_count;
  }

  /**
   * Pixel rect inside the window where the root viewport content is
   * presented (letterbox rect). Empty means full window.
   */
  void
  present_rect (pixel_rect rect)
  {
    m_present_rect = rect;
  }
  [[nodiscard]] pixel_rect
  present_rect () const
  {
    return m_present_rect;
  }

  /** Applies window configuration from Project Settings (standalone, D7). */
  void apply_window_settings (const rsc::window_settings &settings);

  /**
   * Begin the root viewport scene pass.
   *
   * :param clear_color: Clear colour for the whole target.
   * :param clear_color_enabled: When false the colour target is loaded.
   * :param clear_depth: When false the depth target is loaded.
   * :param content_rect: Pixel rect inside the root target that the GPU
   *   viewport/scissor is set to (empty = full target).
   */
  void begin_3d_pass (SDL_FColor clear_color, bool clear_color_enabled,
                      bool clear_depth, pixel_rect content_rect = {},
                      const char *label = "Main 3D Pass") const;
  void end_3d_pass (bool run_postprocess = true);

  /** Begin a scene pass targeting a subviewport offscreen target. */
  void begin_subviewport_pass (const subviewport_target &target,
                               SDL_FColor clear_color, bool clear_color_enabled,
                               bool clear_depth, pixel_rect content_rect = {},
                               const char *label = "Subviewport Pass") const;
  /** End the subviewport render pass (resolves MSAA, no postprocess). */
  void end_subviewport_pass ();

  /**
   * Runs the per-viewport tonemap/bloom post-process for a 3D subviewport
   * whose `hdr` flag is false, writing display-referred output into the
   * target's LDR texture (D4/C2).
   */
  void postprocess_subviewport (subviewport_target &target, float exposure,
                                float bloom_intensity);

  void begin_ui_pass () const;
  void end_ui_pass () const;
  void new_swapchain ();
  void on_resize ();

  // -----------------------------------------------------------------
  // Tracy frame image capture
  // -----------------------------------------------------------------
  // The engine's present_tex (the tonemapped, bloom-augmented LDR
  // output) is the natural source for Tracy's per-frame screenshot
  // in the Frame view. The flow is:
  //
  //   1. frame_image_init() sets the downsample target size
  //      (e.g. 320x180, must be divisible by 4 for Tracy).
  //   2. frame_image_resize(src_w, src_h) (re)allocates the staging
  //      transfer buffer to be large enough to hold the *full*
  //      present_tex download. Called from on_resize().
  //   3. frame_image_issue_copy() records a copy pass from
  //      present_tex into that staging buffer.
  //   4. The main command buffer is submitted (existing path).
  //   5. frame_image_submit(fence) blocks on the submission fence,
  //      maps the staging buffer, downscales BGRA -> RGBA on the
  //      CPU and forwards the result to FrameImage().
  //
  // The downscaled image lives only for the duration of the
  // FrameImage() call; Tracy copies the pixels internally so the
  // staging buffer can be reused the next frame.
  void frame_image_init (uint32_t target_w, uint32_t target_h);
  void frame_image_shutdown ();
  void frame_image_resize (uint32_t src_w, uint32_t src_h);
  void frame_image_issue_copy ();
  void frame_image_submit (SDL_GPUFence *fence);

  void postprocess_hdr_bloom ();

  /**
   * Applies a resolved pixel rect to the active root-viewport render pass
   * (GPU viewport + scissor). The backend only applies rects/targets; the
   * engine-level resolve helper computes them (B1).
   */
  void apply_content_rect (const pixel_rect &rect) const;

  [[nodiscard]] wsl::rsc::resource_manager *
  resource_manager () const
  {
    return m_res_mgr;
  }

private:
  SDL_GPUSampler *ensure_linear_sampler ();
  void ensure_nearest_sampler ();
  void destroy_texture (gpu_texture &texture) const;

  /**
   * Tracked vsync state. Defaults to true (VSYNC) so the engine is
   * safe to use on any driver before the first `set_vsync` call.
   */
  bool m_vsync = true;
  /**
   * Tracked swapchain composition. SDL has no "get current
   * composition" query, so we remember what we last set (or the
   * default, SDR, if we have never set it).
   */
  SDL_GPUSwapchainComposition m_swapchain_composition
      = SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
  SDL_GPUGraphicsPipeline *
  create_fullscreen_pipe (const char *frag_shader_path,
                          SDL_GPUTextureFormat out_format,
                          int num_uniform_buffers, int num_samplers);
  gpu_graphics_pipeline create_composite_pipe ();
  gpu_graphics_pipeline create_downsample_pipe ();
  gpu_graphics_pipeline create_blur_pipe ();

  wsl::rsc::resource_manager *m_res_mgr = nullptr;

  // --- Private member variables (previously public) ---
  SDL_Window *m_handler = nullptr;
  wsl::gfx::texture m_swapchain;
  wsl::gfx::render_context *m_ctx = nullptr;

  gpu_texture m_depth_texture;
  SDL_GPUTextureFormat m_depth_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;

  gpu_texture m_msaa_hdr_scene;
  gpu_texture m_msaa_hdr_bloom;

  gpu_texture m_hdr_scene;
  gpu_texture m_hdr_bloom_src;

  gpu_texture m_bloom_a;
  gpu_texture m_bloom_b;

  wsl::gfx::texture m_present_tex;
  SDL_GPUTextureFormat m_swapchain_format
      = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;

  gpu_graphics_pipeline m_pipe_downsample;
  gpu_graphics_pipeline m_pipe_blur;
  gpu_graphics_pipeline m_pipe_composite;

  gpu_sampler m_linear_sampler;
  gpu_sampler m_nearest_sampler;

  // ---- Root viewport render-target state ----
  // The root scene targets are sized from the resolved root viewport (which
  // may differ from the window size when the root uses viewport scaling).
  // `ensure_root_targets` reallocates lazily when size or sample count
  // changes; `m_present_tex` always tracks the window size.
  uint32_t m_root_target_w = 0;
  uint32_t m_root_target_h = 0;
  int m_root_sample_count = 1;
  pixel_rect m_present_rect{};
  /** 1x1 zero bloom stand-in used when bloom is disabled. */
  gpu_texture m_zero_bloom;

  int m_current_sample_count = 1;
  bool m_present_to_swapchain = true;
  SDL_FColor m_scene_clear_color{ 0.1F, 0.1F, 0.1F, 1.0F };
  float m_exposure = 1.0F;
  float m_bloom_intensity = 1.0F;

  // Tracy frame image capture state. Owned by the render window;
  // the staging transfer buffer is (re)allocated in
  // frame_image_resize() and torn down in frame_image_shutdown().
  gpu_transfer_buffer m_fi_transfer;
  // Size the staging buffer was last allocated for. Tracked so a
  // window resize that changes the present_tex dimensions re-allocates
  // exactly once instead of thrashing per-frame.
  uint32_t m_fi_alloc_w = 0;
  uint32_t m_fi_alloc_h = 0;
  // Source dimensions from the last frame_image_issue_copy().
  uint32_t m_fi_src_w = 0;
  uint32_t m_fi_src_h = 0;
  // Downsample target (set once in frame_image_init, immutable).
  uint32_t m_fi_dst_w = 0;
  uint32_t m_fi_dst_h = 0;
  uint32_t m_fi_dst_pitch = 0;
  bool m_fi_src_is_bgra = true;
};

} // namespace gfx

} // namespace wsl
