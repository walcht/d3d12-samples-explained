struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
};

SamplerState g_sampler : register(s0);
Texture2D g_texture : register(t0);

PSInput VSMain(float4 p: POSITION, float2 uv : TEXCOORD)
{
    PSInput result;

    result.position = p;
    result.uv = uv;

    return result;
}

float4 PSMain(PSInput i) : SV_TARGET
{
    return g_texture.Sample(g_sampler, i.uv);
}
