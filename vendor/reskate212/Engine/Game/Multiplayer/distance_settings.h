#pragma once

namespace dingosdk {
struct MultiplayerDistances {
    int full_rate_return = 50;
    int half_rate_start = 60;
    int half_rate_return = 150;
    int low_rate_start = 170;
    bool operator==(const MultiplayerDistances &) const = default;
    bool valid() const noexcept {
        return full_rate_return >= 0 && full_rate_return < half_rate_start &&
               half_rate_start <= half_rate_return && half_rate_return < low_rate_start &&
               low_rate_start <= 10000;
    }
};
} // namespace dingosdk
