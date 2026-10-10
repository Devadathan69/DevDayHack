#include "d3d-overlay.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d9.h>

#include <atomic>
#include <cstring>
#include <limits>
#include <new>
#include <vector>

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "user32.lib")

static_assert(sizeof(void*) == 4, "The game overlay must be built for Windows x86.");

namespace {
using Create9Fn = IDirect3D9* (WINAPI*)(UINT);
using CreateDeviceFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3D9*, UINT,
    D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
using ReleaseFn = ULONG (STDMETHODCALLTYPE*)(IDirect3DDevice9*);
using EndSceneFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*);
using ResetFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
using PresentFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*,
    const RECT*, const RECT*, HWND, const RGNDATA*);

constexpr unsigned kCreateDeviceSlot = 16;
constexpr unsigned kResetSlot = 16;
constexpr unsigned kPresentSlot = 17;
constexpr unsigned kEndSceneSlot = 42;
constexpr ULONGLONG kRecentFrameMs = 1000;
constexpr std::size_t kMaximumFrameBytes = 256u * 1024u * 1024u;
std::atomic<NpcD3D::GameFrameCallback> gameFrameCallback{nullptr};

IDirect3D9* WINAPI HookCreate9(UINT version);
HRESULT STDMETHODCALLTYPE HookCreateDevice(IDirect3D9* factory, UINT adapter,
    D3DDEVTYPE type, HWND window, DWORD flags, D3DPRESENT_PARAMETERS* parameters,
    IDirect3DDevice9** device);
ULONG STDMETHODCALLTYPE HookRelease(IDirect3DDevice9* device);
HRESULT STDMETHODCALLTYPE HookEndScene(IDirect3DDevice9* device);
HRESULT STDMETHODCALLTYPE HookReset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* parameters);
HRESULT STDMETHODCALLTYPE HookPresent(IDirect3DDevice9* device,
    const RECT* source, const RECT* destination, HWND window, const RGNDATA* dirty);

class ExclusiveLock {
public:
    explicit ExclusiveLock(SRWLOCK& lock) : lock_(lock) { AcquireSRWLockExclusive(&lock_); }
    ~ExclusiveLock() { ReleaseSRWLockExclusive(&lock_); }
    ExclusiveLock(const ExclusiveLock&) = delete;
    ExclusiveLock& operator=(const ExclusiveLock&) = delete;
private:
    SRWLOCK& lock_;
};

class SharedLock {
public:
    explicit SharedLock(SRWLOCK& lock) : lock_(lock) { AcquireSRWLockShared(&lock_); }
    ~SharedLock() { ReleaseSRWLockShared(&lock_); }
    SharedLock(const SharedLock&) = delete;
    SharedLock& operator=(const SharedLock&) = delete;
private:
    SRWLOCK& lock_;
};

struct FactoryHooks {
    void** table;
    CreateDeviceFn createDevice;
    FactoryHooks* next;
};

struct DeviceHooks {
    void** table;
    EndSceneFn endScene;
    ResetFn reset;
    PresentFn present;
    ReleaseFn release;
    bool moduleResident;
    DeviceHooks* next;
};

struct State {
    SRWLOCK initializationLock = SRWLOCK_INIT;
    SRWLOCK hooksLock = SRWLOCK_INIT;
    SRWLOCK pixelsLock = SRWLOCK_INIT;
    SRWLOCK renderLock = SRWLOCK_INIT;
    FactoryHooks* factories = nullptr;
    DeviceHooks* devices = nullptr;
    Create9Fn create9 = nullptr;
    void** importSlot = nullptr;
    HMODULE owner = nullptr;
    HMODULE runtime = nullptr;
    IDirect3D9* probeFactory = nullptr;
    std::atomic<bool> enabled{false};
    std::atomic<bool> probing{false};
    std::atomic<bool> probeInstalled{false};
    std::atomic<HWND> probeWindow{nullptr};
    std::atomic<ULONGLONG> lastGameFrame{0};
    std::atomic<std::uint64_t> presented{0};
    std::vector<unsigned char> pixels;
    int frameWidth = 0;
    int frameHeight = 0;
    std::uint64_t serial = 0;

    // Accessed only under renderLock. No device AddRef is retained.
    IDirect3DDevice9* renderDevice = nullptr;
    IDirect3DTexture9* texture = nullptr;
    UINT textureWidth = 0;
    UINT textureHeight = 0;
    int imageWidth = 0;
    int imageHeight = 0;
    UINT viewportWidth = 0;
    UINT viewportHeight = 0;
    HWND gameWindow = nullptr;
    std::uint64_t uploaded = 0;
    std::uint64_t pending = 0;
};

State& GetState() {
    // Hook records and their locks must outlive every game COM object and CRT
    // teardown. Pinning the DLL alone does not prevent global destructors.
    static State* state = new State;
    return *state;
}

