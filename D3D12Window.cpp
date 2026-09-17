#include "D3D12Window.hpp"
#include "CommonUtils.hpp"
#include "Win32BaseWindow.hpp"

using namespace Utils;

D3D12Window::D3D12Window(UINT width, UINT height, std::wstring name)
    : Win32BaseWindow<D3D12Window>(), m_width(width), m_height(height),
      m_title(name), m_use_warp_device(false), m_frame_idx(0),
      m_viewport(0.0f, 0.0f, static_cast<float>(width),
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

/*
 * Initialize and load pipeline state. This includes the following common
 * Direct3D 12 substeps:
 *  1. Enable debug layer (only for Debug builds). This is conceptually similar
 *     to Vulkan's debug layer (I still don't know to which extent they are
 *     similar).
 *  2. Create a DXGI factory instance (pick the version most suitable for your
 *     needs).
 *  3. Create the device through DXGI factory instance.
 *  4. Create the command queue via the just-created device.
 *  5. Create the swapchain using the just-created DXGI factory.
 *  6. Create the command list allocator.
 *
 */
void D3D12Window::InitPipeline() {
  UINT dxgiFactoryFlags = 0;

  /*
   *  1. Enable debug layer (only for Debug builds)
   *
   * Requires the Graphics Tools "optional feature". NOTE: Enabling the debug
   * layer after device creation will invalidate the active device.
   */
#if defined(_DEBUG)
  {
    ComPtr<ID3D12Debug> debugController;
    ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
    debugController->EnableDebugLayer();

    // Enable additional debug layers.
    dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
  }
#endif

  /*
   * 2. Create a DXGI factory instance (pick the version most suitable for your
   * needs).
   *
   * This is used to create the D3D12 device and the swapchain (among other
   * things).
   *
   * The degit at the end of `IDXGIFactory\d` can be thought of as a newer
   * version of the interface with increasing order. It reflects the interface
   * revision and inherits from the version prior to it.
   */
  ComPtr<IDXGIFactory2> factory;
  ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

  /*
   * 3. Create the device.
   *
   * We create the device by looping through available "adapters" (i.e., GPUs;
   * to some degree) and picking the first one that satisfies our needs (e.g.,
   * high performance).
   */
  ComPtr<IDXGIAdapter1> hardwareAdapter;
  GetHardwareAdapter(factory.Get(), &hardwareAdapter);
  ThrowIfFailed(D3D12CreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                  IID_PPV_ARGS(&m_device)));

  /*
   * 4. Create the command queue via the just-created device.
   *
   * A command queue is just a queue in which a *command list* is submitted.
   * A command list is simply a list in which GPU rendering and resource
   * management commands are recorded.
   *
   * Unlike Direct3D 11 or OpenGL, and like Vulkan, rendering and resource
   * management commands are prerecorded into a command list and then
   * submitted to a command queue for executions on the device.
   *
   * The reasoning behind this design philosophy is that:
   *  - the driver can potentially perform better optimizations when a whole
   *  set of commands is submitted at once (vs. immediate mode context where
   *  each command is submitted separately).
   *  - concurrent recording of these commands on multi-core systems is
   *  supported.
   *  - set of commands that are submitted repeatably (i.e., not update each
   *  frame) can be recorded into bundles (i.e., secondary command buffer in
   *  Vulkan; not one-to-one equivalent but same concept) and can increase
   *  efficiency.
   *
   * A command lists are typically executed just once (i.e., command lists are
   * usually updated each frame). Application can group together subsets of
   * commands into the so-called `command bundles` which are expected to be
   * executed more than once (i.e., repeated execution within the same command
   * list or within multiple command lists). This is quite a bit advanced for
   * this introductory sample but is still important to be aware of.
   */
  D3D12_COMMAND_QUEUE_DESC queueDesc = {};
  queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  ThrowIfFailed(
      m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_cmd_queue)));

  /*
   * 5. Create the swapchain using the just-created DXGI factory.
   */
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

  /*
   * 6. Create the descriptor heaps.
   *
   * Descriptor heaps are used to store the *descriptor* specifications that
   * shaders reference. Descriptors describe, as their names suggest, the
   * resources a shader references.
   *
   * Descriptors are, probably, one of the most confusing aspects of modern
   * graphics and compute APIs (Direc3D 12 and Vulkan). To understand them,
   * one has to understand Shaders and the multiple kind of resources they can
   * access first. One then has to understand the following: just mentioning
   * the type of resource in Shader code is not sufficient for the GPU to know,
   * for instance, where the resource actually is. This kind of binding has to
   * be done by the application.
   *
   * For any non-trivial graphics application, there will a lot of descriptors.
   * These descriptors have to reside somewhere and given the fact that
   * everything about modern graphics APIs revolve around performance, a memory
   * strategy has to be deployed for most efficient use. It would be really
   * inefficient to just allocate descriptors on the fly whenever they are
   * needed. That would simply mean tons of allocations per a single frame. This
   * is the main reason why descriptor heaps are needed.
   *
   * Q. But why are they not called `descriptor allocators`, why heaps?
   * A. A descriptor heap is simply a contiguous array of descriptors slots of a
   * particular type (e.g., RTV vs. SRV). It simply provides slots where these
   * descriptors can be placed (i.e., backing memory storage). I find the term
   * heap confusing because it hints, initially, at a CPU/GPU memory heap which
   * is absolutely not the case. What Direct3D12 calls `heaps` is simply
   * terminology to describe a pool of addressable storage locations with a
   * fixed capacity. Extremely confusing terminology, if you ask me.
   *
   * Note that when creating a descriptor heap we specify its capacity (same
   * memory allocation technique in play here).
   */
  {
    // Describe and create a render target view (RTV) descriptor heap.
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = NBR_FRAMES_IN_FLIGHT;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc,
                                                 IID_PPV_ARGS(&m_rtv_heap)));

    // Describe and create a shader resource view (SRV) heap for the texture.
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 1;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&srvHeapDesc,
                                                 IID_PPV_ARGS(&m_srv_heap)));

    m_rtv_descriptor_size = m_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  }

  /*
   * 7. Create a render target view.
   */
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

  /*
   *  8. Create the command list allocator.
   *
   *  A command allocator allows us (i.e., the application) to manage the memory
   *  that is allocated for command lists. The type of a command list can be
   *  either: direct, bundle (i.e., indirect/secondary command list/buffer),
   *  computer, copy (for data transfer commands), or a set of
   *  vide-codec-related commands lists that are outside the scope of this
   *  sample. We just want to create a direct command list hence why we supply
   *  `D3D12_COMMAND_LIST_TYPE_DIRECT`.
   *
   *  To reclaim memory allocated by this allocator, simply call
   *  `ID3D12CommandAllocator::Reset` (this doesn't `free` the memory in the
   *  usual sense; it just sets its internal already-allocated memory as
   *  available to use rather than free-ing then re-allocating. I.e., the usual
   *  memory pool design pattern in play here).
   */
  ThrowIfFailed(m_device->CreateCommandAllocator(
      D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_cmd_allocator)));
}

