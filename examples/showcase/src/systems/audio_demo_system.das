options gen2
module audio_demo_system

require weasel

// Audio control demo: entity-level play/stop/volume on an Audio component.
// Headless runs (and builds without an audio mixer) degrade gracefully:
// controls report success when the component exists, while playback state
// stays stopped until the audio system and a mixer actually run.
class System : GameSystem {
    frame : int = 0

    def override on_start(var ctx : SystemContext) : void {
    }

    def override update(var ctx : SystemContext; dt : float) : void {
        frame = frame + 1
        if (frame != 1) {
            return
        }
        var speaker = ctx.world.find_by_name("Speaker")
        if (is_null(speaker)) {
            return
        }
        var started = play_audio(ctx.world, speaker)
        assert(started)
        var leveled = set_audio_volume(ctx.world, speaker, 0.5)
        assert(leveled)
        var stopped = stop_audio(ctx.world, speaker)
        assert(stopped)
    }

    def override on_stop(var ctx : SystemContext) : void {
    }
}
