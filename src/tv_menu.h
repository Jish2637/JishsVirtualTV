#pragma once
#include <windows.h>
#include <dxgi.h>
#include <d3d12.h>
namespace dingosdk::overlay { struct Model; }
namespace virtualtv {
class HookTransaction;
void prepare_tv_menu(HookTransaction&);
void draw_tv_menu(IDXGISwapChain*,ID3D12CommandQueue*,const dingosdk::overlay::Model&,bool&);
bool release_tv_menu(IDXGISwapChain*);
}
