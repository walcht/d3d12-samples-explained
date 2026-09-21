#include "D3D12Window.hpp"
#include "CommonUtils.hpp"
#include "Win32BaseWindow.hpp"
#include <iostream>

using namespace Utils;

D3D12Window::D3D12Window(UINT width, UINT height, std::wstring name)
    : Win32BaseWindow<D3D12Window>(), m_width(width), m_height(height),
      m_title(name), m_frame_idx(0), m_viewport(0.0f, 0.0f, static_cast<float>(width),
                 static_cast<float>(height)),
      m_scissor_rect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height)),
      m_rtv_descriptor_size(0) {
  WCHAR assets_path[512];
  GetAssetsPath(assets_path, _countof(assets_path));
  m_assets_path = assets_path;
  m_aspect_ratio = static_cast<float>(width) / static_cast<float>(height);
}

D3D12Window::~D3D12Window() {}

void D3D12Window::OnInit() {
  InitPipeline();
  LoadAssets();
}

void D3D12Window::InitPipeline() {
  UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
  {
    ComPtr<ID3D12Debug> spDebugController0;
    ComPtr<ID3D12Debug1> spDebugController1;
    ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&spDebugController0)));
    ThrowIfFailed(spDebugController0->QueryInterface(IID_PPV_ARGS(&spDebugController1)));
    spDebugController1->EnableDebugLayer();
    spDebugController1->SetEnableGPUBasedValidation(true);
    // Enable additional debug layers.
    dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
  }
#endif

  ComPtr<IDXGIFactory2> factory;
  ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

  ComPtr<IDXGIAdapter1> hardwareAdapter;
  GetHardwareAdapter(factory.Get(), &hardwareAdapter);
  ThrowIfFailed(D3D12CreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                  IID_PPV_ARGS(&m_device)));

  D3D12_COMMAND_QUEUE_DESC queueDesc = {};
  queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  ThrowIfFailed(
      m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_cmd_queue)));

  ComPtr<IDXGISwapChain1> swapChain;
  DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
  swapChainDesc.BufferCount = NBR_FRAMES_IN_FLIGHT;
  swapChainDesc.Width = m_width;
  swapChainDesc.Height = m_height;
  swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  swapChainDesc.SampleDesc.Count = 1;
  ThrowIfFailed(factory->CreateSwapChainForHwnd(
      m_cmd_queue.Get(), // Swap chain needs the queue so that it can force a
                         // flush on it.
      m_hwnd, &swapChainDesc, nullptr, nullptr, &swapChain));

  // This sample does not support fullscreen transitions.
  ThrowIfFailed(factory->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER));

  // Query the newer IDXGISwapChain3 interface from the IDXGISwapChain1 created
  // object
  ThrowIfFailed(swapChain.As(&m_swapchain));
  m_frame_idx = m_swapchain->GetCurrentBackBufferIndex();

  // Create the descriptor heaps
  {
    /* render target view (RTV) descriptor heap. */
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = NBR_FRAMES_IN_FLIGHT;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc,
                                                 IID_PPV_ARGS(&m_rtv_heap)));
    m_rtv_descriptor_size = m_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    /* shader resource view (SRV) for the texture */
    D3D12_DESCRIPTOR_HEAP_DESC srv_heap_desc = {};
    srv_heap_desc.NumDescriptors = 1;
    srv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&srv_heap_desc, IID_PPV_ARGS(&m_srv_heap)));
  }

  // Create a render target view
  {
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(
        m_rtv_heap->GetCPUDescriptorHandleForHeapStart());

    // Create a RTV for each frame.
    for (UINT n = 0; n < NBR_FRAMES_IN_FLIGHT; n++) {
      ThrowIfFailed(
          m_swapchain->GetBuffer(n, IID_PPV_ARGS(&m_render_targets[n])));
      m_device->CreateRenderTargetView(m_render_targets[n].Get(), nullptr,
                                       rtvHandle);
      rtvHandle.Offset(1, m_rtv_descriptor_size);
    }
  }

  // Create the direct command list allocator
  ThrowIfFailed(m_device->CreateCommandAllocator(
      D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_cmd_allocator)));

  // Create the indirect (i.e., bundle) command list allocator
  ThrowIfFailed(m_device->CreateCommandAllocator(
      D3D12_COMMAND_LIST_TYPE_BUNDLE, IID_PPV_ARGS(&m_bundle_allocator)));
}

