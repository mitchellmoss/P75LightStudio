cmake_minimum_required(VERSION 3.24)
project(P75LightStudio VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF CACHE BOOL "This project does not use C++ modules" FORCE)
include(FetchContent)

# Keep the source portable: SDL owns the window/input/render layer, while HIDAPI
# owns platform-specific keyboard access. Dear ImGui supplies the C++ controls.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build dependencies statically" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(HIDAPI_WITH_TESTS OFF CACHE BOOL "" FORCE)
set(HIDAPI_BUILD_HIDTEST OFF CACHE BOOL "" FORCE)
set(HIDAPI_BUILD_PP_DATA_DUMP OFF CACHE BOOL "" FORCE)
if(WIN32 OR APPLE)
    set(HIDAPI_WITH_LIBUSB OFF CACHE BOOL "" FORCE)
endif()

FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.18
    GIT_SHALLOW TRUE
)
FetchContent_Declare(hidapi
    GIT_REPOSITORY https://github.com/libusb/hidapi.git
    GIT_TAG hidapi-0.15.0
    GIT_SHALLOW TRUE
)
FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9b.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(SDL3 hidapi imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR})
target_link_libraries(imgui PUBLIC SDL3::SDL3)

add_executable(P75LightStudio
    src/main.cpp
    src/p75_protocol.cpp
    src/p75_controller.cpp
)
target_include_directories(P75LightStudio PRIVATE src)
target_link_libraries(P75LightStudio PRIVATE SDL3::SDL3 imgui hidapi::hidapi)

if(MSVC)
    target_compile_options(P75LightStudio PRIVATE /W4 /permissive-)
else()
    target_compile_options(P75LightStudio PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(P75LightStudio PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

