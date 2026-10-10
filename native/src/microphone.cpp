#include "microphone.h"
#include <algorithm>
#include <array>
#include <new>
#include <stdexcept>

#pragma comment(lib, "winmm.lib")

namespace NpcMicrophone {
namespace {
constexpr std::size_t BufferCount = 8;
// Queue 16 seconds so even a UI backlog spanning the entire hold cannot exhaust
// capture storage before the 15-second clip cap. Only completed/returned bytes count.
constexpr DWORD BufferBytes = SampleRate * 2 * 2;

void Remember(std::wstring& error, const std::wstring& message) {
    if (error.empty()) error = message;
}

std::wstring DeviceError(const wchar_t* operation, MMRESULT result) {
    wchar_t description[MAXERRORLENGTH]{};
    waveInGetErrorTextW(result, description, MAXERRORLENGTH);
    std::wstring message = L"Microphone ";
    message += operation;
    message += L" failed (WinMM " + std::to_wstring(result) + L")";
    if (description[0]) message += L": " + std::wstring(description);
    message += L". Check Windows microphone privacy permissions (including desktop apps), "
        L"the default input device in Sound settings, and whether another app is using it. "
        L"The input device must support mono 16 kHz 16-bit PCM.";
    return message;
}

void Write16(std::vector<unsigned char>& output, std::size_t offset, unsigned value) {
    output[offset] = static_cast<unsigned char>(value);
    output[offset + 1] = static_cast<unsigned char>(value >> 8);
}

void Write32(std::vector<unsigned char>& output, std::size_t offset, unsigned value) {
    for (unsigned i = 0; i < 4; ++i)
        output[offset + i] = static_cast<unsigned char>(value >> (i * 8));
}
}

bool AppendPcm16(std::vector<unsigned char>& pcm, const unsigned char* bytes,
    std::size_t byteCount, bool& limitReached, std::wstring& error) {
    error.clear();
    limitReached = pcm.size() == MaximumPcmBytes;
    if (pcm.size() > MaximumPcmBytes || (pcm.size() & 1) || (byteCount & 1) ||
        (byteCount != 0 && bytes == nullptr)) {
        error = L"Invalid PCM16 microphone buffer: samples must be complete two-byte frames within the 15-second limit.";
        return false;
    }
    const std::size_t count = std::min(byteCount, MaximumPcmBytes - pcm.size());
    try {
        if (count != 0) pcm.insert(pcm.end(), bytes, bytes + count);
    } catch (const std::bad_alloc&) {
        error = L"Not enough memory to retain microphone audio. Close other applications and try again.";
        return false;
    } catch (const std::length_error&) {
        error = L"The microphone buffer exceeds the supported memory size.";
        return false;
    }
    limitReached = pcm.size() == MaximumPcmBytes;
    return true;
}

std::vector<unsigned char> EncodePcm16Wave(const std::vector<unsigned char>& pcm,
    std::wstring& error) {
    error.clear();
    if ((pcm.size() & 1) || pcm.size() > MaximumPcmBytes) {
        error = L"Cannot encode microphone WAV: PCM16 samples are incomplete or exceed 15 seconds.";
        return {};
    }
    try {
        std::vector<unsigned char> output(44 + pcm.size(), 0);
        const unsigned char riff[] = {'R', 'I', 'F', 'F'};
        const unsigned char wave[] = {'W', 'A', 'V', 'E'};
        const unsigned char format[] = {'f', 'm', 't', ' '};
        const unsigned char data[] = {'d', 'a', 't', 'a'};
        std::copy(std::begin(riff), std::end(riff), output.begin());
        std::copy(std::begin(wave), std::end(wave), output.begin() + 8);
        std::copy(std::begin(format), std::end(format), output.begin() + 12);
        std::copy(std::begin(data), std::end(data), output.begin() + 36);
        Write32(output, 4, static_cast<unsigned>(36 + pcm.size()));
        Write32(output, 16, 16);
        Write16(output, 20, WAVE_FORMAT_PCM);
        Write16(output, 22, 1);
        Write32(output, 24, SampleRate);
        Write32(output, 28, SampleRate * 2);
        Write16(output, 32, 2);
        Write16(output, 34, 16);
        Write32(output, 40, static_cast<unsigned>(pcm.size()));
        std::copy(pcm.begin(), pcm.end(), output.begin() + 44);
        return output;
    } catch (const std::bad_alloc&) {
        error = L"Not enough memory to encode microphone WAV. Close other applications and try again.";
        return {};
    }
}

struct Recorder::CaptureState {
    HWAVEIN handle = nullptr;
    std::array<std::array<unsigned char, BufferBytes>, BufferCount> buffers{};
    std::array<WAVEHDR, BufferCount> headers{};
    std::array<bool, BufferCount> prepared{};
    std::size_t head = 0;
    std::size_t queued = 0;
};

Recorder::Recorder() = default;
Recorder::Recorder(const WinMmOperations& operations) : operations_(operations) {}

Recorder::~Recorder() {
    Cancel();
    if (state_) Cancel();
    // A broken driver can refuse reset/close. Never free memory it may still write into.
    // Normal paths free everything; this last-resort leak is safer than use-after-free.
    if (state_) state_.release();
}

bool Recorder::Start(std::wstring& error) {
    error.clear();
    if (hasCapture_) {
        error = L"A microphone capture is already pending. Release R to stop it, or cancel it before starting another.";
        return false;
    }
    if (!operations_.Open || !operations_.Prepare || !operations_.Add || !operations_.Start ||
        !operations_.Stop || !operations_.Reset || !operations_.Unprepare || !operations_.Close || !operations_.Clock) {
        error = L"The microphone WinMM operations are incomplete.";
        return false;
    }
    if (state_) {
        CloseHardware(false, error);
        if (state_) {
            Remember(error, L"The previous microphone device could not be closed. Reconnect the input device and try again.");
            return false;
        }
        error.clear();
    }
    ClearCapture();
    try {
        pcm_.reserve(MaximumPcmBytes);
        state_ = std::make_unique<CaptureState>();
    } catch (const std::bad_alloc&) {
        error = L"Not enough memory to start microphone capture. Close other applications and try again.";
        return false;
    }
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 1;
    format.nSamplesPerSec = SampleRate;
    format.nAvgBytesPerSec = SampleRate * 2;
    format.nBlockAlign = 2;
    format.wBitsPerSample = 16;
    MMRESULT result = operations_.Open(&state_->handle, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL);
    if (result != MMSYSERR_NOERROR) {
        error = DeviceError(L"open", result);
        CloseHardware(false, error);
        return false;
    }
    for (std::size_t i = 0; i < BufferCount; ++i) {
        WAVEHDR& header = state_->headers[i];
        header.lpData = reinterpret_cast<LPSTR>(state_->buffers[i].data());
        header.dwBufferLength = BufferBytes;
        result = operations_.Prepare(state_->handle, &header, sizeof(header));
        if (result != MMSYSERR_NOERROR) {
            error = DeviceError(L"buffer preparation", result);
            CloseHardware(false, error);
            return false;
        }
        state_->prepared[i] = true;
        result = operations_.Add(state_->handle, &header, sizeof(header));
        if (result != MMSYSERR_NOERROR) {
            error = DeviceError(L"buffer queue", result);
            CloseHardware(false, error);
            return false;
        }
        ++state_->queued;
    }
    result = operations_.Start(state_->handle);
    if (result != MMSYSERR_NOERROR) {
        error = DeviceError(L"start", result);
        CloseHardware(false, error);
        return false;
    }
    startedAt_ = operations_.Clock();
    recording_ = true;
    hasCapture_ = true;
    return true;
}

bool Recorder::Drain(bool requeue, std::wstring& error) {
    if (!state_ || !state_->handle) return true;
    const std::size_t count = state_->queued;
    for (std::size_t i = 0; i < count; ++i) {
        WAVEHDR& header = state_->headers[state_->head];
        if (!(header.dwFlags & WHDR_DONE)) break;
        state_->head = (state_->head + 1) % BufferCount;
        --state_->queued;
        if (header.dwBytesRecorded > header.dwBufferLength) {
            error = L"The microphone driver returned a buffer larger than its allocated storage. Check or update the input-device driver.";
            return false;
        }
        if (!AppendPcm16(pcm_, reinterpret_cast<const unsigned char*>(header.lpData),
            header.dwBytesRecorded, limitReached_, error)) return false;
        if (limitReached_) return true;
        if (requeue) {
            header.dwBytesRecorded = 0;
            const MMRESULT result = operations_.Add(state_->handle, &header, sizeof(header));
            if (result != MMSYSERR_NOERROR) {
                error = DeviceError(L"buffer requeue", result);
                return false;
            }
            ++state_->queued;
        }
    }
    return true;
}

void Recorder::CloseHardware(bool collect, std::wstring& error) {
    if (!state_) return;
    if (!state_->handle) {
        state_.reset();
        return;
    }
    MMRESULT result = operations_.Stop(state_->handle);
    if (result != MMSYSERR_NOERROR) Remember(error, DeviceError(L"stop", result));
    result = operations_.Reset(state_->handle);
    if (result != MMSYSERR_NOERROR) Remember(error, DeviceError(L"reset", result));
    if (collect) {
        std::wstring drainError;
        if (!Drain(false, drainError)) Remember(error, drainError);
    }
    for (std::size_t i = 0; i < BufferCount; ++i) {
        if (!state_->prepared[i]) continue;
        result = operations_.Unprepare(state_->handle, &state_->headers[i], sizeof(WAVEHDR));
        if (result == MMSYSERR_NOERROR) state_->prepared[i] = false;
        else Remember(error, DeviceError(L"buffer release", result));
    }
    result = operations_.Close(state_->handle);
    if (result == MMSYSERR_NOERROR) state_.reset();
    else Remember(error, DeviceError(L"close", result));
}

void Recorder::Finish() {
    elapsed_ = ElapsedMilliseconds();
    const bool reachedBeforeFlush = limitReached_;
    recording_ = false;
    CloseHardware(true, failure_);
    limitReached_ = reachedBeforeFlush || pcm_.size() == MaximumPcmBytes;
    if (limitReached_) elapsed_ = MaximumMilliseconds;
}

void Recorder::Poll() {
    if (!recording_) return;
    if (!Drain(true, failure_)) {
        Finish();
        return;
    }
    if (limitReached_ || ElapsedMilliseconds() >= MaximumMilliseconds) {
        limitReached_ = true;
        Finish();
    }
}

std::vector<unsigned char> Recorder::Stop(std::wstring& error) {
    error.clear();
    if (!hasCapture_) {
        error = L"No microphone capture is pending. Hold R to record before sending audio.";
        if (state_) CloseHardware(false, error);
        return {};
    }
    if (recording_) Finish();
    else if (state_) CloseHardware(true, failure_);
    std::vector<unsigned char> wave;
    if (!failure_.empty()) error = failure_;
    else if (pcm_.empty())
        error = L"No microphone audio was captured. Hold R longer, then check Windows microphone permissions and the default input device if the problem persists.";
    else wave = EncodePcm16Wave(pcm_, error);
    ClearCapture();
    return wave;
}

void Recorder::ClearCapture() {
    pcm_.clear();
    failure_.clear();
    recording_ = false;
    hasCapture_ = false;
    limitReached_ = false;
    startedAt_ = 0;
    elapsed_ = 0;
}

void Recorder::Cancel() {
    recording_ = false;
    std::wstring ignored;
    CloseHardware(false, ignored);
    ClearCapture();
}

bool Recorder::IsRecording() const { return recording_; }
bool Recorder::HasCapture() const { return hasCapture_; }
bool Recorder::LimitReached() const { return limitReached_; }
unsigned Recorder::ElapsedMilliseconds() const {
    if (!hasCapture_) return 0;
    if (!recording_) return elapsed_;
    return std::min(static_cast<unsigned>(operations_.Clock() - startedAt_), MaximumMilliseconds);
}
}
