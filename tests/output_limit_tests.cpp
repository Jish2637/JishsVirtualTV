#include "output_limits.h"
#include <cassert>
#include <vector>
using namespace virtualtv;
struct Key { std::uint64_t entity{},instance{},session{}; std::uint32_t drop{}; };
int main(){
    static_assert(max_world_screens==64 && max_world_speakers==64,
                  "Video screens and audio speaker caps must both equal 64");
    // A recreated game entity is the same placed object within the same session.
    const Key original{101,1234,7,9};
    const Key recreated{202,1234,7,9};
    const Key another{203,1234,7,10};
    const Key another_session{204,1234,8,9};
    assert(same_placed_object(original,recreated));
    assert(!same_placed_object(original,another));
    assert(!same_placed_object(original,another_session));
    assert(!same_placed_object(Key{},original));
    // A queued screen/speaker also consumes a slot before the game thread attaches it.
    assert(can_add_output(0,0,max_world_speakers));
    assert(can_add_output(63,0,max_world_speakers));
    assert(!can_add_output(64,0,max_world_speakers));
    assert(!can_add_output(63,1,max_world_speakers));
    assert(!can_add_output(60,4,max_world_speakers));
    assert(can_add_output(60,3,max_world_speakers));
    // The primary TV occupies one screen slot (63 additional screens).
    assert(can_add_output(62,0,max_world_screens-1));
    assert(!can_add_output(63,0,max_world_screens-1));
    // 64 distinct physical placements can be occupied, but a recreated handle
    // does not permit adding a second output to a used placement.
    std::vector<Key> placed;
    for(unsigned i=0;i<64;++i)placed.push_back({1000+i,10000+i,7,i});
    assert(placed.size()==max_world_screens);
    for(unsigned i=0;i<64;++i)for(unsigned j=0;j<64;++j)
        assert(same_placed_object(placed[i],placed[j])==(i==j));
}
