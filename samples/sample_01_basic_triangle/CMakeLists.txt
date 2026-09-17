include(../../cmake/compile_hlsl.cmake)

# Add WIN32 flag to build an executable with a WinMain entry point on windows.
# Note however that this removes the console that is usually attached when
# WIN32 is not specified (i.e., just a GUI without a console).
add_executable(sample_01_basic_window WIN32
  Main.cpp
  D3D12Window.cpp
)

target_include_directories(sample_01_basic_window PRIVATE
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/external/DirectX-Headers/include/directx
)
target_link_libraries(sample_01_basic_window PRIVATE dxgi d3d12)

# Compile HLSL Vertex and Pixel shaders
add_hlsl_shader_compilation_target(HLSL_COMPILED_SHADERS
  VSMAIN PSMAIN
  SHADER_PATH ${CMAKE_CURRENT_SOURCE_DIR}/shaders.hlsl
  OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>
  DXC_PATH ${DXC_EXECUTABLE_PATH})
add_dependencies(sample_01_basic_window HLSL_COMPILED_SHADERS)
