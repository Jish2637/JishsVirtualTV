#include "dx12_renderer.h"
#include "tv_screen_selection.h"
#include "tv_runtime.h"
#include "camera_frame_policy.h"
#include "hook_transaction.h"
#include "dx12_native.h"
#include "gate_trace.h"
#include "tv_presentation_projection.h"
#include "depth_capture.h"
#include "depth_observer.h"
#include "depth_tracking.h"
#include "depth_probe.h"
#include "native_depth.h"
#include "live_depth_hooks.h"
#ifndef TV_GPU_TEST
#include "host_bridge.h"
#include "tv_menu.h"
#endif
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <MinHook.h>
#include <thread>
#include <chrono>
#include <stdexcept>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <d3d12sdklayers.h>
#include <iomanip>
#ifdef TV_GPU_TEST
#include <iostream>
#endif
namespace virtualtv {
using Microsoft::WRL::ComPtr;
namespace {
using Present=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*,UINT,UINT);
using Present1=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*,UINT,UINT,const DXGI_PRESENT_PARAMETERS*);
using Resize=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*,UINT,UINT,UINT,DXGI_FORMAT,UINT);
using Resize1=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*,UINT,UINT,UINT,DXGI_FORMAT,UINT,const UINT*,IUnknown*const*);
using Execute=void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*,UINT,ID3D12CommandList*const*);
Present original_present{};Present1 original_present1{};Resize original_resize{};Resize1 original_resize1{};Execute original_execute{};
std::atomic<bool> stopped{};std::thread* transfer{};std::mutex queue_mutex,status_mutex;std::shared_ptr<const VlcPlayer::Frame> latest;
std::mutex sync_scene_mutex;
Placement sync_placement;
Vec3 sync_scale{1,1,1};
Screen sync_screen;
std::atomic<ScreenMode> screen_mode{ScreenMode::Hidden};std::atomic<bool> force_pattern{};
std::wstring render_message=L"Screen hidden; select a nearby object and Attach, or choose Front preview";
void report(const std::wstring& text){std::lock_guard lock(status_mutex);render_message=text;}
struct Pairing {ComPtr<IDXGISwapChain> chain;ComPtr<ID3D12CommandQueue> queue;};
Pairing binding;
struct Pending {ComPtr<ID3D12CommandQueue> queue;ULONGLONG at{};};
thread_local Pending pending;
struct Presentation {IDXGISwapChain* chain{};bool drawn{};};
thread_local Presentation presentation;
thread_local bool submitting{},presenting{};
using Vertex=ScreenMeshVertex;
struct Frame {
    ComPtr<ID3D12Resource> buffer,texture,upload,vertices,depth_copy;
    std::shared_ptr<const depth::Image> depth_image;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commands;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv{};D3D12_GPU_DESCRIPTOR_HANDLE srv{};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 fence{},sequence{};unsigned width{},height{};
};
struct Renderer {
    std::recursive_mutex mutex;ComPtr<IDXGISwapChain3> chain;ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue,submission_queue;
    ComPtr<ID3D12DescriptorHeap> rtvs,srvs;ComPtr<ID3D12RootSignature> root;ComPtr<ID3D12PipelineState> pipeline;
    ComPtr<ID3D12GraphicsCommandList> list;ComPtr<ID3D12Fence> fence;std::vector<Frame> frames;
    HANDLE event{};UINT64 next{},draws{},gui_draws{},skips{};ULONGLONG next_report{};bool failed{},first{},completed_first{},diagnosed{};
    ImGuiContext* gui{};bool gui_backend{};std::vector<UINT64> gui_fences;
};
Renderer& renderer(){static auto* r=new Renderer;return *r;}
struct GuiScope {ImGuiContext* previous{ImGui::GetCurrentContext()};explicit GuiScope(ImGuiContext* c){ImGui::SetCurrentContext(c);}~GuiScope(){ImGui::SetCurrentContext(previous);}};
std::string hr_text(HRESULT hr){std::ostringstream s;s<<"0x"<<std::hex<<std::setw(8)<<std::setfill('0')<<static_cast<unsigned long>(hr);return s.str();}
void device_diagnostics(Renderer& r,const char* operation,HRESULT hr){
    if(r.diagnosed)return;
    log(std::string(operation)+" result="+hr_text(hr)+" removed_reason="+hr_text(r.device?r.device->GetDeviceRemovedReason():E_NOINTERFACE)+" submitted="+std::to_string(r.next)+" completed="+std::to_string(r.fence?r.fence->GetCompletedValue():0));
    report(L"Graphics failure captured; see JishsVirtualTV.log");
    if(!r.device)return;r.diagnosed=FAILED(r.device->GetDeviceRemovedReason());
    ComPtr<ID3D12InfoQueue> info;if(SUCCEEDED(r.device.As(&info))){const auto count=info->GetNumStoredMessagesAllowedByRetrievalFilter();for(UINT64 i=count>16?count-16:0;i<count;++i){SIZE_T size{};info->GetMessage(i,nullptr,&size);if(!size||size>65536)continue;std::vector<unsigned char> bytes(size);auto* msg=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());if(SUCCEEDED(info->GetMessage(i,msg,&size))&&msg->Severity<=D3D12_MESSAGE_SEVERITY_WARNING)log("D3D12 message id="+std::to_string(msg->ID)+" "+std::string(msg->pDescription,msg->DescriptionByteLength?msg->DescriptionByteLength-1:0));}}
    ComPtr<ID3D12DeviceRemovedExtendedData1> dred;if(SUCCEEDED(r.device.As(&dred))){D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs{};const auto result=dred->GetAutoBreadcrumbsOutput1(&breadcrumbs);log("DRED breadcrumbs="+hr_text(result));if(SUCCEEDED(result)){auto* n=breadcrumbs.pHeadAutoBreadcrumbNode;for(unsigned i=0;n&&i<16;++i,n=n->pNext)log("DRED list="+std::to_string(i)+" operations="+std::to_string(n->BreadcrumbCount)+" last_completed="+std::to_string(n->pLastBreadcrumbValue?*n->pLastBreadcrumbValue:0));}D3D12_DRED_PAGE_FAULT_OUTPUT1 fault{};if(SUCCEEDED(dred->GetPageFaultAllocationOutput1(&fault)))log("DRED page_fault_address="+std::to_string(fault.PageFaultVA));}
}
void check(HRESULT hr,const char* where){if(FAILED(hr)){std::ostringstream s;s<<where<<" HRESULT=0x"<<std::hex<<static_cast<unsigned long>(hr);throw std::runtime_error(s.str());}}
bool same(IUnknown* a,IUnknown* b){ComPtr<IUnknown>x,y;return a&&b&&SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(&x)))&&SUCCEEDED(b->QueryInterface(IID_PPV_ARGS(&y)))&&x==y;}
bool wait(Renderer& r,UINT64 value){if(!value)return true;const auto done=r.fence->GetCompletedValue();if(done==UINT64_MAX)return false;if(done>=value)return true;return SUCCEEDED(r.fence->SetEventOnCompletion(value,r.event))&&WaitForSingleObject(r.event,1000)==WAIT_OBJECT_0;}
bool clear(Renderer& r){
    if(r.fence&&!wait(r,r.next)&&r.device&&r.device->GetDeviceRemovedReason()==S_OK){r.failed=true;log("GPU fence timeout; retaining in-flight TV resources");return false;}
    if(r.gui){auto* previous=ImGui::GetCurrentContext();ImGui::SetCurrentContext(r.gui);if(r.gui_backend)ImGui_ImplDX12_Shutdown();ImGui::DestroyContext(r.gui);ImGui::SetCurrentContext(previous==r.gui?nullptr:previous);r.gui=nullptr;r.gui_backend=false;}
    r.frames.clear();r.gui_fences.clear();r.list.Reset();r.pipeline.Reset();r.root.Reset();r.rtvs.Reset();r.srvs.Reset();r.fence.Reset();r.queue.Reset();r.submission_queue.Reset();r.device.Reset();r.chain.Reset();
    if(r.event)CloseHandle(r.event);r.event=nullptr;r.next=0;r.draws=0;r.gui_draws=0;r.failed=false;r.first=false;r.completed_first=false;r.diagnosed=false;return true;
}
D3D12_RESOURCE_DESC buffer_desc(UINT64 size){D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=size;d.Height=1;d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;return d;}
ComPtr<ID3D12Resource> upload_buffer(Renderer& r,UINT64 bytes){auto d=buffer_desc(bytes);D3D12_HEAP_PROPERTIES h{};h.Type=D3D12_HEAP_TYPE_UPLOAD;ComPtr<ID3D12Resource> b;check(r.device->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&b)),"Create TV upload buffer");return b;}
// Read-only diagnostics, throttled independently of the existing setup/retry path.
void log_device_pair(Renderer& r,IDXGISwapChain* chain,ID3D12CommandQueue* queue,ID3D12Device* qdev,HRESULT queue_result,bool equal){
    static ULONGLONG next{};if(GetTickCount64()<next)return;next=GetTickCount64()+5000;
    const auto type=queue->GetDesc().Type;const auto a=r.device->GetAdapterLuid();
    ComPtr<IUnknown> ci,qi;const auto chr=r.device->QueryInterface(IID_PPV_ARGS(&ci));
    const auto qhr=qdev?qdev->QueryInterface(IID_PPV_ARGS(&qi)):E_NOINTERFACE;
    std::ostringstream out;out<<"TV device validation: chain="<<chain<<" queue="<<queue<<" chain_device="<<r.device.Get()<<" queue_device="<<qdev
       <<" queue_GetDevice="<<hr_text(queue_result)<<" chain_IUnknown_hr="<<hr_text(chr)<<" queue_IUnknown_hr="<<hr_text(qhr)
       <<" chain_IUnknown="<<ci.Get()<<" queue_IUnknown="<<qi.Get()<<" same_identity="<<equal
       <<" queue_type="<<static_cast<unsigned>(type)<<" expected_DIRECT="<<static_cast<unsigned>(D3D12_COMMAND_LIST_TYPE_DIRECT)
       <<" chain_adapter="<<a.HighPart<<":"<<a.LowPart<<" chain_nodes="<<r.device->GetNodeCount()<<" chain_removed="<<hr_text(r.device->GetDeviceRemovedReason());
    if(qdev){const auto b=qdev->GetAdapterLuid();out<<" queue_adapter="<<b.HighPart<<":"<<b.LowPart<<" same_adapter="<<(a.HighPart==b.HighPart&&a.LowPart==b.LowPart)<<" queue_nodes="<<qdev->GetNodeCount()<<" queue_removed="<<hr_text(qdev->GetDeviceRemovedReason());}
    log(out.str());
    ComPtr<ID3D12Resource> buffer;const auto bh=chain->GetBuffer(0,IID_PPV_ARGS(&buffer));ComPtr<ID3D12Device> bd;
    const auto dh=buffer?buffer->GetDevice(IID_PPV_ARGS(&bd)):E_NOINTERFACE;ComPtr<IUnknown> bi;const auto ih=bd?bd->QueryInterface(IID_PPV_ARGS(&bi)):E_NOINTERFACE;
    std::ostringstream back;back<<"TV backbuffer ownership: GetBuffer="<<hr_text(bh)<<" GetDevice="<<hr_text(dh)<<" IUnknown_hr="<<hr_text(ih)<<" device="<<bd.Get()<<" IUnknown="<<bi.Get();
    if(bd){const auto b=bd->GetAdapterLuid();back<<" adapter="<<b.HighPart<<":"<<b.LowPart<<" same_as_chain="<<same(bd.Get(),r.device.Get())<<" same_as_queue="<<same(bd.Get(),qdev);}
    log(back.str());
    log(std::string("TV validation verdict: ")+(FAILED(queue_result)?"queue GetDevice failed":!equal?"device COM identities differ":type!=D3D12_COMMAND_LIST_TYPE_DIRECT?"queue is not DIRECT":"accepted")+"; raw identity only; canonical Streamline validation follows");
}
void setup(Renderer& r,IDXGISwapChain* chain,ID3D12CommandQueue* queue){
    if(!clear(r))throw std::runtime_error("Previous graphics resources are still in flight");
    r.queue=queue;
    check(chain->QueryInterface(IID_PPV_ARGS(&r.chain)),"Swapchain3");check(chain->GetDevice(IID_PPV_ARGS(&r.device)),"Swapchain device");
    ComPtr<ID3D12Device> qdev;const auto queue_result=queue->GetDevice(IID_PPV_ARGS(&qdev));const bool raw_equal=same(qdev.Get(),r.device.Get());log_device_pair(r,chain,queue,qdev.Get(),queue_result,raw_equal);check(queue_result,"Queue device");
    bool chain_unwrapped{},queue_unwrapped{};
    auto native_device=native_interface(r.device.Get(),chain_unwrapped);auto native_queue_device=native_interface(qdev.Get(),queue_unwrapped);
    const bool native_equal=same(native_device.Get(),native_queue_device.Get());
    log("TV canonical device validation: chain_unwrapped="+std::to_string(chain_unwrapped)+" queue_device_unwrapped="+std::to_string(queue_unwrapped)+" same_native_identity="+std::to_string(native_equal));
    if(!native_equal||queue->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT)throw std::runtime_error("Canonical queue/swapchain ownership mismatch");
    bool submission_unwrapped{};auto submission_queue=native_interface(queue,submission_unwrapped);
    ComPtr<ID3D12Device> submission_device;check(submission_queue->GetDevice(IID_PPV_ARGS(&submission_device)),"Native submission queue device");
    bool submission_device_unwrapped{};submission_device=native_interface(submission_device.Get(),submission_device_unwrapped);
    if(!same(native_device.Get(),submission_device.Get()))throw std::runtime_error("Native submission queue does not own the screen device");
    r.device=native_device;r.queue=queue;r.submission_queue=submission_queue;
#ifndef TV_GPU_TEST
    // Set defaults from the adapter that actually owns the TV swapchain.
    // Do this only after the queue/device native identity checks have passed.
    {ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter1> adapter;DXGI_ADAPTER_DESC1 adapter_desc{};
      if(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) &&
         SUCCEEDED(factory->EnumAdapterByLuid(r.device->GetAdapterLuid(),IID_PPV_ARGS(&adapter))) &&
         SUCCEEDED(adapter->GetDesc1(&adapter_desc)))
          depth::configure_backend_for_adapter(adapter_desc.VendorId);
      else log("GPU occlusion defaults: unable to identify adapter; retaining current backend");}
