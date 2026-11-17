#pragma once
#include <array>
#include <string>

namespace dingosdk {
inline constexpr std::array<const char*, 3> graphics_keys{"film_grain", "vignette", "chromatic_aberration"};
struct GraphicsControls {
    // -1 leaves the authored setting alone; 0 disables; 1 enables.
    std::array<int, 3> effects{-1, -1, -1};
    bool operator==(const GraphicsControls&) const = default;
};
inline bool valid_graphics_controls(const GraphicsControls& c) {
    for (const auto v : c.effects) if (v < -1 || v > 1) return false;
    return true;
}
struct GraphicsControlsModel {
    GraphicsControls choices;
    bool available{};
    std::array<bool, 3> ready{}, enabled{};
    unsigned filmic_components{};
    std::string status;
};
}
