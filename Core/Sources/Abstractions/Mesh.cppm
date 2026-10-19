module;

#include <vulkan-memory-allocator-hpp/vk_mem_alloc_raii.hpp>
#include <vulkan/vulkan_raii.hpp>

export module Abstractions.Mesh;

import PushStructures;

export class Mesh
{
	vma::raii::Buffer vertexBuffer{nullptr};
	vk::DeviceAddress vertexAddress{};
	vk::DeviceSize vertexSize{};

	vma::raii::Buffer indexBuffer{nullptr};
	vk::DeviceAddress indexAddress{};
	vk::DeviceSize indexSize{};

public:
	explicit Mesh(const vma::raii::Allocator& allocator);
	explicit Mesh(nullptr_t);
	[[nodiscard]] auto GetVertexAddress() const { return vertexAddress; }
	[[nodiscard]] auto GetIndexAddress() const { return indexAddress; }
	void Draw(const vk::raii::CommandBuffer& command_buffer) const;
};

Mesh::Mesh(const vma::raii::Allocator& allocator)
{
	const auto& device = allocator.getDevice();

	constexpr Vertex vertices[36] = {
		{{1, -1, -1}, {1, 0, 0}, {0, 1}}, {{1, -1, 1}, {1, 0, 0}, {1, 1}}, {{1, 1, 1}, {1, 0, 0}, {1, 0}},
		{{1, -1, -1}, {1, 0, 0}, {0, 1}}, {{1, 1, 1}, {1, 0, 0}, {1, 0}}, {{1, 1, -1}, {1, 0, 0}, {0, 0}}, // +X
		{{-1, -1, 1}, {-1, 0, 0}, {0, 1}}, {{-1, -1, -1}, {-1, 0, 0}, {1, 1}}, {{-1, 1, -1}, {-1, 0, 0}, {1, 0}},
		{{-1, -1, 1}, {-1, 0, 0}, {0, 1}}, {{-1, 1, -1}, {-1, 0, 0}, {1, 0}}, {{-1, 1, 1}, {-1, 0, 0}, {0, 0}}, // -X
		{{-1, 1, -1}, {0, 1, 0}, {0, 1}}, {{1, 1, -1}, {0, 1, 0}, {1, 1}}, {{1, 1, 1}, {0, 1, 0}, {1, 0}},
		{{-1, 1, -1}, {0, 1, 0}, {0, 1}}, {{1, 1, 1}, {0, 1, 0}, {1, 0}}, {{-1, 1, 1}, {0, 1, 0}, {0, 0}}, // +Y
		{{-1, -1, 1}, {0, -1, 0}, {0, 1}}, {{1, -1, 1}, {0, -1, 0}, {1, 1}}, {{1, -1, -1}, {0, -1, 0}, {1, 0}},
		{{-1, -1, 1}, {0, -1, 0}, {0, 1}}, {{1, -1, -1}, {0, -1, 0}, {1, 0}}, {{-1, -1, -1}, {0, -1, 0}, {0, 0}}, // -Y
		{{-1, -1, 1}, {0, 0, 1}, {0, 1}}, {{1, -1, 1}, {0, 0, 1}, {1, 1}}, {{1, 1, 1}, {0, 0, 1}, {1, 0}},
		{{-1, -1, 1}, {0, 0, 1}, {0, 1}}, {{1, 1, 1}, {0, 0, 1}, {1, 0}}, {{-1, 1, 1}, {0, 0, 1}, {0, 0}}, // +Z
		{{1, -1, -1}, {0, 0, -1}, {0, 1}}, {{-1, -1, -1}, {0, 0, -1}, {1, 1}}, {{-1, 1, -1}, {0, 0, -1}, {1, 0}},
		{{1, -1, -1}, {0, 0, -1}, {0, 1}}, {{-1, 1, -1}, {0, 0, -1}, {1, 0}}, {{1, 1, -1}, {0, 0, -1}, {0, 0}}, // -Z
	};


	constexpr uint32_t indices[36] = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
		12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
		24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35,
	};

	constexpr auto ai = vma::AllocationCreateInfo{}
	                    .setUsage(vma::MemoryUsage::eAuto)
	                    .setFlags(
		                    vma::AllocationCreateFlagBits::eHostAccessSequentialWrite |
		                    vma::AllocationCreateFlagBits::eMapped
	                    );

	vertexSize = sizeof(vertices);
	vertexBuffer = allocator.createBuffer(
		vk::BufferCreateInfo{}
		.setSize(vertexSize)
		.setUsage(vk::BufferUsageFlagBits::eVertexBuffer |
			vk::BufferUsageFlagBits::eStorageBuffer |
			vk::BufferUsageFlagBits::eShaderDeviceAddress)
		.setSharingMode(vk::SharingMode::eExclusive),
		ai);
	vertexAddress = device.getBufferAddress(*vertexBuffer);
	vertexBuffer.getAllocation().copyFromMemory(vertices, 0, vertexSize);

	indexSize = sizeof(indices);
	indexBuffer = allocator.createBuffer(
		vk::BufferCreateInfo{}
		.setSize(indexSize)
		.setUsage(vk::BufferUsageFlagBits::eIndexBuffer |
			vk::BufferUsageFlagBits::eStorageBuffer |
			vk::BufferUsageFlagBits::eShaderDeviceAddress)
		.setSharingMode(vk::SharingMode::eExclusive),
		ai);
	indexAddress = device.getBufferAddress(*indexBuffer);
	indexBuffer.getAllocation().copyFromMemory(indices, 0, indexSize);
}

Mesh::Mesh(nullptr_t)
{
}

void Mesh::Draw(const vk::raii::CommandBuffer& command_buffer) const
{
	command_buffer.draw(vertexSize / sizeof(Vertex), 1, 0, 0);
}
