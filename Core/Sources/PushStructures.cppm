module;

#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

export module PushStructures;

using namespace glm;

export struct PushConstants;
export struct RenderData;
export struct Vertex;

struct RenderData
{
	mat4 mvp;
	vec4 color;
	uint textureIndex;
};

struct Vertex
{
	vec4 position;
	vec2 uv;
};

struct PushConstants
{
	RenderData* data;
	Vertex* vertices;
	uint* indices;
};
