#pragma once

#include "wsl/comp/component_meta.hpp"
#include "wsl/comp/singl/engine_resources.hpp"
#include "wsl/comp/singl/runtime_context_fwd.hpp"
#include "wsl/gfx/transform_gizmo.hpp"
#include "wsl/gfx/view_gizmo.hpp"
#include "wsl/input.hpp"
#include "wsl/comp/camera.hpp"
#include "wsl/comp/camera_2d.hpp"
#include "wsl/rsc/resource_ids.hpp"
#ifndef IN_MODULE_INTERFACE
#include <entt/entity/entity.hpp>
#endif
#ifndef IN_MODULE_INTERFACE
#include <entt/entity/fwd.hpp>
#endif
#ifndef IN_MODULE_INTERFACE
#include <glm/glm.hpp>
#endif
#ifndef IN_MODULE_INTERFACE
#include <glm/gtc/quaternion.hpp>
#endif
#ifndef IN_MODULE_INTERFACE
#include <glm/vec2.hpp>
#endif
#ifndef IN_MODULE_INTERFACE
#include <glm/vec3.hpp>
#endif
#ifndef IN_MODULE_INTERFACE
#include <glm/mat4x4.hpp>
#endif
#ifndef IN_MODULE_INTERFACE
#include <optional>
#endif
#ifndef IN_MODULE_INTERFACE
#include <string>
#endif
#ifndef IN_MODULE_INTERFACE
#include <memory>
#endif
#ifndef IN_MODULE_INTERFACE
#include <cstdint>
#endif

#ifdef WEASEL_BUILD_EDITOR
#ifndef IN_MODULE_INTERFACE
#include <imgui.h>
#endif
#endif

namespace wsl::gfx
{
class imgui_renderer_interface;
}
namespace wsl::debug
{
class debug_renderer_interface;
}

namespace wsl
{
namespace rsc
{
class scene;
}

namespace comp::singl
{

class editor_context : public comp::singleton_component
{
public:
  explicit editor_context (wsl::comp::singl::runtime_context &runtime_ctx);
  ~editor_context (); // Defined in .cpp where renderer types are complete

  editor_context (const editor_context &) = delete;
  editor_context &operator= (const editor_context &) = delete;
  editor_context (editor_context &&) = delete;
  editor_context &operator= (editor_context &&) = delete;

  static void register_meta ();
  bool custom_inspect (const char *label,
                       wsl::comp::singl::runtime_context *runtime_ctx);

  wsl::comp::singl::runtime_context &
  runtime_ctx () const
  {
    return m_runtime_ctx;
  }

  // Editor-specific renderers (created by wsl::editor_app factory methods)
  wsl::gfx::imgui_renderer_interface *get_imgui_renderer () const;
  wsl::debug::debug_renderer_interface *get_debug_renderer () const;

  void
  set_imgui_renderer (std::unique_ptr<wsl::gfx::imgui_renderer_interface> ptr);
  void set_debug_renderer (
      std::unique_ptr<wsl::debug::debug_renderer_interface> ptr);

  enum class game_view_cam_mode : std::uint8_t
  {
    engine_default,
    entity
  };
  game_view_cam_mode
  game_view_camera_mode () const
  {
    return m_game_view_camera_mode;
  }
  void
  game_view_camera_mode (game_view_cam_mode val)
  {
    m_game_view_camera_mode = val;
  }

  entt::entity
  game_view_camera_entity () const
  {
    return m_game_view_camera_entity;
  }
  void
  game_view_camera_entity (entt::entity val)
  {
    m_game_view_camera_entity = val;
  }

  // Viewport combobox selection in the game view toolbar.
  // entt::null = root viewport.
  entt::entity
  game_view_selected_viewport () const
  {
    return m_game_view_selected_viewport;
  }
  void
  game_view_selected_viewport (entt::entity val)
  {
    m_game_view_selected_viewport = val;
  }

