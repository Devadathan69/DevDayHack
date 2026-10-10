#include "..\src\bootstrap.cpp"
#include <fstream>
#include <iterator>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    std::ifstream input(argv[1], std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (bytes.empty()) return 3;
    wchar_t directory[MAX_PATH]{}, unique[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, directory) || !GetTempFileNameW(directory, L"npa", 0, unique)) return 4;
    DeleteFileW(unique);
    if (!CreateDirectoryW(unique, nullptr)) return 5;
    g_tempDir = unique;
    g_threadId = GetCurrentThreadId();
    g_running.store(true);
    g_conversationGeneration.store(1);
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = OverlayWndProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"NpcAudioOwnerRegression";
    if (!RegisterClassW(&windowClass)) return 6;
    g_overlay = CreateWindowW(windowClass.lpszClassName, L"", WS_POPUP, 0, 0, 1, 1,
        nullptr, nullptr, windowClass.hInstance, nullptr);
    if (!g_overlay) return 7;
    auto pumpUntil = [](const std::atomic<bool>& done) {
        MSG message{};
        while (!done.load()) {
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            Sleep(1);
        }
    };
    int result = 0;
    for (int turn = 0; turn < 5; ++turn) {
        std::atomic<bool> done{false};
        bool written = false, played = false;
        DWORD duration = 0;
        std::thread worker([&]() {
            AudioWriteRequest request{&bytes, 1};
            SendMessageW(g_overlay, AUDIO_WRITE_MESSAGE, 0, reinterpret_cast<LPARAM>(&request));
            written = request.written;
            played = PlayAudioFile(g_tempDir + L"\\npc_reply.mp3", 1, duration);
            done.store(true);
        });
        pumpUntil(done);
        worker.join();
        if (!written || !played || duration == 0 || !AudioIsPlaying()) {
            std::fprintf(stderr, "Cross-worker reply %d failed: written=%d played=%d duration=%lu.\n",
                turn + 1, written, played, duration);
            result = 8;
            break;
        }
    }
    g_conversationGeneration.store(2);
    std::atomic<bool> cancelled{false};
    bool staleWrite = true, stalePlay = true;
    std::thread cancellingWorker([&]() {
        StopAudio();
        AudioWriteRequest stale{&bytes, 1};
        SendMessageW(g_overlay, AUDIO_WRITE_MESSAGE, 0, reinterpret_cast<LPARAM>(&stale));
        staleWrite = stale.written;
        DWORD duration = 0;
        stalePlay = PlayAudioFile(g_tempDir + L"\\npc_reply.mp3", 1, duration);
        cancelled.store(true);
    });
    pumpUntil(cancelled);
    cancellingWorker.join();
    if (staleWrite || stalePlay || AudioIsPlaying()) result = 9;
    g_running.store(false);
    StopAudio();
    DestroyWindow(g_overlay);
    g_overlay = nullptr;
    UnregisterClassW(windowClass.lpszClassName, windowClass.hInstance);
    DeleteFileW((g_tempDir + L"\\npc_reply.mp3").c_str());
    RemoveDirectoryW(g_tempDir.c_str());
    if (result == 0) std::puts("Production audio dispatcher passed: five distinct turn workers, owner-thread playback, cancellation and stale rejection.");
    return result;
}
