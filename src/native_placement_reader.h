#pragma once
#include <array>
#include <cstdint>
#include <cstring>

namespace virtualtv::native_placement_reader {
// ReSkate's client-side native placed-object store consists of 0x50-byte rows.
// The reader only copies and validates them; it neither creates hooks nor edits
// the game's object state. All offsets come from the pinned 20260929 build.
inline constexpr std::size_t record_bytes=0x50;
inline constexpr std::size_t maximum_records=65536;
struct Record {
    std::uint64_t entity{};
    std::uint32_t group{};
    std::uint8_t active{};
};
inline bool valid_span(std::uintptr_t begin,std::uintptr_t end) noexcept {
    return begin>=0x10000 && end>=begin &&
        (end-begin)%record_bytes==0 &&
        (end-begin)/record_bytes<=maximum_records;
}
inline Record decode(const std::array<unsigned char,record_bytes>& bytes) noexcept {
    Record result{};
    std::memcpy(&result.entity,bytes.data()+0x10,sizeof result.entity);
    std::memcpy(&result.group,bytes.data()+0x28,sizeof result.group);
    result.active=bytes[0x3a];
    return result;
}
inline bool same_object(Record before,Record after) noexcept {
    return before.entity && before.entity==after.entity &&
           before.group==after.group && before.active==after.active &&
           before.active!=0x7f;
}
}
