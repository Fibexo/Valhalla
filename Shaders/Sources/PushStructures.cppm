module;

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

export module PushStructures;

using namespace glm;

export struct PushConstants;
export struct RenderData;
export struct Vertex;

struct RenderData
{
	mat4 m;
	mat4 v;
	mat4 p;
	vec4 color;

	vec3 light_color;
	vec3 light_dir;

	uint texture_index;
};

struct Vertex
{
	vec3 position;
    vec3 normal;
	vec2 uv;
};

struct PushConstants
{
	uint64_t data_address;
	uint64_t vertices_address;
	uint64_t indices_address;
};
