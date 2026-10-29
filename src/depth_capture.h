#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <memory>
#include <string>
#include "tv_runtime.h"
#include "../vendor/streamline211/sl_core_types.h"
namespace virtualtv::depth {
enum class Backend { Auto, StreamlineOnly, NativeOnly, NativeTagged, ReShadeNative };
void set_backend(Backend);
bool automatic_fallback_active() noexcept;
// Choose the initial backend from the actual DXGI device vendor, once per process.
void configure_backend_for_adapter(unsigned vendor_id);
Backend backend() noexcept;
const char* backend_name(Backend) noexcept;
struct Image {
    Microsoft::WRL::ComPtr<ID3D12Resource> texture,source;
    Microsoft::WRL::ComPtr<ID3D12Fence> ready;
    UINT64 value{};
    DXGI_FORMAT view_format=DXGI_FORMAT_UNKNOWN;
    D3D12_RESOURCE_STATES source_state{};
    sl::Extent extent;
    unsigned frame{},viewport{},epoch{};
    ULONGLONG at{};
    bool direct{},poisoned{};
};
struct Sample {std::shared_ptr<const Image> image;sl::Constants camera;
    // Native ReShade only: the immutable camera pose recorded together with
    // the depth copy. Do not substitute the current TV presentation pose.
    virtualtv::Camera capture_pose;bool capture_pose_valid{};
};
// Optional, independent transaction: a depth failure must not disable playback.
void initialize(ID3D12CommandQueue* discovery_queue,ID3D12GraphicsCommandList* discovery_list) noexcept;
void initialize_lifetime(ID3D12CommandQueue*,ID3D12GraphicsCommandList*) noexcept;
void activate(bool);
void invalidate();
void end_present();
std::string status();
Sample snapshot(ID3D12Device*,const Scene&,unsigned width,unsigned height);
DXGI_FORMAT sample_format(DXGI_FORMAT);
DXGI_FORMAT copy_format(DXGI_FORMAT);
bool valid_camera(const sl::Constants&);
bool matches_camera(const sl::Constants&,const Scene&);
bool world_camera(const sl::Constants&,const Camera&,std::array<float,16>&);
bool latest_world_camera(const Scene&,std::array<float,16>&,sl::float4x4&);
#ifdef TV_GPU_TEST
void test_constants(const sl::Constants&,unsigned frame,unsigned viewport);
void test_tag(const sl::ResourceTag&,unsigned frame,unsigned viewport,ID3D12GraphicsCommandList*);
void test_submitted(ID3D12CommandQueue*,UINT,ID3D12CommandList*const*);
#endif
}
