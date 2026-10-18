#include "batch_renderer_2d.hpp"
#include "render_context.hpp"
#include "render_window.hpp"
#include "shader.hpp"
#include "wsl/rsc/resource_manager.hpp"
#include <algorithm>

namespace wsl::gfx
{

batch_renderer_2d::batch_renderer_2d (wsl::gfx::render_window &window,
                                      render_context *ctx,
                                      wsl::rsc::resource_manager *res_mgr)
    : renderer (window, ctx, res_mgr)
{
  create_pipeline ();
  create_buffers ();

  m_vertices.reserve (max_vertices);
}

batch_renderer_2d::~batch_renderer_2d ()
{
  destroy_pipeline ();
  destroy_buffers ();
}

void
batch_renderer_2d::submit (const draw_command &cmd)
{
  m_queue.push_back (cmd);
}

void
batch_renderer_2d::set_projection (const glm::mat4 &proj)
{
  m_override_projection = proj;
}

std::vector<batch_renderer_2d::draw_command>
batch_renderer_2d::snapshot_queue () const
{
  return m_queue;
}

void
batch_renderer_2d::restore_queue (const std::vector<draw_command> &cmds)
{
  m_queue = cmds;
}

void
batch_renderer_2d::build_and_upload ()
{
  m_batches.clear ();
  if (m_queue.empty ()) {
    return;
  }

  // Sort by z-index then image. Stable so equal keys preserve submission
  // order: viewport overlays are submitted in hierarchy order, which is the
  // documented tie-break for equal `display_order` (D11).
  std::stable_sort (m_queue.begin (), m_queue.end (),
                    [] (const draw_command &lhs, const draw_command &rhs) {
                      if (lhs.z_index != rhs.z_index) {
                        return lhs.z_index < rhs.z_index;
                      }
                      return lhs.image.value < rhs.image.value;
                    });

  uint32_t window_width;
  uint32_t window_height;
  m_window->get_size (window_width, window_height);
  m_projection = m_override_projection.has_value ()
                     ? *m_override_projection
                     : glm::ortho (0.0F, (float)window_width,
                                   (float)window_height, 0.0F, -1.0F, 1.0F);
  m_override_projection.reset ();

  m_vertices.clear ();

  SDL_GPUTexture *current_texture = nullptr;
  bool current_nearest = false;
  uint32_t batch_start = 0;

  for (const auto &cmd : m_queue) {
    SDL_GPUTexture *tex = nullptr;
    if (cmd.texture_override != nullptr) {
      tex = cmd.texture_override;
    } else {
      auto img_handle = m_res_mgr->get (cmd.image);
      tex = img_handle ? img_handle->texture.get () : nullptr;
    }

    if (tex != current_texture || cmd.nearest_filter != current_nearest
        || m_vertices.size () + 6 > max_vertices) {
      if (current_texture != nullptr || !m_vertices.empty ()) {
        m_batches.push_back ({ current_texture, batch_start,
                               (uint32_t)m_vertices.size () - batch_start,
                               current_nearest });
      }
      current_texture = tex;
      current_nearest = cmd.nearest_filter;
      batch_start = (uint32_t)m_vertices.size ();

      if (m_vertices.size () + 6 > max_vertices) {
        break;
      }
    }

    // Quad vertices with rotation support
    float const pos_x = cmd.position.x;
    float const pos_y = cmd.position.y;
    float const size_w = cmd.size.x;
    float const size_h = cmd.size.y;

    float const half_sw = size_w * 0.5F;
    float const half_sh = size_h * 0.5F;

    glm::vec2 const corners[4] = { { -half_sw, -half_sh },
                                   { half_sw, -half_sh },
                                   { half_sw, half_sh },
                                   { -half_sw, half_sh } };

    glm::vec2 rotated_corners[4];
    float const cos_r = std::cos (cmd.rotation);
    float const sin_r = std::sin (cmd.rotation);

    for (int i = 0; i < 4; ++i) {
      rotated_corners[i].x
          = pos_x + half_sw + (corners[i].x * cos_r - corners[i].y * sin_r);
      rotated_corners[i].y
          = pos_y + half_sh + (corners[i].x * sin_r + corners[i].y * cos_r);
    }

    glm::vec2 uv0 = cmd.uv_offset;
    glm::vec2 uv1 = cmd.uv_offset + cmd.uv_scale;

    if (cmd.flip_h) {
      std::swap (uv0.x, uv1.x);
    }
    if (cmd.flip_v) {
      std::swap (uv0.y, uv1.y);
    }

    m_vertices.push_back ({ rotated_corners[0], { uv0.x, uv0.y }, cmd.color });
    m_vertices.push_back ({ rotated_corners[1], { uv1.x, uv0.y }, cmd.color });
    m_vertices.push_back ({ rotated_corners[2], { uv1.x, uv1.y }, cmd.color });

    m_vertices.push_back ({ rotated_corners[0], { uv0.x, uv0.y }, cmd.color });
    m_vertices.push_back ({ rotated_corners[2], { uv1.x, uv1.y }, cmd.color });
    m_vertices.push_back ({ rotated_corners[3], { uv0.x, uv1.y }, cmd.color });
  }

  if (!m_vertices.empty ()) {
    m_batches.push_back ({ current_texture, batch_start,
                           (uint32_t)m_vertices.size () - batch_start });
  }

  // Upload vertices
  if (!m_vertices.empty ()) {
    void *mapped = SDL_MapGPUTransferBuffer (m_ctx->gpu_device,
                                             m_vbo_transfer.get (), true);
    std::memcpy (mapped, m_vertices.data (),
                 m_vertices.size () * sizeof (vertex_2d));
    SDL_UnmapGPUTransferBuffer (m_ctx->gpu_device, m_vbo_transfer.get ());

    SDL_GPUCommandBuffer *cmd_buf = m_ctx->command_buffer ();
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass (cmd_buf);
    SDL_GPUTransferBufferLocation src{ m_vbo_transfer.get (), 0 };
    SDL_GPUBufferRegion dst{
      m_vbo.get (), 0, (uint32_t)(m_vertices.size () * sizeof (vertex_2d))
    };
    SDL_UploadToGPUBuffer (copy, &src, &dst, false);
    SDL_EndGPUCopyPass (copy);
  }
}

void
batch_renderer_2d::draw ()
{
  if (m_batches.empty ()) {
    m_queue.clear ();
    return;
  }

  SDL_GPURenderPass *pass = m_ctx->main_render_pass ();
  if (pass == nullptr) {
    return;
  }

  SDL_BindGPUGraphicsPipeline (pass, m_pipeline.get ());
  SDL_GPUBufferBinding vbo_binding{ m_vbo.get (), 0 };
  SDL_BindGPUVertexBuffers (pass, 0, &vbo_binding, 1);

  // Push projection matrix
  SDL_PushGPUVertexUniformData (m_ctx->command_buffer (), 0, &m_projection,
                                sizeof (m_projection));

  for (const auto &batch : m_batches) {
    SDL_GPUTexture *tex
        = batch.texture ? batch.texture : m_window->hdr_scene ().get ();
    if (tex) {
      SDL_GPUSampler *sampler
          = batch.nearest_filter ? m_nearest_sampler.get () : m_sampler.get ();
      SDL_GPUTextureSamplerBinding tex_binding{ tex, sampler };
      SDL_BindGPUFragmentSamplers (pass, 0, &tex_binding, 1);
    }
    SDL_DrawGPUPrimitives (pass, batch.vertex_count, 1, batch.first_vertex, 0);
  }

  m_queue.clear ();
  m_batches.clear ();
  m_vertices.clear ();
}

void
batch_renderer_2d::flush ()
{
  build_and_upload ();
  draw ();
}

void
batch_renderer_2d::create_pipeline ()
{
  auto vert_id = m_res_mgr->register_shader (
      "engine://compiled_shaders/sprite_2d.vert.slang.spv");
  auto frag_id = m_res_mgr->register_shader (
      "engine://compiled_shaders/sprite_2d.frag.slang.spv");

  SDL_GPUShader *vert = shader::load_from_manager (
      m_ctx->gpu_device, m_res_mgr, vert_id, SDL_GPU_SHADERSTAGE_VERTEX, 1, 0);
  SDL_GPUShader *frag
      = shader::load_from_manager (m_ctx->gpu_device, m_res_mgr, frag_id,
                                   SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 1);

  if (!vert || !frag) {
    return;
  }

  SDL_GPUGraphicsPipelineCreateInfo pipe{};
  SDL_zero (pipe);
  pipe.vertex_shader = vert;
  pipe.fragment_shader = frag;
  pipe.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

  SDL_GPUColorTargetDescription ctd[1]{};
  ctd[0].format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
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

  pipe.target_info.num_color_targets = 1;
  pipe.target_info.color_target_descriptions = ctd;

  SDL_GPUVertexBufferDescription vbd[1]{};
  vbd[0].slot = 0;
  vbd[0].input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
  vbd[0].pitch = sizeof (vertex_2d);

  pipe.vertex_input_state.num_vertex_buffers = 1;
  pipe.vertex_input_state.vertex_buffer_descriptions = vbd;

  SDL_GPUVertexAttribute va[3]{};
  va[0].buffer_slot = 0;
  va[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
  va[0].location = 0;
  va[0].offset = offsetof (vertex_2d, pos);

  va[1].buffer_slot = 0;
  va[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
  va[1].location = 1;
  va[1].offset = offsetof (vertex_2d, uv);

  va[2].buffer_slot = 0;
  va[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
  va[2].location = 2;
  va[2].offset = offsetof (vertex_2d, color);

  pipe.vertex_input_state.num_vertex_attributes = 3;
  pipe.vertex_input_state.vertex_attributes = va;

  m_pipeline = gpu_graphics_pipeline (m_ctx->gpu_device, pipe);

  SDL_ReleaseGPUShader (m_ctx->gpu_device, vert);
  SDL_ReleaseGPUShader (m_ctx->gpu_device, frag);

  SDL_GPUSamplerCreateInfo sinfo{};
  sinfo.min_filter = SDL_GPU_FILTER_LINEAR;
  sinfo.mag_filter = SDL_GPU_FILTER_LINEAR;
  sinfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
  sinfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  sinfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  m_sampler = gpu_sampler (m_ctx->gpu_device, sinfo);

  SDL_GPUSamplerCreateInfo ninfo{};
  ninfo.min_filter = SDL_GPU_FILTER_NEAREST;
  ninfo.mag_filter = SDL_GPU_FILTER_NEAREST;
  ninfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
  ninfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  ninfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  m_nearest_sampler = gpu_sampler (m_ctx->gpu_device, ninfo);
}

void
batch_renderer_2d::destroy_pipeline ()
{
  m_pipeline.reset ();
  m_sampler.reset ();
  m_nearest_sampler.reset ();
}

void
batch_renderer_2d::create_buffers ()
{
  SDL_GPUBufferCreateInfo binfo{};
  binfo.size = max_vertices * sizeof (vertex_2d);
  binfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
  m_vbo = gpu_buffer (m_ctx->gpu_device, binfo);

  SDL_GPUTransferBufferCreateInfo tinfo{};
  tinfo.size = max_vertices * sizeof (vertex_2d);
  tinfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  m_vbo_transfer = gpu_transfer_buffer (m_ctx->gpu_device, tinfo);
}

void
batch_renderer_2d::destroy_buffers ()
{
  m_vbo.reset ();
  m_vbo_transfer.reset ();
}

} // namespace wsl::gfx