#endif
    log("TV native submission queue verified: unwrapped="+std::to_string(submission_unwrapped));
    DXGI_SWAP_CHAIN_DESC desc{};check(chain->GetDesc(&desc),"Swapchain description");if(desc.BufferCount<2||desc.BufferCount>8)throw std::runtime_error("Unsupported backbuffer count");
    D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=desc.BufferCount;check(r.device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&r.rtvs)),"TV RTV heap");
    hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=desc.BufferCount*2+1;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;check(r.device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&r.srvs)),"TV SRV heap");
    auto rt=r.rtvs->GetCPUDescriptorHandleForHeapStart();auto srv=r.srvs->GetGPUDescriptorHandleForHeapStart();r.frames.resize(desc.BufferCount);r.gui_fences.assign(desc.BufferCount,0);
    for(auto& f:r.frames){const auto i=static_cast<UINT>(&f-r.frames.data());check(r.chain->GetBuffer(i,IID_PPV_ARGS(&f.buffer)),"TV backbuffer");f.rtv=rt;f.srv=srv;r.device->CreateRenderTargetView(f.buffer.Get(),nullptr,rt);
        check(r.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&f.allocator)),"TV allocator");f.vertices=upload_buffer(r,sizeof(Vertex)*max_curve_vertices_per_screen*max_world_screens);
        check(r.device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,f.allocator.Get(),nullptr,IID_PPV_ARGS(&f.commands)),"TV per-frame command list");check(f.commands->Close(),"Close initial TV list");f.commands->SetName(L"JishsVirtualTV frame");
        auto null_depth=r.srvs->GetCPUDescriptorHandleForHeapStart();null_depth.ptr+=(i*2+1)*r.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);D3D12_SHADER_RESOURCE_VIEW_DESC null_view{};null_view.Format=DXGI_FORMAT_R32_FLOAT;null_view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;null_view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;null_view.Texture2D.MipLevels=1;r.device->CreateShaderResourceView(nullptr,&null_view,null_depth);
        rt.ptr+=r.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);srv.ptr+=2*r.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
    D3D12_DESCRIPTOR_RANGE range{};range.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;range.NumDescriptors=2;
    D3D12_ROOT_PARAMETER params[2]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[0].Constants.Num32BitValues=60;params[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
    params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[1].DescriptorTable.NumDescriptorRanges=1;params[1].DescriptorTable.pDescriptorRanges=&range;params[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC sampler{};sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;sampler.MaxLOD=D3D12_FLOAT32_MAX;sampler.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;
    D3D12_ROOT_SIGNATURE_DESC rd{};rd.NumParameters=2;rd.pParameters=params;rd.NumStaticSamplers=1;rd.pStaticSamplers=&sampler;rd.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> signature,error;check(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&signature,&error),"Serialize TV root signature");check(r.device->CreateRootSignature(0,signature->GetBufferPointer(),signature->GetBufferSize(),IID_PPV_ARGS(&r.root)),"TV root signature");
    constexpr char shader[]=R"(
cbuffer Camera:register(b0){float4 right;float4 up;float4 back;float4 origin;float4 projection;row_major float4x4 gameProjection;float4 depthRect;float4 depthInfo;float4 depthOptions;float4 captureRight;float4 captureUp;float4 captureBack;}
struct V {float3 p:POSITION;float2 uv:TEXCOORD0;};struct P{float4 p:SV_POSITION;float2 uv:TEXCOORD0;float distance:TEXCOORD1;float3 world:TEXCOORD2;};
P vs(V v){P o;float3 d=v.p-origin.xyz;float z=-dot(d,back.xyz);float x=dot(d,right.xyz),y=dot(d,up.xyz);o.p=(depthInfo.w>0||projection.z>0)?mul(float4(x,y,z*depthOptions.w,1),gameProjection):float4(x*projection.x,y*projection.y,z-0.05,z);
 // With ReShade depth, present the fixed world quad using the FINAL game
 // camera's focal length, not the older estimated depth-capture focal length.
 // The depth pixel shader still exclusively uses the capture projection.
 if(depthOptions.z<0 && projection.w>0){o.p.x+=x*(projection.x-gameProjection[0][0]);o.p.y+=y*(projection.y-gameProjection[1][1]);}
 o.uv=v.uv;o.distance=z;o.world=v.p;return o;}
Texture2D video:register(t0);SamplerState linearSampler:register(s0);
Texture2D<float> sceneDepth:register(t1);
float4 ps(P p):SV_TARGET{
 if(depthInfo.w>0){
  uint w,h;sceneDepth.GetDimensions(w,h);
  // Streamline: native screen-space lookup, unchanged.
  // ReShade: the captured depth belongs to an earlier camera. Project the
  // actual world-space TV pixel through THAT camera, independently of the
  // camera currently presenting the TV quad. A stale depth map will no
  // longer slide over the quad merely because the player is moving.
  float2 uv;
  float distanceToCapture=p.distance;
  if(depthOptions.z<0){
   float3 originCapture=float3(captureRight.w,captureUp.w,captureBack.w);
   float3 delta=p.world-originCapture;
   float3 viewPos=float3(dot(delta,captureRight.xyz),dot(delta,captureUp.xyz),-dot(delta,captureBack.xyz));
   distanceToCapture=viewPos.z;
   float4 clipCapture=mul(float4(viewPos.xy,viewPos.z*depthOptions.w,1),gameProjection);
   if(clipCapture.w<=0.00001)return float4(video.Sample(linearSampler,p.uv).rgb,1);
   float2 captureUV=clipCapture.xy/clipCapture.w*float2(0.5,-0.5)+0.5;
   if(any(captureUV<0.0)||any(captureUV>1.0))return float4(video.Sample(linearSampler,p.uv).rgb,1);
   uv=captureUV*depthRect.zw+depthRect.xy+depthOptions.xy;
  }else{
   uv=(p.p.xy/depthInfo.xy)*depthRect.zw+depthRect.xy+depthOptions.xy;
  }
  int2 at=int2(uv*float2(w,h));
  float4 biased=mul(float4(0,0,max(distanceToCapture-abs(depthOptions.z),0.001)*depthOptions.w,1),gameProjection);
  float tvDepth=biased.z/biased.w;
  float closest=depthInfo.z>0?0:1;uint occluding=0;
  [unroll]for(int y=0;y<2;++y)[unroll]for(int x=0;x<2;++x){
   float d=sceneDepth.Load(int3(clamp(at+int2(x,y),int2(0,0),int2(w-1,h-1)),0));
   closest=depthInfo.z>0?max(closest,d):min(closest,d);
   if(depthInfo.z>0?d>tvDepth+1e-7:d<tvDepth-1e-7)++occluding;
  }
  // ReShade's independently captured depth fluctuates along single-pixel
  // edges. Requiring 3/4 agreeing texels avoids camera-motion sparkle.
  // Streamline keeps the original conservative any-sample comparison.
  if(depthOptions.z<0 ? occluding>=3 : (depthInfo.z>0?closest>tvDepth+1e-7:closest<tvDepth-1e-7))discard;
 }
 return float4(video.Sample(linearSampler,p.uv).rgb,1);
}
)";
    ComPtr<ID3DBlob> vs,ps;check(D3DCompile(shader,sizeof(shader),"TV",nullptr,nullptr,"vs","vs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&vs,&error),"Compile TV vertex shader");check(D3DCompile(shader,sizeof(shader),"TV",nullptr,nullptr,"ps","ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&ps,&error),"Compile TV pixel shader");
    D3D12_INPUT_ELEMENT_DESC input[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC p{};p.pRootSignature=r.root.Get();p.VS={vs->GetBufferPointer(),vs->GetBufferSize()};p.PS={ps->GetBufferPointer(),ps->GetBufferSize()};p.InputLayout={input,2};p.SampleMask=UINT_MAX;p.SampleDesc.Count=1;p.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;p.NumRenderTargets=1;p.RTVFormats[0]=r.frames[0].buffer->GetDesc().Format;
    p.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;p.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;p.RasterizerState.DepthClipEnable=TRUE;
    p.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
    p.DepthStencilState.DepthEnable=FALSE;p.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ZERO;p.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;
    check(r.device->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&r.pipeline)),"TV graphics pipeline");
    r.list=r.frames[0].commands;
    check(r.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&r.fence)),"TV fence");r.event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!r.event)throw std::runtime_error("TV fence event creation failed");
    r.gui=ImGui::CreateContext();{GuiScope scope(r.gui);auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;
        ImGui_ImplDX12_InitInfo init;init.Device=r.device.Get();init.CommandQueue=queue;init.NumFramesInFlight=static_cast<int>(desc.BufferCount);init.RTVFormat=r.frames[0].buffer->GetDesc().Format;init.SrvDescriptorHeap=r.srvs.Get();
        init.LegacySingleSrvCpuDescriptor=r.srvs->GetCPUDescriptorHandleForHeapStart();init.LegacySingleSrvGpuDescriptor=r.srvs->GetGPUDescriptorHandleForHeapStart();const auto step=r.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);init.LegacySingleSrvCpuDescriptor.ptr+=std::size_t(desc.BufferCount)*2*step;init.LegacySingleSrvGpuDescriptor.ptr+=std::uint64_t(desc.BufferCount)*2*step;
        if(!ImGui_ImplDX12_Init(&init))throw std::runtime_error("Reused ImGui DX12 backend initialization failed");r.gui_backend=true;if(!ImGui_ImplDX12_CreateDeviceObjects())throw std::runtime_error("ImGui font/pipeline upload failed");
    }
    log("DX12 setup: buffers="+std::to_string(desc.BufferCount)+" size="+std::to_string(desc.BufferDesc.Width)+"x"+std::to_string(desc.BufferDesc.Height)+" format="+std::to_string(r.frames[0].buffer->GetDesc().Format)+" backend=ReSkate-2.1.1-ImGui");
}
void barrier(Renderer& r,ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=resource;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;b.Transition.StateBefore=before;b.Transition.StateAfter=after;r.list->ResourceBarrier(1,&b);}
void texture(Renderer& r,Frame& f,const VlcPlayer::Frame& pixels,UINT index){
    const bool create=!f.texture||f.width!=pixels.width||f.height!=pixels.height;
    if(create){f.texture.Reset();f.upload.Reset();f.sequence=0;D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=pixels.width;d.Height=pixels.height;d.DepthOrArraySize=1;d.MipLevels=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.SampleDesc.Count=1;
        D3D12_HEAP_PROPERTIES h{};h.Type=D3D12_HEAP_TYPE_DEFAULT;check(r.device->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&f.texture)),"TV video texture");
        UINT64 bytes{};r.device->GetCopyableFootprints(&d,0,1,0,&f.footprint,nullptr,nullptr,&bytes);f.upload=upload_buffer(r,bytes);f.width=pixels.width;f.height=pixels.height;
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=d.Format;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(0,1,2,D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1);srv.Texture2D.MipLevels=1;
        auto cpu=r.srvs->GetCPUDescriptorHandleForHeapStart();cpu.ptr+=std::size_t(index)*2*r.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);r.device->CreateShaderResourceView(f.texture.Get(),&srv,cpu);
    }
    if(f.sequence==pixels.sequence&&!create)return;
    if(!create)barrier(r,f.texture.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
    void* mapped{};D3D12_RANGE none{};check(f.upload->Map(0,&none,&mapped),"Map TV texture upload");for(unsigned y=0;y<pixels.height;++y)std::memcpy(static_cast<unsigned char*>(mapped)+f.footprint.Offset+std::size_t(y)*f.footprint.Footprint.RowPitch,pixels.bgra.data()+std::size_t(y)*pixels.width*4,std::size_t(pixels.width)*4);f.upload->Unmap(0,nullptr);
    D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=f.texture.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;src.pResource=f.upload.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=f.footprint;r.list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);barrier(r,f.texture.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);f.sequence=pixels.sequence;
}
void draw_front(Renderer& r,Frame& f,float width,float height,const VlcPlayer::Frame& pixels){
    GuiScope scope(r.gui);auto& io=ImGui::GetIO();io.DisplaySize=ImVec2(width,height);io.DeltaTime=1.0f/60;
    ImGui_ImplDX12_SetOutputTransfer(f.buffer->GetDesc().Format==DXGI_FORMAT_R16G16B16A16_FLOAT?1:0,200);
    ImGui_ImplDX12_NewFrame();ImGui::NewFrame();
    const float aspect=static_cast<float>(pixels.width)/pixels.height;
    const float w=std::min(width*.55f,height*.60f*aspect),h=w/aspect;
    const ImVec2 left((width-w)*.5f,(height-h)*.5f),right(left.x+w,left.y+h);
    auto* list=ImGui::GetBackgroundDrawList();list->AddRectFilled(ImVec2(left.x-4,left.y-4),ImVec2(right.x+4,right.y+4),IM_COL32(255,255,255,255));
    list->AddImage(static_cast<ImTextureID>(f.srv.ptr),left,right);
    ImGui::Render();ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(),r.list.Get());
}
void progress(Renderer& r){
    if(!r.fence)return;const auto completed=r.fence->GetCompletedValue();
    if(completed==UINT64_MAX){r.failed=true;device_diagnostics(r,"GPU fence reports device removal",DXGI_ERROR_DEVICE_REMOVED);return;}
    if(r.first&&!r.completed_first&&completed){r.completed_first=true;log("First TV GPU submission completed: fence="+std::to_string(completed));report(L"TV GPU work completed; attached objects use world-space perspective");}
    const auto now=GetTickCount64();if(debug_logging()&&now>=r.next_report){r.next_report=now+5000;log("TV graphics progress: draws="+std::to_string(r.draws)+" last_signal="+std::to_string(r.next)+" completed="+std::to_string(completed)+" fence_waits="+std::to_string(r.skips)+" mode="+std::to_string(static_cast<int>(screen_mode.load())));}
}
void poll_device_health(){auto& r=renderer();std::unique_lock lock(r.mutex,std::try_to_lock);if(!lock.owns_lock()||!r.device)return;const auto removed=r.device->GetDeviceRemovedReason();if(FAILED(removed)){r.failed=true;device_diagnostics(r,"Background device-health check",removed);}else progress(r);}
bool depth_inputs(Renderer& r,Frame& f,const Scene& scene,UINT index,unsigned width,unsigned height,std::array<float,60>& constants){
    f.depth_image.reset();const auto sample=depth::snapshot(r.device.Get(),scene,width,height);
    if(sample.image && depth::backend()==depth::Backend::ReShadeNative && !sample.capture_pose_valid){
        static GateTrace missing;missing.sample("reshade_reprojection","missing_capture_pose",[]{return "Depth capture has no paired camera; preserve TV without depth";});
        return false;
    }
    if(sample.image&&!depth::native::reserve(sample.image)){
        static GateTrace reserve_trace;
        reserve_trace.sample("world_occlusion","depth_reader_busy",[]{return "ReShade/Streamline depth copy was not available for a safe GPU read";});
        return false;
    }
    f.depth_image=sample.image;
    static GateTrace trace;trace.sample("world_occlusion",sample.image?"active":"unavailable",[]{return depth::status();});
    if(!sample.image)return false;
    const auto& image=*sample.image;auto texture=image.texture;
    if(image.direct){
        const auto desc=texture->GetDesc();const auto format=depth::copy_format(desc.Format);
        const auto old=f.depth_copy?f.depth_copy->GetDesc():D3D12_RESOURCE_DESC{};
        if(!f.depth_copy||old.Width!=desc.Width||old.Height!=desc.Height||old.Format!=format||old.MipLevels!=desc.MipLevels||old.Flags!=static_cast<D3D12_RESOURCE_FLAGS>(desc.Flags&~D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)){
            auto d=desc;d.Format=format;d.Flags=static_cast<D3D12_RESOURCE_FLAGS>(d.Flags&~D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
            ComPtr<ID3D12Resource> copy;check(r.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&copy)),"Create presentation depth copy");f.depth_copy=copy;
        }
        barrier(r,texture.Get(),image.source_state,D3D12_RESOURCE_STATE_COPY_SOURCE);barrier(r,f.depth_copy.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
        r.list->CopyResource(f.depth_copy.Get(),texture.Get());
        barrier(r,f.depth_copy.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);barrier(r,texture.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,image.source_state);texture=f.depth_copy;
    }else if(image.ready&&image.value)check(r.submission_queue->Wait(image.ready.Get(),image.value),"Wait for scene depth writer");
    const auto desc=texture->GetDesc();D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.Format=image.view_format;view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;view.Texture2D.MipLevels=1;
    auto cpu=r.srvs->GetCPUDescriptorHandleForHeapStart();cpu.ptr+=(index*2+1)*r.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);r.device->CreateShaderResourceView(texture.Get(),&view,cpu);
    const auto& camera=sample.camera;
    if(depth::backend()==depth::Backend::ReShadeNative){
        // The ReShade copy and this capture pose were recorded together. The
        // vertex shader still renders from the CURRENT camera; pixel-depth
        // lookup reprojects every world-space TV fragment into this camera.
        const auto& pose=sample.capture_pose.world;
        // Three float4s = right/up/back basis vectors plus camera origin.
        // Root constants: 60 DWORD + one descriptor table = 61 (< 64).
        for(unsigned axis=0;axis<3;++axis){
            const unsigned n=axis*4;
            for(unsigned col=0;col<3;++col)constants[48+n+col]=pose[n+col];
            constants[48+n+3]=pose[12+axis];
        }
    }
    std::array<float,16> final_pose{};const bool final=depth::world_camera(camera,scene.camera,final_pose);
    if(final && depth::backend()!=depth::Backend::ReShadeNative)std::copy(final_pose.begin(),final_pose.end(),constants.begin());
    static GateTrace pose_trace;pose_trace.sample("world_camera",final?"render_frame_pose":"native_pose_fallback",[&]{return "frame="+std::to_string(image.frame)+" viewport="+std::to_string(image.viewport)+" translation_delta="+std::to_string(camera.cameraPos.x-scene.camera.world[12])+","+std::to_string(camera.cameraPos.y-scene.camera.world[13])+","+std::to_string(camera.cameraPos.z-scene.camera.world[14])+" vertices=object_world_coordinates";});
    std::memcpy(constants.data()+20,&camera.cameraViewToClip,sizeof(float)*16);
    constants[36]=float(image.extent.left)/float(desc.Width);constants[37]=float(image.extent.top)/desc.Height;constants[38]=float(image.extent.width)/float(desc.Width);constants[39]=float(image.extent.height)/desc.Height;
    constants[40]=float(width);constants[41]=float(height);constants[42]=camera.depthInverted==sl::eTrue?1.f:0.f;constants[43]=1;
    constants[44]=camera.jitterOffset.x/float(desc.Width);constants[45]=camera.jitterOffset.y/desc.Height;
    // Signed 2cm bias: negative requests stable 3-of-4 ReShade edge testing.
    // NVIDIA Streamline keeps its existing positive-bias comparison.
    constants[46]=depth::backend()==depth::Backend::ReShadeNative?-0.02f:0.02f;
    constants[47]=camera.cameraViewToClip[2].w;
    if(depth::backend()==depth::Backend::ReShadeNative){
        static GateTrace paired;
        paired.sample("reshade_reprojection","capture_camera_paired",[&]{
            const auto& a=sample.capture_pose.world;
            const auto& b=scene.camera.world;
            return "image_frame="+std::to_string(image.frame)+" camera_capture="+
                std::to_string(a[12])+","+std::to_string(a[13])+","+std::to_string(a[14])+
                " camera_present="+std::to_string(b[12])+","+std::to_string(b[13])+","+std::to_string(b[14])+
                " independent_depth_uv=1";
        });
    }
    f.depth_image=sample.image;return true;
}
void draw(IDXGISwapChain* chain,ComPtr<ID3D12CommandQueue> queue,const Scene& scene,const VlcPlayer::Frame& pixels,bool front=false,const std::vector<Scene>* additional=nullptr){
    depth::probe::TvScope diagnostic_tv_scope;
    auto& r=renderer();std::lock_guard lock(r.mutex);if(r.failed){if(same(r.chain.Get(),chain)&&r.queue==queue)return;if(!clear(r))return;}
    try{
        if(!same(r.chain.Get(),chain)||r.queue!=queue)setup(r,chain,queue.Get());const UINT index=r.chain->GetCurrentBackBufferIndex();if(index>=r.frames.size())throw std::runtime_error("Invalid TV backbuffer index");auto& f=r.frames[index];
        struct NativeReaderEnd {Frame& frame;ID3D12CommandQueue* queue;~NativeReaderEnd(){depth::native::reader_end(frame.depth_image,queue);}} native_reader_end{f,r.submission_queue.Get()};
        progress(r);if(r.failed)return;
        ComPtr<ID3D12Resource> current;check(chain->GetBuffer(index,IID_PPV_ARGS(&current)),"Current backbuffer");if(!same(current.Get(),f.buffer.Get()))throw std::runtime_error("TV backbuffer identity changed without resize");
        const auto upload_slot=static_cast<std::size_t>(r.gui_draws%r.gui_fences.size());const auto completed=r.fence->GetCompletedValue();
        if(completed<f.fence||(front&&completed<r.gui_fences[upload_slot])){++r.skips;if(!wait(r,f.fence)||(front&&!wait(r,r.gui_fences[upload_slot])))throw std::runtime_error("TV GPU reuse fence timed out; rendering stopped instead of flickering");}
        f.depth_image.reset();r.list=f.commands;check(f.allocator->Reset(),"Reset TV allocator");check(r.list->Reset(f.allocator.Get(),r.pipeline.Get()),"Reset TV list");texture(r,f,pixels,index);
        const auto d=f.buffer->GetDesc();const float width=static_cast<float>(d.Width),height=static_cast<float>(d.Height);
        if(front){barrier(r,f.buffer.Get(),D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_RENDER_TARGET);r.list->OMSetRenderTargets(1,&f.rtv,FALSE,nullptr);ID3D12DescriptorHeap* heaps[]{r.srvs.Get()};r.list->SetDescriptorHeaps(1,heaps);draw_front(r,f,width,height,pixels);}
        else {
        // One decoded texture, one depth capture and one GPU submission for all
        // world screens. Their 6-vertex quads are in a single upload buffer;
        // none of them owns a separate VLC player or playback clock.
        // Flat TVs still use six vertices. Only curved screens subdivide,
        // without creating additional textures, players, queues or draw calls.
        // Heap storage avoids overflowing the game's render-thread stack at 64 TVs.
        thread_local std::vector<Vertex> vertices;
        vertices.clear();
        unsigned screen_count=0;
        const auto append_mesh=[&](const Scene& target){
            if(screen_count>=max_world_screens || !target.attached || !valid(target.placement,target.screen))return;
            const auto first=vertices.size();
            append_screen_mesh(target.screen,vertices);
            for(std::size_t i=first;i<vertices.size();++i){
                auto& p=vertices[i].position;
                p={p.x*target.scale.x,p.y*target.scale.y,p.z*target.scale.z};
                p=rotate(target.placement.rotation,p);
                p={p.x+target.placement.position.x,p.y+target.placement.position.y,p.z+target.placement.position.z};
            }
            ++screen_count;
        };
        append_mesh(scene);
        if(additional)for(const auto& extra:*additional)append_mesh(extra);
        if(!screen_count)throw std::runtime_error("No valid world TV geometry");
        if(vertices.size()>max_curve_vertices_per_screen*max_world_screens)
            throw std::runtime_error("Curved screen mesh exceeds TV vertex buffer");
        const unsigned vertex_bytes=static_cast<unsigned>(sizeof(Vertex)*vertices.size());
        void* data{};D3D12_RANGE none{};check(f.vertices->Map(0,&none,&data),"Map TV vertices");
        std::memcpy(data,vertices.data(),vertex_bytes);f.vertices->Unmap(0,nullptr);
        const Camera presentation_camera=scene.camera;
        // Freeze ONE camera choice per TV draw. A second read performed after
        // the game has rendered can belong to the NEXT game frame, producing
        // apparent TV movement even when the attached vertices never change.
        // Prefer the game's tagged final camera (below); use the verified
        // tick snapshot as the fallback, not a new later camera sample.
        // This must never use player position as a TV placement offset.
        const float focal=1/std::tan(presentation_camera.vertical_fov*3.14159265359f/360);
        std::array<float,60> constants{};std::copy(presentation_camera.world.begin(),presentation_camera.world.end(),constants.begin());constants[16]=focal*height/width;constants[17]=focal;
        if(depth::backend()!=depth::Backend::ReShadeNative){
            std::array<float,16> latest_pose{};sl::float4x4 latest_projection{};
            if(depth::latest_world_camera(scene,latest_pose,latest_projection)){
                std::copy(latest_pose.begin(),latest_pose.end(),constants.begin());
                std::memcpy(constants.data()+20,&latest_projection,sizeof(float)*16);
                constants[18]=1;constants[47]=latest_projection[2].w;
            }
        }
        Scene depth_scene=scene;
        if(depth::backend()==depth::Backend::ReShadeNative)depth_scene.camera=presentation_camera;
        try{depth_inputs(r,f,depth_scene,index,static_cast<unsigned>(width),static_cast<unsigned>(height),constants);}catch(const std::exception& e){static GateTrace trace;trace.sample("world_occlusion","copy_unavailable",[&]{return std::string(e.what());});constants[43]=0;}
        if(depth::backend()==depth::Backend::ReShadeNative){
            // Retain world-space object vertices. The native depth sample's
            // synthetic camera metadata must not replace the pose used for TV
            // presentation. On NVIDIA, use the actual final render camera from
            // Streamline, but keep the ReShade native depth texture.
            std::copy(presentation_camera.world.begin(),presentation_camera.world.end(),constants.begin());
            Scene corrected=scene;corrected.camera=presentation_camera;
            std::array<float,16> final_pose{};sl::float4x4 final_projection{};
            const bool final=depth::latest_world_camera(corrected,final_pose,final_projection);
            if(final){
                std::copy(final_pose.begin(),final_pose.end(),constants.begin());
                // Keep the depth-capture matrix in constants[20..35] for
                // reprojection. A final game-render camera may have a slightly
                // different focal length: adjust only the PRESENTATION VS
                // geometry through the spare projection.xy/w constants.
                if(constants[43]>0){
                    const auto final_focal=presentation::final_focal(final_projection[0].x,final_projection[1].y,
                                                                      constants[20],constants[25]);
                    if(final_focal.valid){constants[16]=final_focal.x;constants[17]=final_focal.y;constants[19]=1.f;}
                    static GateTrace focal_trace;
                    focal_trace.sample("world_projection",final_focal.valid?"final_focal":"capture_focal_fallback",[&]{
                        return "final_x="+std::to_string(final_projection[0].x)+
                               " capture_x="+std::to_string(constants[20])+" final_y="+
                               std::to_string(final_projection[1].y)+" capture_y="+
                               std::to_string(constants[25])+" geometry_only=1";
                    });
                } else {
                    // No depth means no paired projection to protect; use the
                    // final game projection for the TV itself as before.
                    std::memcpy(constants.data()+20,&final_projection,sizeof(float)*16);
                    constants[18]=1;constants[47]=final_projection[2].w;
                }
            }
            static GateTrace lock_trace;
            lock_trace.sample("world_lock",final?"final_render_camera":"native_tick_camera",[&]{
                return "object_world_locked=1 backend=reshade final_camera="+std::to_string(final)+
                " camera_latched_at_tick=1 final_projection_override="+std::to_string(constants[19]>0);
            });
        }
        barrier(r,f.buffer.Get(),D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_RENDER_TARGET);r.list->OMSetRenderTargets(1,&f.rtv,FALSE,nullptr);r.list->SetGraphicsRootSignature(r.root.Get());r.list->SetGraphicsRoot32BitConstants(0,60,constants.data(),0);
        ID3D12DescriptorHeap* heaps[]{r.srvs.Get()};r.list->SetDescriptorHeaps(1,heaps);r.list->SetGraphicsRootDescriptorTable(1,f.srv);D3D12_VIEWPORT vp{0,0,width,height,0,1};D3D12_RECT scissor{0,0,static_cast<LONG>(d.Width),static_cast<LONG>(d.Height)};r.list->RSSetViewports(1,&vp);r.list->RSSetScissorRects(1,&scissor);r.list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        D3D12_VERTEX_BUFFER_VIEW vb{f.vertices->GetGPUVirtualAddress(),vertex_bytes,sizeof(Vertex)};r.list->IASetVertexBuffers(0,1,&vb);r.list->DrawInstanced(static_cast<UINT>(vertices.size()),1,0,0);
        }
        barrier(r,f.buffer.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PRESENT);check(r.list->Close(),"Close TV draw");
        ID3D12CommandList* lists[]{r.list.Get()};submitting=true;r.submission_queue->ExecuteCommandLists(1,lists);submitting=false;
        f.fence=++r.next;check(r.submission_queue->Signal(r.fence.Get(),f.fence),"Signal TV fence");if(front){r.gui_fences[upload_slot]=f.fence;++r.gui_draws;}++r.draws;if(!r.first){r.first=true;log(std::string("First TV draw submitted (not yet GPU confirmed): mode=")+(front?"front-ImGui":"attached-world")+" sequence="+std::to_string(pixels.sequence)+" size="+std::to_string(pixels.width)+"x"+std::to_string(pixels.height));}
    }catch(const std::exception& e){submitting=false;r.failed=true;static ULONGLONG next_error{};static unsigned suppressed{};if(GetTickCount64()>=next_error){next_error=GetTickCount64()+5000;log(std::string("TV renderer disabled: ")+e.what()+" repeated_errors_suppressed="+std::to_string(suppressed));suppressed=0;device_diagnostics(r,"TV draw failure",E_FAIL);}else ++suppressed;}
}
const VlcPlayer::Frame& test_pattern(){static const auto pixels=[] {VlcPlayer::Frame f;f.width=320;f.height=180;f.sequence=UINT64_MAX-1;f.bgra.resize(320*180*4);const unsigned char colours[8][3]={{255,255,255},{0,255,255},{255,255,0},{0,255,0},{255,0,255},{0,0,255},{255,0,0},{0,0,0}};for(unsigned y=0;y<180;++y)for(unsigned x=0;x<320;++x){const auto offset=(y*320+x)*4;for(unsigned c=0;c<3;++c)f.bgra[offset+c]=y<140?colours[x/40][c]:static_cast<unsigned char>(((x/10+y/10)%2)*255);f.bgra[offset+3]=255;}return f;}();return pixels;}
void render(IDXGISwapChain* chain,ComPtr<ID3D12CommandQueue> queue){
    struct EndDepth {~EndDepth(){depth::end_present();}} end_depth;
    struct Info {const char* reason="forwarded_to_draw";bool attached{},camera_valid{};const char* camera_reason="not_sampled";ULONGLONG age{};std::uint64_t entity{},sequence{};int mode{};};Info info;info.mode=static_cast<int>(screen_mode.load());
    struct Trace {Info& i;~Trace(){static GateTrace trace;trace.sample("render_inputs",i.reason,[&]{std::ostringstream out;out<<"mode="<<i.mode<<" attached="<<i.attached<<" entity="<<i.entity<<" camera_valid="<<i.camera_valid<<" camera_reason="<<i.camera_reason<<" snapshot_age_ms="<<i.age<<" max_age_ms=250 sequence="<<i.sequence;return out.str();});}} trace{info};
    if(stopped.load()||submitting||screen_mode.load()==ScreenMode::Hidden){info.reason=stopped.load()?"renderer_stopped":submitting?"recursive_submission":"screen_hidden";return;}const bool front=screen_mode.load()==ScreenMode::Front;Scene scene;
    if(!front){scene=scene_snapshot();
        if(screen_mode.load()==ScreenMode::Attached && !scene.attached){
            // Restored attachment manifests may contain only secondary TVs.
            // Those are real, verified screens, but the old draw path rejected
            // them until the user manually created a primary attachment.
            // Use one as the render reference without changing saved roles;
            // a primary object can still be restored later.
            const auto restored=world_screens_snapshot();
            if(choose_presentation_screen(scene,restored)){
                static std::atomic<std::uint64_t> logged_entity{};
                if(logged_entity.exchange(scene.identity.entity)!=scene.identity.entity)
                    log("TV presentation resumed from restored additional screen (no manual primary attach)");
            }
        }
        if(screen_mode.load()==ScreenMode::SyncedWorld){
            if(borrowed_presentation_active()){
                auto screens=borrowed_screens_snapshot();
                if(screens.empty()){info.reason="shared_objects_pending_or_speaker_only";return;}
                scene=screens.front();
            }else{
                std::lock_guard sync_lock(sync_scene_mutex);
                scene.placement=sync_placement;scene.scale=sync_scale;scene.screen=sync_screen;scene.attached=true;
            }
        }
        // Camera refresh is independent of the 30Hz object/attachment poll.
        if(!latch_presentation_camera(scene,GetTickCount64(),render_camera_snapshot)){info.reason="presentation_camera_unavailable_or_attachment_stale";return;}
        // Mirror Streamline's continuous presentation: missing one tick-camera
        // sample must not silently drop the entire TV draw on the ReShade path.
        // Never retain an attachment that the game has detached or invalidated.
        if(depth::backend()==depth::Backend::ReShadeNative && scene.attached && scene.identity.entity){
            struct LastCamera {std::mutex mutex;Identity identity{};Camera camera{};ULONGLONG at{};};
            static LastCamera last;
            const auto now=GetTickCount64();
            const bool usable=scene.camera.valid && scene.at && now>=scene.at && now-scene.at<=250;
            const char* recovery=nullptr;
            ULONGLONG last_real_camera_age{};
            if(!usable){
                Camera refreshed{};
                if(render_camera_snapshot(refreshed)){
                    scene.camera=refreshed;scene.at=now;recovery="render_camera_refresh";
                }else{
                    std::lock_guard cache_lock(last.mutex);
                    // A *brief* single-frame hold is safe for presentation,
                    // but not for depth matching. Preserve identity and do not
                    // turn an absent object or stale game world into a TV.
                    if(last.camera.valid && last.identity==scene.identity &&
                       now>=last.at && now-last.at<=125){
                        scene.camera=last.camera;scene.at=now;
                        recovery="one_frame_camera_hold";
                    }
                }
            }
            if(scene.camera.valid && scene.at && now>=scene.at && now-scene.at<=250){
                std::lock_guard cache_lock(last.mutex);
                last.identity=scene.identity;last.camera=scene.camera;
                // Holding a sample does not extend its expiry indefinitely.
                if(!recovery || std::strcmp(recovery,"one_frame_camera_hold")!=0)last.at=now;
                last_real_camera_age=now-last.at;
            }
            if(recovery){static GateTrace recovery_trace;recovery_trace.sample("reshade_frame_continuity",recovery,[&]{
                return "attached=1 identity="+std::to_string(scene.identity.entity)+
                  " last_real_camera_ms="+std::to_string(last_real_camera_age)+" render_not_skipped=1";
            });}
        }
        // The restored placement can become live before the first camera
        // snapshot. Retry the verified read from this very render callback;
        // do not require opening F8 or manually attaching to prime rendering.
        // This is read-only and runs only when the existing snapshot is stale.
        if(scene.attached && (!scene.camera.valid || !scene.at ||
           GetTickCount64()<scene.at || GetTickCount64()-scene.at>250)){
            Camera refreshed{};
            if(render_camera_snapshot(refreshed)){
                scene.camera=refreshed;
                scene.at=GetTickCount64();
                static std::atomic<bool> logged{false};
                if(!logged.exchange(true))log("Restored TV camera acquired from presentation callback; no manual reattach required");
            }
        }
        info.attached=scene.attached;info.entity=scene.identity.entity;info.camera_valid=scene.camera.valid;info.camera_reason=scene.camera.diagnostic;
        info.age=scene.at && GetTickCount64()>=scene.at?GetTickCount64()-scene.at:UINT64_MAX;
        if(!scene.attached||!scene.camera.valid||info.age>250){info.reason=!scene.attached?"attachment_unavailable":!scene.camera.valid?"camera_invalid":"snapshot_stale";
            static ULONGLONG next_blocked_report{};
            if(GetTickCount64()>=next_blocked_report){next_blocked_report=GetTickCount64()+5000;
                debug_log(std::string("TV screen pending: ")+info.reason+" (scene attached="+std::to_string(scene.attached)+
                    " camera="+std::to_string(scene.camera.valid)+" age_ms="+std::to_string(info.age)+")");}
            report(L"Attached mode: waiting for a live attachment and camera");return;}}
    auto pixels=std::atomic_load(&latest);
    const auto& frame=force_pattern.load()||!pixels?test_pattern():*pixels;info.sequence=frame.sequence;
    static std::atomic<int> last_source{-1};const int source=force_pattern.load()||!pixels?0:1;if(source!=last_source.exchange(source))log(source?"Presentation source switched to decoded VLC frames":"Presentation source switched to colour-bar test pattern");
    if(!frame.width||!frame.height||frame.width>8192||frame.height>8192||frame.bgra.size()!=std::size_t(frame.width)*frame.height*4){info.reason="frame_dimensions_invalid";return;}
    DXGI_SWAP_CHAIN_DESC desc{};DWORD pid{};if(FAILED(chain->GetDesc(&desc))||!desc.OutputWindow){info.reason="swapchain_description_unavailable";return;}GetWindowThreadProcessId(desc.OutputWindow,&pid);if(pid!=GetCurrentProcessId()||desc.BufferDesc.Width<64||desc.BufferDesc.Height<64){info.reason="swapchain_window_or_size_rejected";return;}
    std::vector<Scene> additional;
    if(!front && (screen_mode.load()==ScreenMode::Attached || screen_mode.load()==ScreenMode::SyncedWorld)){
        auto world=screen_mode.load()==ScreenMode::SyncedWorld&&borrowed_presentation_active()?borrowed_screens_snapshot():world_screens_snapshot();
        for(auto& other:world)if(!(other.identity==scene.identity) && other.attached){
            other.camera=scene.camera;other.at=scene.at;
            additional.push_back(std::move(other));
        }
    }
    draw(chain,queue,scene,frame,front,additional.empty()?nullptr:&additional);
}
bool bind_pair(IDXGISwapChain* chain,ID3D12CommandQueue* queue){
    DXGI_SWAP_CHAIN_DESC desc{};DWORD pid{};if(FAILED(chain->GetDesc(&desc))||!desc.OutputWindow)return false;GetWindowThreadProcessId(desc.OutputWindow,&pid);if(pid!=GetCurrentProcessId()||desc.BufferDesc.Width<64||desc.BufferDesc.Height<64)return false;
    ComPtr<ID3D12Device> a,b;if(FAILED(chain->GetDevice(IID_PPV_ARGS(&a)))||FAILED(queue->GetDevice(IID_PPV_ARGS(&b)))||!same(a.Get(),b.Get()))return false;
    std::lock_guard lock(queue_mutex);if(!same(binding.chain.Get(),chain)||binding.queue.Get()!=queue){binding={chain,queue};log("Verified swapchain/queue pair from same-thread ReSkate overlay submission: window="+std::to_string(reinterpret_cast<std::uintptr_t>(desc.OutputWindow))+" chain="+std::to_string(reinterpret_cast<std::uintptr_t>(chain))+" queue="+std::to_string(reinterpret_cast<std::uintptr_t>(queue)));report(L"Graphics queue paired; preparing front screen");}return true;
}
void STDMETHODCALLTYPE execute_hook(ID3D12CommandQueue* queue,UINT count,ID3D12CommandList*const* lists){original_execute(queue,count,lists);const auto error=GetLastError();
    if(!stopped.load()&&!submitting&&queue->GetDesc().Type==D3D12_COMMAND_LIST_TYPE_DIRECT){for(UINT i=0;i<count;++i){wchar_t name[128]{};UINT size=sizeof name;if(lists[i]&&SUCCEEDED(lists[i]->GetPrivateData(WKPDID_D3DDebugObjectNameW,&size,name))&&size<sizeof name&&wcscmp(name,L"ReSkate overlay draw")==0){
        pending={queue,GetTickCount64()};
        if(presentation.chain&&!presentation.drawn&&bind_pair(presentation.chain,queue)){presentation.drawn=true;render(presentation.chain,queue);pending={};}
        break;
    }}}SetLastError(error);
}
void present_result(IDXGISwapChain* chain,HRESULT result){
    auto& r=renderer();std::lock_guard lock(r.mutex);if(!same(r.chain.Get(),chain))return;
    if(FAILED(result)){r.failed=true;device_diagnostics(r,"Present failed",result);}
    else if(r.device){const auto removed=r.device->GetDeviceRemovedReason();if(FAILED(removed)){r.failed=true;device_diagnostics(r,"Device health after Present",removed);}else progress(r);}
}
template<class Invoke> HRESULT present_frame(IDXGISwapChain* chain,UINT flags,Invoke invoke){
    if(presenting||(flags&DXGI_PRESENT_TEST)||stopped.load())return invoke();
    presenting=true;presentation={chain,false};struct Scope{~Scope(){presentation={};pending={};presenting=false;}} scope;
    try{
        ComPtr<ID3D12CommandQueue> queue;{std::lock_guard lock(queue_mutex);if(same(binding.chain.Get(),chain))queue=binding.queue;}
        if(!queue&&pending.queue&&GetTickCount64()-pending.at<=100&&bind_pair(chain,pending.queue.Get()))queue=pending.queue;
        if(queue){presentation.drawn=true;render(chain,queue);}
    }catch(const std::exception& e){log(std::string("TV presentation preparation failed: ")+e.what());}
    const auto result=invoke();if(presentation.drawn)depth::native::present();present_result(chain,result);return result;
}
HRESULT STDMETHODCALLTYPE present_hook(IDXGISwapChain* chain,UINT sync,UINT flags){return present_frame(chain,flags,[&]{return original_present(chain,sync,flags);});}
HRESULT STDMETHODCALLTYPE present1_hook(IDXGISwapChain1* chain,UINT sync,UINT flags,const DXGI_PRESENT_PARAMETERS* p){return present_frame(chain,flags,[&]{return original_present1(chain,sync,flags,p);});}
bool before_resize(IDXGISwapChain* chain){
#ifndef TV_GPU_TEST
    if(!release_tv_menu(chain))return false;
#endif
    auto& r=renderer();std::lock_guard lock(r.mutex);depth::invalidate();return !same(r.chain.Get(),chain)||clear(r);}
