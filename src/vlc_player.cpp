#include "vlc_player.h"
#include "url_resolver.h"
#include <filesystem>
#include <cstdlib>
#include <array>
#include <vector>
#include <memory>
#include <system_error>
#include <algorithm>
#include <cmath>
namespace {
using namespace std::filesystem;
path find_vlc() {
    if (const wchar_t* custom = _wgetenv(L"VLC_HOME")) {
        path home(custom);
        if (exists(home / L"libvlc.dll")) return home;
    }
    std::array<wchar_t, 32768> buffer{};
    for (const wchar_t* variable : {L"ProgramFiles", L"ProgramFiles(x86)"}) {
        DWORD n = GetEnvironmentVariableW(variable, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (n && n < buffer.size()) {
            path home = path(buffer.data()) / L"VideoLAN" / L"VLC";
            if (exists(home / L"libvlc.dll")) return home;
        }
    }
    return {};
}
template<class T> bool symbol(HMODULE m, const char* name, T& dest) {
    dest = reinterpret_cast<T>(GetProcAddress(m, name));
    return dest != nullptr;
}
}
struct VlcPlayer::API {
    HMODULE dll{};
    void* instance{};
    void* player{};
    using New = void* (__cdecl*)(int, const char* const*);
    using Release = void(__cdecl*)(void*);
    using MediaNew = void* (__cdecl*)(void*, const char*);
    using MediaRelease = void(__cdecl*)(void*);
    using MediaOption = void(__cdecl*)(void*,const char*);
    using MediaSlave = int(__cdecl*)(void*,int,unsigned,const char*);
    using PlayerInt = int(__cdecl*)(void*);
    using PlayerNew = void* (__cdecl*)(void*);
    using PlayerRelease = void(__cdecl*)(void*);
    using PlayerSetMedia = void(__cdecl*)(void*, void*);
    using PlayerSetHwnd = void(__cdecl*)(void*, void*);
    using VideoCallbacks = void(__cdecl*)(void*,void*(__cdecl*)(void*,void**),void(__cdecl*)(void*,void*,void* const*),void(__cdecl*)(void*,void*),void*);
    using VideoFormat = void(__cdecl*)(void*,const char*,unsigned,unsigned,unsigned);
    using PlayerPlay = int(__cdecl*)(void*);
    using PlayerStop = void(__cdecl*)(void*);
    using PlayerSetPause = void(__cdecl*)(void*, int);
    using PlayerGetTime = std::int64_t(__cdecl*)(void*);
    using PlayerSetTime = void(__cdecl*)(void*,std::int64_t);
    using AudioVolume = int(__cdecl*)(void*, int);
    New create{}; Release release{}; MediaNew media_create{}; MediaRelease media_release{};
    MediaOption media_option{};MediaSlave media_slave{};PlayerInt state{},audio_count{},is_seekable{};
    PlayerNew player_create{}; PlayerRelease player_release{}; PlayerSetMedia set_media{};
    PlayerSetHwnd set_hwnd{}; VideoCallbacks callbacks{}; VideoFormat format{}; PlayerPlay start{}; PlayerStop stop{}; PlayerSetPause set_pause{}; PlayerGetTime get_time{}; PlayerSetTime set_time{}; AudioVolume volume{};
};
bool VlcPlayer::initialize(std::wstring& error) {
    if (api_) return true;
    auto dir = find_vlc();
    if (dir.empty()) { error = L"VLC not found. Install 64-bit VLC or set VLC_HOME to its installation directory."; return false; }
    auto tmp = std::make_unique<API>();
    const auto lib = (dir / L"libvlc.dll").wstring();
    tmp->dll = LoadLibraryExW(lib.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!tmp->dll) { error = L"Cannot load 64-bit libvlc.dll (Win32 error " + std::to_wstring(GetLastError()) + L")."; return false; }
    const bool ok = symbol(tmp->dll,"libvlc_new",tmp->create) &&
        symbol(tmp->dll,"libvlc_release",tmp->release) &&
        symbol(tmp->dll,"libvlc_media_new_location",tmp->media_create) &&
        symbol(tmp->dll,"libvlc_media_release",tmp->media_release) &&
        symbol(tmp->dll,"libvlc_media_player_new",tmp->player_create) &&
        symbol(tmp->dll,"libvlc_media_player_release",tmp->player_release) &&
        symbol(tmp->dll,"libvlc_media_player_set_media",tmp->set_media) &&
        symbol(tmp->dll,"libvlc_video_set_callbacks",tmp->callbacks) &&
        symbol(tmp->dll,"libvlc_video_set_format",tmp->format) &&
        symbol(tmp->dll,"libvlc_media_player_play",tmp->start) &&
        symbol(tmp->dll,"libvlc_media_player_stop",tmp->stop) &&
        symbol(tmp->dll,"libvlc_media_player_set_pause",tmp->set_pause) &&
        symbol(tmp->dll,"libvlc_audio_set_volume",tmp->volume);
    if (!ok) { FreeLibrary(tmp->dll); error=L"The installed VLC is missing a required libVLC API."; return false; }
    symbol(tmp->dll,"libvlc_media_add_option",tmp->media_option);
    symbol(tmp->dll,"libvlc_media_slaves_add",tmp->media_slave);
    symbol(tmp->dll,"libvlc_media_player_get_state",tmp->state);
    symbol(tmp->dll,"libvlc_media_player_get_time",tmp->get_time);
    symbol(tmp->dll,"libvlc_media_player_is_seekable",tmp->is_seekable);
    symbol(tmp->dll,"libvlc_media_player_set_time",tmp->set_time);
    symbol(tmp->dll,"libvlc_audio_get_track_count",tmp->audio_count);
    const auto plugin_dir = (dir / L"plugins").wstring();
    SetEnvironmentVariableW(L"VLC_PLUGIN_PATH", plugin_dir.c_str());
    const char* args[] = {"--no-video-title-show", "--quiet"};
    tmp->instance = tmp->create(2,args);
    if (!tmp->instance) { FreeLibrary(tmp->dll); error=L"libVLC could not initialize."; return false; }
    tmp->player = tmp->player_create(tmp->instance);
    if (!tmp->player) { tmp->release(tmp->instance); FreeLibrary(tmp->dll); error=L"libVLC could not create a player."; return false; }
    api_ = tmp.release();
    audio_control_.initialize();
    volume(volume_);
    // libVLC requires 32-byte aligned planes; std::vector only guarantees byte

    // alignment. Keep padding for the aligned base and SIMD-aligned scan lines.

    decode_buffer_.resize(static_cast<std::size_t>(width_)*((height_+31u)&~31u)*4+31);
    api_->callbacks(api_->player,&VlcPlayer::lock_callback,&VlcPlayer::unlock_callback,&VlcPlayer::display_callback,this);
    api_->format(api_->player,"RV32",width_,height_,width_*4);
    return true;
}
bool VlcPlayer::play(const std::string& url, HWND surface, std::wstring& error) {
    (void)surface;
    virtualtv::ResolvedMedia media;media.video=url;return play_resolved(media,error);
}
bool VlcPlayer::play_resolved(const virtualtv::ResolvedMedia& source,std::wstring& error) {
    if (!api_ && !initialize(error)) return false;
    if (source.video.empty()) { error=L"Enter a video URL."; return false; }
    if((!source.user_agent.empty()||!source.referer.empty())&&!api_->media_option){error=L"VLC cannot apply the resolved HTTP headers.";return false;}
    if(!source.audio.empty()&&!api_->media_slave){error=L"VLC 3 or newer is required for separate video/audio streams.";return false;}
    void* media = api_->media_create(api_->instance, source.video.c_str());
    if (!media) { error=L"libVLC rejected this media address."; return false; }
    if(!source.user_agent.empty())api_->media_option(media,(":http-user-agent="+source.user_agent).c_str());
    if(!source.referer.empty())api_->media_option(media,(":http-referrer="+source.referer).c_str());
    if(!source.audio.empty()&&api_->media_slave(media,1,4,source.audio.c_str())!=0){api_->media_release(media);error=L"VLC could not attach the resolved audio stream.";return false;}
    stop();
    {std::lock_guard lock(frame_mutex_);decode_buffer_.resize(static_cast<std::size_t>(width_)*((height_+31u)&~31u)*4+31);}
    api_->set_media(api_->player,media);
    api_->media_release(media);
    // Never prime a new track at volume 0. libVLC often creates/replaces its
    // actual WASAPI/MMDevice output *after* play returns, so a zero written here
    // can remain in effect until the user touches the volume slider.
    // The audio worker will apply the current spatial gains separately.
    audio_control_.new_media();
    if (api_->start(api_->player) != 0) { error=L"libVLC could not start the stream."; return false; }
    applied_volume_=-1;
    last_volume_playback_state_=-1;
    const auto now=GetTickCount64();
    next_volume_retry_=now;
    volume_retry_until_=now+5000;
    force_volume_refresh_=true;
    audio_control_.playing(true);
    audio_target(audio_matrix_);
    return true;
}
int VlcPlayer::playback_state() const{return api_&&api_->state?api_->state(api_->player):-1;}
int VlcPlayer::audio_track_count() const{return api_&&api_->audio_count?api_->audio_count(api_->player):-1;}
void VlcPlayer::volume(int v) {
    volume_=std::clamp(v,0,100);
    // Also make setting the same number an explicit request to reapply it.
    force_volume_refresh_=true;
    next_volume_retry_=0;
    if(api_)audio_target(audio_matrix_);
}
void VlcPlayer::audio_target(std::array<float,4> matrix){
    audio_matrix_=matrix;
    audio_control_.target(matrix);
    // Keep ordinary VLC playback even when the Windows per-channel session
    // hasn't been created yet. If it is unavailable, use scalar attenuation.
    const float gain=std::max(matrix[0]+matrix[1],matrix[2]+matrix[3]);
    const bool ready=audio_control_.controlled();
    const int desired=ready?volume_:static_cast<int>(std::lround(volume_*std::clamp(std::isfinite(gain)?gain:0.f,0.f,1.f)));
    if(!api_)return;
    const auto now=GetTickCount64();
    // libVLC creates the audio output asynchronously and can replace it on
    // playlist transitions. Track the Playing transition and retry for a short
    // period after each start. Do not assume a set_volume call made during
    // Opening/Buffering persists into the eventual MMDevice output.
    if(api_->state){
        const int state=api_->state(api_->player);
        if(state==3&&last_volume_playback_state_!=3){
            force_volume_refresh_=true;
            volume_retry_until_=now+1500;
            next_volume_retry_=now;
        }
        last_volume_playback_state_=state;
    }
    const bool startup_retry=now<volume_retry_until_&&now>=next_volume_retry_;
    const bool changed=desired!=applied_volume_;
    if((changed&&!force_volume_refresh_) ||
       (now>=next_volume_retry_&&(force_volume_refresh_||startup_retry))){
        // A failed call is not a successfully applied level. Retry it rather
        // than caching the wrong value and requiring a slider change.
        if(api_->volume(api_->player,desired)==0){
            applied_volume_=desired;
            force_volume_refresh_=false;
        }else{
            applied_volume_=-1;
            force_volume_refresh_=true;
        }
        next_volume_retry_=now+250;
    }
}
std::int64_t VlcPlayer::playback_time_ms() const {return api_&&api_->get_time?api_->get_time(api_->player):-1;}
bool VlcPlayer::seekable() const {return api_&&api_->is_seekable&&api_->get_time&&api_->set_time&&api_->is_seekable(api_->player)!=0;}
bool VlcPlayer::seek_to_ms(std::int64_t milliseconds){
    if(!api_||!api_->set_time||milliseconds<0)return false;
    api_->set_time(api_->player,milliseconds);++seek_revision_;return true;
}
bool VlcPlayer::set_paused(bool paused) {
    if(!api_||!api_->set_pause)return false;
    api_->set_pause(api_->player,paused?1:0);
    if(!paused){
        // A resumed output may have been reconstructed by VLC.
        force_volume_refresh_=true;
        next_volume_retry_=GetTickCount64();
        volume_retry_until_=next_volume_retry_+1500;
    }
    audio_control_.playing(!paused);
    return true;
}
void VlcPlayer::stop() {
    // Stop the decoder but don't persist a hard mute into its next audio output.
    volume_retry_until_=0;
    next_volume_retry_=0;
    last_volume_playback_state_=-1;
    applied_volume_=-1;
    force_volume_refresh_=true;
    if(api_){api_->stop(api_->player);api_->set_media(api_->player,nullptr);}
    {std::lock_guard lock(frame_mutex_);frames_.clear();std::vector<std::uint8_t>().swap(decode_buffer_);}
    audio_control_.playing(false);
}
void VlcPlayer::shutdown() {
    if(!api_) return;
    stop(); api_->player_release(api_->player); api_->release(api_->instance);
    audio_control_.shutdown();
    FreeLibrary(api_->dll); delete api_; api_=nullptr;
}

void* __cdecl VlcPlayer::lock_callback(void* data,void** planes) {

    auto* self=static_cast<VlcPlayer*>(data);

    self->frame_mutex_.lock();

    *planes=reinterpret_cast<void*>((reinterpret_cast<std::uintptr_t>(self->decode_buffer_.data())+31)&~std::uintptr_t(31));

    return nullptr;

}

void __cdecl VlcPlayer::unlock_callback(void* data,void*,void* const* planes) {

    auto* self=static_cast<VlcPlayer*>(data);

    try {
        auto frame=std::make_shared<Frame>();frame->width=self->width_;frame->height=self->height_;
        const auto* pixels=static_cast<const std::uint8_t*>(*planes);
        frame->bgra.assign(pixels,pixels+static_cast<std::size_t>(self->width_)*self->height_*4);
        self->frames_.publish(std::move(frame));
    }catch(...){self->frames_.clear();} // Never throw through libVLC's C callback.
    self->frame_mutex_.unlock();
}

void __cdecl VlcPlayer::display_callback(void*,void*) {}
bool VlcPlayer::copy_frame(Frame& output) {
    auto frame=frames_.snapshot();if(!frame)return false;
    output=*frame;
    return true;
}
