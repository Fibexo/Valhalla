find_package(VulkanHeaders CONFIG REQUIRED)
find_package(Vulkan REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(unofficial-vulkan-memory-allocator-hpp CONFIG REQUIRED)

file(TO_CMAKE_PATH "$ENV{VULKAN_SDK}" VULKAN_SDK_DIR)

file(GLOB_RECURSE MODULES CONFIGURE_DEPENDS Sources/*.cppm)
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS Sources/*.cpp)

add_library(Core STATIC)

target_sources(Core
        PUBLIC ${SOURCES}
        PUBLIC FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/Sources
        FILES ${MODULES})

target_link_libraries(Core PUBLIC Shaders
        Vulkan::Vulkan unofficial::VulkanMemoryAllocator-Hpp::VulkanMemoryAllocator-Hpp
        imgui::imgui)
target_include_directories(Core PUBLIC
        "${VULKAN_SDK_DIR}/Include"
        "${VULKAN_SDK_DIR}/Include/vma"
)
