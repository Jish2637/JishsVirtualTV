#pragma once
#include <cstdint>
namespace virtualtv {
// Each placed object may be used for one video screen OR one speaker, never both.
// One decoded VLC stream feeds all outputs; the primary is included in the screen limit.
inline constexpr unsigned max_world_screens=64;
inline constexpr unsigned max_world_speakers=64;
// The native entity handle can change when the editor recreates a placed object.
// Session + placement instance + drop identify the physical object across that change.
template<typename IdentityT>
constexpr bool same_placed_object(const IdentityT& a,const IdentityT& b) noexcept {
    return a.instance!=0 && b.instance!=0 && a.session==b.session &&
           a.instance==b.instance && a.drop==b.drop;
}
// Count checks include reservations so repeated clicks cannot bypass limits.
constexpr bool can_add_output(unsigned active,unsigned pending,unsigned maximum) noexcept {
    return active<maximum && pending<maximum-active;
}
}
