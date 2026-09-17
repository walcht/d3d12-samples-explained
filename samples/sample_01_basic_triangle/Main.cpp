#ifndef UNICODE
#define UNICODE
#include "D3D12Window.hpp"
#include <string>
#endif

#include <windows.h>
#include <iostream>
#include "CommonUtils.hpp"

int WINAPI wWinMain(HINSTANCE, HINSTANCE /* legacy param */, PWSTR,
                    int cmd_show) {
#if defined(_DEBUG)
  /*
   * Attach to the parent console because GUI apps on Windows do not have 
   * a console and any std::cout/std::cerr is printed to the void.
   */
  AttachConsole(ATTACH_PARENT_PROCESS);
  freopen("CONOUT$", "w", stdout);
  freopen("CONOUT$", "w", stderr);
  std::ios::sync_with_stdio();
#endif

  try {
    D3D12Window d3d12window{1280, 720, L"Sample 01: Basic Triangle"};
    d3d12window.Run(cmd_show);
  } catch (const std::exception& e) {
#if defined(_DEBUG)
    std::cerr << "some exception occurded with message: " << e.what() << std::endl;
    std::cout << "type anything to exit\n";
    WCHAR c;
    std::wcin >> c;
#endif
    return 1;
  }

  return 0;
}
