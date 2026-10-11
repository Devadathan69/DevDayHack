#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <TlHelp32.h>
#include "..\src\world-context.h"
#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <cwchar>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

using namespace NpcWorld;
static_assert(sizeof(uintptr_t) == 4, "Run fixtures with the x86 toolchain");

namespace {
struct Memory {
    uintptr_t base = 0x400000;
    std::vector<unsigned char> bytes;
    uintptr_t fault = 0;
    bool unstableClock = false;
    unsigned clockReads = 0;
};
bool Read(uintptr_t address, void* output, size_t bytes, void* context) noexcept {
    auto& memory = *static_cast<Memory*>(context);
    if (address == memory.fault || address < memory.base ||
        static_cast<uint64_t>(address - memory.base) + bytes > memory.bytes.size()) return false;
    std::memcpy(output, memory.bytes.data() + address - memory.base, bytes);
    if (memory.unstableClock && address == memory.base + 0x7FD4C6 && bytes == 2)
        static_cast<unsigned char*>(output)[0] = static_cast<unsigned char>(++memory.clockReads % 2);
    return true;
}
template<class T> void Put(Memory& memory, uintptr_t preferred, T value) {
    const size_t offset = preferred - 0x400000;
    assert(offset + sizeof(value) <= memory.bytes.size());
    std::memcpy(memory.bytes.data() + offset, &value, sizeof(value));
}
void Bytes(Memory& memory, uintptr_t preferred, const std::vector<unsigned char>& values) {
    const size_t offset = preferred - 0x400000;
    assert(offset + values.size() <= memory.bytes.size());
    std::memcpy(memory.bytes.data() + offset, values.data(), values.size());
}
Memory Fixture(uintptr_t base) {
    Memory m;
    m.base = base;
    m.bytes.resize(0x985000);
    IMAGE_DOS_HEADER dos{};
    dos.e_magic = IMAGE_DOS_SIGNATURE;
    dos.e_lfanew = 0x80;
    Put(m, 0x400000, dos);
    IMAGE_NT_HEADERS32 nt{};
    nt.Signature = IMAGE_NT_SIGNATURE;
    nt.FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
    nt.FileHeader.NumberOfSections = 2;
    nt.FileHeader.TimeDateStamp = 0x547CD3A3;
    nt.FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER32);
    nt.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    nt.OptionalHeader.ImageBase = 0x400000;
    nt.OptionalHeader.SizeOfImage = 0x985000;
    nt.OptionalHeader.AddressOfEntryPoint = 0x458DB8;
    Put(m, 0x400080, nt);
    IMAGE_SECTION_HEADER code{}, data{};
    code.VirtualAddress = 0x1000;
    code.Misc.VirtualSize = 0x49D035;
    code.Characteristics = IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE;
    data.VirtualAddress = 0x513000;
    data.Misc.VirtualSize = 0x4156B8;
    data.Characteristics = IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE;
    Put(m, 0x400080 + sizeof(nt), code);
    Put(m, 0x400080 + sizeof(nt) + sizeof(code), data);
    // Independent mock instruction/data table, with operands relocated just like the PE loader.
    Bytes(m, 0x53AE07, {0xC6,0x05,0,0,0,0,0x0C,0xA2,0,0,0,0,0x66,0x89,0x15,0,0,0,0,
        0xA2,0,0,0,0,0xC6,0x05,0,0,0,0,0x04});
    Bytes(m, 0x53AFC6, {0x80,0x3D,0,0,0,0,0x3C,0x0F,0x82,0x83,0,0,0,0xA0,0,0,0,0,
        0x53,0xB3,0x01,0x02,0xC3,0x88,0x15,0,0,0,0,0xA2,0,0,0,0,0x3C,0x18});
    Bytes(m, 0x75CA60, {0x55,0x8B,0xEC,0x66,0x8B,0x45,0x08,0x66,0xA3,0,0,0,0,
        0x66,0xA3,0,0,0,0,0x66,0xA3,0,0,0,0,0x5D,0xC3});
    Bytes(m, 0x75E18F, {0x66,0x83,0xF9,0x10,0x74,0x06,0x66,0x83,0xF9,0x08,0x75,0x03,
        0xD9,0x55,0xF8,0x66,0x83,0xFE,0x10,0x74,0x06,0x66,0x83,0xFE,0x08,0x75,0x0A});
    Bytes(m, 0x75E1FC, {0xD9,0x05,0,0,0,0,0xD9,0xC0,0xDE,0xEA,0xD9,0xC9,0xD9,0x5D,0xF8,0xD9,0x45,0xF8});
    Bytes(m, 0x75E24D, {0xD9,0xC9,0xD9,0x1D,0,0,0,0,0xD9,0x55,0xF8,0xD9,0x05,0,0,0,0});
    const uintptr_t references[][2] = {
        {0x53AE09,0xBFD4C7},{0x53AE0F,0xBFD4C6},{0x53AE16,0xBFD4C4},
        {0x53AE1B,0xBFD4BC},{0x53AE21,0xBFD4C3},
        {0x53AFC8,0xBFD4C6},{0x53AFD4,0xBFD4C7},{0x53AFDF,0xBFD4C6},{0x53AFE4,0xBFD4C7},
        {0x75CA69,0xD0D068},{0x75CA6F,0xD0D070},{0x75CA75,0xD0D06C},
        {0x75E1FE,0xD0D074},{0x75E251,0xD0D074},{0x75E25A,0xD0D05C}
    };
    for (const auto& reference : references) Put(m, reference[0], static_cast<uint32_t>(base + reference[1] - 0x400000));
    return m;
}

