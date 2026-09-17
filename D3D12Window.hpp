#pragma once

#include "Win32BaseWindow.hpp"
#include "stdafx.hpp"
#include <D3dx12.h>
#include <vector>

using Microsoft::WRL::ComPtr;

class D3D12Window : public Win32BaseWindow<D3D12Window> {
public:
  D3D12Window(UINT width, UINT height, std::wstring name);
  ~D3D12Window();

  int Run(int cmd_show);
  void OnInit();
  void OnUpdate();
  void OnRender();
  void OnDestroy();

  inline UINT GetWidth() const { return m_width; }
  inline UINT GetHeight() const { return m_height; }
  const WCHAR *GetTitle() const { return m_title.c_str(); }

  LRESULT HandleMessage(UINT msg, WPARAM wparam, LPARAM lparam) override;
  PCWSTR ClassName() const override { return L"Direct3D 12 Window Class"; }
  // Helper function for resolving the full path of assets.
  inline std::wstring GetAssetFullPath(LPCWSTR assetName) {
    return m_assets_path + assetName;
  }

  struct Vertex {
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT2 uv;
  };

private:
  static const UINT NBR_FRAMES_IN_FLIGHT = 2;
  static const UINT TEXTURE_WIDTH = 256;
  static const UINT TEXTURE_HEIGHT = 256;
  static const UINT TEXTURE_STRIDE = 4;

  // viewport dims
  UINT m_width;
  UINT m_height;
  float m_aspect_ratio;

  // window GUI stuff
  std::wstring m_title;

  // assets stuff
  std::wstring m_assets_path;

  // pipeline objects (just like Vulkan)
  CD3DX12_VIEWPORT m_viewport;
  CD3DX12_RECT m_scissor_rect;
  ComPtr<IDXGISwapChain3> m_swapchain;
  ComPtr<ID3D12Device> m_device;
  ComPtr<ID3D12Resource> m_render_targets[NBR_FRAMES_IN_FLIGHT];
  ComPtr<ID3D12CommandAllocator> m_cmd_allocator;
  ComPtr<ID3D12CommandQueue> m_cmd_queue;
  ComPtr<ID3D12RootSignature> m_root_signature;
  ComPtr<ID3D12DescriptorHeap> m_rtv_heap;
  ComPtr<ID3D12DescriptorHeap> m_srv_heap;
  ComPtr<ID3D12PipelineState> m_pipeline_state;
  ComPtr<ID3D12GraphicsCommandList> m_graphics_cmd_list;
  UINT m_rtv_descriptor_size;

  // resources
  ComPtr<ID3D12Resource> m_vertex_buffer;
  D3D12_VERTEX_BUFFER_VIEW m_vertex_buffer_view;
  ComPtr<ID3D12Resource> m_texture;

  // synchronization objects
  UINT m_frame_idx;
  HANDLE m_fence_event;
  ComPtr<ID3D12Fence> m_fence;
  UINT64 m_fence_value;

  // adapter stuff
  bool m_use_warp_device;

  void GetHardwareAdapter(_In_ IDXGIFactory1 *pFactory,
                          _Outptr_result_maybenull_ IDXGIAdapter1 **ppAdapter,
                          bool high_performance_adapter = false);
  void SetCustomWindowText(LPCWSTR text);
  void InitPipeline();
  void LoadAssets();
  std::vector<UINT8> GenerateTextureData();
  void PopulateGraphicsCmdList();
  void WaitForPreviousFrame();
};
