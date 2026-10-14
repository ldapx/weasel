#pragma once

#include "wsl/gfx/render_window.hpp"
#include "clustered_lighting.hpp"
#include "cubemap.hpp"
#include "debug_overlay.hpp"
#include "lighting.hpp"
#include "mesh.hpp"
#include "model_3d.hpp"
#include "pipeline_cache.hpp"
#include "renderer.hpp"
#include "shader_program.hpp"
#include "wsl/rsc/resource_ids.hpp"

#ifndef IN_MODULE_INTERFACE
#include <SDL3/SDL_gpu.h>
#endif
#ifndef IN_MODULE_INTERFACE
#include <SDL3/SDL_pixels.h>
#endif

#ifndef IN_MODULE_INTERFACE
#include <entt/entity/fwd.hpp>
#endif

#ifndef IN_MODULE_INTERFACE
#include <array>
#endif
#ifndef IN_MODULE_INTERFACE
#include <cstddef>
#endif
#ifndef IN_MODULE_INTERFACE
#include <deque>
#endif
#ifndef IN_MODULE_INTERFACE
#include <span>
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

/**
 * Engine-facing 3D renderer.
 *
 * Owns GPU pipelines, per-frame submission state, and supporting passes such
 * as shadows, SSAO, skybox rendering, and outlines.
 */
class scene_renderer : public renderer
{
public:
  /** Camera state resolved for a single 3D frame. */
  struct view_state
  {
    /** True when a valid camera was resolved and the matrices can be used. */
    bool valid = false;
    /** Aspect ratio used to build `proj`. */
    float aspect_ratio = 1.0F;
    /** Camera origin in world space. */
    glm::vec3 world_position{ 0.0F };
    /** World-to-view transform. */
    glm::mat4 view{ 1.0F };
    /** View-to-clip transform. */
    glm::mat4 proj{ 1.0F };
    /** Cached `proj * view`. */
    glm::mat4 view_proj{ 1.0F };
  };

  /**
   * Model-space joint palette for one skinned instance.
   *
   * Mirrors the `JointPalette` cbuffer in rsc/shaders/skinning.slang:
   * `matrices` occupies the first `joint_count` slots, each 64 bytes, and
   * `count` follows immediately after. Pushing only the used prefix keeps the
   * per-draw cost proportional to the rig rather than always 8 KB.
   */
  struct joint_palette
  {
    static constexpr size_t max_joints = 128;

    std::vector<glm::mat4> matrices;
  };

  /**
   * Byte layout of the `JointPalette` uniform block, mirrored from
   * `rsc/shaders/skinning.slang`:
   *
   *   uint     u_JointCount;                     // offset 0
   *   float4x4 u_JointPalette[max_joints];       // offset 16, 64 B per entry
   *
   * std140 aligns the array to 16 bytes, hence the 16-byte gap after the count.
   *
   * The count leads deliberately. It used to trail the array at offset
   * `max_joints * 64`, and a RenderDoc capture of a live skinned draw showed
   * every palette matrix arriving intact while `u_JointCount` read back as 0
   * from that offset -- despite the CPU packing 24 there and uploading the
   * whole block in one call, with the SDK's push path memcpy'ing the full
   * length unclamped into a 32 KB pool buffer. The write is not truncated, so
   * the loss happens after the upload; keeping the scalar at the head of the
   * block puts it inside any plausible descriptor range rather than at the
   * tail.
   *
   * This value gates everything: read as 0, the shader's
   * `index < u_JointCount` test rejects all four influences, `skin_position`
   * returns 0 for every vertex, and the mesh collapses to the origin with no
   * API error.
   */
  struct joint_palette_layout
  {
    /** Byte offset of `u_JointCount`. Always 0 -- see the note above. */
    static constexpr size_t count_offset = 0;
    /** Byte offset of the first `u_JointPalette` entry (std140 alignment). */
    static constexpr size_t matrix_offset = 16;
    /** Total size of the uniform block in bytes. */
    static constexpr size_t total_bytes
        = matrix_offset + joint_palette::max_joints * sizeof (glm::mat4);
  };

  /**
   * Packs *palette* into *scratch* in the exact `JointPalette` block layout.
   *
   * `scratch` is grown to the full block size so the count always lands at
   * `count_offset`. The unused tail keeps its previous contents, which the
   * shader never reads because it bounds-checks against `u_JointCount`.
   *
   * @return Total bytes to upload.
   */
  static size_t pack_joint_palette (const joint_palette &palette,
                                    std::vector<uint8_t> &scratch);

