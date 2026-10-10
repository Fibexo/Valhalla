module;

#include <atomic>
#include <cstdio>
#include <vector>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <chrono>
#include <glm/glm.hpp>
#include <glm/ext.hpp>

#include <vma/vk_mem_alloc.h>

#include <vulkan/vulkan_raii.hpp>
#include <imgui_impl_vulkan.h>

#include "Shaders.hpp"

export module Renderer;

import Logger;

export class SurfaceSource
{
public:
	virtual ~SurfaceSource() = default;
	virtual vk::ArrayProxyNoTemporaries<const char*const> RequiredInstanceExtensions() = 0;
	virtual vk::SurfaceKHR CreateSurface(vk::Instance instance) = 0;
	virtual vk::Extent2D SurfaceSize() = 0;
};

export class Renderer
{
	SurfaceSource& surfaceSource;
	vk::raii::Context context{};
	vk::raii::Instance instance{nullptr};
	vk::raii::SurfaceKHR surface{nullptr};
	vk::raii::PhysicalDevice physicalDevice{nullptr};
	vk::raii::Device device{nullptr};
	vk::raii::Queue queue{nullptr};
	uint32_t queueFamily{0};

	vk::raii::SwapchainKHR swapchain{nullptr};
	std::vector<vk::Image> images{};
	std::vector<vk::raii::ImageView> views{};
	std::vector<vk::ImageLayout> layouts{};
	vk::Format format{};
	vk::Extent2D extent{};

	vk::raii::CommandPool commandPool{nullptr};
	std::vector<vk::raii::CommandBuffer> commandBuffers{};
	vk::raii::Semaphore imageAvailable{nullptr}; // 二值：acquireNextImage 用
	vk::raii::Semaphore renderTimeline{nullptr}; // timeline：帧间同步
	std::vector<vk::raii::Semaphore> renderFinished; // 每图一个二值：presentKHR 用
	uint64_t submitted{0}; // timeline 已信号的值

	vk::raii::PipelineLayout pipelineLayout{nullptr};
	vk::raii::Pipeline pipeline{nullptr};

	vk::raii::DescriptorPool imGuiDescriptorPool{nullptr};

	constexpr static size_t maxFrameInFlight = 2;
	size_t frame = 0;
	[[nodiscard]] auto currentSlot() const { return frame % maxFrameInFlight; }


	struct RenderData
	{
		glm::mat4 mvp{};
		glm::vec4 color{};
	};

	struct PushedData
	{
		vk::DeviceAddress data;
	};

	VmaAllocator allocator{nullptr};
	std::array<VkBuffer, maxFrameInFlight> buffers{nullptr, nullptr};
	std::array<VmaAllocation, maxFrameInFlight> bufferMemory{nullptr, nullptr};
	std::array<RenderData*, maxFrameInFlight> mappedMemory{};
	std::array<vk::DeviceAddress, maxFrameInFlight> bufferAddress{};

	float Colors[4] = {1.0f, 0.0f, 1.0f, 1.0f};

public:
	explicit Renderer(SurfaceSource& surfaceSource) : surfaceSource(surfaceSource)
	{
		CreateInstance();
		CreateSurface();
		InitializeDevice();
		CreateSwapchain();
		CreateAllocator();
		CreateBuffers();
		CreatePipeline();

		Logger->Log("Renderer initializing.");
	}

	~Renderer()
	{
		device.waitIdle();

		for (size_t i = 0; i < maxFrameInFlight; ++i)
		{
			if (buffers[i] != VK_NULL_HANDLE)
			{
				vmaDestroyBuffer(allocator, buffers[i], bufferMemory[i]);
				buffers[i] = VK_NULL_HANDLE;
			}
		}
		if (allocator != VK_NULL_HANDLE) { vmaDestroyAllocator(allocator); }

		Logger->Log("Renderer shutting down.");
	}

	void Render();
	void InitializeImGui();
	void ShutdownImGui();
	void ResetSwapchain();

private:
	void CreateInstance();
	void CreateSurface();
	void InitializeDevice();
	void CreateSwapchain();
	void CreateAllocator();
	void CreateBuffers();
	void CreatePipeline();


	void RenderGUI();
	void Process();
};

void Renderer::RenderGUI()
{
	ImGui::ShowDemoWindow();
	ImGui::ColorEdit4("Color Edit", Colors);
	ImGui::SliderFloat4("Color", Colors, 0.0f, 1.0f);
}

