#include "tv_runtime.h"
#include "gate_trace.h"
#include "native_contracts.h"
#include "object_motion.h"
#include "native_placement_reader.h"
#include "tv_asset_presets.h"
#include "new_placement_policy.h"
#include <set>
#include "Engine/Game/World/park_editor.h"
#include <intrin.h>
#include <fstream>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <deque>
namespace virtualtv {
namespace {
template<class T> bool read(std::uintptr_t p,T& out) { SIZE_T n{};return p>=0x10000 && ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(p),&out,sizeof out,&n)&&n==sizeof out; }
std::uintptr_t base{};std::mutex mutex;Scene scene;Identity selected{},saved{};std::wstring message=L"Native attachment starting";
NearbySnapshot nearby; Vec3 player_position{}; ULONGLONG player_at{}, next_scan{}; bool player_valid{}; Identity requested{}; Identity locked_identity{}; ObjectMotion movement_tracker{}; ULONGLONG object_missing_since{};
struct LiveOutput {
    OutputAnchor output; ObjectMotion tracking;
    ULONGLONG last_seen{}, missing_since{};
};
struct NewOutput {Identity id;OutputKind kind;Screen screen;};
std::vector<LiveOutput> live_outputs;
std::deque<NewOutput> output_requests;
std::vector<attachment_store::Record> persisted_outputs;
struct NamedPlacement { Identity identity; std::string item; Vec3 position; };
std::vector<NamedPlacement> named_placements, remote_placements;
std::vector<PlacementName> local_catalog, remote_catalog;
bool borrowed_active{};
std::vector<BorrowedOutput> borrowed_specs, borrowed_live;
ULONGLONG catalog_scan_at{};
std::uint64_t persisted_revision=1;
ULONGLONG last_restoration_scan{};
std::atomic<bool> auto_restored_screen{};
std::vector<std::pair<std::string,Screen>> automatic_screens;
NewPlacementPolicy new_speakers;std::uint64_t last_editor_revision{};
std::set<std::uint64_t> pending_speaker_ids;
std::set<std::pair<std::uint64_t,std::uint32_t>> auto_detach_suppressed;

bool same_key(const attachment_store::Record& rec, Identity id){return attachment_store::key_matches(rec,id.instance,id.drop);}
const NamedPlacement* named(Identity id){
    auto it=std::find_if(named_placements.begin(),named_placements.end(),[&](const auto& n){return n.identity==id;});
    if(it!=named_placements.end())return &*it;
    auto remote=std::find_if(remote_placements.begin(),remote_placements.end(),[&](const auto& n){return n.identity==id;});
    return remote==remote_placements.end()?nullptr:&*remote;
}
void remember_output(Identity id,OutputKind kind,const Screen& alignment,bool primary,Vec3 position){
    if(!id.instance || !valid(Placement{},alignment))return;
    attachment_store::Record next{id.instance,id.drop,primary?0u:(kind==OutputKind::Screen?1u:2u),alignment};
    next.origin=position;next.has_origin=attachment_store::reasonable(position);
    if(const auto* n=named(id))next.asset=n->item;
    if(primary)for(auto& r:persisted_outputs)if(r.role==0&&!same_key(r,id))r.role=1;
    auto it=std::find_if(persisted_outputs.begin(),persisted_outputs.end(),[&](const auto& r){
        return same_key(r,id)||(!next.asset.empty() && attachment_store::anchor_matches(r,{next.instance,next.drop,next.asset,next.origin}));
    });
    if(it!=persisted_outputs.end())*it=next;
    else if(persisted_outputs.size()<128)persisted_outputs.push_back(next);
    ++persisted_revision;
}
void update_saved_pose(Identity id,Vec3 position){
    for(auto& r:persisted_outputs){
        if(!same_key(r,id))continue;
        const auto previous=r.asset;
        if(r.asset.empty())if(const auto* n=named(id))r.asset=n->item;
        if(!r.has_origin||attachment_store::squared_distance(r.origin,position)>.01f){
            r.origin=position;r.has_origin=attachment_store::reasonable(position);++persisted_revision;
        }else if(r.asset!=previous)++persisted_revision;
    }
}
void forget_output(Identity id){
    if(!id.instance)return;
    auto end=std::remove_if(persisted_outputs.begin(),persisted_outputs.end(),[&](const auto& r){return same_key(r,id);});
    if(end!=persisted_outputs.end()){persisted_outputs.erase(end,persisted_outputs.end());++persisted_revision;}
}
bool primary_was_attached{};
std::atomic<unsigned> commands{};
std::atomic<bool> stopping{}, source_verified{}, source_active{}, native_query_fault{};
std::atomic<unsigned> native_query_fault_count{};
std::atomic<ULONGLONG> next_poll_due{};
std::atomic_flag polling = ATOMIC_FLAG_INIT;
std::uint64_t process_session{},epoch=1;std::uintptr_t previous_context{},previous_world{};bool playing{};
template<class T> T fn(std::uintptr_t rva){return reinterpret_cast<T>(base+rva);}
using Resolve=void*(*)(void*,std::uint64_t,bool);using Valid=bool(*)(const void*);using Query=void*(*)(void*,std::uint64_t);using Pose=float*(*)(float*,const void*);
// File verification uses section RVAs, never treats an RVA as an on-disk offset.
bool disk_match(std::uintptr_t rva,const unsigned char* bytes,std::size_t count) {
    wchar_t path[32768]{};if(!GetModuleFileNameW(reinterpret_cast<HMODULE>(base),path,32768))return false;
    std::ifstream f(path,std::ios::binary);IMAGE_DOS_HEADER dos{};f.read(reinterpret_cast<char*>(&dos),sizeof dos);
    if(dos.e_magic!=IMAGE_DOS_SIGNATURE)return false;f.seekg(dos.e_lfanew);IMAGE_NT_HEADERS64 nt{};f.read(reinterpret_cast<char*>(&nt),sizeof nt);
    if(nt.Signature!=IMAGE_NT_SIGNATURE||nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64)return false;
    for(unsigned i=0;i<nt.FileHeader.NumberOfSections;++i){IMAGE_SECTION_HEADER s{};f.read(reinterpret_cast<char*>(&s),sizeof s);
        if(rva>=s.VirtualAddress && rva-s.VirtualAddress+count<=s.SizeOfRawData && (s.Characteristics&IMAGE_SCN_MEM_EXECUTE)){
            f.seekg(s.PointerToRawData+rva-s.VirtualAddress);std::vector<unsigned char> got(count);f.read(reinterpret_cast<char*>(got.data()),count);return f.good()&&std::equal(got.begin(),got.end(),bytes);
        }
    }return false;
}
bool contract(std::uintptr_t rva,const unsigned char* bytes,std::size_t count,bool allow_chain=false) {
    if(!disk_match(rva,bytes,count))return false;
    std::vector<unsigned char> got(count);SIZE_T n{};if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(base+rva),got.data(),count,&n)||n!=count)return false;
    if(std::equal(got.begin(),got.end(),bytes))return true;
    if(!allow_chain)return false;
    // Only chain a previously installed jump owned by one of the reference mods.
    std::uintptr_t dest{};if(got[0]==0xe9){std::int32_t rel{};std::memcpy(&rel,got.data()+1,4);dest=base+rva+5+rel;}
    else if(got[0]==0xff&&got[1]==0x25){std::int32_t rel{};std::memcpy(&rel,got.data()+2,4);if(!read(base+rva+6+rel,dest))return false;}
    HMODULE owner{};wchar_t path[32768]{};
    // Detours/MinHook may put a near relay in allocated executable memory.
    for(unsigned hop=0;dest&&hop<4;++hop){
        if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(dest),&owner))break;
        std::array<unsigned char,16> jump{};if(!read(dest,jump))return false;
        std::int32_t relative{};
        if(jump[0]==0xe9){std::memcpy(&relative,jump.data()+1,4);dest=dest+5+relative;}
        else if(jump[0]==0xff&&jump[1]==0x25){std::memcpy(&relative,jump.data()+2,4);std::uintptr_t next{};if(!read(dest+6+relative,next))return false;dest=next;}
        else return false;
    }
    if(!owner||!GetModuleFileNameW(owner,path,32768))return false;
    auto name=std::filesystem::path(path).filename().wstring();std::transform(name.begin(),name.end(),name.begin(),::towlower);
    return name==L"reskate.dll"||name==L"jishsfirstpersonmod.dll";
}
bool verify(const contracts::Fingerprint& c) {return contract(c.rva,c.bytes.data(),c.bytes.size());}
bool identity(std::uint64_t entity,std::uint64_t session,Identity& id) {
    alignas(16) std::array<std::uint64_t,2> root{};
    if(fn<Resolve>(0x6fdb00)(root.data(),entity,false)!=root.data()||root[0]!=entity||!fn<Valid>(0x710e10)(root.data()))return false;
    alignas(16) std::array<std::uint64_t,5> component{};fn<void*(*)(const void*,void*)>(0x46aa70)(&entity,component.data());
    Identity candidate;candidate.entity=entity;candidate.session=session;
    if(!component[1]||!component[2]||component[3]!=entity||!read(component[2]+0x24,candidate.drop)||candidate.drop==UINT32_MAX||!read(component[2]+8,candidate.instance)||!candidate.instance)return false;
    id=candidate;return true;
}
bool transform(const Identity& id,Placement& p,Vec3& scale) {
    alignas(16) std::array<std::uint64_t,3> query{};alignas(16) std::array<float,12> pose{};
    if(fn<Query>(0x276f3e0)(query.data(),id.entity)!=query.data()||fn<Pose>(0x287ac10)(pose.data(),query.data())!=pose.data())return false;
    for(unsigned i:{0u,1u,2u,4u,5u,6u,7u,8u,9u,10u})if(!std::isfinite(pose[i]))return false;
    p.position={pose[8],pose[9],pose[10]};p.rotation={pose[4],pose[5],pose[6],pose[7]};p.scale=1;scale={pose[0],pose[1],pose[2]};
    return scale.x>0&&scale.y>0&&scale.z>0&&valid(p,Screen{});
}
bool camera(Camera& c, bool attached_for_log) {
    c.diagnostic="not_sampled";c.active_vtable=0;c.view_count=0;c.basis_norms={};
    const auto reject=[&](const char* reason){c.diagnostic=reason;return false;};
    std::uintptr_t manager{},vt{},begin{},end{},view{},active{};
    if(!read(base+0x75cbc48,manager)||!read(manager,vt))return reject("manager_read_failed");
    if(vt!=base+0x63ece68)return reject("manager_type_rejected");
    if(!read(manager+8,begin)||!read(manager+0x10,end))return reject("view_list_read_failed");
    if(end<begin||(end-begin)%8||(end-begin)/8>32)return reject("view_list_bounds_rejected");
    c.view_count=static_cast<unsigned>((end-begin)/8);
    for(auto it=begin;it<end;it+=8){std::uintptr_t v{};std::uint32_t local{},kind{};unsigned char enabled{};
        if(!read(it,v)||!read(v,vt))return reject("view_read_failed");
        if(vt!=base+0x63ed0a0)return reject("view_type_rejected");
        if(!read(v+0x148,local)||!read(v+0x14c,kind))return reject("view_identity_read_failed");
        if(!local&&!kind){
            if(view)return reject("multiple_local_views");view=v;
            if(!read(v+0x45d,enabled))return reject("view_enabled_read_failed");
            if(!enabled)return reject("local_view_disabled");
            if(!read(v+0x2b0,active))return reject("active_camera_read_failed");
        }
    }
    if(!view)return reject("no_local_view");
    if(!read(active,vt))return reject("active_camera_type_read_failed");
    c.active_vtable=vt>=base?vt-base:vt;
    if(vt!=base+0x6255870&&vt!=base+0x63fc220)return reject("active_camera_type_rejected");
    if(!read(active+0x50,c.world)||!read(active+0xac,c.vertical_fov))return reject("camera_pose_read_failed");
    bool matrix_nonfinite=false;
    for(auto x:c.world)if(!std::isfinite(x))matrix_nonfinite=true;
    // Log raw native values before normalizing unused SIMD lanes.
    static GateTrace matrix_trace;
    matrix_trace.sample("camera_components",matrix_nonfinite?"nonfinite":"finite",[&]{
        std::ostringstream out;out<<"index_base=0 camera_vtable_rva=0x"<<std::hex<<c.active_vtable<<std::dec<<" fov="<<c.vertical_fov<<" attached="<<attached_for_log<<" components=";
        for(unsigned i=0;i<c.world.size();++i){
            std::uint32_t bits{};std::memcpy(&bits,&c.world[i],sizeof bits);
            out<<'['<<i<<":value="<<c.world[i]<<",bits=0x"<<std::hex<<bits<<std::dec<<",shader_xyz="<<(i%4!=3)<<",finite="<<std::isfinite(c.world[i])<<']';
        }
        return out.str();
    });
    // The shader consumes only XYZ from each column. Native W lanes can hold
    // NaNs even when the camera pose is valid; never use them for acceptance.
    for(unsigned i=0;i<c.world.size();++i)
        if(i%4!=3&&!std::isfinite(c.world[i]))return reject("camera_matrix_nonfinite");
    c.world[3]=0;c.world[7]=0;c.world[11]=0;c.world[15]=1;
    if(!std::isfinite(c.vertical_fov)||c.vertical_fov<=1||c.vertical_fov>=175)return reject("camera_fov_rejected");
    for(unsigned i:{0u,4u,8u}){float n=0;for(unsigned j=0;j<3;++j)n+=c.world[i+j]*c.world[i+j];c.basis_norms[i/4]=n;if(std::abs(n-1)>0.05f)return reject("camera_basis_norm_rejected");}
    c.valid=true;c.diagnostic="valid";return true;
}

