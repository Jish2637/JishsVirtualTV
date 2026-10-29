#pragma once
#include "tv_runtime.h"
#include <array>
#include <cstring>
#include <sstream>
namespace virtualtv {
// Logging only: aggregate brief flaps, rate-limit transitions to 2/s, heartbeat
// every 5s. Never throw or change the caller's Win32 error state.
class GateTrace {
    struct Count {const char* reason{};unsigned count{};};
    std::mutex mutex_;const char* last_{};ULONGLONG since_{},next_change_{},next_summary_{};
    unsigned transitions_{};std::array<Count,24> counts_{};
public:
    template<class Details> void sample(const char* stage,const char* reason,Details details) noexcept {
        if(!debug_logging())return;
        const auto error=GetLastError();
        try{
            std::lock_guard lock(mutex_);const auto now=GetTickCount64();
            const bool changed=!last_||std::strcmp(last_,reason)!=0;
            if(changed){last_=reason;since_=now;++transitions_;}
            for(auto& count:counts_)if(!count.reason||std::strcmp(count.reason,reason)==0){count.reason=reason;++count.count;break;}
            if((changed&&now>=next_change_)||now>=next_summary_){
                std::ostringstream out;out<<"TV visibility: stage="<<stage<<" reason="<<reason<<" state_age_ms="<<(now-since_)<<" transitions="<<transitions_<<" samples=";
                for(const auto& count:counts_)if(count.reason)out<<count.reason<<':'<<count.count<<',';
                out<<' '<<details();log(out.str());counts_={};transitions_=0;next_change_=now+500;next_summary_=now+5000;
            }
        }catch(...){}
        SetLastError(error);
    }
};
}
