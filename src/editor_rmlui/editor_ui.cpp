// RmlUi-based editor UI: composition root.
//
// Owns the editor RmlUi context and the shared SDL_GPU backend instances and
// wires the three layers together: ui_model (state + domain operations),
// ui_view (document rendering) and ui_controller (input + orchestration).
// No ImGui anywhere: this is the successor editor's UI layer.

#include "editor_ui.hpp"

#include "log_sink.hpp"
#include "ui_agent.hpp"
#include "ui_controller.hpp"
#include "ui_model.hpp"
#include "ui_util.hpp"
#include "ui_view.hpp"

#include "wsl/gfx/render_context.hpp"
#include "wsl/gfx/render_window.hpp"
#include "wsl/gfx/rml_runtime.hpp"
#include "wsl/gfx/shader.hpp"
#include "wsl/gfx/ui_system_interface.hpp"
#include "wsl/log/log.hpp"
#include "wsl/comp/singl/editor_context.hpp"
#include "wsl/comp/singl/runtime_context.hpp"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/FileInterface.h>

// RmlUi_Renderer_SDL_GPU.h uses `interface` as a parameter name, which
// collides with the COM macro from windows.h whenever a Windows header
// precedes this include. Guard it order-independently.
#ifdef interface
#pragma push_macro("interface")
#undef interface
#define WSL_POP_RMLUI_INTERFACE
#endif
#include <RmlUi_Renderer_SDL_GPU.h>
#ifdef WSL_POP_RMLUI_INTERFACE
#pragma pop_macro("interface")
#undef WSL_POP_RMLUI_INTERFACE
#endif

#include <RmlUi_Platform_SDL.h>

#include <cstdio>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <utility>

namespace editor_rml
{

namespace
{

/** File interface resolving RmlUi paths against a base directory. */
class source_file_interface final : public Rml::FileInterface
{
public:
  explicit source_file_interface (std::string base) : m_base (std::move (base))
  {
  }

  /**
   * Editor theme palette. This RmlUi build has no stylesheet variables,
   * so ``editor.rcss`` references these as ``@tokens`` and they are
   * expanded to literals when the stylesheet is served below. Unknown
   * ``@names`` are left untouched so typos still surface in RmlUi's logs.
   */
  static const std::pair<const char *, const char *> *
  theme_tokens (size_t &count)
  {
    static const std::pair<const char *, const char *> tokens[] = {
      { "text", "#f2ebe9" },
      { "bright", "#ffffff" },
      { "dim", "#9a938b" },
      { "muted", "#8f8a80" },
      { "faint", "#6b655a" },
      { "faint-alt", "#6e6a63" },
      { "bg-root", "#09090c" },
      { "bg-panel", "#0c0d12f7" },
      { "bg-toolbar", "#0c0d12f5" },
      { "bg-deep", "#101116" },
      { "bg-input", "#101218" },
      { "bg-card", "#14161ee6" },
      { "bg-well", "#06070ad9" },
      { "bg-row", "#1b1d26" },
      { "bg-hover", "#262a37" },
      { "bg-tab-hover", "#2c3242" },
      { "bg-menu-hover", "#2b3040" },
      { "border", "#2a2c38" },
      { "border-strong", "#2f3242" },
      { "border-faint", "#23252f" },
      { "olive", "#68763c" },
      { "olive-light", "#8fa34f" },
      { "gold", "#dec16b" },
      { "gold-light", "#e8d194" },
      { "gold-hover", "#dec16b1c" },
      { "gold-target", "#dec16b30" },
      { "gold-selected", "#dec16b40" },
      { "gold-selected-hover", "#dec16b59" },
      { "zebra-even", "#ffffff06" },
      { "red", "#e06c60" },
      { "amber", "#f9e2af" },
      { "green-bright", "#a6e3a1" },
      { "error-dark", "#7c3a3a" },
      { "error-border", "#a05050" },
      { "role-blue", "#66b3ff" },
      { "role-green", "#99e699" },
      { "role-yellow", "#cccc66" },
      { "sep", "#4a4438" },
      { "card", "rgb(14, 15, 20)" },
      { "backdrop", "#0000008c" },
      { "veil", "#07080bf2" },
      { "scrollbar-track", "#09090c80" },
    };
    count = sizeof (tokens) / sizeof (tokens[0]);
    return tokens;
  }

