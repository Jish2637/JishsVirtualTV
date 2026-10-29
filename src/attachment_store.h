#pragma once
// Portable screen/speaker manifest. A placed object's game-assigned ID can
// change each launch: persistent restoration uses asset key and world position.
#include "tv_geometry.h"
#include <cstdint>
#include <istream>
#include <ostream>
#include <vector>
#include <set>
#include <string>
#include <utility>
#include <iomanip>
#include <cmath>
#include <optional>
namespace virtualtv::attachment_store {
struct Record {
    std::uint64_t instance{}; // only a UI / current-session hint
    std::uint32_t drop{};
    unsigned role{}; // 0 primary TV, 1 additional TV, 2 speaker
    Screen screen{};
    std::string asset; // ReSkate editor internal asset key, not localized display text
    Vec3 origin{}; // native world placement, not screen alignment offset
    bool has_origin{};
};
struct Candidate {
    std::uint64_t instance{};
    std::uint32_t drop{};
    std::string asset;
    Vec3 position{};
};
inline constexpr bool key_matches(const Record& r,std::uint64_t instance,std::uint32_t drop){
    return r.instance!=0&&r.instance==instance&&r.drop==drop;
}
inline bool reasonable(Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&
    std::abs(p.x)<=1000000&&std::abs(p.y)<=1000000&&std::abs(p.z)<=1000000;}
inline bool good(const Record& r){return r.instance!=0 && r.role<=2 && valid(Placement{},r.screen) &&
    r.asset.size()<=256 && (r.asset.empty()||r.has_origin) && (!r.has_origin||reasonable(r.origin));}
inline constexpr float restore_radius_m=0.35f;
inline float squared_distance(Vec3 a,Vec3 b){const float x=a.x-b.x,y=a.y-b.y,z=a.z-b.z;return x*x+y*y+z*z;}
inline bool anchor_matches(const Record& r,const Candidate& c){
    // Never restore a legacy entry on its changing game object number alone.
    return r.has_origin&&!r.asset.empty()&&r.asset==c.asset&&reasonable(c.position)&&
        squared_distance(r.origin,c.position)<=restore_radius_m*restore_radius_m;
}
// Two-way uniqueness: refuse if two live placements fit a saved anchor, or
// if two saved anchors fit the same live placement.
inline std::optional<std::size_t> unique_match(std::size_t index,
        const std::vector<Record>& saved,const std::vector<Candidate>& current){
    if(index>=saved.size())return std::nullopt;
    std::optional<std::size_t> chosen;
    for(std::size_t i=0;i<current.size();++i)
        if(anchor_matches(saved[index],current[i])){if(chosen)return std::nullopt;chosen=i;}
    if(!chosen)return std::nullopt;
    for(std::size_t i=0;i<saved.size();++i)
        if(i!=index&&anchor_matches(saved[i],current[*chosen]))return std::nullopt;
    return chosen;
}
inline bool read(std::istream& in,std::vector<Record>& out){
    std::string magic;std::size_t count{};
    if(!(in>>magic>>count)||(magic!="JISH_TV_ATTACHMENTS_1"&&magic!="JISH_TV_ATTACHMENTS_2"&&magic!="JISH_TV_ATTACHMENTS_3")||count>128)return false;
    const bool v2=magic!="JISH_TV_ATTACHMENTS_1";const bool v3=magic=="JISH_TV_ATTACHMENTS_3";
    std::vector<Record> parsed;parsed.reserve(count);
    std::set<std::pair<std::uint64_t,std::uint32_t>> keys;
    unsigned primary_count=0,screen_count=0,speaker_count=0;
    for(std::size_t i=0;i<count;++i){
        Record r;
        if(!(in>>r.instance>>r.drop>>r.role))return false;
        if(v2){unsigned flag{};if(!(in>>std::quoted(r.asset)>>flag>>r.origin.x>>r.origin.y>>r.origin.z)||flag>1)return false;r.has_origin=flag!=0;}
        if(!(in>>r.screen.width>>r.screen.height
            >>r.screen.offset.x>>r.screen.offset.y>>r.screen.offset.z
            >>r.screen.rotation.x>>r.screen.rotation.y>>r.screen.rotation.z>>r.screen.rotation.w))return false;
        if(v3&&!(in>>r.screen.horizontal_curve_deg>>r.screen.vertical_curve_deg))return false;
        if(!good(r)||!keys.emplace(r.instance,r.drop).second)return false;
        if(r.role==0)++primary_count;
        if(r.role<=1)++screen_count;else ++speaker_count;
        if(primary_count>1||screen_count>64||speaker_count>64)return false;
        parsed.push_back(std::move(r));
    }
    std::string trailing;if(in>>trailing)return false;
    out=std::move(parsed);return true;
}
inline bool write(std::ostream& out,const std::vector<Record>& records){
    if(records.size()>128)return false;
    std::set<std::pair<std::uint64_t,std::uint32_t>> keys;
    unsigned primary_count=0,screen_count=0,speaker_count=0;
    for(const auto& r:records){
        if(!good(r)||!keys.emplace(r.instance,r.drop).second)return false;
        if(r.role==0)++primary_count;
        if(r.role<=1)++screen_count;else ++speaker_count;
    }
    if(primary_count>1||screen_count>64||speaker_count>64)return false;
    out<<"JISH_TV_ATTACHMENTS_3 "<<records.size()<<'\n'<<std::setprecision(9);
    for(const auto& r:records)
        out<<r.instance<<' '<<r.drop<<' '<<r.role<<' '
           <<std::quoted(r.asset)<<' '<<(r.has_origin?1:0)<<' '
           <<r.origin.x<<' '<<r.origin.y<<' '<<r.origin.z<<' '
           <<r.screen.width<<' '<<r.screen.height<<' '
           <<r.screen.offset.x<<' '<<r.screen.offset.y<<' '<<r.screen.offset.z<<' '
           <<r.screen.rotation.x<<' '<<r.screen.rotation.y<<' '<<r.screen.rotation.z<<' '<<r.screen.rotation.w<<' '
           <<r.screen.horizontal_curve_deg<<' '<<r.screen.vertical_curve_deg<<'\n';
    return static_cast<bool>(out);
}
}
