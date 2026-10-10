#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "ped-hold.h"
#include <cmath>
#include <cstddef>
#include <cstring>

static_assert(sizeof(uintptr_t) == 4, "The supported CPed layout is x86 only");

namespace {
constexpr uintptr_t Stride = 0x7C4;
constexpr unsigned char HoldBit = 0x20;
constexpr uint32_t Freeze = 1u << 0, Stay = 1u << 1, Move = 1u << 2, Swim = 1u << 3;
constexpr uint32_t Heading = 1u << 4, Goal = 1u << 5;
constexpr uint32_t RightX = 1u << 6, RightY = 1u << 7;
constexpr uint32_t ForwardX = 1u << 8, ForwardY = 1u << 9;
constexpr uint32_t PoseBits = Move | Swim | Heading | Goal | RightX | RightY | ForwardX | ForwardY;

bool Address(uintptr_t address, size_t bytes) noexcept {
    return address >= 0x10000u && bytes != 0 &&
        static_cast<uint64_t>(address) + bytes <= 0x100000000ull;
}

// Keep SEH in functions containing no C++ objects requiring unwinding (/EHsc).
bool ReadMemory(uintptr_t address, void* output, size_t bytes) noexcept {
    if (!Address(address, bytes)) return false;
    __try {
        std::memcpy(output, reinterpret_cast<const void*>(address), bytes);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool WriteMemory(uintptr_t address, const void* input, size_t bytes) noexcept {
    if (!Address(address, bytes)) return false;
    __try {
        std::memcpy(reinterpret_cast<void*>(address), input, bytes);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

template<class T> bool Read(uintptr_t address, T& output) noexcept {
    return ReadMemory(address, &output, sizeof(output));
}

bool Executable(uintptr_t address) noexcept {
    MEMORY_BASIC_INFORMATION info{};
    if (!Address(address, 1) || !VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info)) ||
        info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    const DWORD protection = info.Protect & 0xFF;
    return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
        protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
}

struct StandstillTask {
    uint32_t vtable, parent;
    int time;
    uint32_t timerStart, timerInterval;
    unsigned char timerStarted, timerStopped, padding[2];
    unsigned char looped, useIdleStance, padding2[2];
    float blend;
};
static_assert(sizeof(StandstillTask) == 32 && offsetof(StandstillTask, time) == 8 &&
    offsetof(StandstillTask, timerStarted) == 0x14 && offsetof(StandstillTask, looped) == 0x18 &&
    offsetof(StandstillTask, blend) == 0x1C, "Standstill stack task layout changed");

bool InvokeStandstill(uintptr_t function, uintptr_t ped) noexcept {
    StandstillTask task{};
    task.looped = 1;
    task.blend = 1000.0f;
    __try {
        // The looped task normally returns false (not finished), not an error.
        reinterpret_cast<bool(__thiscall*)(void*, void*)>(function)(&task, reinterpret_cast<void*>(ped));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

struct Pose {
    uintptr_t matrix = 0;
    float x = 0, y = 0, z = 0, health = 0;
    unsigned char vehicle = 0, area = 0;
};

bool ReadPose(uintptr_t ped, Pose& pose) noexcept {
    if (!Address(ped, Stride) || !Read(ped + 0x14, pose.matrix) || !Address(pose.matrix, 0x40) ||
        !Read(pose.matrix + 0x30, pose.x) || !Read(pose.matrix + 0x34, pose.y) ||
        !Read(pose.matrix + 0x38, pose.z) || !Read(ped + 0x540, pose.health) ||
        !Read(ped + 0x46D, pose.vehicle) || !Read(ped + 0x2F, pose.area)) return false;
    return std::isfinite(pose.x) && std::isfinite(pose.y) && std::isfinite(pose.z) &&
        std::isfinite(pose.health) && pose.health > 0 && pose.health <= 100000 && !(pose.vehicle & 1) &&
        pose.x > -4000 && pose.x < 4000 && pose.y > -4000 && pose.y < 4000 &&
        pose.z > -200 && pose.z < 1200;
}

bool InRange(const Pose& player, const Pose& ped) noexcept {
    const float dx = player.x - ped.x, dy = player.y - ped.y, dz = player.z - ped.z;
    return player.area == ped.area && std::fabs(dz) <= 2.0f && dx * dx + dy * dy + dz * dz <= 25.0f;
}

uint32_t Bits(float value) noexcept {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}
}

namespace NpcPedHold {
Holder::~Holder() noexcept { Release(); }

void Holder::Forget() noexcept {
    state_ = {};
    request_ = {};
}

Holder::Identity Holder::CheckIdentity() const noexcept {
    uintptr_t pool = 0, objects = 0, flags = 0, player = 0, vtable = 0;
    int size = 0, pedType = 0;
    uint16_t model = 0;
    unsigned char slotFlags = 0;
    if (!Read(request_.pedPoolPointerAddress, pool)) return Identity::Unreadable;
    if (!pool || pool != state_.pool) return Identity::Changed;
    if (!Address(pool, 12) || !Read(pool, objects) || !Read(pool + 4, flags) || !Read(pool + 8, size))
        return Identity::Unreadable;
    if (objects != state_.objects || flags != state_.flags || size != state_.size || size < 1 || size > 512)
        return Identity::Changed;
    const uint32_t slot = request_.pedHandle >> 8;
    if (slot >= static_cast<uint32_t>(size) ||
        static_cast<uint64_t>(objects) + static_cast<uint64_t>(slot) * Stride != request_.pedAddress)
        return Identity::Changed;
    if (!Read(flags + slot, slotFlags)) return Identity::Unreadable;
    if ((slotFlags & 0x80) || slotFlags != state_.slotFlags ||
        static_cast<uint32_t>(slotFlags) != (request_.pedHandle & 0xFF)) return Identity::Changed;
    if (!Read(request_.playerPedPointerAddress, player) || !Read(request_.pedAddress, vtable) ||
        !Read(request_.pedAddress + 0x22, model) || !Read(request_.pedAddress + 0x598, pedType))
        return Identity::Unreadable;
    if (request_.pedAddress == player || request_.pedAddress == state_.player ||
        vtable != state_.vtable || model != state_.model || pedType != state_.pedType)
        return Identity::Changed;
    return Identity::Same;
}

bool Holder::Capture(const Request& request) noexcept {
    State saved{};
    uintptr_t firstVirtual = 0;
    unsigned char flag = 0;
    if (!Address(request.pedAddress, Stride) || !Executable(request.standstillProcessAddress) ||
        !Read(request.playerPedPointerAddress, saved.player) || !saved.player || saved.player == request.pedAddress ||
        !Read(request.pedPoolPointerAddress, saved.pool) || !Address(saved.pool, 12) ||
        !Read(saved.pool, saved.objects) || !Read(saved.pool + 4, saved.flags) || !Read(saved.pool + 8, saved.size) ||
        saved.size < 1 || saved.size > 512 || !Address(saved.objects, static_cast<size_t>(saved.size) * Stride) ||
        !Address(saved.flags, static_cast<size_t>(saved.size))) return false;
    const uint32_t slot = request.pedHandle >> 8;
    if (slot >= static_cast<uint32_t>(saved.size) ||
        static_cast<uint64_t>(saved.objects) + static_cast<uint64_t>(slot) * Stride != request.pedAddress ||
        !Read(saved.flags + slot, saved.slotFlags) || (saved.slotFlags & 0x80) ||
        static_cast<uint32_t>(saved.slotFlags) != (request.pedHandle & 0xFF) ||
        !Read(request.pedAddress, saved.vtable) || !Read(saved.vtable, firstVirtual) || !Executable(firstVirtual) ||
        !Read(request.pedAddress + 0x22, saved.model) || saved.model < 1 || saved.model > 311 ||
        !Read(request.pedAddress + 0x598, saved.pedType) || saved.pedType < 4 || saved.pedType > 31) return false;
    Pose player{}, ped{};
    if (!ReadPose(saved.player, player) || !ReadPose(request.pedAddress, ped) || !InRange(player, ped)) return false;
    saved.matrix = ped.matrix;
    if (!Read(request.pedAddress + 0x41, flag)) return false;
    saved.freeze = flag & HoldBit;
    if (!Read(request.pedAddress + 0x46E, flag)) return false;
    saved.stay = flag & HoldBit;
    if (!Read(request.pedAddress + 0x534, saved.move) || !Read(request.pedAddress + 0x538, saved.swim) ||
        !Read(request.pedAddress + 0x558, saved.heading) || !Read(request.pedAddress + 0x55C, saved.goal) ||
        !Read(saved.matrix, saved.rightX) || !Read(saved.matrix + 4, saved.rightY) ||
        !Read(saved.matrix + 0x10, saved.forwardX) || !Read(saved.matrix + 0x14, saved.forwardY) ||
        !std::isfinite(saved.heading) || !std::isfinite(saved.goal) ||
        !std::isfinite(saved.rightX) || !std::isfinite(saved.rightY) ||
        !std::isfinite(saved.forwardX) || !std::isfinite(saved.forwardY)) return false;
    request_ = request;
    state_ = saved;
    if (CheckIdentity() != Identity::Same) { Forget(); return false; }
    return true;
}

bool Holder::WriteFlag(uintptr_t offset, unsigned char value, uint32_t bit) noexcept {
    if (CheckIdentity() != Identity::Same) return false;
    unsigned char current = 0;
    if (!Read(request_.pedAddress + offset, current)) return false;
    const unsigned char updated = static_cast<unsigned char>((current & ~HoldBit) | (value & HoldBit));
    if (updated == current) return true;
    state_.changed |= bit;
    return WriteMemory(request_.pedAddress + offset, &updated, sizeof(updated));
}

bool Holder::WriteField(uintptr_t address, uint32_t value, uint32_t bit, bool matrix) noexcept {
    if (CheckIdentity() != Identity::Same) return false;
    if (matrix) {
        uintptr_t currentMatrix = 0;
        if (!Read(request_.pedAddress + 0x14, currentMatrix) || currentMatrix != state_.matrix) return false;
    }
    uint32_t current = 0;
    if (!Read(address, current)) return false;
    if (current == value) return true;
    // Retain ownership even if a write faults after modifying only some bytes.
    state_.changed |= bit;
    return WriteMemory(address, &value, sizeof(value));
}

bool Holder::Apply() noexcept {
    Pose player{}, ped{};
    uintptr_t playerAddress = 0;
    float rx = 0, ry = 0, fx = 0, fy = 0;
    if (CheckIdentity() != Identity::Same || !Executable(request_.standstillProcessAddress) ||
        !Read(request_.playerPedPointerAddress, playerAddress) || playerAddress != state_.player ||
        !ReadPose(playerAddress, player) || !ReadPose(request_.pedAddress, ped) ||
        ped.matrix != state_.matrix || !InRange(player, ped) ||
        !Read(ped.matrix, rx) || !Read(ped.matrix + 4, ry) ||
        !Read(ped.matrix + 0x10, fx) || !Read(ped.matrix + 0x14, fy) ||
        !std::isfinite(rx) || !std::isfinite(ry) || !std::isfinite(fx) || !std::isfinite(fy) ||
        rx * rx + ry * ry < 0.0001f || rx * rx + ry * ry > 4.0f || fx * fx + fy * fy > 4.0f) return false;
    const float dx = player.x - ped.x, dy = player.y - ped.y;
    float heading = 0;
    if (!Read(request_.pedAddress + 0x558, heading) || !std::isfinite(heading)) return false;
    if (dx * dx + dy * dy > 0.000001f) heading = std::atan2(-dx, dy);
    // Rotate only XY: preserve basis lengths, slope/Z, matrix flags and position.
    const float rotation = heading - std::atan2(ry, rx);
    const float c = std::cos(rotation), s = std::sin(rotation);
    if (!WriteFlag(0x41, HoldBit, Freeze) || !WriteFlag(0x46E, HoldBit, Stay) ||
        CheckIdentity() != Identity::Same) return false;
    // ProcessPed can change these two states even when its invocation faults.
    state_.changed |= Move | Swim;
    if (!InvokeStandstill(request_.standstillProcessAddress, request_.pedAddress) ||
        !ReadPose(request_.pedAddress, ped) || ped.matrix != state_.matrix || !InRange(player, ped)) return false;
    return WriteField(request_.pedAddress + 0x534, 1, Move, false) &&
        WriteField(request_.pedAddress + 0x538, 1, Swim, false) &&
        WriteField(request_.pedAddress + 0x558, Bits(heading), Heading, false) &&
        WriteField(request_.pedAddress + 0x55C, Bits(heading), Goal, false) &&
        WriteField(ped.matrix, Bits(rx * c - ry * s), RightX, true) &&
        WriteField(ped.matrix + 4, Bits(rx * s + ry * c), RightY, true) &&
        WriteField(ped.matrix + 0x10, Bits(fx * c - fy * s), ForwardX, true) &&
        WriteField(ped.matrix + 0x14, Bits(fx * s + fy * c), ForwardY, true);
}

bool Holder::Release() noexcept {
    state_.held = false;
    if (!state_.pool) return true;
    const Identity identity = CheckIdentity();
    if (identity == Identity::Changed) { Forget(); return true; }
    if (identity == Identity::Unreadable) return false;
    float health = 0;
    unsigned char vehicle = 0;
    uintptr_t matrix = 0;
    if (!Read(request_.pedAddress + 0x540, health) || !std::isfinite(health) || health <= 0 ||
        !Read(request_.pedAddress + 0x46D, vehicle) || (vehicle & 1) ||
        !Read(request_.pedAddress + 0x14, matrix) || matrix != state_.matrix) state_.changed &= ~PoseBits;
    if ((state_.changed & Freeze) && WriteFlag(0x41, state_.freeze, Freeze)) state_.changed &= ~Freeze;
    if ((state_.changed & Stay) && WriteFlag(0x46E, state_.stay, Stay)) state_.changed &= ~Stay;
    struct Field { uintptr_t address; uint32_t value, bit; bool matrix; };
    const Field fields[] = {
        {request_.pedAddress + 0x534, static_cast<uint32_t>(state_.move), Move, false},
        {request_.pedAddress + 0x538, static_cast<uint32_t>(state_.swim), Swim, false},
        {request_.pedAddress + 0x558, Bits(state_.heading), Heading, false},
        {request_.pedAddress + 0x55C, Bits(state_.goal), Goal, false},
        {state_.matrix, Bits(state_.rightX), RightX, true},
        {state_.matrix + 4, Bits(state_.rightY), RightY, true},
        {state_.matrix + 0x10, Bits(state_.forwardX), ForwardX, true},
        {state_.matrix + 0x14, Bits(state_.forwardY), ForwardY, true}
    };
    for (const Field& field : fields) {
        if ((state_.changed & field.bit) && WriteField(field.address, field.value, field.bit, field.matrix))
            state_.changed &= ~field.bit;
    }
    if (state_.changed) return false;
    Forget();
    return true;
}

Result Holder::Update(const Request& request) noexcept {
    const bool sameRequest = state_.pool && request.generation == request_.generation &&
        request.pedAddress == request_.pedAddress && request.pedHandle == request_.pedHandle &&
        request.playerPedPointerAddress == request_.playerPedPointerAddress &&
        request.pedPoolPointerAddress == request_.pedPoolPointerAddress &&
        request.standstillProcessAddress == request_.standstillProcessAddress;
    if (!request.active) return Release() ? Result::Idle : Result::Failed;
    if (state_.pool && (!sameRequest || !state_.held) && !Release()) return Result::Failed;
    if (!state_.pool && !Capture(request)) return Result::Failed;
    if (!Apply()) { Release(); return Result::Failed; }
    state_.held = true;
    return Result::Held;
}

bool Holder::IsHeld(unsigned long generation) const noexcept {
    return state_.held && request_.generation == generation;
}
}
