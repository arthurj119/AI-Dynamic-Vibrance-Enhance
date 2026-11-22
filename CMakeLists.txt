cmake_minimum_required(VERSION 3.20)
project(AIDynamicVibranceEnhance LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Ensure deterministic single output directory across Makefile and Visual Studio generators
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}")

add_executable(AIDynamicVibranceEnhance WIN32
    src/main.cpp
    src/profiles.cpp
    src/dx11_overlay.cpp
    src/sidebar_ui.cpp
    src/control_hub.cpp
    src/app.rc
)

target_include_directories(AIDynamicVibranceEnhance PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(AIDynamicVibranceEnhance PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

if(MSVC)
    target_compile_options(AIDynamicVibranceEnhance PRIVATE /W4 /utf-8 /EHsc)
endif()

target_link_libraries(AIDynamicVibranceEnhance PRIVATE
    d3d11
    dxgi
    d3dcompiler
    dcomp
    d2d1
    dwrite
    dwmapi
    user32
    gdi32
    shell32
    ole32
    comdlg32
    shlwapi
    psapi
    advapi32
    uuid
    runtimeobject
)

# Keep standalone shaders synced automatically on every build
set(EXPORTED_HLSL "${CMAKE_CURRENT_SOURCE_DIR}/shaders/DynamicVibrance.hlsl")
set(EXPORTED_FX   "${CMAKE_CURRENT_SOURCE_DIR}/shaders/AIDynamicVibranceEnhance.fx")

add_custom_command(
    OUTPUT "${EXPORTED_HLSL}" "${EXPORTED_FX}"
    COMMAND $<TARGET_FILE:AIDynamicVibranceEnhance> --export-shaders "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    DEPENDS AIDynamicVibranceEnhance "${CMAKE_CURRENT_SOURCE_DIR}/src/shader_source.h"
    COMMENT "Exporting standalone HLSL and ReShade shaders"
    VERBATIM
)

add_custom_target(ExportShaders DEPENDS "${EXPORTED_HLSL}" "${EXPORTED_FX}")

# Configure and build Windows Setup Installer embedding the portable binary & shaders
set(SETUP_ICON_PATH   "${CMAKE_CURRENT_SOURCE_DIR}/assets/app_icon.ico")
set(SETUP_EXE_PATH    "${CMAKE_BINARY_DIR}/AIDynamicVibranceEnhance.exe")
set(SETUP_HLSL_PATH   "${EXPORTED_HLSL}")
set(SETUP_FX_PATH     "${EXPORTED_FX}")
set(SETUP_README_PATH "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/installer/setup.rc.in"
    "${CMAKE_BINARY_DIR}/setup.rc"
    @ONLY
)

set_source_files_properties("${CMAKE_BINARY_DIR}/setup.rc" PROPERTIES
    OBJECT_DEPENDS "${SETUP_EXE_PATH};${EXPORTED_HLSL};${EXPORTED_FX};${SETUP_README_PATH}"
)

add_executable(AIDynamicVibranceEnhance_Setup WIN32
    installer/setup_main.cpp
    "${CMAKE_BINARY_DIR}/setup.rc"
)

add_dependencies(AIDynamicVibranceEnhance_Setup ExportShaders)

target_compile_definitions(AIDynamicVibranceEnhance_Setup PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

if(MSVC)
    target_compile_options(AIDynamicVibranceEnhance_Setup PRIVATE /W4 /utf-8 /EHsc)
endif()

target_link_libraries(AIDynamicVibranceEnhance_Setup PRIVATE
    d2d1
    dwrite
    dwmapi
    user32
    gdi32
    shell32
    ole32
    comdlg32
    shlwapi
    advapi32
    uuid
)
