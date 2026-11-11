#include "../../src/host_2145.h"
#include "Extension/UI/Overlay/overlay_internal.h"
#include <iostream>
#define F(x) std::cout << #x << "=" << offsetof(dingosdk::overlay::detail::State,x) << "\n";
int main(){F(render_mutex) F(visible) F(ready) F(failed) F(swapchain) F(queue) F(context) F(model) F(menu)
using M=dingosdk::overlay::Model; std::cout<<"Model="<<sizeof(M)<<" editor="<<offsetof(M,editor)<<" debug="<<offsetof(M,debug)<<"\n";}

static_assert(offsetof(dingosdk::overlay::detail::State,render_mutex)==0);
static_assert(offsetof(dingosdk::overlay::detail::State,visible)==virtualtv::host2145::visible);
static_assert(offsetof(dingosdk::overlay::detail::State,ready)==virtualtv::host2145::ready);
static_assert(offsetof(dingosdk::overlay::detail::State,failed)==virtualtv::host2145::failed);
static_assert(offsetof(dingosdk::overlay::detail::State,swapchain)==virtualtv::host2145::swapchain);
static_assert(offsetof(dingosdk::overlay::detail::State,queue)==virtualtv::host2145::queue);
static_assert(offsetof(dingosdk::overlay::detail::State,context)==virtualtv::host2145::context);
static_assert(offsetof(dingosdk::overlay::detail::State,model)==virtualtv::host2145::model);
static_assert(offsetof(dingosdk::overlay::detail::State,menu)==virtualtv::host2145::menu);
static_assert(sizeof(dingosdk::overlay::Model)==26248);
static_assert(sizeof(decltype(dingosdk::overlay::detail::State::render_mutex))==80);
static_assert(sizeof(dingosdk::overlay::Status)==16);
