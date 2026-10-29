#include "depth_probe.h"
#include "hook_transaction.h"
#include "native_depth.h"
#include <intrin.h>
#include <array>
#include <vector>
#include <mutex>
#include <atomic>
#include <cmath>
#include <cstring>
#include <sstream>
#include <algorithm>

namespace virtualtv::depth::probe {
namespace {
thread_local bool tv_scope{};
constexpr std::array<unsigned,7> slots{21,22,32,36,38,46,47};
constexpr std::array<const char*,7> names{"RSSetViewports","RSSetScissorRects","SetGraphicsRootDescriptorTable","SetGraphicsRoot32BitConstants","SetGraphicsRootConstantBufferView","OMSetRenderTargets","ClearDepthStencilView"};
using Scissor=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,const D3D12_RECT*);
using Constants=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,UINT,const void*,UINT);
using Cbv=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,D3D12_GPU_VIRTUAL_ADDRESS);
using Table=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,D3D12_GPU_DESCRIPTOR_HANDLE);
Scissor original_scissor{};Constants original_constants{};Cbv original_cbv{};Table original_table{};
struct Family {void* vtable{};std::array<void*,7> methods{};void* example{};void* caller{};const char* source{};UINT64 hits{};};
struct Binding {const char* kind{};void* object{};void* caller{};UINT index{},count{},offset{};UINT64 address{},at{};D3D12_RECT rect{};};
struct Projection {void* object{};UINT index{},offset{};std::array<float,16> values{};bool transposed{};UINT64 at{};};
struct State {
    std::atomic<bool> enabled{},installed{};
    std::atomic<UINT64> scissors{},constants{},cbvs{},tables{},external{},contention{},overflow{},errors{},skipped_tv{};
    std::mutex mutex;std::array<void*,7> discovery{};std::array<Family,16> families{};size_t family_count{};
    std::array<Binding,12> bindings{};size_t binding_count{};
    std::array<Projection,4> projections{};size_t projection_count{};
    float native_fov{};bool native_valid{};
    size_t reported_families{};bool discovery_reported{};
};
State& state(){static auto* value=new State;return *value;}
std::string utf8(const wchar_t* value){const int n=WideCharToMultiByte(CP_UTF8,0,value,-1,nullptr,0,nullptr,nullptr);if(n<=1)return {};std::string out(n,'\0');WideCharToMultiByte(CP_UTF8,0,value,-1,out.data(),n,nullptr,nullptr);out.pop_back();return out;}
std::string owner(void* target){MEMORY_BASIC_INFORMATION info{};std::ostringstream out;out<<target;
    if(VirtualQuery(target,&info,sizeof info)){wchar_t path[32768]{};GetModuleFileNameW(static_cast<HMODULE>(info.AllocationBase),path,32768);out<<"@"<<utf8(path)<<"+0x"<<std::hex<<(reinterpret_cast<UINT_PTR>(target)-reinterpret_cast<UINT_PTR>(info.AllocationBase));}return out.str();}
