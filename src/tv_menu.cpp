// Companion-window approach adapted from JishsFirstPersonMod 1.34.0.
// Presentation pairing, frame guard and input routing follow the supplied working menu.
#include "tv_menu.h"
#include "dx12_native.h"
#include <dxgi1_4.h>
#include "host_bridge.h"
#include "hook_transaction.h"
#include "tv_runtime.h"
#include "tv_menu_input.h"
#include "Extension/UI/Overlay/overlay.h"
#include <algorithm>
#include <atomic>
#include <memory>
#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <wrl/client.h>
#include <windowsx.h>
#include <array>
#include <vector>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <sstream>
#include <intrin.h>
namespace virtualtv {
namespace {
using Microsoft::WRL::ComPtr;
struct Frame {ComPtr<ID3D12Resource> buffer;ComPtr<ID3D12CommandAllocator> allocator;D3D12_CPU_DESCRIPTOR_HANDLE rtv{};UINT64 fence{};};
struct Panel {std::recursive_mutex mutex;ComPtr<IDXGISwapChain3> chain;ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue,submission_queue;ComPtr<ID3D12DescriptorHeap> rtvs,srvs;ComPtr<ID3D12GraphicsCommandList> list;ComPtr<ID3D12Fence> fence;std::vector<Frame> frames;std::vector<UINT64> uploads;HANDLE event{};UINT64 next{},draws{};LARGE_INTEGER frequency{},last{};ImGuiContext* context{};bool backend{},failed{};};
Panel& panel(){static auto* value=new Panel;return *value;}
void check(HRESULT hr,const char* step){if(FAILED(hr)){std::ostringstream s;s<<step<<" failed: 0x"<<std::hex<<static_cast<unsigned long>(hr);throw std::runtime_error(s.str());}}
bool same(IUnknown* a,IUnknown* b){ComPtr<IUnknown>x,y;return a&&b&&SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(&x)))&&SUCCEEDED(b->QueryInterface(IID_PPV_ARGS(&y)))&&x==y;}
void hide(){hide_tv_menu_input();}
bool wait(Panel& p,UINT64 value){return !value||p.fence->GetCompletedValue()>=value||(SUCCEEDED(p.fence->SetEventOnCompletion(value,p.event))&&WaitForSingleObject(p.event,1000)==WAIT_OBJECT_0);}
bool release(Panel& p){
    hide();if(p.fence&&!wait(p,p.next)&&p.device&&p.device->GetDeviceRemovedReason()==S_OK)return false;
    if(p.context){auto* previous=ImGui::GetCurrentContext();ImGui::SetCurrentContext(p.context);if(p.backend)ImGui_ImplDX12_Shutdown();ImGui::DestroyContext(p.context);ImGui::SetCurrentContext(previous==p.context?nullptr:previous);}
    p.context=nullptr;p.backend=false;p.frames.clear();p.uploads.clear();p.list.Reset();p.rtvs.Reset();p.srvs.Reset();p.fence.Reset();p.queue.Reset();p.submission_queue.Reset();p.device.Reset();p.chain.Reset();if(p.event)CloseHandle(p.event);p.event=nullptr;p.next=p.draws=0;return true;
}
void initialize(Panel& p,IDXGISwapChain* chain,ID3D12CommandQueue* queue){
    if(!release(p))throw std::runtime_error("TV menu GPU resources still in flight");
    check(chain->QueryInterface(IID_PPV_ARGS(&p.chain)),"TV menu swapchain");check(queue->GetDevice(IID_PPV_ARGS(&p.device)),"TV menu queue device");ComPtr<ID3D12Device> device;check(chain->GetDevice(IID_PPV_ARGS(&device)),"TV menu swapchain device");bool chain_unwrapped{},queue_device_unwrapped{};
    auto native_device=native_interface(device.Get(),chain_unwrapped);auto native_queue_device=native_interface(p.device.Get(),queue_device_unwrapped);
    const bool native_equal=same(native_device.Get(),native_queue_device.Get());
    log("TV menu canonical validation: chain_unwrapped="+std::to_string(chain_unwrapped)+" queue_device_unwrapped="+std::to_string(queue_device_unwrapped)+" same_native_identity="+std::to_string(native_equal));
    if(!native_equal||queue->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT)throw std::runtime_error("TV menu canonical device/queue mismatch");
    bool submission_unwrapped{};p.submission_queue=native_interface(queue,submission_unwrapped);
    ComPtr<ID3D12Device> submission_device;check(p.submission_queue->GetDevice(IID_PPV_ARGS(&submission_device)),"TV menu native queue device");
    bool submission_device_unwrapped{};submission_device=native_interface(submission_device.Get(),submission_device_unwrapped);
    if(!same(native_device.Get(),submission_device.Get()))throw std::runtime_error("TV menu native queue ownership mismatch");
    p.device=native_device;p.queue=queue;
    DXGI_SWAP_CHAIN_DESC desc{};check(chain->GetDesc(&desc),"TV menu swapchain description");if(desc.BufferCount<2||desc.BufferCount>8)throw std::runtime_error("TV menu buffer count rejected");
    D3D12_DESCRIPTOR_HEAP_DESC heap{};heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;heap.NumDescriptors=desc.BufferCount;check(p.device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&p.rtvs)),"TV menu RTV heap");heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;heap.NumDescriptors=1;heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;check(p.device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&p.srvs)),"TV menu font heap");
    auto handle=p.rtvs->GetCPUDescriptorHandleForHeapStart();const auto stride=p.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);p.frames.resize(desc.BufferCount);p.uploads.assign(desc.BufferCount,0);
    for(UINT n=0;n<desc.BufferCount;++n){auto& f=p.frames[n];check(chain->GetBuffer(n,IID_PPV_ARGS(&f.buffer)),"TV menu backbuffer");check(p.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&f.allocator)),"TV menu allocator");f.rtv=handle;p.device->CreateRenderTargetView(f.buffer.Get(),nullptr,handle);handle.ptr+=stride;}
    check(p.device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,p.frames[0].allocator.Get(),nullptr,IID_PPV_ARGS(&p.list)),"TV menu list");check(p.list->Close(),"TV menu initial close");p.list->SetName(L"JishsVirtualTV menu draw");check(p.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&p.fence)),"TV menu fence");p.event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!p.event)throw std::runtime_error("TV menu fence event unavailable");
    auto* previous=ImGui::GetCurrentContext();struct Restore{ImGuiContext* old;~Restore(){ImGui::SetCurrentContext(old);}} restore{previous};p.context=ImGui::CreateContext();ImGui::SetCurrentContext(p.context);ImGui::GetIO().IniFilename=nullptr;ImGui::GetIO().LogFilename=nullptr;ImGui::StyleColorsDark();
    // ReSkate Extension/UI/skate_theme.h: flat, high-contrast charcoal tiles
    // with its blue accent. Own context: never modify the host overlay style.
    auto& style=ImGui::GetStyle();
    style.WindowRounding=4.f;style.ChildRounding=0.f;style.FrameRounding=2.f;
    style.GrabRounding=2.f;style.PopupRounding=3.f;
    style.WindowPadding=ImVec2(17,15);style.FramePadding=ImVec2(11,7);
    style.ItemSpacing=ImVec2(10,9);style.ItemInnerSpacing=ImVec2(8,7);
    style.IndentSpacing=18;style.ScrollbarSize=17;style.WindowBorderSize=1;
    auto& co=style.Colors;
    co[ImGuiCol_Text]=ImVec4(245/255.f,245/255.f,245/255.f,1);
    co[ImGuiCol_TextDisabled]=ImVec4(150/255.f,152/255.f,158/255.f,1);
    co[ImGuiCol_WindowBg]=ImVec4(18/255.f,18/255.f,19/255.f,.965f);
    co[ImGuiCol_PopupBg]=ImVec4(26/255.f,26/255.f,26/255.f,.988f);
    co[ImGuiCol_Border]=ImVec4(61/255.f,62/255.f,66/255.f,1);
    co[ImGuiCol_FrameBg]=co[ImGuiCol_Button]=ImVec4(45/255.f,45/255.f,47/255.f,.94f);
    co[ImGuiCol_FrameBgHovered]=co[ImGuiCol_ButtonHovered]=co[ImGuiCol_Header]=ImVec4(61/255.f,62/255.f,66/255.f,.94f);
    co[ImGuiCol_FrameBgActive]=co[ImGuiCol_HeaderHovered]=ImVec4(72/255.f,73/255.f,78/255.f,1);
    co[ImGuiCol_ButtonActive]=co[ImGuiCol_HeaderActive]=ImVec4(0,110/255.f,215/255.f,1);
    co[ImGuiCol_CheckMark]=co[ImGuiCol_SliderGrab]=ImVec4(1/255.f,131/255.f,1,1);
    co[ImGuiCol_SliderGrabActive]=ImVec4(1,1,1,1);
    co[ImGuiCol_Separator]=co[ImGuiCol_Border];
    ImGui::GetIO().ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;ImGui::GetIO().BackendPlatformName="VirtualTV Win32 messages (FirstPersonMod)";
    ImGui_ImplDX12_InitInfo info{};info.Device=p.device.Get();info.CommandQueue=p.submission_queue.Get();info.NumFramesInFlight=desc.BufferCount;info.RTVFormat=p.frames[0].buffer->GetDesc().Format;info.SrvDescriptorHeap=p.srvs.Get();info.LegacySingleSrvCpuDescriptor=p.srvs->GetCPUDescriptorHandleForHeapStart();info.LegacySingleSrvGpuDescriptor=p.srvs->GetGPUDescriptorHandleForHeapStart();if(!ImGui_ImplDX12_Init(&info))throw std::runtime_error("TV menu ImGui initialization failed");p.backend=true;route_tv_menu_input(desc.OutputWindow);p.failed=false;log("Independent Virtual TV menu initialized on verified swapchain/queue");
}
void render_panel(IDXGISwapChain*,ID3D12CommandQueue*,const dingosdk::overlay::Model&,bool&);
}
namespace {
void render_panel(IDXGISwapChain* chain,ID3D12CommandQueue* queue,const dingosdk::overlay::Model& model,bool& visible){
    auto& p=panel();std::lock_guard lock(p.mutex);if(!visible){hide();return;}if(p.failed)return;
    try{DXGI_SWAP_CHAIN_DESC desc{};check(chain->GetDesc(&desc),"TV menu description");if(GetForegroundWindow()!=GetAncestor(desc.OutputWindow,GA_ROOT)){hide();if(p.context){auto* old=ImGui::GetCurrentContext();ImGui::SetCurrentContext(p.context);ImGui::GetIO().ClearInputKeys();ImGui::GetIO().ClearInputMouse();ImGui::SetCurrentContext(old);}return;}
        if(!same(p.chain.Get(),chain)||!same(p.queue.Get(),queue))initialize(p,chain,queue);
        const auto index=p.chain->GetCurrentBackBufferIndex();if(index>=p.frames.size())throw std::runtime_error("TV menu buffer index rejected");auto& f=p.frames[index];const auto slot=p.draws%p.uploads.size();if(!wait(p,f.fence)||!wait(p,p.uploads[slot]))throw std::runtime_error("TV menu GPU fence timeout");
        auto* previous=ImGui::GetCurrentContext();struct Restore{ImGuiContext* old;~Restore(){ImGui::SetCurrentContext(old);}} restore{previous};ImGui::SetCurrentContext(p.context);auto& io=ImGui::GetIO();const auto size=f.buffer->GetDesc();io.DisplaySize={float(size.Width),float(size.Height)};LARGE_INTEGER now{};if(!p.frequency.QuadPart)QueryPerformanceFrequency(&p.frequency);QueryPerformanceCounter(&now);io.DeltaTime=p.last.QuadPart?std::clamp(float(double(now.QuadPart-p.last.QuadPart)/double(p.frequency.QuadPart)),.001f,.1f):1.f/60;p.last=now;io.AddFocusEvent(true);feed_input(desc.OutputWindow);
        ImGui_ImplDX12_SetOutputTransfer(size.Format==DXGI_FORMAT_R16G16B16A16_FLOAT?1:0,200);ImGui_ImplDX12_NewFrame();
        const float dpi=std::clamp(float(GetDpiForWindow(desc.OutputWindow))/96.f,1.f,2.f);io.FontGlobalScale=dpi;
        const ImVec2 maximum{std::max(200.f,io.DisplaySize.x-24),std::max(180.f,io.DisplaySize.y-24)};
        ImGui::NewFrame();ImGui::SetNextWindowSize({std::min(690*dpi,maximum.x),std::min(790*dpi,maximum.y)},ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos({std::max(8.f,io.DisplaySize.x-700*dpi),8},ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints({std::min(420*dpi,maximum.x),std::min(320*dpi,maximum.y)},maximum);
        PanelBounds bounds{};bool keyboard{};
        if(ImGui::Begin("Jish's Virtual TV",&visible,ImGuiWindowFlags_NoCollapse)){
            const auto pos=ImGui::GetWindowPos(),extent=ImGui::GetWindowSize();
            const ImVec2 bounded{std::clamp(pos.x,0.f,std::max(0.f,io.DisplaySize.x-extent.x)),std::clamp(pos.y,0.f,std::max(0.f,io.DisplaySize.y-extent.y))};
            if(pos.x!=bounded.x||pos.y!=bounded.y)ImGui::SetWindowPos(bounded);
            bounds={int(bounded.x),int(bounded.y),int(bounded.x+extent.x),int(bounded.y+extent.y)};
            ImGui::TextColored(ImVec4(1/255.f,131/255.f,1,1),"VIRTUAL TV  /  SETTINGS");
            ImGui::Separator();
            ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x+ImGui::GetCursorPosX());
            draw_tv_controls(model);
            ImGui::PopTextWrapPos();
            if(ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId))bounds={0,0,int(io.DisplaySize.x),int(io.DisplaySize.y)};
            keyboard=io.WantCaptureKeyboard;
        }
        ImGui::End();
        io.MouseDrawCursor=bounds.contains(int(io.MousePos.x),int(io.MousePos.y))||(io.WantCaptureMouse&&ImGui::IsMouseDown(0));
        set_tv_menu_bounds(bounds,visible,keyboard);ImGui::Render();
        check(f.allocator->Reset(),"TV menu allocator reset");check(p.list->Reset(f.allocator.Get(),nullptr),"TV menu list reset");D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition.pResource=f.buffer.Get();barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_PRESENT;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_RENDER_TARGET;p.list->ResourceBarrier(1,&barrier);p.list->OMSetRenderTargets(1,&f.rtv,FALSE,nullptr);ID3D12DescriptorHeap* heaps[]{p.srvs.Get()};p.list->SetDescriptorHeaps(1,heaps);ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(),p.list.Get());std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);p.list->ResourceBarrier(1,&barrier);check(p.list->Close(),"TV menu list close");ID3D12CommandList* lists[]{p.list.Get()};p.submission_queue->ExecuteCommandLists(1,lists);check(p.submission_queue->Signal(p.fence.Get(),++p.next),"TV menu signal");f.fence=p.next;p.uploads[slot]=p.next;if(p.draws++==0)log("First independent Virtual TV menu submitted");
    }catch(const std::exception& e){p.failed=true;visible=false;hide();log(std::string("Virtual TV menu disabled: ")+e.what());}catch(...){p.failed=true;visible=false;hide();log("Virtual TV menu disabled after unexpected exception");}
}
// Host-selected presentation callback already follows ReSkate's overlay draw and
// VirtualTV's video submission. Reuse that verified chain/queue; no second DXGI
// method-discovery device, Present hooks or queue observer is needed for this panel.
struct Submission {ComPtr<IDXGISwapChain> chain;UINT count{},index{};bool valid{};};
Submission last_submission;
}
void prepare_tv_menu(HookTransaction& hooks){prepare_tv_menu_input(hooks);log("TV menu input prepared; using existing verified TV presentation callback");}
bool release_tv_menu(IDXGISwapChain* chain){auto& p=panel();std::lock_guard lock(p.mutex);if(same(last_submission.chain.Get(),chain))last_submission={};if(chain&&!same(p.chain.Get(),chain))return true;const bool result=release(p);if(result){p.failed=false;last_submission={};}return result;}
void draw_tv_menu(IDXGISwapChain* chain,ID3D12CommandQueue* queue,const dingosdk::overlay::Model& model,bool& visible){
    auto& p=panel();std::lock_guard lock(p.mutex);
    if(!visible){hide();last_submission.valid=false;if(p.context){auto* old=ImGui::GetCurrentContext();ImGui::SetCurrentContext(p.context);ImGui::GetIO().ClearInputKeys();ImGui::GetIO().ClearInputMouse();ImGui::SetCurrentContext(old);}return;}
    UINT count{};ComPtr<IDXGISwapChain3> extended;
    if(SUCCEEDED(chain->GetLastPresentCount(&count))&&SUCCEEDED(chain->QueryInterface(IID_PPV_ARGS(&extended)))){
        const UINT index=extended->GetCurrentBackBufferIndex();
        if(last_submission.valid&&same(last_submission.chain.Get(),chain)&&last_submission.count==count&&last_submission.index==index)return;
        last_submission={chain,count,index,true};
    }
    render_panel(chain,queue,model,visible);
}
}