  /** Expands ``@token`` references using the theme palette above. */
  static std::string
  expand_theme_tokens (const std::string &css)
  {
    size_t count = 0;
    const auto *tokens = theme_tokens (count);
    std::string out;
    out.reserve (css.size ());
    for (size_t i = 0; i < css.size ();) {
      if (css[i] == '@' && i + 1 < css.size ()
          && std::islower (static_cast<unsigned char> (css[i + 1]))) {
        size_t end = i + 1;
        while (end < css.size ()
               && (std::isalnum (static_cast<unsigned char> (css[end]))
                   || css[end] == '-')) {
          ++end;
        }
        const std::string name = css.substr (i + 1, end - i - 1);
        const char *value = nullptr;
        for (size_t t = 0; t < count; ++t) {
          if (name == tokens[t].first) {
            value = tokens[t].second;
            break;
          }
        }
        if (value != nullptr) {
          out += value;
          i = end;
          continue;
        }
        // Unknown token: leave it verbatim so RmlUi still reports it.
      }
      out += css[i++];
    }
    return out;
  }

  Rml::FileHandle
  Open (const Rml::String &path) override
  {
    // Absolute paths (e.g. fonts) bypass the base directory.
    const bool absolute = !path.empty ()
                          && (path[0] == '/'
#ifdef _WIN32
                              || path.find (':') != Rml::String::npos
#endif
                          );
    const std::string full
        = (absolute || m_base.empty ()) ? path : (m_base + "/" + path);

    // Stylesheets pass through the theme expander and are served from
    // memory; everything else streams straight from disk as before.
    if (full.size () >= 5 && full.compare (full.size () - 5, 5, ".rcss") == 0) {
      FILE *fp = std::fopen (full.c_str (), "rb");
      if (fp == nullptr) {
        return 0;
      }
      std::string text;
      char chunk[4096];
      size_t got = 0;
      while ((got = std::fread (chunk, 1, sizeof (chunk), fp)) > 0) {
        text.append (chunk, got);
      }
      std::fclose (fp);
      text = expand_theme_tokens (text);
      auto *buffer = new mem_file{ std::move (text), 0 };
      const Rml::FileHandle handle = reinterpret_cast<Rml::FileHandle> (buffer);
      std::lock_guard<std::mutex> lock (m_mutex);
      m_buffers.insert (handle);
      return handle;
    }
    return reinterpret_cast<Rml::FileHandle> (std::fopen (full.c_str (), "rb"));
  }

  void
  Close (Rml::FileHandle file) override
  {
    if (file == 0) {
      return;
    }
    {
      std::lock_guard<std::mutex> lock (m_mutex);
      auto it = m_buffers.find (file);
      if (it != m_buffers.end ()) {
        m_buffers.erase (it);
        delete reinterpret_cast<mem_file *> (file);
        return;
      }
    }
    std::fclose (reinterpret_cast<FILE *> (file));
  }

  size_t
  Read (void *buffer, size_t size, Rml::FileHandle file) override
  {
    if (file == 0) {
      return 0;
    }
    std::lock_guard<std::mutex> lock (m_mutex);
    if (m_buffers.find (file) != m_buffers.end ()) {
      auto *mem = reinterpret_cast<mem_file *> (file);
      const size_t left = mem->data.size () - mem->pos;
      const size_t count = size < left ? size : left;
      std::memcpy (buffer, mem->data.data () + mem->pos, count);
      mem->pos += count;
      return count;
    }
    return std::fread (buffer, 1, size, reinterpret_cast<FILE *> (file));
  }