void TestZones() {
    std::vector<Detail::Zone> zones;
    assert(Detail::ParseZones("# fixture only\nzone\nBIG,3,-100,-100,-100,100,100,100,1,UNUSED\n"
        "SMALL,0,0,0,0,10,10,10,1,SMALL\nTIE,0,0,0,0,10,10,10,2,TIE\nend\n", zones));
    assert(zones.size() == 3);
    const auto* zone = Detail::FindZone(zones, 0, 0, 0, 0);
    assert(zone && zone->code == "SMALL");
    assert(Detail::FindZone(zones, 10, 10, 10, 0)->code == "SMALL");
    assert(Detail::FindZone(zones, 10.001f, 10, 10, 0)->code == "BIG");
    assert(!Detail::FindZone(zones, 101, 0, 0, 0));
    assert(!Detail::FindZone(zones, 1, 1, 1, 1)); // island column is NOT the ped's interior area.
    assert(!Detail::FindZone(zones, std::numeric_limits<float>::quiet_NaN(), 1, 1, 0));
    assert(!Detail::FindZone(zones, 20001, 1, 1, 0));
    std::vector<Detail::Zone> malformed;
    assert(Detail::ParseZones("zone\nGOOD,0,-1,-1,-1,1,1,1,1,GOOD\n"
        "FLAT,0,0,0,0,0,1,1,1,FLAT\nREV,0,2,0,0,1,1,1,1,REV\n"
        "NAN,0,nan,0,0,1,1,1,1,NAN\nINF,0,0,0,0,inf,1,1,1,INF\n"
        "MISSING,0,0,0,0,1,1,1,1\nTRAIL,0,0,0,0,1,1,1,1,TRAIL,oops\n"
        "TOOBIG,0,0,0,0,20001,1,1,1,TOOBIG\nTYPE,8,0,0,0,1,1,1,1,TYPE\n"
        "CITY,0,0,0,0,1,1,1,4,CITY\nTOOLONGCODE,0,0,0,0,1,1,1,1,KEY\n"
        "EMPTY,,0,0,0,0,1,1,1,1,EMPTY\nBAD,0,0,0,0,1,1,1,1,BAD,\nend\n", malformed));
    assert(malformed.size() == 1 && malformed[0].code == "GOOD");
    const auto original = malformed.size();
    assert(!Detail::ParseZones("zone\nX,0,0,0,0,1,1,1,1,X\n", malformed));
    assert(malformed.size() == original);
    assert(!Detail::ParseZones(std::string(1024 * 1024 + 1, 'x'), malformed));
    assert(!Detail::ParseZones("zone\n" + std::string(513, 'x') + "\nend", malformed));
    std::vector<Detail::Zone> full(4096);
    assert(!Detail::ParseZones("zone\nX,0,0,0,0,1,1,1,1,X\nend", full));
}

void Append32(std::string& bytes, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes.push_back(static_cast<char>((value >> (i * 8)) & 0xFF));
}
void Patch32(std::string& bytes, size_t offset, uint32_t value) {
    assert(offset + 4 <= bytes.size());
    for (unsigned i = 0; i < 4; ++i) bytes[offset + i] = static_cast<char>((value >> (i * 8)) & 0xFF);
}
std::string GxtFixture(const std::string& label = "Synthetic Grove", bool duplicateMain = false) {
    std::string bytes;
    Append32(bytes, 0x00080004);
    bytes += "TABL";
    Append32(bytes, duplicateMain ? 24u : 12u);
    const unsigned tables = duplicateMain ? 2u : 1u;
    for (unsigned i = 0; i < tables; ++i) {
        bytes.append("MAIN\0\0\0\0", 8);
        Append32(bytes, 12 + tables * 12);
    }
    bytes += "TKEY";
    Append32(bytes, 8);
    Append32(bytes, 0);
    Append32(bytes, Detail::GxtKey("LAB1"));
    bytes += "TDAT";
    Append32(bytes, static_cast<uint32_t>(label.size() + 1));
    bytes += label;
    bytes += '\0';
    return bytes;
}
void TestGxt() {
    assert(Detail::GxtKey("123456789") == 0x340BC6D9u); // Standard CRC32 remainder without final XOR.
    assert(Detail::GxtKey("GAN") == 0x0D6DAF7Bu && Detail::GxtKey("gan") == 0x0D6DAF7Bu);
    assert(Detail::GxtKey("GAN1") != Detail::GxtKey("GAN"));
    std::vector<Detail::Zone> zones;
    assert(Detail::ParseZones("zone\nBLOCK1,0,0,0,0,1,1,1,1,LAB1\nend", zones));
    assert(zones[0].code == "BLOCK1" && zones[0].textKey == "LAB1" && zones[0].label.empty());
    const auto fixture = GxtFixture();
    assert(Detail::ResolveGxtLabels(fixture, zones) && zones[0].label == "Synthetic Grove");
    const size_t fields[] = {0,8,20,28,32,44};
    for (const size_t offset : fields) {
        auto bad = fixture;
        Patch32(bad, offset, 0xFFFFFFFFu);
        assert(!Detail::ResolveGxtLabels(bad, zones) && zones[0].label == "Synthetic Grove");
    }
    for (const size_t offset : {size_t{4},size_t{24},size_t{40}}) {
        auto bad = fixture;
        bad[offset] = 'X';
        assert(!Detail::ResolveGxtLabels(bad, zones) && zones[0].label == "Synthetic Grove");
    }
    auto bits16 = fixture;
    Patch32(bits16, 0, 0x00100004);
    assert(!Detail::ResolveGxtLabels(bits16, zones));
    assert(!Detail::ResolveGxtLabels(GxtFixture("Synthetic Grove", true), zones));
    auto duplicateHash = fixture;
    duplicateHash.insert(40, fixture.substr(32, 8));
    Patch32(duplicateHash, 28, 16);
    assert(!Detail::ResolveGxtLabels(duplicateHash, zones));
    for (size_t length = 0; length < fixture.size(); ++length)
        assert(!Detail::ResolveGxtLabels(std::string_view(fixture.data(), length), zones));
    assert(!Detail::ResolveGxtLabels(std::string(8 * 1024 * 1024 + 1, 'x'), zones));
    auto absent = fixture;
    Patch32(absent, 36, Detail::GxtKey("OTHER"));
    assert(Detail::ResolveGxtLabels(absent, zones) && zones[0].label.empty());
    for (const std::string label : {std::string(81, 'x'),std::string(""),std::string("   "),
        std::string("~r~Synthetic"),std::string("Line\nBreak"),std::string("Custom\xE9")}) {
        assert(Detail::ResolveGxtLabels(GxtFixture(label), zones) && zones[0].label.empty());
    }
    auto noNull = fixture;
    noNull.back() = 'x';
    assert(Detail::ResolveGxtLabels(noNull, zones) && zones[0].label.empty());
    std::vector<Detail::Zone> full(4097);
    assert(!Detail::ResolveGxtLabels(fixture, full));
    assert(Detail::ResolveGxtLabels(GxtFixture(std::string(80, 'A')), zones) && zones[0].label.size() == 80);
    assert(Detail::ResolveGxtLabels(fixture, zones) && zones[0].label == "Synthetic Grove");
}

