#include "editor_context.hpp"

#include "comp/component_meta.hpp"
#include "comp/world_transform.hpp"
#include "debug/debug_renderer.hpp"
#include "input.hpp"
#include "wsl/comp/camera.hpp"
#include "wsl/comp/camera_2d.hpp"
#include "wsl/comp/singl/runtime_context.hpp"
#include "wsl/comp/singl/rendering_manager.hpp"
#include "wsl/gfx/render_window.hpp"
#include "wsl/comp/subviewport.hpp"
#include "wsl/comp/transform.hpp"
#include "wsl/comp/transform_2d.hpp"
#include "wsl/rsc/scene.hpp"
#include "wsl/log/log.hpp"

#include <SDL3/SDL_keycode.h>
#include <climits>
#include <cmath>
#include <cstdint>
#include <entt/entity/entity.hpp>
#include <entt/entity/fwd.hpp>
#include <filesystem>
#include <glm/common.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/quaternion_common.hpp>
#include <glm/ext/quaternion_geometric.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/matrix.hpp>
#include <glm/trigonometric.hpp>
#include <string>
#ifdef _WIN32
// WIN32_LEAN_AND_MEAN keeps windows.h small; NOMINMAX keeps its min/max
// macros from breaking std::min/std::max. near/far are 16-bit relics
// defined unconditionally by minwindef.h that break member access like
// camera.near(), so they are undefined TU-wide here.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef near
#undef near
#endif
#ifdef far
#undef far
#endif
#else
#include <unistd.h>
#endif

