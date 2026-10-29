#pragma once
#include <d3d12.h>
#include "tv_runtime.h"
namespace virtualtv::depth::probe {
// Suppress projection candidates from the TV's own shader constants.
struct TvScope {bool previous;TvScope() noexcept;~TvScope();};
void initialize(ID3D12GraphicsCommandList*) noexcept;
void active(bool) noexcept;
void list(ID3D12GraphicsCommandList*,const char*,void*) noexcept;
void frame(const Scene&) noexcept;
void report() noexcept;
bool is_tv() noexcept;
void live_constants(ID3D12GraphicsCommandList*,UINT,UINT,const void*,UINT) noexcept;
void live_cbv(ID3D12GraphicsCommandList*,UINT,D3D12_GPU_VIRTUAL_ADDRESS) noexcept;
void live_table(ID3D12GraphicsCommandList*,UINT,D3D12_GPU_DESCRIPTOR_HANDLE) noexcept;
#ifdef TV_GPU_TEST
bool test_contracts() noexcept;
bool test_hooks() noexcept;
#endif
}
