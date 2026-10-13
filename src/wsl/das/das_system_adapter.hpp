#pragma once

#include "../sys/system.hpp"
#include "../event/event_hub.hpp"
#if WEASEL_HAS_DASLANG
#include "modules/weasel_ecs_adapter_gen.inc"
#endif
#ifndef IN_MODULE_INTERFACE
#include <cstdint>
#endif
#ifndef IN_MODULE_INTERFACE
#include <memory>
#endif
#ifndef IN_MODULE_INTERFACE
#include <string>
#endif
#ifndef IN_MODULE_INTERFACE
#include <vector>
#endif

namespace das
{
struct StructInfo;
class Context;
}

namespace wsl::das
{

class das_engine;

/**
 * Dual-inheritance bridge: ecs_system (C++) + EcsSystemAdapter
 * (daslang).
 *
 * When C++ calls on_init/on_update/on_event/on_inactive, the adapter
 * checks whether the daslang class overrides the method and invokes it
 * through das_invoke_function. This is the class adapter pattern from
 * daScript tutorial 19.
 */
#if WEASEL_HAS_DASLANG
class das_system_adapter : public sys::ecs_system, public EcsSystemAdapter
#else
class das_system_adapter : public sys::ecs_system
#endif
{
public:
  /**
   * Constructs a daslang-backed system.
   * :param name: Display name for the system.
   * :param script_path: Path to the .das script file.
   * :param engine: Reference to the daslang engine.
   * :param type_id: Per-instance type identifier (from registration).
   * :param class_ptr: Pointer to the daslang class instance (VM heap).
   * :param class_info: StructInfo of the daslang class (for adapter offsets).
   * :param ctx: daslang execution context.
   */
  das_system_adapter (const std::string &name, const std::string &script_path,
                      das_engine &engine, entt::id_type type_id,
                      void *class_ptr, const ::das::StructInfo *class_info,
                      ::das::Context *ctx);

  ~das_system_adapter () override = default;

  entt::id_type get_type_id () const override;
  const char *get_type_name () const override;

  void on_init (entt::registry &registry) override;
  void on_update (entt::registry &registry, double dt) override;
  void on_inactive (entt::registry &registry) override;

  void register_event_sources (event::event_hub &hub) override;
  void register_event_sinks (event::event_hub &hub) override;

  bool
  has_failed () const override
  {
    return m_has_failed;
  }

  const std::string &
  script_path () const
  {
    return m_script_path;
  }

  /**
   * Records a script-declared event source owned by this system and
   * re-declares all stored sources into `hub`.
   *
   * :param event_id: Stable id of an already-known (C++-defined) event type.
   * :param event_name: Reflected C++ type name of the event.
   * :param event_size: Payload size in bytes of the event type.
   * :return: ``true`` if the source was recorded and declared.
   */
  bool add_script_event_source (entt::id_type event_id,
                                std::string_view event_name,
                                std::size_t event_size);

  /**
   * Records a script-declared event sink (a class method handler by name)
   * owned by this system and re-declares all stored sinks into `hub`.
   *
   * :param event_id: Stable id of an already-known (C++-defined) event type.
   * :param event_name: Reflected C++ type name of the event.
   * :param method_name: Name of the daslang class method acting as handler.
   * :return: ``true`` if the sink was recorded and declared.
   */
  bool add_script_event_sink (entt::id_type event_id,
                              std::string_view event_name,
                              std::string_view method_name,
                              std::size_t event_size = 0,
                              std::uint64_t payload_layout_hash = 0);

  /** Returns the raw-POD structure accepted by a declared event method. */
  bool event_handler_payload_signature (const char *method_name,
                                        std::size_t &payload_size,
                                        std::uint64_t &layout_hash) const;

  /** Returns ``true`` when the daslang class declares a method `method_name`.
   */
  bool has_method (const char *method_name) const;

  /** Returns the adapter currently executing a lifecycle call, or nullptr. */
  static das_system_adapter *current ();

  /** Daslang helper: assign the stage of the currently-running system. */
  static void
  set_current_stage (std::string_view stage)
  {
    if (auto *c = current ()) {
      c->set_stage (std::string (stage));
    }
  }

  /** Daslang helper: declare a dependency for the running system. */
  static void
  add_current_dependency (std::string_view name)
  {
    if (auto *c = current ()) {
      c->add_dependency (std::string (name));
    }
  }

  /** Daslang helper: declare a conflict for the running system. */
  static void
  add_current_conflict (std::string_view name)
  {
    if (auto *c = current ()) {
      c->add_conflict (std::string (name));
    }
  }

private:
  struct event_declaration
  {
    entt::id_type event_id{};
    std::string event_name;
    std::string method_name;
    std::size_t event_size = 0;
    std::uint64_t payload_layout_hash = 0;
  };

  /** Heap block passed as the sink ``owner`` so one static thunk can serve
   *  every declaration (the invoke fn carries no closure). */
  struct sink_invoker
  {
    das_system_adapter *adapter = nullptr;
    std::size_t index = 0;
  };

  static void sink_thunk (void *owner, entt::registry &registry,
                          const void *payload);

  void invoke_event_handler (std::size_t index, entt::registry &registry,
                             const void *payload);

  std::string m_script_path;
  das_engine &m_engine;
  entt::id_type m_type_id;
  void *m_class_ptr;
  const ::das::StructInfo *m_class_info;
  ::das::Context *m_ctx;
  bool m_has_failed = false;

  std::vector<event_declaration> m_event_sources;
  std::vector<event_declaration> m_event_sinks;
  std::vector<std::unique_ptr<sink_invoker>> m_sink_invokers;
};

} // namespace wsl::das
