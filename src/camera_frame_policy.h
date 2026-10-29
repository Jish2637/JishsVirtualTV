#pragma once
#include "tv_runtime.h"
#include <cmath>
namespace virtualtv {
template<class Sampler> bool latch_presentation_camera(Scene& scene,std::uint64_t now,Sampler sample){
    if(!scene.at||now<scene.at||now-scene.at>250)return false;
    Camera current;if(!sample(current)||!current.valid||!current.sampled_at)return false;
    scene.camera=current;scene.at=current.sampled_at;return true;
}
// Two independently sampled poses must agree before final-render constants can
// replace the latched presentation camera. Recency alone is not sufficient.
inline bool camera_pair_matches(const Camera& recorded,const Camera& present,std::uint64_t now){
    if(!recorded.valid||!present.valid||!recorded.sampled_at||recorded.viewport!=present.viewport||now<recorded.sampled_at||now-recorded.sampled_at>50)return false;
    float square{};for(unsigned i=12;i<15;++i){float d=recorded.world[i]-present.world[i];square+=d*d;}if(!std::isfinite(square)||square>.000625f)return false;
    for(unsigned i=0;i<3;++i){float dot{};for(unsigned j=0;j<3;++j)dot+=recorded.world[i*4+j]*present.world[i*4+j];if(!std::isfinite(dot)||dot<.9999f)return false;}
    return std::abs(recorded.vertical_fov-present.vertical_fov)<=.05f;
}
}