void** VTable(void* object) {
    return *static_cast<void***>(object);
}

void* ReadSlot(void** slot) {
    // Aligned 32-bit reads are atomic on x86. An interlocked read-modify-write
    // would fault on a read-only vtable page, even when its value is unchanged.
    void* value = *reinterpret_cast<void* volatile*>(slot);
    MemoryBarrier();
    return value;
}

bool PatchSlot(void** slot, void* original, void* replacement) {
    if (ReadSlot(slot) == replacement) return true;
    DWORD protection = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &protection)) return false;
    void* previous = InterlockedCompareExchangePointer(
        reinterpret_cast<void* volatile*>(slot), replacement, original);
    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(void*), protection, &ignored);
    return (previous == original || previous == replacement) && ReadSlot(slot) == replacement;
}

bool PinAddress(void* address) {
    HMODULE owner = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(address), &owner) != FALSE;
}

bool InstallFactoryHooks(IDirect3D9* factory) {
    if (!factory) return false;
    State& state = GetState();
    ExclusiveLock lock(state.hooksLock);
    void** table = VTable(factory);
    FactoryHooks* record = state.factories;
    while (record && record->table != table) record = record->next;
    if (!record) {
        if (!PinAddress(table)) return false;
        auto original = reinterpret_cast<CreateDeviceFn>(ReadSlot(table + kCreateDeviceSlot));
        if (!original || original == HookCreateDevice || !PinAddress(reinterpret_cast<void*>(original))) return false;
        record = new (std::nothrow) FactoryHooks{table, original, state.factories};
        if (!record) return false;
        state.factories = record;
    }
    // Publish immutable originals under the lock before making hooks callable.
    return PatchSlot(table + kCreateDeviceSlot,
        reinterpret_cast<void*>(record->createDevice), reinterpret_cast<void*>(HookCreateDevice));
}

bool InstallDeviceHooks(IDirect3DDevice9* device) {
    if (!device) return false;
    State& state = GetState();
    ExclusiveLock lock(state.hooksLock);
    void** table = VTable(device);
    DeviceHooks* record = state.devices;
    while (record && record->table != table) record = record->next;
    if (record) {
        const auto matches = [table](unsigned slot, void* original, void* hook) {
            void* current = ReadSlot(table + slot);
            return current == original || current == hook;
        };
        if (!matches(kEndSceneSlot, reinterpret_cast<void*>(record->endScene), reinterpret_cast<void*>(HookEndScene)) ||
            !matches(kResetSlot, reinterpret_cast<void*>(record->reset), reinterpret_cast<void*>(HookReset)) ||
            !matches(kPresentSlot, reinterpret_cast<void*>(record->present), reinterpret_cast<void*>(HookPresent)) ||
            !matches(2, reinterpret_cast<void*>(record->release), reinterpret_cast<void*>(HookRelease))) record = nullptr;
    }
    if (!record) {
        auto endScene = reinterpret_cast<EndSceneFn>(ReadSlot(table + kEndSceneSlot));
        auto reset = reinterpret_cast<ResetFn>(ReadSlot(table + kResetSlot));
        auto present = reinterpret_cast<PresentFn>(ReadSlot(table + kPresentSlot));
        auto release = reinterpret_cast<ReleaseFn>(ReadSlot(table + 2));
        if (!endScene || !reset || !present || !release || endScene == HookEndScene ||
            reset == HookReset || present == HookPresent || release == HookRelease ||
            !PinAddress(reinterpret_cast<void*>(endScene)) || !PinAddress(reinterpret_cast<void*>(reset)) ||
            !PinAddress(reinterpret_cast<void*>(present)) || !PinAddress(reinterpret_cast<void*>(release))) return false;
        // Native D3D9 can allocate a per-device vtable on the heap. Such tables
        // are inspected only through a currently live COM object, never later
        // by HasDeviceHooks. Records/originals remain valid after table death.
        record = new (std::nothrow) DeviceHooks{table, endScene, reset, present, release,
            PinAddress(table), state.devices};
        if (!record) return false;
        state.devices = record;
    }
    // EndScene is installed last: never allocate default-pool resources without
    // Reset/Release hooks, nor acknowledge delivery without a Present hook.
    if (!PatchSlot(table + 2,
        reinterpret_cast<void*>(record->release), reinterpret_cast<void*>(HookRelease))) return false;
    if (!PatchSlot(table + kResetSlot,
        reinterpret_cast<void*>(record->reset), reinterpret_cast<void*>(HookReset))) return false;
    if (!PatchSlot(table + kPresentSlot,
        reinterpret_cast<void*>(record->present), reinterpret_cast<void*>(HookPresent))) return false;
    return PatchSlot(table + kEndSceneSlot,
        reinterpret_cast<void*>(record->endScene), reinterpret_cast<void*>(HookEndScene));
}

