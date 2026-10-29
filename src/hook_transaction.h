#pragma once
#include <MinHook.h>
#include <array>
#include <stdexcept>
namespace virtualtv {
struct HookApi {
    decltype(&MH_CreateHook) create=MH_CreateHook;
    decltype(&MH_QueueEnableHook) queue_enable=MH_QueueEnableHook;
    decltype(&MH_ApplyQueued) apply=MH_ApplyQueued;
    decltype(&MH_QueueDisableHook) queue_disable=MH_QueueDisableHook;
    decltype(&MH_DisableHook) disable=MH_DisableHook;
    decltype(&MH_RemoveHook) remove=MH_RemoveHook;
};
// All targets are created disabled. A single commit enables this DLL's
// graphics/menu hooks. Never free a trampoline that a callback could still use.
class HookTransaction {
    HookApi api_;std::array<void*,4> targets_{};unsigned count_{};bool exposed_{},committed_{};
public:
    explicit HookTransaction(HookApi api={}):api_(api){}
    ~HookTransaction(){if(!committed_)rollback();}
    HookTransaction(const HookTransaction&)=delete;
    void add(void* target,void* detour,void** original){
        if(count_==targets_.size())throw std::runtime_error("Hook transaction capacity exceeded");
        if(api_.create(target,detour,original)!=MH_OK)throw std::runtime_error("Create disabled TV hook failed");
        targets_[count_++]=target;
    }
    void commit(){
        for(unsigned i=0;i<count_;++i)if(api_.queue_enable(targets_[i])!=MH_OK)throw std::runtime_error("Queue TV hook activation failed");
        exposed_=true; // ApplyQueued can partially succeed before reporting an error.
        if(api_.apply()!=MH_OK)throw std::runtime_error("Commit TV hooks failed");
        committed_=true;
    }
    bool rollback() noexcept {
        bool good=true;
        for(unsigned i=count_;i;--i){auto* p=targets_[i-1];
            if(exposed_){good&=api_.queue_disable(p)==MH_OK;const auto status=api_.disable(p);good&=status==MH_OK||status==MH_ERROR_DISABLED;}
            else good&=api_.remove(p)==MH_OK;
        }
        count_=0;return good;
    }
    bool may_have_run() const{return exposed_;}
};
}
