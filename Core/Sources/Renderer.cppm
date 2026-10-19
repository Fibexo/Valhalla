module;

#include <atomic>
#include <vector>
#include <chrono>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/ext.hpp>

#include <vulkan-memory-allocator-hpp/vk_mem_alloc_raii.hpp>

#include <vulkan/vulkan_raii.hpp>
#include <imgui_impl_vulkan.h>

export module Renderer;

import Logger;
import PushStructures;
import Abstractions.Mesh;
import Abstractions.Device;
import Shaders;
import Abstractions.Instance;

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
	vk::raii::CommandBuffers commandBuffers{nullptr};
	vk::raii::Semaphore imageAvailable{nullptr};
	vk::raii::Semaphore renderTimeline{nullptr};
	std::vector<vk::raii::Semaphore> renderFinished;
	uint64_t submitted{0};

	vk::raii::PipelineLayout pipelineLayout{nullptr};
	vk::raii::Pipeline pipeline{nullptr};

	vk::raii::DescriptorPool imGuiDescriptorPool{nullptr};

	constexpr static size_t maxFrameInFlight = 2;
	size_t frame = 0;
	[[nodiscard]] auto currentSlot() const { return frame % maxFrameInFlight; }

	vma::raii::Allocator allocator{nullptr};
	std::array<vma::raii::Buffer, maxFrameInFlight> buffers{nullptr, nullptr};
	std::array<RenderData*, maxFrameInFlight> mappedMemory{};
	std::array<vk::DeviceAddress, maxFrameInFlight> bufferAddress{};

	vma::raii::Image depthImage{nullptr};
	vk::raii::ImageView depthView{nullptr};

	Mesh mesh{nullptr};

	vma::raii::Image textureImage{nullptr};
	vk::raii::ImageView textureView{nullptr};
	vk::raii::Sampler textureSampler{nullptr};
	static constexpr uint32_t maxTextures = 1024;
	vk::raii::DescriptorSetLayout textureSetLayout{nullptr};
	vk::raii::DescriptorPool texturePool{nullptr};
	vk::raii::DescriptorSet textureSet{nullptr};

	float cameraFov{45.0f};
	float cameraDistance{5.0f};

	float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
	float lightColor[3] = {0.2f, 0.35f, 0.4f};
	float lightDirection[3] = {0.2f, 0.35f, 0.4f};
	RenderData data{};

public:
	explicit Renderer(SurfaceSource& surfaceSource) : surfaceSource(surfaceSource)
	{
		CreateInstance();
		CreateSurface();
		InitializeDevice();
		CreateSwapchain();
		CreateAllocator();
		CreateBuffers();
		mesh = Mesh{allocator};
		CreateDepthBuffer();
		CreateTexture();
		CreateTextureSetLayout();
		CreateTextureDescriptors();
		CreatePipeline();

		Logger->Log("Renderer initializing.");
	}

	~Renderer()
	{
		device.waitIdle();
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
	void CreateDepthBuffer();
	void CreateTexture();
	void CreateTextureSetLayout();
	void CreateTextureDescriptors();
	void CreatePipeline();


	void RenderGUI();
	void Process();
};

