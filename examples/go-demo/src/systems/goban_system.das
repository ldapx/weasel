options gen2
module goban_system

require weasel
require math

class System : GameSystem {
    m_fp_camera : Entity = Entity()
    m_goban_camera : Entity = Entity()
    m_is_black_turn : bool = true

    def override on_start(var ctx : SystemContext) : void {
        refresh_cameras(ctx)
        ctx.input.bind_key("ui_cancel", int(SDL_SCANCODE_ESCAPE))
        ctx.input.bind_mouse("place", int(SDL_BUTTON_LEFT))
        ctx.input.bind_mouse("remove", int(SDL_BUTTON_RIGHT))
        ctx.input.set_mouse_captured(true)
        ctx.input.set_cursor_visible(false)
    }

    def override on_stop(var ctx : SystemContext) : void {
        ctx.input.set_mouse_captured(false)
    }

    // Input uses named actions bound in on_start: "ui_cancel" (Escape),
    // "place" (left button press) and "remove" (right button press).
    // just_pressed reports this frame's press edges from the message bus.
    def process_input(var ctx : SystemContext) : void {
        if (ctx.input.just_pressed("ui_cancel")) {
            refresh_cameras(ctx)
            var active = active_camera(ctx.world)
            if (active == m_goban_camera) {
                switch_to_camera(ctx, m_fp_camera)
            }
        }

        if (ctx.input.just_pressed("place")) {
            var mp = ctx.input.mouse_position()
            refresh_cameras(ctx)
            var active = active_camera(ctx.world)
            if (active != m_goban_camera) {
                switch_to_camera(ctx, m_goban_camera)
            } else {
                place_stone_at(ctx, int(mp.x), int(mp.y))
            }
        }

        if (ctx.input.just_pressed("remove")) {
            var mp = ctx.input.mouse_position()
            refresh_cameras(ctx)
            var active = active_camera(ctx.world)
            if (active == m_goban_camera) {
                remove_stone_at(ctx, int(mp.x), int(mp.y))
            }
        }
    }

    def override update(var ctx : SystemContext; dt : float) : void {
        refresh_cameras(ctx)
        process_input(ctx)

        var active = active_camera(ctx.world)
        var on_fp = (active == m_fp_camera)
        var on_goban = (active == m_goban_camera)

        if (on_fp) {
            if (ctx.input.is_cursor_visible()) {
                ctx.input.set_cursor_visible(false)
            }
        } elif (on_goban) {
            if (!ctx.input.is_cursor_visible()) {
                ctx.input.set_cursor_visible(true)
            }
        }
    }

    def refresh_cameras(var ctx : SystemContext) : void {
        if (is_null(m_fp_camera) || !ctx.world.is_alive(m_fp_camera)) {
            m_fp_camera = ctx.world.find_by_name("FP Camera")
        }
        if (is_null(m_goban_camera) || !ctx.world.is_alive(m_goban_camera)) {
            m_goban_camera = ctx.world.find_by_name("Goban Camera")
        }
    }

    def switch_to_camera(var ctx : SystemContext; camera : Entity) : void {
        set_active_camera(ctx.world, camera)

        var fp_active = (camera == m_fp_camera)
        ctx.input.set_mouse_captured(fp_active)
        if (fp_active) {
            ctx.input.set_cursor_visible(false)
        } else {
            ctx.input.set_cursor_visible(true)
        }
    }

    def get_grid_at(var ctx : SystemContext; mx : int; my : int) : tuple<int,int,bool> {
        refresh_cameras(ctx)

        if (is_null(m_goban_camera)) return (0, 0, false)
        if (!has(ctx.world, m_goban_camera, Camera())) return (0, 0, false)
        if (!has(ctx.world, m_goban_camera, WorldTransform())) return (0, 0, false)

        var ws = window_size()
        if (ws.x <= 0.0 || ws.y <= 0.0) return (0, 0, false)

        var mouse_x = float(mx)
        var mouse_y = float(my)

        var (origin, dir, ray_ok) = pick_ray(ctx.world, m_goban_camera,
                                            mouse_x, mouse_y,
                                            0.0, 0.0, ws.x, ws.y)
        if (!ray_ok) {
            return (0, 0, false)
        }

        var (hit, hit_ok) = ray_plane_hit(origin.x, origin.y, origin.z,
                                          dir.x, dir.y, dir.z,
                                          0.0, 2.21, 0.0, 0.0, 1.0, 0.0)
        if (!hit_ok) {
            return (0, 0, false)
        }

        var hit_x = hit.x
        var hit_z = hit.z

        var board_min = -2.21
        var board_max = 2.21
        if (hit_x < board_min || hit_x > board_max) return (0, 0, false)
        if (hit_z < board_min || hit_z > board_max) return (0, 0, false)

        var grid_size = 18
        var step = (board_max - board_min) / float(grid_size)

        var grid_x = int(round((hit_x - board_min) / step))
        var grid_z = int(round((hit_z - board_min) / step))

        grid_x = clamp(grid_x, 0, grid_size)
        grid_z = clamp(grid_z, 0, grid_size)

        return (grid_x, grid_z, true)
    }

    def place_stone_at(var ctx : SystemContext; mx : int; my : int) : void {
        var (grid_x, grid_z, valid) = get_grid_at(ctx, mx, my)
        if (!valid) return

        var board_min = -2.21
        var board_max = 2.21
        var grid_size = 18
        var step = (board_max - board_min) / float(grid_size)
        var world_x = board_min + float(grid_x) * step
        var world_z = board_min + float(grid_z) * step

        var prefab_path = "res://rsc/scenes/Black_Stone.prefab"
        var color = "Black"
        if (!m_is_black_turn) {
            prefab_path = "res://rsc/scenes/White_Stone.prefab"
            color = "White"
        }

        var stone_name = "{color} Stone ({grid_x},{grid_z})"

        var existing_black = ctx.world.find_by_name("Black Stone ({grid_x},{grid_z})")
        if (!is_null(existing_black)) {
            ctx.world.destroy(existing_black)
        }

        var existing_white = ctx.world.find_by_name("White Stone ({grid_x},{grid_z})")
        if (!is_null(existing_white)) {
            ctx.world.destroy(existing_white)
        }

        var new_stone = instantiate_prefab(ctx.world, prefab_path)
        if (is_null(new_stone)) return

        ctx.world.set_name(new_stone, stone_name)

        if (has(ctx.world, new_stone, Transform())) {
            var transform = get(ctx.world, new_stone, Transform())
            transform.position = float3(world_x, 2.21, world_z)
            transform.scale = float3(2.0, 2.0, 2.0)
        }

        m_is_black_turn = !m_is_black_turn
    }

    def remove_stone_at(var ctx : SystemContext; mx : int; my : int) : void {
        var (grid_x, grid_z, valid) = get_grid_at(ctx, mx, my)
        if (!valid) return

        var existing_black = ctx.world.find_by_name("Black Stone ({grid_x},{grid_z})")
        if (!is_null(existing_black)) {
            ctx.world.destroy(existing_black)
            return
        }

        var existing_white = ctx.world.find_by_name("White Stone ({grid_x},{grid_z})")
        if (!is_null(existing_white)) {
            ctx.world.destroy(existing_white)
            return
        }
    }
}