DeviceHooks* FindDeviceHooks(IDirect3DDevice9* device) {
    State& state = GetState();
    SharedLock lock(state.hooksLock);
    void** table = VTable(device);
    for (DeviceHooks* record = state.devices; record; record = record->next) {
        if (record->table == table) return record;
    }
    return nullptr;
}

bool HasDeviceHooks() {
    State& state = GetState();
    SharedLock lock(state.hooksLock);
    for (DeviceHooks* record = state.devices; record; record = record->next) {
        if (!record->moduleResident) continue;
        if (ReadSlot(record->table + kEndSceneSlot) == reinterpret_cast<void*>(HookEndScene) &&
            ReadSlot(record->table + kResetSlot) == reinterpret_cast<void*>(HookReset) &&
            ReadSlot(record->table + kPresentSlot) == reinterpret_cast<void*>(HookPresent)) return true;
    }
    return false;
}

bool HasFactoryHooks() {
    State& state = GetState();
    SharedLock lock(state.hooksLock);
    for (FactoryHooks* record = state.factories; record; record = record->next) {
        if (ReadSlot(record->table + kCreateDeviceSlot) == reinterpret_cast<void*>(HookCreateDevice)) return true;
    }
    return false;
}

bool InImage(DWORD rva, std::size_t bytes, DWORD imageSize) {
    return rva <= imageSize && bytes <= static_cast<std::size_t>(imageSize - rva);
}

const char* ImageString(unsigned char* image, DWORD rva, DWORD imageSize) {
    if (!InImage(rva, 1, imageSize)) return nullptr;
    const char* text = reinterpret_cast<const char*>(image + rva);
    return std::memchr(text, '\0', imageSize - rva) ? text : nullptr;
}

bool InstallImportHook() {
    State& state = GetState();
    ExclusiveLock lock(state.hooksLock);
    if (state.importSlot) return ReadSlot(state.importSlot) == reinterpret_cast<void*>(HookCreate9);
    auto image = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    if (!image) return false;
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < sizeof(IMAGE_DOS_HEADER)) return false;
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(image + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_IMPORT) return false;
    const DWORD imageSize = nt->OptionalHeader.SizeOfImage;
    const auto imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!imports.VirtualAddress || !InImage(imports.VirtualAddress, imports.Size, imageSize)) return false;
    auto descriptors = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(image + imports.VirtualAddress);
    const std::size_t count = imports.Size / sizeof(IMAGE_IMPORT_DESCRIPTOR);
    Create9Fn exportFunction = nullptr;
    if (HMODULE runtime = GetModuleHandleW(L"d3d9.dll")) {
        exportFunction = reinterpret_cast<Create9Fn>(GetProcAddress(runtime, "Direct3DCreate9"));
    }
    for (std::size_t i = 0; i < count && descriptors[i].Name; ++i) {
        const auto& descriptor = descriptors[i];
        const char* name = ImageString(image, descriptor.Name, imageSize);
        if (!name || _stricmp(name, "d3d9.dll") != 0 || !descriptor.FirstThunk) continue;
        for (DWORD index = 0; ; ++index) {
            const std::size_t offset = static_cast<std::size_t>(index) * sizeof(IMAGE_THUNK_DATA32);
            if (offset > imageSize || descriptor.FirstThunk > imageSize - offset ||
                !InImage(descriptor.FirstThunk + static_cast<DWORD>(offset), sizeof(IMAGE_THUNK_DATA32), imageSize)) break;
            auto thunk = reinterpret_cast<IMAGE_THUNK_DATA32*>(image + descriptor.FirstThunk + offset);
            if (!thunk->u1.Function) break;
            bool matches = false;
            if (descriptor.OriginalFirstThunk) {
                if (descriptor.OriginalFirstThunk > imageSize - offset ||
                    !InImage(descriptor.OriginalFirstThunk + static_cast<DWORD>(offset), sizeof(IMAGE_THUNK_DATA32), imageSize)) break;
                auto original = reinterpret_cast<const IMAGE_THUNK_DATA32*>(image + descriptor.OriginalFirstThunk + offset);
                if (!IMAGE_SNAP_BY_ORDINAL32(original->u1.Ordinal) &&
                    InImage(original->u1.AddressOfData, sizeof(WORD) + 1, imageSize)) {
                    const char* functionName = ImageString(image, original->u1.AddressOfData + sizeof(WORD), imageSize);
                    matches = functionName && std::strcmp(functionName, "Direct3DCreate9") == 0;
                }
            } else {
                matches = exportFunction && reinterpret_cast<void*>(thunk->u1.Function) == reinterpret_cast<void*>(exportFunction);
            }
            if (!matches) continue;
            void** slot = reinterpret_cast<void**>(&thunk->u1.Function);
            auto original = reinterpret_cast<Create9Fn>(ReadSlot(slot));
            if (!original || original == HookCreate9) return false;
            state.create9 = original;
            if (PatchSlot(slot, reinterpret_cast<void*>(original), reinterpret_cast<void*>(HookCreate9))) {
                state.importSlot = slot;
                return true;
            }
            return false;
        }
    }
    return false;
}

