#include "host_bridge.h"
#include "native_depth.h"
#include "tv_menu.h"
#include "tv_menu_input.h"
#include "host_212.h"
#include "gate_trace.h"
#include "hook_transaction.h"
#include <wrl/client.h>
#include "tv_runtime.h"
#include "Extension/UI/Overlay/overlay.h"
#include <imgui_internal.h>
#include <MinHook.h>
#include <bcrypt.h>
#include <fstream>
#include <cstring>
#include <iomanip>
#include <sstream>
namespace virtualtv {
namespace {
// Pinned to official v2.1.2, not a public extension ABI. Fail closed on any other host.
std::uintptr_t host{};
HostDraw video_draw{};
bool panel_visible{},panel_opened_overlay{};
using Mods=void(*)(void*,int,const dingosdk::overlay::Model*,const void*);
using Render=void(*)(IDXGISwapChain*,UINT);
Mods original_mods{};Render original_render{};
std::atomic<bool> open_requested{},show_requested{},close_url_requested{},ui_failed{},callbacks_enabled{},frame_failed{};
bool opened_for_url{};
thread_local const char* callback_stage="entry";
static_assert(IMGUI_VERSION_NUM==19191 && sizeof(ImGuiContext)==10576 && sizeof(ImGuiIO)==3032);
static_assert(sizeof(dingosdk::overlay::Model)==26104);
static_assert(offsetof(dingosdk::overlay::Model,debug)==128 && offsetof(dingosdk::overlay::Model,editor)==24192);
template<class T> T at(std::uintptr_t offset){return *reinterpret_cast<T*>(host+offset);}
bool matching_file(HMODULE mod){
    wchar_t name[32768]{};if(!GetModuleFileNameW(mod,name,32768))return false;
    std::ifstream in(name,std::ios::binary);if(!in)return false;
    BCRYPT_ALG_HANDLE alg{};BCRYPT_HASH_HANDLE hash{};
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return false;
    if(BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)<0){BCryptCloseAlgorithmProvider(alg,0);return false;}
    std::array<unsigned char,65536> chunk{};bool good=true;
    while(in){in.read(reinterpret_cast<char*>(chunk.data()),chunk.size());if(in.gcount()&&BCryptHashData(hash,chunk.data(),static_cast<ULONG>(in.gcount()),0)<0){good=false;break;}}
    if(!in.eof())good=false;
    std::array<unsigned char,32> digest{};if(BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)<0)good=false;
    BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(alg,0);
    std::ostringstream s;for(auto b:digest)s<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(b);
    return good&&s.str()==host212::sha256;
}
template<std::size_t N> bool prefix(std::uintptr_t rva,const unsigned char(&bytes)[N]){
    MEMORY_BASIC_INFORMATION m{};return VirtualQuery(reinterpret_cast<void*>(host+rva),&m,sizeof m)&&m.State==MEM_COMMIT&&!(m.Protect&(PAGE_NOACCESS|PAGE_GUARD))&&std::memcmp(reinterpret_cast<void*>(host+rva),bytes,N)==0;
}
#ifdef TV_HOST_TEST
// Adapted from JishsFirstPersonMod's ScopedHostImGui. Allocation and context ownership
// are switched together and restored before any of our own renderer code can run.
struct GuiScope {
    ImGuiContext* previous=ImGui::GetCurrentContext();ImGuiMemAllocFunc alloc{};ImGuiMemFreeFunc free{};void* data{};
    explicit GuiScope(ImGuiContext* context){ImGui::GetAllocatorFunctions(&alloc,&free,&data);ImGui::SetAllocatorFunctions(at<ImGuiMemAllocFunc>(host212::alloc),at<ImGuiMemFreeFunc>(host212::free),at<void*>(host212::allocator_data));ImGui::SetCurrentContext(context);}
    ~GuiScope(){ImGui::SetCurrentContext(previous);ImGui::SetAllocatorFunctions(alloc,free,data);}
};
void mods_hook(void* menu,int tab,const dingosdk::overlay::Model* model,const void* callbacks){
    const auto state=at<std::uintptr_t>(host212::state);
    if(callbacks_enabled&&!ui_failed&&state&&reinterpret_cast<std::uintptr_t>(menu)==state+host212::menu&&reinterpret_cast<std::uintptr_t>(model)==state+host212::model){
        auto* context=*reinterpret_cast<ImGuiContext**>(state+host212::context);
        if(context&&context==at<ImGuiContext*>(host212::gui_context)&&context->WithinFrameScope){
            GuiScope scope(context);ImGuiErrorRecoveryState recovery{};ImGui::ErrorRecoveryStoreState(&recovery);
            try{
                const auto& d=model->debug;publish_player_position({d.skater_position[0],d.skater_position[1],d.skater_position[2]},d.skater_position_valid);
                draw_tv_controls(*model);
            }catch(...){ImGui::ErrorRecoveryTryToRecoverState(&recovery);ui_failed=true;log("Embedded TV menu disabled after an exception; original Mods menu preserved");}
        }
    }
    original_mods(menu,tab,model,callbacks);
}
#endif // TV_HOST_TEST: legacy embedded Mods-page integration is not used by proto3
void host_frame(IDXGISwapChain* presented,UINT flags){
    struct Info {const char* reason="forwarded_to_renderer";std::uintptr_t state{},selected{},queue{},presented_identity{},selected_identity{};bool ready{},failed{};HRESULT presented_qi=E_NOINTERFACE,selected_qi=E_NOINTERFACE;};
    Info info;
    struct Trace {Info& i;IDXGISwapChain* presented;UINT flags;~Trace(){static GateTrace trace;trace.sample("host_presentation",i.reason,[&]{std::ostringstream out;out<<"flags="<<flags<<" presented="<<presented<<" host_state=0x"<<std::hex<<i.state<<" selected=0x"<<i.selected<<" queue=0x"<<i.queue<<" presented_identity=0x"<<i.presented_identity<<" selected_identity=0x"<<i.selected_identity<<" presented_qi=0x"<<i.presented_qi<<" selected_qi=0x"<<i.selected_qi<<std::dec<<" ready="<<i.ready<<" failed="<<i.failed;return out.str();});}} trace{info,presented,flags};
    if((flags&DXGI_PRESENT_TEST)||!presented){info.reason="test_or_null_presentation";return;}
    callback_stage="read host state";
    const auto state=at<std::uintptr_t>(host212::state);info.state=state;if(!state){info.reason="host_state_missing";return;}
    callback_stage="lock host state after original render";
    using Mutex=int(*)(void*);auto lock=reinterpret_cast<Mutex>(host+host212::mutex_lock);auto unlock=reinterpret_cast<Mutex>(host+host212::mutex_unlock);
    if(lock(reinterpret_cast<void*>(state))!=0){info.reason="host_lock_failed";return;}
    struct Unlock{Mutex fn;void* p;~Unlock(){fn(p);}} guard{unlock,reinterpret_cast<void*>(state)};
    if(!callbacks_enabled){info.reason="callbacks_disabled";return;}
    if(open_requested.exchange(false)){
        auto& visible=*reinterpret_cast<std::atomic<bool>*>(state+host212::visible);
        panel_visible=!panel_visible;
        opened_for_url=false;
        if(panel_visible){panel_opened_overlay=!visible.load();visible.store(true);}
        else if(panel_opened_overlay){visible.store(false);panel_opened_overlay=false;}
        log(panel_visible?"F8: Virtual TV window opened":"F8: Virtual TV window closed");
    }
    // This request is idempotent; rapid Insert button clicks cannot toggle the panel closed.
    if(show_requested.exchange(false)){
        auto& visible=*reinterpret_cast<std::atomic<bool>*>(state+host212::visible);
        if(!panel_visible){opened_for_url=true;panel_visible=true;panel_opened_overlay=!visible.load();}
        visible.store(true);
        log("Native VirtualTV URL entry requested from Insert plugin");
    }
    if(close_url_requested.exchange(false)&&opened_for_url){
        auto& visible=*reinterpret_cast<std::atomic<bool>*>(state+host212::visible);
        panel_visible=false;
        if(panel_opened_overlay){visible.store(false);panel_opened_overlay=false;}
        opened_for_url=false;
    }
    callback_stage="validate host graphics state";
    info.ready=*reinterpret_cast<bool*>(state+host212::ready);if(!info.ready){info.reason="host_not_ready";return;}
    info.failed=reinterpret_cast<std::atomic<bool>*>(state+host212::failed)->load();if(info.failed){info.reason="host_failed";return;}
    auto* selected=*reinterpret_cast<IDXGISwapChain**>(state+host212::swapchain);auto* queue=*reinterpret_cast<ID3D12CommandQueue**>(state+host212::queue);
    info.selected=reinterpret_cast<std::uintptr_t>(selected);info.queue=reinterpret_cast<std::uintptr_t>(queue);
    if(!selected||!queue){info.reason="selected_pair_missing";return;}
    callback_stage="validate selected presentation identity";
    Microsoft::WRL::ComPtr<IUnknown> a,b;
    const bool same=SUCCEEDED(info.presented_qi=presented->QueryInterface(IID_PPV_ARGS(&a)))&&SUCCEEDED(info.selected_qi=selected->QueryInterface(IID_PPV_ARGS(&b)))&&a==b;
    info.presented_identity=reinterpret_cast<std::uintptr_t>(a.Get());info.selected_identity=reinterpret_cast<std::uintptr_t>(b.Get());
    if(!same)info.reason="presentation_identity_mismatch";else if(!video_draw)info.reason="video_callback_missing";
    if(same){
        auto& visible=*reinterpret_cast<std::atomic<bool>*>(state+host212::visible);
        if(!visible.load())panel_visible=false;
        const auto& model=*reinterpret_cast<const dingosdk::overlay::Model*>(state+host212::model);
        const auto& d=model.debug;
        publish_player_position({d.skater_position[0],d.skater_position[1],d.skater_position[2]},d.skater_position_valid);
        // Model is held by the already-existing verified ReSkate host callback.
        // ReSkate's explicit native client realm resolves the placement store;
        // no independent hook or game EXE patch is installed by VirtualTV.
        callback_stage="native placed-object poll";
        poll_native_objects();
        callback_stage="cache park editor object names";
        observe_editor_placement_names(model.editor);
        if(video_draw){callback_stage="TV video callback";video_draw(selected,queue);}
        callback_stage="independent TV menu";draw_tv_menu(selected,queue,model,panel_visible);
        if(!panel_visible&&panel_opened_overlay){visible.store(false);panel_opened_overlay=false;}
    }
}
int record_frame_fault(EXCEPTION_POINTERS* fault) noexcept {
    frame_failed=true;
    try{std::ostringstream out;out<<"TV host callback disabled: stage="<<callback_stage<<" exception=0x"<<std::hex<<fault->ExceptionRecord->ExceptionCode<<" address="<<fault->ExceptionRecord->ExceptionAddress;
        HMODULE owner{};if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(fault->ExceptionRecord->ExceptionAddress),&owner))out<<" module_base="<<owner<<" rva=0x"<<(reinterpret_cast<std::uintptr_t>(fault->ExceptionRecord->ExceptionAddress)-reinterpret_cast<std::uintptr_t>(owner));log(out.str());
    }catch(...){}
    return EXCEPTION_EXECUTE_HANDLER;
}
void guarded_frame_call(void(*operation)(IDXGISwapChain*,UINT),IDXGISwapChain* chain,UINT flags){
    __try{operation(chain,flags);}__except(record_frame_fault(GetExceptionInformation())){}
}
void render_hook(IDXGISwapChain* presented,UINT flags){
    static std::atomic<bool> first{};const bool trace=callbacks_enabled&&!first.exchange(true);
    if(trace)log("TV host callback entered; invoking original ReSkate render");
    // Original host failures belong to its existing guarded_render. Do not mask them.
    // The TV must not hold an extra host lock while the original polls HID/input.
    begin_tv_menu_input_frame();
    original_render(presented,flags);
    finish_tv_menu_input_frame();
    if(trace)log("Original ReSkate render returned; entering guarded TV extension");
    {static GateTrace dispatch_trace;dispatch_trace.sample("callback_dispatch",!callbacks_enabled?"callbacks_inactive":frame_failed?"extension_disabled_after_fault":"active",[&]{return "flags="+std::to_string(flags);});}
    if(callbacks_enabled&&!frame_failed){
        guarded_frame_call(host_frame,presented,flags);
        // ReSkate's host callback is the actual presentation path for the TV.
        // The DXGI Present hook is not reached in this path, so the native
        // tracker previously never advanced its submission-based ranking.
        depth::native::present();
    }
}

}
bool host_shutdown_requested(){
    if(!host||!at<std::uintptr_t>(host212::state))return false;
    dingosdk::overlay::Status status;
    try{reinterpret_cast<void(*)(dingosdk::overlay::Status*)>(host+host212::status)(&status);return status.stopping||status.failed;}
    catch(...){return true;}
}
HostReadiness host_readiness(){
    static bool rejected{};if(rejected)return HostReadiness::Rejected;
    if(!host){
        const auto mod=GetModuleHandleW(L"ReSkate.dll");if(!mod)return HostReadiness::Waiting;
        if(!matching_file(mod)){rejected=true;log("Host readiness rejected: ReSkate.dll does not match the verified 2026-10-24 updated host revision (ReSkate 6)");return HostReadiness::Rejected;}
        host=reinterpret_cast<std::uintptr_t>(mod);
        const auto& status=host212::status_bytes;
        if(GetProcAddress(mod,"DingoSDKOverlayGetStatus")!=reinterpret_cast<FARPROC>(host+host212::status)||!prefix(host212::status,status)){rejected=true;log("Host readiness rejected: loaded status export differs");return HostReadiness::Rejected;}
    }
    // Do not invoke the getter's lazy singleton initialization during game bootstrap.
    if(!at<std::uintptr_t>(host212::state))return HostReadiness::Waiting;
    dingosdk::overlay::Status status;
    try{reinterpret_cast<void(*)(dingosdk::overlay::Status*)>(host+host212::status)(&status);}
    catch(...){rejected=true;log("Host readiness status query failed; no TV graphics/menu hooks installed");return HostReadiness::Rejected;}
    if(status.failed||status.stopping){rejected=true;log("Host readiness rejected: ReSkate reports failed/stopping overlay");return HostReadiness::Rejected;}
    return status.started&&status.bound&&status.ready&&status.rendered_frames>0?HostReadiness::Ready:HostReadiness::Waiting;
}
void prepare_host(HostDraw draw,HookTransaction& hooks){
    if(!host||!prefix(host212::mods,host212::mods_bytes)||!prefix(host212::render,host212::render_bytes)||!prefix(host212::context_getter,host212::context_getter_bytes)||!prefix(host212::mutex_lock,host212::mutex_lock_bytes)||!prefix(host212::mutex_unlock,host212::mutex_unlock_bytes))throw std::runtime_error("ReSkate 2026-10-24 updated host function bytes rejected");
    prepare_tv_menu(hooks);
    hooks.add(reinterpret_cast<void*>(host+host212::render),reinterpret_cast<void*>(&render_hook),reinterpret_cast<void**>(&original_render));
    video_draw=draw;
    log("TV host hooks prepared disabled; independent TV menu replaces Mods-page injection");
}
void activate_host_callbacks(bool enabled){callbacks_enabled=enabled;}
void host_open_tv(){open_requested=true;}
void host_show_tv(){show_requested=true;}
void host_close_plugin_url(){close_url_requested=true;}
void* lock_host_render(){if(!host)return nullptr;auto* state=at<void*>(host212::state);if(!state)return nullptr;return reinterpret_cast<int(*)(void*)>(host+host212::mutex_lock)(state)==0?state:nullptr;}
void unlock_host_render(void* token){if(token)reinterpret_cast<int(*)(void*)>(host+host212::mutex_unlock)(token);}
#ifdef TV_HOST_TEST
bool host_bridge_test(){
    callbacks_enabled=true;
    // Synthetic host memory only. No ReSkate module is loaded or hooked by this test.
    auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,host212::image_size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    auto* state=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x11000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!image||!state)return false;
    auto* model=new(state+host212::model) dingosdk::overlay::Model;
    model->debug.skater_position_valid=true;model->debug.skater_position={10,20,30};
    auto* previous_context=ImGui::GetCurrentContext();ImGuiMemAllocFunc previous_alloc{};ImGuiMemFreeFunc previous_free{};void* previous_data{};
    ImGui::GetAllocatorFunctions(&previous_alloc,&previous_free,&previous_data);
    auto* own=ImGui::CreateContext();std::size_t allocations{};
    const auto alloc=+[](std::size_t bytes,void* user)->void*{++*static_cast<std::size_t*>(user);return HeapAlloc(GetProcessHeap(),0,bytes);};
    const auto free=+[](void* p,void* user){if(p){--*static_cast<std::size_t*>(user);HeapFree(GetProcessHeap(),0,p);}};
    ImGui::SetAllocatorFunctions(alloc,free,&allocations);auto* context=ImGui::CreateContext();ImGui::SetCurrentContext(context);
    auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1280,1080};io.DeltaTime=1.f/60;
    unsigned char* pixels{};int width{},height{};io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    host=reinterpret_cast<std::uintptr_t>(image);
    *reinterpret_cast<void**>(image+host212::state)=state;
    *reinterpret_cast<ImGuiContext**>(image+host212::gui_context)=context;
    *reinterpret_cast<ImGuiMemAllocFunc*>(image+host212::alloc)=alloc;
    *reinterpret_cast<ImGuiMemFreeFunc*>(image+host212::free)=free;
    *reinterpret_cast<void**>(image+host212::allocator_data)=&allocations;
    *reinterpret_cast<ImGuiContext**>(state+host212::context)=context;
    static unsigned original_calls{};original_calls=0;
    original_mods=+[](void*,int,const dingosdk::overlay::Model*,const void*){++original_calls;};
    bool good=true;
    for(unsigned frame=0;frame<4;++frame){
        ImGui::SetAllocatorFunctions(alloc,free,&allocations);ImGui::SetCurrentContext(context);ImGui::NewFrame();
        ImGui::SetNextWindowSize({1100,1000});ImGui::Begin("Mods");
        ImGui::SetCurrentContext(own);ImGui::SetAllocatorFunctions(previous_alloc,previous_free,previous_data);
        mods_hook(state+host212::menu,0,model,nullptr);
        ImGuiMemAllocFunc a{};ImGuiMemFreeFunc f{};void* u{};ImGui::GetAllocatorFunctions(&a,&f,&u);
        good&=ImGui::GetCurrentContext()==own&&a==previous_alloc&&f==previous_free&&u==previous_data&&!ui_failed;
        ImGui::SetAllocatorFunctions(alloc,free,&allocations);ImGui::SetCurrentContext(context);ImGui::End();ImGui::Render();good&=ImGui::GetDrawData()->TotalVtxCount>0;
    }
    good&=original_calls==4;
    // An unrelated menu address must preserve the original draw without touching TV UI.
    mods_hook(state+27665,0,model,nullptr);good&=original_calls==5;
    ImGui::DestroyContext(context);good&=allocations==0;
    ImGui::SetAllocatorFunctions(previous_alloc,previous_free,previous_data);ImGui::SetCurrentContext(own);ImGui::DestroyContext(own);ImGui::SetCurrentContext(previous_context);
    callback_stage="synthetic callback failure";
    guarded_frame_call(+[](IDXGISwapChain*,UINT){RaiseException(0xe0420001,0,0,nullptr);},nullptr,0);
    good&=frame_failed.load();frame_failed=false;
    model->~Model();callbacks_enabled=false;host=0;original_mods=nullptr;VirtualFree(state,0,MEM_RELEASE);VirtualFree(image,0,MEM_RELEASE);return good;
}
#endif
}
