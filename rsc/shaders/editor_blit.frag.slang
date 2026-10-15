// Samples the engine's present texture into the editor game-view rect.
// SDL GPU SPIR-V resource layout for fragment shaders:
//   space2: sampled textures, storage textures, storage buffers
//   space3: uniform buffers

Texture2D u_Texture : register(t0, space2);
SamplerState u_Sampler : register(s0, space2);

struct PSInput {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

[shader("fragment")]
float4 fsMain(PSInput input) : SV_Target
{
    return u_Texture.Sample(u_Sampler, input.uv);
}
