#include "reshade_switch_policy.h"
#include "reshade_capture_policy.h"
#include "reshade_camera_reprojection.h"
#include "reshade_projection_aspect.h"
#include "tv_presentation_projection.h"
#include <cmath>
#include <cassert>
int main(){
    // Real field capture: ReSkate output 3440x1440, native depth 2296x960.
    // Streamline measured X focal=0.657078 and Y focal=1.56969.
    using virtualtv::depth::native::reshade::projection_aspect;
    const double outputAspect=projection_aspect(3440,1440,2296.0/960.0);
    const double estimatedFocalX=1.56969/outputAspect;
    assert(std::abs(outputAspect-3440.0/1440.0)<1e-12);
    assert(std::abs(estimatedFocalX-0.657078)<0.00001);
    assert(std::abs(1.56969/(2296.0/960.0)-0.657078)>0.0007);
    assert(projection_aspect(0,0,2296.0/960.0)==2296.0/960.0);
    assert(std::abs(projection_aspect(1920,1080,1280.0/720.0)-16.0/9.0)<1e-12);

    // The observed ReShade capture X focal (0.656315) differs from the game
    // final camera (0.657078). This must alter only presented geometry; a
    // saved depth sample continues using its own immutable projection.
    const auto corrected=virtualtv::presentation::final_focal(0.657078f,1.56969f,0.656315f,1.56969f);
    assert(corrected.valid && std::abs(corrected.x-0.657078f)<1e-7f);
    assert(!virtualtv::presentation::final_focal(0.8f,1.56f,0.656315f,1.56969f).valid);
    assert(!virtualtv::presentation::final_focal(NAN,1.56f,0.656315f,1.56969f).valid);
    // Moving the CURRENT camera must never alter an existing depth texture's
    // reprojection. A TV world point remains tied to the CAPTURE pose.
    using virtualtv::depth::reprojection::project;
    const std::array<float,16> capture{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    const std::array<float,16> projection{1,0,0,0,0,1,0,0,0,0,0,-1,0,0,.06f,0};
    const std::array<float,3> tvPoint{0,0,3};
    const auto centered=project(capture,projection,tvPoint);
    assert(centered.visible && std::abs(centered.x-.5f)<1e-6f && std::abs(centered.y-.5f)<1e-6f);
    auto movedCapture=capture;movedCapture[12]=1;
    const auto reprojection=project(movedCapture,projection,tvPoint);
    assert(reprojection.visible && reprojection.x<centered.x-.1f);
    // A player can move the presentation camera without mutating the
    // original capture's pose, so the stored depth lookup remains centered.
    const auto stillCentered=project(capture,projection,tvPoint);
    assert(stillCentered.x==centered.x && stillCentered.y==centered.y);
    auto behind=capture;const auto clipped=project(behind,projection,{0,0,-3});
    assert(!clipped.visible);

    using virtualtv::depth::native::selection::ChallengerTracker;
    using virtualtv::depth::native::capture_policy::keep_completed_pass;
    assert(keep_completed_pass(true,0,2,1,100,116));
    assert(keep_completed_pass(true,0,2,1,100,140));
    assert(!keep_completed_pass(true,0,2,1,100,141));
    assert(!keep_completed_pass(true,0,2,1,100,99));
    assert(!keep_completed_pass(true,0,1,2,100,116));
    assert(!keep_completed_pass(true,2,2,1,100,116));
    assert(!keep_completed_pass(false,0,2,1,100,116));
    ChallengerTracker h; int one=1,two=2,three=3; const void* a=&one;const void* b=&two;const void* c=&three;
    // Similar-scoring resources must never force churn.
    for(int i=0;i<100;++i)h.observe(b,a,110,100);
    assert(!h.ready());
    // Sustained strongly superior work arms a challenger but does not switch.
    for(unsigned i=0;i<ChallengerTracker::required_frames-1;++i)h.observe(b,a,300,100);
    assert(!h.ready() && h.candidate==b);
    h.observe(b,a,300,100);assert(h.ready());
    // One frame of competing evidence cancels the unconfirmed challenger.
    h.observe(c,a,300,100);assert(!h.ready() && h.candidate==c && h.consecutive==1);
    // Disappearing current resource allows a steady replacement to arm.
    for(int i=0;i<7;++i)h.observe(c,a,350,0);
    assert(h.ready());
    // Any frame in which the selected resource is best clears pending switch.
    h.observe(a,a,400,300);assert(!h.ready() && !h.candidate);
    // A one-frame/low-work transient must not arm anything.
    h.observe(b,a,7,0);assert(!h.ready());
    // Empty frame should not arm or retain stale replacement.
    h.observe(nullptr,a,0,0);assert(!h.candidate);
}
