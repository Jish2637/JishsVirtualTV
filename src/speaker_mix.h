#pragma once
#include <array>
#include <algorithm>
#include <cmath>
namespace virtualtv {
// Combines channel gains from world-space speaker emitters. Audio itself is
// rendered only once by the sole VLC instance; no per-speaker buffering,
// duplicate playback, time-shifting or playback-rate manipulation.
struct SpeakerMix {
    float left{},right{};
    void add(const std::array<float,4>& channel_matrix) noexcept {
        const float l=channel_matrix[0]+channel_matrix[1];
        const float r=channel_matrix[2]+channel_matrix[3];
        if(std::isfinite(l)&&l>0)left+=l;
        if(std::isfinite(r)&&r>0)right+=r;
    }
    std::array<float,4> result()const noexcept {
        return {std::clamp(left,0.f,1.f),0.f,0.f,std::clamp(right,0.f,1.f)};
    }
};
}
