#pragma once
// Transport-independent VirtualTV sync protocol. No access to ReSkate networking is assumed.
#include <cstdint>
#include <string>
#include <string_view>
#include <optional>
namespace virtualtv::sync {
enum class Kind : std::uint8_t { offer=1, ready=2, start=3, state=4, pause=5, stop=6, ping=7, pong=8, prepare=9, capability=10, catalog_request=11, catalog=12, setup_snapshot=13, setup_delta=14, resync=15, full_offer=16, timeline_request=17, timeline_anchor=18 };
struct Packet {
    Kind kind{};
    std::uint64_t room{};      // ephemeral identifier, changes on map/session transition
    std::uint64_t revision{};  // each new media/placement offer increments
    std::uint64_t sender{};    // MUST be checked against relay-authenticated sender
    std::uint64_t timing_us{}; // monotonic controller clock for start/state/ping/pong
    std::uint64_t position_ms{};
    std::string payload;      // offer: UTF-8 media/transform payload, app-defined
};
inline constexpr std::size_t max_payload=1455;
inline void u64(std::string& out, std::uint64_t v){for(int i=0;i<8;++i)out.push_back(static_cast<char>(v>>(8*i)));}
inline std::uint64_t read64(std::string_view b,std::size_t at){std::uint64_t v=0;for(int i=0;i<8;++i)v|=std::uint64_t(static_cast<unsigned char>(b[at+i]))<<(8*i);return v;}
inline std::optional<std::string> encode(const Packet& p){
    if(p.payload.size()>max_payload||p.kind<Kind::offer||p.kind>Kind::timeline_anchor)return std::nullopt;
    std::string out="VTS1";out.push_back(static_cast<char>(p.kind));
    u64(out,p.room);u64(out,p.revision);u64(out,p.sender);u64(out,p.timing_us);u64(out,p.position_ms);
    out+=p.payload;return out;
}
inline std::optional<Packet> decode(std::string_view bytes){
    if(bytes.size()<45||bytes.size()>45+max_payload||bytes.substr(0,4)!="VTS1")return std::nullopt;
    const auto kind=static_cast<Kind>(static_cast<unsigned char>(bytes[4]));
    if(kind<Kind::offer||kind>Kind::timeline_anchor)return std::nullopt;
    return Packet{kind,read64(bytes,5),read64(bytes,13),read64(bytes,21),read64(bytes,29),read64(bytes,37),std::string(bytes.substr(45))};
}
// Strict session, revision and authenticated-origin checks are required before acting on a packet.
inline bool acceptable(const Packet& p,std::uint64_t relay_sender,std::uint64_t room,std::uint64_t revision,
                       std::uint64_t controller){
    if(!room||p.room!=room||p.sender!=relay_sender||p.revision!=revision)return false;
    if(p.kind==Kind::ready||p.kind==Kind::ping||p.kind==Kind::pong)return true;
    return controller!=0&&p.sender==controller;
}
struct OffsetSample {std::int64_t offset_us{};std::uint64_t rtt_us{};};
// NTP-style midpoint estimate from t0 client send, t1 controller receive,
// t2 controller send, t3 client receive. All monotonic microseconds.
inline std::optional<OffsetSample> estimate_offset(std::uint64_t t0,std::uint64_t t1,
                                                   std::uint64_t t2,std::uint64_t t3){
    if(t3<t0||t2<t1||t3-t0<t2-t1)return std::nullopt;
    const auto rtt=(t3-t0)-(t2-t1);
    const auto offset=(static_cast<std::int64_t>(t1)-static_cast<std::int64_t>(t0)+
                       static_cast<std::int64_t>(t2)-static_cast<std::int64_t>(t3))/2;
    return OffsetSample{offset,rtt};
}
inline std::int64_t desired_position_ms(std::uint64_t controller_now_us,std::uint64_t start_us,
                                        std::uint64_t start_position_ms){
    if(controller_now_us<=start_us)return static_cast<std::int64_t>(start_position_ms);
    return static_cast<std::int64_t>(start_position_ms+(controller_now_us-start_us)/1000);
}
}
