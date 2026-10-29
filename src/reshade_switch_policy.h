#pragma once
#include <cstdint>

// Platform-independent decision logic shared by native DX12 selection and tests.
// Switching is deliberately conservative: sustained scene evidence alone only
// arms a challenger. GPU completion is verified separately before promotion.
namespace virtualtv::depth::native::selection {
struct ChallengerTracker {
    const void* candidate{};
    unsigned consecutive{};
    static constexpr unsigned required_frames=6;

    void reset() noexcept { candidate=nullptr; consecutive=0; }
    void observe(const void* best, const void* current,
                 std::uint64_t best_draws, std::uint64_t current_draws) noexcept {
        // A single noisy shadow/prepass should never dislodge a working DSV.
        if(!best || !current || best==current || best_draws<8 ||
           (current_draws>=8 && best_draws<=current_draws+current_draws/2)) {
            reset(); return;
        }
        if(candidate!=best){candidate=best;consecutive=1;}
        else if(consecutive<required_frames)++consecutive;
    }
    bool ready() const noexcept { return candidate && consecutive>=required_frames; }
};
}
