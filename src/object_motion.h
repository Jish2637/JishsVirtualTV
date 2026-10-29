#pragma once
#include "tv_geometry.h"
#include <cmath>
#include <algorithm>
namespace virtualtv {
// Tracks real placement edits, not tiny sampling fluctuations. Only the
// translation/rotation/scale read from the placed object's transform is used.
struct ObjectMotion {
    unsigned changed_ticks{};
    bool following{};
    unsigned quiet_ticks{};
    void reset() noexcept {changed_ticks=quiet_ticks=0;following=false;}
    static float movement(Vec3 a,Vec3 b) noexcept {
        const auto x=a.x-b.x,y=a.y-b.y,z=a.z-b.z;
        return std::sqrt(x*x+y*y+z*z);
    }
    static bool differs(const Placement& a,Vec3 as,const Placement& b,Vec3 bs,float min_move) noexcept {
        const float dot=std::abs(a.rotation.x*b.rotation.x+a.rotation.y*b.rotation.y+
                                 a.rotation.z*b.rotation.z+a.rotation.w*b.rotation.w);
        const float scale_delta=std::max({std::abs(as.x-bs.x),std::abs(as.y-bs.y),std::abs(as.z-bs.z)});
        return movement(a.position,b.position)>min_move || dot<0.99998f || scale_delta>0.003f;
    }
    // Once motion is confirmed by consecutive tick samples, follow the exact
    // transform, not an interpolated camera/player-based estimate.
    bool apply(Placement& anchor,Vec3& scale,const Placement& observed,Vec3 observed_scale) noexcept {
        if(!differs(anchor,scale,observed,observed_scale,following?0.002f:0.012f)){
            changed_ticks=0;
            if(following&&++quiet_ticks>=18){following=false;quiet_ticks=0;}
            return false;
        }
        quiet_ticks=0;
        if(!following && ++changed_ticks<3)return false;
        following=true;changed_ticks=0;
        anchor=observed;scale=observed_scale;
        return true;
    }
};
}