  bool
  Seek (Rml::FileHandle file, long offset, int origin) override
  {
    if (file == 0) {
      return false;
    }
    {
      std::lock_guard<std::mutex> lock (m_mutex);
      if (m_buffers.find (file) != m_buffers.end ()) {
        auto *mem = reinterpret_cast<mem_file *> (file);
        long base = 0;
        if (origin == SEEK_CUR) {
          base = static_cast<long> (mem->pos);
        } else if (origin == SEEK_END) {
          base = static_cast<long> (mem->data.size ());
        } else if (origin != SEEK_SET) {
          return false;
        }
        const long pos = base + offset;
        if (pos < 0 || static_cast<size_t> (pos) > mem->data.size ()) {
          return false;
        }
        mem->pos = static_cast<size_t> (pos);
        return true;
      }
    }
    return std::fseek (reinterpret_cast<FILE *> (file), offset, origin) == 0;
  }

  size_t
  Tell (Rml::FileHandle file) override
  {
    if (file == 0) {
      return 0;
    }
    std::lock_guard<std::mutex> lock (m_mutex);
    if (m_buffers.find (file) != m_buffers.end ()) {
      return reinterpret_cast<mem_file *> (file)->pos;
    }
    return static_cast<size_t> (std::ftell (reinterpret_cast<FILE *> (file)));
  }

private:
  struct mem_file
  {
    std::string data;
    size_t pos = 0;
  };

  std::string m_base;
  std::mutex m_mutex;
  std::unordered_set<Rml::FileHandle> m_buffers;
};

std::string
read_text_file (const std::string &path)
{
  std::ifstream in (path, std::ios::binary);
  if (!in) {
    return {};
  }
  std::ostringstream out;
  out << in.rdbuf ();
  return out.str ();
}

} // namespace

struct editor_ui::impl
{
  explicit impl (services in_services)
      : model (std::move (in_services)), view (model), controller (model, view)
  {
  }

  std::unique_ptr<wsl::gfx::ui_system_interface> system_interface;
  std::unique_ptr<RenderInterface_SDL_GPU> render_interface;
  std::unique_ptr<source_file_interface> file_interface;

  Rml::Context *context = nullptr;
  Rml::ElementDocument *document = nullptr;

  // Game-view blit resources (present texture -> UI rect).
  SDL_GPUGraphicsPipeline *blit_pipeline = nullptr;
  SDL_GPUSampler *blit_sampler = nullptr;

  bool shut_down = false;

  ui_model model;
  ui_view view;
  ui_controller controller;

  bool initialize ();
  void shutdown ();
  void handle_event (const wsl::engine_event &event);
  void update (double dt);
  void render ();
  void set_log_sink (log_sink *sink);

