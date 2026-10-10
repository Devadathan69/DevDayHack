#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <string>

namespace {

volatile LONG initializationState = 0;

bool CreateDirectoryIfMissing(const std::wstring& path) {
    if (CreateDirectoryW(path.c_str(), nullptr)) {
        return true;
    }

    return GetLastError() == ERROR_ALREADY_EXISTS;
}

void WriteLifecycleEvent(const wchar_t* eventName) {
    const DWORD required = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (required == 0 || required > 32767) {
        return;
    }

    std::wstring baseDirectory(required, L'\0');
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", baseDirectory.data(), required);
    if (length == 0 || length >= required) {
        return;
    }
    baseDirectory.resize(length);
    baseDirectory += L"\\MalayalamNpcDemo";
    if (!CreateDirectoryIfMissing(baseDirectory)) {
        return;
    }

    const std::wstring logDirectory = baseDirectory + L"\\logs";
    if (!CreateDirectoryIfMissing(logDirectory)) {
        return;
    }

    const std::wstring logPath = logDirectory + L"\\game-bridge.log";
    const HANDLE logFile = CreateFileW(
        logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (logFile == INVALID_HANDLE_VALUE) {
        return;
    }

    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t line[160]{};
    const int lineLength = swprintf_s(
        line, L"[%04u-%02u-%02u %02u:%02u:%02u] %s\r\n",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
        eventName);
    if (lineLength > 0) {
        DWORD bytesWritten = 0;
        WriteFile(logFile, line, static_cast<DWORD>(lineLength * sizeof(wchar_t)),
                  &bytesWritten, nullptr);
    }

    CloseHandle(logFile);
}

}  // namespace

extern "C" __declspec(dllexport) void __cdecl InitializeASI() {
    if (InterlockedCompareExchange(&initializationState, 1, 0) == 0) {
        WriteLifecycleEvent(L"ASI initialized");
    }
}

extern "C" __declspec(dllexport) void __cdecl ShutdownASI() {
    if (InterlockedCompareExchange(&initializationState, 2, 1) == 1) {
        WriteLifecycleEvent(L"ASI shutdown");
    }
}
