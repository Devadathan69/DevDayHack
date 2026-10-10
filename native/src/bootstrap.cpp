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
#include <algorithm>

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

constexpr uintptr_t PLAYER_PED_ADDRESS = 0xB6F5F0;
constexpr uintptr_t PED_POOL_ADDRESS = 0xB74490;
constexpr size_t PED_POOL_SLOT_SIZE = 0x7C4; // GTA SA 1.0 US CPool<CPed, CCopPed> slot size.
constexpr float INTERACT_RADIUS = 3.5f;
constexpr DWORD OVERLAY_FPS  = 30;
constexpr DWORD TIMER_MS     = 1000 / OVERLAY_FPS;
constexpr INTERNET_PORT DEFAULT_BACKEND_PORT = 4177;
constexpr int   MAX_INPUT_LEN = 500;
constexpr DWORD SUBTITLE_MIN_MS = 3000;
constexpr DWORD SUBTITLE_PER_CHAR_MS = 80;
constexpr UINT_PTR TIMER_ID  = 42001;

const wchar_t* const OVERLAY_CLASS = L"MalayalamNpcOverlay_v1";
const wchar_t* const BACKEND_HOST  = L"127.0.0.1";

// ============================================================================
// Section 2: Data structures
// ============================================================================

enum class UiState {
    HIDDEN,        // Nothing to show
    PROMPT,        // "Press F6 / Enter to talk to Manu"
    GREETING,      // Connecting + auto-greeting
    SUBTITLE,      // Showing NPC reply (+ optional audio)
    READY,         // Ready for player input
    TYPING,        // Player is typing
    PROCESSING,    // Waiting for backend
    ERROR_SHOW,    // Showing an error message
};

