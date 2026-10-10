file(GLOB_RECURSE SHADERS CONFIGURE_DEPENDS Sources/*.slang)

set(SHADER_INCLUDES "")
set(SHADER_DECLS "")
set(SHADER_HEADERS "")

foreach (SHADER_SRC IN LISTS SHADERS)
    get_filename_component(shader_name "${SHADER_SRC}" NAME_WE)      # Foo.slang → Foo

    foreach (pair IN ITEMS "vsMain:Vertex" "fsMain:Fragment")
        string(REPLACE ":" ";" kv "${pair}")
        list(GET kv 0 entry)
        list(GET kv 1 suffix)

        set(sym "${shader_name}${suffix}")                                   # FooVertex / FooFragment
        set(out "${CMAKE_CURRENT_BINARY_DIR}/Generated/${shader_name}${suffix}.g.h")   # slangc 生成的定义

        message(STATUS "slangc ${shader_name}:${entry} → ${out}.h@${sym}")
        add_custom_command(OUTPUT "${out}"
                COMMAND "slangc" "${SHADER_SRC}"
                -target spirv -profile spirv_1_4
                -entry "${entry}"
                -matrix-layout-column-major
                -source-embed-style u32
                -source-embed-name "${sym}"
                -fvk-use-entrypoint-name
                -o "${out}"
                DEPENDS "${SHADER_SRC}"
                COMMENT "slangc ${shader_name}:${entry} → ${out}")
        LIST(APPEND SHADER_HEADERS "${out}")
        string(APPEND SHADER_INCLUDES "#include \"${shader_name}${suffix}.g.h\"\n")
    endforeach ()
endforeach ()

# 汇总头：使用侧只 include 这一个
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/Generated/Shaders.g.h" "${SHADER_INCLUDES}")


add_library(Shaders STATIC ${SHADER_HEADERS} ${CMAKE_CURRENT_BINARY_DIR}/Generated/Shaders.g.h Sources/Shaders.hpp Sources/Shaders.cpp)
target_include_directories(Shaders PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/Generated/"
        PUBLIC Sources)
