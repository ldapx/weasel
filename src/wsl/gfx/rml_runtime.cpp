#include "rml_runtime.hpp"

#include "ui_system_interface.hpp"
#include "wsl/log/log.hpp"

#include <RmlUi/Core/Core.h>

// RmlUi_Renderer_SDL_GPU.h uses `interface` as a parameter name, which
// collides with the COM macro from windows.h (combaseapi.h) whenever a
// Windows header precedes this include. Guard it order-independently;
// restored immediately after so COM consumers are unaffected.
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

namespace wsl
{
namespace gfx
{

namespace
{
int s_rmlui_refcount = 0;
RenderInterface_SDL_GPU *s_shared_render_interface = nullptr;
ui_system_interface *s_shared_system_interface = nullptr;
} // namespace

bool
rmlui_acquire ()
{
  if (s_rmlui_refcount == 0) {
    if (!Rml::Initialise ()) {
      wsl::log::editor ()->error ("RmlUi initialisation failed");
      return false;
    }
  }

  ++s_rmlui_refcount;
  return true;
}

void
rmlui_release ()
{
  if (s_rmlui_refcount == 0) {
    return;
  }

  --s_rmlui_refcount;
  if (s_rmlui_refcount == 0) {
    Rml::Shutdown ();
  }
}

bool
rmlui_is_initialised ()
{
  return s_rmlui_refcount > 0;
}

RenderInterface_SDL_GPU *
shared_ui_render_interface ()
{
  return s_shared_render_interface;
}

void
set_shared_ui_render_interface (RenderInterface_SDL_GPU *render_interface)
{
  s_shared_render_interface = render_interface;
  if (render_interface != nullptr) {
    Rml::SetRenderInterface (render_interface);
  }
}

ui_system_interface *
shared_ui_system_interface ()
{
  return s_shared_system_interface;
}

void
set_shared_ui_system_interface (ui_system_interface *system_interface)
{
  s_shared_system_interface = system_interface;
  if (system_interface != nullptr) {
    Rml::SetSystemInterface (system_interface);
  }
}

} // namespace gfx
} // namespace wsl