IDirect3D9* WINAPI HookCreate9(UINT version) {
    State& state = GetState();
    Create9Fn original = nullptr;
    {
        SharedLock lock(state.hooksLock);
        original = state.create9;
    }
    if (!original) return nullptr;
    IDirect3D9* factory = original(version);
    InstallFactoryHooks(factory);
    return factory;
}

HRESULT STDMETHODCALLTYPE HookCreateDevice(IDirect3D9* factory, UINT adapter,
    D3DDEVTYPE type, HWND window, DWORD flags, D3DPRESENT_PARAMETERS* parameters,
    IDirect3DDevice9** device) {
    State& state = GetState();
    CreateDeviceFn original = nullptr;
    {
        SharedLock lock(state.hooksLock);
        void** table = VTable(factory);
        for (FactoryHooks* record = state.factories; record; record = record->next) {
            if (record->table == table) {
                original = record->createDevice;
                break;
            }
        }
    }
    if (!original) return D3DERR_INVALIDCALL;
    HRESULT result = original(factory, adapter, type, window, flags, parameters, device);
    if (SUCCEEDED(result) && device && *device) InstallDeviceHooks(*device);
    return result;
}

bool IsGameWindow(HWND window) {
    State& state = GetState();
    if (!window || window == state.probeWindow.load() || !IsWindow(window) ||
        !IsWindowVisible(window) || IsIconic(window)) return false;
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    return process == GetCurrentProcessId() && GetAncestor(window, GA_ROOT) == window &&
        (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) == 0;
}

bool GameBackbuffer(IDirect3DDevice9* device, HWND& window, UINT& width, UINT& height) {
    D3DDEVICE_CREATION_PARAMETERS creation{};
    if (FAILED(device->GetCreationParameters(&creation))) return false;
    IDirect3DSwapChain9* chain = nullptr;
    if (FAILED(device->GetSwapChain(0, &chain)) || !chain) return false;
    D3DPRESENT_PARAMETERS parameters{};
    HRESULT result = chain->GetPresentParameters(&parameters);
    chain->Release();
    if (FAILED(result)) return false;
    window = parameters.hDeviceWindow ? parameters.hDeviceWindow : creation.hFocusWindow;
    if (!IsGameWindow(window) ||
        (creation.hFocusWindow && !IsGameWindow(creation.hFocusWindow))) return false;

    IDirect3DSurface9* backbuffer = nullptr;
    IDirect3DSurface9* target = nullptr;
    if (FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer)) || !backbuffer) return false;
    result = device->GetRenderTarget(0, &target);
    D3DSURFACE_DESC description{};
    bool valid = SUCCEEDED(result) && target == backbuffer && SUCCEEDED(backbuffer->GetDesc(&description));
    if (target) target->Release();
    backbuffer->Release();
    if (!valid || !description.Width || !description.Height) return false;
    width = description.Width;
    height = description.Height;
    return true;
}

void ReleaseTexture(State& state) {
    if (state.texture) state.texture->Release();
    state.texture = nullptr;
    state.textureWidth = state.textureHeight = 0;
    state.imageWidth = state.imageHeight = 0;
    state.uploaded = state.pending = 0;
}

void ForgetDevice(State& state) {
    ReleaseTexture(state);
    state.renderDevice = nullptr;
    state.gameWindow = nullptr;
    state.viewportWidth = state.viewportHeight = 0;
    state.lastGameFrame.store(0);
}

UINT PowerOfTwo(UINT value) {
    UINT result = 1;
    while (result < value && result <= (std::numeric_limits<UINT>::max)() / 2) result <<= 1;
    return result;
}

