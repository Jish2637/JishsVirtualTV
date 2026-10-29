#include "tv_geometry.h"
#include "audio_position.h"
#include "speaker_mix.h"
#include <cassert>
#include <cmath>
#include <limits>
using namespace virtualtv;
int main(){
    // Adding another screen creates distinct world coordinates; it must not
    // relocate the main screen or change its media UVs.
    Placement first{};first.position={1,2,3};
    Placement second{};second.position={4,5,6};
    Screen s{};s.width=2;s.height=1;
    const auto a=corners(first,s),b=corners(second,s);
    assert(a.positions[0].x!=b.positions[0].x);
    assert(std::abs(a.positions[0].x-0.f)<1.e-5f);
    assert(std::abs(b.positions[0].x-3.f)<1.e-5f);
    // Speaker emission point is object anchored (including saved local offset).
    s.offset={0,1,0};
    const auto at=audio_emitter(second,{1,1,1},s);
    assert(std::abs(at.y-6.f)<1.e-5f);
    SpeakerMix mix;mix.add({0.25f,0.f,0.f,0.5f});mix.add({0.5f,0.f,0.f,0.25f});
    auto channels=mix.result();assert(std::abs(channels[0]-0.75f)<1.e-5f&&std::abs(channels[3]-0.75f)<1.e-5f);
    mix.add({1,0,1,0});channels=mix.result();assert(channels[0]==1.f&&channels[3]==1.f);
    SpeakerMix invalid;invalid.add({std::numeric_limits<float>::quiet_NaN(),0,0,0});
    assert(invalid.result()[0]==0.f);
}
