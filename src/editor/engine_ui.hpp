#pragma once

#include "wsl/comp/singl/runtime_context_fwd.hpp"
#include "root.hpp"
#include "wsl/event/event_hub.hpp"
#include "editor_support/editor_ui_layer_interface.hpp"

#include <entt/entity/fwd.hpp>
#include <imgui.h>

namespace editor
{

class engine_ui : public wsl::editor::editor_ui_layer_interface
{
public:
  struct game_focus_toggled
  {
    bool focused = true;
  };

  engine_ui (wsl::comp::singl::runtime_context *runtime_ctx,
             wsl::comp::singl::editor_context *editor_ctx);

  static void register_event_sources (wsl::event::event_hub &hub);
  void initialize () override;
  void set_console_command_handler (
      std::function<std::string (const std::string &)> handler) override;
  void handle_event (const wsl::engine_event &event) override;
  void build_draw_data (entt::registry &registry) override;
  void prepare_gpu_resources () override;
  void record_draw_commands () override;

private:
  wsl::comp::singl::runtime_context *m_runtime_ctx = nullptr;
  wsl::comp::singl::editor_context *m_editor_ctx = nullptr;
  root m_root;
  ImDrawData *m_draw_data = nullptr;
  bool m_game_focus = true;
};

} // namespace editor
