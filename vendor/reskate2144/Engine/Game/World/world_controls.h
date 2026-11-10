#pragma once
#include "atmosphere_controls.h"
#include <array>
#include <cmath>
#include <string>
#include <string_view>

namespace dingosdk {
// Negative values mean authored defaults. Multipliers are relative to each
// environment's own values, so day/night and quality presets retain their look.
struct WorldControls {
    int traffic{-1}, pedestrians{-1}; // -1 default, 0 off, 1 light, 2 normal, 3 busy
    int fog{-1};                     // -1 default, 0 off, 1 on
    float fog_distance{-1}, sky_brightness{-1}, clouds{-1};
    float wind_strength{-1}, wind_direction{-1};
    AtmosphereChoices atmosphere;
    bool operator==(const WorldControls&) const = default;
};
inline constexpr std::array<const char*, 5> population_labels{"Default", "Off", "Light", "Normal", "Busy"};
inline bool valid_world_controls(const WorldControls& c) {
    const auto scalar = [](float v, float min, float max) {
        return std::isfinite(v) && (v == -1 || (v >= min && v <= max));
    };
    return valid_atmosphere_choices(c.atmosphere) && c.traffic >= -1 && c.traffic <= 3 && c.pedestrians >= -1 && c.pedestrians <= 3 &&
        c.fog >= -1 && c.fog <= 1 && scalar(c.fog_distance, .1f, 5) &&
        scalar(c.sky_brightness, 0, 3) && scalar(c.clouds, 0, 2) &&
        scalar(c.wind_strength, 0, 30) && scalar(c.wind_direction, 0, 360);
}
struct WorldControlsModel {
    WorldControls choices;
    bool environment_available{}, population_available{};
    std::array<unsigned, 3> environments{}; // fog, sky, wind
    std::array<unsigned, 2> population{};
    std::array<bool, 2> population_ready{};
    std::array<AtmosphereReading, atmosphere_controls.size()> atmosphere;
    std::array<std::vector<std::string>, atmosphere_controls.size()> textures;
    std::string status;
};
}
