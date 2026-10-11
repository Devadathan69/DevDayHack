#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <WinHttp.h>
#include <objidl.h>
#include <gdiplus.h>
#include <mmsystem.h>

#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <algorithm>
#include "interaction.h"
#include "game-layout.h"
#include "audio-file.h"
#include "ped-hold.h"
#include "ped-metadata.h"
#include "microphone.h"
#include "world-context.h"
#include "world-wire.h"
#include "d3d-overlay.h"

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "msimg32.lib")

// ============================================================================
// Section 1: Configuration
// ============================================================================

namespace {

uintptr_t g_playerPedAddress = 0;
uintptr_t g_pedPoolAddress = 0;
uintptr_t g_standstillProcessAddress = 0;
constexpr size_t PED_POOL_SLOT_SIZE = 0x7C4;
constexpr float INTERACT_RADIUS = NpcInteraction::StartRadius;
constexpr DWORD OVERLAY_FPS  = 30;
constexpr DWORD TIMER_MS     = 1000 / OVERLAY_FPS;
constexpr INTERNET_PORT DEFAULT_BACKEND_PORT = 4177;
constexpr int   MAX_INPUT_LEN = 500;
constexpr DWORD SUBTITLE_MIN_MS = 3000;
constexpr DWORD SUBTITLE_PER_CHAR_MS = 80;
constexpr UINT_PTR TIMER_ID  = 42001;
constexpr UINT AUDIO_STOP_MESSAGE = WM_APP + 41;
constexpr UINT AUDIO_PLAY_MESSAGE = WM_APP + 42;
constexpr UINT AUDIO_WRITE_MESSAGE = WM_APP + 43;
constexpr UINT MIC_START_MESSAGE = WM_APP + 44;
constexpr UINT MIC_STOP_MESSAGE = WM_APP + 45;

const wchar_t* const OVERLAY_CLASS = L"MalayalamNpcOverlay_v1";
const wchar_t* const BACKEND_HOST  = L"127.0.0.1";

// ============================================================================
// Section 2: Data structures
// ============================================================================

enum class UiState {
    HIDDEN,        // Nothing to show
    PROMPT,        // "Press F6 / Enter to talk to Manu"
    GREETING,      // Preparing the standing NPC and its personality
    SUBTITLE,      // Showing NPC reply (+ optional audio)
    READY,         // Ready for player input
    TYPING,        // Player is typing
    PROCESSING,    // Waiting for backend
    ERROR_SHOW,    // Showing an error message
    RECORDING,     // R is held; microphone capture stays on the UI thread
};

struct OverlayData {
    std::mutex       mtx;
    UiState          state        = UiState::HIDDEN;
    std::wstring     subtitle;
    std::wstring     npcName      = L"pedestrian";
    std::wstring     inputText;
    std::wstring     recognizedText;
    std::wstring     errorText;
    std::wstring     statusText;
    std::wstring     audioPath;
    DWORD            subtitleEnd  = 0;
    bool             backendOk   = false;
    bool             nearNpc     = false;
    int              cursorBlink = 0;
    NpcInteraction::Ped nearbyPed;
    NpcInteraction::Ped selectedPed;
    unsigned long    subtitleGeneration = 0;
    uint64_t         subtitleFrame = 0;
    bool             audioPlaying = false;
    NpcWorld::Snapshot world;
    NpcInteraction::Ped worldPlayer;
    uint64_t worldCapturedAt = 0;
};

// ============================================================================
// Section 3: Globals
// ============================================================================

volatile LONG       g_initState  = 0;
std::atomic<bool>   g_running{false};
HANDLE              g_thread     = nullptr;
DWORD               g_threadId   = 0;
HANDLE              g_stopEvent  = nullptr;
HANDLE              g_heartbeatThread = nullptr;
HWND                g_overlay    = nullptr;
HWND                g_gameWnd    = nullptr;
HHOOK               g_kbHook     = nullptr;
OverlayData         g_data;
std::mutex          g_bridgeConfigMutex;
std::string         g_bridgeToken;
INTERNET_PORT       g_backendPort = DEFAULT_BACKEND_PORT;
ULONG_PTR           g_gdipToken  = 0;
std::wstring        g_tempDir;
std::atomic<bool>  g_gameLayoutSupported{false};
std::atomic<unsigned long> g_conversationGeneration{0};
std::mutex g_turnMutex;
std::atomic<bool> g_cancelPending{false};
std::atomic<unsigned int> g_selectedPedHandle{0};
std::atomic<unsigned int> g_sceneEpoch{0};
std::atomic<unsigned int> g_heldKeys[256]{};
NpcPedHold::Holder g_pedHolder;
std::atomic<unsigned long> g_heldConversation{0};
std::atomic<unsigned long> g_holdFailure{0};
std::atomic<unsigned long> g_loggedPersonaGeneration{0};
std::atomic<float> g_nearbyFemaleDistance{-1.0f};
std::atomic<float> g_nearbyMaleDistance{-1.0f};
NpcPedMetadata::Table g_modelGender{};
NpcWorld::Reader g_worldReader;
bool g_installed2014 = false;
bool g_worldReady = false;
DWORD g_gameHeaderImageBase = 0;
NpcMicrophone::Recorder g_microphone;
unsigned long g_recordGeneration = 0;
DWORD g_recordDownTime = 0;
wchar_t g_recordTapCharacter = L'r';
std::wstring g_recordError;
bool g_recordStartPosted = false;

// ============================================================================
// Section 4: Utility — safe memory, string conversion, logging
// ============================================================================

static bool ReadMemorySafe(const void* addr, void* buf, size_t sz) {
    __try {
        memcpy(buf, addr, sz);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (len <= 0) return {};
    std::wstring r(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), r.data(), len);
    return r;
}

std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string r(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), r.data(), len, nullptr, nullptr);
    return r;
}

std::string EscapeJsonString(const std::string& value) {
    static constexpr char HEX[] = "0123456789abcdef";
    std::string escaped;
    escaped.reserve(value.size() + 16);
    for (unsigned char c : value) {
        switch (c) {
        case '"': escaped += "\\\""; break;
        case '\\': escaped += "\\\\"; break;
        case '\b': escaped += "\\b"; break;
        case '\f': escaped += "\\f"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default:
            if (c < 0x20) {
                escaped += "\\u00";
                escaped += HEX[c >> 4];
                escaped += HEX[c & 0x0F];
            } else {
                escaped += static_cast<char>(c);
            }
        }
    }
    return escaped;
}

