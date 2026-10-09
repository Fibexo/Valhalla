cmake_minimum_required(VERSION 4.1.2)
project(Valhalla LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(Shaders)
add_subdirectory(Core)
add_subdirectory(Platform)
