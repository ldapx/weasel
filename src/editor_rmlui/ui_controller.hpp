#pragma once

// CONTROLLER — input handling and per-frame orchestration. The controller
// owns the SDL event flow (click dispatch, drags, camera keys), the RmlUi
// event listeners, the game-view gizmo interaction, and dialog processing.
// It mutates the model and asks the view to refresh; it generates no markup
// itself.

#include "ui_model.hpp"
#include "ui_view.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/Event.h>

namespace editor_rml
{

/** Adapter turning a std::function into an RmlUi event listener. */
class listener_adapter final : public Rml::EventListener
{
public:
  explicit listener_adapter (std::function<void (Rml::Event &)> fn)
      : m_fn (std::move (fn))
  {
  }

  void
  ProcessEvent (Rml::Event &event) override
  {
    if (m_fn) {
      m_fn (event);
    }
  }

private:
  std::function<void (Rml::Event &)> m_fn;
};

struct ui_controller
{
  ui_controller (ui_model &model, ui_view &view) : model_ (model), view_ (view)
  {
  }

  void attach (Rml::ElementDocument *document, Rml::Context *context);
  void detach ();

  /** Drops all event listeners (before the RmlUi context is destroyed). */
  void clear_event_listeners ();

  /** Forwards one engine (SDL) event: UI routing plus camera/drag input. */
  void handle_event (const wsl::engine_event &event);

  /**
   * Per-frame input: viewport resize, camera fly, dialogs, hover tracking,
   * welcome auto-dismiss, game-view gizmos, build polling.
   */
  void update_frame (double dt);

  /** Subscribes the RmlUi-side listeners (console input, log filter...). */
  void wire_events ();

  /** Routes one press-matched click id to its action. */
  void dispatch_click (const std::string &id);

  /** Reads the new-project form and forwards it to the model. */
  void create_project_from_form ();

  /**
   * Applies a finished native dialog: project/scene/folder selection.
   * Owns the ``np-folder`` form write, so it needs the document.
   */
  void process_dialog_result ();

  ui_model &model_;
  ui_view &view_;

  Rml::ElementDocument *document_ = nullptr;
  Rml::Context *context_ = nullptr;
  std::vector<std::unique_ptr<listener_adapter>> listeners;

  // Splitter drag: 1 = left edge, 2 = right edge, 3 = bottom edge.
  int active_handle = 0;
  std::string pending_click_id;

  // Camera fly state.
  bool rmb_down = false;
  bool key_w = false;
  bool key_a = false;
  bool key_s = false;
  bool key_d = false;
  bool key_q = false;
  bool key_e = false;
  bool key_shift = false;

  // Game-view gizmo pointer state (for view/transform gizmo interaction).
  float prev_mouse_x = 0.0F;
  float prev_mouse_y = 0.0F;
  bool prev_lmb_down = false;
  bool have_prev_mouse = false;

  /** Window pixel density, matching RmlSDL's mouse-coordinate convention. */
  float pointer_density () const;

  /**
   * Returns the id of the deepest-named element under the point. A pure
   * hit test via Context::GetElementAtPoint (no hover-chain side effects).
   */
  std::string hovered_element_id (float x, float y);

  /** Returns the drag-handle id under the given window-space point. */
  int hit_test_handle (float x, float y);

  /** Updates the dragged splitter from a window-space mouse position. */
  void drag_handle (float x, float y);

  void toggle_menu (const std::string &button_id);
  void close_menus ();
  void handle_menu_action (const std::string &id);

  /** Per-frame WASD/QE camera fly controls. */
  void update_camera (double dt);
  bool camera_input_enabled () const;

  /**
   * Drives the 3D game-view gizmos (view navigation cube + entity
   * transform handles) from the ``#gameview`` element rect.
   */
  void update_game_view_gizmos (double dt);
};

} // namespace editor_rml