bool EnsureDir(const std::wstring& path) {
    if (CreateDirectoryW(path.c_str(), nullptr)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

bool DetectGameLayout() {
    HMODULE module = GetModuleHandleW(nullptr);
    if (!module) return false;
    const uintptr_t runtimeBase = reinterpret_cast<uintptr_t>(module);
    IMAGE_DOS_HEADER dos{};
    if (!ReadMemorySafe(module, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew <= 0 || dos.e_lfanew > 0x10000) return false;
    IMAGE_NT_HEADERS nt{};
    if (!ReadMemorySafe(reinterpret_cast<const BYTE*>(module) + dos.e_lfanew, &nt, sizeof(nt)) ||
        nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_I386) return false;
    g_gameHeaderImageBase = nt.OptionalHeader.ImageBase;
    const DWORD entry = nt.OptionalHeader.AddressOfEntryPoint;
    if (entry == 0x424570 || entry == 0x42457C) {
        if (runtimeBase != 0x400000) return false;
        g_playerPedAddress = 0xB6F5F0;
        g_pedPoolAddress = 0xB74490;
        g_standstillProcessAddress = 0x62F370;
        return true;
    }
    // The installed 2014 layout has different globals. Verify both code references,
    // not just its entry point, before reading any player or pool memory.
    if (entry != NpcGameLayout::InstalledEntry || nt.OptionalHeader.SizeOfImage != NpcGameLayout::InstalledImageSize) return false;
    BYTE playerBytes[sizeof(NpcGameLayout::PlayerCode)]{}, poolBytes[sizeof(NpcGameLayout::PoolCode)]{};
    if (!ReadMemorySafe(reinterpret_cast<const void*>(NpcGameLayout::Resolve(runtimeBase, NpcGameLayout::PlayerCodeAddress)), playerBytes, sizeof(playerBytes)) ||
        !ReadMemorySafe(reinterpret_cast<const void*>(NpcGameLayout::Resolve(runtimeBase, NpcGameLayout::PoolCodeAddress)), poolBytes, sizeof(poolBytes)) ||
        !NpcGameLayout::MatchesInstalled(entry, nt.OptionalHeader.SizeOfImage, playerBytes, poolBytes, runtimeBase)) return false;
    BYTE standstillBytes[sizeof(NpcGameLayout::StandstillCode)]{};
    BYTE taskTypeBytes[sizeof(NpcGameLayout::StandstillTaskType)]{};
    const uintptr_t standstill = NpcGameLayout::Resolve(runtimeBase, NpcGameLayout::StandstillProcessAddress);
    if (!ReadMemorySafe(reinterpret_cast<const void*>(standstill), standstillBytes, sizeof(standstillBytes)) ||
        !ReadMemorySafe(reinterpret_cast<const void*>(NpcGameLayout::Resolve(runtimeBase, NpcGameLayout::StandstillTaskTypeAddress)), taskTypeBytes, sizeof(taskTypeBytes)) ||
        !NpcGameLayout::MatchesStandstill(standstillBytes, taskTypeBytes)) return false;
    g_playerPedAddress = NpcGameLayout::Resolve(runtimeBase, 0xC100D0);
    g_pedPoolAddress = NpcGameLayout::Resolve(runtimeBase, 0xC018A8);
    g_standstillProcessAddress = standstill;
    g_installed2014 = true;
    return true;
}

void WriteLog(const wchar_t* msg) {
    DWORD req = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (req == 0 || req > 32767) return;
    std::wstring base(req, L'\0');
    DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", base.data(), req);
    if (len == 0 || len >= req) return;
    base.resize(len);
    base += L"\\MalayalamNpcDemo";
    if (!EnsureDir(base)) return;
    std::wstring logDir = base + L"\\logs";
    if (!EnsureDir(logDir)) return;
    HANDLE f = CreateFileW((logDir + L"\\game-bridge.log").c_str(),
        FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME now{}; GetLocalTime(&now);
    wchar_t line[512]{};
    int n = swprintf_s(line, L"[%04u-%02u-%02u %02u:%02u:%02u] %s\r\n",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, msg);
    if (n > 0) { DWORD bw = 0; WriteFile(f, line, (DWORD)(n * sizeof(wchar_t)), &bw, nullptr); }
    CloseHandle(f);
}

std::wstring GameDirectory() {
    wchar_t modulePath[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    if (!length || length >= MAX_PATH) return {};
    std::wstring path(modulePath, length);
    const auto separator = path.find_last_of(L"\\");
    if (separator == std::wstring::npos) return {};
    path.resize(separator);
    return path;
}

uint64_t UnixMilliseconds() {
    FILETIME time{};
    GetSystemTimeAsFileTime(&time);
    ULARGE_INTEGER value{};
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    return value.QuadPart / 10000ULL - 11644473600000ULL;
}

bool LoadModelGenderMetadata() {
    std::wstring path = GameDirectory();
    if (path.empty()) return false;
    path += L"\\data\\peds.ide";
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 1024 * 1024) {
        CloseHandle(file);
        return false;
    }
    std::string contents(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    const BOOL ok = ReadFile(file, contents.data(), static_cast<DWORD>(contents.size()), &read, nullptr);
    CloseHandle(file);
    if (!ok || read != contents.size()) return false;
    g_modelGender = NpcPedMetadata::Parse(contents);
    return std::any_of(g_modelGender.begin(), g_modelGender.end(), [](unsigned char value) { return value != 0; });
}

// Minimal JSON string value extractor (handles escapes + UTF-8)
std::string ExtractJsonString(const std::string& json, const char* key) {
    std::string search = std::string("\"") + key + "\":\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return {};
    pos += search.size();
    std::string result;
    result.reserve(256);
    for (size_t i = pos; i < json.size(); i++) {
        char c = json[i];
        if (c == '"') break;
        if (c == '\\' && i + 1 < json.size()) {
            char n = json[i + 1];
            if (n == '"') { result += '"'; i++; }
            else if (n == '\\') { result += '\\'; i++; }
            else if (n == '/') { result += '/'; i++; }
            else if (n == 'n') { result += '\n'; i++; }
            else if (n == 'r') { result += '\r'; i++; }
            else if (n == 't') { result += '\t'; i++; }
            else if (n == 'u' && i + 5 < json.size()) {
                unsigned cp = 0;
                for (int j = 0; j < 4; j++) {
                    char h = json[i + 2 + j]; cp <<= 4;
                    if (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
                    else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
                }
                if (cp < 0x80) result += (char)cp;
                else if (cp < 0x800) {
                    result += (char)(0xC0 | (cp >> 6));
                    result += (char)(0x80 | (cp & 0x3F));
                } else {
                    result += (char)(0xE0 | (cp >> 12));
                    result += (char)(0x80 | ((cp >> 6) & 0x3F));
                    result += (char)(0x80 | (cp & 0x3F));
                }
                i += 5;
            } else { result += c; }
        } else { result += c; }
    }
    return result;
}

// ============================================================================
// Section 5: Game memory — nearest pedestrian on fingerprinted layouts
// ============================================================================

bool IsInsideGameImage(DWORD address);

bool ReadPed(uintptr_t address, NpcInteraction::Ped& ped) {
    DWORD vtable = 0, matrix = 0;
    float position[3]{};
    BYTE vehicleFlags = 0;
    ped.address = address;
    if (address < 0x10000u ||
        !ReadMemorySafe(reinterpret_cast<const void*>(address), &vtable, sizeof(vtable)) || !IsInsideGameImage(vtable) ||
        !ReadMemorySafe(reinterpret_cast<const void*>(address + 0x14), &matrix, sizeof(matrix)) || matrix < 0x10000u ||
        !ReadMemorySafe(reinterpret_cast<const void*>(matrix + 0x30), position, sizeof(position)) ||
        !ReadMemorySafe(reinterpret_cast<const void*>(address + 0x540), &ped.health, sizeof(ped.health)) ||
        !ReadMemorySafe(reinterpret_cast<const void*>(address + 0x46D), &vehicleFlags, sizeof(vehicleFlags)) ||
        !ReadMemorySafe(reinterpret_cast<const void*>(address + 0x2F), &ped.area, sizeof(ped.area)) ||
        !ReadMemorySafe(reinterpret_cast<const void*>(address + 0x22), &ped.modelId, sizeof(ped.modelId)) ||
        !ReadMemorySafe(reinterpret_cast<const void*>(address + 0x598), &ped.pedType, sizeof(ped.pedType))) return false;
    if (ped.pedType < 0 || ped.pedType > 31) return false;
    ped.modelGender = g_modelGender[ped.modelId];
    ped.x = position[0]; ped.y = position[1]; ped.z = position[2];
    ped.inVehicle = (vehicleFlags & 1) != 0;
    return std::isfinite(ped.x) && std::isfinite(ped.y) && std::isfinite(ped.z) &&
        std::isfinite(ped.health) && ped.x > -4000 && ped.x < 4000 && ped.y > -4000 && ped.y < 4000 &&
        ped.z > -200 && ped.z < 1200;
}

void CaptureWorldContext() {
    DWORD address = 0;
    NpcInteraction::Ped player;
    NpcWorld::Snapshot world;
    uint64_t capturedAt = 0;
    if (g_gameLayoutSupported.load() &&
        ReadMemorySafe(reinterpret_cast<const void*>(g_playerPedAddress), &address, sizeof(address)) && ReadPed(address, player)) {
        world = g_worldReader.Read(player.x, player.y, player.z, player.area);
        if (world.available) capturedAt = UnixMilliseconds();
    }
    std::lock_guard<std::mutex> lock(g_data.mtx);
    g_data.world = std::move(world);
    g_data.worldPlayer = player;
    g_data.worldCapturedAt = capturedAt;
}

bool IsInsideGameImage(DWORD address) {
    static const uintptr_t begin = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    static const uintptr_t end = []() {
        HMODULE module = GetModuleHandleW(nullptr);
        IMAGE_DOS_HEADER dos{};
        if (!module || !ReadMemorySafe(module, &dos, sizeof(dos)) || dos.e_lfanew <= 0 || dos.e_lfanew > 0x10000) return uintptr_t{0};
        IMAGE_NT_HEADERS nt{};
        if (!ReadMemorySafe(reinterpret_cast<const BYTE*>(module) + dos.e_lfanew, &nt, sizeof(nt)) ||
            nt.Signature != IMAGE_NT_SIGNATURE) return uintptr_t{0};
        return static_cast<uintptr_t>(reinterpret_cast<uintptr_t>(module) + nt.OptionalHeader.SizeOfImage);
    }();
    return end > begin && address >= begin && address < end;
}

bool FindNearbyPed(NpcInteraction::Ped& nearest, const NpcInteraction::Ped& selected, bool& selectedInRange) {
    nearest = {};
    selectedInRange = false;
    g_nearbyFemaleDistance.store(-1.0f);
    g_nearbyMaleDistance.store(-1.0f);
    if (!g_gameLayoutSupported.load()) return false;
    DWORD playerAddress = 0, pool = 0, objects = 0, flags = 0;
    int poolSize = 0;
    NpcInteraction::Ped player;
    if (!ReadMemorySafe(reinterpret_cast<const void*>(g_playerPedAddress), &playerAddress, sizeof(playerAddress)) ||
        !ReadPed(playerAddress, player) ||
        !ReadMemorySafe(reinterpret_cast<const void*>(g_pedPoolAddress), &pool, sizeof(pool)) || pool < 0x10000u ||
        !ReadMemorySafe(reinterpret_cast<const void*>(pool), &objects, sizeof(objects)) ||
        !ReadMemorySafe(reinterpret_cast<const void*>(pool + 4), &flags, sizeof(flags)) ||
        !ReadMemorySafe(reinterpret_cast<const void*>(pool + 8), &poolSize, sizeof(poolSize)) ||
        objects < 0x10000u || flags < 0x10000u || poolSize < 1 || poolSize > 512) return false;
    float nearestDistance = INTERACT_RADIUS * INTERACT_RADIUS;
    for (int i = 0; i < poolSize; ++i) {
        BYTE slotFlags = 0;
        if (!ReadMemorySafe(reinterpret_cast<const void*>(flags + i), &slotFlags, sizeof(slotFlags))) return false;
        if ((slotFlags & 0x80u) != 0) continue;
        const uint64_t address = static_cast<uint64_t>(objects) + static_cast<uint64_t>(i) * PED_POOL_SLOT_SIZE;
        if (address > 0xFFFFFFFFu || address == playerAddress) continue;
        NpcInteraction::Ped candidate;
        if (!ReadPed(static_cast<uintptr_t>(address), candidate)) continue;
        candidate.handle = static_cast<uint32_t>(i) * 256 + slotFlags;
        if (NpcInteraction::SamePed(selected, candidate))
            selectedInRange = NpcInteraction::CanTalk(player, candidate, NpcInteraction::LeaveRadius);
        if (!NpcInteraction::CanTalk(player, candidate, INTERACT_RADIUS)) continue;
        const float distance = NpcInteraction::DistanceSquared(player, candidate);
        const char* gender = NpcInteraction::Gender(candidate);
        auto& nearestGender = std::strcmp(gender, "female") == 0 ? g_nearbyFemaleDistance : g_nearbyMaleDistance;
        if (std::strcmp(gender, "unknown") != 0) {
            const float metres = std::sqrt(distance);
            const float previous = nearestGender.load();
            if (previous < 0 || metres < previous) nearestGender.store(metres);
        }
        if (distance <= nearestDistance) {
            nearestDistance = distance;
            nearest = candidate;
        }
    }
    return true;
}

// ============================================================================
// Section 6: HTTP client (WinHTTP, synchronous on worker threads)
// ============================================================================

void OnGameFrame() {
    NpcPedHold::Request request{};
    request.playerPedPointerAddress = g_playerPedAddress;
    request.pedPoolPointerAddress = g_pedPoolAddress;
    request.standstillProcessAddress = g_standstillProcessAddress;
    request.generation = g_conversationGeneration.load();
    {
        std::lock_guard<std::mutex> lock(g_data.mtx);
        request.active = g_running.load() && g_gameLayoutSupported.load() &&
            NpcInteraction::IsConversation(static_cast<unsigned>(g_data.state));
        request.pedAddress = g_data.selectedPed.address;
        request.pedHandle = g_data.selectedPed.handle;
    }
    if (g_gameWnd && GetCurrentThreadId() != GetWindowThreadProcessId(g_gameWnd, nullptr)) {
        if (request.active) g_holdFailure.store(request.generation);
        return;
    }
    const auto result = g_pedHolder.Update(request);
    if (result == NpcPedHold::Result::Held) g_heldConversation.store(request.generation);
    else {
        g_heldConversation.store(0);
        if (request.active) g_holdFailure.store(request.generation);
    }
}

struct HttpResponse {
    int    status = 0;
    std::string body;
    bool   ok() const { return status >= 200 && status < 300; }
};

HttpResponse HttpRequest(const wchar_t* verb, const wchar_t* path,
                         const std::string& bodyUtf8 = {},
                         const wchar_t* contentType = nullptr,
                         DWORD timeoutMs = 30000) {
    HttpResponse resp;
    std::string bridgeToken;
    INTERNET_PORT backendPort = DEFAULT_BACKEND_PORT;
    {
        std::lock_guard<std::mutex> lock(g_bridgeConfigMutex);
        bridgeToken = g_bridgeToken;
        backendPort = g_backendPort;
    }
    HINTERNET session = WinHttpOpen(L"MalayalamNpcASI/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0);
    if (!session) return resp;
    WinHttpSetTimeouts(session, (int)timeoutMs, (int)timeoutMs, (int)timeoutMs, (int)timeoutMs);
    HINTERNET conn = WinHttpConnect(session, BACKEND_HOST, backendPort, 0);
    if (!conn) { WinHttpCloseHandle(session); return resp; }
    HINTERNET req = WinHttpOpenRequest(conn, verb, path, nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!req) { WinHttpCloseHandle(conn); WinHttpCloseHandle(session); return resp; }

    // Add bridge token header
    if (!bridgeToken.empty()) {
        std::wstring hdr = L"X-Bridge-Token: " + Utf8ToWide(bridgeToken);
        WinHttpAddRequestHeaders(req, hdr.c_str(), (DWORD)hdr.size(), WINHTTP_ADDREQ_FLAG_ADD);
    }
    // Content-Type
    if (contentType) {
        std::wstring ct = std::wstring(L"Content-Type: ") + contentType;
        WinHttpAddRequestHeaders(req, ct.c_str(), (DWORD)ct.size(), WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }

    BOOL sent = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        bodyUtf8.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)bodyUtf8.data(),
        (DWORD)bodyUtf8.size(), (DWORD)bodyUtf8.size(), 0);
    if (!sent || !WinHttpReceiveResponse(req, nullptr)) {
        WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(session);
        return resp;
    }

    DWORD statusCode = 0, sz = sizeof(statusCode);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        nullptr, &statusCode, &sz, nullptr);
    resp.status = (int)statusCode;

    // Read body
    constexpr size_t MAX_RESPONSE_BYTES = 8 * 1024 * 1024;
    DWORD bytesAvailable = 0;
    while (WinHttpQueryDataAvailable(req, &bytesAvailable) && bytesAvailable > 0) {
        std::vector<char> buf(bytesAvailable);
        DWORD bytesRead = 0;
        if (WinHttpReadData(req, buf.data(), bytesAvailable, &bytesRead)) {
            if (resp.body.size() + bytesRead > MAX_RESPONSE_BYTES) {
                resp.body.clear();
                resp.status = 0;
                break;
            }
            resp.body.append(buf.data(), bytesRead);
        } else break;
    }

    WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(session);
    return resp;
}

// ============================================================================
// Section 7: Bridge token
// ============================================================================

bool LoadBridgeToken() {
    DWORD req = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (req == 0) return false;
    std::wstring base(req, L'\0');
    DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", base.data(), req);
    if (len == 0 || len >= req) return false;
    base.resize(len);
    std::wstring tokenPath = base + L"\\MalayalamNpcDemo\\bridge-token";

    HANDLE f = CreateFileW(tokenPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    char buf[256]{};
    DWORD bytesRead = 0;
    ReadFile(f, buf, sizeof(buf) - 1, &bytesRead, nullptr);
    CloseHandle(f);
    const std::string contents(buf, bytesRead);
    const size_t tokenStart = contents.find("token=");
    const size_t tokenEnd = contents.find_first_of("\r\n", tokenStart);
    const size_t portStart = contents.find("port=");
    if (tokenStart == std::string::npos || tokenEnd == std::string::npos || portStart == std::string::npos) return false;

    const std::string token = contents.substr(tokenStart + 6, tokenEnd - (tokenStart + 6));
    const size_t portEnd = contents.find_first_of("\r\n", portStart);
    const std::string portText = contents.substr(portStart + 5,
        (portEnd == std::string::npos ? contents.size() : portEnd) - (portStart + 5));
    char* end = nullptr;
    const long port = std::strtol(portText.c_str(), &end, 10);
    const bool validHex = token.size() == 64 && std::all_of(token.begin(), token.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
    if (!validHex || end == portText.c_str() || *end != '\0' || port < 1024 || port > 65535) return false;

    {
        std::lock_guard<std::mutex> lock(g_bridgeConfigMutex);
        g_bridgeToken = token;
        g_backendPort = static_cast<INTERNET_PORT>(port);
    }
    return true;
}

// ============================================================================
// Section 8: Audio playback (MCI)
// ============================================================================

struct AudioPlayRequest {
    const std::wstring* path;
    unsigned long generation;
    DWORD duration = 0;
    bool played = false;
};
struct AudioWriteRequest {
    const std::string* bytes;
    unsigned long generation;
    bool written = false;
};

void StopAudio() {
    if (GetCurrentThreadId() == g_threadId)
        mciSendStringW(L"close npc_reply", nullptr, 0, nullptr);
    else if (g_overlay && IsWindow(g_overlay))
        SendMessageW(g_overlay, AUDIO_STOP_MESSAGE, 0, 0);
}

bool AudioIsPlaying() {
    wchar_t mode[32]{};
    return mciSendStringW(L"status npc_reply mode", mode, 32, nullptr) == 0 && wcscmp(mode, L"playing") == 0;
}

bool PlayAudioOnOverlay(const std::wstring& filePath, unsigned long generation, DWORD& duration) {
    if (!g_running.load() || generation != g_conversationGeneration.load()) return false;
    mciSendStringW(L"close npc_reply", nullptr, 0, nullptr);
    std::wstring cmd = L"open \"" + filePath + L"\" type mpegvideo alias npc_reply";
    if (mciSendStringW(cmd.c_str(), nullptr, 0, nullptr) != 0) return false;
    mciSendStringW(L"set npc_reply time format milliseconds", nullptr, 0, nullptr);
    wchar_t length[32]{};
    if (mciSendStringW(L"status npc_reply length", length, 32, nullptr) == 0)
        duration = std::wcstoul(length, nullptr, 10);
    if (mciSendStringW(L"play npc_reply", nullptr, 0, nullptr) == 0) return true;
    mciSendStringW(L"close npc_reply", nullptr, 0, nullptr);
    return false;
}

bool PlayAudioFile(const std::wstring& filePath, unsigned long generation, DWORD& duration) {
    if (!g_running.load() || !g_overlay || !IsWindow(g_overlay)) return false;
    AudioPlayRequest request{&filePath, generation};
    SendMessageW(g_overlay, AUDIO_PLAY_MESSAGE, 0, reinterpret_cast<LPARAM>(&request));
    duration = request.duration;
    return request.played;
}

bool DownloadAudio(const std::string& audioUrlPath, unsigned long generation) {
    if (audioUrlPath.rfind("/api/audio/", 0) != 0) return false;
    std::wstring widePath = Utf8ToWide(audioUrlPath);
    HttpResponse resp = HttpRequest(L"GET", widePath.c_str());
    if (!resp.ok() || resp.body.empty()) return false;

    if (!g_running.load() || !g_overlay || !IsWindow(g_overlay)) return false;
    AudioWriteRequest request{&resp.body, generation};
    SendMessageW(g_overlay, AUDIO_WRITE_MESSAGE, 0, reinterpret_cast<LPARAM>(&request));
    return request.written;
}

// ============================================================================
// Section 9: Backend interaction (runs on detached threads)
// ============================================================================

bool BackendSelect(unsigned int pedHandle, unsigned long generation) {
    NpcInteraction::Ped pedestrian, player;
    NpcWorld::Snapshot world;
    uint64_t worldCapturedAt = 0;
    {
        std::lock_guard<std::mutex> lock(g_data.mtx);
        if (generation != g_conversationGeneration.load() || !g_data.selectedPed.address) return false;
        pedestrian = g_data.selectedPed;
        player = g_data.worldPlayer;
        world = g_data.world;
        worldCapturedAt = g_data.worldCapturedAt;
    }
    LoadBridgeToken();
    const auto worldJson = NpcWorldWire::Serialize(world, player, worldCapturedAt, UnixMilliseconds(), EscapeJsonString);
    const std::string body = "{\"gameSessionId\":\"gta-" + std::to_string(GetCurrentProcessId()) +
        "\",\"sceneGeneration\":" + std::to_string(pedHandle) +
        ",\"pedestrian\":{\"modelId\":" + std::to_string(pedestrian.modelId) +
        ",\"pedType\":" + std::to_string(pedestrian.pedType) +
        ",\"gender\":\"" + NpcInteraction::Gender(pedestrian) +
        "\",\"modelGender\":\"" + NpcInteraction::ModelGender(pedestrian) + "\"},\"worldContext\":" + worldJson + "}";
    const auto response = HttpRequest(L"POST", L"/api/select", body, L"application/json");
    if (!response.ok()) return false;
    const auto name = Utf8ToWide(ExtractJsonString(response.body, "displayName"));
    if (name.empty()) return false;
    if (generation == g_conversationGeneration.load() && g_loggedPersonaGeneration.exchange(generation) != generation) {
        wchar_t diagnostic[384]{};
        const auto modelGender = Utf8ToWide(NpcInteraction::ModelGender(pedestrian));
        const auto effectiveGender = Utf8ToWide(NpcInteraction::Gender(pedestrian));
        const auto backendGender = Utf8ToWide(ExtractJsonString(response.body, "gender"));
        swprintf_s(diagnostic, L"NPC selection metadata: model=%u, liveType=%d, modelGender=%s, effectiveGender=%s, backendGender=%s, nearFemale=%.2f, nearMale=%.2f.",
            static_cast<unsigned>(pedestrian.modelId), pedestrian.pedType, modelGender.c_str(), effectiveGender.c_str(), backendGender.c_str(),
            g_nearbyFemaleDistance.load(), g_nearbyMaleDistance.load());
        WriteLog(diagnostic);
    }
    std::lock_guard<std::mutex> lock(g_data.mtx);
    if (generation == g_conversationGeneration.load()) g_data.npcName = name;
    return true;
}

void BackendCancel() {
    HttpRequest(L"POST", L"/api/deselect", "{}", L"application/json", 2000);
}

void ExitConversation() {
    g_microphone.Cancel();
    g_recordGeneration = 0;
    g_recordError.clear();
    g_recordStartPosted = false;
    g_conversationGeneration.fetch_add(1);
    {
        std::lock_guard<std::mutex> lock(g_data.mtx);
        g_data.state = UiState::HIDDEN;
        g_data.inputText.clear();
        g_data.recognizedText.clear();
        g_data.subtitle.clear();
        g_data.selectedPed = {};
        g_data.audioPlaying = false;
        g_data.subtitleFrame = 0;
    }
    StopAudio();
    g_cancelPending.store(true);
    std::thread([]() {
        BackendCancel();
        g_cancelPending.store(false);
    }).detach();
}

bool HasActiveConversation() {
    std::lock_guard<std::mutex> lock(g_data.mtx);
    return NpcInteraction::IsConversation(static_cast<unsigned>(g_data.state));
}

bool GameIsForeground();

void BeginConversation(unsigned long generation, unsigned int pedHandle) {
    std::lock_guard<std::mutex> turnLock(g_turnMutex);
    const DWORD deadline = GetTickCount() + 2000;
    while (g_running.load() && generation == g_conversationGeneration.load() &&
        g_heldConversation.load() != generation && g_holdFailure.load() != generation &&
        !NpcInteraction::DeadlineReached(GetTickCount(), deadline)) Sleep(10);
    if (!g_running.load() || generation != g_conversationGeneration.load()) return;
    const bool selected = g_heldConversation.load() == generation && BackendSelect(pedHandle, generation);
    if (generation != g_conversationGeneration.load()) {
        BackendCancel();
        return;
    }
    std::lock_guard<std::mutex> lock(g_data.mtx);
    if (generation != g_conversationGeneration.load()) return;
    if (selected) {
        g_data.state = UiState::READY;
        g_data.statusText.clear();
    } else {
        g_data.state = UiState::ERROR_SHOW;
        g_data.errorText = L"Cannot prepare this NPC. Restart the local server or press Esc and retry.";
        g_data.subtitleEnd = GetTickCount() + 5000;
    }
}

void BackendTurn(const std::wstring& text, unsigned long generation, unsigned int pedHandle,
    const std::vector<BYTE>& audio = {}) {
    std::lock_guard<std::mutex> turnLock(g_turnMutex);
    if (!g_running.load() || generation != g_conversationGeneration.load()) return;
    if (!BackendSelect(pedHandle, generation)) {
        std::lock_guard lk(g_data.mtx);
        if (generation == g_conversationGeneration.load()) {
            g_data.state = UiState::ERROR_SHOW;
            g_data.errorText = L"Cannot connect to the local conversation server. Start the backend and retry.";
            g_data.subtitleEnd = GetTickCount() + 5000;
        }
        return;
    }

    if (!g_running.load() || generation != g_conversationGeneration.load()) {
        BackendCancel();
        return;
    }
    HttpResponse resp;
    if (audio.empty()) {
        const std::string escaped = EscapeJsonString(WideToUtf8(text));
        const std::string body = R"({"text":")" + escaped + R"("})";
        resp = HttpRequest(L"POST", L"/api/text-turn", body, L"application/json");
    } else {
        const std::string wav(reinterpret_cast<const char*>(audio.data()), audio.size());
        resp = HttpRequest(L"POST", L"/api/turn", wav, L"audio/wav");
    }

    if (!g_running.load() || generation != g_conversationGeneration.load()) return;

    if (!resp.ok()) {
        std::string errMsg = ExtractJsonString(resp.body, "error");
        std::lock_guard lk(g_data.mtx);
        if (generation != g_conversationGeneration.load()) return;
        g_data.state = UiState::ERROR_SHOW;
        g_data.errorText = errMsg.empty() ? L"Backend returned an error." : Utf8ToWide(errMsg);
        g_data.subtitleEnd = GetTickCount() + 4000;
        return;
    }

    std::string replyMl = ExtractJsonString(resp.body, "replyMl");
    std::string audioUrl = ExtractJsonString(resp.body, "audioUrl");
    std::string turnId = ExtractJsonString(resp.body, "turnId");
    const std::string speechError = ExtractJsonString(resp.body, "speechError");

    if (replyMl.empty()) {
        std::lock_guard lk(g_data.mtx);
        if (generation != g_conversationGeneration.load()) return;
        g_data.state = UiState::ERROR_SHOW;
        g_data.errorText = L"The NPC had nothing to say. Try again.";
        g_data.subtitleEnd = GetTickCount() + 3000;
        return;
    }

    const bool audioDownloaded = !audioUrl.empty() && DownloadAudio(audioUrl, generation);
    if (!g_running.load() || generation != g_conversationGeneration.load()) return;
    const std::wstring wideReply = Utf8ToWide(replyMl);
    const DWORD readingDuration = static_cast<DWORD>(std::min<size_t>(15000,
        SUBTITLE_MIN_MS + wideReply.size() * SUBTITLE_PER_CHAR_MS));
    {
        std::lock_guard<std::mutex> lock(g_data.mtx);
        if (generation != g_conversationGeneration.load()) return;
        g_data.subtitle = wideReply;
        g_data.recognizedText = audio.empty() ? L"" : Utf8ToWide(ExtractJsonString(resp.body, "transcript"));
        g_data.subtitleEnd = GetTickCount() + readingDuration;
        g_data.subtitleGeneration = generation;
        g_data.subtitleFrame = 0;
        g_data.audioPlaying = false;
        g_data.statusText.clear();
        g_data.state = UiState::SUBTITLE;
    }
    bool subtitleDisplayed = false;
    const DWORD renderDeadline = GetTickCount() + 2000;
    while (g_running.load() && generation == g_conversationGeneration.load() &&
        !NpcInteraction::DeadlineReached(GetTickCount(), renderDeadline)) {
        uint64_t frame = 0;
        {
            std::lock_guard<std::mutex> lock(g_data.mtx);
            frame = g_data.subtitleFrame;
        }
        if (frame != 0 && NpcD3D::PresentedFrame() >= frame && GameIsForeground()) {
            subtitleDisplayed = true;
            break;
        }
        Sleep(20);
    }
    if (!g_running.load() || generation != g_conversationGeneration.load()) return;
    DWORD audioDuration = 0;
    const bool audioPlayed = subtitleDisplayed && audioDownloaded &&
        PlayAudioFile(g_tempDir + L"\\npc_reply.mp3", generation, audioDuration);
    {
        std::lock_guard<std::mutex> lock(g_data.mtx);
        if (generation != g_conversationGeneration.load()) return;
        g_data.audioPlaying = audioPlayed;
        g_data.statusText = audioPlayed ? L"" : (speechError.empty()
            ? L"Voice unavailable; displaying the reply as subtitles."
            : Utf8ToWide(speechError));
        g_data.subtitleEnd = GetTickCount() + (audioPlayed ? std::max<DWORD>(readingDuration, audioDuration + 500) : readingDuration);
        if (!subtitleDisplayed) {
            g_data.state = UiState::ERROR_SHOW;
            g_data.errorText = L"Reply not displayed. Keep GTA focused and retry.";
            g_data.subtitle.clear();
            g_data.subtitleEnd = GetTickCount() + 4000;
        }
    }
    if (!turnId.empty() && (subtitleDisplayed || audioPlayed)) {
        const std::string ackBody = R"({"turnId":")" + EscapeJsonString(turnId) +
            R"(","subtitleDisplayed":)" + (subtitleDisplayed ? "true" : "false") +
            R"(,"audioPlayed":)" + (audioPlayed ? "true" : "false") + "}";
        HttpRequest(L"POST", L"/api/delivery", ackBody, L"application/json");
    } else if (!turnId.empty()) {
        const std::string cancelBody = R"({"turnId":")" + EscapeJsonString(turnId) + "\"}";
        HttpRequest(L"POST", L"/api/cancel", cancelBody, L"application/json", 2000);
    }
}

void ShowRecordingError(const std::wstring& error, unsigned long generation) {
    g_microphone.Cancel();
    g_recordGeneration = 0;
    g_recordStartPosted = false;
    std::lock_guard<std::mutex> lock(g_data.mtx);
    if (generation != g_conversationGeneration.load()) return;
    g_data.state = UiState::ERROR_SHOW;
    g_data.errorText = error.empty() ? L"Microphone capture failed. Check Windows microphone access and the default input device." : error;
    g_data.subtitleEnd = GetTickCount() + 5000;
}

void StartRecording(unsigned long generation) {
    {
        std::lock_guard<std::mutex> lock(g_data.mtx);
        if (generation != g_conversationGeneration.load() || g_data.state != UiState::RECORDING) return;
    }
    std::wstring error;
    if (!g_microphone.Start(error)) {
        g_recordError = error;
        std::lock_guard<std::mutex> lock(g_data.mtx);
        if (generation == g_conversationGeneration.load() && g_data.state == UiState::RECORDING)
            g_data.statusText = L"Microphone unavailable. Release R for details, or tap R to type it.";
        return;
    }
    bool cancelled = false;
    {
        std::lock_guard<std::mutex> lock(g_data.mtx);
        cancelled = generation != g_conversationGeneration.load() || g_data.state != UiState::RECORDING;
        if (!cancelled) g_data.statusText = L"Recording... release R to send; Esc cancels.";
    }
    if (cancelled) g_microphone.Cancel();
}

void FinishRecording(DWORD heldMilliseconds, unsigned long generation) {
    {
        std::lock_guard<std::mutex> lock(g_data.mtx);
        if (generation != g_conversationGeneration.load() || g_data.state != UiState::RECORDING) return;
    }
    if (NpcInteraction::IsRecordTap(heldMilliseconds)) {
        g_microphone.Cancel();
        g_recordGeneration = 0;
        g_recordStartPosted = false;
        g_recordError.clear();
        std::lock_guard<std::mutex> lock(g_data.mtx);
        if (generation == g_conversationGeneration.load()) {
            g_data.inputText += g_recordTapCharacter;
            g_data.state = UiState::TYPING;
        }
        return;
    }
    if (!g_recordError.empty()) {
        ShowRecordingError(g_recordError, generation);
        return;
    }
    std::wstring error;
    auto wav = g_microphone.Stop(error);
    g_recordGeneration = 0;
    g_recordStartPosted = false;
    if (wav.empty()) {
        ShowRecordingError(error, generation);
        return;
    }
    const unsigned int handle = g_selectedPedHandle.load();
    {
        std::lock_guard<std::mutex> lock(g_data.mtx);
        if (generation != g_conversationGeneration.load()) return;
        g_data.state = UiState::PROCESSING;
        g_data.statusText = L"Recognising your speech and preparing the reply...";
    }
    std::thread([audio = std::move(wav), generation, handle]() { BackendTurn({}, generation, handle, audio); }).detach();
}

struct GameWindowSearch {
    HWND window = nullptr;
    long long area = 0;
};

BOOL CALLBACK FindGameWindowCallback(HWND hwnd, LPARAM parameter) {
    if (hwnd == g_overlay) return TRUE;
    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    if (processId != GetCurrentProcessId() || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;

    RECT rect{};
    if (!GetWindowRect(hwnd, &rect)) return TRUE;
    const long long area = static_cast<long long>(rect.right - rect.left) * (rect.bottom - rect.top);
    auto* search = reinterpret_cast<GameWindowSearch*>(parameter);
    if (area > search->area) {
        search->window = hwnd;
        search->area = area;
    }
    return TRUE;
}

HWND FindGameWindow() {
    GameWindowSearch search{};
    EnumWindows(FindGameWindowCallback, reinterpret_cast<LPARAM>(&search));
    return search.window;
}

bool GameIsForeground() {
    HWND foreground = GetForegroundWindow();
    if (!foreground) return false;
    DWORD processId = 0;
    GetWindowThreadProcessId(foreground, &processId);
    return processId == GetCurrentProcessId();
}

DWORD WINAPI BridgeHeartbeatProc(LPVOID) {
    while (g_running.load()) {
        const DWORD waitResult = g_stopEvent
            ? WaitForSingleObject(g_stopEvent, 5000)
            : (Sleep(5000), WAIT_TIMEOUT);
        if (waitResult != WAIT_TIMEOUT || !g_running.load()) break;

        // Refresh a rotated token if the backend restarted while the game stayed open.
        LoadBridgeToken();
        const HttpResponse response = HttpRequest(L"GET", L"/api/status", {}, nullptr, 1000);
        std::lock_guard<std::mutex> lock(g_data.mtx);
        g_data.backendOk = response.ok();
    }
    return 0;
}

// ============================================================================
// Section 10: Keyboard hook for text input mode
// ============================================================================

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode != HC_ACTION) return CallNextHookEx(g_kbHook, nCode, wParam, lParam);
    KBDLLHOOKSTRUCT* kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
    const DWORD vk = kb->vkCode;
    if (vk >= 256) return CallNextHookEx(g_kbHook, nCode, wParam, lParam);
    if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
        if (vk == 'R' && g_recordGeneration && GameIsForeground()) {
            const DWORD heldFor = kb->time - g_recordDownTime;
            if (NpcInteraction::IsRecordTap(heldFor) && !g_recordStartPosted) FinishRecording(heldFor, g_recordGeneration);
            else PostMessageW(g_overlay, MIC_STOP_MESSAGE, heldFor, g_recordGeneration);
        }
        const unsigned int held = g_heldKeys[vk].exchange(0);
        if (held == 2 && GameIsForeground()) return 1;
        return CallNextHookEx(g_kbHook, nCode, wParam, lParam);
    }
    if ((wParam != WM_KEYDOWN && wParam != WM_SYSKEYDOWN) || !GameIsForeground())
        return CallNextHookEx(g_kbHook, nCode, wParam, lParam);
    const bool repeated = g_heldKeys[vk].exchange(1) != 0;

    UiState state;
    {
        std::lock_guard<std::mutex> lock(g_data.mtx);
        state = g_data.state;
    }
    const bool printable = (vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9') ||
        vk == VK_SPACE || vk == VK_BACK || vk == VK_RETURN || (vk >= VK_OEM_1 && vk <= VK_OEM_102);
    if (state == UiState::RECORDING && !g_recordStartPosted && g_recordGeneration && vk != 'R' && printable) {
        g_microphone.Cancel();
        g_recordGeneration = 0;
        g_recordError.clear();
        std::lock_guard<std::mutex> lock(g_data.mtx);
        g_data.inputText += g_recordTapCharacter;
        g_data.state = state = UiState::TYPING;
    }
    const bool isTyping = state == UiState::TYPING || state == UiState::READY;
    const bool conversationActive = NpcInteraction::IsConversation(static_cast<unsigned>(state));

    // Always let through: Alt+Tab, Windows key, Alt+F4.
    if (vk == VK_LWIN || vk == VK_RWIN) return CallNextHookEx(g_kbHook, nCode, wParam, lParam);
    if (vk == VK_TAB && (GetAsyncKeyState(VK_MENU) & 0x8000)) return CallNextHookEx(g_kbHook, nCode, wParam, lParam);
    if (vk == VK_F4 && (GetAsyncKeyState(VK_MENU) & 0x8000)) return CallNextHookEx(g_kbHook, nCode, wParam, lParam);

    // Escape cancels even while a backend response is still processing.
    if (conversationActive || (state == UiState::PROMPT && (vk == VK_F6 || vk == VK_RETURN)))
        g_heldKeys[vk].store(2);
    if ((vk == VK_F6 || vk == VK_RETURN || vk == VK_ESCAPE) && repeated &&
        (conversationActive || state == UiState::PROMPT)) return 1;
    if (vk == VK_ESCAPE && conversationActive) {
        ExitConversation();
        return 1;
    }

    // Handle the interaction key in the hook so GTA does not also consume
    // Enter as a game/menu action. Polling GetAsyncKeyState on a timer can miss
    // short presses because other game code may clear its transition bit.
    if (state == UiState::PROMPT && (vk == VK_F6 || vk == VK_RETURN)) {
        unsigned long generation = 0;
        unsigned int handle = 0;
        {
            std::lock_guard<std::mutex> lock(g_data.mtx);
            if (g_data.state == UiState::PROMPT && g_data.nearbyPed.address && !g_cancelPending.load() && NpcD3D::IsRendering()) {
                g_data.selectedPed = g_data.nearbyPed;
                handle = NpcInteraction::ScopeGeneration(g_sceneEpoch.load(), g_data.selectedPed.handle);
                g_selectedPedHandle.store(handle);
                g_data.npcName = L"pedestrian";
                g_data.inputText.clear();
                g_data.subtitle.clear();
                g_data.state = UiState::GREETING;
                generation = g_conversationGeneration.fetch_add(1) + 1;
            }
        }
        if (generation) std::thread([generation, handle]() { BeginConversation(generation, handle); }).detach();
        return 1;
    }

    if (!isTyping) return conversationActive ? 1 : CallNextHookEx(g_kbHook, nCode, wParam, lParam);
    if (vk == 'R' && !repeated && !(GetAsyncKeyState(VK_CONTROL) & 0x8000) && !(GetAsyncKeyState(VK_MENU) & 0x8000)) {
        bool record = false;
        {
            std::lock_guard<std::mutex> lock(g_data.mtx);
            if (g_data.inputText.empty()) {
                g_recordGeneration = g_conversationGeneration.load();
                g_recordDownTime = kb->time;
                const bool shifted = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                g_recordTapCharacter = shifted != ((GetKeyState(VK_CAPITAL) & 1) != 0) ? L'R' : L'r';
                g_recordError.clear();
                g_recordStartPosted = false;
                g_data.recognizedText.clear();
                g_data.state = UiState::RECORDING;
                g_data.statusText = L"Hold R until Recording appears, then speak; release R to send.";
                record = true;
            }
        }
        if (record) return 1;
    }

    // Enter sends the current text; an empty input is ignored.
    if (vk == VK_RETURN) {
        std::wstring textToSend;
        {
            std::lock_guard<std::mutex> lock(g_data.mtx);
            textToSend = g_data.inputText;
            g_data.inputText.clear();
            if (!textToSend.empty()) {
                g_data.state = UiState::PROCESSING;
                g_data.statusText = g_data.npcName + L" is thinking...";
            }
        }
        if (!textToSend.empty()) {
            const unsigned long generation = g_conversationGeneration.load();
            const unsigned int handle = g_selectedPedHandle.load();
            std::thread([text = std::move(textToSend), generation, handle]() { BackendTurn(text, generation, handle); }).detach();
        }
        return 1;
    }

    if (vk == VK_BACK) {
        std::lock_guard<std::mutex> lock(g_data.mtx);
        if (!g_data.inputText.empty()) g_data.inputText.pop_back();
        if (g_data.state == UiState::READY) g_data.state = UiState::TYPING;
        return 1;
    }

    BYTE kbState[256]{};
    GetKeyboardState(kbState);
    for (const int modifier : {VK_SHIFT, VK_CONTROL, VK_MENU})
        kbState[modifier] = (GetAsyncKeyState(modifier) & 0x8000) ? 0x80 : 0;
    kbState[VK_CAPITAL] = static_cast<BYTE>(GetKeyState(VK_CAPITAL) & 1);
    DWORD foregroundThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    wchar_t ch[8]{};
    const int result = ToUnicodeEx(vk, kb->scanCode, kbState, ch, 8, 0, GetKeyboardLayout(foregroundThread));
    if (result > 0 && ch[0] >= 32) {
        std::lock_guard<std::mutex> lock(g_data.mtx);
        const size_t available = MAX_INPUT_LEN - g_data.inputText.size();
        g_data.inputText.append(ch, std::min<size_t>(available, result));
        if (g_data.state == UiState::READY) g_data.state = UiState::TYPING;
    }
    return 1;
}

// ============================================================================
// Section 11: Overlay rendering (GDI+)
// ============================================================================

struct RenderBox {
    Gdiplus::RectF rect;
    Gdiplus::Color bgColor;
    Gdiplus::Color borderColor;
    std::wstring   text;
    Gdiplus::Color textColor;
    float          fontSize;
    int            alignment; // 0=left, 1=center
};

void DrawRoundedRect(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float radius,
                     const Gdiplus::Color& bg, const Gdiplus::Color& border) {
    Gdiplus::GraphicsPath path;
    float d = radius * 2;
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    path.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    path.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    path.CloseFigure();

    Gdiplus::SolidBrush bgBrush(bg);
    g.FillPath(&bgBrush, &path);
    Gdiplus::Pen pen(border, 1.5f);
    g.DrawPath(&pen, &path);
}

void RenderOverlay(int w, int h) {
    if (w <= 0 || h <= 0) return;
    bool renderingSubtitle = false;
    unsigned long renderGeneration = 0;
    Gdiplus::Bitmap bitmap(w, h, PixelFormat32bppARGB);
    {
    Gdiplus::Graphics gfx(&bitmap);
    gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    gfx.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    gfx.Clear(Gdiplus::Color(0, 0, 0, 0));

    // Collect what to draw based on state
    std::vector<RenderBox> boxes;
    float panelW = (float)w * 0.7f;
    if (panelW > 800.f) panelW = 800.f;
    if (panelW < 300.f) panelW = 300.f;
    float panelX = ((float)w - panelW) / 2.f;
    float curY = (float)h;
    float pad = 16.f;

    UiState currentState;
    std::wstring subtitle, inputText, npcName, statusText, errorText, recognizedText, targetGender, worldLabel;
    int cursorBlink;
    {
        std::lock_guard lk(g_data.mtx);
        currentState = g_data.state;
        subtitle     = g_data.subtitle;
        inputText    = g_data.inputText;
        npcName      = g_data.npcName;
        statusText   = g_data.statusText;
        errorText    = g_data.errorText;
        recognizedText = g_data.recognizedText;
        targetGender = Utf8ToWide(NpcInteraction::Gender(g_data.nearbyPed));
        if (g_data.world.available && g_data.worldCapturedAt && UnixMilliseconds() - g_data.worldCapturedAt <= 10000) {
            worldLabel = Utf8ToWide(g_data.world.location);
            if (g_data.world.hour >= 0 && g_data.world.minute >= 0) {
                wchar_t clock[16]{};
                swprintf_s(clock, L"%02d:%02d", g_data.world.hour, g_data.world.minute);
                if (!worldLabel.empty()) worldLabel += L"  |  ";
                worldLabel += clock;
            }
            if (!g_data.world.weather.empty()) {
                if (!worldLabel.empty()) worldLabel += L"  |  ";
                worldLabel += Utf8ToWide(g_data.world.weather);
            }
        }
        cursorBlink  = g_data.cursorBlink;
        renderGeneration = g_data.subtitleGeneration;
        renderingSubtitle = currentState == UiState::SUBTITLE && !subtitle.empty();
    }
    if (!GameIsForeground()) { currentState = UiState::HIDDEN; renderingSubtitle = false; }
    if (currentState == UiState::HIDDEN) goto finish;

    // -- Input bar (typing/ready states) --
    if (currentState == UiState::TYPING || currentState == UiState::READY) {
        std::wstring inputDisplay = L"> " + inputText;
        if (cursorBlink % 2 == 0) inputDisplay += L"_";
        float inputH = 44.f;
        curY -= inputH + 8.f;
        boxes.push_back({
            Gdiplus::RectF(panelX, curY, panelW, inputH),
            Gdiplus::Color(210, 15, 15, 40),      // bg
            Gdiplus::Color(180, 80, 180, 255),     // border
            inputDisplay,
            Gdiplus::Color(255, 180, 230, 255),    // text: light blue
            15.f, 0
        });

        // Hint text
        float hintH = 28.f;
        curY -= hintH + 2.f;
        boxes.push_back({
            Gdiplus::RectF(panelX, curY, panelW, hintH),
            Gdiplus::Color(0, 0, 0, 0),
            Gdiplus::Color(0, 0, 0, 0),
            L"Hold R to speak or type + Enter  \u2022  Esc to leave",
            Gdiplus::Color(200, 160, 160, 190),
            11.f, 1
        });
    }

    // -- Status / processing bar --
    if (currentState == UiState::PROCESSING || currentState == UiState::GREETING || currentState == UiState::RECORDING) {
        std::wstring display = (currentState == UiState::GREETING) ? L"Preparing this NPC's conversation..." : statusText;
        float barH = 40.f;
        curY -= barH + 8.f;
        boxes.push_back({
            Gdiplus::RectF(panelX, curY, panelW, barH),
            Gdiplus::Color(200, 20, 20, 50),
            Gdiplus::Color(150, 255, 200, 80),
            display,
            Gdiplus::Color(255, 255, 220, 100),
            14.f, 1
        });
    }

    if (currentState == UiState::SUBTITLE && !statusText.empty()) {
        curY -= 68.f;
        boxes.push_back({Gdiplus::RectF(panelX, curY, panelW, 64.f),
            Gdiplus::Color(200, 20, 20, 50), Gdiplus::Color(150, 255, 200, 80),
            statusText, Gdiplus::Color(255, 255, 220, 100), 12.f, 1});
    }
    // -- Error bar --
    if (currentState == UiState::ERROR_SHOW) {
        float barH = 48.f;
        curY -= barH + 8.f;
        boxes.push_back({
            Gdiplus::RectF(panelX, curY, panelW, barH),
            Gdiplus::Color(200, 60, 10, 10),
            Gdiplus::Color(200, 255, 80, 80),
            errorText,
            Gdiplus::Color(255, 255, 180, 180),
            13.f, 1
        });
    }

    if (!recognizedText.empty() && (currentState == UiState::SUBTITLE ||
        currentState == UiState::READY || currentState == UiState::TYPING)) {
        curY -= 68.f;
        const std::wstring heard = L"Heard: " + recognizedText.substr(0, 180) +
            (recognizedText.size() > 180 ? L"..." : L"");
        boxes.push_back({Gdiplus::RectF(panelX, curY, panelW, 64.f),
            Gdiplus::Color(210, 12, 28, 35), Gdiplus::Color(160, 100, 210, 160),
            heard, Gdiplus::Color(255, 190, 240, 215), 13.f, 0});
    }
    // -- Subtitle panel --
    if (!subtitle.empty() && (currentState == UiState::SUBTITLE ||
        currentState == UiState::READY || currentState == UiState::TYPING)) {
        // Measure text height
        Gdiplus::Font font(L"Segoe UI", 16.f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::RectF measureRect(0, 0, panelW - pad * 2.f, 400.f);
        Gdiplus::RectF bounds;
        gfx.MeasureString(subtitle.c_str(), (int)subtitle.size(), &font, measureRect, &bounds);
        float textH = bounds.Height;
        float nameH = 24.f;
        float boxH = nameH + textH + pad * 2.5f;
        curY -= boxH + 8.f;

        // NPC name
        boxes.push_back({
            Gdiplus::RectF(panelX, curY, panelW, boxH),
            Gdiplus::Color(220, 10, 10, 35),
            Gdiplus::Color(180, 60, 180, 255),
            L"",                                    // drawn manually below
            Gdiplus::Color(0, 0, 0, 0),
            0.f, 0
        });
        // We'll draw name + subtitle manually for this box
    }

    // -- Prompt bar --
    if (currentState == UiState::PROMPT) {
        float barH = 48.f;
        curY -= barH + 12.f;
        const std::wstring target = targetGender == L"unknown" ? L"pedestrian" : targetGender + L" pedestrian";
        const std::wstring prompt = L"\u25B6  Interact with " + target + L"  [F6 / Enter]";
        boxes.push_back({
            Gdiplus::RectF(panelX, curY, panelW, barH),
            Gdiplus::Color(210, 10, 10, 35),
            Gdiplus::Color(200, 255, 200, 60),
            prompt,
            Gdiplus::Color(255, 255, 220, 90),
            16.f, 1
        });
    }

    if (!worldLabel.empty()) {
        curY -= 30.f;
        boxes.push_back({Gdiplus::RectF(panelX, curY, panelW, 26.f),
            Gdiplus::Color(170, 12, 24, 20), Gdiplus::Color(0, 0, 0, 0),
            worldLabel, Gdiplus::Color(230, 165, 215, 185), 12.f, 1});
    }
    // -- Render all boxes --
    for (auto& box : boxes) {
        if (box.bgColor.GetA() > 0 || box.borderColor.GetA() > 0)
            DrawRoundedRect(gfx, box.rect, 10.f, box.bgColor, box.borderColor);
        if (!box.text.empty()) {
            Gdiplus::Font font(L"Segoe UI", box.fontSize, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            Gdiplus::StringFormat fmt;
            fmt.SetAlignment(box.alignment == 1 ? Gdiplus::StringAlignmentCenter : Gdiplus::StringAlignmentNear);
            fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            fmt.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
            Gdiplus::RectF textRect(box.rect.X + pad, box.rect.Y, box.rect.Width - pad * 2, box.rect.Height);
            Gdiplus::SolidBrush brush(box.textColor);
            gfx.DrawString(box.text.c_str(), -1, &font, textRect, &fmt, &brush);
        }
    }

    // -- Draw subtitle panel content (name + text, special layout) --
    if (!subtitle.empty() && (currentState == UiState::SUBTITLE ||
        currentState == UiState::READY || currentState == UiState::TYPING)) {
        // Find the subtitle box (it has empty text)
        for (auto& box : boxes) {
            if (box.text.empty() && box.bgColor.GetA() > 0 && box.fontSize == 0.f) {
                float y0 = box.rect.Y + pad;
                // NPC name
                Gdiplus::Font nameFont(L"Segoe UI", 14.f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
                Gdiplus::SolidBrush nameBrush(Gdiplus::Color(255, 100, 255, 150));
                Gdiplus::RectF nameRect(box.rect.X + pad, y0, box.rect.Width - pad * 2, 22.f);
                Gdiplus::StringFormat nfmt;
                nfmt.SetAlignment(Gdiplus::StringAlignmentNear);
                gfx.DrawString(npcName.c_str(), -1, &nameFont, nameRect, &nfmt, &nameBrush);

                // Subtitle text
                Gdiplus::Font subFont(L"Segoe UI", 16.f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
                Gdiplus::SolidBrush subBrush(Gdiplus::Color(255, 255, 255, 255));
                Gdiplus::RectF subRect(box.rect.X + pad, y0 + 26.f,
                    box.rect.Width - pad * 2, box.rect.Height - pad * 2 - 26.f);
                Gdiplus::StringFormat sfmt;
                sfmt.SetAlignment(Gdiplus::StringAlignmentNear);
                sfmt.SetLineAlignment(Gdiplus::StringAlignmentNear);
                sfmt.SetFormatFlags(0);
                gfx.DrawString(subtitle.c_str(), -1, &subFont, subRect, &sfmt, &subBrush);
                break;
            }
        }
    }

    gfx.Flush();
    }

finish:
    std::vector<BYTE> pixels(static_cast<size_t>(w) * h * 4);

    Gdiplus::Rect lockRect(0, 0, w, h);
    Gdiplus::BitmapData bitmapData{};
    if (bitmap.LockBits(&lockRect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bitmapData) == Gdiplus::Ok) {
        BYTE* destination = pixels.data();
        const BYTE* source = static_cast<const BYTE*>(bitmapData.Scan0);
        for (int y = 0; y < h; ++y) {
            const BYTE* sourceRow = source + static_cast<ptrdiff_t>(y) * bitmapData.Stride;
            BYTE* destinationRow = destination + static_cast<size_t>(y) * w * 4;
            for (int x = 0; x < w; ++x) {
                const BYTE* src = sourceRow + static_cast<size_t>(x) * 4;
                BYTE* dst = destinationRow + static_cast<size_t>(x) * 4;
                const unsigned int alpha = src[3];
                dst[0] = static_cast<BYTE>((src[0] * alpha + 127) / 255);
                dst[1] = static_cast<BYTE>((src[1] * alpha + 127) / 255);
                dst[2] = static_cast<BYTE>((src[2] * alpha + 127) / 255);
                dst[3] = src[3];
            }
        }
        bitmap.UnlockBits(&bitmapData);
    } else return;

    const uint64_t frame = NpcD3D::SubmitFrame(w, h, pixels.data(), w * 4);
    if (renderingSubtitle && frame != 0) {
        std::lock_guard<std::mutex> lock(g_data.mtx);
        if (g_data.state == UiState::SUBTITLE && g_data.subtitleGeneration == renderGeneration &&
            renderGeneration == g_conversationGeneration.load() && g_data.subtitleFrame == 0)
            g_data.subtitleFrame = frame;
    }
}

// ============================================================================
// Section 12: Overlay window
// ============================================================================

LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    // MCI aliases belong to their opening thread, not to each HTTP turn worker.
    case AUDIO_STOP_MESSAGE:
        mciSendStringW(L"close npc_reply", nullptr, 0, nullptr);
        return 0;
    case AUDIO_PLAY_MESSAGE: {
        auto& request = *reinterpret_cast<AudioPlayRequest*>(lParam);
        request.played = PlayAudioOnOverlay(*request.path, request.generation, request.duration);
        return 0;
    }
    case AUDIO_WRITE_MESSAGE: {
        auto& request = *reinterpret_cast<AudioWriteRequest*>(lParam);
        if (g_running.load() && request.generation == g_conversationGeneration.load())
            request.written = NpcAudio::ReplaceReplyFile(g_tempDir + L"\\npc_reply.mp3", *request.bytes);
        return 0;
    }
    case MIC_START_MESSAGE:
        StartRecording(static_cast<unsigned long>(wParam));
        return 0;
    case MIC_STOP_MESSAGE:
        FinishRecording(static_cast<DWORD>(wParam), static_cast<unsigned long>(lParam));
        return 0;
    case WM_TIMER:
        if (wParam == TIMER_ID) {
            if (!g_gameWnd || !IsWindow(g_gameWnd)) {
                g_gameWnd = FindGameWindow();
                if (g_gameWnd) {
                    WriteLog(L"Visible game window located.");
                    if (!g_worldReady && g_gameLayoutSupported.load()) {
                        g_worldReady = g_worldReader.Initialize(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)),
                            g_installed2014, GameDirectory());
                        WriteLog(g_worldReady ? L"World sampler validated after game-window startup."
                            : L"World sampler retry rejected; unsupported facts remain unavailable.");
                        WriteLog(Utf8ToWide(g_worldReader.InitializationDiagnostic()).c_str());
                    }
                }
            }
            static bool renderingLogged = false;
            if (!renderingLogged && NpcD3D::IsRendering()) {
                renderingLogged = true;
                WriteLog(L"Direct3D game frames confirmed; in-frame overlay rendering active.");
            }
            if (!g_gameWnd || !IsWindow(g_gameWnd)) {
                if (HasActiveConversation()) ExitConversation();
                return 0;
            }
            RECT client{};
            if (!GetClientRect(g_gameWnd, &client)) {
                if (HasActiveConversation()) ExitConversation();
                return 0;
            }
            int ow = client.right - client.left, oh = client.bottom - client.top;
            NpcD3D::GetViewportSize(ow, oh);
            if (ow < 100 || oh < 100) {
                if (HasActiveConversation()) ExitConversation();
                return 0;
            }
            const bool focused = !IsIconic(g_gameWnd) && GameIsForeground();
            g_microphone.Poll();
            bool recording = false;
            {
                std::lock_guard<std::mutex> lock(g_data.mtx);
                recording = g_data.state == UiState::RECORDING && g_recordGeneration == g_conversationGeneration.load();
            }
            if (recording && focused && !g_recordStartPosted && NpcInteraction::RecordHoldReady(GetTickCount(), g_recordDownTime)) {
                g_recordStartPosted = true;
                PostMessageW(g_overlay, MIC_START_MESSAGE, g_recordGeneration, 0);
            }
            if (recording && g_microphone.LimitReached()) {
                std::lock_guard<std::mutex> lock(g_data.mtx);
                g_data.statusText = L"15-second recording limit reached. Release R to send, or Esc to cancel.";
            }
            static DWORD lastProximityCheck = 0;
            const DWORD now = GetTickCount();
            bool leaveConversation = false;
            if (lastProximityCheck == 0 || now - lastProximityCheck >= 100 || !focused) {
                lastProximityCheck = now;
                CaptureWorldContext();
                NpcInteraction::Ped selected, nearest;
                bool active = false, selectedInRange = false;
                {
                    std::lock_guard<std::mutex> lock(g_data.mtx);
                    selected = g_data.selectedPed;
                    active = NpcInteraction::IsConversation(static_cast<unsigned>(g_data.state));
                }
                const bool memorySceneAvailable = FindNearbyPed(nearest, selected, selectedInRange);
                static bool previousSceneAvailable = false;
                if (previousSceneAvailable && !memorySceneAvailable) g_sceneEpoch.fetch_add(1);
                previousSceneAvailable = memorySceneAvailable;
                const bool sceneAvailable = focused && memorySceneAvailable;
                {
                    std::lock_guard<std::mutex> lock(g_data.mtx);
                    g_data.nearbyPed = sceneAvailable ? nearest : NpcInteraction::Ped{};
                    g_data.nearNpc = g_data.nearbyPed.address != 0;
                    leaveConversation = active && (!sceneAvailable || !selectedInRange);
                }
            }
            static unsigned long loggedHold = 0;
            const unsigned long held = g_heldConversation.load();
            if (held && held != loggedHold) {
                loggedHold = held;
                WriteLog(L"Selected NPC is held standing on the game thread.");
            }
            bool holdFailed = false;
            {
                std::lock_guard<std::mutex> lock(g_data.mtx);
                holdFailed = g_data.selectedPed.address && g_holdFailure.load() == g_conversationGeneration.load();
            }
            if (leaveConversation || holdFailed) {
                if (holdFailed) WriteLog(L"NPC standstill hold failed safely; conversation cancelled.");
                ExitConversation();
            }

            // --- State machine ---
            {
                std::lock_guard lk(g_data.mtx);
                g_data.cursorBlink = static_cast<int>(now / 500);

                switch (g_data.state) {
                case UiState::HIDDEN:
                    if (focused && g_data.nearNpc && !g_cancelPending.load()) g_data.state = UiState::PROMPT;
                    break;

                case UiState::PROMPT:
                    if (!g_data.nearNpc || !focused) {
                        g_data.state = UiState::HIDDEN;
                    }
                    break;

                case UiState::SUBTITLE:
                    if (NpcInteraction::DeadlineReached(now, g_data.subtitleEnd) &&
                        (!g_data.audioPlaying || !AudioIsPlaying())) {
                        StopAudio();
                        g_data.state = UiState::READY;
                        g_data.audioPlaying = false;
                        g_data.subtitle.clear();
                    }
                    break;

                case UiState::ERROR_SHOW:
                    if (NpcInteraction::DeadlineReached(now, g_data.subtitleEnd)) {
                        g_data.state = UiState::READY;
                        g_data.errorText.clear();
                    }
                    break;

                default:
                    break;
                }
            }

            // --- Render ---
            RenderOverlay(ow, oh);
        }
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_ID);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ============================================================================
// Section 13: Worker thread
// ============================================================================

DWORD WINAPI OverlayThreadProc(LPVOID) {
    if (!NpcD3D::Initialize()) WriteLog(L"Direct3D hook not yet installed; rendering must be confirmed before interaction.");
    Gdiplus::GdiplusStartupInput gdipInput;
    if (Gdiplus::GdiplusStartup(&g_gdipToken, &gdipInput, nullptr) != Gdiplus::Ok) {
        WriteLog(L"GDI+ text renderer initialization failed.");
        return 1;
    }
    g_gameLayoutSupported.store(DetectGameLayout());
    g_worldReady = g_gameLayoutSupported.load() && g_worldReader.Initialize(
        reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)), g_installed2014, GameDirectory());
    WriteLog(g_worldReady ? L"Verified game clock/weather/zone sampler initialized (real game values, not PC time)."
        : L"World sampler unavailable for this executable; dialogue will not invent live game facts.");
    if (!g_worldReady) WriteLog(Utf8ToWide(g_worldReader.InitializationDiagnostic()).c_str());
    wchar_t bases[128]{};
    swprintf_s(bases, L"World layout diagnostic: moduleBase=0x%08X, headerImageBase=0x%08X.",
        static_cast<unsigned>(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))), static_cast<unsigned>(g_gameHeaderImageBase));
    WriteLog(bases);
    NpcD3D::SetGameFrameCallback(OnGameFrame);
    WriteLog(LoadModelGenderMetadata()
        ? L"Installed model/animation gender metadata loaded; it overrides generic runtime ped types."
        : L"Model gender metadata unavailable; using civil ped types or neutral fallback.");
    WriteLog(g_gameLayoutSupported.load()
        ? L"Game code fingerprints recognized; nearest pedestrian interaction enabled."
        : L"Unsupported game code fingerprints; proximity interaction safely disabled.");

    // Prep temp directory
    wchar_t tmpBuf[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tmpBuf);
    g_tempDir = tmpBuf;
    g_tempDir += L"MalayalamNpcDemo";
    EnsureDir(g_tempDir);

    LoadBridgeToken();

    // Register window class
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = OverlayWndProc;
    wc.hInstance      = GetModuleHandleW(nullptr);
    wc.lpszClassName  = OVERLAY_CLASS;
    wc.hCursor        = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&wc)) {
        WriteLog(L"Failed to register overlay window class.");
        Gdiplus::GdiplusShutdown(g_gdipToken);
        return 1;
    }

    // This invisible window owns the timer and keyboard-hook message queue only.
    g_overlay = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        OVERLAY_CLASS, L"MalayalamNpcOverlay",
        WS_POPUP,
        0, 0, 1, 1,
        nullptr, nullptr, wc.hInstance, nullptr);

    if (!g_overlay) {
        WriteLog(L"Failed to create overlay window.");
        UnregisterClassW(OVERLAY_CLASS, wc.hInstance);
        Gdiplus::GdiplusShutdown(g_gdipToken);
        return 1;
    }

    g_heartbeatThread = CreateThread(nullptr, 0, BridgeHeartbeatProc, nullptr, 0, nullptr);
    if (!g_heartbeatThread) WriteLog(L"Could not start the bridge heartbeat thread.");

    HMODULE pluginModule = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&LowLevelKeyboardProc), &pluginModule);
    g_kbHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, pluginModule, 0);
    if (!g_kbHook) WriteLog(L"Keyboard hook failed; interaction keys are unavailable.");
    SetTimer(g_overlay, TIMER_ID, TIMER_MS, nullptr);
    WriteLog(L"Interaction timer ready; waiting for game-rendered frames and a nearby pedestrian.");

    // Message loop
    MSG msg;
    while (g_running.load() && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // Cleanup
    if (g_stopEvent) SetEvent(g_stopEvent);
    if (g_heartbeatThread) {
        WaitForSingleObject(g_heartbeatThread, 1500);
        CloseHandle(g_heartbeatThread);
        g_heartbeatThread = nullptr;
    }
    if (g_kbHook) { UnhookWindowsHookEx(g_kbHook); g_kbHook = nullptr; }
    NpcD3D::Shutdown();
    g_microphone.Cancel();
    g_recordGeneration = 0;
    StopAudio();
    DeleteFileW((g_tempDir + L"\\npc_reply.mp3").c_str());
    if (g_overlay && IsWindow(g_overlay)) DestroyWindow(g_overlay);
    g_overlay = nullptr;
    UnregisterClassW(OVERLAY_CLASS, wc.hInstance);
    Gdiplus::GdiplusShutdown(g_gdipToken);
    g_gdipToken = 0;

    return 0;
}

}  // anonymous namespace