bool UploadFrame(IDirect3DDevice9* device, State& state) {
    SharedLock lock(state.pixelsLock);
    if (state.pixels.empty()) return false;
    if (state.texture && state.uploaded == state.serial) return true;
    if (!state.texture || state.imageWidth != state.frameWidth || state.imageHeight != state.frameHeight) {
        ReleaseTexture(state);
        D3DCAPS9 caps{};
        if (FAILED(device->GetDeviceCaps(&caps))) return false;
        UINT width = static_cast<UINT>(state.frameWidth);
        UINT height = static_cast<UINT>(state.frameHeight);
        if (caps.TextureCaps & D3DPTEXTURECAPS_POW2) {
            width = PowerOfTwo(width);
            height = PowerOfTwo(height);
        }
        if (caps.TextureCaps & D3DPTEXTURECAPS_SQUAREONLY) {
            width = height = width > height ? width : height;
        }
        if (width > caps.MaxTextureWidth || height > caps.MaxTextureHeight ||
            FAILED(device->CreateTexture(width, height, 1, D3DUSAGE_DYNAMIC,
                D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &state.texture, nullptr)) || !state.texture) return false;
        state.textureWidth = width;
        state.textureHeight = height;
        state.imageWidth = state.frameWidth;
        state.imageHeight = state.frameHeight;
    }
    D3DLOCKED_RECT rect{};
    if (FAILED(state.texture->LockRect(0, &rect, nullptr, D3DLOCK_DISCARD))) return false;
    const std::size_t rowBytes = static_cast<std::size_t>(state.frameWidth) * 4;
    const std::size_t textureRowBytes = static_cast<std::size_t>(state.textureWidth) * 4;
    bool valid = rect.pBits && rect.Pitch > 0 && static_cast<std::size_t>(rect.Pitch) >= textureRowBytes;
    if (valid) {
        // Initialize padding too: bilinear sampling at NPOT image edges must
        // not blend with undefined bytes from a discarded texture allocation.
        for (UINT row = 0; row < state.textureHeight; ++row) {
            auto destination = static_cast<unsigned char*>(rect.pBits) + static_cast<std::size_t>(row) * rect.Pitch;
            const UINT sourceRow = row < static_cast<UINT>(state.frameHeight) ? row : state.frameHeight - 1;
            const auto source = state.pixels.data() + static_cast<std::size_t>(sourceRow) * rowBytes;
            std::memcpy(destination, source, rowBytes);
            for (std::size_t column = rowBytes; column < textureRowBytes; column += 4) {
                std::memcpy(destination + column, source + rowBytes - 4, 4);
            }
        }
    }
    HRESULT result = state.texture->UnlockRect(0);
    if (!valid || FAILED(result)) return false;
    state.uploaded = state.serial;
    return true;
}

class DeviceState {
public:
    explicit DeviceState(IDirect3DDevice9* device) : device_(device) {}
    ~DeviceState() {
        Restore();
        if (block_) block_->Release();
        if (vertexShader_) vertexShader_->Release();
        if (pixelShader_) pixelShader_->Release();
        if (declaration_) declaration_->Release();
        if (stream_) stream_->Release();
        if (indices_) indices_->Release();
    }
    bool Capture() {
        if (FAILED(device_->CreateStateBlock(D3DSBT_ALL, &block_)) || !block_ || FAILED(block_->Capture())) return false;
        if (FAILED(device_->GetVertexShader(&vertexShader_)) || FAILED(device_->GetPixelShader(&pixelShader_)) ||
            FAILED(device_->GetVertexDeclaration(&declaration_)) || FAILED(device_->GetFVF(&fvf_)) ||
            FAILED(device_->GetViewport(&viewport_)) || FAILED(device_->GetStreamSource(0, &stream_, &offset_, &stride_)) ||
            FAILED(device_->GetIndices(&indices_))) return false;
        captured_ = true;
        return true;
    }
    bool Restore() {
        if (!captured_) return true;
        captured_ = false;
        bool success = SUCCEEDED(block_->Apply());
        // Restore explicitly even when a driver/stateblock omits these. UP
        // drawing also clears stream zero, so its original binding is retained.
        if (FAILED(device_->SetVertexShader(vertexShader_))) success = false;
        if (FAILED(device_->SetPixelShader(pixelShader_))) success = false;
        if (fvf_ && FAILED(device_->SetFVF(fvf_))) success = false;
        // A custom declaration may have a valid derived FVF as well. Restore
        // the exact declaration, not just a structurally equivalent FVF cache.
        if (FAILED(device_->SetVertexDeclaration(declaration_))) success = false;
        if (FAILED(device_->SetViewport(&viewport_))) success = false;
        if (FAILED(device_->SetStreamSource(0, stream_, offset_, stride_))) success = false;
        if (FAILED(device_->SetIndices(indices_))) success = false;
        return success;
    }
private:
    IDirect3DDevice9* device_;
    IDirect3DStateBlock9* block_ = nullptr;
    IDirect3DVertexShader9* vertexShader_ = nullptr;
    IDirect3DPixelShader9* pixelShader_ = nullptr;
    IDirect3DVertexDeclaration9* declaration_ = nullptr;
    IDirect3DVertexBuffer9* stream_ = nullptr;
    IDirect3DIndexBuffer9* indices_ = nullptr;
    D3DVIEWPORT9 viewport_{};
    DWORD fvf_ = 0;
    UINT offset_ = 0;
    UINT stride_ = 0;
    bool captured_ = false;
};

