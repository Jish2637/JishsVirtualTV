#pragma once

namespace virtualtv::depth::native::reshade {
// The game's projection uses the output/swapchain aspect ratio. Dynamic
// resolution can round the depth dimensions independently (e.g. the game's
// 3440x1440 output renders a 2296x960 depth texture). Using the depth
// texture's aspect introduces camera/depth reprojection misregistration.
// Keep the depth texture dimensions for sampling/UV mapping, not for FOV.
inline double projection_aspect(unsigned output_width,
                                unsigned output_height,
                                double depth_aspect_fallback) noexcept {
    return (output_width && output_height)
        ? static_cast<double>(output_width) / static_cast<double>(output_height)
        : depth_aspect_fallback;
}
}
