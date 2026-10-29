#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <charconv>
namespace virtualtv::sync {
struct LobbyInfo {bool active{},hosting{},reported{};unsigned others{};};
// HB v2: boot|active|other-players|local-host. Legacy HB with only boot is valid.
inline bool heartbeat_info(std::string_view body,std::uint64_t& boot,LobbyInfo& lobby){
    const auto split=body.find('|');
    auto number=[](std::string_view value,std::uint64_t& out){auto r=std::from_chars(value.data(),value.data()+value.size(),out);return !value.empty()&&r.ec==std::errc{}&&r.ptr==value.data()+value.size();};
    if(!number(body.substr(0,split),boot)||!boot)return false;
    lobby={};
    if(split==body.npos)return true;
    body.remove_prefix(split+1);
    const auto p=body.find('|');if(p==body.npos)return false;
    const auto q=body.find('|',p+1);if(q==body.npos)return false;
    std::uint64_t a{},b{},c{};
    if(!number(body.substr(0,p),a)||!number(body.substr(p+1,q-p-1),b)||!number(body.substr(q+1),c)||a>1||b>256||c>1)return false;
    lobby={a==1,c==1,true,static_cast<unsigned>(b)};
    return true;
}
inline std::string lobby_status(bool plugin_online,const LobbyInfo& info,std::uint64_t id){
    if(!plugin_online)return "TV sync plugin offline. Enable JishsVirtualTVBridge in Mod Manager.";
    if(info.reported&&!info.active)return "No players in session. Host or join a multiplayer lobby first.";
    if(!id)return "Waiting for your multiplayer player ID. Host or join a lobby.";
    if(!info.reported)return "Multiplayer connection detected. Checking session players...";
    if(info.others==0)return info.hosting?"Sync is ready - waiting for players to join your hosted lobby.":"No other players in session. Waiting for someone to join.";
    return "Sync is ready - "+std::to_string(info.others)+" other player"+(info.others==1?"":"s")+" in session.";
}
}
