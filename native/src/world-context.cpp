#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "world-context.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <locale>
#include <sstream>

static_assert(sizeof(uintptr_t) == 4, "World context supports the x86 game only");

namespace {
constexpr uintptr_t PreferredBase = 0x400000;
constexpr uint32_t ImageSize = 0x985000;
constexpr size_t FileLimit = 1024 * 1024, ZoneLimit = 4096, GxtLimit = 8 * 1024 * 1024;
constexpr uintptr_t Hours = 0xBFD4C7, Minutes = 0xBFD4C6;
constexpr uintptr_t OldWeather = 0xD0D070, NewWeather = 0xD0D06C, Rain = 0xD0D074;

bool SafeAddress(uintptr_t address, size_t bytes) noexcept {
    return address >= 0x10000u && bytes > 0 &&
        static_cast<uint64_t>(address) + bytes <= 0x100000000ull;
}

// No objects requiring unwinding in this SEH boundary. No writes to game memory.
bool ProcessRead(uintptr_t address, void* output, size_t bytes, void*) noexcept {
    if (!output || !SafeAddress(address, bytes)) return false;
    __try {
        std::memcpy(output, reinterpret_cast<const void*>(address), bytes);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

uintptr_t Resolve(uintptr_t base, uintptr_t preferred) noexcept {
    return base + preferred - PreferredBase;
}

void Operand(unsigned char* code, size_t offset, uintptr_t address) noexcept {
    const uint32_t value = static_cast<uint32_t>(address);
    std::memcpy(code + offset, &value, sizeof(value));
}

template<size_t N> bool Matches(uintptr_t address, const unsigned char (&expected)[N],
    NpcWorld::Detail::MemoryRead read, void* context, const char* unreadable,
    const char* mismatch, const char*& diagnostic) noexcept {
    unsigned char actual[N]{};
    if (!read(address, actual, N, context)) { diagnostic = unreadable; return false; }
    if (std::memcmp(actual, expected, N) != 0) { diagnostic = mismatch; return false; }
    return true;
}

bool CodeFingerprints(uintptr_t base, NpcWorld::Detail::MemoryRead read, void* context,
    const char*& diagnostic) noexcept {
    // Original fingerprints from installed SHA256 AA07FCDC...95034FE3, dumpbin /disasm:bytes.
    // CClock::Initialise sets hours=12/minutes=0; Update compares minutes with 60 then hours with 24.
    unsigned char clockInit[] = {
        0xC6,0x05,0,0,0,0,0x0C,0xA2,0,0,0,0,
        0x66,0x89,0x15,0,0,0,0,0xA2,0,0,0,0,
        0xC6,0x05,0,0,0,0,0x04
    };
    Operand(clockInit, 2, Resolve(base, Hours));
    Operand(clockInit, 8, Resolve(base, Minutes));
    Operand(clockInit, 15, Resolve(base, 0xBFD4C4));
    Operand(clockInit, 20, Resolve(base, 0xBFD4BC));
    Operand(clockInit, 26, Resolve(base, 0xBFD4C3));
    unsigned char rollover[] = {
        0x80,0x3D,0,0,0,0,0x3C,0x0F,0x82,0x83,0,0,0,
        0xA0,0,0,0,0,0x53,0xB3,0x01,0x02,0xC3,
        0x88,0x15,0,0,0,0,0xA2,0,0,0,0,0x3C,0x18
    };
    Operand(rollover, 2, Resolve(base, Minutes));
    Operand(rollover, 14, Resolve(base, Hours));
    Operand(rollover, 25, Resolve(base, Minutes));
    Operand(rollover, 30, Resolve(base, Hours));
    // ForceWeatherNow stores the argument to Forced, Old, then New, independently identifying New.
    unsigned char force[] = {
        0x55,0x8B,0xEC,0x66,0x8B,0x45,0x08,
        0x66,0xA3,0,0,0,0,0x66,0xA3,0,0,0,0,
        0x66,0xA3,0,0,0,0,0x5D,0xC3
    };
    Operand(force, 9, Resolve(base, 0xD0D068));
    Operand(force, 15, Resolve(base, OldWeather));
    Operand(force, 21, Resolve(base, NewWeather));
    // CWeather::Update tests actual rainy enum IDs 16/8 for both old and new weather.
    constexpr unsigned char rainy[] = {
        0x66,0x83,0xF9,0x10,0x74,0x06,0x66,0x83,0xF9,0x08,0x75,0x03,
        0xD9,0x55,0xF8,0x66,0x83,0xFE,0x10,0x74,0x06,0x66,0x83,0xFE,0x08,0x75,0x0A
    };
    unsigned char rainLoad[] = {
        0xD9,0x05,0,0,0,0,0xD9,0xC0,0xDE,0xEA,0xD9,0xC9,
        0xD9,0x5D,0xF8,0xD9,0x45,0xF8
    };
    Operand(rainLoad, 2, Resolve(base, Rain));
    unsigned char rainStore[] = {0xD9,0xC9,0xD9,0x1D,0,0,0,0,0xD9,0x55,0xF8,0xD9,0x05,0,0,0,0};
    Operand(rainStore, 4, Resolve(base, Rain));
    Operand(rainStore, 13, Resolve(base, 0xD0D05C));
    return Matches(Resolve(base, 0x53AE07), clockInit, read, context,
            "clock-initialization-unreadable", "clock-initialization-mismatch", diagnostic) &&
        Matches(Resolve(base, 0x53AFC6), rollover, read, context,
            "clock-rollover-unreadable", "clock-rollover-mismatch", diagnostic) &&
        Matches(Resolve(base, 0x75CA60), force, read, context,
            "weather-force-unreadable", "weather-force-mismatch", diagnostic) &&
        Matches(Resolve(base, 0x75E18F), rainy, read, context,
            "rain-enums-unreadable", "rain-enums-mismatch", diagnostic) &&
        Matches(Resolve(base, 0x75E1FC), rainLoad, read, context,
            "rain-load-unreadable", "rain-load-mismatch", diagnostic) &&
        Matches(Resolve(base, 0x75E24D), rainStore, read, context,
            "rain-store-unreadable", "rain-store-mismatch", diagnostic);
}

bool Token(const std::string& value) noexcept {
    if (value.empty() || value.size() > 8) return false;
    for (const unsigned char c : value) {
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return true;
}

bool ReadLocalFile(const std::wstring& path, size_t limit, std::string& contents) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || static_cast<uint64_t>(size.QuadPart) > limit) {
        CloseHandle(file);
        return false;
    }
    try { contents.resize(static_cast<size_t>(size.QuadPart)); }
    catch (...) { CloseHandle(file); throw; }
    DWORD got = 0;
    const bool ok = ReadFile(file, contents.data(), static_cast<DWORD>(contents.size()), &got, nullptr) != FALSE &&
        got == contents.size();
    CloseHandle(file);
    return ok;
}

bool ReadZoneFile(const std::wstring& path, std::vector<NpcWorld::Detail::Zone>& zones) {
    std::string contents;
    return ReadLocalFile(path, FileLimit, contents) && NpcWorld::Detail::ParseZones(contents, zones);
}

uint32_t Little32(std::string_view bytes, size_t offset) noexcept {
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value |= static_cast<uint32_t>(static_cast<unsigned char>(bytes[offset + i])) << (i * 8);
    return value;
}

bool Tag(std::string_view bytes, size_t offset, const char* tag) noexcept {
    return offset <= bytes.size() && bytes.size() - offset >= 4 &&
        std::memcmp(bytes.data() + offset, tag, 4) == 0;
}
}

namespace NpcWorld::Detail {
bool VerifyLayout(uintptr_t moduleBase, bool installed2014, MemoryRead read, void* context,
    Addresses& addresses, const char** diagnostic, uintptr_t* headerImageBase) noexcept {
    addresses = {};
    if (headerImageBase) *headerImageBase = 0;
    if (diagnostic) *diagnostic = "not-verified";
    const auto fail = [diagnostic](const char* reason) noexcept {
        if (diagnostic) *diagnostic = reason;
        return false;
    };
    if (!installed2014) return fail("unsupported-layout");
    if (!read) return fail("invalid-memory-reader");
    if (!SafeAddress(moduleBase, ImageSize)) return fail("module-range");
    IMAGE_DOS_HEADER dos{};
    if (!read(moduleBase, &dos, sizeof(dos), context)) return fail("dos-header-unreadable");
    if (dos.e_magic != IMAGE_DOS_SIGNATURE) return fail("dos-signature");
    if (dos.e_lfanew < static_cast<LONG>(sizeof(dos)) || dos.e_lfanew > 0x1000) return fail("nt-offset");
    IMAGE_NT_HEADERS32 nt{};
    const uintptr_t ntAddress = moduleBase + static_cast<uintptr_t>(dos.e_lfanew);
    if (!read(ntAddress, &nt, sizeof(nt), context)) return fail("nt-header-unreadable");
    if (nt.Signature != IMAGE_NT_SIGNATURE) return fail("pe-signature");
    if (nt.FileHeader.Machine != IMAGE_FILE_MACHINE_I386) return fail("pe-machine");
    if (nt.FileHeader.NumberOfSections == 0 || nt.FileHeader.NumberOfSections > 16) return fail("pe-section-count");
    if (nt.FileHeader.TimeDateStamp != 0x547CD3A3) return fail("pe-timestamp");
    if (nt.FileHeader.SizeOfOptionalHeader != sizeof(IMAGE_OPTIONAL_HEADER32)) return fail("pe-optional-header-size");
    if (nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) return fail("pe-optional-header-magic");
    if (headerImageBase) *headerImageBase = nt.OptionalHeader.ImageBase;
    // Some loaders rewrite this header to the mapped base. File RVAs still use known base400000,
    // and neither accepted header form can bypass the identity/sections/all six relocated fingerprints.
    if (nt.OptionalHeader.ImageBase != PreferredBase && nt.OptionalHeader.ImageBase != moduleBase)
        return fail("pe-image-base");
    if (nt.OptionalHeader.SizeOfImage != ImageSize) return fail("pe-image-size");
    if (nt.OptionalHeader.AddressOfEntryPoint != 0x458DB8) return fail("pe-entry-point");
    IMAGE_SECTION_HEADER sections[16]{};
    if (!read(ntAddress + sizeof(nt), sections,
        static_cast<size_t>(nt.FileHeader.NumberOfSections) * sizeof(sections[0]), context))
        return fail("pe-sections-unreadable");
    const auto inSection = [&](uintptr_t preferred, size_t bytes, DWORD flags) noexcept {
        const uint64_t rva = preferred - PreferredBase;
        for (WORD i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
            const auto& section = sections[i];
            const uint64_t end = static_cast<uint64_t>(section.VirtualAddress) + section.Misc.VirtualSize;
            if (end <= ImageSize && rva >= section.VirtualAddress && rva + bytes <= end &&
                (section.Characteristics & flags) == flags) return true;
        }
        return false;
    };
    constexpr DWORD CodeFlags = IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE;
    constexpr DWORD DataFlags = IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE;
    if (!inSection(0x53AE07, 31, CodeFlags) || !inSection(0x53AFC6, 36, CodeFlags) ||
        !inSection(0x75CA60, 27, CodeFlags) || !inSection(0x75E18F, 27, CodeFlags) ||
        !inSection(0x75E1FC, 18, CodeFlags) || !inSection(0x75E24D, 17, CodeFlags))
        return fail("pe-code-section");
    if (!inSection(Minutes, 2, DataFlags) || !inSection(OldWeather, 2, DataFlags) ||
        !inSection(NewWeather, 2, DataFlags) || !inSection(Rain, 4, DataFlags))
        return fail("pe-data-section");
    const char* fingerprintDiagnostic = "not-verified";
    if (!CodeFingerprints(moduleBase, read, context, fingerprintDiagnostic)) return fail(fingerprintDiagnostic);
    addresses.minutes = Resolve(moduleBase, Minutes);
    addresses.hours = Resolve(moduleBase, Hours);
    addresses.oldWeather = Resolve(moduleBase, OldWeather);
    addresses.newWeather = Resolve(moduleBase, NewWeather);
    addresses.rain = Resolve(moduleBase, Rain);
    if (diagnostic) *diagnostic = "ok";
    return true;
}

bool ParseZones(std::string_view text, std::vector<Zone>& zones) {
    if (text.empty() || text.size() > FileLimit || zones.size() > ZoneLimit) return false;
    std::vector<Zone> parsed;
    std::istringstream file{std::string(text)};
    file.imbue(std::locale::classic());
    bool inside = false, ended = false;
    size_t lines = 0;
    std::string line;
    while (std::getline(file, line)) {
        if (++lines > 16384 || line.size() > 512) return false;
        const auto comment = line.find('#');
        if (comment != std::string::npos) line.resize(comment);
        const auto first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos) continue;
        line = line.substr(first, line.find_last_not_of(" \t\r") - first + 1);
        if (line == "zone") { if (inside || ended) return false; inside = true; continue; }
        if (line == "end") { if (!inside) return false; ended = true; inside = false; continue; }
        if (!inside) continue;
        if (line.find(',') != std::string::npos) {
            if (std::count(line.begin(), line.end(), ',') != 9) continue;
            std::string_view fields(line);
            bool emptyField = false;
            while (!fields.empty()) {
                const auto comma = fields.find(',');
                const auto field = fields.substr(0, comma);
                if (field.find_first_not_of(" \t\r") == std::string_view::npos) emptyField = true;
                if (comma == std::string_view::npos) break;
                fields.remove_prefix(comma + 1);
                if (fields.empty()) emptyField = true;
            }
            if (emptyField) continue;
        }
        std::replace(line.begin(), line.end(), ',', ' ');
        std::istringstream record(line);
        record.imbue(std::locale::classic());
        Zone zone;
        int type = -1, island = -1;
        std::string label, extra;
        if (!(record >> zone.code >> type >> zone.minimum[0] >> zone.minimum[1] >> zone.minimum[2] >>
            zone.maximum[0] >> zone.maximum[1] >> zone.maximum[2] >> island >> label) ||
            (record >> extra) || !Token(zone.code) || !Token(label) ||
            (type != 0 && type != 3) || island < 0 || island > 3) continue;
        bool valid = true;
        zone.volume = 1;
        for (unsigned i = 0; i < 3; ++i) {
            if (!std::isfinite(zone.minimum[i]) || !std::isfinite(zone.maximum[i]) ||
                std::fabs(zone.minimum[i]) > 20000 || std::fabs(zone.maximum[i]) > 20000 ||
                zone.minimum[i] >= zone.maximum[i]) valid = false;
            zone.volume *= static_cast<double>(zone.maximum[i]) - zone.minimum[i];
        }
        if (!valid) continue;
        if (parsed.size() + zones.size() >= ZoneLimit) return false;
        zone.textKey = std::move(label);
        parsed.push_back(std::move(zone));
    }
    if (!ended || inside || parsed.empty()) return false;
    zones.insert(zones.end(), parsed.begin(), parsed.end());
    return true;
}

uint32_t GxtKey(std::string_view key) noexcept {
    // GTA uses the reflected IEEE CRC32 remainder, initialized to all ones, WITHOUT final XOR.
    uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char c : key) {
        if (c >= 'a' && c <= 'z') c = static_cast<unsigned char>(c - ('a' - 'A'));
        crc ^= c;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
    }
    return crc;
}

