#pragma once

#ifndef UNICODE
#define UNICODE
#endif

#include <windows.h>

template <class DERIVED_TYPE> class Win32BaseWindow {
public:
  static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam,
                                     LPARAM lparam) {
    DERIVED_TYPE *pThis = NULL;
    if (msg == WM_CREATE) {
      auto pCreate = reinterpret_cast<CREATESTRUCT *>(lparam);
      pThis = reinterpret_cast<DERIVED_TYPE *>(pCreate->lpCreateParams);
      SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
    } else {
      pThis = reinterpret_cast<DERIVED_TYPE *>(
          GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (!pThis)
      return DefWindowProcW(hwnd, msg, wparam, lparam);

    return pThis->HandleMessage(msg, wparam, lparam);
  }

  Win32BaseWindow() : m_hwnd{nullptr} {}

  inline BOOL Create(PCWSTR windowname, DWORD style, DWORD exstyle = 0,
                     int width = CW_USEDEFAULT, int height = CW_USEDEFAULT) {
    WNDCLASSW wc = {0};

    wc.lpfnWndProc = DERIVED_TYPE::WindowProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = ClassName();

    RegisterClassW(&wc);

    m_hwnd = CreateWindowExW(
        exstyle, ClassName(), windowname, style, CW_USEDEFAULT, CW_USEDEFAULT,
        width, height, nullptr /* no parent window */, nullptr /* no menu */,
        GetModuleHandle(NULL), this);

    return m_hwnd ? TRUE : FALSE;
  }

  inline HWND GetHWND() const { return m_hwnd; }

protected:
  virtual LRESULT HandleMessage(UINT msg, WPARAM wparam, LPARAM lparam) = 0;
  virtual PCWSTR ClassName() const = 0;
  HWND m_hwnd;
};
