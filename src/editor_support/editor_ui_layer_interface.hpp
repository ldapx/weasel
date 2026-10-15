#pragma once

#ifndef IN_MODULE_INTERFACE
#include <entt/entity/fwd.hpp>
#endif
#ifndef IN_MODULE_INTERFACE
#include <functional>
#endif
#ifndef IN_MODULE_INTERFACE
#include <string>
#endif

namespace wsl
{
class engine_event;
}

namespace wsl::comp::singl
{
class runtime_context;
class editor_context;
}

namespace wsl::editor
{

class editor_ui_layer_interface
{
public:
  virtual ~editor_ui_layer_interface () = default;

  virtual void initialize () = 0;
  virtual void set_console_command_handler (
      std::function<std::string (const std::string &)> handler) = 0;
  virtual void handle_event (const wsl::engine_event &e) = 0;
  virtual void build_draw_data (entt::registry &reg) = 0;
  virtual void prepare_gpu_resources () = 0;
  virtual void record_draw_commands () = 0;
};

} // namespace wsl::editor
