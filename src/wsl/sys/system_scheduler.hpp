#pragma once

#ifndef IN_MODULE_INTERFACE
#include <functional>
#endif
#ifndef IN_MODULE_INTERFACE
#include <memory>
#endif
#ifndef IN_MODULE_INTERFACE
#include <vector>
#endif

#ifndef IN_MODULE_INTERFACE
#include <entt/entt.hpp>
#endif

#include "reg/system_factory_registry.hpp"
#include "sys/stage_registry.hpp"
#include "sys/system.hpp"
#include "sys/system_dependency_graph.hpp"
#include "sys/task_pool.hpp"

namespace wsl::sys
{

/** Which lifecycle entry point a pass drives. */
enum class system_pass
{
  update,
  editor_update,
  fixed_update,
  late_update,
  render_build
};

/**
 * Drives a pass of ECS systems across a thread pool, fencing between
 * dependency levels produced by `system_dependency_graph`.
 */
class system_scheduler
{
public:
  system_scheduler () = default;

  void
  configure (const graph_config &cfg, std::size_t worker_threads)
  {
    std::size_t w = worker_threads == 0 ? std::thread::hardware_concurrency ()
                                        : worker_threads;
    m_cfg = cfg;
    if (!m_pool || m_workers != w) {
      m_pool = std::make_unique<task_pool> (w);
      m_workers = w;
    }
  }

  const graph_config &
  config () const
  {
    return m_cfg;
  }

  /**
   * Build levels for `systems` and execute them. `invoker` must call the
   * correct lifecycle entry point for the pass (it captures dt / frame
   * context). Systems are fenced level-by-level so every system in level N
   * finishes before level N+1 starts.
   */
  void
  run_pass (system_pass pass, const std::vector<ecs_system *> &systems,
            entt::registry &reg, const reg::system_factory_registry &sys_reg,
            const stage_registry &stages,
            const std::function<void (ecs_system *, entt::registry &)> &invoker)
  {
    if (systems.empty ()) {
      return;
    }

    const bool parallel = m_cfg.parallel_systems_enabled
                          && !(pass == system_pass::render_build
                               && !m_cfg.parallel_render_build_enabled);
    if (!parallel) {
      for (ecs_system *s : systems) {
        invoker (s, reg);
      }
      return;
    }

    if (m_cfg.deterministic_system_order) {
      auto ordered = systems;
      std::sort (ordered.begin (), ordered.end (),
                 [] (ecs_system *a, ecs_system *b) {
                   return a->get_type_id () < b->get_type_id ();
                 });
      for (ecs_system *s : ordered) {
        invoker (s, reg);
      }
      return;
    }

    auto levels = system_dependency_graph::build_levels (systems, stages,
                                                         sys_reg, m_cfg);

    for (const auto &level : levels) {
      if (level.empty ()) {
        continue;
      }
      ZoneScopedN ("sys::scheduler::level");
      if (level.size () == 1) {
        invoker (level.front (), reg);
        continue;
      }
      std::vector<std::function<void ()>> tasks;
      tasks.reserve (level.size ());
      for (ecs_system *s : level) {
        tasks.emplace_back ([s, &reg, &invoker] { invoker (s, reg); });
      }
      m_pool->dispatch (tasks); // blocks until the whole level finishes
    }
    (void)pass;
  }

private:
  std::unique_ptr<task_pool> m_pool;
  graph_config m_cfg;
  std::size_t m_workers = 0;
};

} // namespace wsl::sys
