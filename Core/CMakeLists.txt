find_package(VulkanHeaders CONFIG REQUIRED)
find_package(Vulkan REQUIRED)
find_package(imgui CONFIG REQUIRED)

file(GLOB_RECURSE MODULES CONFIGURE_DEPENDS src/*.cppm)

add_library(Core STATIC)

target_sources(Core
        PUBLIC FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src
        FILES ${MODULES})

target_link_libraries(Core PUBLIC Vulkan::Vulkan Vulkan::Headers imgui::imgui Shaders)
