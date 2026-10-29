#include "tv_audio_routing.h"
#include <cassert>
#include <iostream>
#include <vector>
using namespace virtualtv;
int main(){
    auto stereo=[](Vec3,Vec3,Vec3 pos,float){
        if(pos.x>0)return std::array<float,4>{0,0,0.8f,0};
        return std::array<float,4>{0.7f,0,0,0};
    };
    const Vec3 ear{},right{1,0,0};
    const std::vector<Vec3> speakers{{1,0,0},{-1,0,0}};
    constexpr std::array<float,4> unity{1,0,0,1};
    // Speaker-only case: no primary screen exists; both speakers audible.
    const auto mixed=route_tv_audio(true,false,true,ear,right,speakers,35,stereo);
    assert(mixed[0]>.69f&&mixed[3]>.79f);
    // Hiding a screen is not an audio routing input; sound stays active.
    assert(route_tv_audio(true,false,true,ear,right,speakers,35,stereo)==mixed);
    // Non-directional, preview and uninitialised listener: normal VLC stereo.
    assert(route_tv_audio(false,false,true,ear,right,speakers,35,stereo)==unity);
    assert(route_tv_audio(true,true,true,ear,right,speakers,35,stereo)==unity);
    assert(route_tv_audio(true,false,false,ear,right,speakers,35,stereo)==unity);
    // No mounted outputs: keep decoding and ordinary audio available.
    assert(route_tv_audio(true,false,true,ear,right,{},35,stereo)==unity);
    constexpr std::array<float,4> silent{};
    // Borrowed outputs never fall back to camera/global stereo while unmatched.
    assert(route_tv_audio(true,false,true,ear,right,{},35,stereo,true)==silent);
    assert(route_tv_audio(false,false,true,ear,right,{},35,stereo,true)==silent);
    assert(route_tv_audio(true,false,false,ear,right,speakers,35,stereo,true)==silent);
    assert(route_tv_audio(true,false,true,ear,right,speakers,35,stereo,true)==mixed);
    std::cout<<"Speaker-only, hidden-screen audio, fallback stereo: passed\n";
}
