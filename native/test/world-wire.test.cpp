#include "..\src\world-wire.h"
#include <cassert>
#include <cstdio>
#include <limits>

std::string Escape(const std::string& text) {
    std::string result;
    for (char c : text) {
        if (c == '"' || c == '\\') result += '\\';
        result += c;
    }
    return result;
}
int main() {
    NpcWorld::Snapshot world;
    NpcInteraction::Ped player;
    player.x = 2495; player.y = -1687; player.z = 13;
    assert(NpcWorldWire::Serialize(world, player, 1000, 1001, Escape) == "null");
    world.available = true; world.location = "Ganton"; world.zoneCode = "GAN1";
    world.hour = 0; world.minute = 0; world.weatherId = 1; world.targetWeatherId = 8;
    world.weather = "transitioning"; world.rainIntensity = 0;
    auto json = NpcWorldWire::Serialize(world, player, 1000, 1001, Escape);
    assert(json.find("\"name\":\"Ganton\"") != std::string::npos);
    assert(json.find("\"hour\":0,\"minute\":0") != std::string::npos);
    assert(json.find("\"targetId\":8") != std::string::npos);
    assert(json.find("\"condition\":\"transitioning\"") != std::string::npos);
    assert(json.find("\"capturedAt\":1000") != std::string::npos);
    assert(NpcWorldWire::Serialize(world, player, 1000, 999, Escape) == "null");
    assert(NpcWorldWire::Serialize(world, player, 1000, 11001, Escape) == "null");
    assert(NpcWorldWire::Serialize(world, player, 0, 1000, Escape) == "null");
    player.x = std::numeric_limits<float>::quiet_NaN();
    assert(NpcWorldWire::Serialize(world, player, 1000, 1001, Escape) == "null");
    player.x = 20001;
    assert(NpcWorldWire::Serialize(world, player, 1000, 1001, Escape) == "null");
    player.x = 2495;
    world.location = "Ganton \"corner\"";
    json = NpcWorldWire::Serialize(world, player, 1000, 1001, Escape);
    assert(json.find("Ganton \\\"corner\\\"") != std::string::npos);
    world.hour = -1; world.minute = -1; world.weatherId = -1; world.rainIntensity = -1;
    json = NpcWorldWire::Serialize(world, player, 1000, 1001, Escape);
    assert(json.find("\"time\":null") != std::string::npos && json.find("\"weather\":null") != std::string::npos);
    assert(json.find("\"hour\":-1") == std::string::npos);
    assert(json.find("\"id\":-1") == std::string::npos);
    assert(json.find("\"rainIntensity\":-1") == std::string::npos);
    world.location.clear(); world.zoneCode.clear();
    assert(NpcWorldWire::Serialize(world, player, 1000, 1001, Escape) == "null");
    std::puts("World wire checks passed: real snapshot fields, clock midnight, transitions, freshness, escaping, missing signals and safe positions.");
    return 0;
}
