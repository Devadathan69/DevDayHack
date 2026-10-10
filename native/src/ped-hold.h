#pragma once
#include <cstdint>

namespace NpcPedHold {
struct Request {
    uintptr_t playerPedPointerAddress = 0;
    uintptr_t pedPoolPointerAddress = 0;
    uintptr_t standstillProcessAddress = 0;
    uintptr_t pedAddress = 0;
    uint32_t pedHandle = 0;
    unsigned long generation = 0;
    bool active = false;
};

enum class Result { Idle, Held, Failed };

// Caller owns this object and calls it only on the game thread, on a verified x86
// layout. The engine address must already be fingerprinted/resolved by the caller.
class Holder final {
public:
    Holder() noexcept = default;
    ~Holder() noexcept;
    Holder(const Holder&) = delete;
    Holder& operator=(const Holder&) = delete;
    Holder(Holder&&) = delete;
    Holder& operator=(Holder&&) = delete;

    Result Update(const Request& request) noexcept;
    bool IsHeld(unsigned long generation) const noexcept;
    // False means restoration faulted: keep the Holder alive and retry. A vanished
    // or recycled slot is forgotten without writing into its replacement.
    bool Release() noexcept;

private:
    enum class Identity { Same, Changed, Unreadable };
    struct State {
        uintptr_t pool = 0, objects = 0, flags = 0, player = 0;
        uintptr_t vtable = 0, matrix = 0;
        int size = 0, pedType = 0;
        uint16_t model = 0;
        unsigned char slotFlags = 0, freeze = 0, stay = 0;
        int move = 0, swim = 0;
        float heading = 0, goal = 0;
        float rightX = 0, rightY = 0, forwardX = 0, forwardY = 0;
        uint32_t changed = 0;
        bool held = false;
    };
    Request request_{};
    State state_{};

    bool Capture(const Request& request) noexcept;
    Identity CheckIdentity() const noexcept;
    bool Apply() noexcept;
    bool WriteFlag(uintptr_t offset, unsigned char value, uint32_t bit) noexcept;
    bool WriteField(uintptr_t address, uint32_t value, uint32_t bit, bool matrix) noexcept;
    void Forget() noexcept;
};
}
