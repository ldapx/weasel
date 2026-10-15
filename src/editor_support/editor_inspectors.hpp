#pragma once

/**
 * Editor inspector registration.
 *
 * The ImGui inspector widgets (`custom_inspect`) for engine components,
 * resources and math types live in editor_support so the core library
 * (`libwsl`) stays free of ImGui. Core `register_meta()` functions register
 * the reflected data fields only; this entry point re-registers the
 * ImGui-backed inspector functions on top of them.
 *
 * Call once after the core component/singleton meta registration has run
 * (wsl::editor_app does this in its constructor). Safe to call repeatedly.
 */
namespace wsl
{
namespace editor
{

void register_editor_inspectors ();

} // namespace editor
} // namespace wsl
