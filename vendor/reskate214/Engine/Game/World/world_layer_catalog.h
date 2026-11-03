#pragma once
// The world-layer catalog is read from the installed level data at startup
// (Engine/Vfs/world_layer_scan.h) and installed once, before anything reads it.
#include "world_layer_types.h"
#include <utility>

namespace dingosdk {
inline WorldLayerCatalog& world_layer_catalog_storage() {
    static WorldLayerCatalog value;
    return value;
}
inline const WorldLayerCatalog& world_layer_catalog() { return world_layer_catalog_storage(); }
inline void install_world_layer_catalog(WorldLayerCatalog catalog) { world_layer_catalog_storage() = std::move(catalog); }
inline const std::vector<WorldLayerNode>& world_layer_nodes() { return world_layer_catalog().nodes; }
inline const std::vector<unsigned>& world_map_anchors() { return world_layer_catalog().anchors; }
inline const std::vector<WorldLayer>& world_layers() { return world_layer_catalog().layers; }
}