  // Game view mode determines editor interactions and camera.
  enum class game_view_mode : std::uint8_t
  {
    mode_2d_edit,
    mode_3d_edit,
    mode_2d_view,
    mode_3d_view,
    mode_3d_fly,
  };

  // Camera combobox selection in the game view toolbar.
  enum class game_view_camera_sel : std::uint8_t
  {
    default_editor,
    editor_3d,
    editor_2d,
    default_runtime,
    entity,
  };

  game_view_camera_sel
  game_view_camera_selection () const
  {
    return m_game_view_camera_selection;
  }
  void
  game_view_camera_selection (game_view_camera_sel val)
  {
    m_game_view_camera_selection = val;
  }

  entt::entity
  game_view_selected_camera_entity () const
  {
    return m_game_view_selected_camera_entity;
  }
  void
  game_view_selected_camera_entity (entt::entity val)
  {
    m_game_view_selected_camera_entity = val;
  }

  // Editor state (engine default camera = NOT an entity)
  wsl::comp::camera const &
  editor_camera () const
  {
    return m_editor_camera;
  }
  void
  editor_camera (wsl::comp::camera const &val)
  {
    m_editor_camera = val;
  }

  glm::vec3 const &
  editor_cam_pos () const
  {
    return m_editor_cam_pos;
  }
  void
  editor_cam_pos (glm::vec3 const &val)
  {
    m_editor_cam_pos = val;
  }

  glm::quat const &
  editor_cam_rot () const
  {
    return m_editor_cam_rot;
  }
  void
  editor_cam_rot (glm::quat const &val)
  {
    m_editor_cam_rot = val;
  }

  // Editor 2D camera state
  wsl::comp::camera_2d const &
  editor_camera_2d () const
  {
    return m_editor_camera_2d;
  }
  void
  editor_camera_2d (wsl::comp::camera_2d const &val)
  {
    m_editor_camera_2d = val;
  }

  glm::vec2 const &
  editor_cam_2d_pos () const
  {
    return m_editor_cam_2d_pos;
  }
  void
  editor_cam_2d_pos (glm::vec2 const &val)
  {
    m_editor_cam_2d_pos = val;
  }

  // Editor selection used by rendering/editor overlays
  entt::entity
  selected_entity () const
  {
    return m_selected_entity;
  }
  void
  selected_entity (entt::entity val)
  {
    m_selected_entity = val;
  }

  // Editor resources
  wsl::comp::singl::engine_resources &
  editor_resources ()
  {
    return m_editor_resources;
  }

  wsl::input::action_map &
  editor_input_map ()
  {
    return m_editor_input_map;
  }

  bool
  game_fullscreen () const
  {
    return m_game_fullscreen;
  }
  void
  game_fullscreen (bool val)
  {
    m_game_fullscreen = val;
  }

  std::optional<std::string> const &
  pending_project_load () const
  {
    return m_pending_project_load;
  }
  void
  pending_project_load (std::optional<std::string> const &val)
  {
    m_pending_project_load = val;
  }

  // Editor icons
  rsc::image_id const &
  icon_signal () const
  {
    return m_icon_signal;
  }
  rsc::image_id const &
  icon_singleton () const
  {
    return m_icon_singleton;
  }
  rsc::image_id const &
  icon_system () const
  {
    return m_icon_system;
  }
  rsc::image_id const &
  icon_entity () const
  {
    return m_icon_entity;
  }
  rsc::image_id const &
  icon_inspector () const
  {
    return m_icon_inspector;
  }

  rsc::image_id const &
  icon_show () const
  {
    return m_icon_show;
  }
  rsc::image_id const &
  icon_hide () const
  {
    return m_icon_hide;
  }

