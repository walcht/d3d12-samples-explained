cmake_minimum_required(VERSION 3.22)

project(d3d12window)

# Add WIN32 flag to build an executable with a WinMain entry point on windows.
add_executable(d3d12window WIN32
  Main.cpp
  D3D12Window.cpp
)
target_include_directories(d3d12window PRIVATE external/DirectX-Headers/include/directx)
target_link_libraries(d3d12window PRIVATE dxgi d3d12)

message(STATUS "CMAKE_SYSTEM_VERSION=${CMAKE_SYSTEM_VERSION}")
message(STATUS "CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION=${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}")
