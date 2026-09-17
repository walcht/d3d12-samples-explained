#ifndef UNICODE
#define UNICODE
#include "D3D12Window.hpp"
#include <string>
#endif

#include <windows.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE /* legacy param */, PWSTR,
                    int cmd_show) {
  D3D12Window d3d12window{1280, 720, L"Sample 00: Basic Window"};
  d3d12window.Run(cmd_show);
  return 0;
}
