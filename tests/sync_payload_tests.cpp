#include "tv_sync_payload.h"
#include <cassert>
#include <iostream>
int main(){
    using namespace virtualtv;
    sync::Offer offer;
    offer.url="https://example.org/video.mp4";
    offer.placement.position={5,4,3};
    offer.placement.rotation={0,0,0,1};
    offer.scale={1.1f,1.2f,1.3f};
    offer.screen.offset={0.2f,0.5f,0.1f};
    offer.screen.width=2.5f;offer.screen.height=1.5f;
    auto bytes=sync::encode_offer(offer);assert(bytes);
    auto decoded=sync::decode_offer(*bytes);assert(decoded);
    assert(decoded->url==offer.url);
    assert(decoded->placement.position.x==5 && decoded->scale.z==1.3f);
    assert(decoded->screen.offset.y==0.5f && decoded->screen.width==2.5f);
    auto playlist_offer=offer;playlist_offer.playlist_index=2;playlist_offer.playlist_total=10;playlist_offer.playlist_repeat=true;
    auto list_bytes=sync::encode_offer(playlist_offer);assert(list_bytes);
    auto list_decoded=sync::decode_offer(*list_bytes);assert(list_decoded);
    assert(list_decoded->playlist_index==2&&list_decoded->playlist_total==10&&list_decoded->playlist_repeat);
    auto corrupt_meta=*list_bytes;corrupt_meta[corrupt_meta.size()-9]='X';assert(!sync::decode_offer(corrupt_meta));
    // Curved and curved-playlist payloads preserve geometry; old flat bytes stay unchanged.
    auto curved=offer;curved.screen.horizontal_curve_deg=90;curved.screen.vertical_curve_deg=-30;
    auto curve_bytes=sync::encode_offer(curved);assert(curve_bytes&&curve_bytes->size()==bytes->size()+12);
    auto curve_decoded=sync::decode_offer(*curve_bytes);assert(curve_decoded);
    assert(curve_decoded->screen.horizontal_curve_deg==90&&curve_decoded->screen.vertical_curve_deg==-30);
    curved.playlist_total=3;curved.playlist_index=1;
    auto curved_playlist=sync::encode_offer(curved);assert(curved_playlist);
    auto curved_playlist_read=sync::decode_offer(*curved_playlist);assert(curved_playlist_read);
    assert(curved_playlist_read->playlist_index==1&&curved_playlist_read->screen.horizontal_curve_deg==90);
    auto broken=*bytes;broken.pop_back();assert(!sync::decode_offer(broken));
    auto invalid=offer;invalid.scale.x=0;
    auto malformed=sync::encode_offer(invalid);assert(malformed);
    assert(!sync::decode_offer(*malformed));
    std::cout<<"sync payload tests passed\n";
}