void Renderer::Render()
{
	if (static_cast<vk::Device>(device) == nullptr) { return; }
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
	constexpr auto depth_range = vk::ImageSubresourceRange{}
	                             .setAspectMask(vk::ImageAspectFlagBits::eDepth)
	                             .setBaseMipLevel(0).setLevelCount(1)
	                             .setBaseArrayLayer(0).setLayerCount(1);

	const auto depth_barrier = vk::ImageMemoryBarrier2{}
	                           .setSrcStageMask(vk::PipelineStageFlagBits2::eEarlyFragmentTests)
	                           .setDstStageMask(vk::PipelineStageFlagBits2::eEarlyFragmentTests)
	                           .setSrcAccessMask(vk::AccessFlagBits2::eDepthStencilAttachmentWrite)
	                           .setDstAccessMask(vk::AccessFlagBits2::eDepthStencilAttachmentWrite)
	                           .setOldLayout(vk::ImageLayout::eUndefined)
	                           .setNewLayout(vk::ImageLayout::eDepthAttachmentOptimal)
	                           .setImage(*depthImage)
	                           .setSubresourceRange(depth_range);

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
	auto barriers = {begin_barrier, depth_barrier};
	const auto begin_di = vk::DependencyInfo{}.setImageMemoryBarriers(barriers);
	const auto color_attachment = vk::RenderingAttachmentInfo{}
	                              .setImageView(views[image_index])
	                              .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
	                              .setLoadOp(vk::AttachmentLoadOp::eClear)
	                              .setStoreOp(vk::AttachmentStoreOp::eStore)
	                              .setClearValue(vk::ClearValue{
		                              vk::ClearColorValue{0.0f, 0.0f, 0.0f, 1.0f}
	                              });
	const auto depth_attachment = vk::RenderingAttachmentInfo{}
	                              .setImageView(*depthView)
	                              .setImageLayout(vk::ImageLayout::eDepthAttachmentOptimal)
	                              .setLoadOp(vk::AttachmentLoadOp::eClear)
	                              .setStoreOp(vk::AttachmentStoreOp::eStore)
	                              .setClearValue(vk::ClearValue{vk::ClearDepthStencilValue{1.0f, 0}}); // 1.0 = 最远

	const auto ri = vk::RenderingInfo{}
	                .setRenderArea(vk::Rect2D{{0, 0}, extent})
	                .setColorAttachments(color_attachment)
	                .setPDepthAttachment(&depth_attachment)
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
	command.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineLayout, 0, *textureSet, {});
	command.pushConstants<PushConstants>(*pipelineLayout, vk::ShaderStageFlagBits::eVertex |
	                                     vk::ShaderStageFlagBits::eFragment, 0,
	                                     PushConstants{
		                                     .data_address = (bufferAddress[currentSlot()]),
		                                     .vertices_address = (mesh.GetVertexAddress()),
		                                     .indices_address = (mesh.GetIndexAddress())
	                                     });
	command.setViewport(0, vp);
	command.setScissor(0, vk::Rect2D{{0, 0}, extent});

	mesh.Draw(command);

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
	if (present_result == vk::Result::eErrorOutOfDateKHR /*||
		present_result == vk::Result::eSuboptimalKHR*/)
	{
		ResetSwapchain();
		return;
	}

	layouts[image_index] = vk::ImageLayout::ePresentSrcKHR;
	frame++;
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
		                               .setDepthAttachmentFormat(vk::Format::eD32Sfloat)
	};
	ImGui_ImplVulkan_Init(&info);
}

void Renderer::ShutdownImGui()
{
	device.waitIdle();
}

void Renderer::ResetSwapchain()
{
	static std::atomic_flag resetting{};
	if (resetting.test_and_set()) { return; } // 返回 true = 已有调用在重置中 → 这次退出
	device.waitIdle();

	depthView = nullptr;
	depthImage = nullptr;

	swapchain = nullptr;
	CreateSwapchain();
	CreateDepthBuffer();
	resetting.clear();
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
	instance = context.createInstance(
		vk::InstanceCreateInfo{}
		.setPApplicationInfo(&ai)
		.setPEnabledExtensionNames(surfaceSource.RequiredInstanceExtensions())
		.setPEnabledLayerNames(layers)
	);
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


	constexpr auto priorities = 1.0f;
	constexpr auto extensions = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
	constexpr auto features = vk::PhysicalDeviceFeatures{}.setShaderInt64(true);

	device = {
		physicalDevice,
		vk::StructureChain{
			vk::DeviceCreateInfo{}
			.setQueueCreateInfos(
				vk::DeviceQueueCreateInfo{}
				.setQueueFamilyIndex(queueFamily)
				.setQueuePriorities(priorities)
			)
			.setPEnabledExtensionNames(extensions)
			.setPEnabledFeatures(&features),
			vk::PhysicalDeviceVulkan11Features{}
			.setShaderDrawParameters(true),
			vk::PhysicalDeviceVulkan12Features{}
			.setTimelineSemaphore(true)
			.setBufferDeviceAddress(true)
			.setDescriptorIndexing(true)
			.setShaderSampledImageArrayNonUniformIndexing(true)
			.setDescriptorBindingSampledImageUpdateAfterBind(true)
			.setDescriptorBindingPartiallyBound(true)
			.setDescriptorBindingVariableDescriptorCount(true)
			.setRuntimeDescriptorArray(true)
			.setScalarBlockLayout(true),
			vk::PhysicalDeviceVulkan13Features{}
			.setDynamicRendering(true)
			.setSynchronization2(true)
		}.get()
	};
	queue = {device, queueFamily, 0};

	commandPool = device.createCommandPool(
		vk::CommandPoolCreateInfo{}
		.setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer)
		.setQueueFamilyIndex(queueFamily)
	);

	commandBuffers = device.allocateCommandBuffers(
		vk::CommandBufferAllocateInfo{}
		.setCommandPool(*commandPool)
		.setLevel(vk::CommandBufferLevel::ePrimary)
		.setCommandBufferCount(maxFrameInFlight)
	);

	imageAvailable = device.createSemaphore({});
	renderTimeline = device.createSemaphore(
		vk::SemaphoreCreateInfo{}.setPNext(
			vk::SemaphoreTypeCreateInfo{}
			.setSemaphoreType(vk::SemaphoreType::eTimeline)
			.setInitialValue(0))
	);
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

	swapchain = device.createSwapchainKHR(
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
	);

	images = swapchain.getImages();
	layouts.assign(images.size(), vk::ImageLayout::eUndefined);

	views.clear();
	renderFinished.clear();
	for (const auto& image : images)
	{
		views.emplace_back(device.createImageView(
				vk::ImageViewCreateInfo{}
				.setImage(image)
				.setViewType(vk::ImageViewType::e2D)
				.setFormat(format)
				.setSubresourceRange(
					vk::ImageSubresourceRange{}
					.setAspectMask(vk::ImageAspectFlagBits::eColor)
					.setBaseMipLevel(0)
					.setLevelCount(1)
					.setBaseArrayLayer(0)
					.setLayerCount(1)))
		);
		renderFinished.emplace_back(device.createSemaphore({}));
	}
}

