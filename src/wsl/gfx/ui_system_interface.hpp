#pragma once

#include "wsl/log/log.hpp"
#include <RmlUi/Core/SystemInterface.h>
#include <RmlUi_Platform_SDL.h>
#include <SDL3/SDL.h>

namespace wsl
{
namespace gfx
{

/**
 * RmlUi system interface used by the runtime UI manager and the editors.
 * SDL-backed timing/paths plus engine logging.
 */
class ui_system_interface : public SystemInterface_SDL
{
public:
  bool
  LogMessage (Rml::Log::Type /*type*/, const Rml::String &message) override
  {
    wsl::log::editor ()->debug ("{}", message);
    return true;
  }
};

} // namespace gfx
} // namespace wsl
