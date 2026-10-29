#include "depth_tracking.h"
#include "depth_probe.h"
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <vector>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <cstring>

namespace virtualtv::depth::tracking {
namespace {
struct Descriptor {Record data;UINT64 generation{},copied_from{},at{};};
struct List {UINT64 generation{},epoch{};D3D12_VIEWPORT viewport{};UINT viewport_count{};};
struct Candidate {
    Record last;UINT64 at{},binds{},clears{},zeros{},ones{},other{},transitions{},submitted{},generation{},interval{},submitted_interval{};
    UINT64 descriptor_generation{};D3D12_VIEWPORT viewport{};UINT viewport_count{};bool mapped{},copied{},viewport_valid{};
};
struct State {
    std::mutex mutex;
    std::unordered_map<SIZE_T,Descriptor> descriptors;
    std::unordered_map<void*,List> lists;
    std::unordered_map<void*,Candidate> candidates;
    std::unordered_map<SIZE_T,UINT64> unknown_handles;
    std::atomic<UINT64> contention{},catalog_invalidations{},unknown{},matched{},copies{},viewports{},resets{},submissions{},errors{},candidate_drops{},reset_epoch{};
    UINT64 serial{},interval{};unsigned width{},height{};virtualtv::Camera camera{};UINT64 camera_at{};
};
State& state(){static auto* value=new State;return *value;}
bool scene_size(const Record& r,unsigned width,unsigned height){
    if(!width||!height||r.desc.DepthOrArraySize!=1||r.desc.Height<64)return false;
    const double scale=double(r.desc.Width)/width;
    return scale>=.24&&scale<=1.1&&std::abs((double(r.desc.Width)/r.desc.Height)/(double(width)/height)-1)<.03;
}
void invalidate(State& s){s.descriptors.clear();++s.catalog_invalidations;}
void copied(State& s,SIZE_T dst,SIZE_T src){
    const auto found=s.descriptors.find(src);
    if(found==s.descriptors.end()){s.descriptors.erase(dst);return;}
    auto entry=found->second;entry.copied_from=entry.generation;entry.generation=++s.serial;entry.at=GetTickCount64();entry.data.handle=dst;
    if(s.descriptors.size()>=2048&&!s.descriptors.count(dst)){invalidate(s);return;}
    s.descriptors.insert_or_assign(dst,entry);
}
List& list(State& s,void* object){
    if(s.lists.size()>=2048&&!s.lists.count(object)){s.lists.clear();++s.reset_epoch;}
    return s.lists[object];
}
}
bool lookup(SIZE_T handle,Record& out) noexcept {try{auto& s=state();std::lock_guard lock(s.mutex);const auto found=s.descriptors.find(handle);if(found==s.descriptors.end())return false;out=found->second.data;return out.resource!=nullptr;}catch(...){++state().errors;return false;}}
void record(const Record& incoming) noexcept {try{
    auto& s=state();
    // Creation is rare and updates the catalog even before TV attachment.
    const bool create=std::strcmp(incoming.event,"create_dsv")==0;
    std::unique_lock lock(s.mutex,std::defer_lock);if(create)lock.lock();else if(!lock.try_lock()){++s.contention;return;}
    if(create){
        if(s.descriptors.size()>=2048&&!s.descriptors.count(incoming.handle))invalidate(s);
        s.descriptors.insert_or_assign(incoming.handle,Descriptor{incoming,++s.serial,0,GetTickCount64()});return;
    }
    auto r=incoming;const Descriptor* descriptor{};
    if(r.handle){const auto found=s.descriptors.find(r.handle);if(found!=s.descriptors.end()){
        descriptor=&found->second;r.resource=descriptor->data.resource;r.desc=descriptor->data.desc;r.view=descriptor->data.view;r.explicit_view=descriptor->data.explicit_view;++s.matched;
    }else {++s.unknown;if(s.unknown_handles.size()<16||s.unknown_handles.count(r.handle))++s.unknown_handles[r.handle];}}
    if(!r.resource)return;
    auto found=s.candidates.find(r.resource);
    if(found==s.candidates.end()&&s.candidates.size()>=64){
        // Keep scene-aspect candidates ahead of shadow-map traffic.
        if(!scene_size(r,s.width,s.height)){++s.candidate_drops;return;}
        auto victim=std::find_if(s.candidates.begin(),s.candidates.end(),[&](const auto& e){return !scene_size(e.second.last,s.width,s.height);});
        if(victim==s.candidates.end()){++s.candidate_drops;return;}s.candidates.erase(victim);
    }
    auto& c=s.candidates[r.resource];
    // Retain the last descriptor association when a later transition has no handle.
    if(!descriptor&&c.mapped){r.handle=c.last.handle;r.view=c.last.view;r.explicit_view=c.last.explicit_view;}
    c.last=r;c.at=GetTickCount64();c.interval=s.interval;
    if(descriptor){c.mapped=true;c.copied=descriptor->copied_from!=0;c.descriptor_generation=descriptor->generation;}
    const auto command=s.lists.find(r.object);c.viewport_valid=command!=s.lists.end()&&command->second.epoch==s.reset_epoch.load()&&command->second.viewport_count>0;
    c.generation=command!=s.lists.end()?command->second.generation:0;
    if(c.viewport_valid){c.viewport=command->second.viewport;c.viewport_count=command->second.viewport_count;}else{c.viewport={};c.viewport_count=0;}
    if(std::strcmp(r.event,"bind")==0)++c.binds;
    else if(std::strcmp(r.event,"clear")==0){++c.clears;if(r.flags&D3D12_CLEAR_FLAG_DEPTH){if(r.clear==0)++c.zeros;else if(r.clear==1)++c.ones;else ++c.other;}}
    else ++c.transitions;
}catch(...){++state().errors;}}
void copy_simple(ID3D12Device* device,UINT count,D3D12_CPU_DESCRIPTOR_HANDLE dst,D3D12_CPU_DESCRIPTOR_HANDLE src) noexcept {try{
    auto& s=state();++s.copies;std::lock_guard lock(s.mutex);if(count>4096){invalidate(s);return;}
    const auto step=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    for(UINT i=0;i<count;++i)copied(s,dst.ptr+SIZE_T(i)*step,src.ptr+SIZE_T(i)*step);
}catch(...){++state().errors;}}
void copy_ranges(ID3D12Device* device,UINT nd,const D3D12_CPU_DESCRIPTOR_HANDLE* dst,const UINT* ds,UINT ns,const D3D12_CPU_DESCRIPTOR_HANDLE* src,const UINT* ss) noexcept {try{
    auto& s=state();++s.copies;std::lock_guard lock(s.mutex);
    if(nd>4096||ns>4096||(!dst&&nd)||(!src&&ns)){invalidate(s);return;}
    UINT64 total_d{},total_s{};for(UINT i=0;i<nd;++i)total_d+=ds?ds[i]:1;for(UINT i=0;i<ns;++i)total_s+=ss?ss[i]:1;
    if(total_d!=total_s||total_d>4096){invalidate(s);return;}
    const auto step=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    UINT si{},offset{};
    for(UINT di=0;di<nd;++di)for(UINT j=0;j<(ds?ds[di]:1);++j){
        while(si<ns&&offset==(ss?ss[si]:1)){++si;offset=0;}
        if(si==ns){invalidate(s);return;}copied(s,dst[di].ptr+SIZE_T(j)*step,src[si].ptr+SIZE_T(offset++)*step);
    }
}catch(...){++state().errors;}}
void viewport(ID3D12GraphicsCommandList* object,UINT count,const D3D12_VIEWPORT* values) noexcept {try{
    auto& s=state();++s.viewports;std::unique_lock lock(s.mutex,std::try_to_lock);if(!lock){++s.contention;++s.reset_epoch;return;}
    auto& command=list(s,object);command.viewport_count=values&&count?count:0;command.viewport=command.viewport_count?values[0]:D3D12_VIEWPORT{};command.epoch=s.reset_epoch;
}catch(...){++state().errors;}}
void reset(ID3D12GraphicsCommandList* object) noexcept {try{
    auto& s=state();++s.resets;std::unique_lock lock(s.mutex,std::try_to_lock);if(!lock){++s.contention;++s.reset_epoch;return;}
    auto& command=list(s,object);command={};command.generation=++s.serial;command.epoch=s.reset_epoch;
}catch(...){++state().errors;}}
void submitted(ID3D12CommandQueue* queue,UINT count,ID3D12CommandList*const* objects) noexcept {try{
    auto& s=state();++s.submissions;if(!objects||!count)return;
    // Submission matching is sampled, bounded, and does not query or wait on the queue.
    if((s.submissions.load()&63)!=1)return;
    std::unique_lock lock(s.mutex,std::try_to_lock);if(!lock){++s.contention;return;}
    for(auto& entry:s.candidates){auto& c=entry.second;for(UINT i=0;i<count&&i<64;++i)if(c.last.object==objects[i]){
        const auto command=s.lists.find(objects[i]);if(command!=s.lists.end()&&command->second.generation==c.generation){++c.submitted;c.submitted_interval=s.interval;}break;
    }}(void)queue;
}catch(...){++state().errors;}}
void frame(const Scene& scene,unsigned width,unsigned height) noexcept {try{
    probe::frame(scene);
    auto& s=state();std::unique_lock lock(s.mutex,std::try_to_lock);if(!lock){++s.contention;return;}
    ++s.interval;s.width=width;s.height=height;s.camera=scene.camera;s.camera_at=GetTickCount64();
}catch(...){++state().errors;}}
void report() noexcept {if(!debug_logging())return;try{
    auto& s=state();std::vector<Candidate> rows;std::unordered_map<SIZE_T,UINT64> unknown_handles;unsigned width{},height{};virtualtv::Camera camera;UINT64 interval{},camera_at{};size_t descriptors{},lists{};
    {std::lock_guard lock(s.mutex);width=s.width;height=s.height;camera=s.camera;camera_at=s.camera_at;interval=s.interval;descriptors=s.descriptors.size();lists=s.lists.size();
        for(auto& e:s.candidates){if(e.second.at)rows.push_back(e.second);}s.candidates.clear();unknown_handles.swap(s.unknown_handles);}
    std::sort(rows.begin(),rows.end(),[&](const Candidate& a,const Candidate& b){const auto score=[&](const Candidate& c){return UINT64(scene_size(c.last,width,height))*1000000000+c.binds+c.clears+c.transitions;};return score(a)>score(b);});
    std::ostringstream summary;summary<<"Depth correlation: build=native-proto3 catalog="<<descriptors<<" lists="<<lists<<" mapped_samples="<<s.matched.load()<<" unknown_dsv_samples="<<s.unknown.load()<<" descriptor_copy_calls="<<s.copies.load()<<" viewport_calls="<<s.viewports.load()<<" reset_calls="<<s.resets.load()<<" submission_calls="<<s.submissions.load()<<" contention_drops="<<s.contention.load()<<" candidate_drops="<<s.candidate_drops.load()<<" catalog_invalidations="<<s.catalog_invalidations.load()<<" errors="<<s.errors.load()<<" candidates="<<rows.size()<<" rows_omitted="<<(rows.size()>16?rows.size()-16:0)<<" output="<<width<<'x'<<height<<" tv_draw_interval="<<interval<<" interval_is_not_game_frame=1 descriptor_lifetime_unverified=1";log(summary.str());
    if(!unknown_handles.empty()){std::ostringstream unknown;unknown<<"Depth unknown descriptors: capped_at=16 samples=";for(const auto& e:unknown_handles)unknown<<"0x"<<std::hex<<e.first<<std::dec<<':'<<e.second<<',';log(unknown.str());}
    const auto now=GetTickCount64();std::ostringstream pose;pose<<"Depth native camera: valid="<<camera.valid<<" age_ms="<<(camera_at?now-camera_at:0)<<" vertical_fov="<<camera.vertical_fov<<" position="<<camera.world[12]<<','<<camera.world[13]<<','<<camera.world[14]<<" basis=";for(unsigned i=0;i<12;++i)pose<<camera.world[i]<<',';pose<<" actual_game_projection=not_captured";log(pose.str());
    for(size_t i=0;i<rows.size()&&i<16;++i){const auto& c=rows[i];const auto& r=c.last;std::ostringstream out;
        out<<"Depth candidate: rank="<<i+1<<" scene_size="<<scene_size(r,width,height)<<" resource="<<r.resource<<" dsv=0x"<<std::hex<<r.handle<<std::dec<<" width="<<r.desc.Width<<" height="<<r.desc.Height<<" format="<<r.desc.Format<<" flags="<<r.desc.Flags<<" array="<<r.desc.DepthOrArraySize<<" samples="<<r.desc.SampleDesc.Count<<" binds="<<c.binds<<" clears="<<c.clears<<" clear_zero="<<c.zeros<<" clear_one="<<c.ones<<" clear_other="<<c.other<<" transitions="<<c.transitions<<" last_event="<<r.event<<" last_before="<<r.before<<" last_after="<<r.after<<" barrier_or_clear_flags="<<r.flags<<" subresource="<<r.subresource<<" age_ms="<<now-c.at<<" catalog_match="<<c.mapped<<" copied_descriptor="<<c.copied<<" descriptor_generation="<<c.descriptor_generation<<" view_explicit="<<r.explicit_view<<" view_format="<<r.view.Format<<" view_dimension="<<r.view.ViewDimension<<" view_flags="<<r.view.Flags<<" object="<<r.object<<" reset_generation="<<c.generation<<" viewport_known="<<c.viewport_valid<<" viewport_count="<<c.viewport_count<<" viewport="<<c.viewport.TopLeftX<<','<<c.viewport.TopLeftY<<','<<c.viewport.Width<<','<<c.viewport.Height<<','<<c.viewport.MinDepth<<','<<c.viewport.MaxDepth<<" recorded_interval="<<c.interval<<" sampled_submissions="<<c.submitted<<" submitted_interval="<<c.submitted_interval;log(out.str());
    }
}catch(...){++state().errors;}}
#ifdef TV_GPU_TEST
bool test_correlated() noexcept {auto& s=state();return s.matched>0&&s.copies>0&&s.viewports>0&&s.errors==0;}
bool test_descriptor(SIZE_T handle,ID3D12Resource* resource) noexcept {auto& s=state();std::lock_guard lock(s.mutex);const auto found=s.descriptors.find(handle);return found!=s.descriptors.end()&&found->second.data.resource==resource;}
bool test_catalog_contracts() noexcept {try{
    State s;int first{},second{};Record a{};a.resource=&first;a.desc.Width=3440;a.handle=100;
    s.descriptors[100]={a,++s.serial,0,1};copied(s,200,100);
    if(s.descriptors.at(200).data.resource!=&first||s.descriptors.at(200).copied_from==0)return false;
    a.resource=&second;a.desc.Width=1720;s.descriptors[100]={a,++s.serial,0,2};
    // Rewriting a source descriptor must not retrospectively rewrite its old copy.
    if(s.descriptors.at(200).data.resource!=&first)return false;
    copied(s,200,100);if(s.descriptors.at(200).data.resource!=&second||s.descriptors.at(200).data.desc.Width!=1720)return false;
    copied(s,200,999);if(s.descriptors.count(200))return false;
    auto& command=list(s,&first);command.viewport_count=1;command.viewport.Width=3440;command.generation=7;
    command={};command.generation=8;if(command.viewport_count!=0||command.viewport.Width!=0)return false;
    return true;
}catch(...){return false;}}
#endif
}
