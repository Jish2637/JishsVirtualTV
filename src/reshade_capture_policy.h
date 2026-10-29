#pragma once
#include <cstdint>
// Independent, pure selection policy for the ReShade Any-eligible path.
namespace virtualtv::depth::native::capture_policy {
constexpr std::uint64_t unbind_hold_ms=40;
constexpr bool keep_completed_pass(bool reshade, int capture_boundary,
                                  unsigned previous_stage, unsigned incoming_stage,
                                  std::uint64_t previous_at, std::uint64_t incoming_at) noexcept {
    return reshade && capture_boundary==0 && previous_stage==2 && incoming_stage==1 &&
           incoming_at>=previous_at && incoming_at-previous_at<=unbind_hold_ms;
}
}
