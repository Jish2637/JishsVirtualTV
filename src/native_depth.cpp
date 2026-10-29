#include "native_depth.h"
#include "depth_tracking.h"
#include "cbv_capture.h"
#include "reshade_switch_policy.h"
#include "reshade_capture_policy.h"
#include "reshade_projection_aspect.h"
#include <unordered_map>
#include <vector>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <condition_variable>
namespace virtualtv::depth::native {
using Microsoft::WRL::ComPtr;
namespace {
thread_local bool inserting{};
#ifdef TV_GPU_TEST
Camera fixture_camera;
#endif
struct Matrix {std::array<float,16> data{};UINT index{},offset{};cbv::Snapshot buffer;};
struct Proof {void* resource{};sl::Constants camera;virtualtv::Camera pose;D3D12_VIEWPORT viewport{};UINT64 at{},interval{};bool valid{};cbv::Snapshot buffer;};
struct PlaneState {D3D12_RESOURCE_STATES value{};bool known{};};
struct Score {ComPtr<ID3D12Resource> resource;UINT64 draws{},vertices{};};
// ReShade-inspired heuristic: statistics belong to the bound DSV on this
// command list, rather than to whatever projection CBV happened to be bound.
struct Work {virtualtv::Camera camera;bool camera_attempted{};UINT64 draws{},vertices{};D3D12_VIEWPORT viewport{};bool has_viewport{};};
struct List {void* resource{};tracking::Record descriptor{};D3D12_VIEWPORT viewport{};bool viewport_valid{},in_pass{},discard_depth{},unsupported_pass{};std::vector<Matrix> matrices;Proof proof;std::array<UINT64,64> cbvs{};ComPtr<ID3D12RootSignature> signature;std::unordered_map<void*,PlaneState> states;std::unordered_map<void*,Score> scores;std::unordered_map<void*,Work> work;};
struct Packet {std::shared_ptr<Image> image;sl::Constants camera;virtualtv::Camera pose;UINT64 interval{},area{};cbv::Snapshot buffer;bool valid=true;unsigned stage{};};
// Prefer end-of-pass copies over incomplete transition samples where safe.
// Policy and its window are shared with the platform-neutral regression test.
struct Stable {UINT64 interval{};unsigned count{};};
struct Clear {float value{};UINT64 at{};};
struct Slot {std::shared_ptr<Image> image;ComPtr<ID3D12Fence> readers;UINT64 reader_value{},writer_value{};bool reserved{},retired{};};
struct State {
    std::atomic<bool> enabled{},lifetime_ready{},heuristic_mode{};std::atomic<int> capture_boundary_mode{0},minimum_scene_draw_count{8};std::atomic<float> estimated_near_plane{0.0603f};std::atomic<UINT64> projection_inputs{};
    std::mutex mutex;std::unordered_map<void*,List> lists;std::unordered_map<void*,Clear> clears;
    std::unordered_map<void*,std::vector<Packet>> pending,recordings;std::vector<Slot> pool;std::condition_variable readers_posted;
    std::unordered_map<void*,Stable> stable;std::unordered_map<UINT64,void*> interval_sources;
    std::vector<std::array<void*,11>> approved;
    std::unordered_map<void*,Score> submitted_scores;ComPtr<ID3D12Resource> preferred;
    std::vector<Score> last_candidates;int manual_selection=-1;ComPtr<ID3D12Resource> manual_resource;
    // ReShade Auto: retain the accepted resource while a stronger candidate is
    // observed and copied. A candidate cannot replace latest until its GPU
    // producer fence has actually completed.
    selection::ChallengerTracker switch_policy;
    ComPtr<ID3D12Resource> challenger;
    Packet replacement;
    UINT64 continuity_switch_waits{},continuity_challenger_copies{},continuity_switches_committed{},continuity_pose_grace{};
    std::unordered_map<std::string,UINT64> rejection_counts;
    Packet latest;Scene scene;unsigned width{},height{},epoch=1;UINT64 interval{},ambiguous_interval=UINT64_MAX;
    UINT64 copies{},published{},draws{},rejects{},discarded{},sequence{},buffer_inputs{},projection_compared{},projection_matches_count{},projection_ties{};unsigned cbv_scans{};std::atomic<UINT64> errors{};
    UINT64 clear_copies{},unbind_copies{},pass_copies{},plane_copies{},pass_begins{},pass_ends{},next_copy_log{};
    UINT64 heuristic_draws{},heuristic_proofs{},heuristic_matched_projection{},heuristic_copy_attempts{},heuristic_rejected{};
    UINT64 heuristic_clears_seen{},heuristic_clears_with_work{},heuristic_clears_without_work{},heuristic_clears_no_selection{},heuristic_clears_missing_clear_history{};
    UINT64 continuity_depth_grace{},continuity_depth_misses{},continuity_preferred_held{};
    UINT64 continuity_stage_holds{},continuity_unbind_published{},continuity_transition_fallbacks{};
    std::string reason="Native occlusion waiting for live hooks and projection";
};
State& state(){static auto* value=new State;return *value;}
Slot* slot(State& s,const Image* image){for(auto& entry:s.pool)if(entry.image.get()==image)return &entry;return nullptr;}
bool perspective(const std::array<float,16>& m){
    for(float v:m)if(!std::isfinite(v)||std::abs(v)>1e6f)return false;
    return m[0]>.01f&&m[5]>.01f&&std::abs(std::abs(m[11])-1)<.001f&&std::abs(m[15])<1e-6f&&
        std::abs(m[1])+std::abs(m[2])+std::abs(m[3])+std::abs(m[4])+std::abs(m[6])+std::abs(m[7])+std::abs(m[12])+std::abs(m[13])<.001f&&
        std::abs(m[8])+std::abs(m[9])<.02f&&std::abs(m[10])<=1.1f&&std::abs(m[14])>1e-6f&&std::abs(m[14])<10;
}
bool projection_matches(const std::array<float,16>& p,const virtualtv::Camera& camera,double aspect){
    if(!camera.valid||camera.vertical_fov<=1||camera.vertical_fov>=175||aspect<=0||!perspective(p))return false;
    const auto y=1/std::tan(camera.vertical_fov*3.14159265359/360);
    return std::abs(p[5]/y-1)<.02&&std::abs((p[5]/p[0])/aspect-1)<.02;
}
bool pose_matches(const virtualtv::Camera& a,const virtualtv::Camera& b){
    if(!a.valid||!b.valid||std::abs(a.vertical_fov-b.vertical_fov)>.25f)return false;
    double d{};for(unsigned i=12;i<15;++i){const double delta=a.world[i]-b.world[i];d+=delta*delta;}
    if(!std::isfinite(d)||d>.25)return false;
    for(unsigned i:{0u,4u,8u}){const double dot=a.world[i]*b.world[i]+a.world[i+1]*b.world[i+1]+a.world[i+2]*b.world[i+2];if(!std::isfinite(dot)||dot<.995)return false;}return true;
}
bool pose_matches_reshade(const virtualtv::Camera& a,const virtualtv::Camera& b,UINT64 age_ms){
    // Accept only a small, fresh render/capture discrepancy; the sampled depth
    // continues using the projection and pose recorded with its own copy.
    if(!a.valid||!b.valid||std::abs(a.vertical_fov-b.vertical_fov)>.25f)return false;
    // The ReShade pixel shader reprojects a depth image through its paired
    // capture camera, so ordinary player movement should not reject the image
    // solely because the presentation camera advanced a frame or two.
    // Still bound the motion to avoid reusing depth after teleport/cutscene.
    const double max_dist=age_ms<=50?2.0:3.0;
    const double min_dot=age_ms<=50?.94:.90;
    double distance{};
    for(unsigned i=12;i<15;++i){const double delta=a.world[i]-b.world[i];distance+=delta*delta;}
    if(!std::isfinite(distance)||distance>max_dist*max_dist)return false;
    for(unsigned i:{0u,4u,8u}){
        const double dot=a.world[i]*b.world[i]+a.world[i+1]*b.world[i+1]+a.world[i+2]*b.world[i+2];
        if(!std::isfinite(dot)||dot<min_dot)return false;
    }
    return true;
}
// Promotion happens after the producer fence has completed, never merely
// because a candidate topped one submitted-frame ranking.
void promote_ready_challenger(State& s){
    if(!s.heuristic_mode||s.manual_selection>=0||!s.challenger||!s.replacement.image)return;
    const auto& p=s.replacement;const auto& image=*p.image;
    if(image.source.Get()!=s.challenger.Get()||image.epoch!=s.epoch||image.poisoned||
       !image.ready||!image.value){s.replacement={};return;}
    if(image.ready->GetCompletedValue()<image.value)return;
    const auto now=GetTickCount64();
    const UINT64 age=now>=image.at?now-image.at:UINT64_MAX;
    if(age>120||!pose_matches_reshade(p.pose,s.scene.camera,age)){
        s.replacement={};return;
    }
    s.preferred=s.challenger;
    s.latest=p;
    s.challenger.Reset();s.replacement={};s.switch_policy.reset();
    ++s.continuity_switches_committed;
    s.reason="ReShade depth resource promoted after validated, GPU-completed replacement capture";
    debug_log("ReShade-style scene depth handover committed after producer fence completion");
}
bool same(IUnknown* a,IUnknown* b){ComPtr<IUnknown>x,y;return a&&b&&SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(&x)))&&SUCCEEDED(b->QueryInterface(IID_PPV_ARGS(&y)))&&x==y;}
List* entry(State& s,void* object){auto found=s.lists.find(object);if(found!=s.lists.end())return &found->second;if(s.lists.size()>=512)return nullptr;return &s.lists[object];}
void reject(State& s,const char* why){++s.rejects;++s.rejection_counts[why];s.reason=why;}
void transition(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to,UINT subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES){if(from==to)return;D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={resource,subresource,from,to};list->ResourceBarrier(1,&b);}
void invalidate_locked(State& s){++s.epoch;s.latest={};s.preferred.Reset();s.submitted_scores.clear();s.last_candidates.clear();s.lists.clear();s.clears.clear();s.stable.clear();s.interval_sources.clear();s.switch_policy.reset();s.challenger.Reset();s.replacement={};s.ambiguous_interval=UINT64_MAX;s.reason="Native occlusion invalidated; waiting for fresh recording";}
}
void activate(bool value) noexcept {auto& s=state();if(s.enabled.exchange(value)!=value)invalidate();}
void heuristic(bool value) noexcept {auto& s=state();if(s.heuristic_mode.exchange(value)!=value)invalidate();}
void invalidate() noexcept {try{auto& s=state();std::lock_guard lock(s.mutex);invalidate_locked(s);}catch(...){++state().errors;}}
void lifetime(bool ready) noexcept {state().lifetime_ready=ready;}
// These settings affect only the ReShade-style backend. Other backends never
// consult them, so a working Streamline installation is unaffected.
int capture_boundary() noexcept {return state().capture_boundary_mode.load();}
void set_capture_boundary(int value) noexcept {if(value<0||value>3)return;auto& s=state();if(s.capture_boundary_mode.exchange(value)!=value)invalidate();}
float estimated_near_plane() noexcept {return state().estimated_near_plane.load();}
void set_estimated_near_plane(float value) noexcept {if(!std::isfinite(value)||value<0.02f||value>0.15f)return;auto& s=state();if(std::abs(s.estimated_near_plane.exchange(value)-value)>0.000001f)invalidate();}
int minimum_scene_draws() noexcept {return state().minimum_scene_draw_count.load();}
void set_minimum_scene_draws(int value) noexcept {if(value<8||value>2048)return;auto& s=state();if(s.minimum_scene_draw_count.exchange(value)!=value)invalidate();}
int selected_depth() noexcept {auto& s=state();std::lock_guard lock(s.mutex);return s.manual_selection;}
void select_depth(int index) noexcept {try{
    auto& s=state();std::lock_guard lock(s.mutex);
    if(index < -1 || index >= 8)return;
    if(s.manual_selection!=index){
        s.manual_selection=index;s.manual_resource.Reset();
        if(index>=0&&size_t(index)<s.last_candidates.size())
            s.manual_resource=s.last_candidates[index].resource;
        s.preferred.Reset();s.latest={};s.stable.clear();s.challenger.Reset();s.replacement={};s.switch_policy.reset();
    }
}catch(...){++state().errors;}}
std::string depth_candidates() noexcept {try{
    auto& s=state();std::lock_guard lock(s.mutex);std::ostringstream out;
    out<<"Selected: "<<(s.manual_selection<0?"automatic":"manual")<<". Main-scene candidates from the last submitted frame:";
    unsigned index{};
    for(const auto& c:s.last_candidates){
        const auto d=c.resource->GetDesc();out<<"\n"<<index++<<": "<<d.Width<<'x'<<d.Height<<" format="<<d.Format
            <<" draws="<<c.draws<<" vertices="<<c.vertices;
    }
    if(s.last_candidates.empty())out<<" none yet";
    return out.str();
}catch(...){++state().errors;return "Depth candidate report unavailable";}}
void approve(const std::array<void*,11>& methods) noexcept {try{auto& s=state();std::lock_guard lock(s.mutex);if(s.approved.size()<4&&std::find(s.approved.begin(),s.approved.end(),methods)==s.approved.end())s.approved.push_back(methods);}catch(...){++state().errors;}}
void frame(const Scene& scene,unsigned width,unsigned height) noexcept {try{auto& s=state();std::lock_guard lock(s.mutex);
    if(s.width!=width||s.height!=height)invalidate_locked(s);s.width=width;s.height=height;s.scene=scene;
}catch(...){++state().errors;}}
void present() noexcept {try{auto& s=state();if(!s.enabled)return;std::lock_guard lock(s.mutex);
    promote_ready_challenger(s);
    s.last_candidates.clear();
    for(const auto& e:s.submitted_scores)s.last_candidates.push_back(e.second);
    std::sort(s.last_candidates.begin(),s.last_candidates.end(),[](const Score& a,const Score& b){
        if(a.draws!=b.draws)return a.draws>b.draws;
        return a.vertices>b.vertices;
    });
    if(s.last_candidates.size()>8)s.last_candidates.resize(8);
    const Score* best=s.last_candidates.empty()?nullptr:&s.last_candidates[0];
    if(s.heuristic_mode&&s.manual_selection>=0){
        // Manual override remains pinned to actual D3D12 resource identity.
        best=nullptr;
        for(const auto& c:s.last_candidates)if(c.resource.Get()==s.manual_resource.Get()){best=&c;break;}
        s.switch_policy.reset();s.challenger.Reset();s.replacement={};
    } else if(s.heuristic_mode && s.preferred){
        // Never evict the current capture merely because draw rankings change.
        // Record a sustained challenger, but commit it ONLY after a usable GPU
        // copy has completed. The old depth remains valid meanwhile.
        const auto old=s.submitted_scores.find(s.preferred.Get());
        const UINT64 current_draws=old==s.submitted_scores.end()?0:old->second.draws;
        s.switch_policy.observe(best?best->resource.Get():nullptr,s.preferred.Get(),
                                best?best->draws:0,current_draws);
        const auto candidate=s.switch_policy.ready()?s.switch_policy.candidate:nullptr;
        if(candidate!=s.challenger.Get()){
            s.challenger=static_cast<ID3D12Resource*>(const_cast<void*>(candidate));
            s.replacement={};
        }
        if(best&&best->resource.Get()!=s.preferred.Get()){
            ++s.continuity_switch_waits;
            ++s.continuity_preferred_held;
        }
        best=nullptr; // retain the current preferred until a valid handover
    } else if(best){
        // Preserve the pre-existing behavior for experimental non-ReShade DX12.
        const auto old=s.submitted_scores.find(s.preferred.Get());
        if(old!=s.submitted_scores.end()&&old->second.draws>=8&&
           old->second.draws*4>=best->draws*3)best=&old->second;
    }
    if(best && s.preferred!=best->resource){
        s.preferred=best->resource;
        s.latest={}; // initial choice, native experimental backend or explicit manual
        debug_log(s.heuristic_mode?"ReShade-style initial scene depth selection":"Native preferred depth selected from submitted draw statistics");
    }
    s.submitted_scores.clear();++s.interval;s.interval_sources.clear();s.cbv_scans=0;
}catch(...){++state().errors;}}
void viewport(ID3D12GraphicsCommandList* object,UINT count,const D3D12_VIEWPORT* values) noexcept {try{
    auto& s=state();if(!s.enabled||inserting)return;std::lock_guard lock(s.mutex);auto* l=entry(s,object);if(!l)return;l->proof.valid=false;
    l->viewport_valid=count==1&&values&&std::isfinite(values[0].Width)&&std::isfinite(values[0].Height)&&std::isfinite(values[0].TopLeftX)&&std::isfinite(values[0].TopLeftY)&&values[0].TopLeftX>=0&&values[0].TopLeftY>=0&&values[0].MinDepth==0&&values[0].MaxDepth==1;
    if(l->viewport_valid)l->viewport=values[0];
}catch(...){++state().errors;}}
void signature(ID3D12GraphicsCommandList* object,ID3D12RootSignature* signature) noexcept {try{auto& s=state();if(!s.enabled||inserting)return;std::lock_guard lock(s.mutex);if(auto* l=entry(s,object)){if(l->signature.Get()==signature)return;l->signature=signature;l->matrices.clear();l->cbvs={};l->proof.valid=false;}}catch(...){++state().errors;}}
void constants(ID3D12GraphicsCommandList* object,UINT index,UINT count,const void* data,UINT offset) noexcept {try{
    auto& s=state();if(!s.enabled||inserting||!count||!data)return;std::lock_guard lock(s.mutex);auto* l=entry(s,object);if(!l)return;l->proof.valid=false;
    if(index<l->cbvs.size())l->cbvs[index]=0;
    // Partial writes overlapping a previous candidate invalidate it.
    l->matrices.erase(std::remove_if(l->matrices.begin(),l->matrices.end(),[&](const Matrix& m){return m.index==index&&UINT64(offset)<UINT64(m.offset)+16&&UINT64(m.offset)<UINT64(offset)+count;}),l->matrices.end());
    if(count<16)return;const auto n=std::min<UINT>(count,64);std::array<float,64> values{};std::memcpy(values.data(),data,n*sizeof(float));
    for(UINT start=0;start+16<=n&&l->matrices.size()<4;++start){std::array<float,16> m{};std::copy_n(values.begin()+start,16,m.begin());
        if(!perspective(m)){std::array<float,16> t{};for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x)t[y*4+x]=m[x*4+y];if(!perspective(t))continue;m=t;}
        l->matrices.push_back({m,index,offset+start});++s.projection_inputs;start+=15;
    }
}catch(...){++state().errors;}}
void root_cbv(ID3D12GraphicsCommandList* object,UINT index,D3D12_GPU_VIRTUAL_ADDRESS address) noexcept {try{
    auto& s=state();if(!s.enabled||inserting||index>=64)return;std::lock_guard lock(s.mutex);auto* l=entry(s,object);if(!l)return;
    if(l->cbvs[index]==address)return;
    l->proof.valid=false;l->cbvs[index]=address;
    l->matrices.erase(std::remove_if(l->matrices.begin(),l->matrices.end(),[&](const Matrix& m){return m.index==index;}),l->matrices.end());
}catch(...){++state().errors;}}
void bind(ID3D12GraphicsCommandList* object,D3D12_CPU_DESCRIPTOR_HANDLE handle) noexcept {try{
    auto& s=state();if(!s.enabled||inserting)return;tracking::Record descriptor{};const bool known=tracking::lookup(handle.ptr,descriptor);
    std::lock_guard lock(s.mutex);if(auto* l=entry(s,object)){l->proof.valid=false;l->resource=known?descriptor.resource:nullptr;l->descriptor=descriptor;}
}catch(...){++state().errors;}}
void clear(ID3D12GraphicsCommandList* object,D3D12_CPU_DESCRIPTOR_HANDLE handle,D3D12_CLEAR_FLAGS flags,float value,UINT rectangles) noexcept {try{
    auto& s=state();if(!s.enabled||inserting||!(flags&D3D12_CLEAR_FLAG_DEPTH))return;tracking::Record descriptor{};if(!tracking::lookup(handle.ptr,descriptor)||!descriptor.resource)return;
    std::lock_guard lock(s.mutex);
    // ReShade tracks clears per recording command list. Clearing a resource
    // on one list must not erase work recorded independently on other lists.
    const auto it=s.lists.find(object);
    if(it!=s.lists.end()){
        if(it->second.proof.resource==descriptor.resource)it->second.proof.valid=false;
        if(s.heuristic_mode&&!rectangles)it->second.work.erase(descriptor.resource);
    }
    if(!rectangles){if(s.clears.size()>=128&&!s.clears.count(descriptor.resource))s.clears.clear();s.clears[descriptor.resource]={value,GetTickCount64()};}
}catch(...){++state().errors;}}
void clear_state(ID3D12GraphicsCommandList* object) noexcept {try{auto& s=state();std::lock_guard lock(s.mutex);s.lists.erase(object);}catch(...){++state().errors;}}
// ReShade's selection principle: meaningful draw activity on a scene-sized
// depth target is evidence for scene depth. It does NOT validate a shader CBV.
// D3D12 resource lifetime and the command list's actual DEPTH_WRITE state
// remain mandatory before a GPU copy is inserted.
bool scene_depth(const State& s,const List& l){
    if(!l.resource||!l.viewport_valid||!s.width||!s.height)return false;
    const auto& d=l.descriptor.desc;const auto& v=l.viewport;
    if(d.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||d.DepthOrArraySize!=1||d.MipLevels!=1||
       d.SampleDesc.Count!=1||!(d.Flags&D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)||
       sample_format(d.Format)==DXGI_FORMAT_UNKNOWN||v.Width<128||v.Height<128||
       v.TopLeftX<0||v.TopLeftY<0||v.TopLeftX+v.Width>double(d.Width)+1||
       v.TopLeftY+v.Height>double(d.Height)+1||v.Width<s.width*.20||
       d.Width>UINT64(s.width)*2||d.Height>UINT(s.height)*2)return false;
    if(l.descriptor.explicit_view &&
       (l.descriptor.view.ViewDimension!=D3D12_DSV_DIMENSION_TEXTURE2D||
        l.descriptor.view.Texture2D.MipSlice!=0))return false;
    // Dynamic-resolution upscalers may render at 50-100% of display size.
    const double aspect=double(v.Width)/v.Height;
    return std::abs(aspect/(double(s.width)/s.height)-1.0)<.08;
}
sl::Constants approximate_camera(const virtualtv::Camera& native,double aspect,bool reverse,float near_plane){
    sl::Constants c{};
    for(auto& row:c.cameraViewToClip.row)row=sl::float4(0,0,0,0);
    const auto focal=float(1.0/std::tan(native.vertical_fov*3.141592653589793/360.0));
    c.cameraViewToClip[0].x=float(focal/aspect);c.cameraViewToClip[1].y=focal;
    c.cameraViewToClip[2].w=-1.f;
    // Observed exact Skate 2.1.2 Streamline projection (RTX 5070 Ti):
    // [2].z = 5.36442e-6, [2].w = -1, [3].z = 0.0600003.
    // An earlier 0.10 approximation made TV reversed-Z depths ~67% too high,
    // so nearby characters could occlude while walls failed the depth test.
    // Do not depend on Streamline at runtime: AMD uses this calibrated fallback.
    c.cameraViewToClip[2].z=reverse?5.36442e-6f:-1.f;
    c.cameraViewToClip[3].z=reverse?near_plane:-near_plane;
    c.depthInverted=reverse?sl::eTrue:sl::eFalse;
    c.orthographicProjection=sl::eFalse;c.jitterOffset={0,0};
    const auto& w=native.world;
    c.cameraPos={w[12],w[13],w[14]};c.cameraRight={w[0],w[1],w[2]};
    c.cameraUp={w[4],w[5],w[6]};c.cameraFwd={w[8],w[9],w[10]};
    return c;
}
void draw(ID3D12GraphicsCommandList* object,UINT vertices,UINT instances) noexcept {try{
    auto& s=state();if(!s.enabled||inserting)return;std::lock_guard lock(s.mutex);++s.draws;
    const auto found=s.lists.find(object);if(found==s.lists.end())return;auto& l=found->second;l.proof.valid=false;
    if(l.unsupported_pass){reject(s,"Native occlusion rejected suspended/resumed render-pass recording");return;}
    constexpr std::array<unsigned,11> slots{21,36,38,32,12,13,30,34,11,27,59};std::array<void*,11> methods{};auto** vt=*reinterpret_cast<void***>(object);for(unsigned i=0;i<slots.size();++i)methods[i]=vt[slots[i]];
    if(std::find(s.approved.begin(),s.approved.end(),methods)==s.approved.end()){reject(s,"Native occlusion waiting for complete live recording hooks");return;}
    if(!s.lifetime_ready){reject(s,"Native occlusion waiting for queue/reset observers");return;}
    if(s.heuristic_mode){
        ++s.heuristic_draws;
        if(!scene_depth(s,l)||!s.scene.camera.valid||s.scene.camera.vertical_fov<=1||
           s.scene.camera.vertical_fov>=175||!s.scene.at||GetTickCount64()-s.scene.at>100){
            ++s.heuristic_rejected;return;
        }
        auto& work=l.work[l.resource];if(l.work.size()>24){l.work.clear();++s.heuristic_rejected;return;}
        ++work.draws;work.vertices+=UINT64(vertices)*instances;
        work.viewport=l.viewport;work.has_viewport=true;
        if(l.scores.size()<16||l.scores.count(l.resource)){
            auto& score=l.scores[l.resource];score.resource=static_cast<ID3D12Resource*>(l.resource);
            ++score.draws;score.vertices+=UINT64(vertices)*instances;
        }
        // Do not capture an empty cleared texture or a shadow-only pass.
        // After the first submitted frame we only capture the preferred DSV.
        if(work.draws<UINT64(s.minimum_scene_draw_count.load())||
           (s.preferred&&s.preferred.Get()!=l.resource&&s.challenger.Get()!=l.resource))return;
        const auto found_clear=s.clears.find(l.resource);
        if(found_clear==s.clears.end()||GetTickCount64()-found_clear->second.at>150||
           (found_clear->second.value!=0.f&&found_clear->second.value!=1.f))return;
        const bool reversed=found_clear->second.value==0.f;
        const double aspect=double(l.viewport.Width)/l.viewport.Height;
        // Use the swapchain aspect for the game projection; the DSV may be
        // rounded to a slightly different ratio by dynamic resolution.
        if(!work.camera_attempted){work.camera_attempted=true;
            if(render_camera_snapshot(work.camera))work.camera.source=CameraSource::Recording;
#ifdef TV_GPU_TEST
            else work.camera=fixture_camera;
#endif
        }
        if(!work.camera.valid){++s.heuristic_rejected;return;}
        const auto& recording_camera=work.camera;
        auto camera=approximate_camera(recording_camera,reshade::projection_aspect(s.width,s.height,aspect),reversed,s.estimated_near_plane.load());
        // An actual matrix from observed inline root constants is better than
        // the fallback camera approximation, but isn't required for selection.
        for(const auto& matrix:l.matrices)if(projection_matches(matrix.data,recording_camera,aspect)&&
            ((matrix.data[14]>0)==reversed)){
            std::memcpy(&camera.cameraViewToClip,matrix.data.data(),sizeof(float)*16);
            ++s.heuristic_matched_projection;break;
        }
        if(!valid_camera(camera)){++s.heuristic_rejected;return;}
        l.proof={l.resource,camera,recording_camera,l.viewport,GetTickCount64(),s.interval,true,{}};
        ++s.heuristic_proofs;
        return;
    }
    if(!l.resource||!l.viewport_valid||!s.width||!s.height||!s.scene.camera.valid||!s.scene.at||GetTickCount64()-s.scene.at>100){reject(s,"Native occlusion waiting for bound depth, full viewport and fresh native camera");return;}
    const auto& d=l.descriptor.desc;const auto& v=l.viewport;
    if(d.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||d.DepthOrArraySize!=1||d.MipLevels!=1||d.SampleDesc.Count!=1||v.Width<=0||v.Height<64||v.TopLeftX+v.Width>double(d.Width)||v.TopLeftY+v.Height>d.Height||v.Width<s.width/4||v.Width>s.width||d.Width>UINT64(s.width)*2||d.Height>s.height*2||sample_format(d.Format)==DXGI_FORMAT_UNKNOWN){reject(s,"Native occlusion rejected depth dimensions/view/sample layout");return;}
    if(l.descriptor.explicit_view&&(l.descriptor.view.ViewDimension!=D3D12_DSV_DIMENSION_TEXTURE2D||l.descriptor.view.Texture2D.MipSlice!=0||(UINT(l.descriptor.view.Flags)&~3u))){reject(s,"Native occlusion rejected depth view");return;}
    const double aspect=double(v.Width)/v.Height;if(std::abs(aspect/(double(s.width)/s.height)-1)>.02){reject(s,"Native occlusion rejected non-scene aspect");return;}
    if(l.scores.size()<16||l.scores.count(l.resource)){auto& score=l.scores[l.resource];score.resource=static_cast<ID3D12Resource*>(l.resource);++score.draws;}
    else {reject(s,"Native occlusion recording depth-statistics budget reached");return;}
    if(s.preferred&&s.preferred.Get()!=l.resource){reject(s,"Native occlusion skipped non-selected submitted depth pass");return;}
    // Reuse only candidates whose bytes still match. This avoids spending the
    // scan budget on every draw of a pass that rebinds the same camera buffer.
    l.matrices.erase(std::remove_if(l.matrices.begin(),l.matrices.end(),[](const Matrix& m){return m.buffer.resource&&!cbv::unchanged(m.buffer);}),l.matrices.end());
    ComPtr<ID3D12Device> device;object->GetDevice(IID_PPV_ARGS(&device));
    for(UINT index=0;index<l.cbvs.size();++index)if(l.cbvs[index]){
        if(std::any_of(l.matrices.begin(),l.matrices.end(),[&](const Matrix& m){return m.index==index&&m.buffer.resource;}))continue;
        bool scanned{};auto candidates=cbv::read(device.Get(),l.cbvs[index],l.signature.Get(),index,s.cbv_scans<16,&scanned);if(scanned)++s.cbv_scans;
        for(auto& candidate:candidates){if(l.matrices.size()>=32){reject(s,"Native occlusion rejected excessive projection candidates");return;}l.matrices.push_back({candidate.matrix,index,UINT(candidate.snapshot.offset/4),std::move(candidate.snapshot)});++s.buffer_inputs;}
    }
    // Multiple CBVs can contain equivalent camera projections (for instance,
    // jittered and unjittered versions). Reject genuinely different depth
    // conventions, not harmless duplicate matrix candidates from the same draw.
    const Matrix* selected{};double best_error=1e9;bool conflicting{};
    const double expected_y=1.0/std::tan(s.scene.camera.vertical_fov*3.141592653589793/360.0);
    for(const auto& m:l.matrices){
        ++s.projection_compared;
        if(!projection_matches(m.data,s.scene.camera,aspect))continue;
        ++s.projection_matches_count;
        const double error=std::abs(double(m.data[5])/expected_y-1.0)+
            std::abs((double(m.data[5])/m.data[0])/aspect-1.0);
        if(selected && std::abs(error-best_error)<1e-7 &&
           ((m.data[14]>0)!=(selected->data[14]>0))){conflicting=true;continue;}
        if(!selected || error<best_error){selected=&m;best_error=error;conflicting=false;}
    }
    if(conflicting){++s.projection_ties;reject(s,"Native occlusion rejected conflicting equally-matched projection conventions");return;}
    if(!selected){reject(s,"Native occlusion waiting for camera-matched projection from root constants/CBV (descriptor-table/GPU-only unresolved)");return;}
    const auto clear=s.clears.find(l.resource);const auto now=GetTickCount64();const bool reverse=selected->data[14]>0;
    if(clear==s.clears.end()||now-clear->second.at>75||clear->second.value!=(reverse?0.f:1.f)){reject(s,"Native occlusion waiting for fresh clear matching observed depth convention");return;}
    sl::Constants camera;std::memcpy(&camera.cameraViewToClip,selected->data.data(),64);camera.depthInverted=reverse?sl::eTrue:sl::eFalse;camera.orthographicProjection=sl::eFalse;camera.jitterOffset={0,0};
    const auto& w=s.scene.camera.world;camera.cameraPos={w[12],w[13],w[14]};camera.cameraRight={w[0],w[1],w[2]};camera.cameraUp={w[4],w[5],w[6]};camera.cameraFwd={w[8],w[9],w[10]};
    if(!valid_camera(camera)){reject(s,"Native occlusion rejected camera basis/projection");return;}
    l.proof={l.resource,camera,s.scene.camera,v,now,s.interval,true,selected->buffer};
}catch(...){++state().errors;}}
static void capture_locked(State& s,ID3D12GraphicsCommandList* object,List& recording,D3D12_RESOURCE_STATES after,unsigned stage=0){auto& proof=recording.proof;if(!proof.valid||recording.in_pass)return;
        constexpr auto readable=D3D12_RESOURCE_STATE_DEPTH_READ|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        if(after!=D3D12_RESOURCE_STATE_DEPTH_WRITE&&after!=D3D12_RESOURCE_STATE_COPY_SOURCE&&(!(after&readable)||(after&~readable))){proof.valid=false;reject(s,"Native occlusion rejected unknown/unsupported depth-plane state at boundary");return;}
        const auto now=GetTickCount64();if(now-proof.at>50||proof.interval+1<s.interval){proof.valid=false;reject(s,"Native occlusion rejected stale recording proof");return;}
        if(!cbv::unchanged(proof.buffer)){proof.valid=false;reject(s,"Native occlusion rejected changed CBV projection before depth copy");return;}
        auto* source=static_cast<ID3D12Resource*>(proof.resource);const auto desc=source->GetDesc();const auto& saved=recording.descriptor.desc;
        if(desc.Width!=saved.Width||desc.Height!=saved.Height||desc.Format!=saved.Format||desc.MipLevels!=1||desc.DepthOrArraySize!=1||desc.SampleDesc.Count!=1){proof.valid=false;reject(s,"Native occlusion rejected changed resource metadata");return;}
        // Wait for a resource chosen using submitted work from a real game
        // frame. Without that, multiple DSVs can all look scene-sized.
        if(s.heuristic_mode&&!s.preferred){proof.valid=false;return;}
        if(s.preferred&&s.preferred.Get()!=source&&(!s.heuristic_mode||s.challenger.Get()!=source)){
            proof.valid=false;return;
        }
        // ReShade's selected resource and an explicitly armed challenger may
        // both have the same dimensions. This is not ambiguous: only their
        // independently fenced snapshots decide which is displayed. Do not
        // poison the current image merely because both were observed.
        const auto size=(UINT64(desc.Width)<<32)|desc.Height;
        if(!s.heuristic_mode){
            const auto seen=s.interval_sources.find(size);
            if(seen!=s.interval_sources.end()){
                if(seen->second!=source){s.ambiguous_interval=s.interval;s.latest={};reject(s,"Native occlusion rejected multiple scene-depth candidates at the same resolution");}
                proof.valid=false;return;
            }
        }
        s.interval_sources[size]=source;auto& stable=s.stable[source];if(stable.interval!=s.interval){stable.count=(stable.count&&s.interval-stable.interval<=4)?stable.count+1:1;stable.interval=s.interval;}
        if(s.stable.size()>64){s.stable.clear();proof.valid=false;reject(s,"Native occlusion candidate history reset");return;}
        if(stable.count<3){proof.valid=false;reject(s,"Native occlusion warming up candidate across three TV intervals");return;}
        size_t pending_count{};for(const auto& e:s.pending)pending_count+=e.second.size();if(pending_count>=8||s.interval_sources.size()>2){proof.valid=false;reject(s,"Native occlusion capture budget reached");return;}
        ComPtr<ID3D12Device> device,owner;if(FAILED(source->GetDevice(IID_PPV_ARGS(&device)))||FAILED(object->GetDevice(IID_PPV_ARGS(&owner)))||!same(device.Get(),owner.Get())||object->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT){proof.valid=false;reject(s,"Native occlusion rejected resource/list device ownership");return;}
        Slot* storage{};for(auto& candidate:s.pool)if(candidate.image.use_count()==1&&!candidate.image->poisoned&&!candidate.reserved&&(!candidate.image->ready||candidate.image->ready->GetCompletedValue()>=candidate.writer_value)&&(!candidate.readers||candidate.readers->GetCompletedValue()>=candidate.reader_value)){storage=&candidate;break;}
        if(!storage){if(s.pool.size()>=6){proof.valid=false;reject(s,"Native occlusion waiting for capture/read fences");return;}s.pool.push_back({std::make_shared<Image>()});storage=&s.pool.back();}
        auto image=storage->image;
        const auto format=copy_format(desc.Format);if(format==DXGI_FORMAT_UNKNOWN)return;
        const auto old=image->texture?image->texture->GetDesc():D3D12_RESOURCE_DESC{};
        ComPtr<ID3D12Device> old_owner;if(image->texture)image->texture->GetDevice(IID_PPV_ARGS(&old_owner));
        if(!image->texture||old.Width!=desc.Width||old.Height!=desc.Height||old.Format!=format||!same(old_owner.Get(),device.Get())){
            auto d=desc;d.Alignment=0;d.Format=format;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;d.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;
            D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;image->texture.Reset();image->ready.Reset();image->value=0;storage->readers.Reset();storage->reader_value=storage->writer_value=0;
            if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&image->texture)))||FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&image->ready)))||FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&storage->readers)))){image->poisoned=true;proof.valid=false;reject(s,"Native occlusion depth copy allocation failed");return;}
        }
        storage->retired=false;
        struct Guard {Guard(){inserting=true;}~Guard(){inserting=false;}} guard;
        image->source=source;image->direct=false;image->poisoned=false;image->view_format=sample_format(desc.Format);image->extent.left=UINT(proof.viewport.TopLeftX);image->extent.top=UINT(proof.viewport.TopLeftY);image->extent.width=UINT(proof.viewport.Width);image->extent.height=UINT(proof.viewport.Height);image->epoch=s.epoch;image->frame=UINT(++s.sequence);image->viewport=0;
        transition(object,source,after,D3D12_RESOURCE_STATE_COPY_SOURCE,0);transition(object,image->texture.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST,0);
        D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=image->texture.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;src.pResource=source;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;object->CopyTextureRegion(&dst,0,0,0,&src,nullptr);transition(object,image->texture.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,0);transition(object,source,D3D12_RESOURCE_STATE_COPY_SOURCE,after,0);
        if(debug_logging()&&now>=s.next_copy_log){s.next_copy_log=now+1000;std::ostringstream trace;trace<<"Native capture proof: resource="<<source<<" format="<<desc.Format<<" allocation="<<desc.Width<<'x'<<desc.Height<<" depth_plane=0 source_state="<<after<<" viewport="<<proof.viewport.TopLeftX<<','<<proof.viewport.TopLeftY<<','<<proof.viewport.Width<<','<<proof.viewport.Height<<" present_frame="<<s.interval<<" cbv_resource="<<proof.buffer.resource.Get()<<" cbv_byte_offset="<<proof.buffer.offset<<" projection=";for(const auto& row:proof.camera.cameraViewToClip.row)trace<<row.x<<','<<row.y<<','<<row.z<<','<<row.w<<';';log(trace.str());}
        s.pending[object].push_back({image,proof.camera,proof.pose,proof.interval,desc.Width*desc.Height,proof.buffer,true,stage});++s.copies;proof.valid=false;s.reason="Native occlusion copy recorded; waiting for producer submission";return;
}
void barriers(ID3D12GraphicsCommandList* object,UINT count,const D3D12_RESOURCE_BARRIER* barriers) noexcept {try{
    auto& s=state();if(!s.enabled||inserting||!barriers)return;std::lock_guard lock(s.mutex);
    auto* list=entry(s,object);if(!list)return;auto& proof=list->proof;
    if(count>256){proof.valid=false;list->states.clear();reject(s,"Native occlusion rejected oversized barrier batch");return;}
    unsigned source_transitions{};for(UINT i=0;i<count;++i){const auto& b=barriers[i];if(b.Type==D3D12_RESOURCE_BARRIER_TYPE_ALIASING){proof.valid=false;list->states.clear();reject(s,"Native occlusion rejected aliasing barrier batch");return;}
        if(b.Type!=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION||!b.Transition.pResource)continue;
        if(b.Transition.Subresource!=0&&b.Transition.Subresource!=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)continue;
        if(list->states.size()<128||list->states.count(b.Transition.pResource))list->states[b.Transition.pResource]={b.Transition.StateAfter,b.Flags==D3D12_RESOURCE_BARRIER_FLAG_NONE};
        if(b.Transition.pResource==proof.resource)++source_transitions;
    }
    if(!proof.valid)return;
    if(source_transitions!=1){if(source_transitions>1){proof.valid=false;reject(s,"Native occlusion rejected multiple depth-plane transitions in one batch");}return;}
    for(UINT i=0;i<count;++i){const auto& b=barriers[i];
        if(b.Type!=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION||b.Transition.pResource!=proof.resource)continue;
        if(b.Flags!=D3D12_RESOURCE_BARRIER_FLAG_NONE){proof.valid=false;reject(s,"Native occlusion rejected split depth-plane transition");return;}
        if(b.Transition.Subresource!=0&&b.Transition.Subresource!=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)continue;
        // Only depth plane zero is copied. Stencil retains its independent state.
        const auto after=b.Transition.StateAfter;const auto allowed=D3D12_RESOURCE_STATE_DEPTH_READ|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        if(after!=D3D12_RESOURCE_STATE_COPY_SOURCE&&(!(after&allowed)||(after&~allowed)!=0)){proof.valid=false;reject(s,"Native occlusion invalidated by a non-readable depth state");return;}
        // With "Read transition" selected, require the game's depth buffer to
        // move out of its writable pass and become shader-readable. A mere
        // rebind/unbind is not proof that opaque environment geometry finished.
        if(s.heuristic_mode && s.capture_boundary_mode.load()!=0){
            const auto choice=s.capture_boundary_mode.load();
            if(choice!=1)return;
            const auto before_state=b.Transition.StateBefore;
            if(before_state!=D3D12_RESOURCE_STATE_DEPTH_WRITE||!(after&D3D12_RESOURCE_STATE_DEPTH_READ))return;
        }
        const auto before=s.copies;capture_locked(s,object,*list,after,1);if(s.copies>before&&b.Transition.Subresource==0)++s.plane_copies;return;
    }
}catch(...){state().enabled=false;++state().errors;}}
void before_bind(ID3D12GraphicsCommandList* object,D3D12_CPU_DESCRIPTOR_HANDLE handle) noexcept {try{
    auto& s=state();if(!s.enabled||inserting)return;std::lock_guard lock(s.mutex);auto found=s.lists.find(object);if(found==s.lists.end())return;auto& l=found->second;
    if(handle.ptr==l.descriptor.handle)return;
    if(s.heuristic_mode && s.capture_boundary_mode.load()!=0 && s.capture_boundary_mode.load()!=2)return;
    const auto known=l.states.find(l.proof.resource);
    if(known!=l.states.end()&&known->second.known){const auto before=s.copies;capture_locked(s,object,l,known->second.value,2);if(s.copies>before)++s.unbind_copies;}
}catch(...){state().enabled=false;++state().errors;}}
void before_clear(ID3D12GraphicsCommandList* object,D3D12_CPU_DESCRIPTOR_HANDLE handle,D3D12_CLEAR_FLAGS flags) noexcept {try{
    auto& s=state();if(!s.enabled||inserting||!(flags&D3D12_CLEAR_FLAG_DEPTH))return;
    tracking::Record descriptor{};const bool descriptor_known=tracking::lookup(handle.ptr,descriptor)&&descriptor.resource;
    std::lock_guard lock(s.mutex);
    if(s.heuristic_mode)++s.heuristic_clears_seen;
    if(!descriptor_known)return;
    if(s.heuristic_mode && s.capture_boundary_mode.load()!=0 && s.capture_boundary_mode.load()!=3)return;
    auto found=s.lists.find(object);if(found==s.lists.end())return;
    auto& recording=found->second;
    if(s.heuristic_mode){
        // ReShade keeps workload keyed by resource, NOT by whichever depth was
        // most recently bound to this command list. The old code required
        // recording.proof.resource == descriptor.resource here, discarding all
        // legitimate pre-clear opportunities seen in the field logs.
        auto work=recording.work.find(descriptor.resource);
        if(work==recording.work.end()||work->second.draws<UINT64(s.minimum_scene_draw_count.load())||!work->second.has_viewport){
            ++s.heuristic_clears_without_work;return;
        }
        ++s.heuristic_clears_with_work;
        if(!s.preferred||s.preferred.Get()!=descriptor.resource){
            ++s.heuristic_clears_no_selection;return;
        }
        const auto& w=work->second;
        if(recording.in_pass||recording.unsupported_pass||!s.scene.camera.valid||!s.scene.at||
           GetTickCount64()-s.scene.at>100||w.viewport.Width<=0||w.viewport.Height<=0)return;
        // A previous whole-depth clear is needed to establish the direction of
        // Z. Never guess here or copy unknown content just to increment counts.
        const auto prev=s.clears.find(descriptor.resource);
        if(prev==s.clears.end()||GetTickCount64()-prev->second.at>5000||
           (prev->second.value!=0.f&&prev->second.value!=1.f)){
            ++s.heuristic_clears_missing_clear_history;return;
        }
        // Use the same display-aspect projection for pre-clear copies.
        if(!w.camera.valid)return;
        const auto& recording_camera=w.camera;
        auto camera=approximate_camera(recording_camera,reshade::projection_aspect(s.width,s.height,double(w.viewport.Width)/w.viewport.Height),prev->second.value==0.f,s.estimated_near_plane.load());
        if(!valid_camera(camera))return;
        // The explicit DSV descriptor, not a different DSV's last binding,
        // describes the depth resource about to be cleared.
        const auto previous_descriptor=recording.descriptor;
        recording.descriptor=descriptor;
        recording.proof={descriptor.resource,camera,recording_camera,w.viewport,GetTickCount64(),s.interval,true,{}};
        ++s.heuristic_copy_attempts;
        const auto before=s.copies;
        // D3D12 mandates DEPTH_WRITE for a ClearDepthStencilView operation.
        // Insertion happens before the original clear in depth_observer.cpp.
        capture_locked(s,object,recording,D3D12_RESOURCE_STATE_DEPTH_WRITE);
        recording.descriptor=previous_descriptor;
        if(s.copies>before)++s.clear_copies;
        return;
    }
    if(recording.proof.resource!=descriptor.resource)return;
    const auto before=s.copies;capture_locked(s,object,recording,D3D12_RESOURCE_STATE_DEPTH_WRITE);if(s.copies>before)++s.clear_copies;
}catch(...){state().enabled=false;++state().errors;}}
void begin_pass(ID3D12GraphicsCommandList* object,const D3D12_RENDER_PASS_DEPTH_STENCIL_DESC* depth,D3D12_RENDER_PASS_FLAGS flags) noexcept {try{
    // Render-pass load-op clears bypass ClearDepthStencilView: they are the
    // other important ReShade-style before-clear capture boundary. The hook
    // runs before the game's BeginRenderPass command is recorded.
    if(depth&&depth->DepthBeginningAccess.Type==D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR)
        before_clear(object,depth->cpuDescriptor,D3D12_CLEAR_FLAG_DEPTH);
    bind(object,depth?depth->cpuDescriptor:D3D12_CPU_DESCRIPTOR_HANDLE{});
    if(depth&&depth->DepthBeginningAccess.Type==D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR)clear(object,depth->cpuDescriptor,D3D12_CLEAR_FLAG_DEPTH,depth->DepthBeginningAccess.Clear.ClearValue.DepthStencil.Depth,0);
    auto& s=state();if(!s.enabled||inserting)return;std::lock_guard lock(s.mutex);++s.pass_begins;if(auto* l=entry(s,object)){l->in_pass=true;l->discard_depth=!depth||depth->DepthEndingAccess.Type!=D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;l->unsupported_pass|=(flags&(D3D12_RENDER_PASS_FLAG_SUSPENDING_PASS|D3D12_RENDER_PASS_FLAG_RESUMING_PASS))!=0;}
}catch(...){state().enabled=false;++state().errors;}}
void end_pass(ID3D12GraphicsCommandList* object) noexcept {try{auto& s=state();if(!s.enabled||inserting)return;std::lock_guard lock(s.mutex);++s.pass_ends;auto found=s.lists.find(object);if(found==s.lists.end())return;auto& l=found->second;l.in_pass=false;
    if(l.unsupported_pass){l.proof.valid=false;return;}
    if(l.discard_depth){l.proof.valid=false;reject(s,"Native occlusion rejected render-pass depth discard/resolve");return;}
    if(s.heuristic_mode && s.capture_boundary_mode.load()!=0)return;
    const auto known=l.states.find(l.proof.resource);if(known!=l.states.end()&&known->second.known){const auto before=s.copies;capture_locked(s,object,l,known->second.value);if(s.copies>before)++s.pass_copies;}
}catch(...){state().enabled=false;++state().errors;}}
void reset(ID3D12GraphicsCommandList* object) noexcept {try{auto& s=state();std::lock_guard lock(s.mutex);s.lists.erase(object);s.recordings.erase(object);const auto p=s.pending.find(object);if(p!=s.pending.end()){s.discarded+=p->second.size();s.pending.erase(p);}}catch(...){++state().errors;}}
bool reserve(const std::shared_ptr<const Image>& image) noexcept {if(!image)return true;try{auto& s=state();std::lock_guard lock(s.mutex);auto* item=slot(s,image.get());if(!item)return true;if(item->retired||image->poisoned||item->reserved)return false;++item->reader_value;item->reserved=true;return true;}catch(...){++state().errors;return false;}}
void reader_end(const std::shared_ptr<const Image>& image,ID3D12CommandQueue* queue) noexcept {if(!image)return;try{auto& s=state();std::lock_guard lock(s.mutex);auto* item=slot(s,image.get());if(!item||!item->reserved)return;
    if(!queue||FAILED(queue->Signal(item->readers.Get(),item->reader_value))){item->image->poisoned=true;s.enabled=false;++s.errors;}
    item->reserved=false;s.readers_posted.notify_all();
}catch(...){++state().errors;}}
void before_submit(ID3D12CommandQueue* queue,UINT count,ID3D12CommandList*const* objects) noexcept {try{
    auto& s=state();std::unique_lock lock(s.mutex);if(!objects)return;
    for(UINT i=0;i<count;++i){auto p=s.pending.find(objects[i]);if(p!=s.pending.end())for(auto& packet:p->second)if(!cbv::unchanged(packet.buffer)){packet.valid=false;reject(s,"Native occlusion rejected changed CBV projection before submission");}}
    if(s.recordings.empty())return;std::vector<std::shared_ptr<Image>> repeated;
    for(UINT i=0;i<count;++i){const auto found=s.recordings.find(objects[i]);if(found==s.recordings.end())continue;for(auto& p:found->second){if(auto* item=slot(s,p.image.get()))item->retired=true;if(s.latest.image==p.image)s.latest={};
        if(s.replacement.image==p.image)s.replacement={};
        repeated.push_back(p.image);}}
    if(repeated.empty())return;
    // Wait only for CPU posting of already-reserved TV reader completion signals.
    // This avoids a same-queue wait on a signal that has not yet been enqueued.
    s.readers_posted.wait(lock,[&]{for(const auto& image:repeated){auto* item=slot(s,image.get());if(item&&item->reserved)return false;}return true;});
    for(const auto& image:repeated){auto* item=slot(s,image.get());if(item&&item->reader_value&&FAILED(queue->Wait(item->readers.Get(),item->reader_value))){s.enabled=false;++s.errors;}}
    s.reason="Native occlusion retired a repeated closed command list until reset";
}catch(...){state().enabled=false;++state().errors;}}
void submitted(ID3D12CommandQueue* queue,UINT count,ID3D12CommandList*const* objects) noexcept {try{
    auto& s=state();std::lock_guard lock(s.mutex);if(!objects)return;
    for(UINT i=0;i<count;++i){const auto recorded=s.lists.find(objects[i]);if(recorded==s.lists.end())continue;for(const auto& e:recorded->second.scores)if(s.submitted_scores.size()<64||s.submitted_scores.count(e.first)){auto& score=s.submitted_scores[e.first];score.resource=e.second.resource;score.draws+=e.second.draws;score.vertices+=e.second.vertices;}}
    for(UINT i=0;i<count;++i){const auto found=s.pending.find(objects[i]);if(found==s.pending.end()){
        const auto old=s.recordings.find(objects[i]);if(old!=s.recordings.end())for(auto& packet:old->second){auto* item=slot(s,packet.image.get());if(item&&FAILED(queue->Signal(packet.image->ready.Get(),++item->writer_value))){packet.image->poisoned=true;++s.errors;}}continue;}
        for(auto& packet:found->second){auto& image=*packet.image;auto* item=slot(s,&image);if(!item)continue;image.value=++item->writer_value;
            if(FAILED(queue->Signal(image.ready.Get(),image.value))){image.poisoned=true;reject(s,"Native occlusion producer fence signal failed");continue;}
            if(!s.enabled||!packet.valid||image.poisoned||image.epoch!=s.epoch||packet.interval==s.ambiguous_interval)continue;
            image.at=GetTickCount64();
            if(s.heuristic_mode&&s.challenger&&image.source.Get()==s.challenger.Get()){
                // Fence has been signalled, but may not yet be GPU complete.
                // Keep an owning reference to the image until promotion or
                // challenger cancellation; do not overwrite the live capture.
                if(!s.replacement.image){
                    // Keep the first eligible candidate until its GPU fence
                    // finishes. Replacing it every submission could otherwise
                    // starve promotion indefinitely at high frame rates.
                    s.replacement=packet;++s.continuity_challenger_copies;
                }
                continue;
            }
            if(s.heuristic_mode&&s.preferred&&image.source.Get()!=s.preferred.Get())continue;
            const bool hold_finished_pass=s.latest.image && capture_policy::keep_completed_pass(
                s.heuristic_mode.load(),s.capture_boundary_mode.load(),s.latest.stage,packet.stage,
                s.latest.image->at,image.at);
            if(hold_finished_pass){
                ++s.continuity_stage_holds;
                continue; // wait for the next end-of-pass copy, not an incomplete pass
            }
            if(!s.latest.image||packet.interval>s.latest.interval||(packet.interval==s.latest.interval&&packet.area>=s.latest.area)){
                if(s.heuristic_mode){
                    if(packet.stage==2)++s.continuity_unbind_published;
                    else if(packet.stage==1)++s.continuity_transition_fallbacks;
                }
                s.latest=packet;++s.published;s.reason="Native occlusion native depth copy published (experimental)";
            }
        }s.recordings[objects[i]]=found->second;s.pending.erase(found);
    }
}catch(...){state().enabled=false;++state().errors;}}
Sample snapshot(ID3D12Device* device,const Scene& scene,unsigned width,unsigned height) noexcept {Sample sample;try{
    auto& s=state();std::lock_guard lock(s.mutex);if(!s.enabled)return sample;
    promote_ready_challenger(s);
    if(!s.latest.image)return sample;
    const auto& image=*s.latest.image;
    const auto now=GetTickCount64();
    const auto age=now>=image.at?now-image.at:UINT64_MAX;
    // ReShade depth is an independent GPU copy. When the game briefly fails
    // to publish a new capture, re-use only a *still-owned, unpoisoned* image
    // with a matching camera, within two typical display frames of the old
    // cutoff. Never extend the actual Streamline/native-only acceptance rules.
    const auto max_age=s.heuristic_mode.load()?120ull:75ull;
    if(image.epoch!=s.epoch||image.poisoned||!image.ready||!image.value||age>max_age||s.latest.interval==s.ambiguous_interval){
        if(s.heuristic_mode)++s.continuity_depth_misses;
        reject(s,"Native occlusion rejected stale/invalid capture");return sample;
    }
    if(s.heuristic_mode&&s.latest.pose.sampled_at&&(now<s.latest.pose.sampled_at||now-s.latest.pose.sampled_at>120)){reject(s,"ReShade capture camera expired; preserving video without depth");return sample;}
    const bool strict_pose=pose_matches(s.latest.pose,scene.camera);
    const bool reshade_pose=s.heuristic_mode.load() && pose_matches_reshade(s.latest.pose,scene.camera,age);
    if(width!=s.width||height!=s.height||(!strict_pose&&!reshade_pose)){
        if(s.heuristic_mode)++s.continuity_depth_misses;
        reject(s,"Native occlusion rejected camera motion or changed output");return sample;
    }
    // An image can be scheduled for reuse by the producer. In that case
    // reserve() in the renderer still rejects it; do not bypass the existing
    // producer/reader fences or invent a depth sample.
    if(s.heuristic_mode && age>75)++s.continuity_depth_grace;
    if(reshade_pose&&!strict_pose)++s.continuity_pose_grace;
    ComPtr<ID3D12Device> owner;if(FAILED(image.texture->GetDevice(IID_PPV_ARGS(&owner)))||!same(device,owner.Get())){reject(s,"Native occlusion rejected foreign device");return sample;}
    sample.image=s.latest.image;sample.camera=s.latest.camera;sample.capture_pose=s.latest.pose;sample.capture_pose_valid=s.heuristic_mode.load()&&s.latest.pose.valid;s.reason=s.heuristic_mode?"Occlusion active: ReShade-style DX12 depth copy (estimated native projection)":(s.latest.buffer.resource?"Occlusion active: experimental native depth and upload-CBV projection":"Occlusion active: experimental native depth and inline projection");
}catch(...){++state().errors;}return sample;}
std::string status(){auto& s=state();std::lock_guard lock(s.mutex);return !s.enabled&&s.errors.load()?"Native occlusion disabled after an error; playback preserved":s.reason;}
void report() noexcept {if(!debug_logging())return;try{auto& s=state();std::lock_guard lock(s.mutex);std::ostringstream out;out<<"Native prototype: build=native-proto3 enabled="<<s.enabled.load()<<" lifetime_ready="<<s.lifetime_ready.load()<<" reshade_style="<<s.heuristic_mode.load()<<" reshade_boundary="<<s.capture_boundary_mode.load()<<" reshade_min_draws="<<s.minimum_scene_draw_count.load()<<" reshade_near="<<s.estimated_near_plane.load()<<" reshade_draws="<<s.heuristic_draws<<" reshade_proofs="<<s.heuristic_proofs<<" reshade_actual_projections="<<s.heuristic_matched_projection<<" reshade_before_clear_attempts="<<s.heuristic_copy_attempts<<" reshade_clears_seen="<<s.heuristic_clears_seen<<" reshade_clears_with_work="<<s.heuristic_clears_with_work<<" reshade_clears_without_work="<<s.heuristic_clears_without_work<<" reshade_clears_not_selected="<<s.heuristic_clears_no_selection<<" reshade_clears_without_direction="<<s.heuristic_clears_missing_clear_history<<" reshade_filtered="<<s.heuristic_rejected<<" continuity_stage_holds="<<s.continuity_stage_holds<<" continuity_unbind_published="<<s.continuity_unbind_published<<" continuity_transition_fallbacks="<<s.continuity_transition_fallbacks<<" continuity_depth_grace="<<s.continuity_depth_grace<<" continuity_depth_misses="<<s.continuity_depth_misses<<" continuity_preferred_held="<<s.continuity_preferred_held<<" continuity_switch_waits="<<s.continuity_switch_waits<<" continuity_challenger_copies="<<s.continuity_challenger_copies<<" continuity_switches_committed="<<s.continuity_switches_committed<<" continuity_pose_grace="<<s.continuity_pose_grace<<" challenger_streak="<<s.switch_policy.consecutive<<" challenger_resource="<<s.challenger.Get()<<" projection_inputs="<<s.projection_inputs.load()<<" buffer_projection_inputs="<<s.buffer_inputs<<" projection_compared="<<s.projection_compared<<" projection_matches="<<s.projection_matches_count<<" projection_ties="<<s.projection_ties<<" draws="<<s.draws<<" copies_recorded="<<s.copies<<" published="<<s.published<<" rejected="<<s.rejects<<" reset_discards="<<s.discarded<<" errors="<<s.errors.load()<<" pool="<<s.pool.size()<<" pending_lists="<<s.pending.size()<<" retained_recordings="<<s.recordings.size()<<" present_frame="<<s.interval<<" preferred_resource="<<s.preferred.Get()<<" depth_plane_copies="<<s.plane_copies<<" before_clear_copies="<<s.clear_copies<<" before_unbind_copies="<<s.unbind_copies<<" end_pass_copies="<<s.pass_copies<<" render_pass_begins="<<s.pass_begins<<" render_pass_ends="<<s.pass_ends<<" requested_backend="<<backend_name(backend())<<" reason="<<s.reason;log(out.str());for(const auto& reason:s.rejection_counts)log("Native rejection total: count="+std::to_string(reason.second)+" reason="+reason.first);}catch(...){++state().errors;}}
#ifdef TV_GPU_TEST
void test_recording_camera(const Camera& camera) noexcept {fixture_camera=camera;fixture_camera.sampled_at=GetTickCount64();fixture_camera.source=CameraSource::Recording;++fixture_camera.sequence;}
bool test_boundaries() noexcept {auto& s=state();std::lock_guard lock(s.mutex);return s.plane_copies&&s.pass_copies&&s.clear_copies&&s.unbind_copies&&s.pass_begins&&s.pass_ends;}
bool test_contracts() noexcept {virtualtv::Camera c;c.valid=true;c.vertical_fov=90;c.world={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};std::array<float,16> p{1,0,0,0,0,1,0,0,0,0,0,-1,0,0,.1f,0};if(!projection_matches(p,c,1)||projection_matches(p,c,2))return false;auto moved=c;moved.world[12]=1;if(pose_matches(c,moved))return false;p[15]=1;return !projection_matches(p,c,1);}
#endif
}