void Renderer::CreateAllocator()
{
	// 结构体本身有链式 setter（vk_mem_alloc_structs.hpp 里 115 个），照 vulkan.hpp 风格写
	// 注意：instance / device / pVulkanFunctions 不能设 —— createAllocator 会自己填，
	//       设了会命中它的校验并抛 eErrorValidationFailed
	allocator = vma::raii::createAllocator(
		instance, device,
		vma::AllocatorCreateInfo{}
		.setFlags(vma::AllocatorCreateFlagBits::eBufferDeviceAddress)
		.setPhysicalDevice(*physicalDevice)
		.setVulkanApiVersion(VK_API_VERSION_1_4)
	);
}

void Renderer::CreateBuffers()
{
	for (size_t i = 0; i < maxFrameInFlight; ++i)
	{
		buffers[i] = allocator.createBuffer(
			vk::BufferCreateInfo{}
			.setSize(sizeof(RenderData))
			.setUsage(vk::BufferUsageFlagBits::eUniformBuffer |
				vk::BufferUsageFlagBits::eShaderDeviceAddress)
			.setSharingMode(vk::SharingMode::eExclusive),
			vma::AllocationCreateInfo{}
			.setUsage(vma::MemoryUsage::eAuto)
			.setFlags(vma::AllocationCreateFlagBits::eHostAccessSequentialWrite |
				vma::AllocationCreateFlagBits::eMapped));

		mappedMemory[i] = static_cast<RenderData*>(buffers[i].getAllocation().getInfo().pMappedData);
		bufferAddress[i] = device.getBufferAddress(vk::BufferDeviceAddressInfo{}.setBuffer(*buffers[i]));
	}
}

void Renderer::CreateDepthBuffer()
{
	constexpr auto range = vk::ImageSubresourceRange{}
	                       .setAspectMask(vk::ImageAspectFlagBits::eDepth)
	                       .setBaseMipLevel(0).setLevelCount(1)
	                       .setBaseArrayLayer(0).setLayerCount(1);

	constexpr auto ai = vma::AllocationCreateInfo{}.setUsage(vma::MemoryUsage::eAutoPreferDevice);

	depthImage = allocator.createImage(
		vk::ImageCreateInfo{}
		.setImageType(vk::ImageType::e2D)
		.setFormat(vk::Format::eD32Sfloat) // 只要深度，不要 stencil
		.setExtent({extent.width, extent.height, 1}) // ★ 与 swapchain 同尺寸
		.setMipLevels(1).setArrayLayers(1)
		.setSamples(vk::SampleCountFlagBits::e1)
		.setTiling(vk::ImageTiling::eOptimal)
		.setUsage(vk::ImageUsageFlagBits::eDepthStencilAttachment)
		.setSharingMode(vk::SharingMode::eExclusive)
		.setInitialLayout(vk::ImageLayout::eUndefined),
		ai);

	depthView = device.createImageView(
		vk::ImageViewCreateInfo{}
		.setImage(*depthImage)
		.setViewType(vk::ImageViewType::e2D)
		.setFormat(vk::Format::eD32Sfloat)
		.setSubresourceRange(range)
	);
}