void TestMemoryAndWeather() {
    const char* labels[] = {"extra-sunny","sunny","smoggy","smoggy","cloudy","sunny","extra-sunny",
        "cloudy","rainy","foggy","sunny","extra-sunny","cloudy","extra-sunny","sunny","cloudy",
        "rainy","extra-sunny","sunny","sandstorm","underwater","extra-colours","extra-colours"};
    for (int i = 0; i < 23; ++i) assert(std::strcmp(Detail::WeatherCondition(i), labels[i]) == 0);
    assert(!*Detail::WeatherCondition(-1) && !*Detail::WeatherCondition(23));
    for (const uintptr_t base : {uintptr_t{0x400000}, uintptr_t{0xB00000}, uintptr_t{0x24000000}}) {
        auto memory = Fixture(base);
        Detail::Addresses addresses;
        assert(Detail::VerifyLayout(base, true, Read, &memory, addresses));
        assert(addresses.hours == base + 0x7FD4C7 && addresses.minutes == base + 0x7FD4C6);
        assert(addresses.oldWeather == base + 0x90D070 && addresses.newWeather == base + 0x90D06C &&
            addresses.rain == base + 0x90D074);
        Put(memory, 0xBFD4C6, static_cast<unsigned char>(59));
        Put(memory, 0xBFD4C7, static_cast<unsigned char>(23));
        Put(memory, 0xD0D070, int16_t{8});
        Put(memory, 0xD0D06C, int16_t{1});
        Put(memory, 0xD0D074, 0.45f);
        auto snapshot = Detail::Sample(addresses, Read, &memory);
        assert(snapshot.available && snapshot.hour == 23 && snapshot.minute == 59);
        assert(snapshot.weatherId == 8 && snapshot.targetWeatherId == 1 && snapshot.weather == "rainy" &&
            snapshot.rainIntensity == 0.45f);
        // Stable categorical rain is not observed rain until the measured threshold is exceeded.
        for (int id = 0; id <= 22; ++id) {
            Put(memory, 0xD0D070, static_cast<int16_t>(id));
            Put(memory, 0xD0D06C, static_cast<int16_t>(id));
            for (const float intensity : {0.0f, std::nextafter(0.05f, 0.0f), 0.05f,
                std::nextafter(0.05f, 1.0f), 1.0f}) {
                Put(memory, 0xD0D074, intensity);
                snapshot = Detail::Sample(addresses, Read, &memory);
                assert(snapshot.weatherId == id && snapshot.targetWeatherId == id);
                const char* expected = intensity > 0.05f ? "rainy" :
                    ((id == 8 || id == 16) ? "transitioning" : labels[id]);
                assert(snapshot.weather == expected);
            }
        }
        // A differing fog/cloud/sandstorm/rain target never becomes a completed current condition.
        const int transitions[][2] = {{1,8},{8,1},{9,4},{4,9},{5,19},{19,5},{0,22}};
        for (const auto& transition : transitions) {
            Put(memory, 0xD0D070, static_cast<int16_t>(transition[0]));
            Put(memory, 0xD0D06C, static_cast<int16_t>(transition[1]));
            for (const float intensity : {0.0f, 0.01f, 0.05f}) {
                Put(memory, 0xD0D074, intensity);
                snapshot = Detail::Sample(addresses, Read, &memory);
                assert(snapshot.weatherId == transition[0] && snapshot.targetWeatherId == transition[1] &&
                    snapshot.weather == "transitioning");
            }
        }
        Put(memory, 0xD0D070, int16_t{1});
        Put(memory, 0xD0D06C, int16_t{8});
        Put(memory, 0xD0D074, std::nextafter(0.05f, 1.0f));
        snapshot = Detail::Sample(addresses, Read, &memory);
        assert(snapshot.weatherId == 1 && snapshot.targetWeatherId == 8 && snapshot.weather == "rainy");
        Put(memory, 0xD0D06C, int16_t{1});
        assert(Detail::Sample(addresses, Read, &memory).weather == "rainy"); // Observed residual rain wins even if both IDs are sunny.
        Put(memory, 0xD0D074, std::numeric_limits<float>::quiet_NaN());
        snapshot = Detail::Sample(addresses, Read, &memory);
        assert(snapshot.weatherId == 1 && snapshot.targetWeatherId == 1 && snapshot.weather.empty());
        memory.fault = addresses.rain;
        assert(Detail::Sample(addresses, Read, &memory).weather.empty());
        Put(memory, 0xD0D074, 0.0f);
        memory.fault = addresses.oldWeather;
        snapshot = Detail::Sample(addresses, Read, &memory);
        assert(snapshot.weatherId == -1 && snapshot.targetWeatherId == 1 && snapshot.weather.empty());
        memory.fault = addresses.newWeather;
        snapshot = Detail::Sample(addresses, Read, &memory);
        assert(snapshot.weatherId == 1 && snapshot.targetWeatherId == -1 && snapshot.weather.empty());
        memory.fault = 0;
        auto targetOnly = addresses;
        targetOnly.hours = targetOnly.minutes = targetOnly.oldWeather = targetOnly.rain = 0;
        snapshot = Detail::Sample(targetOnly, Read, &memory);
        assert(!snapshot.available && snapshot.targetWeatherId == 1 && snapshot.weather.empty());
        Put(memory, 0xBFD4C6, static_cast<unsigned char>(60));
        Put(memory, 0xD0D070, int16_t{23});
        Put(memory, 0xD0D06C, int16_t{23});
        for (const float invalid : {-0.1f, 1.01f, std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::infinity()}) {
            Put(memory, 0xD0D074, invalid);
            snapshot = Detail::Sample(addresses, Read, &memory);
            assert(!snapshot.available && snapshot.hour == -1 && snapshot.minute == -1 &&
                snapshot.weatherId == -1 && snapshot.targetWeatherId == -1 && snapshot.weather.empty() &&
                snapshot.rainIntensity == -1);
        }
        Put(memory, 0xBFD4C6, static_cast<unsigned char>(0));
        Put(memory, 0xBFD4C7, static_cast<unsigned char>(24));
        assert(Detail::Sample(addresses, Read, &memory).hour == -1);
        Put(memory, 0xBFD4C7, static_cast<unsigned char>(0));
        memory.unstableClock = true;
        assert(Detail::Sample(addresses, Read, &memory).hour == -1);
        memory.unstableClock = false;
        memory.fault = addresses.minutes;
        assert(Detail::Sample(addresses, Read, &memory).hour == -1);
        memory.fault = 0;
        Put(memory, 0xBFD4C6, static_cast<unsigned char>(0));
        Put(memory, 0xBFD4C7, static_cast<unsigned char>(0));
        for (const float endpoint : {0.0f, 1.0f}) {
            Put(memory, 0xD0D074, endpoint);
            snapshot = Detail::Sample(addresses, Read, &memory);
            assert(snapshot.hour == 0 && snapshot.minute == 0 && snapshot.rainIntensity == endpoint);
        }
        assert(!Detail::Sample({}, Read, &memory).available);
        assert(!Detail::Sample(addresses, nullptr, &memory).available);
        memory.fault = base + 0x35E24D;
        assert(!Detail::VerifyLayout(base, true, Read, &memory, addresses));
        memory.fault = 0;
        Put(memory, 0x400080 + offsetof(IMAGE_NT_HEADERS32, OptionalHeader) +
            offsetof(IMAGE_OPTIONAL_HEADER32, AddressOfEntryPoint), uint32_t{0x458DB9});
        assert(!Detail::VerifyLayout(base, true, Read, &memory, addresses));
        Put(memory, 0x400080 + offsetof(IMAGE_NT_HEADERS32, OptionalHeader) +
            offsetof(IMAGE_OPTIONAL_HEADER32, AddressOfEntryPoint), uint32_t{0x458DB8});
        const uintptr_t dataFlags = 0x400080 + sizeof(IMAGE_NT_HEADERS32) + sizeof(IMAGE_SECTION_HEADER) +
            offsetof(IMAGE_SECTION_HEADER, Characteristics);
        Put(memory, dataFlags, uint32_t{IMAGE_SCN_MEM_READ});
        assert(!Detail::VerifyLayout(base, true, Read, &memory, addresses));
        Put(memory, dataFlags, uint32_t{IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE});
        for (const uintptr_t code : {uintptr_t{0x53AE07},uintptr_t{0x53AFC6},uintptr_t{0x75CA60},
            uintptr_t{0x75E18F},uintptr_t{0x75E1FC},uintptr_t{0x75E24D}}) {
            memory.bytes[code - 0x400000] ^= 1;
            assert(!Detail::VerifyLayout(base, true, Read, &memory, addresses));
            assert(!addresses.hours && !addresses.oldWeather && !addresses.newWeather && !addresses.rain);
            memory.bytes[code - 0x400000] ^= 1;
        }
        Put(memory, 0x75CA6F, uint32_t{0xD0D070});
        assert(Detail::VerifyLayout(base, true, Read, &memory, addresses) == (base == 0x400000));
        Put(memory, 0x75CA6F, static_cast<uint32_t>(base + 0x90D070));
        Put(memory, 0x75CA75, uint32_t{0xD0D06C});
        assert(Detail::VerifyLayout(base, true, Read, &memory, addresses) == (base == 0x400000));
        assert(!Detail::VerifyLayout(base, false, Read, &memory, addresses));
        assert(!Detail::VerifyLayout(base + 1, true, Read, &memory, addresses));
        assert(!Detail::VerifyLayout(0xFFC00000, true, Read, &memory, addresses));
        Put(memory, 0x400000, uint16_t{0});
        assert(!Detail::VerifyLayout(base, true, Read, &memory, addresses));
    }
    Reader reader;
    assert(!reader.Initialize(1, true, L""));
    assert(!reader.Read(0, 0, 0, 0).available);
    void* fault = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_NOACCESS);
    assert(fault && !reader.Initialize(reinterpret_cast<uintptr_t>(fault), true, L""));
    assert(VirtualFree(fault, 0, MEM_RELEASE));
}

