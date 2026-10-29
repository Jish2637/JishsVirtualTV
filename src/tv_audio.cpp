#include "tv_audio.h"
#include <windows.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>
namespace virtualtv {
bool valid_audio(const AudioSettings& s){return std::isfinite(s.range)&&s.range>=5&&s.range<=1000&&s.volume>=0&&s.volume<=100;}
std::array<float,4> audio_matrix(Vec3 ear,Vec3 right,Vec3 emitter,float radius){
    // ReSkate v2.1.1 voice_gain / voice_stereo: inverse-distance attenuation,
    // smooth final-quarter fade, and equal-power stereo panning.
    const float x=emitter.x-ear.x,y=emitter.y-ear.y,z=emitter.z-ear.z;
    const float d=std::sqrt(x*x+y*y+z*z),axis=std::sqrt(right.x*right.x+right.y*right.y+right.z*right.z);
    if(!std::isfinite(d)||!std::isfinite(axis)||axis<.1f||!std::isfinite(radius)||radius<5||d>=radius)return {};
    const float full=std::max(3.f,radius*.15f);float gain=d<=full?1.f:full/d;
    if(d>radius*.75f){const float t=(d-radius*.75f)/(radius*.25f);gain*=1-t*t*(3-2*t);}
    const float pan=d>.01f?std::clamp((x*right.x+y*right.y+z*right.z)/(d*axis),-1.f,1.f):0.f;
    const float l=.5f*gain*std::sqrt((1-pan)*.5f),r=.5f*gain*std::sqrt((1+pan)*.5f);
    return {l,l,r,r}; // Row sums give the stereo gains; preview uses identity.
}
struct VlcAudioControl::State {
    // VLC 3 mmdevice.c uses this process-local session when volume-save is on.
    // Never address GUID_NULL (the game/default session) or another process.
    static constexpr GUID vlc_session{0x4533f59d,0x59ee,0x00c6,{0xad,0xb2,0xc6,0x8b,0x50,0x1a,0x66,0x55}};
    mutable std::mutex mutex;std::condition_variable cv;std::thread worker;
    std::array<float,2> desired{1,1};bool stopping{},playing{};
    std::uint64_t media_generation{};
    std::atomic<bool> active{},controlled{};std::atomic<float> peak{};
    std::atomic<float> actual_left{1},actual_right{1};
    std::atomic<HRESULT> error{S_OK};std::atomic<unsigned> channel_count{},bindings{};
    void run(){
        using Microsoft::WRL::ComPtr;
        const auto com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        if(FAILED(com)){error=com;return;}
        {
            ComPtr<IMMDeviceEnumerator> devices;
            HRESULT hr=CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&devices));
            ComPtr<IAudioSessionControl> session;ComPtr<IChannelAudioVolume> channels;
            ComPtr<IAudioClient> control_client;ComPtr<IAudioMeterInformation> meter;
            auto release=[&](bool restore=false){
                if(restore&&channels){UINT32 count{};if(SUCCEEDED(channels->GetChannelCount(&count))&&count<=32){std::vector<float> unity(count,1);channels->SetAllVolumes(count,unity.data(),nullptr);}}
                channels.Reset();control_client.Reset();meter.Reset();session.Reset();active=false;controlled=false;channel_count=0;peak=0;
            };
            std::array<float,2> current{1,1};ULONGLONG retry{};
            std::uint64_t active_generation{};
            while(SUCCEEDED(hr)){
                std::array<float,2> target{};bool want{};std::uint64_t generation{};
                {std::unique_lock lock(mutex);if(stopping)break;target=desired;want=playing;generation=media_generation;}
                // A playlist item may replace the underlying Windows audio
                // session even while the worker is still holding the old one.
                // Drop the old binding and reacquire VLC's new active session.
                if(generation!=active_generation){release(true);active_generation=generation;retry=0;}
                AudioSessionState status{};
                if(session&&(!want||FAILED(session->GetState(&status))||status!=AudioSessionStateActive))release(!want);
                if(!session&&want&&GetTickCount64()>=retry){
                    retry=GetTickCount64()+500;ComPtr<IMMDeviceCollection> endpoints;
                    HRESULT result=devices->EnumAudioEndpoints(eRender,DEVICE_STATE_ACTIVE,&endpoints);
                    UINT count{};if(SUCCEEDED(result))result=endpoints->GetCount(&count);
                    for(UINT i=0;SUCCEEDED(result)&&i<count&&!session;++i){
                        ComPtr<IMMDevice> endpoint;ComPtr<IAudioSessionManager> manager;ComPtr<IAudioSessionControl> candidate;
                        if(FAILED(endpoints->Item(i,&endpoint))||FAILED(endpoint->Activate(__uuidof(IAudioSessionManager),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(manager.GetAddressOf())))||FAILED(manager->GetAudioSessionControl(&vlc_session,0,&candidate)))continue;
                        ComPtr<IAudioSessionControl2> detail;DWORD owner{};
                        if(FAILED(candidate.As(&detail))||FAILED(detail->GetProcessId(&owner))||owner!=GetCurrentProcessId()||FAILED(candidate->GetState(&status))||status!=AudioSessionStateActive)continue;
                        session=candidate;active=true;++bindings;session.As(&meter);
                        result=session.As(&channels);
                        if(FAILED(result)){
                            // GetService is the documented route to the session channel
                            // control. This unstarted stream joins VLC's session only;
                            // VLC remains the sole producer and owner of audible audio.
                            result=endpoint->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(control_client.GetAddressOf()));
                            WAVEFORMATEX stereo{WAVE_FORMAT_IEEE_FLOAT,2,48000,48000*8,8,32,0};
                            if(SUCCEEDED(result))result=control_client->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM|AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,0,0,&stereo,&vlc_session);
                            if(SUCCEEDED(result))result=control_client->GetService(IID_PPV_ARGS(&channels));
                        }
                        UINT32 n{};if(SUCCEEDED(result))result=channels->GetChannelCount(&n);
                        if(SUCCEEDED(result)&&n>0&&n<=32){channel_count=n;current=target;error=S_OK;}
                        else{channels.Reset();control_client.Reset();error=FAILED(result)?result:E_UNEXPECTED;}
                    }
                    if(FAILED(result))error=result;
                }
                if(session){
                    float measured{};if(meter&&SUCCEEDED(meter->GetPeakValue(&measured)))peak=std::max(peak.load(),measured);
                    if(channels){
                        for(unsigned i=0;i<2;++i)current[i]+=(target[i]-current[i])*.35f;
                        const auto count=channel_count.load();std::vector<float> gains(count,std::max(current[0],current[1]));
                        if(count==1)gains[0]=std::max(current[0],current[1]);else{gains[0]=current[0];gains[1]=current[1];}
                        const auto result=channels->SetAllVolumes(count,gains.data(),nullptr);
                        if(FAILED(result)){error=result;release();}
                        else if(SUCCEEDED(channels->GetAllVolumes(count,gains.data()))){actual_left=gains[0];actual_right=gains[count>1?1:0];controlled=true;}
                    }
                }
                std::unique_lock lock(mutex);cv.wait_for(lock,std::chrono::milliseconds(want?20:250),[&]{return stopping||playing!=want||media_generation!=generation;});
            }
            if(FAILED(hr))error=hr;release(true);
        }
        CoUninitialize();
    }
};
VlcAudioControl::VlcAudioControl():state_(std::make_unique<State>()){}
VlcAudioControl::~VlcAudioControl(){shutdown();}
void VlcAudioControl::initialize(){auto& s=*state_;std::lock_guard lock(s.mutex);if(s.worker.joinable())return;s.stopping=false;s.worker=std::thread([&s]{s.run();});}
void VlcAudioControl::target(std::array<float,4> m){auto clamp=[](float v){return std::isfinite(v)?std::clamp(v,0.f,1.f):0.f;};std::lock_guard lock(state_->mutex);state_->desired={clamp(m[0]+m[1]),clamp(m[2]+m[3])};}
void VlcAudioControl::playing(bool on){std::lock_guard lock(state_->mutex);state_->playing=on;state_->cv.notify_all();}
void VlcAudioControl::new_media(){
    std::lock_guard lock(state_->mutex);
    ++state_->media_generation;
    state_->cv.notify_all();
}
void VlcAudioControl::shutdown(){auto& s=*state_;{std::lock_guard lock(s.mutex);s.stopping=true;s.cv.notify_all();}if(s.worker.joinable())s.worker.join();}
bool VlcAudioControl::active()const{return state_->active.load();}
bool VlcAudioControl::controlled()const{return state_->controlled.load();}
float VlcAudioControl::peak()const{return state_->peak.load();}
std::array<float,2> VlcAudioControl::gains()const{return {state_->actual_left.load(),state_->actual_right.load()};}
std::string VlcAudioControl::diagnostics()const{return "VLC depth3 output; session_active="+std::to_string(active())+" channel_control="+std::to_string(controlled())+" channels="+std::to_string(state_->channel_count.load())+" session_peak="+std::to_string(peak())+" actual_left="+std::to_string(state_->actual_left.load())+" actual_right="+std::to_string(state_->actual_right.load())+" bindings="+std::to_string(state_->bindings.load())+" HRESULT="+std::to_string(static_cast<unsigned long>(state_->error.load()));}
}