bool ResolveGxtLabels(std::string_view bytes, std::vector<Zone>& zones) {
    if (bytes.size() < 24 || bytes.size() > GxtLimit || zones.size() > ZoneLimit ||
        Little32(bytes, 0) != 0x00080004 || !Tag(bytes, 4, "TABL")) return false;
    const size_t tableBytes = Little32(bytes, 8);
    if (!tableBytes || tableBytes > 64 * 1024 || tableBytes % 12 || tableBytes > bytes.size() - 12)
        return false;
    const size_t tableEnd = 12 + tableBytes;
    size_t mainOffset = 0, mainEnd = bytes.size();
    for (size_t entry = 12; entry < tableEnd; entry += 12) {
        const size_t offset = Little32(bytes, entry + 8);
        if (offset < tableEnd || offset >= bytes.size()) return false;
        if (std::memcmp(bytes.data() + entry, "MAIN\0\0\0\0", 8) == 0) {
            if (mainOffset) return false;
            mainOffset = offset;
        }
    }
    if (!mainOffset) return false;
    for (size_t entry = 12; entry < tableEnd; entry += 12) {
        const size_t offset = Little32(bytes, entry + 8);
        if (offset > mainOffset && offset < mainEnd) mainEnd = offset;
    }
    if (mainEnd - mainOffset < 16 || !Tag(bytes, mainOffset, "TKEY")) return false;
    const size_t keyBytes = Little32(bytes, mainOffset + 4);
    if (!keyBytes || keyBytes > 512 * 1024 || keyBytes % 8 || keyBytes > mainEnd - mainOffset - 16)
        return false;
    const size_t keys = mainOffset + 8, dataHeader = keys + keyBytes;
    if (!Tag(bytes, dataHeader, "TDAT")) return false;
    const size_t dataBytes = Little32(bytes, dataHeader + 4), data = dataHeader + 8;
    if (!dataBytes || dataBytes > 4 * 1024 * 1024 || dataBytes > mainEnd - data) return false;
    const size_t count = keyBytes / 8;
    uint32_t previous = 0;
    for (size_t i = 0; i < count; ++i) {
        const size_t entry = keys + i * 8;
        const uint32_t hash = Little32(bytes, entry + 4);
        if (Little32(bytes, entry) >= dataBytes || (i && hash <= previous)) return false;
        previous = hash;
    }
    // Only requested local zone names survive initialization; no GXT table/assets are retained or shipped.
    for (auto& zone : zones) {
        zone.label.clear();
        if (!Token(zone.textKey)) continue;
        const uint32_t hash = GxtKey(zone.textKey);
        size_t low = 0, high = count;
        while (low < high) {
            const size_t middle = low + (high - low) / 2;
            if (Little32(bytes, keys + middle * 8 + 4) < hash) low = middle + 1;
            else high = middle;
        }
        if (low == count || Little32(bytes, keys + low * 8 + 4) != hash) continue;
        const size_t offset = Little32(bytes, keys + low * 8);
        const auto text = bytes.substr(data + offset, (std::min)(size_t{81}, dataBytes - offset));
        const auto length = text.find('\0');
        if (length == std::string_view::npos || length == 0 || length > 80) continue;
        const auto label = text.substr(0, length);
        bool printable = label.find_first_not_of(' ') != std::string_view::npos;
        for (const unsigned char c : label)
            if (c < 0x20 || c > 0x7E || c == '~') printable = false;
        // American ASCII names need no guessed conversion of GTA's custom 8-bit font glyphs.
        if (printable) zone.label.assign(label.data(), label.size());
    }
    return true;
}

