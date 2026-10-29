#pragma once
#include <array>
#include <cstdint>
#include <vector>
namespace virtualtv {
struct Vec3 { float x{},y{},z{}; };
struct Quat { float x{},y{},z{},w{1}; };
struct Placement { std::uint64_t saved_id{}; Vec3 position; Quat rotation; float scale{1}; };
struct Screen {
    Vec3 offset{};
    Quat rotation{};
    float width{1.4f},height{0.8f};
    // Signed total bend angle across each axis. Zero keeps a truly flat quad.
    // Positive bends the outer edges in local +Z; negative in local -Z.
    float horizontal_curve_deg{},vertical_curve_deg{};
};
struct ScreenMeshVertex { Vec3 position; float u{},v{}; };
inline constexpr unsigned max_curve_segments_x=24,max_curve_segments_y=16;
inline constexpr unsigned max_curve_vertices_per_screen=6*max_curve_segments_x*max_curve_segments_y;
// Append local, rotated/offset geometry; object placement/scale are applied by renderer.
void append_screen_mesh(const Screen&,std::vector<ScreenMeshVertex>&);
struct Corners { std::array<Vec3,4> positions; };
// Row-independent right-handed quaternion transformation. Coordinates are inherited from ReSkate.
Vec3 rotate(Quat q,Vec3 v);
Corners corners(const Placement& object,const Screen& screen);
bool valid(const Placement& object,const Screen& screen);
}
