#pragma once

#include "../event/event_hub.hpp"
#include "../comp/component_meta.hpp"
#include "wsl/log/log.hpp"

#include "entt/entity/fwd.hpp"
#ifndef IN_MODULE_INTERFACE
#include <entt/entt.hpp>
#endif
#ifndef IN_MODULE_INTERFACE
#include <functional>
#endif
#ifndef IN_MODULE_INTERFACE
#include <string>
#endif
#ifndef IN_MODULE_INTERFACE
#include <tracy/Tracy.hpp>
#endif
#ifndef IN_MODULE_INTERFACE
#include <utility>
#endif
#ifndef IN_MODULE_INTERFACE
#include <vector>
#endif

namespace wsl
{

// Logic and behavior implemented through ECS systems.
namespace sys
{

class ecs_system
{
public:
  using iteration_fn_t = std::function<void (entt::registry &, double)>;

  struct registered_iteration
  {
    std::string name;
    iteration_fn_t fn;
    std::vector<entt::id_type> read_components;
    std::vector<entt::id_type> write_components;
  };

  explicit ecs_system (const std::string &name) : m_name (name) {}
  virtual ~ecs_system () = default;

  virtual entt::id_type get_type_id () const = 0;
  virtual const char *get_type_name () const = 0;

  virtual void
  register_event_sources (event::event_hub &)
  {
  }
  virtual void
  register_event_sinks (event::event_hub &)
  {
  }
  virtual void
  register_iterations (event::event_hub &)
  {
  }

  virtual void
  on_init (entt::registry &)
  {
  }
  virtual void
  on_inactive (entt::registry &)
  {
  }
  virtual void
  on_update (entt::registry &, double /*dt*/)
  {
  }
  virtual void
  on_editor_update (entt::registry &, double /*dt*/)
  {
  }

  /**
   * Fixed-rate lifecycle entry point. Runs zero or more times per frame,
   * driven by the frame-phase driver before the variable `on_update` pass,
   * with the physics fixed step. Default is a no-op so existing systems
   * (including all built-in C++ systems) are unaffected.
   */
  virtual void
  on_fixed_update (entt::registry &, double /*fixed_dt*/)
  {
  }

  /**
   * Late lifecycle entry point. Runs once per frame after the variable
   * `on_update` pass. Default is a no-op.
   */
  virtual void
  on_late_update (entt::registry &, double /*dt*/)
  {
  }

  virtual void
  on_render_build_draw_data (entt::registry &)
  {
  }
  virtual void
  on_render_prepare_gpu_rsc (entt::registry &)
  {
  }
  virtual void
  on_render_record_draw_cmd (entt::registry &)
  {
  }

  void
  init (entt::registry *registry)
  {
    ZoneScopedN ("sys::init");
    ZoneText (m_name.data (), m_name.size ());

    if (m_initialized) {
      return;
    }

    wsl::log::sys ()->trace ("Initializing {}", get_name ());
    on_init (*registry);
    m_initialized = true;
  }

  void
  shutdown (entt::registry *registry)
  {
    ZoneScopedN ("sys::shutdown");
    ZoneText (m_name.data (), m_name.size ());

    if (m_active && (registry != nullptr)) {
      wsl::log::sys ()->trace ("Shutting down {}", get_name ());
      on_inactive (*registry);
    }

    m_active = false;
    m_initialized = false;
  }

  void
  refresh_activation (entt::registry *registry, bool is_playing)
  {
    if (registry == nullptr) {
      m_active = false;
      return;
    }

    if (is_playing) {
      if (m_init_on_startup) {
        if (!m_active) {
          wsl::log::sys ()->trace ("Activating {}", get_name ());
          init (registry);
          m_active = true;
        }
      } else {
        if (m_active) {
          wsl::log::sys ()->trace ("Deactivating {}", get_name ());
          on_inactive (*registry);
          m_active = false;
        }
      }
    } else {
      // Editor mode: init if the system is editor-active and not yet
      // initialized (e.g. so observers are connected for live editing).
      if (!m_initialized && m_editor_active) {
        init (registry);
      }
      if (m_active) {
        wsl::log::sys ()->trace ("Deactivating {}", get_name ());
        on_inactive (*registry);
        m_active = false;
      }
    }
  }

