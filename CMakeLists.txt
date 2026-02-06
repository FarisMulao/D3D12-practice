cmake_minimum_required(VERSION 3.20)
project(D3D12-practice VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Source files
set(SOURCES
    ${CMAKE_SOURCE_DIR}/src/main.cpp
    ${CMAKE_SOURCE_DIR}/src/D3D12App.cpp
    ${CMAKE_SOURCE_DIR}/src/D3DRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/Win32Window.cpp
    ${CMAKE_SOURCE_DIR}/src/ImGuiManager.cpp
)

# Header files (for IDE)
file(GLOB_RECURSE HEADERS
    "${CMAKE_SOURCE_DIR}/include/*.h"
    "${CMAKE_SOURCE_DIR}/include/*.hpp"
)

# ImGui sources
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/external/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx12.cpp
)

# Create executable
add_executable(${PROJECT_NAME} ${SOURCES} ${IMGUI_SOURCES})

# Include ImGui directories
target_include_directories(${PROJECT_NAME} PRIVATE 
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

# Copy shader files to output directory
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/shaders
    $<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders
)

# Windows-specific settings
if(WIN32)
    # Set Windows SDK version (use latest available)
    set(CMAKE_SYSTEM_VERSION 10.0)
    
    # DirectX 12 libraries
    target_link_libraries(${PROJECT_NAME} PRIVATE
        d3d12.lib
        dxgi.lib
        d3dcompiler.lib
        dxguid.lib
    )
    
    # Windows-specific compiler options
    target_compile_options(${PROJECT_NAME} PRIVATE
        /W4
        /WX-
        /permissive-
    )
    
    # Set subsystem to Windows for a graphical application
    set_target_properties(${PROJECT_NAME} PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()

# Debug/Release configurations
if(CMAKE_CONFIGURATION_TYPES OR CMAKE_BUILD_TYPE)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MDd")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MD /O2")
endif()