  /** Single model submission reused by renderer passes. */
  struct draw_command
  {
    /** Model resource to draw. */
    gfx::model_3d *model = nullptr;
    /** Scene slot inside the model resource. */
    size_t scene_index = 0;
    /** Model-to-world transform. */
    glm::mat4 transform{ 1.0F };
    /** Source entity for selection and debug linkage. */
    entt::entity entity = entt::null;
    /** True when the submission should also receive an outline pass. */
    bool draw_outline = false;
    /** Per-instance texture mip LOD bias. */
    float mip_lod_bias = 0.0F;
    /** Per-instance geometry LOD aggressiveness bias. */
    float geometry_lod_bias = 0.0F;
    /** Per-instance max draw distance (0 = unlimited). */
    float visibility_range = 0.0F;
    /**
     * Optional per-instance material override. When set (non-null), the
     * renderer renders every primitive of this instance with the assigned
     * material instead of the model's own materials (consumed via
     * ``m_active_material_override`` in ``render_mesh``).
     */
    rsc::material_id material_override{};
    /**
     * Optional per-instance skinning palette. When set, the renderer binds
     * the skinned pipeline variant and uploads these model-space joint
     * matrices. Null for every static instance, which then keeps using the
     * plain variant that has no palette binding at all.
     */
    const joint_palette *palette = nullptr;
  };

  /** Creates the renderer and its long-lived GPU resources. */
  scene_renderer (wsl::gfx::render_window &window, render_context *ctx,
                  wsl::rsc::resource_manager *res_mgr);

  /** Releases GPU resources owned by the renderer. */
  ~scene_renderer () override;

  /**
   * When false, custom material paths are disabled and all meshes fall back
   * to the legacy PBR pipeline. Used for backwards compatibility / debugging.
   */
  bool enable_custom_materials = true;

  /** Begins a frame with resolved camera data. */
  void begin_frame (const view_state &view);
  /** Clears transient frame-local state. */
  void end_frame ();
  /** Returns true when the renderer currently owns valid frame state. */
  [[nodiscard]] auto has_active_frame () const -> bool;
  /** Returns the active frame view state. */
  [[nodiscard]] auto frame_view () const -> const view_state &;

  /** Replaces the visible draw list used by scene passes. */
  void set_visible_draws (std::vector<draw_command> draws);
  /** Returns the visible draw list for the active frame. */
  [[nodiscard]] auto visible_draws () const
      -> const std::vector<draw_command> &;
  /** Clears the visible draw list for the current frame. */
  void clear_visible_draws ();

  /** Sets the environment cubemap used by skybox and IBL rendering. */
  void set_environment (const gfx::cubemap *env);

  /** Binds the default lit main-scene pipeline. */
  void bind_main_pipeline ();
  /** Draws the current visible models. */
  void draw_visible_models ();
  /** Draws outlines for submissions that requested highlighting. */
  void draw_visible_model_outlines ();
  /** Builds SSAO from the current visible draw list. */
  void build_ssao_for_visible_models ();
  /** Draws the active environment cubemap as the current skybox. */
  void draw_active_environment (const glm::quat &skybox_rotation = glm::quat{});

  /**
   * Draws a procedural infinite grid at the origin.
   *
   * :param camera_pos: The world-space position of the camera.
   * :param fog_center: The world-space center of the visibility radius.
   * :param fog_radius: The world-space radius of the Fog of War.
   */
  void draw_grid (const glm::vec3 &camera_pos, const glm::vec3 &fog_center,
                  float fog_radius);

  /**
   * Immediate debug line drawing (world-space lines rendered with depth test
   * and depth writes enabled).
   */
  struct debug_vertex
  {
    glm::vec3 pos;
    glm::vec4 color;
  };
  void draw_debug_lines (const std::vector<debug_vertex> &verts,
                         const glm::mat4 &vp);

  /** How debug overlay geometry is composited into the active pass. */
  enum class overlay_blending : uint8_t
  {
    /** Blend by source alpha so the scene color shows through. */
    blend,
    /**
     * Write straight RGBA, replacing the destination. Use for targets that
     * store straight alpha and are composited later (e.g. model previews).
     */
    write,
  };