namespace wsl::comp::singl
{

editor_context::editor_context (wsl::comp::singl::runtime_context &runtime_ctx)
    : m_runtime_ctx (runtime_ctx), m_editor_resources (runtime_ctx)
{
  wsl::log::editor ()->trace ("Core constructor started");
  m_editor_resources.set_editor_context (this);

  // Only override the engine resource path with compile-time paths if the
  // current path (set by the launcher) does not already contain resources.
  // This allows installed / archived builds to work without recompilation.
  {
    const std::string &current_path
        = m_runtime_ctx.resource_manager ().get_engine_resource_path ();
    bool const has_dev_resources = std::filesystem::exists (
        std::filesystem::path (current_path) / "compiled_shaders");
    bool const has_packaged_resources = std::filesystem::exists (
        std::filesystem::path (current_path) / "share/weasel/compiled_shaders");

    if (!has_dev_resources && !has_packaged_resources) {
#ifdef _WIN32
      char exe_buf[MAX_PATH];
      const DWORD exe_len = GetModuleFileNameA (nullptr, exe_buf, MAX_PATH);
      if (exe_len != 0 && exe_len < MAX_PATH) {
        exe_buf[exe_len] = '\0';
        std::filesystem::path const exe_path (exe_buf);
#else
      char exe_buf[PATH_MAX];
      ssize_t const len
          = readlink ("/proc/self/exe", exe_buf, sizeof (exe_buf) - 1);
      if (len != -1) {
        exe_buf[len] = '\0';
        std::filesystem::path const exe_path (exe_buf);
#endif
        std::filesystem::path const prefix
            = exe_path.parent_path ().parent_path ();
        if (std::filesystem::exists (prefix
                                     / "share/weasel/compiled_shaders")) {
          m_runtime_ctx.resource_manager ().set_engine_resource_path (
              prefix.string ());
        } else {
#ifdef WEASEL_BUILD_DIR
          m_runtime_ctx.resource_manager ().set_engine_resource_path (
              WEASEL_BUILD_DIR);
#elif defined(WEASEL_SOURCE_DIR)
          m_runtime_ctx.resource_manager ().set_engine_resource_path (
              WEASEL_SOURCE_DIR);
#endif
        }
      } else {
#ifdef WEASEL_BUILD_DIR
        m_runtime_ctx.resource_manager ().set_engine_resource_path (
            WEASEL_BUILD_DIR);
#elif defined(WEASEL_SOURCE_DIR)
        m_runtime_ctx.resource_manager ().set_engine_resource_path (
            WEASEL_SOURCE_DIR);
#endif
      }
    }
  }

#ifdef WEASEL_SOURCE_DIR
  m_wsl_library_path = WEASEL_SOURCE_DIR;
#endif

  // Register engine/builtin fonts
  const std::vector<std::string> engine_fonts = {
    "engine://otf/fanwood.otf",           "engine://otf/splinesans-regular.otf",
    "engine://otf/splinesans-bold.otf",   "engine://otf/splinesans-light.otf",
    "engine://otf/splinesans-medium.otf", "engine://otf/splinesans-semibold.otf"
  };

  for (const auto &font : engine_fonts) {
    m_editor_resources.register_font (font);
  }

  // Register editor icons
  m_icon_signal
      = m_editor_resources.register_image ("engine://icons/signal.svg");
  m_icon_singleton
      = m_editor_resources.register_image ("engine://icons/singleton.svg");
  m_icon_system
      = m_editor_resources.register_image ("engine://icons/system.svg");
  m_icon_entity
      = m_editor_resources.register_image ("engine://icons/entity.svg");
  m_icon_inspector
      = m_editor_resources.register_image ("engine://icons/inspector.svg");

  m_icon_show = m_editor_resources.register_image ("engine://icons/show.svg");
  m_icon_hide = m_editor_resources.register_image ("engine://icons/hide.svg");
  m_editor_resources.load (m_icon_show);
  m_editor_resources.load (m_icon_hide);

  m_icon_focus_cam
      = m_editor_resources.register_image ("engine://icons/focus_cam.svg");
  m_icon_reset_cam
      = m_editor_resources.register_image ("engine://icons/reset_cam.svg");
  m_icon_play = m_editor_resources.register_image ("engine://icons/play.svg");
  m_editor_resources.load (m_icon_play);
  m_icon_pause = m_editor_resources.register_image ("engine://icons/pause.svg");
  m_icon_stop = m_editor_resources.register_image ("engine://icons/stop.svg");
  m_icon_translate
      = m_editor_resources.register_image ("engine://icons/translate.svg");
  m_icon_rotate
      = m_editor_resources.register_image ("engine://icons/rotate.svg");
  m_icon_scale = m_editor_resources.register_image ("engine://icons/scale.svg");
  m_icon_refresh
      = m_editor_resources.register_image ("engine://icons/refresh.svg");
  m_editor_resources.load (m_icon_refresh);
  m_icon_grid = m_editor_resources.register_image ("engine://icons/grid.svg");
  m_editor_resources.load (m_icon_grid);
  m_icon_welcome_bg
      = m_editor_resources.register_image ("engine://icons/welcome_bg.svg");
  m_editor_resources.load (m_icon_welcome_bg);

  // Default editor bindings
  m_editor_input_map.bindings["toggle_game_focus"]
      = wsl::input::key_binding{ .mod = SDL_KMOD_NONE,
                                 .scancode = SDL_SCANCODE_F1 };
}

editor_context::~editor_context () = default;

wsl::debug::debug_renderer_interface *
editor_context::get_debug_renderer () const
{
  return m_debug_renderer ? m_debug_renderer.get () : nullptr;
}

void
editor_context::set_debug_renderer (
    std::unique_ptr<wsl::debug::debug_renderer_interface> ptr)
{
  m_debug_renderer = std::move (ptr);
}

void
editor_context::re_register_editor_resources ()
{
  wsl::log::editor ()->trace ("Re-registering editor resources");

  m_icon_signal
      = m_editor_resources.register_image ("engine://icons/signal.svg");
  m_icon_singleton
      = m_editor_resources.register_image ("engine://icons/singleton.svg");
  m_icon_system
      = m_editor_resources.register_image ("engine://icons/system.svg");
  m_icon_entity
      = m_editor_resources.register_image ("engine://icons/entity.svg");
  m_icon_inspector
      = m_editor_resources.register_image ("engine://icons/inspector.svg");

  m_icon_show = m_editor_resources.register_image ("engine://icons/show.svg");
  m_icon_hide = m_editor_resources.register_image ("engine://icons/hide.svg");
  m_editor_resources.load (m_icon_show);
  m_editor_resources.load (m_icon_hide);

  m_icon_focus_cam
      = m_editor_resources.register_image ("engine://icons/focus_cam.svg");
  m_icon_reset_cam
      = m_editor_resources.register_image ("engine://icons/reset_cam.svg");
  m_icon_play = m_editor_resources.register_image ("engine://icons/play.svg");
  m_editor_resources.load (m_icon_play);
  m_icon_pause = m_editor_resources.register_image ("engine://icons/pause.svg");
  m_icon_stop = m_editor_resources.register_image ("engine://icons/stop.svg");
  m_icon_translate
      = m_editor_resources.register_image ("engine://icons/translate.svg");
  m_icon_rotate
      = m_editor_resources.register_image ("engine://icons/rotate.svg");
  m_icon_scale = m_editor_resources.register_image ("engine://icons/scale.svg");
  m_icon_refresh
      = m_editor_resources.register_image ("engine://icons/refresh.svg");
  m_editor_resources.load (m_icon_refresh);
  m_icon_grid = m_editor_resources.register_image ("engine://icons/grid.svg");
  m_editor_resources.load (m_icon_grid);
  m_icon_welcome_bg
      = m_editor_resources.register_image ("engine://icons/welcome_bg.svg");
  m_editor_resources.load (m_icon_welcome_bg);
}

void
editor_context::register_meta ()
{
  using namespace entt::literals;

  // The ImGui inspector for this singleton is registered by editor_support
  // (register_editor_inspectors) so the core library stays ImGui-free.
  entt::meta_factory<editor_context> ()
      .type (entt::type_hash<editor_context>::value ())
      .custom<wsl::comp::meta_info> (wsl::comp::meta_info{
          "Editor Context", "Global state for the editor application.", "" });
}

void
editor_context::resolved_camera::reset ()
{
  m_valid = false;
  m_using_engine_default = false;
  m_entity = entt::null;
  m_aspect_ratio = 1.0F;
  m_world_pos = glm::vec3{ 0.0F };
  m_view = glm::mat4{ 1.0F };
  m_proj = glm::mat4{ 1.0F };
  m_vp = glm::mat4{ 1.0F };
}

editor_context::game_view_mode
editor_context::resolve_game_view_mode (entt::registry &registry,
                                        wsl::rsc::scene *scene) const
{
  if (scene == nullptr) {
    return game_view_mode::mode_3d_edit;
  }

  // In edit mode, the toolbar selection is authoritative.
  // entt::null explicitly means the root viewport.
  entt::entity const target_viewport = m_game_view_selected_viewport;

  switch (m_game_view_camera_selection) {
  case game_view_camera_sel::editor_3d:
    return game_view_mode::mode_3d_edit;

  case game_view_camera_sel::editor_2d:
    return game_view_mode::mode_2d_edit;

  case game_view_camera_sel::default_runtime:
  case game_view_camera_sel::entity: {
    entt::entity cam_entity = entt::null;
    if (m_game_view_camera_selection == game_view_camera_sel::default_runtime) {
      if (target_viewport != entt::null) {
        if (auto *subviewport
            = registry.try_get<comp::subviewport> (target_viewport)) {
          if (subviewport->camera.value != entt::null
              && registry.valid (subviewport->camera.value)) {
            cam_entity = subviewport->camera.value;
          }
        }
      } else {
        cam_entity = scene->camera;
      }
    } else {
      cam_entity = m_game_view_selected_camera_entity;
    }

    if (cam_entity != entt::null && registry.valid (cam_entity)) {
      if (registry.all_of<comp::camera_2d> (cam_entity)) {
        return game_view_mode::mode_2d_view;
      }
      if (registry.all_of<comp::camera> (cam_entity)) {
        return game_view_mode::mode_3d_view;
      }
    }
    // Fallback to 3D edit if no valid camera
    return game_view_mode::mode_3d_edit;
  }

  case game_view_camera_sel::default_editor:
  default: {
    // Component-driven mode: a 2D subviewport (transform_2d) uses the
    // 2D editor camera by default.
    if (target_viewport != entt::null
        && comp::is_2d_subviewport (registry, target_viewport)) {
      return game_view_mode::mode_2d_edit;
    }

    // Analyze viewport contents
    bool has_3d = false;
    bool has_2d = false;

    auto t3d_view = registry.view<comp::transform> ();
    for (auto entity : t3d_view) {
      if (comp::find_nearest_viewport (registry, entity) == target_viewport) {
        has_3d = true;
        break;
      }
    }

    if (!has_3d) {
      auto t2d_view = registry.view<comp::transform_2d> ();
      for (auto entity : t2d_view) {
        if (comp::find_nearest_viewport (registry, entity) == target_viewport) {
          has_2d = true;
          break;
        }
      }
    }

    bool const has_3d_final = has_3d;
    bool const has_2d_final = has_2d;

    if (has_2d_final) {
      return game_view_mode::mode_2d_edit;
    }

    return game_view_mode::mode_3d_edit;
  }
  }
}

bool
editor_context::resolve_game_view_camera (entt::registry &registry,
                                          wsl::rsc::scene *scene,
                                          resolved_camera &out) const
{
  out.reset ();

  if (scene == nullptr) {
    return false;
  }

  bool const running = m_runtime_ctx.is_running ();
  entt::entity target_viewport = entt::null;

  if (running) {
    // During play mode, always use the rendering manager's active viewport.
    auto &scene_reg = scene->get_registry ();
    auto &ctx = scene_reg.ctx ();
    if (ctx.contains<comp::singl::rendering_manager> ()) {
      auto &rendering = ctx.get<comp::singl::rendering_manager> ();
      target_viewport = rendering.render_viewport;
    }
  } else {
    // In edit mode, the toolbar selection is authoritative.
    // entt::null explicitly means the root viewport.
    target_viewport = m_game_view_selected_viewport;
  }

  uint32_t w;
  uint32_t h;
  m_runtime_ctx.window ().get_size (w, h);
  float aspect = (float)w / (float)h;

  if (target_viewport != entt::null) {
    if (auto *subviewport
        = registry.try_get<comp::subviewport> (target_viewport)) {
      w = static_cast<uint32_t> (subviewport->virtual_size.x ());
      h = static_cast<uint32_t> (subviewport->virtual_size.y ());
      aspect = (w > 0 && h > 0) ? (float)w / (float)h : 1.0F;
    }
  }
  out.aspect_ratio (aspect);

  // During play mode, preserve previous behavior
  if (running) {
    // Resolve camera from the target viewport
    if (target_viewport != entt::null && registry.valid (target_viewport)) {
      if (auto *subviewport
          = registry.try_get<wsl::comp::subviewport> (target_viewport)) {
        if (subviewport->camera.value != entt::null
            && registry.valid (subviewport->camera.value)) {
          out.entity (subviewport->camera.value);
          out.using_engine_default (false);
        } else {
          out.using_engine_default (true);
        }
      } else {
        bool is_camera
            = registry.all_of<wsl::comp::camera> (target_viewport)
              || registry.all_of<wsl::comp::camera_2d> (target_viewport);
        if (is_camera) {
          out.entity (target_viewport);
          out.using_engine_default (false);
        } else {
          out.using_engine_default (true);
        }
      }
    } else {
      out.using_engine_default (true);
      out.entity (scene->camera);
      if (out.entity () != entt::null) {
        out.using_engine_default (false);
      }
    }
  } else {
    // Edit mode: use camera selection
    auto mode = resolve_game_view_mode (registry, scene);

    switch (mode) {
    case game_view_mode::mode_2d_edit:
      out.using_engine_default (true);
      {
        out.world_pos (
            glm::vec3 (m_editor_cam_2d_pos.x, m_editor_cam_2d_pos.y, 0.0F));
        out.proj (glm::ortho (0.0F, (float)w, (float)h, 0.0F, -1.0F, 1.0F));

        glm::mat4 view_mat = glm::mat4 (1.0F);
        view_mat
            = glm::scale (view_mat, glm::vec3 (m_editor_camera_2d.zoom,
                                               m_editor_camera_2d.zoom, 1.0F));
        view_mat = glm::translate (
            view_mat,
            glm::vec3 (-m_editor_cam_2d_pos.x * m_editor_camera_2d.zoom,
                       -m_editor_cam_2d_pos.y * m_editor_camera_2d.zoom, 0.0F));
        out.view (view_mat);
        out.valid (true);
      }
      break;

    case game_view_mode::mode_3d_edit:
    case game_view_mode::mode_3d_fly:
      out.using_engine_default (true);
      out.world_pos (m_editor_cam_pos);
      out.view (glm::mat4_cast (glm::inverse (m_editor_cam_rot)));
      out.view (glm::translate (out.view (), -m_editor_cam_pos));
      out.proj (glm::perspective (glm::radians (m_editor_camera.fov ()),
                                  out.aspect_ratio (), m_editor_camera.near (),
                                  m_editor_camera.far ()));
      out.valid (true);
      break;

    case game_view_mode::mode_2d_view:
    case game_view_mode::mode_3d_view: {
      entt::entity cam_entity = entt::null;
      if (m_game_view_camera_selection
          == game_view_camera_sel::default_runtime) {
        if (target_viewport != entt::null) {
          if (auto *subviewport
              = registry.try_get<comp::subviewport> (target_viewport)) {
            if (subviewport->camera.value != entt::null
                && registry.valid (subviewport->camera.value)) {
              cam_entity = subviewport->camera.value;
            }
          }
        } else {
          cam_entity = scene->camera;
        }
      } else {
        cam_entity = m_game_view_selected_camera_entity;
      }

      if (cam_entity != entt::null && registry.valid (cam_entity)) {
        if (registry.all_of<comp::camera, comp::world_transform> (cam_entity)) {
          const auto &cam = registry.get<comp::camera> (cam_entity);
          const auto &wt = registry.get<comp::world_transform> (cam_entity);
          glm::mat4 const wtm = wt.value ();
          out.world_pos (glm::vec3 (wtm[3]));
          out.view (glm::inverse (wtm));
          out.proj (glm::perspective (glm::radians (cam.fov ()),
                                      out.aspect_ratio (), cam.near (),
                                      cam.far ()));
          out.valid (true);
          out.entity (cam_entity);
          out.using_engine_default (false);
        } else if (registry.all_of<comp::camera_2d, comp::transform_2d> (
                       cam_entity)) {
          const auto &cam2d = registry.get<comp::camera_2d> (cam_entity);
          const auto &t2d = registry.get<comp::transform_2d> (cam_entity);
          float const vp_w = static_cast<float> (w);
          float const vp_h = static_cast<float> (h);
          out.world_pos (
              glm::vec3 (t2d.position.x (), t2d.position.y (), 0.0F));
          out.proj (glm::ortho (0.0F, vp_w, (float)vp_h, 0.0F, -1.0F, 1.0F));

          glm::mat4 view_mat = glm::mat4 (1.0F);
          view_mat
              = glm::scale (view_mat, glm::vec3 (cam2d.zoom, cam2d.zoom, 1.0F));
          view_mat = glm::translate (
              view_mat, glm::vec3 (-t2d.position.x () * cam2d.zoom,
                                   -t2d.position.y () * cam2d.zoom, 0.0F));
          out.view (view_mat);
          out.valid (true);
          out.entity (cam_entity);
          out.using_engine_default (false);
        }
      }
      break;
    }
    }
  }

  // Compute matrices for play-mode runtime cameras
  if (running && out.entity () != entt::null
      && registry.valid (out.entity ())) {
    if (registry.all_of<comp::camera, comp::world_transform> (out.entity ())) {
      const auto &cam = registry.get<comp::camera> (out.entity ());
      const auto &wt = registry.get<comp::world_transform> (out.entity ());
      glm::mat4 const wtm = wt.value ();
      out.world_pos (glm::vec3 (wtm[3]));
      out.view (glm::inverse (wtm));
      out.proj (glm::perspective (glm::radians (cam.fov ()),
                                  out.aspect_ratio (), cam.near (),
                                  cam.far ()));
      out.valid (true);
    } else if (registry.all_of<comp::camera_2d, comp::transform_2d> (
                   out.entity ())) {
      const auto &cam2d = registry.get<comp::camera_2d> (out.entity ());
      const auto &t2d = registry.get<comp::transform_2d> (out.entity ());
      float const vp_w = static_cast<float> (w);
      float const vp_h = static_cast<float> (h);
      out.world_pos (glm::vec3 (t2d.position.x (), t2d.position.y (), 0.0F));
      glm::mat4 view_mat = glm::mat4 (1.0F);
      view_mat
          = glm::scale (view_mat, glm::vec3 (cam2d.zoom, cam2d.zoom, 1.0F));
      view_mat = glm::translate (
          view_mat, glm::vec3 (-t2d.position.x () * cam2d.zoom,
                               -t2d.position.y () * cam2d.zoom, 0.0F));
      out.view (view_mat);
      out.proj (glm::ortho (0.0F, vp_w, (float)vp_h, 0.0F, -1.0F, 1.0F));
      out.valid (true);
    }
  } else if (running && out.using_engine_default ()) {
    out.world_pos (m_editor_cam_pos);
    out.view (glm::mat4_cast (glm::inverse (m_editor_cam_rot)));
    out.view (glm::translate (out.view (), -m_editor_cam_pos));
    out.proj (glm::perspective (glm::radians (m_editor_camera.fov ()),
                                out.aspect_ratio (), m_editor_camera.near (),
                                m_editor_camera.far ()));
    out.valid (true);
  }

  if (out.valid ()) {
    out.vp (out.proj () * out.view ());
  }

  return out.valid ();
}

void
editor_context::reset_editor_camera ()
{
  cancel_editor_camera_anim ();
  m_editor_cam_pos = glm::vec3 (6.64463F, 3.4202F, 6.64463F);
  m_editor_cam_rot = glm::quat (glm::radians (glm::vec3 (-20.0F, 45.0F, 0.0F)));
}

void
editor_context::tick_editor_camera_anim (float dt)
{
  if (m_cam_anim.active ()) {
    m_cam_anim.tick (m_editor_cam_pos, m_editor_cam_rot, dt);
  }
}

void
editor_context::focus_editor_camera_to_point (const glm::vec3 &target_world_pos)
{
  glm::vec3 dir = glm::normalize (m_editor_cam_pos - target_world_pos);
  if (glm::length (dir) < 0.001F) {
    dir = glm::vec3 (0, 0, 1);
  }

  glm::vec3 const new_pos = target_world_pos + dir * 5.0F;
  glm::quat const new_rot
      = make_look_at_quat (new_pos, target_world_pos, glm::vec3 (0, 1, 0));

  m_cam_anim.begin (new_pos, new_rot);
}

void
editor_context::cancel_editor_camera_anim ()
{
  m_cam_anim.active (false);
}

glm::quat
editor_context::make_look_at_quat (const glm::vec3 &cam_pos,
                                   const glm::vec3 &target, const glm::vec3 &up)
{
  glm::mat4 const m = glm::lookAt (cam_pos, target, up);
  return glm::inverse (glm::quat_cast (m));
}

void
editor_context::camera_anim::begin (const glm::vec3 &pos, const glm::quat &rot)
{
  m_active = true;
  m_target_pos = pos;
  m_target_rot = rot;
}

void
editor_context::camera_anim::tick (glm::vec3 &pos, glm::quat &rot, float dt)
{
  float const t = 1.0F - std::exp (-m_speed * dt);
  pos = glm::mix (pos, m_target_pos, t);
  rot = glm::slerp (rot, m_target_rot, t);

  if (glm::distance (pos, m_target_pos) < m_pos_eps
      && glm::abs (glm::dot (rot, m_target_rot)) > 1.0F - m_rot_eps) {
    pos = m_target_pos;
    rot = m_target_rot;
    m_active = false;
  }
}

} // namespace wsl::comp::singl
