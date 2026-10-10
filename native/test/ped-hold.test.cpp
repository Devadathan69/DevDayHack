#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "..\src\ped-hold.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <type_traits>

using namespace NpcPedHold;
static_assert(sizeof(uintptr_t) == 4, "Run these fixtures with the x86 toolchain");
static_assert(!std::is_copy_constructible<Holder>::value && !std::is_move_constructible<Holder>::value,
    "A hold must have exactly one owner");

namespace {
constexpr size_t Stride = 0x7C4;
unsigned calls = 0;
enum class MockMode { Normal, Fault, ReadOnlyFault, Kill, EnterVehicle, Recycle };
MockMode mode = MockMode::Normal;
uintptr_t faultPage = 0;
unsigned char* recycleFlag = nullptr;
unsigned allocations = 0;

template<class T> T Get(uintptr_t base, size_t offset = 0) {
    T value{};
    std::memcpy(&value, reinterpret_cast<const void*>(base + offset), sizeof(value));
    return value;
}
template<class T> void Put(uintptr_t base, size_t offset, T value) {
    std::memcpy(reinterpret_cast<void*>(base + offset), &value, sizeof(value));
}
bool Near(float a, float b) { return std::fabs(a - b) < 0.00002f; }

// __fastcall supplies the ECX task and stack ped argument of the production
// __thiscall function; the unused EDX parameter needs no value from the caller.
bool __fastcall MockStandstill(void* task, void*, void* ped) {
    ++calls;
    const uintptr_t t = reinterpret_cast<uintptr_t>(task), p = reinterpret_cast<uintptr_t>(ped);
    assert(Get<uint32_t>(t) == 0 && Get<uint32_t>(t, 4) == 0 && Get<int>(t, 8) == 0);
    assert(Get<uint32_t>(t, 0xC) == 0 && Get<uint32_t>(t, 0x10) == 0);
    assert(Get<unsigned char>(t, 0x14) == 0 && Get<unsigned char>(t, 0x15) == 0);
    assert(Get<unsigned char>(t, 0x18) == 1 && Get<unsigned char>(t, 0x19) == 0);
    assert(Get<float>(t, 0x1C) == 1000.0f);
    Put<unsigned char>(t, 0x14, 1);
    Put<int>(p, 0x534, 6);
    Put<int>(p, 0x538, 7);
    if (mode == MockMode::ReadOnlyFault) {
        DWORD old = 0;
        assert(VirtualProtect(reinterpret_cast<void*>(faultPage), 4096, PAGE_READONLY, &old));
    }
    if (mode == MockMode::Kill) Put<float>(p, 0x540, 0);
    if (mode == MockMode::EnterVehicle) Put<unsigned char>(p, 0x46D, 1);
    if (mode == MockMode::Recycle) ++*recycleFlag;
    if (mode == MockMode::Fault || mode == MockMode::ReadOnlyFault)
        RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
    return false;
}
uintptr_t vtable[] = {reinterpret_cast<uintptr_t>(&MockStandstill)};

uintptr_t Allocate(size_t bytes) {
    void* memory = VirtualAlloc(nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    assert(memory);
    ++allocations;
    return reinterpret_cast<uintptr_t>(memory);
}
void Free(uintptr_t memory) {
    assert(VirtualFree(reinterpret_cast<void*>(memory), 0, MEM_RELEASE));
    --allocations;
}
void Protect(uintptr_t memory, DWORD protection) {
    DWORD old = 0;
    assert(VirtualProtect(reinterpret_cast<void*>(memory), 4096, protection, &old));
}

struct Fixture {
    struct Pool { uintptr_t objects, flags; int size; };
    uintptr_t objects = Allocate(Stride * 3);
    uintptr_t matrices = Allocate(4096);
    uintptr_t player = Allocate(4096);
    unsigned char flags[3]{1, 2, 3};
    Pool pool{objects, reinterpret_cast<uintptr_t>(flags), 3};
    uintptr_t poolGlobal = reinterpret_cast<uintptr_t>(&pool);
    uintptr_t playerGlobal = player;

    Fixture() {
        mode = MockMode::Normal;
        calls = 0;
        faultPage = 0;
        recycleFlag = nullptr;
        std::memset(reinterpret_cast<void*>(objects), 0xA5, Stride * 3);
        for (unsigned i = 0; i < 3; ++i) Init(Ped(i), Matrix(i), 2.0f + i * 0.5f, 0);
        Init(player, matrices + 0x100, 0, 0);
        Put<uint16_t>(player, 0x22, 0);
        Put<int>(player, 0x598, 0);
    }
    ~Fixture() {
        Free(player);
        Free(matrices);
        Free(objects);
    }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    uintptr_t Ped(unsigned slot = 1) const { return objects + slot * Stride; }
    uintptr_t Matrix(unsigned slot = 1) const { return matrices + slot * 0x40; }
    void Init(uintptr_t ped, uintptr_t matrix, float x, float y) {
        Put<uintptr_t>(ped, 0, reinterpret_cast<uintptr_t>(vtable));
        Put<uintptr_t>(ped, 0x14, matrix);
        Put<uint16_t>(ped, 0x22, 7);
        Put<unsigned char>(ped, 0x2F, 0);
        Put<unsigned char>(ped, 0x41, 0x04);
        Put<unsigned char>(ped, 0x46D, 0);
        Put<unsigned char>(ped, 0x46E, 0x02);
        Put<int>(ped, 0x534, 3);
        Put<int>(ped, 0x538, 4);
        Put<float>(ped, 0x540, 100);
        Put<float>(ped, 0x558, 0.25f);
        Put<float>(ped, 0x55C, 0.5f);
        Put<int>(ped, 0x598, 4);
        Put<float>(matrix, 0, 0.8f);
        Put<float>(matrix, 4, 0);
        Put<float>(matrix, 8, 0.6f);
        Put<float>(matrix, 0x10, 0);
        Put<float>(matrix, 0x14, 1);
        Put<float>(matrix, 0x18, 0);
        Put<float>(matrix, 0x30, x);
        Put<float>(matrix, 0x34, y);
        Put<float>(matrix, 0x38, 10);
    }
    Request Make(unsigned slot = 1, unsigned long generation = 10) const {
        return {reinterpret_cast<uintptr_t>(&playerGlobal), reinterpret_cast<uintptr_t>(&poolGlobal),
            reinterpret_cast<uintptr_t>(&MockStandstill), Ped(slot), slot * 256 + flags[slot], generation, true};
    }
    void Original(unsigned slot = 1, unsigned char freeze = 0x04, unsigned char stay = 0x02) const {
        const uintptr_t p = Ped(slot), m = Matrix(slot);
        assert(Get<unsigned char>(p, 0x41) == freeze && Get<unsigned char>(p, 0x46E) == stay);
        assert(Get<int>(p, 0x534) == 3 && Get<int>(p, 0x538) == 4);
        assert(Get<float>(p, 0x558) == 0.25f && Get<float>(p, 0x55C) == 0.5f);
        assert(Get<float>(m) == 0.8f && Get<float>(m, 4) == 0);
        assert(Get<float>(m, 0x10) == 0 && Get<float>(m, 0x14) == 1);
    }
};

void TestHoldAndRelease() {
    Fixture f;
    unsigned char before[Stride]{}, playerBefore[Stride]{}, matrixBefore[0x40]{};
    std::memcpy(before, reinterpret_cast<void*>(f.Ped()), Stride);
    std::memcpy(playerBefore, reinterpret_cast<void*>(f.player), Stride);
    std::memcpy(matrixBefore, reinterpret_cast<void*>(f.Matrix()), sizeof(matrixBefore));
    Holder holder;
    Request request = f.Make();
    assert(holder.Update(Request{}) == Result::Idle);
    assert(holder.Update(request) == Result::Held && holder.IsHeld(10) && !holder.IsHeld(11));
    assert(calls == 1);
    assert(Get<unsigned char>(f.Ped(), 0x41) == 0x24 && Get<unsigned char>(f.Ped(), 0x46E) == 0x22);
    assert(Get<int>(f.Ped(), 0x534) == 1 && Get<int>(f.Ped(), 0x538) == 1);
    const float pi = std::acos(-1.0f);
    assert(Near(Get<float>(f.Ped(), 0x558), pi / 2) && Near(Get<float>(f.Ped(), 0x55C), pi / 2));
    assert(Near(Get<float>(f.Matrix()), 0) && Near(Get<float>(f.Matrix(), 4), 0.8f));
    assert(Near(Get<float>(f.Matrix(), 0x10), -1) && Near(Get<float>(f.Matrix(), 0x14), 0));
    for (unsigned frame = 0; frame < 200; ++frame) {
        Put<int>(f.Ped(), 0x534, 3);
        assert(holder.Update(request) == Result::Held && holder.IsHeld(10));
    }
    assert(calls == 201);
    Put<float>(f.matrices + 0x100, 0x30, 2.5f);
    Put<float>(f.matrices + 0x100, 0x34, 3);
    assert(holder.Update(request) == Result::Held);
    assert(Near(Get<float>(f.Ped(), 0x558), 0));
    assert(Get<float>(f.Matrix(), 8) == 0.6f && Get<float>(f.Matrix(), 0x18) == 0);
    assert(Get<float>(f.Matrix(), 0x30) == 2.5f && Get<float>(f.Matrix(), 0x34) == 0);
    assert(std::memcmp(playerBefore, reinterpret_cast<void*>(f.player), Stride) == 0);
    for (size_t i = 0; i < Stride; ++i) {
        const bool owned = i == 0x41 || i == 0x46E || (i >= 0x534 && i < 0x53C) || (i >= 0x558 && i < 0x560);
        if (!owned) assert(before[i] == Get<unsigned char>(f.Ped(), i));
    }
    for (size_t i = 0; i < 0x40; ++i) {
        const bool owned = i < 8 || (i >= 0x10 && i < 0x18);
        if (!owned) assert(matrixBefore[i] == Get<unsigned char>(f.Matrix(), i));
    }
    Put<unsigned char>(f.Ped(), 0x41, 0xA4);
    Put<unsigned char>(f.Ped(), 0x46E, 0x62);
    request.active = false;
    assert(holder.Update(request) == Result::Idle && !holder.IsHeld(10));
    f.Original(1, 0x84, 0x42);
    assert(holder.Release() && holder.Release());
    std::puts("hold/face/replies/cancellation: passed; only owned bytes changed");
}

void TestOriginallyFrozenAndDestructor() {
    Fixture f;
    Put<unsigned char>(f.Ped(), 0x41, 0x24);
    Put<unsigned char>(f.Ped(), 0x46E, 0x22);
    {
        Holder holder;
        assert(holder.Update(f.Make()) == Result::Held);
    }
    f.Original(1, 0x24, 0x22);
    {
        Holder holder;
        assert(holder.Update(f.Make()) == Result::Held);
        Put<unsigned char>(f.Ped(), 0x41, 0x04);
        Put<unsigned char>(f.Ped(), 0x46E, 0x02);
        assert(holder.Release());
    }
    f.Original(); // Neither pre-existing bit was changed by the module.
    std::puts("pre-existing frozen bits/destructor: passed");
}

void TestExclusions() {
    auto rejected = [](Fixture& f, Request request) {
        unsigned char before[Stride]{};
        std::memcpy(before, reinterpret_cast<void*>(f.Ped()), Stride);
        Holder holder;
        const unsigned previousCalls = calls;
        assert(holder.Update(request) == Result::Failed && !holder.IsHeld(request.generation));
        assert(calls == previousCalls);
        assert(std::memcmp(before, reinterpret_cast<void*>(f.Ped()), Stride) == 0);
    };
    {
        Fixture f;
        Put<float>(f.Matrix(), 0x30, 5.01f);
        rejected(f, f.Make());
        Put<float>(f.Matrix(), 0x30, 5);
        Holder holder;
        assert(holder.Update(f.Make()) == Result::Held);
        assert(holder.Release());
    }
    {
        Fixture f;
        Put<unsigned char>(f.Ped(), 0x2F, 1);
        rejected(f, f.Make());
        Put<unsigned char>(f.Ped(), 0x2F, 0);
        Put<float>(f.Matrix(), 0x38, 12.01f);
        rejected(f, f.Make());
        Put<float>(f.Matrix(), 0x38, std::numeric_limits<float>::quiet_NaN());
        rejected(f, f.Make());
        Put<float>(f.Matrix(), 0x38, 10);
        Put<float>(f.Ped(), 0x540, std::numeric_limits<float>::infinity());
        rejected(f, f.Make());
        Put<float>(f.Ped(), 0x540, 0);
        rejected(f, f.Make());
        Put<float>(f.Ped(), 0x540, 100);
        Put<unsigned char>(f.Ped(), 0x46D, 1);
        rejected(f, f.Make());
    }
    {
        Fixture f;
        f.playerGlobal = f.Ped();
        rejected(f, f.Make());
        f.playerGlobal = f.player;
        Put<uint16_t>(f.Ped(), 0x22, 0);
        rejected(f, f.Make());
        Put<uint16_t>(f.Ped(), 0x22, 312);
        rejected(f, f.Make());
        Put<uint16_t>(f.Ped(), 0x22, 7);
        Put<int>(f.Ped(), 0x598, 0);
        rejected(f, f.Make());
        Put<int>(f.Ped(), 0x598, 4);
        f.flags[1] |= 0x80;
        rejected(f, f.Make());
    }
    {
        Fixture f;
        Request request = f.Make();
        ++request.pedAddress;
        rejected(f, request);
        request = f.Make();
        ++request.pedHandle;
        rejected(f, request);
        request = f.Make();
        request.pedHandle = 3 * 256;
        rejected(f, request);
        request = f.Make();
        request.standstillProcessAddress = f.Matrix();
        rejected(f, request);
        request = f.Make();
        request.playerPedPointerAddress = 0x10000;
        rejected(f, request);
        request = f.Make();
        f.pool.size = 0;
        rejected(f, request);
        f.pool.size = 513;
        rejected(f, request);
        f.pool.size = 3;
        f.poolGlobal = 0;
        rejected(f, request);
    }
    std::puts("range/area/health/vehicle/CJ/shape/invalid-address exclusions: passed");
}

void TestLiveExitAndStateChanges() {
    for (unsigned scenario = 0; scenario < 6; ++scenario) {
        Fixture f;
        Holder holder;
        const Request request = f.Make();
        assert(holder.Update(request) == Result::Held);
        if (scenario == 0) Put<float>(f.Ped(), 0x540, 0);
        if (scenario == 1) Put<unsigned char>(f.Ped(), 0x46D, 1);
        if (scenario == 2) Put<float>(f.Ped(), 0x540, std::numeric_limits<float>::quiet_NaN());
        if (scenario == 3) Put<float>(f.Matrix(), 0x30, 6);
        if (scenario == 4) Put<unsigned char>(f.Ped(), 0x2F, 1);
        if (scenario == 5) f.playerGlobal = 0;
        if (scenario < 3) {
            Put<int>(f.Ped(), 0x534, 77);
            Put<int>(f.Ped(), 0x538, 88);
            Put<float>(f.Ped(), 0x558, 1.234f);
            Put<float>(f.Ped(), 0x55C, 1.345f);
            Put<float>(f.Matrix(), 0, 0.7f);
        }
        assert(holder.Update(request) == Result::Failed && !holder.IsHeld(10));
        assert(Get<unsigned char>(f.Ped(), 0x41) == 0x04 && Get<unsigned char>(f.Ped(), 0x46E) == 0x02);
        if (scenario < 3) {
            assert(Get<int>(f.Ped(), 0x534) == 77 && Get<int>(f.Ped(), 0x538) == 88);
            assert(Get<float>(f.Ped(), 0x558) == 1.234f && Get<float>(f.Ped(), 0x55C) == 1.345f);
            assert(Get<float>(f.Matrix()) == 0.7f);
        } else f.Original();
    }
    for (MockMode transition : {MockMode::Kill, MockMode::EnterVehicle}) {
        Fixture f;
        Holder holder;
        mode = transition;
        assert(holder.Update(f.Make()) == Result::Failed);
        assert(Get<unsigned char>(f.Ped(), 0x41) == 0x04 && Get<unsigned char>(f.Ped(), 0x46E) == 0x02);
        assert(Get<int>(f.Ped(), 0x534) == 6 && Get<int>(f.Ped(), 0x538) == 7);
    }
    std::puts("death/vehicle/invalid health/range/area/null player exits: passed");
}

void TestStaleSlotAndWorld() {
    for (unsigned scenario = 0; scenario < 9; ++scenario) {
        Fixture f;
        Holder holder;
        Request request = f.Make();
        assert(holder.Update(request) == Result::Held);
        unsigned char before[Stride]{};
        if (scenario == 0) ++f.flags[1];
        if (scenario == 1) f.flags[1] |= 0x80;
        if (scenario == 2) Put<uint16_t>(f.Ped(), 0x22, 8);
        if (scenario == 3) Put<int>(f.Ped(), 0x598, 5);
        if (scenario == 4) ++f.pool.objects;
        if (scenario == 5) ++f.pool.flags;
        if (scenario == 6) f.poolGlobal = 0;
        if (scenario == 7) f.pool.size = 2;
        if (scenario == 8) f.playerGlobal = f.Ped();
        std::memcpy(before, reinterpret_cast<void*>(f.Ped()), Stride);
        request.active = false;
        assert(holder.Update(request) == Result::Idle && !holder.IsHeld(10));
        assert(std::memcmp(before, reinterpret_cast<void*>(f.Ped()), Stride) == 0);
    }
    {
        Fixture f;
        Holder holder;
        Request request = f.Make();
        assert(holder.Update(request) == Result::Held);
        unsigned char before[0x40]{};
        std::memcpy(before, reinterpret_cast<void*>(f.Matrix(2)), sizeof(before));
        Put<uintptr_t>(f.Ped(), 0x14, f.Matrix(2));
        assert(holder.Update(request) == Result::Failed);
        assert(Get<unsigned char>(f.Ped(), 0x41) == 0x04 && Get<unsigned char>(f.Ped(), 0x46E) == 0x02);
        assert(std::memcmp(before, reinterpret_cast<void*>(f.Matrix(2)), sizeof(before)) == 0);
    }
    {
        Fixture f;
        Holder holder;
        mode = MockMode::Recycle;
        recycleFlag = f.flags + 1;
        assert(holder.Update(f.Make()) == Result::Failed && !holder.IsHeld(10));
        assert(Get<unsigned char>(f.Ped(), 0x41) == 0x24); // Never roll back into the new occupant.
        assert(Get<int>(f.Ped(), 0x534) == 6);
    }
    std::puts("recycled/empty/model/type/reallocated pool/null world/replaced matrix: passed (no stale writes)");
}

void TestGenerationAndSelection() {
    Fixture f;
    Holder holder;
    assert(holder.Update(f.Make(1, 10)) == Result::Held);
    assert(holder.Update(f.Make(2, 11)) == Result::Held && holder.IsHeld(11) && !holder.IsHeld(10));
    f.Original(1);
    assert(holder.Update(f.Make(2, 12)) == Result::Held && holder.IsHeld(12));
    assert(holder.Release());
    f.Original(2);
    std::puts("selection/generation replacement: passed");
}

void TestFaultsAndRecovery() {
    {
        Fixture f;
        Holder holder;
        Protect(f.matrices, PAGE_READONLY);
        assert(holder.Update(f.Make()) == Result::Failed && calls == 1 && !holder.IsHeld(10));
        f.Original();
        Protect(f.matrices, PAGE_READWRITE);
        assert(holder.Release());
    }
    {
        Fixture f;
        Holder holder;
        Protect(f.objects, PAGE_READONLY);
        assert(holder.Update(f.Make()) == Result::Failed && calls == 0);
        f.Original();
        Protect(f.objects, PAGE_READWRITE);
    }
    {
        Fixture f;
        Holder holder;
        mode = MockMode::Fault;
        assert(holder.Update(f.Make()) == Result::Failed && calls == 1);
        f.Original();
        mode = MockMode::Normal;
        assert(holder.Update(f.Make()) == Result::Held);
        assert(holder.Release());
        f.Original();
    }
    {
        Fixture f;
        Holder holder;
        mode = MockMode::ReadOnlyFault;
        faultPage = f.objects;
        assert(holder.Update(f.Make()) == Result::Failed && !holder.IsHeld(10));
        assert(!holder.Release());
        Protect(f.objects, PAGE_READWRITE);
        mode = MockMode::Normal;
        assert(holder.Release());
        f.Original();
    }
    {
        Fixture f;
        Holder holder;
        const Request request = f.Make();
        assert(holder.Update(request) == Result::Held);
        Protect(f.objects, PAGE_NOACCESS);
        assert(!holder.Release() && !holder.IsHeld(10));
        Protect(f.objects, PAGE_READWRITE);
        assert(holder.Release());
        f.Original();
    }
    std::puts("write/invocation/read faults: passed; rollback and retry are reversible");
}
}

int main() {
    TestHoldAndRelease();
    TestOriginallyFrozenAndDestructor();
    TestExclusions();
    TestLiveExitAndStateChanges();
    TestStaleSlotAndWorld();
    TestGenerationAndSelection();
    TestFaultsAndRecovery();
    assert(allocations == 0);
    std::puts("All ped-hold x86 regression tests passed; fixture allocations released.");
}
