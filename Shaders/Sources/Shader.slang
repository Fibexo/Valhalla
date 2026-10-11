struct RenderData
{
    float4x4 mvp;
    float4 color;
    uint textureIndex;
};

struct Vertex{
    float4 position;
    float2 uv;
};

struct PushConstants
{
    RenderData* data;
    Vertex* vertices;
    uint* indices;
};

[[vk::push_constant]]
PushConstants pc;

// bindless：运行时长度不定的纹理数组（runtimeDescriptorArray）
[[vk::binding(0, 0)]]
Sampler2D textures[];

static float4 light;

[shader("vertex")]
float4 vsMain(uint id : SV_VertexID, uint iid : SV_InstanceID, out Vertex v) : SV_Position
{
    v = pc.vertices[pc.indices[id]];
    return mul(pc.data->mvp, v.position);
}

[shader("fragment")]
float4 fsMain(in Vertex v) : SV_Target
{
    return textures[pc.data->textureIndex].Sample(v.uv) * pc.data->color;
}
