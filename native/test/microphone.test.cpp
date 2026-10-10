#include "..\src\microphone.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <deque>
#include <limits>
#include <stdexcept>

namespace {
using namespace NpcMicrophone;

void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

unsigned Read16(const std::vector<unsigned char>& bytes, std::size_t offset) {
    return bytes.at(offset) | (static_cast<unsigned>(bytes.at(offset + 1)) << 8);
}

unsigned Read32(const std::vector<unsigned char>& bytes, std::size_t offset) {
    unsigned value = 0;
    for (unsigned i = 0; i < 4; ++i) value |= static_cast<unsigned>(bytes.at(offset + i)) << (8 * i);
    return value;
}

void CheckWave(const std::vector<unsigned char>& wave, std::size_t pcmLength) {
    Check(wave.size() == pcmLength + 44, "WAV length");
    Check(std::memcmp(wave.data(), "RIFF", 4) == 0, "RIFF identifier");
    Check(Read32(wave, 4) == pcmLength + 36, "RIFF little-endian size");
    Check(std::memcmp(wave.data() + 8, "WAVEfmt ", 8) == 0, "WAVE and fmt identifiers");
    Check(Read32(wave, 16) == 16, "PCM fmt size");
    Check(Read16(wave, 20) == WAVE_FORMAT_PCM, "PCM format");
    Check(Read16(wave, 22) == 1, "mono channel count");
    Check(Read32(wave, 24) == 16000, "sample rate");
    Check(Read32(wave, 28) == 32000, "byte rate");
    Check(Read16(wave, 32) == 2, "block alignment");
    Check(Read16(wave, 34) == 16, "sample bits");
    Check(std::memcmp(wave.data() + 36, "data", 4) == 0, "data identifier");
    Check(Read32(wave, 40) == pcmLength, "data little-endian size");
}

// These functions never forward to WinMM. All device activity is synthetic.
struct FakeDriver {
    static FakeDriver* current;
    std::deque<WAVEHDR*> queued;
    std::array<WAVEHDR*, 8> prepared{};
    std::vector<unsigned char> partial;
    DWORD now = 1000;
    bool opened = false;
    bool started = false;
    int openCalls = 0, prepareCalls = 0, addCalls = 0, startCalls = 0;
    int stopCalls = 0, resetCalls = 0, unprepareCalls = 0, closeCalls = 0;
    int failPrepareAt = 0, failAddAt = 0;
    int failStopCount = 0, failResetCount = 0, failUnprepareCount = 0, failCloseCount = 0;
    MMRESULT openError = MMSYSERR_NOERROR;
    MMRESULT startError = MMSYSERR_NOERROR;

    FakeDriver() { Check(current == nullptr, "one fake device at a time"); current = this; }
    ~FakeDriver() { current = nullptr; }

    static MMRESULT WINAPI Open(LPHWAVEIN handle, UINT id, LPCWAVEFORMATEX format,
        DWORD_PTR callback, DWORD_PTR instance, DWORD flags) {
        FakeDriver& d = *current;
        ++d.openCalls;
        Check(!d.opened, "opening a device twice");
        Check(id == WAVE_MAPPER, "default microphone selection");
        Check(flags == CALLBACK_NULL && callback == 0 && instance == 0, "no callbacks or threads");
        Check(format->wFormatTag == WAVE_FORMAT_PCM && format->nChannels == 1 &&
            format->nSamplesPerSec == 16000 && format->nAvgBytesPerSec == 32000 &&
            format->nBlockAlign == 2 && format->wBitsPerSample == 16 && format->cbSize == 0,
            "exact capture format");
        if (d.openError != MMSYSERR_NOERROR) return d.openError;
        d.opened = true;
        *handle = reinterpret_cast<HWAVEIN>(&d);
        return MMSYSERR_NOERROR;
    }

    static MMRESULT WINAPI Prepare(HWAVEIN handle, LPWAVEHDR header, UINT size) {
        FakeDriver& d = *current;
        Check(handle == reinterpret_cast<HWAVEIN>(&d) && d.opened && size == sizeof(WAVEHDR), "prepare handle");
        ++d.prepareCalls;
        if (d.failPrepareAt == d.prepareCalls) return MMSYSERR_NOMEM;
        Check(header->lpData && header->dwBufferLength == 64000, "bounded ring storage covering full hold backlog");
        for (WAVEHDR*& slot : d.prepared) {
            if (slot == nullptr) {
                slot = header;
                header->dwFlags |= WHDR_PREPARED;
                return MMSYSERR_NOERROR;
            }
        }
        throw std::runtime_error("too many prepared headers");
    }

