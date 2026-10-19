find_package(glm CONFIG REQUIRED)
find_program(SLANGC_EXECUTABLE slangc REQUIRED)

set(SHADER_PROFILE "spirv_1_6" CACHE STRING "传给 slangc -profile 的 SPIR-V 版本")

set(SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Sources")
set(SHADER_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/Generated")
file(MAKE_DIRECTORY "${SHADER_GENERATED_DIR}")

set(SHADER_ENTRIES vsMain fsMain)
set(SHADER_SUFFIXES Vertex Fragment)

set(SLANGC_FLAGS
        -target spirv
        -profile "${SHADER_PROFILE}"
        -matrix-layout-column-major
        -source-embed-style u32
        -fvk-use-entrypoint-name)

file(GLOB_RECURSE SHADER_SOURCES CONFIGURE_DEPENDS "${SHADER_SOURCE_DIR}/*.slang")

set(GENERATED_HEADERS "")
set(AGGREGATE_INCLUDES "")
set(SHADER_NAMES "")

foreach (SHADER_SOURCE IN LISTS SHADER_SOURCES)
    get_filename_component(shader_name "${SHADER_SOURCE}" NAME_WE)

    if (shader_name IN_LIST SHADER_NAMES)
        message(FATAL_ERROR "shader name duplicated：${SHADER_SOURCE}")
    endif ()
    list(APPEND SHADER_NAMES "${shader_name}")

    foreach (entry suffix IN ZIP_LISTS SHADER_ENTRIES SHADER_SUFFIXES)
        set(symbol "${shader_name}${suffix}")
        set(header_path "${SHADER_GENERATED_DIR}/${symbol}.g.h")

        add_custom_command(
                OUTPUT "${header_path}"
                COMMAND "${SLANGC_EXECUTABLE}" "${SHADER_SOURCE}" ${SLANGC_FLAGS}
                -entry "${entry}" -source-embed-name "${symbol}" -o "${header_path}"
                DEPENDS "${SHADER_SOURCE}"
                COMMENT "slangc ${shader_name}:${entry} → ${symbol}.g.h"
                VERBATIM)

        list(APPEND GENERATED_HEADERS "${header_path}")
        string(APPEND AGGREGATE_INCLUDES "#include \"${symbol}.g.h\"
        REGISTER_SHADER_FROM_EMBEDDED(${symbol}, ${entry}); \n")
    endforeach ()
endforeach ()

message(STATUS "Shaders: ${SHADER_NAMES} → ${SHADER_GENERATED_DIR} (profile ${SHADER_PROFILE})")

file(GENERATE OUTPUT "${SHADER_GENERATED_DIR}/Shaders.g.h" CONTENT "${AGGREGATE_INCLUDES}")
file(GLOB_RECURSE MODULES CONFIGURE_DEPENDS Sources/*.cppm)

add_library(Shaders STATIC
        ${GENERATED_HEADERS}
        "${SHADER_GENERATED_DIR}/Shaders.g.h")

target_sources(Shaders
        PUBLIC ${SOURCES}
        PUBLIC FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/Sources
        FILES ${MODULES})

target_include_directories(Shaders PUBLIC
        "${SHADER_GENERATED_DIR}"
        "${SHADER_SOURCE_DIR}")

target_link_libraries(Shaders PUBLIC glm::glm)