  void
  set_init_on_startup (bool value, entt::registry *registry, bool is_playing)
  {
    m_init_on_startup = value;
    refresh_activation (registry, is_playing);
  }

  void
  set_editor_active (bool value)
  {
    m_editor_active = value;
  }

  void
  set_active (bool value, entt::registry *registry)
  {
    if (m_active == value) {
      return;
    }

    if (value) {
      if (registry != nullptr) {
        init (registry);
        m_active = true;
      }
    } else {
      if (registry != nullptr) {
        on_inactive (*registry);
      }
      m_active = false;
    }
  }

  void
  update (entt::registry *registry, double dt)
  {
    ZoneScopedN ("sys::update");
    ZoneText (m_name.data (), m_name.size ());

    if (m_active && (registry != nullptr)) {
      on_update (*registry, dt);
    }
  }

  void
  fixed_update (entt::registry *registry, double fixed_dt)
  {
    ZoneScopedN ("sys::fixed_update");
    ZoneText (m_name.data (), m_name.size ());

    if (m_active && (registry != nullptr)) {
      on_fixed_update (*registry, fixed_dt);
    }
  }

  void
  late_update (entt::registry *registry, double dt)
  {
    ZoneScopedN ("sys::late_update");
    ZoneText (m_name.data (), m_name.size ());

    if (m_active && (registry != nullptr)) {
      on_late_update (*registry, dt);
    }
  }

  void
  editor_update (entt::registry *registry, double dt)
  {
    ZoneScopedN ("sys::editor_update");
    ZoneText (m_name.data (), m_name.size ());

    if (m_editor_active && (registry != nullptr)) {
      on_editor_update (*registry, dt);
    }
  }

  void
  render_build_draw_data (entt::registry *registry)
  {
    ZoneScopedN ("sys::render_build_draw_data");
    ZoneText (m_name.data (), m_name.size ());

    if ((m_active || m_editor_active) && (registry != nullptr)) {
      on_render_build_draw_data (*registry);
    }
  }

  void
  render_prepare_gpu_rsc (entt::registry *registry)
  {
    ZoneScopedN ("sys::render_prepare_gpu_rsc");
    ZoneText (m_name.data (), m_name.size ());

    if ((m_active || m_editor_active) && (registry != nullptr)) {
      on_render_prepare_gpu_rsc (*registry);
    }
  }

  void
  render_record_draw_cmd (entt::registry *registry)
  {
    ZoneScopedN ("sys::render_record_draw_cmd");
    ZoneText (m_name.data (), m_name.size ());

    if ((m_active || m_editor_active) && (registry != nullptr)) {
      on_render_record_draw_cmd (*registry);
    }
  }

  const std::string &
  get_name () const
  {
    return m_name;
  }

  const std::string &
  get_stage () const
  {
    return m_stage;
  }

  /** Assign the execution stage (must match a name in `stage_registry`). */
  void
  set_stage (std::string name)
  {
    m_stage = std::move (name);
  }

  /** Append a declared dependency by system display name. */
  void
  add_dependency (std::string name)
  {
    m_dependencies.push_back (std::move (name));
  }

  /** Append a declared conflict by system display name. */
  void
  add_conflict (std::string name)
  {
    m_conflicts.push_back (std::move (name));
  }

  /**
   * Union of component ids touched by this system's registered iterations.
   * Used by the scheduler to derive component-access edges (CADS). Systems
   * that touch components only via raw `registry.get/view` (not through
   * `register_iteration`) yield empty sets and are treated conservatively.
   */
  void
  collect_component_access (std::vector<entt::id_type> &out_reads,
                            std::vector<entt::id_type> &out_writes) const
  {
    for (const auto &it : m_iterations) {
      out_reads.insert (out_reads.end (), it.read_components.begin (),
                        it.read_components.end ());
      out_writes.insert (out_writes.end (), it.write_components.begin (),
                         it.write_components.end ());
    }
  }

