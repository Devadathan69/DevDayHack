#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "..\src\interaction.h"
#include "..\src\game-layout.h"
#include "..\src\ped-metadata.h"
#include "..\src\audio-file.h"
#pragma comment(lib, "winmm.lib")
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>
#include <vector>
#include <thread>

using namespace NpcInteraction;

void TestInteraction() {
    Ped player{0x10000, 1, 0, 0, 0, 100, false, 0};
    Ped npc{0x20000, 258, 3, 0, 0, 100, false, 0};
    assert(CanTalk(player, npc, StartRadius));
    npc.x = StartRadius;
    assert(CanTalk(player, npc, StartRadius));
    npc.x += 0.01f;
    assert(!CanTalk(player, npc, StartRadius));
    assert(CanTalk(player, npc, LeaveRadius));
    npc.x = LeaveRadius + 0.01f;
    assert(!CanTalk(player, npc, LeaveRadius));
    npc.x = 2;
    npc.z = 2.01f;
    assert(!CanTalk(player, npc, StartRadius));
    npc.z = 0;
    npc.health = 0;
    assert(!CanTalk(player, npc, StartRadius));
    npc.health = std::numeric_limits<float>::quiet_NaN();
    assert(!CanTalk(player, npc, StartRadius));
    npc.health = 100;
    npc.inVehicle = true;
    assert(!CanTalk(player, npc, StartRadius));
    npc.inVehicle = false;
    player.inVehicle = true;
    assert(!CanTalk(player, npc, StartRadius));
    player.inVehicle = false;
    npc.area = 1;
    assert(!CanTalk(player, npc, StartRadius));
    npc.area = 0;
    assert(!CanTalk(player, player, StartRadius));
    Ped recycled = npc;
    assert(SamePed(npc, recycled));
    ++recycled.handle;
    assert(!SamePed(npc, recycled));
    assert(!SamePed(Ped{}, Ped{}));
    assert(ScopeGeneration(0, 258) != ScopeGeneration(1, 258));
    assert(ScopeGeneration(1, 258) != ScopeGeneration(1, 259));
    assert(ScopeGeneration(1, 0x1FFFFu) < ScopeGeneration(2, 0));
    recycled = npc;
    recycled.x = 1;
    assert(DistanceSquared(player, recycled) < DistanceSquared(player, npc));
    npc.x = std::numeric_limits<float>::infinity();
    assert(!CanTalk(player, npc, StartRadius));
    assert(DeadlineReached(10, 10));
    assert(!DeadlineReached(9, 10));
    assert(DeadlineReached(10, 0xFFFFFFF0));
    assert(!DeadlineReached(0xFFFFFFF0, 10));
    npc.pedType = 4;
    assert(std::string(Gender(npc)) == "male");
    npc.pedType = 5;
    assert(std::string(Gender(npc)) == "female");
    npc.pedType = 6;
    assert(std::string(Gender(npc)) == "unknown");
    npc.pedType = 4;
    npc.modelGender = 2;
    assert(std::string(Gender(npc)) == "female");
    assert(std::string(ModelGender(npc)) == "female");
    npc.pedType = 5;
    npc.modelGender = 1;
    assert(std::string(Gender(npc)) == "male");
    assert(!IsConversation(0) && !IsConversation(1));
    for (unsigned state = 2; state <= 8; ++state) assert(IsConversation(state));
    assert(IsRecordTap(179) && !IsRecordTap(180));
    assert(!RecordHoldReady(279, 100) && RecordHoldReady(280, 100));
    assert(RecordHoldReady(164, 0xFFFFFFF0));
    std::puts("Interaction checks passed (radius, identity, health, vehicles, area, timers).");
}

