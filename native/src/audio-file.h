#pragma once
#include <Windows.h>
#include <mmsystem.h>
#include <string>

namespace NpcAudio {
inline bool ReplaceReplyFile(const std::wstring& filePath, const std::string& audio) {
    if (audio.empty() || audio.size() > MAXDWORD) return false;
    // Call on the audio-owner thread: MCI aliases are thread-local and keep stopped MP3s open.
    mciSendStringW(L"close npc_reply", nullptr, 0, nullptr);
    HANDLE file = CreateFileW(filePath.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const BOOL ok = WriteFile(file, audio.data(), static_cast<DWORD>(audio.size()), &written, nullptr);
    CloseHandle(file);
    return ok && written == audio.size();
}
}
