#include "scene_renderer.hpp"

#include "gfx/lighting.hpp"
#include "gfx/material.hpp"
#include "gfx/mesh.hpp"
#include "gfx/model_3d.hpp"
#include "gfx/render_window.hpp"
#include "render_context.hpp"
#include "shader.hpp"
#include "wsl/rsc/resource_manager.hpp"
#include "wsl/log/log.hpp"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_stdinc.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_float3x3.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_float3.hpp>

#include <glm/geometric.hpp>
#include <glm/matrix.hpp>
#include <glm/trigonometric.hpp>
#include <cstring>
#include <glm/gtc/random.hpp>
#include <utility>
#include <vector>

#include <tracy/Tracy.hpp>

namespace wsl
{

namespace
{

void
configure_mesh_vertex_input (SDL_GPUGraphicsPipelineCreateInfo &pipe)
{
  static SDL_GPUVertexBufferDescription vertex_buffer{};
  static SDL_GPUVertexAttribute attributes[6]{};

  vertex_buffer = {};
  vertex_buffer.slot = 0;
  vertex_buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
  vertex_buffer.pitch = static_cast<Uint32> (sizeof (gfx::vertex));

  attributes[0] = {};
  attributes[0].location = 0;
  attributes[0].buffer_slot = 0;
  attributes[0].offset = offsetof (gfx::vertex, pos);
  attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;

  attributes[1] = {};
  attributes[1].location = 1;
  attributes[1].buffer_slot = 0;
  attributes[1].offset = offsetof (gfx::vertex, normal);
  attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;

  attributes[2] = {};
  attributes[2].location = 2;
  attributes[2].buffer_slot = 0;
  attributes[2].offset = offsetof (gfx::vertex, uv);
  attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;

  attributes[3] = {};
  attributes[3].location = 3;
  attributes[3].buffer_slot = 0;
  attributes[3].offset = offsetof (gfx::vertex, tangent);
  attributes[3].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;

  attributes[4] = {};
  attributes[4].location = 4;
  attributes[4].buffer_slot = 0;
  attributes[4].offset = offsetof (gfx::vertex, joints);
  attributes[4].format = SDL_GPU_VERTEXELEMENTFORMAT_UINT4;

  attributes[5] = {};
  attributes[5].location = 5;
  attributes[5].buffer_slot = 0;
  attributes[5].offset = offsetof (gfx::vertex, weights);
  attributes[5].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;

  pipe.vertex_input_state.num_vertex_buffers = 1;
  pipe.vertex_input_state.vertex_buffer_descriptions = &vertex_buffer;
  pipe.vertex_input_state.num_vertex_attributes = 6;
  pipe.vertex_input_state.vertex_attributes = attributes;
}

} // namespace

void
gfx::scene_renderer::create_default_texture ()
{
  // ---- sampler (shared) ----
  SDL_GPUSamplerCreateInfo sinfo{};
  sinfo.min_filter = SDL_GPU_FILTER_LINEAR;
  sinfo.mag_filter = SDL_GPU_FILTER_LINEAR;
  sinfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
  sinfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
  sinfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
  sinfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
  sinfo.max_lod = 1000.0F;

  m_default_sampler = SDL_CreateGPUSampler (m_ctx->gpu_device, &sinfo);

  // ---- per-purpose 1x1 defaults ----
  // NOTE: These are R8G8B8A8_UNORM (linear). That is correct for MR/Normal.
  // BaseColor and Emissive *textures* should be SRGB normally, but for
  // constant 1x1 fallback it's fine because (0 or 1) is the same in
  // sRGB/linear.
  m_default_basecolor_tex = create_1x1_texture (255, 255, 255, 255); // white
  m_default_emissive_tex = create_1x1_texture (0, 0, 0, 255);        // black
  m_default_normal_tex = create_1x1_texture (128, 128, 255, 255); // (0.5,0.5,1)
  m_default_mr_tex
      = create_1x1_texture (0, 255, 0, 255); // G=1 rough, B=0 metal

  m_default_cubemap_tex = create_1x1_cubemap (0, 0, 0, 255); // black
  m_default_brdf_lut_tex = create_1x1_texture (255, 128, 0, 255);

  if ((m_default_basecolor_tex == nullptr)
      || (m_default_emissive_tex == nullptr)
      || (m_default_normal_tex == nullptr) || (m_default_mr_tex == nullptr)
      || (m_default_sampler == nullptr) || (m_default_cubemap_tex == nullptr)
      || (m_default_brdf_lut_tex == nullptr)) {
    wsl::log::gfx ()->error ("Failed creating default textures/sampler: {}",
                             SDL_GetError ());
  }
}

auto
gfx::scene_renderer::extract_position (const glm::mat4 &m) -> glm::vec3
{
  return glm::vec3 (m[3]);
}

auto
gfx::scene_renderer::select_lod (gfx::node &n, float geometry_lod_bias) const
    -> gfx::mesh *
{
  if (n.mesh_lods.empty ()) {
    return nullptr;
  }

  if (n.mesh_lods.size () == 1) {
    return n.mesh_lods[0];
  }

  const glm::vec3 node_pos = extract_position (n.world_transform);
  const float dist = glm::length (m_camera_pos - node_pos);
  const float effective_dist = dist * (1.0F + geometry_lod_bias);

  if (effective_dist < 10.0F) {
    return n.mesh_lods[0];
  }

  if (effective_dist < 30.0F && n.mesh_lods.size () > 1) {
    return n.mesh_lods[1];
  }

  return n.mesh_lods.back ();
}

inline void
gfx::scene_renderer::render_node (gfx::node &n, const glm::mat4 &view_proj,
                                  float mip_lod_bias, float geometry_lod_bias,
                                  float visibility_range)
{
  if (!n.mesh_lods.empty ()) {
    if (visibility_range > 0.0F) {
      const glm::vec3 node_pos = extract_position (n.world_transform);
      const float dist = glm::length (m_camera_pos - node_pos);
      if (dist > visibility_range) {
        return;
      }
    }

    gfx::mesh const *m = select_lod (n, geometry_lod_bias);
    if (m != nullptr) {
      render_mesh (n.world_transform, view_proj, *m, mip_lod_bias);
    }
  }

  for (gfx::node &child : n.children) {
    render_node (child, view_proj, mip_lod_bias, geometry_lod_bias,
                 visibility_range);
  }
}

void
gfx::scene_renderer::render_scene (gfx::scene &s, const glm::mat4 &view_proj,
                                   float mip_lod_bias, float geometry_lod_bias,
                                   float visibility_range)
{
  for (gfx::node &root : s.roots) {
    render_node (root, view_proj, mip_lod_bias, geometry_lod_bias,
                 visibility_range);
  }
}

void
gfx::scene_renderer::update_node_world (gfx::node &n, const glm::mat4 &parent)
{
  n.world_transform = parent * n.local_transform;

  for (gfx::node &child : n.children) {
    update_node_world (child, n.world_transform);
  }
}

gfx::scene_renderer::scene_renderer (wsl::gfx::render_window &window,
                                     render_context *ctx,
                                     wsl::rsc::resource_manager *res_mgr)
    : renderer (window, ctx, res_mgr), m_clustered (ctx, res_mgr),
      m_pipeline_cache (ctx->gpu_device)
{

  create_pipeline ();
  create_skybox_pipeline ();
  create_default_texture ();
  create_unlit_pipeline ();
  create_preview_bg_pipeline ();
  create_shadow_resources (2048);
  create_ssao_pipeline ();
  create_outline_pipeline ();
  create_grid_pipeline ();

  int w;
  int h;
  window.get_size (w, h);
  create_ssao_resources (w, h);
}

void
gfx::scene_renderer::begin_frame (const view_state &view)
{
  ZoneScoped;
  m_active_view = view;
  m_camera_pos = view.world_position;

  if (m_active_view.valid) {
    update_directional_shadow_view ();
  } else {
    m_light_vp = glm::mat4 (1.0F);
  }

  // Inform the clustered lighting module of the screen size. Camera
  // parameters (view/proj/near/far) are refreshed inside
  // `run_clustered_lighting` because the lighting system has access to
  // the camera's near/far values.
  if (m_clustered.is_active ()) {
    uint32_t w = 0;
    uint32_t h = 0;
    m_window->get_size (w, h);
    m_clustered.on_resize (w, h);
  }
}

void
gfx::scene_renderer::end_frame ()
{
  ZoneScoped;
  m_active_view = view_state{};
  m_visible_draws.clear ();
  m_debug_overlay_queue.clear ();
}

auto
gfx::scene_renderer::has_active_frame () const -> bool
{
  return m_active_view.valid;
}

auto
gfx::scene_renderer::frame_view () const
    -> const gfx::scene_renderer::view_state &
{
  return m_active_view;
}

void
gfx::scene_renderer::set_visible_draws (std::vector<draw_command> draws)
{
  m_visible_draws = std::move (draws);
}

auto
gfx::scene_renderer::visible_draws () const
    -> const std::vector<gfx::scene_renderer::draw_command> &
{
  return m_visible_draws;
}

void
gfx::scene_renderer::clear_visible_draws ()
{
  m_visible_draws.clear ();
}

void
gfx::scene_renderer::bind_main_pipeline ()
{
  bind_pipeline ();
}

void
gfx::scene_renderer::draw_visible_models ()
{
  ZoneScoped;
  if (!m_active_view.valid) {
    return;
  }

  {
    ZoneScopedN ("scene_renderer::draw_visible_models::loop");
    TracyPlot ("draw_commands", (int64_t)m_visible_draws.size ());

    // Pass 1: opaque + mask primitives only (writes depth).
    m_alpha_pass = alpha_render_pass::opaque;
    for (const draw_command &draw : m_visible_draws) {
      if (draw.model == nullptr) {
        continue;
      }

      m_active_material_override = draw.material_override;
      m_active_palette = draw.palette;
      draw_model (*draw.model, draw.scene_index, draw.transform,
                  m_active_view.view_proj, draw.mip_lod_bias,
                  draw.geometry_lod_bias, draw.visibility_range);
      m_active_material_override = {};
      m_active_palette = nullptr;
    }

    // Pass 2: blend primitives only (no depth write, reads depth from pass 1).
    m_alpha_pass = alpha_render_pass::blend;
    for (const draw_command &draw : m_visible_draws) {
      if (draw.model == nullptr) {
        continue;
      }

      m_active_material_override = draw.material_override;
      m_active_palette = draw.palette;
      draw_model (*draw.model, draw.scene_index, draw.transform,
                  m_active_view.view_proj, draw.mip_lod_bias,
                  draw.geometry_lod_bias, draw.visibility_range);
      m_active_material_override = {};
      m_active_palette = nullptr;
    }

    m_alpha_pass = alpha_render_pass::all;
  }
}

void
gfx::scene_renderer::draw_visible_model_outlines ()
{
  if (!m_active_view.valid) {
    return;
  }

  for (const draw_command &draw : m_visible_draws) {
    if ((draw.model == nullptr) || !draw.draw_outline) {
      continue;
    }

    draw_model_outline (*draw.model, draw.scene_index, draw.transform,
                        m_active_view.view_proj, draw.palette);
  }
}

void
gfx::scene_renderer::build_ssao_for_visible_models ()
{
  ZoneScoped;
  if (!m_active_view.valid || !ssao_enabled) {
    return;
  }

  uint32_t width = 0;
  uint32_t height = 0;
  m_window->get_size (width, height);

  create_ssao_resources (width, height);
  begin_ssao_prepass (m_active_view.view, m_active_view.proj);

  for (const draw_command &draw : m_visible_draws) {
    if (draw.model == nullptr) {
      continue;
    }

    m_active_palette = draw.palette;
    draw_model_ssao (*draw.model, draw.scene_index, draw.transform,
                     m_active_view.view, m_active_view.proj);
    m_active_palette = nullptr;
  }

  end_ssao_prepass ();
  run_ssao_pass (m_active_view.proj);
  run_ssao_blur_pass ();
}

void
gfx::scene_renderer::draw_active_environment (const glm::quat &skybox_rotation)
{
  if ((m_active_env == nullptr) || !m_active_view.valid) {
    return;
  }

  bind_skybox_pipeline ();
  draw_skybox (*m_active_env, m_active_view.view, m_active_view.proj,
               skybox_rotation);
}

void
gfx::scene_renderer::upload_lighting (const lighting_ubo &lighting)
{
  ZoneScoped;
  SDL_PushGPUFragmentUniformData (m_ctx->main_cmd, 1, &lighting,
                                  sizeof (lighting));
}

void
gfx::scene_renderer::run_clustered_lighting (
    std::span<const gpu_point_light> point_lights, const glm::mat4 &view,
    float z_near, float z_far)
{
  ZoneScoped;
  if (!m_clustered.is_active ()) {
    return;
  }
  if (m_ctx == nullptr || m_ctx->main_cmd == nullptr) {
    return;
  }

  uint32_t w = 0;
  uint32_t h = 0;
  m_window->get_size (w, h);
  m_clustered.on_resize (w, h);
  m_clustered.on_camera_changed (view, m_active_view.proj, z_near, z_far);

  TracyPlot ("point_lights", (int64_t)point_lights.size ());

  // Convert the lighting system's `gpu_point_light` array (already in
  // world space) into the layout expected by the compute shader. The two
  // structs are layout-compatible, so this is a plain field copy.
  std::vector<gpu_cluster_light> cluster_lights;
  cluster_lights.reserve (point_lights.size ());
  for (const auto &src : point_lights) {
    gpu_cluster_light dst{};
    dst.pos_radius = src.pos_radius;
    dst.color_intensity = src.color_intensity;
    dst.shadow_info = src.shadow_info;
    cluster_lights.push_back (dst);
  }

  m_clustered.update (m_ctx->main_cmd, cluster_lights, view);
  TracyPlot ("clustered_lights",
             (int64_t)std::min<size_t> (cluster_lights.size (), 4096));
}

SDL_GPUGraphicsPipeline *
gfx::scene_renderer::create_debug_pipeline (SDL_GPUPrimitiveType primitive,
                                            bool depth_test, bool color_blend,
                                            bool keep_bloom)
{
  auto vert_id = m_res_mgr->register_shader (
      "engine://compiled_shaders/flat.vert.slang.spv");
  auto frag_id = m_res_mgr->register_shader (
      "engine://compiled_shaders/flat.frag.slang.spv");

  SDL_GPUShader *vert = gfx::shader::load_from_manager (
      m_ctx->gpu_device, m_res_mgr, vert_id, SDL_GPU_SHADERSTAGE_VERTEX, 1, 0);
  SDL_GPUShader *frag
      = gfx::shader::load_from_manager (m_ctx->gpu_device, m_res_mgr, frag_id,
                                        SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0);

  if ((vert == nullptr) || (frag == nullptr)) {
    if (vert != nullptr) {
      SDL_ReleaseGPUShader (m_ctx->gpu_device, vert);
    }
    if (frag != nullptr) {
      SDL_ReleaseGPUShader (m_ctx->gpu_device, frag);
    }
    return nullptr;
  }

  SDL_GPUGraphicsPipelineCreateInfo pi{};
  SDL_zero (pi);
  pi.vertex_shader = vert;
  pi.fragment_shader = frag;
  pi.primitive_type = primitive;

  // Debug geometry is never culled so slices and handles stay visible from
  // every angle.
  pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
  pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;

  pi.target_info.has_depth_stencil_target = true;
  pi.target_info.depth_stencil_format = m_window->depth_format ();
  pi.depth_stencil_state.enable_depth_test = depth_test;
  pi.depth_stencil_state.enable_depth_write = depth_test;
  if (depth_test) {
    pi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
  }

  // Color targets (match main renderer)
  SDL_GPUColorTargetDescription ctd[2]{};
  ctd[0].format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
  ctd[1].format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;

  if (color_blend) {
    ctd[0].blend_state.enable_blend = true;
    ctd[0].blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    ctd[0].blend_state.dst_color_blendfactor
        = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    ctd[0].blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    ctd[0].blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    ctd[0].blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
    ctd[0].blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
  }

  if (keep_bloom) {
    // The flat shader emits black for the bloom source; keeping the
    // destination prevents the overlay from punching bloom-free holes.
    ctd[1].blend_state.enable_blend = true;
    ctd[1].blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
    ctd[1].blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    ctd[1].blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    ctd[1].blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
    ctd[1].blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    ctd[1].blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
  }

  pi.target_info.num_color_targets = 2;
  pi.target_info.color_target_descriptions = ctd;

  pi.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_4;

  static SDL_GPUVertexBufferDescription vb{};
  vb.slot = 0;
  vb.pitch = (Uint32)sizeof (debug_vertex);
  vb.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
  pi.vertex_input_state.num_vertex_buffers = 1;
  pi.vertex_input_state.vertex_buffer_descriptions = &vb;

  static SDL_GPUVertexAttribute attrs[2];
  SDL_zero (attrs);
  attrs[0].location = 0;
  attrs[0].buffer_slot = 0;
  attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
  attrs[0].offset = offsetof (debug_vertex, pos);
  attrs[1].location = 1;
  attrs[1].buffer_slot = 0;
  attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
  attrs[1].offset = offsetof (debug_vertex, color);
  pi.vertex_input_state.num_vertex_attributes = 2;
  pi.vertex_input_state.vertex_attributes = attrs;

  SDL_GPUGraphicsPipeline *pipeline
      = SDL_CreateGPUGraphicsPipeline (m_ctx->gpu_device, &pi);

  SDL_ReleaseGPUShader (m_ctx->gpu_device, vert);
  SDL_ReleaseGPUShader (m_ctx->gpu_device, frag);
  return pipeline;
}

SDL_GPUGraphicsPipeline *
gfx::scene_renderer::create_ldr_overlay_pipeline ()
{
  auto vert_id = m_res_mgr->register_shader (
      "engine://compiled_shaders/flat.vert.slang.spv");
  auto frag_id = m_res_mgr->register_shader (
      "engine://compiled_shaders/flat.frag.slang.spv");

  SDL_GPUShader *vert = gfx::shader::load_from_manager (
      m_ctx->gpu_device, m_res_mgr, vert_id, SDL_GPU_SHADERSTAGE_VERTEX, 1, 0);
  SDL_GPUShader *frag
      = gfx::shader::load_from_manager (m_ctx->gpu_device, m_res_mgr, frag_id,
                                        SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0);

  if ((vert == nullptr) || (frag == nullptr)) {
    if (vert != nullptr) {
      SDL_ReleaseGPUShader (m_ctx->gpu_device, vert);
    }
    if (frag != nullptr) {
      SDL_ReleaseGPUShader (m_ctx->gpu_device, frag);
    }
    return nullptr;
  }

  SDL_GPUGraphicsPipelineCreateInfo pi{};
  SDL_zero (pi);
  pi.vertex_shader = vert;
  pi.fragment_shader = frag;
  pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

  pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
  pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;

  // The present texture is a single-sample LDR target, matching the
  // swapchain format, and has no depth attachment.
  SDL_GPUColorTargetDescription ctd[1]{};
  ctd[0].format = m_window->swapchain_format ();
  ctd[0].blend_state.enable_blend = true;
  ctd[0].blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
  ctd[0].blend_state.dst_color_blendfactor
      = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  ctd[0].blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
  // Keep the destination alpha: the present texture is displayed 1:1 and
  // must stay opaque.
  ctd[0].blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
  ctd[0].blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
  ctd[0].blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

  pi.target_info.num_color_targets = 1;
  pi.target_info.color_target_descriptions = ctd;
  pi.target_info.has_depth_stencil_target = false;

  pi.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;

  static SDL_GPUVertexBufferDescription vb{};
  vb.slot = 0;
  vb.pitch = (Uint32)sizeof (overlay_vertex);
  vb.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
  pi.vertex_input_state.num_vertex_buffers = 1;
  pi.vertex_input_state.vertex_buffer_descriptions = &vb;

  static SDL_GPUVertexAttribute attrs[2];
  SDL_zero (attrs);
  attrs[0].location = 0;
  attrs[0].buffer_slot = 0;
  attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
  attrs[0].offset = offsetof (overlay_vertex, position);
  attrs[1].location = 1;
  attrs[1].buffer_slot = 0;
  attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
  attrs[1].offset = offsetof (overlay_vertex, color);
  pi.vertex_input_state.num_vertex_attributes = 2;
  pi.vertex_input_state.vertex_attributes = attrs;

  SDL_GPUGraphicsPipeline *pipeline
      = SDL_CreateGPUGraphicsPipeline (m_ctx->gpu_device, &pi);

  SDL_ReleaseGPUShader (m_ctx->gpu_device, vert);
  SDL_ReleaseGPUShader (m_ctx->gpu_device, frag);
  return pipeline;
}

void
gfx::scene_renderer::ensure_debug_lines_pipeline ()
{
  if (m_pipeline_debug_lines == nullptr) {
    m_pipeline_debug_lines = create_debug_pipeline (
        SDL_GPU_PRIMITIVETYPE_LINELIST, true, false, false);
  }
}

void
gfx::scene_renderer::ensure_debug_overlay_pipeline (overlay_blending blending)
{
  if (blending == overlay_blending::write) {
    if (m_pipeline_debug_overlay_write == nullptr) {
      m_pipeline_debug_overlay_write = create_debug_pipeline (
          SDL_GPU_PRIMITIVETYPE_TRIANGLELIST, false, false, true);
    }
    return;
  }

  if (m_pipeline_debug_overlay == nullptr) {
    m_pipeline_debug_overlay = create_debug_pipeline (
        SDL_GPU_PRIMITIVETYPE_TRIANGLELIST, false, true, true);
  }
}

void
gfx::scene_renderer::ensure_ldr_overlay_pipeline ()
{
  if (m_pipeline_debug_overlay_ldr == nullptr) {
    m_pipeline_debug_overlay_ldr = create_ldr_overlay_pipeline ();
  }
}

void
gfx::scene_renderer::destroy_debug_pipelines ()
{
  if (m_pipeline_debug_lines != nullptr) {
    SDL_ReleaseGPUGraphicsPipeline (m_ctx->gpu_device, m_pipeline_debug_lines);
    m_pipeline_debug_lines = nullptr;
  }
  if (m_pipeline_debug_overlay != nullptr) {
    SDL_ReleaseGPUGraphicsPipeline (m_ctx->gpu_device,
                                    m_pipeline_debug_overlay);
    m_pipeline_debug_overlay = nullptr;
  }
  if (m_pipeline_debug_overlay_write != nullptr) {
    SDL_ReleaseGPUGraphicsPipeline (m_ctx->gpu_device,
                                    m_pipeline_debug_overlay_write);
    m_pipeline_debug_overlay_write = nullptr;
  }
  if (m_pipeline_debug_overlay_ldr != nullptr) {
    SDL_ReleaseGPUGraphicsPipeline (m_ctx->gpu_device,
                                    m_pipeline_debug_overlay_ldr);
    m_pipeline_debug_overlay_ldr = nullptr;
  }
}

SDL_GPUBuffer *
gfx::scene_renderer::upload_debug_vertices (const void *data, size_t size)
{
  SDL_GPUTransferBufferCreateInfo tinfo{};
  SDL_zero (tinfo);
  tinfo.size = (Uint64)size;
  tinfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  SDL_GPUTransferBuffer *upload
      = SDL_CreateGPUTransferBuffer (m_ctx->gpu_device, &tinfo);
  if (upload == nullptr) {
    return nullptr;
  }

  void *mapped = SDL_MapGPUTransferBuffer (m_ctx->gpu_device, upload, false);
  if (mapped != nullptr) {
    memcpy (mapped, data, size);
    SDL_UnmapGPUTransferBuffer (m_ctx->gpu_device, upload);
  }

  SDL_GPUBufferCreateInfo bi{};
  SDL_zero (bi);
  bi.size = (Uint64)size;
  bi.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
  SDL_GPUBuffer *buffer = SDL_CreateGPUBuffer (m_ctx->gpu_device, &bi);

  if (buffer != nullptr) {
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer (m_ctx->gpu_device);
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass (cmd);

    SDL_GPUTransferBufferLocation const src{ .transfer_buffer = upload,
                                             .offset = 0 };
    SDL_GPUBufferRegion const region{ .buffer = buffer,
                                      .offset = 0,
                                      .size = static_cast<Uint32> (size) };
    SDL_UploadToGPUBuffer (cp, &src, &region, true);

    SDL_EndGPUCopyPass (cp);
    SDL_SubmitGPUCommandBuffer (cmd);
  }

  SDL_ReleaseGPUTransferBuffer (m_ctx->gpu_device, upload);
  return buffer;
}

void
gfx::scene_renderer::draw_debug_lines (const std::vector<debug_vertex> &verts,
                                       const glm::mat4 &vp)
{
  if (verts.empty () || (m_ctx->main_pass == nullptr)) {
    return;
  }

  ensure_debug_lines_pipeline ();
  if (m_pipeline_debug_lines == nullptr) {
    return;
  }

  const size_t bytes = verts.size () * sizeof (debug_vertex);
  SDL_GPUBuffer *buffer = upload_debug_vertices (verts.data (), bytes);
  if (buffer == nullptr) {
    return;
  }

  // Push VP uniform
  SDL_PushGPUVertexUniformData (m_ctx->main_cmd, 0, &vp, sizeof (glm::mat4));

  // Bind and draw
  SDL_GPUBufferBinding binding{};
  binding.buffer = buffer;
  binding.offset = 0;
  SDL_BindGPUVertexBuffers (m_ctx->main_pass, 0, &binding, 1);
  SDL_BindGPUGraphicsPipeline (m_ctx->main_pass, m_pipeline_debug_lines);
  SDL_DrawGPUPrimitives (m_ctx->main_pass, (Uint32)verts.size (), 1, 0, 0);

  SDL_ReleaseGPUBuffer (m_ctx->gpu_device, buffer);
}

void
gfx::scene_renderer::draw_debug_overlay (
    const std::vector<overlay_vertex> &verts, overlay_blending blending)
{
  if (verts.empty () || (m_ctx->main_pass == nullptr)) {
    return;
  }

  ensure_debug_overlay_pipeline (blending);
  SDL_GPUGraphicsPipeline *pipeline = (blending == overlay_blending::write)
                                          ? m_pipeline_debug_overlay_write
                                          : m_pipeline_debug_overlay;
  if (pipeline == nullptr) {
    return;
  }

  const size_t bytes = verts.size () * sizeof (overlay_vertex);
  SDL_GPUBuffer *buffer = upload_debug_vertices (verts.data (), bytes);
  if (buffer == nullptr) {
    return;
  }

  // Overlay positions already are normalized device coordinates, but the
  // flat shader still expects the view-projection uniform in vertex slot 0.
  const glm::mat4 identity (1.0F);
  SDL_PushGPUVertexUniformData (m_ctx->main_cmd, 0, &identity,
                                sizeof (glm::mat4));

  SDL_GPUBufferBinding binding{};
  binding.buffer = buffer;
  binding.offset = 0;
  SDL_BindGPUVertexBuffers (m_ctx->main_pass, 0, &binding, 1);
  SDL_BindGPUGraphicsPipeline (m_ctx->main_pass, pipeline);
  SDL_DrawGPUPrimitives (m_ctx->main_pass, (Uint32)verts.size (), 1, 0, 0);

  SDL_ReleaseGPUBuffer (m_ctx->gpu_device, buffer);
}

void
gfx::scene_renderer::queue_debug_overlay (std::vector<overlay_vertex> verts)
{
  m_debug_overlay_queue = std::move (verts);
}

void
gfx::scene_renderer::flush_debug_overlay ()
{
  const gfx::texture &present = m_window->present_tex ();
  if (m_debug_overlay_queue.empty () || (m_ctx->main_cmd == nullptr)
      || (present.texture_data == nullptr) || (present.width == 0)
      || (present.height == 0)) {
    m_debug_overlay_queue.clear ();
    return;
  }

  ensure_ldr_overlay_pipeline ();
  if (m_pipeline_debug_overlay_ldr == nullptr) {
    m_debug_overlay_queue.clear ();
    return;
  }

  const std::vector<overlay_vertex> &verts = m_debug_overlay_queue;
  SDL_GPUBuffer *buffer = upload_debug_vertices (
      verts.data (), verts.size () * sizeof (overlay_vertex));
  if (buffer == nullptr) {
    m_debug_overlay_queue.clear ();
    return;
  }

  // Composite into the tone-mapped present texture so overlay colors are
  // written to the final image unchanged by exposure, bloom, or gamma.
  SDL_GPUColorTargetInfo ct{};
  SDL_zero (ct);
  ct.texture = present.texture_data;
  ct.load_op = SDL_GPU_LOADOP_LOAD;
  ct.store_op = SDL_GPU_STOREOP_STORE;

  SDL_GPURenderPass *pass
      = SDL_BeginGPURenderPass (m_ctx->main_cmd, &ct, 1, nullptr);
  if (pass != nullptr) {
    SDL_GPUViewport viewport{};
    SDL_zero (viewport);
    viewport.w = static_cast<float> (present.width);
    viewport.h = static_cast<float> (present.height);
    viewport.max_depth = 1.0F;
    SDL_SetGPUViewport (pass, &viewport);

    SDL_Rect scissor{};
    SDL_zero (scissor);
    scissor.w = static_cast<int> (present.width);
    scissor.h = static_cast<int> (present.height);
    SDL_SetGPUScissor (pass, &scissor);

    // Overlay positions already are normalized device coordinates, but the
    // flat shader still expects the view-projection uniform in vertex slot 0.
    const glm::mat4 identity (1.0F);
    SDL_PushGPUVertexUniformData (m_ctx->main_cmd, 0, &identity,
                                  sizeof (glm::mat4));

    SDL_GPUBufferBinding binding{};
    binding.buffer = buffer;
    binding.offset = 0;
    SDL_BindGPUVertexBuffers (pass, 0, &binding, 1);
    SDL_BindGPUGraphicsPipeline (pass, m_pipeline_debug_overlay_ldr);
    SDL_DrawGPUPrimitives (pass, (Uint32)verts.size (), 1, 0, 0);

    SDL_EndGPURenderPass (pass);
  }

  SDL_ReleaseGPUBuffer (m_ctx->gpu_device, buffer);
  m_debug_overlay_queue.clear ();
}

void
gfx::scene_renderer::set_camera_position (const glm::vec3 &position)
{
  m_camera_pos = position;
}

const glm::vec3 &
gfx::scene_renderer::camera_position () const
{
  return m_camera_pos;
}

glm::vec2
gfx::scene_renderer::render_target_size () const
{
  uint32_t width = 0;
  uint32_t height = 0;
  m_window->get_size (width, height);
  return glm::vec2 (static_cast<float> (width), static_cast<float> (height));
}

void
gfx::scene_renderer::set_force_unlit (bool enabled)
{
  m_force_unlit = enabled;
}

bool
gfx::scene_renderer::is_force_unlit () const
{
  return m_force_unlit;
}

void
gfx::scene_renderer::set_shadow_direction (const glm::vec3 &direction)
{
  if (glm::dot (direction, direction) <= 0.0F) {
    return;
  }

  m_shadow_dir = glm::normalize (direction);
}

const glm::vec3 &
gfx::scene_renderer::shadow_direction () const
{
  return m_shadow_dir;
}

void
gfx::scene_renderer::set_directional_shadows_enabled (bool enabled)
{
  m_shadows_enabled = enabled;
}

bool
gfx::scene_renderer::directional_shadows_enabled () const
{
  return m_shadows_enabled;
}

void
gfx::scene_renderer::update_directional_shadow_view ()
{
  if (!m_active_view.valid || !m_shadows_enabled) {
    m_light_vp = glm::mat4 (1.0F);
    return;
  }

  m_light_vp = make_light_vp_from_camera (m_active_view.view,
                                          m_active_view.proj, m_shadow_dir);
}

const glm::mat4 &
gfx::scene_renderer::directional_shadow_view () const
{
  return m_light_vp;
}

float
gfx::scene_renderer::shadow_map_bias () const
{
  return m_shadow_bias;
}

void
gfx::scene_renderer::set_shadow_map_bias (float bias)
{
  m_shadow_bias = bias < 0.0F ? 0.0F : bias;
}

float
gfx::scene_renderer::shadow_map_strength () const
{
  return m_shadow_strength;
}

void
gfx::scene_renderer::set_shadow_map_strength (float strength)
{
  m_shadow_strength = strength < 0.0F ? 0.0F : strength;
}

void
gfx::scene_renderer::set_ibl_intensity (float intensity)
{
  m_ibl_intensity = intensity < 0.0F ? 0.0F : intensity;
}

uint32_t
gfx::scene_renderer::shadow_map_resolution () const
{
  return m_shadow_size;
}

std::array<shadow_map_2d, 4> &
gfx::scene_renderer::spot_shadow_maps ()
{
  return m_spot_shadows;
}

const std::array<shadow_map_2d, 4> &
gfx::scene_renderer::spot_shadow_maps () const
{
  return m_spot_shadows;
}

std::array<point_shadow_map, 2> &
gfx::scene_renderer::point_shadow_maps ()
{
  return m_point_shadows;
}

const std::array<point_shadow_map, 2> &
gfx::scene_renderer::point_shadow_maps () const
{
  return m_point_shadows;
}

void
gfx::scene_renderer::create_pipeline ()
{
  auto vert_id = m_res_mgr->register_shader (
      "engine://compiled_shaders/cube.vert.slang.spv");
  auto frag_id = m_res_mgr->register_shader (
      "engine://compiled_shaders/cube.frag.slang.spv");

  SDL_GPUShader *vert = gfx::shader::load_from_manager (
      m_ctx->gpu_device, m_res_mgr, vert_id, SDL_GPU_SHADERSTAGE_VERTEX, 1, 0);

  SDL_GPUShader *frag = gfx::shader::load_from_manager (
      m_ctx->gpu_device, m_res_mgr, frag_id, SDL_GPU_SHADERSTAGE_FRAGMENT,
      /*num_uniform_buffers=*/5, // Material, Lighting, IBL, Post, ClusterParams
      /*num_samplers=*/15,
      /*num_storage_buffers=*/2); // point light SSBO, cluster SSBO

  if ((vert == nullptr) || (frag == nullptr)) {
    if (vert != nullptr) {
      SDL_ReleaseGPUShader (m_ctx->gpu_device, vert);
    }
    if (frag != nullptr) {
      SDL_ReleaseGPUShader (m_ctx->gpu_device, frag);
    }
    return;
  }

  // Helper: create a pipeline with specified alpha mode and cull mode.
  //
  // `vs` defaults to the plain (non-skinned) vertex shader. The skinned
  // variants pass the skinning variant instead, so the two sets are built from
  // exactly the same state description and cannot drift apart. Deriving the
  // skinned set by hand-copying this function instead is what previously let
  // the vertex input layout and the 4x MSAA sample count silently go missing
  // from the skinned pipelines -- the mesh then vanished with no API error.
  //
  // `vs` is passed explicitly rather than defaulted: a lambda default argument
  // may not reference a captured local.
  auto make_pipeline = [&] (alpha_mode a_mode, SDL_GPUCullMode cull,
                            SDL_GPUShader *vs) -> SDL_GPUGraphicsPipeline * {
    SDL_GPUGraphicsPipelineCreateInfo pipe{};
    SDL_zero (pipe);

    pipe.vertex_shader = vs;
    pipe.fragment_shader = frag;
    pipe.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipe.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    pipe.depth_stencil_state.enable_depth_test = true;
    pipe.depth_stencil_state.enable_stencil_test = false;

    pipe.target_info.has_depth_stencil_target = true;
    pipe.target_info.depth_stencil_format = m_window->depth_format ();

    SDL_GPUColorTargetDescription ctd[2]{};
    ctd[0].format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    ctd[1].format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;

    switch (a_mode) {
    case alpha_mode::opaque:
      // No blending, depth write ON.
      ctd[0].blend_state.enable_blend = false;
      ctd[1].blend_state.enable_blend = false;
      pipe.depth_stencil_state.enable_depth_write = true;
      break;

    case alpha_mode::mask:
      // Alpha-to-coverage: no traditional blend, coverage from alpha.
      // Depth write ON.
      ctd[0].blend_state.enable_blend = false;
      ctd[1].blend_state.enable_blend = false;
      pipe.depth_stencil_state.enable_depth_write = true;
      pipe.multisample_state.enable_alpha_to_coverage = true;
      break;

    case alpha_mode::blend:
      // Standard alpha blending, depth write OFF.
      ctd[0].blend_state.enable_blend = true;
      ctd[0].blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
      ctd[0].blend_state.dst_color_blendfactor
          = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
      ctd[0].blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
      ctd[0].blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
      ctd[0].blend_state.dst_alpha_blendfactor
          = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
      ctd[0].blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
      ctd[0].blend_state.color_write_mask
          = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G
            | SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;

      ctd[1].blend_state.enable_blend = true;
      ctd[1].blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
      ctd[1].blend_state.dst_color_blendfactor
          = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
      ctd[1].blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
      ctd[1].blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
      ctd[1].blend_state.dst_alpha_blendfactor
          = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
      ctd[1].blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
      ctd[1].blend_state.color_write_mask
          = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G
            | SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;

      pipe.depth_stencil_state.enable_depth_write = false;
      break;
    }

    for (int i = 0; i < 2; ++i) {
      ctd[i].blend_state.color_write_mask
          = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G
            | SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;
    }

    pipe.target_info.num_color_targets = 2;
    pipe.target_info.color_target_descriptions = ctd;

    pipe.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pipe.rasterizer_state.cull_mode = cull;

    pipe.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_4;
    pipe.multisample_state.sample_mask = 0;

    configure_mesh_vertex_input (pipe);

    return SDL_CreateGPUGraphicsPipeline (m_ctx->gpu_device, &pipe);
  };

  // Lit, single-sided: 3 alpha mode variants
  m_pipelines.opaque
      = make_pipeline (alpha_mode::opaque, SDL_GPU_CULLMODE_BACK, vert);
  m_pipelines.mask
      = make_pipeline (alpha_mode::mask, SDL_GPU_CULLMODE_BACK, vert);
  m_pipelines.blend
      = make_pipeline (alpha_mode::blend, SDL_GPU_CULLMODE_BACK, vert);

  // Lit, double-sided: 3 alpha mode variants
  m_pipelines_double_sided.opaque
      = make_pipeline (alpha_mode::opaque, SDL_GPU_CULLMODE_NONE, vert);
  m_pipelines_double_sided.mask
      = make_pipeline (alpha_mode::mask, SDL_GPU_CULLMODE_NONE, vert);
  m_pipelines_double_sided.blend
      = make_pipeline (alpha_mode::blend, SDL_GPU_CULLMODE_NONE, vert);

  // Legacy pointers point to opaque variants (for backwards compatibility).
  m_pipeline = m_pipelines.opaque;
  m_pipeline_double_sided = m_pipelines_double_sided.opaque;

  // ---- skinned variants ----
  // Identical to the lit pipelines above except for the vertex shader, so they
  // are built through the same helper. Only draws that actually carry a
  // palette bind these; everything else keeps the plain pipelines and pays
  // nothing for skinning. Built before the shaders below are released, since
  // `frag` is still referenced here.
  if (SDL_GPUShader *skinned_vert = create_skinned_vertex_shader (
          "engine://compiled_shaders/cube_skinned.vert.slang.spv");
      skinned_vert != nullptr) {
    m_pipelines_skinned.opaque = make_pipeline (
        alpha_mode::opaque, SDL_GPU_CULLMODE_BACK, skinned_vert);
    m_pipelines_skinned.mask
        = make_pipeline (alpha_mode::mask, SDL_GPU_CULLMODE_NONE, skinned_vert);
    m_pipelines_skinned.blend = make_pipeline (
        alpha_mode::blend, SDL_GPU_CULLMODE_NONE, skinned_vert);

    m_pipelines_skinned_double_sided.opaque = make_pipeline (
        alpha_mode::opaque, SDL_GPU_CULLMODE_NONE, skinned_vert);
    m_pipelines_skinned_double_sided.mask
        = make_pipeline (alpha_mode::mask, SDL_GPU_CULLMODE_NONE, skinned_vert);
    m_pipelines_skinned_double_sided.blend = make_pipeline (
        alpha_mode::blend, SDL_GPU_CULLMODE_NONE, skinned_vert);

    // A null pipeline is skipped silently by the draw loop, so an animated mesh
    // would just vanish with nothing in the log. Surface it once here instead.
    if (m_pipelines_skinned.opaque == nullptr
        || m_pipelines_skinned_double_sided.opaque == nullptr) {
      wsl::log::gfx ()->warn (
          "Skinned lit pipelines unavailable ({}); animated meshes will not "
          "render",
          SDL_GetError ());
    }

    SDL_ReleaseGPUShader (m_ctx->gpu_device, skinned_vert);
  }

  SDL_ReleaseGPUShader (m_ctx->gpu_device, vert);
  SDL_ReleaseGPUShader (m_ctx->gpu_device, frag);
}

void
gfx::scene_renderer::destroy_pipeline ()
{
  auto destroy_set = [this] (pipeline_set &ps) {
    if (ps.opaque != nullptr) {
      SDL_ReleaseGPUGraphicsPipeline (m_ctx->gpu_device, ps.opaque);
      ps.opaque = nullptr;
    }
    if (ps.mask != nullptr) {
      SDL_ReleaseGPUGraphicsPipeline (m_ctx->gpu_device, ps.mask);
      ps.mask = nullptr;
    }
    if (ps.blend != nullptr) {
      SDL_ReleaseGPUGraphicsPipeline (m_ctx->gpu_device, ps.blend);
      ps.blend = nullptr;
    }
  };

  destroy_set (m_pipelines);
  destroy_set (m_pipelines_double_sided);
  destroy_set (m_pipelines_skinned);
  destroy_set (m_pipelines_skinned_double_sided);

  m_pipeline = nullptr;
  m_pipeline_double_sided = nullptr;
}

gfx::scene_renderer::~scene_renderer ()
{
  destroy_grid_pipeline ();
  destroy_pipeline ();
  destroy_unlit_pipeline ();
  destroy_preview_bg_pipeline ();
  destroy_shadow_resources ();
  destroy_ssao_resources ();
  destroy_ssao_pipeline ();
  destroy_outline_pipeline ();
  destroy_ibl_pipelines ();
  destroy_debug_pipelines ();
  destroy_default_resources ();

  if (m_skybox_pipeline != nullptr) {
    SDL_ReleaseGPUGraphicsPipeline (m_ctx->gpu_device, m_skybox_pipeline);
    m_skybox_pipeline = nullptr;
  }

  if (m_equi_to_cube_pipe != nullptr) {
    SDL_ReleaseGPUGraphicsPipeline (m_ctx->gpu_device, m_equi_to_cube_pipe);
    m_equi_to_cube_pipe = nullptr;
  }

  if (m_procedural_skybox_pipe != nullptr) {
    SDL_ReleaseGPUGraphicsPipeline (m_ctx->gpu_device,
                                    m_procedural_skybox_pipe);
    m_procedural_skybox_pipe = nullptr;
  }

  if (m_vertex_transfer_buffer != nullptr) {
    SDL_ReleaseGPUTransferBuffer (m_ctx->gpu_device, m_vertex_transfer_buffer);
  }
  if (m_index_transfer_buffer != nullptr) {
    SDL_ReleaseGPUTransferBuffer (m_ctx->gpu_device, m_index_transfer_buffer);
  }
}

void
gfx::scene_renderer::bind_pipeline ()
{
  if ((m_pipeline == nullptr) || (m_ctx == nullptr)
      || (m_ctx->main_pass == nullptr)) {
    return;
  }

  SDL_BindGPUGraphicsPipeline (m_ctx->main_pass, m_pipeline);
}

void
gfx::scene_renderer::draw_model (gfx::model_3d &model, size_t scene_index,
                                 const glm::mat4 &model_matrix,
                                 const glm::mat4 &view_proj, float mip_lod_bias,
                                 float geometry_lod_bias,
                                 float visibility_range)
{
  if (scene_index >= model.scenes.size ()) {
    return;
  }

  model.ensure_gpu_buffers (m_ctx);
  model.bind (m_ctx->main_pass);

  gfx::scene &scene = model.scenes[scene_index];

  for (gfx::node &root : scene.roots) {
    update_node_world (root, model_matrix);
  }

  render_scene (scene, view_proj, mip_lod_bias, geometry_lod_bias,
                visibility_range);
}

SDL_GPUShader *
gfx::scene_renderer::create_skinned_vertex_shader (const char *path,
                                                   uint32_t num_uniform_buffers)
{
  auto id = m_res_mgr->register_shader (path);
  // Slot 0 is the per-draw matrices, slot 1 the joint palette; the outline
  // pass additionally keeps its own params cbuffer in between.
  SDL_GPUShader *shader = gfx::shader::load_from_manager (
      m_ctx->gpu_device, m_res_mgr, id, SDL_GPU_SHADERSTAGE_VERTEX,
      num_uniform_buffers, 0);
  if (shader == nullptr) {
    wsl::log::gfx ()->warn ("Skinned vertex shader unavailable ({}): {}", path,
                            SDL_GetError ());
  }
  return shader;
}

size_t
gfx::scene_renderer::pack_joint_palette (const joint_palette &palette,
                                         std::vector<uint8_t> &scratch)
{
  const size_t count
      = std::min (palette.matrices.size (), joint_palette::max_joints);

  // Whole block, always. The count leads the block and the array follows at the
  // std140-aligned offset 16, so there is no "used prefix" variant: the shader
  // indexes the array directly and a short upload would leave stale entries
  // behind for indices inside the count.
  scratch.resize (joint_palette_layout::total_bytes);

  // A rig with no joints still needs a count of 0 so the shader blends nothing
  // rather than reading uninitialised memory.
  const auto count_value = static_cast<uint32_t> (count);
  std::memcpy (scratch.data () + joint_palette_layout::count_offset,
               &count_value, sizeof (count_value));

  if (count > 0) {
    std::memcpy (scratch.data () + joint_palette_layout::matrix_offset,
                 palette.matrices.data (), count * sizeof (glm::mat4));
  }

  return scratch.size ();
}

void
gfx::scene_renderer::push_active_palette (SDL_GPUCommandBuffer *cmd,
                                          uint32_t slot)
{
  if (m_active_palette == nullptr) {
    return;
  }

  // Layout must match the JointPalette cbuffer exactly: the full 128-entry
  // mat4 array, then the uint count at joint_palette_layout::count_offset.
  const std::size_t bytes
      = pack_joint_palette (*m_active_palette, m_palette_scratch);

  SDL_PushGPUVertexUniformData (cmd, slot, m_palette_scratch.data (), bytes);
}

void
gfx::scene_renderer::render_mesh (const glm::mat4 &model,
                                  const glm::mat4 &view_proj, const mesh &m,
                                  float mip_lod_bias)
{
  ZoneScoped;

  struct alignas (16) matrices
  {
    glm::mat4 model;
    glm::mat4 viewproj;
    glm::mat4 normal; // padded & matches HLSL float4x4
    glm::mat4 view;
  };
  glm::mat3 n3 = glm::transpose (glm::inverse (glm::mat3 (model)));

  // pack into a mat4 (upper-left 3x3 used, rest ignored)
  glm::mat4 n4 (1.0F);
  n4[0] = glm::vec4 (n3[0], 0.0F);
  n4[1] = glm::vec4 (n3[1], 0.0F);
  n4[2] = glm::vec4 (n3[2], 0.0F);
  // Recover the view matrix from the cached view_proj if not supplied.
  glm::mat4 view = m_active_view.valid ? m_active_view.view : glm::mat4 (1.0F);
  matrices mat{ model, view_proj, n4, view };

  SDL_PushGPUVertexUniformData (m_ctx->main_cmd, 0, &mat, sizeof (matrices));

  // Joint palette lives in vertex uniform slot 1 for the main pass, matching
  // the JointPalette cbuffer in skinning.slang.
  push_active_palette (m_ctx->main_cmd, 1);

  // Push ClusterParams cbuffer (b4, space3 in Slang -> slot 4 here).
  if (m_clustered.is_active ()) {
    uint32_t sw = 0;
    uint32_t sh = 0;
    m_window->get_size (sw, sh);
    m_clustered.push_graphics_uniforms (
        m_ctx->main_cmd, glm::inverse (m_active_view.proj),
        glm::vec2 (static_cast<float> (sw), static_cast<float> (sh)));
  }

  for (const primitive &prim : m.primitives) {
    // When a global alpha pass is active, skip primitives that don't belong.
    if (m_alpha_pass == alpha_render_pass::opaque
        && prim.mat.mode == alpha_mode::blend) {
      continue;
    }
    if (m_alpha_pass == alpha_render_pass::blend
        && prim.mat.mode != alpha_mode::blend) {
      continue;
    }

    if (m_active_material_override.value != entt::null) {
      // Per-instance material override: render every primitive of this mesh
      // with the assigned material, ignoring the mesh's own material.
      gfx::material_instance override_inst;
      override_inst.asset_id = m_active_material_override;
      render_custom_primitive (prim, override_inst, mip_lod_bias);
      continue;
    }

    if (enable_custom_materials && prim.use_custom_material) {
      render_custom_primitive (prim, mip_lod_bias);
      continue;
    }

    SDL_GPUGraphicsPipeline *pipe = nullptr;

    // A draw that carries a palette must use a pipeline built from the
    // skinned vertex shader, because only that one declares the palette
    // cbuffer. Custom-material primitives take their own pipeline path above
    // and do not participate in the variant set.
    const bool skinned = m_active_palette != nullptr;

    if (m_force_unlit || prim.mat.unlit) {
      const pipeline_set &ps
          = !skinned ? (prim.mat.double_sided ? m_pipelines_unlit_double_sided
                                              : m_pipelines_unlit)
                     : (prim.mat.double_sided
                            ? m_pipelines_skinned_unlit_double_sided
                            : m_pipelines_skinned_unlit);
      switch (prim.mat.mode) {
      case alpha_mode::mask:
        pipe = ps.mask;
        break;
      case alpha_mode::blend:
        pipe = ps.blend;
        break;
      case alpha_mode::opaque:
      default:
        pipe = ps.opaque;
        break;
      }
    } else {
      const pipeline_set &ps
          = !skinned ? (prim.mat.double_sided ? m_pipelines_double_sided
                                              : m_pipelines)
                     : (prim.mat.double_sided ? m_pipelines_skinned_double_sided
                                              : m_pipelines_skinned);
      switch (prim.mat.mode) {
      case alpha_mode::mask:
        pipe = ps.mask;
        break;
      case alpha_mode::blend:
        pipe = ps.blend;
        break;
      case alpha_mode::opaque:
      default:
        pipe = ps.opaque;
        break;
      }
    }

    if ((pipe == nullptr) || (m_ctx->main_pass == nullptr)) {
      continue;
    }

    SDL_BindGPUGraphicsPipeline (m_ctx->main_pass, pipe);

    // Material uniforms
    gfx::gpu_material gpu_mat{};
    gpu_mat.base_color = prim.mat.base_color_factor;
    gpu_mat.metallic = prim.mat.metallic_factor;
    gpu_mat.roughness = prim.mat.roughness_factor;
    gpu_mat.alpha_cutoff = prim.mat.alpha_cutoff;
    gpu_mat.alpha_mode = static_cast<float> (prim.mat.mode);
    gpu_mat.emissive = prim.mat.emissive_factor;
    gpu_mat.mip_lod_bias = mip_lod_bias;

    SDL_PushGPUFragmentUniformData (m_ctx->main_cmd, 0, &gpu_mat,
                                    sizeof (gfx::gpu_material));

    const SDL_GPUTexture *irr = nullptr;
    const SDL_GPUTexture *pre = nullptr;
    const SDL_GPUTexture *lut = nullptr;
    SDL_GPUSampler *ibl_samp = nullptr;
    float max_mip = 0.0F;

    // "Ready" means: all 3 IBL textures exist.
    const bool ibl_ready = (m_active_env != nullptr)
                           && (m_active_env->ibl_irradiance != nullptr)
                           && (m_active_env->ibl_prefilter != nullptr)
                           && (m_active_env->ibl_brdf_lut != nullptr);

    if (ibl_ready) {
      irr = m_active_env->ibl_irradiance;
      pre = m_active_env->ibl_prefilter;
      lut = m_active_env->ibl_brdf_lut;
      ibl_samp = (m_active_env->ibl_sampler != nullptr)
                     ? m_active_env->ibl_sampler
                     : m_active_env->sampler;
      max_mip = m_active_env->prefilter_max_mip;
    } else {
      // If IBL isn't ready, FORCE shader to use fallback u_Ambient.
      // Still bind *something* non-null to keep backends happy:
      // use the environment cubemap as a harmless placeholder if present.
      if ((m_active_env != nullptr) && (m_active_env->texture != nullptr)) {
        irr = m_active_env->texture;
        pre = m_active_env->texture;
      }
      // LUT can be left null if intensity=0, but binding a valid texture
      // is nicer. If you don't have a BRDF fallback texture yet, keep
      // intensity = 0 and leave lut null.
      ibl_samp = (m_active_env != nullptr) && (m_active_env->sampler != nullptr)
                     ? m_active_env->sampler
                     : m_default_sampler;
      max_mip = 0.0F;
    }

    // push params (IMPORTANT: intensity is 0 if not ready)
    struct alignas (16) gpu_ibl_params
    {
      float intensity;
      float prefilter_max_mip;
      float pad0, pad1;
    };

    gpu_ibl_params ibl{};
    ibl.intensity = ibl_ready ? m_ibl_intensity : 0.0F;
    ibl.prefilter_max_mip = max_mip;
    SDL_PushGPUFragmentUniformData (m_ctx->main_cmd, 2, &ibl, sizeof (ibl));

    struct alignas (16) gpu_post_params
    {
      glm::vec4 bloom_and_ssao0; // x=bloomThreshold, y=bloomKnee,
                                 // z=bloomIntensity, w=ssaoEnabled
      glm::vec4 ssao_and_pad;    // x=ssaoIntensity, yzw=pad
    };

    gpu_post_params post{};
    post.bloom_and_ssao0
        = glm::vec4 (bloom_threshold, bloom_knee, bloom_intensity,
                     (ssao_enabled && (m_ssao_blur != nullptr)) ? 1.0F : 0.0F);
    post.ssao_and_pad = glm::vec4 (ssao_intensity, 0.0F, 0.0F, 0.0F);

    SDL_PushGPUFragmentUniformData (m_ctx->main_cmd, 3, &post, sizeof (post));

    // Texture bindings
    SDL_GPUTextureSamplerBinding texbind[15]{};

    // One sampler for the material textures (t0..t3)
    SDL_GPUSampler *samp
        = (prim.mat.sampler != nullptr) ? prim.mat.sampler : m_default_sampler;

    texbind[0].texture = (prim.mat.base_color_tex != nullptr)
                             ? prim.mat.base_color_tex
                             : m_default_basecolor_tex;
    texbind[0].sampler = samp;

    texbind[1].texture = (prim.mat.metallic_roughness_tex != nullptr)
                             ? prim.mat.metallic_roughness_tex
                             : m_default_mr_tex;
    texbind[1].sampler = samp;

    texbind[2].texture = (prim.mat.normal_tex != nullptr)
                             ? prim.mat.normal_tex
                             : m_default_normal_tex;
    texbind[2].sampler = samp;

    texbind[3].texture = (prim.mat.emissive_tex != nullptr)
                             ? prim.mat.emissive_tex
                             : m_default_emissive_tex;
    texbind[3].sampler = samp;

    // IBL samplers (t4..t6)
    SDL_GPUSampler *use_ibl_samp
        = (ibl_samp != nullptr) ? ibl_samp : m_default_sampler;

    texbind[4].texture
        = (irr != nullptr) ? (SDL_GPUTexture *)irr : m_default_cubemap_tex;
    texbind[4].sampler = use_ibl_samp;

    texbind[5].texture
        = (pre != nullptr) ? (SDL_GPUTexture *)pre : m_default_cubemap_tex;
    texbind[5].sampler = use_ibl_samp;

    texbind[6].texture
        = (lut != nullptr) ? (SDL_GPUTexture *)lut : m_default_brdf_lut_tex;
    texbind[6].sampler = use_ibl_samp;

    texbind[7].texture = (m_shadow_depth != nullptr) ? m_shadow_depth
                                                     : m_default_basecolor_tex;
    texbind[7].sampler
        = (m_shadow_sampler != nullptr) ? m_shadow_sampler : m_default_sampler;

    SDL_GPUSampler *shadow_samp
        = (m_shadow_sampler != nullptr) ? m_shadow_sampler : m_default_sampler;

    texbind[8].texture = (m_spot_shadows[0].depth != nullptr)
                             ? m_spot_shadows[0].depth
                             : m_shadow_depth;
    if (texbind[8].texture == nullptr) {
      texbind[8].texture = m_default_basecolor_tex;
    }
    texbind[8].sampler = shadow_samp;

    texbind[9].texture = (m_spot_shadows[1].depth != nullptr)
                             ? m_spot_shadows[1].depth
                             : m_shadow_depth;
    if (texbind[9].texture == nullptr) {
      texbind[9].texture = m_default_basecolor_tex;
    }
    texbind[9].sampler = shadow_samp;

    texbind[10].texture = (m_spot_shadows[2].depth != nullptr)
                              ? m_spot_shadows[2].depth
                              : m_shadow_depth;
    if (texbind[10].texture == nullptr) {
      texbind[10].texture = m_default_basecolor_tex;
    }
    texbind[10].sampler = shadow_samp;

    texbind[11].texture = (m_spot_shadows[3].depth != nullptr)
                              ? m_spot_shadows[3].depth
                              : m_shadow_depth;
    if (texbind[11].texture == nullptr) {
      texbind[11].texture = m_default_basecolor_tex;
    }
    texbind[11].sampler = shadow_samp;

    texbind[12].texture = (m_point_shadows[0].depth_cube != nullptr)
                              ? m_point_shadows[0].depth_cube
                              : m_default_cubemap_tex;
    texbind[12].sampler = shadow_samp;

    texbind[13].texture = (m_point_shadows[1].depth_cube != nullptr)
                              ? m_point_shadows[1].depth_cube
                              : m_default_cubemap_tex;
    texbind[13].sampler = shadow_samp;

    texbind[14].texture
        = (m_ssao_blur != nullptr) ? m_ssao_blur : m_default_basecolor_tex;
    texbind[14].sampler = (m_ssao_linear_sampler != nullptr)
                              ? m_ssao_linear_sampler
                              : m_default_sampler;

    SDL_BindGPUFragmentSamplers (m_ctx->main_pass, 0, texbind, 15);

    // Bind the clustered lighting SSBOs (light + cluster buffers).
    m_clustered.bind_for_graphics (m_ctx->main_pass);

    SDL_DrawGPUIndexedPrimitives (m_ctx->main_pass,
                                  static_cast<Uint32> (prim.indices.size ()), 1,
                                  prim.first_index, 0, 0);
  }
}

void
gfx::scene_renderer::render_custom_primitive (const primitive &prim,
                                              float mip_lod_bias)
{
  render_custom_primitive (prim, prim.custom_mat, mip_lod_bias);
}

void
gfx::scene_renderer::render_custom_primitive (
    const primitive &prim, const gfx::material_instance &mat_inst,
    float mip_lod_bias)
{
  if ((m_ctx == nullptr) || (m_ctx->main_pass == nullptr)
      || (m_res_mgr == nullptr)) {
    return;
  }

  auto mat_asset = m_res_mgr->get (mat_inst.asset_id);
  if (!mat_asset) {
    wsl::log::gfx ()->warn ("Custom material asset not found");
    return;
  }

  auto prog = m_res_mgr->get (mat_asset->shader_program);
  if (!prog || prog->fragment_bytecode.empty ()) {
    wsl::log::gfx ()->warn ("Custom shader program not found or incomplete");
    return;
  }

  // Load the vertex shader variant specified by the material asset.
  auto vert_id = m_res_mgr->register_shader (mat_asset->vertex_shader_path);
  SDL_GPUShader *vert = gfx::shader::load_from_manager (
      m_ctx->gpu_device, m_res_mgr, vert_id, SDL_GPU_SHADERSTAGE_VERTEX, 1, 0);

  if (vert == nullptr) {
    return;
  }

  // Load/create fragment shader from runtime bytecode. SDL_GPU requires the
  // pipeline's sampler count to match the shader's actual sampler bindings.
  // The reflection count is authoritative: a texture-less graph declares no
  // samplers (an unused `u_Samplers[1]` is stripped from the SPIR-V), so the
  // count can legitimately be 0.
  const uint32_t num_samplers = prog->fragment_reflection.num_samplers ();
  SDL_GPUShader *frag = gfx::shader::create_from_bytecode (
      m_ctx->gpu_device, prog->fragment_bytecode.data (),
      prog->fragment_bytecode.size (), SDL_GPU_SHADERSTAGE_FRAGMENT,
      prog->fragment_reflection.num_uniform_buffers (), num_samplers,
      prog->fragment_reflection.num_storage_buffers ());
  if (frag == nullptr) {
    SDL_ReleaseGPUShader (m_ctx->gpu_device, vert);
    return;
  }

  // Build pipeline description matching the main pass targets
  SDL_GPUGraphicsPipelineCreateInfo pipe{};
  SDL_zero (pipe);
  pipe.vertex_shader = vert;
  pipe.fragment_shader = frag;
  pipe.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  pipe.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
  pipe.depth_stencil_state.enable_depth_test = true;
  pipe.depth_stencil_state.enable_depth_write = true;
  pipe.depth_stencil_state.enable_stencil_test = false;

  pipe.target_info.has_depth_stencil_target = true;
  pipe.target_info.depth_stencil_format = m_window->depth_format ();

  SDL_GPUColorTargetDescription ctd[2]{};
  ctd[0].format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
  ctd[1].format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
  for (int i = 0; i < 2; ++i) {
    ctd[i].blend_state.enable_blend = true;
    ctd[i].blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    ctd[i].blend_state.dst_color_blendfactor
        = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    ctd[i].blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    ctd[i].blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    ctd[i].blend_state.dst_alpha_blendfactor
        = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    ctd[i].blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    ctd[i].blend_state.color_write_mask
        = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G
          | SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;
  }
  pipe.target_info.num_color_targets = 2;
  pipe.target_info.color_target_descriptions = ctd;

  pipe.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
  pipe.rasterizer_state.cull_mode
      = mat_asset->double_sided ? SDL_GPU_CULLMODE_NONE : SDL_GPU_CULLMODE_BACK;

  pipe.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_4;
  pipe.multisample_state.sample_mask = 0;
  pipe.multisample_state.enable_mask = false;

  configure_mesh_vertex_input (pipe);

  gfx::pipeline_key key{};
  key.shader_program_hash
      = std::hash<entt::id_type>{}(mat_asset->shader_program.value);
  // The custom-material path always builds the same 6-attribute interleaved
  // mesh layout and renders into the main window target, so both dimensions
  // are constant for every key produced here.
  key.vertex_layout_hash = 0;
  key.render_target_hash = 0;
  key.flags = mat_asset->double_sided ? 1 : 0;

  SDL_GPUGraphicsPipeline *gfx_pipe = m_pipeline_cache.acquire (key, pipe);

  SDL_ReleaseGPUShader (m_ctx->gpu_device, vert);
  SDL_ReleaseGPUShader (m_ctx->gpu_device, frag);

  if (gfx_pipe == nullptr) {
    return;
  }

  SDL_BindGPUGraphicsPipeline (m_ctx->main_pass, gfx_pipe);

  gfx::material_instance effective_inst = mat_inst;
  auto has_param = [&] (const std::string &name) {
    return effective_inst.overrides.find (name)
               != effective_inst.overrides.end ()
           || mat_asset->default_parameters.find (name)
                  != mat_asset->default_parameters.end ();
  };

  if (!has_param ("u_BaseColorFactor")) {
    effective_inst.overrides["u_BaseColorFactor"] = gfx::material_parameter (
        "u_BaseColorFactor", prim.mat.base_color_factor);
  }
  if (!has_param ("u_MetallicFactor")) {
    effective_inst.overrides["u_MetallicFactor"] = gfx::material_parameter (
        "u_MetallicFactor", prim.mat.metallic_factor);
  }
  if (!has_param ("u_RoughnessFactor")) {
    effective_inst.overrides["u_RoughnessFactor"] = gfx::material_parameter (
        "u_RoughnessFactor", prim.mat.roughness_factor);
  }
  if (!has_param ("u_EmissiveFactor")) {
    effective_inst.overrides["u_EmissiveFactor"] = gfx::material_parameter (
        "u_EmissiveFactor", prim.mat.emissive_factor);
  }
  if (!has_param ("u_MipLodBias")) {
    effective_inst.overrides["u_MipLodBias"]
        = gfx::material_parameter ("u_MipLodBias", mip_lod_bias);
  }

  // Build and push uniform blob (Material cbuffer, slot 0)
  auto blob = effective_inst.build_uniform_blob (prog->fragment_reflection,
                                                 *mat_asset);
  if (!blob.empty ()) {
    SDL_PushGPUFragmentUniformData (m_ctx->main_cmd, 0, blob.data (),
                                    static_cast<Uint32> (blob.size ()));
  }

  // The shader-graph material uses the engine's full PBR (via pbr_common), so
  // we must bind the same lighting/IBL/Post/Cluster resources the standard
  // cube.frag material receives. Lighting (slot 1) is already pushed globally
  // each frame by the lighting system.

  // ---- IBL cbuffer (slot 2) ----
  const SDL_GPUTexture *irr = nullptr;
  const SDL_GPUTexture *pre = nullptr;
  const SDL_GPUTexture *lut = nullptr;
  SDL_GPUSampler *ibl_samp = nullptr;
  float max_mip = 0.0F;

  const bool ibl_ready = (m_active_env != nullptr)
                         && (m_active_env->ibl_irradiance != nullptr)
                         && (m_active_env->ibl_prefilter != nullptr)
                         && (m_active_env->ibl_brdf_lut != nullptr);

  if (ibl_ready) {
    irr = m_active_env->ibl_irradiance;
    pre = m_active_env->ibl_prefilter;
    lut = m_active_env->ibl_brdf_lut;
    ibl_samp = (m_active_env->ibl_sampler != nullptr)
                   ? m_active_env->ibl_sampler
                   : m_default_sampler;
    max_mip = m_active_env->prefilter_max_mip;
  } else {
    if ((m_active_env != nullptr) && (m_active_env->texture != nullptr)) {
      irr = m_active_env->texture;
      pre = m_active_env->texture;
    }
    ibl_samp = (m_active_env != nullptr) && (m_active_env->sampler != nullptr)
                   ? m_active_env->sampler
                   : m_default_sampler;
    max_mip = 0.0F;
  }

  struct alignas (16) gpu_ibl_params
  {
    float intensity;
    float prefilter_max_mip;
    float pad0, pad1;
  };
  gpu_ibl_params ibl{};
  ibl.intensity = ibl_ready ? m_ibl_intensity : 0.0F;
  ibl.prefilter_max_mip = max_mip;
  SDL_PushGPUFragmentUniformData (m_ctx->main_cmd, 2, &ibl, sizeof (ibl));

  // ---- Post cbuffer (slot 3) ----
  struct alignas (16) gpu_post_params
  {
    glm::vec4 bloom_and_ssao0;
    glm::vec4 ssao_and_pad;
  };
  gpu_post_params post{};
  post.bloom_and_ssao0
      = glm::vec4 (bloom_threshold, bloom_knee, bloom_intensity,
                   (ssao_enabled && (m_ssao_blur != nullptr)) ? 1.0F : 0.0F);
  post.ssao_and_pad = glm::vec4 (ssao_intensity, 0.0F, 0.0F, 0.0F);
  SDL_PushGPUFragmentUniformData (m_ctx->main_cmd, 3, &post, sizeof (post));

  // ---- ClusterParams cbuffer (slot 4) ----
  if (m_clustered.is_active ()) {
    uint32_t sw = 0;
    uint32_t sh = 0;
    m_window->get_size (sw, sh);
    m_clustered.push_graphics_uniforms (
        m_ctx->main_cmd, glm::inverse (m_active_view.proj),
        glm::vec2 (static_cast<float> (sw), static_cast<float> (sh)));
  }

  // ---- Texture/sampler bindings (15 slots, fixed PBR layout) ----
  // t0..t3 hold any graph texture-sample nodes; t4..t14 are the PBR-internal
  // IBL / shadow / SSAO textures. Slots without a graph texture fall back to
  // a default so the descriptor set stays fully bound.
  const auto &tex_refl = prog->fragment_reflection.textures;
  SDL_GPUTextureSamplerBinding texbind[15]{};

  SDL_GPUSampler *samp
      = (m_default_sampler != nullptr) ? m_default_sampler : nullptr;
  SDL_GPUSampler *prim_samp
      = (prim.mat.sampler != nullptr) ? prim.mat.sampler : samp;
  auto has_name = [] (const std::string &actual, const char *expected) {
    return actual == expected || actual.find (expected) != std::string::npos;
  };

  static uint32_t s_custom_tex_log_budget = 64;
  SDL_GPUSampler *use_ibl_samp
      = (ibl_samp != nullptr) ? ibl_samp : m_default_sampler;
  SDL_GPUSampler *shadow_samp
      = (m_shadow_sampler != nullptr) ? m_shadow_sampler : m_default_sampler;

  for (int slot = 0; slot < 15; ++slot) {
    texbind[slot].texture = m_default_basecolor_tex;
    texbind[slot].sampler = m_default_sampler;
  }

  for (const auto &tb : tex_refl) {
    if (tb.set != 2 || tb.binding >= 15) {
      continue;
    }

    const uint32_t slot = tb.binding;
    std::string const &tex_name = tb.name;
    SDL_GPUTexture *gpu_tex = m_default_basecolor_tex;
    SDL_GPUSampler *gpu_samp = samp;
    const char *source = "fallback:default";

    // Engine PBR shared textures.
    if (has_name (tex_name, "u_IrradianceMap")) {
      gpu_tex
          = (irr != nullptr) ? (SDL_GPUTexture *)irr : m_default_cubemap_tex;
      gpu_samp = use_ibl_samp;
      source = "pbr:irradiance";
    } else if (has_name (tex_name, "u_PrefilterMap")) {
      gpu_tex
          = (pre != nullptr) ? (SDL_GPUTexture *)pre : m_default_cubemap_tex;
      gpu_samp = use_ibl_samp;
      source = "pbr:prefilter";
    } else if (has_name (tex_name, "u_BRDFLUT")) {
      gpu_tex
          = (lut != nullptr) ? (SDL_GPUTexture *)lut : m_default_brdf_lut_tex;
      gpu_samp = use_ibl_samp;
      source = "pbr:brdf_lut";
    } else if (has_name (tex_name, "u_DirShadowMap")) {
      gpu_tex = (m_shadow_depth != nullptr) ? m_shadow_depth
                                            : m_default_basecolor_tex;
      gpu_samp = shadow_samp;
      source = "pbr:dir_shadow";
    } else if (has_name (tex_name, "u_SpotShadowMap0")) {
      gpu_tex = (m_spot_shadows[0].depth != nullptr)
                    ? m_spot_shadows[0].depth
                    : ((m_shadow_depth != nullptr) ? m_shadow_depth
                                                   : m_default_basecolor_tex);
      gpu_samp = shadow_samp;
      source = "pbr:spot_shadow0";
    } else if (has_name (tex_name, "u_SpotShadowMap1")) {
      gpu_tex = (m_spot_shadows[1].depth != nullptr)
                    ? m_spot_shadows[1].depth
                    : ((m_shadow_depth != nullptr) ? m_shadow_depth
                                                   : m_default_basecolor_tex);
      gpu_samp = shadow_samp;
      source = "pbr:spot_shadow1";
    } else if (has_name (tex_name, "u_SpotShadowMap2")) {
      gpu_tex = (m_spot_shadows[2].depth != nullptr)
                    ? m_spot_shadows[2].depth
                    : ((m_shadow_depth != nullptr) ? m_shadow_depth
                                                   : m_default_basecolor_tex);
      gpu_samp = shadow_samp;
      source = "pbr:spot_shadow2";
    } else if (has_name (tex_name, "u_SpotShadowMap3")) {
      gpu_tex = (m_spot_shadows[3].depth != nullptr)
                    ? m_spot_shadows[3].depth
                    : ((m_shadow_depth != nullptr) ? m_shadow_depth
                                                   : m_default_basecolor_tex);
      gpu_samp = shadow_samp;
      source = "pbr:spot_shadow3";
    } else if (has_name (tex_name, "u_PointShadowMap0")) {
      gpu_tex = (m_point_shadows[0].depth_cube != nullptr)
                    ? m_point_shadows[0].depth_cube
                    : m_default_cubemap_tex;
      gpu_samp = shadow_samp;
      source = "pbr:point_shadow0";
    } else if (has_name (tex_name, "u_PointShadowMap1")) {
      gpu_tex = (m_point_shadows[1].depth_cube != nullptr)
                    ? m_point_shadows[1].depth_cube
                    : m_default_cubemap_tex;
      gpu_samp = shadow_samp;
      source = "pbr:point_shadow1";
    } else if (has_name (tex_name, "u_SSAOTex")) {
      gpu_tex
          = (m_ssao_blur != nullptr) ? m_ssao_blur : m_default_basecolor_tex;
      gpu_samp = (m_ssao_linear_sampler != nullptr) ? m_ssao_linear_sampler
                                                    : m_default_sampler;
      source = "pbr:ssao";
    }
    // Primitive material textures.
    else if (has_name (tex_name, "u_BaseColorTex")) {
      gpu_tex = (prim.mat.base_color_tex != nullptr) ? prim.mat.base_color_tex
                                                     : m_default_basecolor_tex;
      gpu_samp = prim_samp;
      source = "primitive:base_color";
    } else if (has_name (tex_name, "u_MetallicRoughnessTex")) {
      gpu_tex = (prim.mat.metallic_roughness_tex != nullptr)
                    ? prim.mat.metallic_roughness_tex
                    : m_default_mr_tex;
      gpu_samp = prim_samp;
      source = "primitive:metallic_roughness";
    } else if (has_name (tex_name, "u_NormalTex")) {
      gpu_tex = (prim.mat.normal_tex != nullptr) ? prim.mat.normal_tex
                                                 : m_default_normal_tex;
      gpu_samp = prim_samp;
      source = "primitive:normal";
    } else if (has_name (tex_name, "u_EmissiveTex")) {
      gpu_tex = (prim.mat.emissive_tex != nullptr) ? prim.mat.emissive_tex
                                                   : m_default_emissive_tex;
      gpu_samp = prim_samp;
      source = "primitive:emissive";
    }

    // Explicit material parameter overrides win over fallback bindings.
    auto param_val = effective_inst.get_parameter (tb.name, *mat_asset);
    if (std::holds_alternative<rsc::image_id> (param_val)) {
      auto img = m_res_mgr->get (std::get<rsc::image_id> (param_val));
      if (img && img->texture) {
        gpu_tex = img->texture.get ();
        gpu_samp = samp;
        source = "param:image_id";
      }
    } else if (std::holds_alternative<rsc::cubemap_id> (param_val)) {
      auto cub = m_res_mgr->get (std::get<rsc::cubemap_id> (param_val));
      if (cub && cub->texture) {
        gpu_tex = cub->texture;
        gpu_samp = samp;
        source = "param:cubemap_id";
      }
    }

    texbind[slot].texture = gpu_tex;
    texbind[slot].sampler = gpu_samp;

    if (s_custom_tex_log_budget > 0) {
      wsl::log::gfx ()->debug (
          "[custom_mat] tex binding {} name='{}' source={} "
          "prim_has(base={},mr={},n={},e={})",
          slot, tex_name, source, prim.mat.base_color_tex != nullptr,
          prim.mat.metallic_roughness_tex != nullptr,
          prim.mat.normal_tex != nullptr, prim.mat.emissive_tex != nullptr);
      --s_custom_tex_log_budget;
    }
  }

  SDL_BindGPUFragmentSamplers (m_ctx->main_pass, 0, texbind, 15);

  // ---- Clustered lighting storage buffers ----
  m_clustered.bind_for_graphics (m_ctx->main_pass);

  SDL_DrawGPUIndexedPrimitives (m_ctx->main_pass,
                                static_cast<Uint32> (prim.indices.size ()), 1,
                                prim.first_index, 0, 0);
}

void
gfx::scene_renderer::destroy_default_resources ()
{
  if (m_default_sampler != nullptr) {
    SDL_ReleaseGPUSampler (m_ctx->gpu_device, m_default_sampler),
        m_default_sampler = nullptr;
  }

  if (m_default_basecolor_tex != nullptr) {
    SDL_ReleaseGPUTexture (m_ctx->gpu_device, m_default_basecolor_tex),
        m_default_basecolor_tex = nullptr;
  }

  if (m_default_mr_tex != nullptr) {
    SDL_ReleaseGPUTexture (m_ctx->gpu_device, m_default_mr_tex),
        m_default_mr_tex = nullptr;
  }

  if (m_default_normal_tex != nullptr) {
    SDL_ReleaseGPUTexture (m_ctx->gpu_device, m_default_normal_tex),
        m_default_normal_tex = nullptr;
  }

  if (m_default_emissive_tex != nullptr) {
    SDL_ReleaseGPUTexture (m_ctx->gpu_device, m_default_emissive_tex),
        m_default_emissive_tex = nullptr;
  }

  if (m_default_cubemap_tex != nullptr) {
    SDL_ReleaseGPUTexture (m_ctx->gpu_device, m_default_cubemap_tex),
        m_default_cubemap_tex = nullptr;
  }

  if (m_default_brdf_lut_tex != nullptr) {
    SDL_ReleaseGPUTexture (m_ctx->gpu_device, m_default_brdf_lut_tex);
    m_default_brdf_lut_tex = nullptr;
  }
}

void
gfx::scene_renderer::create_skybox_pipeline ()
{
  auto vert_id = m_res_mgr->register_shader (
      "engine://compiled_shaders/skybox.vert.slang.spv");
  auto frag_id = m_res_mgr->register_shader (
      "engine://compiled_shaders/skybox.frag.slang.spv");

  SDL_GPUShader *vert = gfx::shader::load_from_manager (
      m_ctx->gpu_device, m_res_mgr, vert_id, SDL_GPU_SHADERSTAGE_VERTEX, 1, 0);

  SDL_GPUShader *frag
      = gfx::shader::load_from_manager (m_ctx->gpu_device, m_res_mgr, frag_id,
                                        SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 1);

  if ((vert == nullptr) || (frag == nullptr)) {
    if (vert != nullptr) {
      SDL_ReleaseGPUShader (m_ctx->gpu_device, vert);
    }
    if (frag != nullptr) {
      SDL_ReleaseGPUShader (m_ctx->gpu_device, frag);
    }
    return;
  }

  SDL_GPUGraphicsPipelineCreateInfo pipe{};
  SDL_zero (pipe);

  pipe.vertex_shader = vert;
  pipe.fragment_shader = frag;
  pipe.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

  // depth: always pass so nothing can clip the skybox, don't write depth
  pipe.depth_stencil_state.enable_depth_test = true;
  pipe.depth_stencil_state.enable_depth_write = false;
  pipe.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_ALWAYS;

  // no culling needed for fullscreen triangle
  pipe.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;

  // ---- NO vertex buffers ----
  pipe.vertex_input_state.num_vertex_buffers = 0;
  pipe.vertex_input_state.vertex_buffer_descriptions = nullptr;
  pipe.vertex_input_state.num_vertex_attributes = 0;
  pipe.vertex_input_state.vertex_attributes = nullptr;

  SDL_GPUColorTargetDescription ctd[2]{};
  ctd[0].format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
  ctd[1].format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;

  pipe.target_info.num_color_targets = 2;
  pipe.target_info.color_target_descriptions = ctd;

  pipe.target_info.has_depth_stencil_target = true;
  pipe.target_info.depth_stencil_format = m_window->depth_format ();

  pipe.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_4;
  pipe.multisample_state.sample_mask = 0;
  pipe.multisample_state.enable_mask = false;

  m_skybox_pipeline = SDL_CreateGPUGraphicsPipeline (m_ctx->gpu_device, &pipe);

  SDL_ReleaseGPUShader (m_ctx->gpu_device, vert);
  SDL_ReleaseGPUShader (m_ctx->gpu_device, frag);
}

void
gfx::scene_renderer::bind_skybox_pipeline ()
{
  if ((m_skybox_pipeline == nullptr) || (m_ctx == nullptr)
      || (m_ctx->main_pass == nullptr)) {
    return;
  }

  SDL_BindGPUGraphicsPipeline (m_ctx->main_pass, m_skybox_pipeline);
}

void
gfx::scene_renderer::draw_skybox (const gfx::cubemap &cubemap,
                                  const glm::mat4 &view, const glm::mat4 &proj,
                                  const glm::quat &skybox_rotation)
{
  if ((cubemap.texture == nullptr) || (cubemap.sampler == nullptr)) {
    return;
  }

  // Reconstruct the camera's world orientation from the view matrix.
  // The view matrix is inverse(camera_transform).  Since build_render_frame
  // now guarantees the camera matrix is orthonormal (no parent scale),
  // the inverse of the 3x3 part is simply its transpose.
  glm::mat3 const rot = glm::transpose (glm::mat3 (view));
  glm::mat4 const camera_rot = glm::mat4 (rot);

  // Apply optional skybox rotation to the sampling direction.
  glm::mat4 const skybox_rot
      = glm::mat4_cast (glm::conjugate (skybox_rotation));
  glm::mat4 inv_vp = skybox_rot * camera_rot * glm::inverse (proj);

  SDL_PushGPUVertexUniformData (m_ctx->main_cmd, 0, &inv_vp,
                                sizeof (glm::mat4));

  SDL_GPUTextureSamplerBinding bind{};
  bind.texture = cubemap.texture;
  bind.sampler = cubemap.sampler;

  SDL_BindGPUFragmentSamplers (m_ctx->main_pass, 0, &bind, 1);

  SDL_DrawGPUPrimitives (m_ctx->main_pass, 3, 1, 0, 0);
}

void
gfx::scene_renderer::create_unlit_pipeline ()
{
  auto vert_id = m_res_mgr->register_shader (
      "engine://compiled_shaders/cube.vert.slang.spv");
  auto frag_id = m_res_mgr->register_shader (
      "engine://compiled_shaders/unlit.frag.slang.spv");

  SDL_GPUShader *vert = gfx::shader::load_from_manager (
      m_ctx->gpu_device, m_res_mgr, vert_id, SDL_GPU_SHADERSTAGE_VERTEX, 1, 0);

  SDL_GPUShader *frag = gfx::shader::load_from_manager (
      m_ctx->gpu_device, m_res_mgr, frag_id, SDL_GPU_SHADERSTAGE_FRAGMENT,
      /*num_uniform_buffers=*/2, // Material + Lighting
      /*num_samplers=*/4);       // t0..t3 (base/mr/normal/emissive)

  if ((vert == nullptr) || (frag == nullptr)) {
    if (vert != nullptr) {
      SDL_ReleaseGPUShader (m_ctx->gpu_device, vert);
    }
    if (frag != nullptr) {
      SDL_ReleaseGPUShader (m_ctx->gpu_device, frag);
    }
    return;
  }

  // The skinned set passes the skinning vertex shader so both are built from
  // one state description and cannot drift apart. `vs` is explicit because a
  // lambda default argument may not reference a captured local. See the lit
  // equivalent in create_pipeline().
  auto make_pipeline = [&] (alpha_mode a_mode, SDL_GPUCullMode cull,
                            SDL_GPUShader *vs) -> SDL_GPUGraphicsPipeline * {
    SDL_GPUGraphicsPipelineCreateInfo pipe{};
    SDL_zero (pipe);

    pipe.vertex_shader = vs;
    pipe.fragment_shader = frag;
    pipe.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

    pipe.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    pipe.depth_stencil_state.enable_depth_test = true;
    pipe.depth_stencil_state.enable_stencil_test = false;

    pipe.target_info.has_depth_stencil_target = true;
    pipe.target_info.depth_stencil_format = m_window->depth_format ();

    pipe.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;

    pipe.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_4;
    pipe.multisample_state.sample_mask = 0;

    configure_mesh_vertex_input (pipe);

    SDL_GPUColorTargetDescription ctd[2]{};
    ctd[0].format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    ctd[1].format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;

    switch (a_mode) {
    case alpha_mode::opaque:
      ctd[0].blend_state.enable_blend = false;
      ctd[1].blend_state.enable_blend = false;
      pipe.depth_stencil_state.enable_depth_write = true;
      break;

    case alpha_mode::mask:
      ctd[0].blend_state.enable_blend = false;
      ctd[1].blend_state.enable_blend = false;
      pipe.depth_stencil_state.enable_depth_write = true;
      pipe.multisample_state.enable_alpha_to_coverage = true;
      break;

    case alpha_mode::blend:
      ctd[0].blend_state.enable_blend = true;
      ctd[0].blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
      ctd[0].blend_state.dst_color_blendfactor
          = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
      ctd[0].blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
      ctd[0].blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
      ctd[0].blend_state.dst_alpha_blendfactor
          = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
      ctd[0].blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
      ctd[0].blend_state.color_write_mask
          = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G
            | SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;

      ctd[1].blend_state.enable_blend = true;
      ctd[1].blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
      ctd[1].blend_state.dst_color_blendfactor
          = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
      ctd[1].blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
      ctd[1].blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
      ctd[1].blend_state.dst_alpha_blendfactor
          = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
      ctd[1].blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
      ctd[1].blend_state.color_write_mask
          = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G
            | SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;

      pipe.depth_stencil_state.enable_depth_write = false;
      break;
    }

    for (int i = 0; i < 2; ++i) {
      ctd[i].blend_state.color_write_mask
          = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G
            | SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;
    }

    pipe.target_info.num_color_targets = 2;
    pipe.target_info.color_target_descriptions = ctd;

    pipe.rasterizer_state.cull_mode = cull;

    return SDL_CreateGPUGraphicsPipeline (m_ctx->gpu_device, &pipe);
  };

  // Unlit, single-sided
  m_pipelines_unlit.opaque
      = make_pipeline (alpha_mode::opaque, SDL_GPU_CULLMODE_BACK, vert);
  m_pipelines_unlit.mask
      = make_pipeline (alpha_mode::mask, SDL_GPU_CULLMODE_BACK, vert);
  m_pipelines_unlit.blend
      = make_pipeline (alpha_mode::blend, SDL_GPU_CULLMODE_BACK, vert);

  // Unlit, double-sided
  m_pipelines_unlit_double_sided.opaque
      = make_pipeline (alpha_mode::opaque, SDL_GPU_CULLMODE_NONE, vert);
  m_pipelines_unlit_double_sided.mask
      = make_pipeline (alpha_mode::mask, SDL_GPU_CULLMODE_NONE, vert);
  m_pipelines_unlit_double_sided.blend
      = make_pipeline (alpha_mode::blend, SDL_GPU_CULLMODE_NONE, vert);

  // ---- skinned unlit variants ----
  // Built through the same helper as the plain unlit set; only the vertex
  // shader differs. Created before `vert`/`frag` are released below.
  if (SDL_GPUShader *skinned_vert = create_skinned_vertex_shader (
          "engine://compiled_shaders/cube_skinned.vert.slang.spv");
      skinned_vert != nullptr) {
    m_pipelines_skinned_unlit.opaque = make_pipeline (
        alpha_mode::opaque, SDL_GPU_CULLMODE_BACK, skinned_vert);
    m_pipelines_skinned_unlit.mask
        = make_pipeline (alpha_mode::mask, SDL_GPU_CULLMODE_NONE, skinned_vert);
    m_pipelines_skinned_unlit.blend = make_pipeline (
        alpha_mode::blend, SDL_GPU_CULLMODE_NONE, skinned_vert);

    m_pipelines_skinned_unlit_double_sided.opaque = make_pipeline (
        alpha_mode::opaque, SDL_GPU_CULLMODE_NONE, skinned_vert);
    m_pipelines_skinned_unlit_double_sided.mask
        = make_pipeline (alpha_mode::mask, SDL_GPU_CULLMODE_NONE, skinned_vert);
    m_pipelines_skinned_unlit_double_sided.blend = make_pipeline (
        alpha_mode::blend, SDL_GPU_CULLMODE_NONE, skinned_vert);

    if (m_pipelines_skinned_unlit.opaque == nullptr
        || m_pipelines_skinned_unlit_double_sided.opaque == nullptr) {
      wsl::log::gfx ()->warn (
          "Skinned unlit pipelines unavailable ({}); animated unlit meshes "
          "will not render",
          SDL_GetError ());
    }

    SDL_ReleaseGPUShader (m_ctx->gpu_device, skinned_vert);
  }

  SDL_ReleaseGPUShader (m_ctx->gpu_device, vert);
  SDL_ReleaseGPUShader (m_ctx->gpu_device, frag);
}

void
gfx::scene_renderer::destroy_unlit_pipeline ()
{
  // The pipeline_set members are the sole owners; each is released and nulled
  // exactly once here. They previously also had `m_pipeline_unlit` /
  // `m_pipeline_unlit_double_sided` aliases (assigned straight from
  // `.opaque`), and this function released the alias before releasing the set
  // -- releasing the same SDL_GPUGraphicsPipeline twice.
  //
  // SDL does not guard against that: VULKAN_ReleaseGraphicsPipeline() appends
  // unconditionally, so the pointer lands in graphicsPipelinesToDestroy[]
  // twice. PerformPendingDestroys() then destroys the first entry --
  // vkDestroyPipeline succeeds, SDL_free() frees the struct -- and reaches the
  // duplicate, reads referenceCount out of freed memory, and calls
  // vkDestroyPipeline again with a handle loaded from freed memory. The RADON
  // driver blocked in there, freezing the editor on teardown, and it reproduced
  // even under vkDeviceWaitIdle because it was never an in-flight-work problem.
  //
  // The aliases are now removed rather than merely left unreleased, so the
  // ownership invariant cannot drift again.
  auto destroy_set = [this] (pipeline_set &ps) {
    if (ps.opaque != nullptr) {
      SDL_ReleaseGPUGraphicsPipeline (m_ctx->gpu_device, ps.opaque);
      ps.opaque = nullptr;
    }
    if (ps.mask != nullptr) {
      SDL_ReleaseGPUGraphicsPipeline (m_ctx->gpu_device, ps.mask);
      ps.mask = nullptr;
    }
    if (ps.blend != nullptr) {
      SDL_ReleaseGPUGraphicsPipeline (m_ctx->gpu_device, ps.blend);
      ps.blend = nullptr;
    }
  };

  destroy_set (m_pipelines_unlit);
  destroy_set (m_pipelines_unlit_double_sided);
  destroy_set (m_pipelines_skinned_unlit);
  destroy_set (m_pipelines_skinned_unlit_double_sided);
}

void
gfx::scene_renderer::bind_preview_bg_pipeline (SDL_GPURenderPass *pass)
{
  if ((m_pipeline_preview_bg == nullptr) || (pass == nullptr)) {
    return;
  }

  SDL_BindGPUGraphicsPipeline (pass, m_pipeline_preview_bg);

  // IMPORTANT:
  // preview_bg.frag was created with num_samplers = 0
  // Binding samplers here can crash on some backends.
  // So: do NOT call SDL_BindGPUFragmentSamplers for this m_pipeline.
}

void
gfx::scene_renderer::create_preview_bg_pipeline ()
{
  auto vert_id = m_res_mgr->register_shader (
      "engine://compiled_shaders/fullscreen.vert.slang.spv");
  auto frag_id = m_res_mgr->register_shader (
      "engine://compiled_shaders/preview_bg.frag.slang.spv");

  SDL_GPUShader *vert = gfx::shader::load_from_manager (
      m_ctx->gpu_device, m_res_mgr, vert_id, SDL_GPU_SHADERSTAGE_VERTEX, 0, 0);

  SDL_GPUShader *frag = gfx::shader::load_from_manager (
      m_ctx->gpu_device, m_res_mgr, frag_id, SDL_GPU_SHADERSTAGE_FRAGMENT,
      /*num_uniform_buffers=*/1,
      /*num_samplers=*/0);

  if ((vert == nullptr) || (frag == nullptr)) {
    if (vert != nullptr) {
      SDL_ReleaseGPUShader (m_ctx->gpu_device, vert);
    }
    if (frag != nullptr) {
      SDL_ReleaseGPUShader (m_ctx->gpu_device, frag);
    }
    return;
  }

  SDL_GPUGraphicsPipelineCreateInfo pipe{};
  SDL_zero (pipe);

  pipe.vertex_shader = vert;
  pipe.fragment_shader = frag;
  pipe.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

  pipe.depth_stencil_state.enable_depth_test = false;
  pipe.depth_stencil_state.enable_depth_write = false;

  pipe.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;

  pipe.vertex_input_state.num_vertex_buffers = 0;
  pipe.vertex_input_state.num_vertex_attributes = 0;

  // MUST match your 3D pass (2 HDR targets)
  SDL_GPUColorTargetDescription ctd[2]{};
  ctd[0].format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT; // scene
  ctd[1].format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT; // bloom

  pipe.target_info.num_color_targets = 2;
  pipe.target_info.color_target_descriptions = ctd;

  pipe.target_info.has_depth_stencil_target = true;
  pipe.target_info.depth_stencil_format = m_window->depth_format ();

  pipe.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_4;

  m_pipeline_preview_bg
      = SDL_CreateGPUGraphicsPipeline (m_ctx->gpu_device, &pipe);

  SDL_ReleaseGPUShader (m_ctx->gpu_device, vert);
  SDL_ReleaseGPUShader (m_ctx->gpu_device, frag);
}

void
gfx::scene_renderer::destroy_preview_bg_pipeline ()
{
  if (m_pipeline_preview_bg == nullptr) {
    return;
  }
  SDL_ReleaseGPUGraphicsPipeline (m_ctx->gpu_device, m_pipeline_preview_bg);
  m_pipeline_preview_bg = nullptr;
}

[[maybe_unused]] static uint32_t
mip_count_square (uint32_t s)
{
  uint32_t levels = 1;
  while (s > 1) {
    s >>= 1;
    ++levels;
  }
  return levels;
}

static glm::mat4
capture_proj ()
{
  return glm::perspective (glm::radians (90.0F), 1.0F, 0.1F, 10.0F);
}

static glm::mat4
capture_view_for_face (int face)
{
  // Standard cubemap face targets (RH)
  // 0: +X, 1: -X, 2: +Y, 3: -Y, 4: +Z, 5: -Z
  static const glm::vec3 targets[6]
      = { { +1, 0, 0 }, { -1, 0, 0 }, { 0, +1, 0 },
          { 0, -1, 0 }, { 0, 0, +1 }, { 0, 0, -1 } };
  // Ups adjusted to ensure world Up maps to texture top (y=0) given Y-flipped
  // projection. Lateral faces use world Up (0,1,0). +Y face uses -Z as Up. -Y
  // face uses +Z as Up.
  static const glm::vec3 ups[6] = { { 0, 1, 0 }, { 0, 1, 0 }, { 0, 0, -1 },
                                    { 0, 0, 1 }, { 0, 1, 0 }, { 0, 1, 0 } };
  return glm::lookAt (glm::vec3 (0.0F), targets[face], ups[face]);
}

void
gfx::scene_renderer::create_ibl_pipelines ()
{
  if ((m_ibl_irradiance_pipe != nullptr) && (m_ibl_prefilter_pipe != nullptr)
      && (m_ibl_brdf_lut_pipe != nullptr)) {
    return;
  }

  // Fullscreen triangle vertex shader (no vertex buffers)
  auto vert_id = m_res_mgr->register_shader (
      "engine://compiled_shaders/fullscreen.vert.slang.spv");
  SDL_GPUShader *vert = gfx::shader::load_from_manager (
      m_ctx->gpu_device, m_res_mgr, vert_id, SDL_GPU_SHADERSTAGE_VERTEX,
      /*num_uniform_buffers=*/0,
      /*num_samplers=*/0);

  // Irradiance convolution: Params cbuffer + env cubemap sampler
  auto frag_irr_id = m_res_mgr->register_shader (
      "engine://compiled_shaders/ibl_irradiance.frag.slang.spv");
  SDL_GPUShader *frag_irr = gfx::shader::load_from_manager (
      m_ctx->gpu_device, m_res_mgr, frag_irr_id, SDL_GPU_SHADERSTAGE_FRAGMENT,
      /*num_uniform_buffers=*/1,
      /*num_samplers=*/1);

  // Prefilter GGX: Params cbuffer + env cubemap sampler
  auto frag_pre_id = m_res_mgr->register_shader (
      "engine://compiled_shaders/ibl_prefilter.frag.slang.spv");
  SDL_GPUShader *frag_pre = gfx::shader::load_from_manager (
      m_ctx->gpu_device, m_res_mgr, frag_pre_id, SDL_GPU_SHADERSTAGE_FRAGMENT,
      /*num_uniform_buffers=*/1,
      /*num_samplers=*/1);

  // BRDF LUT: no uniforms, no samplers
  auto frag_lut_id = m_res_mgr->register_shader (
      "engine://compiled_shaders/brdf_lut.frag.slang.spv");
  SDL_GPUShader *frag_lut = gfx::shader::load_from_manager (
      m_ctx->gpu_device, m_res_mgr, frag_lut_id, SDL_GPU_SHADERSTAGE_FRAGMENT,
      /*num_uniform_buffers=*/0,
      /*num_samplers=*/0);

  if ((vert == nullptr) || (frag_irr == nullptr) || (frag_pre == nullptr)
      || (frag_lut == nullptr)) {
    if (vert != nullptr) {
      SDL_ReleaseGPUShader (m_ctx->gpu_device, vert);
    }
    if (frag_irr != nullptr) {
      SDL_ReleaseGPUShader (m_ctx->gpu_device, frag_irr);
    }
    if (frag_pre != nullptr) {
      SDL_ReleaseGPUShader (m_ctx->gpu_device, frag_pre);
    }
    if (frag_lut != nullptr) {
      SDL_ReleaseGPUShader (m_ctx->gpu_device, frag_lut);
    }
    return;
  }

  std::function<SDL_GPUGraphicsPipeline *(SDL_GPUShader *)> const make_pipe
      = [&] (SDL_GPUShader *fs) -> SDL_GPUGraphicsPipeline * {
    SDL_GPUGraphicsPipelineCreateInfo pipe{};
    SDL_zero (pipe);

    pipe.vertex_shader = vert;
    pipe.fragment_shader = fs;
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
    ctd.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;

    pipe.target_info.num_color_targets = 1;
    pipe.target_info.color_target_descriptions = &ctd;

    pipe.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    pipe.multisample_state.sample_mask = 0;
    pipe.multisample_state.enable_mask = false;

    return SDL_CreateGPUGraphicsPipeline (m_ctx->gpu_device, &pipe);
  };

  m_ibl_irradiance_pipe = make_pipe (frag_irr);
  m_ibl_prefilter_pipe = make_pipe (frag_pre);
  m_ibl_brdf_lut_pipe = make_pipe (frag_lut);

  SDL_ReleaseGPUShader (m_ctx->gpu_device, vert);
  SDL_ReleaseGPUShader (m_ctx->gpu_device, frag_irr);
  SDL_ReleaseGPUShader (m_ctx->gpu_device, frag_pre);
  SDL_ReleaseGPUShader (m_ctx->gpu_device, frag_lut);

  if ((m_ibl_irradiance_pipe == nullptr) || (m_ibl_prefilter_pipe == nullptr)
      || (m_ibl_brdf_lut_pipe == nullptr)) {
    wsl::log::gfx ()->error ("IBL: failed to create bake pipelines: {}",
                             SDL_GetError ());
  }
}

void
gfx::scene_renderer::destroy_ibl_pipelines ()
{
  if (m_ibl_irradiance_pipe != nullptr) {
    SDL_ReleaseGPUGraphicsPipeline (m_ctx->gpu_device, m_ibl_irradiance_pipe);
    m_ibl_irradiance_pipe = nullptr;
  }
  if (m_ibl_prefilter_pipe != nullptr) {
    SDL_ReleaseGPUGraphicsPipeline (m_ctx->gpu_device, m_ibl_prefilter_pipe);
    m_ibl_prefilter_pipe = nullptr;
  }
  if (m_ibl_brdf_lut_pipe != nullptr) {
    SDL_ReleaseGPUGraphicsPipeline (m_ctx->gpu_device, m_ibl_brdf_lut_pipe);
    m_ibl_brdf_lut_pipe = nullptr;
  }
}

// Call this after cubemap load (main thread)
void
gfx::scene_renderer::bake_equirect_to_cube (gfx::cubemap &env,
                                            SDL_GPUTexture *equi_tex)
{
  if ((env.texture == nullptr) || (equi_tex == nullptr)) {
    return;
  }

  if (m_equi_to_cube_pipe == nullptr) {
    auto vert_id = m_res_mgr->register_shader (
        "engine://compiled_shaders/skybox.vert.slang.spv");
    auto frag_id = m_res_mgr->register_shader (
        "engine://compiled_shaders/equirect_to_cube.frag.slang.spv");

    SDL_GPUShader *vert = gfx::shader::load_from_manager (
        m_ctx->gpu_device, m_res_mgr, vert_id, SDL_GPU_SHADERSTAGE_VERTEX,
        /*num_uniform_buffers=*/1,
        /*num_samplers=*/0);

    SDL_GPUShader *frag = gfx::shader::load_from_manager (
        m_ctx->gpu_device, m_res_mgr, frag_id, SDL_GPU_SHADERSTAGE_FRAGMENT,
        /*num_uniform_buffers=*/0,
        /*num_samplers=*/1);

    if ((vert == nullptr) || (frag == nullptr)) {
      if (vert != nullptr) {
        SDL_ReleaseGPUShader (m_ctx->gpu_device, vert);
      }
      if (frag != nullptr) {
        SDL_ReleaseGPUShader (m_ctx->gpu_device, frag);
      }
      return;
    }

    SDL_GPUGraphicsPipelineCreateInfo pipe{};
    SDL_zero (pipe);
    pipe.vertex_shader = vert;
    pipe.fragment_shader = frag;
    pipe.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipe.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;

    SDL_GPUColorTargetDescription ctd{};
    ctd.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;

    pipe.target_info.num_color_targets = 1;
    pipe.target_info.color_target_descriptions = &ctd;
    pipe.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;

    m_equi_to_cube_pipe
        = SDL_CreateGPUGraphicsPipeline (m_ctx->gpu_device, &pipe);

    SDL_ReleaseGPUShader (m_ctx->gpu_device, vert);
    SDL_ReleaseGPUShader (m_ctx->gpu_device, frag);
  }

  SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer (m_ctx->gpu_device);

  SDL_GPUTextureSamplerBinding equi_bind{};
  equi_bind.texture = equi_tex;
  equi_bind.sampler = m_default_sampler;

  for (int face = 0; face < 6; ++face) {
    SDL_GPUColorTargetInfo ct{};
    ct.texture = env.texture;
    ct.mip_level = 0;
    ct.layer_or_depth_plane = (Uint32)face;
    ct.load_op = SDL_GPU_LOADOP_CLEAR;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    ct.clear_color = SDL_FColor{ 0, 0, 0, 1 };

    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass (cmd, &ct, 1, nullptr);
    SDL_BindGPUGraphicsPipeline (pass, m_equi_to_cube_pipe);

    int capture_face = face;
    if (face == 0) {
      capture_face = 1;
    } else if (face == 1) {
      capture_face = 0;
    }

    glm::mat4 const view = capture_view_for_face (capture_face);
    glm::mat4 inv_vp = glm::inverse (capture_proj () * view);

    SDL_PushGPUVertexUniformData (cmd, 0, &inv_vp, sizeof (inv_vp));
    SDL_BindGPUFragmentSamplers (pass, 0, &equi_bind, 1);

    SDL_DrawGPUPrimitives (pass, 3, 1, 0, 0);
    SDL_EndGPURenderPass (pass);
  }

  SDL_SubmitGPUCommandBuffer (cmd);
}

// Call this after cubemap load (main thread)
void
gfx::scene_renderer::bake_procedural_skybox (gfx::cubemap &env,
                                             const glm::vec3 &sun_dir)
{
  if (env.texture == nullptr) {
    return;
  }

  if (m_last_baked_sun_dir == sun_dir) {
    return;
  }

  m_last_baked_sun_dir = sun_dir;

  if (m_procedural_skybox_pipe == nullptr) {
    auto vert_id = m_res_mgr->register_shader (
        "engine://compiled_shaders/skybox.vert.slang.spv");
    auto frag_id = m_res_mgr->register_shader (
        "engine://compiled_shaders/procedural_skybox.frag.slang.spv");

    SDL_GPUShader *vert = gfx::shader::load_from_manager (
        m_ctx->gpu_device, m_res_mgr, vert_id, SDL_GPU_SHADERSTAGE_VERTEX,
        /*num_uniform_buffers=*/1,
        /*num_samplers=*/0);

    SDL_GPUShader *frag = gfx::shader::load_from_manager (
        m_ctx->gpu_device, m_res_mgr, frag_id, SDL_GPU_SHADERSTAGE_FRAGMENT,
        /*num_uniform_buffers=*/1,
        /*num_samplers=*/0);

    if ((vert == nullptr) || (frag == nullptr)) {
      if (vert != nullptr) {
        SDL_ReleaseGPUShader (m_ctx->gpu_device, vert);
      }
      if (frag != nullptr) {
        SDL_ReleaseGPUShader (m_ctx->gpu_device, frag);
      }
      return;
    }

    SDL_GPUGraphicsPipelineCreateInfo pipe{};
    SDL_zero (pipe);
    pipe.vertex_shader = vert;
    pipe.fragment_shader = frag;
    pipe.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipe.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;

    SDL_GPUColorTargetDescription ctd{};
    ctd.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;

    pipe.target_info.num_color_targets = 1;
    pipe.target_info.color_target_descriptions = &ctd;
    pipe.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;

    m_procedural_skybox_pipe
        = SDL_CreateGPUGraphicsPipeline (m_ctx->gpu_device, &pipe);

    SDL_ReleaseGPUShader (m_ctx->gpu_device, vert);
    SDL_ReleaseGPUShader (m_ctx->gpu_device, frag);
  }

  SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer (m_ctx->gpu_device);

  struct alignas (16) params
  {
    glm::vec3 sun_dir;
    float pad;
  } p{};
  p.sun_dir = sun_dir;

  for (int face = 0; face < 6; ++face) {
    SDL_GPUColorTargetInfo ct{};
    ct.texture = env.texture;
    ct.mip_level = 0;
    ct.layer_or_depth_plane = (Uint32)face;
    ct.load_op = SDL_GPU_LOADOP_CLEAR;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    ct.clear_color = SDL_FColor{ 0, 0, 0, 1 };

    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass (cmd, &ct, 1, nullptr);
    if ((m_procedural_skybox_pipe != nullptr) && (pass != nullptr)) {
      SDL_BindGPUGraphicsPipeline (pass, m_procedural_skybox_pipe);

      int capture_face = face;
      // Consistent with capture_view_for_face in bake_equirect_to_cube
      if (face == 0) {
        capture_face = 1;
      } else if (face == 1) {
        capture_face = 0;
      }

      glm::mat4 const view = capture_view_for_face (capture_face);
      glm::mat4 inv_vp = glm::inverse (capture_proj () * view);

      SDL_PushGPUVertexUniformData (cmd, 0, &inv_vp, sizeof (inv_vp));
      SDL_PushGPUFragmentUniformData (cmd, 0, &p, sizeof (p));

      SDL_DrawGPUPrimitives (pass, 3, 1, 0, 0);
    }
    SDL_EndGPURenderPass (pass);
  }

  SDL_SubmitGPUCommandBuffer (cmd);

  // After baking the main cubemap, we must also bake IBL for it to work!
  bake_ibl (env);
}

// Call this after cubemap load (main thread)
void
gfx::scene_renderer::bake_ibl (gfx::cubemap &env)
{
  if ((env.texture == nullptr) || (env.sampler == nullptr)
      || (env.ibl_irradiance == nullptr) || (env.ibl_prefilter == nullptr)
      || (env.ibl_brdf_lut == nullptr)) {
    wsl::log::gfx ()->warn (
        "IBL: bake_ibl called with missing textures/samplers");
    return;
  }

  create_ibl_pipelines ();
  if ((m_ibl_irradiance_pipe == nullptr) || (m_ibl_prefilter_pipe == nullptr)
      || (m_ibl_brdf_lut_pipe == nullptr)) {
    return;
  }

  SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer (m_ctx->gpu_device);

  // Common binding: source environment cubemap at fragment slot 0
  SDL_GPUTextureSamplerBinding src_env{};
  src_env.texture = env.texture;
  src_env.sampler = env.sampler;

  // ========= (A) Irradiance cubemap =========
  {
    for (int face = 0; face < 6; ++face) {
      SDL_GPUColorTargetInfo ct{};
      ct.texture = env.ibl_irradiance;
      ct.mip_level = 0;
      ct.layer_or_depth_plane = (Uint32)face;
      ct.load_op = SDL_GPU_LOADOP_CLEAR;
      ct.store_op = SDL_GPU_STOREOP_STORE;
      ct.clear_color = SDL_FColor{ 0, 0, 0, 1 };
      ct.cycle = false;

      SDL_GPURenderPass *pass = SDL_BeginGPURenderPass (cmd, &ct, 1, nullptr);

      if ((m_ibl_irradiance_pipe != nullptr) && (pass != nullptr)) {
        SDL_BindGPUGraphicsPipeline (pass, m_ibl_irradiance_pipe);
        SDL_BindGPUFragmentSamplers (pass, 0, &src_env, 1);

        //// We pass inverse VP so fragment can reconstruct world direction.
        // glm::mat4 V = capture_view_for_face(face);
        // glm::mat4 inv_vp = glm::inverse(P * V);

        // vertex uniform slot 0 (matches your style)
        // Fragment params MUST match ibl_irradiance.frag.hlsl cbuffer Params
        // (b0, space3)
        struct alignas (16) irr_params
        {
          int face_index;
          float env_intensity;
          float pad0, pad1;
        } ip{};

        ip.face_index = (face == 2) ? 3 : (face == 3) ? 2 : face;
        ip.env_intensity
            = 1.0F; // start with 1.0; you can expose this as a knob

        SDL_PushGPUFragmentUniformData (cmd, 0, &ip, sizeof (ip));

        // draw fullscreen triangle
        SDL_DrawGPUPrimitives (pass, 3, 1, 0, 0);
      }

      SDL_EndGPURenderPass (pass);
    }
  }

  // ========= (B) Prefiltered specular cubemap =========
  {
    const glm::mat4 p = capture_proj ();

    struct alignas (16) prefilter_params
    {
      int face_index;      // 0..5
      float roughness;     // 0..1
      float env_intensity; // usually 1.0 (or exposure)
      float pad0;
    } pp{};

    pp.env_intensity = 1.0F;

    uint32_t const mips = env.prefilter_mip_count;

    for (uint32_t mip = 0; mip < mips; ++mip) {
      pp.roughness = (mips <= 1) ? 0.0F : (float (mip) / float (mips - 1));

      for (int face = 0; face < 6; ++face) {
        pp.face_index = (face == 2) ? 3 : (face == 3) ? 2 : face;

        SDL_GPUColorTargetInfo ct{};
        ct.texture = env.ibl_prefilter;
        ct.mip_level = mip;
        ct.layer_or_depth_plane = (Uint32)face;
        ct.load_op = SDL_GPU_LOADOP_CLEAR;
        ct.store_op = SDL_GPU_STOREOP_STORE;
        ct.clear_color = SDL_FColor{ 0, 0, 0, 1 };
        ct.cycle = false;

        SDL_GPURenderPass *pass = SDL_BeginGPURenderPass (cmd, &ct, 1, nullptr);

        if ((m_ibl_prefilter_pipe != nullptr) && (pass != nullptr)) {
          SDL_BindGPUGraphicsPipeline (pass, m_ibl_prefilter_pipe);
          SDL_BindGPUFragmentSamplers (pass, 0, &src_env, 1);

          glm::mat4 const v = capture_view_for_face (face);
          glm::mat4 inv_vp = glm::inverse (p * v);
          SDL_PushGPUVertexUniformData (cmd, 0, &inv_vp, sizeof (glm::mat4));

          SDL_PushGPUFragmentUniformData (cmd, 0, &pp, sizeof (pp));

          SDL_DrawGPUPrimitives (pass, 3, 1, 0, 0);
        }

        SDL_EndGPURenderPass (pass);
      }
    }

    env.prefilter_max_mip = float (mips - 1);
  }

  // ========= (C) BRDF LUT =========
  {
    SDL_GPUColorTargetInfo ct{};
    ct.texture = env.ibl_brdf_lut;
    ct.mip_level = 0;
    ct.layer_or_depth_plane = 0;
    ct.load_op = SDL_GPU_LOADOP_CLEAR;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    ct.clear_color = SDL_FColor{ 0, 0, 0, 1 };
    ct.cycle = false;

    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass (cmd, &ct, 1, nullptr);

    if ((m_ibl_brdf_lut_pipe != nullptr) && (pass != nullptr)) {
      SDL_BindGPUGraphicsPipeline (pass, m_ibl_brdf_lut_pipe);
      // no source textures needed for LUT
      SDL_DrawGPUPrimitives (pass, 3, 1, 0, 0);
    }

    SDL_EndGPURenderPass (pass);
  }

  SDL_SubmitGPUCommandBuffer (cmd);

  wsl::log::gfx ()->trace (
      "Baked IBL environment (irradiance + prefilter + BRDF LUT)");
}

void
gfx::scene_renderer::set_environment (const gfx::cubemap *env)
{
  m_active_env = env;

  if (m_active_env != nullptr) {
    m_prefilter_max_mip = m_active_env->prefilter_max_mip;
    m_ibl_sampler = m_active_env->ibl_sampler;
  } else {
    m_prefilter_max_mip = 0.0F;
    m_ibl_sampler = m_default_sampler;
  }
}
void
gfx::scene_renderer::create_shadow_resources (uint32_t size)
{
  m_shadow_size = size;

  // Depth texture (D32)
  SDL_GPUTextureCreateInfo ti{};
  ti.type = SDL_GPU_TEXTURETYPE_2D;
  ti.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT; // if your backend lacks it,
                                               // try D24_UNORM_S8_UINT
  ti.width = m_shadow_size;
  ti.height = m_shadow_size;
  ti.layer_count_or_depth = 1;
  ti.num_levels = 1;
  ti.usage
      = (SDL_GPUTextureUsageFlags)(SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET
                                   | SDL_GPU_TEXTUREUSAGE_SAMPLER);

  m_shadow_depth = SDL_CreateGPUTexture (m_ctx->gpu_device, &ti);
  if (m_shadow_depth == nullptr) {
    wsl::log::gfx ()->error ("Failed to create shadow depth texture: {}",
                             SDL_GetError ());
    return;
  }

  // Clamp sampler (important for shadow edges)
  SDL_GPUSamplerCreateInfo sinfo{};
  sinfo.min_filter = SDL_GPU_FILTER_NEAREST;
  sinfo.mag_filter = SDL_GPU_FILTER_NEAREST;
  sinfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
  sinfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  sinfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  sinfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;

  m_shadow_sampler = SDL_CreateGPUSampler (m_ctx->gpu_device, &sinfo);
  if (m_shadow_sampler == nullptr) {
    wsl::log::gfx ()->error ("Failed to create shadow sampler: {}",
                             SDL_GetError ());
  }

  for (int i = 0; i < max_shadowed_spots; ++i) {
    SDL_GPUTextureCreateInfo ti{};
    ti.type = SDL_GPU_TEXTURETYPE_2D;
    ti.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    ti.width = m_shadow_size;
    ti.height = m_shadow_size;
    ti.layer_count_or_depth = 1;
    ti.num_levels = 1;
    ti.usage
        = (SDL_GPUTextureUsageFlags)(SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET
                                     | SDL_GPU_TEXTUREUSAGE_SAMPLER);

    m_spot_shadows[i].depth = SDL_CreateGPUTexture (m_ctx->gpu_device, &ti);
    m_spot_shadows[i].map_size = float (m_shadow_size);
  }

  for (int i = 0; i < max_shadowed_points; ++i) {
    SDL_GPUTextureCreateInfo ti{};
    ti.type = SDL_GPU_TEXTURETYPE_CUBE;
    ti.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    ti.width = m_shadow_size;
    ti.height = m_shadow_size;
    ti.layer_count_or_depth = 6; // cubemap faces
    ti.num_levels = 1;
    ti.usage
        = (SDL_GPUTextureUsageFlags)(SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET
                                     | SDL_GPU_TEXTUREUSAGE_SAMPLER);

    m_point_shadows[i].depth_cube
        = SDL_CreateGPUTexture (m_ctx->gpu_device, &ti);
    m_point_shadows[i].near_plane = 0.1F;
    m_point_shadows[i].far_plane = 25.0F;
    m_point_shadows[i].bias = 0.02F;
    m_point_shadows[i].enabled = false;
    m_point_shadows[i].strength = 1.0F;
  }

  create_shadow_pipeline ();
  create_point_shadow_pipeline ();

  wsl::log::gfx ()->debug ("Shadow maps: {}x{} ({} spot + {} point)",
                           m_shadow_size, m_shadow_size, max_shadowed_spots,
                           max_shadowed_points);
}

void
gfx::scene_renderer::destroy_shadow_resources ()
{
  if (m_shadow_pipe != nullptr) {
    SDL_ReleaseGPUGraphicsPipeline (m_ctx->gpu_device, m_shadow_pipe);
    m_shadow_pipe = nullptr;
  }

  if (m_shadow_pipe_double_sided != nullptr) {
    SDL_ReleaseGPUGraphicsPipeline (m_ctx->gpu_device,
                                    m_shadow_pipe_double_sided);
    m_shadow_pipe_double_sided = nullptr;
  }
  if (m_shadow_pipe_skinned != nullptr) {
    SDL_ReleaseGPUGraphicsPipeline (m_ctx->gpu_device, m_shadow_pipe_skinned);
    m_shadow_pipe_skinned = nullptr;
  }
  if (m_shadow_pipe_skinned_double_sided != nullptr) {
    SDL_ReleaseGPUGraphicsPipeline (m_ctx->gpu_device,
                                    m_shadow_pipe_skinned_double_sided);
    m_shadow_pipe_skinned_double_sided = nullptr;
  }
  if (m_shadow_sampler != nullptr) {
    SDL_ReleaseGPUSampler (m_ctx->gpu_device, m_shadow_sampler),
        m_shadow_sampler = nullptr;
  }
  if (m_shadow_depth != nullptr) {
    SDL_ReleaseGPUTexture (m_ctx->gpu_device, m_shadow_depth),
        m_shadow_depth = nullptr;
  }

  for (shadow_map_2d &s : m_spot_shadows) {
    if (s.depth != nullptr) {
      SDL_ReleaseGPUTexture (m_ctx->gpu_device, s.depth);
      s.depth = nullptr;
    }
  }

  for (point_shadow_map &p : m_point_shadows) {
    if (p.depth_cube != nullptr) {
      SDL_ReleaseGPUTexture (m_ctx->gpu_device, p.depth_cube);
      p.depth_cube = nullptr;
    }
  }

  /*
  if (spot_shadow_sampler) {
    SDL_ReleaseGPUSampler(m_ctx->gpu_device, spot_shadow_sampler);
    spot_shadow_sampler = nullptr;
  }
  */

  if (m_point_shadow_pipe != nullptr) {
    SDL_ReleaseGPUGraphicsPipeline (m_ctx->gpu_device, m_point_shadow_pipe);
    m_point_shadow_pipe = nullptr;
  }

  if (m_point_shadow_pipe_double_sided != nullptr) {
    SDL_ReleaseGPUGraphicsPipeline (m_ctx->gpu_device,
                                    m_point_shadow_pipe_double_sided);
    m_point_shadow_pipe_double_sided = nullptr;
  }

  if (m_point_shadow_pipe_skinned != nullptr) {
    SDL_ReleaseGPUGraphicsPipeline (m_ctx->gpu_device,
                                    m_point_shadow_pipe_skinned);
    m_point_shadow_pipe_skinned = nullptr;
  }
  if (m_point_shadow_pipe_skinned_double_sided != nullptr) {
    SDL_ReleaseGPUGraphicsPipeline (m_ctx->gpu_device,
                                    m_point_shadow_pipe_skinned_double_sided);
    m_point_shadow_pipe_skinned_double_sided = nullptr;
  }
}

void
gfx::scene_renderer::create_shadow_pipeline ()
{
  auto vert_id = m_res_mgr->register_shader (
      "engine://compiled_shaders/shadow_depth.vert.slang.spv");
  auto frag_id = m_res_mgr->register_shader (
      "engine://compiled_shaders/shadow_depth.frag.slang.spv");

  SDL_GPUShader *vert = gfx::shader::load_from_manager (
      m_ctx->gpu_device, m_res_mgr, vert_id, SDL_GPU_SHADERSTAGE_VERTEX, 1, 0);

  SDL_GPUShader *frag
      = gfx::shader::load_from_manager (m_ctx->gpu_device, m_res_mgr, frag_id,
                                        SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0);

  if ((vert == nullptr) || (frag == nullptr)) {
    if (vert != nullptr) {
      SDL_ReleaseGPUShader (m_ctx->gpu_device, vert);
    }
    if (frag != nullptr) {
      SDL_ReleaseGPUShader (m_ctx->gpu_device, frag);
    }
    return;
  }

  SDL_GPUGraphicsPipelineCreateInfo pipe{};
  SDL_zero (pipe);

  pipe.vertex_shader = vert;
  pipe.fragment_shader = frag;
  pipe.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

  pipe.target_info.has_depth_stencil_target = true;
  pipe.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;

  pipe.target_info.num_color_targets = 0;
  pipe.target_info.color_target_descriptions = nullptr;

  pipe.depth_stencil_state.enable_depth_test = true;
  pipe.depth_stencil_state.enable_depth_write = true;
  pipe.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;

  pipe.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;

  pipe.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;

  configure_mesh_vertex_input (pipe);

  pipe.rasterizer_state.enable_depth_bias = true;
  pipe.rasterizer_state.depth_bias_constant_factor = 2.0F;
  pipe.rasterizer_state.depth_bias_slope_factor = 2.0F;
  pipe.rasterizer_state.depth_bias_clamp = 0.0F;

  // Regular solid m_meshes
  pipe.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
  m_shadow_pipe = SDL_CreateGPUGraphicsPipeline (m_ctx->gpu_device, &pipe);

  // Double-sided m_meshes
  pipe.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
  m_shadow_pipe_double_sided
      = SDL_CreateGPUGraphicsPipeline (m_ctx->gpu_device, &pipe);

  // Skinned variants: same state, but the vertex shader declares the palette
  // cbuffer, so an animated mesh casts a deforming shadow.
  if (SDL_GPUShader *skinned_vert = create_skinned_vertex_shader (
          "engine://compiled_shaders/shadow_depth_skinned.vert.slang.spv");
      skinned_vert != nullptr) {
    pipe.vertex_shader = skinned_vert;

    pipe.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
    m_shadow_pipe_skinned
        = SDL_CreateGPUGraphicsPipeline (m_ctx->gpu_device, &pipe);

    pipe.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    m_shadow_pipe_skinned_double_sided
        = SDL_CreateGPUGraphicsPipeline (m_ctx->gpu_device, &pipe);

    SDL_ReleaseGPUShader (m_ctx->gpu_device, skinned_vert);
  }

  SDL_ReleaseGPUShader (m_ctx->gpu_device, vert);
  SDL_ReleaseGPUShader (m_ctx->gpu_device, frag);

  if ((m_shadow_pipe == nullptr) || (m_shadow_pipe_double_sided == nullptr)) {
    wsl::log::gfx ()->error ("Failed to create shadow m_pipeline(s): {}",
                             SDL_GetError ());
  }
}

glm::mat4
gfx::scene_renderer::make_light_vp_from_camera (
    const glm::mat4 &cam_view, const glm::mat4 &cam_proj,
    const glm::vec3 &light_dir_world, float shadow_near, float shadow_far,
    float z_padding)
{
  glm::mat4 const inv_vp = glm::inverse (cam_proj * cam_view);

  glm::vec3 frustum_ws[8];
  int idx = 0;

  const float z_ndc[2] = { -1.0F, +1.0F };

  for (int z = 0; z < 2; ++z) {
    for (int y = 0; y < 2; ++y) {
      for (int x = 0; x < 2; ++x) {
        glm::vec4 const p_ndc ((x != 0) ? 1.0F : -1.0F, (y != 0) ? 1.0F : -1.0F,
                               z_ndc[z], 1.0F);

        glm::vec4 p_ws = inv_vp * p_ndc;
        p_ws /= p_ws.w;
        frustum_ws[idx++] = glm::vec3 (p_ws);
      }
    }
  }

  glm::vec3 center (0.0F);
  for (const glm::vec3 &p : frustum_ws) {
    center += p;
  }
  center /= 8.0F;

  float radius = 0.0F;
  for (const glm::vec3 &p : frustum_ws) {
    radius = std::max (radius, glm::length (p - center));
  }

  radius = std::ceil (radius * 16.0F) / 16.0F;

  glm::vec3 const dir = glm::normalize (light_dir_world);
  glm::vec3 const up
      = (std::abs (dir.y) > 0.95F) ? glm::vec3 (0, 0, 1) : glm::vec3 (0, 1, 0);

  glm::vec3 light_pos = center - dir * (radius + z_padding + shadow_near);
  glm::mat4 light_v = glm::lookAt (light_pos, center, up);

  glm::vec3 center_ls = glm::vec3 (light_v * glm::vec4 (center, 1.0F));

  float const tex_size = float (std::max (1U, 4096U));
  float const world_units_per_texel = (2.0F * radius) / tex_size;

  center_ls.x = std::floor (center_ls.x / world_units_per_texel)
                * world_units_per_texel;
  center_ls.y = std::floor (center_ls.y / world_units_per_texel)
                * world_units_per_texel;

  // rebuild light position from snapped light-space center
  glm::mat4 const inv_light_v = glm::inverse (light_v);
  glm::vec3 const snapped_center_ws
      = glm::vec3 (inv_light_v * glm::vec4 (center_ls, 1.0F));

  light_pos = snapped_center_ws - dir * (radius + z_padding + shadow_near);
  light_v = glm::lookAt (light_pos, snapped_center_ws, up);

  float const l = -radius;
  float const r = radius;
  float const b = -radius;
  float const t = radius;
  float const n = shadow_near;
  float const f = (2.0F * radius) + (2.0F * z_padding) + shadow_far;

  glm::mat4 const light_p_gl = glm::ortho (l, r, b, t, n, f);
  glm::mat4 const light_vp_gl = light_p_gl * light_v;

  glm::mat4 clip_correction (1.0F);
  clip_correction[2][2] = 0.5F; // z: [-1,1] -> [0,1]
  clip_correction[3][2] = 0.5F;

  return clip_correction * light_vp_gl;
}

void
gfx::scene_renderer::render_shadow_map (gfx::scene &s)
{
  if (!m_shadows_enabled || (m_shadow_depth == nullptr)
      || (m_shadow_pipe == nullptr)) {
    return;
  }

  SDL_GPUDepthStencilTargetInfo ds{};
  ds.texture = m_shadow_depth;
  ds.mip_level = 0;
  ds.layer = 0;
  ds.load_op = SDL_GPU_LOADOP_CLEAR;
  ds.store_op = SDL_GPU_STOREOP_STORE;
  ds.clear_depth = 1.0F;
  ds.clear_stencil = 0;
  ds.cycle = false;

  SDL_GPURenderPass *pass
      = SDL_BeginGPURenderPass (m_ctx->main_cmd, nullptr, 0, &ds);
  if (pass == nullptr) {
    return;
  }

  SDL_BindGPUGraphicsPipeline (pass, m_shadow_pipe);

  // IMPORTANT: set viewport to shadow map size if you do custom viewport
  // elsewhere. If your SDL_gpu backend uses full target by default, you can
  // skip.

  // Draw all nodes/m_meshes depth-only
  std::function<void (gfx::node &)> draw_node;
  draw_node = [&] (gfx::node &n) {
    if (!n.mesh_lods.empty ()) {
      gfx::mesh const *m = select_lod (n);
      if (m) {
        struct alignas (16) shadow_matrices
        {
          glm::mat4 model;
          glm::mat4 lightvp;
        } sm{ n.world_transform, m_light_vp };

        SDL_PushGPUVertexUniformData (m_ctx->main_cmd, 0, &sm, sizeof (sm));

        for (const primitive &prim : m->primitives) {
          // Only geometry; index counts are same
          SDL_DrawGPUIndexedPrimitives (pass, (Uint32)prim.indices.size (), 1,
                                        prim.first_index, 0, 0);
        }
      }
    }

    for (gfx::node &c : n.children) {
      draw_node (c);
    }
  };

  for (gfx::node &root : s.roots) {
    draw_node (root);
  }

  SDL_EndGPURenderPass (pass);
}

void
gfx::scene_renderer::draw_model_shadow (gfx::model_3d &model,
                                        size_t scene_index,
                                        const glm::mat4 &model_matrix,
                                        SDL_GPURenderPass *pass,
                                        SDL_GPUCommandBuffer *cmd)
{
  if (scene_index >= model.scenes.size ()) {
    return;
  }

  model.ensure_gpu_buffers (m_ctx);
  model.bind (pass);

  gfx::scene &scene = model.scenes[scene_index];
  for (gfx::node &root : scene.roots) {
    update_node_world (root, model_matrix);
  }

  std::function<void (gfx::node &)> draw_node = [&] (gfx::node &n) {
    if (!n.mesh_lods.empty ()) {
      gfx::mesh const *m = select_lod (n);
      if (m) {
        struct alignas (16) shadow_matrices
        {
          glm::mat4 model;
          glm::mat4 lightvp;
        } sm{ n.world_transform, m_light_vp };

        SDL_PushGPUVertexUniformData (cmd, 0, &sm, sizeof (sm));
        push_active_palette (cmd, 1);

        for (const primitive &prim : m->primitives) {
          // An animated instance must draw with the skinned pipeline, whose
          // vertex shader declares the palette cbuffer.
          SDL_GPUGraphicsPipeline *pipe = nullptr;
          if (m_active_palette != nullptr) {
            pipe = prim.mat.double_sided ? m_shadow_pipe_skinned_double_sided
                                         : m_shadow_pipe_skinned;
          } else {
            pipe = prim.mat.double_sided ? m_shadow_pipe_double_sided
                                         : m_shadow_pipe;
          }
          if (!pipe || !m_shadow_pass) {
            continue;
          }

          SDL_BindGPUGraphicsPipeline (m_shadow_pass, pipe);

          SDL_DrawGPUIndexedPrimitives (m_shadow_pass,
                                        (Uint32)prim.indices.size (), 1,
                                        prim.first_index, 0, 0);
        }
      }
    }
    for (gfx::node &c : n.children) {
      draw_node (c);
    }
  };

  for (gfx::node &root : scene.roots) {
    draw_node (root);
  }
}

void
gfx::scene_renderer::begin_shadow_pass ()
{
  ZoneScoped;
  if (!m_shadows_enabled || (m_shadow_depth == nullptr)
      || (m_shadow_pipe == nullptr) || (m_shadow_pass != nullptr)
      || (m_ctx->main_cmd == nullptr)) {
    return;
  }

  SDL_GPUDepthStencilTargetInfo ds{};
  ds.texture = m_shadow_depth;
  ds.mip_level = 0;
  ds.layer = 0;
  ds.load_op = SDL_GPU_LOADOP_CLEAR;
  ds.store_op = SDL_GPU_STOREOP_STORE;
  ds.clear_depth = 1.0F;
  ds.clear_stencil = 0;
  ds.cycle = false;

  m_shadow_pass = SDL_BeginGPURenderPass (m_ctx->main_cmd, nullptr, 0, &ds);

  SDL_GPUViewport vp{};
  vp.x = 0;
  vp.y = 0;
  vp.w = (float)m_shadow_size;
  vp.h = (float)m_shadow_size;
  vp.min_depth = 0.0F;
  vp.max_depth = 1.0F;
  SDL_SetGPUViewport (m_shadow_pass, &vp);

  SDL_Rect sc{};
  sc.x = 0;
  sc.y = 0;
  sc.w = (int)m_shadow_size;
  sc.h = (int)m_shadow_size;
  SDL_SetGPUScissor (m_shadow_pass, &sc);
}

void
gfx::scene_renderer::end_shadow_pass ()
{
  if (m_shadow_pass == nullptr) {
    return;
  }
  SDL_EndGPURenderPass (m_shadow_pass);
  m_shadow_pass = nullptr;
}

glm::mat4
gfx::scene_renderer::make_spot_light_vp (const glm::vec3 &light_pos,
                                         const glm::vec3 &light_dir,
                                         float outer_angle_radians,
                                         float near_plane, float far_plane)
{
  glm::vec3 const dir = glm::normalize (light_dir);
  glm::vec3 const up
      = (std::abs (dir.y) > 0.95F) ? glm::vec3 (0, 0, 1) : glm::vec3 (0, 1, 0);

  glm::mat4 const view = glm::lookAt (light_pos, light_pos + dir, up);

  // full cone angle
  float const fov = glm::degrees (outer_angle_radians) * 2.0F;
  glm::mat4 const proj = glm::perspective (fov, 1.0F, near_plane, far_plane);

  glm::mat4 clip_correction (1.0F);
  clip_correction[2][2] = 0.5F;
  clip_correction[3][2] = 0.5F;

  return clip_correction * proj * view;
}

void
gfx::scene_renderer::draw_model_shadow (gfx::model_3d &model,
                                        size_t scene_index,
                                        const glm::mat4 &model_matrix,
                                        const joint_palette *palette)
{
  m_active_palette = palette;
  if (m_shadow_pass == nullptr) {
    return;
  }
  if (scene_index >= model.scenes.size ()) {
    return;
  }

  model.ensure_gpu_buffers (m_ctx);
  model.bind (m_shadow_pass);

  gfx::scene &scene = model.scenes[scene_index];
  for (gfx::node &root : scene.roots) {
    update_node_world (root, model_matrix);
  }

  std::function<void (gfx::node &)> draw_node = [&] (gfx::node &n) {
    if (!n.mesh_lods.empty ()) {
      gfx::mesh const *m = select_lod (n);
      if (m) {
        struct alignas (16) shadow_matrices
        {
          glm::mat4 model;
          glm::mat4 lightvp;
        } sm{ n.world_transform, m_light_vp };

        SDL_PushGPUVertexUniformData (m_ctx->main_cmd, 0, &sm, sizeof (sm));

        for (const primitive &prim : m->primitives) {
          SDL_GPUGraphicsPipeline *pipe = prim.mat.double_sided
                                              ? m_shadow_pipe_double_sided
                                              : m_shadow_pipe;
          if (!pipe || !m_shadow_pass) {
            continue;
          }

          SDL_BindGPUGraphicsPipeline (m_shadow_pass, pipe);

          SDL_DrawGPUIndexedPrimitives (m_shadow_pass,
                                        (Uint32)prim.indices.size (), 1,
                                        prim.first_index, 0, 0);
        }
      }
    }
    for (gfx::node &c : n.children) {
      draw_node (c);
    }
  };

  for (gfx::node &root : scene.roots) {
    draw_node (root);
    m_active_palette = nullptr;
  }
}

void
gfx::scene_renderer::begin_spot_shadow_pass (int index)
{
  if (index < 0 || index >= max_shadowed_spots) {
    return;
  }
  if ((m_spot_shadows[index].depth == nullptr) || (m_shadow_pipe == nullptr)
      || (m_shadow_pass != nullptr) || (m_ctx->main_cmd == nullptr)) {
    return;
  }

  SDL_GPUDepthStencilTargetInfo ds{};
  ds.texture = m_spot_shadows[index].depth;
  ds.mip_level = 0;
  ds.layer = 0;
  ds.load_op = SDL_GPU_LOADOP_CLEAR;
  ds.store_op = SDL_GPU_STOREOP_STORE;
  ds.clear_depth = 1.0F;
  ds.clear_stencil = 0;
  ds.cycle = false;

  m_shadow_pass = SDL_BeginGPURenderPass (m_ctx->main_cmd, nullptr, 0, &ds);

  SDL_GPUViewport vp{};
  vp.x = 0;
  vp.y = 0;
  vp.w = (float)m_shadow_size;
  vp.h = (float)m_shadow_size;
  vp.min_depth = 0.0F;
  vp.max_depth = 1.0F;
  SDL_SetGPUViewport (m_shadow_pass, &vp);

  SDL_Rect sc{};
  sc.x = 0;
  sc.y = 0;
  sc.w = (int)m_shadow_size;
  sc.h = (int)m_shadow_size;
  SDL_SetGPUScissor (m_shadow_pass, &sc);
}
void
gfx::scene_renderer::draw_model_spot_shadow (gfx::model_3d &model,
                                             size_t scene_index,
                                             const glm::mat4 &model_matrix,
                                             const glm::mat4 &light_vp_mat)
{
  if (m_shadow_pass == nullptr) {
    return;
  }
  if (scene_index >= model.scenes.size ()) {
    return;
  }

  model.ensure_gpu_buffers (m_ctx);
  model.bind (m_shadow_pass);

  gfx::scene &scene = model.scenes[scene_index];
  for (gfx::node &root : scene.roots) {
    update_node_world (root, model_matrix);
  }

  std::function<void (gfx::node &)> draw_node = [&] (gfx::node &n) {
    if (!n.mesh_lods.empty ()) {
      gfx::mesh const *m = select_lod (n);
      if (m) {
        struct alignas (16) shadow_matrices
        {
          glm::mat4 model;
          glm::mat4 lightvp;
        } sm{ n.world_transform, light_vp_mat };

        SDL_PushGPUVertexUniformData (m_ctx->main_cmd, 0, &sm, sizeof (sm));

        for (const primitive &prim : m->primitives) {
          SDL_GPUGraphicsPipeline *pipe = prim.mat.double_sided
                                              ? m_shadow_pipe_double_sided
                                              : m_shadow_pipe;
          if (!pipe || !m_shadow_pass) {
            continue;
          }

          SDL_BindGPUGraphicsPipeline (m_shadow_pass, pipe);

          SDL_DrawGPUIndexedPrimitives (m_shadow_pass,
                                        (Uint32)prim.indices.size (), 1,
                                        prim.first_index, 0, 0);
        }
      }
    }

    for (gfx::node &c : n.children) {
      draw_node (c);
    }
  };

  for (gfx::node &root : scene.roots) {
    draw_node (root);
  }
}

void
gfx::scene_renderer::end_spot_shadow_pass ()
{
  if (m_shadow_pass == nullptr) {
    return;
  }

  SDL_EndGPURenderPass (m_shadow_pass);
  m_shadow_pass = nullptr;
}

glm::mat4
gfx::scene_renderer::make_point_light_view_proj (const glm::vec3 &light_pos,
                                                 int face, float near_plane,
                                                 float far_plane)
{
  static const glm::vec3 targets[6]
      = { { +1, 0, 0 }, { -1, 0, 0 }, { 0, +1, 0 },
          { 0, -1, 0 }, { 0, 0, +1 }, { 0, 0, -1 } };

  static const glm::vec3 ups[6] = { { 0, 1, 0 }, { 0, 1, 0 }, { 0, 0, -1 },
                                    { 0, 0, 1 }, { 0, 1, 0 }, { 0, 1, 0 } };

  glm::mat4 const view
      = glm::lookAt (light_pos, light_pos + targets[face], ups[face]);
  glm::mat4 const proj
      = glm::perspective (glm::radians (90.0F), 1.0F, near_plane, far_plane);

  glm::mat4 clip_correction (1.0F);
  clip_correction[2][2] = 0.5F;
  clip_correction[3][2] = 0.5F;

  return clip_correction * proj * view;
}

void
gfx::scene_renderer::begin_point_shadow_pass (int index, int face)
{
  if (index < 0 || index >= max_shadowed_points) {
    return;
  }
  if (face < 0 || face >= 6) {
    return;
  }
  if ((m_point_shadows[index].depth_cube == nullptr)
      || (m_point_shadow_pipe == nullptr) || (m_shadow_pass != nullptr)
      || (m_ctx->main_cmd == nullptr)) {
    return;
  }

  SDL_GPUDepthStencilTargetInfo ds{};
  ds.texture = m_point_shadows[index].depth_cube;
  ds.mip_level = 0;
  ds.layer = face;
  ds.load_op = SDL_GPU_LOADOP_CLEAR;
  ds.store_op = SDL_GPU_STOREOP_STORE;
  ds.clear_depth = 1.0F;
  ds.clear_stencil = 0;
  ds.cycle = false;

  m_shadow_pass = SDL_BeginGPURenderPass (m_ctx->main_cmd, nullptr, 0, &ds);

  SDL_GPUViewport vp{};
  vp.x = 0;
  vp.y = 0;
  vp.w = (float)m_shadow_size;
  vp.h = (float)m_shadow_size;
  vp.min_depth = 0.0F;
  vp.max_depth = 1.0F;
  SDL_SetGPUViewport (m_shadow_pass, &vp);

  SDL_Rect sc{};
  sc.x = 0;
  sc.y = 0;
  sc.w = (int)m_shadow_size;
  sc.h = (int)m_shadow_size;
  SDL_SetGPUScissor (m_shadow_pass, &sc);
}

void
gfx::scene_renderer::end_point_shadow_pass ()
{
  if (m_shadow_pass == nullptr) {
    return;
  }
  SDL_EndGPURenderPass (m_shadow_pass);
  m_shadow_pass = nullptr;
}

void
gfx::scene_renderer::draw_model_point_shadow (
    gfx::model_3d &model, size_t scene_index, const glm::mat4 &model_matrix,
    const glm::mat4 &light_vp_mat, const glm::vec3 &light_pos, float far_plane,
    const joint_palette *palette)
{
  m_active_palette = palette;
  if (m_shadow_pass == nullptr) {
    return;
  }
  if (scene_index >= model.scenes.size ()) {
    return;
  }

  model.ensure_gpu_buffers (m_ctx);
  model.bind (m_shadow_pass);

  gfx::scene &scene = model.scenes[scene_index];
  for (gfx::node &root : scene.roots) {
    update_node_world (root, model_matrix);
  }

  std::function<void (gfx::node &)> draw_node = [&] (gfx::node &n) {
    if (!n.mesh_lods.empty ()) {
      gfx::mesh const *m = select_lod (n);
      if (m) {
        struct alignas (16) shadow_matrices
        {
          glm::mat4 model;
          glm::mat4 lightvp;
        } sm{ n.world_transform, light_vp_mat };

        SDL_PushGPUVertexUniformData (m_ctx->main_cmd, 0, &sm, sizeof (sm));
        push_active_palette (m_ctx->main_cmd, 1);

        struct alignas (16) point_shadow_params
        {
          glm::vec4 light_pos_far;
        } psp{};
        psp.light_pos_far = glm::vec4 (light_pos, far_plane);

        SDL_PushGPUFragmentUniformData (m_ctx->main_cmd, 0, &psp, sizeof (psp));

        for (const primitive &prim : m->primitives) {
          SDL_GPUGraphicsPipeline *pipe = nullptr;
          if (m_active_palette != nullptr) {
            pipe = prim.mat.double_sided
                       ? m_point_shadow_pipe_skinned_double_sided
                       : m_point_shadow_pipe_skinned;
          } else {
            pipe = prim.mat.double_sided ? m_point_shadow_pipe_double_sided
                                         : m_point_shadow_pipe;
          }
          if (!pipe || !m_shadow_pass) {
            continue;
          }

          SDL_BindGPUGraphicsPipeline (m_shadow_pass, pipe);

          SDL_DrawGPUIndexedPrimitives (m_shadow_pass,
                                        (Uint32)prim.indices.size (), 1,
                                        prim.first_index, 0, 0);
        }
      }
    }

    for (gfx::node &c : n.children) {
      draw_node (c);
    }
  };

  for (gfx::node &root : scene.roots) {
    draw_node (root);
    m_active_palette = nullptr;
  }
}

void
gfx::scene_renderer::create_point_shadow_pipeline ()
{
  auto vert_id = m_res_mgr->register_shader (
      "engine://compiled_shaders/point_shadow.vert.slang.spv");
  auto frag_id = m_res_mgr->register_shader (
      "engine://compiled_shaders/point_shadow.frag.slang.spv");

  SDL_GPUShader *vert = gfx::shader::load_from_manager (
      m_ctx->gpu_device, m_res_mgr, vert_id, SDL_GPU_SHADERSTAGE_VERTEX, 1, 0);

  SDL_GPUShader *frag
      = gfx::shader::load_from_manager (m_ctx->gpu_device, m_res_mgr, frag_id,
                                        SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0);

  if ((vert == nullptr) || (frag == nullptr)) {
    if (vert != nullptr) {
      SDL_ReleaseGPUShader (m_ctx->gpu_device, vert);
    }
    if (frag != nullptr) {
      SDL_ReleaseGPUShader (m_ctx->gpu_device, frag);
    }
    return;
  }

  SDL_GPUGraphicsPipelineCreateInfo pipe{};
  SDL_zero (pipe);

  pipe.vertex_shader = vert;
  pipe.fragment_shader = frag;
  pipe.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

  pipe.target_info.has_depth_stencil_target = true;
  pipe.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
  pipe.target_info.num_color_targets = 0;
  pipe.target_info.color_target_descriptions = nullptr;

  pipe.depth_stencil_state.enable_depth_test = true;
  pipe.depth_stencil_state.enable_depth_write = true;
  pipe.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;

  pipe.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;

  pipe.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;

  configure_mesh_vertex_input (pipe);

  pipe.rasterizer_state.enable_depth_bias = true;
  pipe.rasterizer_state.depth_bias_constant_factor = 2.0F;
  pipe.rasterizer_state.depth_bias_slope_factor = 2.0F;
  pipe.rasterizer_state.depth_bias_clamp = 0.0F;

  pipe.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
  m_point_shadow_pipe
      = SDL_CreateGPUGraphicsPipeline (m_ctx->gpu_device, &pipe);

  pipe.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
  m_point_shadow_pipe_double_sided
      = SDL_CreateGPUGraphicsPipeline (m_ctx->gpu_device, &pipe);

  if (SDL_GPUShader *skinned_vert = create_skinned_vertex_shader (
          "engine://compiled_shaders/point_shadow_skinned.vert.slang.spv");
      skinned_vert != nullptr) {
    pipe.vertex_shader = skinned_vert;

    pipe.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
    m_point_shadow_pipe_skinned
        = SDL_CreateGPUGraphicsPipeline (m_ctx->gpu_device, &pipe);

    pipe.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    m_point_shadow_pipe_skinned_double_sided
        = SDL_CreateGPUGraphicsPipeline (m_ctx->gpu_device, &pipe);

    SDL_ReleaseGPUShader (m_ctx->gpu_device, skinned_vert);
  }

  SDL_ReleaseGPUShader (m_ctx->gpu_device, vert);
  SDL_ReleaseGPUShader (m_ctx->gpu_device, frag);

  if ((m_point_shadow_pipe == nullptr)
      || (m_point_shadow_pipe_double_sided == nullptr)) {
    wsl::log::gfx ()->error ("Failed to create point shadow m_pipeline(s): {}",
                             SDL_GetError ());
  }
}

static float
lerpf (float a, float b, float t)
{
  return a + ((b - a) * t);
}

void
gfx::scene_renderer::create_ssao_kernel ()
{
  for (size_t i = 0; i < m_ssao_kernel.size (); ++i) {
    glm::vec3 s (glm::linearRand (-1.0F, 1.0F), glm::linearRand (-1.0F, 1.0F),
                 glm::linearRand (0.0F, 1.0F));

    s = glm::normalize (s);
    s *= glm::linearRand (0.0F, 1.0F);

    float scale = float (i) / float (m_ssao_kernel.size ());
    scale = lerpf (0.1F, 1.0F, scale * scale);

    s *= scale;
    m_ssao_kernel[i] = glm::vec4 (s, 0.0F);
  }
}

void
gfx::scene_renderer::create_ssao_noise_texture (SDL_GPUCommandBuffer *cmd)
{
  if (m_ssao_noise_tex != nullptr) {
    return;
  }

  std::array<glm::vec4, 16> noise{};
  for (glm::vec4 &n : noise) {
    n = glm::vec4 (glm::linearRand (-1.0F, 1.0F), glm::linearRand (-1.0F, 1.0F),
                   0.0F, 0.0F);
  }

  SDL_GPUTextureCreateInfo ti{};
  ti.type = SDL_GPU_TEXTURETYPE_2D;
  ti.format = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
  ti.width = 4;
  ti.height = 4;
  ti.layer_count_or_depth = 1;
  ti.num_levels = 1;
  ti.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;

  m_ssao_noise_tex = SDL_CreateGPUTexture (m_ctx->gpu_device, &ti);
  if (m_ssao_noise_tex == nullptr) {
    wsl::log::gfx ()->error ("Failed to create SSAO noise texture: {}",
                             SDL_GetError ());
    return;
  }

  SDL_GPUTransferBufferCreateInfo tb{};
  tb.size = Uint32 (sizeof (noise));
  tb.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;

  SDL_GPUTransferBuffer *upload
      = SDL_CreateGPUTransferBuffer (m_ctx->gpu_device, &tb);
  if (upload == nullptr) {
    return;
  }

  void *mapped = SDL_MapGPUTransferBuffer (m_ctx->gpu_device, upload, false);
  std::memcpy (mapped, noise.data (), sizeof (noise));
  SDL_UnmapGPUTransferBuffer (m_ctx->gpu_device, upload);

  // Use provided command buffer when called during an active frame to avoid
  // submitting a separate command buffer while main_cmd is still recording.
  bool const own_cmd = (cmd == nullptr);
  if (own_cmd) {
    cmd = SDL_AcquireGPUCommandBuffer (m_ctx->gpu_device);
  }

  SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass (cmd);

  SDL_GPUTextureTransferInfo src{};
  src.transfer_buffer = upload;
  src.offset = 0;
  src.pixels_per_row = 4;
  src.rows_per_layer = 4;

  SDL_GPUTextureRegion dst{};
  dst.texture = m_ssao_noise_tex;
  dst.mip_level = 0;
  dst.layer = 0;
  dst.x = 0;
  dst.y = 0;
  dst.z = 0;
  dst.w = 4;
  dst.h = 4;
  dst.d = 1;

  SDL_UploadToGPUTexture (copy, &src, &dst, false);
  SDL_EndGPUCopyPass (copy);

  if (own_cmd) {
    SDL_SubmitGPUCommandBuffer (cmd);
  }

  SDL_ReleaseGPUTransferBuffer (m_ctx->gpu_device, upload);
}

void
gfx::scene_renderer::create_ssao_resources (uint32_t w, uint32_t h)
{
  if (w == 0 || h == 0) {
    return;
  }

  if (m_ssao_width == w && m_ssao_height == h
      && (m_ssao_normal_depth != nullptr) && (m_ssao_depth != nullptr)
      && (m_ssao_raw != nullptr) && (m_ssao_blur != nullptr)) {
    return;
  }

  destroy_ssao_resources ();

  m_ssao_width = w;
  m_ssao_height = h;

  create_ssao_kernel ();
  create_ssao_noise_texture (m_ctx->main_cmd);

  SDL_GPUTextureCreateInfo nd{};
  nd.type = SDL_GPU_TEXTURETYPE_2D;
  nd.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
  nd.width = w;
  nd.height = h;
  nd.layer_count_or_depth = 1;
  nd.num_levels = 1;
  nd.usage = (SDL_GPUTextureUsageFlags)(SDL_GPU_TEXTUREUSAGE_COLOR_TARGET
                                        | SDL_GPU_TEXTUREUSAGE_SAMPLER);

  m_ssao_normal_depth = SDL_CreateGPUTexture (m_ctx->gpu_device, &nd);

  SDL_GPUTextureCreateInfo dt{};
  dt.type = SDL_GPU_TEXTURETYPE_2D;
  dt.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
  dt.width = w;
  dt.height = h;
  dt.layer_count_or_depth = 1;
  dt.num_levels = 1;
  dt.usage
      = (SDL_GPUTextureUsageFlags)(SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET
                                   | SDL_GPU_TEXTUREUSAGE_SAMPLER);

  m_ssao_depth = SDL_CreateGPUTexture (m_ctx->gpu_device, &dt);

  SDL_GPUTextureCreateInfo ao{};
  ao.type = SDL_GPU_TEXTURETYPE_2D;
  ao.format = SDL_GPU_TEXTUREFORMAT_R16_FLOAT;
  ao.width = w;
  ao.height = h;
  ao.layer_count_or_depth = 1;
  ao.num_levels = 1;
  ao.usage = (SDL_GPUTextureUsageFlags)(SDL_GPU_TEXTUREUSAGE_COLOR_TARGET
                                        | SDL_GPU_TEXTUREUSAGE_SAMPLER);

  m_ssao_raw = SDL_CreateGPUTexture (m_ctx->gpu_device, &ao);
  m_ssao_blur = SDL_CreateGPUTexture (m_ctx->gpu_device, &ao);

  SDL_GPUSamplerCreateInfo linear{};
  linear.min_filter = SDL_GPU_FILTER_LINEAR;
  linear.mag_filter = SDL_GPU_FILTER_LINEAR;
  linear.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
  linear.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  linear.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  linear.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  m_ssao_linear_sampler = SDL_CreateGPUSampler (m_ctx->gpu_device, &linear);

  SDL_GPUSamplerCreateInfo point{};
  point.min_filter = SDL_GPU_FILTER_NEAREST;
  point.mag_filter = SDL_GPU_FILTER_NEAREST;
  point.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
  point.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
  point.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
  point.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
  m_ssao_point_sampler = SDL_CreateGPUSampler (m_ctx->gpu_device, &point);

  if ((m_ssao_normal_depth == nullptr) || (m_ssao_depth == nullptr)
      || (m_ssao_raw == nullptr) || (m_ssao_blur == nullptr)
      || (m_ssao_linear_sampler == nullptr)
      || (m_ssao_point_sampler == nullptr)) {
    wsl::log::gfx ()->error ("Failed to create SSAO resources: {}",
                             SDL_GetError ());
  }

  wsl::log::gfx ()->debug (
      "SSAO targets: normal/depth {}x{} -> AO {}x{} ({} kernel samples)",
      m_ssao_width, m_ssao_height, m_ssao_width, m_ssao_height,
      m_ssao_kernel.size ());
}

void
gfx::scene_renderer::destroy_ssao_resources ()
{
  if (m_ssao_prepass != nullptr) {
    SDL_EndGPURenderPass (m_ssao_prepass);
    m_ssao_prepass = nullptr;
  }

  if (m_ssao_normal_depth != nullptr) {
    SDL_ReleaseGPUTexture (m_ctx->gpu_device, m_ssao_normal_depth);
    m_ssao_normal_depth = nullptr;
  }
  if (m_ssao_depth != nullptr) {
    SDL_ReleaseGPUTexture (m_ctx->gpu_device, m_ssao_depth);
    m_ssao_depth = nullptr;
  }
  if (m_ssao_raw != nullptr) {
    SDL_ReleaseGPUTexture (m_ctx->gpu_device, m_ssao_raw);
    m_ssao_raw = nullptr;
  }
  if (m_ssao_blur != nullptr) {
    SDL_ReleaseGPUTexture (m_ctx->gpu_device, m_ssao_blur);
    m_ssao_blur = nullptr;
  }
  if (m_ssao_noise_tex != nullptr) {
    SDL_ReleaseGPUTexture (m_ctx->gpu_device, m_ssao_noise_tex);
    m_ssao_noise_tex = nullptr;
  }
  if (m_ssao_linear_sampler != nullptr) {
    SDL_ReleaseGPUSampler (m_ctx->gpu_device, m_ssao_linear_sampler);
    m_ssao_linear_sampler = nullptr;
  }
  if (m_ssao_point_sampler != nullptr) {
    SDL_ReleaseGPUSampler (m_ctx->gpu_device, m_ssao_point_sampler);
    m_ssao_point_sampler = nullptr;
  }

  m_ssao_width = 0;
  m_ssao_height = 0;
}

void
gfx::scene_renderer::create_ssao_pipeline ()
{
  // depth+normal prepass
  {
    auto vert_id = m_res_mgr->register_shader (
        "engine://compiled_shaders/ssao_prepass.vert.slang.spv");
    auto frag_id = m_res_mgr->register_shader (
        "engine://compiled_shaders/ssao_prepass.frag.slang.spv");

    SDL_GPUShader *vert
        = gfx::shader::load_from_manager (m_ctx->gpu_device, m_res_mgr, vert_id,
                                          SDL_GPU_SHADERSTAGE_VERTEX, 1, 0);

    SDL_GPUShader *frag
        = gfx::shader::load_from_manager (m_ctx->gpu_device, m_res_mgr, frag_id,
                                          SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0);

    SDL_GPUGraphicsPipelineCreateInfo pipe{};
    SDL_zero (pipe);

    pipe.vertex_shader = vert;
    pipe.fragment_shader = frag;
    pipe.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

    pipe.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    pipe.depth_stencil_state.enable_depth_test = true;
    pipe.depth_stencil_state.enable_depth_write = true;

    pipe.target_info.has_depth_stencil_target = true;
    pipe.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;

    SDL_GPUColorTargetDescription ctd{};
    ctd.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    pipe.target_info.num_color_targets = 1;
    pipe.target_info.color_target_descriptions = &ctd;

    configure_mesh_vertex_input (pipe);

    pipe.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pipe.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;

    pipe.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
    m_ssao_prepass_pipe
        = SDL_CreateGPUGraphicsPipeline (m_ctx->gpu_device, &pipe);

    pipe.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    m_ssao_prepass_pipe_double_sided
        = SDL_CreateGPUGraphicsPipeline (m_ctx->gpu_device, &pipe);

    // Skinned prepass, so ambient occlusion follows the deformed surface.
    if (SDL_GPUShader *skinned_vert = create_skinned_vertex_shader (
            "engine://compiled_shaders/ssao_prepass_skinned.vert.slang.spv");
        skinned_vert != nullptr) {
      pipe.vertex_shader = skinned_vert;

      pipe.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
      m_ssao_prepass_pipe_skinned
          = SDL_CreateGPUGraphicsPipeline (m_ctx->gpu_device, &pipe);

      pipe.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
      m_ssao_prepass_pipe_skinned_double_sided
          = SDL_CreateGPUGraphicsPipeline (m_ctx->gpu_device, &pipe);

      SDL_ReleaseGPUShader (m_ctx->gpu_device, skinned_vert);
    }

    SDL_ReleaseGPUShader (m_ctx->gpu_device, vert);
    SDL_ReleaseGPUShader (m_ctx->gpu_device, frag);
  }

  // ssao fullscreen
  {
    auto vert_id = m_res_mgr->register_shader (
        "engine://compiled_shaders/fullscreen.vert.slang.spv");
    auto frag_id = m_res_mgr->register_shader (
        "engine://compiled_shaders/ssao.frag.slang.spv");

    SDL_GPUShader *vert
        = gfx::shader::load_from_manager (m_ctx->gpu_device, m_res_mgr, vert_id,
                                          SDL_GPU_SHADERSTAGE_VERTEX, 0, 0);

    SDL_GPUShader *frag
        = gfx::shader::load_from_manager (m_ctx->gpu_device, m_res_mgr, frag_id,
                                          SDL_GPU_SHADERSTAGE_FRAGMENT, 2, 2);

    SDL_GPUGraphicsPipelineCreateInfo pipe{};
    SDL_zero (pipe);
    pipe.vertex_shader = vert;
    pipe.fragment_shader = frag;
    pipe.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipe.depth_stencil_state.enable_depth_test = false;
    pipe.depth_stencil_state.enable_depth_write = false;
    pipe.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pipe.vertex_input_state.num_vertex_buffers = 0;
    pipe.vertex_input_state.num_vertex_attributes = 0;

    SDL_GPUColorTargetDescription ctd{};
    ctd.format = SDL_GPU_TEXTUREFORMAT_R16_FLOAT;
    pipe.target_info.num_color_targets = 1;
    pipe.target_info.color_target_descriptions = &ctd;

    pipe.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;

    m_ssao_pipe = SDL_CreateGPUGraphicsPipeline (m_ctx->gpu_device, &pipe);

    SDL_ReleaseGPUShader (m_ctx->gpu_device, vert);
    SDL_ReleaseGPUShader (m_ctx->gpu_device, frag);
  }

  // blur fullscreen
  {
    auto vert_id = m_res_mgr->register_shader (
        "engine://compiled_shaders/fullscreen.vert.slang.spv");
    auto frag_id = m_res_mgr->register_shader (
        "engine://compiled_shaders/ssao_blur.frag.slang.spv");

    SDL_GPUShader *vert
        = gfx::shader::load_from_manager (m_ctx->gpu_device, m_res_mgr, vert_id,
                                          SDL_GPU_SHADERSTAGE_VERTEX, 0, 0);

    SDL_GPUShader *frag
        = gfx::shader::load_from_manager (m_ctx->gpu_device, m_res_mgr, frag_id,
                                          SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);

    SDL_GPUGraphicsPipelineCreateInfo pipe{};
    SDL_zero (pipe);
    pipe.vertex_shader = vert;
    pipe.fragment_shader = frag;
    pipe.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipe.depth_stencil_state.enable_depth_test = false;
    pipe.depth_stencil_state.enable_depth_write = false;
    pipe.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pipe.vertex_input_state.num_vertex_buffers = 0;
    pipe.vertex_input_state.num_vertex_attributes = 0;

    SDL_GPUColorTargetDescription ctd{};
    ctd.format = SDL_GPU_TEXTUREFORMAT_R16_FLOAT;
    pipe.target_info.num_color_targets = 1;
    pipe.target_info.color_target_descriptions = &ctd;

    pipe.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;

    m_ssao_blur_pipe = SDL_CreateGPUGraphicsPipeline (m_ctx->gpu_device, &pipe);

    SDL_ReleaseGPUShader (m_ctx->gpu_device, vert);
    SDL_ReleaseGPUShader (m_ctx->gpu_device, frag);
  }
}

void
gfx::scene_renderer::destroy_ssao_pipeline ()
{
  if (m_ssao_prepass_pipe != nullptr) {
    SDL_ReleaseGPUGraphicsPipeline (m_ctx->gpu_device, m_ssao_prepass_pipe);
    m_ssao_prepass_pipe = nullptr;
  }
  if (m_ssao_prepass_pipe_double_sided != nullptr) {
    SDL_ReleaseGPUGraphicsPipeline (m_ctx->gpu_device,
                                    m_ssao_prepass_pipe_double_sided);
    m_ssao_prepass_pipe_double_sided = nullptr;
  }
  if (m_ssao_prepass_pipe_skinned != nullptr) {
    SDL_ReleaseGPUGraphicsPipeline (m_ctx->gpu_device,
                                    m_ssao_prepass_pipe_skinned);
    m_ssao_prepass_pipe_skinned = nullptr;
  }
  if (m_ssao_prepass_pipe_skinned_double_sided != nullptr) {
    SDL_ReleaseGPUGraphicsPipeline (m_ctx->gpu_device,
                                    m_ssao_prepass_pipe_skinned_double_sided);
    m_ssao_prepass_pipe_skinned_double_sided = nullptr;
  }
  if (m_ssao_pipe != nullptr) {
    SDL_ReleaseGPUGraphicsPipeline (m_ctx->gpu_device, m_ssao_pipe);
    m_ssao_pipe = nullptr;
  }
  if (m_ssao_blur_pipe != nullptr) {
    SDL_ReleaseGPUGraphicsPipeline (m_ctx->gpu_device, m_ssao_blur_pipe);
    m_ssao_blur_pipe = nullptr;
  }
}

void
gfx::scene_renderer::begin_ssao_prepass (const glm::mat4 &view,
                                         const glm::mat4 &proj)
{
  (void)view;
  (void)proj;

  if (!ssao_enabled || (m_ssao_normal_depth == nullptr)
      || (m_ssao_depth == nullptr) || (m_ssao_prepass != nullptr)
      || (m_ctx->main_cmd == nullptr)) {
    return;
  }

  SDL_GPUColorTargetInfo ct{};
  ct.texture = m_ssao_normal_depth;
  ct.mip_level = 0;
  ct.layer_or_depth_plane = 0;
  ct.load_op = SDL_GPU_LOADOP_CLEAR;
  ct.store_op = SDL_GPU_STOREOP_STORE;
  ct.clear_color = SDL_FColor{ 0.0F, 0.0F, 1.0F, 1.0F };
  ct.cycle = false;

  SDL_GPUDepthStencilTargetInfo ds{};
  ds.texture = m_ssao_depth;
  ds.mip_level = 0;
  ds.layer = 0;
  ds.load_op = SDL_GPU_LOADOP_CLEAR;
  ds.store_op = SDL_GPU_STOREOP_STORE;
  ds.clear_depth = 1.0F;
  ds.clear_stencil = 0;
  ds.cycle = false;

  m_ssao_prepass = SDL_BeginGPURenderPass (m_ctx->main_cmd, &ct, 1, &ds);

  SDL_GPUViewport vp{};
  vp.x = 0.0F;
  vp.y = 0.0F;
  vp.w = float (m_ssao_width);
  vp.h = float (m_ssao_height);
  vp.min_depth = 0.0F;
  vp.max_depth = 1.0F;
  SDL_SetGPUViewport (m_ssao_prepass, &vp);

  SDL_Rect sc{};
  sc.x = 0;
  sc.y = 0;
  sc.w = int (m_ssao_width);
  sc.h = int (m_ssao_height);
  SDL_SetGPUScissor (m_ssao_prepass, &sc);
}

void
gfx::scene_renderer::end_ssao_prepass ()
{
  if (m_ssao_prepass == nullptr) {
    return;
  }
  SDL_EndGPURenderPass (m_ssao_prepass);
  m_ssao_prepass = nullptr;
}

void
gfx::scene_renderer::draw_model_ssao (gfx::model_3d &model, size_t scene_index,
                                      const glm::mat4 &model_matrix,
                                      const glm::mat4 &view,
                                      const glm::mat4 &proj)
{
  if ((m_ssao_prepass == nullptr) || scene_index >= model.scenes.size ()) {
    return;
  }

  model.ensure_gpu_buffers (m_ctx);
  model.bind (m_ssao_prepass);

  gfx::scene &scene = model.scenes[scene_index];
  for (gfx::node &root : scene.roots) {
    update_node_world (root, model_matrix);
  }

  std::function<void (gfx::node &)> draw_node = [&] (gfx::node &n) {
    if (!n.mesh_lods.empty ()) {
      gfx::mesh const *m = select_lod (n);
      if (m) {
        glm::mat3 n3
            = glm::transpose (glm::inverse (glm::mat3 (n.world_transform)));
        glm::mat4 n4 (1.0F);
        n4[0] = glm::vec4 (n3[0], 0.0F);
        n4[1] = glm::vec4 (n3[1], 0.0F);
        n4[2] = glm::vec4 (n3[2], 0.0F);

        struct alignas (16) ssao_prepass_matrices
        {
          glm::mat4 model;
          glm::mat4 view;
          glm::mat4 proj;
          glm::mat4 normal;
        } mats{ n.world_transform, view, proj, n4 };

        SDL_PushGPUVertexUniformData (m_ctx->main_cmd, 0, &mats, sizeof (mats));
        push_active_palette (m_ctx->main_cmd, 1);

        for (const primitive &prim : m->primitives) {
          SDL_GPUGraphicsPipeline *pipe = nullptr;
          if (m_active_palette != nullptr) {
            pipe = prim.mat.double_sided
                       ? m_ssao_prepass_pipe_skinned_double_sided
                       : m_ssao_prepass_pipe_skinned;
          } else {
            pipe = prim.mat.double_sided ? m_ssao_prepass_pipe_double_sided
                                         : m_ssao_prepass_pipe;
          }
          if (!pipe || !m_ssao_prepass) {
            continue;
          }

          SDL_BindGPUGraphicsPipeline (m_ssao_prepass, pipe);

          SDL_DrawGPUIndexedPrimitives (m_ssao_prepass,
                                        (Uint32)prim.indices.size (), 1,
                                        prim.first_index, 0, 0);
        }
      }
    }

    for (gfx::node &c : n.children) {
      draw_node (c);
    }
  };

  for (gfx::node &root : scene.roots) {
    draw_node (root);
  }
}

void
gfx::scene_renderer::run_ssao_pass (const glm::mat4 &proj)
{
  if (!ssao_enabled || (m_ssao_pipe == nullptr)
      || (m_ssao_normal_depth == nullptr) || (m_ssao_raw == nullptr)) {
    return;
  }

  glm::mat4 const inv_proj = glm::inverse (proj);

  struct alignas (16) ssao_proj_params
  {
    glm::mat4 proj;
    glm::mat4 inv_proj;
    glm::vec4 screen_noise_scale; // xy = screen/noise
    glm::vec4 radius_bias_power_intensity;
  } params{};

  params.proj = proj;
  params.inv_proj = inv_proj;
  params.screen_noise_scale = glm::vec4 (
      float (m_ssao_width) / 4.0F, float (m_ssao_height) / 4.0F, 0.0F, 0.0F);
  params.radius_bias_power_intensity
      = glm::vec4 (ssao_radius, ssao_bias, ssao_power, ssao_intensity);

  struct alignas (16) ssao_kernel_block
  {
    glm::vec4 samples[64];
  } kernel_block{};

  for (int i = 0; i < 64; ++i) {
    kernel_block.samples[i] = m_ssao_kernel[i];
  }

  SDL_GPUColorTargetInfo ct{};
  ct.texture = m_ssao_raw;
  ct.mip_level = 0;
  ct.layer_or_depth_plane = 0;
  ct.load_op = SDL_GPU_LOADOP_CLEAR;
  ct.store_op = SDL_GPU_STOREOP_STORE;
  ct.clear_color = SDL_FColor{ 1, 1, 1, 1 };
  ct.cycle = false;

  SDL_GPURenderPass *pass
      = SDL_BeginGPURenderPass (m_ctx->main_cmd, &ct, 1, nullptr);
  if ((m_ssao_pipe == nullptr) || (pass == nullptr)) {
    if (pass != nullptr) {
      SDL_EndGPURenderPass (pass);
    }
    return;
  }

  SDL_BindGPUGraphicsPipeline (pass, m_ssao_pipe);

  SDL_PushGPUFragmentUniformData (m_ctx->main_cmd, 0, &params, sizeof (params));
  SDL_PushGPUFragmentUniformData (m_ctx->main_cmd, 1, &kernel_block,
                                  sizeof (kernel_block));

  SDL_GPUTextureSamplerBinding samplers[2]{};
  samplers[0].texture = m_ssao_normal_depth;
  samplers[0].sampler = m_ssao_point_sampler;
  samplers[1].texture = m_ssao_noise_tex;
  samplers[1].sampler = m_ssao_point_sampler;

  SDL_BindGPUFragmentSamplers (pass, 0, samplers, 2);
  SDL_DrawGPUPrimitives (pass, 3, 1, 0, 0);
  SDL_EndGPURenderPass (pass);
}

void
gfx::scene_renderer::run_ssao_blur_pass ()
{
  if (!ssao_enabled || (m_ssao_blur_pipe == nullptr) || (m_ssao_raw == nullptr)
      || (m_ssao_blur == nullptr)) {
    return;
  }

  struct alignas (16) ssao_blur_params
  {
    glm::vec4 texel_size; // xy only
  } params{};
  params.texel_size = glm::vec4 (1.0F / float (m_ssao_width),
                                 1.0F / float (m_ssao_height), 0.0F, 0.0F);

  SDL_GPUColorTargetInfo ct{};
  ct.texture = m_ssao_blur;
  ct.mip_level = 0;
  ct.layer_or_depth_plane = 0;
  ct.load_op = SDL_GPU_LOADOP_CLEAR;
  ct.store_op = SDL_GPU_STOREOP_STORE;
  ct.clear_color = SDL_FColor{ 1, 1, 1, 1 };
  ct.cycle = false;

  SDL_GPURenderPass *pass
      = SDL_BeginGPURenderPass (m_ctx->main_cmd, &ct, 1, nullptr);
  if ((m_ssao_blur_pipe == nullptr) || (pass == nullptr)) {
    if (pass != nullptr) {
      SDL_EndGPURenderPass (pass);
    }
    return;
  }

  SDL_BindGPUGraphicsPipeline (pass, m_ssao_blur_pipe);
  SDL_PushGPUFragmentUniformData (m_ctx->main_cmd, 0, &params, sizeof (params));

  SDL_GPUTextureSamplerBinding bind{};
  bind.texture = m_ssao_raw;
  bind.sampler = m_ssao_linear_sampler;
  SDL_BindGPUFragmentSamplers (pass, 0, &bind, 1);

  SDL_DrawGPUPrimitives (pass, 3, 1, 0, 0);
  SDL_EndGPURenderPass (pass);
}

void
gfx::scene_renderer::create_outline_pipeline ()
{
  auto vert_id = m_res_mgr->register_shader (
      "engine://compiled_shaders/outline.vert.slang.spv");
  auto frag_id = m_res_mgr->register_shader (
      "engine://compiled_shaders/outline.frag.slang.spv");

  SDL_GPUShader *vert = gfx::shader::load_from_manager (
      m_ctx->gpu_device, m_res_mgr, vert_id, SDL_GPU_SHADERSTAGE_VERTEX, 2, 0);

  SDL_GPUShader *frag
      = gfx::shader::load_from_manager (m_ctx->gpu_device, m_res_mgr, frag_id,
                                        SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0);

  SDL_GPUGraphicsPipelineCreateInfo pipe{};
  SDL_zero (pipe);

  pipe.vertex_shader = vert;
  pipe.fragment_shader = frag;
  pipe.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

  pipe.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
  pipe.depth_stencil_state.enable_depth_test = true;
  pipe.depth_stencil_state.enable_depth_write = false;
  pipe.depth_stencil_state.enable_stencil_test = false;

  pipe.target_info.has_depth_stencil_target = true;
  pipe.target_info.depth_stencil_format = m_window->depth_format ();

  SDL_GPUColorTargetDescription ctd[2]{};
  ctd[0].format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
  ctd[1].format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;

  pipe.target_info.num_color_targets = 2;
  pipe.target_info.color_target_descriptions = ctd;

  pipe.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;

  pipe.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_4;
  pipe.multisample_state.sample_mask = 0;
  pipe.multisample_state.enable_mask = false;

  configure_mesh_vertex_input (pipe);

  // IMPORTANT:
  // front-face culling so only expanded backfaces remain visible
  pipe.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_FRONT;
  m_pipeline_outline = SDL_CreateGPUGraphicsPipeline (m_ctx->gpu_device, &pipe);

  // If mesh is double-sided you can also allow no culling, but for outlines
  // front cull is usually still the best.
  pipe.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_FRONT;
  m_pipeline_outline_double_sided
      = SDL_CreateGPUGraphicsPipeline (m_ctx->gpu_device, &pipe);

  // Skinned outline. This shader needs 3 vertex uniform buffers: matrices,
  // outline params, and the joint palette.
  if (SDL_GPUShader *skinned_vert = create_skinned_vertex_shader (
          "engine://compiled_shaders/outline_skinned.vert.slang.spv", 3);
      skinned_vert != nullptr) {
    pipe.vertex_shader = skinned_vert;

    pipe.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_FRONT;
    m_pipeline_outline_skinned
        = SDL_CreateGPUGraphicsPipeline (m_ctx->gpu_device, &pipe);

    m_pipeline_outline_skinned_double_sided
        = SDL_CreateGPUGraphicsPipeline (m_ctx->gpu_device, &pipe);

    SDL_ReleaseGPUShader (m_ctx->gpu_device, skinned_vert);
  }

  SDL_ReleaseGPUShader (m_ctx->gpu_device, vert);
  SDL_ReleaseGPUShader (m_ctx->gpu_device, frag);
}

void
gfx::scene_renderer::destroy_outline_pipeline ()
{
  if (m_pipeline_outline != nullptr) {
    SDL_ReleaseGPUGraphicsPipeline (m_ctx->gpu_device, m_pipeline_outline);
    m_pipeline_outline = nullptr;
  }

  if (m_pipeline_outline_double_sided != nullptr) {
    SDL_ReleaseGPUGraphicsPipeline (m_ctx->gpu_device,
                                    m_pipeline_outline_double_sided);
    m_pipeline_outline_double_sided = nullptr;
  }
  if (m_pipeline_outline_skinned != nullptr) {
    SDL_ReleaseGPUGraphicsPipeline (m_ctx->gpu_device,
                                    m_pipeline_outline_skinned);
    m_pipeline_outline_skinned = nullptr;
  }
  if (m_pipeline_outline_skinned_double_sided != nullptr) {
    SDL_ReleaseGPUGraphicsPipeline (m_ctx->gpu_device,
                                    m_pipeline_outline_skinned_double_sided);
    m_pipeline_outline_skinned_double_sided = nullptr;
  }
}

void
gfx::scene_renderer::draw_model_outline (gfx::model_3d &model,
                                         size_t scene_index,
                                         const glm::mat4 &model_matrix,
                                         const glm::mat4 &view_proj,
                                         const joint_palette *palette)
{
  m_active_palette = palette;
  if (scene_index >= model.scenes.size ()) {
    return;
  }

  model.ensure_gpu_buffers (m_ctx);
  model.bind (m_ctx->main_pass);

  gfx::scene &scene = model.scenes[scene_index];

  for (gfx::node &root : scene.roots) {
    update_node_world (root, model_matrix);
  }

  std::function<void (gfx::node &)> draw_node = [&] (gfx::node &n) {
    if (!n.mesh_lods.empty ()) {
      gfx::mesh const *m = select_lod (n);
      if (m) {
        struct alignas (16) outline_matrices
        {
          glm::mat4 model;
          glm::mat4 viewproj;
          glm::mat4 normal;
        };

        glm::mat3 n3
            = glm::transpose (glm::inverse (glm::mat3 (n.world_transform)));

        glm::mat4 n4 (1.0F);
        n4[0] = glm::vec4 (n3[0], 0.0F);
        n4[1] = glm::vec4 (n3[1], 0.0F);
        n4[2] = glm::vec4 (n3[2], 0.0F);

        outline_matrices mats{ n.world_transform, view_proj, n4 };
        SDL_PushGPUVertexUniformData (m_ctx->main_cmd, 0, &mats, sizeof (mats));

        struct alignas (16) outline_params
        {
          glm::vec4 color;
          glm::vec4 params; // x = width
        } outline{};

        outline.color = outline_color;
        outline.params = glm::vec4 (outline_width, 0.0F, 0.0F, 0.0F);

        SDL_PushGPUVertexUniformData (m_ctx->main_cmd, 1, &outline,
                                      sizeof (outline));
        SDL_PushGPUFragmentUniformData (m_ctx->main_cmd, 0, &outline,
                                        sizeof (outline));
        // The outline pass keeps its own params in slot 1, so the skinning
        // palette binds slot 2 here (see WEASEL_PALETTE_BINDING).
        push_active_palette (m_ctx->main_cmd, 2);

        for (const primitive &prim : m->primitives) {
          SDL_GPUGraphicsPipeline *pipe = nullptr;
          if (m_active_palette != nullptr) {
            pipe = prim.mat.double_sided
                       ? m_pipeline_outline_skinned_double_sided
                       : m_pipeline_outline_skinned;
          } else {
            pipe = prim.mat.double_sided ? m_pipeline_outline_double_sided
                                         : m_pipeline_outline;
          }
          if (!pipe || !m_ctx->main_pass) {
            continue;
          }

          SDL_BindGPUGraphicsPipeline (m_ctx->main_pass, pipe);

          SDL_DrawGPUIndexedPrimitives (m_ctx->main_pass,
                                        (Uint32)prim.indices.size (), 1,
                                        prim.first_index, 0, 0);
        }
      }
    }

    for (gfx::node &child : n.children) {
      draw_node (child);
    }
  };

  for (gfx::node &root : scene.roots) {
    draw_node (root);
    m_active_palette = nullptr;
  }
}

void
gfx::scene_renderer::create_grid_pipeline ()
{
  auto vert_id = m_res_mgr->register_shader (
      "engine://compiled_shaders/grid.vert.slang.spv");
  auto frag_id = m_res_mgr->register_shader (
      "engine://compiled_shaders/grid.frag.slang.spv");

  SDL_GPUShader *vert = gfx::shader::load_from_manager (
      m_ctx->gpu_device, m_res_mgr, vert_id, SDL_GPU_SHADERSTAGE_VERTEX, 1, 0);

  SDL_GPUShader *frag
      = gfx::shader::load_from_manager (m_ctx->gpu_device, m_res_mgr, frag_id,
                                        SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0);

  if ((vert == nullptr) || (frag == nullptr)) {
    if (vert != nullptr) {
      SDL_ReleaseGPUShader (m_ctx->gpu_device, vert);
    }
    if (frag != nullptr) {
      SDL_ReleaseGPUShader (m_ctx->gpu_device, frag);
    }
    return;
  }

  SDL_GPUGraphicsPipelineCreateInfo pipe{};
  SDL_zero (pipe);

  pipe.vertex_shader = vert;
  pipe.fragment_shader = frag;
  pipe.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

  pipe.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
  pipe.depth_stencil_state.enable_depth_test = true;
  pipe.depth_stencil_state.enable_depth_write = false;
  pipe.depth_stencil_state.enable_stencil_test = false;

  pipe.target_info.num_color_targets = 2;
  SDL_GPUColorTargetDescription ctd[2]{};
  ctd[0].format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
  ctd[0].blend_state.enable_blend = true;
  ctd[0].blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
  ctd[0].blend_state.dst_color_blendfactor
      = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
  ctd[0].blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
  ctd[0].blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
  ctd[0].blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
  ctd[0].blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

  ctd[1].format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
  ctd[1].blend_state.enable_blend = false;

  pipe.target_info.color_target_descriptions = ctd;
  pipe.target_info.has_depth_stencil_target = true;
  pipe.target_info.depth_stencil_format = m_window->depth_format ();

  pipe.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
  pipe.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;

  pipe.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_4;

  // The grid is a "Projected Grid": it renders a full-screen triangle and
  // un-projects pixels to the Y=0 plane. This avoids all precision and
  // breaking issues common with massive planes.
  pipe.vertex_input_state.num_vertex_buffers = 0;
  pipe.vertex_input_state.num_vertex_attributes = 0;

  m_pipeline_grid = SDL_CreateGPUGraphicsPipeline (m_ctx->gpu_device, &pipe);

  SDL_ReleaseGPUShader (m_ctx->gpu_device, vert);
  SDL_ReleaseGPUShader (m_ctx->gpu_device, frag);
}

void
gfx::scene_renderer::destroy_grid_pipeline ()
{
  if (m_pipeline_grid != nullptr) {
    SDL_ReleaseGPUGraphicsPipeline (m_ctx->gpu_device, m_pipeline_grid);
    m_pipeline_grid = nullptr;
  }
}

void
gfx::scene_renderer::draw_grid (const glm::vec3 & /*unused*/,
                                const glm::vec3 &fog_center, float fog_radius)
{
  if ((m_pipeline_grid == nullptr) || (m_ctx->main_pass == nullptr)) {
    return;
  }

  SDL_BindGPUGraphicsPipeline (m_ctx->main_pass, m_pipeline_grid);

  /**
   * VS Data: space1, slot 0
   * Centers the grid plane on the gaze point and scales it to the radius.
   */
  struct alignas (16) scene_data
  {
    glm::mat4 view_proj;
    glm::vec3 fog_center;
    float fog_radius;
  } sd{ m_active_view.view_proj, fog_center, fog_radius };

  SDL_PushGPUVertexUniformData (m_ctx->main_cmd, 0, &sd, sizeof (sd));

  /**
   * FS Data: space3, slot 0
   * Configures the grid appearance and Fog of War parameters.
   */
  struct alignas (16) grid_data
  {
    glm::vec3 fog_center;
    float fog_radius;
    glm::vec4 x_axis_color;
    glm::vec4 z_axis_color;
    glm::vec4 grid_color;
    glm::vec4 major_grid_color;
  } gd{ fog_center,
        fog_radius,
        glm::vec4 (0.5F, 0.0F, 0.0F, 1.0F),
        glm::vec4 (0.0F, 0.0F, 0.5F, 1.0F),
        glm::vec4 (0.04F, 0.04F, 0.04F, 1.0F),
        glm::vec4 (0.07F, 0.07F, 0.07F, 1.0F) };

  SDL_PushGPUFragmentUniformData (m_ctx->main_cmd, 0, &gd, sizeof (gd));

  // The vertex shader generates the 3D ground plane from SV_VertexID.
  SDL_DrawGPUPrimitives (m_ctx->main_pass, 6, 1, 0, 0);
}

} // namespace wsl
