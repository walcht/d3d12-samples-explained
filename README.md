# About


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

## Build

```bash
cmake -S . -B build/ -G "Unix Makefiles" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build/ --config [Debug|Release] -j10
cd build
ctest -C [Debug|Release] -j10
cmake --install .
```
