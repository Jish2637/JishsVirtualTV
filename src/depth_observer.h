#pragma once
#include <d3d12.h>
namespace virtualtv::depth::observer {
void initialize(ID3D12CommandQueue*,ID3D12GraphicsCommandList*) noexcept;
void activate(bool) noexcept;
void report() noexcept;
#ifdef TV_GPU_TEST
bool test_observed() noexcept;
#endif
}