  rsc::image_id const &
  icon_focus_cam () const
  {
    return m_icon_focus_cam;
  }
  rsc::image_id const &
  icon_reset_cam () const
  {
    return m_icon_reset_cam;
  }
  rsc::image_id const &
  icon_play () const
  {
    return m_icon_play;
  }
  rsc::image_id const &
  icon_pause () const
  {
    return m_icon_pause;
  }
  rsc::image_id const &
  icon_stop () const
  {
    return m_icon_stop;
  }
  rsc::image_id const &
  icon_translate () const
  {
    return m_icon_translate;
  }
  rsc::image_id const &
  icon_rotate () const
  {
    return m_icon_rotate;
  }
  rsc::image_id const &
  icon_scale () const
  {
    return m_icon_scale;
  }
  rsc::image_id const &
  icon_refresh () const
  {
    return m_icon_refresh;
  }
  rsc::image_id const &
  icon_grid () const
  {
    return m_icon_grid;
  }
  rsc::image_id const &
  icon_welcome_bg () const
  {
    return m_icon_welcome_bg;
  }

  std::string const &
  wsl_library_path () const
  {
    return m_wsl_library_path;
  }
  void
  wsl_library_path (std::string const &val)
  {
    m_wsl_library_path = val;
  }

  bool
  is_loading_project () const
  {
    return m_is_loading_project;
  }
  void
  is_loading_project (bool val)
  {
    m_is_loading_project = val;
  }

  void re_register_editor_resources ();

  glm::vec2 const &
  last_img_min () const
  {
    return m_last_img_min;
  }
  void
  last_img_min (glm::vec2 const &val)
  {
    m_last_img_min = val;
  }

  glm::vec2 const &
  last_img_size () const
  {
    return m_last_img_size;
  }
  void
  last_img_size (glm::vec2 const &val)
  {
    m_last_img_size = val;
  }

  // Grid configuration for 3D rendering
  bool
  grid_visible () const
  {
    return m_grid_visible;
  }
  void
  grid_visible (bool val)
  {
    m_grid_visible = val;
  }

  glm::vec3 const &
  grid_camera_pos () const
  {
    return m_grid_camera_pos;
  }
  void
  grid_camera_pos (glm::vec3 const &val)
  {
    m_grid_camera_pos = val;
  }

  glm::vec3 const &
  grid_fog_center () const
  {
    return m_grid_fog_center;
  }
  void
  grid_fog_center (glm::vec3 const &val)
  {
    m_grid_fog_center = val;
  }

  float
  grid_fog_radius () const
  {
    return m_grid_fog_radius;
  }
  void
  grid_fog_radius (float val)
  {
    m_grid_fog_radius = val;
  }

  // View navigation widget (view gizmo) shared between the editor UI and the
  // 3D render pass that draws it.
  wsl::gfx::view_gizmo &
  view_gizmo_widget ()
  {
    return m_view_gizmo_widget;
  }
  const wsl::gfx::view_gizmo &
  view_gizmo_widget () const
  {
    return m_view_gizmo_widget;
  }

  wsl::gfx::view_gizmo_layout const &
  view_gizmo_layout () const
  {
    return m_view_gizmo_layout;
  }
  void
  view_gizmo_layout (wsl::gfx::view_gizmo_layout const &val)
  {
    m_view_gizmo_layout = val;
  }

  bool
  view_gizmo_visible () const
  {
    return m_view_gizmo_visible;
  }
  void
  view_gizmo_visible (bool val)
  {
    m_view_gizmo_visible = val;
  }

  // Transform manipulation widget (translate/rotate/scale) shared between
  // the editor UI and the 3D render pass that draws it.
  wsl::gfx::transform_gizmo &
  transform_gizmo_widget ()
  {
    return m_transform_gizmo_widget;
  }
  const wsl::gfx::transform_gizmo &
  transform_gizmo_widget () const
  {
    return m_transform_gizmo_widget;
  }

  wsl::gfx::transform_gizmo_mode
  gizmo_mode () const
  {
    return m_gizmo_mode;
  }
  void
  gizmo_mode (wsl::gfx::transform_gizmo_mode val)
  {
    m_gizmo_mode = val;
  }

  bool
  transform_gizmo_visible () const
  {
    return m_transform_gizmo_visible;
  }
  void
  transform_gizmo_visible (bool val)
  {
    m_transform_gizmo_visible = val;
  }

