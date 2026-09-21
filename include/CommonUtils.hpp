#pragma once
#include <exception>
#include <stdexcept>
#include <stdio.h>
#include <string>
#include <windows.h>
#include <wrl.h>
#include <iostream>
#include <format>

using Microsoft::WRL::ComPtr;

namespace Utils {

/*
 * If you are building with MinGW then most likely
 * `Microsoft::WRL::Wrappers::FileHandle` will cause `not declared/found`.
 * Just build our own RAII wrapper.
 */
class FileRAII {
public:
  explicit FileRAII(HANDLE handle) : m_handle{handle} {}
  ~FileRAII() {
    if (m_handle != nullptr && m_handle != INVALID_HANDLE_VALUE)
      CloseHandle(m_handle);
  }

  HANDLE Get() const { return m_handle; }

  FileRAII(const FileRAII &) = delete;
  FileRAII &operator=(const FileRAII &) = delete;

private:
  HANDLE m_handle;
};

inline void GetAssetsPath(_Out_writes_(pathSize) WCHAR *path, UINT pathSize) {
  if (path == nullptr)
    throw std::exception();

  DWORD size = GetModuleFileNameW(nullptr, path, pathSize);
  if (size == 0 || size == pathSize)
    throw std::exception();

  WCHAR *last_slash = wcsrchr(path, L'\\');
  // No need to check if last_slash + 1 < pathSize because a path ending with
  // a slash is not a valid path
  if (last_slash)
    *(last_slash + 1) = L'\0';
}

inline std::string HrToString(HRESULT hr) {
  char s_str[64] = {};
  sprintf_s(s_str, "HRESULT of 0x%08X", static_cast<UINT>(hr));
  return std::string(s_str);
}

class HrException : public std::runtime_error {
public:
  HrException(HRESULT hr) : std::runtime_error(HrToString(hr)), m_hr(hr) {}
  HRESULT Error() const { return m_hr; }

private:
  const HRESULT m_hr;
};

inline void ThrowIfFailed(HRESULT hr) {
  if (FAILED(hr)) {
    throw HrException(hr);
  }
}

inline HRESULT ReadDataFromFile(LPCWSTR filename, byte **data, UINT *size) {
  using namespace Microsoft::WRL;
  CREATEFILE2_EXTENDED_PARAMETERS extendedParams = {};
  extendedParams.dwSize = sizeof(CREATEFILE2_EXTENDED_PARAMETERS);
  extendedParams.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
  extendedParams.dwFileFlags = FILE_FLAG_SEQUENTIAL_SCAN;
  extendedParams.dwSecurityQosFlags = SECURITY_ANONYMOUS;
  extendedParams.lpSecurityAttributes = nullptr;
  extendedParams.hTemplateFile = nullptr;

  Wrappers::FileHandle file(CreateFile2(filename, GENERIC_READ, FILE_SHARE_READ,
                                        OPEN_EXISTING, &extendedParams));
  if (file.Get() == INVALID_HANDLE_VALUE) {
    throw std::runtime_error("could not read file data (CreateFile2 failed)");
  }

  FILE_STANDARD_INFO fileInfo = {};
  if (!GetFileInformationByHandleEx(file.Get(), FileStandardInfo, &fileInfo,
                                    sizeof(fileInfo))) {
    throw std::exception();
  }

  if (fileInfo.EndOfFile.HighPart != 0) {
    throw std::exception();
  }

  *data = reinterpret_cast<byte *>(malloc(fileInfo.EndOfFile.LowPart));
  *size = fileInfo.EndOfFile.LowPart;

  if (!ReadFile(file.Get(), *data, fileInfo.EndOfFile.LowPart, nullptr,
                nullptr)) {
    throw std::exception();
  }

  return S_OK;
}

inline std::vector<UINT8> GenerateCheckboardTexture(UINT width, UINT height, UINT cellsize = 32)
{
  const size_t stride = 4;
  std::vector<UINT8> data;
  data.resize(width * height * stride);
  for (size_t n = 0; n < width * height; ++n) {
    UINT x = n % width;
    UINT y = n / width;
    UINT i = x / cellsize;
    UINT j = y / cellsize;
    if (i % 2 == j % 2) {
      data[n * stride + 0] = 0x00;
      data[n * stride + 1] = 0x00;
      data[n * stride + 2] = 0x00;
      data[n * stride + 3] = 0xFF;
    } else {
      data[n * stride + 0] = 0xFF;
      data[n * stride + 1] = 0xFF;
      data[n * stride + 2] = 0xFF;
      data[n * stride + 3] = 0xFF;
    }
  }
  return data;
}

} // namespace Utils