void Renderer::Process()
{
	static const auto start = std::chrono::system_clock::now();

	auto& data = *mappedMemory[currentSlot()];
	const auto delta = (std::chrono::system_clock::now() - start).count() * 0.000001f;
	const auto angle = delta * glm::pi<float>() / 3;

	data.mvp = glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 0.0f, 1.0f));
	data.color = glm::vec4(Colors[0] / 2, Colors[1] / 2, Colors[2] / 2, Colors[3]);
}

void Renderer::Render()
{
	if (device == nullptr) { return; }
	if (surface == nullptr) { CreateSurface(); }
	if (swapchain == nullptr) { ResetSwapchain(); }

	if (submitted > 0)
	{
		(void)device.waitSemaphores(
			vk::SemaphoreWaitInfo{}
			.setSemaphores({*renderTimeline})
			.setValues({submitted}), std::numeric_limits<uint64_t>::max()
		);
	}
	const auto [result,image_index] = swapchain.acquireNextImage(
		std::numeric_limits<uint64_t>::max(),
		imageAvailable,
		nullptr
	);
	if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR)
	{
		ResetSwapchain();
		return;
	}

	const auto begin_barrier = vk::ImageMemoryBarrier2{}
	                           .setSrcStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)
	                           .setDstStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)
	                           .setSrcAccessMask(vk::AccessFlagBits2::eNone)
	                           .setDstAccessMask(vk::AccessFlagBits2::eColorAttachmentWrite)
	                           .setOldLayout(layouts[image_index])
	                           .setNewLayout(vk::ImageLayout::eColorAttachmentOptimal)
	                           .setImage(images[image_index])
	                           .setSubresourceRange(
		                           vk::ImageSubresourceRange{}
		                           .setAspectMask(vk::ImageAspectFlagBits::eColor)
		                           .setBaseMipLevel(0)
		                           .setLevelCount(1)
		                           .setBaseArrayLayer(0)
		                           .setLayerCount(1)
	                           );
	const auto begin_di = vk::DependencyInfo{}.setImageMemoryBarriers(begin_barrier);
	const auto color_attachment = vk::RenderingAttachmentInfo{}
	                              .setImageView(views[image_index])
	                              .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
	                              .setLoadOp(vk::AttachmentLoadOp::eClear)
	                              .setStoreOp(vk::AttachmentStoreOp::eStore)
	                              .setClearValue(vk::ClearValue{
		                              vk::ClearColorValue{Colors[0], Colors[1], Colors[2], Colors[3]}
	                              });
	const auto ri = vk::RenderingInfo{}
	                .setRenderArea(vk::Rect2D{{0, 0}, extent})
	                .setColorAttachments(color_attachment)
	                .setLayerCount(1);
	const auto end_barrier = vk::ImageMemoryBarrier2{}
	                         .setSrcStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)
	                         .setDstStageMask(vk::PipelineStageFlagBits2::eBottomOfPipe)
	                         .setSrcAccessMask(vk::AccessFlagBits2::eColorAttachmentWrite)
	                         .setDstAccessMask(vk::AccessFlagBits2::eNone)
	                         .setOldLayout(vk::ImageLayout::eColorAttachmentOptimal)
	                         .setNewLayout(vk::ImageLayout::ePresentSrcKHR)
	                         .setImage(images[image_index])
	                         .setSubresourceRange(
		                         vk::ImageSubresourceRange{}
		                         .setAspectMask(vk::ImageAspectFlagBits::eColor)
		                         .setBaseMipLevel(0)
		                         .setLevelCount(1)
		                         .setBaseArrayLayer(0)
		                         .setLayerCount(1)
	                         );
	const auto end_di = vk::DependencyInfo{}.setImageMemoryBarriers(end_barrier);
	const auto vp = vk::Viewport{
		0.f, 0.f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.f, 1.f
	};

	Process();

	const auto& command = commandBuffers[currentSlot()];
	command.reset();

	command.begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
	command.pipelineBarrier2(begin_di);
	command.beginRendering(ri);

	command.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
	command.pushConstants<PushedData>(*pipelineLayout, vk::ShaderStageFlagBits::eVertex |
	                                  vk::ShaderStageFlagBits::eFragment, 0,
	                                  PushedData{bufferAddress[currentSlot()]});
	command.setViewport(0, vp);
	command.setScissor(0, vk::Rect2D{{0, 0}, extent});

	command.draw(3, 1, 0, 0);


	ImGui::NewFrame();
	RenderGUI();
	ImGui::Render();
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), *command);


	command.endRendering();
	command.pipelineBarrier2(end_di);
	command.end();

	auto wait_image = vk::SemaphoreSubmitInfo{}
	                  .setSemaphore(imageAvailable)
	                  .setStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput);

	auto signal_render = vk::SemaphoreSubmitInfo{}
	                     .setSemaphore(renderFinished[image_index])
	                     .setStageMask(vk::PipelineStageFlagBits2::eAllCommands);

	auto signal_timeline = vk::SemaphoreSubmitInfo{}
	                       .setSemaphore(renderTimeline)
	                       .setValue(submitted + 1)
	                       .setStageMask(vk::PipelineStageFlagBits2::eAllCommands);

	auto cbsi = vk::CommandBufferSubmitInfo{}.setCommandBuffer(command);
	auto ssi = {signal_render, signal_timeline};
	auto si = vk::SubmitInfo2{}
	          .setWaitSemaphoreInfos(wait_image)
	          .setCommandBufferInfos(cbsi)
	          .setSignalSemaphoreInfos(ssi);
	queue.submit2(si);
	submitted++;

	auto pi = vk::PresentInfoKHR{}
	          .setWaitSemaphores(*renderFinished[image_index])
	          .setSwapchains({*swapchain})
	          .setImageIndices(image_index);

	const vk::Result present_result = queue.presentKHR(pi);
	if (present_result == vk::Result::eErrorOutOfDateKHR ||
		present_result == vk::Result::eSuboptimalKHR)
	{
		ResetSwapchain();
		return;
	}

	layouts[image_index] = vk::ImageLayout::ePresentSrcKHR;
	frame++;
}

