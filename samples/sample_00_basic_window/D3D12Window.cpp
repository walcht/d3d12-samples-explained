#include "D3D12Window.hpp"
#include "CommonUtils.hpp"
#include "Win32BaseWindow.hpp"

using namespace Utils;

D3D12Window::D3D12Window(UINT width, UINT height, std::wstring name)
    : Win32BaseWindow<D3D12Window>(), m_width(width), m_height(height),
      m_title(name), m_frame_idx(0), m_rtv_descriptor_size(0) {
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
