#pragma once
#include "tv_sync.h"
#include "tv_runtime.h"
#include <cstring>
#include <cmath>
namespace virtualtv::sync {
struct Offer {std::string url;Placement placement;Vec3 scale{1,1,1};Screen screen;
    std::uint16_t playlist_index{},playlist_total{};bool playlist_repeat{};
};
inline std::optional<std::string> encode_preparation(const Offer& o);
inline std::optional<Offer> decode_preparation(std::string_view s);
inline void append_float(std::string& out,float value){std::uint32_t bits{};std::memcpy(&bits,&value,4);for(int i=0;i<4;++i)out.push_back(static_cast<char>(bits>>(i*8)));}
inline bool get_float(std::string_view bytes,std::size_t& pos,float& value){if(pos+4>bytes.size())return false;std::uint32_t bits{};for(int i=0;i<4;++i)bits|=std::uint32_t(static_cast<unsigned char>(bytes[pos++]))<<(i*8);std::memcpy(&value,&bits,4);return std::isfinite(value)&&std::abs(value)<1000000.f;}
inline std::optional<std::string> encode_offer(const Offer& o){
    if(o.url.empty()||o.url.size()>500||o.url.find('\0')!=std::string::npos||!valid(o.placement,o.screen))return std::nullopt;
    std::string s=o.url;s.push_back('\0');
    for(auto v:{o.placement.position.x,o.placement.position.y,o.placement.position.z,o.placement.rotation.x,o.placement.rotation.y,o.placement.rotation.z,o.placement.rotation.w,
       o.scale.x,o.scale.y,o.scale.z,o.screen.offset.x,o.screen.offset.y,o.screen.offset.z,o.screen.rotation.x,o.screen.rotation.y,o.screen.rotation.z,o.screen.rotation.w,o.screen.width,o.screen.height}) append_float(s,v);
    if(o.playlist_total){
        if(o.playlist_total>300||o.playlist_index>=o.playlist_total)return std::nullopt;
        s+="PLQ1";
        s.push_back(static_cast<char>(o.playlist_index&255));s.push_back(static_cast<char>(o.playlist_index>>8));
        s.push_back(static_cast<char>(o.playlist_total&255));s.push_back(static_cast<char>(o.playlist_total>>8));
        s.push_back(o.playlist_repeat?1:0);
    }
    // Old uncurved sync offers retain exactly their old bytes and size.
    if(o.screen.horizontal_curve_deg!=0||o.screen.vertical_curve_deg!=0){
        s+="CRV1";append_float(s,o.screen.horizontal_curve_deg);append_float(s,o.screen.vertical_curve_deg);
    }
    return s;
}
inline std::optional<Offer> decode_offer(std::string_view s){
    auto nul=s.find('\0');if(nul==std::string_view::npos||!nul||nul>500||s.size()<nul+1+19*4||s.size()>nul+1+19*4+9+12)return std::nullopt;
    Offer o;o.url=std::string(s.substr(0,nul));std::size_t pos=nul+1;
    for(float* f:{&o.placement.position.x,&o.placement.position.y,&o.placement.position.z,&o.placement.rotation.x,&o.placement.rotation.y,&o.placement.rotation.z,&o.placement.rotation.w,
       &o.scale.x,&o.scale.y,&o.scale.z,&o.screen.offset.x,&o.screen.offset.y,&o.screen.offset.z,&o.screen.rotation.x,&o.screen.rotation.y,&o.screen.rotation.z,&o.screen.rotation.w,&o.screen.width,&o.screen.height})if(!get_float(s,pos,*f))return std::nullopt;
    if(o.scale.x<=0||o.scale.y<=0||o.scale.z<=0||!valid(o.placement,o.screen))return std::nullopt;
    if(s.size()>=pos+9&&s.substr(pos,4)=="PLQ1"){
        pos+=4;
        o.playlist_index=static_cast<std::uint16_t>(static_cast<unsigned char>(s[pos]))|
                         static_cast<std::uint16_t>(static_cast<unsigned char>(s[pos+1]))<<8;pos+=2;
        o.playlist_total=static_cast<std::uint16_t>(static_cast<unsigned char>(s[pos]))|
                         static_cast<std::uint16_t>(static_cast<unsigned char>(s[pos+1]))<<8;pos+=2;
        if(s[pos]!=0&&s[pos]!=1)return std::nullopt;
        o.playlist_repeat=s[pos]==1;
        if(!o.playlist_total||o.playlist_total>300||o.playlist_index>=o.playlist_total)return std::nullopt;
        ++pos;
    }
    if(s.size()==pos+12){
        if(s.substr(pos,4)!="CRV1")return std::nullopt;
        pos+=4;
        if(!get_float(s,pos,o.screen.horizontal_curve_deg)||!get_float(s,pos,o.screen.vertical_curve_deg))return std::nullopt;
    }
    if(pos!=s.size()||!valid(o.placement,o.screen))return std::nullopt;
    return o;
}
inline std::optional<std::string> encode_preparation(const Offer& o){
    if(!o.playlist_total)return std::nullopt;
    auto payload=encode_offer(o);if(!payload)return std::nullopt;return "NXT1"+*payload;
}
inline std::optional<Offer> decode_preparation(std::string_view s){
    if(s.substr(0,4)!="NXT1")return std::nullopt;
    auto offer=decode_offer(s.substr(4));if(!offer||!offer->playlist_total)return std::nullopt;return offer;
}
}
