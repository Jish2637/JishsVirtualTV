#pragma once

namespace dingosdk {
// ReSkate menu scale. The menu chrome is laid out in pixels, so one factor
// scales both the fonts and that layout. Kept to a range that stays legible on
// a small window and still fits the page content on a large one.
inline constexpr float min_menu_scale = 0.75f, max_menu_scale = 2.0f, default_menu_scale = 1.0f;
} // namespace dingosdk
