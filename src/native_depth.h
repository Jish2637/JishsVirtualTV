#pragma once
#include "depth_capture.h"
namespace virtualtv::depth::native {
void activate(bool) noexcept;
void heuristic(bool) noexcept;
int capture_boundary() noexcept;
void set_capture_boundary(int) noexcept;
float estimated_near_plane() noexcept;
void set_estimated_near_plane(float) noexcept;
int minimum_scene_draws() noexcept;
void set_minimum_scene_draws(int) noexcept;
int selected_depth() noexcept;
void select_depth(int index) noexcept;
std::string depth_candidates() noexcept;
void invalidate() noexcept;
void lifetime(bool) noexcept;
void approve(const std::array<void*,11>&) noexcept;
void frame(const Scene&,unsigned,unsigned) noexcept;
void present() noexcept;
void before_bind(ID3D12GraphicsCommandList*,D3D12_CPU_DESCRIPTOR_HANDLE) noexcept;
void before_clear(ID3D12GraphicsCommandList*,D3D12_CPU_DESCRIPTOR_HANDLE,D3D12_CLEAR_FLAGS) noexcept;
void begin_pass(ID3D12GraphicsCommandList*,const D3D12_RENDER_PASS_DEPTH_STENCIL_DESC*,D3D12_RENDER_PASS_FLAGS) noexcept;
void end_pass(ID3D12GraphicsCommandList*) noexcept;
void viewport(ID3D12GraphicsCommandList*,UINT,const D3D12_VIEWPORT*) noexcept;
void signature(ID3D12GraphicsCommandList*,ID3D12RootSignature*) noexcept;
void constants(ID3D12GraphicsCommandList*,UINT,UINT,const void*,UINT) noexcept;
void root_cbv(ID3D12GraphicsCommandList*,UINT,D3D12_GPU_VIRTUAL_ADDRESS) noexcept;
void bind(ID3D12GraphicsCommandList*,D3D12_CPU_DESCRIPTOR_HANDLE) noexcept;
void clear(ID3D12GraphicsCommandList*,D3D12_CPU_DESCRIPTOR_HANDLE,D3D12_CLEAR_FLAGS,float,UINT) noexcept;
void clear_state(ID3D12GraphicsCommandList*) noexcept;
void draw(ID3D12GraphicsCommandList*,UINT vertices=0,UINT instances=1) noexcept;
void barriers(ID3D12GraphicsCommandList*,UINT,const D3D12_RESOURCE_BARRIER*) noexcept;
void reset(ID3D12GraphicsCommandList*) noexcept;
void submitted(ID3D12CommandQueue*,UINT,ID3D12CommandList*const*) noexcept;
void before_submit(ID3D12CommandQueue*,UINT,ID3D12CommandList*const*) noexcept;
bool reserve(const std::shared_ptr<const Image>&) noexcept;
void reader_end(const std::shared_ptr<const Image>&,ID3D12CommandQueue*) noexcept;
Sample snapshot(ID3D12Device*,const Scene&,unsigned,unsigned) noexcept;
std::string status();
void report() noexcept;
#ifdef TV_GPU_TEST
void test_recording_camera(const Camera&) noexcept;
bool test_contracts() noexcept;
bool test_boundaries() noexcept;
#endif
}
