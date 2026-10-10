#pragma once

#include <cstdint>

namespace NpcD3D {
using GameFrameCallback = void (*)();
// Runs on the game's device thread before the overlay draw, including after
// Shutdown so game-owned state can be restored. Never block on UI/network here.
void SetGameFrameCallback(GameFrameCallback callback);

// Call outside DllMain. Installs the executable's Direct3DCreate9 import hook
// immediately and starts a hidden-device fallback on a separate thread.
// The fallback patches shared factory/device vtables; an already-created
// device with its own private heap vtable cannot be discovered this way.
// True means a factory hook or complete shared device hooks are installed;
// false does not preclude the asynchronous fallback succeeding later.
bool Initialize();

// Copies a top-down premultiplied BGRA frame; no caller buffer is retained.
// stride is signed and points from one logical row to the next. Returns zero
// for invalid input/allocation failure, otherwise a monotonically increasing
// serial. Only the newest submission is retained; superseded frames may skip.
std::uint64_t SubmitFrame(int width, int height,
    const unsigned char* premultipliedBGRA, int stride);

// Reports a successful EndScene on the game's primary backbuffer within the
// last second, not merely installation of a hook or creation of the probe.
bool IsRendering();

// Returns that recent primary backbuffer's full drawable size. The overlay
// stretches to this size, independently of the game's temporary viewport.
bool GetViewportSize(int& width, int& height);

// Latest serial drawn in a successful EndScene and subsequently accepted by
// a successful primary device Present. Equality confirms that submission;
// a greater serial only proves a newer frame was presented. This does not
// promise physical scanout or acknowledge separate swap-chain Present calls.
std::uint64_t PresentedFrame();

// Disables rendering immediately. GPU resources are released on the next
// device callback (or Reset), not on the caller's possibly non-render thread.
// Hooks remain installed, forwarding to originals; their module is pinned and
// one probe factory is retained for process lifetime to keep shared tables live.
// Do not unload this module; Initialize can enable it again.
void Shutdown();
}