  // -------- Camera resolve helper --------
  class resolved_camera
  {
  public:
    bool
    valid () const
    {
      return m_valid;
    }
    void
    valid (bool val)
    {
      m_valid = val;
    }

    // what source are we using?
    bool
    using_engine_default () const
    {
      return m_using_engine_default;
    }
    void
    using_engine_default (bool val)
    {
      m_using_engine_default = val;
    }

    entt::entity
    entity () const
    {
      return m_entity;
    }
    void
    entity (entt::entity val)
    {
      m_entity = val;
    }

    // camera outputs
    float
    aspect_ratio () const
    {
      return m_aspect_ratio;
    }
    void
    aspect_ratio (float val)
    {
      m_aspect_ratio = val;
    }

    glm::vec3 const &
    world_pos () const
    {
      return m_world_pos;
    }
    void
    world_pos (glm::vec3 const &val)
    {
      m_world_pos = val;
    }

    glm::mat4 const &
    view () const
    {
      return m_view;
    }
    void
    view (glm::mat4 const &val)
    {
      m_view = val;
    }

    glm::mat4 const &
    proj () const
    {
      return m_proj;
    }
    void
    proj (glm::mat4 const &val)
    {
      m_proj = val;
    }

    glm::mat4 const &
    vp () const
    {
      return m_vp;
    }
    void
    vp (glm::mat4 const &val)
    {
      m_vp = val;
    }

    void reset ();

  private:
    bool m_valid = false;
    bool m_using_engine_default = false;
    entt::entity m_entity = entt::null;
    float m_aspect_ratio = 1.0F;
    glm::vec3 m_world_pos{ 0.0F };
    glm::mat4 m_view{ 1.0F };
    glm::mat4 m_proj{ 1.0F };
    glm::mat4 m_vp{ 1.0F };
  };

  // Resolves the game view mode based on camera selection and viewport.
  game_view_mode resolve_game_view_mode (entt::registry &registry,
                                         wsl::rsc::scene *scene) const;

  // Resolves the camera used to render the Game View:
  // - Running: scene->camera
  // - Paused:  (entity override if selected) else engine default camera
  bool resolve_game_view_camera (entt::registry &registry,
                                 wsl::rsc::scene *scene,
                                 resolved_camera &out) const;

  // Camera helpers
  void reset_editor_camera (); // default: look at origin from a fixed distance

  // Smooth camera animation tick (call once per frame)
  void tick_editor_camera_anim (float delta_time);

  // Focus: moves editor camera near target and looks at it (smooth)
  void focus_editor_camera_to_point (const glm::vec3 &target_world_pos);

  void cancel_editor_camera_anim ();

  // Daslang runtime compilation mode (editor setting)
  enum class das_compile_mode : int
  {
    interpretation = 0,
    jit,
    aot,
  };

  das_compile_mode
  get_das_compile_mode () const
  {
    return m_das_compile_mode;
  }
  void
  set_das_compile_mode (das_compile_mode val)
  {
    m_das_compile_mode = val;
  }

private:
  // Internal: compute a camera rotation so that camera looks at target
  static glm::quat make_look_at_quat (const glm::vec3 &cam_pos,
                                      const glm::vec3 &target,
                                      const glm::vec3 &up_vector);

  struct camera_anim
  {
    bool
    active () const
    {
      return m_active;
    }
    void
    active (bool val)
    {
      m_active = val;
    }

    glm::vec3 const &
    target_pos () const
    {
      return m_target_pos;
    }
    void
    target_pos (glm::vec3 const &val)
    {
      m_target_pos = val;
    }

    glm::quat const &
    target_rot () const
    {
      return m_target_rot;
    }
    void
    target_rot (glm::quat const &val)
    {
      m_target_rot = val;
    }

    // Exponential smoothing speed (bigger = faster)
    float
    speed () const
    {
      return m_speed;
    }

