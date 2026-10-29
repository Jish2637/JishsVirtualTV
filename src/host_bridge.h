#pragma once
#include <dxgi.h>
#include <d3d12.h>
namespace dingosdk::overlay { struct Model; }
namespace virtualtv {
using HostDraw = void(*)(IDXGISwapChain*,ID3D12CommandQueue*);
class HookTransaction;
enum class HostReadiness { Waiting,Ready,Rejected };
HostReadiness host_readiness();
bool host_shutdown_requested();
void prepare_host(HostDraw,HookTransaction&);
void activate_host_callbacks(bool);
void draw_tv_controls(const dingosdk::overlay::Model&);
void host_open_tv();
void host_show_tv();
void host_close_plugin_url();
void* lock_host_render();
void unlock_host_render(void*);
}
