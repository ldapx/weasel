options gen2
module transition_demo_system

require weasel

// Scene-transition and prefab demo: graceful degradation when targets are
// missing. Async load/change tickets do not exist yet, so missing assets
// report null/false synchronously instead of pending.
class System : GameSystem {
    frame : int = 0

    def override on_start(var ctx : SystemContext) : void {
    }

    def override update(var ctx : SystemContext; dt : float) : void {
        frame = frame + 1
        if (frame != 1) {
            return
        }
        var changed = change_scene(ctx.world, "NoSuchScene")
        assert(!changed)
        var missing = instantiate_prefab(ctx.world, "res://rsc/Nope.prefab")
        var missing_null = is_null(missing)
        assert(missing_null)
        var nowhere = ctx.world.find_by_name("NoSuchEntity")
        var nowhere_null = is_null(nowhere)
        assert(nowhere_null)
    }

    def override on_stop(var ctx : SystemContext) : void {
    }
}
