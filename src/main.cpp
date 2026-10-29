#include <windows.h>
#include <imgui.h>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <deque>
#include <charconv>
#include <map>
#include <set>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include "vlc_player.h"
#include "url_resolver.h"
#include "tv_runtime.h"
#include "dx12_renderer.h"
#include "depth_capture.h"
#include "native_depth.h"
#include "host_bridge.h"
#include "audio_position.h"
#include "speaker_mix.h"
#include "tv_audio_routing.h"
#include "tv_sync_bridge.h"
#include "tv_sync_payload.h"
#include "setup_sync_runtime.h"
#include "sync_timeline.h"
#include "live_depth_hooks.h"
#include "playlist_preparation.h"
#include "tv_asset_presets.h"
#include "Extension/UI/Overlay/overlay.h"
namespace {
HMODULE module_handle{};
VlcPlayer& player=*new VlcPlayer;
std::filesystem::path directory;
std::array<char,4096> url{};
std::atomic<int> native_url_dialog_request{0}; // 1=media, 2=playlist; sent by sandboxed Insert menu
virtualtv::Screen screen;
virtualtv::AudioSettings audio_settings;
virtualtv::Vec3 angles;
enum class Action {Play,Pause,Resume,Stop,Volume,Save,Audio,SyncShare,SyncLeave,SyncRetry,SaveAsset,RemoveAsset,SetAutoAsset,ExportDefaults,LoadPlaylist,PlaylistNext,PlaylistPrevious,PlaylistRepeat,PlaylistClear,PlaylistSelect};
struct Command {Action action;std::string text;int volume{};virtualtv::Screen screen;bool resolve{};virtualtv::AudioSettings audio;virtualtv::Identity target;std::string asset_key;bool from_playlist{};int playlist_target{-1};bool auto_attach{};std::uint64_t sync_sender{},sync_room{},sync_revision{};};
std::mutex control_mutex;
std::deque<Command> commands;
std::string setup_status;float watched_range=35;bool watched_directional=true;
std::string playback_status="Enter a YouTube or direct media URL, then Play.";
std::string plugin_hex(std::string_view source,std::size_t limit=128){
    constexpr char hex[]="0123456789ABCDEF";
    std::string out;out.reserve(std::min(source.size(),limit)*2);
    for(unsigned char b:source.substr(0,limit)){out+=hex[b>>4];out+=hex[b&15];}
    return out;
}

std::vector<virtualtv::PlaylistEntry> ui_playlist_entries;
int ui_playlist_index=-1;
bool ui_playlist_repeat=false;
std::string ui_playlist_status;

// Persistent local alignment relative to each placed object's native transform.
// ReSkate session/entity handles are NOT used as disk keys because they change
// on reconnect. Instance+drop identifies the object's placement instance.
using ObjectKey=std::pair<std::uint64_t,std::uint32_t>;
std::map<ObjectKey,virtualtv::Screen> object_screens;
std::map<ObjectKey,std::string> object_assets;
virtualtv::presets::Library shipped_presets,custom_presets,effective_presets;
virtualtv::presets::Rules shipped_auto,custom_auto,effective_auto;

bool alignment_dirty=false;ULONGLONG alignment_edit_at{};
ObjectKey object_key(virtualtv::Identity id){return {id.instance,id.drop};}
bool object_profile(virtualtv::Identity id,virtualtv::Screen& out){
    if(!id.instance)return false;
    std::lock_guard lock(control_mutex);
    const auto it=object_screens.find(object_key(id));
    if(it==object_screens.end())return false;
    out=it->second;return true;
}
std::string asset_for(virtualtv::Identity id){
    for(const auto& n:virtualtv::placement_names_snapshot())if(n.identity==id&&!n.asset.empty())return virtualtv::presets::normalize(n.asset);
    return {};
}
bool recommended_screen(virtualtv::Identity id,virtualtv::Screen& out){
    if(object_profile(id,out))return true; // User's particular placement wins.
    const auto asset=asset_for(id);
    std::lock_guard lock(control_mutex);
    return virtualtv::presets::find(effective_presets,asset,out);
}
void publish_auto_presets(){
    std::vector<std::pair<std::string,virtualtv::Screen>> enabled;
    {
        std::lock_guard lock(control_mutex);
        for(const auto& [key,automatic]:effective_auto){
            const auto it=effective_presets.find(key);
            if(automatic&&it!=effective_presets.end())enabled.emplace_back(key,it->second);
        }
    }
    virtualtv::set_auto_screen_presets(std::move(enabled));
}
void rebuild_preset_rules_locked(){
    effective_presets=virtualtv::presets::merged(shipped_presets,custom_presets);
    effective_auto=shipped_auto;
    for(const auto& [key,value]:custom_auto)effective_auto[key]=value;
}
void load_asset_presets(){
    virtualtv::presets::Library shipped,user;
    virtualtv::presets::Rules shipped_rules,personal_rules;
    {std::ifstream in(directory/L"JishsVirtualTV.defaults");virtualtv::presets::read(in,shipped,shipped_rules);}
    {std::ifstream in(directory/L"JishsVirtualTV.presets");virtualtv::presets::read(in,user,personal_rules);}
    {
        std::lock_guard lock(control_mutex);
        shipped_presets=std::move(shipped);custom_presets=std::move(user);
        shipped_auto=std::move(shipped_rules);custom_auto=std::move(personal_rules);
        rebuild_preset_rules_locked();
    }
    publish_auto_presets();
}
bool write_asset_library(const std::filesystem::path& target,const virtualtv::presets::Library& library,const virtualtv::presets::Rules& rules){
    auto temp=target;temp+=L".tmp";
    {std::ofstream file(temp,std::ios::trunc);
     if(!file||!virtualtv::presets::write(file,library,rules)){virtualtv::log("Could not write TV asset preset file");return false;}}
    if(!MoveFileExW(temp.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
        virtualtv::log("Could not replace TV asset preset file");return false;}
    return true;
}
bool save_asset_preset(std::string key,const virtualtv::Screen& value){
    key=virtualtv::presets::normalize(std::move(key));
    if(key.empty()||!virtualtv::valid(virtualtv::Placement{},value))return false;
    virtualtv::presets::Library next;virtualtv::presets::Rules rules;
    {std::lock_guard lock(control_mutex);next=custom_presets;rules=custom_auto;next[key]=value;}
    if(!write_asset_library(directory/L"JishsVirtualTV.presets",next,rules))return false;
    {std::lock_guard lock(control_mutex);custom_presets=std::move(next);rebuild_preset_rules_locked();}
    publish_auto_presets();return true;
}
bool set_asset_auto_attach(std::string key,bool enabled){
    key=virtualtv::presets::normalize(std::move(key));
    if(key.empty())return false;
    virtualtv::presets::Library library;virtualtv::presets::Rules rules;
    {
        std::lock_guard lock(control_mutex);
        const auto it=effective_presets.find(key);
        if(it==effective_presets.end())return false;
        library=custom_presets;library[key]=it->second;
        rules=custom_auto;rules[key]=enabled;
    }
    if(!write_asset_library(directory/L"JishsVirtualTV.presets",library,rules))return false;
    {std::lock_guard lock(control_mutex);custom_presets=std::move(library);custom_auto=std::move(rules);rebuild_preset_rules_locked();}
    publish_auto_presets();return true;
}
bool remove_asset_preset(std::string key){
    key=virtualtv::presets::normalize(std::move(key));
    virtualtv::presets::Library next;virtualtv::presets::Rules rules;
    {std::lock_guard lock(control_mutex);next=custom_presets;rules=custom_auto;if(!next.erase(key))return false;rules.erase(key);}
    if(!write_asset_library(directory/L"JishsVirtualTV.presets",next,rules))return false;
    {std::lock_guard lock(control_mutex);custom_presets=std::move(next);custom_auto=std::move(rules);rebuild_preset_rules_locked();}
    publish_auto_presets();return true;
}
bool export_asset_presets(){
    virtualtv::presets::Library next;virtualtv::presets::Rules rules;
    {std::lock_guard lock(control_mutex);next=effective_presets;rules=effective_auto;}
    if(!write_asset_library(directory/L"JishsVirtualTV.defaults",next,rules))return false;
    {std::lock_guard lock(control_mutex);shipped_presets=std::move(next);shipped_auto=std::move(rules);rebuild_preset_rules_locked();}
    publish_auto_presets();return true;
}
// Resolves editor item keys only where a native placement corresponds to
// exactly one live spawned editor row. Coordinates are a matching aid, never
// stored as the asset identity.
void observe_asset(virtualtv::Identity selected,const dingosdk::overlay::Model& model){
    if(!selected.instance)return;
    const auto nearby=virtualtv::nearby_snapshot();
    const virtualtv::NearbyObject* row=nullptr;
    for(const auto& candidate:nearby.objects)if(candidate.identity==selected){row=&candidate;break;}
    if(!row)return;
    const dingosdk::EditorObject* match=nullptr;
    for(const auto& editor:model.editor.objects){
        if(!editor.spawned)continue;
        const auto& pos=row->placement.position;
        const auto dx=pos.x-editor.position[0],dy=pos.y-editor.position[1],dz=pos.z-editor.position[2];
        if(dx*dx+dy*dy+dz*dz>.0001f)continue;
        if(match)return; // Ambiguous placement, don't assign an asset type.
        match=&editor;
    }
    if(!match)return;
    const auto key=virtualtv::presets::normalize(match->item);
    if(key.empty())return;
    std::lock_guard lock(control_mutex);object_assets[object_key(selected)]=key;
}
void load_object_profiles(){
    std::ifstream file(directory/L"JishsVirtualTV.objects");std::string header;
    if(!(file>>header)||(header!="JISH_TV_OBJECTS_1"&&header!="JISH_TV_OBJECTS_2"))return;
    std::map<ObjectKey,virtualtv::Screen> profiles;
    std::size_t count{};
    while(count++<4096){
        ObjectKey key{};virtualtv::Screen value;
        if(!(file>>key.first>>key.second))break;
        if(!(file>>value.width>>value.height>>value.offset.x>>value.offset.y>>value.offset.z
                 >>value.rotation.x>>value.rotation.y>>value.rotation.z>>value.rotation.w))break;
        if(header=="JISH_TV_OBJECTS_2"&&!(file>>value.horizontal_curve_deg>>value.vertical_curve_deg))break;
        if(key.first&&virtualtv::valid(virtualtv::Placement{},value))profiles[key]=value;
    }
    std::lock_guard lock(control_mutex);object_screens=std::move(profiles);
}
void load_attachment_manifest(){
    const auto path=directory/L"JishsVirtualTV.attachments";
    std::vector<virtualtv::attachment_store::Record> records;
    std::ifstream in(path);
    if(in){
        if(!virtualtv::attachment_store::read(in,records)){
            virtualtv::log("Saved TV attachments file invalid; skipping automatic reattachment. Original file not modified.");
            return;
        }
        virtualtv::restore_persistent_outputs(records);
        virtualtv::log("Loaded "+std::to_string(records.size())+" saved screen/speaker attachments");
    }else if(!std::filesystem::exists(path)){
        // Migrate previous primary screen only when no new manifest exists.
        std::ifstream old(directory/L"JishsVirtualTV.screen");virtualtv::Settings legacy;
        if(virtualtv::load_settings(old,legacy)&&legacy.saved.instance){
            records.push_back({legacy.saved.instance,legacy.saved.drop,0,legacy.screen});
            virtualtv::restore_persistent_outputs(records);
            virtualtv::log("Imported existing primary screen attachment for identity-based restoration");
        }
    }
}
bool write_attachment_manifest(const std::vector<virtualtv::attachment_store::Record>& records){
    const auto path=directory/L"JishsVirtualTV.attachments",temp=directory/L"JishsVirtualTV.attachments.tmp";
    {std::ofstream out(temp,std::ios::trunc);if(!out||!virtualtv::attachment_store::write(out,records))return false;
     out.flush();if(!out)return false;}
    if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
        virtualtv::log("Cannot commit saved TV attachments; will retry");return false;
    }
    return true;
}
void save_object_profile(virtualtv::Identity id,const virtualtv::Screen& value){
    if(!id.instance||!virtualtv::valid(virtualtv::Placement{},value))return;
    std::map<ObjectKey,virtualtv::Screen> profiles;
    {std::lock_guard lock(control_mutex);object_screens[object_key(id)]=value;profiles=object_screens;}
    const auto target=directory/L"JishsVirtualTV.objects",temp=directory/L"JishsVirtualTV.objects.tmp";
    {std::ofstream file(temp,std::ios::trunc);if(!file){virtualtv::log("Cannot save per-object screen alignment");return;}
     file<<"JISH_TV_OBJECTS_2\n"<<std::setprecision(9);
     for(const auto& [key,s]:profiles)file<<key.first<<' '<<key.second<<' '<<s.width<<' '<<s.height<<' '
         <<s.offset.x<<' '<<s.offset.y<<' '<<s.offset.z<<' '
         <<s.rotation.x<<' '<<s.rotation.y<<' '<<s.rotation.z<<' '<<s.rotation.w<<' '
         <<s.horizontal_curve_deg<<' '<<s.vertical_curve_deg<<'\n';
     file.flush();if(!file){virtualtv::log("Cannot flush per-object screen alignment");return;}}
    if(!MoveFileExW(temp.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        virtualtv::log("Cannot replace per-object screen alignment");
}

// UI reads this snapshot only; libVLC calls remain on the worker thread.
std::atomic<bool> ui_sync_bridge{false}, ui_sync_active{false}, ui_sync_host{false}, ui_sync_enabled{false}, ui_sync_online{false}, ui_sync_plugin{false};
std::atomic<bool> ui_session_active{false},ui_session_host{false},ui_session_known{false};
std::atomic<unsigned> ui_session_others{0},ui_sync_ready_viewers{0};
std::string sync_status,preset_status;
virtualtv::sync::Selection receive_selection;std::vector<virtualtv::sync::Peer> ui_sync_peers;
void sync_feedback(std::string message){std::lock_guard lock(control_mutex);sync_status=std::move(message);}
void preset_feedback(std::string message){std::lock_guard lock(control_mutex);preset_status=std::move(message);}
std::atomic<bool> user_hid_screen{false};
std::atomic<int> ui_playback_state{0}; // 0 stopped, 1 playing, 2 paused, 3 resolving
void feedback(std::string text){std::lock_guard lock(control_mutex);playback_status=std::move(text);}
void enqueue(Command c){std::lock_guard lock(control_mutex);if(commands.size()<16)commands.push_back(std::move(c));}
std::string utf8(const std::wstring& text){if(text.empty())return {};const auto n=WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);std::string s(n,'\0');WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),s.data(),n,nullptr,nullptr);return s;}
void save(const virtualtv::Screen& current,const virtualtv::AudioSettings& audio){
    const auto target=directory/L"JishsVirtualTV.screen",temp=directory/L"JishsVirtualTV.screen.tmp";
    {std::ofstream out(temp);if(!out){virtualtv::log("Cannot write alignment settings");return;}virtualtv::write_settings(out,{current,virtualtv::attached_identity(),audio});out.flush();if(!out){virtualtv::log("Cannot flush alignment settings");return;}}
    if(!MoveFileExW(temp.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))virtualtv::log("Cannot replace alignment settings");
}
void load(){
    {std::ifstream in(directory/L"JishsVirtualTV.url",std::ios::binary);in.getline(url.data(),url.size(),'\0');}
    std::ifstream in(directory/L"JishsVirtualTV.screen");virtualtv::Settings settings;
    if(virtualtv::load_settings(in,settings)){
        screen=settings.screen;audio_settings=settings.audio;virtualtv::restore_identity(settings.saved);
        // One-time migration of the original single-object alignment.
        virtualtv::Screen migrated;
        if(settings.saved.instance&&!object_profile(settings.saved,migrated))
            save_object_profile(settings.saved,settings.screen);
        const auto q=screen.rotation;constexpr float deg=180/3.14159265359f;
        angles={std::atan2(2*(q.w*q.x+q.y*q.z),1-2*(q.x*q.x+q.y*q.y))*deg,std::asin(std::clamp(2*(q.w*q.y-q.z*q.x),-1.0f,1.0f))*deg,std::atan2(2*(q.w*q.z+q.x*q.y),1-2*(q.y*q.y+q.z*q.z))*deg};
    }
    virtualtv::set_screen(screen);
}
std::string object_label(const virtualtv::NearbyObject& row,const dingosdk::overlay::Model&,
                         const std::vector<std::pair<virtualtv::Identity,virtualtv::OutputKind>>& assignments){
    // One native-verified naming lookup drives lists and presets.
    std::string name="Unnamed object";
    for(const auto& n:virtualtv::placement_names_snapshot())if(n.identity==row.identity){name=n.name.empty()?n.asset:n.name;break;}
    char suffix[192]{};const auto p=row.placement.position;snprintf(suffix,sizeof suffix," #%u | %.1f m | (%.1f, %.1f, %.1f)",row.identity.drop,row.distance,p.x,p.y,p.z);
    for(const auto& [id,kind]:assignments){
        if(virtualtv::same_placed_object(id,row.identity))
            return std::string(kind==virtualtv::OutputKind::Screen?"[TV] ":"[SP] ")+name+suffix;
    }
    return name+suffix;
}
DWORD WINAPI start(LPVOID){
    wchar_t path[32768]{};if(!GetModuleFileNameW(module_handle,path,32768))return 0;
    HMODULE pinned{};GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&start),&pinned);
    directory=std::filesystem::path(path).parent_path();virtualtv::initialize_log(directory/L"JishsVirtualTV.log");load_object_profiles();load_asset_presets();load_attachment_manifest();
    {std::ifstream render(directory/L"JishsVirtualTV.render");float saved_near_plane{};if(render>>saved_near_plane)virtualtv::depth::native::set_estimated_near_plane(saved_near_plane);}
    virtualtv::log("=== JishsVirtualTV selective-sync5: player selection, automatic speakers, playlist preparation ===");load();
    virtualtv::sync::Bridge bridge;
    virtualtv::fullsync::Runtime full_setup(bridge);
    std::string local_playlist_status;int local_playlist_index=-1;bool saved_viewer_ui=false;
    ui_sync_bridge.store(bridge.initialize(directory));
    virtualtv::initialize_native();bool rendering=false;
    using virtualtv::sync::Kind;
    using virtualtv::sync::Packet;
    struct SessionSync {
        bool active{},owner{},local_ready{},ready_sent{},remote_ready{},clock_valid{},start_pending{},started{},ever_started{},prepare_media{},can_schedule{},waiting_decode{};
        std::uint64_t session_epoch{},room{},revision{1},controller{},last_offer{},ready_at{},ping_at{},planned_start_us{},start_position_ms{};
        std::int64_t offset_us{};
        std::string media_url,offer_payload;
        virtualtv::Placement placement;
        virtualtv::Vec3 scale{1,1,1};
        virtualtv::Screen screen;
        virtualtv::sync::JoinTimeline join;virtualtv::sync::HostTimeline host_timeline;std::uint64_t anchor_sequence{},next_timeline_sample{};
        unsigned viewers{};
        std::set<std::uint64_t> viewer_ids;
    } sync;
    auto send_sync=[&](Kind kind,std::uint64_t timing=0,std::uint64_t position=0,const std::string& payload=std::string{},std::uint64_t recipient=0){
        if(!bridge.my_id()||!sync.active)return false;
        if(kind==Kind::full_offer){timing=sync.session_epoch;position=bridge.world_id();}
        return bridge.send(Packet{kind,sync.room,sync.revision,bridge.my_id(),timing,position,payload},recipient);
    };
    auto reset_sync=[&](bool retain_setup=false){if(!retain_setup){full_setup.leave();full_setup.stop_host();if(saved_viewer_ui){std::lock_guard lock(control_mutex);ui_playlist_status=local_playlist_status;ui_playlist_index=local_playlist_index;saved_viewer_ui=false;}}sync=SessionSync{};ui_sync_active=false;ui_sync_host=false;ui_sync_ready_viewers=0;};

    virtualtv::log("TV graphics/menu hooks deferred until ReSkate reports ready and has submitted its first overlay frame");
    ULONGLONG retry=GetTickCount64()+1000;int previous_bridge_state=-1;bool was_down{};auto last=virtualtv::Identity{};auto saved_screen=screen;auto saved_audio=audio_settings;player.volume(saved_audio.volume);
    ULONGLONG next_audio_log{};bool first_audio{};
    virtualtv::UrlResolver resolver,prefetch_resolver;std::string resolving_url;bool resolving_playlist=false;
    virtualtv::PlaylistPreparation preparation;std::uint64_t prefetch_announced{};
    std::string cached_playing_url;bool cached_playing=false;
    auto invalidate_preparation=[&]{prefetch_resolver.cancel();preparation.clear();prefetch_announced=0;};
    auto begin_preparation=[&](std::string source_url,int target,std::uint64_t room,std::uint64_t revision){
        if(preparation.matches(source_url,target,room,revision,GetTickCount64()))return;
        invalidate_preparation();preparation.url=std::move(source_url);preparation.target=target;
        preparation.scope=room;preparation.revision=revision;preparation.started=GetTickCount64();
        prefetch_resolver.start(directory,preparation.url);
    };
    std::vector<virtualtv::PlaylistEntry> playlist_entries;int playlist_index=-1;
    bool playlist_repeat=false;unsigned consecutive_failures=0,skipped_videos=0;
    bool watching_playback{},playback_reached{};int previous_state=-2;ULONGLONG next_state{};
    const auto publish_playlist=[&](std::string status){
        std::lock_guard lock(control_mutex);
        ui_playlist_entries=playlist_entries;ui_playlist_index=playlist_index;
        ui_playlist_repeat=playlist_repeat;ui_playlist_status=std::move(status);
    };
    const auto select_playlist=[&](int target)->bool{
        if(playlist_entries.empty())return false;
        if(target<0||target>=static_cast<int>(playlist_entries.size())){
            if(!playlist_repeat)return false;
            target=(target<0)?static_cast<int>(playlist_entries.size())-1:0;
        }
        playlist_index=target;
        publish_playlist((skipped_videos?"Skipped "+std::to_string(skipped_videos)+" unavailable video(s). ":std::string{})+
                         "Selected "+std::to_string(target+1)+" / "+std::to_string(playlist_entries.size())+": "+playlist_entries[target].title);
        Command track{Action::Play,playlist_entries[target].url};track.resolve=true;track.from_playlist=true;
        enqueue(std::move(track));return true;
    };
    const auto begin_playback=[&](const virtualtv::ResolvedMedia& media,const std::string& original){
        std::wstring error;
        if(player.play_resolved(media,error)){
            if(sync.active&&!sync.owner&&sync.prepare_media){
                player.set_paused(true);sync.local_ready=false;sync.waiting_decode=true;
                sync.ready_sent=false;sync.ready_at=GetTickCount64()+750;sync.prepare_media=false;
                ui_playback_state.store(2);
            }else ui_playback_state.store(1);
            virtualtv::log("TV playback started");virtualtv::show_test_pattern(false);feedback("Playback running. Attached speakers work without a TV; video appears when a screen is attached.");
            if(!sync.active || sync.owner){std::ofstream out(directory/L"JishsVirtualTV.url",std::ios::binary|std::ios::trunc);if(out)out<<original;}
            virtualtv::debug_log("TV audio initialized: original depth3 VLC output, session gain controls; directional="+std::to_string(saved_audio.directional)+" range_m="+std::to_string(saved_audio.range));
            virtualtv::debug_log(std::string("VLC playback requested (address omitted); separate_audio=")+(media.audio.empty()?"0":"1"));watching_playback=true;playback_reached=false;previous_state=-2;
        }else{
            feedback(utf8(error));virtualtv::log("VLC playback failed: "+utf8(error));watching_playback=false;ui_playback_state.store(0);
            if(cached_playing){
                cached_playing=false;if(sync.active&&!sync.started){sync.prepare_media=true;sync.local_ready=false;sync.ready_sent=false;sync.waiting_decode=false;}resolving_url=original;resolver.start(directory,original);
                ui_playback_state.store(3);feedback("Refreshing prepared video link...");return;
            }
            if(!playlist_entries.empty()&&playlist_index>=0&&(sync.owner||!sync.active)){
                if(++consecutive_failures<playlist_entries.size()){
                    ++skipped_videos;enqueue({Action::PlaylistNext});
                }
                else publish_playlist("All playlist entries failed to play.");
            }
        }
    };
    // An existing (even malformed) file is never replaced at startup merely
    // because the in-memory store is empty. Only a user change may replace it.
    const bool had_manifest=std::filesystem::exists(directory/L"JishsVirtualTV.attachments");
    std::uint64_t written_revision=had_manifest?virtualtv::persistent_outputs_snapshot().first:0,pending_revision=0;
    ULONGLONG pending_since=0;
    ULONGLONG next_plugin_poll=0,next_bridge_poll=0,next_log_flush=0,next_host_check=0,next_attachment_scan=0,next_hook_service=0;
    std::uint64_t previous_connection_epoch{};std::uint64_t observed_world{};
    auto emit_anchor=[&](std::uint64_t recipient=0){
        if(!sync.active||!sync.owner)return;
        const auto before=GetTickCount64()*1000;const auto position=player.playback_time_ms();const int state=player.playback_state();const auto after=GetTickCount64()*1000;
        auto playback=virtualtv::sync::Playback::Buffering;
        if(resolving_url.empty()&&watching_playback&&position>=0){if(state==3)playback=virtualtv::sync::Playback::Playing;else if(state==4)playback=virtualtv::sync::Playback::Paused;}
        const auto payload=virtualtv::sync::encode_anchor({++sync.anchor_sequence,(before+after)/2,static_cast<std::uint64_t>(std::max<std::int64_t>(0,position)),playback});
        if(recipient)send_sync(Kind::timeline_anchor,0,0,payload,recipient);
        else for(auto viewer:sync.viewer_ids)send_sync(Kind::timeline_anchor,0,0,payload,viewer);
    };
    for(;;){
        const auto now=GetTickCount64();
        if(now>=next_host_check){next_host_check=now+500;if(virtualtv::host_shutdown_requested())break;}
        if(now>=next_hook_service){next_hook_service=now+100;virtualtv::depth::live::maintain();}
        if(now>=next_log_flush){next_log_flush=now+1000;virtualtv::flush_log();}
        const auto world_epoch=virtualtv::placement_session_epoch();
        if(observed_world&&world_epoch!=observed_world){
            bridge.clear_pending();if(sync.active&&sync.owner)send_sync(Kind::stop);
            resolver.cancel();resolving_url.clear();resolving_playlist=false;invalidate_preparation();
            player.stop();watching_playback=false;cached_playing=false;ui_playback_state.store(0);reset_sync();
            virtualtv::set_screen_mode(virtualtv::ScreenMode::Hidden);
            {std::lock_guard lock(control_mutex);commands.clear();receive_selection.clear();}
            virtualtv::log("Playback and pending media jobs cleared after world change");
        }
        observed_world=world_epoch;
        // Save the desired attachment manifest from this worker, NEVER inside
        // the native game tick hook. Coalesce rapid alignment edits.
        if(now>=next_attachment_scan){
            next_attachment_scan=now+200;
            auto [revision,desired]=virtualtv::persistent_outputs_snapshot();
            if(revision!=written_revision){
                if(revision!=pending_revision){pending_revision=revision;pending_since=now;}
                if(now-pending_since>=400 && write_attachment_manifest(desired))written_revision=revision;
            }
        }
        // Reappearance should not require opening F8. The worker (not the
        // menu render callback) consumes the game-hook restoration signal.
        if(virtualtv::take_restored_screen_signal() && !full_setup.watching() && !user_hid_screen.load() &&
           virtualtv::current_screen_mode()==virtualtv::ScreenMode::Hidden)
            virtualtv::set_screen_mode(virtualtv::ScreenMode::Attached);
        if(!bridge.available()&&now%3000<25){ui_sync_bridge.store(bridge.initialize(directory));}
        bridge.flush(now);
        std::vector<virtualtv::sync::RelayEvent> events;const bool bridge_polled=now>=next_bridge_poll;
        if(bridge_polled){next_bridge_poll=now+200;events=bridge.poll();}
        const auto plugin_epoch=bridge.session_epoch();
        const auto connection_epoch=plugin_epoch?(plugin_epoch^(virtualtv::placement_session_epoch()*0x517cc1b727220a95ull)):0;
        if(connection_epoch!=previous_connection_epoch){virtualtv::set_auto_speakers(false);previous_connection_epoch=connection_epoch;}
        virtualtv::sync::Selection selection;
        {std::lock_guard lock(control_mutex);if(bridge_polled||receive_selection.session!=connection_epoch){ui_sync_peers=bridge.peers();receive_selection.refresh(connection_epoch,ui_sync_peers);}selection=receive_selection;}
        if(sync.active&&(!bridge.online()||!connection_epoch||sync.session_epoch!=connection_epoch||(!sync.owner&&(!ui_sync_enabled.load()||!selection.accepts(sync.controller))))){
            const bool viewer=!sync.owner;
            const std::string reason=!bridge.online()?"bridge heartbeat expired":!connection_epoch?"multiplayer session ended":sync.session_epoch!=connection_epoch?"bridge restarted or world changed":!ui_sync_enabled.load()?"sync disabled":"selected broadcaster changed or left";
            if(!resolving_url.empty())virtualtv::log("Pending video resolution cancelled: "+reason);
            resolver.cancel();resolving_url.clear();resolving_playlist=false;invalidate_preparation();
            cached_playing=false;watching_playback=false;player.stop();ui_playback_state.store(0);
            if(viewer){virtualtv::set_screen_mode(virtualtv::ScreenMode::Hidden);virtualtv::set_synced_scene({}, {1,1,1}, {});}
            bridge.clear_pending();reset_sync();sync_feedback("Sync ended: "+reason+".");
            virtualtv::log("TV sync ended: "+reason);
        }
        full_setup.tick(now,ui_sync_enabled.load(),connection_epoch,saved_audio);
        {std::lock_guard lock(control_mutex);setup_status=full_setup.status();watched_range=full_setup.range();watched_directional=full_setup.directional();}
        for(const auto& event:events){
            const auto& p=event.packet;
            if(p.sender!=event.from||p.sender==bridge.my_id())continue;
            if(full_setup.receive(event,selection.active,now))continue;
            if(sync.owner&&!full_setup.compatible(p.sender))continue;
            if(!sync.owner&&(!ui_sync_enabled.load()||!selection.accepts(p.sender)))continue;
            if(p.kind==Kind::prepare){
                if(!sync.active||sync.owner||p.sender!=sync.controller||p.room!=sync.room||p.revision!=sync.revision+1)continue;
                auto next=virtualtv::sync::decode_preparation(p.payload);
                if(next&&virtualtv::youtube_url(next->url))begin_preparation(next->url,next->playlist_index,p.room,p.revision);
                continue;
            }
            if(p.kind==Kind::offer){sync_feedback("Broadcaster needs the full-setup DLL and bridge update. Legacy positioned offers are disabled.");continue;}
            if(p.kind==Kind::full_offer){
                if(!full_setup.current_offer(p)){sync_feedback("Broadcaster full-setup capability unavailable. Both players need the matched DLL/bridge update.");continue;}
                if(!ui_sync_enabled.load())continue;
                if(sync.active&&sync.owner)continue;
                // Late heartbeat offers for previous playlist tracks must never rewind the viewer.
                if(sync.active&&sync.room==p.room&&p.revision<=sync.revision)continue;
                if(!p.room||!p.revision)continue;
                auto offer=virtualtv::sync::decode_offer(p.payload);
                if(!offer||!virtualtv::media_http_url(offer->url))continue;
                const bool retain=sync.active&&!sync.owner&&sync.controller==p.sender&&sync.room==p.room;
                if(!retain){
                    resolver.cancel();resolving_url.clear();resolving_playlist=false;invalidate_preparation();
                    player.stop();watching_playback=false;cached_playing=false;ui_playback_state.store(0);
                }
                reset_sync(retain);
                if(!saved_viewer_ui){std::lock_guard lock(control_mutex);local_playlist_status=ui_playlist_status;local_playlist_index=ui_playlist_index;saved_viewer_ui=true;}full_setup.watch(p.sender,p.room,now);sync.active=true;sync.session_epoch=connection_epoch;sync.controller=p.sender;sync.room=p.room;sync.revision=p.revision;
                sync.media_url=offer->url;sync.prepare_media=true;ui_sync_active=true;
                if(offer->playlist_total){
                    std::lock_guard lock(control_mutex);
                    ui_playlist_status="Host playlist video "+std::to_string(offer->playlist_index+1)+" / "+std::to_string(offer->playlist_total)+
                        (offer->playlist_repeat?" (repeat enabled)":"");
                    ui_playlist_index=offer->playlist_index;
                }else{
                    std::lock_guard lock(control_mutex);
                    ui_playlist_status="Shared single video.";ui_playlist_index=-1;
                }
                sync.placement=offer->placement;sync.scale=offer->scale;sync.screen=offer->screen;
                {std::lock_guard lock(control_mutex);
                    if(!ui_sync_enabled.load()||!receive_selection.accepts(p.sender))continue;
                    // Full setup binds verified local objects; offer coordinates never place outputs.
                    virtualtv::set_screen_mode(virtualtv::ScreenMode::SyncedWorld);
                }
                Command play{Action::Play,offer->url};play.resolve=virtualtv::youtube_url(offer->url);
                play.sync_sender=p.sender;play.sync_room=p.room;play.sync_revision=p.revision;
                play.playlist_target=offer->playlist_total?offer->playlist_index:-1;
                play.from_playlist=true; // Host supplied track; preserve the shared queue status.
                enqueue(std::move(play));
                send_sync(Kind::ping,now*1000,0,{},sync.controller);sync.ping_at=now;
                virtualtv::log("TV sync broadcaster accepted");sync_feedback("Shared TV received. Loading video and waiting for scheduled start.");
            }else if(!sync.active||p.room!=sync.room||p.revision!=sync.revision)continue;
            else if(p.kind==Kind::timeline_request&&sync.owner){
                if(p.payload!="TLR1")continue;
                sync.viewer_ids.insert(p.sender);sync.viewers=static_cast<unsigned>(sync.viewer_ids.size());ui_sync_ready_viewers=sync.viewers;
                emit_anchor(p.sender);sync_feedback("Viewer is catching up; host playback is unchanged.");
            }else if(p.kind==Kind::ping&&sync.owner){
                send_sync(Kind::pong,now*1000,now,std::to_string(p.timing_us),p.sender);
            }else if(p.kind==Kind::pong&&!sync.owner&&p.sender==sync.controller){
                std::uint64_t sent{};
                auto parsed=std::from_chars(p.payload.data(),p.payload.data()+p.payload.size(),sent);
                if(parsed.ec==std::errc{}&&parsed.ptr==p.payload.data()+p.payload.size()){
                    auto sample=virtualtv::sync::estimate_offset(sent,p.timing_us,p.position_ms*1000,now*1000);
                    if(sample&&sample->rtt_us<3000000){sync.offset_us=sample->offset_us;sync.join.rtt_us=sample->rtt_us;sync.clock_valid=true;}
                }
            }else if(p.sender!=sync.controller)continue;
            else if(p.kind==Kind::timeline_anchor&&!sync.owner){
                if(!sync.local_ready||!sync.clock_valid||!full_setup.ready())continue;
                auto anchor=virtualtv::sync::decode_anchor(p.payload);if(!anchor)continue;
                const auto host_now=static_cast<std::uint64_t>(std::max<std::int64_t>(0,static_cast<std::int64_t>(now*1000)+sync.offset_us));
                if(sync.join.accept(*anchor,host_now)){
                    player.set_paused(true);ui_playback_state.store(2);sync.started=false;
                    if(sync.join.phase==virtualtv::sync::JoinTimeline::Phase::Seek){
                        if(!player.seekable())sync.join.fail("Shared stream is not seekable; cannot align. Use Retry sync or wait for another track.");
                        else if(!player.seek_to_ms(static_cast<std::int64_t>(sync.join.target_ms)))sync.join.fail("VLC could not seek the shared stream. Use Retry sync.");
                    }
                }
            }else if(p.kind==Kind::stop&&!sync.owner){
                resolver.cancel();resolving_url.clear();invalidate_preparation();cached_playing=false;watching_playback=false;
                player.stop();ui_playback_state.store(0);virtualtv::set_screen_mode(virtualtv::ScreenMode::Hidden);reset_sync();
            }
        }
        if(sync.active&&!sync.owner&&!full_setup.watching()){
            resolver.cancel();resolving_url.clear();invalidate_preparation();player.stop();watching_playback=false;ui_playback_state.store(0);reset_sync();
            sync_feedback("Broadcaster session changed. Waiting for a current offer.");
        }
        const int bridge_state=(bridge.available()?1:0)|(bridge.online()?2:0)|(bridge.my_id()?4:0);
        if(bridge_state!=previous_bridge_state){if(!sync.active)sync_feedback("");previous_bridge_state=bridge_state;}
        ui_sync_online.store(bridge.my_id()!=0);ui_sync_plugin.store(bridge.online());
        const auto lobby=bridge.lobby();
        ui_session_active.store(lobby.active);ui_session_host.store(lobby.hosting);
        ui_session_known.store(lobby.reported);ui_session_others.store(lobby.others);
        if(sync.active&&!ui_sync_enabled.load()){
            resolver.cancel();resolving_url.clear();invalidate_preparation();cached_playing=false;
            if(sync.owner)send_sync(Kind::stop);
            else {player.stop();ui_playback_state.store(0);virtualtv::set_screen_mode(virtualtv::ScreenMode::Hidden);}
            reset_sync();sync_feedback("TV sync disabled.");
        }
        if(sync.active){
            if(sync.waiting_decode&&now>=sync.ready_at){
                const int vlc_state=player.playback_state();
                if(vlc_state==4){sync.waiting_decode=false;sync.local_ready=true;sync.ready_at=now+300;}
                else {if(vlc_state==3)player.set_paused(true);sync.ready_at=now+350;}
            }
            if(!sync.owner&&!sync.clock_valid&&sync.ping_at&&now-sync.ping_at>1000){send_sync(Kind::ping,now*1000,0,{},sync.controller);sync.ping_at=now;}
            if(!sync.owner&&sync.clock_valid&&sync.local_ready&&full_setup.ready()){
                using Phase=virtualtv::sync::JoinTimeline::Phase;
                const auto host_now=static_cast<std::uint64_t>(std::max<std::int64_t>(0,static_cast<std::int64_t>(now*1000)+sync.offset_us));
                sync.join.timeout(host_now);
                if(sync.join.phase==Phase::Request){
                    player.set_paused(true);ui_playback_state.store(2);
                    if(sync.join.attempts>=3)sync.join.fail("Sync preparation failed after three attempts. Use Retry sync.");
                    else if(send_sync(Kind::timeline_request,0,0,"TLR1",sync.controller)){
                        sync.join.request_sent(host_now);sync_feedback("Requesting the host's current playback position...");
                    }
                }
                const auto before=sync.join.phase;
                if(sync.join.readback(player.playback_time_ms(),player.playback_state(),host_now)){
                    if(player.set_paused(false)){sync.started=true;sync.ever_started=true;ui_playback_state.store(1);sync_feedback("Caught up to broadcaster; no periodic drift correction.");}
                    else sync.join.fail("VLC could not resume shared playback. Use Retry sync.");
                }else if(before!=Phase::Joined&&sync.join.phase==Phase::Joined){sync_feedback("Aligned to the broadcaster's paused position.");}
                if(sync.join.phase==Phase::Failed)sync_feedback(sync.join.failure);
            }
            if(sync.owner&&now>=sync.next_timeline_sample){
                sync.next_timeline_sample=now+100;
                const int state=resolving_url.empty()&&watching_playback?player.playback_state():1;const auto position=player.playback_time_ms();const auto sampled=now*1000;
                if(sync.host_timeline.observe(sampled,position,state,player.seek_revision()))emit_anchor();
            }
            if(sync.owner&&now-sync.last_offer>=8000&&!sync.offer_payload.empty()){
                sync.last_offer=now;send_sync(Kind::full_offer,0,0,sync.offer_payload);
            }

        }
        if(!rendering&&now>=retry){retry=now+5000;rendering=virtualtv::start_renderer(player);}
        const bool down=(GetAsyncKeyState(VK_F8)&0x8000)!=0;DWORD pid{};GetWindowThreadProcessId(GetForegroundWindow(),&pid);
        if(down&&!was_down&&pid==GetCurrentProcessId())virtualtv::host_open_tv();was_down=down;
        // Plugin menu commands originate from sandboxed reskate.files.write.
        // They are never evaluated or passed to game memory as executable code.
        if(now>=next_plugin_poll){
            next_plugin_poll=now+150;
            for(const auto& action:bridge.poll_menu_commands()){
                const auto& op=action.name;const auto& arg=action.argument;
                if(full_setup.watching()&&op!="VOL"&&op!="SYNC_LEAVE")continue;
                const auto number_arg=[&](int& value,int low,int high){
                    auto parse=std::from_chars(arg.data(),arg.data()+arg.size(),value);
                    return parse.ec==std::errc{}&&parse.ptr==arg.data()+arg.size()&&value>=low&&value<=high;
                };
                const auto object_arg=[&]()->virtualtv::Identity{
                    virtualtv::Identity chosen{};
                    const auto colon=arg.find(':');if(colon==std::string::npos)return chosen;
                    std::uint64_t instance{};std::uint32_t drop{};
                    auto a=std::from_chars(arg.data(),arg.data()+colon,instance);
                    auto b=std::from_chars(arg.data()+colon+1,arg.data()+arg.size(),drop);
                    if(a.ec!=std::errc{}||a.ptr!=arg.data()+colon||b.ec!=std::errc{}||b.ptr!=arg.data()+arg.size()||!instance)return chosen;
                    for(const auto& object:virtualtv::nearby_snapshot().objects){
                        if(object.identity.instance==instance&&object.identity.drop==drop)return object.identity;
                    }
                    return chosen;
                };
                if(op=="OPEN_SETTINGS"){
                    virtualtv::host_show_tv();
                }else if(op=="OPEN_URL"||op=="OPEN_PLAYLIST"){
                    native_url_dialog_request.store(op=="OPEN_PLAYLIST"?2:1);
                    virtualtv::host_show_tv();
                }else if(op=="PLAY" && !arg.empty()&&arg.size()<=800){
                    Command c{Action::Play,arg};c.resolve=virtualtv::youtube_url(arg);enqueue(std::move(c));
                }else if(op=="LOAD"&&!arg.empty()&&arg.size()<=800){enqueue({Action::LoadPlaylist,arg});}
                else if(op=="PAUSE")enqueue({Action::Pause});
                else if(op=="RESUME")enqueue({Action::Resume});
                else if(op=="STOP")enqueue({Action::Stop});
                else if(op=="NEXT")enqueue({Action::PlaylistNext});
                else if(op=="PREV")enqueue({Action::PlaylistPrevious});
                else if(op=="REPEAT")enqueue({Action::PlaylistRepeat});
                else if(op=="CLEAR")enqueue({Action::PlaylistClear});
                else if(op=="PICK") {int index{};if(number_arg(index,0,299)){Command c{Action::PlaylistSelect};c.playlist_target=index;enqueue(std::move(c));}}
                else if(op=="VOL") {int vol{};if(number_arg(vol,0,100))enqueue({Action::Volume,{},vol});}
                else if(op=="DIRECTION"){int value{};if(number_arg(value,0,1)){auto v=saved_audio;v.directional=value!=0;Command c{Action::Audio};c.audio=v;enqueue(std::move(c));}}
                else if(op=="RANGE"){int value{};if(number_arg(value,5,1000)){auto v=saved_audio;v.range=static_cast<float>(value);Command c{Action::Audio};c.audio=v;enqueue(std::move(c));}}
                else if(op=="SYNC_ENABLE") {int value{};if(number_arg(value,0,1))ui_sync_enabled.store(value!=0);}
                else if(op=="SYNC_SHARE")enqueue({Action::SyncShare});
                else if(op=="SYNC_LEAVE")enqueue({Action::SyncLeave});
                else if(op=="HIDE") {user_hid_screen=true;virtualtv::set_screen_mode(virtualtv::ScreenMode::Hidden);}
                else if(op=="SHOW") {user_hid_screen=false;virtualtv::set_screen_mode(virtualtv::ScreenMode::Attached);}
                else if(op=="PREVIEW") {user_hid_screen=false;virtualtv::set_screen_mode(virtualtv::ScreenMode::Front);}
                else if(op=="DETACH_ALL") {virtualtv::clear_persistent_outputs();virtualtv::request_detach();virtualtv::set_screen_mode(virtualtv::ScreenMode::Hidden);user_hid_screen=true;}
                else if(op=="ADJUST"){
                    // Alignment commands use the live object identity for THIS
                    // session; stable name/position-based restore remains native.
                    const auto first=arg.find(':'),second=arg.find(':',first==std::string::npos?0:first+1);
                    const auto third=arg.find(':',second==std::string::npos?0:second+1);
                    if(first==arg.npos||second==arg.npos||third==arg.npos)continue;
                    const auto identity_token=arg.substr(0,second);
                    auto colon=identity_token.find(':');
                    std::uint64_t instance{};std::uint32_t drop{};
                    auto x=std::from_chars(identity_token.data(),identity_token.data()+colon,instance);
                    auto y=std::from_chars(identity_token.data()+colon+1,identity_token.data()+identity_token.size(),drop);
                    if(x.ec!=std::errc{}||y.ec!=std::errc{}||!instance)continue;
                    virtualtv::Identity selected{};
                    for(const auto& object:virtualtv::nearby_snapshot().objects)
                        if(object.identity.instance==instance&&object.identity.drop==drop){selected=object.identity;break;}
                    if(!selected.instance)continue;
                    float amount{};
                    auto val=arg.substr(third+1);const auto parsed=std::from_chars(val.data(),val.data()+val.size(),amount);
                    if(parsed.ec!=std::errc{}||parsed.ptr!=val.data()+val.size()||!std::isfinite(amount))continue;
                    const auto field=arg.substr(second+1,third-second-1);
                    virtualtv::Screen out{};bool attached=false;
                    const auto primary=virtualtv::scene_snapshot();
                    if(primary.attached&&primary.identity==selected){out=primary.screen;attached=true;}
                    for(const auto& extra:virtualtv::extra_outputs_snapshot())if(extra.identity==selected&&extra.kind==virtualtv::OutputKind::Screen){out=extra.screen;attached=true;break;}
                    if(!attached)continue;
                    if(field=="W"&&amount>=.1f&&amount<=50.f)out.width=amount;
                    else if(field=="H"&&amount>=.1f&&amount<=50.f)out.height=amount;
                    else if(field=="HC"&&amount>=-160.f&&amount<=160.f)out.horizontal_curve_deg=amount;
                    else if(field=="VC"&&amount>=-120.f&&amount<=120.f)out.vertical_curve_deg=amount;
                    else if(field=="OX"&&amount>=-10.f&&amount<=10.f)out.offset.x=amount;
                    else if(field=="OY"&&amount>=-10.f&&amount<=10.f)out.offset.y=amount;
                    else if(field=="OZ"&&amount>=-10.f&&amount<=10.f)out.offset.z=amount;
                    else continue;
                    if(virtualtv::valid(virtualtv::Placement{},out)){
                        virtualtv::set_screen_for(selected,out);save_object_profile(selected,out);
                    }
                }
                else if(op=="ATTACH_TV"||op=="ATTACH_SP"||op=="DETACH"){
                    auto chosen=object_arg();if(!chosen.instance){feedback("Object no longer nearby; refresh the Insert menu list.");continue;}
                    if(op=="DETACH"){virtualtv::detach_one_output(chosen);continue;}
                    const auto assigned=virtualtv::output_assignments_snapshot();
                    if(std::any_of(assigned.begin(),assigned.end(),[&](const auto& item){return virtualtv::same_placed_object(item.first,chosen);})){
                        feedback("Object already has a screen or speaker.");continue;
                    }
                    virtualtv::Screen placement{};recommended_screen(chosen,placement);
                    if(op=="ATTACH_SP"){
                        if(!virtualtv::request_add_output(chosen,virtualtv::OutputKind::Speaker,placement))feedback("Speaker could not be added (limit or duplicate).");
                    }else{
                        const auto primary=virtualtv::scene_snapshot();
                        const bool has_primary=primary.attached || virtualtv::current_screen_mode()==virtualtv::ScreenMode::SyncedWorld;
                        if(!has_primary){virtualtv::set_screen_for(chosen,placement);virtualtv::request_select(chosen);virtualtv::request_attach();}
                        else if(!virtualtv::request_add_output(chosen,virtualtv::OutputKind::Screen,placement))feedback("Screen could not be added (limit or duplicate).");
                        user_hid_screen=false;virtualtv::set_screen_mode(virtualtv::ScreenMode::Attached);
                    }
                }
            }
        }
        if(auto result=prefetch_resolver.take()){
            if(result->ok){preparation.media=std::move(result->media);preparation.completed=now;virtualtv::debug_log("Next playlist video prepared");}
            else virtualtv::debug_log("Next playlist preparation unavailable; normal resolution will be used");
        }
        std::deque<Command> pending;{std::lock_guard lock(control_mutex);pending.swap(commands);}
        for(auto& c:pending){
            if(sync.active&&!sync.owner&&!c.sync_sender&&c.action!=Action::Volume&&c.action!=Action::SyncLeave&&c.action!=Action::SyncRetry)continue;
            if(c.sync_sender){std::lock_guard lock(control_mutex);if(!ui_sync_enabled.load()||!receive_selection.accepts(c.sync_sender)||!sync.active||sync.controller!=c.sync_sender||sync.room!=c.sync_room||sync.revision!=c.sync_revision)continue;}

            if(c.action==Action::LoadPlaylist){
                if(!virtualtv::youtube_playlist_url(c.text)){feedback("Enter a YouTube playlist URL containing list=.");continue;}
                invalidate_preparation();cached_playing=false;resolver.cancel();resolving_url=c.text;resolving_playlist=true;
                ui_playback_state.store(3);
                publish_playlist("Loading playlist entries (no videos downloaded)...");
                feedback("Loading YouTube playlist...");resolver.start(directory,c.text,true);
            }else if(c.action==Action::PlaylistRepeat){
                invalidate_preparation();playlist_repeat=!playlist_repeat;
                publish_playlist(playlist_repeat?"Playlist repeat enabled.":"Playlist repeat disabled.");
            }else if(c.action==Action::PlaylistClear){
                resolver.cancel();resolving_url.clear();resolving_playlist=false;
                invalidate_preparation();playlist_entries.clear();playlist_index=-1;consecutive_failures=0;skipped_videos=0;
                publish_playlist("Playlist cleared.");
            }else if(c.action==Action::PlaylistSelect){
                if(sync.active&&!sync.owner)continue;
                if(c.playlist_target<0||c.playlist_target>=static_cast<int>(playlist_entries.size()))continue;
                if(c.playlist_target==playlist_index)continue;
                consecutive_failures=0;skipped_videos=0;
                select_playlist(c.playlist_target);
            }else if(c.action==Action::PlaylistNext||c.action==Action::PlaylistPrevious){
                if(sync.active&&!sync.owner)continue; // Viewers cannot change the host's queue.
                const int target=playlist_index+(c.action==Action::PlaylistNext?1:-1);
                if(!select_playlist(target)){
                    if(c.action==Action::PlaylistNext){
                        if(sync.active&&sync.owner)send_sync(Kind::stop);
                        invalidate_preparation();cached_playing=false;reset_sync();player.stop();ui_playback_state.store(0);watching_playback=false;
                        publish_playlist("Playlist finished.");feedback("Playlist finished.");
                    }
                }
            }else if(c.action==Action::Play){
                if(virtualtv::youtube_playlist_url(c.text)&&!c.from_playlist){enqueue({Action::LoadPlaylist,c.text});continue;}
                std::optional<virtualtv::ResolvedMedia> prepared;
                const int cache_target=c.sync_sender?c.playlist_target:playlist_index;
                const auto cache_room=sync.active?sync.room:0;
                const auto cache_revision=sync.active?(sync.owner?sync.revision+1:sync.revision):0;
                if(c.from_playlist&&preparation.matches(c.text,cache_target,cache_room,cache_revision,now)&&preparation.media)prepared=std::move(preparation.media);
                invalidate_preparation();cached_playing=false;
                resolver.cancel();resolving_url.clear();resolving_playlist=false;
                if(!c.from_playlist){playlist_entries.clear();playlist_index=-1;consecutive_failures=0;skipped_videos=0;publish_playlist("Single video mode.");}
                if(sync.active&&sync.owner){
                    // Each playlist/video transition is a new, revision-scoped media offer.
                    virtualtv::sync::Offer offer{c.text,sync.placement,sync.scale,sync.screen};
                    if(!playlist_entries.empty()&&playlist_index>=0){
                        offer.playlist_index=static_cast<std::uint16_t>(playlist_index);
                        offer.playlist_total=static_cast<std::uint16_t>(playlist_entries.size());
                        offer.playlist_repeat=playlist_repeat;
                    }
                    auto payload=virtualtv::sync::encode_offer(offer);
                    if(!payload){sync_feedback("Cannot share this video URL. Playback remains local.");}
                    else{
                        ++sync.revision;sync.media_url=c.text;sync.offer_payload=*payload;
                        sync.last_offer=now;sync.local_ready=false;sync.ready_sent=false;
                        sync.remote_ready=false;sync.viewer_ids.clear();sync.viewers=0;ui_sync_ready_viewers=0;
                        sync.prepare_media=true;sync.waiting_decode=false;sync.start_pending=false;
                        sync.started=false;sync.ever_started=false;sync.can_schedule=true;
                        send_sync(Kind::full_offer,0,0,*payload);
                        sync.host_timeline={};sync_feedback("Next video announced. Host continues independently; viewers catch up when ready.");
                    }
                }
                if(prepared){cached_playing=true;cached_playing_url=c.text;begin_playback(*prepared,c.text);}
                else if(c.resolve||virtualtv::youtube_url(c.text)){
                    watching_playback=false;ui_playback_state.store(3);resolving_url=c.text;feedback("Resolving video link... Stop cancels. This can take up to 90 seconds.");
                    if(c.sync_sender)virtualtv::log("Shared video URL resolution started (address omitted)");
                    virtualtv::debug_log("URL resolution started using bundled helper (address omitted)");resolver.start(directory,c.text);
                }else{virtualtv::ResolvedMedia media;media.video=c.text;begin_playback(media,c.text);}
            }else if(c.action==Action::Pause){
                if(ui_playback_state.load()==1&&player.set_paused(true)){
                    ui_playback_state.store(2);feedback("Paused.");virtualtv::log("VLC paused");
                    if(sync.owner&&sync.active){sync.started=false;}
                }
            }else if(c.action==Action::Resume){
                if((!sync.active||sync.owner)&&ui_playback_state.load()==2&&player.set_paused(false)){
                    ui_playback_state.store(1);feedback("Resumed.");virtualtv::log("VLC resumed");
                }
            }else if(c.action==Action::Stop){invalidate_preparation();cached_playing=false;if(sync.owner&&sync.active)send_sync(Kind::stop);reset_sync();resolver.cancel();resolving_url.clear();resolving_playlist=false;watching_playback=false;player.stop();ui_playback_state.store(0);feedback("Stopped.");virtualtv::log("VLC stopped; pending resolution cancelled");}
            else if(c.action==Action::SyncShare){
                const auto scene=virtualtv::scene_snapshot();
                if(!ui_sync_enabled.load())sync_feedback("Enable TV sync first.");
                else if(!bridge.available())sync_feedback("TV sync plugin files not found. Install JishsVirtualTVBridge in Mods.");
                else if(!bridge.online())sync_feedback("TV sync plugin is not responding. Check that it is enabled in Mod Manager.");
                else if(bridge.lobby().reported&&!bridge.lobby().active)sync_feedback("No players in session. Host or join a lobby, then try Share again.");
                else if(!bridge.my_id())sync_feedback("Host or join a lobby and wait for your local multiplayer player ID.");
                else if(!scene.attached&&virtualtv::extra_outputs_snapshot().empty())sync_feedback("Attach a screen or speaker before sharing.");
                else if(resolving_url.size()||ui_playback_state.load()!=1&&ui_playback_state.load()!=2)sync_feedback("Start a video and let it load before sharing.");
                else{
                    std::ifstream input(directory/L"JishsVirtualTV.url",std::ios::binary);
                    std::string source;std::getline(input,source,'\0');
                    virtualtv::sync::Offer offer{source,scene.placement,scene.scale,scene.screen};
                    if(!playlist_entries.empty()&&playlist_index>=0){
                        offer.playlist_index=static_cast<std::uint16_t>(playlist_index);
                        offer.playlist_total=static_cast<std::uint16_t>(playlist_entries.size());
                        offer.playlist_repeat=playlist_repeat;
                    }
                    auto bytes=virtualtv::sync::encode_offer(offer);
                    if(!bytes)sync_feedback("TV URL or placement cannot be shared.");
                    else{
                        invalidate_preparation();reset_sync();sync.active=true;sync.session_epoch=connection_epoch;sync.owner=true;sync.local_ready=true;sync.room=(now<<16)^(bridge.my_id()&0xffff);sync.controller=bridge.my_id();sync.offer_payload=*bytes;sync.last_offer=now;sync.can_schedule=true;
                        sync.placement=scene.placement;sync.scale=scene.scale;sync.screen=scene.screen;
                        full_setup.host(sync.room,saved_audio);
                        sync.started=player.playback_state()==3;sync.ever_started=sync.started;
                        virtualtv::log("TV sync sharing started");ui_sync_active=true;ui_sync_host=true;
                        send_sync(Kind::full_offer,0,0,*bytes);
                        sync_feedback("Setup shared. Host playback is unchanged; viewers catch up individually.");
                    }
                }
            }else if(c.action==Action::SyncRetry){full_setup.retry(now);sync.join.retry();
            }else if(c.action==Action::SyncLeave){
                resolver.cancel();resolving_url.clear();invalidate_preparation();cached_playing=false;
                if(sync.active&&!sync.owner){player.stop();watching_playback=false;ui_playback_state.store(0);}
                {std::lock_guard lock(control_mutex);receive_selection.choose(0);}
                if(sync.owner&&sync.active)send_sync(Kind::stop);
                if(!sync.owner&&sync.active)virtualtv::set_screen_mode(virtualtv::ScreenMode::Hidden);
                reset_sync();virtualtv::log("TV sync left");sync_feedback("Left TV sync.");
            }
            else if(c.action==Action::SaveAsset){
                preset_feedback(save_asset_preset(c.asset_key,c.screen)?"Asset preset saved. New copies of this object will use it.":"Could not save asset preset. See the TV log.");
            }else if(c.action==Action::SetAutoAsset){
                preset_feedback(set_asset_auto_attach(c.asset_key,c.auto_attach)?"Auto-attach preference saved for this asset.":"Could not save auto-attach preference.");
            }else if(c.action==Action::RemoveAsset){
                preset_feedback(remove_asset_preset(c.asset_key)?"Personal asset preset removed. Shipped defaults still apply.":"No personal preset to remove, or could not write file.");
            }else if(c.action==Action::ExportDefaults){
                preset_feedback(export_asset_presets()?"Preset pack exported to JishsVirtualTV.defaults. Ship it beside the DLL.":"Could not export preset pack. See the TV log.");
            }
            else if(c.action==Action::Audio){
                saved_audio=c.audio;player.volume(saved_audio.volume);
                const auto original=virtualtv::scene_snapshot();
                save(original.attached?original.screen:saved_screen,saved_audio);
                virtualtv::debug_log("TV audio settings: directional="+std::to_string(saved_audio.directional)+" range_m="+std::to_string(saved_audio.range)+" volume="+std::to_string(saved_audio.volume));
            }else if(c.action==Action::Volume){
                saved_audio.volume=c.volume;player.volume(c.volume);
                const auto original=virtualtv::scene_snapshot();
                save(original.attached?original.screen:saved_screen,saved_audio);
            }else{
                saved_screen=c.screen;
                save_object_profile(c.target,c.screen);
                const auto original=virtualtv::scene_snapshot();
                // Saving another output's offset must not replace the primary
                // object's persisted alignment and restore identity.
                if(!original.attached || c.target==original.identity)save(c.screen,saved_audio);
                feedback(c.target.instance?"Alignment saved for this object.":"Default alignment saved.");
            }
        }
        if(auto result=resolver.take()){
            if(resolving_playlist){
                resolving_playlist=false;
                if(result->ok&&result->playlist){
                    playlist_entries=std::move(result->entries);playlist_index=-1;consecutive_failures=0;skipped_videos=0;
                    publish_playlist("Loaded "+std::to_string(playlist_entries.size())+" playlist videos.");
                    select_playlist(0);
                }else{
                    feedback(result->error);publish_playlist("Playlist loading failed: "+result->error);
                    if(!watching_playback)ui_playback_state.store(0);
                }
            }else if(result->ok){
                virtualtv::debug_log(std::string("URL resolution succeeded; separate_audio=")+(result->media.audio.empty()?"0":"1"));
                if(sync.active&&!sync.owner)virtualtv::log("Shared video URL resolution succeeded");
                begin_playback(result->media,resolving_url);
            }else{
                ui_playback_state.store(0);feedback(result->error);
                virtualtv::log("URL resolution failed: "+result->error+" exit_code="+std::to_string(result->exit_code));
                if(sync.active&&!sync.owner)sync_feedback("Shared video resolution failed: "+result->error);
                if(!playlist_entries.empty()&&playlist_index>=0&&(sync.owner||!sync.active)){
                    ++consecutive_failures;
                    if(consecutive_failures<playlist_entries.size()){
                        ++skipped_videos;
                        feedback("Skipping unavailable playlist video: "+result->error);
                        enqueue({Action::PlaylistNext});
                    }else{feedback("All playlist entries failed to load.");publish_playlist("No playable entries found.");}
                }
            }
            resolving_url.clear();
        }
        if(watching_playback&&now>=next_state){next_state=now+500;const int state=player.playback_state();if(state!=previous_state){previous_state=state;virtualtv::debug_log("VLC playback state="+std::to_string(state)+" audio_tracks="+std::to_string(player.audio_track_count()));if(state==3){playback_reached=true;consecutive_failures=0;}
                    else if(state==4)playback_reached=true;
                    if(playback_reached&&(state==5||state==6)){
                        ui_playback_state.store(0);watching_playback=false;
                        if(state==6&&!playlist_entries.empty()&&(sync.owner||!sync.active))enqueue({Action::PlaylistNext});
                    }
                    if(state==7){
                        if(cached_playing){cached_playing=false;if(sync.active&&!sync.started){sync.prepare_media=true;sync.local_ready=false;sync.ready_sent=false;sync.waiting_decode=false;}watching_playback=false;resolving_url=cached_playing_url;resolver.start(directory,resolving_url);ui_playback_state.store(3);feedback("Refreshing prepared video link...");continue;}

                        ui_playback_state.store(0);feedback("Video unavailable or stream rejected; skipping if another playlist item is available.");watching_playback=false;
                        if(!playlist_entries.empty()&&(sync.owner||!sync.active)){
                            if(++consecutive_failures<playlist_entries.size()){
                                ++skipped_videos;enqueue({Action::PlaylistNext});
                            }else publish_playlist("All playlist entries failed to play.");
                        }
                    }}}
        if(watching_playback&&playback_reached&&!resolving_playlist&&resolving_url.empty()&&(!sync.active||sync.owner)&&!playlist_entries.empty()&&playlist_index>=0){
            int next=playlist_index+1;
            if(next>=static_cast<int>(playlist_entries.size()))next=playlist_repeat?0:-1;
            if(next>=0){
                begin_preparation(playlist_entries[next].url,next,sync.active?sync.room:0,sync.active?sync.revision+1:0);
                if(sync.active&&sync.owner&&(!prefetch_announced||now-prefetch_announced>=8000)){
                    virtualtv::sync::Offer next_offer{playlist_entries[next].url,sync.placement,sync.scale,sync.screen};
                    next_offer.playlist_index=static_cast<std::uint16_t>(next);next_offer.playlist_total=static_cast<std::uint16_t>(playlist_entries.size());next_offer.playlist_repeat=playlist_repeat;
                    if(auto payload=virtualtv::sync::encode_preparation(next_offer)){
                        bridge.send(Packet{Kind::prepare,sync.room,sync.revision+1,bridge.my_id(),0,0,*payload});prefetch_announced=now;
                    }
                }
            }
        }
        const auto identity=virtualtv::selected_identity();if(!(identity==last)){
            last=identity;
            virtualtv::Screen specific;
            saved_screen=recommended_screen(identity,specific)?specific:virtualtv::Screen{};
            // Selecting an object is not reattaching the primary screen.
            // Do not persist its draft alignment as the main screen's profile.
        }
        const auto mode=virtualtv::current_screen_mode();
        // Audio emitters exist independently of rendering. Hidden screens,
        // restored speakers and speaker-only sessions must not mute VLC just
        // because no primary TV quad is being drawn. All share one decoder.
        const auto scene=virtualtv::scene_snapshot();
        std::vector<virtualtv::Vec3> audio_emitters;
        audio_emitters.reserve(1+virtualtv::max_world_speakers);
        const auto speakers=virtualtv::extra_outputs_snapshot();
        const bool shared_viewer=sync.active&&!sync.owner;
        if(shared_viewer){
            for(const auto& o:virtualtv::borrowed_outputs_snapshot())if(o.primary||o.anchor.kind==virtualtv::OutputKind::Speaker)
                audio_emitters.push_back(virtualtv::audio_emitter(o.anchor.placement,o.anchor.scale,o.anchor.screen));
        }else if(scene.attached){
            audio_emitters.push_back(virtualtv::audio_emitter(scene.placement,scene.scale,scene.screen));
        }
        if(!shared_viewer)for(const auto& output:speakers){
            if(output.kind==virtualtv::OutputKind::Speaker)
                audio_emitters.push_back(virtualtv::audio_emitter(output.placement,output.scale,output.screen));
        }
        // ReSkate continues publishing the camera even without a TV attached.
        // If it isn't ready yet, avoid silencing an already playing audio link.
        const bool camera_fresh=scene.camera.valid && now>=scene.at && now-scene.at<250;
        const auto& m=scene.camera.world;
        const virtualtv::Vec3 ear{m[12],m[13],m[14]},right{m[0],m[1],m[2]};
        const auto matrix=shared_viewer&&sync.join.mute()?std::array<float,4>{}:virtualtv::route_tv_audio(shared_viewer?full_setup.directional():saved_audio.directional,
            mode==virtualtv::ScreenMode::Front,camera_fresh,
            ear,right,audio_emitters,shared_viewer?full_setup.range():saved_audio.range,
            [](virtualtv::Vec3 ear,virtualtv::Vec3 right,virtualtv::Vec3 emitter,float range){
                return virtualtv::audio_matrix(ear,right,emitter,range);
            },shared_viewer);
        float distance=-1;
        if(camera_fresh && !audio_emitters.empty()){
            const auto& emitter=audio_emitters.front();
            const float dx=emitter.x-ear.x,dy=emitter.y-ear.y,dz=emitter.z-ear.z;
            distance=std::sqrt(dx*dx+dy*dy+dz*dz);
        }
        player.audio_target(matrix);
        if(watching_playback&&((!first_audio&&player.audio_active())||(virtualtv::debug_logging()&&now>=next_audio_log))){
            if(!first_audio&&player.audio_active()){first_audio=true;virtualtv::log("VLC audio session detected (output remains owned by VLC)");}
            next_audio_log=now+5000;if(virtualtv::debug_logging())virtualtv::log("TV audio: "+player.audio_diagnostics()+" mode="+std::to_string(static_cast<int>(mode))+" distance_m="+std::to_string(distance)+" range_m="+std::to_string(saved_audio.range)+" left="+std::to_string(matrix[0]+matrix[1])+" right="+std::to_string(matrix[2]+matrix[3]));
        }
        // Insert now provides only a native menu launcher. No large playlist/object
        // snapshot is assembled or written to plugin files on this worker tick.
        Sleep(20);
    }
    // Worker-owned cleanup, outside DllMain/loader lock. Keep installed trampolines
    // valid until process exit; disabling callbacks makes the renderer quiescent.
    virtualtv::activate_host_callbacks(false);virtualtv::depth::live::shutdown();
    resolver.cancel();prefetch_resolver.cancel();bridge.clear_pending();full_setup.clear();
    {std::lock_guard lock(control_mutex);commands.clear();ui_playlist_entries.clear();ui_sync_peers.clear();receive_selection.clear();}
    const auto saved_attachments=virtualtv::persistent_outputs_snapshot();
    if(saved_attachments.first!=written_revision)write_attachment_manifest(saved_attachments.second);
    player.stop();virtualtv::stop_native();virtualtv::stop_renderer();player.shutdown();
    virtualtv::log("VirtualTV workers stopped; resolver jobs, media and rendering resources released");virtualtv::close_log();return 0;
}
}
namespace virtualtv {
void draw_tv_controls(const dingosdk::overlay::Model& model){
    ImGui::PushID("JishsVirtualTV");
    static ScreenMode visible_before_hide=ScreenMode::Attached;
    static ScreenMode visible_before_preview=ScreenMode::Hidden;
    // Visibility restoration is handled by the background worker even when
    // this F8 panel is closed; this UI still records an explicit Hide choice.
    static Identity shown_identity{};
    const auto active_identity=selected_identity();
    // Match once per placed-object identity; the saved asset key survives
    // subsequent editor moves, when position-based matching is impossible.
    if(asset_for(active_identity).empty())observe_asset(active_identity,model);
    const auto active_asset=asset_for(active_identity);
    static std::string shown_asset_key;
    if(!(active_identity==shown_identity) ||
       (!alignment_dirty && active_asset!=shown_asset_key)){
        shown_asset_key=active_asset;
        // Finish edits for the PREVIOUS object before selecting a new profile.
        if(alignment_dirty){Command command{Action::Save,{},0,screen};command.target=shown_identity;
            enqueue(std::move(command));alignment_dirty=false;}
        shown_identity=active_identity;
        Screen restored=Screen{};
        bool used_live_alignment=false;
        const auto primary=scene_snapshot();
        if(primary.attached && primary.identity==active_identity){
            restored=primary.screen;used_live_alignment=true;
        }else for(const auto& output:extra_outputs_snapshot()){
            if(output.identity==active_identity){restored=output.screen;used_live_alignment=true;break;}
        }
        if(!used_live_alignment&&active_identity.instance)
            recommended_screen(active_identity,restored);
        screen=restored;set_screen_for(active_identity,screen);
        const auto q=screen.rotation;constexpr float d=180.f/3.14159265359f;
        angles={std::atan2(2*(q.w*q.x+q.y*q.z),1-2*(q.x*q.x+q.y*q.y))*d,
                std::asin(std::clamp(2*(q.w*q.y-q.z*q.x),-1.f,1.f))*d,
                std::atan2(2*(q.w*q.z+q.x*q.y),1-2*(q.y*q.y+q.z*q.z))*d};
    }
    // The Insert-menu plugin requests native text entry instead of ReSkate's
    // 256-character Luau dialog. All URL text remains inside this DLL, then
    // uses exactly the same playback / playlist command queue as the F8 menu.
    static int url_dialog_kind=0;
    static bool focus_url_dialog=false;
    static std::string native_url_error;
    if(const int kind=native_url_dialog_request.exchange(0)){
        url_dialog_kind=kind;
        focus_url_dialog=true;
        native_url_error.clear();
        ImGui::OpenPopup("Paste media link##vtv-native-url");
    }
    if(ImGui::BeginPopupModal("Paste media link##vtv-native-url",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
        ImGui::TextUnformatted(url_dialog_kind==2?"YouTube playlist URL":"Video / stream / YouTube URL");
        ImGui::TextDisabled("Paste a URL below (up to 4095 bytes; not limited by Luau).");
        ImGui::SetNextItemWidth(540.0f);
        if(focus_url_dialog){ImGui::SetKeyboardFocusHere();focus_url_dialog=false;}
        const bool enter=ImGui::InputText("##native-url-entry",url.data(),url.size(),ImGuiInputTextFlags_EnterReturnsTrue);
        if(!native_url_error.empty())ImGui::TextWrapped("%s",native_url_error.c_str());
        ImGui::Separator();
        const bool submit=ImGui::Button(url_dialog_kind==2?"Load playlist":"Play link")||enter;
        ImGui::SameLine();
        const bool cancel=ImGui::Button("Cancel");
        if(submit){
            std::string text=url.data();
            if(text.empty()||!media_http_url(text))native_url_error="Enter a valid http:// or https:// link.";
            else if(url_dialog_kind==2&&!youtube_playlist_url(text))native_url_error="Paste a YouTube playlist link containing list=.";
            else if(ui_sync_active.load()&&!ui_sync_host.load())native_url_error="Only the host can change shared TV playback.";
            else {
                if(url_dialog_kind==2)enqueue({Action::LoadPlaylist,std::move(text)});
                else {Command c{Action::Play,std::move(text)};c.resolve=youtube_url(c.text);enqueue(std::move(c));}
                ImGui::CloseCurrentPopup();url_dialog_kind=0;
                host_close_plugin_url();
            }
        }
        if(cancel){ImGui::CloseCurrentPopup();url_dialog_kind=0;host_close_plugin_url();}
        ImGui::EndPopup();
    }
    if(ImGui::CollapsingHeader("Virtual TV",ImGuiTreeNodeFlags_DefaultOpen)){
        ImGui::TextUnformatted("Video");
        ImGui::SetNextItemWidth(-1);ImGui::InputText("##URL",url.data(),url.size());
        static bool resolve_webpage=false;
        ImGui::Checkbox("Resolve webpage links (YouTube automatic)",&resolve_webpage);
        ImGui::BeginDisabled(ui_sync_active.load()&&!ui_sync_host.load());
        if(ImGui::Button("Play new video / playlist")){
            std::string text=url.data();if(media_http_url(text)){Command command{Action::Play,std::move(text)};command.resolve=resolve_webpage;enqueue(std::move(command));}else feedback("Enter an http:// or https:// video URL.");
        }
        ImGui::EndDisabled();
        const int state=ui_playback_state.load();
        ImGui::SameLine();ImGui::BeginDisabled((state!=1&&state!=2)||(ui_sync_active.load()&&!ui_sync_host.load()));
        if(ImGui::Button(state==2?"Resume":"Pause"))enqueue({state==2?Action::Resume:Action::Pause});
        ImGui::EndDisabled();
        ImGui::SameLine();ImGui::BeginDisabled(ui_sync_active.load()&&!ui_sync_host.load());
        if(ImGui::Button("Stop"))enqueue({Action::Stop});ImGui::EndDisabled();
        {std::lock_guard lock(control_mutex);ImGui::TextWrapped("%s",playback_status.c_str());}
        if(ImGui::CollapsingHeader("Playlist queue",ImGuiTreeNodeFlags_DefaultOpen)){
            ImGui::BeginDisabled(ui_sync_active.load()&&!ui_sync_host.load());
            if(ImGui::Button("Load playlist from URL")){
                if(youtube_playlist_url(std::string(url.data())))enqueue({Action::LoadPlaylist,std::string(url.data())});
                else feedback("Paste a YouTube playlist URL with list= first.");
            }
            std::vector<virtualtv::PlaylistEntry> entries;int index{};bool repeat{};std::string status;
            {std::lock_guard lock(control_mutex);entries=ui_playlist_entries;index=ui_playlist_index;repeat=ui_playlist_repeat;status=ui_playlist_status;}
            if(!entries.empty()){
                ImGui::Text("Video %d of %d",index+1,static_cast<int>(entries.size()));
                if(index>=0&&index<static_cast<int>(entries.size()))ImGui::TextWrapped("%s",entries[index].title.c_str());
                if(ImGui::Button("Previous"))enqueue({Action::PlaylistPrevious});
                ImGui::SameLine();if(ImGui::Button("Next"))enqueue({Action::PlaylistNext});
                ImGui::SameLine();if(ImGui::Button(repeat?"Repeat: On":"Repeat: Off"))enqueue({Action::PlaylistRepeat});
                ImGui::SameLine();if(ImGui::Button("Clear queue"))enqueue({Action::PlaylistClear});
                if(ImGui::TreeNodeEx("Browse loaded songs",ImGuiTreeNodeFlags_DefaultOpen)){
                    ImGui::TextDisabled("Select a song to play it now. The host controls shared playback.");
                    if(ImGui::BeginChild("loaded_playlist_tracks",ImVec2(0,200),ImGuiChildFlags_Borders)){
                        ImGuiListClipper clipper;clipper.Begin(static_cast<int>(entries.size()));
                        while(clipper.Step())for(int i=clipper.DisplayStart;i<clipper.DisplayEnd;++i){
                            ImGui::PushID(i);
                            const std::string title=std::to_string(i+1)+". "+entries[static_cast<std::size_t>(i)].title;
                            if(ImGui::Selectable(title.c_str(),index==i)){
                                Command chosen{Action::PlaylistSelect};chosen.playlist_target=i;enqueue(std::move(chosen));
                            }
                            ImGui::PopID();
                        }
                    }
                    ImGui::EndChild();ImGui::TreePop();
                }
                ImGui::TextDisabled("Host selects videos; viewers start together once, without seek correction.");
            }
            if(!status.empty())ImGui::TextWrapped("%s",status.c_str());
            ImGui::EndDisabled();
        }

        if(ImGui::CollapsingHeader("Multiplayer TV sync")){
            static bool opt_in=false;
            if(ImGui::Checkbox("Enable TV sync",&opt_in)){ui_sync_enabled.store(opt_in);if(!opt_in){std::lock_guard lock(control_mutex);receive_selection.choose(0);}}
            {std::lock_guard lock(control_mutex);
                ImGui::TextWrapped("Allow players below, then choose one broadcaster to watch. Enabling sync alone accepts no screens.");
                if(ImGui::RadioButton("Watch nobody",receive_selection.active==0))receive_selection.choose(0);
                for(const auto& peer:ui_sync_peers){
                    const auto key=std::to_string(peer.id);ImGui::PushID(key.c_str());
                    bool allowed=receive_selection.allowed.count(peer.id)!=0;
                    if(ImGui::Checkbox("Allow",&allowed))receive_selection.allow(peer.id,allowed);
                    ImGui::SameLine();ImGui::BeginDisabled(!allowed);
                    if(ImGui::RadioButton("Watch",receive_selection.active==peer.id))receive_selection.choose(peer.id);
                    ImGui::EndDisabled();ImGui::SameLine();ImGui::TextUnformatted(peer.name.c_str());ImGui::PopID();
                }
                if(ui_sync_peers.empty())ImGui::TextDisabled("Waiting for session player list from the updated bridge.");
            }
            ImGui::TextWrapped("%s",ui_sync_plugin.load()?"TV sync plugin: running.":ui_sync_bridge.load()?"TV sync plugin: files found; waiting for a response. Check Mod Manager.":"TV sync plugin: files not found. Install JishsVirtualTVBridge in Mods.");
            virtualtv::sync::LobbyInfo lobby;
            lobby.active=ui_session_active.load();lobby.hosting=ui_session_host.load();
            lobby.reported=ui_session_known.load();lobby.others=ui_session_others.load();
            const auto connection=virtualtv::sync::lobby_status(ui_sync_plugin.load(),lobby,ui_sync_online.load()?1:0);
            ImGui::TextWrapped("%s",connection.c_str());
            if(ui_sync_active.load()){
                if(ui_sync_host.load()){
                    if(lobby.others==0)ImGui::TextWrapped("Hosting TV sync: ready, waiting for players. Share your lobby code; guests need the TV DLL and bridge plugin.");
                    else ImGui::TextWrapped("Hosting TV sync: %u other player(s) in session; %u TV viewer(s) ready. Host playback continues; viewers catch up individually.",lobby.others,ui_sync_ready_viewers.load());
                }else ImGui::TextWrapped("TV viewer: connecting to the host's playback; the video will start when ready.");
            }else if(lobby.reported&&lobby.active){
                ImGui::TextDisabled("To share: attach screens or speakers, start a video, enable sync, then share the setup.");
            }
            {std::lock_guard lock(control_mutex);if(!sync_status.empty())ImGui::TextWrapped("%s",sync_status.c_str());}
            ImGui::BeginDisabled(!ui_sync_enabled.load()||!ui_sync_bridge.load()||ui_sync_active.load());
            if(ImGui::Button("Share screen / speaker setup"))enqueue({Action::SyncShare});
            ImGui::EndDisabled();
            if(ui_sync_active.load()){
                ImGui::SameLine();if(ImGui::Button("Leave sync"))enqueue({Action::SyncLeave});
                ImGui::TextDisabled("Only viewers who allow and select you can watch.");
                if(!ui_sync_host.load()&&ImGui::Button("Retry sync"))enqueue({Action::SyncRetry});
            }
        }
        {std::lock_guard lock(control_mutex);if(!setup_status.empty())ImGui::TextWrapped("%s",setup_status.c_str());}
        const bool borrowed=borrowed_presentation_active();
        ImGui::BeginDisabled(borrowed);
        ImGui::Separator();ImGui::TextUnformatted("Screen");
        const auto current_mode=current_screen_mode();
        if(ImGui::Button(current_mode==ScreenMode::Hidden?"Show":"Hide")){
            if(current_mode==ScreenMode::Hidden){
                set_screen_mode(visible_before_hide);user_hid_screen=false;
            }else{
                visible_before_hide=current_mode;user_hid_screen=true;
                set_screen_mode(ScreenMode::Hidden);
            }
        }
        ImGui::TextWrapped("%s",utf8(attachment_status()).c_str());
        const auto assigned_outputs=output_assignments_snapshot();
        const auto counted_outputs=extra_outputs_snapshot();
        const bool has_primary=scene_snapshot().attached || current_screen_mode()==ScreenMode::SyncedWorld;
        const unsigned additional_screens=static_cast<unsigned>(std::count_if(counted_outputs.begin(),counted_outputs.end(),[](const OutputAnchor& o){return o.kind==OutputKind::Screen;}));
        const unsigned speakers=static_cast<unsigned>(counted_outputs.size())-additional_screens;
        ImGui::Text("Active screens: %u / %u    Active speakers: %u / %u",additional_screens+(has_primary?1:0),max_world_screens,speakers,max_world_speakers);
        ImGui::TextDisabled("[TV] = screen, [SP] = speaker; each object supports one output.");
        bool auto_speakers=auto_speakers_enabled();
        if(ImGui::Checkbox("New objects become speakers",&auto_speakers))set_auto_speakers(auto_speakers);
        ImGui::TextDisabled("New local placements only; temporary per session. Requires directional audio.");
        if(ImGui::CollapsingHeader("Attach to placed object")){
            auto nearby=nearby_snapshot();ImGui::TextWrapped("%s",utf8(nearby.status).c_str());
            static Identity chosen{};static ImGuiTextFilter filter;filter.Draw("Filter objects");
            if(ImGui::BeginChild("nearby",ImVec2(0,150),ImGuiChildFlags_Borders)){
                for(const auto& object:nearby.objects){const auto label=object_label(object,model,assigned_outputs);if(!filter.PassFilter(label.c_str()))continue;
                    ImGui::PushID(static_cast<int>(object.identity.entity));
                    if(ImGui::Selectable(label.c_str(),object.identity==chosen)){chosen=object.identity;request_select(chosen);}
                    ImGui::PopID();
                }
            }ImGui::EndChild();
            const bool listed=std::any_of(nearby.objects.begin(),nearby.objects.end(),[&](const auto& o){return o.identity==chosen;});
            const auto attached_it=std::find_if(assigned_outputs.begin(),assigned_outputs.end(),[&](const auto& assignment){return virtualtv::same_placed_object(assignment.first,chosen);});
            const bool already_assigned=attached_it!=assigned_outputs.end();
            if(already_assigned){
                ImGui::TextDisabled("Selected object: %s attached",attached_it->second==OutputKind::Screen?"[TV] screen":"[SP] speaker");
                if(ImGui::Button("Detach selected object"))detach_one_output(chosen);
            }
            ImGui::BeginDisabled(!listed||already_assigned||(additional_screens+(has_primary?1u:0u)>=max_world_screens));
            if(ImGui::Button(has_primary?"Add another screen":"Attach first screen")){
                Screen placement=screen;
                if(!(active_identity==chosen)){
                    if(!recommended_screen(chosen,placement))placement=Screen{};
                }
                if(!has_primary){set_screen_for(chosen,placement);request_select(chosen);request_attach();set_screen_mode(ScreenMode::Attached);}
                else if(!request_add_output(chosen,OutputKind::Screen,placement))
                    preset_feedback("Screen not added: object already assigned or 64-screen limit reached.");
                else if(current_screen_mode()==ScreenMode::Hidden)set_screen_mode(ScreenMode::Attached);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!listed||already_assigned||speakers>=max_world_speakers);
            if(ImGui::Button("Add speaker")){
                Screen placement=screen;
                if(!(active_identity==chosen))if(!recommended_screen(chosen,placement))placement=Screen{};
                if(!request_add_output(chosen,OutputKind::Speaker,placement))
                    preset_feedback("Speaker not added: object already assigned or 64-speaker limit reached.");
            }
            ImGui::EndDisabled();
            ImGui::SameLine();if(ImGui::Button("Detach all")){
                clear_persistent_outputs();request_detach();set_screen_mode(ScreenMode::Hidden);user_hid_screen=true;
            }
            ImGui::TextDisabled("All screens share one video and playback clock. Objects follow their own placement transforms.");
            if(ImGui::TreeNode("Manage attached screens and speakers")){
                const auto primary=scene_snapshot();
                if(primary.attached){
                    ImGui::PushID("primary_tv");
                    ImGui::Text("[TV] Primary screen | object #%u",primary.identity.drop);
                    ImGui::SameLine();
                    if(ImGui::SmallButton("Detach"))detach_one_output(primary.identity);
                    ImGui::PopID();
                }
                for(const auto& output:counted_outputs){
                    ImGui::PushID(static_cast<int>(output.identity.entity));
                    ImGui::PushID(static_cast<int>(output.kind));
                    ImGui::Text("[%s] %s | object #%u",output.kind==OutputKind::Screen?"TV":"SP",
                                output.kind==OutputKind::Screen?"Screen":"Speaker",output.identity.drop);
                    ImGui::SameLine();
                    if(ImGui::SmallButton("Detach"))detach_one_output(output.identity);
                    ImGui::PopID();ImGui::PopID();
                }
                // Not-yet-spawned placements remain saved for reattachment.
                // They can also be deleted individually without disturbing live outputs.
                const auto [manifest_revision,saved_assignments]=persistent_outputs_snapshot();
                (void)manifest_revision;
                for(const auto& saved:saved_assignments){
                    bool found=primary.attached&&primary.identity.instance==saved.instance&&primary.identity.drop==saved.drop;
                    for(const auto& output:counted_outputs)
                        if(output.identity.instance==saved.instance&&output.identity.drop==saved.drop)found=true;
                    if(found)continue;
                    ImGui::PushID(static_cast<int>(saved.instance));ImGui::PushID(static_cast<int>(saved.drop));
                    if(saved.asset.empty()||!saved.has_origin)
                        ImGui::TextDisabled("[%s] Legacy #%u: reattach once to save name and position",saved.role==2?"SP":"TV",saved.drop);
                    else
                        ImGui::TextDisabled("[%s] %s at (%.1f, %.1f, %.1f) - waiting",saved.role==2?"SP":"TV",saved.asset.c_str(),saved.origin.x,saved.origin.y,saved.origin.z);
                    ImGui::SameLine();
                    Identity offline{};offline.instance=saved.instance;offline.drop=saved.drop;
                    if(ImGui::SmallButton("Forget"))detach_one_output(offline);
                    ImGui::PopID();ImGui::PopID();
                }
                ImGui::TreePop();
            }
        }
        if(ImGui::CollapsingHeader("Screen alignment")){
            bool changed=false;ImGui::SetNextItemWidth(150);changed|=ImGui::DragFloat("Width (m)",&screen.width,.02f,.01f,100,"%.2f");
            ImGui::SetNextItemWidth(150);changed|=ImGui::DragFloat("Height (m)",&screen.height,.02f,.01f,100,"%.2f");
            ImGui::SeparatorText("Screen curvature");
            ImGui::SetNextItemWidth(200);changed|=ImGui::SliderFloat("Horizontal bend (deg)",&screen.horizontal_curve_deg,-160.f,160.f,"%.0f deg");
            ImGui::SetNextItemWidth(200);changed|=ImGui::SliderFloat("Vertical bend (deg)",&screen.vertical_curve_deg,-120.f,120.f,"%.0f deg");
            ImGui::TextDisabled("0 = flat; + bends edges toward local +Z; - toward local -Z.");
            if(ImGui::SmallButton("Reset curvature to flat")){
                if(screen.horizontal_curve_deg!=0||screen.vertical_curve_deg!=0){
                    screen.horizontal_curve_deg=0;screen.vertical_curve_deg=0;changed=true;
                }
            }
            float offset[]{screen.offset.x,screen.offset.y,screen.offset.z};float rotation[]{angles.x,angles.y,angles.z};
            changed|=ImGui::DragFloat3("Local offset (m)",offset,.01f);changed|=ImGui::DragFloat3("Local rotation (degrees)",rotation,.5f);
            if(changed){auto next=screen;next.offset={offset[0],offset[1],offset[2]};next.rotation=euler_degrees({rotation[0],rotation[1],rotation[2]});if(valid(Placement{},next)){screen=next;angles={rotation[0],rotation[1],rotation[2]};set_screen_for(active_identity,screen);}}
            if(changed){alignment_dirty=true;alignment_edit_at=GetTickCount64();}
            // Persist only after dragging settles, not once per render callback.
            if(ImGui::Button("Save alignment")){Command save_cmd{Action::Save,{},0,screen};save_cmd.target=selected_identity();enqueue(std::move(save_cmd));alignment_dirty=false;}
            ImGui::TextDisabled("Placement-specific alignment overrides an asset preset.");
            const std::string asset=asset_for(active_identity);
            if(!asset.empty()){
                ImGui::Separator();
                std::string asset_title=asset;
                if(model.editor.assets)for(const auto& entry:*model.editor.assets)
                    if(presets::normalize(entry.key)==asset){asset_title=entry.title;break;}
                ImGui::TextWrapped("Asset preset for: %s",asset_title.c_str());
                ImGui::TextDisabled("Internal asset key: %s",asset.c_str());
                bool has_preset=false,personal=false,auto_attach=false;Screen profile;
                {std::lock_guard lock(control_mutex);
                    has_preset=presets::find(effective_presets,asset,profile);
                    personal=custom_presets.find(asset)!=custom_presets.end();
                    auto_attach=presets::automatic(effective_auto,asset);}
                ImGui::TextDisabled(has_preset?(personal?"Personal asset preset available":"Bundled asset preset available"):
                                                 "No asset preset saved yet");
                if(ImGui::Button("Save this alignment as asset preset")){
                    Command c{Action::SaveAsset,{},0,screen};c.asset_key=asset;enqueue(std::move(c));
                }
                if(has_preset){
                    ImGui::SameLine();if(ImGui::Button("Apply asset preset")){
                        screen=profile;set_screen_for(active_identity,screen);
                        const auto q=screen.rotation;constexpr float d=180.f/3.14159265359f;
                        angles={std::atan2(2*(q.w*q.x+q.y*q.z),1-2*(q.x*q.x+q.y*q.y))*d,
                                std::asin(std::clamp(2*(q.w*q.y-q.z*q.x),-1.f,1.f))*d,
                                std::atan2(2*(q.w*q.z+q.x*q.y),1-2*(q.y*q.y+q.z*q.z))*d};
                        alignment_dirty=true;alignment_edit_at=GetTickCount64();
                    }
                }
                if(has_preset){
                    bool enabled=auto_attach;
                    if(ImGui::Checkbox("Auto-attach screen on placement",&enabled)){
                        Command c{Action::SetAutoAsset};c.asset_key=asset;c.auto_attach=enabled;enqueue(std::move(c));
                    }
                    ImGui::TextDisabled("Automatically attach a screen whenever a new copy is placed nearby.");
                    ImGui::TextDisabled("Manual detach suppresses auto-attach for that object until the next session.");
                }
                if(personal&&ImGui::Button("Remove personal preset for this asset")){
                    Command c{Action::RemoveAsset};c.asset_key=asset;enqueue(std::move(c));
                }
                ImGui::TextDisabled("Matching future placements use this preset unless saved individually.");
            }else if(active_identity.instance)ImGui::TextDisabled("Asset identity not matched yet. Select a nearby editor object to enable presets.");
            if(ImGui::TreeNode("Ship preset pack with mod")){
                {std::lock_guard lock(control_mutex);
                    ImGui::TextDisabled("Bundled asset presets: %u | Personal asset presets: %u",
                        static_cast<unsigned>(shipped_presets.size()),static_cast<unsigned>(custom_presets.size()));}
                ImGui::TextWrapped("Export all saved asset profiles to JishsVirtualTV.defaults next to the DLL. Include that file in your release; each player can still save personal overrides.");
                if(ImGui::Button("Export preset pack for distribution"))enqueue({Action::ExportDefaults});
                ImGui::TreePop();
            }
            {std::lock_guard lock(control_mutex);if(!preset_status.empty())ImGui::TextWrapped("%s",preset_status.c_str());}
        }
        ImGui::EndDisabled();
        if(ImGui::CollapsingHeader("Audio settings")){
            ImGui::SetNextItemWidth(180);if(ImGui::SliderInt("Volume",&audio_settings.volume,0,100))enqueue({Action::Volume,{},audio_settings.volume});
            if(borrowed){std::lock_guard lock(control_mutex);ImGui::Text("Broadcaster audio: %s, fade-out distance %.0f m",watched_directional?"directional":"stereo",watched_range);ImGui::TextDisabled("Volume stays local. Your audio settings return when you leave sync.");}
            ImGui::BeginDisabled(borrowed);
            bool audio_changed=ImGui::Checkbox("Directional TV audio",&audio_settings.directional);
            ImGui::SetNextItemWidth(230);audio_changed|=ImGui::SliderFloat("Audio fade-out distance (m)",&audio_settings.range,5,1000,"%.0f",ImGuiSliderFlags_Logarithmic);
            if(audio_changed&&valid_audio(audio_settings)){Command c{Action::Audio};c.audio=audio_settings;enqueue(std::move(c));}
            ImGui::EndDisabled();
            ImGui::TextDisabled("Extra speakers operate only with directional audio enabled.");
            ImGui::TextDisabled("One VLC audio stream is spatially mixed at each attached object; no echoing players.");
        }
        ImGui::Separator();
        // Only the two supported user-facing paths are exposed. Keep the
        // underlying legacy enum values for existing internal GPU tests.
        int occlusion_backend=depth::backend()==depth::Backend::ReShadeNative?1:0;
        if(ImGui::Combo("Occlusion path",&occlusion_backend,"Streamline\0ReShade (DX12 scene depth)\0"))
            depth::set_backend(occlusion_backend==0?depth::Backend::StreamlineOnly:depth::Backend::ReShadeNative);
        ImGui::TextDisabled("Streamline automatically switches to ReShade if usable scene depth is missing for 3 seconds.");
        ImGui::TextWrapped("If occlusion is not working, try enabling DLSS in the game settings, then select Streamline again. DLSS may help expose scene depth; it is not guaranteed.");
        if(depth::automatic_fallback_active())ImGui::TextWrapped("Automatically using ReShade: Streamline did not provide usable scene depth.");
        if(ImGui::CollapsingHeader("Advanced",ImGuiTreeNodeFlags_None)){
            static bool debug=debug_logging();
            if(ImGui::Checkbox("Debug mode (detailed logging)",&debug)){
                set_debug_logging(debug);
                log(debug?"Advanced debug logging enabled":"Advanced debug logging disabled");
            }
            ImGui::TextDisabled("Normal mode keeps errors and important events; debug enables expensive diagnostics.");
            if(ImGui::TreeNode("Attachment diagnostics")){
                const auto source=native_object_source_status();
                ImGui::TextWrapped("Native object reader: %s",source.status.c_str());
                ImGui::TextDisabled("Uses ReSkate's native client-realm placement store. No extra game tick hook.");
                ImGui::TreePop();
            }
            if(ImGui::TreeNode("Preview and diagnostics")){
                ImGui::BeginDisabled(borrowed);
                if(ImGui::Button("Front preview")){
                    if(current_screen_mode()!=ScreenMode::Front)visible_before_preview=current_screen_mode();
                    show_test_pattern(false);set_screen_mode(ScreenMode::Front);
                    user_hid_screen=false;
                }
                ImGui::SameLine();if(ImGui::Button("Colour bars")){
                    if(current_screen_mode()!=ScreenMode::Front)visible_before_preview=current_screen_mode();
                    show_test_pattern(true);set_screen_mode(ScreenMode::Front);
                    user_hid_screen=false;
                }
                if(current_screen_mode()==ScreenMode::Front){
                    ImGui::SameLine();
                    if(ImGui::Button("Hide front preview")){
                        show_test_pattern(false);
                        set_screen_mode(visible_before_preview==ScreenMode::Front?ScreenMode::Hidden:visible_before_preview);
                    }
                }
                ImGui::EndDisabled();
                ImGui::TextWrapped("Requested path: %s",depth::backend_name(depth::backend()));
                ImGui::TextWrapped("%s",utf8(renderer_status()).c_str());
                ImGui::TreePop();
            }
            if(depth::backend()==depth::Backend::ReShadeNative && ImGui::TreeNode("ReShade depth settings")){
                int boundary=depth::native::capture_boundary();
                if(ImGui::Combo("Depth capture timing",&boundary,
                    "Any eligible\0After depth-write to read transition\0Before depth unbind\0Before depth clear\0"))
                    depth::native::set_capture_boundary(boundary);
                // Text input intentionally applies only on Enter/Apply. Intermediate
                // values while typing never invalidate GPU depth capture.
                static char near_text[32]{};
                static float displayed_near=-1.f;
                const float configured_near=depth::native::estimated_near_plane();
                if(displayed_near<0.f || std::abs(displayed_near-configured_near)>0.000001f){
                    std::snprintf(near_text,sizeof near_text,"%.6f",configured_near);
                    displayed_near=configured_near;
                }
                auto apply_near=[&](){
                    char* end{};const float value=std::strtof(near_text,&end);
                    if(end==near_text||*end!='\0'||!std::isfinite(value)||value<0.02f||value>0.15f){
                        feedback("Near plane must be a number from 0.020000 to 0.150000 m.");
                        return;
                    }
                    depth::native::set_estimated_near_plane(value);
                    displayed_near=value;
                    std::snprintf(near_text,sizeof near_text,"%.6f",value);
                    std::ofstream stored(directory/L"JishsVirtualTV.render",std::ios::trunc);
                    if(stored)stored<<std::setprecision(9)<<value<<'\n';
                };
                ImGui::SetNextItemWidth(170);
                if(ImGui::InputText("Near plane (m)",near_text,sizeof near_text,ImGuiInputTextFlags_EnterReturnsTrue))apply_near();
                ImGui::SameLine();if(ImGui::Button("Apply near plane"))apply_near();
                ImGui::TextDisabled("Typical tested range: 0.0603 to 0.1000. Saved across restarts.");
                int min_draws=depth::native::minimum_scene_draws();
                if(ImGui::SliderInt("Minimum depth-pass draws",&min_draws,8,512))
                    depth::native::set_minimum_scene_draws(min_draws);
                int manual_depth=depth::native::selected_depth()+1;
                if(ImGui::SliderInt("Depth resource override",&manual_depth,0,8,manual_depth?"Rank %d":"Automatic"))
                    depth::native::select_depth(manual_depth-1);
                if(debug)ImGui::TextWrapped("%s",depth::native::depth_candidates().c_str());
                ImGui::TreePop();
            }
        }
        // Save settled alignment edits even when the alignment panel is collapsed.
        if(alignment_dirty&&GetTickCount64()-alignment_edit_at>=500){
            Command save_cmd{Action::Save,{},0,screen};save_cmd.target=selected_identity();
            enqueue(std::move(save_cmd));alignment_dirty=false;
        }
        ImGui::TextDisabled("F8 toggles this Virtual TV window.");
    }
    ImGui::PopID();
}
}
BOOL WINAPI DllMain(HINSTANCE handle,DWORD reason,LPVOID){
    if(reason==DLL_PROCESS_ATTACH){module_handle=handle;DisableThreadLibraryCalls(handle);HANDLE thread=CreateThread(nullptr,0,start,nullptr,0,nullptr);if(thread)CloseHandle(thread);}return TRUE;
}

