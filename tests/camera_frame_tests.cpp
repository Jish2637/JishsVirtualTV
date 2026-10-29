#include "camera_frame_policy.h"
#include "reshade_camera_reprojection.h"
#include "tv_geometry.h"
#include <cassert>
#include <iostream>
using namespace virtualtv;
int main(){
 Camera a;a.valid=true;a.sampled_at=100;a.sequence=1;a.source=CameraSource::Recording;a.vertical_fov=90;a.world={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};auto b=a;b.source=CameraSource::Presentation;b.sequence=2;
 assert(camera_pair_matches(a,b,120));assert(!camera_pair_matches(a,b,151));b.viewport=1;assert(!camera_pair_matches(a,b,120));b=a;b.world[12]=.03f;assert(!camera_pair_matches(a,b,120));b=a;b.world[0]=0;b.world[2]=1;assert(!camera_pair_matches(a,b,120));b=a;b.vertical_fov=91;assert(!camera_pair_matches(a,b,120));b=a;a.valid=false;assert(!camera_pair_matches(a,b,120));a=b;
 const std::array<float,16> projection{1,0,0,0,0,1,0,0,0,0,0,-1,0,0,.0603f,0};const std::array<float,3> point{0,0,10};
 const auto capture=depth::reprojection::project(a.world,projection,point);
 Placement object;object.position={0,0,10};Screen screen;auto before=corners(object,screen);
 for(unsigned hz:{30,60,144})for(unsigned frame=0;frame<hz;++frame){auto present=a;present.sequence=frame+2;present.sampled_at=100+frame*1000/hz;present.world[12]=float(frame)/hz;
     Scene native;native.at=present.sampled_at;native.camera=a;native.placement=object;native.screen=screen;unsigned reads{};
     assert(latch_presentation_camera(native,present.sampled_at,[&](Camera& output){++reads;output=present;return true;}));assert(reads==1&&native.camera.sequence==present.sequence);
     for(unsigned screen_index=0;screen_index<64;++screen_index){const auto reused=native.camera;assert(reused.sequence==present.sequence&&reads==1);}
     auto visible=depth::reprojection::project(present.world,projection,point);assert(visible.visible);auto immutable_capture=depth::reprojection::project(a.world,projection,point);assert(immutable_capture.x==capture.x&&immutable_capture.depth==capture.depth);
     auto after=corners(object,screen);for(unsigned i=0;i<4;++i)assert(after.positions[i].x==before.positions[i].x&&after.positions[i].y==before.positions[i].y&&after.positions[i].z==before.positions[i].z);
 }
 Scene stale;stale.at=1;unsigned reads{};assert(!latch_presentation_camera(stale,300,[&](Camera&){++reads;return true;})&&reads==0);
 std::cout<<"Camera frames: pose/viewport/age/FOV validation, 30/60/144Hz presentation, immutable depth reprojection and fixed object geometry passed\n";
}
