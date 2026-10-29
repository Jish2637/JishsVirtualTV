#include "tv_sync.h"
#include <cassert>
#include <iostream>
using namespace virtualtv::sync;
int main(){
    Packet p{Kind::offer,22,4,99,123456,2000,"https://example.com/abc?x=1"};
    auto bytes=encode(p);assert(bytes);auto q=decode(*bytes);assert(q);
    assert(q->kind==p.kind&&q->room==p.room&&q->revision==p.revision&&q->sender==p.sender);
    assert(q->payload==p.payload);
    assert(acceptable(*q,99,22,4,99));assert(!acceptable(*q,98,22,4,99));
    assert(!acceptable(*q,99,23,4,99));assert(!acceptable(*q,99,22,5,99));
    assert(!acceptable(*q,99,22,4,100));
    assert(!decode("VTS1"));std::string bad=*bytes;bad[4]=0;assert(!decode(bad));
    p.payload.assign(max_payload+1,'a');assert(!encode(p));
    auto offset=estimate_offset(100000,106000,107000,103000);
    assert(offset&&offset->offset_us==5000&&offset->rtt_us==2000);
    assert(!estimate_offset(200000,100000,100000,100001));
    assert(desired_position_ms(1200000,1000000,5000)==5200);
    assert(desired_position_ms(900000,1000000,5000)==5000);
    std::cout<<"sync protocol tests passed\n";
}
