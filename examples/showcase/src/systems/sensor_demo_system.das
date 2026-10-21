options gen2
module sensor_demo_system

require weasel

// Trigger-volume demo: subscribes to the engine-owned Area3D overlap
// events and marks the overlapping entity. In live play the physics
// engine emits these when a body enters/exits a sensor area; the
// subscription, payload conversion, and cleanup are identical either way.
class System : GameSystem {
    sub_enter : Subscription = Subscription()
    sub_exit : Subscription = Subscription()

    def override on_start(var ctx : SystemContext) : void {
        sub_enter = subscribe_area_entered("on_entered")
        var enter_ok = sub_enter.is_valid()
        assert(enter_ok)
        sub_exit = subscribe_area_exited("on_exited")
        var exit_ok = sub_exit.is_valid()
        assert(exit_ok)
    }

    def override update(var ctx : SystemContext; dt : float) : void {
    }

    def override on_stop(var ctx : SystemContext) : void {
        sub_enter.dispose()
        sub_exit.dispose()
    }

    def on_entered(overlap : SensorOverlap) {
        var world = current_world()
        var other = world.sensor_other(overlap)
        if (world.is_alive(other)) {
            var marker = world.create_entity()
            var named = world.set_name(marker, "Touched")
            assert(named)
        }
    }

    def on_exited(overlap : SensorOverlap) {
        var world = current_world()
        var other = world.sensor_other(overlap)
        if (world.is_alive(other)) {
            var marker = world.create_entity()
            var named = world.set_name(marker, "Untouched")
            assert(named)
        }
    }
}

// Test hook for headless verification: posts a synthetic overlap for the
// named target. Real overlaps arrive from physics in live play.
def debug_post_overlap() : void {
    var world = current_world()
    var tgt = world.find_by_name("Target")
    if (is_null(tgt)) {
        return
    }
    var overlap = sensor_overlap(tgt, tgt)
    event_post("entered", overlap)
}