struct ResizeHostLock {
#ifndef TV_GPU_TEST
    void* token=lock_host_render();~ResizeHostLock(){unlock_host_render(token);}
#endif
};
HRESULT STDMETHODCALLTYPE resize_hook(IDXGISwapChain* c,UINT n,UINT w,UINT h,DXGI_FORMAT f,UINT flags){ResizeHostLock host_lock;std::lock_guard lock(renderer().mutex);if(!before_resize(c))return DXGI_ERROR_INVALID_CALL;{std::lock_guard qlock(queue_mutex);if(same(binding.chain.Get(),c))binding={};}log("ResizeBuffers: "+std::to_string(w)+"x"+std::to_string(h));const auto result=original_resize(c,n,w,h,f,flags);if(FAILED(result))log("ResizeBuffers failed="+hr_text(result));return result;}
HRESULT STDMETHODCALLTYPE resize1_hook(IDXGISwapChain3* c,UINT n,UINT w,UINT h,DXGI_FORMAT f,UINT flags,const UINT* masks,IUnknown*const* qs){ResizeHostLock host_lock;std::lock_guard lock(renderer().mutex);if(!before_resize(c))return DXGI_ERROR_INVALID_CALL;{std::lock_guard qlock(queue_mutex);if(same(binding.chain.Get(),c))binding={};}const auto result=original_resize1(c,n,w,h,f,flags,masks,qs);if(FAILED(result))log("ResizeBuffers1 failed="+hr_text(result));return result;}

}
bool start_renderer(VlcPlayer& player){
    static bool installed{},failed{};if(installed)return true;if(failed)return false;
#ifndef TV_GPU_TEST
    const auto readiness=host_readiness();
    if(readiness==HostReadiness::Rejected){failed=true;report(L"ReSkate readiness rejected; see TV log");return false;}
    if(readiness!=HostReadiness::Ready){report(L"Waiting for ReSkate's first submitted overlay frame; open Insert once if needed");return false;}
#endif
    HookTransaction hooks;
    auto abort_startup=[&](const char* reason){
        failed=true;
#ifndef TV_GPU_TEST
        activate_host_callbacks(false);
#endif
        const bool cleaned=hooks.rollback();stopped=true;
        if(transfer){if(transfer->joinable())transfer->join();delete transfer;transfer=nullptr;}
        log(std::string("TV initialization rolled back; automatic retry disabled: ")+reason+" cleanup_ok="+std::to_string(cleaned)+" retained_exposed_trampolines="+std::to_string(hooks.may_have_run()));
        report(L"TV initialization failed and was rolled back; see log; restart required");return false;
    };
    try{
        log("ReSkate ready; preparing TV graphics initialization transaction");
        const auto status=MH_Initialize();if(status!=MH_OK&&status!=MH_ERROR_ALREADY_INITIALIZED)throw std::runtime_error("Hook library initialization failed");
        ComPtr<IDXGIFactory4> factory;check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"Discovery DXGI factory");ComPtr<ID3D12Device> device;check(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"Discovery DX12 device");
        D3D12_COMMAND_QUEUE_DESC qd{};qd.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;ComPtr<ID3D12CommandQueue> queue;check(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)),"Discovery queue");
        HWND window=CreateWindowExW(0,L"STATIC",L"TV DX12 discovery",WS_POPUP,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);if(!window)throw std::runtime_error("Discovery window failed");
        struct Window{HWND h;~Window(){DestroyWindow(h);}} cleanup{window};
        DXGI_SWAP_CHAIN_DESC1 sd{};sd.Width=64;sd.Height=64;sd.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sd.SampleDesc.Count=1;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.BufferCount=2;sd.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGISwapChain1> chain;check(factory->CreateSwapChainForHwnd(queue.Get(),window,&sd,nullptr,nullptr,&chain),"Discovery swapchain");ComPtr<IDXGISwapChain3> chain3;check(chain.As(&chain3),"Discovery Swapchain3");
        auto vt=*reinterpret_cast<void***>(chain3.Get());
        // The host owns Present/Present1 and queue choice. Only retain resize hooks
        // to release our backbuffer references before the native resize starts.
        hooks.add(vt[13],reinterpret_cast<void*>(&resize_hook),reinterpret_cast<void**>(&original_resize));
        hooks.add(vt[39],reinterpret_cast<void*>(&resize1_hook),reinterpret_cast<void**>(&original_resize1));
