#pragma once
#include "url_resolver.h"
namespace virtualtv {
struct PlaylistPreparation {
    std::uint64_t generation{},scope{},revision{},started{},completed{};
    int target{-1};std::string url;
    std::optional<ResolvedMedia> media;
    void clear(){++generation;scope=revision=started=completed=0;target=-1;url.clear();media.reset();}
    bool matches(const std::string& source,int index,std::uint64_t room,std::uint64_t rev,std::uint64_t now)const{
        return source==url&&index==target&&scope==room&&revision==rev&&started&&now-started<300000;
    }
};
}
