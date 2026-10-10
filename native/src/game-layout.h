#pragma once
#include "interaction.h"

namespace NpcGameLayout {
constexpr uint32_t InstalledEntry = 0x458DB8;
constexpr uint32_t InstalledImageSize = 0x985000;
constexpr uintptr_t PlayerCodeAddress = 0x560C95;
constexpr uintptr_t PoolCodeAddress = 0x40A226;
constexpr unsigned char PlayerCode[] = {
    0x08,0x85,0xC0,0x79,0x07,0x0F,0xB6,0x05,0xBC,0x00,0xC1,0x00,
    0x69,0xC0,0x90,0x01,0x00,0x00,0x8B,0x80,0xD0,0x00,0xC1,0x00
};
constexpr unsigned char PoolCode[] = {
    0xA1,0xA8,0x18,0xC0,0x00,0x8B,0x78,0x08,0x85,0xFF,0x74,0x62,
    0x8B,0xDF,0x69,0xDB,0xC4,0x07,0x00,0x00
};
inline uintptr_t Resolve(uintptr_t runtimeBase, uintptr_t preferredAddress) {
    return runtimeBase + preferredAddress - 0x400000;
}
inline void WriteAddress(unsigned char* bytes, uintptr_t address) {
    for (unsigned i = 0; i < 4; ++i) bytes[i] = static_cast<unsigned char>(address >> (i * 8));
}
inline bool MatchesInstalled(uint32_t entry, uint32_t imageSize,
    const unsigned char* playerCode, const unsigned char* poolCode, uintptr_t runtimeBase = 0x400000) {
    unsigned char expectedPlayer[sizeof(PlayerCode)]{}, expectedPool[sizeof(PoolCode)]{};
    for (size_t i = 0; i < sizeof(PlayerCode); ++i) expectedPlayer[i] = PlayerCode[i];
    for (size_t i = 0; i < sizeof(PoolCode); ++i) expectedPool[i] = PoolCode[i];
    WriteAddress(expectedPlayer + 8, Resolve(runtimeBase, 0xC100BC));
    WriteAddress(expectedPlayer + 20, Resolve(runtimeBase, 0xC100D0));
    WriteAddress(expectedPool + 1, Resolve(runtimeBase, 0xC018A8));
    return entry == InstalledEntry && imageSize == InstalledImageSize &&
        NpcInteraction::MatchBytes(playerCode, expectedPlayer, sizeof(PlayerCode)) &&
        NpcInteraction::MatchBytes(poolCode, expectedPool, sizeof(PoolCode));
}
}