void TestModelMetadata() {
    const auto table = NpcPedMetadata::Parse(
        "# synthetic model metadata, not game assets\n"
        "peds\n"
        "1, fixture, fixture, CIVFEMALE, DEFAULT, generic\n"
        "2, fixture, fixture, CIVMALE, DEFAULT, man\n"
        "3, fixture, fixture, SPECIAL, DEFAULT, woman\n"
        "4, fixture, fixture, CIVMALE, DEFAULT, busywoman\n"
        "5, fixture, fixture, SPECIAL, DEFAULT, manager\n"
        "6, fixture, fixture,  civfemale , DEFAULT, generic # comment\n"
        "7, fixture, fixture, CIVFEMALE, DEFAULT, generic\n"
        "7, fixture, fixture, CIVMALE, DEFAULT, man\n"
        "-1, fixture, fixture, CIVFEMALE, DEFAULT, woman\n"
        "65536, fixture, fixture, CIVFEMALE, DEFAULT, woman\n"
        "99999999999999999999999, fixture, fixture, CIVFEMALE, DEFAULT, woman\n"
        "8broken, fixture, fixture, CIVFEMALE, DEFAULT, woman\n"
        "9, incomplete\n"
        "10, bfyfixture, fixture, SPECIAL, DEFAULT, pro\n"
        "11, swfyfixture, fixture, CIVMALE, DEFAULT, pro\n"
        "12, wmyfixture, fixture, SPECIAL, DEFAULT, neutral\n"
        "13, customname, fixture, SPECIAL, DEFAULT, pro\n"
        "14, af_custom, fixture, SPECIAL, DEFAULT, neutral\nend\n");
    assert(table[1] == 2 && table[2] == 1 && table[3] == 2 && table[4] == 2);
    assert(table[5] == 0 && table[6] == 2 && table[7] == 1 && table[8] == 0 && table[9] == 0);
    assert(table[0] == 0 && table[65535] == 0);
    assert(table[10] == 2 && table[11] == 2 && table[12] == 1 && table[13] == 0 && table[14] == 0);
    std::puts("Model gender tests passed: stock stems/asset/animation priority, neutral fallback and malformed rows.");
}

void TestFingerprint() {
    using namespace NpcGameLayout;
    std::vector<unsigned char> player(PlayerCode, PlayerCode + sizeof(PlayerCode));
    std::vector<unsigned char> pool(PoolCode, PoolCode + sizeof(PoolCode));
    assert(MatchesInstalled(InstalledEntry, InstalledImageSize, player.data(), pool.data()));
    assert(!MatchesInstalled(0, InstalledImageSize, player.data(), pool.data()));
    assert(!MatchesInstalled(InstalledEntry, 0, player.data(), pool.data()));
    player[8] ^= 1;
    assert(!MatchesInstalled(InstalledEntry, InstalledImageSize, player.data(), pool.data()));
    player[8] ^= 1;
    pool[16] ^= 1;
    assert(!MatchesInstalled(InstalledEntry, InstalledImageSize, player.data(), pool.data()));
    pool[16] ^= 1;
    for (const uintptr_t runtimeBase : {uintptr_t{0xB00000}, uintptr_t{0x2000000}}) {
        WriteAddress(player.data() + 8, Resolve(runtimeBase, 0xC100BC));
        WriteAddress(player.data() + 20, Resolve(runtimeBase, 0xC100D0));
        WriteAddress(pool.data() + 1, Resolve(runtimeBase, 0xC018A8));
        assert(MatchesInstalled(InstalledEntry, InstalledImageSize, player.data(), pool.data(), runtimeBase));
        assert(!MatchesInstalled(InstalledEntry, InstalledImageSize, player.data(), pool.data()));
    }
    std::vector<unsigned char> standing(StandstillCode, StandstillCode + sizeof(StandstillCode));
    std::vector<unsigned char> taskType(StandstillTaskType, StandstillTaskType + sizeof(StandstillTaskType));
    assert(MatchesStandstill(standing.data(), taskType.data()));
    standing[14] ^= 1;
    assert(!MatchesStandstill(standing.data(), taskType.data()));
    standing[14] ^= 1;
    taskType[1] ^= 1;
    assert(!MatchesStandstill(standing.data(), taskType.data()));
    std::puts("Fingerprint checks passed (ASLR relocation; globals, pool stride and standstill task code).");
}