float distance(Vec3 a,Vec3 b){const auto x=a.x-b.x,y=a.y-b.y,z=a.z-b.z;return std::sqrt(x*x+y*y+z*z);}
void scan(std::uintptr_t context,std::uint64_t session){
    nearby.objects.clear();nearby.at=GetTickCount64();
    if(!player_valid||nearby.at-player_at>1500){nearby.status=L"Waiting for current player position; keep this menu open";return;}
    std::uint32_t offset{};unsigned char ready{};std::uintptr_t begin{},end{};
    if(!read(base+0x7215038,offset)||offset>0x1000000||!read(context+offset+0x100,ready)||!ready||
       !read(context+offset+0x48,begin)||!read(context+offset+0x50,end)||
       !native_placement_reader::valid_span(begin,end)){
        nearby.status=L"Placement store unavailable";return;
    }
    for(auto record=begin;record<end;record+=native_placement_reader::record_bytes){
        std::array<unsigned char,native_placement_reader::record_bytes> bytes{};
        if(!read(record,bytes)){
            nearby.objects.clear();nearby.status=L"Placement records changed; retrying";return;
        }
        const auto first=native_placement_reader::decode(bytes);
        if(!first.entity||first.active==0x7f)continue;
        NearbyObject row;Vec3 scale;
        if(!identity(first.entity,session,row.identity)||row.identity.drop!=first.group||
           !transform(row.identity,row.placement,scale))continue;
        row.scale=scale;
        // Verify that a concurrent park edit did not replace this slot while
        // its identity and transform were being read.
        if(!read(record,bytes) ||
           !native_placement_reader::same_object(first,native_placement_reader::decode(bytes)))continue;
        nearby.objects.push_back(row);
    }
    filter_nearby(nearby.objects,player_position);
    nearby.status=std::to_wstring(nearby.objects.size())+L" live placed objects within 100 m (nearest first)";
    static std::size_t previous=SIZE_MAX;if(previous!=nearby.objects.size()){previous=nearby.objects.size();debug_log("Nearby placement scan: records="+std::to_string((end-begin)/0x50)+" within_100m="+std::to_string(previous));}
}
void update(std::uintptr_t context) {
    std::lock_guard lock(mutex);scene.at=GetTickCount64();scene.camera.valid=false;scene.camera.diagnostic="not_sampled";
    const char* update_reason="updated";
    struct TraceUpdate {
        Scene& s;const char*& reason;
        ~TraceUpdate(){static GateTrace trace;trace.sample("native_snapshot",reason,[&]{std::ostringstream out;out<<"attached="<<s.attached<<" entity="<<s.identity.entity<<" camera_valid="<<s.camera.valid<<" camera_reason="<<s.camera.diagnostic<<" camera_vtable_rva=0x"<<std::hex<<s.camera.active_vtable<<std::dec<<" views="<<s.camera.view_count<<" fov="<<s.camera.vertical_fov<<" basis_norms="<<s.camera.basis_norms[0]<<','<<s.camera.basis_norms[1]<<','<<s.camera.basis_norms[2];return out.str();});}
    } trace_update{scene,update_reason};
    if(commands.fetch_and(~4u)&4){if(scene.identity.instance)auto_detach_suppressed.insert({scene.identity.instance,scene.identity.drop});forget_output(scene.identity);scene.attached=false;primary_was_attached=false;scene.identity={};locked_identity={};movement_tracker.reset();selected={};saved={};message=L"Detached";log("TV detached");}
    // Same client realm and native 0x50-byte object store that ReSkate's
    // network-object runtime uses. No executable patch, tick detour or TLS
    // reconstruction: the already-installed host presentation callback drives
    // this bounded, read-only observation.
    std::uint32_t store_offset{};std::uint8_t ready{};
    std::uintptr_t first{},last{};
    const bool available=context && read(base+0x7215038,store_offset) && store_offset<=0x1000000 &&
        read(context+store_offset+0x100,ready) && ready &&
        read(context+store_offset+0x48,first) && read(context+store_offset+0x50,last) &&
        native_placement_reader::valid_span(first,last);
    if(!available){
        if(playing){new_speakers.reset();pending_speaker_ids.clear();++epoch;selected={};locked_identity={};scene.attached=false;primary_was_attached=false;
            message=L"Placement store unavailable; waiting for active world";}
        playing=false;previous_context=0;previous_world=0;live_outputs.clear();output_requests.clear();named_placements.clear();local_catalog.clear();remote_catalog.clear();remote_placements.clear();borrowed_specs.clear();borrowed_live.clear();borrowed_active=false;catalog_scan_at=0;
        nearby.objects.clear();nearby.at=0;nearby.status=L"Waiting for ReSkate's native placed-object store";
        update_reason="placement_store_not_ready";return;
    }
    // ReSkate's own world lookup checks that the active map has been created.
    // Calling it requires no callback injection; context is explicit.
    const auto world=fn<std::uintptr_t(*)(std::uintptr_t)>(0x3d2e920)(context);
    if(!world){update_reason="world_unavailable";return;}
    if(context!=previous_context||world!=previous_world){new_speakers.reset();pending_speaker_ids.clear();last_restoration_scan=0;++epoch;scene.attached=false;primary_was_attached=false;locked_identity={};movement_tracker.reset();selected={};live_outputs.clear();output_requests.clear();named_placements.clear();local_catalog.clear();remote_catalog.clear();remote_placements.clear();borrowed_specs.clear();borrowed_live.clear();borrowed_active=false;auto_detach_suppressed.clear();catalog_scan_at=0;previous_context=context;previous_world=world;nearby.objects.clear();next_scan=0;player_valid=false;}
    playing=true;
    const std::uint64_t session=process_session^(context*0x9e3779b97f4a7c15ull)^world^(epoch<<32);
    borrowed_live.clear();
    if(borrowed_active)for(auto spec:borrowed_specs){
        Identity verified{};Placement pose;Vec3 scale;
        if(identity(spec.anchor.identity.entity,session,verified)&&verified==spec.anchor.identity&&transform(verified,pose,scale)){
            spec.anchor.placement=pose;spec.anchor.scale=scale;borrowed_live.push_back(spec);
        }
    }
    camera(scene.camera,scene.attached);scene.camera.sampled_at=scene.at;scene.camera.source=CameraSource::NativePoll;if(!scene.camera.valid)update_reason=scene.camera.diagnostic;
    if(GetTickCount64()>=next_scan){next_scan=GetTickCount64()+500;scan(context,session);}
    unsigned pending=commands.exchange(0);
    if(pending&4){forget_output(scene.identity);scene.attached=false;primary_was_attached=false;scene.identity={};locked_identity={};movement_tracker.reset();selected={};saved={};message=L"Detached";log("TV detached");pending=0;}
    if(pending&1){Identity id{};Placement p;Vec3 scale;
        if(requested.entity&&identity(requested.entity,session,id)&&id==requested&&transform(id,p,scale)&&player_valid&&GetTickCount64()-player_at<=1500&&distance(p.position,player_position)<=100){selected=id;message=L"Object selected; press Attach";debug_log("Placed object selected: group="+std::to_string(id.drop));}
        else{selected={};message=L"Selection expired or outside 100 m; choose a current list entry";}
    }
    if(pending&2){Identity id{};Placement p;Vec3 scale;
        if(selected.entity&&identity(selected.entity,session,id)&&id==selected&&transform(id,p,scale)&&player_valid&&GetTickCount64()-player_at<=1500&&distance(p.position,player_position)<=100){
            const bool occupied=scene.attached ||
                std::any_of(live_outputs.begin(),live_outputs.end(),[&](const LiveOutput& o){return same_placed_object(o.output.identity,id);}) ||
                std::any_of(output_requests.begin(),output_requests.end(),[&](const NewOutput& o){return same_placed_object(o.id,id);});
            if(occupied){message=L"Object already has a TV/speaker, or primary TV already attached";}
            else {scene.identity=id;scene.placement=p;scene.scale=scale;locked_identity=id;movement_tracker.reset();object_missing_since=0;scene.attached=true;primary_was_attached=true;saved=id;remember_output(id,OutputKind::Screen,scene.screen,true,p.position);message=L"Attached to object #"+std::to_wstring(id.drop);log("TV attached: group="+std::to_string(id.drop)+" entity="+std::to_string(id.entity)+" instance="+std::to_string(id.instance));}
        } else message=L"Selection expired; choose a nearby object again";
    }
    // Resolve native entity transforms only against a verified live placement key.
    // A stale ID cannot become a speaker or screen. A moved/respawned object
    // may rebind only to the same unambiguous placement identity.
    while(!output_requests.empty()) {
        auto request=output_requests.front();output_requests.pop_front();
        Identity verified{};Placement p{};Vec3 scale{};
        if(!request.id.entity || !identity(request.id.entity,session,verified) ||
           !(verified==request.id) || !transform(verified,p,scale) ||
           !player_valid || GetTickCount64()-player_at>1500 ||
           distance(p.position,player_position)>100 || !valid(p,request.screen)) {
            message=L"Additional output rejected: object moved or selection expired";continue;
        }
        const auto count=std::count_if(live_outputs.begin(),live_outputs.end(),[&](const LiveOutput& o){return o.output.kind==request.kind;});
        const unsigned max=request.kind==OutputKind::Screen?max_world_screens-1:max_world_speakers;
        if(count>=max){message=L"Additional output limit reached";continue;}
        // Enforce one output per placement, regardless of output type.
        // Use stable placement identity so editor entity recreation cannot bypass it.
        const bool duplicate=std::any_of(live_outputs.begin(),live_outputs.end(),[&](const LiveOutput& o){return same_placed_object(o.output.identity,verified);});
        if(duplicate || (scene.attached&&same_placed_object(scene.identity,verified))){
            message=L"This object already has a TV screen or speaker";continue;
        }
        LiveOutput addition{};addition.output={verified,p,scale,request.screen,request.kind};addition.last_seen=GetTickCount64();
        live_outputs.push_back(std::move(addition));
        remember_output(verified,request.kind,request.screen,false,p.position);
        message=request.kind==OutputKind::Screen?L"Additional TV screen added":L"Directional speaker added";
        log(std::string(request.kind==OutputKind::Screen?"TV screen":"TV speaker")+" linked: group="+std::to_string(verified.drop));
    }
    for(auto it=live_outputs.begin();it!=live_outputs.end();) {
        auto& o=*it;
        Identity observed_id{};Placement position{};Vec3 scale{};
        bool live=identity(o.output.identity.entity,session,observed_id)&&observed_id==o.output.identity&&transform(observed_id,position,scale);
        if(!live && o.output.identity.instance && nearby.at && GetTickCount64()-nearby.at<=1500){
            unsigned matches{};Identity replacement{};Placement replace_pose{};Vec3 replace_scale{};
            for(const auto& row:nearby.objects) {
                if(row.identity.instance!=o.output.identity.instance||row.identity.drop!=o.output.identity.drop)continue;
                Identity verified{};Placement p{};Vec3 s{};
                if(identity(row.identity.entity,session,verified)&&verified==row.identity&&transform(verified,p,s)){
                    replacement=verified;replace_pose=p;replace_scale=s;++matches;
                }
            }
            if(matches==1){o.output.identity=replacement;observed_id=replacement;position=replace_pose;scale=replace_scale;o.tracking.reset();live=true;}
        }
        if(live) {
            o.last_seen=GetTickCount64();o.missing_since=0;
            o.tracking.apply(o.output.placement,o.output.scale,position,scale);
            update_saved_pose(o.output.identity,o.output.placement.position);
            ++it;
        }else {
            if(!o.missing_since)o.missing_since=GetTickCount64();
            if(GetTickCount64()-o.missing_since>900){
                log("Extra screen/speaker removed: placement no longer valid");it=live_outputs.erase(it);
            }else ++it;
        }
    }
    if(!scene.attached&&saved.session==session&&saved.entity){Identity id{};if(identity(saved.entity,session,id)&&can_restore(saved,{id})&&
        !std::any_of(live_outputs.begin(),live_outputs.end(),[&](const LiveOutput& o){return same_placed_object(o.output.identity,id);})) {scene.identity=id;scene.attached=true;primary_was_attached=true;locked_identity={};movement_tracker.reset();message=L"Attachment restored in current session";log("TV attachment restored with exact session and instance identity");}}
    if(scene.attached){
        Identity current{};Placement observed{};Vec3 observed_scale{};
        // Read the actual entity placement, independent of the player and
        // presentation camera. Follow confirmed edits to the placed object,
        // while ignoring small transform-sampling fluctuations.
        bool live=identity(scene.identity.entity,session,current)&&current==scene.identity&&transform(current,observed,observed_scale);
        if(!live && scene.identity.instance && nearby.at && GetTickCount64()-nearby.at<=1500){
            // Some object-editor operations recreate the entity while retaining
            // its placement instance/drop. Only rebind when the exact saved
            // object key resolves to ONE valid nearby entity in this session.
            Identity replacement{};Placement replacement_pose{};Vec3 replacement_scale{};
            unsigned matches=0;
            for(const auto& row:nearby.objects){
                if(row.identity.instance!=scene.identity.instance || row.identity.drop!=scene.identity.drop)continue;
                Identity verified{};Placement p{};Vec3 scale{};
                if(identity(row.identity.entity,session,verified)&&verified==row.identity&&transform(verified,p,scale)){
                    replacement=verified;replacement_pose=p;replacement_scale=scale;++matches;
                }
            }
            if(matches==1){
                scene.identity=replacement;saved=replacement;selected=replacement;
                scene.placement=replacement_pose;scene.scale=replacement_scale;
                locked_identity=replacement;movement_tracker.reset();object_missing_since=0;live=true;
                current=replacement;observed=replacement_pose;observed_scale=replacement_scale;
                message=L"Object moved; TV reattached to its placement instance";
                log("TV automatically reattached to same placement instance: group="+std::to_string(replacement.drop));
            }
        }
        if(!live){
            // Allow an editor re-create and the next 500ms nearby scan to
            // complete; never bind by name or arbitrary nearest object.
            if(!object_missing_since)object_missing_since=GetTickCount64();
            if(GetTickCount64()-object_missing_since<900){
                update_reason="placement_edit_pending";
                return;
            }
            object_missing_since=0;
            scene.attached=false;locked_identity={};movement_tracker.reset();saved={};selected={};
            message=L"Object disappeared or changed identity; detached";
            log("TV attachment lost: object unavailable or no unique matching placement instance");
        } else if(!(locked_identity==scene.identity)) {
            object_missing_since=0;
            scene.placement=observed;scene.scale=observed_scale;locked_identity=scene.identity;
            movement_tracker.reset();
            debug_log("TV attachment pose initialized: group="+std::to_string(scene.identity.drop));
        } else {
            object_missing_since=0;
            if(movement_tracker.apply(scene.placement,scene.scale,observed,observed_scale)) {
            if(debug_logging())log("TV object transform followed: group="+std::to_string(scene.identity.drop)+
                " world="+std::to_string(observed.position.x)+","+std::to_string(observed.position.y)+","+std::to_string(observed.position.z));
            }
        }
        if(scene.attached)update_saved_pose(scene.identity,scene.placement.position);
    }
    // If the original screen is deleted, keep remaining screens active.
    // Promote only an already-verified surviving screen; never follow a player
    // position or an unverified nearby object to find a replacement.
    if(!scene.attached && primary_was_attached){
        auto replacement=std::find_if(live_outputs.begin(),live_outputs.end(),[](const LiveOutput& o){
            return o.output.kind==OutputKind::Screen&&!o.missing_since;
        });
        if(replacement!=live_outputs.end()){
            scene.identity=replacement->output.identity;
            scene.placement=replacement->output.placement;
            scene.scale=replacement->output.scale;
            scene.screen=replacement->output.screen;
            scene.attached=true;saved=scene.identity;selected=scene.identity;
            locked_identity=scene.identity;movement_tracker.reset();object_missing_since=0;
            live_outputs.erase(replacement);
            remember_output(scene.identity,OutputKind::Screen,scene.screen,true,scene.placement.position);
            message=L"Original TV removed; another attached screen is now primary";
            log("Promoted verified additional TV to primary after original placement disappeared");
        }
    }
    // Reattach by ReSkate asset key + world position, NEVER by game-assigned
    // object number. A unique match is required in both directions.
    if(nearby.at && nearby.at!=last_restoration_scan && player_valid && GetTickCount64()-player_at<1500){
        last_restoration_scan=nearby.at;
        std::vector<attachment_store::Candidate> candidates;
        std::vector<Identity> candidate_ids;
        for(const auto& row:nearby.objects)if(const auto* n=named(row.identity)){
            candidates.push_back({row.identity.instance,row.identity.drop,n->item,row.placement.position});
            candidate_ids.push_back(row.identity);
        }
        const auto original=persisted_outputs; // snapshot before reassigning session IDs
        for(std::size_t i=0;i<original.size();++i){
            const auto& stored=original[i];
            if(!attachment_store::good(stored)||stored.asset.empty()||!stored.has_origin)continue;
            auto found=attachment_store::unique_match(i,original,candidates);
            if(!found)continue;
            const auto& target=candidate_ids[*found];
            if(stored.role==0 && scene.attached)continue;
            if(stored.role!=0){
                const OutputKind kind=stored.role==2?OutputKind::Speaker:OutputKind::Screen;
                const unsigned n=static_cast<unsigned>(std::count_if(live_outputs.begin(),live_outputs.end(),[&](const LiveOutput& o){return o.output.kind==kind;}));
                if(n >= (stored.role==2?max_world_speakers:max_world_screens-1))continue;
            }
            if(scene.attached&&same_placed_object(scene.identity,target))continue;
            if(std::any_of(live_outputs.begin(),live_outputs.end(),[&](const LiveOutput& o){return same_placed_object(o.output.identity,target);}))continue;
            if(std::any_of(output_requests.begin(),output_requests.end(),[&](const NewOutput& o){return same_placed_object(o.id,target);}))continue;
            Identity match{};Placement placement{};Vec3 scale{};
            if(!identity(target.entity,session,match)||!(match==target)||!transform(match,placement,scale)||
               !attachment_store::anchor_matches(stored,{match.instance,match.drop,candidates[*found].asset,placement.position})||
               !valid(placement,stored.screen))continue;
            if(stored.role==0){
                scene.identity=match;scene.placement=placement;scene.scale=scale;scene.screen=stored.screen;
                scene.attached=true;primary_was_attached=true;saved=match;locked_identity=match;
                movement_tracker.reset();object_missing_since=0;
                auto_restored_screen.store(true,std::memory_order_release);
                message=L"Saved TV screen restored by object name and position";
                log("Restored primary TV by asset name + coordinates: "+stored.asset);
            }else{
                const OutputKind kind=stored.role==2?OutputKind::Speaker:OutputKind::Screen;
                LiveOutput entry{};entry.output={match,placement,scale,stored.screen,kind};entry.last_seen=GetTickCount64();
                live_outputs.push_back(std::move(entry));
                if(kind==OutputKind::Screen)auto_restored_screen.store(true,std::memory_order_release);
                message=kind==OutputKind::Screen?L"Saved screen restored by object name and position":L"Saved speaker restored by object name and position";
                log(std::string("Restored ")+(kind==OutputKind::Screen?"TV screen: ":"speaker: ")+stored.asset);
            }
            // Update only runtime handle hints. Position + asset remain durable.
            auto& r=persisted_outputs[i];r.instance=match.instance;r.drop=match.drop;++persisted_revision;
        }
    }
}
// Called after game rendering, independently of game-thread hooks.
// Read-only manager/view/camera traversal, with the same native identity and
// finite-matrix validation as the existing attachment camera reader.
// Active camera memory can be updating concurrently; use two reads to avoid
// treating an in-flight pose change as a coherent final camera.
bool sample_camera_at_render(Camera& out) {
    if(!base || stopping.load(std::memory_order_acquire) || !source_verified.load(std::memory_order_acquire))return false;
    Camera a{},b{};
    if(!camera(a,false)||!camera(b,false))return false;
    if(a.active_vtable!=b.active_vtable || a.view_count!=b.view_count)return false;
    for(unsigned i:{0u,1u,2u,4u,5u,6u,8u,9u,10u,12u,13u,14u})
        if(std::abs(a.world[i]-b.world[i])>.025f)return false;
    if(std::abs(a.vertical_fov-b.vertical_fov)>.05f)return false;
    out=b;return true;
}
// Read-only client-realm snapshot; ReSkate performs its own game tick.
// Separate, explicit realm lookup (ReSkate: network_object_runtime.cpp).
// It works from the existing ReSkate host callback; no game update hook.
using RealmContext=std::uintptr_t*(*)(std::uintptr_t*,std::uint32_t);
void poll_impl() {
    if(!source_verified.load(std::memory_order_acquire) || stopping.load(std::memory_order_acquire))return;
    std::uintptr_t context{};
    fn<RealmContext>(0x18b6e40)(&context,0xbf0f9789u);
    // ReSkate checks the out-value rather than the getter's return pointer;
    // the client realm is explicit and doesn't depend on this thread's TLS.
    if(!context){update(0);source_active.store(false,std::memory_order_release);return;}
    update(context);
    source_active.store(playing,std::memory_order_release);
    native_query_fault_count.store(0,std::memory_order_release);
}
// Fault containment lives only around the native lookup, not around the
// existing video renderer. An invalid native call cannot take down the TV UI.
void native_poll_guard() noexcept {
    __try { poll_impl(); }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        source_active.store(false,std::memory_order_release);
        const unsigned failed=native_query_fault_count.fetch_add(1,std::memory_order_acq_rel)+1;
        // Concurrent editor updates may transiently invalidate a native query.
        // Retry no faster than once per second; never repeatedly fault in a frame.
        next_poll_due.store(GetTickCount64()+1000,std::memory_order_release);
        if(failed>=3)source_verified.store(false,std::memory_order_release);
        native_query_fault.store(true,std::memory_order_release);
    }
}
}
void observe_editor_placement_names(const dingosdk::ParkEditorModel& editor){
    std::lock_guard lock(mutex);
    if(!editor.available||editor.failed)return;
    if(!new_speakers.baseline||new_speakers.generation!=editor.generation||last_editor_revision!=editor.revision){
        std::vector<std::uint64_t> editor_ids;for(const auto& object:editor.objects)editor_ids.push_back(object.id);
        new_speakers.observe(editor.generation,editor_ids);last_editor_revision=editor.revision;
    }
    if(!new_speakers.enabled)pending_speaker_ids.clear();
    pending_speaker_ids.insert(new_speakers.eligible.begin(),new_speakers.eligible.end());
    new_speakers.eligible.clear();
    for(auto it=pending_speaker_ids.begin();it!=pending_speaker_ids.end();)if(!new_speakers.seen.count(*it))it=pending_speaker_ids.erase(it);else ++it;
    if(!nearby.at || GetTickCount64()-nearby.at>1500 || nearby.at==catalog_scan_at)return;
    catalog_scan_at=nearby.at;
    std::vector<NamedPlacement> entries;
    std::vector<PlacementName> catalog;
    entries.reserve(nearby.objects.size());
    for(const auto& row:nearby.objects){
        const dingosdk::EditorObject* unique=nullptr;
        for(const auto& object:editor.objects){
            if(!object.spawned||object.item.empty()||object.item.size()>256)continue;
            const Vec3 position{object.position[0],object.position[1],object.position[2]};
            if(attachment_store::squared_distance(position,row.placement.position)>.0001f)continue;
            if(unique){unique=nullptr;break;} // ambiguous same-position items
            unique=&object;
        }
        if(unique){entries.push_back({row.identity,unique->item,row.placement.position});
            std::string display=unique->item;if(editor.assets)for(const auto& asset:*editor.assets)if(asset.key==unique->item){display=asset.title;break;}
            if(display.size()>128)display.resize(128);
            catalog.push_back({row.identity,unique->item,display,0,(epoch*0x9e3779b97f4a7c15ull)^editor.generation,unique->id});}
    }
    for(const auto& old:local_catalog){const bool assigned=(scene.attached&&scene.identity==old.identity)||std::any_of(live_outputs.begin(),live_outputs.end(),[&](auto& o){return o.output.identity==old.identity;});
        if(assigned&&std::none_of(catalog.begin(),catalog.end(),[&](auto& n){return n.identity==old.identity;})){catalog.push_back(old);entries.push_back({old.identity,old.asset,{}});}}
    named_placements=std::move(entries);local_catalog=std::move(catalog);
    if(scene.attached)update_saved_pose(scene.identity,scene.placement.position);
    for(const auto& o:live_outputs)update_saved_pose(o.output.identity,o.output.placement.position);
    if(borrowed_active){pending_speaker_ids.clear();return;}
    // The editor document contains local placements; remote network-source creations
    // are excluded by the host's placement runtime. Track IDs before nearby filtering.
    for(auto it=pending_speaker_ids.begin();it!=pending_speaker_ids.end();){
        auto object=std::find_if(editor.objects.begin(),editor.objects.end(),[&](const auto& o){return o.id==*it&&o.spawned;});
        if(object==editor.objects.end()){++it;continue;}
        const Vec3 origin{object->position[0],object->position[1],object->position[2]};
        const NamedPlacement* match=nullptr;bool ambiguous=false;
        for(const auto& n:named_placements)if(n.item==object->item&&attachment_store::squared_distance(origin,n.position)<=.0001f){if(match){ambiguous=true;break;}match=&n;}
        if(!match||ambiguous){++it;continue;}
        const auto id=match->identity;
        const bool occupied=(scene.attached&&same_placed_object(scene.identity,id))||
            std::any_of(live_outputs.begin(),live_outputs.end(),[&](const auto& o){return same_placed_object(o.output.identity,id);})||
            std::any_of(output_requests.begin(),output_requests.end(),[&](const auto& o){return same_placed_object(o.id,id);});
        const bool saved_attachment=std::any_of(persisted_outputs.begin(),persisted_outputs.end(),[&](const auto& r){return same_key(r,id)||attachment_store::anchor_matches(r,{id.instance,id.drop,match->item,origin});});
        const bool suppressed=auto_detach_suppressed.count({id.instance,id.drop})!=0;
        if(!occupied&&!saved_attachment&&!suppressed){
            auto_detach_suppressed.insert({id.instance,id.drop}); // Speaker choice wins over screen asset rules, even at capacity.
            const auto count=std::count_if(live_outputs.begin(),live_outputs.end(),[](const auto& o){return o.output.kind==OutputKind::Speaker;})+
                std::count_if(output_requests.begin(),output_requests.end(),[](const auto& o){return o.kind==OutputKind::Speaker;});
            if(!can_add_output(static_cast<unsigned>(count),0,max_world_speakers)){message=L"Automatic speaker not added: 64-speaker limit reached";log("Automatic speaker not added: speaker limit reached");}
            else {output_requests.push_back({id,OutputKind::Speaker,Screen{}});message=L"New object queued as speaker (directional audio required)";}
        }
        it=pending_speaker_ids.erase(it);
    }
    // Explicitly opted-in asset presets automatically turn a newly spawned editor
    // object into a screen. Work only from unique live catalogue+transform pairs,
    // never from transient placement numbers, and never reuse an occupied object.
    // Unlike saved restorations, no particular world coordinate is required.
    if(automatic_screens.empty()||!player_valid||GetTickCount64()-player_at>1500)return;
    for(const auto& n:named_placements){
        const bool speaker_pending=std::any_of(editor.objects.begin(),editor.objects.end(),[&](const auto& o){return pending_speaker_ids.count(o.id)&&o.item==n.item&&attachment_store::squared_distance({o.position[0],o.position[1],o.position[2]},n.position)<=.0001f;});
        if(speaker_pending)continue;
        const auto key=std::make_pair(n.identity.instance,n.identity.drop);
        if(auto_detach_suppressed.find(key)!=auto_detach_suppressed.end())continue;
        const auto config=std::find_if(automatic_screens.begin(),automatic_screens.end(),[&](const auto& rule){
            return rule.first==presets::normalize(n.item);
        });
        if(config==automatic_screens.end())continue;
        const auto found=std::find_if(nearby.objects.begin(),nearby.objects.end(),[&](const auto& row){return row.identity==n.identity;});
        if(found==nearby.objects.end()||distance(found->placement.position,player_position)>100)continue;
        if(scene.attached&&same_placed_object(scene.identity,n.identity))continue;
        if(std::any_of(live_outputs.begin(),live_outputs.end(),[&](const LiveOutput& o){return same_placed_object(o.output.identity,n.identity);}))continue;
        if(std::any_of(output_requests.begin(),output_requests.end(),[&](const NewOutput& o){return same_placed_object(o.id,n.identity);}))continue;
        if(!valid(found->placement,config->second))continue;
        Identity id{};Placement pose{};Vec3 scale{};
        if(!identity(n.identity.entity,n.identity.session,id)||!(id==n.identity)||!transform(id,pose,scale))continue;
        if(!scene.attached){
            scene.identity=id;scene.placement=pose;scene.scale=scale;scene.screen=config->second;
            scene.attached=true;primary_was_attached=true;saved=id;locked_identity=id;
            movement_tracker.reset();object_missing_since=0;
            remember_output(id,OutputKind::Screen,config->second,true,pose.position);
        }else{
            const auto count=std::count_if(live_outputs.begin(),live_outputs.end(),[](const LiveOutput& o){return o.output.kind==OutputKind::Screen;});
            if(count>=max_world_screens-1)break;
            LiveOutput extra{};extra.output={id,pose,scale,config->second,OutputKind::Screen};extra.last_seen=GetTickCount64();
            live_outputs.push_back(std::move(extra));
            remember_output(id,OutputKind::Screen,config->second,false,pose.position);
        }
        auto_restored_screen.store(true,std::memory_order_release);
        message=L"TV auto-attached from asset preset";
        log("Auto-attached TV from asset preset: "+config->first);
    }
}
std::uint64_t placement_session_epoch(){std::lock_guard lock(mutex);return playing?epoch:0;}
void set_auto_speakers(bool enabled){std::lock_guard lock(mutex);new_speakers.toggle(enabled);pending_speaker_ids.clear();}
bool auto_speakers_enabled(){std::lock_guard lock(mutex);return new_speakers.enabled;}
void set_auto_screen_presets(std::vector<std::pair<std::string,Screen>> rules){
    std::lock_guard lock(mutex);
    automatic_screens=std::move(rules);
    catalog_scan_at=0; // Existing placed objects can now use an enabled preset.
}
bool render_camera_snapshot(Camera& out){if(!sample_camera_at_render(out))return false;
    static std::atomic<std::uint64_t> sequence{};out.sequence=++sequence;out.sampled_at=GetTickCount64();out.viewport=0;out.source=CameraSource::Presentation;return true;}