// ============================================================================
// Section 14: ASI entry points
// ============================================================================

extern "C" __declspec(dllexport) void __cdecl InitializeASI() {
    if (InterlockedCompareExchange(&g_initState, 1, 0) == 0) {
        WriteLog(L"ASI initialized — starting overlay system.");
        g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        g_running.store(true);
        g_thread = CreateThread(nullptr, 0, OverlayThreadProc, nullptr, 0, &g_threadId);
        if (!g_thread) WriteLog(L"Failed to create the overlay worker thread.");
    }
}

extern "C" __declspec(dllexport) void __cdecl ShutdownASI() {
    if (InterlockedCompareExchange(&g_initState, 2, 1) == 1) {
        WriteLog(L"ASI shutdown requested.");
        g_running.store(false);
        if (g_stopEvent) SetEvent(g_stopEvent);
        if (g_overlay && IsWindow(g_overlay)) {
            PostMessageW(g_overlay, WM_CLOSE, 0, 0);
        }
        if (g_thread) {
            WaitForSingleObject(g_thread, 5000);
            CloseHandle(g_thread);
            g_thread = nullptr;
        }
        if (g_stopEvent) {
            CloseHandle(g_stopEvent);
            g_stopEvent = nullptr;
        }
        WriteLog(L"ASI shutdown complete.");
    }
}