    static MMRESULT WINAPI Add(HWAVEIN handle, LPWAVEHDR header, UINT size) {
        FakeDriver& d = *current;
        Check(handle == reinterpret_cast<HWAVEIN>(&d) && size == sizeof(WAVEHDR), "add handle");
        ++d.addCalls;
        Check((header->dwFlags & WHDR_PREPARED) != 0 && !(header->dwFlags & WHDR_INQUEUE), "safe requeue");
        if (d.failAddAt == d.addCalls) return MMSYSERR_ERROR;
        header->dwFlags = (header->dwFlags & ~WHDR_DONE) | WHDR_INQUEUE;
        header->dwBytesRecorded = 0;
        d.queued.push_back(header);
        return MMSYSERR_NOERROR;
    }

    static MMRESULT WINAPI Start(HWAVEIN handle) {
        FakeDriver& d = *current;
        Check(handle == reinterpret_cast<HWAVEIN>(&d), "start handle");
        ++d.startCalls;
        if (d.startError != MMSYSERR_NOERROR) return d.startError;
        d.started = true;
        return MMSYSERR_NOERROR;
    }

    static MMRESULT WINAPI Stop(HWAVEIN handle) {
        FakeDriver& d = *current;
        Check(handle == reinterpret_cast<HWAVEIN>(&d), "stop handle");
        ++d.stopCalls;
        if (d.failStopCount > 0) { --d.failStopCount; return MMSYSERR_ERROR; }
        d.started = false;
        return MMSYSERR_NOERROR;
    }

    static MMRESULT WINAPI Reset(HWAVEIN handle) {
        FakeDriver& d = *current;
        Check(handle == reinterpret_cast<HWAVEIN>(&d), "reset handle");
        ++d.resetCalls;
        if (d.failResetCount > 0) { --d.failResetCount; return MMSYSERR_ERROR; }
        if (!d.partial.empty() && !d.queued.empty()) {
            d.Complete(d.partial);
            d.partial.clear();
        }
        while (!d.queued.empty()) d.Complete({});
        d.started = false;
        return MMSYSERR_NOERROR;
    }

    static MMRESULT WINAPI Unprepare(HWAVEIN handle, LPWAVEHDR header, UINT size) {
        FakeDriver& d = *current;
        Check(handle == reinterpret_cast<HWAVEIN>(&d) && size == sizeof(WAVEHDR), "unprepare handle");
        ++d.unprepareCalls;
        if (d.failUnprepareCount > 0) { --d.failUnprepareCount; return MMSYSERR_ERROR; }
        if (header->dwFlags & WHDR_INQUEUE) return WAVERR_STILLPLAYING;
        for (WAVEHDR*& slot : d.prepared) {
            if (slot == header) {
                slot = nullptr;
                header->dwFlags &= ~WHDR_PREPARED;
                return MMSYSERR_NOERROR;
            }
        }
        throw std::runtime_error("unprepare unknown header");
    }

    static MMRESULT WINAPI Close(HWAVEIN handle) {
        FakeDriver& d = *current;
        Check(handle == reinterpret_cast<HWAVEIN>(&d), "close handle");
        ++d.closeCalls;
        if (d.failCloseCount > 0) { --d.failCloseCount; return MMSYSERR_ERROR; }
        if (!d.queued.empty() || std::any_of(d.prepared.begin(), d.prepared.end(),
            [](WAVEHDR* header) { return header != nullptr; })) return WAVERR_STILLPLAYING;
        d.opened = false;
        return MMSYSERR_NOERROR;
    }

    static DWORD WINAPI Clock() { return current->now; }

    WinMmOperations Operations() const {
        WinMmOperations ops;
        ops.Open = Open; ops.Prepare = Prepare; ops.Add = Add; ops.Start = Start;
        ops.Stop = Stop; ops.Reset = Reset; ops.Unprepare = Unprepare; ops.Close = Close;
        ops.Clock = Clock;
        return ops;
    }

    void Complete(const std::vector<unsigned char>& bytes) {
        Check(!queued.empty(), "fake completion needs queued header");
        WAVEHDR* header = queued.front();
        queued.pop_front();
        Check(bytes.size() <= header->dwBufferLength, "fake data fits buffer");
        std::copy(bytes.begin(), bytes.end(), reinterpret_cast<unsigned char*>(header->lpData));
        header->dwBytesRecorded = static_cast<DWORD>(bytes.size());
        header->dwFlags = (header->dwFlags & ~WHDR_INQUEUE) | WHDR_DONE;
    }

