// ─────────────────────────────────────────────────────────────
//  Valhalla · 平台入口（SDL3 回调模型）
//    AppInit    ：SDL → 窗口 → ImGui 上下文/字体 → Renderer
//    AppEvent   ：ImGui 优先，其次窗口尺寸/旋转
//    AppIterate ：每帧 NewFrame → Render（场景 + UI + present）
//    AppQuit    ：ImGui 先于 device 收尾
// ─────────────────────────────────────────────────────────────
#include <cstdint>
#include <memory>

#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_vulkan.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include <vulkan/vulkan.hpp>

import LoggerImpl;
import Renderer;

namespace
{
	// 声明顺序即析构顺序：renderer 先于 loggerImpl，window 最后（SDL 自己收）
	struct AppState
	{
		SDL_Window*                window{};
		std::unique_ptr<Renderer>  renderer;
	} app;

	LoggerImpl loggerImpl;      // 静态对象，避免裸 new

	// Core 只认识这个抽象，不认识 SDL
	class : public SurfaceSource
	{
	public:
		vk::SurfaceKHR CreateSurface(const vk::Instance instance) override
		{
			VkSurfaceKHR surface = VK_NULL_HANDLE;
			if (!SDL_Vulkan_CreateSurface(app.window, instance, nullptr, &surface))
			{
				SDL_Log("SDL_Vulkan_CreateSurface failed: %s", SDL_GetError());
			}
			return {surface};
		}

		vk::ArrayProxyNoTemporaries<const char* const> RequiredInstanceExtensions() override
		{
			uint32_t count = 0;
			const auto extensions = SDL_Vulkan_GetInstanceExtensions(&count);
			if (!extensions)
			{
				SDL_Log("SDL_Vulkan_GetInstanceExtensions failed: %s", SDL_GetError());
				return {0u, nullptr};
			}
			return {count, extensions};
		}

		vk::Extent2D SurfaceSize() override
		{
			int width = 0, height = 0;
			if (!SDL_GetWindowSizeInPixels(app.window, &width, &height))
			{
				SDL_Log("SDL_GetWindowSizeInPixels failed: %s", SDL_GetError());
			}
			return {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
		}
	} surfaceSource;
}

// ── 初始化 ───────────────────────────────────────────────────
SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv)
{
	(void)argc;
	(void)argv;
	*appstate = &app;                     // 失败路径也可能走到 Quit，先挂上

	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		SDL_Log("SDL_Init failed: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}

	// Android 上必须在取实例扩展名之前让 SDL 加载 Vulkan loader；桌面无害
	if (!SDL_Vulkan_LoadLibrary(nullptr))
	{
		SDL_Log("SDL_Vulkan_LoadLibrary failed: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}

	app.window = SDL_CreateWindow("Valhalla", 1280, 720, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
	if (!app.window)
	{
		SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}

	Logger = &loggerImpl;

	// ① ImGui：上下文 → 字体/缩放 → SDL3 后端
	ImGui::CreateContext();
	ImGui::StyleColorsDark();

	const float uiScale = SDL_GetWindowDisplayScale(app.window);   // Android ≈ 3.0，桌面 = 1.0
	{
		ImFontConfig fontCfg;
		fontCfg.SizePixels = 13.0f * uiScale;     // 按 scale 重新光栅化，比 FontGlobalScale 清晰
		ImGui::GetIO().Fonts->AddFontDefault(&fontCfg);
		ImGui::GetStyle().ScaleAllSizes(uiScale);
	}
	ImGui_ImplSDL3_InitForVulkan(app.window);

	// ② Renderer：instance → surface → device → swapchain，并接上 ImGui 的 Vulkan 后端
	app.renderer = std::make_unique<Renderer>(surfaceSource);
	app.renderer->InitializeImGui();

	return SDL_APP_CONTINUE;
}

// ── 事件 ─────────────────────────────────────────────────────
SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
	// ImGui 优先：UI 吃掉的输入不再往下传（场景逻辑将来接在这里之后）
	ImGui_ImplSDL3_ProcessEvent(event);

	switch (event->type)
	{
	case SDL_EVENT_QUIT:
	case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
		return SDL_APP_SUCCESS;

	// 尺寸 / 像素尺寸 / 旋转 / 进出后台：surface 与 swapchain 一并作废，
	// 真正的重建落在下一次 Render()
	case SDL_EVENT_WINDOW_RESIZED:
	case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
	case SDL_EVENT_DISPLAY_ORIENTATION:
	case SDL_EVENT_WINDOW_MINIMIZED:
	case SDL_EVENT_WINDOW_RESTORED:
		if (app.renderer) { app.renderer->ResetSwapchain(); }
		break;

	default:
		break;
	}
	return SDL_APP_CONTINUE;
}

// ── 每帧 ─────────────────────────────────────────────────────
SDL_AppResult SDL_AppIterate(void* appstate)
{
	if (!app.renderer) { return SDL_APP_CONTINUE; }

	ImGui_ImplSDL3_NewFrame();
	ImGui_ImplVulkan_NewFrame();
	// 若 ImGui::NewFrame() 现在不在 Renderer::Render 里，就在这里补一行：
	// ImGui::NewFrame();

	// UI 构建放这里：ImGui::Begin("Debug"); ... ImGui::End();

	app.renderer->Render();        // 内部顺序：场景 → ImGui::Render + RenderDrawData → present

	return SDL_APP_CONTINUE;
}

// ── 收尾 ─────────────────────────────────────────────────────
void SDL_AppQuit(void* appstate, SDL_AppResult result)
{
	(void)appstate;
	(void)result;

	// 顺序：ShutdownImGui() 内部先 device.waitIdle()，之后才能安全地拆
	// ImGui 自持的 pipeline/buffer/sampler/descriptor set，最后才轮到 device 本身
	if (app.renderer)
	{
		app.renderer->ShutdownImGui();
		ImGui_ImplVulkan_Shutdown();
	}
	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext();

	app.renderer.reset();
}
