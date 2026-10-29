#include "attachment_store.h"
#include <cassert>
#include <sstream>
int main(){
    using namespace virtualtv::attachment_store;
    Record tv{91,80,0,{},"bkpropshome_generic_television01",{626.9f,201.9f,1063.2f},true};
    Record a{93,82,2,{},"BoomBox90s",{631.5f,199.f,1065.2f},true};
    Record b{95,81,2,{},"BoomBox90s",{622.4f,198.9f,1061.8f},true};
    a.screen.offset.x=.35f;a.screen.horizontal_curve_deg=45.f;a.screen.vertical_curve_deg=-30.f;
    std::stringstream file;assert(write(file,{tv,a,b}));
    std::vector<Record> loaded;assert(read(file,loaded)&&loaded.size()==3);
    assert(loaded[0].asset==tv.asset&&loaded[1].origin.x==631.5f);
    assert(loaded[1].screen.offset.x==.35f&&loaded[1].screen.horizontal_curve_deg==45.f&&loaded[1].screen.vertical_curve_deg==-30.f);
    // Different game-assigned instance/drop numbers still restore safely.
    std::vector<Candidate> live{{599,56,tv.asset,{627.f,201.9f,1063.1f}},
        {600,57,a.asset,a.origin},{601,58,b.asset,b.origin}};
    assert(unique_match(0,loaded,live)==0);
    assert(unique_match(1,loaded,live)==1);
    assert(unique_match(2,loaded,live)==2);
    // Same asset appearing twice close to a saved coordinate is ambiguous.
    live.push_back({611,59,a.asset,{631.55f,199.f,1065.2f}});
    assert(!unique_match(1,loaded,live));live.pop_back();
    // Two stored attachments competing for the same candidate are ambiguous.
    loaded.push_back({900,900,2,{},a.asset,a.origin,true});
    assert(!unique_match(1,loaded,live));loaded.pop_back();
    live[1].asset="Different";assert(!unique_match(1,loaded,live));
    live[1].asset=a.asset;live[1].position.x+=1.f;assert(!unique_match(1,loaded,live));
    std::stringstream legacy("JISH_TV_ATTACHMENTS_1 1\n11 80 0 1 1 0 0 0 0 0 0 1\n");
    std::vector<Record> old;assert(read(legacy,old)&&old.size()==1);
    assert(old[0].asset.empty()&&!unique_match(0,old,live));
    // Older name/position file remains flat when upgraded to v3.
    std::stringstream old_v2("JISH_TV_ATTACHMENTS_2 1\n11 80 0 \"tv\" 1 1 2 3 1 1 0 0 0 0 0 0 1\n");
    std::vector<Record> old_named;assert(read(old_v2,old_named)&&old_named.size()==1);
    assert(old_named[0].screen.horizontal_curve_deg==0&&old_named[0].screen.vertical_curve_deg==0);
    std::stringstream upgrade;assert(write(upgrade,old));assert(read(upgrade,old));
    std::stringstream duplicate;assert(!write(duplicate,{tv,tv}));
    Record another_primary=a;another_primary.role=0;
    std::stringstream multi;assert(!write(multi,{tv,another_primary}));
    std::stringstream malformed("JISH_TV_ATTACHMENTS_2 129\n");assert(!read(malformed,old));
}
