options gen2
module menu_demo_system

require weasel

// UI callback demo: binds a button id to a no-argument method on this
// system. With no RML document loaded the bind fails gracefully (false);
// with a document it returns true and stays connected until dispose or
// scene teardown. The callback must take no arguments.
class System : GameSystem {
    def override on_start(var ctx : SystemContext) : void {
        var bound = ctx.ui.bind_button("start_button", "start_game")
        if (!bound) {
            ctx.log.warn("start button not bound (no document)")
        }
    }

    def start_game() : void {
        var changed = context.world.change_scene("Gameplay")
        if (!changed) {
            context.log.warn("Gameplay scene is not loaded")
        }
    }

    def override update(var ctx : SystemContext; dt : float) : void {
    }

    def override on_stop(var ctx : SystemContext) : void {
    }
}