struct RemoteMemory {
    HANDLE process = nullptr;
    uintptr_t base = 0;
    size_t size = 0, calls = 0, bytes = 0;
};
bool RemoteRead(uintptr_t address, void* output, size_t bytes, void* context) noexcept {
    auto* memory = static_cast<RemoteMemory*>(context);
    if (!output || !bytes || bytes > 640 || address < memory->base ||
        static_cast<uint64_t>(address - memory->base) + bytes > memory->size) return false;
    SIZE_T got = 0;
    ++memory->calls;
    memory->bytes += bytes;
    __try {
        return ReadProcessMemory(memory->process, reinterpret_cast<const void*>(address), output, bytes, &got) != FALSE &&
            got == bytes;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
int ProbeLiveProcess(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!process) {
        std::printf("Read-only OpenProcess failed: Win32=%lu. No game writes/restarts performed.\n", GetLastError());
        return 2;
    }
    HANDLE modules = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (modules == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        CloseHandle(process);
        std::printf("Read-only module query failed: Win32=%lu.\n", error);
        return 2;
    }
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    const bool found = Module32FirstW(modules, &entry) != FALSE;
    CloseHandle(modules);
    if (!found || _wcsicmp(entry.szModule, L"gta-sa.exe") != 0) {
        CloseHandle(process);
        std::puts("Refusing probe: main module is not the requested gta-sa.exe.");
        return 2;
    }
    RemoteMemory memory{process,reinterpret_cast<uintptr_t>(entry.modBaseAddr),entry.modBaseSize};
    Detail::Addresses addresses;
    const char* diagnostic = nullptr;
    uintptr_t headerImageBase = 0;
    const bool verified = Detail::VerifyLayout(memory.base, true, RemoteRead, &memory, addresses,
        &diagnostic, &headerImageBase);
    std::printf("PID=%lu base=%08lX headerImageBase=%08lX imageSize=%08lX initialization=%s reads=%zu bytes=%zu\n",
        pid, static_cast<unsigned long>(memory.base), static_cast<unsigned long>(headerImageBase),
        entry.modBaseSize, diagnostic, memory.calls, memory.bytes);
    if (verified) {
        const auto snapshot = Detail::Sample(addresses, RemoteRead, &memory);
        std::printf("LIVE clock=%d:%02d old=%d target=%d rain=%.6f condition=%s\n", snapshot.hour,
            snapshot.minute,snapshot.weatherId,snapshot.targetWeatherId,snapshot.rainIntensity,
            snapshot.weather.empty() ? "unknown" : snapshot.weather.c_str());
    }
    CloseHandle(process);
    return verified ? 0 : 1;
}

void TestDiagnostics() {
    auto memory = Fixture(0xB00000);
    Detail::Addresses addresses;
    const char* diagnostic = nullptr;
    const auto verify = [&]() {
        const bool ok = Detail::VerifyLayout(memory.base, true, Read, &memory, addresses, &diagnostic);
        if (!ok) assert(!addresses.hours && !addresses.minutes && !addresses.oldWeather &&
            !addresses.newWeather && !addresses.rain);
        return ok;
    };
    assert(verify() && std::strcmp(diagnostic, "ok") == 0);
    const uintptr_t unreadable[] = {memory.base,memory.base + 0x80,memory.base + 0x80 + sizeof(IMAGE_NT_HEADERS32)};
    const char* stages[] = {"dos-header-unreadable","nt-header-unreadable","pe-sections-unreadable"};
    for (size_t i = 0; i < 3; ++i) {
        memory.fault = unreadable[i];
        assert(!verify() && std::strcmp(diagnostic, stages[i]) == 0);
    }
    memory.fault = 0;
    const uintptr_t code[] = {0x53AE07,0x53AFC6,0x75CA60,0x75E18F,0x75E1FC,0x75E24D};
    const char* mismatches[] = {"clock-initialization-mismatch","clock-rollover-mismatch","weather-force-mismatch",
        "rain-enums-mismatch","rain-load-mismatch","rain-store-mismatch"};
    const char* faults[] = {"clock-initialization-unreadable","clock-rollover-unreadable","weather-force-unreadable",
        "rain-enums-unreadable","rain-load-unreadable","rain-store-unreadable"};
    for (size_t i = 0; i < 6; ++i) {
        memory.fault = memory.base + code[i] - 0x400000;
        assert(!verify() && std::strcmp(diagnostic, faults[i]) == 0);
        memory.fault = 0;
        memory.bytes[code[i] - 0x400000] ^= 1;
        assert(!verify() && std::strcmp(diagnostic, mismatches[i]) == 0);
        memory.bytes[code[i] - 0x400000] ^= 1;
        assert(verify()); // A retry succeeds ONLY after the original exact fingerprint is restored.
    }
    const uintptr_t optional = 0x400080 + offsetof(IMAGE_NT_HEADERS32, OptionalHeader);
    Put(memory, optional + offsetof(IMAGE_OPTIONAL_HEADER32, ImageBase), uint32_t{0xC00000});
    assert(!verify() && std::strcmp(diagnostic, "pe-image-base") == 0);
    Put(memory, optional + offsetof(IMAGE_OPTIONAL_HEADER32, ImageBase), uint32_t{0x400000});
    Put(memory, optional + offsetof(IMAGE_OPTIONAL_HEADER32, AddressOfEntryPoint), uint32_t{0});
    assert(!verify() && std::strcmp(diagnostic, "pe-entry-point") == 0);
    Put(memory, optional + offsetof(IMAGE_OPTIONAL_HEADER32, AddressOfEntryPoint), uint32_t{0x458DB8});
    const uintptr_t section = 0x400080 + sizeof(IMAGE_NT_HEADERS32);
    Put(memory, section + offsetof(IMAGE_SECTION_HEADER, Characteristics), uint32_t{IMAGE_SCN_MEM_READ});
    assert(!verify() && std::strcmp(diagnostic, "pe-code-section") == 0);
    Put(memory, section + offsetof(IMAGE_SECTION_HEADER, Characteristics),
        uint32_t{IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE});
    Put(memory, section + sizeof(IMAGE_SECTION_HEADER) + offsetof(IMAGE_SECTION_HEADER, Characteristics),
        uint32_t{IMAGE_SCN_MEM_READ});
    assert(!verify() && std::strcmp(diagnostic, "pe-data-section") == 0);
    assert(!Detail::VerifyLayout(memory.base, false, Read, &memory, addresses, &diagnostic) &&
        std::strcmp(diagnostic, "unsupported-layout") == 0);
    assert(!Detail::VerifyLayout(memory.base, true, nullptr, &memory, addresses, &diagnostic) &&
        std::strcmp(diagnostic, "invalid-memory-reader") == 0);
    assert(!Detail::VerifyLayout(1, true, Read, &memory, addresses, &diagnostic) &&
        std::strcmp(diagnostic, "module-range") == 0);
    Reader reader;
    assert(std::strcmp(reader.InitializationDiagnostic(), "not-initialized") == 0);
    assert(!reader.Initialize(1, true, L"") && std::strcmp(reader.InitializationDiagnostic(), "module-range") == 0);
}

void TestRuntimeHeaderBase() {
    for (const uintptr_t base : {uintptr_t{0x400000},uintptr_t{0xB00000},uintptr_t{0x24000000}}) {
        auto memory = Fixture(base);
        Detail::Addresses addresses;
        const uintptr_t imageBaseField = 0x400080 + offsetof(IMAGE_NT_HEADERS32, OptionalHeader) +
            offsetof(IMAGE_OPTIONAL_HEADER32, ImageBase);
        const char* diagnostic = nullptr;
        uintptr_t observed = 0;
        const auto verify = [&]() {
            const bool ok = Detail::VerifyLayout(base, true, Read, &memory, addresses, &diagnostic, &observed);
            if (!ok) assert(!addresses.hours && !addresses.minutes && !addresses.oldWeather &&
                !addresses.newWeather && !addresses.rain);
            return ok;
        };
        assert(verify() && observed == 0x400000);
        Put(memory, imageBaseField, static_cast<uint32_t>(base));
        assert(verify() && observed == base && std::strcmp(diagnostic, "ok") == 0);
        assert(addresses.hours == base + 0x7FD4C7 && addresses.oldWeather == base + 0x90D070);
        for (const uintptr_t code : {uintptr_t{0x53AE07},uintptr_t{0x53AFC6},uintptr_t{0x75CA60},
            uintptr_t{0x75E18F},uintptr_t{0x75E1FC},uintptr_t{0x75E24D}}) {
            memory.bytes[code - 0x400000] ^= 1;
            assert(!verify() && observed == base);
            memory.bytes[code - 0x400000] ^= 1;
        }
        if (base != 0x400000) {
            Put(memory, 0x75CA6F, uint32_t{0xD0D070}); // Nonrelocated Old operand cannot pass a rewritten header.
            assert(!verify() && std::strcmp(diagnostic, "weather-force-mismatch") == 0);
            Put(memory, imageBaseField, uint32_t{0x35000000});
            assert(!verify() && observed == 0x35000000 && std::strcmp(diagnostic, "pe-image-base") == 0);
            Put(memory, 0x75CA6F, static_cast<uint32_t>(base + 0x90D070));
        }
        Put(memory, imageBaseField, uint32_t{0x35000000});
        assert(!verify() && std::strcmp(diagnostic, "pe-image-base") == 0);
        Put(memory, imageBaseField, static_cast<uint32_t>(base));
        const uintptr_t optional = 0x400080 + offsetof(IMAGE_NT_HEADERS32, OptionalHeader);
        Put(memory, optional + offsetof(IMAGE_OPTIONAL_HEADER32, AddressOfEntryPoint), uint32_t{0x458DB9});
        assert(!verify() && std::strcmp(diagnostic, "pe-entry-point") == 0);
        Put(memory, optional + offsetof(IMAGE_OPTIONAL_HEADER32, AddressOfEntryPoint), uint32_t{0x458DB8});
        Put(memory, optional + offsetof(IMAGE_OPTIONAL_HEADER32, SizeOfImage), uint32_t{0x986000});
        assert(!verify() && std::strcmp(diagnostic, "pe-image-size") == 0);
        Put(memory, optional + offsetof(IMAGE_OPTIONAL_HEADER32, SizeOfImage), uint32_t{0x985000});
        const uintptr_t timestamp = 0x400080 + offsetof(IMAGE_NT_HEADERS32, FileHeader) +
            offsetof(IMAGE_FILE_HEADER, TimeDateStamp);
        Put(memory, timestamp, uint32_t{0});
        assert(!verify() && std::strcmp(diagnostic, "pe-timestamp") == 0);
        Put(memory, timestamp, uint32_t{0x547CD3A3});
        const uintptr_t dataFlags = 0x400080 + sizeof(IMAGE_NT_HEADERS32) + sizeof(IMAGE_SECTION_HEADER) +
            offsetof(IMAGE_SECTION_HEADER, Characteristics);
        Put(memory, dataFlags, uint32_t{IMAGE_SCN_MEM_READ});
        assert(!verify() && std::strcmp(diagnostic, "pe-data-section") == 0);
        Put(memory, dataFlags, uint32_t{IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE});
        assert(verify());
        memory.fault = base + 0x80;
        assert(!verify() && observed == 0 && std::strcmp(diagnostic, "nt-header-unreadable") == 0);
    }
    Reader reader;
    assert(reader.HeaderImageBase() == 0 && reader.ModuleBase() == 0);
    assert(!reader.Initialize(1, true, L"") && reader.ModuleBase() == 1 && reader.HeaderImageBase() == 0);
}

Memory InstalledImage(const wchar_t* path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    assert(file && file.tellg() > 0 && file.tellg() < 16 * 1024 * 1024);
    std::vector<unsigned char> raw(static_cast<size_t>(file.tellg()));
    file.seekg(0);
    assert(file.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size())));
    IMAGE_DOS_HEADER dos{};
    std::memcpy(&dos, raw.data(), sizeof(dos));
    assert(dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew >= 0 &&
        static_cast<size_t>(dos.e_lfanew) + sizeof(IMAGE_NT_HEADERS32) <= raw.size());
    IMAGE_NT_HEADERS32 nt{};
    std::memcpy(&nt, raw.data() + dos.e_lfanew, sizeof(nt));
    assert(nt.OptionalHeader.SizeOfImage == 0x985000 && nt.OptionalHeader.SizeOfHeaders <= raw.size());
    Memory image;
    image.bytes.resize(nt.OptionalHeader.SizeOfImage);
    std::memcpy(image.bytes.data(), raw.data(), nt.OptionalHeader.SizeOfHeaders);
    for (WORD i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
        IMAGE_SECTION_HEADER section{};
        const size_t offset = static_cast<size_t>(dos.e_lfanew) + sizeof(nt) + i * sizeof(section);
        assert(offset + sizeof(section) <= raw.size());
        std::memcpy(&section, raw.data() + offset, sizeof(section));
        assert(static_cast<uint64_t>(section.PointerToRawData) + section.SizeOfRawData <= raw.size());
        assert(static_cast<uint64_t>(section.VirtualAddress) + section.SizeOfRawData <= image.bytes.size());
        std::memcpy(image.bytes.data() + section.VirtualAddress, raw.data() + section.PointerToRawData,
            section.SizeOfRawData);
    }
    return image;
}
void Relocate(Memory& image, uintptr_t base) {
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS32 nt{};
    std::memcpy(&dos, image.bytes.data(), sizeof(dos));
    std::memcpy(&nt, image.bytes.data() + dos.e_lfanew, sizeof(nt));
    const auto directory = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    assert(static_cast<uint64_t>(directory.VirtualAddress) + directory.Size <= image.bytes.size());
    size_t cursor = directory.VirtualAddress;
    while (cursor < static_cast<size_t>(directory.VirtualAddress) + directory.Size) {
        IMAGE_BASE_RELOCATION block{};
        std::memcpy(&block, image.bytes.data() + cursor, sizeof(block));
        assert(block.SizeOfBlock >= sizeof(block) && cursor + block.SizeOfBlock <=
            static_cast<size_t>(directory.VirtualAddress) + directory.Size);
        const size_t count = (block.SizeOfBlock - sizeof(block)) / sizeof(uint16_t);
        for (size_t i = 0; i < count; ++i) {
            uint16_t entry = 0;
            std::memcpy(&entry, image.bytes.data() + cursor + sizeof(block) + i * sizeof(entry), sizeof(entry));
            const unsigned type = entry >> 12;
            if (!type) continue;
            assert(type == IMAGE_REL_BASED_HIGHLOW);
            const size_t target = block.VirtualAddress + (entry & 0xFFF);
            assert(target + sizeof(uint32_t) <= image.bytes.size());
            uint32_t value = 0;
            std::memcpy(&value, image.bytes.data() + target, sizeof(value));
            value += static_cast<uint32_t>(base - image.base);
            std::memcpy(image.bytes.data() + target, &value, sizeof(value));
        }
        cursor += block.SizeOfBlock;
    }
    image.base = base;
}
void TestInstalled(const wchar_t* exe, const std::wstring& root) {
    auto image = InstalledImage(exe);
    Detail::Addresses addresses;
    assert(Detail::VerifyLayout(image.base, true, Read, &image, addresses));
    Relocate(image, 0xB00000);
    assert(Detail::VerifyLayout(image.base, true, Read, &image, addresses));
    IMAGE_DOS_HEADER runtimeDos{};
    std::memcpy(&runtimeDos, image.bytes.data(), sizeof(runtimeDos));
    const uintptr_t headerBaseField = 0x400000 + static_cast<uintptr_t>(runtimeDos.e_lfanew) +
        offsetof(IMAGE_NT_HEADERS32, OptionalHeader) + offsetof(IMAGE_OPTIONAL_HEADER32, ImageBase);
    Put(image, headerBaseField, uint32_t{0xB00000});
    uintptr_t observedHeader = 0;
    assert(Detail::VerifyLayout(image.base, true, Read, &image, addresses, nullptr, &observedHeader));
    assert(observedHeader == 0xB00000);
    assert(addresses.hours == 0x12FD4C7 && addresses.minutes == 0x12FD4C6);
    assert(addresses.oldWeather == 0x140D070 && addresses.newWeather == 0x140D06C && addresses.rain == 0x140D074);
    // Allocate PRIVATE fixture memory; do not run, patch, or attach to the game.
    void* mapped = VirtualAlloc(nullptr, image.bytes.size(), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    assert(mapped);
    Relocate(image, reinterpret_cast<uintptr_t>(mapped));
    Put(image, headerBaseField, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(mapped)));
    Put(image, 0xBFD4C6, static_cast<unsigned char>(37));
    Put(image, 0xBFD4C7, static_cast<unsigned char>(6));
    Put(image, 0xD0D070, int16_t{8});
    Put(image, 0xD0D06C, int16_t{1});
    Put(image, 0xD0D074, 0.75f);
    std::memcpy(mapped, image.bytes.data(), image.bytes.size());
    Reader reader;
    auto* pendingCode = static_cast<unsigned char*>(mapped) + 0x13AE07;
    *pendingCode ^= 1; // Simulate early code not yet matching, in private fixture memory ONLY.
    assert(!reader.Initialize(reinterpret_cast<uintptr_t>(mapped), true, root));
    assert(std::strcmp(reader.InitializationDiagnostic(), "clock-initialization-mismatch") == 0);
    assert(reader.ModuleBase() == reinterpret_cast<uintptr_t>(mapped) && reader.HeaderImageBase() == reader.ModuleBase());
    assert(!reader.Read(2495, -1687, 13, 0).available);
    *pendingCode ^= 1;
    assert(reader.Initialize(reinterpret_cast<uintptr_t>(mapped), true, root));
    assert(std::strcmp(reader.InitializationDiagnostic(), "ok") == 0);
    assert(reader.HeaderImageBase() == reader.ModuleBase());
    const auto snapshot = reader.Read(2495, -1687, 13, 0);
    assert(snapshot.available && snapshot.hour == 6 && snapshot.minute == 37 && snapshot.weatherId == 8 &&
        snapshot.targetWeatherId == 1 && snapshot.weather == "rainy" && snapshot.rainIntensity == 0.75f);
    assert(snapshot.zoneCode == "GAN1" && snapshot.location == "Ganton");
    assert(reader.Read(2495, -1687, 13, 1).zoneCode.empty());
    assert(!reader.Initialize(reinterpret_cast<uintptr_t>(mapped), false, root));
    assert(!reader.Read(2495, -1687, 13, 0).available);
    assert(VirtualFree(mapped, 0, MEM_RELEASE));
    std::printf("Installed PE six fingerprints + real HIGHLOW relocations verified (file400000, rewritten loaded-headerB00000, private mapping).\n");
    std::printf("Installed zone/GXT metadata sample: %s -> %s. Clock/weather values above are PRIVATE fixtures, not live observations.\n",
        snapshot.zoneCode.c_str(), snapshot.location.c_str());
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc == 3 && std::wcscmp(argv[1], L"--live-process") == 0) {
        wchar_t* end = nullptr;
        errno = 0;
        const unsigned long pid = std::wcstoul(argv[2], &end, 10);
        if (errno || !pid || !end || *end || argv[2][0] < L'0' || argv[2][0] > L'9') {
            std::puts("Invalid PID: supply a positive decimal DWORD.");
            return 2;
        }
        return ProbeLiveProcess(static_cast<DWORD>(pid));
    }
    TestZones();
    TestGxt();
    TestMemoryAndWeather();
    TestDiagnostics();
    TestRuntimeHeaderBase();
    if (argc == 3) TestInstalled(argv[1], argv[2]);
    else std::puts("Installed PE validation not requested; supply EXE and game-root arguments.");
    std::puts("world-context fixtures passed");
    return 0;
}
