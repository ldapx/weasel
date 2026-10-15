// Fullscreen-triangle vertex shader for the editor game-view blit.
// No vertex buffers or uniforms; the viewport rect positions the draw.

struct VSOut {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

[shader("vertex")]
VSOut vsMain(uint id : SV_VertexID)
{
    float2 p = (id == 0) ? float2(-1, -1) :
               (id == 1) ? float2(-1,  3) :
                           float2( 3, -1);

    VSOut o;
    o.pos = float4(p, 0, 1);
    // Flip V: engine render textures store the first row at the bottom, so
    // the clip-space bottom edge must sample V=1 to stay upright on screen.
    o.uv = float2(p.x * 0.5f + 0.5f, 0.5f - p.y * 0.5f);
    return o;
}
