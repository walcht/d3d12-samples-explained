# About

Direct3D 12 samples adapted from the official
[Direct3D 12 Samples repo][d3d12-samples-repo] with much more detailed
explanations, CMake support (i.e., no Visual-Studio-sepcific files), detailed
instructions on conceptual similarities with Vulkan (to bring home the point
that modern graphics API share, to some degree, the same underyling GPU
hardware model).

Some very important design/code patterns related to Win32/COM/Direct3D12
development are explained here. Make sure to read those alongside the code
samples.

If anything, doing these samples made me appreciate the insane amount of work
in projects such as Wine where a compatibility layer is developed to run Windows
executables (i.e., Win32API layer, Direct3D12-to-Vulkan converter, etc.).

## How to go through the samples

Samples are numbered accordingly from most basic to most advanced.
You can use `diff` as below to showcase the added functionalities between each
sample:

```bash
diff samples/sample_00_basic_window/D3D12Window.cpp \
    samples/sample_01_basic_window/D3D12Window.cpp
```

## Build

All samples can be built by invoking the top most CMake (you have to provide
the path to the HLSL compiler `dxc.exe`):

```bash
cmake -S . -B build/ -DDXC_EXECUTABLE_PATH=$((Get-Command dxc.exe).Path)
cmake --build build/ --config [Debug|Release] -j10
cd build
./build/samples/<sample-name>/[Debug|Release]/sample_00_basic_window.exe
```

If you want to debug any problems then run through `cdbX64.exe`:

```bash
cdbX64.exe ./build/samples/<sample-name>/[Debug|Release]/sample_00_basic_window.exe
```

If you are facing any issues then you should be aware that on Windows, GUI
applications don't have a console attached to them by default so any
stdout/stderrr/stdin operation are directed to void (i.e., nothing happens).

On Debug builds, these samples attach to the console of the parent process if
there is any, otherwise create a console window. In short, **if you want to
see any std::cout output just build for Debug**.

If you want your Language Server to provide LSP functionalities (e.g., for 
Neovim clangd) then build to a separate directory and keep it:

```bash
cmake -S . -B build_for_lsp/ -G "Unix Makefiles" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cp build_lsp/compile_commands.json .
```

> [!NOTE]
> Building with MinGW is not supported because some WRL features are not supported
> (e.g., `FileHandle` and I also get tons of other compilation issues). Dealing
> with Win32 API + COM + D3D12 is already hard as it is, let's keep it simple
> and just use Visual Studio generator.


## ABI extensibility: COM vs. Vulkan approaches

If you look at any non-trivial COM application (e.g., our Direct3D 12 sample
here), you will notice interfaces such as `IDXGIFactory3`. The number at the
end is simply used to reflect a newer revision (i.e., for `IDXGIFactory3`, some
additional features/functions were added to `IDXGIFactory2`). `IDXGIFactory3`
inherits from `IDXGIFactory2` and it in turn inherits from `IDXGIFactory1` which
in turn inherits from the initial interface `IDXGIFactory`.

The revision number does NOT reflect the DXGI version; it is simply a number
that is incremented whenever a new function (or set of functions) needs to be
added.

But why? COM is a binary interface. If we introduced a new function to the
existing `IDXGIFactory` interface by adding it to the end of the vtable,
existing binaries that use the old interface would continue to work, because
their existing methods remain at the same vtable slots. However, newer code
expecting the new method cannot safely use an old implementation, because that
implementation is not required to provide the new vtable entry; calling it could
therefore read past the end of the vtable. COM therefore treats a published
interface as immutable, and new functionality is exposed through a new interface
such as `IDXGIFactory1`.

COM's approach to ABI extensibility is object-oriented through an 
immutable-interface model. There are essentially two approaches to retrieving
newer interfaces:

1. Directly create the object with the supplied newer interface version:
```C++
ComPtr<ID3D12Device8> device;
HRESULT hr = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12,
    IID_PPV_ARGS(&device));
if (FAILED(hr)) {
    // Oops... ID3D12Device8 is not implemented/supported
}
```

2. Query newer interface version from an already created object using
`IUnknown::QueryInterface(IID_IDXGIFactory1, ...)`/`ComPtr::As(...)`
(remember, `IUnknown` is implemented by all COM objects):
```C++
ComPtr<ID3D12Device> device = ...;  // obtained from somewhere ...
ComPtr<ID3D12Device8> device8;      // newer version
if (FAILED(device.As(&device8))) {
    // ID3D12Device8 is not implemented/supported
}
```

Note: the example above uses `device.As(&device8)` which is equivalent to
`device->QueryInterface(IID_PPV_ARGS(&device8))`.

Vulkan employs a different strategy to handle this issue. Vulkan strategy is
not object-oriented oriented (like Direct3D/COM's approach) because Vulkan is a
C API. Vulkan relies on `void* pNext` chain in its structs to extend structs by
pointing to an extension struct which may itself point to another extension
struct and so on (hence the *extension chain* terminology). The Vulkan
implementation in current use (among other things), relies on the
`VkStructureType sType` field that is present in all Vulkan API structs as the
first field to know (also among other things) if the type of the struct is
known (i.e., a supported extension). Actually, each extension structure has to
*inherit* from the base struct `VkBaseInStructure` which has the following
definition:

```C
// Provided by VK_VERSION_1_0
typedef struct VkBaseInStructure {
    VkStructureType                    sType;
    const struct VkBaseInStructure*    pNext;
} VkBaseInStructure;
```

This provides a common interface for implementations (e.g., drivers) to
traverse the extension chain and check for supported sType extensions. An
example implementation (from VkGuide) is as follows:

```C++
void vkGetSomeExtensionValueFromSomeStruct(const VkSomeStruct* pA) {
    auto next = reinterpret_cast<VkBaseInStructure*>(pA->pNext);
    while (next != nullptr) {
        switch (next->sType) {
            case VK_STRUCTURE_TYPE_B:
                const auto pB = reinterpret_cast<VkB*>(next);
                // Do somtething with this extension struct ...
                break;
            default:
                // Unsupported extension struct. Log/error or smth ...
        }
        next = reinterpret_cast<VkBaseInStructure*>(next->pNext);
    }

    // ...
}
```

Essentially, Direct3D/COM extends interfaces (its core ABI model) by defining
new interfaces while Vulkan extens structs by defining new structs that are
referenced by the base struct. Both solve the same core issue of providing an
ABI extension mechanism without breaking existing binaries.

## Direct3D 12 vs. Vulkan's Concept of *Devices*

The usual workflow to create a logical-like (if you are coming from Vulkan;
otherwise ignore this terminology) is as follows:

1. Create a DXGI factory (Direct3D in general follows this pattern where common
objects to different graphics runtimes are abstracted in DXGI and are created
via DXGI factories).
2. Use said factory to create a DXGI adapter (which resembles a physical device
in Vulkan).
3. Create the logical-like device using `D3D12CreateDevice`.

The above steps roughly translate to this code segment:

```C++
ComPtr<IDXGIFactory4> factory;
ThrowIfFailed(CreateDXGIFactory2(0 /* factory flags */, IID_PPV_ARGS(&factory)));

ComPtr<IDXGIAdapter1> hardware_adapter;
GetHardwareAdapter();
```

If you are coming from Vulkan (like me).

[d3d12-samples-repo]: 
