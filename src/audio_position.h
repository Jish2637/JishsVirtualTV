#pragma once
#include "tv_geometry.h"
namespace virtualtv {
inline Vec3 audio_emitter(const Placement& object,Vec3 scale,const Screen& screen){
    const auto o=screen.offset;
    const auto offset=rotate(object.rotation,{o.x*scale.x,o.y*scale.y,o.z*scale.z});
    return {object.position.x+offset.x,object.position.y+offset.y,object.position.z+offset.z};
}
}
