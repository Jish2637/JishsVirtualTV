#include "object_motion.h"
#include <cassert>
#include <cmath>
using namespace virtualtv;
int main(){
  Placement anchor{}, sample{};Vec3 scale{1,1,1};ObjectMotion tracking;
  // Initial tiny sampling jitter cannot drag the world-locked TV.
  for(int i=0;i<50;++i){sample.position.x=(i%2)?0.004f:-0.004f;
    assert(!tracking.apply(anchor,scale,sample,{1,1,1}));}
  assert(anchor.position.x==0);
  // A real edited object's new coords must be adopted after confirmation.
  sample.position={0.13f,0.0f,0.0f};
  assert(!tracking.apply(anchor,scale,sample,{1,1,1}));
  assert(!tracking.apply(anchor,scale,sample,{1,1,1}));
  assert(tracking.apply(anchor,scale,sample,{1,1,1}));
  assert(std::abs(anchor.position.x-0.13f)<1e-5f);
  sample.position.x=0.22f;assert(tracking.apply(anchor,scale,sample,{1,1,1}));
  assert(std::abs(anchor.position.x-0.22f)<1e-5f);
  // Movement follows orientation and scale too.
  sample.rotation={0,0,0.0871557f,0.996195f};
  assert(tracking.apply(anchor,scale,sample,{1,1,1}));
  sample.position.x+=0.03f;
  assert(tracking.apply(anchor,scale,sample,{1.1f,1,1}));
  assert(std::abs(scale.x-1.1f)<1e-5f);
  tracking.reset();assert(!tracking.following);
  return 0;
}
