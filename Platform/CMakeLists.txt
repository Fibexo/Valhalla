find_package(SDL3 CONFIG REQUIRED)

file(GLOB_RECURSE MODULES CONFIGURE_DEPENDS Sources/*.cppm)

if (CMAKE_SYSTEM_NAME STREQUAL "Android")
    add_library(Valhalla SHARED)
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