// Load the sample assets.
void D3D12Window::LoadAssets() {

  /*
   * 1. Create the command list.
   *
   * Command lists are by default created in a recording state but our main
   * loop expects a closed command list -> this is why we call `Close()`.
   */
  ThrowIfFailed(m_device->CreateCommandList(
      0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_cmd_allocator.Get(), nullptr,
      IID_PPV_ARGS(&m_graphics_cmd_list)));
  ThrowIfFailed(m_graphics_cmd_list->Close());

  /*
   * 2. Create synchronization objects.
   *
   * To quote the official docs:
   *  A fence is an integer that represents the current unit of work being
   *  processed. When the app advances the fence, by calling
   *  ID3D12CommandQueue::Signal, the integer is updated. Apps can check the
   *  value of a fence and determine if a unit of work has been completed in
   *  order to decide whether a subsequent operation can be started.
   *
   * This is not too helpful so here is my rephrasing attempt:
   * A fence is an integer that can be updated by the GPU while being observed
   * by the CPU to achieve a CPU<->GPU synchronization mechanism.
   *
   */
  {
    ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                        IID_PPV_ARGS(&m_fence)));
    m_fence_value = 1;
    m_fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (m_fence_event == nullptr)
      ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
  }

  /*
  // Create the root signature.
  {
    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

    // This is the highest version the sample supports. If CheckFeatureSupport
    // succeeds, the HighestVersion returned will not be greater than this.
    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

    if (FAILED(m_device->CheckFeatureSupport(
            D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData)))) {
      featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
    }

    CD3DX12_DESCRIPTOR_RANGE1 ranges[1];
    ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0,
                   D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

    CD3DX12_ROOT_PARAMETER1 rootParameters[1];
    rootParameters[0].InitAsDescriptorTable(1, &ranges[0],
                                            D3D12_SHADER_VISIBILITY_PIXEL);

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    sampler.MipLODBias = 0;
    sampler.MaxAnisotropy = 0;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Init_1_1(
        _countof(rootParameters), rootParameters, 1, &sampler,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    ThrowIfFailed(D3DX12SerializeVersionedRootSignature(
        &rootSignatureDesc, featureData.HighestVersion, &signature, &error));
    ThrowIfFailed(m_device->CreateRootSignature(
        0, signature->GetBufferPointer(), signature->GetBufferSize(),
        IID_PPV_ARGS(&m_root_signature)));
  }

  // Create the pipeline state, which includes compiling and loading shaders.
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
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};

    // Describe and create the graphics pipeline state object (PSO).
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = {inputElementDescs, _countof(inputElementDescs)};
    psoDesc.pRootSignature = m_root_signature.Get();
    psoDesc.VS =
        CD3DX12_SHADER_BYTECODE(pVertexShaderData, vertexShaderDataLength);
    psoDesc.PS =
        CD3DX12_SHADER_BYTECODE(pPixelShaderData, pixelShaderDataLength);
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

  // Create the graphics command list.
  ThrowIfFailed(m_device->CreateCommandList(
      0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_cmd_allocator.Get(),
      m_pipeline_state.Get(), IID_PPV_ARGS(&m_graphics_cmd_list)));

  // Create the vertex buffer.
  {
    // Define the geometry for a triangle.
    Vertex triangleVertices[] = {
        {{0.0f, 0.25f * m_aspect_ratio, 0.0f}, {0.5f, 0.0f}},
        {{0.25f, -0.25f * m_aspect_ratio, 0.0f}, {1.0f, 1.0f}},
        {{-0.25f, -0.25f * m_aspect_ratio, 0.0f}, {0.0f, 1.0f}}};

    const UINT vertexBufferSize = sizeof(triangleVertices);
    const auto heap_properties =
        CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    const auto heap_desc = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);

    // Note: using upload heaps to transfer static data like vert buffers is not
    // recommended. Every time the GPU needs it, the upload heap will be
    // marshalled over. Please read up on Default Heap usage. An upload heap is
    // used here for code simplicity and because there are very few verts to
    // actually transfer.
    ThrowIfFailed(m_device->CreateCommittedResource(
        &heap_properties, D3D12_HEAP_FLAG_NONE, &heap_desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&m_vertex_buffer)));

    // Copy the triangle data to the vertex buffer.
    UINT8 *pVertexDataBegin;
    CD3DX12_RANGE readRange(
        0, 0); // We do not intend to read from this resource on the CPU.
    ThrowIfFailed(m_vertex_buffer->Map(
        0, &readRange, reinterpret_cast<void **>(&pVertexDataBegin)));
    memcpy(pVertexDataBegin, triangleVertices, sizeof(triangleVertices));
    m_vertex_buffer->Unmap(0, nullptr);

    // Initialize the vertex buffer view.
    m_vertex_buffer_view.BufferLocation =
        m_vertex_buffer->GetGPUVirtualAddress();
    m_vertex_buffer_view.StrideInBytes = sizeof(Vertex);
    m_vertex_buffer_view.SizeInBytes = vertexBufferSize;
  }

  // Note: ComPtr's are CPU objects but this resource needs to stay in scope
  // until the command list that references it has finished executing on the
  // GPU. We will flush the GPU at the end of this method to ensure the resource
  // is not prematurely destroyed.
  ComPtr<ID3D12Resource> textureUploadHeap;

  // Create the texture.
  {
    // Describe and create a Texture2D.
    D3D12_RESOURCE_DESC textureDesc = {};
    textureDesc.MipLevels = 1;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.Width = TEXTURE_WIDTH;
    textureDesc.Height = TEXTURE_HEIGHT;
    textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    textureDesc.DepthOrArraySize = 1;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.SampleDesc.Quality = 0;
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

    const auto heap_properties =
        CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

    ThrowIfFailed(m_device->CreateCommittedResource(
        &heap_properties, D3D12_HEAP_FLAG_NONE, &textureDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_texture)));

    const UINT64 uploadBufferSize =
        GetRequiredIntermediateSize(m_texture.Get(), 0, 1);

    const auto desc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);

    // Create the GPU upload buffer.
    ThrowIfFailed(m_device->CreateCommittedResource(
        &heap_properties, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&textureUploadHeap)));

    // Copy data to the intermediate upload heap and then schedule a copy
    // from the upload heap to the Texture2D.
    std::vector<UINT8> texture = GenerateTextureData();

    D3D12_SUBRESOURCE_DATA textureData = {};
    textureData.pData = &texture[0];
    textureData.RowPitch = TEXTURE_WIDTH * TEXTURE_STRIDE;
    textureData.SlicePitch = textureData.RowPitch * TEXTURE_HEIGHT;

    UpdateSubresources(m_graphics_cmd_list.Get(), m_texture.Get(),
                       textureUploadHeap.Get(), 0, 0, 1, &textureData);
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_graphics_cmd_list->ResourceBarrier(1, &barrier);

    // Describe and create a SRV for the texture.
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = textureDesc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    m_device->CreateShaderResourceView(
        m_texture.Get(), &srvDesc,
        m_srv_heap->GetCPUDescriptorHandleForHeapStart());
  }

  // Close the command list and execute it to begin the initial GPU setup.
  ThrowIfFailed(m_graphics_cmd_list->Close());
  ID3D12CommandList *ppCommandLists[] = {m_graphics_cmd_list.Get()};
  m_cmd_queue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

  // Create synchronization objects and wait until assets have been uploaded to
  // the GPU.
  {
    ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                        IID_PPV_ARGS(&m_fence)));
    m_fence_value = 1;

    // Create an event handle to use for frame synchronization.
    m_fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (m_fence_event == nullptr) {
      ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
    }

    // Wait for the command list to execute; we are reusing the same command
    // list in our main loop but for now, we just want to wait for setup to
    // complete before continuing.
    WaitForPreviousFrame();
  }
  */
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
void D3D12Window::OnUpdate() {}

