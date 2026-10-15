#pragma once

// RmlUi process-wide runtime management.
//
// RmlUi's Initialise/Shutdown and the render/system interfaces are global.
// Multiple clients (an editor front-end and the per-scene runtime UI manager)
// may need RmlUi at the same time, so:
//
//   * acquire/release are ref-counted: Rml::Initialise runs on the first
//     acquire, Rml::Shutdown on the last release.
//   * the SDL_GPU backend instances can be published as "shared": the first
//     client (typically an editor app) installs them, and the runtime UI
//     manager attaches to them instead of creating its own. This keeps all
//     RmlUi contexts compiling/rendering through one render interface, which
//     matters because compiled geometry handles are per-interface.

class RenderInterface_SDL_GPU;

namespace wsl
{
namespace gfx
{

class ui_system_interface;

/** Acquires the global RmlUi runtime; nests safely. */
bool rmlui_acquire ();

/** Releases one reference; shuts RmlUi down when the last one goes. */
void rmlui_release ();

/** True while at least one client holds the runtime. */
bool rmlui_is_initialised ();

/** Currently published render interface, or nullptr. */
RenderInterface_SDL_GPU *shared_ui_render_interface ();

/** Publishes the process-wide render interface (keep it alive while set). */
void set_shared_ui_render_interface (RenderInterface_SDL_GPU *render_interface);

/** Currently published system interface, or nullptr. */
ui_system_interface *shared_ui_system_interface ();

/** Publishes the process-wide system interface (keep it alive while set). */
void set_shared_ui_system_interface (ui_system_interface *system_interface);

} // namespace gfx
} // namespace wsl
