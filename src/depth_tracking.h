#pragma once
#include <d3d12.h>
#include "tv_runtime.h"
namespace virtualtv::depth::tracking {
struct Record {
    const char* event{};void* object{};void* resource{};SIZE_T handle{};
    D3D12_RESOURCE_DESC desc{};UINT before{},after{},subresource{},flags{};float clear{};
    D3D12_DEPTH_STENCIL_VIEW_DESC view{};bool explicit_view{};
};
void record(const Record&) noexcept;
void copy_simple(ID3D12Device*,UINT,D3D12_CPU_DESCRIPTOR_HANDLE,D3D12_CPU_DESCRIPTOR_HANDLE) noexcept;
void copy_ranges(ID3D12Device*,UINT,const D3D12_CPU_DESCRIPTOR_HANDLE*,const UINT*,UINT,const D3D12_CPU_DESCRIPTOR_HANDLE*,const UINT*) noexcept;
void viewport(ID3D12GraphicsCommandList*,UINT,const D3D12_VIEWPORT*) noexcept;
void reset(ID3D12GraphicsCommandList*) noexcept;
void submitted(ID3D12CommandQueue*,UINT,ID3D12CommandList*const*) noexcept;
void frame(const Scene&,unsigned,unsigned) noexcept;
void report() noexcept;
bool lookup(SIZE_T,Record&) noexcept;
#ifdef TV_GPU_TEST
bool test_correlated() noexcept;
bool test_descriptor(SIZE_T,ID3D12Resource*) noexcept;
bool test_catalog_contracts() noexcept;
#endif
}