void poll_native_objects() noexcept {
    if(!source_verified.load(std::memory_order_acquire) || stopping.load(std::memory_order_acquire))return;
    const auto now=GetTickCount64();
    if(now<next_poll_due.load(std::memory_order_relaxed))return;
    if(polling.test_and_set(std::memory_order_acquire))return;
    // ~30 Hz object tracking; the full placement-list scan remains 500 ms.
    next_poll_due.store(now+33,std::memory_order_relaxed);
    native_poll_guard();
    polling.clear(std::memory_order_release);
    static std::atomic<bool> query_failing{false};
    if(native_query_fault.exchange(false,std::memory_order_acq_rel)){
        if(!query_failing.exchange(true)||!source_verified.load())log(source_verified.load(std::memory_order_acquire)?
            "Native client-realm query fault; backing off 1 second before retry (video unaffected)":
            "Native client-realm query failed 3 times; native placement polling disabled (video unaffected)");
    }else if(source_active.load()&&query_failing.exchange(false)){log("Native client-realm query recovered");
    }
}
NativeObjectSourceStatus native_object_source_status() {
    NativeObjectSourceStatus status{};
    status.verified=source_verified.load(std::memory_order_acquire);
    status.active=source_active.load(std::memory_order_acquire);
    status.status=!status.verified?"Native realm contract unavailable":
        status.active?"Native placed objects ready (read-only polling)":
        "Waiting for active placed-object store";
    return status;
}
bool initialize_native() {
    message=L"Native placed-object source unavailable; see JishsVirtualTV.log";
    base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"Skate.exe"));
    if(!base){log("Native placement source: Skate.exe absent");return false;}
    if(!verify(contracts::picking_contracts[5])||!verify(contracts::surface_contracts[0])){
        log("Native placement component or world contract mismatch");return false;
    }
    for(const auto& c:{contracts::placement_resolve_contract,contracts::placement_valid_contract,
        contracts::placement_query_contract,contracts::placement_pose_contract})
        if(!verify(c)){log("Native read-only placement contract mismatch at RVA "+std::to_string(c.rva));return false;}
    // This is ReSkate's verified client-realm getter (20260929 build), NOT the
    // thread-local getter and NOT a patched game function. It returns the real
    // client context even when we are called after the ReSkate overlay draw.
    constexpr unsigned char realm_prefix[]{
        0x89,0x54,0x24,0x10,0x53,0x48,0x83,0xec,0x30,0x4c,0x8b,0x05,0xe8,0xb7,0x70,0x05};
    if(!contract(0x18b6e40,realm_prefix,sizeof realm_prefix)){
        log("Native placed-object realm accessor did not match the supported game build");return false;
    }
    FILETIME created{},exit{},kernel{},user{};
    GetProcessTimes(GetCurrentProcess(),&created,&exit,&kernel,&user);
    process_session=(std::uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime;
    source_verified.store(true,std::memory_order_release);
    nearby.status=L"Waiting for native placed objects";
    message=L"Native object polling ready; choose a placed object in F8";
    log("Native placed-object reader initialized: verified client-realm accessor; no client-tick detour or MinHook allocation");
    return true;
}
void stop_native(){stopping=true;set_borrowed_outputs(false,{});publish_remote_names({});}
std::vector<PlacementName> placement_names_snapshot(){std::lock_guard lock(mutex);auto out=local_catalog;
    for(const auto& r:remote_catalog)if(std::none_of(out.begin(),out.end(),[&](const auto& n){return n.identity==r.identity;}))out.push_back(r);return out;}
