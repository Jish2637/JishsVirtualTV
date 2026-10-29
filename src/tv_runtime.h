#pragma once
#include "tv_geometry.h"
#include "tv_audio.h"
#include "output_limits.h"
#include "attachment_store.h"
#include <windows.h>
#include <array>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <utility>
namespace dingosdk { struct ParkEditorModel; }
namespace virtualtv {
struct Identity {
    std::uint64_t entity{}, instance{}, session{};
    std::uint32_t drop{};
    bool operator==(const Identity& b) const { return entity==b.entity && instance==b.instance && session==b.session && drop==b.drop; }
};
enum class CameraSource { Unknown, NativePoll, Presentation, Recording };
struct Camera {
    std::uint64_t sequence{},sampled_at{};unsigned viewport{};CameraSource source{CameraSource::Unknown};
    std::array<float,16> world{}; float vertical_fov{}; bool valid{};
    // Copied diagnostics only; these fields never affect camera acceptance.
    const char* diagnostic="not_sampled";
    std::uintptr_t active_vtable{};
    unsigned view_count{};
    std::array<float,3> basis_norms{};
};
struct Scene { Camera camera; Placement placement; Vec3 scale{1,1,1}; Screen screen; Identity identity; bool attached{}; ULONGLONG at{}; };
struct Settings { Screen screen; Identity saved; AudioSettings audio; };
// One decoded VLC stream feeds all screens and speaker emitters.
enum class OutputKind { Screen, Speaker };
struct OutputAnchor { Identity identity; Placement placement; Vec3 scale{1,1,1}; Screen screen; OutputKind kind{OutputKind::Screen}; };
// Borrowed presentation is transient and never enters the attachment manifest.
struct PlacementName { Identity identity; std::string asset, name; std::uint64_t owner{}, epoch{}, object{}; };
std::vector<PlacementName> placement_names_snapshot();
void publish_remote_names(std::vector<PlacementName>);
struct BorrowedOutput { OutputAnchor anchor; bool primary{}; };
void set_borrowed_outputs(bool active, std::vector<BorrowedOutput>);
bool borrowed_presentation_active();
std::vector<BorrowedOutput> borrowed_outputs_snapshot();
std::vector<Scene> borrowed_screens_snapshot();
bool load_settings(std::istream&,Settings&);
void write_settings(std::ostream&,const Settings&);
bool can_restore(const Identity& saved,const std::vector<Identity>& live);
Quat euler_degrees(Vec3);
void log(const std::string&);
void debug_log(const std::string&);
void flush_log();
void close_log();
void set_auto_speakers(bool);
bool auto_speakers_enabled();
std::uint64_t placement_session_epoch();
void set_debug_logging(bool) noexcept;
bool debug_logging() noexcept;
void initialize_log(const std::filesystem::path&);
bool initialize_native();
// ReSkate-compatible native object observation from the existing render callback.
// No Lua, game-tick hook, or ReSkate binary changes.
void poll_native_objects() noexcept;
// Native object to ReSkate editor asset-key mapping; no plugins or new hooks.
void observe_editor_placement_names(const dingosdk::ParkEditorModel&);
// Only asset presets explicitly marked automatic are passed here; native reader does the attaching.
void set_auto_screen_presets(std::vector<std::pair<std::string,Screen>>);
struct NativeObjectSourceStatus {
    bool verified{},active{};
    std::string status;
};
NativeObjectSourceStatus native_object_source_status();
struct NearbyObject { Identity identity; Placement placement; float distance{}; Vec3 scale{1,1,1}; };
struct NearbySnapshot { std::vector<NearbyObject> objects; std::wstring status; ULONGLONG at{}; };
void filter_nearby(std::vector<NearbyObject>&,Vec3 player);
void publish_player_position(Vec3,bool valid);
NearbySnapshot nearby_snapshot();
void request_select(Identity); void request_select(); void request_attach(); void request_detach();
Scene scene_snapshot();
// Re-read the verified active camera on the render callback.
// The same callback now updates native placement snapshots without a tick detour.
bool render_camera_snapshot(Camera&);
void set_screen(const Screen&);
void set_screen_for(Identity,const Screen&);
bool request_add_output(Identity,OutputKind,const Screen&);
bool remove_output(Identity,OutputKind);
// Removes the primary TV or one additional TV/speaker, never all outputs.
bool detach_one_output(Identity);
void clear_extra_outputs();
void clear_persistent_outputs();
void restore_persistent_outputs(const std::vector<attachment_store::Record>&);
std::pair<std::uint64_t,std::vector<attachment_store::Record>> persistent_outputs_snapshot();
bool take_restored_screen_signal();
std::vector<OutputAnchor> extra_outputs_snapshot();
// Includes the primary, live extras, queued extras, and queued primary attach.
// Used by the nearby-object list to display [TV]/[SP] and disable occupied objects.
std::vector<std::pair<Identity,OutputKind>> output_assignments_snapshot();
std::vector<Scene> world_screens_snapshot();
std::wstring attachment_status();
void restore_identity(Identity);
Identity selected_identity(); Identity attached_identity();
void stop_native();
class VlcFrameSource;
}