const Zone* FindZone(const std::vector<Zone>& zones, float x, float y, float z, unsigned char area) noexcept {
    if (area != 0 || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
        std::fabs(x) > 20000 || std::fabs(y) > 20000 || std::fabs(z) > 20000) return nullptr;
    const float point[] = {x, y, z};
    const Zone* result = nullptr;
    for (const auto& zone : zones) {
        bool contains = true;
        for (unsigned i = 0; i < 3; ++i)
            if (point[i] < zone.minimum[i] || point[i] > zone.maximum[i]) contains = false;
        if (contains && (!result || zone.volume < result->volume)) result = &zone;
    }
    return result;
}

const char* WeatherCondition(int id) noexcept {
    switch (id) {
    case 0: case 6: case 11: case 13: case 17: return "extra-sunny";
    case 1: case 5: case 10: case 14: case 18: return "sunny";
    case 2: case 3: return "smoggy";
    case 4: case 7: case 12: case 15: return "cloudy";
    case 8: case 16: return "rainy";
    case 9: return "foggy";
    case 19: return "sandstorm";
    case 20: return "underwater";
    case 21: case 22: return "extra-colours";
    default: return "";
    }
}

Snapshot Sample(const Addresses& addresses, MemoryRead read, void* context) {
    Snapshot snapshot;
    if (!read) return snapshot;
    if (addresses.minutes && addresses.hours == addresses.minutes + 1) {
        unsigned char first[2]{}, second[2]{};
        for (unsigned attempt = 0; attempt < 3; ++attempt) {
            if (!read(addresses.minutes, first, sizeof(first), context) ||
                !read(addresses.minutes, second, sizeof(second), context)) break;
            if (std::memcmp(first, second, sizeof(first)) == 0 && first[0] <= 59 && first[1] <= 23) {
                snapshot.minute = first[0];
                snapshot.hour = first[1];
                break;
            }
        }
    }
    int16_t weather = -1;
    if (addresses.oldWeather && read(addresses.oldWeather, &weather, sizeof(weather), context) &&
        weather >= 0 && weather <= 22) snapshot.weatherId = weather;
    weather = -1;
    if (addresses.newWeather && read(addresses.newWeather, &weather, sizeof(weather), context) &&
        weather >= 0 && weather <= 22) snapshot.targetWeatherId = weather;
    float rain = -1;
    if (addresses.rain && read(addresses.rain, &rain, sizeof(rain), context) &&
        std::isfinite(rain) && rain >= 0 && rain <= 1) snapshot.rainIntensity = rain;
    // Target differences describe a transition, not completed conditions. Unmeasured rain stays unknown.
    if (snapshot.rainIntensity > 0.05f) snapshot.weather = "rainy";
    else if (snapshot.rainIntensity >= 0 && snapshot.weatherId >= 0 && snapshot.targetWeatherId >= 0) {
        if (snapshot.weatherId != snapshot.targetWeatherId || snapshot.weatherId == 8 || snapshot.weatherId == 16)
            snapshot.weather = "transitioning";
        else snapshot.weather = WeatherCondition(snapshot.weatherId);
    }
    snapshot.available = snapshot.hour >= 0 || snapshot.weatherId >= 0 || snapshot.rainIntensity >= 0;
    return snapshot;
}
}

