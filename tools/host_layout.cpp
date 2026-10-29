#include "Extension/UI/Overlay/overlay.h"
#include <imgui_internal.h>
#include <iostream>
int main(){using namespace dingosdk::overlay;std::cout<<"model="<<sizeof(Model)<<" debug="<<offsetof(Model,debug)<<" editor="<<offsetof(Model,editor)<<" player="<<offsetof(DebugModel,skater_position)<<" valid="<<offsetof(DebugModel,skater_position_valid)<<" gui="<<sizeof(ImGuiContext)<<" io="<<sizeof(ImGuiIO)<<'\n';}
