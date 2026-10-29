#include "depth_capture.h"
#include "camera_frame_policy.h"
#include "depth_fallback_policy.h"
#include "depth_observer.h"
#include "depth_tracking.h"
#include "native_depth.h"
#include "dx12_native.h"
#include "hook_transaction.h"
#include "gate_trace.h"
#include <bcrypt.h>
#include <fstream>
#include <unordered_map>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <array>
#include <cstdint>
namespace virtualtv::depth {
using Microsoft::WRL::ComPtr;
namespace {
std::atomic<Backend> selected_backend{Backend::Auto};
std::mutex fallback_mutex;FallbackPolicy fallback_policy;std::atomic<bool> fell_back{false};
using Constants=sl::Result(*)(const sl::Constants&,const sl::FrameToken&,const sl::ViewportHandle&);
using Tags=sl::Result(*)(const sl::FrameToken&,const sl::ViewportHandle&,const sl::ResourceTag*,uint32_t,sl::CommandBuffer*);
using Execute=void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*,UINT,ID3D12CommandList*const*);
using Reset=HRESULT(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,ID3D12CommandAllocator*,ID3D12PipelineState*);
Constants original_constants{};Tags original_tags{};Execute original_execute{};Reset original_reset{};
struct Camera {sl::Constants values;ULONGLONG at{};virtualtv::Camera native_pair;};
struct State {
    std::mutex mutex;std::atomic<bool> enabled{},failed{},installed{},attempted{};unsigned epoch=1;
    std::unordered_map<UINT64,Camera> cameras;
    std::unordered_map<ID3D12CommandList*,std::vector<std::shared_ptr<Image>>> pending;
    std::vector<std::shared_ptr<Image>> pool;std::shared_ptr<Image> latest;
    std::string reason="Occlusion: waiting for graphics initialization",rejection;
};
State& state(){static auto* s=new State;return *s;}
// Passive counters only: no extra hooks, resource queries or capture decisions.
struct Diagnostics {
    std::atomic<UINT64> constants_calls{},constants_errors{},constants_accepted{},tags_calls{},tags_errors{},tag_records{},depth_records{},depth_disabled{},depth_accepted{},queue_calls{},copies_published{},reset_discards{},bad_batches{};
    std::atomic<int> constants_result{},tags_result{};
    std::atomic<unsigned> constants_frame{},constants_viewport{},tags_frame{},tags_viewport{};
    std::atomic<ULONGLONG> constants_at{},tags_at{},depth_at{};
    std::array<std::atomic<UINT64>,32> types{};std::atomic<UINT64> other_types{};
    ULONGLONG next_report{}; // protected by State::mutex
};
Diagnostics& diagnostics(){static auto* value=new Diagnostics;return *value;}
void report_diagnostics_locked(State& s){
    if(!debug_logging())return;
    auto& d=diagnostics();const auto now=GetTickCount64();if(now<d.next_report)return;d.next_report=now+10000;
    const auto age=[&](ULONGLONG at)->std::int64_t{return at?static_cast<std::int64_t>(now-at):-1;};
    std::ostringstream out;out<<"Depth diagnostics: enabled="<<s.enabled.load()<<" installed="<<s.installed.load()<<" failed="<<s.failed.load()<<" epoch="<<s.epoch
        <<" constants_calls="<<d.constants_calls.load()<<" constants_errors="<<d.constants_errors.load()<<" constants_result="<<d.constants_result.load()<<" constants_accepted="<<d.constants_accepted.load()<<" constants_age_ms="<<age(d.constants_at.load())<<" constants_frame="<<d.constants_frame.load()<<" constants_viewport="<<d.constants_viewport.load()
        <<" tags_calls="<<d.tags_calls.load()<<" tags_errors="<<d.tags_errors.load()<<" tags_result="<<d.tags_result.load()<<" tags_age_ms="<<age(d.tags_at.load())<<" tags_frame="<<d.tags_frame.load()<<" tags_viewport="<<d.tags_viewport.load()<<" tag_records="<<d.tag_records.load()<<" bad_batches="<<d.bad_batches.load()
        <<" depth_records="<<d.depth_records.load()<<" depth_age_ms="<<age(d.depth_at.load())<<" depth_while_disabled="<<d.depth_disabled.load()<<" depth_accepted="<<d.depth_accepted.load()<<" queue_calls="<<d.queue_calls.load()<<" copies_published="<<d.copies_published.load()<<" reset_discards="<<d.reset_discards.load()
        <<" cameras="<<s.cameras.size()<<" pending_lists="<<s.pending.size()<<" pool="<<s.pool.size()<<" latest="<<bool(s.latest);
    if(s.latest)out<<" latest_frame="<<s.latest->frame<<" latest_viewport="<<s.latest->viewport<<" latest_epoch="<<s.latest->epoch<<" latest_age_ms="<<age(s.latest->at)<<" latest_direct="<<s.latest->direct<<" latest_poisoned="<<s.latest->poisoned;
    out<<" tag_types=";for(unsigned n=0;n<d.types.size();++n){const auto count=d.types[n].load();if(count)out<<n<<':'<<count<<',';}out<<"other:"<<d.other_types.load()<<" depth_type="<<sl::kBufferTypeDepth<<" reason="<<s.reason;
    log(out.str());
    observer::report();
    if(s.enabled&&s.installed&&!d.tags_calls.load()&&!d.constants_calls.load())log("Depth diagnosis: Streamline hooks installed but neither export has been called; experimental native capture evaluates separate recording proofs. Result fields are defaults until calls arrive.");
}

void report_capture_skip_locked(const State& s,const char* reason){
    static GateTrace trace;trace.sample("depth_capture_skip",reason,[&]{return "pending_lists="+std::to_string(s.pending.size())+" pool="+std::to_string(s.pool.size())+" epoch="+std::to_string(s.epoch);});
}
UINT64 key(unsigned frame,unsigned viewport){return (UINT64(frame)<<32)|viewport;}
bool same(IUnknown* a,IUnknown* b){ComPtr<IUnknown>x,y;return a&&b&&SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(&x)))&&SUCCEEDED(b->QueryInterface(IID_PPV_ARGS(&y)))&&x==y;}
void transition(ID3D12GraphicsCommandList* list,ID3D12Resource* texture,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){if(from==to)return;D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={texture,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,from,to};list->ResourceBarrier(1,&b);}
void problem(const char* why){auto& s=state();std::lock_guard lock(s.mutex);s.rejection=why;s.reason=std::string("Occlusion unavailable: ")+why;static GateTrace trace;trace.sample("depth_capture",why,[]{return "video playback preserved";});}
bool finished(const Image& image){return !image.poisoned&&(!image.ready||image.ready->GetCompletedValue()>=image.value);}
void constants(const sl::Constants& c,unsigned frame,unsigned viewport){
    auto& s=state();if(!s.enabled||s.failed)return;
    const bool camera_header=c.structType==sl::Constants::s_structType&&c.structVersion>=1;
    static GateTrace camera_input_trace;camera_input_trace.sample("depth_constants_input",!camera_header?"header_rejected":valid_camera(c)?"camera_valid":"camera_invalid",[&]{std::ostringstream out;out<<"frame="<<frame<<" viewport="<<viewport<<" version="<<c.structVersion<<" type_matches="<<(c.structType==sl::Constants::s_structType)<<" depth_inverted="<<static_cast<unsigned>(c.depthInverted)<<" orthographic="<<static_cast<unsigned>(c.orthographicProjection)<<" projection_diag="<<c.cameraViewToClip[0].x<<','<<c.cameraViewToClip[1].y<<','<<c.cameraViewToClip[2].z<<','<<c.cameraViewToClip[3].w<<" projection_2w="<<c.cameraViewToClip[2].w<<" position="<<c.cameraPos.x<<','<<c.cameraPos.y<<','<<c.cameraPos.z;return out.str();});
    if(c.structType!=sl::Constants::s_structType||c.structVersion<1||!valid_camera(c)){problem("camera constants rejected");return;}
    virtualtv::Camera native_pair;render_camera_snapshot(native_pair);native_pair.viewport=viewport;
    ++diagnostics().constants_accepted;
    std::lock_guard lock(s.mutex);const auto now=GetTickCount64();
    for(auto it=s.cameras.begin();it!=s.cameras.end();)if(now-it->second.at>250)it=s.cameras.erase(it);else ++it;
    if(s.cameras.size()>=64)s.cameras.clear();Camera copied{c,now,native_pair};copied.values.next=nullptr;s.cameras.insert_or_assign(key(frame,viewport),copied);
}
void tag(const sl::ResourceTag& tag,unsigned frame,unsigned viewport,ID3D12GraphicsCommandList* command){
    auto& s=state();if(!s.enabled||s.failed||tag.type!=sl::kBufferTypeDepth)return;
    static GateTrace tag_input_trace;tag_input_trace.sample("depth_tag_input","received",[&]{std::ostringstream out;out<<"frame="<<frame<<" viewport="<<viewport<<" version="<<tag.structVersion<<" type_matches="<<(tag.structType==sl::ResourceTag::s_structType)<<" next_present="<<(tag.next!=nullptr)<<" resource_present="<<(tag.resource!=nullptr)<<" lifecycle="<<unsigned(tag.lifecycle)<<" extent="<<tag.extent.left<<','<<tag.extent.top<<','<<tag.extent.width<<','<<tag.extent.height<<" command_list="<<(command!=nullptr);return out.str();});
    if(tag.structType!=sl::ResourceTag::s_structType||tag.structVersion!=1||tag.next||!tag.resource){problem("unsupported depth tag");return;}
    const auto& resource=*tag.resource;
    static GateTrace resource_trace;resource_trace.sample("depth_resource_input","received",[&]{std::ostringstream out;out<<"frame="<<frame<<" viewport="<<viewport<<" version="<<resource.structVersion<<" type_matches="<<(resource.structType==sl::Resource::s_structType)<<" resource_type="<<unsigned(resource.type)<<" native_present="<<(resource.native!=nullptr)<<" state="<<resource.state;return out.str();});
    if(resource.structType!=sl::Resource::s_structType||resource.structVersion!=1||resource.type!=sl::ResourceType::eTex2d||!resource.native||resource.state==UINT_MAX){problem("depth resource ABI/state rejected");return;}
    if(resource.state!=D3D12_RESOURCE_STATE_DEPTH_WRITE&&resource.state!=D3D12_RESOURCE_STATE_DEPTH_READ&&resource.state!=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE&&resource.state!=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE&&resource.state!=static_cast<unsigned>(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)){problem("unsupported depth state");return;}
    bool unwrapped{};auto source=native_interface(static_cast<ID3D12Resource*>(resource.native),unwrapped);if(!source){problem("depth interface unavailable");return;}
    const auto desc=source->GetDesc();const auto format=sample_format(desc.Format);
    const char* rejected=format==DXGI_FORMAT_UNKNOWN?"unsupported depth format":desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D?"depth is not a 2D texture":desc.DepthOrArraySize!=1?"depth texture array is unsupported":desc.SampleDesc.Count!=1?"multisampled depth is unsupported":!desc.MipLevels?"invalid depth mip count":desc.Width<64||desc.Width>16384||desc.Height<64||desc.Height>16384?"depth dimensions outside supported limits":nullptr;
    static GateTrace descriptor_trace;descriptor_trace.sample("depth_descriptor",rejected?rejected:"accepted",[&]{std::ostringstream out;out<<"format="<<unsigned(desc.Format)<<" view_format="<<unsigned(format)<<" dimension="<<unsigned(desc.Dimension)<<" width="<<desc.Width<<" height="<<desc.Height<<" array="<<desc.DepthOrArraySize<<" samples="<<desc.SampleDesc.Count<<" sample_quality="<<desc.SampleDesc.Quality<<" mips="<<desc.MipLevels<<" flags="<<unsigned(desc.Flags)<<" state="<<resource.state<<" lifecycle="<<unsigned(tag.lifecycle)<<" command_list="<<(command!=nullptr);return out.str();});
    if(rejected){problem(rejected);return;}
    auto extent=tag.extent;if(!extent.width&&!extent.height){extent.width=static_cast<UINT>(desc.Width);extent.height=desc.Height;}
    if(!extent.width||!extent.height||UINT64(extent.left)+extent.width>desc.Width||UINT64(extent.top)+extent.height>desc.Height){problem("depth viewport rejected");return;}
    ComPtr<ID3D12Device> device;const auto device_result=source->GetDevice(IID_PPV_ARGS(&device));if(FAILED(device_result)){static GateTrace trace;trace.sample("depth_resource_device","GetDevice_failed",[&]{return "hr="+std::to_string(static_cast<long>(device_result));});return;}
    std::lock_guard lock(s.mutex);std::shared_ptr<Image> image;
    if(!command){
        if(tag.lifecycle!=sl::ResourceLifecycle::eValidUntilPresent){s.reason="Occlusion unavailable: volatile depth supplied without a command list";report_capture_skip_locked(s,"Occlusion unavailable: volatile depth supplied without a command list");return;}
        image=std::make_shared<Image>();image->texture=source;image->direct=true;
    }else{
        bool list_unwrapped{};auto list=native_interface(command,list_unwrapped);ComPtr<ID3D12Device> owner;
        if(!list||list->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT||FAILED(list->GetDevice(IID_PPV_ARGS(&owner)))||!same(device.Get(),owner.Get())){s.reason="Occlusion unavailable: depth command list ownership mismatch";report_capture_skip_locked(s,"Occlusion unavailable: depth command list ownership mismatch");return;}
        if(s.pending.size()>=64&&s.pending.find(list.Get())==s.pending.end()){s.reason="Occlusion unavailable: pending depth limit reached";report_capture_skip_locked(s,"Occlusion unavailable: pending depth limit reached");return;}
        for(auto& candidate:s.pool)if(candidate.use_count()==1&&finished(*candidate)){image=candidate;break;}
        if(!image){if(s.pool.size()>=8){s.reason="Occlusion waiting for depth GPU resources";report_capture_skip_locked(s,"Occlusion waiting for depth GPU resources");return;}image=std::make_shared<Image>();s.pool.push_back(image);}
        // A slot is reused only after its writer fence and every TV reader have released it.
        const auto desired=copy_format(desc.Format);bool create=!image->texture;
        if(!create){const auto d=image->texture->GetDesc();ComPtr<ID3D12Device> old;image->texture->GetDevice(IID_PPV_ARGS(&old));create=d.Width!=desc.Width||d.Height!=desc.Height||d.Format!=desired||d.MipLevels!=desc.MipLevels||d.Flags!=static_cast<D3D12_RESOURCE_FLAGS>(desc.Flags&~D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)||!same(old.Get(),device.Get());}
        if(create){auto d=desc;d.Format=desired;d.Flags=static_cast<D3D12_RESOURCE_FLAGS>(d.Flags&~D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
            image->texture.Reset();image->ready.Reset();image->value=0;
            if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&image->texture)))||FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&image->ready)))){s.reason="Occlusion unavailable: cannot allocate depth copy";report_capture_skip_locked(s,"Occlusion unavailable: cannot allocate depth copy");return;}}
        image->source=source;image->direct=false;image->poisoned=false;
        transition(list.Get(),source.Get(),static_cast<D3D12_RESOURCE_STATES>(resource.state),D3D12_RESOURCE_STATE_COPY_SOURCE);
        transition(list.Get(),image->texture.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyResource(image->texture.Get(),source.Get());
        transition(list.Get(),image->texture.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        transition(list.Get(),source.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,static_cast<D3D12_RESOURCE_STATES>(resource.state));
        s.pending[list.Get()].push_back(image);
    }
    s.rejection.clear();image->view_format=format;image->extent=extent;image->source_state=static_cast<D3D12_RESOURCE_STATES>(resource.state);image->frame=frame;image->viewport=viewport;image->epoch=s.epoch;image->at=GetTickCount64();
    ++diagnostics().depth_accepted;
    if(image->direct)s.latest=image;
    static bool first{};if(!first){first=true;log("Scene depth captured from Streamline: frame="+std::to_string(frame)+" viewport="+std::to_string(viewport)+" size="+std::to_string(extent.width)+"x"+std::to_string(extent.height)+" format="+std::to_string(desc.Format)+" state="+std::to_string(resource.state)+" lifecycle="+std::to_string(tag.lifecycle)+" native_resource="+std::to_string(reinterpret_cast<uintptr_t>(source.Get()))+" copy_recorded="+std::to_string(!image->direct));}
}
void submitted(ID3D12CommandQueue* queue,UINT count,ID3D12CommandList*const* lists){
    auto& s=state();std::lock_guard lock(s.mutex);if(s.pending.empty())return;
    for(UINT i=0;i<count;++i){const auto found=s.pending.find(lists[i]);if(found==s.pending.end())continue;
        for(auto& image:found->second){++image->value;if(FAILED(queue->Signal(image->ready.Get(),image->value))){image->poisoned=true;s.reason="Occlusion unavailable: depth queue signal failed";continue;}
            if(image->epoch==s.epoch&&s.enabled){image->at=GetTickCount64();s.latest=image;++diagnostics().copies_published;}}
        s.pending.erase(found);
    }
}
void reset(ID3D12GraphicsCommandList* list){auto& s=state();std::lock_guard lock(s.mutex);diagnostics().reset_discards+=s.pending.erase(list);}
sl::Result constants_hook(const sl::Constants& c,const sl::FrameToken& f,const sl::ViewportHandle& v){
    const auto result=original_constants(c,f,v);const auto error=GetLastError();
    try{auto& d=diagnostics();++d.constants_calls;d.constants_result=static_cast<int>(result);d.constants_at=GetTickCount64();d.constants_frame=static_cast<unsigned>(f);d.constants_viewport=static_cast<unsigned>(v);if(result==sl::Result::eOk)constants(c,static_cast<uint32_t>(f),static_cast<uint32_t>(v));else ++d.constants_errors;}
    catch(const std::exception& e){state().failed=true;log(std::string("Depth constants callback exception: ")+e.what());}catch(...){state().failed=true;log("Depth constants callback unexpected exception");}SetLastError(error);return result;
}
sl::Result tags_hook(const sl::FrameToken& f,const sl::ViewportHandle& v,const sl::ResourceTag* tags,uint32_t count,sl::CommandBuffer* command){
    const auto result=original_tags(f,v,tags,count,command);const auto error=GetLastError();
    try{auto& d=diagnostics();++d.tags_calls;d.tags_result=static_cast<int>(result);d.tags_at=GetTickCount64();d.tags_frame=static_cast<unsigned>(f);d.tags_viewport=static_cast<unsigned>(v);
        if(result!=sl::Result::eOk)++d.tags_errors;
        if(result==sl::Result::eOk&&tags&&count<=128){d.tag_records+=count;for(unsigned i=0;i<count;++i){const auto type=static_cast<unsigned>(tags[i].type);if(type<d.types.size())++d.types[type];else ++d.other_types;
            if(tags[i].type==sl::kBufferTypeDepth){++d.depth_records;d.depth_at=GetTickCount64();if(!state().enabled)++d.depth_disabled;}
            if(state().enabled)tag(tags[i],static_cast<uint32_t>(f),static_cast<uint32_t>(v),static_cast<ID3D12GraphicsCommandList*>(command));
        }}else if(!tags||count>128)++d.bad_batches;
    }catch(const std::exception& e){state().failed=true;log(std::string("Depth tags callback exception: ")+e.what());}catch(...){state().failed=true;log("Depth tags callback unexpected exception");}SetLastError(error);return result;
}
void STDMETHODCALLTYPE execute_hook(ID3D12CommandQueue* q,UINT n,ID3D12CommandList*const* lists){const auto entering_error=GetLastError();native::before_submit(q,n,lists);SetLastError(entering_error);original_execute(q,n,lists);const auto error=GetLastError();try{++diagnostics().queue_calls;tracking::submitted(q,n,lists);native::submitted(q,n,lists);submitted(q,n,lists);}catch(...){state().failed=true;}SetLastError(error);}
HRESULT STDMETHODCALLTYPE reset_hook(ID3D12GraphicsCommandList* list,ID3D12CommandAllocator* allocator,ID3D12PipelineState* pipeline){const auto result=original_reset(list,allocator,pipeline);const auto error=GetLastError();try{if(SUCCEEDED(result)){tracking::reset(list);native::reset(list);reset(list);}}catch(...){state().failed=true;}SetLastError(error);return result;}
bool pinned(HMODULE module){
    wchar_t path[32768]{};if(!GetModuleFileNameW(module,path,32768))return false;std::ifstream file(path,std::ios::binary);if(!file)return false;
    BCRYPT_ALG_HANDLE algorithm{};BCRYPT_HASH_HANDLE hash{};if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return false;
    if(BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)<0){BCryptCloseAlgorithmProvider(algorithm,0);return false;}
    bool ok=true;char bytes[65536];while(file){file.read(bytes,sizeof bytes);if(file.gcount()&&BCryptHashData(hash,reinterpret_cast<PUCHAR>(bytes),static_cast<ULONG>(file.gcount()),0)<0)ok=false;}
    unsigned char digest[32]{};ok&=file.eof()&&BCryptFinishHash(hash,digest,sizeof digest,0)>=0;BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);
    std::ostringstream text;for(auto b:digest)text<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(b);
    return ok&&text.str()=="98ac2097902686441d6cbd1182fcf5cb15e9e7fa0744bfdf176ec728a4a49f1a";
}
bool code(void* pointer){MEMORY_BASIC_INFORMATION m{};return VirtualQuery(pointer,&m,sizeof m)&&m.State==MEM_COMMIT&&m.Type==MEM_IMAGE&&!(m.Protect&(PAGE_NOACCESS|PAGE_GUARD))&&(m.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY));}
}
DXGI_FORMAT sample_format(DXGI_FORMAT f){switch(f){case DXGI_FORMAT_R32_TYPELESS:case DXGI_FORMAT_D32_FLOAT:case DXGI_FORMAT_R32_FLOAT:return DXGI_FORMAT_R32_FLOAT;case DXGI_FORMAT_R32G8X24_TYPELESS:case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;case DXGI_FORMAT_R24G8_TYPELESS:case DXGI_FORMAT_D24_UNORM_S8_UINT:case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;case DXGI_FORMAT_R16_TYPELESS:case DXGI_FORMAT_D16_UNORM:case DXGI_FORMAT_R16_UNORM:return DXGI_FORMAT_R16_UNORM;default:return DXGI_FORMAT_UNKNOWN;}}
DXGI_FORMAT copy_format(DXGI_FORMAT f){switch(sample_format(f)){case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:return DXGI_FORMAT_R32G8X24_TYPELESS;case DXGI_FORMAT_R32_FLOAT:return DXGI_FORMAT_R32_TYPELESS;case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:return DXGI_FORMAT_R24G8_TYPELESS;case DXGI_FORMAT_R16_UNORM:return DXGI_FORMAT_R16_TYPELESS;default:return DXGI_FORMAT_UNKNOWN;}}
bool valid_camera(const sl::Constants& c){
    for(const auto& row:c.cameraViewToClip.row)for(float x:{row.x,row.y,row.z,row.w})if(!std::isfinite(x)||std::abs(x)>1e6f)return false;
    for(const auto& v:{c.cameraRight,c.cameraUp,c.cameraFwd}){const auto n=v.x*v.x+v.y*v.y+v.z*v.z;if(!std::isfinite(n)||std::abs(n-1)>0.05f)return false;}
    for(float x:{c.cameraPos.x,c.cameraPos.y,c.cameraPos.z,c.jitterOffset.x,c.jitterOffset.y})if(!std::isfinite(x)||std::abs(x)>1e7f)return false;
    const auto& p=c.cameraViewToClip;return (c.depthInverted==sl::eTrue||c.depthInverted==sl::eFalse)&&c.orthographicProjection==sl::eFalse&&std::abs(std::abs(p[2].w)-1)<.001f&&std::abs(p[3].w)<.001f&&p[0].x>0&&p[1].y>0&&std::abs(p[0].z)+std::abs(p[1].z)<.001f;
}
bool matches_camera(const sl::Constants& c,const Scene& scene){
    // Streamline's pose may use renderer-relative coordinates. Its projection
    // belongs to the tagged frame; world placement keeps the existing native
    // camera transform which already aligns the TV correctly in the game.
    if(!scene.attached||!scene.camera.valid||!std::isfinite(scene.camera.vertical_fov)||scene.camera.vertical_fov<=1||scene.camera.vertical_fov>=175)return false;
    const float expected=1/std::tan(scene.camera.vertical_fov*3.14159265359f/360);
    return std::isfinite(c.cameraViewToClip[1].y)&&std::abs(c.cameraViewToClip[1].y/expected-1)<.08f;
}
bool world_camera(const sl::Constants& c,const virtualtv::Camera& native,std::array<float,16>& out){
    if(!native.valid||!valid_camera(c))return false;
    auto next=native.world;
    const float dx=c.cameraPos.x-next[12],dy=c.cameraPos.y-next[13],dz=c.cameraPos.z-next[14];
    // Reject origin-relative metadata rather than moving a world-anchored TV
    // hundreds of metres. Small spring/head offsets belong to the final view.
    if(!std::isfinite(dx*dx+dy*dy+dz*dz)||dx*dx+dy*dy+dz*dz>256)return false;
    const sl::float3 axes[]{c.cameraRight,c.cameraUp,c.cameraFwd};
    for(unsigned i=0;i<3;++i){
        const auto a=axes[i];const unsigned n=i*4;const float dot=a.x*next[n]+a.y*next[n+1]+a.z*next[n+2];
        // The supplied game's Streamline right axis is opposite the native
        // camera's. Preserve native conventions for each verified basis axis.
        if(!std::isfinite(dot)||std::abs(dot)<.5f)return false;
        const float sign=dot<0?-1.f:1.f;next[n]=sign*a.x;next[n+1]=sign*a.y;next[n+2]=sign*a.z;
    }
    next[12]=c.cameraPos.x;next[13]=c.cameraPos.y;next[14]=c.cameraPos.z;
    next[3]=next[7]=next[11]=0;next[15]=1;out=next;return true;
}
bool latest_world_camera(const Scene& scene,std::array<float,16>& out,sl::float4x4& projection){
    if(backend()==Backend::NativeOnly)return false;
    auto& s=state();std::lock_guard lock(s.mutex);const auto now=GetTickCount64();const Camera* latest{};
    // Select by validated main-view recording provenance and pose, independently
    // of depth availability. Never adopt an arbitrary recent camera/frame.
    for(const auto& entry:s.cameras)if(static_cast<unsigned>(entry.first)==0&&
        camera_pair_matches(entry.second.native_pair,scene.camera,now)&&(!latest||entry.second.at>latest->at))latest=&entry.second;
    if(!latest)return false;
    const auto& c=latest->values;const auto& w=scene.camera.world;
    const float dx=c.cameraPos.x-w[12],dy=c.cameraPos.y-w[13],dz=c.cameraPos.z-w[14];
    if(dx*dx+dy*dy+dz*dz>.000625f)return false;
    const sl::float3 axes[]{c.cameraRight,c.cameraUp,c.cameraFwd};
    for(unsigned i=0;i<3;++i)if(std::abs(axes[i].x*w[4*i]+axes[i].y*w[4*i+1]+axes[i].z*w[4*i+2])<.9999f)return false;
    if(!world_camera(c,scene.camera,out))return false;projection=c.cameraViewToClip;return true;
}
static Sample snapshot_impl(ID3D12Device* device,const Scene& scene,unsigned width,unsigned height){
    if(backend()==Backend::Auto||backend()==Backend::NativeOnly||backend()==Backend::ReShadeNative)native::frame(scene,width,height);
    auto& s=state();std::lock_guard lock(s.mutex);Sample sample;tracking::frame(scene,width,height);report_diagnostics_locked(s);
    const auto fallback=[&]{if(backend()==Backend::StreamlineOnly){s.reason="Occlusion unavailable: Streamline only selected; waiting for matched Streamline depth/projection";return Sample{};}auto result=native::snapshot(device,scene,width,height);if(result.image)s.reason=native::status();else s.reason="Occlusion unavailable: "+native::status();return result;};
    if(backend()==Backend::NativeOnly||backend()==Backend::ReShadeNative)return fallback();
    // DX12 tag-assisted mode uses the independently validated Streamline tag/camera
    // *as input* to our D3D12 copy + depth shader, not native heuristic matching.
    // It must fail closed, never fall back to an unrelated native depth candidate.
    const bool tagged_native=backend()==Backend::NativeTagged;
    if(s.failed){s.reason="Occlusion disabled after a capture failure; video preserved";return tagged_native?Sample{}:fallback();}
    if(s.attempted&&!s.installed)return tagged_native?Sample{}:fallback();
    if(!s.latest||s.latest->epoch!=s.epoch||GetTickCount64()-s.latest->at>100){s.reason=s.rejection.empty()?"Occlusion waiting for fresh scene depth":"Occlusion unavailable: "+s.rejection;return tagged_native?Sample{}:fallback();}
    const auto image=s.latest;const auto found=s.cameras.find(key(image->frame,image->viewport));
    if(found==s.cameras.end()){s.reason="Occlusion waiting for matching frame camera constants";return tagged_native?Sample{}:fallback();}
    const auto& camera=found->second.values;const bool match=image->viewport==0&&matches_camera(camera,scene);
    static GateTrace camera_trace;camera_trace.sample("depth_camera",match?"projection_matched":"projection_rejected",[&]{const auto& w=scene.camera.world;std::ostringstream out;out<<"frame="<<image->frame<<" viewport="<<image->viewport<<" native_position="<<w[12]<<','<<w[13]<<','<<w[14]<<" streamline_position="<<camera.cameraPos.x<<','<<camera.cameraPos.y<<','<<camera.cameraPos.z<<" native_fov_degrees="<<scene.camera.vertical_fov<<" projection_y="<<camera.cameraViewToClip[1].y<<" right_dot="<<(camera.cameraRight.x*w[0]+camera.cameraRight.y*w[1]+camera.cameraRight.z*w[2])<<" up_dot="<<(camera.cameraUp.x*w[4]+camera.cameraUp.y*w[5]+camera.cameraUp.z*w[6])<<" world_pose=selected_at_draw";return out.str();});
    if(!match){s.reason="Occlusion waiting for main-viewport projection matching the native camera FOV";return tagged_native?Sample{}:fallback();}
    const float aspect=float(image->extent.width)/image->extent.height;const float output=float(width)/height;
    if(std::abs(aspect/output-1)>.08f){s.reason="Occlusion waiting for a depth viewport matching the screen";return tagged_native?Sample{}:fallback();}
    ComPtr<ID3D12Device> owner;if(FAILED(image->texture->GetDevice(IID_PPV_ARGS(&owner)))||!same(owner.Get(),device)){s.reason="Occlusion unavailable: scene depth belongs to another device";return tagged_native?Sample{}:fallback();}
    sample.image=image;sample.camera=found->second.values;s.reason=tagged_native?"Occlusion active: DX12 depth test with Streamline-tagged resource/projection":"Occlusion active: scene depth and projection matched";return sample;
}
Sample snapshot(ID3D12Device* device,const Scene& scene,unsigned width,unsigned height){
    auto sample=snapshot_impl(device,scene,width,height);
    const auto selected=backend();bool fallback=false;
    {std::lock_guard lock(fallback_mutex);fallback=fallback_policy.update(GetTickCount64(),
        state().enabled.load()&&scene.camera.valid&&(selected==Backend::StreamlineOnly||selected==Backend::Auto),bool(sample.image));}
    if(fallback){
        set_backend(Backend::ReShadeNative);fell_back=true;
        log("Streamline provided no usable scene depth for 3 seconds; switched to ReShade occlusion");
    }
    return sample;
}
bool automatic_fallback_active() noexcept{return fell_back.load();}
void configure_backend_for_adapter(unsigned vendor_id) {
    static std::atomic<bool> initialized{false};
    if(initialized.exchange(true))return;
    if(vendor_id==0x10DE){
        if(backend()==Backend::Auto)set_backend(Backend::StreamlineOnly);
        log("GPU occlusion defaults: NVIDIA; Streamline");return;
    }
    if(backend()!=Backend::Auto){log("GPU occlusion defaults: preserving manually selected backend");return;}
    native::set_capture_boundary(0);                 // Any eligible (working default)
    // Keep the native default or an exact near plane loaded from the TV settings file.
    native::set_minimum_scene_draws(8);
    native::select_depth(-1);                       // Automatic resource selection
    set_backend(Backend::ReShadeNative);
    log("GPU occlusion defaults: non-NVIDIA vendor="+std::to_string(vendor_id)+" DX12 ReShade-style, any eligible, near="+std::to_string(native::estimated_near_plane())+", min_draws=8, depth=automatic");
}
Backend backend() noexcept {return selected_backend.load();}
const char* backend_name(Backend value) noexcept {switch(value){case Backend::NativeOnly:return "Native DX12 only";case Backend::NativeTagged:return "DX12 (Streamline-tagged depth)";case Backend::ReShadeNative:return "DX12 (ReShade-style depth)";case Backend::StreamlineOnly:return "Streamline only";default:return "Auto (Streamline preferred)";}}
void set_backend(Backend value){fell_back=false;{std::lock_guard lock(fallback_mutex);fallback_policy.reset();}if(value!=Backend::Auto&&value!=Backend::NativeOnly&&value!=Backend::StreamlineOnly&&value!=Backend::NativeTagged&&value!=Backend::ReShadeNative)return;if(selected_backend.exchange(value)==value)return;invalidate();native::heuristic(value==Backend::ReShadeNative);native::activate(state().enabled&&(value==Backend::Auto||value==Backend::NativeOnly||value==Backend::ReShadeNative));log(std::string("Occlusion backend changed: ")+backend_name(value));}
void activate(bool enabled){observer::activate(enabled);native::heuristic(backend()==Backend::ReShadeNative);native::activate(enabled&&(backend()==Backend::Auto||backend()==Backend::NativeOnly||backend()==Backend::ReShadeNative));auto& s=state();if(s.enabled.exchange(enabled)!=enabled){log(std::string("Depth capture activation: enabled=")+(enabled?"1":"0"));invalidate();}}
void invalidate(){{std::lock_guard lock(fallback_mutex);fallback_policy.reset();}native::invalidate();auto& s=state();std::lock_guard lock(s.mutex);++s.epoch;s.latest.reset();s.cameras.clear();}
void end_present(){auto& s=state();std::lock_guard lock(s.mutex);if(s.latest&&s.latest->direct)s.latest.reset();}
std::string status(){auto& s=state();std::lock_guard lock(s.mutex);return s.reason;}
void initialize_lifetime(ID3D12CommandQueue* queue,ID3D12GraphicsCommandList* list) noexcept {
    static std::atomic<bool> attempted{};if(attempted.exchange(true))return;HookTransaction hooks;
    try{bool uq{},ul{};auto q=native_interface(queue,uq);auto l=native_interface(list,ul);
        auto* execute=(*reinterpret_cast<void***>(q.Get()))[10];auto* reset=(*reinterpret_cast<void***>(l.Get()))[10];
        if(!code(execute)||!code(reset))throw std::runtime_error("lifetime methods are not executable image code");
        hooks.add(execute,reinterpret_cast<void*>(&execute_hook),reinterpret_cast<void**>(&original_execute));hooks.add(reset,reinterpret_cast<void*>(&reset_hook),reinterpret_cast<void**>(&original_reset));hooks.commit();native::lifetime(true);log("Native queue/reset observers committed independently of Streamline availability");
    }catch(const std::exception& e){log(std::string("Native queue/reset observers unavailable: ")+e.what());}
}
void initialize(ID3D12CommandQueue* queue,ID3D12GraphicsCommandList* list) noexcept {
    auto& s=state();if(s.attempted)return;s.attempted=true;HookTransaction hooks;
    observer::initialize(queue,list);
    initialize_lifetime(queue,list);
    try{
        const auto module=GetModuleHandleW(L"sl.interposer.dll");if(!module||!pinned(module)){problem("Streamline 2.11.1 binary did not match");return;}
        auto* set_constants=reinterpret_cast<void*>(GetProcAddress(module,"slSetConstants"));auto* set_tags=reinterpret_cast<void*>(GetProcAddress(module,"slSetTagForFrame"));
        constexpr unsigned char a[]{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x49};
        constexpr unsigned char b[]{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57};
        const auto base=reinterpret_cast<std::uintptr_t>(module);
        if(set_constants!=reinterpret_cast<void*>(base+0x77c0)||set_tags!=reinterpret_cast<void*>(base+0x7680)||!code(set_constants)||!code(set_tags)||std::memcmp(set_constants,a,sizeof a)||std::memcmp(set_tags,b,sizeof b)){problem("Streamline export bytes changed or another hook owns them");return;}
        bool uq{},ul{};auto native_queue=native_interface(queue,uq);auto native_list=native_interface(list,ul);auto* execute=(*reinterpret_cast<void***>(native_queue.Get()))[10];auto* reset=(*reinterpret_cast<void***>(native_list.Get()))[10];
        if(!code(execute)||!code(reset)){problem("native queue/list methods were not executable image code");return;}
        hooks.add(set_constants,reinterpret_cast<void*>(&constants_hook),reinterpret_cast<void**>(&original_constants));hooks.add(set_tags,reinterpret_cast<void*>(&tags_hook),reinterpret_cast<void**>(&original_tags));
        log("Depth diagnostic build: passive Streamline call/result/type counters; reports every 5s while a world TV is drawn");
        log("Depth initialization: native_queue_unwrapped="+std::to_string(uq)+" native_list_unwrapped="+std::to_string(ul)+" queue_type="+std::to_string(unsigned(native_queue->GetDesc().Type))+" list_type="+std::to_string(unsigned(native_list->GetType()))+" dlss_module_loaded="+std::to_string(GetModuleHandleW(L"sl.dlss.dll")!=nullptr)+" dlssg_module_loaded="+std::to_string(GetModuleHandleW(L"sl.dlss_g.dll")!=nullptr));
        hooks.commit();s.installed=true;{std::lock_guard lock(s.mutex);s.reason="Occlusion ready; attach a screen to capture scene depth";}
        log("Occlusion initialization committed: pinned Streamline 2.11.1 depth tags/constants and native queue/list lifetime observers; core TV hooks unchanged");
    }catch(...){hooks.rollback();problem("depth hooks could not be installed; playback preserved");}
}

#ifdef TV_GPU_TEST
void test_constants(const sl::Constants& c,unsigned f,unsigned v){constants(c,f,v);}
void test_tag(const sl::ResourceTag& t,unsigned f,unsigned v,ID3D12GraphicsCommandList* l){tag(t,f,v,l);}
void test_submitted(ID3D12CommandQueue* q,UINT n,ID3D12CommandList*const* lists){submitted(q,n,lists);}
#endif
}
