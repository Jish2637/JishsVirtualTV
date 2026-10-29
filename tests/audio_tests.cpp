#include "tv_audio.h"
#include "tv_runtime.h"
#include "vlc_player.h"
#include "url_resolver.h"
#include "audio_position.h"
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <chrono>
#include <filesystem>
#include <string_view>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
using namespace virtualtv;
static std::int64_t __cdecl clock_us(){return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
int main(int argc,char** argv){
    const Vec3 ear{},right{1,0,0};
    auto centre=audio_matrix(ear,right,{0,0,-1},35),left=audio_matrix(ear,right,{-1,0,0},35),r=audio_matrix(ear,right,{1,0,0},35);
    assert(std::abs(centre[0]-centre[2])<1e-6f);assert(left[0]>.49f&&left[2]==0);assert(r[0]==0&&r[2]>.49f);
    assert(audio_matrix(ear,{-1,0,0},{1,0,0},35)==left);
    assert(audio_matrix({100,20,30},right,{100,20,29},35)==centre);
    float last=1;for(float d=0;d<36;d+=.1f){auto m=audio_matrix(ear,right,{0,0,-d},35);float gain=std::sqrt(4*m[0]*m[0]+4*m[2]*m[2]);assert(gain<=last+1e-5f);last=gain;}
    assert(audio_matrix(ear,right,{0,0,-35},35)==(std::array<float,4>{}));
    assert(audio_matrix(ear,right,{NAN,0,0},35)==(std::array<float,4>{}));
    assert(audio_matrix(ear,{},{0,0,1},35)==(std::array<float,4>{}));
    assert(!valid_audio({true,NAN,75})&&!valid_audio({true,4,75})&&!valid_audio({true,35,101}));
    Placement object;object.position={10,20,30};object.rotation=euler_degrees({17,32,70});Screen screen;screen.offset={1,2,3};screen.rotation=euler_degrees({15,27,45});
    const Vec3 scale{2,3,4};const auto emitter=audio_emitter(object,scale,screen);Vec3 centre_world{};
    for(auto p:corners(Placement{},screen).positions){p=rotate(object.rotation,{p.x*scale.x,p.y*scale.y,p.z*scale.z});centre_world.x+=(p.x+object.position.x)*.25f;centre_world.y+=(p.y+object.position.y)*.25f;centre_world.z+=(p.z+object.position.z)*.25f;}
    assert(std::abs(emitter.x-centre_world.x)<.0001f&&std::abs(emitter.y-centre_world.y)<.0001f&&std::abs(emitter.z-centre_world.z)<.0001f);
    Settings settings;settings.audio={false,123,42};std::stringstream text;write_settings(text,settings);Settings loaded;assert(load_settings(text,loaded));assert(!loaded.audio.directional&&loaded.audio.range==123&&loaded.audio.volume==42);
    std::stringstream old("JISH_TV_1\n1.4 .8 1 2 3 0 0 0 1\n0 0 0 0\n");assert(load_settings(old,loaded));assert(loaded.audio.directional&&loaded.audio.range==35&&loaded.screen.offset.z==3);
    std::stringstream legacy("1.4 .8 1 2 3");assert(load_settings(legacy,loaded)&&loaded.audio.volume==75);
    for(const auto* tail:{"2 35 75","1 -1 75","1 35 101","1 35 75 extra"}){std::stringstream bad;bad<<"JISH_TV_2\n1.4 .8 0 0 0 0 0 0 1\n0 0 0 0\n"<<tail;assert(!load_settings(bad,loaded));}
    if(argc>1&&std::string_view(argv[1])=="--device"){
        const auto com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);assert(SUCCEEDED(com));
        using Microsoft::WRL::ComPtr;
        ComPtr<IMMDeviceEnumerator> devices;ComPtr<IMMDevice> endpoint;ComPtr<IAudioClient> other_client;ComPtr<IChannelAudioVolume> other_channels;
        assert(SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&devices))));
        assert(SUCCEEDED(devices->GetDefaultAudioEndpoint(eRender,eConsole,&endpoint)));
        assert(SUCCEEDED(endpoint->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(other_client.GetAddressOf()))));
        GUID other_id{};assert(SUCCEEDED(CoCreateGuid(&other_id)));WAVEFORMATEX format{WAVE_FORMAT_IEEE_FLOAT,2,48000,48000*8,8,32,0};
        assert(SUCCEEDED(other_client->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,0,0,&format,&other_id)));
        assert(SUCCEEDED(other_client->GetService(IID_PPV_ARGS(&other_channels))));
        UINT32 other_count{};assert(SUCCEEDED(other_channels->GetChannelCount(&other_count)));std::vector<float> other_before(other_count),other_after(other_count);
        assert(SUCCEEDED(other_channels->GetAllVolumes(other_count,other_before.data())));
        // Use the restored VLC output and inspect its actual Windows session.
        auto path=std::filesystem::current_path()/"tv-audio-fixture.wav";
        {std::ofstream out(path,std::ios::binary);auto u16=[&](unsigned n){out.put(char(n));out.put(char(n>>8));};auto u32=[&](unsigned n){u16(n);u16(n>>16);};
         out.write("RIFF",4);u32(36+(48000*4*4));out.write("WAVEfmt ",8);u32(16);u16(1);u16(2);u32(48000);u32(48000*4);u16(4);u16(16);out.write("data",4);u32((48000*4*4));
         for(unsigned i=0;i<48000*4;++i){const auto v=static_cast<short>(1000*std::sin(i*440*6.2831853/48000));u16(static_cast<unsigned short>(v));u16(static_cast<unsigned short>(v));}}
        VlcPlayer player;std::wstring error;player.volume(0);ResolvedMedia media;media.video="file:///"+path.generic_string();
        assert(player.play_resolved(media,error));const auto vlc_until=GetTickCount64()+8000;
        while((!player.audio_active()||!player.audio_controlled()||player.audio_peak()<=0)&&GetTickCount64()<vlc_until)Sleep(20);
        std::cout<<"VLC "+player.audio_diagnostics()<<'\n';assert(player.audio_active()&&player.audio_controlled()&&player.audio_peak()>0);
        player.audio_target({0,0,.5f,.5f});Sleep(250);auto gains=player.audio_gains();assert(gains[0]<.05f&&gains[1]>.95f);
        player.audio_target({0,0,0,0});Sleep(250);gains=player.audio_gains();assert(gains[0]<.05f&&gains[1]<.05f);
        assert(SUCCEEDED(other_channels->GetAllVolumes(other_count,other_after.data())));assert(other_before==other_after);
        player.audio_target({1,0,0,1});Sleep(250);gains=player.audio_gains();assert(gains[0]>.95f&&gains[1]>.95f);
        std::cout<<"VLC channel gain readback: right pan, distance silence, stereo restore and separate-session isolation passed\n";
        player.stop();assert(player.play_resolved(media,error));
        for(unsigned i=0;i<40;++i){player.audio_target({0,0,0,0});Sleep(20);}
        gains=player.audio_gains();assert(gains[0]<.05f&&gains[1]<.05f);
        assert(player.seekable());assert(player.set_paused(true));
        auto pause_until=GetTickCount64()+2000;while(player.playback_state()!=4&&GetTickCount64()<pause_until)Sleep(20);assert(player.playback_state()==4);
        auto seek_revision=player.seek_revision();assert(player.seek_to_ms(1000)&&player.seek_revision()==seek_revision+1);
        auto seek_until=GetTickCount64()+2000;while(std::abs(player.playback_time_ms()-1000)>200&&GetTickCount64()<seek_until)Sleep(20);assert(std::abs(player.playback_time_ms()-1000)<=200);
        assert(player.set_paused(false));Sleep(250);assert(player.playback_state()==3);
        std::cout<<"VLC paused seek/readback, seek revision and viewer-only resume passed\n";
        player.stop();player.shutdown();std::filesystem::remove(path);
        other_channels.Reset();other_client.Reset();endpoint.Reset();devices.Reset();CoUninitialize();
    }
    std::cout<<"Audio panning, distance fade, invalid inputs, settings migration passed\n";
}
