#include "setup_sync_runtime.h"
#include <cassert>
#include <iostream>
using namespace virtualtv;
namespace {
Scene personal;
NearbySnapshot nearby;
std::vector<OutputAnchor> extras;
std::vector<BorrowedOutput> borrowed;
std::vector<PlacementName> names, verified_names;
bool active{};
ScreenMode mode=ScreenMode::Attached;
ULONGLONG ticks=10000;
ULONGLONG WINAPI clock_now(){return ticks;}
}
namespace virtualtv {
Scene scene_snapshot(){return personal;}
NearbySnapshot nearby_snapshot(){return nearby;}
std::vector<OutputAnchor> extra_outputs_snapshot(){return extras;}
std::vector<PlacementName> placement_names_snapshot(){auto out=verified_names;out.insert(out.end(),names.begin(),names.end());return out;}
void publish_remote_names(std::vector<PlacementName> n){names=std::move(n);}
void set_borrowed_outputs(bool value,std::vector<BorrowedOutput> n){active=value;borrowed=std::move(n);}
std::vector<BorrowedOutput> borrowed_outputs_snapshot(){return borrowed;}
void set_screen_mode(ScreenMode value){mode=value;}
ScreenMode current_screen_mode(){return mode;}
}
int main(){
    using namespace fullsync;using K=sync::Kind;
    auto root=std::filesystem::temp_directory_path()/("vtv-full-"+std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(root/"Mods/JishsVirtualTVBridge/content");std::filesystem::create_directories(root/"logs");
    std::ofstream(root/"Mods/JishsVirtualTVBridge/content/out.txt")<<"marker";std::ofstream(root/"logs/ReSkate.log")<<"";
    sync::Bridge bridge(clock_now);assert(bridge.initialize(root,root/"storage.json"));auto path=root/"Mods/.reskate_plugin_data/JishsVirtualTVBridge/bridge_state.txt";
    auto status=[&](unsigned serial,unsigned others=2){std::ofstream f(path);f<<"VTVB3|9|"<<serial<<"|42|1|"<<others<<"|1|1|0|776F726C64\n";if(others){f<<"11|416C696365\n";if(others==2)f<<"22|426F62\n";}f<<"END|"<<serial<<"\n";};status(1);bridge.poll();assert(bridge.world_id());
    Runtime runtime(bridge);AudioSettings audio{true,35,17};runtime.tick(ticks,true,222,audio);
    auto packet=[&](K kind,unsigned sender,std::uint64_t room,std::uint64_t revision,std::string bytes,std::uint64_t epoch=111){return sync::RelayEvent{sender,{kind,room,revision,sender,epoch,bridge.world_id(),std::move(bytes)}};};
    runtime.receive(packet(K::capability,11,0,0,"FS3"),11,ticks);runtime.receive(packet(K::capability,22,0,0,"FS3"),11,ticks);assert(runtime.compatible(11));
    sync::Packet offer{K::full_offer,50,1,11,111,bridge.world_id(),{}};assert(runtime.current_offer(offer));offer.timing_us++;assert(!runtime.current_offer(offer));
    runtime.watch(11,50,ticks);assert(active&&mode==ScreenMode::SyncedWorld&&!runtime.ready()&&borrowed.empty());
    Descriptor d;d.ref={11,333,1};d.pose.position={10,20,30};d.scale={2,3,4};d.asset="tv";d.name="Television";
    Setup setup{1,true,100,{{d,{},OutputKind::Screen,true}}};
    auto deliver=[&](K kind,const Setup& value,unsigned sender=11,std::uint64_t room=50,std::uint64_t allowed=11){for(auto& part:chunks(encode_setup(value),value.revision))runtime.receive(packet(kind,sender,room,value.revision,part),allowed,ticks);};
    deliver(K::setup_snapshot,setup,22);deliver(K::setup_snapshot,setup,11,51);deliver(K::setup_snapshot,setup,11,50,22);assert(!runtime.ready());
    deliver(K::setup_snapshot,setup);assert(runtime.ready()&&borrowed.empty());assert(runtime.range()==100&&audio.volume==17); // missing native object remains hidden/silent
    Identity local{123,456,789,1};nearby.objects.push_back({local,d.pose,1,d.scale});ticks+=201;runtime.tick(ticks,true,222,audio);assert(borrowed.size()==1&&borrowed[0].anchor.identity==local);
    ticks+=1001;runtime.tick(ticks,true,222,audio);assert(names.size()==1);
    // verified binding follows movement rather than the broadcast origin
    nearby.objects[0].placement.position={100,200,300};ticks+=201;runtime.tick(ticks,true,222,audio);assert(borrowed.size()==1);
    auto next=setup;next.revision=2;next.directional=false;next.range=200;next.outputs[0].screen.offset={5,6,7};
    for(auto& part:chunks(encode_delta(setup,next),20))runtime.receive(packet(K::setup_delta,11,50,2,part),11,ticks);assert(runtime.range()==200&&!runtime.directional()&&borrowed[0].anchor.screen.offset.y==6);
    deliver(K::setup_snapshot,setup);assert(runtime.range()==200); // stale snapshots cannot revert live edits
    Setup big=next;big.revision=3;for(unsigned i=2;i<30;++i){auto o=big.outputs[0];o.object.ref.object=i;big.outputs.push_back(o);}auto parts=chunks(encode_setup(big),30);assert(parts.size()>1);
    runtime.receive(packet(K::setup_snapshot,11,50,3,parts[0]),11,ticks);assert(runtime.range()==200&&borrowed.size()==1); // incomplete update retains last complete setup
    runtime.watch(11,50,ticks);assert(runtime.ready()&&runtime.range()==200); // subsequent track/repeat offer retains live setup
    // Conflicting naming evidence cannot silently rebind or relabel a setup.
    Writer catalog;catalog.bytes="FS2N";catalog.integer(1);auto conflict=d;conflict.asset="wrong/asset";catalog.descriptor(conflict);
    for(auto& part:chunks(catalog.bytes,90))runtime.receive(packet(K::catalog,11,0,90,part),11,ticks);
    ticks+=201;runtime.tick(ticks,true,222,audio);assert(borrowed.empty()&&names.empty());
    runtime.leave();assert(!active&&borrowed.empty()&&mode==ScreenMode::Attached&&!runtime.ready()&&audio.range==35&&audio.volume==17);
    runtime.watch(11,60,ticks);deliver(K::setup_snapshot,next,11,50);assert(!runtime.ready()); // stale controller room
    runtime.receive(packet(K::setup_snapshot,11,60,2,chunks(encode_setup(next),40)[0],999),11,ticks);assert(!runtime.ready()); // stale authenticated session
    runtime.leave();runtime.watch(22,70,ticks);deliver(K::setup_snapshot,next,11,50);assert(!runtime.ready());
    // Three attempts, then wait for explicit retry or a new setup revision.
    for(unsigned i=0;i<4;++i){ticks+=40000;status(2+i);bridge.poll();runtime.tick(ticks,true,222,audio);}
    assert(runtime.status().find("3 attempts")!=std::string::npos);runtime.retry(ticks);
    assert(runtime.status().find("attempt 1/3")!=std::string::npos);
    runtime.receive(packet(K::capability,22,0,0,"FS3",112),22,ticks);assert(!runtime.watching());
    runtime.receive(packet(K::capability,22,0,0,"FS3",111),22,ticks);offer.sender=22;offer.timing_us=111;assert(!runtime.current_offer(offer));
    runtime.clear();assert(!runtime.compatible(11)&&!active&&names.empty());
    runtime.tick(ticks,true,223,audio);assert(!runtime.compatible(22));
    // Sharing and later resynchronisation use fresh transforms, with a separate setup revision.
    personal.attached=true;personal.identity=local;personal.placement=d.pose;personal.scale=d.scale;
    verified_names={{local,"tv","Television",0,23,17}};
    runtime.receive(packet(K::capability,11,0,0,"FS3"),11,ticks);runtime.host(80,audio);
    personal.placement.position.x=80;ticks+=201;runtime.tick(ticks,true,223,audio);
    runtime.receive(packet(K::resync,11,80,0,{}),11,ticks);bridge.clear_pending();
    auto read_snapshot=[&](){Assembly assembled;std::optional<Setup> result;
        for(unsigned i=0;i<100&&!result;++i){ticks+=25;runtime.tick(ticks,true,223,audio);bridge.flush(ticks);std::ifstream f(root/"Mods/.reskate_plugin_data/JishsVirtualTVBridge/sync_outbox.txt");std::string line;std::getline(f,line);auto at=line.rfind('|');if(at==line.npos)continue;std::string bytes;for(std::size_t j=at+1;j+1<line.size();j+=2)bytes.push_back(char(std::stoi(line.substr(j,2),nullptr,16)));auto wire=sync::decode(bytes);if(wire&&wire->kind==K::setup_snapshot)if(auto complete=assembled.accept(wire->payload,ticks))result=decode_setup(*complete);}
        return result;};
    auto fresh=read_snapshot();assert(fresh&&fresh->revision==1&&fresh->outputs.size()==1&&fresh->outputs[0].object.pose.position.x==80&&fresh->outputs[0].object.ref.owner==42&&fresh->outputs[0].object.ref.object==17);
    runtime.clear();verified_names.clear();
    assert(std::filesystem::equivalent(root.parent_path(),std::filesystem::temp_directory_path()));std::filesystem::remove_all(root);
    std::cout<<"Full setup runtime: capability/session, rejected senders/rooms, late native spawn, live alignment/range, local volume, atomic replacement, stale packets, controller switching and cleanup passed\n";
}