void Renderer::CreateInstance()
{
	constexpr vk::ApplicationInfo ai{
		"Valhalla",
		VK_MAKE_VERSION(1, 0, 0),
		"No Engine",
		VK_MAKE_VERSION(1, 0, 0),
		VK_API_VERSION_1_4,
	};
	std::initializer_list<const char*> layers = {
#ifndef __ANDROID__
		"VK_LAYER_KHRONOS_validation"
#endif
	};
	instance = {
		context, vk::InstanceCreateInfo{}
		         .setPApplicationInfo(&ai)
		         .setPEnabledExtensionNames(surfaceSource.RequiredInstanceExtensions())
		         .setPEnabledLayerNames(layers)
	};
}

void Renderer::CreateSurface()
{
	surface = {instance, surfaceSource.CreateSurface(instance)};
}

void Renderer::InitializeDevice()
{
	for (auto&& candidate : instance.enumeratePhysicalDevices())
	{
		const auto& families = candidate.getQueueFamilyProperties();
		for (uint32_t i = 0; i < families.size(); ++i)
		{
			if (families[i].queueFlags & vk::QueueFlagBits::eGraphics &&
				candidate.getSurfaceSupportKHR(i, *surface))
			{
				physicalDevice = candidate;
				queueFamily = i;
				break;
			}
		}
		if (physicalDevice != nullptr) { break; }
	}


	auto priorities = {1.0f};
	auto extensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

	device = {
		physicalDevice,
		vk::StructureChain{
			vk::DeviceCreateInfo{}
			.setQueueCreateInfos(
				vk::DeviceQueueCreateInfo{}
				.setQueueFamilyIndex(queueFamily)
				.setQueuePriorities(priorities)
			)
			.setPEnabledExtensionNames(extensions),
			vk::PhysicalDeviceVulkan11Features{}
			.setShaderDrawParameters(true),
			vk::PhysicalDeviceVulkan12Features{}
			.setTimelineSemaphore(true)
			.setBufferDeviceAddress(true),
			vk::PhysicalDeviceVulkan13Features{}
			.setDynamicRendering(true)
			.setSynchronization2(true)
		}.get()
	};
	queue = {device, queueFamily, 0};

	commandPool = {
		device,
		vk::CommandPoolCreateInfo{}
		.setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer)
		.setQueueFamilyIndex(queueFamily)
	};

	commandBuffers = device.allocateCommandBuffers(
		vk::CommandBufferAllocateInfo{}
		.setCommandPool(*commandPool)
		.setLevel(vk::CommandBufferLevel::ePrimary)
		.setCommandBufferCount(maxFrameInFlight)
	);

	imageAvailable = {device, vk::SemaphoreCreateInfo{}};
	renderTimeline = {
		device, vk::SemaphoreCreateInfo{}.setPNext(
			vk::SemaphoreTypeCreateInfo{}
			.setSemaphoreType(vk::SemaphoreType::eTimeline)
			.setInitialValue(0)
		)
	};
}

