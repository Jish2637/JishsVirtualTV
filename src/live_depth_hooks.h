#pragma once
#include <d3d12.h>
namespace virtualtv::depth::live {
void initialize(ID3D12GraphicsCommandList*) noexcept;
void observe(ID3D12GraphicsCommandList*) noexcept;
void maintain() noexcept;
void shutdown() noexcept;
void report() noexcept;
#ifdef TV_GPU_TEST
unsigned test_ready() noexcept;
#endif
}
