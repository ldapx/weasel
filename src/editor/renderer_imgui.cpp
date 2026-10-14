#include "renderer_imgui.hpp"

#include "gfx/mesh.hpp"
#include "gfx/model_3d.hpp"
#include "gfx/render_window.hpp"
#include "rsc/resource_ids.hpp"
#include "wsl/comp/singl/runtime_context.hpp"
#include "wsl/gfx/scene_renderer.hpp"
#include "wsl/rsc/resource_manager.hpp"
#include "wsl/gfx/render_context.hpp"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_stdinc.h>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <entt/core/fwd.hpp>
#include <entt/entity/entity.hpp>
#include <glm/common.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>
#include <imsearch.h>

#include <SDL3/SDL_gpu.h>

#include <limits>

namespace editor
{

static void
aabb_init (glm::vec3 &mn, glm::vec3 &mx)
{
  mn = glm::vec3 (std::numeric_limits<float>::infinity ());
  mx = glm::vec3 (-std::numeric_limits<float>::infinity ());
}

static void
aabb_expand (glm::vec3 &mn, glm::vec3 &mx, const glm::vec3 &p)
{
  mn = glm::min (mn, p);
  mx = glm::max (mx, p);
}

static void
compute_scene_bounds_recursive (const wsl::gfx::node &n,
                                const glm::mat4 &parent, glm::vec3 &mn,
                                glm::vec3 &mx)
{
  glm::mat4 const world = parent * n.local_transform;

  // Use highest LOD for bounds (LOD0). All LODs should be similar bounds
  // anyway.
  if (!n.mesh_lods.empty () && (n.mesh_lods[0] != nullptr)) {
    const wsl::gfx::mesh *m = n.mesh_lods[0];

    for (const wsl::gfx::primitive &prim : m->primitives) {
      for (const wsl::gfx::vertex &v : prim.vertices) {
        glm::vec3 const wp = glm::vec3 (world * glm::vec4 (v.pos, 1.0F));
        aabb_expand (mn, mx, wp);
      }
    }
  }

  for (const auto &c : n.children) {
    compute_scene_bounds_recursive (c, world, mn, mx);
  }
}

static bool
compute_model_bounds_world (const wsl::gfx::model_3d &model, int scene_index,
                            glm::vec3 &out_min, glm::vec3 &out_max)
{
  if (scene_index < 0 || scene_index >= (int)model.scenes.size ()) {
    return false;
  }

  glm::vec3 mn;
  glm::vec3 mx;
  aabb_init (mn, mx);

  const wsl::gfx::scene &scn = model.scenes[(size_t)scene_index];
  glm::mat4 const i (1.0F);

  for (const auto &root : scn.roots) {
    compute_scene_bounds_recursive (root, i, mn, mx);
  }

  // if empty (no vertices), mn is +inf
  if (!std::isfinite (mn.x) || !std::isfinite (mx.x)) {
    return false;
  }

  out_min = mn;
  out_max = mx;
  return true;
}

// Clamp helper
static float
clamp01 (float v)
{
  return std::max (0.0F, std::min (1.0F, v));
}

static ImVec4
rgba_u8 (int r, int g, int b, float a = 1.0F)
{
  return ImVec4 (static_cast<float> (r) / 255.F, static_cast<float> (g) / 255.F,
                 static_cast<float> (b) / 255.F, a);
}

// --- RGB <-> HSL (all in 0..1) ---
static void
rgb_to_hsl (float r, float g, float b, float &h, float &s, float &l)
{
  float const maxv = std::max (r, std::max (g, b));
  float const minv = std::min (r, std::min (g, b));
  l = (maxv + minv) * 0.5F;

  if (maxv == minv) {
    h = 0.0F;
    s = 0.0F;
    return;
  }

  float const d = maxv - minv;
  s = (l > 0.5F) ? (d / (2.0F - maxv - minv)) : (d / (maxv + minv));

  if (maxv == r) {
    h = ((g - b) / d) + (g < b ? 6.0F : 0.0F);
  } else if (maxv == g) {
    h = ((b - r) / d) + 2.0F;
  } else {
    h = ((r - g) / d) + 4.0F;
  }

  h /= 6.0F;
}

static float
hue_to_rgb (float p, float q, float t)
{
  if (t < 0.0F) {
    t += 1.0F;
  }
  if (t > 1.0F) {
    t -= 1.0F;
  }
  if (t < 1.0F / 6.0F) {
    return p + ((q - p) * 6.0F * t);
  }
  if (t < 1.0F / 2.0F) {
    return q;
  }
  if (t < 2.0F / 3.0F) {
    return p + ((q - p) * ((2.0F / 3.0F) - t) * 6.0F);
  }
  return p;
}

static void
hsl_to_rgb (float h, float s, float l, float &r, float &g, float &b)
{
  if (s == 0.0F) {
    r = g = b = l;
    return;
  }

  float const q = (l < 0.5F) ? (l * (1.0F + s)) : (l + s - (l * s));
  float const p = (2.0F * l) - q;

  r = hue_to_rgb (p, q, h + (1.0F / 3.0F));
  g = hue_to_rgb (p, q, h);
  b = hue_to_rgb (p, q, h - (1.0F / 3.0F));
}

static ImVec4
with_lightness (ImVec4 c, float l_delta)
{
  float h;
  float s;
  float l;
  rgb_to_hsl (clamp01 (c.x), clamp01 (c.y), clamp01 (c.z), h, s, l);
  l = clamp01 (l + l_delta);

  float r;
  float g;
  float b;
  hsl_to_rgb (h, s, l, r, g, b);
  return ImVec4 (r, g, b, 1.0F);
}

static ImVec4
lighten (ImVec4 c, float amount)
{
  return with_lightness (c, +amount);
}
static ImVec4
darken (ImVec4 c, float amount)
{
  return with_lightness (c, -amount);
}

// Optional: nudge saturation slightly for “glowier” accents (still HSL-based)
static ImVec4
with_saturation (ImVec4 c, float s_delta)
{
  float h;
  float s;
  float l;
  rgb_to_hsl (clamp01 (c.x), clamp01 (c.y), clamp01 (c.z), h, s, l);
  s = clamp01 (s + s_delta);

  float r;
  float g;
  float b;
  hsl_to_rgb (h, s, l, r, g, b);
  return ImVec4 (r, g, b, 1.0F);
}

static ImVec4
mix (ImVec4 a, ImVec4 b, float t)
{
  return ImVec4 (a.x + ((b.x - a.x) * t), a.y + ((b.y - a.y) * t),
                 a.z + ((b.z - a.z) * t), a.w + ((b.w - a.w) * t));
}
void
renderer_imgui::apply_editor_style (const wsl::gfx::editor_theme &t)
{
  this->m_theme = t;
  ImGuiStyle &style = ImGui::GetStyle ();

  m_theme = t;

  // --- Layout ---
  style.WindowRounding = 4.0F;
  style.ChildRounding = 4.0F;
  style.FrameRounding = 2.0F;
  style.PopupRounding = 4.0F;
  style.TabRounding = 3.0F;
  style.ScrollbarRounding = 6.0F;

  style.FramePadding = ImVec2 (6, 4);
  style.ItemSpacing = ImVec2 (6, 4);
  style.ItemInnerSpacing = ImVec2 (6, 4);
  style.IndentSpacing = 16.0F;
  style.ScrollbarSize = 14.0F;

  // --- Derived palette (LIGHTNESS-BASED, NOT ALPHA-BASED) ---
  const ImVec4 bg_main = t.background1;
  const ImVec4 bg_panel = t.background2;
  const ImVec4 bg_popup = lighten (t.background2, 0.02F);
  const ImVec4 surface = lighten (t.background2, 0.1F); // raised surfaces
  const ImVec4 input_bg = lighten (t.background2, 0.03F);

  const ImVec4 fg_main = t.foreground;
  [[maybe_unused]] const ImVec4 fg_muted = darken (t.foreground, 0.25F);
  const ImVec4 fg_disab = darken (t.foreground, 0.5F);

  const ImVec4 accent_primary = with_saturation (t.primary, 0.05F);
  const ImVec4 accent_secondary = with_saturation (t.secondary, 0.05F);

  // “hover/active” are just lightness shifts of the same base
  const ImVec4 hover_soft = mix (bg_panel, accent_secondary, 0.2F);
  const ImVec4 active_hard = lighten (accent_secondary, 0.14F);

  // Borders derived from background lightness
  const ImVec4 border_soft = lighten (t.background2, 0.06F);
  const ImVec4 border_hover = lighten (t.background2, 0.10F);
  const ImVec4 border_active = lighten (t.background2, 0.14F);

  // A “danger” mark: derive from secondary but darken + saturate a bit
  const ImVec4 error_mark
      = with_saturation (darken (t.secondary, 0.20F), 0.15F);

  ImVec4 *colors = style.Colors;

  // --- Text ---
  colors[ImGuiCol_Text] = fg_main;
  colors[ImGuiCol_TextDisabled] = fg_disab;
  colors[ImGuiCol_TextLink] = accent_secondary;
  colors[ImGuiCol_TextSelectedBg] = lighten (bg_panel, 0.10F);

  // --- Windows ---
  colors[ImGuiCol_WindowBg] = bg_main;
  colors[ImGuiCol_ChildBg] = bg_panel;
  colors[ImGuiCol_PopupBg] = bg_popup;

  // --- Borders ---
  colors[ImGuiCol_Border] = border_soft;
  colors[ImGuiCol_BorderShadow] = ImVec4 (0, 0, 0, 0);

  // --- Frames ---
  colors[ImGuiCol_FrameBg] = input_bg;
  colors[ImGuiCol_FrameBgHovered] = hover_soft;
  colors[ImGuiCol_FrameBgActive] = border_active;

  // --- Title bars ---
  colors[ImGuiCol_TitleBg] = bg_panel;
  colors[ImGuiCol_TitleBgActive] = bg_main;
  colors[ImGuiCol_TitleBgCollapsed] = bg_panel;

  // --- Menu / Scrollbar ---
  colors[ImGuiCol_MenuBarBg] = bg_panel;
  colors[ImGuiCol_ScrollbarBg] = bg_panel;
  colors[ImGuiCol_ScrollbarGrab] = border_soft;
  colors[ImGuiCol_ScrollbarGrabHovered] = border_hover;
  colors[ImGuiCol_ScrollbarGrabActive] = border_active;

  // --- Check / Slider ---
  colors[ImGuiCol_CheckMark] = accent_primary;
  colors[ImGuiCol_SliderGrab] = hover_soft;
  colors[ImGuiCol_SliderGrabActive] = active_hard;

  // --- Buttons ---
  colors[ImGuiCol_Button] = surface;
  colors[ImGuiCol_ButtonHovered] = hover_soft;
  colors[ImGuiCol_ButtonActive] = active_hard;

  // --- Headers ---
  colors[ImGuiCol_Header] = surface;
  colors[ImGuiCol_HeaderHovered] = hover_soft;
  colors[ImGuiCol_HeaderActive] = active_hard;

  // --- Separators / Resize ---
  colors[ImGuiCol_Separator] = border_soft;
  colors[ImGuiCol_SeparatorHovered] = border_hover;
  colors[ImGuiCol_SeparatorActive] = border_active;

  colors[ImGuiCol_ResizeGrip] = border_soft;
  colors[ImGuiCol_ResizeGripHovered] = accent_primary;
  colors[ImGuiCol_ResizeGripActive] = lighten (accent_primary, 0.3);

  // --- Tabs ---
  colors[ImGuiCol_Tab] = bg_panel;
  colors[ImGuiCol_TabHovered] = hover_soft;
  colors[ImGuiCol_TabSelected] = bg_main;
  colors[ImGuiCol_TabSelectedOverline] = accent_secondary;
  colors[ImGuiCol_TabDimmed] = bg_panel;
  colors[ImGuiCol_TabDimmedSelected] = bg_main;
  colors[ImGuiCol_TabDimmedSelectedOverline] = hover_soft;

  // --- Docking ---
  colors[ImGuiCol_DockingPreview] = lighten (accent_primary, 0.10F);
  colors[ImGuiCol_DockingEmptyBg] = bg_main;

  // --- Tables ---
  colors[ImGuiCol_TableHeaderBg] = bg_panel;
  colors[ImGuiCol_TableBorderStrong] = border_soft;
  colors[ImGuiCol_TableBorderLight] = border_soft;
  colors[ImGuiCol_TableRowBg] = ImVec4 (0, 0, 0, 0);
  colors[ImGuiCol_TableRowBgAlt] = lighten (bg_panel, 0.02F);

  // --- Plots ---
  colors[ImGuiCol_PlotLines] = accent_secondary;
  colors[ImGuiCol_PlotLinesHovered] = fg_main;
  colors[ImGuiCol_PlotHistogram] = hover_soft;
  colors[ImGuiCol_PlotHistogramHovered] = active_hard;

  // --- Navigation / Drag ---
  colors[ImGuiCol_DragDropTarget] = accent_primary;
  colors[ImGuiCol_DragDropTargetBg] = lighten (accent_primary, 0.10F);
  colors[ImGuiCol_NavCursor] = border_active;
  colors[ImGuiCol_NavWindowingHighlight] = hover_soft;

  // Modal dim REALLY needs alpha (it’s a fullscreen overlay).
  colors[ImGuiCol_NavWindowingDimBg] = ImVec4 (0, 0, 0, 0.55F);
  colors[ImGuiCol_ModalWindowDimBg] = ImVec4 (0, 0, 0, 0.55F);

  // --- Status / Errors ---
  colors[ImGuiCol_UnsavedMarker] = error_mark;
  colors[ImGuiCol_InputTextCursor] = accent_secondary;
  colors[ImGuiCol_TreeLines] = border_soft;
}

static std::string
resolve_font_path (const char *relative_path)
{
  const char *base_path = SDL_GetBasePath ();
  if (base_path != nullptr) {
    std::filesystem::path exe_dir (base_path);
    std::filesystem::path share_dir = exe_dir / ".." / "share" / "weasel";
    if (std::filesystem::exists (share_dir / "otf"))
      return (share_dir / relative_path).string ();
  }

#ifdef WEASEL_BUILD_DIR
  return (std::filesystem::path (WEASEL_BUILD_DIR) / relative_path).string ();
#elif defined(WEASEL_SOURCE_DIR)
  return (std::filesystem::path (WEASEL_SOURCE_DIR) / relative_path).string ();
#else
  return relative_path;
#endif
}

renderer_imgui::renderer_imgui (wsl::gfx::render_window &window,
                                wsl::gfx::render_context *ctx)
    : m_window (&window), m_ctx (ctx)
{
  IMGUI_CHECKVERSION ();

  ImGui::CreateContext ();
  [[maybe_unused]] ImGuiIO &io = ImGui::GetIO ();
  ImSearch::CreateContext ();

  io.Fonts->Clear ();

  ImFontConfig cfg;
  cfg.OversampleH = 2;
  cfg.OversampleV = 2;
  cfg.PixelSnapH = true;

  // Base font size
  constexpr float font_size = 12.0F;

  // Main UI font
  ImFont *font_regular = io.Fonts->AddFontFromFileTTF (
      resolve_font_path ("otf/splinesans-regular.otf").c_str (), font_size,
      &cfg);

  fonts.regular = font_regular;
  fonts.light = io.Fonts->AddFontFromFileTTF (
      resolve_font_path ("otf/splinesans-light.otf").c_str (), font_size, &cfg);
  fonts.medium = io.Fonts->AddFontFromFileTTF (
      resolve_font_path ("otf/splinesans-medium.otf").c_str (), font_size,
      &cfg);
  fonts.semibold = io.Fonts->AddFontFromFileTTF (
      resolve_font_path ("otf/splinesans-semibold.otf").c_str (), font_size,
      &cfg);
  fonts.bold = io.Fonts->AddFontFromFileTTF (
      resolve_font_path ("otf/splinesans-bold.otf").c_str (), font_size, &cfg);
  fonts.title = io.Fonts->AddFontFromFileTTF (
      resolve_font_path ("otf/splinesans-bold.otf").c_str (), 32.0F, &cfg);

  IM_ASSERT (fonts.regular);
  IM_ASSERT (fonts.medium);
  IM_ASSERT (fonts.semibold);
  IM_ASSERT (fonts.bold);
  IM_ASSERT (fonts.title);

  io.FontDefault = fonts.regular;

  fonts.mono = io.Fonts->AddFontFromFileTTF (
      resolve_font_path ("otf/hack-regular.otf").c_str (), 11.0F, &cfg);

  IM_ASSERT (fonts.mono && "Failed to load Hack font");

  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  ImGui::StyleColorsDark ();

  wsl::gfx::editor_theme theme{};
  theme.primary = rgba_u8 (0x68, 0x76, 0x3c);     // dusty_olive
  theme.secondary = rgba_u8 (0xde, 0xc1, 0x6b);   // old_gold
  theme.background1 = rgba_u8 (0x09, 0x09, 0x0c); // black
  theme.background2 = rgba_u8 (0x10, 0x11, 0x15); // onyx
  theme.foreground = rgba_u8 (0xf2, 0xf2, 0xf2);  // warm white

  apply_editor_style (theme);

  ImGui_ImplSDL3_InitForSDLGPU (window.handler ());

  ImGui_ImplSDLGPU3_InitInfo init_info{};
  init_info.Device = ctx->gpu_device;
  init_info.ColorTargetFormat
      = SDL_GetGPUSwapchainTextureFormat (ctx->gpu_device, window.handler ());
  init_info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
  init_info.SwapchainComposition = SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
  init_info.PresentMode = SDL_GPU_PRESENTMODE_IMMEDIATE;
  ImGui_ImplSDLGPU3_Init (&init_info);
}

renderer_imgui::~renderer_imgui ()
{
  if ((m_ctx != nullptr) && (m_ctx->gpu_device != nullptr)) {
    SDL_WaitForGPUIdle (m_ctx->gpu_device);
  }
  destroy_preview_targets ();

  ImGui_ImplSDLGPU3_Shutdown ();
  ImGui_ImplSDL3_Shutdown ();
  ImGui::DestroyContext ();
}

void
renderer_imgui::begin_frame ()
{
  ImGui_ImplSDLGPU3_NewFrame ();
  ImGui_ImplSDL3_NewFrame ();
  ImGui::NewFrame ();
}

void
renderer_imgui::end_frame ()
{
  ImGui::Render ();
}

void
renderer_imgui::prepare (ImDrawData *draw_data)
{
  if (draw_data == nullptr || m_ctx->main_cmd == nullptr) {
    return;
  }
  ImGui_ImplSDLGPU3_PrepareDrawData (draw_data, m_ctx->main_cmd);
}

void
renderer_imgui::render (ImDrawData *draw_data)
{
  // The ImGui backend dereferences `render_pass` immediately, so a
  // null ui_pass (e.g. the swapchain acquire failed and
  // begin_ui_render_pass bailed out) is a segfault. Skip the draw
  // — the next valid frame will catch up.
  if (draw_data == nullptr) {
    wsl::log::editor ()->warn ("ImGui render: draw_data is null, skipping");
    return;
  }
  if (m_ctx == nullptr || m_ctx->main_cmd == nullptr) {
    wsl::log::editor ()->warn ("ImGui render: ctx/main_cmd is null, skipping");
    return;
  }
  if (m_ctx->ui_pass == nullptr) {
    wsl::log::editor ()->warn (
        "ImGui render: ui_pass is null (swapchain acquire failed?), skipping");
    return;
  }
  if (draw_data->DisplaySize.x <= 0.0F || draw_data->DisplaySize.y <= 0.0F) {
    wsl::log::editor ()->warn ("ImGui render: zero DisplaySize, skipping");
    return;
  }
  ImGui_ImplSDLGPU3_RenderDrawData (draw_data, m_ctx->main_cmd, m_ctx->ui_pass);
}

void
renderer_imgui::request_model_preview (
    wsl::comp::singl::runtime_context *runtime_ctx,
    wsl::rsc::resource_manager *resource_manager, entt::id_type model_eid,
    uint32_t w, uint32_t h)
{
  (void)resource_manager;
  if (runtime_ctx == nullptr) {
    return;
  }

  m_preview_runtime = runtime_ctx;
  m_preview_model = model_eid;

  // Re-enabled by preview_navigate while a model preview is being drawn.
  m_preview_gizmo_visible = false;

  // Use the provided size, but clamp to reasonable limits if needed
  const uint32_t new_w = (w > 0) ? w : 256;
  const uint32_t new_h = (h > 0) ? h : 256;

  m_requested_w = new_w;
  m_requested_h = new_h;

  const bool size_changed = (m_desired_w != new_w) || (m_desired_h != new_h);
  m_desired_w = new_w;
  m_desired_h = new_h;

  // Allocate (or reallocate) preview textures when size changes or not created
  if (size_changed || (m_preview_resolve == nullptr)
      || (m_preview_color_msaa == nullptr) || (m_preview_depth_msaa == nullptr)
      || (m_preview_bloom_msaa == nullptr)
      || (m_preview_bloom_resolve == nullptr)) {
    ensure_preview_targets (m_desired_w, m_desired_h);
  }

  m_preview_dirty = true;
}

void
renderer_imgui::destroy_preview_targets ()
{
  auto *dev = m_ctx->gpu_device;

  if (m_preview_color_msaa != nullptr) {
    SDL_ReleaseGPUTexture (dev, m_preview_color_msaa),
        m_preview_color_msaa = nullptr;
  }
  if (m_preview_depth_msaa != nullptr) {
    SDL_ReleaseGPUTexture (dev, m_preview_depth_msaa),
        m_preview_depth_msaa = nullptr;
  }
  if (m_preview_resolve != nullptr) {
    SDL_ReleaseGPUTexture (dev, m_preview_resolve), m_preview_resolve = nullptr;
  }

  // NEW:
  if (m_preview_bloom_msaa != nullptr) {
    SDL_ReleaseGPUTexture (dev, m_preview_bloom_msaa),
        m_preview_bloom_msaa = nullptr;
  }
  if (m_preview_bloom_resolve != nullptr) {
    SDL_ReleaseGPUTexture (dev, m_preview_bloom_resolve),
        m_preview_bloom_resolve = nullptr;
  }

  m_preview_w = m_preview_h = 0;
}

void
renderer_imgui::ensure_preview_targets (uint32_t w, uint32_t h)
{
  if (m_preview_w == w && m_preview_h == h && (m_preview_color_msaa != nullptr)
      && (m_preview_depth_msaa != nullptr) && (m_preview_resolve != nullptr)
      && (m_preview_bloom_msaa != nullptr)
      && (m_preview_bloom_resolve != nullptr)) {
    return;
  }

  destroy_preview_targets ();

  m_preview_w = w;
  m_preview_h = h;

  // ---- MSAA HDR color targets (2 MRT) ----
  SDL_GPUTextureCreateInfo msaa{};
  SDL_zero (msaa);
  msaa.type = SDL_GPU_TEXTURETYPE_2D;
  msaa.width = w;
  msaa.height = h;
  msaa.layer_count_or_depth = 1;
  msaa.num_levels = 1;
  msaa.sample_count = SDL_GPU_SAMPLECOUNT_4;
  msaa.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
  msaa.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;

  m_preview_color_msaa = SDL_CreateGPUTexture (m_ctx->gpu_device, &msaa);
  m_preview_bloom_msaa = SDL_CreateGPUTexture (m_ctx->gpu_device, &msaa); // NEW

  // ---- Resolve HDR textures must be COLOR_TARGET + SAMPLER ----
  SDL_GPUTextureCreateInfo res = msaa;
  res.sample_count = SDL_GPU_SAMPLECOUNT_1;
  res.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;

  m_preview_resolve = SDL_CreateGPUTexture (m_ctx->gpu_device, &res);
  m_preview_bloom_resolve
      = SDL_CreateGPUTexture (m_ctx->gpu_device, &res); // NEW

  // ---- MSAA depth ----
  SDL_GPUTextureCreateInfo ds{};
  SDL_zero (ds);
  ds.type = SDL_GPU_TEXTURETYPE_2D;
  ds.width = w;
  ds.height = h;
  ds.layer_count_or_depth = 1;
  ds.num_levels = 1;
  ds.sample_count = SDL_GPU_SAMPLECOUNT_4;
  ds.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
  ds.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;

  m_preview_depth_msaa = SDL_CreateGPUTexture (m_ctx->gpu_device, &ds);
}

void
renderer_imgui::render_requested_previews ()
{
  // --- idle reset logic (3s after last gizmo interaction) ---
  if (m_preview_user_camera_active) {
    const double now = ImGui::GetTime ();
    if ((now - m_preview_last_gizmo_time) > m_preview_idle_reset_seconds) {
      preview_reset_camera_to_default ();
    } else {
      // while user camera active, no auto rotate
      m_preview_rotate = false;
    }
  }

  // --- auto-rotate model only if enabled and user is NOT controlling camera
  // ---
  if (m_preview_rotate && m_preview_model != entt::null
      && !m_preview_user_camera_active) {
    m_preview_yaw += m_preview_yaw_speed * ImGui::GetIO ().DeltaTime;
    if (m_preview_yaw > glm::two_pi<float> ()) {
      m_preview_yaw -= glm::two_pi<float> ();
    }
    m_preview_dirty = true;
  }

  if (!m_preview_dirty) {
    return;
  }
  if (m_preview_runtime == nullptr) {
    return;
  }
  if (m_ctx->main_cmd == nullptr) {
    return;
  }

  if ((m_preview_color_msaa == nullptr) || (m_preview_depth_msaa == nullptr)
      || (m_preview_resolve == nullptr) || (m_preview_bloom_msaa == nullptr)
      || (m_preview_bloom_resolve == nullptr)) {
    ensure_preview_targets (m_requested_w, m_requested_h);
    if ((m_preview_color_msaa == nullptr) || (m_preview_depth_msaa == nullptr)
        || (m_preview_resolve == nullptr) || (m_preview_bloom_msaa == nullptr)
        || (m_preview_bloom_resolve == nullptr)) {
      return; // allocation failed
    }
  }
  if (m_requested_w != m_preview_w || m_requested_h != m_preview_h) {
    return;
  }

  if (render_model_preview_low_lod (*m_preview_runtime, m_preview_model,
                                    m_preview_w, m_preview_h)) {
    m_preview_dirty = false;
  }
}

bool
renderer_imgui::render_model_preview_low_lod (
    wsl::comp::singl::runtime_context &runtime_ctx, entt::id_type model_id,
    uint32_t w, uint32_t h)
{
  auto &mgr = runtime_ctx.resource_manager ();

  SDL_GPUCommandBuffer const *cmd = m_ctx->main_cmd;
  if (cmd == nullptr) {
    return false;
  }

  SDL_GPUColorTargetInfo ct[2]{};
  SDL_zero (ct);

  // Scene HDR
  ct[0].texture = m_preview_color_msaa;
  ct[0].load_op = SDL_GPU_LOADOP_CLEAR;
  ct[0].store_op = SDL_GPU_STOREOP_RESOLVE;
  ct[0].clear_color = { 0.0F, 0.0F, 0.0F, 0.0F }; // Clear to transparent
  ct[0].resolve_texture = m_preview_resolve;

  // Bloom HDR
  ct[1].texture = m_preview_bloom_msaa;
  ct[1].load_op = SDL_GPU_LOADOP_CLEAR;
  ct[1].store_op = SDL_GPU_STOREOP_RESOLVE;
  ct[1].clear_color = { 0.0F, 0.0F, 0.0F, 1.0F };
  ct[1].resolve_texture = m_preview_bloom_resolve;

  SDL_GPUDepthStencilTargetInfo ds{};
  SDL_zero (ds);
  ds.texture = m_preview_depth_msaa;
  ds.load_op = SDL_GPU_LOADOP_CLEAR;
  ds.store_op = SDL_GPU_STOREOP_STORE;
  ds.clear_depth = 1.0F;

  // Begin preview 3D pass (MRT-compatible!)
  SDL_GPURenderPass *pass
      = SDL_BeginGPURenderPass (m_ctx->main_cmd, ct, 2, &ds);
  if (pass == nullptr) {
    return false;
  }

  // redirect the 3D renderer to our temporary preview pass
  SDL_GPURenderPass *old_pass = m_ctx->main_pass;
  m_ctx->main_pass = pass;

  SDL_GPUViewport const viewport{ 0, 0, (float)w, (float)h, 0.0F, 1.0F };
  SDL_SetGPUViewport (pass, &viewport);
  SDL_Rect const sc{ 0, 0, (int)w, (int)h };
  SDL_SetGPUScissor (pass, &sc);

  auto *scene_renderer = runtime_ctx.try_get_active_scene_renderer ();

  // Render model ONLY if model_id is not null and is loaded AND we have a
  // renderer
  if ((scene_renderer != nullptr) && model_id != entt::null
      && mgr.contains (wsl::rsc::model_id{ model_id })
      && mgr.state (wsl::rsc::model_id{ model_id })
             == wsl::rsc::model_state::loaded) {
    auto handle = mgr.get (wsl::rsc::model_id{ model_id });
    if (handle) {
      int scene_index = handle->default_scene;
      scene_index = std::max (scene_index, 0);

      glm::vec3 bmin;
      glm::vec3 bmax;
      bool const ok_bounds
          = compute_model_bounds_world (*handle, scene_index, bmin, bmax);

      // Fallback if something is weird
      if (!ok_bounds) {
        bmin = glm::vec3 (-0.5F);
        bmax = glm::vec3 (+0.5F);
      }

      glm::vec3 const center = (bmin + bmax) * 0.5F;
      float radius = glm::length (bmax - center);
      radius = glm::max (radius, 0.001F);

      float const aspect = (float)w / (float)h;
      float const fov = glm::radians (45.0F);

      // Fit bounding sphere (vertical fit)
      float dist = radius / glm::tan (fov * 0.5F);
      dist *= 1.25F; // padding

      // Center model at origin
      glm::mat4 model_mtx (1.0F);

      // center to origin
      model_mtx = glm::translate (model_mtx, -center);

      // rotate right constantly (yaw around +Y)
      model_mtx = glm::rotate (model_mtx, -m_preview_yaw,
                               glm::vec3 (0.0F, 1.0F, 0.0F));

      // Pivot in preview space is origin (model is centered to origin via
      // model_mtx)
      m_preview_pivot = glm::vec3 (0.0F);

      // Default camera (fit) depends on dist, so refresh it every preview
      // render
      m_preview_default_pos = glm::vec3 (0.0F, 0.0F, dist);

      // Camera should look at the pivot (camera -> pivot)
      const glm::vec3 default_dir
          = glm::normalize (m_preview_pivot - m_preview_default_pos);
      m_preview_default_rot
          = glm::quatLookAt (default_dir, glm::vec3 (0, 1, 0));

      // If user isn't controlling the camera, keep preview camera snapped
      // to default
      if (!m_preview_user_camera_active) {
        m_preview_cam_pos = m_preview_default_pos;
        m_preview_cam_rot = m_preview_default_rot;
      }

      // Use either user camera or default camera
      glm::vec3 const cam_pos = m_preview_cam_pos;

      const glm::vec3 old_camera_pos = scene_renderer->camera_position ();
      scene_renderer->set_camera_position (cam_pos);

      // Near/Far
      float const nearp = glm::max (0.01F, dist - (radius * 3.0F));
      float const farp = dist + (radius * 6.0F);

      glm::mat4 const view
          = glm::lookAt (cam_pos, m_preview_pivot, glm::vec3 (0, 1, 0));
      glm::mat4 const proj = glm::perspective (fov, aspect, nearp, farp);
      glm::mat4 const vp = proj * view;

      const bool old_force_unlit = scene_renderer->is_force_unlit ();
      scene_renderer->set_force_unlit (true);

      scene_renderer->bind_main_pipeline ();
      scene_renderer->draw_model (*handle, (size_t)scene_index, model_mtx, vp);

      scene_renderer->set_force_unlit (old_force_unlit);
      scene_renderer->set_camera_position (old_camera_pos);
    }
  }

  // Draw the navigation widget inside the preview pass so it is baked into
  // the preview texture instead of an ImGui overlay.
  if ((scene_renderer != nullptr) && m_preview_gizmo_visible) {
    wsl::gfx::view_gizmo_camera gizmo_camera;
    gizmo_camera.position = m_preview_cam_pos;
    gizmo_camera.rotation = m_preview_cam_rot;
    gizmo_camera.pivot = m_preview_pivot;

    std::vector<wsl::gfx::overlay_vertex> overlay;
    // The preview pass is multisampled, so hard edges are already resolved
    // without the overlay builder's alpha fringe.
    wsl::gfx::view_gizmo_build_options build_options;
    build_options.antialias_feather = 0.0F;
    m_preview_gizmo.build_overlay (gizmo_camera, m_preview_gizmo_layout,
                                   overlay, build_options);
    scene_renderer->draw_debug_overlay (
        overlay, wsl::gfx::scene_renderer::overlay_blending::write);
  }

  SDL_EndGPURenderPass (pass);
  m_ctx->main_pass = old_pass;
  return true;
}

void
renderer_imgui::on_resize (uint32_t /*unused*/, uint32_t /*unused*/)
{
  if ((m_ctx == nullptr) || (m_ctx->gpu_device == nullptr)) {
    return;
  }

  if (m_desired_w == 0 || m_desired_h == 0) {
    return;
  }

  SDL_WaitForGPUIdle (m_ctx->gpu_device);
  ensure_preview_targets (m_desired_w, m_desired_h);
  m_preview_dirty = true;
}

void
renderer_imgui::preview_navigate (const wsl::gfx::view_gizmo_input &input,
                                  const wsl::gfx::view_gizmo_layout &layout)
{
  wsl::gfx::view_gizmo_camera camera;
  camera.position = m_preview_cam_pos;
  camera.rotation = m_preview_cam_rot;
  camera.pivot = m_preview_pivot;

  const bool modified = m_preview_gizmo.interact (input, layout, camera);
  const bool interacting
      = m_preview_gizmo.is_active () || m_preview_gizmo.is_animating ();

  if (modified || interacting) {
    m_preview_cam_pos = camera.position;
    m_preview_cam_rot = camera.rotation;

    m_preview_user_camera_active = true;
    m_preview_last_gizmo_time = ImGui::GetTime ();

    // Stop auto-rotate while the user is interacting and ensure the preview
    // redraws with the new camera state.
    m_preview_rotate = false;
    m_preview_dirty = true;
  } else if (m_preview_gizmo.is_hovered ()) {
    // Hover highlights are baked into the preview texture, so a redraw is
    // needed when the highlight moves.
    m_preview_dirty = true;
  }

  m_preview_gizmo_layout = layout;
  m_preview_gizmo_visible = true;
}

void
renderer_imgui::preview_reset_camera_to_default ()
{
  m_preview_cam_pos = m_preview_default_pos;
  m_preview_cam_rot = m_preview_default_rot;
  m_preview_user_camera_active = false;

  m_preview_rotate = true;
  m_preview_dirty = true;
}

SDL_GPUTexture *
renderer_imgui::get_model_preview_texture () const
{
  return m_preview_resolve;
}

} // namespace editor