namespace NpcWorld {
bool Reader::Initialize(uintptr_t moduleBase, bool installed2014, const std::wstring& gameRoot) noexcept {
    addresses_ = {};
    zones_.clear();
    moduleBase_ = moduleBase;
    headerImageBase_ = 0;
    if (!Detail::VerifyLayout(moduleBase, installed2014, &ProcessRead, nullptr, addresses_,
        &diagnostic_, &headerImageBase_)) return false;
    try {
        if (!gameRoot.empty() && gameRoot.size() <= 32700) {
            std::wstring prefix = gameRoot;
            if (prefix.back() != L'\\') prefix += L'\\';
            ReadZoneFile(prefix + L"data\\info.zon", zones_);
            ReadZoneFile(prefix + L"data\\map.zon", zones_);
            std::string gxt;
            if (!zones_.empty() && ReadLocalFile(prefix + L"text\\american.gxt", GxtLimit, gxt))
                Detail::ResolveGxtLabels(gxt, zones_);
        }
    } catch (...) { zones_.clear(); }
    return true; // Clock/weather are usable even when optional local zone files are unavailable.
}

Snapshot Reader::Read(float x, float y, float z, unsigned char area) noexcept {
    try {
        Snapshot snapshot = Detail::Sample(addresses_, &ProcessRead, nullptr);
        if (const auto* zone = Detail::FindZone(zones_, x, y, z, area)) {
            snapshot.zoneCode = zone->code;
            snapshot.location = zone->label.empty() ? "Zone " + zone->code : zone->label;
            snapshot.available = true;
        }
        return snapshot;
    } catch (...) { return {}; }
}
}