// Load the sample assets.
void D3D12Window::LoadAssets() {

  // Create the root signature.
  {
    D3D12_FEATURE_DATA_ROOT_SIGNATURE feature_data = {};
    feature_data.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
    if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &feature_data, sizeof(feature_data))))
        feature_data.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;

    CD3DX12_DESCRIPTOR_RANGE1 ranges[1];
    CD3DX12_ROOT_PARAMETER1 root_parameters[1];

    ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
    root_parameters[0].InitAsDescriptorTable(_countof(ranges), ranges, D3D12_SHADER_VISIBILITY_PIXEL);

    D3D12_STATIC_SAMPLER_DESC static_sampler = {};
    static_sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    static_sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    static_sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    static_sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    static_sampler.MipLODBias = 0;
    static_sampler.MaxAnisotropy = 0;
    static_sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    static_sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    static_sampler.MinLOD = 0.0f;
    static_sampler.MaxLOD = D3D12_FLOAT32_MAX;
    static_sampler.ShaderRegister = 0;
    static_sampler.RegisterSpace = 0;
    static_sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_FLAGS root_signature_flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc;
    root_signature_desc.Init_1_1(_countof(root_parameters), root_parameters, 1, &static_sampler, root_signature_flags);

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&root_signature_desc, feature_data.HighestVersion, &signature, &error));
    ThrowIfFailed(m_device->CreateRootSignature(
        0, signature->GetBufferPointer(), signature->GetBufferSize(),
        IID_PPV_ARGS(&m_root_signature)));
  }

  // Create the pipeline state object (PSO), which includes compiling and loading shaders.
  {
    UINT8 *pVertexShaderData = nullptr;
    UINT8 *pPixelShaderData = nullptr;
    UINT vertexShaderDataLength = 0;
    UINT pixelShaderDataLength = 0;

    ThrowIfFailed(
        ReadDataFromFile(GetAssetFullPath(L"shaders_VSMain.cso").c_str(),
                         &pVertexShaderData, &vertexShaderDataLength));
    ThrowIfFailed(
        ReadDataFromFile(GetAssetFullPath(L"shaders_PSMain.cso").c_str(),
                         &pPixelShaderData, &pixelShaderDataLength));

    // Define the vertex input layout.
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};

    // Describe and create the graphics pipeline state object (PSO).
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = {inputElementDescs, _countof(inputElementDescs)};
    psoDesc.pRootSignature = m_root_signature.Get();
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(pVertexShaderData, vertexShaderDataLength);
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(pPixelShaderData, pixelShaderDataLength);
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;
    ThrowIfFailed(m_device->CreateGraphicsPipelineState(
        &psoDesc, IID_PPV_ARGS(&m_pipeline_state)));
  }

  // Create the command list
  ThrowIfFailed(m_device->CreateCommandList(
      0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_cmd_allocator.Get(), m_pipeline_state.Get(),
      IID_PPV_ARGS(&m_graphics_cmd_list)));
  // Don't close it yet. We need to upload some texture resources first ...

  // Create the vertex buffer
  {
      Vertex triangle_vertices[] = {
            { { 0.0f, 0.25f * m_aspect_ratio, 0.0f }, { 0.5f, 0.0f } },
            { { 0.25f, -0.25f * m_aspect_ratio, 0.0f }, { 1.0f, 1.0f } },
            { { -0.25f, -0.25f * m_aspect_ratio, 0.0f }, { 0.0f, 1.0f } }
      };

      const auto heap_props = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
      const auto vertex_resource_desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(triangle_vertices));
      ThrowIfFailed(m_device->CreateCommittedResource(
        &heap_props,
        D3D12_HEAP_FLAG_NONE,
        &vertex_resource_desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_vertex_buffer)));

      UINT8* pvertex_data_begin;
      CD3DX12_RANGE read_range(0, 0);
      ThrowIfFailed(m_vertex_buffer->Map(0, &read_range, reinterpret_cast<void**>(&pvertex_data_begin)));
      memcpy(pvertex_data_begin, triangle_vertices, sizeof(triangle_vertices));
      m_vertex_buffer->Unmap(0, nullptr);

      m_vertex_buffer_view.BufferLocation = m_vertex_buffer->GetGPUVirtualAddress();
      m_vertex_buffer_view.StrideInBytes = sizeof(Vertex);  // how many bytes to jump to next entry
      m_vertex_buffer_view.SizeInBytes = sizeof(triangle_vertices);
  }

  ComPtr<ID3D12Resource> texture_upload_heap;

  {
    // Create the texture resource on the GPU
    D3D12_RESOURCE_DESC texture_desc = {};
    texture_desc.MipLevels = 1;
    texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture_desc.Width = TEXTURE_WIDTH;
    texture_desc.Height = TEXTURE_HEIGHT;
    texture_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    texture_desc.DepthOrArraySize = 1;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.SampleDesc.Quality = 0;
    texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    const auto heap_props = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(m_device->CreateCommittedResource(
          &heap_props,
          D3D12_HEAP_FLAG_NONE,
          &texture_desc,
          D3D12_RESOURCE_STATE_COPY_DEST,
          nullptr,
          IID_PPV_ARGS(&m_texture)));

    // Create the GPU texture upload buffer
    const UINT64 upload_buffer_size = GetRequiredIntermediateSize(m_texture.Get(), 0, 1);
    const auto upload_heap_props = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    const auto upload_heap_resource_desc = CD3DX12_RESOURCE_DESC::Buffer(upload_buffer_size);
    ThrowIfFailed(m_device->CreateCommittedResource(
          &upload_heap_props,
          D3D12_HEAP_FLAG_NONE,
          &upload_heap_resource_desc,
          D3D12_RESOURCE_STATE_GENERIC_READ,
          nullptr,
          IID_PPV_ARGS(&texture_upload_heap)));

    // Get the texture pixels
    std::vector<UINT8> texture = Utils::GenerateCheckboardTexture(TEXTURE_WIDTH, TEXTURE_HEIGHT);
    assert(upload_buffer_size == texture.size());

    D3D12_SUBRESOURCE_DATA texture_data = {};
    texture_data.pData = texture.data();
    texture_data.RowPitch = TEXTURE_WIDTH * TEXTURE_STRIDE;
    texture_data.SlicePitch = texture_data.RowPitch * TEXTURE_HEIGHT;

    // UpdateSubresources is a DirectX helper that populates the command list
    // with the needed command(s) to upload some texture data from the
    // CPU -> GPU texture upload heap -> GPU actual texture
    UpdateSubresources(m_graphics_cmd_list.Get(), m_texture.Get(),
        texture_upload_heap.Get(), 0, 0, 1, &texture_data);
    const auto resource_transition = CD3DX12_RESOURCE_BARRIER::Transition(m_texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
          D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_graphics_cmd_list->ResourceBarrier(1, &resource_transition);

    // Create the SRV for the texture
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Format = texture_desc.Format;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = 1;
    m_device->CreateShaderResourceView(m_texture.Get(), &srv_desc,
        m_srv_heap->GetCPUDescriptorHandleForHeapStart());
  }

  // Close the command list and execute it (yes, in this initialization method)
  // to upload the texture pixels to the GPU.
  ThrowIfFailed(m_graphics_cmd_list->Close());
  ID3D12CommandList* ppcmd_lists[] = { m_graphics_cmd_list.Get() };
  m_cmd_queue->ExecuteCommandLists(_countof(ppcmd_lists), ppcmd_lists);


  // Create the bundle command list and record bundle commands.
  {
    ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_BUNDLE,
          m_bundle_allocator.Get(), m_pipeline_state.Get(), IID_PPV_ARGS(&m_bundle)));
    m_bundle->SetGraphicsRootSignature(m_root_signature.Get());
    m_bundle->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_bundle->IASetVertexBuffers(0, 1, &m_vertex_buffer_view);
    m_bundle->DrawInstanced(3, 1, 0, 0);
    ThrowIfFailed(m_bundle->Close());
  }

  // Create synchronization objects.
  {
    ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                        IID_PPV_ARGS(&m_fence)));
    m_fence_value = 1;
    m_fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (m_fence_event == nullptr)
      ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
  }

  WaitForPreviousFrame();
}