  const std::vector<std::string> &
  get_dependencies () const
  {
    return m_dependencies;
  }

  const std::vector<std::string> &
  get_conflicts () const
  {
    return m_conflicts;
  }

  bool
  is_active () const
  {
    return m_active;
  }

  bool
  is_init_on_startup () const
  {
    return m_init_on_startup;
  }

  bool
  is_editor_active () const
  {
    return m_editor_active;
  }

  virtual bool
  has_failed () const
  {
    return false;
  }

protected:
  void
  set_dependencies (std::vector<std::string> dependencies)
  {
    m_dependencies = std::move (dependencies);
  }

  void
  set_conflicts (std::vector<std::string> conflicts)
  {
    m_conflicts = std::move (conflicts);
  }

  void
  set_relationships (std::vector<std::string> dependencies,
                     std::vector<std::string> conflicts = {})
  {
    m_dependencies = std::move (dependencies);
    m_conflicts = std::move (conflicts);
  }

  void
  clear_registered_iterations ()
  {
    m_iterations.clear ();
  }

  void
  run_registered_iterations (entt::registry &registry, double dt)
  {
    for (auto &it : m_iterations) {
      if (it.fn) {
        it.fn (registry, dt);
      }
    }
  }

  void
  run_registered_iteration (entt::registry &registry, double dt,
                            const char *iteration_name)
  {
    if (iteration_name == nullptr) {
      return;
    }

    for (auto &it : m_iterations) {
      if (it.name == iteration_name) {
        if (it.fn) {
          it.fn (registry, dt);
        }
        return;
      }
    }
  }

  std::vector<registered_iteration> m_iterations;

private:
  std::string m_name;
  std::string m_stage = "logic";
  std::vector<std::string> m_dependencies;
  std::vector<std::string> m_conflicts;
  bool m_active = false;
  bool m_init_on_startup = true;
  bool m_editor_active = true;
  bool m_initialized = false;
};

template <typename Derived> class ecs_system_t : public ecs_system
{
  ecs_system_t () = default;

public:
  using ecs_system::ecs_system;

  entt::id_type
  get_type_id () const override
  {
    return wsl::comp::stable_type_id<Derived> ();
  }

  const char *
  get_type_name () const override
  {
    static const std::string type_name
        = std::string (entt::type_name<Derived> ().value ());
    return type_name.c_str ();
  }

protected:
  template <typename... Components, typename Fn>
  void
  register_iteration (event::event_hub &hub, const char *iteration_name,
                      Fn &&fn)
  {
    hub.template declare_iteration<Derived, Components...> (iteration_name);

    registered_iteration it{ iteration_name ? iteration_name : "",
                             ecs_system::iteration_fn_t (std::forward<Fn> (fn)),
                             {},
                             {} };
    (it.write_components.push_back (wsl::comp::stable_type_id<Components> ()),
     ...);
    this->m_iterations.push_back (std::move (it));
  }

  /**
   * Like `register_iteration` but splits component access into reads and
   * writes so the scheduler can derive precise component-access (CADS) edges.
   * Prefer this form so parallel systems are not over-serialized.
   */
  template <typename... Reads, typename... Writes, typename Fn>
  void
  register_iteration_rw (event::event_hub &hub, const char *iteration_name,
                         Fn &&fn)
  {
    hub.template declare_iteration<Derived, Reads..., Writes...> (
        iteration_name);

    registered_iteration it{ iteration_name ? iteration_name : "",
                             ecs_system::iteration_fn_t (std::forward<Fn> (fn)),
                             {},
                             {} };
    (it.read_components.push_back (wsl::comp::stable_type_id<Reads> ()), ...);
    (it.write_components.push_back (wsl::comp::stable_type_id<Writes> ()), ...);
    this->m_iterations.push_back (std::move (it));
  }

  friend Derived;
};

} // namespace sys

} // namespace wsl