  /**
   * Draws screen-space triangles into the active pass, ignoring depth.
   *
   * Vertices are already in normalized device coordinates. The bloom source
   * target is always left untouched.
   *
   * :param verts: Overlay geometry to draw.
   * :param blending: How the geometry is composited.
   */
  void draw_debug_overlay (const std::vector<overlay_vertex> &verts,
                           overlay_blending blending = overlay_blending::blend);

  /**
   * Queues screen-space overlay geometry for the post-tonemap pass.
   *
   * Use for editor widgets whose colors must reach the final image
   * unchanged. The geometry is drawn by :cpp:func:`flush_debug_overlay` after
   * the scene has been tone mapped into the present texture, so it is not
   * affected by exposure, bloom, or gamma encoding.
   */
  void queue_debug_overlay (std::vector<overlay_vertex> verts);

  /**
   * Draws queued overlay geometry into the present texture.
   *
   * Call once per frame after postprocessing has run. Pending geometry is
   * discarded when the frame ends.
   */
  void flush_debug_overlay ();

  /** Uploads the frame lighting buffer. */
  void upload_lighting (const lighting_ubo &lighting);

  /**
   * Dispatches the clustered-light compute passes for the active frame.
   * `point_lights` holds world-space lights in `gpu_point_light` layout;
   * the renderer copies them into the GPU SSBO and assigns them to the
   * cluster grid. When `clustered_lighting` is disabled, the call is a
   * no-op and the lights stay in the UBO fallback path.
   */
  void run_clustered_lighting (std::span<const gpu_point_light> point_lights,
                               const glm::mat4 &view, float z_near,
                               float z_far);

  /** Returns true when the clustered-light compute passes are active. */
  [[nodiscard]] bool
  clustered_lighting_enabled () const
  {
    return m_clustered.is_active ();
  }

  /** Draws a model immediately using explicit transform data. */
  void draw_model (gfx::model_3d &model, size_t scene_index,
                   const glm::mat4 &model_matrix, const glm::mat4 &view_proj,
                   float mip_lod_bias = 0.0F, float geometry_lod_bias = 0.0F,
                   float visibility_range = 0.0F);

  /** Overrides the cached camera position used by shading. */
  void set_camera_position (const glm::vec3 &position);
  /** Returns the camera position used by shading. */
  [[nodiscard]] auto camera_position () const -> const glm::vec3 &;

  /** Returns the size of the active render target in pixels. */
  [[nodiscard]] auto render_target_size () const -> glm::vec2;

  /** Forces subsequent model draws to use the unlit material path. */
  void set_force_unlit (bool enabled);
  /** Returns true when the unlit override is active. */
  [[nodiscard]] auto is_force_unlit () const -> bool;

  /** Sets the world-space direction used by the directional shadow cascade. */
  void set_shadow_direction (const glm::vec3 &direction);
  /** Returns the world-space directional shadow direction. */
  [[nodiscard]] auto shadow_direction () const -> const glm::vec3 &;

  /** Enables or disables directional shadow rendering. */
  void set_directional_shadows_enabled (bool enabled);
  /** Returns true when directional shadows are enabled. */
  [[nodiscard]] auto directional_shadows_enabled () const -> bool;
  /** Rebuilds the directional shadow matrix from the active camera state. */
  void update_directional_shadow_view ();
  /** Returns the active directional light view-projection matrix. */
  [[nodiscard]] auto directional_shadow_view () const -> const glm::mat4 &;

  /** Returns the default shadow depth bias. */
  [[nodiscard]] auto shadow_map_bias () const -> float;
  /** Updates the default shadow depth bias. */
  void set_shadow_map_bias (float bias);
  /** Returns the default shadow strength multiplier. */
  [[nodiscard]] auto shadow_map_strength () const -> float;
  /** Updates the default shadow strength multiplier. */
  void set_shadow_map_strength (float strength);
  /** Returns the shadow map resolution used by renderer-owned targets. */
  [[nodiscard]] auto shadow_map_resolution () const -> uint32_t;

  /** Updates the intensity used by image-based lighting. */
  void set_ibl_intensity (float intensity);

  /** Returns the renderer-owned spot shadow slots. */
  auto spot_shadow_maps () -> std::array<shadow_map_2d, 4> &;
  /** Returns the renderer-owned spot shadow slots. */
  [[nodiscard]] auto spot_shadow_maps () const
      -> const std::array<shadow_map_2d, 4> &;