int D3D12Window::Run(int cmd_show) {
  // Create a Win32 Window instance
  if (!Create(L"Direct3D 12 Sample", WS_OVERLAPPEDWINDOW))
    return 1;

  // Initialize Direct3D 12 stuff ...
  OnInit();

  ShowWindow(m_hwnd, cmd_show);

  // Start he message loop (i.e., GUI's main loop)
  MSG msg = {};
  while (msg.message != WM_QUIT) {
    // Process any messages in the queue.
    if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }

  // Release Direct3D 12 resources
  OnDestroy();
  return 0;
}

/*
 * Get first hardware adpater (i.e., logical-like device) that satisfies the
 * given high performance requirement.
 *
 * Returns first hardware adapter enumerated by GPU preference that succeeds in
 * creating a D3D12 device with `D3D_FEATURE_LEVEL_11_0`.
 */
void D3D12Window::GetHardwareAdapter(
    _In_ IDXGIFactory1 *pFactory,
    _Outptr_result_maybenull_ IDXGIAdapter1 **ppAdapter,
    bool high_performance_adapter) {
  *ppAdapter = nullptr;

  ComPtr<IDXGIAdapter1> adapter;
  ComPtr<IDXGIFactory6> factory6;
}

LRESULT D3D12Window::HandleMessage(UINT msg, WPARAM wparam, LPARAM lparam) {
  switch (msg) {
  case WM_PAINT:
    OnUpdate();
    OnRender();
    return 0;
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  }

  // For all other messages that we don't handle, invoke the default Windows
  // procedure for them (default message handler).
  return DefWindowProcW(m_hwnd, msg, wparam, lparam);
}

