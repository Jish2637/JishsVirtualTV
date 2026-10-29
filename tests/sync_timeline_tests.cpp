#include "sync_timeline.h"
#include <cassert>
#include <iostream>
using namespace virtualtv::sync;
int main(){
 auto anchor=Anchor{1,10000000,90000,Playback::Playing};auto wire=encode_anchor(anchor);auto decoded=decode_anchor(wire);assert(decoded&&decoded->position_ms==90000);assert(!decode_anchor(wire+"x"));wire[28]=3;assert(!decode_anchor(wire));
 JoinTimeline viewer;assert(viewer.mute());assert(viewer.request_sent(10000000));assert(viewer.accept(anchor,10200000));assert(viewer.target_ms==90950&&viewer.start_us==10950000);
 assert(!viewer.readback(90950,4,10300000));assert(viewer.phase==JoinTimeline::Phase::AwaitStart);assert(viewer.readback(90950,4,10950000));assert(!viewer.mute());
 // Repeated ticks and duplicate/reordered packets never seek or re-align a joined viewer.
 for(unsigned i=0;i<1000;++i)assert(!viewer.readback(92000+i,3,11000000+i*1000));assert(!viewer.accept(anchor,11000000));assert(!viewer.accept({0,1,1,Playback::Playing},11000000));
 // Slow resolution uses the host's fresh position at readiness, not its original offer position.
 JoinTimeline late;late.rtt_us=600000;late.request_sent(90000000);late.accept({2,90000000,170000,Playback::Playing},90400000);assert(late.start_us==91850000&&late.target_ms==171850);
 late.readback(0,4,90500000);late.timeout(91850000);assert(late.phase==JoinTimeline::Phase::Request);assert(late.request_sent(92000000));late.accept({3,92200000,172200,Playback::Playing},92400000);assert(late.readback(late.target_ms,4,late.start_us-1000)==false);assert(late.readback(late.target_ms,4,late.start_us));
 JoinTimeline paused;paused.request_sent(10000000);paused.accept({4,10000000,51000,Playback::Paused},10200000);assert(paused.target_ms==51000);assert(!paused.readback(51000,4,10300000)&&paused.phase==JoinTimeline::Phase::Joined&&paused.paused_host);
 assert(paused.accept({5,11000000,51000,Playback::Playing},11100000));paused.readback(paused.target_ms,4,11200000);assert(paused.readback(paused.target_ms,4,paused.start_us));
 // Buffering does not burn a preparation attempt while the host resolves its own track.
 JoinTimeline buffer;buffer.request_sent(1);buffer.accept({1,10,0,Playback::Buffering},20);assert(buffer.attempts==0);buffer.timeout(5000021);assert(buffer.phase==JoinTimeline::Phase::Request);
 JoinTimeline fail;for(unsigned i=0;i<3;++i){assert(fail.request_sent(i*6000000));fail.timeout(i*6000000+5000000);}assert(!fail.request_sent(20000000)&&fail.phase==JoinTimeline::Phase::Failed);assert(!fail.accept({100,20000000,0,Playback::Playing},20000000));fail.retry();assert(fail.request_sent(21000000));fail.fail("not seekable");assert(fail.mute());fail.reset();assert(fail.sequence==0&&fail.attempts==0);
 JoinTimeline stale;stale.request_sent(1);assert(!stale.accept({1,1,0,Playback::Playing},10000000));assert(stale.phase==JoinTimeline::Phase::Request);stale.rtt_us=4000000;stale.accept({2,10000000,0,Playback::Playing},10000000);assert(stale.start_us==13000000);
 HostTimeline host;assert(host.observe(10000000,90000,3,0));for(unsigned i=1;i<100;++i)assert(!host.observe(10000000+i*100000,90000+i*100,3,0));assert(host.observe(20000000,100000,4,0));assert(!host.observe(21000000,100000,4,0));assert(host.observe(22000000,100000,3,0));assert(host.observe(22100000,100200,3,1));assert(host.observe(22200000,150000,3,1));
 // Two viewers prepare independently; a slow joiner cannot modify or stall the host clock.
 HostTimeline running;JoinTimeline first,second;first.request_sent(1);second.request_sent(1);for(unsigned i=0;i<100;++i){running.observe(10000000+i*100000,90000+i*100,3,0);if(i==20)first.accept({1,running.time_us,std::uint64_t(running.position),Playback::Playing},running.time_us);if(i==60)second.accept({1,running.time_us,std::uint64_t(running.position),Playback::Playing},running.time_us);}assert(running.position==99900&&running.state==3);
 std::cout<<"Viewer-only timeline: late join, future target, latency, slow seek, paused host/resume, buffering, stale/reordered anchors, retry bounds, seek discontinuity and independent viewers passed\n";
}
