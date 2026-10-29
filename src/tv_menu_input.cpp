// Input routing and queued message feed ported from JishsFirstPersonMod 1.34.0.
#include "tv_menu_input.h"
#include "hook_transaction.h"
#include <imgui.h>
#include <intrin.h>
#include <map>
#include <deque>
#include <memory>
#include <mutex>
#include <windowsx.h>
#include <stdexcept>
#include <array>
namespace virtualtv {
bool PointerRouter::route(UINT message,WPARAM key,int x,int y) noexcept {
    if(!visible) { dragging=keyboard=false; return false; }
    if(message==WM_KEYDOWN || message==WM_KEYUP || message==WM_SYSKEYDOWN || message==WM_SYSKEYUP) {
        if(key==VK_INSERT || key==VK_ESCAPE || message==WM_SYSKEYDOWN || message==WM_SYSKEYUP) return false;
        return keyboard;
    }
    if(message==WM_CHAR) return keyboard;
    if(message==WM_KILLFOCUS || message==WM_ACTIVATEAPP) { dragging=keyboard=false; return false; }
    const bool inside=bounds.contains(x,y);
    if(message==WM_LBUTTONDOWN || message==WM_RBUTTONDOWN || message==WM_MBUTTONDOWN) {
        keyboard=inside; dragging=inside; return inside;
    }
    if(message==WM_LBUTTONUP || message==WM_RBUTTONUP || message==WM_MBUTTONUP) {
        const auto own=inside||dragging; dragging=false; return own;
    }
    if(message==WM_MOUSEWHEEL || message==WM_MOUSEHWHEEL) return inside||dragging;
    if(message==WM_INPUT) return inside||dragging;
    // Both menus keep the same pointer location, but clicks in this panel never
    // reach stock widgets beneath it. Insert/Escape always reach stock.
    return false;
}
namespace {
struct WindowLink { WNDPROC previous{}; };
struct InputState {
    std::mutex mutex;
    std::map<HWND,std::unique_ptr<WindowLink>> links;
    std::deque<MenuInput> pending;
    PointerRouter router;
    HWND selected{};
    std::array<bool,5> sampled_buttons{};unsigned sampled_mask{};
};
InputState& state(){static auto* value=new InputState;return *value;}
using AsyncKey=SHORT(WINAPI*)(int);
AsyncKey original_async_key{};
thread_local bool sampling_host{};
thread_local unsigned sampled_mask{},sampled_down{};
bool owns_mouse() {
    auto& input=state(); std::lock_guard lock(input.mutex);
    if(!input.router.visible || !input.selected || GetAncestor(input.selected,GA_ROOT)!=GetForegroundWindow()) return false;
    CURSORINFO cursor{sizeof(CURSORINFO)};
    if(!GetCursorInfo(&cursor)) return input.router.dragging;
    auto point=cursor.ptScreenPos;
    return ScreenToClient(input.selected,&point) && (input.router.bounds.contains(point.x,point.y)||input.router.dragging);
}
SHORT WINAPI panel_async_key(int key) {
    const auto caller=_ReturnAddress();
    const auto result=original_async_key(key);
    const auto error=GetLastError();
    HMODULE owner{};
    const bool mouse=key==VK_LBUTTON || key==VK_RBUTTON || key==VK_MBUTTON || key==VK_XBUTTON1 || key==VK_XBUTTON2;
    const bool host_mouse=mouse && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(caller),&owner) && owner==GetModuleHandleW(L"reskate.dll");
    // ReSkate 2.1.2 filters polled input outside its OverlayInputAccess scope.
    // Preserve its own authorized sample before isolating clicks from stock UI.
    if(host_mouse&&sampling_host){const int keys[]{VK_LBUTTON,VK_RBUTTON,VK_MBUTTON,VK_XBUTTON1,VK_XBUTTON2};for(unsigned n=0;n<5;++n)if(key==keys[n]){sampled_mask|=1u<<n;if(result&0x8000)sampled_down|=1u<<n;}}
    const bool block=host_mouse && owns_mouse();
    SetLastError(error);
    return block ? 0:result;
}
LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    auto& input=state(); WNDPROC previous{}; bool swallow{};
    {
        std::lock_guard lock(input.mutex);
        const auto found=input.links.find(window);
        if(found!=input.links.end()) previous=found->second->previous;
        if(window==input.selected) {
            POINT pointer{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            if(message==WM_MOUSEWHEEL || message==WM_MOUSEHWHEEL) ScreenToClient(window,&pointer);
            if(message==WM_INPUT) {
                CURSORINFO cursor{sizeof(CURSORINFO)};
                if(GetCursorInfo(&cursor)){pointer=cursor.ptScreenPos;ScreenToClient(window,&pointer);}
            }
            // F8 remains owned by VirtualTV's existing foreground hotkey handler.
            swallow=wp!=VK_F8 || (message!=WM_KEYDOWN&&message!=WM_KEYUP)
                ? input.router.route(message,wp,pointer.x,pointer.y):false;
            const bool relevant=(message>=WM_MOUSEFIRST&&message<=WM_MOUSELAST)||
                (message>=WM_KEYFIRST&&message<=WM_KEYLAST)||message==WM_KILLFOCUS;
            if(input.router.visible&&relevant) {
                if(message==WM_MOUSEMOVE&&!input.pending.empty()&&input.pending.back().message==WM_MOUSEMOVE) input.pending.back()={message,wp,lp,window};
                else {if(input.pending.size()>=256)input.pending.pop_front();input.pending.push_back({message,wp,lp,window});}
            }
            if(message==WM_NCDESTROY){input.router={};input.selected=nullptr;input.pending.clear();}
        }
    }
    if(swallow)return message==WM_INPUT?DefWindowProcW(window,message,wp,lp):0;
    return previous?CallWindowProcW(previous,window,message,wp,lp):DefWindowProcW(window,message,wp,lp);
}
}
void prepare_tv_menu_input(HookTransaction& hooks){hooks.add(reinterpret_cast<void*>(&GetAsyncKeyState),reinterpret_cast<void*>(&panel_async_key),reinterpret_cast<void**>(&original_async_key));}
void route_tv_menu_input(HWND window) {
    auto& input=state(); std::lock_guard lock(input.mutex);
    input.selected=window;
    if(input.links.contains(window)) return;
    auto link=std::make_unique<WindowLink>();
    link->previous=reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window,GWLP_WNDPROC));
    input.links.emplace(window,std::move(link));
    SetLastError(0);
    const auto old=SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(&procedure));
    if(!old && GetLastError()) { input.links.erase(window); throw std::runtime_error("Could not attach companion-menu input."); }
    if(old) input.links.at(window)->previous=reinterpret_cast<WNDPROC>(old);
}

