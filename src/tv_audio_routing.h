#pragma once
#include "tv_geometry.h"
#include "speaker_mix.h"
#include <array>
#include <vector>
namespace virtualtv {
// Sound sources are independent of world-screen visibility. A loaded URL can
// play through speaker-only setups, and videos are decoded even with no TV.
// When no spatial listener or emitter exists, use ordinary VLC stereo rather
// than writing zero gain and making startup audio require a slider nudge.
template<class MatrixFn>
std::array<float,4> route_tv_audio(bool directional,bool front_preview,bool listener_valid,
                                  Vec3 ear,Vec3 right,const std::vector<Vec3>& emitters,
                                  float range,MatrixFn matrix_for,bool require_bound_source=false) {
    if(require_bound_source&&(!listener_valid||emitters.empty()))return {};
    constexpr std::array<float,4> normal_stereo{1.f,0.f,0.f,1.f};
    if(!directional || front_preview || !listener_valid || emitters.empty()) return normal_stereo;
    SpeakerMix mix;
    for(const auto& emitter:emitters)mix.add(matrix_for(ear,right,emitter,range));
    return mix.result();
}
}
