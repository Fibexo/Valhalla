struct RenderData
{
    float4x4 mvp;
    float4 color;
    uint textureIndex;
};

struct PushedData
{
    RenderData* data;
};

[[vk::push_constant]]
PushedData m;

// bindless：运行时长度不定的纹理数组（runtimeDescriptorArray）
[[vk::binding(0, 0)]]
Sampler2D textures[];

[shader("vertex")]
float4 vsMain(uint id : SV_VertexID, out float2 uv : TEXCOORD0) : SV_Position
{
    const float2 p[] = {
        float2(-0.5, -0.5),
        float2(-0.5, 0.5),
        float2(0.5, -0.5),
        float2(-0.5, 0.5),
        float2(0.5, -0.5),
        float2(0.5,0.5)
    };
    uv = p[id] * 0.5 + 0.5;                     // 位置映射到 [0,1]，暂时当 UV 用
    return mul(m.data->mvp, float4(p[id], 0.0, 1.0));
}

[shader("fragment")]
float4 fsMain(float2 uv : TEXCOORD0) : SV_Target
{
    return textures[m.data->textureIndex].Sample(uv) * float4(uv,0,1);
}