  /** Returns the renderer-owned point shadow slots. */
  auto point_shadow_maps () -> std::array<point_shadow_map, 2> &;
  /** Returns the renderer-owned point shadow slots. */
  [[nodiscard]] auto point_shadow_maps () const
      -> const std::array<point_shadow_map, 2> &;

  /** Builds a directional light matrix derived from the camera frustum. */
  static auto make_light_vp_from_camera (const glm::mat4 &cam_view,
                                         const glm::mat4 &cam_proj,
                                         const glm::vec3 &light_dir_world,
                                         float shadow_near = 0.1F,
                                         float shadow_far = 200.0F,
                                         float z_padding = 10.0F) -> glm::mat4;

  /** Builds a spotlight view-projection matrix for a single light. */
  static auto make_spot_light_vp (const glm::vec3 &light_pos,
                                  const glm::vec3 &light_dir,
                                  float outer_angle_radians, float near_plane,
                                  float far_plane) -> glm::mat4;

  /** Begins the directional shadow pass. */
  void begin_shadow_pass ();
  /** Ends the directional shadow pass. */
  void end_shadow_pass ();
  /**
   * Draws a model into the directional shadow map.
   * `palette` enables skinning for this draw; pass nullptr for static meshes.
   */
  void draw_model_shadow (gfx::model_3d &model, size_t scene_index,
                          const glm::mat4 &model_matrix,
                          const joint_palette *palette = nullptr);

  /** Begins a spotlight shadow pass for a renderer-owned slot. */
  void begin_spot_shadow_pass (int index);
  /** Ends the active spotlight shadow pass. */
  void end_spot_shadow_pass ();
  /** Draws a model into the active spotlight shadow pass. */
  void draw_model_spot_shadow (gfx::model_3d &model, size_t scene_index,
                               const glm::mat4 &model_matrix,
                               const glm::mat4 &light_vp);

  /** Begins a point-light cubemap shadow face pass. */
  void begin_point_shadow_pass (int index, int face);
  /** Ends the active point-light cubemap shadow face pass. */
  void end_point_shadow_pass ();
  /** Draws a model into a point-light shadow cubemap face. */
  void draw_model_point_shadow (gfx::model_3d &model, size_t scene_index,
                                const glm::mat4 &model_matrix,
                                const glm::mat4 &light_vp_mat,
                                const glm::vec3 &light_pos, float far_plane,
                                const joint_palette *palette = nullptr);

  /** Returns the view-projection matrix for a point-light cubemap face. */
  static auto make_point_light_view_proj (const glm::vec3 &light_pos, int face,
                                          float near_plane, float far_plane)
      -> glm::mat4;

  /** Bakes irradiance, prefilter, and BRDF resources for a cubemap. */
  void bake_ibl (gfx::cubemap &env);
  /** Bakes an equirectangular 2D texture into a cubemap. */
  void bake_equirect_to_cube (gfx::cubemap &env, SDL_GPUTexture *equi_tex);
  /** Bakes the procedural skybox into a cubemap. */
  void bake_procedural_skybox (gfx::cubemap &env, const glm::vec3 &sun_dir);
  /** Binds the preview background pipeline used by asset previews. */
  void bind_preview_bg_pipeline (SDL_GPURenderPass *pass);

