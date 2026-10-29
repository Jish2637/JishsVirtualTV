#include "tv_runtime.h"
#include <cassert>
#include <sstream>
#include <limits>
#include <cmath>
using namespace virtualtv;
bool eq(float a,float b){return std::abs(a-b)<0.0002f;}
int main(){
    Settings settings;std::istringstream legacy("2 1 3 4 5\n");assert(load_settings(legacy,settings));assert(settings.screen.width==2&&settings.screen.offset.z==5);
    settings.screen.horizontal_curve_deg=80;settings.screen.vertical_curve_deg=-35;settings.screen.rotation=euler_degrees({30,60,90});settings.saved={55,22,123,9};std::ostringstream encoded;write_settings(encoded,settings);Settings restored;std::istringstream input(encoded.str());assert(load_settings(input,restored));assert(restored.saved==settings.saved);assert(eq(restored.screen.rotation.w,settings.screen.rotation.w)&&eq(restored.screen.horizontal_curve_deg,80)&&eq(restored.screen.vertical_curve_deg,-35));
    for(const char* invalid:{"", "JISH_TV_2 1 1 0 0 0", "-1 1 0 0 0", "1 1 0 0", "1 1 0 0 0 extra", "nan 1 0 0 0"}){std::istringstream stream(invalid);assert(!load_settings(stream,restored));}
    const Identity id{55,22,123,9};assert(can_restore(id,{id}));assert(!can_restore(id,{}));assert(!can_restore(id,{id,id}));auto reused=id;reused.instance++;assert(!can_restore(id,{reused}));reused=id;reused.session++;assert(!can_restore(id,{reused}));assert(!can_restore(Identity{}, {Identity{}}));
    std::vector<NearbyObject> rows;
    for(unsigned i=1;i<=4;++i){NearbyObject r;r.identity={i,42,123,i};r.placement.position={float(i*25),0,0};rows.push_back(r);}
    rows.push_back(rows.front());auto outside=rows.front();outside.identity.entity=8;outside.placement.position={100.01f,0,0};rows.push_back(outside);
    auto bad=rows.front();bad.identity.entity=9;bad.placement.position.x=std::numeric_limits<float>::quiet_NaN();rows.push_back(bad);
    filter_nearby(rows,{0,0,0});assert(rows.size()==4&&rows.front().distance==25&&rows.back().distance==100);
    auto recycled=rows.front();recycled.identity.instance++;rows.push_back(recycled);filter_nearby(rows,{0,0,0});assert(rows.size()==3&&rows.front().identity.entity==2);
    filter_nearby(rows,{std::numeric_limits<float>::infinity(),0,0});assert(rows.empty());
    Placement p;p.position={10,20,30};p.rotation=euler_degrees({0,0,90});p.scale=2;Screen s;s.width=2;s.height=2;s.offset={1,0,0};s.rotation=euler_degrees({0,0,90});auto c=corners(p,s);assert(eq(c.positions[0].x,12)&&eq(c.positions[0].y,20)&&eq(c.positions[0].z,30));
    s.width=std::numeric_limits<float>::infinity();assert(!valid(p,s));s=Screen{};p.rotation={0,0,0,0};assert(!valid(p,s));
}