void set_tv_menu_bounds(PanelBounds bounds,bool visible,bool keyboard){
    auto& input=state();std::lock_guard lock(input.mutex);
    input.router.bounds=bounds;input.router.visible=visible;input.router.keyboard=keyboard;
    // Preserve the HWND down/up capture latch between render frames.
    if(!visible){input.router.dragging=false;input.pending.clear();}
}
void hide_tv_menu_input(){set_tv_menu_bounds({},false,false);}
std::vector<MenuInput> take_tv_menu_input(){auto& input=state();std::lock_guard lock(input.mutex);std::vector<MenuInput> result(input.pending.begin(),input.pending.end());input.pending.clear();return result;}
void begin_tv_menu_input_frame(){sampling_host=true;sampled_mask=sampled_down=0;}
void finish_tv_menu_input_frame(){
    sampling_host=false;auto& input=state();std::lock_guard lock(input.mutex);
    input.sampled_mask=sampled_mask;
    CURSORINFO cursor{sizeof(CURSORINFO)};POINT point{};bool inside=false;
    if(input.selected&&GetCursorInfo(&cursor)){point=cursor.ptScreenPos;inside=ScreenToClient(input.selected,&point)&&input.router.bounds.contains(point.x,point.y);}
    for(unsigned n=0;n<5;++n)if(sampled_mask&(1u<<n))input.sampled_buttons[n]=(sampled_down&(1u<<n))!=0;
    if(input.router.visible&&sampled_mask){
        const bool down=(sampled_down&7)!=0;
        if(down&&inside){input.router.dragging=true;input.router.keyboard=true;}
        if(!down&&(sampled_mask&7)==7)input.router.dragging=false;
    }
}
void feed_input(HWND window) {
    auto& io=ImGui::GetIO();
    std::array<bool,5> message_button{};
    for(const auto& input:take_tv_menu_input()) {
        if(input.message==WM_LBUTTONDOWN||input.message==WM_LBUTTONUP)message_button[0]=true;
        if(input.message==WM_RBUTTONDOWN||input.message==WM_RBUTTONUP)message_button[1]=true;
        if(input.message==WM_MBUTTONDOWN||input.message==WM_MBUTTONUP)message_button[2]=true;
        if(input.message==WM_LBUTTONDOWN || input.message==WM_LBUTTONUP ||
           input.message==WM_RBUTTONDOWN || input.message==WM_RBUTTONUP ||
           input.message==WM_MBUTTONDOWN || input.message==WM_MBUTTONUP) {
            io.AddMousePosEvent(static_cast<float>(GET_X_LPARAM(input.lparam)),static_cast<float>(GET_Y_LPARAM(input.lparam)));
        }
        switch(input.message) {
        case WM_MOUSEMOVE: io.AddMousePosEvent(static_cast<float>(GET_X_LPARAM(input.lparam)),static_cast<float>(GET_Y_LPARAM(input.lparam))); break;
        case WM_LBUTTONDOWN: io.AddMouseButtonEvent(0,true); break;
        case WM_LBUTTONUP: io.AddMouseButtonEvent(0,false); break;
        case WM_RBUTTONDOWN: io.AddMouseButtonEvent(1,true); break;
        case WM_RBUTTONUP: io.AddMouseButtonEvent(1,false); break;
        case WM_MBUTTONDOWN: io.AddMouseButtonEvent(2,true); break;
        case WM_MBUTTONUP: io.AddMouseButtonEvent(2,false); break;
        case WM_MOUSEWHEEL: io.AddMouseWheelEvent(0,static_cast<float>(GET_WHEEL_DELTA_WPARAM(input.wparam))/WHEEL_DELTA); break;
        case WM_MOUSEHWHEEL: io.AddMouseWheelEvent(-static_cast<float>(GET_WHEEL_DELTA_WPARAM(input.wparam))/WHEEL_DELTA,0); break;
        case WM_CHAR: if(input.wparam>0 && input.wparam<0x10000) io.AddInputCharacterUTF16(static_cast<ImWchar16>(input.wparam)); break;
        case WM_KILLFOCUS: io.AddFocusEvent(false); break;
        case WM_KEYDOWN: case WM_KEYUP: {
            const bool down=input.message==WM_KEYDOWN;
            ImGuiKey key=ImGuiKey_None;
            const auto vk=input.wparam;
            if(vk>='A' && vk<='Z') key=static_cast<ImGuiKey>(ImGuiKey_A+(vk-'A'));
            else if(vk>='0' && vk<='9') key=static_cast<ImGuiKey>(ImGuiKey_0+(vk-'0'));
            else switch(vk) {
            case VK_TAB:key=ImGuiKey_Tab;break; case VK_LEFT:key=ImGuiKey_LeftArrow;break; case VK_RIGHT:key=ImGuiKey_RightArrow;break;
            case VK_UP:key=ImGuiKey_UpArrow;break; case VK_DOWN:key=ImGuiKey_DownArrow;break; case VK_HOME:key=ImGuiKey_Home;break;
            case VK_END:key=ImGuiKey_End;break; case VK_DELETE:key=ImGuiKey_Delete;break; case VK_BACK:key=ImGuiKey_Backspace;break;
            case VK_RETURN:key=ImGuiKey_Enter;break; case VK_ESCAPE:key=ImGuiKey_Escape;break; case VK_SPACE:key=ImGuiKey_Space;break;
            case VK_CONTROL:io.AddKeyEvent(ImGuiMod_Ctrl,down);break; case VK_SHIFT:io.AddKeyEvent(ImGuiMod_Shift,down);break;
            }
            if(key!=ImGuiKey_None) io.AddKeyEvent(key,down);
            break;
        }
        }
    }
    // Keep queued transitions authoritative. Controller/raw-input mode can suppress
    // legacy button messages, so fill only missing buttons from stock's own samples.
    {auto& input=state();std::lock_guard lock(input.mutex);const bool swapped=GetSystemMetrics(SM_SWAPBUTTON)!=0;
        for(unsigned n=0;n<5;++n){const auto physical=swapped&&n<2?1-n:n;if(!message_button[n]&&(input.sampled_mask&(1u<<physical)))io.AddMouseButtonEvent(n,input.sampled_buttons[physical]);}}
    // GetCursorInfo is not filtered by stock's GetCursorPos input gate. Poll
    // after queued messages so old movement cannot override the latest position.
    CURSORINFO cursor{sizeof(CURSORINFO)};
    if(GetCursorInfo(&cursor)) {
        auto point=cursor.ptScreenPos;
        if(ScreenToClient(window,&point)) io.AddMousePosEvent(static_cast<float>(point.x),static_cast<float>(point.y));
    }
}
}
