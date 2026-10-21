#include "ui_manager.hpp"
#include "wsl/log/log.hpp"

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <entt/core/hashed_string.hpp>
#include <entt/core/type_info.hpp>
#include <entt/entity/entity.hpp>
#include <entt/entity/fwd.hpp>
#include <entt/meta/factory.hpp>

#include "comp/component_meta.hpp"
#include "gfx/render_context.hpp"
#include "gfx/render_window.hpp"
#include "gfx/rml_runtime.hpp"
#include "gfx/ui_canvas.hpp"
#include "rsc/resource_manager.hpp"
#include "runtime_context.hpp"
#include "wsl/das/das_system_adapter.hpp"
#include <functional>
#include <filesystem>
#include <string>

namespace wsl
{

namespace comp
{
namespace singl
{

struct ui_manager::script_button_binding
{
  struct listener final : Rml::EventListener
  {
    std::function<void ()> callback;

    explicit listener (std::function<void ()> callback_fn)
        : callback (std::move (callback_fn))
    {
    }

    void
    ProcessEvent (Rml::Event &) override
    {
      if (callback) {
        callback ();
      }
    }
  };

  Rml::Element *element = nullptr;
  wsl::das::das_system_adapter *owner = nullptr;
  std::string element_id;
  std::string method_name;
  std::unique_ptr<listener> event_listener;
};

ui_manager::ui_manager (gfx::render_context &ctx,
                        wsl::gfx::render_window &window,
                        wsl::rsc::resource_manager *res_mgr)
{
  if (ctx.gpu_device == nullptr || window.handler () == nullptr) {
    wsl::log::editor ()->trace ("Headless mode, skipping RmlUi initialization");
    return;
  }

  // Attach to shared backends when an editor front-end published them; all
  // RmlUi contexts must compile/render through one render interface because
  // compiled geometry handles are per-interface.
  m_render_interface = wsl::gfx::shared_ui_render_interface ();
  if (m_render_interface == nullptr) {
    m_owned_render_interface = std::make_unique<RenderInterface_SDL_GPU> (
        ctx.gpu_device, window.handler ());
    m_render_interface = m_owned_render_interface.get ();
    Rml::SetRenderInterface (m_render_interface);
  }

  m_system_interface = wsl::gfx::shared_ui_system_interface ();
  if (m_system_interface == nullptr) {
    m_owned_system_interface
        = std::make_unique<wsl::gfx::ui_system_interface> ();
    m_system_interface = m_owned_system_interface.get ();
    Rml::SetSystemInterface (m_system_interface);
  }

  wsl::log::editor ()->trace ("Using GPU device {}", (void *)ctx.gpu_device);
  if (!wsl::gfx::rmlui_acquire ()) {
    return;
  }

  int width = 0;
  int height = 0;
  window.get_size (width, height);
  m_context = Rml::CreateContext ("UI Context", Rml::Vector2i{ width, height });

  if (res_mgr != nullptr) {
    for (const auto &font : res_mgr->list_fonts ()) {
      std::string const resolved = res_mgr->resolve_path (font.path);
      load_font (resolved);
    }
  }
}

ui_manager::~ui_manager ()
{
  if (m_context == nullptr)
    return;

  clear_scene_bindings ();
  if (m_active_document_instance != nullptr) {
    m_active_document_instance->Close ();
    m_active_document_instance = nullptr;
  }

  // Destroy only this context; RmlUi itself is torn down when the last
  // acquire (this manager or an editor front-end) releases the runtime.
  Rml::RemoveContext ("UI Context");
  m_context = nullptr;
  wsl::gfx::rmlui_release ();
}

void
ui_manager::register_meta ()
{
  using namespace entt::literals;
  auto &&factory_ui
      = entt::meta_factory<comp::singl::ui_manager> ()
            .type (entt::type_hash<comp::singl::ui_manager>::value ())
            .custom<comp::meta_info> (comp::meta_info{
                "UI Manager",
                "Runtime-owned RmlUi context and renderer used "
                "by the active scene.",
                "" });
}

void
ui_manager::clear_scene_bindings ()
{
  clear_event_listeners ();
  if (m_context != nullptr) {
    for (const std::string &name : m_active_model_names) {
      m_context->RemoveDataModel (name);
    }
  }

  m_active_model_names.clear ();
  m_prepared_registry = nullptr;
  ++m_binding_generation;
}

bool
ui_manager::bind_button (entt::registry &registry,
                         const std::string &element_id,
                         wsl::das::das_system_adapter *owner,
                         const std::string &method_name)
{
  if (m_context == nullptr || owner == nullptr || element_id.empty ()
      || method_name.empty ()) {
    return false;
  }
  if (m_prepared_registry != &registry) {
    prepare_scene (registry);
  }

  for (const auto &binding : m_script_button_bindings) {
    if (binding->owner == owner && binding->element_id == element_id
        && binding->method_name == method_name) {
      return true;
    }
  }

  auto binding = std::make_unique<script_button_binding> ();
  binding->owner = owner;
  binding->element_id = element_id;
  binding->method_name = method_name;
  entt::registry *const callback_registry = &registry;
  const std::string callback_method = method_name;
  binding->event_listener = std::make_unique<script_button_binding::listener> (
      [owner, callback_registry, callback_method] {
        if (callback_registry != nullptr) {
          owner->invoke_ui_callback (callback_method.c_str (),
                                     *callback_registry);
        }
      });

  if (m_active_document_instance != nullptr) {
    binding->element
        = m_active_document_instance->GetElementById (element_id);
    if (binding->element == nullptr) {
      return false;
    }
    binding->element->AddEventListener ("click",
                                        binding->event_listener.get ());
  }
  m_script_button_bindings.push_back (std::move (binding));
  return true;
}

void
ui_manager::attach_event_listeners ()
{
  if (m_context == nullptr || m_active_document_instance == nullptr) {
    return;
  }
  for (const auto &binding : m_script_button_bindings) {
    if (binding->element != nullptr) {
      continue;
    }
    binding->element
        = m_active_document_instance->GetElementById (binding->element_id);
    if (binding->element == nullptr) {
      wsl::log::sys ()->warn ("UI button '{}' was not found in the active document",
                              binding->element_id);
      continue;
    }
    binding->element->AddEventListener ("click",
                                        binding->event_listener.get ());
  }
}

void
ui_manager::detach_event_listeners ()
{
  for (const auto &binding : m_script_button_bindings) {
    if (binding->element != nullptr && binding->event_listener != nullptr) {
      binding->element->RemoveEventListener ("click",
                                            binding->event_listener.get ());
      binding->element = nullptr;
    }
  }
}

void
ui_manager::clear_event_listeners ()
{
  detach_event_listeners ();
  m_script_button_bindings.clear ();
}

void
ui_manager::clear_event_listeners_for (wsl::das::das_system_adapter *owner)
{
  if (owner == nullptr) {
    return;
  }
  for (auto it = m_script_button_bindings.begin ();
       it != m_script_button_bindings.end ();) {
    const auto &binding = *it;
    if (binding->owner != owner) {
      ++it;
      continue;
    }
    if (binding->element != nullptr && binding->event_listener != nullptr) {
      binding->element->RemoveEventListener ("click",
                                            binding->event_listener.get ());
    }
    it = m_script_button_bindings.erase (it);
  }
}

wsl::gfx::ui_canvas &
ui_manager::ensure_virtual_canvas (gfx::render_context &ctx,
                                   wsl::gfx::render_window &window,
                                   uint32_t width, uint32_t height)
{
  if (!m_virtual_canvas) {
    m_virtual_canvas = std::make_unique<gfx::ui_canvas> ();
  }
  m_virtual_canvas->ensure (window, ctx, width, height);
  return *m_virtual_canvas;
}

wsl::gfx::ui_canvas *
ui_manager::virtual_canvas ()
{
  return m_virtual_canvas.get ();
}

void
ui_manager::prepare_scene (entt::registry &registry)
{
  if (m_prepared_registry == &registry) {
    return;
  }

  clear_scene_bindings ();
  if (m_active_document_instance != nullptr) {
    m_active_document_instance->Close ();
    m_active_document_instance = nullptr;
  }

  m_loaded_document_id.value = entt::null;
  m_prepared_registry = &registry;
}

void
ui_manager::load_font (const std::string &path)
{
  if (m_loaded_fonts.contains (path)) {
    return;
  }

  if (Rml::LoadFontFace (path)) {
    m_loaded_fonts.insert (path);
  } else {
    wsl::log::editor ()->error ("Failed to load font face from {}", path);
  }
}

Rml::DataModelConstructor
ui_manager::ensure_data_model (entt::registry &registry,
                               const std::string &name)
{
  if ((m_context == nullptr) || name.empty ()) {
    return {};
  }

  prepare_scene (registry);

  Rml::DataModelConstructor constructor;
  const auto data_models = m_context->GetDataModels ();
  if (const auto it = data_models.find (name); it != data_models.end ()) {
    constructor = it->second;
  } else {
    constructor = m_context->CreateDataModel (name);
  }

  if (constructor) {
    m_active_model_names.insert (name);
  }

  return constructor;
}

} // namespace singl
} // namespace comp

} // namespace wsl