/*
 * 1. Populate the graphics command list with our rendering commands.
 * 2. Execute the graphics command list.
 * 3. Present the rendered frame.
 * 4. Wait for previous frame to finish so that the next invocation of
 *    `OnRender()` doesn't cause any resource contentions. As stated before,
 *    there is a much better way of doing this than simply *waiting for
 *    the previous frame to finish*. This is absolutely not utilizing the GPU
 *    sufficiently.
 */
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
  /*
   * 1. Reset the command allocator. This has to be done before recording the
   *    new command list and CANNOT be done while the associated command lists
   *    are still executing (i.e., use a fence to know when said command lists
   *    have finished after which you can reset the command allocator).
   * 2. Reset the graphics command list before re-recording. All command lists
   *    must be reset before re-recording.
   * 3. Start recording the actual commands:
   *    1. Do a resource transition for current render target to a
   *       `D3D12_RESOURCE_STATE_RENDER_TARGET` which essentially tells the GPU
   *       to do any layout transformations necessary to prepare the D3D12
   *       resource for writing as a render target.
   *    2. Retrieve the current render target handle (needed for
   *       render-target-related commands).
   *    3. Write your actual graphics rendering commands
   *    4. Do a transition from render target write to presentation (i.e.,
   *       instruct the GPU that this resource will be used from now on as input
   *       for presentation).
   * 4. Close the command list.
   */
  ThrowIfFailed(m_cmd_allocator->Reset());
  ThrowIfFailed(m_graphics_cmd_list->Reset(m_cmd_allocator.Get(),
                                           m_pipeline_state.Get()));
  const auto transition_to_rt_write = CD3DX12_RESOURCE_BARRIER::Transition(
      m_render_targets[m_frame_idx].Get(), D3D12_RESOURCE_STATE_PRESENT,
      D3D12_RESOURCE_STATE_RENDER_TARGET);
  m_graphics_cmd_list->ResourceBarrier(1, &transition_to_rt_write);

  CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(
      m_rtv_heap->GetCPUDescriptorHandleForHeapStart(), m_frame_idx,
      m_rtv_descriptor_size);


  // BEGIN record commands

  const float clear_color[] = {0.0f, 1.0f, 0.0f, 1.0f};
  m_graphics_cmd_list->ClearRenderTargetView(rtv_handle, clear_color, 0,
                                             nullptr);

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