void Renderer::CreateSwapchain()
{
	const auto& capabilities = physicalDevice.getSurfaceCapabilitiesKHR(surface);
	const auto& formats = physicalDevice.getSurfaceFormatsKHR(surface);

	extent = capabilities.currentExtent;
	if (extent.width == std::numeric_limits<uint32_t>::max())
	{
		extent = surfaceSource.SurfaceSize();
		extent.width = std::clamp(extent.width,
		                          capabilities.minImageExtent.width,
		                          capabilities.maxImageExtent.width);
		extent.height = std::clamp(extent.height,
		                           capabilities.minImageExtent.height,
		                           capabilities.maxImageExtent.height);
	}

	format = formats.front().format;
	auto color_space = formats.front().colorSpace;
	for (const auto& candidate : formats)
	{
		if (candidate.format == vk::Format::eB8G8R8A8Srgb ||
			candidate.format == vk::Format::eR8G8B8A8Srgb)
		{
			format = candidate.format;
			color_space = candidate.colorSpace;
			break;
		}
	}
	const auto image_count = (capabilities.maxImageCount > 0 &&
		                         capabilities.minImageCount + 1 > capabilities.maxImageCount)
		                         ? capabilities.maxImageCount
		                         : capabilities.minImageCount + 1;

	auto composite_alpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
	if (capabilities.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::eInherit)
	{
		composite_alpha = vk::CompositeAlphaFlagBitsKHR::eInherit;
	}
	else if (capabilities.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::ePreMultiplied)
	{
		composite_alpha = vk::CompositeAlphaFlagBitsKHR::ePreMultiplied;
	}

	swapchain = {
		device,
		vk::SwapchainCreateInfoKHR{}
		.setSurface(surface)
		.setMinImageCount(image_count)
		.setImageFormat(format)
		.setImageColorSpace(color_space)
		.setImageExtent(extent)
		.setImageArrayLayers(1)
		.setImageUsage(vk::ImageUsageFlagBits::eColorAttachment)
		.setPreTransform(vk::SurfaceTransformFlagBitsKHR::eIdentity)
		.setPresentMode(vk::PresentModeKHR::eFifo)
		.setClipped(true)
		.setCompositeAlpha(composite_alpha)
	};
	images = swapchain.getImages();
	layouts.assign(images.size(), vk::ImageLayout::eUndefined);

	views.clear();
	renderFinished.clear();
	for (const auto& image : images)
	{
		views.emplace_back(device, vk::ImageViewCreateInfo{}
		                           .setImage(image)
		                           .setViewType(vk::ImageViewType::e2D)
		                           .setFormat(format)
		                           .setSubresourceRange(vk::ImageSubresourceRange{}
		                                                .setAspectMask(vk::ImageAspectFlagBits::eColor)
		                                                .setBaseMipLevel(0)
		                                                .setLevelCount(1)
		                                                .setBaseArrayLayer(0)
		                                                .setLayerCount(1)
		                           )
		);
		renderFinished.emplace_back(device, vk::SemaphoreCreateInfo{});
	}
}

void Renderer::CreateAllocator()
{
	const auto ci = VmaAllocatorCreateInfo{
		.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
		.physicalDevice = *physicalDevice,
		.device = *device,
		.instance = *instance,
		.vulkanApiVersion = VK_API_VERSION_1_4
	};
	vmaCreateAllocator(&ci, &allocator);
}

