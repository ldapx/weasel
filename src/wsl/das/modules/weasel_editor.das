options gen2
module weasel_editor

require weasel_api

// Editor-only viewport helpers.
//
// Import boundary: gameplay scripts must `require weasel` and must never
// require this module (or call the raw `weasel_api` editor functions). These
// helpers read editor UI state through the editor context and are no-ops
// outside an editor session: `editor_refresh_viewport` re-reads the last
// editor viewport rectangle, and the size accessors report zeros when no
// editor is attached (for example in headless or standalone runs).
//
// Enforcement note: the underlying natives still live in `weasel_api` for
// backward compatibility (in-repo examples predate the split). Splitting
// them into a separate native module so gameplay code cannot reach them is
// future work, tracked with the old-export removal cutover.

def editor_refresh_viewport() : void {
    refresh_editor_viewport()
}

def editor_viewport_min() : float2 {
    return float2(get_editor_img_min_x(), get_editor_img_min_y())
}

def editor_viewport_size() : float2 {
    return float2(get_editor_img_size_x(), get_editor_img_size_y())
}
