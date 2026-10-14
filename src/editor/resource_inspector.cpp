#include "resource_inspector.hpp"

#include "rsc/resource_ids.hpp"
#include "rsc/resource_ref.hpp"
#include "wsl/comp/singl/editor_context.hpp"
#include "renderer_imgui.hpp"
#include "wsl/comp/singl/runtime_context.hpp"
#include "wsl/gfx/view_gizmo.hpp"
#include "wsl/rsc/resource_manager.hpp"

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <entt/core/fwd.hpp>
#include <entt/entity/entity.hpp>
#include <filesystem>
#include <system_error>
#include <glm/ext/vector_float3.hpp>
#include <glm/fwd.hpp>
#include <imgui.h>
#include <imsearch.h>
#include <string>
#include <vector>

namespace editor
{

static std::string
format_size (uintmax_t size)
{
  if (size == 0) {
    return "0 B";
  }
  if (size < 1024) {
    return std::to_string (size) + " B";
  }
  if (size < static_cast<uintmax_t> (1024 * 1024)) {
    return std::to_string (size / 1024) + " KB";
  }
  char buf[64];
  std::snprintf (buf, sizeof (buf), "%.2f MB",
                 (double)size / (1024.0 * 1024.0));
  return std::string (buf);
}

resource_inspector::resource_inspector (
    wsl::comp::singl::runtime_context *runtime_ctx,
    wsl::comp::singl::editor_context *editor_ctx)
    : m_runtime_ctx (runtime_ctx), m_editor_ctx (editor_ctx) {};

resource_inspector::~resource_inspector ()
{
  if (m_preview_track != nullptr) {
    MIX_DestroyTrack (m_preview_track);
  }
}

static const SDL_DialogFileFilter gltf_filters[] = {
  { "glTF files", "gltf;glb" },
  { "All files", "*" },
};

static const SDL_DialogFileFilter scene_filters[] = {
  { "Scene files", "scene;json" },
  { "Prefab files", "prefab" },
  { "All files", "*" },
};

static const SDL_DialogFileFilter texture_filters[] = {
  { "Image files", "png;jpg;jpeg;tga;bmp" },
  { "All files", "*" },
};

static const SDL_DialogFileFilter cubemap_filters[] = {
  { "Cubemap sources", "tar;png;hdr" },
  { "All files", "*" },
};

static const SDL_DialogFileFilter audio_filters[] = {
  { "Audio files", "wav;mp3;ogg" },
  { "All files", "*" },
};

static const char *
to_string (wsl::rsc::model_state s)
{
  switch (s) {
  case wsl::rsc::model_state::not_loaded:
    return "Not loaded";
  case wsl::rsc::model_state::loading_cpu:
    return "CPU Loading";
  case wsl::rsc::model_state::preparing_gpu:
    return "Preparing GPU";
  case wsl::rsc::model_state::uploading_gpu:
    return "GPU Uploading";
  case wsl::rsc::model_state::loaded:
    return "Loaded";
  }
  return "?";
}

static const char *
to_string (wsl::rsc::image_state s)
{
  switch (s) {
  case wsl::rsc::image_state::not_loaded:
    return "Not loaded";
  case wsl::rsc::image_state::loading:
    return "Loading";
  case wsl::rsc::image_state::loaded:
    return "Loaded";
  }
  return "?";
}

static const char *
to_string (wsl::rsc::scene_state s)
{
  switch (s) {
  case wsl::rsc::scene_state::not_loaded:
    return "Not loaded";
  case wsl::rsc::scene_state::loading:
    return "Loading";
  case wsl::rsc::scene_state::loaded:
    return "Loaded";
  }
  return "?";
}

static const char *
to_string (wsl::rsc::cubemap_state s)
{
  switch (s) {
  case wsl::rsc::cubemap_state::not_loaded:
    return "Not loaded";
  case wsl::rsc::cubemap_state::loading:
    return "Loading";
  case wsl::rsc::cubemap_state::loaded:
    return "Loaded";
  }
  return "?";
}

static const char *
to_string (wsl::rsc::audio_state s)
{
  switch (s) {
  case wsl::rsc::audio_state::not_loaded:
    return "Not loaded";
  case wsl::rsc::audio_state::loading:
    return "Loading";
  case wsl::rsc::audio_state::loaded:
    return "Loaded";
  }
  return "?";
}

static const char *
to_string (wsl::rsc::material_state s)
{
  switch (s) {
  case wsl::rsc::material_state::not_loaded:
    return "Not loaded";
  case wsl::rsc::material_state::loaded:
    return "Loaded";
  }
  return "?";
}

static const char *
to_string (wsl::rsc::shader_state s)
{
  switch (s) {
  case wsl::rsc::shader_state::not_loaded:
    return "Not loaded";
  case wsl::rsc::shader_state::loading:
    return "Loading";
  case wsl::rsc::shader_state::loaded:
    return "Loaded";
  }
  return "?";
}

static const char *
to_string (wsl::rsc::shader_program_state s)
{
  switch (s) {
  case wsl::rsc::shader_program_state::not_loaded:
    return "Not loaded";
  case wsl::rsc::shader_program_state::loaded:
    return "Loaded";
  }
  return "?";
}

void
resource_inspector::draw ()
{
  if (!m_visible || (m_runtime_ctx == nullptr)) {
    return;
  }

  if (!ImGui::Begin ("Resources")) {
    ImGui::End ();
    return;
  }

  enum class active_tab
  {
    models,
    images,
    scenes,
    audio,
    ui_layouts,
    fonts,
    materials,
    shaders
  };
  static active_tab tab = active_tab::models;

  // ---------- Splitter state ----------
  ImGuiIO const &io = ImGui::GetIO ();
  const float splitter_w = 6.0F;
  const float min_left_w = 220.0F;
  const float min_right_w = 260.0F;

  ImVec2 const avail = ImGui::GetContentRegionAvail ();

  static float left_w = 0.0F;
  if (left_w <= 0.0F) {
    left_w = avail.x * 0.55F; // initial ratio
  }

  // Clamp left width against current window size
  float effective_left_w = left_w;
  if (!m_show_preview) {
    effective_left_w = avail.x;
  } else {
    effective_left_w = std::max (
        min_left_w, std::min (left_w, avail.x - min_right_w - splitter_w));
  }

  // ================= LEFT PANEL =================
  ImGui::BeginChild ("ResourceInspector_Left", ImVec2 (effective_left_w, 0.0F),
                     1);

  if (ImGui::BeginTabBar ("ResourceTabs")) {

    if (ImGui::BeginTabItem ("Models")) {
      tab = active_tab::models;
      draw_models ();
      ImGui::EndTabItem ();
    }

    if (ImGui::BeginTabItem ("Textures")) {
      tab = active_tab::images;
      draw_images ();
      ImGui::EndTabItem ();
    }

    if (ImGui::BeginTabItem ("Scenes")) {
      tab = active_tab::scenes;
      draw_scenes ();
      ImGui::EndTabItem ();
    }

    if (ImGui::BeginTabItem ("Audio")) {
      tab = active_tab::audio;
      draw_audio ();
      ImGui::EndTabItem ();
    }

    if (ImGui::BeginTabItem ("UI Layouts")) {
      tab = active_tab::ui_layouts;
      draw_ui_layouts ();
      ImGui::EndTabItem ();
    }

    if (ImGui::BeginTabItem ("Fonts")) {
      tab = active_tab::fonts;
      draw_fonts ();
      ImGui::EndTabItem ();
    }

    if (ImGui::BeginTabItem ("Materials")) {
      tab = active_tab::materials;
      draw_materials ();
      ImGui::EndTabItem ();
    }

    if (ImGui::BeginTabItem ("Shaders")) {
      tab = active_tab::shaders;
      draw_shaders ();
      ImGui::EndTabItem ();
    }

    ImGui::EndTabBar ();
  }

  ImGui::EndChild ();

  if (m_show_preview) {
    ImGui::SameLine (0.0F, 0.0F);

    // ================= SPLITTER =================
    {
      // Splitter hitbox
      ImVec2 const splitter_pos = ImGui::GetCursorScreenPos ();
      ImGui::InvisibleButton ("ResourceInspector_Splitter",
                              ImVec2 (splitter_w, avail.y));

      // Draw splitter
      ImU32 col = ImGui::GetColorU32 (ImGuiCol_Separator);
      if (ImGui::IsItemHovered () || ImGui::IsItemActive ()) {
        col = ImGui::GetColorU32 (ImGuiCol_SeparatorHovered);
      }

      auto *dl = ImGui::GetWindowDrawList ();
      dl->AddRectFilled (
          splitter_pos,
          ImVec2 (splitter_pos.x + splitter_w, splitter_pos.y + avail.y), col);

      // Drag behavior
      if (ImGui::IsItemActive ()) {
        left_w += io.MouseDelta.x;
        left_w = std::max (
            min_left_w, std::min (left_w, avail.x - min_right_w - splitter_w));
      }

      // Nice cursor while hovering
      if (ImGui::IsItemHovered ()) {
        ImGui::SetMouseCursor (ImGuiMouseCursor_ResizeEW);
      }
    }

    ImGui::SameLine (0.0F, 0.0F);

    // ================= RIGHT PANEL =================
    ImGui::BeginChild ("ResourceInspector_Right", ImVec2 (0.0F, 0.0F), 1);

    // ================= RIGHT SIDE CONTENT
    // =================

    bool can_import = false;
    bool can_export = false;
    bool has_selection = false;
    entt::id_type selected_id = entt::null;
    wsl::rsc::io::resource_type type{};

    switch (tab) {
    case active_tab::models:
      can_import = true;
      if (m_selected_model != entt::null) {
        has_selection = true;
        selected_id = m_selected_model;
        type = wsl::rsc::io::resource_type::model;
      }
      break;

    case active_tab::images:
      can_import = true;
      if (m_selected_image != entt::null) {
        has_selection = true;
        selected_id = m_selected_image;
        type = wsl::rsc::io::resource_type::image;
      } else if (m_selected_cubemap != entt::null) {
        has_selection = true;
        selected_id = m_selected_cubemap;
        type = wsl::rsc::io::resource_type::cubemap;
      }
      break;

    case active_tab::scenes:
      can_import = true;
      can_export = true;
      has_selection = (m_selected_scene != entt::null);
      selected_id = m_selected_scene;
      type = wsl::rsc::io::resource_type::scene;
      break;

    case active_tab::audio:
      can_import = true;
      has_selection = (m_selected_audio != entt::null);
      selected_id = m_selected_audio;
      type = wsl::rsc::io::resource_type::audio;
      break;

    case active_tab::ui_layouts:
      can_import = false; // not implemented for UI yet
      has_selection = (m_selected_ui_layout != entt::null);
      selected_id = m_selected_ui_layout;
      break;

    case active_tab::fonts:
      can_import = false;
      has_selection = (m_selected_font != entt::null);
      selected_id = m_selected_font;
      break;

    case active_tab::materials:
      has_selection = (m_selected_material != entt::null);
      selected_id = m_selected_material;
      break;

    case active_tab::shaders:
      has_selection = (m_selected_shader != entt::null);
      selected_id = m_selected_shader;
      break;
    }

    wsl::rsc::scene *scene = m_runtime_ctx->scene_manager ().get_active ();
    const bool selected_scene_loaded
        = tab == active_tab::scenes && m_selected_scene != entt::null
          && m_runtime_ctx->resource_manager ().state (
                 wsl::rsc::scene_id{ m_selected_scene })
                 == wsl::rsc::scene_state::loaded;
    wsl::rsc::scene const *selected_loaded_scene
        = tab == active_tab::scenes && m_selected_scene != entt::null
              ? m_runtime_ctx->resource_manager ().find_loaded_scene (
                    wsl::rsc::scene_id{ m_selected_scene })
              : nullptr;

    // ---- Buttons ----
    if (tab == active_tab::scenes) {
      if (ImGui::Button ("New Scene")) {
        new_scene_dialog ();
      }
      ImGui::SameLine ();

      if (!has_selection) {
        ImGui::BeginDisabled ();
      }

      auto s_info = m_runtime_ctx->resource_manager ().info (
          wsl::rsc::scene_id{ m_selected_scene });
      bool const is_prefab = s_info && s_info->is_prefab;

      if (!has_selection) {
        ImGui::Button ("Load Scene");
      } else if (is_prefab) {
        if (ImGui::Button ("Instantiate Prefab")) {
          static_cast<void> (
              m_runtime_ctx->resource_manager ().instantiate_prefab (
                  wsl::rsc::scene_id{ m_selected_scene }));
        }
      } else if (!selected_scene_loaded) {
        if (ImGui::Button ("Load Scene")) {
          static_cast<void> (m_runtime_ctx->resource_manager ().load (
              wsl::rsc::scene_id{ m_selected_scene }));
        }
      } else {
        const bool is_active
            = selected_loaded_scene != nullptr
              && selected_loaded_scene
                     == m_runtime_ctx->scene_manager ().get_active ();

        if (is_active) {
          ImGui::BeginDisabled ();
        }

        if (ImGui::Button (is_active ? "Active Scene" : "Set Active")) {
          static_cast<void> (m_runtime_ctx->resource_manager ().activate_scene (
              wsl::rsc::scene_id{ m_selected_scene }));
        }

        if (is_active) {
          ImGui::EndDisabled ();
        }

        ImGui::SameLine ();
        if (ImGui::Button ("Unload Scene")) {
          m_runtime_ctx->resource_manager ().unload (
              wsl::rsc::scene_id{ m_selected_scene });
        }
      }

      if (!has_selection) {
        ImGui::EndDisabled ();
      }
    } else {
      if ((scene == nullptr) || !has_selection) {
        ImGui::BeginDisabled ();
      }

      if ((scene != nullptr) && has_selection) {
        bool const in_scene = scene->has_resource (type, selected_id);

        if (!in_scene) {
          if (ImGui::Button ("Add to Scene")) {
            scene->add_resource (type, selected_id);

            // Ensure pinned resources are loaded and stop being
            // preview-owned
            switch (type) {
            case wsl::rsc::io::resource_type::model:
              static_cast<void> (m_runtime_ctx->resource_manager ().load (
                  wsl::rsc::model_id{ selected_id }));
              m_runtime_ctx->resource_manager ()
                  .release_preview_ownership_if_matches (
                      wsl::rsc::model_id{ selected_id });
              break;
            case wsl::rsc::io::resource_type::image:
            case wsl::rsc::io::resource_type::cubemap:
            case wsl::rsc::io::resource_type::scene:
            case wsl::rsc::io::resource_type::audio:
              static_cast<void> (m_runtime_ctx->resource_manager ().load (
                  { type, selected_id }));
              break;
            case wsl::rsc::io::resource_type::material:
              static_cast<void> (m_runtime_ctx->resource_manager ().load (
                  wsl::rsc::material_id{ selected_id }));
              break;
            }
          }
        } else {
          if (ImGui::Button ("Remove from Scene")) {
            scene->remove_resource (type, selected_id);

            // If it is not the current temp preview, unload it now
            if (type != wsl::rsc::io::resource_type::model
                || m_runtime_ctx->resource_manager ()
                           .current_preview_model ()
                           .value
                       != selected_id) {

              switch (type) {
              case wsl::rsc::io::resource_type::model:
                m_runtime_ctx->resource_manager ().unload (
                    wsl::rsc::model_id{ selected_id });
                break;
              case wsl::rsc::io::resource_type::image:
              case wsl::rsc::io::resource_type::cubemap:
              case wsl::rsc::io::resource_type::scene:
              case wsl::rsc::io::resource_type::audio:
              case wsl::rsc::io::resource_type::material:
                m_runtime_ctx->resource_manager ().unload (
                    { type, selected_id });
                break;
              }
            }
          }
        }
      } else {
        ImGui::Button ("Add to Scene");
      }

      if ((scene == nullptr) || !has_selection) {
        ImGui::EndDisabled ();
      }
    }

    ImGui::SameLine ();

    if (!can_import) {
      ImGui::BeginDisabled ();
    }

    if (ImGui::Button ("Import")) {
      ImGui::OpenPopup ("ImportPopup");
    }

    if (ImGui::BeginPopup ("ImportPopup")) {
      if (tab == active_tab::models) {
        if (ImGui::MenuItem ("Import Model (.gltf, .glb)")) {
          import_model_dialog ();
        }
      } else if (tab == active_tab::images) {
        if (ImGui::MenuItem ("Import Image (.png, .jpg, ...)")) {
          import_image_dialog ();
        }
        if (ImGui::MenuItem ("Import Cubemap (.tar, .png, .hdr)")) {
          import_cubemap_dialog ();
        }
      } else if (tab == active_tab::scenes) {
        if (ImGui::MenuItem ("Import Scene (.json)")) {
          import_scene_dialog ();
        }
      } else if (tab == active_tab::audio) {
        if (ImGui::MenuItem ("Import Audio (.wav, .mp3, .ogg)")) {
          import_audio_dialog ();
        }
      }
      ImGui::EndPopup ();
    }

    if (!can_import) {
      ImGui::EndDisabled ();
    }

    ImGui::SameLine ();

    if (!can_export) {
      ImGui::BeginDisabled ();
    }

    if (ImGui::Button ("Export")) {
      if (tab == active_tab::scenes) {
        save_scene_dialog ();
      }
    }

    if (!can_export) {
      ImGui::EndDisabled ();
    }

    if (tab == active_tab::materials && has_selection) {
      ImGui::SameLine ();
      if (ImGui::Button ("Open in Shader Graph")) {
        auto info = m_runtime_ctx->resource_manager ().info (
            wsl::rsc::material_id{ m_selected_material });
        if (info) {
          // Replace .wslmat extension with .wslgraph
          std::string graph_path = info->path;
          auto dot = graph_path.rfind ('.');
          if (dot != std::string::npos) {
            graph_path = graph_path.substr (0, dot) + ".wslgraph";
          } else {
            graph_path += ".wslgraph";
          }
          m_request_open_shader_graph = graph_path;
        }
      }
    }

    ImGui::Separator ();

    // ---- Preview ----
    ImVec2 const preview_size = ImGui::GetContentRegionAvail ();

    SDL_GPUTexture const *preview_tex = nullptr;

    bool const should_show_checkerboard = true;

    ImGui::BeginChild ("PreviewChild", preview_size, 1,
                       ImGuiWindowFlags_NoScrollbar
                           | ImGuiWindowFlags_NoScrollWithMouse);

    ImVec2 const child_min = ImGui::GetCursorScreenPos ();
    ImVec2 const child_avail = ImGui::GetContentRegionAvail ();

    if (should_show_checkerboard && (m_editor_ctx != nullptr)) {
      entt::id_type model_to_preview = entt::null;
      if (tab == active_tab::models) {
        model_to_preview = m_selected_model;
      }

      m_editor_ctx->get_imgui_renderer ()->request_model_preview (
          m_runtime_ctx, &m_runtime_ctx->resource_manager (), model_to_preview,
          (uint32_t)child_avail.x, (uint32_t)child_avail.y);
      preview_tex
          = m_editor_ctx->get_imgui_renderer ()->get_model_preview_texture ();
    }

    auto *dl = ImGui::GetWindowDrawList ();
    const auto &theme = m_editor_ctx->get_imgui_renderer ()->get_theme ();
    ImU32 const color_a = ImGui::GetColorU32 (theme.background1);
    ImU32 const color_b = ImGui::GetColorU32 (theme.background2);

    // Draw 2D checkerboard background
    const float check_size = 16.0F;
    for (float y = 0; y < child_avail.y; y += check_size) {
      for (float x = 0; x < child_avail.x; x += check_size) {
        bool const dark = (static_cast<int> (x / check_size)
                           + static_cast<int> (y / check_size))
                              % 2
                          == 0;
        ImU32 const color = dark ? color_a : color_b;

        ImVec2 const p_min (child_min.x + x, child_min.y + y);
        ImVec2 const p_max (
            child_min.x + std::min (x + check_size, child_avail.x),
            child_min.y + std::min (y + check_size, child_avail.y));
        dl->AddRectFilled (p_min, p_max, color);
      }
    }

    if (preview_tex != nullptr) {
      ImGui::Image ((ImTextureID)preview_tex, child_avail);
      if ((preview_tex != nullptr) && tab == active_tab::models
          && (m_editor_ctx != nullptr) && m_selected_model != entt::null) {
        const float gizmo_scale = 0.30F;
        const float gizmo_half = 128.0F * gizmo_scale;
        const float pad = 8.0F;

        const glm::vec2 view_size (child_avail.x, child_avail.y);
        const glm::vec2 gizmo_center (view_size.x - gizmo_half - pad,
                                      gizmo_half + pad);
        const wsl::gfx::view_gizmo_layout gizmo_layout
            = wsl::gfx::view_gizmo_layout::from_pixel_center (
                gizmo_center, view_size, gizmo_scale);

        const ImVec2 mouse = ImGui::GetMousePos ();
        const ImVec2 mouse_delta = ImGui::GetIO ().MouseDelta;

        wsl::gfx::view_gizmo_input gizmo_input;
        gizmo_input.pointer_position = wsl::gfx::view_gizmo_ndc_from_pixel (
            glm::vec2 (mouse.x - child_min.x, mouse.y - child_min.y),
            view_size);
        gizmo_input.pointer_delta = wsl::gfx::view_gizmo_ndc_delta_from_pixel (
            glm::vec2 (mouse_delta.x, mouse_delta.y), view_size);
        gizmo_input.delta_time = ImGui::GetIO ().DeltaTime;
        gizmo_input.primary_down = ImGui::IsMouseDown (ImGuiMouseButton_Left);
        gizmo_input.primary_released
            = ImGui::IsMouseReleased (ImGuiMouseButton_Left);
        gizmo_input.pointer_over_viewport = ImGui::IsMouseHoveringRect (
            child_min,
            ImVec2 (child_min.x + child_avail.x, child_min.y + child_avail.y),
            false);

        m_editor_ctx->get_imgui_renderer ()->preview_navigate (gizmo_input,
                                                               gizmo_layout);
      }
    } else if (tab == active_tab::audio) {
      auto &mgr = m_runtime_ctx->resource_manager ();

      ImGui::SetCursorScreenPos (
          ImVec2 (child_min.x + (child_avail.x * 0.5F) - 100,
                  child_min.y + (child_avail.y * 0.5F) - 20));

      if (m_selected_audio == entt::null) {
        ImGui::BeginDisabled ();
        ImGui::Button ("Play", ImVec2 (95, 40));
        ImGui::SameLine ();
        ImGui::Button ("Stop", ImVec2 (95, 40));
        ImGui::EndDisabled ();
      } else {
        MIX_Audio *audio = mgr.get (wsl::rsc::audio_id{
            static_cast<entt::id_type> (m_selected_audio) });

        if (audio == nullptr) {
          if (ImGui::Button ("Load Audio", ImVec2 (200, 40))) {
            static_cast<void> (
                mgr.load (wsl::rsc::audio_id{ m_selected_audio }));
          }
        } else {
          if ((m_preview_track == nullptr) && (mgr.mixer () != nullptr)) {
            m_preview_track = MIX_CreateTrack (mgr.mixer ());
          }

          if (ImGui::Button ("Play", ImVec2 (95, 40))) {
            if (m_preview_track != nullptr) {
              MIX_SetTrackAudio (m_preview_track, audio);
              MIX_PlayTrack (m_preview_track, 0);
            }
          }
          ImGui::SameLine ();
          if (ImGui::Button ("Stop", ImVec2 (95, 40))) {
            if (m_preview_track != nullptr) {
              MIX_StopTrack (m_preview_track, 0);
            }
          }
        }
      }
    } else {
      ImGui::SetCursorScreenPos (ImVec2 (child_min.x + 10, child_min.y + 10));
      ImGui::TextDisabled ("Preview");
    }

    // ---- Overlayed Info ----
    {
      auto &mgr = m_runtime_ctx->resource_manager ();
      std::string info;
      auto make_hex = [] (entt::id_type v) {
        char buf[64];
        std::snprintf (buf, sizeof (buf), "0x%04llx", (unsigned long long)v);
        return std::string (buf);
      };

      switch (tab) {
      case active_tab::models:
        if (m_selected_model != entt::null) {
          if (auto rec = mgr.info (wsl::rsc::model_id{ m_selected_model })) {
            info = "Model\nName: " + rec->name + "\nPath: " + rec->path
                   + "\nState: " + to_string (rec->state)
                   + "\nID: " + make_hex (m_selected_model);
          }
        } else if (m_selected_cubemap != entt::null) {
          if (auto rec
              = mgr.info (wsl::rsc::cubemap_id{ m_selected_cubemap })) {
            info = "Cubemap\nName: " + rec->name + "\nPath: " + rec->path
                   + "\nState: " + to_string (rec->state)
                   + "\nID: " + make_hex (m_selected_cubemap);
          }
        }
        break;
      case active_tab::images:
        if (m_selected_image != entt::null) {
          if (auto rec = mgr.info (wsl::rsc::image_id{ m_selected_image })) {
            info = "Image\nName: " + rec->name + "\nPath: " + rec->path
                   + "\nState: " + to_string (rec->state)
                   + "\nID: " + make_hex (m_selected_image);
          }
        }
        break;
      case active_tab::scenes:
        if (m_selected_scene != entt::null) {
          if (auto rec = mgr.info (wsl::rsc::scene_id{ m_selected_scene })) {
            info = "Scene\nName: " + rec->name + "\nPath: " + rec->path
                   + "\nState: " + to_string (rec->state)
                   + "\nID: " + make_hex (m_selected_scene);
          }
        }
        break;
      case active_tab::audio:
        if (m_selected_audio != entt::null) {
          if (auto rec = mgr.info (wsl::rsc::audio_id{ m_selected_audio })) {
            info = "Audio\nName: " + rec->name + "\nPath: " + rec->path
                   + "\nState: " + to_string (rec->state)
                   + "\nID: " + make_hex (m_selected_audio);
          }
        }
        break;
      case active_tab::ui_layouts:
        if (m_selected_ui_layout != entt::null) {
          if (auto rec
              = mgr.info (wsl::rsc::ui_layout_id{ m_selected_ui_layout })) {
            info = "UI Layout\nName: " + rec->name + "\nPath: " + rec->path
                   + "\nID: " + make_hex (m_selected_ui_layout);
          }
        }
        break;
      case active_tab::fonts:
        if (m_selected_font != entt::null) {
          if (auto rec = mgr.info (wsl::rsc::font_id{ m_selected_font })) {
            info = "Font\nName: " + rec->name + "\nPath: " + rec->path
                   + "\nID: " + make_hex (m_selected_font);
          }
        }
        break;
      case active_tab::materials:
        if (m_selected_material != entt::null) {
          if (auto rec
              = mgr.info (wsl::rsc::material_id{ m_selected_material })) {
            info = "Material\nName: " + rec->name + "\nPath: " + rec->path
                   + "\nState: " + to_string (rec->state)
                   + "\nID: " + make_hex (m_selected_material);
          }
        }
        break;
      case active_tab::shaders:
        if (m_selected_shader != entt::null) {
          if (auto rec
              = mgr.info (wsl::rsc::shader_program_id{ m_selected_shader })) {
            info = "Shader\nName: " + rec->name + "\nPath: " + rec->path
                   + "\nState: " + to_string (rec->state)
                   + "\nID: " + make_hex (m_selected_shader);
          }
        }
        break;
      }

      if (!info.empty ()) {
        const float text_scale = 0.90F;
        float max_w = 0.0F;
        int lines = 1;
        for (size_t i = 0, start = 0; i <= info.size (); ++i) {
          if (i == info.size () || info[i] == '\n') {
            std::string const line = info.substr (start, i - start);
            max_w = std::max (max_w, ImGui::CalcTextSize (line.c_str ()).x);
            start = i + 1;
            if (i != info.size ()) {
              lines++;
            }
          }
        }
        float const line_h = ImGui::GetTextLineHeight () * text_scale;
        ImVec2 const text_size (max_w * text_scale,
                                static_cast<float> (lines) * line_h);
        const float margin = 8.0F;
        const float box_pad = 6.0F;
        ImVec2 const text_pos (child_min.x + margin, child_min.y + child_avail.y
                                                         - margin
                                                         - text_size.y);
        ImVec2 const box_min (text_pos.x - box_pad, text_pos.y - box_pad);
        ImVec2 const box_max (text_pos.x + text_size.x + box_pad,
                              text_pos.y + text_size.y + box_pad);
        auto *dl2 = ImGui::GetWindowDrawList ();
        dl2->AddRectFilled (box_min, box_max, IM_COL32 (0, 0, 0, 160), 4.0F);
        dl2->AddRect (box_min, box_max, IM_COL32 (255, 255, 255, 40), 4.0F);
        dl2->AddText (nullptr, ImGui::GetFontSize () * text_scale, text_pos,
                      IM_COL32 (255, 255, 255, 230), info.c_str ());
      }
    }
    ImGui::EndChild (); // PreviewChild
    ImGui::EndChild (); // ResourceInspector_Right
  }

  ImGui::End (); // Resources window
}

void
resource_inspector::draw_models ()
{
  auto &mgr = m_runtime_ctx->resource_manager ();
  ImGui::BeginChild ("ModelListChild", ImVec2 (0, 0), 1);
  if (ImSearch::BeginSearch ()) {
    bool pushed = false;
    if (m_show_preview) {
      ImGui::PushStyleColor (ImGuiCol_Button,
                             ImGui::GetStyle ().Colors[ImGuiCol_ButtonActive]);
      pushed = true;
    }
    if (ImGui::Button (m_show_preview ? "P##toggle" : "P", ImVec2 (24, 24))) {
      m_show_preview = !m_show_preview;
    }
    if (pushed) {
      ImGui::PopStyleColor ();
    }

    ImGui::SameLine ();
    ImSearch::SearchBar ();
    ImGui::Separator ();

    static ImGuiTableFlags const flags
        = ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable
          | ImGuiTableFlags_Hideable | ImGuiTableFlags_BordersOuter
          | ImGuiTableFlags_BordersV | ImGuiTableFlags_ScrollY
          | ImGuiTableFlags_RowBg;
    if (ImGui::BeginTable ("ModelTable", 5, flags)) {
      ImGui::TableSetupColumn ("Name", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn ("Type", ImGuiTableColumnFlags_WidthFixed,
                               100.0F);
      ImGui::TableSetupColumn ("Size", ImGuiTableColumnFlags_WidthFixed, 80.0F);
      ImGui::TableSetupColumn ("LODs", ImGuiTableColumnFlags_WidthFixed, 50.0F);
      ImGui::TableSetupColumn ("State", ImGuiTableColumnFlags_WidthFixed,
                               80.0F);
      ImGui::TableHeadersRow ();
      auto models = mgr.list_models ();
      struct entry
      {
        entt::id_type id;
        std::string name, path, type, state_str;
        uintmax_t size;
        int lods;
      };
      std::vector<entry> entries;
      const bool has_project
          = (m_runtime_ctx->resource_manager ().current_project () != nullptr);
      for (const auto &m : models) {
        if (!has_project
            && (m.path.find ("builtin://") != std::string::npos
                || m.path.find ("engine://") != std::string::npos)) {
          continue;
        }

        int lod_count = 0;
        if (m.state == wsl::rsc::model_state::loaded) {
          auto handle = mgr.get (wsl::rsc::model_id{ m.id });
          if (handle && !handle->lod_groups.empty ()) {
            lod_count = (int)handle->lod_groups[0].levels.size ();
          }
        }
        uintmax_t fsize = 0;
        std::error_code fsize_ec;
        fsize
            = std::filesystem::file_size (mgr.resolve_path (m.path), fsize_ec);
        if (fsize_ec) {
          fsize = 0;
        }
        entries.push_back ({ m.id, m.name, m.path, "gltf model",
                             to_string (m.state), fsize, lod_count });
      }
      for (const auto &e : entries) {
        ImSearch::SearchableItem (e.name.c_str (), [&] (const char *) {
          ImGui::PushID ((int)e.id);
          ImGui::TableNextRow ();
          ImGui::TableNextColumn ();
          bool const selected = (m_selected_model == e.id);
          if (ImGui::Selectable (e.name.c_str (), selected,
                                 ImGuiSelectableFlags_SpanAllColumns
                                     | ImGuiSelectableFlags_AllowOverlap)) {
            m_selected_model = e.id;
            m_selected_cubemap = entt::null;
            m_selected_lod_group = -1;
            m_selected_lod_level = -1;
            wsl::rsc::scene const *scene
                = m_runtime_ctx->scene_manager ().get_active ();
            const bool pinned = (scene
                                 && scene->has_resource (
                                     wsl::rsc::io::resource_type::model, e.id));
            if (!pinned) {
              {
                m_runtime_ctx->resource_manager ().load_preview_model_low_lod (
                    wsl::rsc::model_id{ e.id });
              }
            } else {
              static_cast<void> (m_runtime_ctx->resource_manager ().load (
                  wsl::rsc::model_id{ e.id }));
              m_runtime_ctx->resource_manager ()
                  .release_preview_ownership_if_matches (
                      wsl::rsc::model_id{ e.id });
            }
          }
          ImGui::TableNextColumn ();
          ImGui::TextUnformatted (e.type.c_str ());
          ImGui::TableNextColumn ();
          ImGui::TextUnformatted (format_size (e.size).c_str ());
          ImGui::TableNextColumn ();
          if (e.lods > 0) {
            ImGui::Text ("%d", e.lods);
          } else {
            ImGui::Text ("-");
          }
          ImGui::TableNextColumn ();
          ImGui::TextUnformatted (e.state_str.c_str ());
          ImGui::PopID ();
        });
      }
      ImSearch::Submit ();
      ImGui::EndTable ();
    }
    ImSearch::EndSearch ();
  }
  ImGui::EndChild ();
}

void
resource_inspector::draw_images ()
{
  auto &mgr = m_runtime_ctx->resource_manager ();
  ImGui::BeginChild ("ImageListChild", ImVec2 (0, 0), 1);
  if (ImSearch::BeginSearch ()) {
    bool pushed = false;
    if (m_show_preview) {
      ImGui::PushStyleColor (ImGuiCol_Button,
                             ImGui::GetStyle ().Colors[ImGuiCol_ButtonActive]);
      pushed = true;
    }
    if (ImGui::Button (m_show_preview ? "P##toggle" : "P", ImVec2 (24, 24))) {
      m_show_preview = !m_show_preview;
    }
    if (pushed) {
      ImGui::PopStyleColor ();
    }

    ImGui::SameLine ();
    ImSearch::SearchBar ();
    ImGui::Separator ();

    static ImGuiTableFlags const flags
        = ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable
          | ImGuiTableFlags_Hideable | ImGuiTableFlags_BordersOuter
          | ImGuiTableFlags_BordersV | ImGuiTableFlags_ScrollY
          | ImGuiTableFlags_RowBg;
    if (ImGui::BeginTable ("TextureTable", 4, flags)) {
      ImGui::TableSetupColumn ("Name", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn ("Type", ImGuiTableColumnFlags_WidthFixed,
                               100.0F);
      ImGui::TableSetupColumn ("Size", ImGuiTableColumnFlags_WidthFixed, 80.0F);
      ImGui::TableSetupColumn ("State", ImGuiTableColumnFlags_WidthFixed,
                               80.0F);
      ImGui::TableHeadersRow ();
      auto images = mgr.list_images ();
      auto cubemaps = mgr.list_cubemaps ();
      struct entry
      {
        entt::id_type id;
        std::string name, path, type, state_str;
        bool is_cubemap;
        uintmax_t size;
      };
      std::vector<entry> entries;
      const bool has_project
          = (m_runtime_ctx->resource_manager ().current_project () != nullptr);
      for (const auto &img : images) {
        uintmax_t fsize = 0;
        try {
          fsize = std::filesystem::file_size (mgr.resolve_path (img.path));
        } catch (...) {
        }
        entries.push_back ({ img.id, img.name, img.path, "image",
                             to_string (img.state), false, fsize });
      }
      for (const auto &c : cubemaps) {
        if (!has_project
            && (c.path.find ("builtin/") != std::string::npos
                || c.path.find ("engine/") != std::string::npos)) {
          continue;
        }

        uintmax_t fsize = 0;
        try {
          fsize = std::filesystem::file_size (mgr.resolve_path (c.path));
        } catch (...) {
        }
        std::string type = "cubemap";
        std::filesystem::path const p (c.path);
        std::string ext = p.extension ().string ();
        for (auto &ch : ext) {
          ch = (char)std::tolower (ch);
        }
        if (ext == ".tar") {
          type = "tar cubemap";
        } else if (ext == ".hdr") {
          type = "hdr cubemap";
        } else if (ext == ".png") {
          type = "png cubemap";
        }
        entries.push_back (
            { c.id, c.name, c.path, type, to_string (c.state), true, fsize });
      }
      for (const auto &e : entries) {
        ImSearch::SearchableItem (e.name.c_str (), [&] (const char *) {
          ImGui::PushID (e.is_cubemap ? (int)e.id + 2000000 : (int)e.id);
          ImGui::TableNextRow ();
          ImGui::TableNextColumn ();
          bool const selected = e.is_cubemap ? (m_selected_cubemap == e.id)
                                             : (m_selected_image == e.id);
          if (ImGui::Selectable (e.name.c_str (), selected,
                                 ImGuiSelectableFlags_SpanAllColumns
                                     | ImGuiSelectableFlags_AllowOverlap)) {
            if (e.is_cubemap) {
              m_selected_cubemap = e.id;
              m_selected_image = entt::null;
            } else {
              m_selected_image = e.id;
              m_selected_cubemap = entt::null;
            }
          }
          ImGui::TableNextColumn ();
          ImGui::TextUnformatted (e.type.c_str ());
          ImGui::TableNextColumn ();
          ImGui::TextUnformatted (format_size (e.size).c_str ());
          ImGui::TableNextColumn ();
          ImGui::TextUnformatted (e.state_str.c_str ());
          ImGui::PopID ();
        });
      }
      ImSearch::Submit ();
      ImGui::EndTable ();
    }
    ImSearch::EndSearch ();
  }
  ImGui::EndChild ();
}

void
resource_inspector::draw_scenes ()
{
  auto &mgr = m_runtime_ctx->resource_manager ();
  ImGui::BeginChild ("SceneListChild", ImVec2 (0, 0), 1);
  if (ImSearch::BeginSearch ()) {
    bool pushed = false;
    if (m_show_preview) {
      ImGui::PushStyleColor (ImGuiCol_Button,
                             ImGui::GetStyle ().Colors[ImGuiCol_ButtonActive]);
      pushed = true;
    }
    if (ImGui::Button (m_show_preview ? "P##toggle" : "P", ImVec2 (24, 24))) {
      m_show_preview = !m_show_preview;
    }
    if (pushed) {
      ImGui::PopStyleColor ();
    }

    ImGui::SameLine ();
    ImSearch::SearchBar ();
    ImGui::Separator ();

    static ImGuiTableFlags const flags
        = ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable
          | ImGuiTableFlags_Hideable | ImGuiTableFlags_BordersOuter
          | ImGuiTableFlags_BordersV | ImGuiTableFlags_ScrollY
          | ImGuiTableFlags_RowBg;
    if (ImGui::BeginTable ("SceneTable", 3, flags)) {
      ImGui::TableSetupColumn ("Name", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn ("Type", ImGuiTableColumnFlags_WidthFixed, 80.0F);
      ImGui::TableSetupColumn ("State", ImGuiTableColumnFlags_WidthFixed,
                               100.0F);
      ImGui::TableHeadersRow ();
      for (const auto &rec : mgr.list_scenes ()) {
        ImSearch::SearchableItem (rec.name.c_str (), [&] (const char *) {
          ImGui::PushID ((int)rec.id);
          ImGui::TableNextRow ();
          ImGui::TableNextColumn ();
          bool const selected = (m_selected_scene == rec.id);
          if (ImGui::Selectable (rec.name.c_str (), selected,
                                 ImGuiSelectableFlags_SpanAllColumns
                                     | ImGuiSelectableFlags_AllowOverlap)) {
            m_selected_scene = rec.id;
          }
          if (ImGui::IsItemHovered () && ImGui::IsMouseDoubleClicked (0)) {
            if (rec.is_prefab) {
              static_cast<void> (
                  m_runtime_ctx->resource_manager ().instantiate_prefab (
                      wsl::rsc::scene_id{ rec.id }));
            } else {
              static_cast<void> (m_runtime_ctx->resource_manager ().load (
                  wsl::rsc::scene_id{ rec.id }));
            }
          }
          ImGui::TableNextColumn ();
          ImGui::TextUnformatted (rec.is_prefab ? "Prefab" : "Scene");
          ImGui::TableNextColumn ();
          ImGui::TextUnformatted (to_string (rec.state));
          ImGui::PopID ();
        });
      }
      ImSearch::Submit ();
      ImGui::EndTable ();
    }
    ImSearch::EndSearch ();
  }
  ImGui::EndChild ();
}

void
resource_inspector::import_model_dialog ()
{
  SDL_ShowOpenFileDialog (
      [] (void *userdata, const char *const *files, int) {
        auto *self = static_cast<resource_inspector *> (userdata);
        if (!files || !files[0]) {
          return;
        }
        static_cast<void> (
            self->m_runtime_ctx->resource_manager ().import_model (files[0]));
      },
      this,
      (m_runtime_ctx != nullptr) ? m_runtime_ctx->window ().handler ()
                                 : nullptr,
      gltf_filters, SDL_arraysize (gltf_filters), nullptr, false);
}

void
resource_inspector::import_image_dialog ()
{
  SDL_ShowOpenFileDialog (
      [] (void *userdata, const char *const *files, int) {
        auto *self = static_cast<resource_inspector *> (userdata);
        if (!files || !files[0]) {
          return;
        }
        static_cast<void> (
            self->m_runtime_ctx->resource_manager ().import_image (files[0]));
      },
      this,
      (m_runtime_ctx != nullptr) ? m_runtime_ctx->window ().handler ()
                                 : nullptr,
      texture_filters, SDL_arraysize (texture_filters), nullptr, false);
}

void
resource_inspector::import_scene_dialog ()
{
  SDL_ShowOpenFileDialog (
      [] (void *userdata, const char *const *files, int) {
        auto *self = static_cast<resource_inspector *> (userdata);
        if (!files || !files[0]) {
          return;
        }
        static_cast<void> (
            self->m_runtime_ctx->resource_manager ().import_scene (files[0]));
      },
      this,
      (m_runtime_ctx != nullptr) ? m_runtime_ctx->window ().handler ()
                                 : nullptr,
      scene_filters, SDL_arraysize (scene_filters), nullptr, false);
}

void
resource_inspector::save_scene_dialog ()
{
  SDL_ShowSaveFileDialog (
      [] (void *userdata, const char *const *files, int) {
        auto *self = static_cast<resource_inspector *> (userdata);
        if (!files || !files[0]) {
          return;
        }
        std::filesystem::path const p (files[0]);
        bool const is_prefab = p.extension () == ".prefab";
        wsl::rsc::scene const *scene
            = self->m_runtime_ctx->scene_manager ().get_active ();
        if (scene) {
          self->m_runtime_ctx->resource_manager ().save_scene (*scene, files[0],
                                                               is_prefab);
        }
      },
      this,
      (m_runtime_ctx != nullptr) ? m_runtime_ctx->window ().handler ()
                                 : nullptr,
      scene_filters, SDL_arraysize (scene_filters), nullptr);
}

void
resource_inspector::new_scene_dialog ()
{
  SDL_ShowSaveFileDialog (
      [] (void *userdata, const char *const *files, int) {
        auto *self = static_cast<resource_inspector *> (userdata);
        if (!files || !files[0]) {
          return;
        }
        std::string const path (files[0]);
        std::filesystem::path const p (path);
        std::string const name = p.stem ().string ();
        bool const is_prefab = p.extension () == ".prefab";

        auto &scene
            = self->m_runtime_ctx->scene_manager ().create_default_scene (name,
                                                                          true);
        self->m_runtime_ctx->resource_manager ().save_scene (scene, path,
                                                             is_prefab);
        static_cast<void> (
            self->m_runtime_ctx->resource_manager ().register_scene (path));
      },
      this,
      (m_runtime_ctx != nullptr) ? m_runtime_ctx->window ().handler ()
                                 : nullptr,
      scene_filters, SDL_arraysize (scene_filters), nullptr);
}

void
resource_inspector::import_cubemap_dialog ()
{
  SDL_ShowOpenFileDialog (
      [] (void *userdata, const char *const *files, int) {
        auto *self = static_cast<resource_inspector *> (userdata);
        if (!files || !files[0]) {
          return;
        }
        static_cast<void> (
            self->m_runtime_ctx->resource_manager ().import_cubemap (files[0]));
      },
      this,
      (m_runtime_ctx != nullptr) ? m_runtime_ctx->window ().handler ()
                                 : nullptr,
      cubemap_filters, SDL_arraysize (cubemap_filters), nullptr, false);
}

void
resource_inspector::draw_audio ()
{
  auto &mgr = m_runtime_ctx->resource_manager ();
  ImGui::BeginChild ("AudioListChild", ImVec2 (0, 0), 1);
  if (ImSearch::BeginSearch ()) {
    bool pushed = false;
    if (m_show_preview) {
      ImGui::PushStyleColor (ImGuiCol_Button,
                             ImGui::GetStyle ().Colors[ImGuiCol_ButtonActive]);
      pushed = true;
    }
    if (ImGui::Button (m_show_preview ? "P##toggle" : "P", ImVec2 (24, 24))) {
      m_show_preview = !m_show_preview;
    }
    if (pushed) {
      ImGui::PopStyleColor ();
    }

    ImGui::SameLine ();
    ImSearch::SearchBar ();
    ImGui::Separator ();
    for (const auto &rec : mgr.list_audio ()) {
      ImSearch::SearchableItem (rec.name.c_str (), [&] (const char *) {
        ImGui::PushID ((int)rec.id);
        bool const selected = (m_selected_audio == rec.id);
        std::string const text = rec.name + " (" + to_string (rec.state) + ")";
        if (ImGui::Selectable (text.c_str (), selected)) {
          m_selected_audio = rec.id;
        }
        ImGui::PopID ();
      });
    }
    ImSearch::EndSearch ();
  }
  ImGui::EndChild ();
}

void
resource_inspector::import_audio_dialog ()
{
  SDL_ShowOpenFileDialog (
      [] (void *userdata, const char *const *files, int) {
        auto *self = static_cast<resource_inspector *> (userdata);
        if (!files || !files[0]) {
          return;
        }
        static_cast<void> (
            self->m_runtime_ctx->resource_manager ().import_audio (files[0]));
      },
      this,
      (m_runtime_ctx != nullptr) ? m_runtime_ctx->window ().handler ()
                                 : nullptr,
      audio_filters, SDL_arraysize (audio_filters), nullptr, false);
}

void
resource_inspector::draw_ui_layouts ()
{
  auto &mgr = m_runtime_ctx->resource_manager ();
  ImGui::BeginChild ("UIListChild", ImVec2 (0, 0), 1);
  if (ImSearch::BeginSearch ()) {
    bool pushed = false;
    if (m_show_preview) {
      ImGui::PushStyleColor (ImGuiCol_Button,
                             ImGui::GetStyle ().Colors[ImGuiCol_ButtonActive]);
      pushed = true;
    }
    if (ImGui::Button (m_show_preview ? "P##toggle" : "P", ImVec2 (24, 24))) {
      m_show_preview = !m_show_preview;
    }
    if (pushed) {
      ImGui::PopStyleColor ();
    }

    ImGui::SameLine ();
    ImSearch::SearchBar ();
    ImGui::Separator ();

    static ImGuiTableFlags const flags
        = ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable
          | ImGuiTableFlags_Hideable | ImGuiTableFlags_BordersOuter
          | ImGuiTableFlags_BordersV | ImGuiTableFlags_ScrollY
          | ImGuiTableFlags_RowBg;
    if (ImGui::BeginTable ("UITable", 2, flags)) {
      ImGui::TableSetupColumn ("Name", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn ("Type", ImGuiTableColumnFlags_WidthFixed, 80.0F);
      ImGui::TableHeadersRow ();
      for (const auto &rec : mgr.list_ui_layouts ()) {
        ImSearch::SearchableItem (rec.name.c_str (), [&] (const char *) {
          ImGui::PushID ((int)rec.id);
          ImGui::TableNextRow ();
          ImGui::TableNextColumn ();
          bool const selected = (m_selected_ui_layout == rec.id);
          if (ImGui::Selectable (rec.name.c_str (), selected,
                                 ImGuiSelectableFlags_SpanAllColumns
                                     | ImGuiSelectableFlags_AllowOverlap)) {
            m_selected_ui_layout = rec.id;
          }
          ImGui::TableNextColumn ();
          std::filesystem::path const p (rec.path);
          std::string ext = p.extension ().string ();
          if (!ext.empty () && ext[0] == '.') {
            ext = ext.substr (1);
          }
          ImGui::TextUnformatted (ext.c_str ());
          ImGui::PopID ();
        });
      }
      ImSearch::Submit ();
      ImGui::EndTable ();
    }
    ImSearch::EndSearch ();
  }
  ImGui::EndChild ();
}

void
resource_inspector::draw_fonts ()
{
  auto &mgr = m_runtime_ctx->resource_manager ();
  ImGui::BeginChild ("FontListChild", ImVec2 (0, 0), 1);
  if (ImSearch::BeginSearch ()) {
    bool pushed = false;
    if (m_show_preview) {
      ImGui::PushStyleColor (ImGuiCol_Button,
                             ImGui::GetStyle ().Colors[ImGuiCol_ButtonActive]);
      pushed = true;
    }
    if (ImGui::Button (m_show_preview ? "P##toggle" : "P", ImVec2 (24, 24))) {
      m_show_preview = !m_show_preview;
    }
    if (pushed) {
      ImGui::PopStyleColor ();
    }

    ImGui::SameLine ();
    ImSearch::SearchBar ();
    ImGui::Separator ();

    static ImGuiTableFlags const flags
        = ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable
          | ImGuiTableFlags_Hideable | ImGuiTableFlags_BordersOuter
          | ImGuiTableFlags_BordersV | ImGuiTableFlags_ScrollY
          | ImGuiTableFlags_RowBg;
    if (ImGui::BeginTable ("FontTable", 2, flags)) {
      ImGui::TableSetupColumn ("Name", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn ("Type", ImGuiTableColumnFlags_WidthFixed, 80.0F);
      ImGui::TableHeadersRow ();
      for (const auto &rec : mgr.list_fonts ()) {
        ImSearch::SearchableItem (rec.name.c_str (), [&] (const char *) {
          ImGui::PushID ((int)rec.id);
          ImGui::TableNextRow ();
          ImGui::TableNextColumn ();
          bool const selected = (m_selected_font == rec.id);
          if (ImGui::Selectable (rec.name.c_str (), selected,
                                 ImGuiSelectableFlags_SpanAllColumns
                                     | ImGuiSelectableFlags_AllowOverlap)) {
            m_selected_font = rec.id;
          }
          ImGui::TableNextColumn ();
          std::filesystem::path const p (rec.path);
          std::string ext = p.extension ().string ();
          if (!ext.empty () && ext[0] == '.') {
            ext = ext.substr (1);
          }
          ImGui::TextUnformatted (ext.c_str ());
          ImGui::PopID ();
        });
      }
      ImSearch::Submit ();
      ImGui::EndTable ();
    }
    ImSearch::EndSearch ();
  }
  ImGui::EndChild ();
}

void
resource_inspector::draw_materials ()
{
  if (ImSearch::BeginSearch ()) {
    auto &mgr = m_runtime_ctx->resource_manager ();

    ImGui::SameLine ();
    ImSearch::SearchBar ();
    ImGui::Separator ();

    static ImGuiTableFlags const flags
        = ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable
          | ImGuiTableFlags_Hideable | ImGuiTableFlags_BordersOuter
          | ImGuiTableFlags_BordersV | ImGuiTableFlags_ScrollY
          | ImGuiTableFlags_RowBg;
    if (ImGui::BeginTable ("MaterialTable", 3, flags)) {
      ImGui::TableSetupColumn ("Name", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn ("State", ImGuiTableColumnFlags_WidthFixed,
                               80.0F);
      ImGui::TableSetupColumn ("Shader", ImGuiTableColumnFlags_WidthFixed,
                               100.0F);
      ImGui::TableHeadersRow ();

      for (const auto &rec : mgr.list_materials ()) {
        ImSearch::SearchableItem (rec.name.c_str (), [&] (const char *) {
          ImGui::PushID ((int)rec.id);
          ImGui::TableNextRow ();
          ImGui::TableNextColumn ();
          bool const selected = (m_selected_material == rec.id);
          if (ImGui::Selectable (rec.name.c_str (), selected,
                                 ImGuiSelectableFlags_SpanAllColumns
                                     | ImGuiSelectableFlags_AllowOverlap)) {
            m_selected_material = rec.id;
          }
          ImGui::TableNextColumn ();
          ImGui::TextUnformatted (to_string (rec.state));
          ImGui::TableNextColumn ();
          ImGui::Text ("%u", rec.shader_program_id);
          ImGui::PopID ();
        });
      }
      ImSearch::Submit ();
      ImGui::EndTable ();
    }
    ImSearch::EndSearch ();
  }
}

void
resource_inspector::draw_shaders ()
{
  if (ImSearch::BeginSearch ()) {
    auto &mgr = m_runtime_ctx->resource_manager ();

    ImGui::SameLine ();
    ImSearch::SearchBar ();
    ImGui::Separator ();

    static ImGuiTableFlags const flags
        = ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable
          | ImGuiTableFlags_Hideable | ImGuiTableFlags_BordersOuter
          | ImGuiTableFlags_BordersV | ImGuiTableFlags_ScrollY
          | ImGuiTableFlags_RowBg;
    if (ImGui::BeginTable ("ShaderTable", 2, flags)) {
      ImGui::TableSetupColumn ("Name", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn ("State", ImGuiTableColumnFlags_WidthFixed,
                               80.0F);
      ImGui::TableHeadersRow ();

      for (const auto &rec : mgr.list_shaders ()) {
        ImSearch::SearchableItem (rec.name.c_str (), [&] (const char *) {
          ImGui::PushID ((int)rec.id);
          ImGui::TableNextRow ();
          ImGui::TableNextColumn ();
          bool const selected = (m_selected_shader == rec.id);
          if (ImGui::Selectable (rec.name.c_str (), selected,
                                 ImGuiSelectableFlags_SpanAllColumns
                                     | ImGuiSelectableFlags_AllowOverlap)) {
            m_selected_shader = rec.id;
          }
          ImGui::TableNextColumn ();
          ImGui::TextUnformatted (to_string (rec.state));
          ImGui::PopID ();
        });
      }
      ImSearch::Submit ();
      ImGui::EndTable ();
    }
    ImSearch::EndSearch ();
  }
}

} // namespace editor
