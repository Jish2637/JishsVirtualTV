#include "tv_geometry.h"
#include <cassert>
#include <cmath>
using namespace virtualtv;
static bool eq(float a,float b){return std::abs(a-b)<0.0002f;}
int main(){
  Placement p{};Screen s{};s.width=2;s.height=2;
  auto c=corners(p,s);assert(eq(c.positions[0].x,-1)&&eq(c.positions[0].y,1));
  p.position={5,6,7};p.scale=2;c=corners(p,s);assert(eq(c.positions[0].x,3)&&eq(c.positions[0].y,8)&&eq(c.positions[0].z,7));
  p.position={};p.scale=1;p.rotation={0,0,0.707106781f,0.707106781f};c=corners(p,s);
  assert(eq(c.positions[0].x,-1)&&eq(c.positions[0].y,-1));
  s.width=-1;assert(!valid(p,s));
  s.width=2;p={};
  std::vector<ScreenMeshVertex> mesh;
  append_screen_mesh(s,mesh);assert(mesh.size()==6);
  assert(eq(mesh[0].position.x,-1)&&eq(mesh[0].position.y,1));
  s.horizontal_curve_deg=100;s.vertical_curve_deg=60;
  mesh.clear();append_screen_mesh(s,mesh);
  assert(mesh.size()>6&&mesh.size()<=max_curve_vertices_per_screen);
  assert(eq(mesh[0].u,0)&&eq(mesh[0].v,0));
  assert(mesh[0].position.z>0.01f);
  // Opposite bends reverse depth without reversing the screen's UV coordinates.
  auto z=mesh[0].position.z;s.horizontal_curve_deg=-100;s.vertical_curve_deg=-60;
  mesh.clear();append_screen_mesh(s,mesh);assert(eq(mesh[0].position.z,-z));
  s.horizontal_curve_deg=161;assert(!valid(p,s));
}
