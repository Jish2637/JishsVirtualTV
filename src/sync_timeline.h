#pragma once
#include "tv_sync.h"
#include <algorithm>
#include <cmath>
namespace virtualtv::sync {
enum class Playback : unsigned char { Buffering, Playing, Paused };
struct Anchor {std::uint64_t sequence{},host_us{},position_ms{};Playback state{};};
inline std::string encode_anchor(Anchor a){std::string out="TLA1";u64(out,a.sequence);u64(out,a.host_us);u64(out,a.position_ms);out.push_back(char(a.state));return out;}
inline std::optional<Anchor> decode_anchor(std::string_view data){if(data.size()!=29||data.substr(0,4)!="TLA1"||static_cast<unsigned char>(data[28])>2)return {};Anchor a{read64(data,4),read64(data,12),read64(data,20),Playback((unsigned char)data[28])};if(!a.sequence||!a.host_us||a.position_ms>864000000ull)return {};return a;}
// Observes host state; it has no pause/seek/play API and never waits for viewers.
struct HostTimeline {
    std::uint64_t time_us{},seek_revision{};std::int64_t position{};int state=-2;
    bool observe(std::uint64_t now,std::int64_t next_position,int next_state,std::uint64_t seek){
        const bool jump=next_state==3&&state==3&&time_us&&next_position>=0&&
            std::abs(double(next_position-position)-double(now-time_us)/1000)>1250;
        const bool changed=state!=next_state||seek!=seek_revision||jump;
        time_us=now;position=next_position;state=next_state;seek_revision=seek;return changed;
    }
};
// Viewer-only state machine. There is intentionally no host player interface.
class JoinTimeline {
public:
    enum class Phase {Idle,Request,AwaitAnchor,Seek,AwaitStart,Joined,Failed};
    Phase phase{Phase::Request};unsigned attempts{};std::uint64_t sequence{},deadline_us{},start_us{},target_ms{},rtt_us{};bool paused_host{};const char* failure="";
    void reset(){*this=JoinTimeline{};}
    void retry(){phase=Phase::Request;attempts=0;failure="";}
    bool request_sent(std::uint64_t now){if(attempts>=3){fail("Sync preparation failed after three attempts. Use Retry sync.");return false;}++attempts;phase=Phase::AwaitAnchor;deadline_us=now+5000000;return true;}
    void fail(const char* reason){failure=reason;phase=Phase::Failed;}
    void timeout(std::uint64_t now){if((phase==Phase::AwaitAnchor||phase==Phase::Seek)&&now>=deadline_us)phase=Phase::Request;}
    bool accept(Anchor a,std::uint64_t host_now){
        if(a.sequence<=sequence||phase==Phase::Failed)return false;sequence=a.sequence;
        if(a.state==Playback::Buffering){if(attempts)--attempts;phase=Phase::AwaitAnchor;deadline_us=host_now+5000000;return true;}
        if(phase==Phase::Joined)attempts=1;else if(!attempts)attempts=1;
        paused_host=a.state==Playback::Paused;
        const auto lead=std::clamp<std::uint64_t>(2*rtt_us+250000,750000,3000000);
        start_us=paused_host?host_now+3000000:host_now+lead;
        if(a.host_us>host_now+250000||host_now>a.host_us+5000000){phase=Phase::Request;return false;}
        target_ms=a.position_ms+(paused_host?0:(start_us-a.host_us)/1000);
        deadline_us=start_us;phase=Phase::Seek;return true;
    }
    bool readback(std::int64_t position,int vlc_state,std::uint64_t host_now){
        if(phase!=Phase::Seek&&phase!=Phase::AwaitStart)return false;
        const bool ready=position>=0&&std::abs(double(position)-double(target_ms))<=200&&vlc_state==4;
        if(paused_host&&ready){phase=Phase::Joined;return false;}
        if(phase==Phase::Seek){if(ready&&host_now<start_us)phase=Phase::AwaitStart;else if(host_now>=start_us){phase=Phase::Request;return false;}}
        if(phase==Phase::AwaitStart&&host_now>=start_us){if(host_now-start_us>150000||!ready){phase=Phase::Request;return false;}phase=Phase::Joined;return true;}
        return false;
    }
    bool mute()const{return phase!=Phase::Joined;}
};
}
