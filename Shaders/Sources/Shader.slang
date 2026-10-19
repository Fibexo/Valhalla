struct RenderData
{
    float4x4 m;
    float4x4 v;
    float4x4 p;
    float4 color;

    float3 light_color;
    float3 light_dir;

    uint texture_index;
};

struct Vertex{
    float3 position;
    float3 normal;
    float2 uv;
};

struct PushConstants
{
    uint64_t data_address;
    uint64_t vertices_address;
    uint64_t indices_address;
};

[[vk::push_constant]]
PushConstants pc;

[[vk::binding(0, 0)]]
Sampler2D textures[];

[shader("vertex")]
float4 vsMain(uint id : SV_VertexID, uint iid : SV_InstanceID, out Vertex vertex) : SV_Position
{
    RenderData data = *(RenderData*)pc.data_address;
    uint *indices = (uint*)pc.indices_address;
    Vertex *vertices = (Vertex*)pc.vertices_address;

    vertex = vertices[indices[id]];
    vertex.position = mul(data.m, float4(vertex.position,1.0)).xyz;
    vertex.normal = mul(data.m, float4(vertex.normal,1.0)).xyz;

    return mul(data.p,mul(data.v,float4(vertex.position,1.0)));
}

[shader("fragment")]
float4 fsMain(in Vertex vertex) : SV_Target
{
    RenderData data = *(RenderData*)pc.data_address;
    uint *indices = (uint*)pc.indices_address;

    float3 obj_color = (textures[data.texture_index].Sample(vertex.uv) * data.color).xyz;

    float ambient_strength = 0.1f;
    float diffuse_strength = max(dot(vertex.normal, normalize(data.light_dir)), 0.0);

    float3 ambient = ambient_strength * data.light_color;
    float3 diffuse = diffuse_strength * data.light_color;
    float3 result = (ambient+diffuse) * obj_color;

    return float4(result,1.0f);
}
