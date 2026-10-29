#include "tv_geometry.h"
#include <cmath>
#include <algorithm>
namespace virtualtv {
namespace {
Vec3 add(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
Vec3 mul(Vec3 a,float s){return {a.x*s,a.y*s,a.z*s};}
float norm(Quat q){return std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);}
}
Vec3 rotate(Quat q,Vec3 v){
  const float n=norm(q); if(n<0.00001f)return v;
  q.x/=n;q.y/=n;q.z/=n;q.w/=n;
  Vec3 t{2*(q.y*v.z-q.z*v.y),2*(q.z*v.x-q.x*v.z),2*(q.x*v.y-q.y*v.x)};
  return {v.x+q.w*t.x+q.y*t.z-q.z*t.y,v.y+q.w*t.y+q.z*t.x-q.x*t.z,v.z+q.w*t.z+q.x*t.y-q.y*t.x};
}
Corners corners(const Placement& p,const Screen& s){
  const float w=s.width*.5f,h=s.height*.5f;
  const std::array<Vec3,4> local{{{-w,h,0},{w,h,0},{w,-h,0},{-w,-h,0}}};
  Corners result{};
  for(std::size_t i=0;i<4;++i) {
    Vec3 at=add(s.offset,rotate(s.rotation,local[i]));
    result.positions[i]=add(p.position,rotate(p.rotation,mul(at,p.scale)));
  }
  return result;
}
void append_screen_mesh(const Screen& s,std::vector<ScreenMeshVertex>& out){
  // Preserve the original 6-vertex quad exactly for all existing flat screens.
  const unsigned columns=s.horizontal_curve_deg==0?1u:std::min(max_curve_segments_x,
      static_cast<unsigned>(std::max(1.f,std::ceil(std::abs(s.horizontal_curve_deg)/7.f))));
  const unsigned rows=s.vertical_curve_deg==0?1u:std::min(max_curve_segments_y,
      static_cast<unsigned>(std::max(1.f,std::ceil(std::abs(s.vertical_curve_deg)/7.f))));
  const std::size_t start=out.size();
  out.reserve(start+6u*columns*rows);
  constexpr float radians=3.14159265358979323846f/180.f;
  const float horizontal=s.horizontal_curve_deg*radians;
  const float vertical=s.vertical_curve_deg*radians;
  const auto point=[&](float u,float v)->ScreenMeshVertex {
    const float horizontal_t=(u-.5f)*horizontal;
    const float vertical_t=(.5f-v)*vertical;
    const float x=horizontal==0?s.width*(u-.5f):s.width*std::sin(horizontal_t)/horizontal;
    const float y=vertical==0?s.height*(.5f-v):s.height*std::sin(vertical_t)/vertical;
    const float z=(horizontal==0?0.f:s.width*(1.f-std::cos(horizontal_t))/horizontal)
                 +(vertical==0?0.f:s.height*(1.f-std::cos(vertical_t))/vertical);
    const auto rotated=rotate(s.rotation,{x,y,z});
    return {add(s.offset,rotated),u,v};
  };
  for(unsigned row=0;row<rows;++row){
    const float top=static_cast<float>(row)/rows,bottom=static_cast<float>(row+1)/rows;
    for(unsigned col=0;col<columns;++col){
      const float left=static_cast<float>(col)/columns,right=static_cast<float>(col+1)/columns;
      const auto a=point(left,top),b=point(right,top),c=point(right,bottom),d=point(left,bottom);
      out.insert(out.end(),{a,b,c,a,c,d});
    }
  }
}
bool valid(const Placement& p,const Screen& s){
  const auto finite=[](float x){return std::isfinite(x);};
  for(float x:{p.position.x,p.position.y,p.position.z,p.rotation.x,p.rotation.y,p.rotation.z,p.rotation.w,p.scale,
               s.offset.x,s.offset.y,s.offset.z,s.rotation.x,s.rotation.y,s.rotation.z,s.rotation.w,s.width,s.height,s.horizontal_curve_deg,s.vertical_curve_deg})if(!finite(x))return false;
  return p.scale>0 && s.width>0 && s.height>0 &&
      std::abs(s.horizontal_curve_deg)<=160.f && std::abs(s.vertical_curve_deg)<=120.f &&
      norm(p.rotation)>.00001f && norm(s.rotation)>.00001f;
}
}
