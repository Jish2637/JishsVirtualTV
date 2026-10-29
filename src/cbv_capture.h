#pragma once
#include "depth_capture.h"
#include <vector>
namespace virtualtv::depth::cbv {
struct Snapshot {
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    UINT64 offset{};
    std::array<float,16> raw{};
};
struct Candidate {std::array<float,16> matrix{};Snapshot snapshot;};
void initialize(ID3D12Device*) noexcept;
std::vector<Candidate> read(ID3D12Device*,D3D12_GPU_VIRTUAL_ADDRESS,ID3D12RootSignature* =nullptr,UINT index=0,bool allow_scan=true,bool* scanned=nullptr) noexcept;
bool unchanged(const Snapshot&) noexcept;
std::string status();
}
