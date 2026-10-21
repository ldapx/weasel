#pragma once

// Shared frame-phase driver. Both scene::update (serial, tests/standalone)
// and core_systems::update (scheduler, production) run the same phase order
// so interpreted behavior matches: zero or more fixed steps, then the
// variable update, then the late update.
//
// The fixed-step policy mirrors phys::engine: the frame dt is clamped, an
// accumulator collects it, and at most max_substeps run per frame; leftover
// time beyond that is dropped (spiral-of-death guard). Per-substep
// interleaving of script code inside the physics step loop is out of scope:
// fixed script phases run before the variable pass (which is where physics
// consumes the frame), with the same fixed step the engine uses.

#ifndef IN_MODULE_INTERFACE
#include <algorithm>
#endif
#ifndef IN_MODULE_INTERFACE
#include <functional>
#endif

namespace wsl
{
namespace sys
{

/** Lifecycle phase executed by the frame driver. */
enum class frame_phase
{
  fixed,
  variable,
  late
};

/** Fixed-step policy for the frame driver. */
struct frame_phase_config
{
  double fixed_step = 1.0 / 60.0;
  double max_frame_time = 0.25;
  int max_substeps = 5;
};

/**
 * Runs one frame of lifecycle phases.
 *
 * :param dt: Variable frame delta in seconds (negative values are clamped
 *   to zero).
 * :param cfg: Fixed-step policy (step, clamp, substep cap).
 * :param accumulator: Persistent per-driver leftover time, updated in place.
 * :param run_phase: Invoked as `run_phase(phase, step_dt)` for each fixed
 *   step (with `cfg.fixed_step`), then once for `variable` (with `dt`) and
 *   once for `late` (with `dt`).
 * :param on_fixed_step: Optional hook invoked once per completed fixed step
 *   (after that step's systems ran), e.g. to advance a fixed-step clock.
 *   Empty by default.
 * :param paused: When true, fixed steps are skipped entirely (the
 *   accumulator is frozen, so no catch-up burst follows) while the
 *   variable/late passes still run. Menus and UI keep updating while the
 *   simulation holds still.
 */
template <typename RunPhaseFn>
void
run_frame_phases (double dt, const frame_phase_config &cfg, double &accumulator,
                  RunPhaseFn &&run_phase,
                  const std::function<void (double)> &on_fixed_step = {},
                  bool paused = false)
{
  if (dt < 0.0) {
    dt = 0.0;
  }
  if (!paused) {
    if (cfg.fixed_step <= 0.0) {
      run_phase (frame_phase::variable, dt);
      run_phase (frame_phase::late, dt);
      return;
    }
    accumulator += std::min (dt, cfg.max_frame_time);
    int steps = 0;
    while (accumulator >= cfg.fixed_step && steps < cfg.max_substeps) {
      run_phase (frame_phase::fixed, cfg.fixed_step);
      if (on_fixed_step) {
        on_fixed_step (cfg.fixed_step);
      }
      accumulator -= cfg.fixed_step;
      ++steps;
    }
    if (steps >= cfg.max_substeps) {
      accumulator = 0.0;
    }
  }
  run_phase (frame_phase::variable, dt);
  run_phase (frame_phase::late, dt);
}

} // namespace sys
} // namespace wsl