/*
 * Update frame-based values (e.g., object transforms).
 */
void D3D12Window::OnUpdate() { }

void D3D12Window::OnRender() {
  PopulateGraphicsCmdList();
  ID3D12CommandList *ppcmd_lists[] = {m_graphics_cmd_list.Get()};
  m_cmd_queue->ExecuteCommandLists(_countof(ppcmd_lists), ppcmd_lists);

  ThrowIfFailed(m_swapchain->Present(1, 0));

  WaitForPreviousFrame();
}

void D3D12Window::OnDestroy() {
  /*
   * You do NOT want to release/destroy resources that are still accessed by
   * the GPU. Just wait until it finishes rendering the current frame then
   * close/cleanup stuff.
   */
  WaitForPreviousFrame();
  CloseHandle(m_fence_event);
}

void D3D12Window::PopulateGraphicsCmdList() {
  ThrowIfFailed(m_cmd_allocator->Reset());
  ThrowIfFailed(m_graphics_cmd_list->Reset(m_cmd_allocator.Get(),
                                           m_pipeline_state.Get()));

  m_graphics_cmd_list->SetGraphicsRootSignature(m_root_signature.Get());

  ID3D12DescriptorHeap* ppheaps[] = { m_srv_heap.Get() };
  m_graphics_cmd_list->SetDescriptorHeaps(_countof(ppheaps), ppheaps);

  m_graphics_cmd_list->SetGraphicsRootDescriptorTable(0, m_srv_heap->GetGPUDescriptorHandleForHeapStart());
  m_graphics_cmd_list->RSSetViewports(1, &m_viewport);
  m_graphics_cmd_list->RSSetScissorRects(1, &m_scissor_rect);

  const auto transition_to_rt_write = CD3DX12_RESOURCE_BARRIER::Transition(
      m_render_targets[m_frame_idx].Get(), D3D12_RESOURCE_STATE_PRESENT,
      D3D12_RESOURCE_STATE_RENDER_TARGET);
  m_graphics_cmd_list->ResourceBarrier(1, &transition_to_rt_write);

  CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(
      m_rtv_heap->GetCPUDescriptorHandleForHeapStart(), m_frame_idx,
      m_rtv_descriptor_size);
  m_graphics_cmd_list->OMSetRenderTargets(1, &rtv_handle, FALSE, nullptr);


  // BEGIN record commands

  const float clear_color[] = {0.0f, 1.0f, 0.0f, 1.0f};
  m_graphics_cmd_list->ClearRenderTargetView(rtv_handle, clear_color, 0,
                                             nullptr);

  // Instead of drawing the triangle using cmds submitted in this direct cmd
  // list, we use the more optimized bundle set up earlier and just execute it.
  m_graphics_cmd_list->ExecuteBundle(m_bundle.Get());

  // END record commands

  const auto transition_to_present = CD3DX12_RESOURCE_BARRIER::Transition(
      m_render_targets[m_frame_idx].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
      D3D12_RESOURCE_STATE_PRESENT);
  m_graphics_cmd_list->ResourceBarrier(1, &transition_to_present);

  ThrowIfFailed(m_graphics_cmd_list->Close());
}

/*
 * This is not the ideal way to synchronize CPU<->GPU resources.
 */
void D3D12Window::WaitForPreviousFrame() {
  const UINT64 fence = m_fence_value; // copy current fence integer value
  /*
   * Tell the command queue to signal the fence after it finishes whatever
   * previous work (i.e., command lists) that was submitted to it.
   */
  ThrowIfFailed(m_cmd_queue->Signal(m_fence.Get(), fence));
  ++m_fence_value;
  /*
   * Retrieve current value from the GPU. If this value hasn't reached the value
   * that we have set for the queue to signal on its command list completion
   * then block until it is.
   *
   * Blocking may seem confusing at first but it goes as follows:
   * - The D3D12 fence object itself cannot be used to block this Win32 thread
   *   (think of it, Direct3D12 targets both desktop Windows and Xbox console;
   *   it makes no sense to add just-Win32 vtable entries. Or does it?)
   * - What we do instead is that we use Win32's `synchapi.h` which provides
   *   synchronization mechanisms/objects for Windows.
   *
   * `SetEventOnCompletion` says: when the GPU fence reaches the value we set it
   * to signal on, signal this Windows event.
   *
   * `WaitForSingleObject` simply blocks forever (because of the `INFINITE`)
   * until that event is set/signaled.
   */
  if (m_fence->GetCompletedValue() < fence) {
    ThrowIfFailed(m_fence->SetEventOnCompletion(fence, m_fence_event));
    WaitForSingleObject(m_fence_event, INFINITE);
  }
  m_frame_idx = m_swapchain->GetCurrentBackBufferIndex();
}
