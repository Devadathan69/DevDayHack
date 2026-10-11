#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace NpcWorld {
struct Snapshot {
    bool available = false; // At least one genuine signal, not necessarily all three.
    std::string location, zoneCode;
    int hour = -1, minute = -1;
    int weatherId = -1; // Engine OldWeatherType, not a claim that the blend is complete.
    int targetWeatherId = -1; // Engine NewWeatherType; never an observed condition.
    std::string weather; // Rain>0.05 is rainy; differing IDs or unobserved categorical rain are transitioning.
    float rainIntensity = -1.0f;
};

namespace Detail {
struct Zone {
    std::string code, textKey, label;
    float minimum[3]{}, maximum[3]{};
    double volume = 0;
};
// Public pure helpers permit offline fixtures without a running game.
bool ParseZones(std::string_view text, std::vector<Zone>& zones);
uint32_t GxtKey(std::string_view key) noexcept;
bool ResolveGxtLabels(std::string_view bytes, std::vector<Zone>& zones);
const Zone* FindZone(const std::vector<Zone>& zones, float x, float y, float z,
    unsigned char area) noexcept;
const char* WeatherCondition(int id) noexcept;
struct Addresses {
    uintptr_t minutes = 0, hours = 0, oldWeather = 0, newWeather = 0, rain = 0;
};
using MemoryRead = bool (*)(uintptr_t address, void* output, size_t bytes, void* context) noexcept;
bool VerifyLayout(uintptr_t moduleBase, bool installed2014, MemoryRead read, void* context,
    Addresses& addresses, const char** diagnostic = nullptr, uintptr_t* headerImageBase = nullptr) noexcept;
Snapshot Sample(const Addresses& addresses, MemoryRead read, void* context);
}

class Reader {
public:
    // Only the fingerprint-verified 2014 x86 layout is supported. Failure resets state.
    bool Initialize(uintptr_t moduleBase, bool installed2014, const std::wstring& gameRoot) noexcept;
    const char* InitializationDiagnostic() const noexcept { return diagnostic_; }
    uintptr_t HeaderImageBase() const noexcept { return headerImageBase_; }
    uintptr_t ModuleBase() const noexcept { return moduleBase_; }
    // Coordinates/area are the parent's already-validated player CPed observation.
    Snapshot Read(float x, float y, float z, unsigned char area) noexcept;
private:
    const char* diagnostic_ = "not-initialized";
    uintptr_t moduleBase_ = 0, headerImageBase_ = 0;
    Detail::Addresses addresses_{};
    std::vector<Detail::Zone> zones_;
};
}
