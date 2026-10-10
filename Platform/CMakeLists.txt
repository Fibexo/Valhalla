find_package(SDL3 CONFIG REQUIRED)

file(GLOB_RECURSE MODULES CONFIGURE_DEPENDS Sources/*.cppm)

if (CMAKE_SYSTEM_NAME STREQUAL "Android")
    # SDLActivity.java 按固定名字 "main" 加载原生库，所以产物必须是 libmain.so
    add_library(Valhalla SHARED)
    set_target_properties(Valhalla PROPERTIES OUTPUT_NAME main)
    target_link_libraries(Valhalla PRIVATE log)

else ()
    add_executable(Valhalla)
endif ()


target_sources(Valhalla PRIVATE Sources/main.cpp
        PUBLIC FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/Sources
        FILES ${MODULES}
)
target_link_libraries(Valhalla PRIVATE SDL3::SDL3 Core)
