struct RenderData
{
    float4x4 mvp;
    float4 color;
};

struct PushedData
{
    RenderData* data;
};

[[vk::push_constant]]
PushedData m;

[shader("vertex")]
float4 vsMain(uint id : SV_VertexID) : SV_Position
{
    const float2 p[3] = { float2(0.0, -0.6), float2(0.55, 0.5), float2(-0.55, 0.5) };
    m.data->color = float4(1.0);
    return mul(m.data->mvp, float4(p[id], 0.0, 1.0));
}

[shader("fragment")]
float4 fsMain() : SV_Target
{
    return (sin(m.data->color * 4 * 3.14159) + 1)/2;
}