void Renderer::CreateTexture()
{
	// 2x2 棋盘（RGBA8 逆向）
	constexpr uint32_t pixels[4] = {
		0xFF0000FF, 0xFF00FF00,
		0xFFFF0000, 0xFF00FFFF,
	};
	constexpr vk::DeviceSize imageSize = sizeof(pixels); // 16

	constexpr auto range = vk::ImageSubresourceRange{}
	                       .setAspectMask(vk::ImageAspectFlagBits::eColor)
	                       .setBaseMipLevel(0).setLevelCount(1)
	                       .setBaseArrayLayer(0).setLayerCount(1);

	// staging buffer：VMA 分配 + 持久映射
	vma::AllocationCreateInfo sai{};
	sai.setUsage(vma::MemoryUsage::eAuto);
	sai.setFlags(vma::AllocationCreateFlagBits::eHostAccessSequentialWrite |
		vma::AllocationCreateFlagBits::eMapped);

	vma::raii::Buffer staging = allocator.createBuffer(
		vk::BufferCreateInfo{}
		.setSize(imageSize)
		.setUsage(vk::BufferUsageFlagBits::eTransferSrc),
		sai);
	std::memcpy(staging.getAllocation().getInfo().pMappedData, pixels, imageSize);

	// GPU-only 图像
	vma::AllocationCreateInfo iai{};
	iai.setUsage(vma::MemoryUsage::eAutoPreferDevice);
	textureImage = allocator.createImage(
		vk::ImageCreateInfo{}
		.setImageType(vk::ImageType::e2D)
		.setFormat(vk::Format::eR8G8B8A8Unorm)
		.setExtent({2, 2, 1})
		.setMipLevels(1)
		.setArrayLayers(1)
		.setSamples(vk::SampleCountFlagBits::e1)
		.setTiling(vk::ImageTiling::eOptimal)
		.setUsage(vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled)
		.setSharingMode(vk::SharingMode::eExclusive)
		.setInitialLayout(vk::ImageLayout::eUndefined),
		iai);

	// 一次性命令缓冲：UNDEFINED → TRANSFER_DST → 拷贝 → SHADER_READ_ONLY
	vk::raii::CommandBuffer upload = std::move(device.allocateCommandBuffers(
		vk::CommandBufferAllocateInfo{}
		.setCommandPool(*commandPool)
		.setLevel(vk::CommandBufferLevel::ePrimary)
		.setCommandBufferCount(1)).front());
	upload.begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

	const auto toDst = vk::ImageMemoryBarrier2{}
	                   .setSrcStageMask(vk::PipelineStageFlagBits2::eTopOfPipe)
	                   .setDstStageMask(vk::PipelineStageFlagBits2::eTransfer)
	                   .setSrcAccessMask(vk::AccessFlagBits2::eNone)
	                   .setDstAccessMask(vk::AccessFlagBits2::eTransferWrite)
	                   .setOldLayout(vk::ImageLayout::eUndefined)
	                   .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
	                   .setImage(textureImage)
	                   .setSubresourceRange(range);
	upload.pipelineBarrier2(vk::DependencyInfo{}.setImageMemoryBarriers(toDst));

	constexpr auto region = vk::BufferImageCopy{}
	                        .setImageSubresource(vk::ImageSubresourceLayers{}
	                                             .setAspectMask(vk::ImageAspectFlagBits::eColor).setLayerCount(1))
	                        .setImageExtent({2, 2, 1});
	upload.copyBufferToImage(staging, textureImage,
	                         vk::ImageLayout::eTransferDstOptimal, region);

	const auto toShader = vk::ImageMemoryBarrier2{}
	                      .setSrcStageMask(vk::PipelineStageFlagBits2::eTransfer)
	                      .setDstStageMask(vk::PipelineStageFlagBits2::eFragmentShader)
	                      .setSrcAccessMask(vk::AccessFlagBits2::eTransferWrite)
	                      .setDstAccessMask(vk::AccessFlagBits2::eShaderRead)
	                      .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
	                      .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
	                      .setImage(textureImage)
	                      .setSubresourceRange(range);
	upload.pipelineBarrier2(vk::DependencyInfo{}.setImageMemoryBarriers(toShader));

	upload.end();
	queue.submit(vk::SubmitInfo{}.setCommandBuffers(*upload));
	queue.waitIdle();
	// staging 是 vma::raii::Buffer，离开作用域自动销毁

	// view + sampler
	textureView = device.createImageView(
		vk::ImageViewCreateInfo{}
		.setImage(textureImage)
		.setViewType(vk::ImageViewType::e2D)
		.setFormat(vk::Format::eR8G8B8A8Unorm)
		.setSubresourceRange(range)
	);

	textureSampler = device.createSampler(
		vk::SamplerCreateInfo{}
		.setMagFilter(vk::Filter::eNearest)
		.setMinFilter(vk::Filter::eNearest)
		.setAddressModeU(vk::SamplerAddressMode::eRepeat)
		.setAddressModeV(vk::SamplerAddressMode::eRepeat)
		.setAddressModeW(vk::SamplerAddressMode::eRepeat)
	);
}