void VerifyInstalled(const char* path) {
    std::ifstream file(path, std::ios::binary);
    assert(file && "Game executable could not be read");
    std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    assert(bytes.size() >= sizeof(IMAGE_DOS_HEADER));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());
    assert(dos->e_magic == IMAGE_DOS_SIGNATURE && dos->e_lfanew > 0);
    assert(static_cast<size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS) <= bytes.size());
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(bytes.data() + dos->e_lfanew);
    assert(nt->Signature == IMAGE_NT_SIGNATURE && nt->FileHeader.Machine == IMAGE_FILE_MACHINE_I386);
    const auto* sections = IMAGE_FIRST_SECTION(nt);
    assert(reinterpret_cast<const unsigned char*>(sections + nt->FileHeader.NumberOfSections) <= bytes.data() + bytes.size());
    auto codeAt = [&](uintptr_t address, size_t length) -> const unsigned char* {
        const uintptr_t rva = address - nt->OptionalHeader.ImageBase;
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
            const auto& section = sections[i];
            if (rva >= section.VirtualAddress && rva + length <= section.VirtualAddress + section.SizeOfRawData) {
                const size_t offset = section.PointerToRawData + rva - section.VirtualAddress;
                assert(offset + length <= bytes.size());
                return bytes.data() + offset;
            }
        }
        assert(false && "Code fingerprint was not inside a file section");
        return nullptr;
    };
    using namespace NpcGameLayout;
    assert(MatchesInstalled(nt->OptionalHeader.AddressOfEntryPoint, nt->OptionalHeader.SizeOfImage,
        codeAt(PlayerCodeAddress, sizeof(PlayerCode)), codeAt(PoolCodeAddress, sizeof(PoolCode))));
    assert(MatchesStandstill(codeAt(StandstillProcessAddress, sizeof(StandstillCode)),
        codeAt(StandstillTaskTypeAddress, sizeof(StandstillTaskType))));
    std::puts("Installed executable matches player, pool and standstill-task fingerprints (read-only verification).");
    const std::string executable(path);
    const auto separator = executable.find_last_of('\\');
    if (separator != std::string::npos) {
        std::ifstream metadata(executable.substr(0, separator) + "\\data\\peds.ide", std::ios::binary);
        if (metadata) {
            const std::string text{std::istreambuf_iterator<char>(metadata), std::istreambuf_iterator<char>()};
            const auto genders = NpcPedMetadata::Parse(text);
            const auto male = std::count(genders.begin(), genders.end(), 1);
            const auto female = std::count(genders.begin(), genders.end(), 2);
            assert(male > 0 && female > 0);
            std::printf("Installed model metadata: %d male and %d female labels recognised (no asset data copied).\n", male, female);
        }
    }
}

void TestRepeatedAudio(const char* path) {
    std::ifstream input(path, std::ios::binary);
    assert(input && "Speech fixture could not be read");
    const std::string audio{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    assert(!audio.empty());
    wchar_t directory[MAX_PATH]{}, file[MAX_PATH]{};
    assert(GetTempPathW(MAX_PATH, directory) != 0);
    assert(GetTempFileNameW(directory, L"npc", 0, file) != 0);
    assert(NpcAudio::ReplaceReplyFile(file, audio));
    for (unsigned turn = 0; turn < 3; ++turn) {
        const std::wstring command = L"open \"" + std::wstring(file) + L"\" type mpegvideo alias npc_reply";
        assert(mciSendStringW(command.c_str(), nullptr, 0, nullptr) == 0);
        assert(mciSendStringW(L"play npc_reply", nullptr, 0, nullptr) == 0);
        wchar_t mode[32]{};
        assert(mciSendStringW(L"status npc_reply mode", mode, 32, nullptr) == 0);
        assert(wcscmp(mode, L"playing") == 0);
        assert(mciSendStringW(L"stop npc_reply", nullptr, 0, nullptr) == 0);
        HANDLE locked = CreateFileW(file, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (locked != INVALID_HANDLE_VALUE) CloseHandle(locked);
        assert(locked == INVALID_HANDLE_VALUE && "MCI file-lock reproduction failed");
        assert(NpcAudio::ReplaceReplyFile(file, audio));
    }
    mciSendStringW(L"close npc_reply", nullptr, 0, nullptr);
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    bool opened = false, replaced = false;
    unsigned long closeError = 0;
    std::thread owner([&]() {
        const std::wstring command = L"open \"" + std::wstring(file) + L"\" type mpegvideo alias npc_reply";
        opened = mciSendStringW(command.c_str(), nullptr, 0, nullptr) == 0;
        SetEvent(ready);
        WaitForSingleObject(release, 5000);
        mciSendStringW(L"close npc_reply", nullptr, 0, nullptr);
    });
    WaitForSingleObject(ready, 5000);
    std::thread nextTurn([&]() {
        closeError = mciSendStringW(L"close npc_reply", nullptr, 0, nullptr);
        replaced = NpcAudio::ReplaceReplyFile(file, audio);
    });
    nextTurn.join();
    SetEvent(release);
    owner.join();
    CloseHandle(ready); CloseHandle(release);
    assert(opened && closeError != 0 && !replaced);
    std::puts("MCI thread-local alias failure reproduced; replies must use a stable audio-owner thread.");
    assert(DeleteFileW(file));
    std::puts("Three successive x86 MCI replies passed; stopped-but-open MP3 lock reproduced and released.");
}

int main(int argc, char** argv) {
    TestInteraction();
    TestModelMetadata();
    TestFingerprint();
    if (argc > 1 && argv[1][0] != '-') VerifyInstalled(argv[1]);
    if (argc > 2) TestRepeatedAudio(argv[2]);
    return 0;
}
