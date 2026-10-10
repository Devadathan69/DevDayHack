// Standalone GPU smoke host; run from an x86 Visual Studio developer shell:
// cl /nologo /MT /EHsc /std:c++17 /W4 /DUNICODE /D_UNICODE /Fe:native\build\d3d-overlay-smoke.exe native\test\d3d-overlay.test.cpp native\src\d3d-overlay.cpp /link /MACHINE:X86 d3d9.lib user32.lib
// Add /DOVERLAY_FALLBACK_ONLY to verify the no-IAT fallback. A visible,
// nonactivating 64x48 window is used; no game or network is needed.
#include "..\src\d3d-overlay.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <atomic>
#include <climits>
#include <cstdio>
#include <cstring>
#include <vector>

static int failures = 0;
static int checks = 0;
static std::atomic<unsigned> gameCallbacks{0};
static std::atomic<DWORD> callbackThread{0};
static void OnGameFrame() {
    callbackThread.store(GetCurrentThreadId());
    gameCallbacks.fetch_add(1);
}
static void Check(bool condition, const char* message) {
    ++checks;
    if (!condition) { std::printf("FAIL: %s\n", message); ++failures; }
}
static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    return DefWindowProcW(hwnd, message, w, l);
}
static std::atomic<std::uint64_t> producerSerial{0};
static DWORD WINAPI ProduceFrames(void*) {
    const unsigned char pixels[] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255};
    for (int i = 0; i < 30; ++i) {
        producerSerial.store(NpcD3D::SubmitFrame(3, 1, pixels, 12));
        Sleep(1);
    }
    return 0;
}
static bool Pixel(IDirect3DDevice9* device, UINT x, UINT y, unsigned expected) {
    IDirect3DSurface9* backbuffer = nullptr;
    IDirect3DSurface9* readable = nullptr;
    D3DSURFACE_DESC desc{};
    D3DLOCKED_RECT locked{};
    bool success = SUCCEEDED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer)) &&
        SUCCEEDED(backbuffer->GetDesc(&desc)) &&
        SUCCEEDED(device->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &readable, nullptr)) &&
        SUCCEEDED(device->GetRenderTargetData(backbuffer, readable)) &&
        SUCCEEDED(readable->LockRect(&locked, nullptr, D3DLOCK_READONLY));
    unsigned actual = 0;
    if (success) {
        std::memcpy(&actual, static_cast<unsigned char*>(locked.pBits) + y * locked.Pitch + x * 4, 4);
        readable->UnlockRect();
        success = (actual & 0xffffff) == expected;
        if (!success) std::printf("pixel %u,%u: actual %06x expected %06x\n", x, y, actual & 0xffffff, expected);
    }
    if (readable) readable->Release();
    if (backbuffer) backbuffer->Release();
    return success;
}
int main() {
    unsigned char red[] = {0, 0, 128, 128};
    Check(!NpcD3D::IsRendering() && !NpcD3D::PresentedFrame(), "initial state is not rendering/presented");
    Check(!NpcD3D::SubmitFrame(1, 1, red, 4), "submission rejected before initialization");
    bool initialized = NpcD3D::Initialize();
#ifdef OVERLAY_FALLBACK_ONLY
    Check(!initialized, "no immediate hook without a D3D9 import");
#else
    Check(initialized, "Direct3DCreate9 IAT hook installed immediately");
#endif
    Sleep(600);
    Check(!NpcD3D::IsRendering() && !NpcD3D::PresentedFrame(), "hidden probe never claims a game frame");
    int width = 9, height = 9;
    Check(!NpcD3D::GetViewportSize(width, height) && !width && !height, "probe never claims viewport");
    Check(!NpcD3D::SubmitFrame(INT_MAX, 1, red, INT_MAX), "overflow width rejected");
    Check(!NpcD3D::SubmitFrame(2, 1, red, 4), "short stride rejected");
    Check(!NpcD3D::SubmitFrame(1, 2, red, INT_MIN), "overflow signed row offset rejected");
    Check(!NpcD3D::SubmitFrame(1, 1, nullptr, 4), "null pixels rejected");
    Check(!NpcD3D::SubmitFrame(0, 1, red, 4), "zero width rejected");
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.hInstance = instance; wc.lpfnWndProc = WindowProc; wc.lpszClassName = L"NpcD3DOverlayValidation";
    Check(RegisterClassW(&wc) != 0, "register validation window");
    HWND window = CreateWindowExW(WS_EX_NOACTIVATE, wc.lpszClassName, L"Overlay validation", WS_POPUP | WS_VISIBLE,
        GetSystemMetrics(SM_CXSCREEN) - 64, GetSystemMetrics(SM_CYSCREEN) - 48, 64, 48, nullptr, nullptr, instance, nullptr);
    Check(window != nullptr, "create nonactivating validation window");
    IDirect3D9* factory = nullptr;
#ifdef OVERLAY_FALLBACK_ONLY
    HMODULE runtime = LoadLibraryW(L"d3d9.dll");
    auto create = reinterpret_cast<IDirect3D9* (WINAPI*)(UINT)>(GetProcAddress(runtime, "Direct3DCreate9"));
    factory = create(D3D_SDK_VERSION);
    Check(NpcD3D::Initialize(), "fallback installed shared factory hooks without a D3D9 IAT import");
#else
    factory = Direct3DCreate9(D3D_SDK_VERSION);
#endif
    Check(factory != nullptr, "create D3D9 factory");
    if (!factory || !window) return 2;
    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow = window;
    pp.BackBufferWidth = 64; pp.BackBufferHeight = 48; pp.BackBufferFormat = D3DFMT_UNKNOWN;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    NpcD3D::SetGameFrameCallback(OnGameFrame);
    HWND helper = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"",
        WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance, nullptr);
    IDirect3DDevice9* helperDevice = nullptr;
    D3DPRESENT_PARAMETERS helperParameters = pp;
    helperParameters.hDeviceWindow = helper;
    helperParameters.BackBufferWidth = helperParameters.BackBufferHeight = 1;
    Check(helper && SUCCEEDED(factory->CreateDevice(0, D3DDEVTYPE_HAL, helper,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &helperParameters, &helperDevice)),
        "create explicitly hidden helper device");
    if (helperDevice) {
        Check(SUCCEEDED(helperDevice->BeginScene()) && SUCCEEDED(helperDevice->EndScene()) &&
            SUCCEEDED(helperDevice->Present(nullptr, nullptr, nullptr, nullptr)), "helper executes real D3D9 frames");
        Check(!NpcD3D::IsRendering() && !NpcD3D::PresentedFrame() && !NpcD3D::GetViewportSize(width, height),
            "hidden helper frames are not game rendering or delivery");
        helperDevice->Release();
    }
    if (helper) DestroyWindow(helper);
    Check(gameCallbacks.load() == 0, "hidden helper does not execute game-state callback");
    IDirect3DDevice9* device = nullptr;
    Check(SUCCEEDED(factory->CreateDevice(0, D3DDEVTYPE_HAL, window,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device)), "create real validation device");
    if (!device) return 2;
    Check(SUCCEEDED(device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xff0000ff, 1, 0)), "clear blue background");
    D3DVIEWPORT9 viewport{7, 8, 16, 12, 0.2f, 0.8f};
    RECT scissor{8, 9, 12, 14};
    device->SetViewport(&viewport);
    device->SetScissorRect(&scissor);
    device->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    device->SetRenderState(D3DRS_ZENABLE, TRUE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
    device->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_BLUE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_CW);
    device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    IDirect3DVertexBuffer9* vb = nullptr;
    IDirect3DIndexBuffer9* ib = nullptr;
    IDirect3DTexture9* gameTexture = nullptr;
    device->CreateVertexBuffer(128, 0, 0, D3DPOOL_DEFAULT, &vb, nullptr);
    device->CreateIndexBuffer(16, 0, D3DFMT_INDEX16, D3DPOOL_DEFAULT, &ib, nullptr);
    device->CreateTexture(2, 2, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &gameTexture, nullptr);
    device->SetStreamSource(0, vb, 16, 16); device->SetIndices(ib); device->SetTexture(0, gameTexture);
    const auto first = NpcD3D::SubmitFrame(1, 1, red, 4);
    Check(first != 0, "submit premultiplied red");
    std::memset(red, 0, sizeof(red));
    Check(SUCCEEDED(device->BeginScene()) && SUCCEEDED(device->EndScene()), "draw first frame");
    Check(gameCallbacks.load() == 1 && callbackThread.load() == GetCurrentThreadId(),
        "game-state callback executes on the device-owning thread");
    Check(NpcD3D::IsRendering(), "real successful EndScene observed");
    Check(NpcD3D::GetViewportSize(width, height) && width == 64 && height == 48, "full backbuffer size, not temporary viewport");
    Check(!NpcD3D::PresentedFrame(), "EndScene alone is not delivery");
    Check(Pixel(device, 2, 2, 0x80007f) && Pixel(device, 60, 44, 0x80007f), "copied premultiplied pixels stretch across full backbuffer");
    D3DVIEWPORT9 restored{};
    RECT restoredScissor{};
    DWORD value = 0;
    device->GetViewport(&restored); device->GetScissorRect(&restoredScissor);
    Check(std::memcmp(&viewport, &restored, sizeof(viewport)) == 0, "viewport restored");
    Check(std::memcmp(&scissor, &restoredScissor, sizeof(scissor)) == 0, "scissor restored");
    device->GetFVF(&value); Check(value == (D3DFVF_XYZ | D3DFVF_DIFFUSE), "FVF restored");
    device->GetRenderState(D3DRS_COLORWRITEENABLE, &value); Check(value == D3DCOLORWRITEENABLE_BLUE, "color write restored");
    device->GetRenderState(D3DRS_CULLMODE, &value); Check(value == D3DCULL_CW, "cull restored");
    device->GetRenderState(D3DRS_SCISSORTESTENABLE, &value); Check(value == TRUE, "scissor enable restored");
    device->GetSamplerState(0, D3DSAMP_MINFILTER, &value); Check(value == D3DTEXF_POINT, "sampler restored");
    IDirect3DVertexBuffer9* restoredVB = nullptr;
    IDirect3DIndexBuffer9* restoredIB = nullptr;
    IDirect3DBaseTexture9* restoredTexture = nullptr;
    UINT offset = 0, stride = 0;
    device->GetStreamSource(0, &restoredVB, &offset, &stride);
    device->GetIndices(&restoredIB); device->GetTexture(0, &restoredTexture);
    Check(restoredVB == vb && offset == 16 && stride == 16 && restoredIB == ib && restoredTexture == gameTexture,
        "UP stream, indices and texture restored");
    if (restoredVB) restoredVB->Release(); if (restoredIB) restoredIB->Release(); if (restoredTexture) restoredTexture->Release();
    Check(SUCCEEDED(device->Present(nullptr, nullptr, nullptr, nullptr)) && NpcD3D::PresentedFrame() == first, "successful Present acknowledges exact serial");
    const DWORD vertexBytecode[] = {0xfffe0200, 0x0200001f, 0x80000000, 0x900f0000, 0x02000001, 0xc00f0000, 0x90e40000, 0x0000ffff};
    const DWORD pixelBytecode[] = {0xffff0200, 0x02000001, 0x800f0800, 0xa0e40000, 0x0000ffff};
    IDirect3DVertexShader9* vs = nullptr;
    IDirect3DPixelShader9* ps = nullptr;
    Check(SUCCEEDED(device->CreateVertexShader(vertexBytecode, &vs)), "create validation vertex shader");
    Check(SUCCEEDED(device->CreatePixelShader(pixelBytecode, &ps)), "create validation pixel shader");
    device->SetVertexShader(vs); device->SetPixelShader(ps);
    const D3DVERTEXELEMENT9 elements[] = {{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0}, D3DDECL_END()};
    IDirect3DVertexDeclaration9* declaration = nullptr;
    device->CreateVertexDeclaration(elements, &declaration); device->SetVertexDeclaration(declaration);
    const unsigned char rows[] = {0, 255, 0, 255, 0, 0, 255, 255};
    const auto second = NpcD3D::SubmitFrame(1, 2, rows + 4, -4);
    Check(second > first, "negative stride submission has increasing serial");
    Check(SUCCEEDED(device->BeginScene()) && SUCCEEDED(device->EndScene()), "draw shader-state frame");
    IDirect3DVertexShader9* restoredVS = nullptr; IDirect3DPixelShader9* restoredPS = nullptr;
    IDirect3DVertexDeclaration9* restoredDeclaration = nullptr;
    device->GetVertexShader(&restoredVS); device->GetPixelShader(&restoredPS); device->GetVertexDeclaration(&restoredDeclaration);
    Check(restoredVS == vs && restoredPS == ps && restoredDeclaration == declaration, "shaders and non-FVF declaration restored");
    if (restoredVS) restoredVS->Release(); if (restoredPS) restoredPS->Release(); if (restoredDeclaration) restoredDeclaration->Release();
    Check(Pixel(device, 32, 2, 0xff0000) && Pixel(device, 32, 44, 0x00ff00), "signed stride preserves top-down row order");
    Check(SUCCEEDED(device->Present(nullptr, nullptr, nullptr, nullptr)) && NpcD3D::PresentedFrame() == second, "second delivery acknowledged");
    device->SetVertexShader(nullptr); device->SetPixelShader(nullptr); device->SetTexture(0, nullptr);
    device->SetStreamSource(0, nullptr, 0, 0); device->SetIndices(nullptr); device->SetVertexDeclaration(nullptr);
    if (vs) vs->Release(); if (ps) ps->Release(); if (declaration) declaration->Release();
    if (vb) vb->Release(); if (ib) ib->Release(); if (gameTexture) gameTexture->Release();
    pp.BackBufferWidth = 80; pp.BackBufferHeight = 60;
    Check(SUCCEEDED(device->Reset(&pp)), "Reset succeeds with overlay default-pool resources released");
    Check(!NpcD3D::IsRendering() && !NpcD3D::GetViewportSize(width, height), "Reset invalidates old viewport and liveness");
    Check(SUCCEEDED(device->BeginScene()) && SUCCEEDED(device->EndScene()) && SUCCEEDED(device->Present(nullptr, nullptr, nullptr, nullptr)),
        "retained CPU frame reuploaded after Reset");
    Check(NpcD3D::GetViewportSize(width, height) && width == 80 && height == 60, "resized viewport observed");
    Check(NpcD3D::PresentedFrame() == second, "Reset reupload retains frame serial");
    HANDLE producer = CreateThread(nullptr, 0, ProduceFrames, nullptr, 0, nullptr);
    Check(producer != nullptr, "start concurrent frame producer");
    if (producer) {
        bool rendered = true;
        for (int i = 0; i < 30; ++i) {
            if (FAILED(device->BeginScene()) || FAILED(device->EndScene()) ||
                FAILED(device->Present(nullptr, nullptr, nullptr, nullptr))) rendered = false;
            Sleep(1);
        }
        Check(rendered, "render while another thread submits replacement pixels");
        Check(WaitForSingleObject(producer, 2000) == WAIT_OBJECT_0, "producer completes within two seconds");
        CloseHandle(producer);
        Check(SUCCEEDED(device->BeginScene()) && SUCCEEDED(device->EndScene()) &&
            SUCCEEDED(device->Present(nullptr, nullptr, nullptr, nullptr)) && producerSerial.load() > second &&
            NpcD3D::PresentedFrame() == producerSerial.load(), "latest concurrent submission is delivered");
    }
    Sleep(1100);
    Check(!NpcD3D::IsRendering() && !NpcD3D::GetViewportSize(width, height), "liveness expires without game frames");
    const unsigned callbacksBeforeShutdown = gameCallbacks.load();
    NpcD3D::Shutdown();
    Check(!NpcD3D::SubmitFrame(1, 1, rows, 4), "Shutdown rejects submission");
    Check(SUCCEEDED(device->BeginScene()) && SUCCEEDED(device->EndScene()) && SUCCEEDED(device->Present(nullptr, nullptr, nullptr, nullptr)),
        "disabled hooks still forward original calls");
    Check(gameCallbacks.load() == callbacksBeforeShutdown + 1,
        "pending game-state restoration still runs after renderer shutdown");
    NpcD3D::SetGameFrameCallback(nullptr);
    const unsigned callbacksAfterRemoval = gameCallbacks.load();
    Check(SUCCEEDED(device->BeginScene()) && SUCCEEDED(device->EndScene()) &&
        gameCallbacks.load() == callbacksAfterRemoval, "callback removal prevents further game-state calls");
    Check(!NpcD3D::IsRendering(), "Shutdown stays nonrendering");
    Check(NpcD3D::Initialize(), "resident hooks can be re-enabled");
    const auto third = NpcD3D::SubmitFrame(1, 1, rows, 4);
    Check(third > second, "serial remains monotonic after Shutdown");
    Check(SUCCEEDED(device->BeginScene()) && SUCCEEDED(device->EndScene()) && SUCCEEDED(device->Present(nullptr, nullptr, nullptr, nullptr)) &&
        NpcD3D::PresentedFrame() == third, "delivery after re-enable");
    device->AddRef();
    Check(device->Release() != 0, "ordinary device reference pair preserves a live device");
    Check(device->Release() == 0, "active final device Release frees overlay resources too");
    Check(!NpcD3D::IsRendering() && !NpcD3D::GetViewportSize(width, height), "final Release clears liveness and viewport");
    NpcD3D::Shutdown();
    factory->Release();
    DestroyWindow(window); UnregisterClassW(wc.lpszClassName, instance);
    std::printf("%s: %d checks, %d failures\n", failures ? "FAILED" : "PASSED", checks, failures);
    return failures ? 1 : 0;
}
