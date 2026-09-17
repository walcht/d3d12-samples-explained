# Add WIN32 flag to build an executable with a WinMain entry point on windows.
add_executable(sample_00_basic_window WIN32
  Main.cpp
  D3D12Window.cpp
)

target_include_directories(sample_00_basic_window PRIVATE
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/external/DirectX-Headers/include/directx
)
target_link_libraries(sample_00_basic_window PRIVATE dxgi d3d12)