void Renderer::CreatePipeline()
{
	vk::raii::ShaderModule vert{device, vk::ShaderModuleCreateInfo{}.setCode(Shaders::ShaderVertex)};
	vk::raii::ShaderModule frag{device, vk::ShaderModuleCreateInfo{}.setCode(Shaders::ShaderFragment)};

	const std::vector stages{
		vk::PipelineShaderStageCreateInfo{}
		.setStage(vk::ShaderStageFlagBits::eVertex)
		.setModule(*vert).setPName("vsMain"),
		vk::PipelineShaderStageCreateInfo{}
		.setStage(vk::ShaderStageFlagBits::eFragment).setModule(*frag).setPName("fsMain"),
	};

	constexpr auto vin = vk::PipelineVertexInputStateCreateInfo{}; // 无顶点输入
	constexpr auto ia = vk::PipelineInputAssemblyStateCreateInfo{}
		.setTopology(vk::PrimitiveTopology::eTriangleList);
	constexpr auto vp = vk::PipelineViewportStateCreateInfo{}
	                    .setViewportCount(1)
	                    .setScissorCount(1);
	constexpr auto rs = vk::PipelineRasterizationStateCreateInfo{}
	                    .setPolygonMode(vk::PolygonMode::eFill)
	                    .setCullMode(vk::CullModeFlagBits::eNone) // 绕序可能被翻，先别剔
	                    .setFrontFace(vk::FrontFace::eCounterClockwise)
	                    .setLineWidth(1.0f);
	constexpr auto ms = vk::PipelineMultisampleStateCreateInfo{}
		.setRasterizationSamples(vk::SampleCountFlagBits::e1); // 默认 0，必须写

	constexpr auto cba = vk::PipelineColorBlendAttachmentState{}
		.setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
			vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
	const auto cb = vk::PipelineColorBlendStateCreateInfo{}
		.setAttachments(cba); // 单个元素可直接传对象

	const std::vector<vk::DynamicState> ds{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
	const auto dsci = vk::PipelineDynamicStateCreateInfo{}
		.setDynamicStates(ds); // 值版：count 由容器长度推导
	constexpr auto pcr = vk::PushConstantRange{}
	                     .setStageFlags(vk::ShaderStageFlagBits::eVertex |
		                     vk::ShaderStageFlagBits::eFragment)
	                     .setOffset(0)
	                     .setSize(sizeof(vk::DeviceAddress)); // 8

	pipelineLayout = vk::raii::PipelineLayout{
		device, vk::PipelineLayoutCreateInfo{}
		.setPushConstantRanges(pcr)
	}; // 没有 setSetLayouts

	// 显式给出模板参数并默认构造，再用 get<>() 逐字段设置。
	// 不要用 vk::StructureChain{ A{...}, B{...} } 这种 CTAD + 花括号的形式 ——
	// 各成员都是临时量，内部 pNext 链指向随即失效的地址，get<>() 拿回来的是空壳，
	// 表现就是 stageCount=0 / pViewports=NULL / dynamicStateCount=0。
	vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> chain;

	auto& gp = chain.get<vk::GraphicsPipelineCreateInfo>();
	gp.setStages(stages) // 值版：stageCount 由容器长度推导
	  .setPVertexInputState(&vin)
	  .setPInputAssemblyState(&ia)
	  .setPViewportState(&vp)
	  .setPRasterizationState(&rs)
	  .setPMultisampleState(&ms)
	  .setPColorBlendState(&cb)
	  .setPDynamicState(&dsci)
	  .setLayout(*pipelineLayout);

	chain.get<vk::PipelineRenderingCreateInfo>()
	     .setColorAttachmentCount(1)
	     .setPColorAttachmentFormats(&format);

	pipeline = vk::raii::Pipeline{device, nullptr, chain.get<vk::GraphicsPipelineCreateInfo>()};
}

void Renderer::CreateBuffers()
{
	for (size_t i = 0; i < maxFrameInFlight; ++i)
	{
		constexpr vk::DeviceSize size = sizeof(RenderData);
		VkBufferCreateInfo bi{
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = size,
			.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE
		};

		VmaAllocationCreateInfo ai{
			.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
			VMA_ALLOCATION_CREATE_MAPPED_BIT,
			.usage = VMA_MEMORY_USAGE_AUTO
		};

		VmaAllocationInfo info{};
		vmaCreateBuffer(allocator, &bi, &ai, &buffers[i], &bufferMemory[i], &info);

		mappedMemory[i] = static_cast<RenderData*>(info.pMappedData);
		bufferAddress[i] = device.getBufferAddress(vk::BufferDeviceAddressInfo{}.setBuffer(buffers[i])); // ★ 取 64 位设备地址
	}
}

void Renderer::ResetSwapchain()
{
	static std::atomic_flag resetting{};
	if (resetting.test_and_set()) { return; } // 返回 true = 已有调用在重置中 → 这次退出

	device.waitIdle();
	swapchain = nullptr;
	CreateSwapchain();
	resetting.clear();
}

void Renderer::InitializeImGui()
{
	constexpr vk::DescriptorPoolSize sizes[]{
		{vk::DescriptorType::eSampler, 8},
		{vk::DescriptorType::eSampledImage, 8},
		{vk::DescriptorType::eUniformBuffer, 8},
	};
	imGuiDescriptorPool = vk::raii::DescriptorPool{
		device, vk::DescriptorPoolCreateInfo{}
		        .setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet)
		        .setMaxSets(8).setPoolSizes(sizes)
	};

	ImGui_ImplVulkan_InitInfo info{};
	info.Instance = *instance;
	info.PhysicalDevice = *physicalDevice;
	info.Device = *device;
	info.QueueFamily = queueFamily;
	info.Queue = *queue;
	info.DescriptorPool = *imGuiDescriptorPool;
	info.MinImageCount = 2;
	info.ImageCount = 2;
	info.UseDynamicRendering = true;
	info.PipelineInfoMain = {
		.PipelineRenderingCreateInfo = vk::PipelineRenderingCreateInfo{}
		.setColorAttachmentFormats({format})
	};
	ImGui_ImplVulkan_Init(&info);
}

void Renderer::ShutdownImGui()
{
	device.waitIdle();
}
