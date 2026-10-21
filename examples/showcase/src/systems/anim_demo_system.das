options gen2
module anim_demo_system

require weasel

// Animation state demo: drives an Animator component through
// play/crossfade/pause/resume/stop and reads back state. Clips are asset
// paths; without a loaded skeleton the duration queries stay at -1
// (unknown), which is the documented sentinel, not an error.
class System : GameSystem {
    frame : int = 0

    def override on_start(var ctx : SystemContext) : void {
    }

    def override update(var ctx : SystemContext; dt : float) : void {
        frame = frame + 1
        var actor = ctx.world.find_by_name("Actor")
        if (is_null(actor)) {
            return
        }
        if (frame == 1) {
            var started = play_animation(ctx.world, actor, "res://rsc/models/Fox_Walk.anim.ozz")
            assert(started)
            var playing = is_animation_playing(ctx.world, actor)
            assert(playing)
        }
        if (frame == 2) {
            var faded = crossfade_animation(ctx.world, actor, "res://rsc/models/Fox_Run.anim.ozz", 0.12)
            assert(faded)
            var paused = pause_animation(ctx.world, actor)
            assert(paused)
            var is_paused = is_animation_paused(ctx.world, actor)
            assert(is_paused)
        }
        if (frame == 3) {
            var resumed = resume_animation(ctx.world, actor)
            assert(resumed)
            var stopped = stop_animation(ctx.world, actor)
            assert(stopped)
            var still = is_animation_playing(ctx.world, actor)
            assert(!still)
        }
    }

    def override on_stop(var ctx : SystemContext) : void {
    }
}
