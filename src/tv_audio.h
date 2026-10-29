#pragma once
#include "tv_geometry.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
namespace virtualtv {
struct AudioSettings { bool directional{true}; float range{35}; int volume{75}; };
bool valid_audio(const AudioSettings&);
// Stereo gain matrix; row sums control the existing VLC output's left/right gain.
std::array<float,4> audio_matrix(Vec3 listener,Vec3 right,Vec3 emitter,float range);
// Controls VLC's existing Windows audio session. Does not render or replace audio.
class VlcAudioControl {
public:
    VlcAudioControl(); ~VlcAudioControl();
    void initialize();
    void target(std::array<float,4>);
    void playing(bool);
    void new_media();
    void shutdown();
    std::string diagnostics() const;
    bool active() const;
    bool controlled() const;
    float peak() const;
    std::array<float,2> gains() const;
private:
    struct State;std::unique_ptr<State> state_;
};
}
