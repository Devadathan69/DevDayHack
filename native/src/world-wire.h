#pragma once
#include "world-context.h"
#include "interaction.h"
#include <string>
#include <cmath>

namespace NpcWorldWire {
using Escape = std::string (*)(const std::string&);
inline std::string Serialize(const NpcWorld::Snapshot& world, const NpcInteraction::Ped& player,
    uint64_t capturedAt, uint64_t now, Escape escape) {
    if (!escape || !world.available || !capturedAt || now < capturedAt || now - capturedAt > 10000 ||
        !std::isfinite(player.x) || !std::isfinite(player.y) || !std::isfinite(player.z) ||
        std::fabs(player.x) > 20000 || std::fabs(player.y) > 20000 || std::fabs(player.z) > 20000) return "null";
    const bool location = !world.location.empty() && !world.zoneCode.empty();
    const bool time = world.hour >= 0 && world.hour <= 23 && world.minute >= 0 && world.minute <= 59;
    const bool weather = world.weatherId >= 0 && world.weatherId <= 22 && !world.weather.empty() &&
        std::isfinite(world.rainIntensity) && world.rainIntensity >= 0 && world.rainIntensity <= 1;
    if (!location && !time && !weather) return "null";
    std::string result = "{\"capturedAt\":" + std::to_string(capturedAt) + ",\"location\":";
    if (location) result += "{\"name\":\"" + escape(world.location) + "\",\"zoneCode\":\"" + escape(world.zoneCode) +
        "\",\"position\":{\"x\":" + std::to_string(player.x) + ",\"y\":" + std::to_string(player.y) +
        ",\"z\":" + std::to_string(player.z) + ",\"area\":" + std::to_string(player.area) + "}}";
    else result += "null";
    result += ",\"time\":";
    if (time) result += "{\"hour\":" + std::to_string(world.hour) + ",\"minute\":" + std::to_string(world.minute) + "}";
    else result += "null";
    result += ",\"weather\":";
    if (weather) {
        result += "{\"id\":" + std::to_string(world.weatherId) + ",\"condition\":\"" + escape(world.weather) +
            "\",\"rainIntensity\":" + std::to_string(world.rainIntensity);
        if (world.targetWeatherId >= 0 && world.targetWeatherId <= 22)
            result += ",\"targetId\":" + std::to_string(world.targetWeatherId);
        result += "}";
    } else result += "null";
    return result + "}";
}
}
