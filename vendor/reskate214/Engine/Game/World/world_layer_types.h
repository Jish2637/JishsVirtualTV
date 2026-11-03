#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace dingosdk {
enum class WorldMap { none, bam, grom, ftue, stadium_1, stadium_2, mpr };
inline std::string_view world_map_key(WorldMap map) {
    switch (map) {
    case WorldMap::bam: return "bam";
    case WorldMap::grom: return "grom";
    case WorldMap::ftue: return "ftue";
    case WorldMap::stadium_1: return "stadium_1";
    case WorldMap::stadium_2: return "stadium_2";
    case WorldMap::mpr: return "mpr";
    default: return {};
    }
}
inline const char* world_map_label(WorldMap map) {
    switch (map) {
    case WorldMap::bam: return "BAM";
    case WorldMap::grom: return "Isle of Grom";
    case WorldMap::ftue: return "FTUE Island";
    case WorldMap::stadium_1: return "Stadium 1";
    case WorldMap::stadium_2: return "Stadium 2";
    case WorldMap::mpr: return "MPR";
    default: return "No supported map loaded";
    }
}
// One SubWorldReference that the player can switch, found in the level data.
// Parents precede their children; a root's parent is -1.
struct WorldLayerNode { std::string bundle; WorldMap map; int parent; bool autoload; };
struct WorldLayer {
    std::string key, label, detail;
    WorldMap map;
    unsigned leaf, switch_slot;
    std::string category;
};
struct WorldLayerCatalog {
    std::vector<WorldLayerNode> nodes;
    std::vector<unsigned> anchors; // one root node per map that identifies it
    std::vector<WorldLayer> layers;
};
}
