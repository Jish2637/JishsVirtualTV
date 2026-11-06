#include "../../src/host_2142.h"
#include "Extension/UI/Overlay/overlay_internal.h"
#include <iostream>
#define F(x) std::cout << #x << "=" << offsetof(dingosdk::overlay::detail::State,x) << "\n";
int main(){F(render_mutex) F(visible) F(ready) F(failed) F(swapchain) F(queue) F(context) F(model) F(menu)
using M=dingosdk::overlay::Model; std::cout<<"Model="<<sizeof(M)<<" editor="<<offsetof(M,editor)<<" debug="<<offsetof(M,debug)<<"\n";}

static_assert(offsetof(dingosdk::overlay::detail::State,render_mutex)==0);
static_assert(offsetof(dingosdk::overlay::detail::State,visible)==virtualtv::host2142::visible);
static_assert(offsetof(dingosdk::overlay::detail::State,ready)==virtualtv::host2142::ready);
static_assert(offsetof(dingosdk::overlay::detail::State,failed)==virtualtv::host2142::failed);
static_assert(offsetof(dingosdk::overlay::detail::State,swapchain)==virtualtv::host2142::swapchain);
static_assert(offsetof(dingosdk::overlay::detail::State,queue)==virtualtv::host2142::queue);
static_assert(offsetof(dingosdk::overlay::detail::State,context)==virtualtv::host2142::context);
static_assert(offsetof(dingosdk::overlay::detail::State,model)==virtualtv::host2142::model);
static_assert(offsetof(dingosdk::overlay::detail::State,menu)==virtualtv::host2142::menu);