#include "sync_selection.h"
#include "new_placement_policy.h"
#include "playlist_preparation.h"
#include "tv_sync_payload.h"
#include <cassert>
#include <iostream>
using namespace virtualtv;
int main(){
    sync::Selection selection;std::vector<sync::Peer> peers{{11,"Same name"},{22,"Same name"},{33,"Remote"}};
    selection.refresh(1,peers);assert(!selection.accepts(11));
    selection.allow(11,true);selection.allow(22,true);assert(!selection.accepts(11));
    selection.choose(11);assert(selection.accepts(11)&&!selection.accepts(22));
    selection.choose(33);assert(selection.active==11); // Unallowed cannot become active.
    selection.choose(22);assert(selection.accepts(22)&&!selection.accepts(11));
    selection.allow(22,false);assert(!selection.active&&!selection.accepts(22));
    selection.choose(11);selection.refresh(1,{{22,"Same name"}});assert(!selection.active&&selection.allowed.empty());
    selection.allow(22,true);selection.choose(22);selection.refresh(2,peers);assert(!selection.active&&selection.allowed.empty());
    selection.allow(11,true);selection.choose(11);selection.refresh(0,{});assert(!selection.accepts(11));
    // Existing document IDs stay existing as they move in and out of the nearby scan.
    NewPlacementPolicy placement;placement.observe(10,{1,2});placement.toggle(true);placement.observe(10,{1,2});assert(placement.eligible.empty());
    placement.observe(10,{1,2,3,4});assert(placement.eligible==std::set<std::uint64_t>({3,4}));
    placement.eligible.clear();placement.observe(10,{1,2,3,4});assert(placement.eligible.empty());
    placement.toggle(false);placement.observe(10,{1,2,3,4,5});placement.toggle(true);placement.observe(10,{1,2,3,4,5,6});assert(placement.eligible==std::set<std::uint64_t>({6}));
    placement.observe(10,{1,2});assert(placement.eligible.empty());
    placement.observe(11,{1,2,7});assert(!placement.enabled&&placement.eligible.empty());
    PlaylistPreparation prep;prep.started=100;prep.url="https://youtube.com/watch?v=next";prep.target=2;prep.scope=8;prep.revision=4;
    assert(prep.matches(prep.url,2,8,4,200)&&!prep.media); // In flight is not playable yet.
    prep.media=ResolvedMedia{"https://media.invalid/stream","https://media.invalid/audio",{}, {}};
    assert(prep.matches(prep.url,2,8,4,200));
    assert(!prep.matches(prep.url,3,8,4,200));assert(!prep.matches(prep.url,2,9,4,200));assert(!prep.matches(prep.url,2,8,5,200));
    assert(!prep.matches("https://youtube.com/watch?v=other",2,8,4,200));assert(!prep.matches(prep.url,2,8,4,300100));
    const auto generation=prep.generation;prep.clear();assert(prep.generation==generation+1&&!prep.media&&prep.url.empty());
    // Repeat wraparound uses target 0 and remains revision-scoped.
    prep.url="https://youtube.com/watch?v=first";prep.target=0;prep.started=100;prep.scope=8;prep.revision=5;assert(prep.matches(prep.url,0,8,5,101));
    sync::Offer offer{prep.url,Placement{},{1,1,1},Screen{}};offer.playlist_index=0;offer.playlist_total=3;offer.playlist_repeat=true;
    auto payload=sync::encode_preparation(offer);assert(payload);auto decoded=sync::decode_preparation(*payload);assert(decoded&&decoded->url==offer.url&&decoded->playlist_index==0&&decoded->playlist_repeat);
    assert(!sync::decode_preparation("NXT2"+payload->substr(4)));assert(!sync::decode_preparation(payload->substr(0,12)));
    sync::Packet packet{sync::Kind::prepare,8,5,11,0,0,*payload};auto wire=sync::encode(packet);assert(wire&&sync::decode(*wire));
    assert(sync::acceptable(packet,11,8,5,11));assert(!sync::acceptable(packet,22,8,5,11));assert(!sync::acceptable(packet,11,8,4,11));
    // Legacy offer wire representation and decoding are unchanged.
    auto legacy=sync::encode_offer(offer);assert(legacy&&sync::decode_offer(*legacy));
    offer.playlist_total=0;assert(!sync::encode_preparation(offer));
    std::cout<<"Selection, session cleanup, new local IDs, preparation scope and compatibility passed\n";
}
