#pragma once
#include <vector>
namespace virtualtv {
// Select a verified available screen for the GPU submission. A restored
// secondary TV is drawable even when the original primary is absent.
// This must not change any persisted attachment ownership/role.
template<class SceneType>
bool choose_presentation_screen(SceneType& primary,const std::vector<SceneType>& candidates){
    if(primary.attached)return true;
    for(const auto& candidate:candidates){
        if(candidate.attached){primary=candidate;return true;}
    }
    return false;
}
}