void Renderer::CreateTextureSetLayout()
{
	constexpr auto binding = vk::DescriptorSetLayoutBinding{}
	                         .setBinding(0)
	                         .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
	                         .setDescriptorCount(maxTextures) // ★ 数组上限
	                         .setStageFlags(vk::ShaderStageFlagBits::eFragment);

	// 三个 flag 走 pNext（setBindingFlags 收 ArrayProxyNoTemporaries，所以要具名变量）
	constexpr vk::DescriptorBindingFlags bindingFlagsValue =
		vk::DescriptorBindingFlagBits::ePartiallyBound |
		vk::DescriptorBindingFlagBits::eUpdateAfterBind |
		vk::DescriptorBindingFlagBits::eVariableDescriptorCount;
	const auto bindingFlags = vk::DescriptorSetLayoutBindingFlagsCreateInfo{}
		.setBindingFlags(bindingFlagsValue);

	const auto info = vk::DescriptorSetLayoutCreateInfo{}
	                  .setPNext(&bindingFlags) // bindingFlags 同作用域，安全
	                  .setFlags(vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool) // ★ 必须
	                  .setBindings(binding);

	textureSetLayout = device.createDescriptorSetLayout(info);
	Logger->Log("Texture set layout created (bindless, {} max).", maxTextures);
}

void Renderer::CreateTextureDescriptors()
{
	constexpr uint32_t textureCount = 1; // 目前只有棋盘那一张

	// pool：必须带 UPDATE_AFTER_BIND，容量 >= 实际数量
	constexpr auto poolSize = vk::DescriptorPoolSize{}
	                          .setType(vk::DescriptorType::eCombinedImageSampler)
	                          .setDescriptorCount(maxTextures);

	texturePool = device.createDescriptorPool(
		vk::DescriptorPoolCreateInfo{}
		.setFlags(vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind |
			vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet)
		.setMaxSets(1)
		.setPoolSizes(poolSize)
	);

	// 用 variableDescriptorCount 声明"实际用几个槽"（setBindingFlags 那类 API 要具名变量）
	const auto countInfo = vk::DescriptorSetVariableDescriptorCountAllocateInfo{}
		.setDescriptorCounts(maxTextures);

	const auto allocInfo = vk::DescriptorSetAllocateInfo{}
	                       .setDescriptorPool(*texturePool)
	                       .setSetLayouts(*textureSetLayout)
	                       .setPNext(&countInfo); // countInfo 同作用域，安全

	textureSet = std::move(device.allocateDescriptorSets(allocInfo).front());

	// 把纹理写进数组的 [0]
	const auto imageInfo = vk::DescriptorImageInfo{}
	                       .setImageView(*textureView)
	                       .setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
	                       .setSampler(*textureSampler);

	const auto write = vk::WriteDescriptorSet{}
	                   .setDstSet(*textureSet)
	                   .setDstBinding(0)
	                   .setDstArrayElement(0)
	                   .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
	                   .setImageInfo(imageInfo);

	device.updateDescriptorSets(write, {});
	Logger->Log("Texture written to bindless slot 0.");
}

