#pragma once
#include <cmath>
namespace virtualtv::presentation {
// The depth projection is frozen with its depth image. A newer final game
// camera can have a slightly different focal length. Correct only the TV
// geometry's X/Y clip terms; the depth shader continues to use the capture
// matrix. This preserves the world vertex positions exactly.
struct FocalOverride {float x{}, y{}; bool valid{};};
inline FocalOverride final_focal(float x, float y, float capture_x, float capture_y) noexcept {
    if (!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(capture_x)||!std::isfinite(capture_y)||
        x<=0.f||y<=0.f||capture_x<=0.f||capture_y<=0.f) return {};
    // Large differences suggest a cutscene/different viewport: don't couple
    // unrelated projection matrices to this world-locked TV.
    if (std::abs(x/capture_x-1.f)>0.08f || std::abs(y/capture_y-1.f)>0.08f) return {};
    return {x,y,true};
}
}