bool DrawOverlay(IDirect3DDevice9* device, State& state, UINT width, UINT height) {
    if (!UploadFrame(device, state)) return false;
    DeviceState saved(device);
    if (!saved.Capture()) return false;
    D3DVIEWPORT9 viewport{0, 0, width, height, 0.0f, 1.0f};
    bool valid = SUCCEEDED(device->SetVertexShader(nullptr)) && SUCCEEDED(device->SetPixelShader(nullptr)) &&
        SUCCEEDED(device->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1)) && SUCCEEDED(device->SetViewport(&viewport)) &&
        SUCCEEDED(device->SetTexture(0, state.texture));
    const struct { D3DRENDERSTATETYPE type; DWORD value; } renderStates[] = {
        {D3DRS_ZENABLE, FALSE}, {D3DRS_ZWRITEENABLE, FALSE},
        {D3DRS_ALPHATESTENABLE, FALSE}, {D3DRS_ALPHABLENDENABLE, TRUE},
        {D3DRS_SRCBLEND, D3DBLEND_ONE}, {D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA},
        {D3DRS_BLENDOP, D3DBLENDOP_ADD}, {D3DRS_SEPARATEALPHABLENDENABLE, FALSE},
        {D3DRS_CULLMODE, D3DCULL_NONE}, {D3DRS_LIGHTING, FALSE}, {D3DRS_FOGENABLE, FALSE},
        {D3DRS_STENCILENABLE, FALSE}, {D3DRS_SCISSORTESTENABLE, FALSE},
        {D3DRS_CLIPPLANEENABLE, 0}, {D3DRS_FILLMODE, D3DFILL_SOLID},
        {D3DRS_SHADEMODE, D3DSHADE_GOURAUD}, {D3DRS_SRGBWRITEENABLE, FALSE},
        {D3DRS_WRAP0, 0}, {D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED |
            D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA}
    };
    for (const auto& entry : renderStates) {
        if (FAILED(device->SetRenderState(entry.type, entry.value))) valid = false;
    }
    const struct { DWORD stage; D3DTEXTURESTAGESTATETYPE type; DWORD value; } stageStates[] = {
        {0, D3DTSS_COLOROP, D3DTOP_SELECTARG1}, {0, D3DTSS_COLORARG1, D3DTA_TEXTURE},
        {0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1}, {0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE},
        {0, D3DTSS_TEXCOORDINDEX, 0}, {0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE},
        {0, D3DTSS_RESULTARG, D3DTA_CURRENT},
        {1, D3DTSS_COLOROP, D3DTOP_DISABLE}, {1, D3DTSS_ALPHAOP, D3DTOP_DISABLE}
    };
    for (const auto& entry : stageStates) {
        if (FAILED(device->SetTextureStageState(entry.stage, entry.type, entry.value))) valid = false;
    }
    const struct { D3DSAMPLERSTATETYPE type; DWORD value; } samplerStates[] = {
        {D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP}, {D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP},
        {D3DSAMP_MINFILTER, D3DTEXF_LINEAR}, {D3DSAMP_MAGFILTER, D3DTEXF_LINEAR},
        {D3DSAMP_MIPFILTER, D3DTEXF_NONE}, {D3DSAMP_SRGBTEXTURE, FALSE},
        {D3DSAMP_MAXMIPLEVEL, 0}, {D3DSAMP_MIPMAPLODBIAS, 0}
    };
    for (const auto& entry : samplerStates) {
        if (FAILED(device->SetSamplerState(0, entry.type, entry.value))) valid = false;
    }
    struct Vertex { float x, y, z, rhw, u, v; };
    const float right = static_cast<float>(width) - 0.5f;
    const float bottom = static_cast<float>(height) - 0.5f;
    const float u = static_cast<float>(state.imageWidth) / state.textureWidth;
    const float v = static_cast<float>(state.imageHeight) / state.textureHeight;
    const Vertex vertices[] = {
        {-0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f}, {right, -0.5f, 0.0f, 1.0f, u, 0.0f},
        {-0.5f, bottom, 0.0f, 1.0f, 0.0f, v}, {right, bottom, 0.0f, 1.0f, u, v}
    };
    HRESULT result = valid ? device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(Vertex)) : D3DERR_INVALIDCALL;
    bool restored = saved.Restore();
    return SUCCEEDED(result) && restored;
}

thread_local bool insideDeviceHook = false;
class DeviceHookScope {
public:
    DeviceHookScope() { insideDeviceHook = true; }
    ~DeviceHookScope() { insideDeviceHook = false; }
};