  /** Default clear color used by renderer-owned full-screen passes. */
  SDL_FColor clear_color{ .r = 0.1F, .g = 0.15F, .b = 0.2F, .a = 1.0F };
  /** Enables or disables SSAO generation. */
  bool ssao_enabled = true;
  /** Sampling radius in view space for SSAO. */
  float ssao_radius = 0.6F;
  /** Bias applied to SSAO depth comparisons. */
  float ssao_bias = 0.025F;
  /** Exponent applied to SSAO output. */
  float ssao_power = 1.25F;
  /** Final multiplier applied to SSAO contribution. */
  float ssao_intensity = 1.0F;
  /** Brightness threshold where bloom starts contributing. */
  float bloom_threshold = 1.0F;
  /** Soft transition width around the bloom threshold. */
  float bloom_knee = 0.5F;
  /** Final bloom contribution multiplier. */
  float bloom_intensity = 1.0F;
  /** Color used by the outline pass. */
  glm::vec4 outline_color{ 1.0F, 0.65F, 0.1F, 1.0F };
  /** Expansion distance used by the outline pass. */
  float outline_width = 0.035F;

private:
  // High-level scene drawing helpers.
  static auto extract_position (const glm::mat4 &matrix) -> glm::vec3;
  auto select_lod (gfx::node &n, float geometry_lod_bias = 0.0F) const
      -> gfx::mesh *;
  void bind_pipeline ();
  void render_mesh (const glm::mat4 &model, const glm::mat4 &view_proj,
                    const mesh &mesh, float mip_lod_bias = 0.0F);
  void render_custom_primitive (const primitive &prim, float mip_lod_bias);
  /**
   * Renders a primitive using an explicit material instance (used both for
   * per-mesh custom materials and for per-instance material overrides).
   */
  void render_custom_primitive (const primitive &prim,
                                const gfx::material_instance &mat_inst,
                                float mip_lod_bias);
  void render_node (gfx::node &n, const glm::mat4 &view_proj,
                    float mip_lod_bias = 0.0F, float geometry_lod_bias = 0.0F,
                    float visibility_range = 0.0F);
  void render_scene (gfx::scene &scene, const glm::mat4 &view_proj,
                     float mip_lod_bias = 0.0F, float geometry_lod_bias = 0.0F,
                     float visibility_range = 0.0F);
  void update_node_world (gfx::node &n, const glm::mat4 &parent);

  // Main scene resource management.
  void create_depth_texture (uint32_t width, uint32_t height);
  void create_pipeline ();
  void destroy_pipeline ();
  void create_unlit_pipeline ();
  void destroy_unlit_pipeline ();
  void create_default_texture ();
  void destroy_default_resources ();

  // Environment and preview helpers.
  void create_skybox_pipeline ();
  void bind_skybox_pipeline ();
  void draw_skybox (const gfx::cubemap &cubemap, const glm::mat4 &view,
                    const glm::mat4 &proj,
                    const glm::quat &skybox_rotation = glm::quat{});
  void create_preview_bg_pipeline ();
  void destroy_preview_bg_pipeline ();
  void create_ibl_pipelines ();
  void destroy_ibl_pipelines ();

  // Shadow resource management and internal helpers.
  void create_shadow_resources (uint32_t size = 2048);
  void destroy_shadow_resources ();
  void render_shadow_map (gfx::scene &scene);
  void create_shadow_pipeline ();
  void create_point_shadow_pipeline ();
  void draw_model_shadow (gfx::model_3d &model, size_t scene_index,
                          const glm::mat4 &model_matrix,
                          SDL_GPURenderPass *pass, SDL_GPUCommandBuffer *cmd);

  // SSAO resource management and internal helpers.
  void create_ssao_resources (uint32_t width, uint32_t height);
  void destroy_ssao_resources ();
  void begin_ssao_prepass (const glm::mat4 &view, const glm::mat4 &proj);
  void end_ssao_prepass ();
  void draw_model_ssao (gfx::model_3d &model, size_t scene_index,
                        const glm::mat4 &model_matrix, const glm::mat4 &view,
                        const glm::mat4 &proj);
  void run_ssao_pass (const glm::mat4 &proj);
  void run_ssao_blur_pass ();
  void create_ssao_pipeline ();
  void destroy_ssao_pipeline ();
  void create_ssao_noise_texture (SDL_GPUCommandBuffer *cmd = nullptr);
  void create_ssao_kernel ();

  // Outline resource management and internal helpers.
  void create_outline_pipeline ();
  void destroy_outline_pipeline ();
  void draw_model_outline (gfx::model_3d &model, size_t scene_index,
                           const glm::mat4 &model_matrix,
                           const glm::mat4 &view_proj,
                           const joint_palette *palette = nullptr);

  // Grid helpers.
  void create_grid_pipeline ();
  void destroy_grid_pipeline ();

  view_state m_active_view{};
  std::vector<draw_command> m_visible_draws;
  const gfx::cubemap *m_active_env = nullptr;
  glm::vec3 m_camera_pos{ 0.0F, 0.0F, 0.0F };
  bool m_force_unlit = false;

