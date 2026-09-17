cmake_minimum_required(VERSION 3.31)

project(d3d12-samples LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)

# In case this fails you just have to set -DDXC_EXECUTABLE_PATH=<path-to-dxc>
set(DXC_EXECUTABLE_PATH  "" CACHE PATH "Path to HLSL compiler (dxc.exe). If already in PATH consider using the following PowerShell command: -DXC_EXECUTABLE_PATH=$((Get-Command dxc.exe).Path)")
if (NOT DXC_EXECUTABLE_PATH)
  message(FATAL_ERROR "Path to HLSL compiler (dxc.exe) has to be provided. Consider setting the following CMake variable: -DXC_EXECUTABLE_PATH=<path-to-dxc>")
endif()

# omfg ... CMake is such a pile of shit... EXISTS still fails because the
# executable is not readable/writable even though it exists... hollyshit...
# if (NOT EXISTS DXC_EXECUTABLE_PATH)
#   message(FATAL_ERROR "Invalid path to dxc.exe \"${DXC_EXECUTABLE_PATH}\"")
# endif()

# Print some useful stuff
message(STATUS "CXX_STANDARD=${CXX_STANDARD}")
message(STATUS "CMAKE_SYSTEM_VERSION=${CMAKE_SYSTEM_VERSION}")
message(STATUS "CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION=${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}")

file(GLOB sample_dirs "samples/**")
foreach(sample_dir ${sample_dirs})
  add_subdirectory(${sample_dir})
endforeach()