void publish_remote_names(std::vector<PlacementName> names){std::lock_guard lock(mutex);remote_catalog=std::move(names);remote_placements.clear();
    for(const auto& n:remote_catalog)if(!n.asset.empty())remote_placements.push_back({n.identity,n.asset,{}});}
void set_borrowed_outputs(bool active,std::vector<BorrowedOutput> outputs){std::lock_guard lock(mutex);
    borrowed_active=active;borrowed_specs=std::move(outputs);
    borrowed_live.erase(std::remove_if(borrowed_live.begin(),borrowed_live.end(),[&](auto& live){
        auto it=std::find_if(borrowed_specs.begin(),borrowed_specs.end(),[&](auto& spec){return spec.anchor.identity==live.anchor.identity&&spec.anchor.kind==live.anchor.kind;});
        if(!active||it==borrowed_specs.end())return true;live.anchor.screen=it->anchor.screen;live.primary=it->primary;return false;}),borrowed_live.end());
    if(active){commands=0;output_requests.clear();pending_speaker_ids.clear();}}
bool borrowed_presentation_active(){std::lock_guard lock(mutex);return borrowed_active;}
std::vector<BorrowedOutput> borrowed_outputs_snapshot(){std::lock_guard lock(mutex);return GetTickCount64()-scene.at<=250?borrowed_live:std::vector<BorrowedOutput>{};}
std::vector<Scene> borrowed_screens_snapshot(){std::lock_guard lock(mutex);std::vector<Scene> out;
    if(GetTickCount64()-scene.at>250)return out;
    for(const auto& o:borrowed_live)if(o.anchor.kind==OutputKind::Screen){auto next=scene;next.identity=o.anchor.identity;next.placement=o.anchor.placement;next.scale=o.anchor.scale;next.screen=o.anchor.screen;next.attached=true;out.push_back(next);}return out;}

