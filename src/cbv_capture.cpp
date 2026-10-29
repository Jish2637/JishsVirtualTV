#include "cbv_capture.h"
#include "hook_transaction.h"
#include "depth_probe.h"
#include <d3d12sdklayers.h>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <sstream>

namespace virtualtv::depth::cbv {
using Microsoft::WRL::ComPtr;
namespace {
using Address=D3D12_GPU_VIRTUAL_ADDRESS(STDMETHODCALLTYPE*)(ID3D12Resource*);
using Map=HRESULT(STDMETHODCALLTYPE*)(ID3D12Resource*,UINT,const D3D12_RANGE*,void**);
Address original_address{};Map original_map{};
thread_local bool internal{};
struct Buffer {ComPtr<ID3D12Resource> resource;UINT64 address{},bytes{},used{};};
struct Layout {ComPtr<ID3D12RootSignature> signature;UINT index{};std::vector<UINT64> offsets;};
struct State {
    std::mutex mutex;std::vector<Buffer> buffers;UINT64 bytes{};
    std::atomic<UINT64> reads{},misses{},unsupported{},candidates{},changes{},errors{},evictions{};
    std::atomic<bool> installed{};
    std::vector<Layout> layouts;std::atomic<UINT64> hint_hits{},full_scans{},budget_skips{};
};
State& state(){static auto* value=new State;return *value;}
struct Guard {bool old=internal;Guard(){internal=true;}~Guard(){internal=old;}};
void remember(ID3D12Resource* resource,UINT64 address) noexcept {try{
    if(!address||internal||probe::is_tv())return;Guard guard;
    const auto desc=resource->GetDesc();D3D12_HEAP_PROPERTIES heap{};D3D12_HEAP_FLAGS flags{};
    if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER||desc.Width<256||desc.Width>128ull*1024*1024||
       FAILED(resource->GetHeapProperties(&heap,&flags))||heap.Type!=D3D12_HEAP_TYPE_UPLOAD)return;
    auto& s=state();ComPtr<ID3D12Resource> retired;
    // Retaining each catalog entry prevents resource destruction/address reuse.
    // Evict outside the lock; a capture snapshot keeps its own reference.
    std::unique_lock lock(s.mutex);
    for(auto& b:s.buffers)if(b.resource.Get()==resource){b.used=GetTickCount64();return;}
    while(s.buffers.size()>=128||s.bytes+desc.Width>128ull*1024*1024){
        auto oldest=std::min_element(s.buffers.begin(),s.buffers.end(),[](const Buffer& a,const Buffer& b){return a.used<b.used;});
        if(oldest==s.buffers.end())return;s.bytes-=oldest->bytes;retired=std::move(oldest->resource);s.buffers.erase(oldest);++s.evictions;
        lock.unlock();retired.Reset();lock.lock();
    }
    s.buffers.push_back({resource,address,desc.Width,GetTickCount64()});s.bytes+=desc.Width;
}catch(...){++state().errors;}}
UINT64 STDMETHODCALLTYPE address_hook(ID3D12Resource* resource){const auto value=original_address(resource);const auto error=GetLastError();remember(resource,value);SetLastError(error);return value;}
HRESULT STDMETHODCALLTYPE map_hook(ID3D12Resource* resource,UINT subresource,const D3D12_RANGE* range,void** data){
    const auto hr=original_map(resource,subresource,range,data);const auto error=GetLastError();
    if(SUCCEEDED(hr)&&!internal&&!probe::is_tv())remember(resource,resource->GetGPUVirtualAddress());SetLastError(error);return hr;
}
bool perspective(const std::array<float,16>& m){
    for(float v:m)if(!std::isfinite(v)||std::abs(v)>1e6f)return false;
    return m[0]>.01f&&m[5]>.01f&&std::abs(std::abs(m[11])-1)<.001f&&std::abs(m[15])<1e-6f&&
        std::abs(m[1])+std::abs(m[2])+std::abs(m[3])+std::abs(m[4])+std::abs(m[6])+std::abs(m[7])+std::abs(m[12])+std::abs(m[13])<.001f&&
        std::abs(m[8])+std::abs(m[9])<.02f&&std::abs(m[10])<=1.1f&&std::abs(m[14])>1e-6f&&std::abs(m[14])<10;
}
bool copy(ID3D12Resource* resource,UINT64 offset,void* dst,size_t bytes){
    Guard guard;void* mapped{};D3D12_RANGE range{SIZE_T(offset),SIZE_T(offset+bytes)};
    if(FAILED(resource->Map(0,&range,&mapped))||!mapped)return false;
    std::memcpy(dst,static_cast<const unsigned char*>(mapped)+offset,bytes);
    const D3D12_RANGE no_writes{0,0};resource->Unmap(0,&no_writes);return true;
}
}
void initialize(ID3D12Device* device) noexcept {try{
    if(state().installed)return;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=256;desc.Height=1;desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> resource;if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&resource))))throw std::runtime_error("upload discovery allocation failed");
    auto** vt=*reinterpret_cast<void***>(resource.Get());HookTransaction hooks;
    hooks.add(vt[11],reinterpret_cast<void*>(&address_hook),reinterpret_cast<void**>(&original_address));
    hooks.add(vt[8],reinterpret_cast<void*>(&map_hook),reinterpret_cast<void**>(&original_map));hooks.commit();state().installed=true;
    log("Native CBV capture installed: resource GPU-address/Map discovery, upload heaps only, retained catalog <=128 MiB/128 buffers, scan <=16 KiB per binding");
}catch(const std::exception& e){log(std::string("Native CBV capture unavailable; playback preserved: ")+e.what());++state().errors;}}
std::vector<Candidate> read(ID3D12Device* device,D3D12_GPU_VIRTUAL_ADDRESS address,ID3D12RootSignature* signature,UINT index,bool allow_scan,bool* scanned) noexcept {std::vector<Candidate> result;try{
    if(scanned)*scanned=false;auto& s=state();++s.reads;if(!device||!address||(address&255)){++s.misses;return result;}
    // The SDK debug layer can replay root-CBV recordings through a different
    // backing list. Until that identity is tracked, never inject CBV captures.
    ComPtr<ID3D12InfoQueue> debug;if(SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&debug)))){++s.unsupported;return result;}
    Buffer found;unsigned matches{};
    {std::lock_guard lock(s.mutex);for(auto& b:s.buffers)if(address>=b.address&&address-b.address<b.bytes){found=b;b.used=GetTickCount64();++matches;}}
    if(matches!=1){++s.misses;return result;}
    ComPtr<ID3D12Device> owner;if(FAILED(found.resource->GetDevice(IID_PPV_ARGS(&owner)))||owner.Get()!=device){++s.unsupported;return result;}
    const auto offset=address-found.address;const auto bytes=std::min<UINT64>(16384,found.bytes-offset);
    std::vector<UINT64> hints;if(signature){std::lock_guard lock(s.mutex);for(const auto& layout:s.layouts)if(layout.signature.Get()==signature&&layout.index==index){hints=layout.offsets;break;}}
    bool complete=!hints.empty();
    for(const auto hint:hints){Candidate candidate;if(hint+64>bytes||!copy(found.resource.Get(),offset+hint,candidate.snapshot.raw.data(),64)){complete=false;break;}
        candidate.matrix=candidate.snapshot.raw;if(!perspective(candidate.matrix)){for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x)candidate.matrix[y*4+x]=candidate.snapshot.raw[x*4+y];if(!perspective(candidate.matrix)){complete=false;break;}}
        candidate.snapshot.resource=found.resource;candidate.snapshot.offset=offset+hint;result.push_back(std::move(candidate));}
    if(complete){++s.hint_hits;return result;}result.clear();
    if(!allow_scan){++s.budget_skips;return result;}if(scanned)*scanned=true;++s.full_scans;
    if(bytes<64){++s.misses;return result;}std::vector<float> values(size_t(bytes/4));
    if(!copy(found.resource.Get(),offset,values.data(),values.size()*4)){++s.errors;return result;}
    // Register-aligned windows, including both matrix storage orders.
    for(size_t start=0;start+16<=values.size();start+=4){Candidate candidate;std::copy_n(values.begin()+start,16,candidate.snapshot.raw.begin());candidate.matrix=candidate.snapshot.raw;
        if(!perspective(candidate.matrix)){for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x)candidate.matrix[y*4+x]=candidate.snapshot.raw[x*4+y];if(!perspective(candidate.matrix))continue;}
        if(result.size()==8){result.clear();++s.unsupported;return result;}
        candidate.snapshot.resource=found.resource;candidate.snapshot.offset=offset+start*4;result.push_back(std::move(candidate));++s.candidates;
    }
    if(signature&&!result.empty()){std::lock_guard lock(s.mutex);auto it=std::find_if(s.layouts.begin(),s.layouts.end(),[&](const Layout& layout){return layout.signature.Get()==signature&&layout.index==index;});
        if(it==s.layouts.end()){if(s.layouts.size()>=32)s.layouts.erase(s.layouts.begin());s.layouts.push_back({signature,index,{}});it=std::prev(s.layouts.end());}
        it->offsets.clear();for(const auto& candidate:result)it->offsets.push_back(candidate.snapshot.offset-offset);}
}catch(...){result.clear();++state().errors;}return result;}
bool unchanged(const Snapshot& snapshot) noexcept {if(!snapshot.resource)return true;try{
    std::array<float,16> raw{};const bool valid=copy(snapshot.resource.Get(),snapshot.offset,raw.data(),64)&&std::memcmp(raw.data(),snapshot.raw.data(),64)==0;
    if(!valid)++state().changes;return valid;
}catch(...){++state().errors;return false;}}
std::string status(){auto& s=state();std::lock_guard lock(s.mutex);std::ostringstream out;out<<"Native CBV capture: installed="<<s.installed<<" buffers="<<s.buffers.size()<<" retained_bytes="<<s.bytes<<" reads="<<s.reads<<" unresolved="<<s.misses<<" unsupported="<<s.unsupported<<" candidates="<<s.candidates<<" changed="<<s.changes<<" evictions="<<s.evictions<<" full_scans="<<s.full_scans<<" layout_hits="<<s.hint_hits<<" scan_budget_skips="<<s.budget_skips<<" errors="<<s.errors;return out.str();}
}
