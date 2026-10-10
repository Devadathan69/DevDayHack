#pragma once
#include <cmath>
#include <cstdint>
#include <cstddef>

namespace NpcInteraction {
constexpr float StartRadius = 3.5f;
constexpr float LeaveRadius = 5.0f;
struct Ped {
    uintptr_t address = 0;
    uint32_t handle = 0;
    float x = 0, y = 0, z = 0;
    float health = 0;
    bool inVehicle = false;
    unsigned char area = 0;
};
inline bool CanTalk(const Ped& player, const Ped& npc, float radius) {
    if (!player.address || !npc.address || player.address == npc.address ||
        !std::isfinite(player.health) || !std::isfinite(npc.health) ||
        player.health <= 0 || npc.health <= 0 || player.inVehicle || npc.inVehicle ||
        player.area != npc.area) return false;
    const float dx = npc.x - player.x, dy = npc.y - player.y, dz = npc.z - player.z;
    return std::isfinite(dx) && std::isfinite(dy) && std::isfinite(dz) &&
        std::fabs(dz) <= 2.0f && dx * dx + dy * dy + dz * dz <= radius * radius;
}
inline float DistanceSquared(const Ped& a, const Ped& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}
inline bool SamePed(const Ped& a, const Ped& b) {
    return a.address != 0 && a.address == b.address && a.handle == b.handle;
}
inline uint32_t ScopeGeneration(uint32_t sceneEpoch, uint32_t pedHandle) {
    return (sceneEpoch << 17) | (pedHandle & 0x1FFFFu);
}
inline bool IsConversation(unsigned state) {
    return state >= 2 && state <= 7;
}
inline bool DeadlineReached(uint32_t now, uint32_t deadline) {
    return static_cast<int32_t>(now - deadline) >= 0;
}
inline bool MatchBytes(const unsigned char* data, const unsigned char* expected, size_t size) {
    for (size_t i = 0; i < size; ++i) if (data[i] != expected[i]) return false;
    return true;
}
}