ULONG STDMETHODCALLTYPE HookRelease(IDirect3DDevice9* device) {
    DeviceHooks* hooks = FindDeviceHooks(device);
    if (!hooks) return 0;
    if (insideDeviceHook) return hooks->release(device);
    DeviceHookScope scope;
    State& state = GetState();
    ExclusiveLock lock(state.renderLock);
    const bool active = state.renderDevice == device;
    const std::uint64_t pending = active ? state.pending : 0;
    // Native D3D9 counts the texture's device reference. Drop it only when
    // the caller is releasing the last other reference; ordinary GetDevice /
    // Release pairs must not discard a texture and reupload the same serial.
    // The extra reference makes this count query safe even on the final call.
    if (active && state.texture) {
        const ULONG count = device->AddRef();
        hooks->release(device);
        if (count <= 3) ReleaseTexture(state);
    }
    ULONG references = hooks->release(device);
    if (active) {
        if (!references) ForgetDevice(state);
        else state.pending = pending;
    }
    return references;
}

HRESULT STDMETHODCALLTYPE HookEndScene(IDirect3DDevice9* device) {
    DeviceHooks* hooks = FindDeviceHooks(device);
    if (!hooks) return D3DERR_INVALIDCALL;
    if (insideDeviceHook) return hooks->endScene(device);
    DeviceHookScope scope;
    State& state = GetState();
    ExclusiveLock lock(state.renderLock);
    HWND window = nullptr;
    UINT width = 0, height = 0;
    const bool gameFrame = GameBackbuffer(device, window, width, height);
    if (gameFrame) {
        const auto callback = gameFrameCallback.load();
        if (callback) callback();
    }
    if (!state.enabled.load()) {
        ForgetDevice(state);
        return hooks->endScene(device);
    }
    bool drawn = false;
    if (gameFrame) {
        if (state.renderDevice != device) ForgetDevice(state);
        state.renderDevice = device;
        state.gameWindow = window;
        state.viewportWidth = width;
        state.viewportHeight = height;
        state.pending = 0;
        drawn = DrawOverlay(device, state, width, height);
    } else if (state.renderDevice == device) {
        state.pending = 0;
    }
    HRESULT result = hooks->endScene(device);
    if (gameFrame && SUCCEEDED(result) && state.enabled.load()) {
        state.lastGameFrame.store(GetTickCount64());
        if (drawn) state.pending = state.uploaded;
    }
    return result;
}

HRESULT STDMETHODCALLTYPE HookReset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* parameters) {
    DeviceHooks* hooks = FindDeviceHooks(device);
    if (!hooks) return D3DERR_INVALIDCALL;
    if (insideDeviceHook) return hooks->reset(device, parameters);
    DeviceHookScope scope;
    State& state = GetState();
    ExclusiveLock lock(state.renderLock);
    if (state.renderDevice == device) ForgetDevice(state);
    // No stateblocks, default-pool textures, or backbuffer references survive
    // this point. Failed resets leave resources empty until a later EndScene.
    return hooks->reset(device, parameters);
}

HRESULT STDMETHODCALLTYPE HookPresent(IDirect3DDevice9* device,
    const RECT* source, const RECT* destination, HWND window, const RGNDATA* dirty) {
    DeviceHooks* hooks = FindDeviceHooks(device);
    if (!hooks) return D3DERR_INVALIDCALL;
    if (insideDeviceHook) return hooks->present(device, source, destination, window, dirty);
    DeviceHookScope scope;
    State& state = GetState();
    ExclusiveLock lock(state.renderLock);
    const bool active = state.enabled.load() && state.renderDevice == device &&
        IsGameWindow(window ? window : state.gameWindow);
    const std::uint64_t serial = active ? state.pending : 0;
    HRESULT result = hooks->present(device, source, destination, window, dirty);
    if (state.renderDevice == device) state.pending = 0;
    if (result == D3D_OK && serial && state.enabled.load()) state.presented.store(serial);
    if (state.renderDevice == device && FAILED(result)) state.lastGameFrame.store(0);
    if (!state.enabled.load()) ForgetDevice(state);
    return result;
}

LRESULT CALLBACK ProbeWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    return DefWindowProcW(window, message, wParam, lParam);
}

