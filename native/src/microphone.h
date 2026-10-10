#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace NpcMicrophone {

constexpr unsigned SampleRate = 16000;
constexpr unsigned MaximumMilliseconds = 15000;
constexpr std::size_t MaximumPcmBytes = SampleRate * 2 * MaximumMilliseconds / 1000;

// Pure helpers: little-endian mono PCM16; no device access or file writes.
bool AppendPcm16(std::vector<unsigned char>& pcm, const unsigned char* bytes,
    std::size_t byteCount, bool& limitReached, std::wstring& error);
std::vector<unsigned char> EncodePcm16Wave(const std::vector<unsigned char>& pcm,
    std::wstring& error);

// Injectable only to exercise device lifecycle failures without accessing a microphone.
struct WinMmOperations {
    decltype(&waveInOpen) Open = &waveInOpen;
    decltype(&waveInPrepareHeader) Prepare = &waveInPrepareHeader;
    decltype(&waveInAddBuffer) Add = &waveInAddBuffer;
    decltype(&waveInStart) Start = &waveInStart;
    decltype(&waveInStop) Stop = &waveInStop;
    decltype(&waveInReset) Reset = &waveInReset;
    decltype(&waveInUnprepareHeader) Unprepare = &waveInUnprepareHeader;
    decltype(&waveInClose) Close = &waveInClose;
    decltype(&GetTickCount) Clock = &GetTickCount;
};

// Owned by the persistent overlay UI thread; call every method on that thread.
// Poll around 30 Hz. Start is the only operation that opens capture hardware.
class Recorder {
public:
    Recorder();
    explicit Recorder(const WinMmOperations& operations);
    ~Recorder();
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    bool Start(std::wstring& error);
    void Poll();
    std::vector<unsigned char> Stop(std::wstring& error);
    void Cancel();
    bool IsRecording() const;
    // Stays true after the cap closes hardware; Stop on key release sends the clip.
    bool HasCapture() const;
    bool LimitReached() const;
    unsigned ElapsedMilliseconds() const;

private:
    struct CaptureState;
    bool Drain(bool requeue, std::wstring& error);
    void CloseHardware(bool collect, std::wstring& error);
    void Finish();
    void ClearCapture();

    WinMmOperations operations_;
    std::unique_ptr<CaptureState> state_;
    std::vector<unsigned char> pcm_;
    std::wstring failure_;
    DWORD startedAt_ = 0;
    unsigned elapsed_ = 0;
    bool recording_ = false;
    bool hasCapture_ = false;
    bool limitReached_ = false;
};

}