  std::string document_root () const;
  std::string load_editor_rml () const;
  void load_fonts () const;
  void blit_game_view (SDL_GPUCommandBuffer *cmd,
                       const wsl::gfx::texture &swapchain);
  void create_blit_resources ();
  void destroy_blit_resources ();
};

bool
editor_ui::impl::initialize ()
{
  if ((model.svc.runtime == nullptr) || (model.svc.window == nullptr)
      || (model.svc.editor == nullptr)) {
    return false;
  }

  auto &render_ctx = model.svc.runtime->render_ctx ();
  SDL_Window *window = model.svc.window->handler ();
  if ((render_ctx.gpu_device == nullptr) || (window == nullptr)) {
    wsl::log::editor ()->error (
        "RmlUi editor UI: no GPU device or window available");
    return false;
  }

  system_interface = std::make_unique<wsl::gfx::ui_system_interface> ();
  render_interface = std::make_unique<RenderInterface_SDL_GPU> (
      render_ctx.gpu_device, window);
  file_interface = std::make_unique<source_file_interface> (document_root ());

  // Publish the shared backends before initialising RmlUi so scene-level
  // ui_manager instances attach to them instead of creating their own.
  wsl::gfx::set_shared_ui_system_interface (system_interface.get ());
  wsl::gfx::set_shared_ui_render_interface (render_interface.get ());
  Rml::SetFileInterface (file_interface.get ());

  if (!wsl::gfx::rmlui_acquire ()) {
    return false;
  }

  load_fonts ();

  int width = 0;
  int height = 0;
  model.svc.window->get_size (width, height);
  context = Rml::CreateContext (
      "editor",
      Rml::Vector2i{ width > 0 ? width : 1280, height > 0 ? height : 720 });
  if (context == nullptr) {
    wsl::log::editor ()->error ("RmlUi editor UI: CreateContext failed");
    shutdown ();
    return false;
  }

  const std::string rml = load_editor_rml ();
  if (rml.empty ()) {
    wsl::log::editor ()->error (
        "RmlUi editor UI: could not locate rsc/editor_rml/editor.rml");
    shutdown ();
    return false;
  }

  document = context->LoadDocumentFromMemory (rml, "editor.rml");
  if (document == nullptr) {
    wsl::log::editor ()->error ("RmlUi editor UI: document failed to parse");
    shutdown ();
    return false;
  }

  document->Show ();
  view.attach (document, context);
  controller.attach (document, context);
  create_blit_resources ();
  model.load_recent_projects ();
  controller.wire_events ();
  model.append_console ("Weasel RmlUi editor ready. Type 'help' for commands.");
  view.refresh ();
  return true;
}

void
editor_ui::impl::shutdown ()
{
  if (shut_down) {
    return;
  }
  shut_down = true;

  destroy_blit_resources ();

  // Unload documents and destroy the context BEFORE dropping listener
  // adapters: elements hold non-owning listener pointers and teardown can
  // dispatch events (e.g. mouse-leave) while they are removed.
  document = nullptr;
  if (context != nullptr) {
    context->UnloadAllDocuments ();
    Rml::RemoveContext ("editor");
    context = nullptr;
  }
  controller.clear_event_listeners ();
  view.detach ();
  controller.detach ();

  wsl::gfx::rmlui_release ();

  if (wsl::gfx::rmlui_is_initialised ()) {
    // Scene-level UI managers still hold the runtime and reference our
    // shared backend through RmlUi's global pointer; keep the instances
    // alive for the rest of the process (teardown-only leak by design).
    (void)render_interface.release ();
    (void)system_interface.release ();
    (void)file_interface.release ();
  }

  wsl::gfx::set_shared_ui_render_interface (nullptr);
  wsl::gfx::set_shared_ui_system_interface (nullptr);
}

void
editor_ui::impl::handle_event (const wsl::engine_event &event)
{
  controller.handle_event (event);
}

void
editor_ui::impl::update (double dt)
{
  controller.update_frame (dt);
  view.update (dt);
}

void
editor_ui::impl::set_log_sink (log_sink *sink)
{
  view.set_log_sink (sink);
}

std::string
editor_ui::impl::document_root () const
{
  return editor_document_root (model.svc);
}

void
editor_ui::impl::render ()
{
  if ((context == nullptr) || (render_interface == nullptr)
      || (model.svc.window == nullptr)) {
    return;
  }

  auto &render_ctx = model.svc.runtime->render_ctx ();
  SDL_GPUCommandBuffer *cmd = render_ctx.command_buffer ();
  if (cmd == nullptr) {
    return;
  }

  const wsl::gfx::texture &swapchain = model.svc.window->swapchain ();
  if ((swapchain.texture_data == nullptr) || (swapchain.width == 0)
      || (swapchain.height == 0)) {
    return;
  }

  // Blit the scene's present texture into the game-view element's rect so
  // the docked game view looks like the ImGui editor's image panel.
  blit_game_view (cmd, swapchain);

  render_interface->BeginFrame (cmd, swapchain.texture_data, swapchain.width,
                                swapchain.height);
  context->Render ();
  render_interface->EndFrame ();
}

void
editor_ui::impl::blit_game_view (SDL_GPUCommandBuffer *cmd,
                                 const wsl::gfx::texture &swapchain)
{
  if (document == nullptr) {
    return;
  }

  Rml::Element *view = document->GetElementById ("gameview");
  const wsl::gfx::texture &present = model.svc.window->present_tex ();

  // The scene never writes the swapchain (present_to_swapchain is off for
  // this editor), so this pass clears the whole target to the editor
  // background and then blits the scene into the game-view rect only.
  SDL_GPUColorTargetInfo ct{};
  SDL_zero (ct);
  ct.texture = swapchain.texture_data;
  ct.load_op = SDL_GPU_LOADOP_CLEAR;
  ct.store_op = SDL_GPU_STOREOP_STORE;
  ct.clear_color = SDL_FColor{ 0.016F, 0.016F, 0.023F, 1.0F };

  SDL_GPURenderPass *pass = SDL_BeginGPURenderPass (cmd, &ct, 1, nullptr);
  if (pass == nullptr) {
    return;
  }

  if ((view != nullptr) && (present.texture_data != nullptr)
      && (blit_pipeline != nullptr) && (blit_sampler != nullptr)) {
    const Rml::Vector2f offset = view->GetAbsoluteOffset (Rml::BoxArea::Border);
    const Rml::Vector2f size = view->GetBox ().GetSize (Rml::BoxArea::Border);

    int x = static_cast<int> (offset.x);
    int y = static_cast<int> (offset.y);
    int w = static_cast<int> (size.x);
    int h = static_cast<int> (size.y);

    // Clip to the swapchain.
    if (x < 0) {
      w += x;
      x = 0;
    }
    if (y < 0) {
      h += y;
      y = 0;
    }
    w = std::min (w, static_cast<int> (swapchain.width) - x);
    h = std::min (h, static_cast<int> (swapchain.height) - y);

    if (w > 0 && h > 0) {
      SDL_GPUViewport viewport{};
      viewport.x = static_cast<float> (x);
      viewport.y = static_cast<float> (y);
      viewport.w = static_cast<float> (w);
      viewport.h = static_cast<float> (h);
      viewport.max_depth = 1.0F;
      SDL_SetGPUViewport (pass, &viewport);

      SDL_Rect scissor{ x, y, w, h };
      SDL_SetGPUScissor (pass, &scissor);

      SDL_BindGPUGraphicsPipeline (pass, blit_pipeline);

      SDL_GPUTextureSamplerBinding binding{ present.texture_data,
                                            blit_sampler };
      SDL_BindGPUFragmentSamplers (pass, 0, &binding, 1);

      SDL_DrawGPUPrimitives (pass, 3, 1, 0, 0);
    }
  }

  SDL_EndGPURenderPass (pass);
}

void
editor_ui::impl::create_blit_resources ()
{
  auto &render_ctx = model.svc.runtime->render_ctx ();
  SDL_GPUDevice *device = render_ctx.gpu_device;
  wsl::rsc::resource_manager &res_mgr = model.svc.runtime->resource_manager ();

  auto vs_id = res_mgr.register_shader (
      "engine://compiled_shaders/editor_blit.vert.slang.spv");
  auto fs_id = res_mgr.register_shader (
      "engine://compiled_shaders/editor_blit.frag.slang.spv");

  SDL_GPUShader *vs = wsl::gfx::shader::load_from_manager (
      device, &res_mgr, vs_id, SDL_GPU_SHADERSTAGE_VERTEX, 0, 0);
  SDL_GPUShader *fs = wsl::gfx::shader::load_from_manager (
      device, &res_mgr, fs_id, SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 1);
  if ((vs == nullptr) || (fs == nullptr)) {
    wsl::log::editor ()->error (
        "RmlUi editor UI: game-view blit shaders unavailable");
    if (vs != nullptr) {
      SDL_ReleaseGPUShader (device, vs);
    }
    if (fs != nullptr) {
      SDL_ReleaseGPUShader (device, fs);
    }
    return;
  }

  SDL_GPUGraphicsPipelineCreateInfo info{};
  SDL_zero (info);
  info.vertex_shader = vs;
  info.fragment_shader = fs;
  info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
  info.depth_stencil_state.enable_depth_test = false;
  info.depth_stencil_state.enable_depth_write = false;
  info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;

  SDL_GPUColorTargetDescription ct{};
  ct.format
      = SDL_GetGPUSwapchainTextureFormat (device, model.svc.window->handler ());
  info.target_info.num_color_targets = 1;
  info.target_info.color_target_descriptions = &ct;

  blit_pipeline = SDL_CreateGPUGraphicsPipeline (device, &info);

  SDL_ReleaseGPUShader (device, vs);
  SDL_ReleaseGPUShader (device, fs);

  SDL_GPUSamplerCreateInfo sampler_info{};
  SDL_zero (sampler_info);
  sampler_info.min_filter = SDL_GPU_FILTER_LINEAR;
  sampler_info.mag_filter = SDL_GPU_FILTER_LINEAR;
  sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
  sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  blit_sampler = SDL_CreateGPUSampler (device, &sampler_info);
}

void
editor_ui::impl::destroy_blit_resources ()
{
  if (model.svc.runtime == nullptr) {
    return;
  }
  SDL_GPUDevice *device = model.svc.runtime->render_ctx ().gpu_device;
  if (device == nullptr) {
    return;
  }
  if (blit_pipeline != nullptr) {
    SDL_ReleaseGPUGraphicsPipeline (device, blit_pipeline);
    blit_pipeline = nullptr;
  }
  if (blit_sampler != nullptr) {
    SDL_ReleaseGPUSampler (device, blit_sampler);
    blit_sampler = nullptr;
  }
}

// -------------------------------------------------------------- refresh

std::string
editor_ui::impl::load_editor_rml () const
{
  std::string text = read_text_file (document_root () + "/editor.rml");
  if (!text.empty ()) {
    return text;
  }
  return read_text_file (model.svc.asset_root + "/editor_rml/editor.rml");
}

void
editor_ui::impl::load_fonts () const
{
  static const char *const kFonts[]
      = { "splinesans-regular.otf", "splinesans-medium.otf",
          "splinesans-bold.otf", "hack-regular.otf" };
  for (const char *font : kFonts) {
    std::string path = model.svc.asset_root + "/otf/" + font;
    if (!std::filesystem::exists (path)) {
      path = model.svc.source_root + "/rsc/otf/" + font;
    }
    if (!std::filesystem::exists (path)) {
      continue;
    }
    if (!Rml::LoadFontFace (path)) {
      wsl::log::editor ()->warn ("RmlUi: failed to load font {}", path);
    }
  }
}

editor_ui::editor_ui (services svc)
    : m_impl (std::make_unique<impl> (std::move (svc)))
{
}

editor_ui::~editor_ui () { m_impl->shutdown (); }

void
editor_ui::set_log_sink (log_sink *sink)
{
  m_log_sink = sink;
  m_impl->set_log_sink (sink);
}

bool
editor_ui::initialize ()
{
  return m_impl->initialize ();
}

void
editor_ui::shutdown ()
{
  m_impl->shutdown ();
}

void
editor_ui::handle_event (const wsl::engine_event &event)
{
  m_impl->handle_event (event);
}

void
editor_ui::update (double dt)
{
  m_impl->update (dt);
}

void
editor_ui::render ()
{
  m_impl->render ();
}

} // namespace editor_rml
