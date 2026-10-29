#include "depth_observer.h"
#include "depth_capture.h"
#include "depth_tracking.h"
#include "depth_probe.h"
#include "native_depth.h"
#include "live_depth_hooks.h"
#include "cbv_capture.h"
#include <intrin.h>
#include "dx12_native.h"
#include "hook_transaction.h"
#include "tv_runtime.h"
#include <dxgi1_4.h>
#include <atomic>
#include <mutex>
#include <array>
#include <sstream>
#include <cstring>
#include <cstddef>
#include <algorithm>

namespace virtualtv::depth::observer {
using Microsoft::WRL::ComPtr;
namespace {
using Barrier=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,const D3D12_RESOURCE_BARRIER*);
using Bind=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,const D3D12_CPU_DESCRIPTOR_HANDLE*,BOOL,const D3D12_CPU_DESCRIPTOR_HANDLE*);
using Clear=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,D3D12_CPU_DESCRIPTOR_HANDLE,D3D12_CLEAR_FLAGS,FLOAT,UINT8,UINT,const D3D12_RECT*);
using Create=void(STDMETHODCALLTYPE*)(ID3D12Device*,ID3D12Resource*,const D3D12_DEPTH_STENCIL_VIEW_DESC*,D3D12_CPU_DESCRIPTOR_HANDLE);
Barrier original_barrier{};Bind original_bind{};Clear original_clear{};Create original_create{};
using Record=tracking::Record;
struct State {
    std::atomic<bool> active{},installed{},attempted{};
    std::atomic<UINT64> barriers{},sampled{},bindings{},clears{},creates{},errors{},dropped{};

};
State& state(){static auto* value=new State;return *value;}
// Copy metadata only. No resources, descriptor contents, or command lists are retained.
void remember(const Record& r){tracking::record(r);}
void STDMETHODCALLTYPE barrier_hook(ID3D12GraphicsCommandList* list,UINT count,const D3D12_RESOURCE_BARRIER* items){
    original_barrier(list,count,items);const auto error=GetLastError();auto& s=state();
    if(!probe::is_tv())native::barriers(list,count,items);
    if(s.active){const auto batch=++s.barriers;
        // At most eight GetDesc calls per 256 barrier batches, while TV occlusion is enabled.
        if((batch&255)==1&&items&&count)try{++s.sampled;const UINT start=UINT((batch>>8)%count);for(UINT i=0;i<count&&i<8;++i){const auto& b=items[(start+i)%count];
            if(b.Type!=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION||!b.Transition.pResource)continue;
            const auto desc=b.Transition.pResource->GetDesc();
            if(!(desc.Flags&D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))continue;
            Record r{};r.event="transition";r.object=list;r.resource=b.Transition.pResource;r.desc=desc;
            r.before=b.Transition.StateBefore;r.after=b.Transition.StateAfter;r.subresource=b.Transition.Subresource;r.flags=b.Flags;remember(r);
        }}catch(...){++s.errors;}
    }SetLastError(error);
}
void STDMETHODCALLTYPE bind_hook(ID3D12GraphicsCommandList* list,UINT count,const D3D12_CPU_DESCRIPTOR_HANDLE* targets,BOOL range,const D3D12_CPU_DESCRIPTOR_HANDLE* depth){
    const auto entering=GetLastError();if(!probe::is_tv())native::before_bind(list,depth?*depth:D3D12_CPU_DESCRIPTOR_HANDLE{});SetLastError(entering);
    original_bind(list,count,targets,range,depth);const auto error=GetLastError();auto& s=state();
    if(!probe::is_tv())native::bind(list,depth?*depth:D3D12_CPU_DESCRIPTOR_HANDLE{});
    if(s.active&&depth){const auto n=++s.bindings;if((n&63)==1)try{probe::list(list,"depth_bind",_ReturnAddress());live::observe(list);Record r{};r.event="bind";r.object=list;r.handle=depth->ptr;remember(r);}catch(...){++s.errors;}}SetLastError(error);
}
void STDMETHODCALLTYPE clear_hook(ID3D12GraphicsCommandList* list,D3D12_CPU_DESCRIPTOR_HANDLE handle,D3D12_CLEAR_FLAGS flags,FLOAT depth,UINT8 stencil,UINT count,const D3D12_RECT* rects){
    const auto entering=GetLastError();if(!probe::is_tv())native::before_clear(list,handle,flags);SetLastError(entering);
    original_clear(list,handle,flags,depth,stencil,count,rects);const auto error=GetLastError();auto& s=state();
    if(!probe::is_tv())native::clear(list,handle,flags,depth,count);
    if(s.active){const auto n=++s.clears;if((n&15)==1)try{probe::list(list,"depth_clear",_ReturnAddress());live::observe(list);Record r{};r.event="clear";r.object=list;r.handle=handle.ptr;r.flags=flags;r.clear=depth;remember(r);}catch(...){++s.errors;}}SetLastError(error);
}
void STDMETHODCALLTYPE create_hook(ID3D12Device* device,ID3D12Resource* resource,const D3D12_DEPTH_STENCIL_VIEW_DESC* view,D3D12_CPU_DESCRIPTOR_HANDLE handle){
    original_create(device,resource,view,handle);const auto error=GetLastError();auto& s=state();
    // Also observe creation before attachment; existing descriptors may predate installation.
    ++s.creates;try{Record r{};r.event="create_dsv";r.object=device;r.handle=handle.ptr;r.resource=resource;
        if(resource)r.desc=resource->GetDesc();r.after=view?UINT(view->Format):0;r.explicit_view=view!=nullptr;if(view)r.view=*view;remember(r);
    }catch(...){++s.errors;}SetLastError(error);
}
using Viewport=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,const D3D12_VIEWPORT*);
using Copy=void(STDMETHODCALLTYPE*)(ID3D12Device*,UINT,const D3D12_CPU_DESCRIPTOR_HANDLE*,const UINT*,UINT,const D3D12_CPU_DESCRIPTOR_HANDLE*,const UINT*,D3D12_DESCRIPTOR_HEAP_TYPE);
using CopySimple=void(STDMETHODCALLTYPE*)(ID3D12Device*,UINT,D3D12_CPU_DESCRIPTOR_HANDLE,D3D12_CPU_DESCRIPTOR_HANDLE,D3D12_DESCRIPTOR_HEAP_TYPE);
Viewport original_viewport{};Copy original_copy{};CopySimple original_copy_simple{};
void STDMETHODCALLTYPE viewport_hook(ID3D12GraphicsCommandList* list,UINT count,const D3D12_VIEWPORT* values){original_viewport(list,count,values);const auto error=GetLastError();if(state().active)tracking::viewport(list,count,values);if(!probe::is_tv())native::viewport(list,count,values);SetLastError(error);}
void STDMETHODCALLTYPE copy_hook(ID3D12Device* device,UINT nd,const D3D12_CPU_DESCRIPTOR_HANDLE* dst,const UINT* ds,UINT ns,const D3D12_CPU_DESCRIPTOR_HANDLE* src,const UINT* ss,D3D12_DESCRIPTOR_HEAP_TYPE type){original_copy(device,nd,dst,ds,ns,src,ss,type);const auto error=GetLastError();if(type==D3D12_DESCRIPTOR_HEAP_TYPE_DSV)tracking::copy_ranges(device,nd,dst,ds,ns,src,ss);SetLastError(error);}
void STDMETHODCALLTYPE copy_simple_hook(ID3D12Device* device,UINT count,D3D12_CPU_DESCRIPTOR_HANDLE dst,D3D12_CPU_DESCRIPTOR_HANDLE src,D3D12_DESCRIPTOR_HEAP_TYPE type){original_copy_simple(device,count,dst,src,type);const auto error=GetLastError();if(type==D3D12_DESCRIPTOR_HEAP_TYPE_DSV)tracking::copy_simple(device,count,dst,src);SetLastError(error);}
void auxiliary(ID3D12GraphicsCommandList* list,ID3D12Device* device){HookTransaction hooks;try{
    auto** vt=*reinterpret_cast<void***>(list);auto** dv=*reinterpret_cast<void***>(device);
    hooks.add(vt[21],reinterpret_cast<void*>(&viewport_hook),reinterpret_cast<void**>(&original_viewport));
    hooks.add(dv[23],reinterpret_cast<void*>(&copy_hook),reinterpret_cast<void**>(&original_copy));
    hooks.add(dv[24],reinterpret_cast<void*>(&copy_simple_hook),reinterpret_cast<void**>(&original_copy_simple));
    hooks.commit();log("Depth correlation hooks installed: RSSetViewports, CopyDescriptors, CopyDescriptorsSimple; reset/submission observers share the existing depth hooks");
}catch(const std::exception& e){log(std::string("Depth correlation hooks unavailable: ")+e.what());}}
std::string utf8(const wchar_t* value){const int n=WideCharToMultiByte(CP_UTF8,0,value,-1,nullptr,0,nullptr,nullptr);if(n<=1)return {};std::string result(n,'\0');WideCharToMultiByte(CP_UTF8,0,value,-1,result.data(),n,nullptr,nullptr);result.pop_back();return result;}
void module(const wchar_t* name){const auto handle=GetModuleHandleW(name);std::ostringstream out;out<<"Depth module: name="<<utf8(name)<<" loaded="<<bool(handle);if(handle){wchar_t path[32768]{};GetModuleFileNameW(handle,path,32768);out<<" base="<<handle<<" path="<<utf8(path);}log(out.str());}
void address(const char* name,void* target){MEMORY_BASIC_INFORMATION info{};const auto queried=VirtualQuery(target,&info,sizeof info);wchar_t path[32768]{};if(queried)GetModuleFileNameW(static_cast<HMODULE>(info.AllocationBase),path,32768);std::ostringstream out;out<<"Depth hook target: name="<<name<<" address="<<target<<" owner="<<utf8(path)<<" rva=0x"<<std::hex<<(queried?reinterpret_cast<UINT_PTR>(target)-reinterpret_cast<UINT_PTR>(info.AllocationBase):0);log(out.str());}
}
void activate(bool value) noexcept {state().active=value;probe::active(value);}
void report() noexcept {if(!debug_logging())return;try{
    auto& s=state();
    std::ostringstream out;out<<"Native depth observer: build=native-proto3 installed="<<s.installed.load()<<" active="<<s.active.load()<<" barrier_batches="<<s.barriers.load()<<" sampled_batches="<<s.sampled.load()<<" dsv_bindings="<<s.bindings.load()<<" depth_clears="<<s.clears.load()<<" dsv_creates="<<s.creates.load()<<" observer_errors="<<s.errors.load()<<" sampling=barrier_1_in_256_max_8_bind_1_in_64_clear_1_in_16";log(out.str());
    tracking::report();
    probe::report();
    live::report();native::report();log(cbv::status());
    for(const auto* name:{L"sl.interposer.dll",L"sl.common.dll",L"sl.dlss.dll",L"sl.dlss_g.dll",L"amd_fidelityfx_dx12.dll",L"ffx_fsr2_api_x64.dll",L"ffx_fsr3upscaler_x64.dll"})module(name);
}catch(...){++state().errors;}}
void initialize(ID3D12CommandQueue* queue,ID3D12GraphicsCommandList* list) noexcept {
    if(state().attempted.exchange(true))return;
    HookTransaction hooks;try{
        bool uq{},ul{},ud{};auto native_queue=native_interface(queue,uq);auto native_list=native_interface(list,ul);ComPtr<ID3D12Device> device;
        const auto hr=native_queue->GetDevice(IID_PPV_ARGS(&device));if(FAILED(hr))throw std::runtime_error("observer GetDevice failed");device=native_interface(device.Get(),ud);
        const auto luid=device->GetAdapterLuid();ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter1> adapter;DXGI_ADAPTER_DESC1 desc{};
        HRESULT adapter_hr=CreateDXGIFactory1(IID_PPV_ARGS(&factory));if(SUCCEEDED(adapter_hr))adapter_hr=factory->EnumAdapterByLuid(luid,IID_PPV_ARGS(&adapter));if(SUCCEEDED(adapter_hr))adapter_hr=adapter->GetDesc1(&desc);
        std::ostringstream out;out<<"Depth environment: build=native-proto3 adapter_hr="<<adapter_hr<<" adapter="<<utf8(desc.Description)<<" vendor=0x"<<std::hex<<desc.VendorId<<" device=0x"<<desc.DeviceId<<" luid="<<luid.HighPart<<':'<<luid.LowPart<<std::dec<<" dedicated_bytes="<<desc.DedicatedVideoMemory<<" queue="<<native_queue.Get()<<" list="<<native_list.Get()<<" device_object="<<device.Get()<<" queue_unwrapped="<<uq<<" list_unwrapped="<<ul<<" device_unwrapped="<<ud;log(out.str());
#ifndef TV_GPU_TEST
        // Apply adapter defaults at hook initialization, so F8 shows the
        // requested non-NVIDIA defaults even before a TV is attached.
        if(SUCCEEDED(adapter_hr))configure_backend_for_adapter(desc.VendorId);
#endif
        auto** vt=*reinterpret_cast<void***>(native_list.Get());auto** dv=*reinterpret_cast<void***>(device.Get());
        // Slots from the Windows SDK's ID3D12GraphicsCommandList and ID3D12Device ABI.
        address("ResourceBarrier",vt[26]);address("OMSetRenderTargets",vt[46]);address("ClearDepthStencilView",vt[47]);address("CreateDepthStencilView",dv[21]);
        hooks.add(vt[26],reinterpret_cast<void*>(&barrier_hook),reinterpret_cast<void**>(&original_barrier));
        hooks.add(vt[46],reinterpret_cast<void*>(&bind_hook),reinterpret_cast<void**>(&original_bind));
        hooks.add(vt[47],reinterpret_cast<void*>(&clear_hook),reinterpret_cast<void**>(&original_clear));
        hooks.add(dv[21],reinterpret_cast<void*>(&create_hook),reinterpret_cast<void**>(&original_create));
        live::initialize(native_list.Get());hooks.commit();state().installed=true;auxiliary(native_list.Get(),device.Get());probe::initialize(native_list.Get());cbv::initialize(device.Get());live::observe(native_list.Get());log("Native depth observer installed: native-proto3 may insert guarded depth copies; legacy barriers only");
    }catch(const std::exception& e){log(std::string("Native depth observer unavailable: ")+e.what());}catch(...){log("Native depth observer unavailable: unexpected initialization failure");}
}
#ifdef TV_GPU_TEST
bool test_observed() noexcept {auto& s=state();return s.installed&&s.barriers>0&&s.sampled>0&&s.bindings>0&&s.clears>0&&s.creates>0&&s.errors==0&&tracking::test_correlated()&&probe::test_hooks();}
#endif
}
