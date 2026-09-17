#pragma once

// we don't need rarely used stuff from Windows headers
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
// d3dx12

#include <string>
#include <wrl.h>
#include <shellapi.h>
