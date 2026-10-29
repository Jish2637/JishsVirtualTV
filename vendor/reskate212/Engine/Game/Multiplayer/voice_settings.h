#pragma once
#include "Engine/Game/Input/controller_bindings.h"
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace dingosdk {
inline constexpr float max_voice_volume = 10.f;
// A listener's hearing distance, and the host's voice range: how far the host
// forwards proximity voice at all (a host setting, default 300 m).
inline constexpr float min_hearing_distance = 5.f, max_hearing_distance = 1000.f;
inline constexpr float min_voice_range = 50.f, max_voice_range = 1000.f, default_voice_range = 300.f;
inline bool valid_voice_range(float value) noexcept {
    return std::isfinite(value) && value >= min_voice_range && value <= max_voice_range;
}
inline bool valid_voice_volume(float value) noexcept {
    return std::isfinite(value) && value >= 0.f && value <= max_voice_volume;
}
struct VoiceSettings {
    bool enabled{}, proximity{true}, open_mic{};
    int push_to_talk = 'V';
    std::uint32_t controller_combo{};
    float distance = 35.f, volume = 1.f, microphone = 1.f;
    bool operator==(const VoiceSettings &) const = default;
    bool valid() const noexcept {
        return push_to_talk >= 0 && push_to_talk < 256 && valid_controller_combo(controller_combo) &&
            std::isfinite(distance) && distance >= min_hearing_distance && distance <= max_hearing_distance &&
            valid_voice_volume(volume) && valid_voice_volume(microphone);
    }
};
struct VoicePlayer {
    std::uint64_t id{};
    bool muted{}, speaking{};
    float volume = 1.f;
};
struct VoiceModel {
    VoiceSettings settings;
    bool ready{}, transmitting{};
    bool allowed = true;
    std::string status = "Voice chat is off.";
    std::vector<VoicePlayer> players;
};
}
