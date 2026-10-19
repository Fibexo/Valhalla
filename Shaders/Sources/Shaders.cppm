module;
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

export module Shaders;

export struct ShaderCode
{
	std::string entryName;
	std::vector<uint32_t> code;

	[[nodiscard]] auto begin() const { return code.begin(); }
	[[nodiscard]] auto size() const { return code.size(); }
	[[nodiscard]] auto data() const { return code.data(); }
};

export class Shaders
{
public:
	static const ShaderCode& GetShader(const std::string& name);
};


#define REGISTER_SHADER_FROM_EMBEDDED(register_name,entry_name) \
		data.emplace(#register_name, ShaderCode{#entry_name, std::vector(register_name, register_name + std::size(register_name))});

static std::unordered_map<std::string, ShaderCode> data;

const ShaderCode& Shaders::GetShader(const std::string& name)
{
	if (data.empty())
	{
#define const constexpr
#include "Shaders.g.h"
#undef const
	}
	try
	{
		return data.at(name);
	}
	catch (std::out_of_range&)
	{
		throw std::runtime_error("Shader not found: " + name);
	}
}
