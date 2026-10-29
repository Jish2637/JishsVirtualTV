#include "tv_sync_bridge.h"
#include <cassert>
#include <iostream>
using namespace virtualtv::sync;
static ULONGLONG tick=10000;
static ULONGLONG WINAPI now(){return tick;}
int main(){
 auto root=std::filesystem::temp_directory_path()/("vtv-budget-"+std::to_string(GetCurrentProcessId()));
 std::filesystem::create_directories(root/"Mods/JishsVirtualTVBridge/content");std::filesystem::create_directories(root/"logs");std::ofstream(root/"Mods/JishsVirtualTVBridge/content/out.txt")<<"marker";std::ofstream(root/"logs/ReSkate.log")<<"";
 Bridge bridge(now);assert(bridge.initialize(root,root/"storage.json"));auto folder=root/"Mods/.reskate_plugin_data/JishsVirtualTVBridge";unsigned serial{};
 auto ack=[&](std::string seq="0"){std::ofstream(folder/"bridge_state.txt")<<"VTVB3|9|"<<++serial<<"|42|1|1|1|1|"<<seq<<"|776F726C64\n11|416C696365\nEND|"<<serial<<"\n";bridge.poll();};ack();assert(bridge.world_id());
 auto read=[&](){std::ifstream f(folder/"sync_outbox.txt");std::string row;std::getline(f,row);return row;};
 auto kind=[&](const std::string& row){auto at=row.rfind('|');return std::stoi(row.substr(at+1+8,2),nullptr,16);};
 Packet bulk{Kind::catalog,0,1,42,0,0,std::string(max_payload,'x')};Packet setup=bulk;setup.kind=Kind::setup_snapshot;Packet pause{Kind::pause,50,1,42,0,0,{}};
 assert(bridge.send(bulk)&&bridge.send(setup)&&bridge.send(pause));bridge.flush(tick);auto row=read();assert(kind(row)==int(Kind::pause));ack(row.substr(0,row.find('|')));tick+=25;bridge.flush(tick);row=read();assert(kind(row)==int(Kind::setup_snapshot));
 // Preserve the in-flight packet, then prioritise playback over queued discovery.
 assert(bridge.send(pause));tick+=250;bridge.flush(tick);assert(read()==row);ack(row.substr(0,row.find('|')));tick+=25;bridge.flush(tick);row=read();assert(kind(row)==int(Kind::pause));ack(row.substr(0,row.find('|')));bridge.clear_pending();
 auto too_big=bulk;too_big.payload.push_back('x');assert(!bridge.send(too_big));
 for(unsigned i=0;i<24;++i)assert(bridge.send(bulk));assert(!bridge.send(bulk));for(unsigned i=0;i<8;++i)assert(bridge.send(pause));assert(!bridge.send(pause));bridge.clear_pending();
 tick+=1000;std::vector<std::pair<ULONGLONG,std::size_t>> traffic;std::string last=row;
 for(unsigned i=0;i<300;++i){if(bridge.pending_count()<4)assert(bridge.send(bulk));tick+=25;bridge.flush(tick);row=read();if(row!=last){auto at=row.rfind('|');auto bytes=(row.size()-at-1)/2;assert(bytes<=1500);traffic.push_back({tick,bytes+16});ack(row.substr(0,row.find('|')));last=row;}}
 assert(traffic.size()>20);for(const auto& end:traffic){std::size_t bytes{};for(const auto& sample:traffic)if(sample.first<=end.first&&end.first-sample.first<1000)bytes+=sample.second;assert(bytes<=8192);}
 assert(std::filesystem::equivalent(root.parent_path(),std::filesystem::temp_directory_path()));std::filesystem::remove_all(root);
 std::cout<<"Transport: 1500-byte packets, strict rolling 8KiB/s including retries, control/setup/catalog priority, in-flight ACK stability and reserved control capacity passed\n";
}