    // Stop thresholds
    float
    pos_eps () const
    {
      return m_pos_eps;
    }
    float
    rot_eps () const
    {
      return m_rot_eps;
    }

    void begin (const glm::vec3 &pos, const glm::quat &rot);
    void tick (glm::vec3 &pos, glm::quat &rot, float delta_time);

  private:
    bool m_active = false;
    glm::vec3 m_target_pos{ 0.0F };
    glm::quat m_target_rot{ 1.0F, 0.0F, 0.0F, 0.0F };
    float m_speed = 18.0F;
    float m_pos_eps = 0.0005F;
    float m_rot_eps = 0.0005F;
  };

  wsl::comp::singl::runtime_context &m_runtime_ctx;
  std::unique_ptr<wsl::gfx::imgui_renderer_interface> m_imgui_renderer;
  std::unique_ptr<wsl::debug::debug_renderer_interface> m_debug_renderer;

  game_view_cam_mode m_game_view_camera_mode
      = game_view_cam_mode::engine_default;
  entt::entity m_game_view_camera_entity = entt::null;
  entt::entity m_game_view_selected_viewport = entt::null;
  game_view_camera_sel m_game_view_camera_selection
      = game_view_camera_sel::default_editor;
  entt::entity m_game_view_selected_camera_entity = entt::null;

  wsl::comp::camera m_editor_camera{};
  glm::vec3 m_editor_cam_pos{ 6.64463F, 3.4202F, 6.64463F };
  glm::quat m_editor_cam_rot{ glm::radians (glm::vec3 (-20.0F, 45.0F, 0.0F)) };

  wsl::comp::camera_2d m_editor_camera_2d{};
  glm::vec2 m_editor_cam_2d_pos{ 0.0F, 0.0F };

  entt::entity m_selected_entity = entt::null;
  wsl::comp::singl::engine_resources m_editor_resources;
  wsl::input::action_map m_editor_input_map;
  bool m_game_fullscreen = false;
  std::optional<std::string> m_pending_project_load;

  rsc::image_id m_icon_signal;
  rsc::image_id m_icon_singleton;
  rsc::image_id m_icon_system;
  rsc::image_id m_icon_entity;
  rsc::image_id m_icon_inspector;
  rsc::image_id m_icon_show;
  rsc::image_id m_icon_hide;
  rsc::image_id m_icon_focus_cam;
  rsc::image_id m_icon_reset_cam;
  rsc::image_id m_icon_play;
  rsc::image_id m_icon_pause;
  rsc::image_id m_icon_stop;
  rsc::image_id m_icon_translate;
  rsc::image_id m_icon_rotate;
  rsc::image_id m_icon_scale;
  rsc::image_id m_icon_refresh;
  rsc::image_id m_icon_grid;
  rsc::image_id m_icon_welcome_bg;

  std::string m_wsl_library_path;
  bool m_is_loading_project = false;

  glm::vec2 m_last_img_min{ 0.0F, 0.0F };
  glm::vec2 m_last_img_size{ 0.0F, 0.0F };

  bool m_grid_visible = true;
  glm::vec3 m_grid_camera_pos{ 0.0F };
  glm::vec3 m_grid_fog_center{ 0.0F };
  float m_grid_fog_radius = 400.0F;

  // The editor UI refreshes the view gizmo placement each frame; the 3D
  // render pass consumes it to draw the widget.
  wsl::gfx::view_gizmo m_view_gizmo_widget;
  wsl::gfx::view_gizmo_layout m_view_gizmo_layout;
  bool m_view_gizmo_visible = false;

  // Transform manipulation widget state shared with the 3D render pass.
  wsl::gfx::transform_gizmo m_transform_gizmo_widget;
  wsl::gfx::transform_gizmo_mode m_gizmo_mode
      = wsl::gfx::transform_gizmo_mode::translate;
  bool m_transform_gizmo_visible = false;

  camera_anim m_cam_anim;

private:
  das_compile_mode m_das_compile_mode = das_compile_mode::interpretation;
};

} // namespace comp::singl

} // namespace wsl
