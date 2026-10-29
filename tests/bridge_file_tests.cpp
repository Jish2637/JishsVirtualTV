#include "tv_sync_bridge.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <atomic>
using namespace virtualtv::sync;
static ULONGLONG ticks=10000;
static ULONGLONG WINAPI now(){return ticks;}
int main(){
 auto root=std::filesystem::temp_directory_path()/("vtv-file-"+std::to_string(GetCurrentProcessId()));
 std::filesystem::create_directories(root/"Mods/JishsVirtualTVBridge/content");std::filesystem::create_directories(root/"Mods/.reskate_plugin_data/JishsVirtualTVBridge");std::filesystem::create_directories(root/"logs");
 const auto status=root/"Mods/.reskate_plugin_data/JishsVirtualTVBridge/bridge_state.txt",out=root/"Mods/.reskate_plugin_data/JishsVirtualTVBridge/sync_outbox.txt",log=root/"logs/ReSkate.log";
 std::filesystem::create_directories(out.parent_path());
 const auto package_marker=root/"Mods/JishsVirtualTVBridge/content/out.txt";
 std::ofstream(package_marker)<<"static package marker";const auto marker_time=std::filesystem::last_write_time(package_marker);
 std::ofstream(out)<<"";std::ofstream(log)<<"";
 Bridge b(now);assert(b.initialize(root,root/"storage.json"));
 auto snapshot=[&](unsigned serial,std::string ack="0",unsigned epoch=1){std::ofstream(status)<<"VTVB2|9|"<<serial<<"|42|1|2|1|"<<epoch<<"|"<<ack<<"\n11|416C696365\n22|426F62\nEND|"<<serial<<"\n";};
 snapshot(1);b.poll();assert(b.online()&&b.my_id()==42&&b.peers().size()==2&&b.peers()[0].name=="Alice");auto session=b.session_epoch();
 Packet p{Kind::ping,2,3,42,500,0,{}};assert(b.send(p));b.flush(ticks);std::string first;{std::ifstream f(out);std::getline(f,first);}auto seq=first.substr(0,first.find('|'));assert(!seq.empty());
 ticks+=2100;b.poll();b.flush(ticks);std::string retry;{std::ifstream f(out);std::getline(f,retry);}assert(first==retry);
 snapshot(2,seq);b.poll();assert(b.send(p,22));ticks+=201;b.flush(ticks);{std::ifstream f(out);std::getline(f,retry);}assert(retry!=first&&retry.find("|22|")!=retry.npos);
 ticks+=6001;b.poll();assert(!b.online()&&b.peers().empty()); // Unchanged file must not extend liveness.
 snapshot(3);b.poll();assert(b.online());
 std::ofstream(status)<<"VTVB2|9|4|42|1|1|1|1|0\n11|416C696365\n";ticks+=6001;b.poll();assert(!b.online()); // Torn snapshot rejected.
 snapshot(4,"0",2);b.poll();assert(b.online()&&b.session_epoch()!=session);
 snapshot(3);ticks+=6001;b.poll();assert(!b.online()); // Replayed serial rejected.
 snapshot(5);b.poll();std::ofstream(log,std::ios::trunc)<<"";b.poll();assert(b.online()); // Log rotation is unrelated.
 // The real plugin atomically replaces status files while the DLL polls them.
 std::atomic<bool> done{false};std::atomic<unsigned> replace_errors{};
 std::thread writer([&]{for(unsigned serial=6;serial<56;++serial){
   auto temp=status;temp+=".tmp";
   {std::ofstream f(temp,std::ios::binary);f<<"VTVB2|9|"<<serial<<"|42|1|0|1|2|0\nEND|"<<serial<<"\n";}
   bool replaced=false;for(unsigned retry=0;retry<4&&!replaced;++retry){replaced=!!MoveFileExW(temp.c_str(),status.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);if(!replaced)Sleep(1);}
   if(!replaced)++replace_errors;
   Sleep(1);
 }done=true;});
 while(!done.load())b.poll();writer.join();b.poll();assert(replace_errors==0&&b.online()&&b.my_id()==42);
 assert(std::filesystem::last_write_time(package_marker)==marker_time);
 {std::ifstream f(package_marker);std::string text;std::getline(f,text);assert(text=="static package marker");}
 assert(std::filesystem::equivalent(root.parent_path(),std::filesystem::temp_directory_path()));std::filesystem::remove_all(root);
 std::cout<<"File heartbeat, roster, retry, ack, timeout, epoch, torn writes and replay passed\n";
}
