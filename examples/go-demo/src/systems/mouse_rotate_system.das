options gen2
module mouse_rotate_system

require weasel
require math
require mouse_rotate

def quaternion_from_euler_degrees(pitch : float; yaw : float; roll : float) : float4 {
    let half_pitch = radians(pitch) * 0.5
    let half_yaw = radians(yaw) * 0.5
    let half_roll = radians(roll) * 0.5
    let cp = cos(half_pitch)
    let sp = sin(half_pitch)
    let cy = cos(half_yaw)
    let sy = sin(half_yaw)
    let cr = cos(half_roll)
    let sr = sin(half_roll)
    return float4(
        sp * cy * cr + cp * sy * sr,
        cp * sy * cr - sp * cy * sr,
        cp * cy * cr - sp * sy * sr,
        cp * cy * cr + sp * sy * sr)
}

class System : GameSystem {
    def override on_start(var ctx : SystemContext) : void {
        query(ctx.world) $(t : Transform&; mr : MouseRotate&) {
            mr.yaw = 0.0
            mr.pitch = 0.0
        }
    }

    def override on_stop(var ctx : SystemContext) : void {
        ctx.input.set_mouse_captured(false)
    }

    def override update(var ctx : SystemContext; dt : float) : void {
        var active_cam = active_camera(ctx.world)
        var fp_active = false

        if (!is_null(active_cam)) {
            query(ctx.world) $(mr : MouseRotate&) {
                if (has(ctx.world, entity, Camera()) && entity == active_cam) {
                    fp_active = true
                }
            }
        }

        if (!fp_active) {
            return
        }

        // Consumes this frame's accumulated relative motion.
        var md = ctx.input.mouse_delta()
        if (md.x == 0.0 && md.y == 0.0) return

        query(ctx.world) $(t : Transform&; mr : MouseRotate&) {
            mr.yaw -= md.x * mr.sensitivity
            mr.pitch -= md.y * mr.sensitivity
            mr.pitch = clamp(mr.pitch, mr.pitch_min, mr.pitch_max)
            t.rotation = quaternion_from_euler_degrees(mr.pitch, mr.yaw, 0.0)
        }
    }
}
