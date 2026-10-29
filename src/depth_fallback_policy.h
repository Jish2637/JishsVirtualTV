#pragma once
#include <cstdint>
namespace virtualtv::depth {
struct FallbackPolicy {
    std::uint64_t missing_since{};bool waiting{};
    void reset(){waiting=false;missing_since=0;}
    bool update(std::uint64_t now,bool eligible,bool usable){
        if(!eligible||usable){reset();return false;}
        if(!waiting){waiting=true;missing_since=now;return false;}
        return now>=missing_since&&now-missing_since>=3000;
    }
};
}