void publish_player_position(Vec3 p,bool valid){std::lock_guard lock(mutex);player_position=p;player_valid=valid&&std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);player_at=GetTickCount64();}
NearbySnapshot nearby_snapshot(){std::lock_guard lock(mutex);auto out=nearby;
    if(!source_verified.load(std::memory_order_acquire)) {
        out.objects.clear();out.status=L"Native placed-object reader unavailable for this game build";return out;
    }
    if(GetTickCount64()-out.at>1500){out.objects.clear();out.status=L"Waiting for a fresh native placed-object snapshot";}return out;}
void request_select(Identity id){std::lock_guard lock(mutex);if(borrowed_active)return;requested=id;commands.fetch_or(1);}
void request_select(){request_select({});}void request_attach(){std::lock_guard lock(mutex);if(!borrowed_active)commands.fetch_or(2);}void request_detach(){std::lock_guard lock(mutex);if(!borrowed_active)commands.fetch_or(4);}
Scene scene_snapshot(){std::lock_guard lock(mutex);return scene;}
void set_screen(const Screen& s){std::lock_guard lock(mutex);if(!scene.attached)scene.screen=s;}
void set_screen_for(Identity id,const Screen& s){
    if(!valid(Placement{},s))return;
    std::lock_guard lock(mutex);if(borrowed_active)return;
    bool updated=false;
    if(scene.attached&&scene.identity==id){scene.screen=s;updated=true;}
    for(auto& o:live_outputs)if(o.output.identity==id){o.output.screen=s;updated=true;}
    if(updated){
        auto it=std::find_if(persisted_outputs.begin(),persisted_outputs.end(),[&](const auto& r){return same_key(r,id);});
        if(it!=persisted_outputs.end()){
            // Avoid hundreds of disk writes while dragging: worker coalesces revisions.
            it->screen=s;++persisted_revision;
        }
    }
    if(!updated&&!scene.attached)scene.screen=s; // profile for the initial attachment
}
bool request_add_output(Identity id,OutputKind kind,const Screen& s){
    if(!id.entity||!id.instance||!valid(Placement{},s))return false;
    std::lock_guard lock(mutex);if(borrowed_active)return false;
    const auto already=std::count_if(live_outputs.begin(),live_outputs.end(),[&](const LiveOutput& o){return o.output.kind==kind;});
    const auto waiting=std::count_if(output_requests.begin(),output_requests.end(),[&](const NewOutput& o){return o.kind==kind;});
    if(!can_add_output(static_cast<unsigned>(already),static_cast<unsigned>(waiting),
                       kind==OutputKind::Screen?max_world_screens-1:max_world_speakers))return false;
    if(scene.attached&&same_placed_object(scene.identity,id))return false;
    if(std::any_of(live_outputs.begin(),live_outputs.end(),[&](const LiveOutput& o){return same_placed_object(o.output.identity,id);} ))return false;
    if(std::any_of(output_requests.begin(),output_requests.end(),[&](const NewOutput& o){return same_placed_object(o.id,id);} ))return false;
    const unsigned pending_primary=commands.load(std::memory_order_acquire);
    if(pending_primary&2u){
        const Identity& attach_target=(pending_primary&1u)?requested:selected;
        if(same_placed_object(attach_target,id))return false;
    }
    output_requests.push_back({id,kind,s});return true;
}
bool detach_one_output(Identity id){
    if(!id.instance)return false;
    std::lock_guard lock(mutex);if(borrowed_active)return false;
    const bool primary=scene.attached&&same_placed_object(scene.identity,id);
    auto_detach_suppressed.insert({id.instance,id.drop});
    const auto old=live_outputs.size(),queued=output_requests.size();
    live_outputs.erase(std::remove_if(live_outputs.begin(),live_outputs.end(),[&](const LiveOutput& o){return same_placed_object(o.output.identity,id);}),live_outputs.end());
    output_requests.erase(std::remove_if(output_requests.begin(),output_requests.end(),[&](const NewOutput& o){return same_placed_object(o.id,id);}),output_requests.end());
    const bool saved_before=std::any_of(persisted_outputs.begin(),persisted_outputs.end(),[&](const auto& o){return same_key(o,id);});
    forget_output(id);
    if(primary){
        scene.attached=false;scene.identity={};locked_identity={};saved={};movement_tracker.reset();
        object_missing_since=0;
        // A surviving screen can become primary on the next verified native poll.
        primary_was_attached=true;
        message=L"Selected TV detached; other outputs retained";
    }
    return primary||old!=live_outputs.size()||queued!=output_requests.size()||saved_before;
}
bool remove_output(Identity id,OutputKind kind){
    if(!id.instance)return false;
    std::lock_guard lock(mutex);if(borrowed_active)return false;
    if(scene.attached&&same_placed_object(scene.identity,id)){
        if(kind!=OutputKind::Screen)return false;
        // Handle the primary through the same single-detach path, outside lock.
    }else if(!std::any_of(live_outputs.begin(),live_outputs.end(),[&](const LiveOutput& o){return same_placed_object(o.output.identity,id)&&o.output.kind==kind;}) &&
             !std::any_of(output_requests.begin(),output_requests.end(),[&](const NewOutput& o){return same_placed_object(o.id,id)&&o.kind==kind;}))return false;
    auto_detach_suppressed.insert({id.instance,id.drop});
    const bool primary=scene.attached&&same_placed_object(scene.identity,id);
    live_outputs.erase(std::remove_if(live_outputs.begin(),live_outputs.end(),[&](const LiveOutput& o){return same_placed_object(o.output.identity,id);}),live_outputs.end());
    output_requests.erase(std::remove_if(output_requests.begin(),output_requests.end(),[&](const NewOutput& o){return same_placed_object(o.id,id);}),output_requests.end());
    forget_output(id);
    if(primary){scene.attached=false;scene.identity={};locked_identity={};saved={};movement_tracker.reset();primary_was_attached=true;message=L"Selected TV detached";}
    return true;
}
void clear_extra_outputs(){
    std::lock_guard lock(mutex);if(borrowed_active)return;
    for(const auto& o:live_outputs){auto_detach_suppressed.insert({o.output.identity.instance,o.output.identity.drop});forget_output(o.output.identity);}
    for(const auto& o:output_requests){auto_detach_suppressed.insert({o.id.instance,o.id.drop});forget_output(o.id);}
    live_outputs.clear();output_requests.clear();
}
void clear_persistent_outputs(){
    std::lock_guard lock(mutex);if(borrowed_active)return;
    persisted_outputs.clear();++persisted_revision;
    live_outputs.clear();output_requests.clear();
}
void restore_persistent_outputs(const std::vector<attachment_store::Record>& stored){
    std::lock_guard lock(mutex);
    persisted_outputs=stored;last_restoration_scan=0;
}
std::pair<std::uint64_t,std::vector<attachment_store::Record>> persistent_outputs_snapshot(){
    std::lock_guard lock(mutex);return {persisted_revision,persisted_outputs};
}
bool take_restored_screen_signal(){return auto_restored_screen.exchange(false,std::memory_order_acq_rel);}
std::vector<std::pair<Identity,OutputKind>> output_assignments_snapshot(){
    std::lock_guard lock(mutex);
    std::vector<std::pair<Identity,OutputKind>> assignments;
    assignments.reserve(1+live_outputs.size()+output_requests.size()+1);
    if(scene.attached)assignments.emplace_back(scene.identity,OutputKind::Screen);
    for(const auto& o:live_outputs)assignments.emplace_back(o.output.identity,o.output.kind);
    for(const auto& o:output_requests)assignments.emplace_back(o.id,o.kind);
    const unsigned pending=commands.load(std::memory_order_acquire);
    if(pending&2u){
        const Identity& target=(pending&1u)?requested:selected;
        if(target.entity)assignments.emplace_back(target,OutputKind::Screen);
    }
    return assignments;
}
std::vector<OutputAnchor> extra_outputs_snapshot(){
    std::lock_guard lock(mutex);std::vector<OutputAnchor> out;
    const auto now=GetTickCount64();
    for(const auto& o:live_outputs)if(!o.missing_since && now-o.last_seen<250)out.push_back(o.output);
    return out;
}
std::vector<Scene> world_screens_snapshot(){
    std::lock_guard lock(mutex);std::vector<Scene> out;
    if(scene.attached)out.push_back(scene);
    const auto now=GetTickCount64();
    for(const auto& o:live_outputs)if(o.output.kind==OutputKind::Screen&&!o.missing_since&&now-o.last_seen<250){
        Scene s=scene;s.identity=o.output.identity;s.placement=o.output.placement;s.scale=o.output.scale;s.screen=o.output.screen;s.attached=true;
        out.push_back(s);
    }
    return out;
}
void restore_identity(Identity id){std::lock_guard lock(mutex);saved=id;}
Identity selected_identity(){std::lock_guard lock(mutex);return selected.entity?selected:saved;}
Identity attached_identity(){std::lock_guard lock(mutex);return scene.attached?scene.identity:Identity{};}
std::wstring attachment_status(){std::lock_guard lock(mutex);return message;}
}