void Renderer::CreatePipeline()
{
	vk::raii::ShaderModule vert{device, vk::ShaderModuleCreateInfo{}.setCode(Shaders::GetShader("ShaderVertex"))};
	vk::raii::ShaderModule frag{device, vk::ShaderModuleCreateInfo{}.setCode(Shaders::GetShader("ShaderFragment"))};

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
	                     .setSize(sizeof(PushConstants));

	pipelineLayout = device.createPipelineLayout(
		vk::PipelineLayoutCreateInfo{}
		.setSetLayouts(*textureSetLayout) // ★ set 0 = 纹理数组（bindless）
		.setPushConstantRanges(pcr)
	);
	// 在 pcr 附近加
	constexpr auto dssci = vk::PipelineDepthStencilStateCreateInfo{}
	                       .setDepthTestEnable(true)
	                       .setDepthWriteEnable(true)
	                       .setDepthCompareOp(vk::CompareOp::eLess); // 近的赢


	vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> chain;

	chain.get<vk::GraphicsPipelineCreateInfo>().setStages(stages) // 值版：stageCount 由容器长度推导
	     .setPVertexInputState(&vin)
	     .setPDepthStencilState(&dssci)
	     .setPInputAssemblyState(&ia)
	     .setPViewportState(&vp)
	     .setPRasterizationState(&rs)
	     .setPMultisampleState(&ms)
	     .setPColorBlendState(&cb)
	     .setPDynamicState(&dsci)
	     .setLayout(*pipelineLayout);

	chain.get<vk::PipelineRenderingCreateInfo>()
	     .setColorAttachmentCount(1)
	     .setPColorAttachmentFormats(&format)
	     .setDepthAttachmentFormat(vk::Format::eD32Sfloat);

	const auto pc = device.createPipelineCache({});

	Logger->Log("pipeline cache = {}", static_cast<void*>(*pc)); // 看是否非 0
	pipeline = device.createGraphicsPipeline(pc, chain.get());
	Logger->Log("graphics pipeline created."); // 没打出来就说明崩在这行
}

void Renderer::RenderGUI()
{
	ImGui::ShowDemoWindow();
	ImGui::ColorEdit4("Color", color);
	ImGui::ColorEdit3("Light Color", lightColor);
	ImGui::SliderFloat3("Light Direction", lightDirection, -1.0f, 1.0f);

	ImGui::SeparatorText("Camera");
	ImGui::SliderFloat("Fov (deg)", &cameraFov, 10.0f, 120.0f);
	ImGui::SliderFloat("Distance", &cameraDistance, 0.5f, 20.0f);
}

void Renderer::Process()
{
	static const auto start = std::chrono::system_clock::now();

	const auto delta = (std::chrono::system_clock::now() - start).count() * 0.000001f;
	const auto angle = delta * glm::pi<float>() / 30;

	// model：旋转
	const auto model =
		glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 1.0f, 0.0f)) *
		glm::rotate(glm::mat4(1.0f), angle / 2, glm::vec3(1.0f, 0.0f, 0.0f));

	// view：相机沿 +Z 看原点，距离可调，up = +Y
	const auto view = glm::lookAt(glm::vec3(0.0f, 0.0f, cameraDistance),
	                              glm::vec3(0.0f),
	                              glm::vec3(0.0f, 1.0f, 0.0f));

	// projection：GLM_FORCE_DEPTH_ZERO_TO_ONE 已定义（z ∈ [0,1]）；
	// Vulkan 的 NDC 是 y 向下，所以翻转 proj 的 [1][1]
	const auto aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
	auto proj = glm::perspective(glm::radians(cameraFov), aspect, 0.1f, 100.0f);
	proj[1][1] *= -1.0f;

	data.m = model;
	data.v = view;
	data.p = proj;
	data.color = {color[0], color[1], color[2], color[3]};
	data.light_color = {lightColor[0], lightColor[1], lightColor[2]};
	data.light_dir = {lightDirection[0], lightDirection[1], lightDirection[2]};

	*mappedMemory[currentSlot()] = data;
}