    void Clean() const {
        Check(!opened && !started && queued.empty(), "closed synthetic device");
        Check(std::all_of(prepared.begin(), prepared.end(), [](WAVEHDR* header) { return header == nullptr; }),
            "all headers unprepared");
    }
};
FakeDriver* FakeDriver::current = nullptr;

void PureTests() {
    std::wstring error;
    const std::vector<unsigned char> pcm{0, 0, 0xff, 0x7f, 0, 0x80, 0xfe, 0xff};
    const auto wave = EncodePcm16Wave(pcm, error);
    Check(error.empty(), "PCM encoding succeeded");
    CheckWave(wave, pcm.size());
    Check(std::equal(pcm.begin(), pcm.end(), wave.begin() + 44), "signed sample bytes remain little endian");
    CheckWave(EncodePcm16Wave({}, error), 0);
    Check(error.empty(), "pure encoder accepts empty RIFF");
    Check(EncodePcm16Wave({1}, error).empty() && !error.empty(), "odd PCM rejected");
    std::vector<unsigned char> oversized(MaximumPcmBytes + 2, 0x77);
    Check(EncodePcm16Wave(oversized, error).empty() && !error.empty(), "oversize PCM rejected");
    std::vector<unsigned char> collected;
    bool limit = false;
    Check(AppendPcm16(collected, pcm.data(), pcm.size(), limit, error) && !limit, "first pure append");
    Check(AppendPcm16(collected, oversized.data(), oversized.size(), limit, error) && limit, "append clamps at 15 seconds");
    Check(collected.size() == 480000, "15-second byte bound");
    Check(std::equal(pcm.begin(), pcm.end(), collected.begin()), "append preserves leading order");
    const auto before = collected;
    Check(AppendPcm16(collected, pcm.data(), pcm.size(), limit, error) && limit && collected == before,
        "additional held-key audio cannot exceed cap");
    CheckWave(EncodePcm16Wave(collected, error), MaximumPcmBytes);
    collected.clear();
    Check(AppendPcm16(collected, nullptr, 0, limit, error) && !limit, "empty append");
    Check(!AppendPcm16(collected, nullptr, 2, limit, error) && !error.empty() && collected.empty(), "null data rejected");
    Check(!AppendPcm16(collected, pcm.data(), 1, limit, error) && collected.empty(), "incomplete frame rejected");
    Check(!AppendPcm16(collected, pcm.data(), std::numeric_limits<std::size_t>::max(), limit, error),
        "huge invalid size rejected before access");
    collected = {1};
    Check(!AppendPcm16(collected, pcm.data(), 2, limit, error) && collected.size() == 1, "bad existing PCM rejected");
    collected = oversized;
    Check(!AppendPcm16(collected, nullptr, 0, limit, error), "oversized existing collector rejected");
}

void UnusedAndEmptyTests() {
    { Recorder unused; unused.Poll(); unused.Cancel(); }
    FakeDriver d;
    Recorder recorder(d.Operations());
    std::wstring error;
    Check(!recorder.IsRecording() && !recorder.HasCapture() && !recorder.LimitReached() &&
        recorder.ElapsedMilliseconds() == 0, "unused state");
    recorder.Poll(); recorder.Cancel();
    Check(recorder.Stop(error).empty() && !error.empty(), "stop without capture is explicit error");
    Check(d.openCalls == 0 && d.stopCalls == 0, "no automatic microphone access");
    Check(recorder.Start(error) && error.empty(), "explicit synthetic start");
    Check(recorder.IsRecording() && recorder.HasCapture(), "active state");
    Check(!recorder.Start(error) && !error.empty() && d.openCalls == 1, "duplicate start cannot discard pending clip");
    Check(recorder.Stop(error).empty() && !error.empty(), "zero captured samples not fabricated as audio");
    Check(!recorder.HasCapture() && !recorder.IsRecording(), "empty stop clears state");
    d.Clean();
    Check(recorder.Start(error), "restart after empty stop");
    d.Complete({1, 2}); recorder.Poll(); recorder.Cancel(); recorder.Cancel();
    Check(recorder.Stop(error).empty() && !error.empty(), "cancel discards recorded bytes");
    d.Clean();
    Check(recorder.Start(error), "restart after repeated cancel");
    recorder.Cancel(); d.Clean();
}

void OrderAndPartialTests() {
    FakeDriver d;
    Recorder recorder(d.Operations());
    std::wstring error;
    Check(recorder.Start(error), "order start");
    std::vector<unsigned char> expected;
    auto batch = [&](unsigned first, unsigned count) {
        for (unsigned i = first; i < first + count; ++i) {
            const std::vector<unsigned char> bytes{static_cast<unsigned char>(i), 0};
            d.Complete(bytes);
            expected.insert(expected.end(), bytes.begin(), bytes.end());
        }
        recorder.Poll();
    };
    batch(1, 3);
    batch(4, 8); // Header indices now wrap: 3,4,5,6,7,0,1,2.
    batch(12, 5);
    d.Complete({17, 0}); expected.insert(expected.end(), {17, 0});
    d.Complete({18, 0}); expected.insert(expected.end(), {18, 0});
    d.partial = {19, 0, 20, 0}; expected.insert(expected.end(), d.partial.begin(), d.partial.end());
    const auto wave = recorder.Stop(error);
    Check(error.empty(), "partial stop successful");
    CheckWave(wave, expected.size());
    Check(std::equal(expected.begin(), expected.end(), wave.begin() + 44), "recycled ring and final partial chronological order");
    Check(d.unprepareCalls == 8 && d.closeCalls == 1 && d.addCalls == 24, "all headers released after recycled ring");
    d.Clean();
}

void CapAndClockTests() {
    {
        FakeDriver d;
        Recorder recorder(d.Operations());
        std::wstring error;
        Check(recorder.Start(error), "entire hold backlog start");
        std::vector<unsigned char> expected;
        for (unsigned i = 0; i < 8; ++i) {
            const std::vector<unsigned char> bytes(64000, static_cast<unsigned char>(i + 1));
            d.Complete(bytes);
            expected.insert(expected.end(), bytes.begin(), bytes.end());
        }
        d.now += 16000;
        recorder.Poll();
        Check(recorder.LimitReached() && !recorder.IsRecording() && recorder.HasCapture(),
            "entire hold backlog capped without dropping captured prefix");
        d.Clean();
        const auto wave = recorder.Stop(error);
        Check(error.empty(), "entire hold backlog release");
        CheckWave(wave, MaximumPcmBytes);
        Check(std::equal(expected.begin(), expected.begin() + MaximumPcmBytes, wave.begin() + 44),
            "oldest 15 seconds survive delayed Poll and final-buffer truncation");
    }
    {
        FakeDriver d;
        Recorder recorder(d.Operations());
        std::wstring error;
        Check(recorder.Start(error), "sample cap start");
        std::vector<unsigned char> expected;
        for (unsigned i = 0; i < 150; ++i) {
            const std::vector<unsigned char> bytes(3200, static_cast<unsigned char>(i));
            d.Complete(bytes); expected.insert(expected.end(), bytes.begin(), bytes.end());
            d.now += 100;
            recorder.Poll();
        }
        Check(recorder.LimitReached() && recorder.HasCapture() && !recorder.IsRecording(), "cap retains clip without sending");
        Check(recorder.ElapsedMilliseconds() == 15000, "elapsed capped");
        d.Clean();
        const int calls = d.openCalls + d.addCalls + d.stopCalls;
        d.now += 60000; recorder.Poll();
        Check(d.openCalls + d.addCalls + d.stopCalls == calls, "held key after cap does not reopen hardware");
        Check(!recorder.Start(error) && !error.empty(), "cap waits for release or cancel");
        const auto wave = recorder.Stop(error);
        Check(error.empty(), "capped release success");
        CheckWave(wave, MaximumPcmBytes);
        Check(std::equal(expected.begin(), expected.end(), wave.begin() + 44), "150 synthetic chunks in order");
        Check(!recorder.HasCapture() && !recorder.LimitReached() && recorder.ElapsedMilliseconds() == 0, "release resets cap state");
    }
    {
        FakeDriver d;
        Recorder recorder(d.Operations());
        std::wstring error;
        Check(recorder.Start(error), "wall cap start");
        d.partial = {0x34, 0x12};
        d.now += 16000; recorder.Poll();
        Check(recorder.LimitReached() && !recorder.IsRecording() && recorder.HasCapture(), "wall clock cap despite missing completed buffers");
        Check(recorder.ElapsedMilliseconds() == 15000, "wall cap remains after partial flush");
        d.Clean();
        const auto wave = recorder.Stop(error);
        Check(error.empty(), "wall cap partial release");
        CheckWave(wave, 2);
        Check(wave[44] == 0x34 && wave[45] == 0x12, "wall cap retains partial bytes");
    }
    {
        FakeDriver d;
        d.now = 0xfffffff0;
        Recorder recorder(d.Operations());
        std::wstring error;
        Check(recorder.Start(error), "clock wrap start");
        d.now = 0x30;
        Check(recorder.ElapsedMilliseconds() == 64, "GetTickCount wrap is unsigned");
        recorder.Cancel(); d.Clean();
    }
}

void FailureTests() {
    for (int failure = 0; failure < 6; ++failure) {
        FakeDriver d;
        if (failure == 0) d.openError = MMSYSERR_NODRIVER;
        if (failure == 1) d.openError = WAVERR_BADFORMAT;
        if (failure == 2) d.openError = MMSYSERR_ALLOCATED;
        if (failure == 3) d.failPrepareAt = 3;
        if (failure == 4) d.failAddAt = 3;
        if (failure == 5) d.startError = MMSYSERR_ERROR;
        Recorder recorder(d.Operations());
        std::wstring error;
        Check(!recorder.Start(error) && !error.empty(), "start failure is explicit");
        Check(error.find(L"permissions") != std::wstring::npos && error.find(L"default input") != std::wstring::npos,
            "actionable Windows device error");
        Check(!recorder.HasCapture() && !recorder.IsRecording(), "failed start has no capture");
        d.Clean();
        d.openError = d.startError = MMSYSERR_NOERROR; d.failPrepareAt = d.failAddAt = 0;
        Check(recorder.Start(error), "restart after startup failure");
        recorder.Cancel(); d.Clean();
    }
    for (int failure = 0; failure < 4; ++failure) {
        FakeDriver d;
        Recorder recorder(d.Operations());
        std::wstring error;
        Check(recorder.Start(error), "cleanup failure start");
        d.Complete({1, 0}); recorder.Poll();
        if (failure == 0) d.failStopCount = 1;
        if (failure == 1) d.failResetCount = 1;
        if (failure == 2) d.failUnprepareCount = 1;
        if (failure == 3) d.failCloseCount = 1;
        Check(recorder.Stop(error).empty() && !error.empty(), "cleanup failure cannot report successful WAV");
        Check(!recorder.HasCapture() && !recorder.IsRecording(), "failed stop clears capture");
        recorder.Cancel(); d.Clean();
        Check(recorder.Start(error), "restart after transient cleanup failure");
        recorder.Cancel(); d.Clean();
    }
    {
        FakeDriver d;
        Recorder recorder(d.Operations());
        std::wstring error;
        Check(recorder.Start(error), "requeue failure start");
        d.failAddAt = 9; d.Complete({3, 0}); recorder.Poll();
        Check(!recorder.IsRecording() && recorder.HasCapture(), "poll failure retained for release");
        d.Clean();
        Check(recorder.Stop(error).empty() && error.find(L"requeue") != std::wstring::npos, "deferred poll error surfaced");
    }
    {
        FakeDriver d;
        Recorder recorder(d.Operations());
        std::wstring error;
        Check(recorder.Start(error), "bad driver length start");
        WAVEHDR* header = d.queued.front();
        d.Complete({1, 0}); header->dwBytesRecorded = header->dwBufferLength + 2;
        recorder.Poll();
        Check(recorder.Stop(error).empty() && error.find(L"larger") != std::wstring::npos, "bad driver length rejected before read");
        d.Clean();
    }
    {
        FakeDriver d;
        Recorder recorder(d.Operations());
        std::wstring error;
        Check(recorder.Start(error), "broken driver odd frame start");
        d.Complete({1}); recorder.Poll();
        Check(recorder.Stop(error).empty() && !error.empty(), "driver incomplete frame rejected");
        d.Clean();
    }
    {
        FakeDriver d;
        Recorder recorder(d.Operations());
        std::wstring error;
        Check(recorder.Start(error), "persistent close failure start");
        d.failCloseCount = 2; recorder.Cancel();
        Check(!recorder.HasCapture() && !recorder.Start(error) && !error.empty() && d.openCalls == 1,
            "cannot reopen while driver still holds device");
        Check(recorder.Start(error), "retry after failed close retains safe storage");
        recorder.Cancel(); d.Clean();
    }
    {
        FakeDriver d;
        { Recorder recorder(d.Operations()); std::wstring error; Check(recorder.Start(error), "destructor cleanup start"); }
        d.Clean();
    }
    {
        FakeDriver d;
        auto operations = d.Operations(); operations.Add = nullptr;
        Recorder recorder(operations);
        std::wstring error;
        Check(!recorder.Start(error) && !error.empty() && d.openCalls == 0, "invalid operations never open hardware");
    }
}
}

int main() {
    try {
        PureTests();
        UnusedAndEmptyTests();
        OrderAndPartialTests();
        CapAndClockTests();
        FailureTests();
        std::puts("Microphone tests passed: pure WAV/collector bounds, synthetic ring order, partial flush, 15-second cap, cancellation, restart and WinMM failures. No microphone was opened.");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Microphone test failure: %s\n", error.what());
        return 1;
    }
}