  // CPU-side scratch storage used while building and traversing scenes.
  std::deque<mesh> m_meshes;
  std::vector<vertex> m_vertices;
  std::vector<uint32_t> m_indices;
  SDL_GPUTransferBuffer *m_vertex_transfer_buffer = nullptr;
  SDL_GPUTransferBuffer *m_index_transfer_buffer = nullptr;

  // Main scene pipelines.
  /** Pipeline set: one variant per alpha mode (opaque/mask/blend). */
  struct pipeline_set
  {
    SDL_GPUGraphicsPipeline *opaque = nullptr; // no blend, depth write ON
    SDL_GPUGraphicsPipeline *mask = nullptr;   // alpha-to-cov, depth write ON
    SDL_GPUGraphicsPipeline *blend = nullptr;  // alpha blend, depth write OFF
  };

  pipeline_set m_pipelines;                    // lit, single-sided
  pipeline_set m_pipelines_double_sided;       // lit, double-sided
  pipeline_set m_pipelines_unlit;              // unlit, single-sided
  pipeline_set m_pipelines_unlit_double_sided; // unlit, double-sided

  /**
   * Skinned counterparts of the main scene pipelines.
   *
   * These are built from the `_skinned` vertex shaders, which declare the
   * JointPalette cbuffer, so a pipeline that lacks it cannot be used for a
   * draw that uploads a palette (and vice versa). Static meshes keep using
   * the plain sets above and never pay for the binding.
   */
  pipeline_set m_pipelines_skinned;                    // lit, single-sided
  pipeline_set m_pipelines_skinned_double_sided;       // lit, double-sided
  pipeline_set m_pipelines_skinned_unlit;              // unlit, single-sided
  pipeline_set m_pipelines_skinned_unlit_double_sided; // unlit, double-sided

  // Aliases of `m_pipelines.opaque` / `m_pipelines_double_sided.opaque`, kept
  // for the bind paths. These are NOT owners: the corresponding pipeline_set
  // holds the only owning pointer, and destroy_pipeline() releases it there.
  // Never release a pipeline through one of these.
  SDL_GPUGraphicsPipeline *m_pipeline = nullptr;
  SDL_GPUGraphicsPipeline *m_pipeline_double_sided = nullptr;

  // Shared material fallback resources.
  SDL_GPUTexture *m_default_basecolor_tex = nullptr;
  SDL_GPUTexture *m_default_mr_tex = nullptr;
  SDL_GPUTexture *m_default_normal_tex = nullptr;
  SDL_GPUTexture *m_default_emissive_tex = nullptr;
  SDL_GPUTexture *m_default_cubemap_tex = nullptr;
  SDL_GPUTexture *m_default_brdf_lut_tex = nullptr;
  SDL_GPUSampler *m_default_sampler = nullptr;

  // Environment / skybox / IBL resources.
  SDL_GPUGraphicsPipeline *m_skybox_pipeline = nullptr;
  SDL_GPUTexture *m_skybox_cubemap = nullptr;
  SDL_GPUSampler *m_skybox_sampler = nullptr;
  SDL_GPUBuffer *m_skybox_vbo = nullptr;
  SDL_GPUGraphicsPipeline *m_pipeline_preview_bg = nullptr;
  SDL_GPUGraphicsPipeline *m_ibl_irradiance_pipe = nullptr;
  SDL_GPUGraphicsPipeline *m_ibl_prefilter_pipe = nullptr;
  SDL_GPUGraphicsPipeline *m_ibl_brdf_lut_pipe = nullptr;
  SDL_GPUGraphicsPipeline *m_equi_to_cube_pipe = nullptr;
  SDL_GPUGraphicsPipeline *m_procedural_skybox_pipe = nullptr;
  SDL_GPUSampler *m_ibl_sampler = nullptr;
  glm::vec3 m_last_baked_sun_dir{ 0, 0, 0 };
  float m_ibl_intensity = 1.0F;
  float m_prefilter_max_mip = 0.0F;