struct OverlayData {
    std::mutex       mtx;
    UiState          state        = UiState::HIDDEN;
    std::wstring     subtitle;
    std::wstring     npcName      = L"Manu";
    std::wstring     inputText;
    std::wstring     errorText;
    std::wstring     statusText;
    std::wstring     audioPath;
    DWORD            subtitleEnd  = 0;
    bool             backendOk   = false;
    bool             nearNpc     = false;
    int              cursorBlink = 0;
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
std::mutex          g_audioMutex;
std::mutex          g_bridgeConfigMutex;
std::string         g_bridgeToken;
INTERNET_PORT       g_backendPort = DEFAULT_BACKEND_PORT;
ULONG_PTR           g_gdipToken  = 0;
std::wstring        g_tempDir;
std::atomic<bool>  g_gameLayoutSupported{false};
std::atomic<unsigned long> g_conversationGeneration{0};

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

bool IsSupportedGta10Us() {
    HMODULE module = GetModuleHandleW(nullptr);
    if (!module || reinterpret_cast<uintptr_t>(module) != 0x400000) return false;
    IMAGE_DOS_HEADER dos{};
    if (!ReadMemorySafe(module, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew <= 0 || dos.e_lfanew > 0x10000) return false;
    IMAGE_NT_HEADERS nt{};
    if (!ReadMemorySafe(reinterpret_cast<const BYTE*>(module) + dos.e_lfanew, &nt, sizeof(nt)) ||
        nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_I386) return false;

    // These are the two GTA SA 1.0 US entry points recognized by Plugin-SDK.
    return nt.OptionalHeader.AddressOfEntryPoint == 0x424570 ||
           nt.OptionalHeader.AddressOfEntryPoint == 0x42457C;
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
// Section 5: Game memory — nearest pedestrian (GTA SA 1.0 US only)
// ============================================================================

bool ReadPlayerPosition(float& x, float& y, float& z) {
    if (!g_gameLayoutSupported.load()) return false;
    DWORD pedPtr = 0;
    if (!ReadMemorySafe(reinterpret_cast<const void*>(PLAYER_PED_ADDRESS), &pedPtr, sizeof(pedPtr)) ||
        pedPtr < 0x10000u) return false;

    // CEntity::m_matrix at offset +0x14
    DWORD matPtr = 0;
    if (!ReadMemorySafe(reinterpret_cast<const void*>(pedPtr + 0x14), &matPtr, sizeof(matPtr))) return false;
    if (matPtr < 0x10000u) return false;

    // CMatrix position vector at +0x30 (right.x, right.y, right.z, 0,  up.x, up.y, up.z, 0,
    //                                    at.x, at.y, at.z, 0,  pos.x, pos.y, pos.z, 0)
    float pos[3];
    if (!ReadMemorySafe(reinterpret_cast<const void*>(matPtr + 0x30), pos, sizeof(pos))) return false;

    x = pos[0]; y = pos[1]; z = pos[2];
    return (x > -4000.f && x < 4000.f && y > -4000.f && y < 4000.f && z > -200.f && z < 1200.f);
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

bool FindNearbyPed(float playerX, float playerY, float playerZ, bool& nearPed) {
    nearPed = false;
    if (!g_gameLayoutSupported.load()) return false;

    DWORD playerPed = 0;
    DWORD pool = 0;
    if (!ReadMemorySafe(reinterpret_cast<const void*>(PLAYER_PED_ADDRESS), &playerPed, sizeof(playerPed)) ||
        !ReadMemorySafe(reinterpret_cast<const void*>(PED_POOL_ADDRESS), &pool, sizeof(pool)) ||
        playerPed < 0x10000u || pool < 0x10000u) return false;

    DWORD objects = 0;
    DWORD flags = 0;
    int poolSize = 0;
    if (!ReadMemorySafe(reinterpret_cast<const void*>(pool), &objects, sizeof(objects)) ||
        !ReadMemorySafe(reinterpret_cast<const void*>(pool + 4), &flags, sizeof(flags)) ||
        !ReadMemorySafe(reinterpret_cast<const void*>(pool + 8), &poolSize, sizeof(poolSize)) ||
        objects < 0x10000u || flags < 0x10000u || poolSize < 1 || poolSize > 512) return false;

    const float radiusSquared = INTERACT_RADIUS * INTERACT_RADIUS;
    for (int i = 0; i < poolSize; ++i) {
        BYTE slotFlags = 0;
        if (!ReadMemorySafe(reinterpret_cast<const void*>(flags + i), &slotFlags, sizeof(slotFlags))) return false;
        if ((slotFlags & 0x80u) != 0) continue; // CPool slot is empty.

        const uint64_t pedAddress64 = static_cast<uint64_t>(objects) + static_cast<uint64_t>(i) * PED_POOL_SLOT_SIZE;
        if (pedAddress64 > 0xFFFFFFFFu || pedAddress64 == playerPed) continue;
        const uintptr_t pedAddress = static_cast<uintptr_t>(pedAddress64);

        DWORD vtable = 0;
        DWORD matrix = 0;
        if (!ReadMemorySafe(reinterpret_cast<const void*>(pedAddress), &vtable, sizeof(vtable)) ||
            !IsInsideGameImage(vtable) ||
            !ReadMemorySafe(reinterpret_cast<const void*>(pedAddress + 0x14), &matrix, sizeof(matrix)) ||
            matrix < 0x10000u) continue;

        float position[3]{};
        if (!ReadMemorySafe(reinterpret_cast<const void*>(matrix + 0x30), position, sizeof(position))) continue;
        if (position[0] <= -4000.f || position[0] >= 4000.f ||
            position[1] <= -4000.f || position[1] >= 4000.f ||
            position[2] <= -200.f || position[2] >= 1200.f) continue;

        const float dx = position[0] - playerX;
        const float dy = position[1] - playerY;
        const float dz = position[2] - playerZ;
        if (dx * dx + dy * dy + dz * dz <= radiusSquared) {
            nearPed = true;
            return true;
        }
    }
    return true;
}

// ============================================================================
// Section 6: HTTP client (WinHTTP, synchronous on worker threads)
// ============================================================================

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

void StopAudio() {
    std::lock_guard<std::mutex> lock(g_audioMutex);
    mciSendStringW(L"close npc_reply", nullptr, 0, nullptr);
}

bool PlayAudioFile(const std::wstring& filePath) {
    std::lock_guard<std::mutex> lock(g_audioMutex);
    mciSendStringW(L"close npc_reply", nullptr, 0, nullptr);
    std::wstring cmd = L"open \"" + filePath + L"\" type mpegvideo alias npc_reply";
    MCIERROR err = mciSendStringW(cmd.c_str(), nullptr, 0, nullptr);
    if (err != 0) return false;
    return mciSendStringW(L"play npc_reply", nullptr, 0, nullptr) == 0;
}

bool DownloadAndPlayAudio(const std::string& audioUrlPath) {
    if (audioUrlPath.empty()) return false;
    std::wstring widePath = Utf8ToWide(audioUrlPath);
    HttpResponse resp = HttpRequest(L"GET", widePath.c_str());
    if (!resp.ok() || resp.body.empty()) return false;

    std::wstring filePath = g_tempDir + L"\\npc_reply.mp3";
    HANDLE f = CreateFileW(filePath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD bw = 0;
    const BOOL writeOk = WriteFile(f, resp.body.data(), (DWORD)resp.body.size(), &bw, nullptr);
    CloseHandle(f);
    if (!writeOk || bw != resp.body.size()) return false;

    return PlayAudioFile(filePath);
}

// ============================================================================
// Section 9: Backend interaction (runs on detached threads)
// ============================================================================

bool BackendSelect() {
    const std::string body = "{\"gameSessionId\":\"gta-" + std::to_string(GetCurrentProcessId()) +
        "\",\"sceneGeneration\":0}";
    return HttpRequest(L"POST", L"/api/select", body, L"application/json").ok();
}

void BackendCancel() {
    HttpRequest(L"POST", L"/api/cancel", "{}", L"application/json", 2000);
}

void BackendTurn(const std::wstring& text) {
    const unsigned long generation = g_conversationGeneration.load();
    {
        std::lock_guard lk(g_data.mtx);
        g_data.state = UiState::PROCESSING;
        g_data.statusText = L"Manu is thinking...";
    }

    // Select first (idempotent). This is a local bridge request, not a game-thread call.
    if (!BackendSelect()) {
        std::lock_guard lk(g_data.mtx);
        if (generation == g_conversationGeneration.load()) {
            g_data.state = UiState::ERROR_SHOW;
            g_data.errorText = L"Cannot connect to the local conversation server. Start the backend and retry.";
            g_data.subtitleEnd = GetTickCount() + 5000;
        }
        return;
    }

    std::string utf8Text = WideToUtf8(text);
    const std::string escaped = EscapeJsonString(utf8Text);
    std::string body = R"({"text":")" + escaped + R"("})";
    HttpResponse resp = HttpRequest(L"POST", L"/api/text-turn", body, L"application/json");

    if (!g_running.load() || generation != g_conversationGeneration.load()) return;

    if (!resp.ok()) {
        std::string errMsg = ExtractJsonString(resp.body, "error");
        std::lock_guard lk(g_data.mtx);
        g_data.state = UiState::ERROR_SHOW;
        g_data.errorText = errMsg.empty() ? L"Backend returned an error." : Utf8ToWide(errMsg);
        g_data.subtitleEnd = GetTickCount() + 4000;
        return;
    }

    std::string replyMl = ExtractJsonString(resp.body, "replyMl");
    std::string audioUrl = ExtractJsonString(resp.body, "audioUrl");
    std::string turnId = ExtractJsonString(resp.body, "turnId");

    if (replyMl.empty()) {
        std::lock_guard lk(g_data.mtx);
        g_data.state = UiState::ERROR_SHOW;
        g_data.errorText = L"Manu had nothing to say. Try again.";
        g_data.subtitleEnd = GetTickCount() + 3000;
        return;
    }

    std::wstring wideReply = Utf8ToWide(replyMl);
    DWORD duration = (DWORD)(SUBTITLE_MIN_MS + wideReply.size() * SUBTITLE_PER_CHAR_MS);
    if (duration > 15000) duration = 15000;

    {
        std::lock_guard lk(g_data.mtx);
        g_data.subtitle = wideReply;
        g_data.subtitleEnd = GetTickCount() + duration;
        g_data.state = UiState::SUBTITLE;
    }

    // Try audio playback; subtitles remain visible if the generated audio is unavailable.
    bool audioPlayed = false;
    if (!audioUrl.empty()) {
        audioPlayed = DownloadAndPlayAudio(audioUrl);
    }
    if (!g_running.load() || generation != g_conversationGeneration.load()) {
        StopAudio();
        return;
    }

    // Acknowledge delivery
    if (!turnId.empty()) {
        std::string ackBody = R"({"turnId":")" + turnId + R"(","subtitleDisplayed":true,"audioPlayed":)" +
            (audioPlayed ? "true" : "false") + "}";
        HttpRequest(L"POST", L"/api/delivery", ackBody, L"application/json");
    }
}

void DoGreeting() {
    BackendTurn(L"Hello!");
}

void DoSendText(const std::wstring& text) {
    BackendTurn(text);
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
    if (nCode != HC_ACTION || !GameIsForeground()) return CallNextHookEx(g_kbHook, nCode, wParam, lParam);
    KBDLLHOOKSTRUCT* kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
    if (wParam != WM_KEYDOWN && wParam != WM_SYSKEYDOWN) return CallNextHookEx(g_kbHook, nCode, wParam, lParam);
    const DWORD vk = kb->vkCode;

    UiState state;
    {
        std::lock_guard<std::mutex> lock(g_data.mtx);
        state = g_data.state;
    }
    const bool isTyping = state == UiState::TYPING || state == UiState::READY;
    const bool conversationActive = state == UiState::GREETING || state == UiState::SUBTITLE ||
        state == UiState::READY || state == UiState::TYPING || state == UiState::PROCESSING || state == UiState::ERROR_SHOW;

    // Always let through: Alt+Tab, Windows key, Alt+F4.
    if (vk == VK_LWIN || vk == VK_RWIN) return CallNextHookEx(g_kbHook, nCode, wParam, lParam);
    if (vk == VK_TAB && (GetAsyncKeyState(VK_MENU) & 0x8000)) return CallNextHookEx(g_kbHook, nCode, wParam, lParam);
    if (vk == VK_F4 && (GetAsyncKeyState(VK_MENU) & 0x8000)) return CallNextHookEx(g_kbHook, nCode, wParam, lParam);

    // Escape cancels even while a backend response is still processing.
    if (vk == VK_ESCAPE && conversationActive) {
        g_conversationGeneration.fetch_add(1);
        {
            std::lock_guard<std::mutex> lock(g_data.mtx);
            g_data.state = g_gameLayoutSupported.load() ? UiState::HIDDEN : UiState::PROMPT;
            g_data.inputText.clear();
            g_data.subtitle.clear();
        }
        StopAudio();
        std::thread([]() { BackendCancel(); }).detach();
        return 1;
    }

    // Handle the interaction key in the hook so GTA does not also consume
    // Enter as a game/menu action. Polling GetAsyncKeyState on a timer can miss
    // short presses because other game code may clear its transition bit.
    if (state == UiState::PROMPT && (vk == VK_F6 || vk == VK_RETURN)) {
        bool startConversation = false;
        {
            std::lock_guard<std::mutex> lock(g_data.mtx);
            if (g_data.state == UiState::PROMPT) {
                g_data.state = UiState::GREETING;
                startConversation = true;
            }
        }
        if (startConversation) std::thread([]() { DoGreeting(); }).detach();
        return 1;
    }

    if (!isTyping) return CallNextHookEx(g_kbHook, nCode, wParam, lParam);

    // Enter sends the current text; an empty input is ignored.
    if (vk == VK_RETURN) {
        std::wstring textToSend;
        {
            std::lock_guard<std::mutex> lock(g_data.mtx);
            textToSend = g_data.inputText;
            g_data.inputText.clear();
            if (!textToSend.empty()) {
                g_data.state = UiState::PROCESSING;
                g_data.statusText = L"Manu is thinking...";
            }
        }
        if (!textToSend.empty()) {
            std::thread([text = std::move(textToSend)]() { DoSendText(text); }).detach();
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
    wchar_t ch[4]{};
    const int result = ToUnicode(vk, kb->scanCode, kbState, ch, 4, 0);
    if (result == 1 && ch[0] >= 32) {
        std::lock_guard<std::mutex> lock(g_data.mtx);
        if ((int)g_data.inputText.size() < MAX_INPUT_LEN) g_data.inputText += ch[0];
        if (g_data.state == UiState::READY) g_data.state = UiState::TYPING;
        return 1;
    }

    // While entering text, suppress GTA controls; leave the voice fallback key alone.
    if (vk != VK_F7) return 1;
    return CallNextHookEx(g_kbHook, nCode, wParam, lParam);
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

void RenderOverlay(HWND hwnd, int w, int h, POINT ptDst) {
    if (w <= 0 || h <= 0) return;

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
    std::wstring subtitle, inputText, npcName, statusText, errorText;
    int cursorBlink;
    {
        std::lock_guard lk(g_data.mtx);
        currentState = g_data.state;
        subtitle     = g_data.subtitle;
        inputText    = g_data.inputText;
        npcName      = g_data.npcName;
        statusText   = g_data.statusText;
        errorText    = g_data.errorText;
        cursorBlink  = g_data.cursorBlink;
    }

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
            L"Type and press Enter  \u2022  Press Esc to leave",
            Gdiplus::Color(200, 160, 160, 190),
            11.f, 1
        });
    }

    // -- Status / processing bar --
    if (currentState == UiState::PROCESSING || currentState == UiState::GREETING) {
        std::wstring display = (currentState == UiState::GREETING) ? L"Connecting to Manu..." : statusText;
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
        const std::wstring prompt = g_gameLayoutSupported.load()
            ? L"\u25B6  Press F6 or Enter to talk to " + npcName
            : L"\u25B6  Press F6 or Enter to chat (NPC proximity unavailable for this game build)";
        boxes.push_back({
            Gdiplus::RectF(panelX, curY, panelW, barH),
            Gdiplus::Color(210, 10, 10, 35),
            Gdiplus::Color(200, 255, 200, 60),
            prompt,
            Gdiplus::Color(255, 255, 220, 90),
            16.f, 1
        });
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
    // A top-down DIB preserves alpha for UpdateLayeredWindow. Premultiply its
    // color channels because ULW_ALPHA requires premultiplied BGRA pixels.
    HDC screenDC = GetDC(nullptr);
    HDC memDC = CreateCompatibleDC(screenDC);
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* dibPixels = nullptr;
    HBITMAP hBmp = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &dibPixels, nullptr, 0);
    if (!screenDC || !memDC || !hBmp || !dibPixels) {
        if (hBmp) DeleteObject(hBmp);
        if (memDC) DeleteDC(memDC);
        if (screenDC) ReleaseDC(nullptr, screenDC);
        return;
    }
    HBITMAP old = static_cast<HBITMAP>(SelectObject(memDC, hBmp));
    ZeroMemory(dibPixels, static_cast<size_t>(w) * h * 4);

    Gdiplus::Rect lockRect(0, 0, w, h);
    Gdiplus::BitmapData bitmapData{};
    if (bitmap.LockBits(&lockRect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bitmapData) == Gdiplus::Ok) {
        BYTE* destination = static_cast<BYTE*>(dibPixels);
        const BYTE* source = static_cast<const BYTE*>(bitmapData.Scan0);
        const int sourceStride = std::abs(bitmapData.Stride);
        for (int y = 0; y < h; ++y) {
            const BYTE* sourceRow = bitmapData.Stride >= 0
                ? source + static_cast<size_t>(y) * sourceStride
                : source + static_cast<size_t>(h - 1 - y) * sourceStride;
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
    }

    POINT ptSrc = {0, 0};
    SIZE sz = {w, h};
    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    UpdateLayeredWindow(hwnd, screenDC, &ptDst, &sz, memDC, &ptSrc, 0, &blend, ULW_ALPHA);

    SelectObject(memDC, old);
    DeleteObject(hBmp);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);
}

// ============================================================================
// Section 12: Overlay window
// ============================================================================

LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TIMER:
        if (wParam == TIMER_ID) {
            if (!g_gameWnd || !IsWindow(g_gameWnd)) {
                g_gameWnd = FindGameWindow();
            }
            if (!g_gameWnd || !IsWindow(g_gameWnd) || IsIconic(g_gameWnd) || !GameIsForeground()) {
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            }
            ShowWindow(hwnd, SW_SHOWNOACTIVATE);

            RECT client{};
            POINT ptDst{0, 0};
            if (!GetClientRect(g_gameWnd, &client) || !ClientToScreen(g_gameWnd, &ptDst)) return 0;
            const int ow = client.right - client.left;
            const int oh = client.bottom - client.top;
            if (ow < 100 || oh < 100) return 0;

            // --- Proximity check ---
            static DWORD lastProximityCheck = 0;
            static bool cachedNearPed = false;
            const DWORD now = GetTickCount();
            if (g_gameLayoutSupported.load() && (lastProximityCheck == 0 || now - lastProximityCheck >= 200)) {
                lastProximityCheck = now;
                float px = 0.f, py = 0.f, pz = 0.f;
                bool foundPed = false;
                if (ReadPlayerPosition(px, py, pz) && FindNearbyPed(px, py, pz, foundPed)) {
                    cachedNearPed = foundPed;
                } else {
                    cachedNearPed = false;
                    g_gameLayoutSupported.store(false);
                    WriteLog(L"GTA memory layout read failed; switched to manual F6 chat mode.");
                }
            } else if (!g_gameLayoutSupported.load()) cachedNearPed = false;
            const bool nearPedAvailable = cachedNearPed;

            // --- State machine ---
            {
                std::lock_guard lk(g_data.mtx);
                g_data.nearNpc = nearPedAvailable;
                g_data.cursorBlink++;

                switch (g_data.state) {
                case UiState::HIDDEN:
                    if (nearPedAvailable || !g_gameLayoutSupported.load()) g_data.state = UiState::PROMPT;
                    break;

                case UiState::PROMPT:
                    if (!nearPedAvailable && g_gameLayoutSupported.load()) {
                        g_data.state = UiState::HIDDEN;
                    }
                    break;

                case UiState::SUBTITLE:
                    if (GetTickCount() > g_data.subtitleEnd) {
                        g_data.state = UiState::READY;
                    }
                    break;

                case UiState::ERROR_SHOW:
                    if (GetTickCount() > g_data.subtitleEnd) {
                        g_data.state = UiState::READY;
                        g_data.errorText.clear();
                    }
                    break;

                default:
                    break;
                }
            }

            // --- Render ---
            RenderOverlay(hwnd, ow, oh, ptDst);
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
    // Initialize GDI+
    Gdiplus::GdiplusStartupInput gdipInput;
    Gdiplus::GdiplusStartup(&g_gdipToken, &gdipInput, nullptr);

    g_gameLayoutSupported.store(IsSupportedGta10Us());
    WriteLog(g_gameLayoutSupported.load()
        ? L"GTA SA 1.0 US memory layout recognized; nearby pedestrian prompts enabled."
        : L"Unsupported GTA executable; nearby pedestrian detection disabled and manual F6 chat enabled.");

    // Prep temp directory
    wchar_t tmpBuf[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tmpBuf);
    g_tempDir = tmpBuf;
    g_tempDir += L"MalayalamNpcDemo";
    EnsureDir(g_tempDir);

    // Load bridge token (retry a few times in case server is starting)
    for (int i = 0; i < 10 && g_running.load(); i++) {
        if (LoadBridgeToken()) break;
        Sleep(2000);
    }

    // Check backend connectivity
    {
        HttpResponse resp = HttpRequest(L"GET", L"/api/status");
        std::lock_guard lk(g_data.mtx);
        g_data.backendOk = resp.ok();
    }

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

    // Create layered, transparent, topmost window
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    g_overlay = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        OVERLAY_CLASS, L"MalayalamNpcOverlay",
        WS_POPUP,
        0, 0, sw, sh,
        nullptr, nullptr, wc.hInstance, nullptr);

    if (!g_overlay) {
        WriteLog(L"Failed to create overlay window.");
        UnregisterClassW(OVERLAY_CLASS, wc.hInstance);
        Gdiplus::GdiplusShutdown(g_gdipToken);
        return 1;
    }

    g_heartbeatThread = CreateThread(nullptr, 0, BridgeHeartbeatProc, nullptr, 0, nullptr);
    if (!g_heartbeatThread) WriteLog(L"Could not start the bridge heartbeat thread.");

    ShowWindow(g_overlay, SW_SHOWNOACTIVATE);

    // Install keyboard hook
    g_kbHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandleW(nullptr), 0);

    // Start timer
    SetTimer(g_overlay, TIMER_ID, TIMER_MS, nullptr);
    WriteLog(L"Overlay initialized — F6/Enter starts a chat when the game window is focused.");

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