#ifndef TV_GPU_TEST
        prepare_host([](IDXGISwapChain* chain,ID3D12CommandQueue* q){render(chain,q);},hooks);
#endif
        transfer=new std::thread([&player]{try{
            std::uint64_t sequence{};bool published=false;ULONGLONG health{};
            while(!stopped.load()){
                if(GetTickCount64()>=health){health=GetTickCount64()+250;poll_device_health();}
                auto frame=player.wait_frame(sequence,100);
                if(stopped.load())break;
                if(frame&&frame->sequence!=sequence){sequence=frame->sequence;std::atomic_store(&latest,std::move(frame));published=true;}
                else if(!frame&&published){std::atomic_store(&latest,std::shared_ptr<const VlcPlayer::Frame>{});published=false;}
            }
        }catch(...){stopped=true;log("Video frame transfer stopped after an internal failure");}});
        if(stopped.load())throw std::runtime_error("Frame worker stopped before hook commit");
        hooks.commit();
#ifndef TV_GPU_TEST
        activate_host_callbacks(true);
#endif
        installed=true;log("TV initialization committed: core graphics/menu hooks activated together after host readiness; ReSkate-selected presentation and creation queue preserved");
        ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
        if(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)))&&SUCCEEDED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)))){list->Close();depth::initialize(queue.Get(),list.Get());}
        return true;
    }catch(const std::exception& e){return abort_startup(e.what());}
    catch(...){return abort_startup("Unexpected exception during initialization");}
}
void stop_renderer(){
#ifndef TV_GPU_TEST
    activate_host_callbacks(false);
#endif
    stopped=true;depth::live::shutdown();
    if(transfer){if(transfer->joinable())transfer->join();delete transfer;transfer=nullptr;}
    std::atomic_store(&latest,std::shared_ptr<const VlcPlayer::Frame>{});
    ResizeHostLock host_lock;
#ifndef TV_GPU_TEST
    release_tv_menu(nullptr);
#endif
    depth::activate(false);
    auto& r=renderer();std::lock_guard lock(r.mutex);clear(r);
    std::lock_guard qlock(queue_mutex);binding={};pending={};
}
void set_synced_scene(const Placement& placement,const Vec3& scale,const Screen& settings){
    std::lock_guard lock(sync_scene_mutex);sync_placement=placement;sync_scale=scale;sync_screen=settings;
}
void set_screen_mode(ScreenMode mode){screen_mode=mode;depth::activate(mode==ScreenMode::Attached||mode==ScreenMode::SyncedWorld);log("Screen mode="+std::to_string(static_cast<int>(mode)));if(mode==ScreenMode::Hidden)report(L"Screen hidden");else report(mode==ScreenMode::Front?L"Front screen enabled; test pattern until VLC supplies video":mode==ScreenMode::SyncedWorld?L"Network-synced world screen enabled":L"Object-attached screen enabled");}
void show_test_pattern(bool value){force_pattern=value;log(value?"Screen source: test pattern":"Screen source: VLC video, with test pattern while waiting");}
ScreenMode current_screen_mode(){return screen_mode.load();}
std::wstring renderer_status(){std::lock_guard lock(status_mutex);auto message=render_message;if(screen_mode.load()==ScreenMode::Attached||screen_mode.load()==ScreenMode::SyncedWorld){const auto detail=depth::status();message+=L"\n";message.append(detail.begin(),detail.end());}return message;}
#ifdef TV_GPU_TEST
// Uses the shipping pipeline, texture upload and draw path on WARP. No game hooks installed.
bool renderer_gpu_test(bool hardware){
    set_screen_mode(ScreenMode::Front);
    HWND window{};
    try {
        wchar_t no_debug[8]{};const bool skip_debug=GetEnvironmentVariableW(L"JISH_TV_TEST_NO_DEBUG",no_debug,8)>0;
        ComPtr<ID3D12Debug> debug;if(!skip_debug&&SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))){debug->EnableDebugLayer();std::cout<<"DX12 debug layer enabled\n";}else std::cout<<(skip_debug?"DX12 debug layer disabled for native runtime test\n":"DX12 debug layer unavailable\n");
        ComPtr<IDXGIFactory4> factory;check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"Test factory");ComPtr<IDXGIAdapter> warp;if(!hardware)check(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)),"WARP adapter");ComPtr<ID3D12Device> device;check(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"Test DX12 device");
        const auto luid=device->GetAdapterLuid();ComPtr<IDXGIAdapter1> actual_adapter;if(SUCCEEDED(factory->EnumAdapterByLuid(luid,IID_PPV_ARGS(&actual_adapter)))){DXGI_ADAPTER_DESC1 d{};if(SUCCEEDED(actual_adapter->GetDesc1(&d)))std::wcout<<L"Adapter: "<<d.Description<<L'\n';}
        ComPtr<ID3D12InfoQueue> info;device.As(&info);
        D3D12_COMMAND_QUEUE_DESC qd{};ComPtr<ID3D12CommandQueue> queue;check(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)),"Test queue");original_execute=reinterpret_cast<Execute>((*reinterpret_cast<void***>(queue.Get()))[10]);
        window=CreateWindowExW(0,L"STATIC",L"TV GPU test",WS_POPUP,0,0,128,128,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);if(!window)throw std::runtime_error("Test window failed");
        DXGI_SWAP_CHAIN_DESC1 sd{};sd.Width=128;sd.Height=128;sd.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sd.SampleDesc.Count=1;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.BufferCount=2;sd.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGISwapChain1> chain;check(factory->CreateSwapChainForHwnd(queue.Get(),window,&sd,nullptr,nullptr,&chain),"Test swapchain");
        Scene s;s.attached=true;s.camera.valid=true;s.camera.world={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};s.camera.vertical_fov=90;s.placement.position={0,0,-2};s.screen.width=2;s.screen.height=2;
        VlcPlayer::Frame pixels;pixels.width=8;pixels.height=8;pixels.sequence=1;pixels.bgra.resize(8*8*4);for(unsigned i=0;i<64;++i){pixels.bgra[i*4+2]=255;pixels.bgra[i*4+3]=0;}
        auto& r=renderer();
        D3D12_COMMAND_QUEUE_DESC bad_desc{};bad_desc.Type=D3D12_COMMAND_LIST_TYPE_COMPUTE;
        ComPtr<ID3D12CommandQueue> bad_queue;check(device->CreateCommandQueue(&bad_desc,IID_PPV_ARGS(&bad_queue)),"Negative-test compute queue");
        draw(chain.Get(),bad_queue,s,pixels);
        if(!r.failed||r.queue!=bad_queue||r.next)throw std::runtime_error("Invalid queue failure was not latched before submission");
        draw(chain.Get(),bad_queue,s,pixels);
        if(!r.failed||r.queue!=bad_queue||r.next)throw std::runtime_error("Failed queue was retried or submitted");
        for(unsigned iteration=0;iteration<6;++iteration){pixels.sequence++;draw(chain.Get(),queue,s,pixels);if(r.failed||!wait(r,r.next))throw std::runtime_error("Shipping draw path failed");}
        // Read back the centre through the actual shipping pipelines, including alpha handling.
        auto read_pixel=[&](unsigned x,unsigned y){
        auto& frame=r.frames[r.chain->GetCurrentBackBufferIndex()];r.list=frame.commands;auto desc=frame.buffer->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes{};device->GetCopyableFootprints(&desc,0,1,0,&footprint,nullptr,nullptr,&bytes);
        auto bd=buffer_desc(bytes);D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;ComPtr<ID3D12Resource> readback;check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)),"Test readback buffer");
        check(frame.allocator->Reset(),"Test allocator reset");check(r.list->Reset(frame.allocator.Get(),nullptr),"Test readback list");barrier(r,frame.buffer.Get(),D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=footprint;src.pResource=frame.buffer.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;r.list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);barrier(r,frame.buffer.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_PRESENT);check(r.list->Close(),"Test close");ID3D12CommandList* lists[]{r.list.Get()};queue->ExecuteCommandLists(1,lists);check(queue->Signal(r.fence.Get(),++r.next),"Test signal");if(!wait(r,r.next))throw std::runtime_error("Readback fence timeout");
        unsigned char* mapped{};D3D12_RANGE range{0,static_cast<SIZE_T>(bytes)};check(readback->Map(0,&range,reinterpret_cast<void**>(&mapped)),"Test map readback");const auto centre=mapped+footprint.Offset+y*footprint.Footprint.RowPitch+x*4;std::array<unsigned char,4> result{centre[0],centre[1],centre[2],centre[3]};readback->Unmap(0,nullptr);return result;
        };
        auto require_red=[&]{const auto pixel=read_pixel(64,64);if(pixel[0]<251||pixel[1]>2||pixel[2]>2||pixel[3]<251)throw std::runtime_error("GPU readback was not opaque red at screen centre");};require_red();
        // Real GPU depth capture and shipping pixel shader: a foreground object
        // covers the left half of the TV; the right half remains visible.
        {
        depth::activate(true);
        sl::Constants camera;for(auto& row:camera.cameraViewToClip.row)row=sl::float4(0,0,0,0);
        camera.cameraViewToClip[0].x=1;camera.cameraViewToClip[1].y=1;camera.cameraViewToClip[2].w=-1;camera.cameraViewToClip[3].z=.1f;
        camera.cameraPos={0,0,0};camera.cameraRight={1,0,0};camera.cameraUp={0,1,0};camera.cameraFwd={0,0,-1};camera.jitterOffset={0,0};camera.depthInverted=sl::eTrue;
        if(!depth::valid_camera(camera)||!depth::matches_camera(camera,s)||depth::sample_format(DXGI_FORMAT_D32_FLOAT)!=DXGI_FORMAT_R32_FLOAT||depth::sample_format(DXGI_FORMAT_D24_UNORM_S8_UINT)!=DXGI_FORMAT_R24_UNORM_X8_TYPELESS||depth::sample_format(DXGI_FORMAT_R8G8B8A8_UNORM)!=DXGI_FORMAT_UNKNOWN)throw std::runtime_error("Depth ABI/format validation failed");
        auto mismatched_projection=camera;mismatched_projection.cameraViewToClip[1].y*=2;if(depth::matches_camera(mismatched_projection,s))throw std::runtime_error("Unrelated camera projection was accepted");
        ComPtr<ID3D12Resource> scene_depth;D3D12_RESOURCE_DESC depth_desc{};depth_desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;depth_desc.Width=128;depth_desc.Height=128;depth_desc.DepthOrArraySize=1;depth_desc.MipLevels=1;depth_desc.Format=DXGI_FORMAT_R32_FLOAT;depth_desc.SampleDesc.Count=1;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&depth_desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&scene_depth)),"Test scene-depth allocation");
        ComPtr<ID3D12CommandAllocator> depth_allocator;ComPtr<ID3D12GraphicsCommandList> depth_commands;check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&depth_allocator)),"Depth test allocator");check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,depth_allocator.Get(),nullptr,IID_PPV_ARGS(&depth_commands)),"Depth test list");check(depth_commands->Close(),"Initial depth test close");
        // Optional isolated integration check against the supplied Streamline
        // module. Never enabled by ordinary unit tests or the shipping DLL.
        const auto observer_mh=MH_Initialize();if(observer_mh!=MH_OK&&observer_mh!=MH_ERROR_ALREADY_INITIALIZED)throw std::runtime_error("Observer test hook library failed");
        if(!depth::tracking::test_catalog_contracts()||!depth::probe::test_contracts())throw std::runtime_error("Depth diagnostic contract checks failed");
        depth::observer::initialize(queue.Get(),depth_commands.Get());
        depth::initialize_lifetime(queue.Get(),depth_commands.Get());
        // Cold startup must not depend on diagnostic reporting or debug mode.
        set_debug_logging(false);const auto ready_before=depth::live::test_ready();depth::live::report();
        if(depth::live::test_ready()!=ready_before)throw std::runtime_error("Reporting mutated live hooks");
        depth::live::maintain();const auto ready_after=depth::live::test_ready();
        if(ready_after<=ready_before)throw std::runtime_error("Cold debug-off maintenance did not install discovered hooks");
        depth::live::maintain();if(depth::live::test_ready()!=ready_after)throw std::runtime_error("Live hook maintenance was not idempotent");
        set_debug_logging(true);depth::live::report();set_debug_logging(false);
        if(depth::live::test_ready()!=ready_after)throw std::runtime_error("Debug reporting changed installed live families");
        wchar_t streamline_path[32768]{};if(GetEnvironmentVariableW(L"JISH_TV_TEST_STREAMLINE",streamline_path,32768)){
            if(!LoadLibraryExW(streamline_path,nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS))throw std::runtime_error("Cannot load integration-test Streamline module");
            const auto initialized=MH_Initialize();if(initialized!=MH_OK&&initialized!=MH_ERROR_ALREADY_INITIALIZED)throw std::runtime_error("Integration-test hook library failed");
            depth::initialize(queue.Get(),depth_commands.Get());const auto status=depth::status();std::cout<<status<<'\n';if(status.find("Occlusion ready")!=0)throw std::runtime_error("Actual Streamline/native DX12 depth-hook transaction failed");
        }
        constexpr auto read_state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        auto tagged_state=read_state;
        bool first_upload=true;unsigned number=100;
        auto finish=[&]{check(depth_commands->Close(),"Depth test close");ID3D12CommandList* lists[]{depth_commands.Get()};queue->ExecuteCommandLists(1,lists);depth::test_submitted(queue.Get(),1,lists);check(queue->Signal(r.fence.Get(),++r.next),"Depth test signal");if(!wait(r,r.next))throw std::runtime_error("Depth test GPU timeout");};
        auto begin=[&]{check(depth_allocator->Reset(),"Reset depth test allocator");check(depth_commands->Reset(depth_allocator.Get(),nullptr),"Reset depth test list");};
        auto fill=[&](bool reverse){
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes{};device->GetCopyableFootprints(&depth_desc,0,1,0,&footprint,nullptr,nullptr,&bytes);auto upload=upload_buffer(r,bytes);void* data{};D3D12_RANGE none{};check(upload->Map(0,&none,&data),"Map depth test upload");
            for(unsigned y=0;y<128;++y)for(unsigned x=0;x<128;++x){const float value=reverse?(x<64?.1f:.01f):(x<64?.9f:.99f);std::memcpy(static_cast<unsigned char*>(data)+footprint.Offset+y*footprint.Footprint.RowPitch+x*4,&value,4);}upload->Unmap(0,nullptr);
            begin();auto* old=r.list.Get();r.list=depth_commands;if(!first_upload)barrier(r,scene_depth.Get(),read_state,D3D12_RESOURCE_STATE_COPY_DEST);
            D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=scene_depth.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;src.pResource=upload.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=footprint;depth_commands->CopyTextureRegion(&dst,0,0,0,&src,nullptr);barrier(r,scene_depth.Get(),D3D12_RESOURCE_STATE_COPY_DEST,read_state);r.list=old;finish();first_upload=false;
        };
        auto background=[&]{begin();auto& frame=r.frames[r.chain->GetCurrentBackBufferIndex()];auto* old=r.list.Get();r.list=depth_commands;barrier(r,frame.buffer.Get(),D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_RENDER_TARGET);const float blue[]{0,0,1,1};depth_commands->ClearRenderTargetView(frame.rtv,blue,0,nullptr);barrier(r,frame.buffer.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PRESENT);r.list=old;finish();};
        auto test_occlusion=[&](bool copied,bool cropped){
            background();depth::test_constants(camera,++number,0);sl::Resource resource(sl::ResourceType::eTex2d,scene_depth.Get(),tagged_state);sl::Extent extent;if(cropped){extent.left=32;extent.top=32;extent.width=64;extent.height=64;}
            sl::ResourceTag tagged(&resource,sl::kBufferTypeDepth,copied?sl::eOnlyValidNow:sl::eValidUntilPresent,cropped?&extent:nullptr);
            if(copied){begin();depth::test_tag(tagged,number,0,depth_commands.Get());finish();}else depth::test_tag(tagged,number,0,nullptr);
            draw(chain.Get(),queue,s,pixels);if(r.failed||!wait(r,r.next))throw std::runtime_error("Occluded TV draw failed");
            if(!r.frames[r.chain->GetCurrentBackBufferIndex()].depth_image)throw std::runtime_error("Depth snapshot was rejected in GPU test");
            const auto left=read_pixel(48,64),right=read_pixel(80,64);
            if(left[0]>2||left[1]>2||left[2]<251||right[0]<251||right[1]>2||right[2]>2)throw std::runtime_error("Depth occlusion readback: foreground was not hidden or background TV disappeared");
            depth::end_present();
        };
        fill(true);test_occlusion(false,false);test_occlusion(true,true);
        depth::set_backend(depth::Backend::StreamlineOnly);test_occlusion(false,false);
        depth::set_backend(depth::Backend::NativeOnly);
        depth::test_constants(camera,++number,0);{sl::Resource resource(sl::ResourceType::eTex2d,scene_depth.Get(),tagged_state);sl::ResourceTag tag(&resource,sl::kBufferTypeDepth,sl::eValidUntilPresent,nullptr);depth::test_tag(tag,number,0,nullptr);}
        {std::array<float,16> pose{};sl::float4x4 projection;if(depth::snapshot(device.Get(),s,128,128).image||depth::latest_world_camera(s,pose,projection))throw std::runtime_error("Native only mode consumed Streamline depth/camera");}
        depth::set_backend(depth::Backend::Auto);
        // Keep object/native tick pose fixed while the final rendered camera
        // springs sideways. The TV's projected edges must follow the final view.
        for(float spring:{.5f,-.5f}){
            camera.cameraPos.x=spring;camera.cameraRight={-1,0,0};
            background();depth::test_constants(camera,++number,0);sl::Resource resource(sl::ResourceType::eTex2d,scene_depth.Get(),tagged_state);sl::ResourceTag tagged(&resource,sl::kBufferTypeDepth,sl::eValidUntilPresent,nullptr);depth::test_tag(tagged,number,0,nullptr);
            draw(chain.Get(),queue,s,pixels);if(r.failed||!wait(r,r.next))throw std::runtime_error("Final-camera TV draw failed");
            const auto pixel=read_pixel(spring>0?88:104,64);
            if(spring>0?(pixel[0]>2||pixel[2]<251):(pixel[0]<251||pixel[2]>2))throw std::runtime_error("World TV followed stale tick camera instead of final spring pose");
            depth::end_present();
        }
        camera.cameraPos.x=0;camera.cameraRight={1,0,0};
        {auto rotated=camera;rotated.cameraRight={-.9396926f,0,.3420201f};rotated.cameraFwd={-.3420201f,0,-.9396926f};std::array<float,16> pose{};
         if(!depth::world_camera(rotated,s.camera,pose)||std::abs(pose[0]-.9396926f)>.001f||std::abs(pose[8]-.3420201f)>.001f)throw std::runtime_error("Final camera basis convention conversion failed");}
        s.camera.world[12]=600;s.camera.world[13]=200;s.camera.world[14]=1000;s.placement.position={600,200,998};test_occlusion(false,false);test_occlusion(true,false);s.camera.world[12]=s.camera.world[13]=s.camera.world[14]=0;s.placement.position={0,0,-2};
        // Standard Z and left-handed projection use the same world placement.
        camera.depthInverted=sl::eFalse;camera.cameraViewToClip[2].z=-1;camera.cameraViewToClip[3].z=-.1f;fill(false);test_occlusion(false,false);
        camera.cameraViewToClip[2].w=1;camera.cameraViewToClip[2].z=1;test_occlusion(true,false);
        // Pool reuse crosses more frames than the capture pool has slots.
        for(unsigned i=0;i<12;++i)test_occlusion(true,false);
        // Native depth/stencil textures can deny SRV access; the owned copy
        // must still sample correctly without modifying the source depth.
        camera.depthInverted=sl::eTrue;camera.cameraViewToClip[2].w=-1;camera.cameraViewToClip[2].z=0;camera.cameraViewToClip[3].z=.1f;
        for(auto format:{DXGI_FORMAT_D32_FLOAT,DXGI_FORMAT_D24_UNORM_S8_UINT,DXGI_FORMAT_D32_FLOAT_S8X24_UINT}){
            auto desc=depth_desc;desc.Format=format;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL|D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
            ComPtr<ID3D12Resource> made;check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_DEPTH_WRITE,nullptr,IID_PPV_ARGS(&made)),"Test native DSV texture");scene_depth=made;tagged_state=D3D12_RESOURCE_STATE_DEPTH_WRITE;
            D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;hd.NumDescriptors=3;ComPtr<ID3D12DescriptorHeap> dsv_heap;check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&dsv_heap)),"Depth test DSV heap");auto handle=dsv_heap->GetCPUDescriptorHandleForHeapStart();D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};dsv.Format=format;dsv.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2D;device->CreateDepthStencilView(scene_depth.Get(),&dsv,handle);
            auto copied_handle=handle;copied_handle.ptr+=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
            device->CopyDescriptorsSimple(1,copied_handle,handle,D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
            if(!depth::tracking::test_descriptor(copied_handle.ptr,scene_depth.Get()))throw std::runtime_error("CopyDescriptorsSimple depth association failed");
            auto ranged_handle=copied_handle;ranged_handle.ptr+=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
            device->CopyDescriptors(1,&ranged_handle,nullptr,1,&copied_handle,nullptr,D3D12_DESCRIPTOR_HEAP_TYPE_DSV);handle=ranged_handle;
            if(!depth::tracking::test_descriptor(handle.ptr,scene_depth.Get()))throw std::runtime_error("CopyDescriptors depth association failed");
            begin();D3D12_VIEWPORT diagnostic_view{0,0,128,128,0,1};depth_commands->RSSetViewports(1,&diagnostic_view);depth_commands->OMSetRenderTargets(0,nullptr,FALSE,&handle);depth_commands->ClearDepthStencilView(handle,D3D12_CLEAR_FLAG_DEPTH,.01f,0,0,nullptr);D3D12_RECT left{0,0,64,128};depth_commands->ClearDepthStencilView(handle,D3D12_CLEAR_FLAG_DEPTH,.1f,0,1,&left);depth_commands->OMSetRenderTargets(0,nullptr,FALSE,nullptr);finish();test_occlusion(false,false);test_occlusion(true,false);
        }
        depth_desc.MipLevels=3;ComPtr<ID3D12Resource> mip_depth;check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&depth_desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&mip_depth)),"Test mipmapped depth");scene_depth=mip_depth;tagged_state=read_state;first_upload=true;fill(true);test_occlusion(false,false);test_occlusion(true,false);
        // Exercise the actual probe ABI on valid bindings without drawing.
        {
            D3D12_DESCRIPTOR_RANGE range{};range.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;range.NumDescriptors=1;
            D3D12_ROOT_PARAMETER parameters[3]{};
            parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;parameters[0].Constants.Num32BitValues=16;
            parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;parameters[1].Descriptor.ShaderRegister=1;
            parameters[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameters[2].DescriptorTable.NumDescriptorRanges=1;parameters[2].DescriptorTable.pDescriptorRanges=&range;
            D3D12_ROOT_SIGNATURE_DESC signature{};signature.NumParameters=3;signature.pParameters=parameters;
            ComPtr<ID3DBlob> blob,error_blob;check(D3D12SerializeRootSignature(&signature,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error_blob),"Probe root signature serialize");
            ComPtr<ID3D12RootSignature> root;check(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)),"Probe root signature create");
            D3D12_HEAP_PROPERTIES cb_heap{};cb_heap.Type=D3D12_HEAP_TYPE_UPLOAD;auto cb_desc=buffer_desc(256);ComPtr<ID3D12Resource> cb;
            check(device->CreateCommittedResource(&cb_heap,D3D12_HEAP_FLAG_NONE,&cb_desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&cb)),"Probe CBV resource");
            begin();depth_commands->SetGraphicsRootSignature(root.Get());ID3D12DescriptorHeap* heaps[]{r.srvs.Get()};depth_commands->SetDescriptorHeaps(1,heaps);
            depth_commands->SetGraphicsRootConstantBufferView(1,cb->GetGPUVirtualAddress());depth_commands->SetGraphicsRootDescriptorTable(2,r.srvs->GetGPUDescriptorHandleForHeapStart());
            const float perspective[16]{1,0,0,0,0,1,0,0,0,0,0,-1,0,0,.1f,0};
            for(unsigned i=0;i<65;++i)depth_commands->SetGraphicsRoot32BitConstants(0,16,perspective,0);
            D3D12_RECT rect{0,0,128,128};depth_commands->RSSetScissorRects(1,&rect);finish();
        }
        if(!depth::observer::test_observed())throw std::runtime_error("Native depth observer/probe did not observe the required D3D12 hooks");
        depth::observer::report();
        depth::invalidate();if(depth::snapshot(device.Get(),s,128,128).image)throw std::runtime_error("Stale depth survived invalidation");depth::activate(false);
        draw(chain.Get(),queue,s,pixels);if(r.failed||!wait(r,r.next))throw std::runtime_error("Fallback after depth loss failed");require_red();
        // Native prototype integration: real draw/viewport/constants/barrier hooks,
        // owned copy, producer/consumer fences and shipping occlusion shader.
        {
            if(!depth::native::test_contracts())throw std::runtime_error("Native projection/pose contract checks failed");
            depth::activate(true);depth::invalidate();
            D3D12_ROOT_PARAMETER parameters[2]{};parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;parameters[0].Constants.Num32BitValues=16;
            parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;parameters[1].Descriptor.ShaderRegister=1;
            D3D12_ROOT_SIGNATURE_DESC signature{};signature.NumParameters=2;signature.pParameters=parameters;
            ComPtr<ID3DBlob> blob,error;check(D3D12SerializeRootSignature(&signature,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error),"Native prototype test root serialize");
            ComPtr<ID3D12RootSignature> native_root;check(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&native_root)),"Native prototype test root");
            constexpr char native_vs[]="float4 main(uint id:SV_VertexID):SV_Position {float2 p=float2((id<<1)&2,id&2);return float4(p*2-1,.5,1);}";
            ComPtr<ID3DBlob> vs;check(D3DCompile(native_vs,sizeof(native_vs),"NativeProbe",nullptr,nullptr,"main","vs_5_0",0,0,&vs,&error),"Native prototype test VS");
            D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};pipeline.pRootSignature=native_root.Get();pipeline.VS={vs->GetBufferPointer(),vs->GetBufferSize()};pipeline.SampleMask=UINT_MAX;
            for(auto& target:pipeline.BlendState.RenderTarget){target.SrcBlend=D3D12_BLEND_ONE;target.DestBlend=D3D12_BLEND_ZERO;target.BlendOp=D3D12_BLEND_OP_ADD;target.SrcBlendAlpha=D3D12_BLEND_ONE;target.DestBlendAlpha=D3D12_BLEND_ZERO;target.BlendOpAlpha=D3D12_BLEND_OP_ADD;target.LogicOp=D3D12_LOGIC_OP_NOOP;target.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;}
            pipeline.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pipeline.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;pipeline.RasterizerState.DepthClipEnable=TRUE;
            pipeline.DepthStencilState.DepthEnable=TRUE;pipeline.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ZERO;pipeline.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;
            pipeline.DepthStencilState.FrontFace={D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_COMPARISON_FUNC_ALWAYS};pipeline.DepthStencilState.BackFace=pipeline.DepthStencilState.FrontFace;
            pipeline.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;pipeline.DSVFormat=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;pipeline.SampleDesc.Count=1;
            ComPtr<ID3D12PipelineState> native_pipeline;check(device->CreateGraphicsPipelineState(&pipeline,IID_PPV_ARGS(&native_pipeline)),"Native prototype test pipeline");
            auto native_desc=depth_desc;native_desc.MipLevels=1;native_desc.Format=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;native_desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
            ComPtr<ID3D12Resource> native_source;check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&native_desc,D3D12_RESOURCE_STATE_DEPTH_WRITE,nullptr,IID_PPV_ARGS(&native_source)),"Native prototype test depth");
            D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;hd.NumDescriptors=2;ComPtr<ID3D12DescriptorHeap> descriptors;check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&descriptors)),"Native prototype test DSV heap");
            auto handle=descriptors->GetCPUDescriptorHandleForHeapStart();device->CreateDepthStencilView(native_source.Get(),nullptr,handle);
            auto readonly_handle=handle;readonly_handle.ptr+=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);D3D12_DEPTH_STENCIL_VIEW_DESC readonly_desc{};readonly_desc.Format=native_desc.Format;readonly_desc.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2D;readonly_desc.Flags=D3D12_DSV_FLAG_READ_ONLY_DEPTH|D3D12_DSV_FLAG_READ_ONLY_STENCIL;device->CreateDepthStencilView(native_source.Get(),&readonly_desc,readonly_handle);
            const float projection[16]{1,0,0,0,0,1,0,0,0,0,0,-1,0,0,.1f,0};bool first=true;
            D3D12_HEAP_PROPERTIES upload_heap{};upload_heap.Type=D3D12_HEAP_TYPE_UPLOAD;auto cb_desc=buffer_desc(2048);
            ComPtr<ID3D12Resource> projection_buffer,unsupported_buffer;
            check(device->CreateCommittedResource(&upload_heap,D3D12_HEAP_FLAG_NONE,&cb_desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&projection_buffer)),"CBV projection upload buffer");
            check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&cb_desc,D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,nullptr,IID_PPV_ARGS(&unsupported_buffer)),"CBV projection GPU-only buffer");
            const auto projection_address=projection_buffer->GetGPUVirtualAddress();unsigned cbv_mode{};
            unsigned native_mode{};D3D12_RESOURCE_STATES native_states[2]{D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_DEPTH_WRITE};
            ComPtr<ID3D12GraphicsCommandList4> pass_commands;depth_commands.As(&pass_commands);
            auto native_state=[&](D3D12_RESOURCE_STATES depth_state,D3D12_RESOURCE_STATES stencil_state){
                if(native_states[0]==native_states[1]&&depth_state==stencil_state){if(native_states[0]!=depth_state)barrier(r,native_source.Get(),native_states[0],depth_state);}
                else for(UINT plane=0;plane<2;++plane){const auto target=plane?stencil_state:depth_state;if(native_states[plane]!=target){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={native_source.Get(),plane,native_states[plane],target};depth_commands->ResourceBarrier(1,&b);}}
                native_states[0]=depth_state;native_states[1]=stencil_state;
            };
            auto write_projection=[&](bool valid,bool transpose){void* mapped{};D3D12_RANGE no_reads{0,0};check(projection_buffer->Map(0,&no_reads,&mapped),"CBV projection test map");std::memset(mapped,0,2048);
                if(valid){std::array<float,16> values{};for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x)values[y*4+x]=transpose?projection[x*4+y]:projection[y*4+x];std::memcpy(static_cast<unsigned char*>(mapped)+768,values.data(),64);}
                D3D12_RANGE written{0,2048};projection_buffer->Unmap(0,&written);
            };
            std::vector<ComPtr<ID3D12Resource>> projection_ring;
            if(!info)for(unsigned i=0;i<32;++i){ComPtr<ID3D12Resource> resource;check(device->CreateCommittedResource(&upload_heap,D3D12_HEAP_FLAG_NONE,&cb_desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&resource)),"CBV ring resource");void* mapped{};D3D12_RANGE no_reads{0,0};check(resource->Map(0,&no_reads,&mapped),"CBV ring map");std::memset(mapped,0,2048);std::memcpy(static_cast<unsigned char*>(mapped)+768,projection,64);D3D12_RANGE written{0,2048};resource->Unmap(0,&written);resource->GetGPUVirtualAddress();projection_ring.push_back(resource);}
            auto record_native=[&](bool include_projection,bool partial_write){
                depth::native::present();s.at=GetTickCount64();depth::native::test_recording_camera(s.camera);depth::native::frame(s,128,128);begin();auto* old=r.list.Get();r.list=depth_commands;
                native_state(D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_DEPTH_WRITE);
                if(native_mode==3||native_mode==5||native_mode==8){native_state(read_state,read_state);native_state(D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_DEPTH_WRITE);}
                depth_commands->SetPipelineState(native_pipeline.Get());depth_commands->SetGraphicsRootSignature(native_root.Get());
                depth::live::observe(depth_commands.Get());depth::live::maintain();
                if(include_projection&&!cbv_mode)depth_commands->SetGraphicsRoot32BitConstants(0,16,projection,0);
                else {const float identity[16]{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};depth_commands->SetGraphicsRoot32BitConstants(0,16,identity,0);}
                if(partial_write)depth_commands->SetGraphicsRoot32BitConstant(0,0,5);
                if(cbv_mode){write_projection(cbv_mode!=3,cbv_mode==2);depth_commands->SetGraphicsRootConstantBufferView(1,cbv_mode==4?unsupported_buffer->GetGPUVirtualAddress():projection_address+256);}
                D3D12_VIEWPORT viewport=native_mode==9?D3D12_VIEWPORT{16,16,96,96,0,1}:native_mode==10?D3D12_VIEWPORT{16,16,128,128,0,1}:D3D12_VIEWPORT{0,0,128,128,0,1};D3D12_RECT scissor{0,0,128,128};depth_commands->RSSetViewports(1,&viewport);depth_commands->RSSetScissorRects(1,&scissor);
                depth_commands->OMSetRenderTargets(0,nullptr,FALSE,&handle);depth_commands->ClearDepthStencilView(handle,D3D12_CLEAR_FLAG_DEPTH,0,0,0,nullptr);
                D3D12_RECT left{0,0,64,128},right{64,0,128,128};depth_commands->ClearDepthStencilView(handle,D3D12_CLEAR_FLAG_DEPTH,.1f,0,1,&left);depth_commands->ClearDepthStencilView(handle,D3D12_CLEAR_FLAG_DEPTH,.01f,0,1,&right);
                if(native_mode==2){native_state(D3D12_RESOURCE_STATE_DEPTH_READ,D3D12_RESOURCE_STATE_DEPTH_READ);depth_commands->OMSetRenderTargets(0,nullptr,FALSE,&readonly_handle);}
                if(native_mode==3||native_mode==8){if(!pass_commands)throw std::runtime_error("Render-pass interface unavailable in GPU test");D3D12_RENDER_PASS_DEPTH_STENCIL_DESC pass{};pass.cpuDescriptor=handle;pass.DepthBeginningAccess.Type=pass.StencilBeginningAccess.Type=D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE;pass.DepthEndingAccess.Type=native_mode==8?D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_DISCARD:D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;pass.StencilEndingAccess.Type=D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;pass_commands->BeginRenderPass(0,nullptr,&pass,D3D12_RENDER_PASS_FLAG_NONE);}
                depth_commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);depth_commands->DrawInstanced(3,1,0,0);
                if(cbv_mode==1)for(unsigned i=0;i<32;++i){depth_commands->SetGraphicsRootConstantBufferView(1,projection_address+256);depth_commands->DrawInstanced(3,1,0,0);}
                if(cbv_mode==7)for(const auto& resource:projection_ring){depth_commands->SetGraphicsRootSignature(native_root.Get());depth_commands->SetGraphicsRootConstantBufferView(1,resource->GetGPUVirtualAddress()+256);depth_commands->DrawInstanced(3,1,0,0);}
                if(cbv_mode==5)write_projection(false,false);
                if(native_mode==3||native_mode==8)pass_commands->EndRenderPass();
                if(native_mode==4)depth_commands->ClearDepthStencilView(handle,D3D12_CLEAR_FLAG_DEPTH,0,0,0,nullptr);
                if(native_mode==5)depth_commands->OMSetRenderTargets(0,nullptr,FALSE,nullptr);
                if(native_mode==6){D3D12_RESOURCE_BARRIER alias{};alias.Type=D3D12_RESOURCE_BARRIER_TYPE_ALIASING;depth_commands->ResourceBarrier(1,&alias);}
                if(native_mode==7){D3D12_RESOURCE_BARRIER split{};split.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;split.Transition={native_source.Get(),0,native_states[0],read_state};split.Flags=D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY;depth_commands->ResourceBarrier(1,&split);split.Flags=D3D12_RESOURCE_BARRIER_FLAG_END_ONLY;depth_commands->ResourceBarrier(1,&split);native_states[0]=read_state;}
                native_state(read_state,native_mode==1?D3D12_RESOURCE_STATE_DEPTH_WRITE:read_state);
                if(cbv_mode==6)write_projection(false,false);
                r.list=old;finish();first=false;
            };
            background();for(unsigned i=0;i<4;++i)record_native(true,false);
            auto moved=s;moved.camera.world[12]+=1;if(depth::native::snapshot(device.Get(),moved,128,128).image)throw std::runtime_error("Native prototype accepted moved camera");
            if(depth::native::snapshot(device.Get(),s,96,96).image)throw std::runtime_error("Native prototype accepted changed output");
            draw(chain.Get(),queue,s,pixels);if(r.failed||!wait(r,r.next))throw std::runtime_error("Native prototype TV draw failed");
            if(!r.frames[r.chain->GetCurrentBackBufferIndex()].depth_image)throw std::runtime_error("Native prototype capture missing: "+depth::native::status());
            const auto left=read_pixel(48,64),right=read_pixel(80,64);if(left[2]<251||left[0]>2||right[0]<251||right[2]>2)throw std::runtime_error("Native prototype foreground occlusion pixel check failed");
            depth::native::report();
            Sleep(90);if(depth::native::snapshot(device.Get(),s,128,128).image)throw std::runtime_error("Native prototype accepted stale capture");
            // A closed list containing an injected copy can be submitted again.
            // It must be retired, ordered after TV readers, and retained until reset.
            {ID3D12CommandList* lists[]{depth_commands.Get()};queue->ExecuteCommandLists(1,lists);check(queue->Signal(r.fence.Get(),++r.next),"Repeated native recording fence");if(!wait(r,r.next))throw std::runtime_error("Repeated native recording GPU timeout");}
            if(depth::native::snapshot(device.Get(),s,128,128).image)throw std::runtime_error("Native prototype exposed repeated recording");
            depth::invalidate();for(unsigned i=0;i<4;++i)record_native(false,false);
            if(depth::native::snapshot(device.Get(),s,128,128).image)throw std::runtime_error("Native prototype accepted missing projection");
            depth::invalidate();for(unsigned i=0;i<4;++i)record_native(true,true);
            if(depth::native::snapshot(device.Get(),s,128,128).image)throw std::runtime_error("Native prototype accepted overwritten partial projection");
            if(!info)for(unsigned mode:{1u,2u,7u}){
                depth::invalidate();cbv_mode=mode;background();for(unsigned i=0;i<4;++i)record_native(false,false);
                if(!depth::native::snapshot(device.Get(),s,128,128).image){depth::native::report();throw std::runtime_error("Upload CBV projection missing mode="+std::to_string(mode)+": "+depth::native::status());}
                draw(chain.Get(),queue,s,pixels);if(r.failed||!wait(r,r.next))throw std::runtime_error("Upload CBV TV draw failed");
                const auto cbv_left=read_pixel(48,64),cbv_right=read_pixel(80,64);if(cbv_left[2]<251||cbv_left[0]>2||cbv_right[0]<251||cbv_right[2]>2)throw std::runtime_error("Upload CBV foreground occlusion pixel check failed");
            }
            for(unsigned mode:{3u,4u,5u,6u}){
                depth::invalidate();cbv_mode=mode;for(unsigned i=0;i<4;++i)record_native(false,false);
                if(depth::native::snapshot(device.Get(),s,128,128).image)throw std::runtime_error("Native prototype accepted invalid/unsupported/mutated CBV projection mode="+std::to_string(mode));
            }
            if(info){depth::invalidate();cbv_mode=1;for(unsigned i=0;i<4;++i)record_native(false,false);if(depth::native::snapshot(device.Get(),s,128,128).image)throw std::runtime_error("CBV capture accepted an unsupported debug-layer recording");std::cout<<"Upload root-CBV: debug-layer capture disabled; fallback rejection passed\n";}
            else std::cout<<"Upload root-CBV: nonzero GPUVA/matrix offsets, both storage orders, shipping shader occlusion pixels, missing/GPU-only/mutated-before-copy/mutated-before-submit rejection passed\n";
            cbv_mode=0;depth::set_backend(depth::Backend::NativeOnly);
            for(unsigned mode:{1u,2u,3u,4u,5u,9u}){
                depth::invalidate();native_mode=mode;for(unsigned i=0;i<6;++i)record_native(true,false);
                auto sample=depth::snapshot(device.Get(),s,128,128);if(!sample.image)throw std::runtime_error("Native plane/pass/boundary capture missing mode="+std::to_string(mode)+": "+depth::native::status());
                draw(chain.Get(),queue,s,pixels);if(r.failed||!wait(r,r.next))throw std::runtime_error("Native plane/pass/boundary TV draw failed");
                const auto l=read_pixel(48,64),rr=read_pixel(80,64);if(l[2]<251||l[0]>2||rr[0]<251||rr[2]>2)throw std::runtime_error("Native plane/pass/boundary occlusion pixel mismatch mode="+std::to_string(mode));
            }
            for(unsigned mode:{6u,7u,8u,10u}){depth::invalidate();native_mode=mode;for(unsigned i=0;i<5;++i)record_native(true,false);if(depth::native::snapshot(device.Get(),s,128,128).image)throw std::runtime_error("Native accepted alias/split/discard/out-of-bounds mode="+std::to_string(mode));}
            depth::invalidate();native_mode=1;
            for(unsigned i=0;i<48;++i){record_native(true,false);if(i>3&&!depth::native::snapshot(device.Get(),s,128,128).image)throw std::runtime_error("Native capture stopped refreshing across frames");}
            // A valid Streamline sample is preferred in Auto, but bypassed in Native only.
            auto native_sample=depth::snapshot(device.Get(),s,128,128);if(!native_sample.image)throw std::runtime_error("Native only selection failed");
            depth::test_constants(camera,++number,0);{sl::Resource resource(sl::ResourceType::eTex2d,scene_depth.Get(),tagged_state);sl::ResourceTag tag(&resource,sl::kBufferTypeDepth,sl::eValidUntilPresent,nullptr);depth::test_tag(tag,number,0,nullptr);}
            if(depth::snapshot(device.Get(),s,128,128).image!=native_sample.image)throw std::runtime_error("Native only selected Streamline sample");
            depth::set_backend(depth::Backend::StreamlineOnly);
            if(depth::snapshot(device.Get(),s,128,128).image)throw std::runtime_error("Mode change retained a previous native sample");
            depth::set_backend(depth::Backend::Auto);
            for(unsigned i=0;i<6;++i)record_native(true,false);
            auto fallback_sample=depth::native::snapshot(device.Get(),s,128,128);if(!fallback_sample.image)throw std::runtime_error("Auto native fallback capture missing");
            depth::test_constants(camera,++number,0);{sl::Resource resource(sl::ResourceType::eTex2d,scene_depth.Get(),tagged_state);sl::ResourceTag tag(&resource,sl::kBufferTypeDepth,sl::eValidUntilPresent,nullptr);depth::test_tag(tag,number,0,nullptr);}
            auto preferred_sample=depth::snapshot(device.Get(),s,128,128);if(!preferred_sample.image||!preferred_sample.image->direct||preferred_sample.image==fallback_sample.image)throw std::runtime_error("Auto did not prefer valid Streamline sample");
            depth::end_present();if(depth::snapshot(device.Get(),s,128,128).image!=fallback_sample.image)throw std::runtime_error("Auto did not fall back after Streamline sample expired");
            if(!depth::native::test_boundaries())throw std::runtime_error("Not all native depth-plane/pass/boundary copy paths executed");
            std::cout<<"Native proto3: depth-plane-only transitions, read-only DSV, render-pass preserve/discard, before-clear/unbind copies, cropped viewport, alias/split rejection, 48-frame refresh and backend switching passed\n";
            depth::invalidate();depth::activate(false);draw(chain.Get(),queue,s,pixels);if(r.failed||!wait(r,r.next))throw std::runtime_error("Native prototype fallback draw failed");require_red();
            std::cout<<"Native prototype: actual draw/viewport/projection/transition hooks, depth-copy pixel readback, moved-camera/output/missing/partial projection rejection, repeated-recording fences and fallback passed\n";
        }
        }
        for(unsigned i=0;i<8;++i){++pixels.sequence;draw(chain.Get(),queue,s,pixels,true);if(r.failed||!wait(r,r.next))throw std::runtime_error("ImGui front screen draw failed");}require_red();
        // A native camera/attachment is intentionally absent from these hook-order checks.
        ComPtr<ID3D12CommandAllocator> named_allocator;ComPtr<ID3D12GraphicsCommandList> named_commands;check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&named_allocator)),"Pairing test allocator");check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,named_allocator.Get(),nullptr,IID_PPV_ARGS(&named_commands)),"Pairing test list");check(named_commands->Close(),"Pairing test close");named_commands->SetName(L"ReSkate overlay draw");
        auto named_submission=[&]{ID3D12CommandList* lists[]{named_commands.Get()};execute_hook(queue.Get(),1,lists);};
        binding={};pending={};auto draws=r.draws;
        present_frame(chain.Get(),0,[&]{named_submission();return S_OK;});if(r.draws!=draws+1||!wait(r,r.next))throw std::runtime_error("Overlay-inside-Present pairing failed");progress(r);if(!r.completed_first)throw std::runtime_error("GPU completion was not acknowledged");
        const auto yellow=read_pixel(40,55);if(yellow[0]<251||yellow[1]<251||yellow[2]>2||yellow[3]<251)throw std::runtime_error("Camera-independent colour bars failed GPU readback");
        latest=std::make_shared<const VlcPlayer::Frame>(pixels);binding={};pending={};draws=r.draws;named_submission();present_frame(chain.Get(),0,[]{return S_OK;});if(r.draws!=draws+1||!wait(r,r.next))throw std::runtime_error("Overlay-before-Present pairing failed");require_red();latest.reset();
        binding={};pending={queue,GetTickCount64()-101};draws=r.draws;present_frame(chain.Get(),0,[]{return S_OK;});if(r.draws!=draws)throw std::runtime_error("Expired queue evidence was accepted");
        set_screen_mode(ScreenMode::Hidden);binding={chain.Get(),queue.Get()};present_frame(chain.Get(),0,[]{return S_OK;});if(r.draws!=draws)throw std::runtime_error("Hidden mode submitted a draw");set_screen_mode(ScreenMode::Front);binding={};pending={};
        if(!before_resize(chain.Get()))throw std::runtime_error("Resize resource drain failed");check(chain->ResizeBuffers(2,96,96,sd.Format,0),"Test ResizeBuffers");draw(chain.Get(),queue,s,pixels);if(r.failed||!wait(r,r.next))throw std::runtime_error("Resized swapchain draw failed");clear(r);chain.Reset();
        check(factory->CreateSwapChainForHwnd(queue.Get(),window,&sd,nullptr,nullptr,&chain),"Replacement test swapchain");pixels.width=16;pixels.height=4;pixels.bgra.resize(16*4*4,255);pixels.sequence++;draw(chain.Get(),queue,s,pixels);if(r.failed||!wait(r,r.next))throw std::runtime_error("Replacement swapchain draw failed");clear(r);
        // Exercise the public snapshot path, not just the timer policy.
        depth::set_backend(depth::Backend::StreamlineOnly);depth::activate(true);depth::invalidate();
        depth::snapshot(device.Get(),s,96,96);Sleep(3050);depth::snapshot(device.Get(),s,96,96);
        if(depth::backend()!=depth::Backend::ReShadeNative||!depth::automatic_fallback_active())throw std::runtime_error("Missing Streamline depth did not trigger ReShade fallback");
        depth::set_backend(depth::Backend::StreamlineOnly);
        if(depth::automatic_fallback_active())throw std::runtime_error("Manual Streamline retry retained old fallback notice");
        depth::activate(false);
        stop_renderer();stop_renderer(); // Cleanup is safe to repeat with no active resources.
        bool errors=false;if(info){for(UINT64 i=0;i<info->GetNumStoredMessagesAllowedByRetrievalFilter();++i){SIZE_T length{};info->GetMessage(i,nullptr,&length);std::vector<unsigned char> storage(length);auto* message=reinterpret_cast<D3D12_MESSAGE*>(storage.data());if(SUCCEEDED(info->GetMessage(i,message,&length))&&message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){errors=true;std::cerr<<message->pDescription<<'\n';}}}
        DestroyWindow(window);std::cout<<(hardware?"Hardware":"WARP")<<": world/front BGRA, scene-depth occlusion, normal/reversed Z, handedness, cropped depth, volatile capture fences/pool reuse, depth loss fallback, overlay/Present ordering, GPU completion, hide and resize passed\n";return !errors;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';clear(renderer());if(window)DestroyWindow(window);return false;}
}
#endif
}