  // Shadow state and resources.
  static constexpr int max_shadowed_spots = 4;
  static constexpr int max_shadowed_points = 2;
  glm::vec3 m_shadow_dir = glm::vec3 (0, -1, 0);
  bool m_shadows_enabled = true;
  glm::mat4 m_light_vp = glm::mat4 (1.0F);
  float m_shadow_bias = 0.0025F;
  float m_shadow_strength = 1.0F;
  uint32_t m_shadow_size = 2048;
  std::array<shadow_map_2d, 4> m_spot_shadows{};
  std::array<point_shadow_map, 2> m_point_shadows{};
  shadow_map_2d m_dir_shadow{};
  SDL_GPUGraphicsPipeline *m_shadow_pipe = nullptr;
  SDL_GPUGraphicsPipeline *m_shadow_pipe_double_sided = nullptr;
  SDL_GPUGraphicsPipeline *m_point_shadow_pipe = nullptr;
  SDL_GPUGraphicsPipeline *m_point_shadow_pipe_double_sided = nullptr;
  // Skinned shadow variants, so animated meshes cast deforming shadows.
  SDL_GPUGraphicsPipeline *m_shadow_pipe_skinned = nullptr;
  SDL_GPUGraphicsPipeline *m_shadow_pipe_skinned_double_sided = nullptr;
  SDL_GPUGraphicsPipeline *m_point_shadow_pipe_skinned = nullptr;
  SDL_GPUGraphicsPipeline *m_point_shadow_pipe_skinned_double_sided = nullptr;
  SDL_GPUTexture *m_shadow_depth = nullptr;
  SDL_GPUSampler *m_shadow_sampler = nullptr;
  SDL_GPUCommandBuffer *m_shadow_cmd = nullptr;
  SDL_GPURenderPass *m_shadow_pass = nullptr;

  // SSAO state and resources.
  SDL_GPUGraphicsPipeline *m_ssao_prepass_pipe = nullptr;
  SDL_GPUGraphicsPipeline *m_ssao_prepass_pipe_double_sided = nullptr;
  // Skinned SSAO prepass, so occlusion follows the deformed surface.
  SDL_GPUGraphicsPipeline *m_ssao_prepass_pipe_skinned = nullptr;
  SDL_GPUGraphicsPipeline *m_ssao_prepass_pipe_skinned_double_sided = nullptr;
  SDL_GPUGraphicsPipeline *m_ssao_pipe = nullptr;
  SDL_GPUGraphicsPipeline *m_ssao_blur_pipe = nullptr;
  SDL_GPUTexture *m_ssao_normal_depth = nullptr;
  SDL_GPUTexture *m_ssao_depth = nullptr;
  SDL_GPUTexture *m_ssao_raw = nullptr;
  SDL_GPUTexture *m_ssao_blur = nullptr;
  SDL_GPUTexture *m_ssao_noise_tex = nullptr;
  SDL_GPUSampler *m_ssao_linear_sampler = nullptr;
  SDL_GPUSampler *m_ssao_point_sampler = nullptr;
  SDL_GPURenderPass *m_ssao_prepass = nullptr;
  uint32_t m_ssao_width = 0;
  uint32_t m_ssao_height = 0;
  std::array<glm::vec4, 64> m_ssao_kernel{};

  // Outline resources.
  SDL_GPUGraphicsPipeline *m_pipeline_outline = nullptr;
  SDL_GPUGraphicsPipeline *m_pipeline_outline_double_sided = nullptr;
  // Skinned outline, so selection highlight tracks the deformation. Its
  // palette binds vertex uniform slot 2 (slot 1 is the outline params).
  SDL_GPUGraphicsPipeline *m_pipeline_outline_skinned = nullptr;
  SDL_GPUGraphicsPipeline *m_pipeline_outline_skinned_double_sided = nullptr;

  // Debug line pipeline (for editor gizmos)
  SDL_GPUGraphicsPipeline *m_pipeline_debug_lines = nullptr;

  // Screen-space debug overlay pipeline (for editor overlays)
  SDL_GPUGraphicsPipeline *m_pipeline_debug_overlay = nullptr;
  // Straight-alpha variant used when the target is composited later.
  SDL_GPUGraphicsPipeline *m_pipeline_debug_overlay_write = nullptr;
  // Post-tonemap variant for the LDR present texture.
  SDL_GPUGraphicsPipeline *m_pipeline_debug_overlay_ldr = nullptr;

  /** Overlay geometry waiting for the post-tonemap pass. */
  std::vector<overlay_vertex> m_debug_overlay_queue;

  // Grid resources.
  /** Graphics pipeline for the procedural projected grid. */
  SDL_GPUGraphicsPipeline *m_pipeline_grid = nullptr;

  /**
   * Clustered forward lighting. Owns the SSBOs and compute pipelines
   * used by `cube.frag.slang` to shade point lights in clusters.
   */
  clustered_lighting m_clustered;

