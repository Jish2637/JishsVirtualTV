#pragma once
#include <array>
#include <cstdint>

// Native camera, game UI and noclip functions used by the debug controls and party spectate.
namespace dingosdk::game::build::v20260929::client_source_spawn {
// ClientSkaterSource creator (source). Creates source+0x68 from data+0xd0. Reference only
// (ai_skaters mirrors it).
inline constexpr std::uintptr_t source_create = 0x51fef0;
// uint32 offset of the owning client in a game context.
inline constexpr std::uintptr_t context_client_offset = 0x6fc25f0;

// Free camera.
// uint32 native camera local id.
inline constexpr std::uintptr_t camera_local_id = 0x73c7ab8;
inline constexpr std::uintptr_t camera_controller_vtable = 0x63efab8;
// Camera controller vtable slot +0xb8.
inline constexpr std::uintptr_t camera_mode_switch = 0x2f2c360;
inline constexpr std::array<unsigned char, 24> camera_mode_prefix{
    0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x8b,0xda,0x48,0x8b,0xf9,0x39,
    0x51,0x08,0x74,0x14,0x83,0xfa,0x01,0x77};
inline constexpr std::uintptr_t free_camera_vtable = 0x63fc220;
// FreeCamera vtable slot +0xd0.
inline constexpr std::uintptr_t camera_transform_setter = 0x2f8fa90;
inline constexpr std::array<unsigned char, 24> camera_transform_prefix{
    0xc5,0xfc,0x10,0x02,0xc5,0xfc,0x11,0x81,0x80,0x01,0x00,0x00,0xc5,0xfc,0x10,0x4a,
    0x20,0xc5,0xfc,0x11,0x89,0xa0,0x01,0x00};
// Initializes the camera's degree FOV at +0xac. Reference only.
inline constexpr std::uintptr_t camera_fov_init = 0x2f8bb70;
// FreeCamera input helpers at +0x1d0 and +0x1d8.
inline constexpr std::array<std::uintptr_t, 2> free_camera_helper_vtables{0x63fc1e0, 0x63fc200};
// Pointer to the camera view manager.
inline constexpr std::uintptr_t view_manager = 0x75cbc48;
// Pointer: the registered camera view manager.
inline constexpr std::uintptr_t view_manager_registration = 0x75dd288;
inline constexpr std::uintptr_t view_manager_vtable = 0x63ece68;
// view_manager_vtable slots 0, +8, +0x28 and +0x98.
inline constexpr std::array<std::uintptr_t, 4> view_manager_methods{0x2f11470, 0x2f0f700, 0x2f0d7c0, 0x2f0d0b0};
inline constexpr std::uintptr_t camera_view_vtable = 0x63ed0a0;

// Debug UI and noclip.
// Pointer to the game UI settings; draw flag at +0x4e.
inline constexpr std::uintptr_t ui_settings = 0x7761aa0;
inline constexpr std::uintptr_t ui_settings_vtable = 0x65ae378;
inline constexpr std::uintptr_t ui_settings_type = 0x77674d0;
inline constexpr std::uintptr_t board_physics_vtable = 0x65e67a0;
inline constexpr std::uintptr_t rig_physics_vtable = 0x65dabc0;
// Skater physics core update; no_bail::bail_core_vtable slot +0x58. Hooked for noclip.
inline constexpr std::uintptr_t physics_update = 0x47ddca0;
inline constexpr std::array<unsigned char, 32> physics_update_prefix{
    0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x48,0x8b,0x89,0xb0,0x03,0x00,0x00,
    0x48,0x8b,0x01,0xff,0x50,0x18,0x48,0x8b,0x8b,0xf8,0x03,0x00,0x00,0xe8,0xfe,0xd3};
// Skater motion target update. Hooked for noclip.
inline constexpr std::uintptr_t skater_motion = 0x4776a80;
inline constexpr std::array<unsigned char, 32> skater_motion_prefix{
    0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x50,0x49,
    0x8d,0x70,0x10,0x48,0x8b,0xd9,0x49,0x8d,0x78,0x20,0xc4,0xc1,0x7c,0x10,0x00,0xc5};
}

