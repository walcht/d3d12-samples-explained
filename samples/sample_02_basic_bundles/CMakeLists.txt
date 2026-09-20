include(../../cmake/compile_hlsl.cmake)

# Add WIN32 flag to build an executable with a WinMain entry point on windows.
# Note however that this removes the console that is usually attached when
# WIN32 is not specified (i.e., just a GUI without a console).
set(EXECUTABLE_NAME "sample_02_basic_bundles")
add_executable(${EXECUTABLE_NAME} WIN32
  Main.cpp
  D3D12Window.cpp
)

target_include_directories(${EXECUTABLE_NAME} PRIVATE
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/external/DirectX-Headers/include/directx
)
target_link_libraries(${EXECUTABLE_NAME} PRIVATE dxgi d3d12)

# Compile HLSL Vertex and Pixel shaders
add_hlsl_shader_compilation_target(${EXECUTABLE_NAME}_SHADERS
  VSMAIN PSMAIN
  SHADER_PATH ${CMAKE_CURRENT_SOURCE_DIR}/shaders.hlsl
  OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>
  DXC_PATH ${DXC_EXECUTABLE_PATH})
add_dependencies(${EXECUTABLE_NAME} ${EXECUTABLE_NAME}_SHADERS)