  /** Pipeline cache for custom material permutations. */
  pipeline_cache m_pipeline_cache;

  /**
   * Per-instance material override currently in effect for the active draw
   * (set from draw_command::material_override, consumed by render_mesh).
   */
  rsc::material_id m_active_material_override{};

  /**
   * Per-instance skinning palette currently in effect for the active draw
   * (set from draw_command::palette, consumed by render_mesh and the
   * shadow/SSAO/outline passes). Null selects the non-skinned pipeline.
   */
  const joint_palette *m_active_palette = nullptr;

  /** Scratch buffer reused when uploading the palette uniform. */
  std::vector<uint8_t> m_palette_scratch;

  /**
   * Loads a skinned vertex shader. `num_uniform_buffers` is 2 for the passes
   * whose only other cbuffer is the per-draw matrices, and 3 for the outline
   * pass, which keeps its own params cbuffer in slot 1 and puts the palette in
   * slot 2. Returns nullptr when the compiled variant is unavailable, in which
   * case the pass keeps its non-skinned pipeline.
   */
  [[nodiscard]] SDL_GPUShader *
  create_skinned_vertex_shader (const char *path,
                                uint32_t num_uniform_buffers = 2);

  /** Uploads the active palette to a vertex uniform slot, if any. */
  void push_active_palette (SDL_GPUCommandBuffer *cmd, uint32_t slot);

  /**
   * Creates a pipeline for debug geometry using the flat shader.
   *
   * :param primitive: Primitive topology to rasterize.
   * :param depth_test: When true, tests and writes the scene depth buffer.
   * :param color_blend: When true, blends the scene color by source alpha;
   *   when false, writes the color as-is.
   * :param keep_bloom: When true, keeps the destination on the bloom source
   *   target instead of overwriting it with the shader's black output.
   * :return: Created pipeline, or ``nullptr`` on shader/pipeline failure.
   */
  [[nodiscard]] SDL_GPUGraphicsPipeline *
  create_debug_pipeline (SDL_GPUPrimitiveType primitive, bool depth_test,
                         bool color_blend, bool keep_bloom);

  /**
   * Creates the overlay pipeline used to draw into the LDR present texture.
   *
   * :return: Created pipeline, or ``nullptr`` on shader/pipeline failure.
   */
  [[nodiscard]] SDL_GPUGraphicsPipeline *create_ldr_overlay_pipeline ();

  /** Creates the debug line pipeline on first use. */
  void ensure_debug_lines_pipeline ();
  /** Creates the screen-space debug overlay pipeline on first use. */
  void ensure_debug_overlay_pipeline (overlay_blending blending);
  /** Creates the post-tonemap (LDR) overlay pipeline on first use. */
  void ensure_ldr_overlay_pipeline ();
  /** Releases the debug line and overlay pipelines. */
  void destroy_debug_pipelines ();

  /**
   * Uploads a transient vertex buffer through a dedicated copy pass.
   *
   * :param data: Vertex data to upload.
   * :param size: Size of *data* in bytes.
   * :return: The GPU buffer, or ``nullptr`` on failure. The caller owns it
   *   and releases it after recording the draw that consumes it.
   */
  [[nodiscard]] SDL_GPUBuffer *upload_debug_vertices (const void *data,
                                                      size_t size);

  /** Controls which alpha modes render_mesh draws during the current pass. */
  enum class alpha_render_pass : uint8_t
  {
    all,    /**< render all primitives (default, legacy behaviour). */
    opaque, /**< only opaque + mask primitives. */
    blend,  /**< only blend primitives. */
  };
  alpha_render_pass m_alpha_pass = alpha_render_pass::all;
};

/**
 * Binds a custom material and its associated pipeline for a single draw.
 *
 * Used by scene_renderer when a primitive has use_custom_material set.
 */
void bind_custom_material (const gfx::material_instance &inst,
                           const gfx::shader_program &prog,
                           SDL_GPUCommandBuffer *cmd, SDL_GPURenderPass *pass,
                           pipeline_cache &cache, SDL_GPUDevice *device,
                           const SDL_GPUColorTargetDescription *color_targets,
                           int num_color_targets,
                           SDL_GPUTextureFormat depth_format,
                           bool double_sided);

} // namespace gfx

} // namespace wsl
