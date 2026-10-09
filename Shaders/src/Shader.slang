// 顶点由 SV_VertexID 现算 —— 不需要顶点缓冲，也不需要任何描述符
[shader("vertex")]
float4 vsMain(uint id : SV_VertexID) : SV_Position
{
    const float2 p[3] = { float2(0.0, -0.6), float2(0.55, 0.5), float2(-0.55, 0.5) };
    return float4(p[id], 0.0, 1.0);
}

[shader("fragment")]
float4 fsMain() : SV_Target
{
    return float4(0.9, 0.35, 0.2, 1.0);
}
