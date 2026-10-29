#pragma once
#include <cstdint>
#include <set>
#include <vector>
namespace virtualtv {
// IDs come from the full local editor document, never the nearby native scan.
struct NewPlacementPolicy {
    bool enabled{},baseline{};
    std::uint64_t generation{};
    std::set<std::uint64_t> seen,eligible;
    void reset(){enabled=false;baseline=false;seen.clear();eligible.clear();generation=0;}
    void toggle(bool value){enabled=value;eligible.clear();}
    void observe(std::uint64_t next,const std::vector<std::uint64_t>& ids){
        if(generation!=next){reset();generation=next;}
        std::set<std::uint64_t> current(ids.begin(),ids.end());
        if(baseline&&enabled)for(auto id:current)if(!seen.count(id))eligible.insert(id);
        for(auto it=eligible.begin();it!=eligible.end();)if(!current.count(*it))it=eligible.erase(it);else ++it;
        seen=std::move(current);baseline=true;
    }
};
}
