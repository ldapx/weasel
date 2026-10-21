#pragma once

#include "wsl/gfx/ui_system_interface.hpp"
#ifndef IN_MODULE_INTERFACE
#include <RmlUi/Core.h>
#endif
#ifndef IN_MODULE_INTERFACE
#include <RmlUi/Core/Math.h>
#endif
#ifndef IN_MODULE_INTERFACE
#include <RmlUi_Platform_SDL.h>
#endif
// RmlUi_Renderer_SDL_GPU.h uses `interface` as a parameter name, which
// collides with the COM macro from windows.h (combaseapi.h) whenever a
// Windows header precedes this include. Guard it order-independently;
// restored immediately after so COM consumers are unaffected.
#ifdef interface
#pragma push_macro("interface")
#undef interface
#define WSL_POP_RMLUI_INTERFACE
#endif
#ifndef IN_MODULE_INTERFACE
#include <RmlUi_Renderer_SDL_GPU.h>
#endif
#ifdef WSL_POP_RMLUI_INTERFACE
#pragma pop_macro("interface")
#undef WSL_POP_RMLUI_INTERFACE
#endif

#ifndef IN_MODULE_INTERFACE
#include <entt/entt.hpp>
#endif

#ifndef IN_MODULE_INTERFACE
#include <memory>
#endif
#ifndef IN_MODULE_INTERFACE
#include <string>
#endif
#ifndef IN_MODULE_INTERFACE
#include <unordered_set>
#endif
#ifndef IN_MODULE_INTERFACE
#include <vector>
#endif

#include "wsl/comp/component_meta.hpp"
#include "wsl/gfx/render_context.hpp"
#include "wsl/gfx/render_window.hpp"
#include "wsl/rsc/resource_manager.hpp"

namespace wsl::das
{
class das_system_adapter;
}

namespace wsl
{

namespace gfx
{
class ui_canvas;
}

namespace comp
{

namespace singl
{

class runtime_context;

class ui_manager : public comp::singleton_component
{
public:
  ui_manager (gfx::render_context &ctx, wsl::gfx::render_window &window,
              wsl::rsc::resource_manager *res_mgr);
  ~ui_manager ();

  static void register_meta ();
  bool custom_inspect (const char *label,
                       comp::singl::runtime_context *runtime);

  void prepare_scene (entt::registry &registry);
  Rml::DataModelConstructor ensure_data_model (entt::registry &registry,
                                               const std::string &name);
  void clear_scene_bindings ();
  bool bind_button (entt::registry &registry, const std::string &element_id,
                    wsl::das::das_system_adapter *owner,
                    const std::string &method_name);
  void attach_event_listeners ();
  void detach_event_listeners ();
  void clear_event_listeners ();
  void clear_event_listeners_for (wsl::das::das_system_adapter *owner);
  void load_font (const std::string &path);
  std::size_t
  binding_generation () const
  {
    return m_binding_generation;
  }
  entt::registry *
  prepared_registry () const
  {
    return m_prepared_registry;
  }

  // -- getters / setters
  // -------------------------------------------------------

  const wsl::gfx::ui_system_interface &
  system_interface () const
  {
    return *m_system_interface;
  }
  wsl::gfx::ui_system_interface &
  system_interface ()
  {
    return *m_system_interface;
  }

  RenderInterface_SDL_GPU *
  render_interface () const
  {
    return m_render_interface;
  }

  Rml::Context *
  context () const
  {
    return m_context;
  }
  Rml::Context *&
  context ()
  {
    return m_context;
  }

  Rml::ElementDocument *
  active_document_instance () const
  {
    return m_active_document_instance;
  }
  Rml::ElementDocument *&
  active_document_instance ()
  {
    return m_active_document_instance;
  }

  const rsc::ui_layout_id &
  active_document_id () const
  {
    return m_active_document_id;
  }
  rsc::ui_layout_id &
  active_document_id ()
  {
    return m_active_document_id;
  }

  const rsc::ui_layout_id &
  loaded_document_id () const
  {
    return m_loaded_document_id;
  }
  rsc::ui_layout_id &
  loaded_document_id ()
  {
    return m_loaded_document_id;
  }

  bool
  needs_reload () const
  {
    return m_needs_reload;
  }
  bool &
  needs_reload ()
  {
    return m_needs_reload;
  }

  /**
   * Ensures the Virtual-Canvas UI target (D15/U1) exists at the requested
   * virtual size and returns it. Reallocates lazily on size changes.
   */
  wsl::gfx::ui_canvas &ensure_virtual_canvas (gfx::render_context &ctx,
                                              wsl::gfx::render_window &window,
                                              uint32_t width, uint32_t height);
  /** Virtual-canvas target, or nullptr before the first `ensure`. */
  [[nodiscard]] wsl::gfx::ui_canvas *virtual_canvas ();

private:
  struct script_button_binding;

  std::unique_ptr<wsl::gfx::ui_canvas> m_virtual_canvas;
  std::unique_ptr<wsl::gfx::ui_system_interface> m_owned_system_interface;
  wsl::gfx::ui_system_interface *m_system_interface = nullptr;
  std::unique_ptr<RenderInterface_SDL_GPU> m_owned_render_interface;
  RenderInterface_SDL_GPU *m_render_interface = nullptr;
  Rml::Context *m_context = nullptr;
  Rml::ElementDocument *m_active_document_instance = nullptr;
  rsc::ui_layout_id m_active_document_id{ entt::null };
  rsc::ui_layout_id m_loaded_document_id{ entt::null };
  bool m_needs_reload = false;
  entt::registry *m_prepared_registry = nullptr;
  std::unordered_set<std::string> m_loaded_fonts;
  std::unordered_set<std::string> m_active_model_names;
  std::vector<std::unique_ptr<script_button_binding>> m_script_button_bindings;
  std::size_t m_binding_generation = 0;
};

} // namespace singl

} // namespace comp

} // namespace wsl
