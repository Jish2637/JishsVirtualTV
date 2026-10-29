#pragma once
#include <windows.h>
#include <vector>
namespace virtualtv {
class HookTransaction;
struct PanelBounds { int left{},top{},right{},bottom{}; bool contains(int x,int y) const noexcept { return x>=left && x<right && y>=top && y<bottom; } };
struct PointerRouter {
    PanelBounds bounds;
    bool visible{},dragging{},keyboard{};
    bool route(UINT message,WPARAM key,int x,int y) noexcept;
};
struct MenuInput { UINT message{}; WPARAM wparam{}; LPARAM lparam{}; HWND window{}; };
void prepare_tv_menu_input(HookTransaction&);
void route_tv_menu_input(HWND);
void set_tv_menu_bounds(PanelBounds,bool,bool);
void hide_tv_menu_input();
std::vector<MenuInput> take_tv_menu_input();
void feed_input(HWND);
void begin_tv_menu_input_frame();
void finish_tv_menu_input_frame();
}
