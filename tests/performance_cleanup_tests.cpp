#include "depth_fallback_policy.h"
#include "video_frame.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <atomic>
int main(){
    virtualtv::depth::FallbackPolicy p;
    assert(!p.update(0,true,false));assert(!p.update(2999,true,false));assert(p.update(3000,true,false));
    p.reset();assert(!p.update(4000,true,false));assert(!p.update(6500,true,true));
    assert(!p.update(7000,true,false));assert(!p.update(9999,true,false));assert(p.update(10000,true,false));
    assert(!p.update(11000,false,false));assert(!p.update(20000,true,false)); // Hidden time doesn't count.
    p.reset();assert(!p.update(25000,true,false));assert(!p.update(24000,true,false)); // clock cannot underflow
    VideoFrameMailbox box;assert(!box.snapshot());
    auto a=std::make_shared<VideoFrame>();a->width=2;a->height=1;a->bgra.assign(8,42);box.publish(a);a.reset();
    auto first=box.snapshot();std::weak_ptr<const VideoFrame> weak=first;auto sequence=first->sequence;
    for(int i=0;i<1000;++i){auto same=box.snapshot();assert(same.get()==first.get()&&same->bgra.data()==first->bgra.data());}
    auto b=std::make_shared<VideoFrame>();b->bgra.assign(8,9);box.publish(b);b.reset();
    auto second=box.snapshot();assert(second->sequence>sequence&&first->bgra[0]==42&&second->bgra[0]==9);
    first.reset();assert(weak.expired()); // no permanent reference to an obsolete frame
    sequence=second->sequence;std::weak_ptr<const VideoFrame> stopped=second;second.reset();box.clear();assert(stopped.expired());
    // Clear wakes a waiting transfer worker and releases its frame; sequence remains monotonic on replay.
    std::atomic<bool> waiting=false;std::thread consumer([&]{waiting=true;auto frame=box.wait(sequence,1000);assert(frame&&frame->sequence>sequence);});
    while(!waiting.load())std::this_thread::yield();
    auto c=std::make_shared<VideoFrame>();c->bgra.assign(8,7);box.publish(c);c.reset();consumer.join();
    box.clear();assert(!box.snapshot());
    std::cout<<"Depth grace/reset/retry and immutable zero-copy frames, wakeup and release passed\n";
}