bool perspective(const std::array<float,16>& m){
    for(float v:m)if(!std::isfinite(v)||std::abs(v)>1e6f)return false;
    return m[0]>.01f&&m[5]>.01f&&std::abs(std::abs(m[11])-1)<.001f&&std::abs(m[15])<1e-6f&&
        std::abs(m[1])+std::abs(m[2])+std::abs(m[3])+std::abs(m[4])+std::abs(m[6])+std::abs(m[7])+std::abs(m[12])+std::abs(m[13])<.001f&&std::abs(m[14])>1e-8f;
}
void family_locked(State& s,ID3D12GraphicsCommandList* object,const char* source,void* caller){
    auto** vt=*reinterpret_cast<void***>(object);std::array<void*,7> methods{};for(size_t i=0;i<slots.size();++i)methods[i]=vt[slots[i]];
    for(size_t i=0;i<s.family_count;++i)if(s.families[i].methods==methods){++s.families[i].hits;return;}
    if(s.family_count==s.families.size()){++s.overflow;return;}
    auto& f=s.families[s.family_count++];f.vtable=vt;f.example=object;f.source=source;f.caller=caller;f.hits=1;for(size_t i=0;i<slots.size();++i)f.methods[i]=vt[slots[i]];
}
void binding(const Binding& b) noexcept {try{
    auto& s=state();if(!s.enabled)return;if(tv_scope){++s.skipped_tv;return;}++s.external;
    std::unique_lock lock(s.mutex,std::try_to_lock);if(!lock){++s.contention;return;}
    family_locked(s,static_cast<ID3D12GraphicsCommandList*>(b.object),b.kind,b.caller);
    // Reserve three entries per event kind so one path cannot crowd out the others.
    size_t kind_count{};for(size_t i=0;i<s.binding_count;++i)if(s.bindings[i].kind==b.kind)++kind_count;
    if(kind_count>=3||s.binding_count==s.bindings.size())return;s.bindings[s.binding_count++]=b;
}catch(...){++state().errors;}}
void inline_projection(ID3D12GraphicsCommandList* object,UINT index,UINT count,const void* data,UINT offset) noexcept {try{
    auto& s=state();if(!s.enabled||tv_scope||count<16||!data)return;
    std::array<float,64> values{};const auto limit=std::min<UINT>(count,64);std::memcpy(values.data(),data,limit*sizeof(float));
    for(UINT start=0;start+16<=limit;++start){std::array<float,16> m{};std::copy_n(values.begin()+start,16,m.begin());bool transposed{};
        if(!perspective(m)){std::array<float,16> t{};for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x)t[y*4+x]=m[x*4+y];if(!perspective(t))continue;m=t;transposed=true;}
        std::unique_lock lock(s.mutex,std::try_to_lock);if(!lock){++s.contention;return;}
        if(s.projection_count==s.projections.size())return;
        for(size_t i=0;i<s.projection_count;++i)if(s.projections[i].index==index&&s.projections[i].offset==offset+start&&s.projections[i].values==m)return;
        s.projections[s.projection_count++]={object,index,offset+start,m,transposed,GetTickCount64()};return;
    }
}catch(...){++state().errors;}}
void STDMETHODCALLTYPE scissor_hook(ID3D12GraphicsCommandList* object,UINT count,const D3D12_RECT* rects){
    original_scissor(object,count,rects);const auto error=GetLastError();auto& s=state();const auto n=++s.scissors;
    if((n&63)==1){Binding b{};b.kind="scissor";b.object=object;b.caller=_ReturnAddress();b.count=count;b.at=GetTickCount64();if(count&&rects)b.rect=rects[0];binding(b);}SetLastError(error);
}
void STDMETHODCALLTYPE constants_hook(ID3D12GraphicsCommandList* object,UINT index,UINT count,const void* data,UINT offset){
    original_constants(object,index,count,data,offset);const auto error=GetLastError();if(!tv_scope)native::constants(object,index,count,data,offset);auto& s=state();const auto n=++s.constants;
    if((n&63)==1){Binding b{};b.kind="root_constants";b.object=object;b.caller=_ReturnAddress();b.index=index;b.count=count;b.offset=offset;b.at=GetTickCount64();binding(b);inline_projection(object,index,count,data,offset);}SetLastError(error);
}
void STDMETHODCALLTYPE cbv_hook(ID3D12GraphicsCommandList* object,UINT index,D3D12_GPU_VIRTUAL_ADDRESS address){
    original_cbv(object,index,address);const auto error=GetLastError();if(!tv_scope)native::root_cbv(object,index,address);auto& s=state();const auto n=++s.cbvs;
    if((n&63)==1){Binding b{};b.kind="root_cbv";b.object=object;b.caller=_ReturnAddress();b.index=index;b.address=address;b.at=GetTickCount64();binding(b);}SetLastError(error);
}
void STDMETHODCALLTYPE table_hook(ID3D12GraphicsCommandList* object,UINT index,D3D12_GPU_DESCRIPTOR_HANDLE handle){
    original_table(object,index,handle);const auto error=GetLastError();auto& s=state();const auto n=++s.tables;
    if((n&63)==1){Binding b{};b.kind="root_table";b.object=object;b.caller=_ReturnAddress();b.index=index;b.address=handle.ptr;b.at=GetTickCount64();binding(b);}SetLastError(error);
}
}
TvScope::TvScope() noexcept:previous(tv_scope){tv_scope=true;}TvScope::~TvScope(){tv_scope=previous;}
bool is_tv() noexcept {return tv_scope;}
void live_constants(ID3D12GraphicsCommandList* object,UINT index,UINT count,const void* data,UINT offset) noexcept {auto& s=state();if((++s.constants&63)!=1)return;Binding b{};b.kind="live_root_constants";b.object=object;b.index=index;b.count=count;b.offset=offset;b.at=GetTickCount64();binding(b);inline_projection(object,index,count,data,offset);}
void live_cbv(ID3D12GraphicsCommandList* object,UINT index,D3D12_GPU_VIRTUAL_ADDRESS address) noexcept {auto& s=state();if((++s.cbvs&63)!=1)return;Binding b{};b.kind="live_root_cbv";b.object=object;b.index=index;b.address=address;b.at=GetTickCount64();binding(b);}
void live_table(ID3D12GraphicsCommandList* object,UINT index,D3D12_GPU_DESCRIPTOR_HANDLE handle) noexcept {auto& s=state();if((++s.tables&63)!=1)return;Binding b{};b.kind="live_root_table";b.object=object;b.index=index;b.address=handle.ptr;b.at=GetTickCount64();binding(b);}
void active(bool value) noexcept {state().enabled=value;}
void list(ID3D12GraphicsCommandList* object,const char* source,void* caller) noexcept {try{
    auto& s=state();if(!s.enabled||tv_scope||!object)return;std::unique_lock lock(s.mutex,std::try_to_lock);if(!lock){++s.contention;return;}family_locked(s,object,source,caller);
}catch(...){++state().errors;}}
void frame(const Scene& scene) noexcept {try{auto& s=state();std::unique_lock lock(s.mutex,std::try_to_lock);if(!lock){++s.contention;return;}s.native_fov=scene.camera.vertical_fov;s.native_valid=scene.camera.valid;}catch(...){++state().errors;}}
void initialize(ID3D12GraphicsCommandList* object) noexcept {HookTransaction hooks;try{
    auto& s=state();auto** vt=*reinterpret_cast<void***>(object);{std::lock_guard lock(s.mutex);for(size_t i=0;i<slots.size();++i)s.discovery[i]=vt[slots[i]];}
    hooks.add(vt[22],reinterpret_cast<void*>(&scissor_hook),reinterpret_cast<void**>(&original_scissor));
    hooks.add(vt[36],reinterpret_cast<void*>(&constants_hook),reinterpret_cast<void**>(&original_constants));
    hooks.add(vt[38],reinterpret_cast<void*>(&cbv_hook),reinterpret_cast<void**>(&original_cbv));
    hooks.add(vt[32],reinterpret_cast<void*>(&table_hook),reinterpret_cast<void**>(&original_table));
    hooks.commit();s.installed=true;log("Depth live-list probe installed: scissor/root constants/root CBV/root table; no live-target rehooking, GPU reads or projection substitution");
}catch(const std::exception& e){log(std::string("Depth live-list probe unavailable: ")+e.what());}catch(...){log("Depth live-list probe unavailable: unexpected initialization failure");}}
void report() noexcept {if(!debug_logging())return;try{
    auto& s=state();std::array<Family,16> families{};std::array<Binding,12> bindings{};std::array<Projection,4> projections{};std::array<void*,7> discovery{};size_t nf{},nb{},np{},first{};float fov{};bool valid{},print_discovery{};
    {std::lock_guard lock(s.mutex);nf=s.family_count;first=s.reported_families;s.reported_families=nf;print_discovery=s.installed&&!s.discovery_reported;if(print_discovery)s.discovery_reported=true;discovery=s.discovery;nb=s.binding_count;np=s.projection_count;families=s.families;bindings=s.bindings;projections=s.projections;s.binding_count=0;s.projection_count=0;fov=s.native_fov;valid=s.native_valid;}
    std::ostringstream summary;summary<<"Depth live-list probe: build=native-proto3 installed="<<s.installed.load()<<" scissor_calls_total="<<s.scissors.load()<<" root_constants_calls_total="<<s.constants.load()<<" root_cbv_calls_total="<<s.cbvs.load()<<" root_table_calls_total="<<s.tables.load()<<" external_samples="<<s.external.load()<<" tv_samples_skipped="<<s.skipped_tv.load()<<" families="<<nf<<" contention_drops="<<s.contention.load()<<" family_overflow="<<s.overflow.load()<<" errors="<<s.errors.load()<<" inline_projection_candidates="<<np<<" sampling=1_in_64 first_64_dwords_only=1 constant_buffer_contents=upload_root_cbv_capture_separate_summary";log(summary.str());
    if(print_discovery)for(size_t i=0;i<slots.size();++i){std::ostringstream row;row<<"Depth discovery method: name="<<names[i]<<" target="<<owner(discovery[i]);log(row.str());}
    for(size_t i=first;i<nf;++i){const auto& f=families[i];std::ostringstream row;row<<"Depth live-list family: vtable="<<f.vtable<<" example="<<f.example<<" first_source="<<f.source<<" sampled_hits="<<f.hits<<" first_caller="<<owner(f.caller);log(row.str());
        for(size_t j=0;j<slots.size();++j){std::ostringstream method;method<<"Depth live method: vtable="<<f.vtable<<" name="<<names[j]<<" same_as_discovery="<<(f.methods[j]==discovery[j])<<" target="<<owner(f.methods[j]);log(method.str());}}
    const auto now=GetTickCount64();for(size_t i=0;i<nb;++i){const auto& b=bindings[i];std::ostringstream row;row<<"Depth binding path: kind="<<b.kind<<" object="<<b.object<<" caller="<<owner(b.caller)<<" root_index="<<b.index<<" count="<<b.count<<" offset="<<b.offset<<" gpu_address_or_handle=0x"<<std::hex<<b.address<<std::dec<<" first_scissor="<<b.rect.left<<','<<b.rect.top<<','<<b.rect.right<<','<<b.rect.bottom<<" age_ms="<<now-b.at;log(row.str());}
    for(size_t i=0;i<np;++i){const auto& p=projections[i];std::ostringstream row;row<<"Depth inline projection candidate: validated=0 object="<<p.object<<" root_index="<<p.index<<" dword_offset="<<p.offset<<" transposed_to_rows="<<p.transposed<<" age_ms="<<now-p.at<<" native_camera_valid="<<valid<<" native_fov="<<fov;
        if(valid&&fov>1&&fov<175)row<<" projection_y_over_native="<<p.values[5]*std::tan(fov*3.14159265359/360);row<<" matrix=";for(float v:p.values)row<<v<<',';log(row.str());}
}catch(...){++state().errors;}}
#ifdef TV_GPU_TEST
bool test_contracts() noexcept {
    std::array<float,16> p{1,0,0,0,0,1,0,0,0,0,0,-1,0,0,.1f,0};if(!perspective(p))return false;p[15]=1;if(perspective(p))return false;p[15]=0;p[2]=10;if(perspective(p))return false;
    const auto before=tv_scope;{TvScope a;if(!tv_scope)return false;{TvScope b;if(!tv_scope)return false;}if(!tv_scope)return false;}return tv_scope==before;
}
bool test_hooks() noexcept {auto& s=state();std::lock_guard lock(s.mutex);return s.installed&&s.scissors>0&&s.constants>0&&s.cbvs>0&&s.tables>0&&s.errors==0&&s.family_count>0&&s.projection_count>0;}
#endif
}
