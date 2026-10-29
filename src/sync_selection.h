#pragma once
#include <cstdint>
#include <set>
#include <vector>
#include <string>
namespace virtualtv::sync {
struct Peer {std::uint64_t id{};std::string name;};
struct Selection {
    std::set<std::uint64_t> allowed;
    std::uint64_t active{},session{};
    void clear(){allowed.clear();active=0;}
    void refresh(std::uint64_t next,const std::vector<Peer>& peers){
        if(next!=session){clear();session=next;}
        std::set<std::uint64_t> live;for(const auto& p:peers)live.insert(p.id);
        for(auto i=allowed.begin();i!=allowed.end();)if(!live.count(*i))i=allowed.erase(i);else ++i;
        if(!allowed.count(active))active=0;
    }
    void allow(std::uint64_t id,bool enabled){if(enabled&&id)allowed.insert(id);else {allowed.erase(id);if(active==id)active=0;}}
    void choose(std::uint64_t id){if(!id||allowed.count(id))active=id;}
    bool accepts(std::uint64_t id)const{return id&&active==id&&allowed.count(id);}
};
}
