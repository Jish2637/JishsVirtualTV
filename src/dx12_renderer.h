#pragma once
#include "vlc_player.h"
namespace virtualtv {
enum class ScreenMode { Front, Attached, Hidden, SyncedWorld };
void set_screen_mode(ScreenMode);
ScreenMode current_screen_mode();
void show_test_pattern(bool);
void set_synced_scene(const Placement&, const Vec3&, const Screen&);
std::wstring renderer_status();
bool start_renderer(VlcPlayer&);
void stop_renderer();
#ifdef TV_GPU_TEST
bool renderer_gpu_test(bool hardware=false);
#endif
}
