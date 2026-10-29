#include "hook_transaction.h"
#include <cassert>
#include <cstdint>
using namespace virtualtv;
namespace {
unsigned creates{},queues{},applies{},disables{},removes{},queue_disables{};
unsigned fail_create{},fail_queue{};bool fail_apply{},fail_disable{};
MH_STATUS WINAPI create(LPVOID p,LPVOID,LPVOID* original){++creates;if(creates==fail_create)return MH_ERROR_MEMORY_ALLOC;*original=p;return MH_OK;}
MH_STATUS WINAPI queue(LPVOID){return ++queues==fail_queue?MH_ERROR_NOT_CREATED:MH_OK;}
MH_STATUS WINAPI apply(){++applies;return fail_apply?MH_ERROR_MEMORY_PROTECT:MH_OK;}
MH_STATUS WINAPI queue_disable(LPVOID){++queue_disables;return MH_OK;}
MH_STATUS WINAPI disable(LPVOID){++disables;return fail_disable?MH_ERROR_MEMORY_PROTECT:MH_OK;}
MH_STATUS WINAPI remove(LPVOID){++removes;return MH_OK;}
HookApi api{create,queue,apply,queue_disable,disable,remove};
void reset(){creates=queues=applies=disables=removes=queue_disables=fail_create=fail_queue=0;fail_apply=fail_disable=false;}
void add(HookTransaction& t,unsigned count){for(unsigned i=0;i<count;++i){void* original{};t.add(reinterpret_cast<void*>(std::uintptr_t(i+1)),reinterpret_cast<void*>(16),&original);}}
}
int main(){
    reset();{HookTransaction t(api);add(t,4);assert(!queues&&!applies);t.commit();}assert(creates==4&&queues==4&&applies==1&&!removes&&!disables);
    // A failure at any preparation step removes only targets already owned.
    for(unsigned failure=1;failure<=4;++failure){reset();fail_create=failure;try{HookTransaction t(api);add(t,4);assert(false);}catch(const std::exception&){}assert(!applies&&removes==failure-1&&!disables);}
    // A queue failure has not exposed callbacks; queued targets can be removed.
    for(unsigned failure=1;failure<=4;++failure){reset();fail_queue=failure;try{HookTransaction t(api);add(t,4);t.commit();assert(false);}catch(const std::exception&){}assert(!applies&&removes==4&&!disables);}
    // Commit may have partially activated callbacks. Disable all, retain trampolines.
    reset();fail_apply=true;try{HookTransaction t(api);add(t,4);t.commit();assert(false);}catch(const std::exception&){}assert(disables==4&&queue_disables==4&&!removes);
    reset();fail_disable=true;{HookTransaction t(api);add(t,4);t.commit();assert(!t.rollback());}assert(disables==4&&queue_disables==4&&!removes);
}
