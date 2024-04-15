cmake_minimum_required(VERSION 3.5.0)


set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake CACHE STRING "Vcpkg toolchain file")

project(noPlayer VERSION 0.0.0 LANGUAGES CXX)

find_package(glew CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

find_package(OpenColorIO CONFIG REQUIRED)
find_package(OpenImageIO CONFIG REQUIRED)

add_subdirectory(src)
