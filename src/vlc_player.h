#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <mutex>
#include <cstdint>
#include "tv_audio.h"
#include "video_frame.h"
namespace virtualtv {struct ResolvedMedia;}
class VlcPlayer {
public:
    using Frame=VideoFrame;
    bool initialize(std::wstring& error);
    bool play(const std::string& url, HWND video_surface, std::wstring& error);
    bool play_resolved(const virtualtv::ResolvedMedia&,std::wstring& error);
    int playback_state() const;
    int audio_track_count() const;
    bool copy_frame(Frame& output);
    std::shared_ptr<const Frame> wait_frame(std::uint64_t after,unsigned timeout_ms){return frames_.wait(after,timeout_ms);}
    bool set_paused(bool paused);
    std::int64_t playback_time_ms() const;
    std::uint64_t seek_revision()const{return seek_revision_.load();}
    bool seekable() const;
    bool seek_to_ms(std::int64_t milliseconds);
    void stop();
    void volume(int amount);
    void audio_target(std::array<float,4>);
    bool audio_active()const{return audio_control_.active();}
    bool audio_controlled()const{return audio_control_.controlled();}
    float audio_peak()const{return audio_control_.peak();}
    std::array<float,2> audio_gains()const{return audio_control_.gains();}
    std::string audio_diagnostics()const{return audio_control_.diagnostics();}
    void shutdown();
    ~VlcPlayer() { shutdown(); }
private:
    std::atomic<std::uint64_t> seek_revision_{};
    struct API;
    API* api_{};
    virtualtv::VlcAudioControl audio_control_;
    int volume_{75},applied_volume_{-1};
    std::array<float,4> audio_matrix_{1,0,0,1};
    // A new VLC media player audio output can appear after libvlc_media_player_play()
    // succeeds. Reapply the requested level as it opens (and after resume), rather
    // than leaving the new output at the old stream's muted level.
    ULONGLONG next_volume_retry_{}, volume_retry_until_{};
    int last_volume_playback_state_{-1};
    bool force_volume_refresh_{};

    static void* __cdecl lock_callback(void*,void**);
    static void __cdecl unlock_callback(void*,void*,void* const*);
    static void __cdecl display_callback(void*,void*);
    std::mutex frame_mutex_;
    VideoFrameMailbox frames_;
    std::vector<std::uint8_t> decode_buffer_;
    unsigned width_{1280},height_{720};
};
