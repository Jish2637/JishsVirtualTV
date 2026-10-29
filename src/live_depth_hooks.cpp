#include "live_depth_hooks.h"
#include "native_depth.h"
#include "depth_probe.h"
#include "depth_tracking.h"
#include "hook_transaction.h"
#include <array>
#include <mutex>
#include <sstream>
#include <atomic>
#include <algorithm>
namespace virtualtv::depth::live {
namespace {
constexpr std::array<unsigned,11> slots{21,36,38,32,12,13,30,34,11,27,59};
using Viewport=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,const D3D12_VIEWPORT*);
using Constants=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,UINT,const void*,UINT);
using Cbv=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,D3D12_GPU_VIRTUAL_ADDRESS);
using Table=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,D3D12_GPU_DESCRIPTOR_HANDLE);
using Draw=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,UINT,UINT,UINT);
using Indexed=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,UINT,UINT,INT,UINT);
using Signature=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,ID3D12RootSignature*);
using Single=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,UINT,UINT);
using ClearState=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,ID3D12PipelineState*);
using Bundle=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,ID3D12GraphicsCommandList*);
using Indirect=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,ID3D12CommandSignature*,UINT,ID3D12Resource*,UINT64,ID3D12Resource*,UINT64);
using BeginPass=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList4*,UINT,const D3D12_RENDER_PASS_RENDER_TARGET_DESC*,const D3D12_RENDER_PASS_DEPTH_STENCIL_DESC*,D3D12_RENDER_PASS_FLAGS);
using EndPass=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList4*);
struct Family {std::array<void*,13> targets{};std::array<bool,13> hooked{};bool attempted{},ready{};Viewport viewport{};Constants constants{};Cbv cbv{};Table table{};Draw draw{};Indexed indexed{};Signature signature{};Single single{};ClearState clear{};Bundle bundle{};Indirect indirect{};BeginPass begin{};EndPass end{};};
struct State {std::mutex mutex;std::array<Family,4> families{};std::array<void*,13> discovery{};unsigned count{};std::atomic<unsigned> errors{},overflow{};bool initialized{};std::atomic<bool> stopped{};};
State& state(){static auto* s=new State;return *s;}
std::array<void*,13> methods(ID3D12GraphicsCommandList* object){std::array<void*,13> result{};auto** vt=*reinterpret_cast<void***>(object);for(unsigned i=0;i<slots.size();++i)result[i]=vt[slots[i]];
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> pass;if(SUCCEEDED(object->QueryInterface(IID_PPV_ARGS(&pass)))){auto** pv=*reinterpret_cast<void***>(pass.Get());result[11]=pv[68];result[12]=pv[69];}return result;}
template<unsigned I> void STDMETHODCALLTYPE viewport(ID3D12GraphicsCommandList* l,UINT n,const D3D12_VIEWPORT* v){state().families[I].viewport(l,n,v);const auto error=GetLastError();if(!probe::is_tv()){tracking::viewport(l,n,v);native::viewport(l,n,v);}SetLastError(error);}
template<unsigned I> void STDMETHODCALLTYPE constants(ID3D12GraphicsCommandList* l,UINT index,UINT n,const void* v,UINT offset){state().families[I].constants(l,index,n,v,offset);const auto error=GetLastError();if(!probe::is_tv()){native::constants(l,index,n,v,offset);probe::live_constants(l,index,n,v,offset);}SetLastError(error);}
template<unsigned I> void STDMETHODCALLTYPE cbv(ID3D12GraphicsCommandList* l,UINT index,D3D12_GPU_VIRTUAL_ADDRESS address){state().families[I].cbv(l,index,address);const auto error=GetLastError();if(!probe::is_tv()){native::root_cbv(l,index,address);probe::live_cbv(l,index,address);}SetLastError(error);}
template<unsigned I> void STDMETHODCALLTYPE table(ID3D12GraphicsCommandList* l,UINT index,D3D12_GPU_DESCRIPTOR_HANDLE h){state().families[I].table(l,index,h);const auto error=GetLastError();if(!probe::is_tv())probe::live_table(l,index,h);SetLastError(error);}
template<unsigned I> void STDMETHODCALLTYPE draw(ID3D12GraphicsCommandList* l,UINT a,UINT b,UINT c,UINT d){state().families[I].draw(l,a,b,c,d);const auto error=GetLastError();if(!probe::is_tv())native::draw(l,a,b);SetLastError(error);}
template<unsigned I> void STDMETHODCALLTYPE indexed(ID3D12GraphicsCommandList* l,UINT a,UINT b,UINT c,INT d,UINT e){state().families[I].indexed(l,a,b,c,d,e);const auto error=GetLastError();if(!probe::is_tv())native::draw(l,a,b);SetLastError(error);}
template<unsigned I> void STDMETHODCALLTYPE signature(ID3D12GraphicsCommandList* l,ID3D12RootSignature* v){state().families[I].signature(l,v);const auto error=GetLastError();if(!probe::is_tv())native::signature(l,v);SetLastError(error);}
template<unsigned I> void STDMETHODCALLTYPE single(ID3D12GraphicsCommandList* l,UINT index,UINT value,UINT offset){state().families[I].single(l,index,value,offset);const auto error=GetLastError();if(!probe::is_tv())native::constants(l,index,1,&value,offset);SetLastError(error);}
template<unsigned I> void STDMETHODCALLTYPE clear(ID3D12GraphicsCommandList* l,ID3D12PipelineState* p){state().families[I].clear(l,p);const auto error=GetLastError();if(!probe::is_tv())native::clear_state(l);SetLastError(error);}
template<unsigned I> void STDMETHODCALLTYPE bundle(ID3D12GraphicsCommandList* l,ID3D12GraphicsCommandList* p){state().families[I].bundle(l,p);const auto error=GetLastError();if(!probe::is_tv())native::clear_state(l);SetLastError(error);}
template<unsigned I> void STDMETHODCALLTYPE indirect(ID3D12GraphicsCommandList* l,ID3D12CommandSignature* p,UINT n,ID3D12Resource* args,UINT64 offset,ID3D12Resource* counts,UINT64 count_offset){state().families[I].indirect(l,p,n,args,offset,counts,count_offset);const auto error=GetLastError();if(!probe::is_tv())native::clear_state(l);SetLastError(error);}
template<unsigned I> void STDMETHODCALLTYPE begin_pass(ID3D12GraphicsCommandList4* l,UINT n,const D3D12_RENDER_PASS_RENDER_TARGET_DESC* targets,const D3D12_RENDER_PASS_DEPTH_STENCIL_DESC* depth,D3D12_RENDER_PASS_FLAGS flags){
    const auto error=GetLastError();if(!probe::is_tv()){native::before_bind(l,depth?depth->cpuDescriptor:D3D12_CPU_DESCRIPTOR_HANDLE{});native::begin_pass(l,depth,flags);}SetLastError(error);state().families[I].begin(l,n,targets,depth,flags);
}
template<unsigned I> void STDMETHODCALLTYPE end_pass(ID3D12GraphicsCommandList4* l){state().families[I].end(l);const auto error=GetLastError();if(!probe::is_tv())native::end_pass(l);SetLastError(error);}
template<unsigned I> void install(){
    auto& s=state();auto& f=s.families[I];f.attempted=true;HookTransaction a,b,c,d;
    const std::array<void*,13> detours{reinterpret_cast<void*>(&viewport<I>),reinterpret_cast<void*>(&constants<I>),reinterpret_cast<void*>(&cbv<I>),reinterpret_cast<void*>(&table<I>),reinterpret_cast<void*>(&draw<I>),reinterpret_cast<void*>(&indexed<I>),reinterpret_cast<void*>(&signature<I>),reinterpret_cast<void*>(&single<I>),reinterpret_cast<void*>(&clear<I>),reinterpret_cast<void*>(&bundle<I>),reinterpret_cast<void*>(&indirect<I>),reinterpret_cast<void*>(&begin_pass<I>),reinterpret_cast<void*>(&end_pass<I>)};
    const std::array<void**,13> originals{reinterpret_cast<void**>(&f.viewport),reinterpret_cast<void**>(&f.constants),reinterpret_cast<void**>(&f.cbv),reinterpret_cast<void**>(&f.table),reinterpret_cast<void**>(&f.draw),reinterpret_cast<void**>(&f.indexed),reinterpret_cast<void**>(&f.signature),reinterpret_cast<void**>(&f.single),reinterpret_cast<void**>(&f.clear),reinterpret_cast<void**>(&f.bundle),reinterpret_cast<void**>(&f.indirect),reinterpret_cast<void**>(&f.begin),reinterpret_cast<void**>(&f.end)};
    std::array<bool,13> added{};
    try{for(unsigned n=0;n<f.targets.size();++n){
        if(n>=11&&!f.targets[n])continue;
        // Existing discovery hooks already feed these four event types.
        if(n<4&&f.targets[n]==s.discovery[n])continue;
        bool shared{};for(unsigned k=0;k<s.count;++k)if(k!=I&&s.families[k].hooked[n]&&s.families[k].targets[n]==f.targets[n])shared=true;if(shared)continue;
        MEMORY_BASIC_INFORMATION info{};if(!VirtualQuery(f.targets[n],&info,sizeof info)||info.Type!=MEM_IMAGE||!(info.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))throw std::runtime_error("live method is not executable image memory");
        (n<4?a:n<8?b:n<11?c:d).add(f.targets[n],detours[n],originals[n]);added[n]=true;
    }
    a.commit();for(unsigned n=0;n<4;++n)if(added[n])f.hooked[n]=true;
    b.commit();for(unsigned n=4;n<8;++n)if(added[n])f.hooked[n]=true;
    c.commit();for(unsigned n=8;n<11;++n)if(added[n])f.hooked[n]=true;
    d.commit();for(unsigned n=11;n<13;++n)if(added[n])f.hooked[n]=true;
    std::array<void*,11> base{};std::copy_n(f.targets.begin(),11,base.begin());native::approve(base);f.ready=true;
    std::ostringstream out;out<<"Native live hooks committed: family="<<I<<" grouped_by_method_targets=1 viewport="<<f.targets[0]<<" constants="<<f.targets[1]<<" cbv="<<f.targets[2]<<" table="<<f.targets[3]<<" draw="<<f.targets[4]<<" indexed="<<f.targets[5]<<" signature="<<f.targets[6];debug_log(out.str());
    }catch(const std::exception& e){++s.errors;log(std::string("Native live hooks incomplete; playback preserved: ")+e.what());}
}
}
void initialize(ID3D12GraphicsCommandList* object) noexcept {try{auto& s=state();std::lock_guard lock(s.mutex);s.discovery=methods(object);s.initialized=true;}catch(...){++state().errors;}}
void observe(ID3D12GraphicsCommandList* object) noexcept {try{
    if(!object||probe::is_tv()||state().stopped)return;auto& s=state();std::unique_lock lock(s.mutex,std::try_to_lock);if(!lock||!s.initialized)return;
    const auto targets=methods(object);
    for(unsigned i=0;i<s.count;++i)if(s.families[i].targets==targets)return;
    if(s.count==s.families.size()){++s.overflow;return;}s.families[s.count++].targets=targets;
}catch(...){++state().errors;}}
void maintain() noexcept {try{
    auto& s=state();if(s.stopped)return;std::lock_guard lock(s.mutex);
    // At most one previously discovered family per maintenance tick. No diagnostics required.
    for(unsigned i=0;i<s.count;++i)if(!s.families[i].attempted){switch(i){case 0:install<0>();break;case 1:install<1>();break;case 2:install<2>();break;case 3:install<3>();break;}break;}
}catch(...){++state().errors;}}
void shutdown() noexcept {state().stopped=true;}
#ifdef TV_GPU_TEST
unsigned test_ready() noexcept {auto& s=state();std::lock_guard lock(s.mutex);unsigned ready{};for(unsigned i=0;i<s.count;++i)ready+=s.families[i].ready;return ready;}
#endif
void report() noexcept {if(!debug_logging())return;try{
    auto& s=state();std::lock_guard lock(s.mutex);
    std::ostringstream out;out<<"Native live hooks: families="<<s.count<<" overflow="<<s.overflow.load()<<" errors="<<s.errors.load();debug_log(out.str());
}catch(...){++state().errors;}}
}
