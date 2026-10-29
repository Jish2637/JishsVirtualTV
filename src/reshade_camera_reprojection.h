#pragma once
#include <array>
#include <cmath>
namespace virtualtv::depth::reprojection {
struct Uv {float x{},y{},depth{};bool visible{};};
// Row-major camera-to-clip transform matching mul(viewPos, gameProjection)
// in the D3D12 TV shader. Camera world rows are right/up/back/translation.
inline Uv project(const std::array<float,16>& world_camera,
                  const std::array<float,16>& projection,
                  const std::array<float,3>& world) noexcept {
    const float d[3]{world[0]-world_camera[12],world[1]-world_camera[13],world[2]-world_camera[14]};
    const float v[4]{d[0]*world_camera[0]+d[1]*world_camera[1]+d[2]*world_camera[2],
                     d[0]*world_camera[4]+d[1]*world_camera[5]+d[2]*world_camera[6],
                    -(d[0]*world_camera[8]+d[1]*world_camera[9]+d[2]*world_camera[10]),1};
    float clip[4]{};
    for(int i=0;i<4;++i)for(int j=0;j<4;++j)clip[i]+=v[j]*projection[j*4+i];
    if(!std::isfinite(clip[3])||clip[3]<=0.00001f)return {};
    const auto x=0.5f+0.5f*clip[0]/clip[3];
    const auto y=0.5f-0.5f*clip[1]/clip[3];
    const auto z=clip[2]/clip[3];
    return {x,y,z,std::isfinite(x)&&std::isfinite(y)&&std::isfinite(z)&&x>=0&&x<=1&&y>=0&&y<=1};
}
}