DWORD WINAPI ProbeThread(void*) {
    State& state = GetState();
    // Keep the runtime loaded for as long as patched shared vtables exist.
    if (!state.runtime) state.runtime = LoadLibraryW(L"d3d9.dll");
    if (state.runtime && state.enabled.load()) {
        auto create = reinterpret_cast<Create9Fn>(GetProcAddress(state.runtime, "Direct3DCreate9"));
        wchar_t className[64]{};
        wsprintfW(className, L"NpcD3D9Probe_%lu", GetCurrentProcessId());
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = ProbeWindowProc;
        windowClass.hInstance = state.owner;
        windowClass.lpszClassName = className;
        ATOM registered = RegisterClassW(&windowClass);
        if (registered) {
            HWND window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, className, L"",
                WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, state.owner, nullptr);
            state.probeWindow.store(window);
            if (window && create && state.enabled.load()) {
                if (!state.probeFactory) state.probeFactory = create(D3D_SDK_VERSION);
                IDirect3D9* factory = state.probeFactory;
                if (factory) {
                    InstallFactoryHooks(factory);
                    D3DPRESENT_PARAMETERS parameters{};
                    parameters.BackBufferWidth = parameters.BackBufferHeight = 1;
                    parameters.BackBufferFormat = D3DFMT_UNKNOWN;
                    parameters.BackBufferCount = 1;
                    parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
                    parameters.hDeviceWindow = window;
                    parameters.Windowed = TRUE;
                    parameters.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
                    IDirect3DDevice9* device = nullptr;
                    HRESULT result = factory->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
                        D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED | D3DCREATE_FPU_PRESERVE,
                        &parameters, &device);
                    if (SUCCEEDED(result) && device) InstallDeviceHooks(device);
                    state.probeInstalled.store(HasFactoryHooks() || HasDeviceHooks());
                    if (device) device->Release();
                    // Keep one factory alive with the resident hooks. D3D9 can
                    // rebuild its shared factory vtable when the last factory
                    // is destroyed and a new one is subsequently constructed.
                }
            }
            if (window) DestroyWindow(window);
            state.probeWindow.store(nullptr);
            UnregisterClassW(className, state.owner);
        }
    }
    state.probing.store(false);
    return 0;
}

bool RecentFrame(const State& state) {
    const ULONGLONG last = state.lastGameFrame.load();
    return state.enabled.load() && last && GetTickCount64() - last <= kRecentFrameMs;
}
}

namespace NpcD3D {
void SetGameFrameCallback(GameFrameCallback callback) {
    gameFrameCallback.store(callback);
}

bool Initialize() {
    try {
        State& state = GetState();
        ExclusiveLock lock(state.initializationLock);
        if (!state.owner && !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&Initialize), &state.owner)) return false;
        state.enabled.store(true);
        bool installed = InstallImportHook();
        installed = HasFactoryHooks() || HasDeviceHooks() || installed;
        if (!state.probeInstalled.load() && !state.probing.exchange(true)) {
            HANDLE thread = CreateThread(nullptr, 0, ProbeThread, nullptr, 0, nullptr);
            if (thread) CloseHandle(thread);
            else state.probing.store(false);
        }
        return installed;
    } catch (...) {
        return false;
    }
}

std::uint64_t SubmitFrame(int width, int height, const unsigned char* premultipliedBGRA, int stride) {
    if (width <= 0 || height <= 0 || !premultipliedBGRA ||
        static_cast<std::uint64_t>(width) > kMaximumFrameBytes / 4) return 0;
    const std::size_t rowBytes = static_cast<std::size_t>(width) * 4;
    const std::int64_t rowStride = stride;
    const std::uint64_t strideBytes = static_cast<std::uint64_t>(rowStride < 0 ? -rowStride : rowStride);
    if (strideBytes < rowBytes || rowBytes > kMaximumFrameBytes ||
        static_cast<std::size_t>(height) > kMaximumFrameBytes / rowBytes ||
        static_cast<std::uint64_t>(height - 1) * strideBytes > static_cast<std::uint64_t>((std::numeric_limits<std::ptrdiff_t>::max)())) return 0;
    try {
        State& state = GetState();
        if (!state.enabled.load()) return 0;
        std::vector<unsigned char> pixels(rowBytes * static_cast<std::size_t>(height));
        for (int row = 0; row < height; ++row) {
            const auto source = premultipliedBGRA + static_cast<std::ptrdiff_t>(row) * stride;
            std::memcpy(pixels.data() + static_cast<std::size_t>(row) * rowBytes, source, rowBytes);
        }
        ExclusiveLock lock(state.pixelsLock);
        if (!state.enabled.load() || state.serial == (std::numeric_limits<std::uint64_t>::max)()) return 0;
        state.pixels.swap(pixels);
        state.frameWidth = width;
        state.frameHeight = height;
        return ++state.serial;
    } catch (...) {
        return 0;
    }
}

bool IsRendering() {
    return RecentFrame(GetState());
}

bool GetViewportSize(int& width, int& height) {
    State& state = GetState();
    SharedLock lock(state.renderLock);
    width = height = 0;
    if (!RecentFrame(state) || !state.viewportWidth || !state.viewportHeight) return false;
    width = static_cast<int>(state.viewportWidth);
    height = static_cast<int>(state.viewportHeight);
    return true;
}

std::uint64_t PresentedFrame() {
    return GetState().presented.load();
}

void Shutdown() {
    State& state = GetState();
    state.enabled.store(false);
    state.lastGameFrame.store(0);
    ExclusiveLock lock(state.pixelsLock);
    state.pixels.clear();
    state.frameWidth = state.frameHeight = 0;
}
}
