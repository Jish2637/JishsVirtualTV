#pragma once
#include <array>
#include <cstdint>

namespace dingosdk::game::build::v20260929::camera_observer {
// Camera callback observed by the camera diagnostics.
inline constexpr std::uintptr_t camera_callback = 0x178b690;
inline constexpr std::array<unsigned char, 24> camera_callback_prefix{
    0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x59,0x18,0x48,0x8b,
    0x41,0x20,0xc5,0xfa,0x10,0x02,0x48,0x89,0x53,0x60,0x48,0x8d};
// Return address of the verified native caller of the camera callback.
inline constexpr std::uintptr_t native_caller_return = 0x1728bd5;
// Camera callback object.
inline constexpr std::uintptr_t callback_vtable = 0x625b768;
// The two native camera entry variants. 14178fb00 and 14178fba0 create them
// through the same 1417224a0 base constructor; the live stuck-camera capture
// selected the alternate one after a camera/editor transition.
inline constexpr std::uintptr_t entry_vtable = 0x625c3d8;
inline constexpr std::uintptr_t alternate_entry_vtable = 0x625c270;
// Callback helper.
inline constexpr std::uintptr_t helper_vtable = 0x625b7b0;
}

