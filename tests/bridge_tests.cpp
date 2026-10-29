#include "tv_sync_bridge.h"
#include <cassert>
#include <iostream>
using namespace virtualtv::sync;
static ULONGLONG time_now=100000;
static ULONGLONG WINAPI clock_now(){return time_now;}
static std::string hex(std::string_view s){std::string out;for(unsigned char c:s){out+="0123456789ABCDEF"[c>>4];out+="0123456789ABCDEF"[c&15];}return out;}
int main(){
 auto root=std::filesystem::temp_directory_path()/("vtv-bridge-"+std::to_string(GetCurrentProcessId()));
 std::filesystem::create_directories(root/"Mods/JishsVirtualTVBridge/content");std::filesystem::create_directories(root/"logs");
 auto out=root/"Mods/.reskate_plugin_data/JishsVirtualTVBridge/sync_outbox.txt",log=root/"logs/ReSkate.log",storage=root/"storage.json";
 std::filesystem::create_directories(out.parent_path());
 const auto package_marker=root/"Mods/JishsVirtualTVBridge/content/out.txt";
 std::ofstream(package_marker)<<"static package marker";const auto marker_time=std::filesystem::last_write_time(package_marker);
 std::ofstream(out)<<"";std::ofstream(log)<<"";
 Bridge bridge(clock_now);assert(bridge.initialize(root,storage)&&bridge.available()&&!bridge.online());
 auto append=[&](std::string s){std::ofstream(log,std::ios::app)<<s;};
 append("prefix VTVSYNC|HB|42|9");bridge.poll();assert(!bridge.online());append("\n");bridge.poll();assert(bridge.online()&&bridge.my_id()==42);
 Packet p{Kind::ping,2,3,100,5000,0,""};auto wire=encode(p);assert(wire);
 std::ofstream(storage)<<"{\"vtv_inbox\":\"9|1|100|"<<hex(*wire)<<"\"}";
 auto events=bridge.poll();assert(events.size()==1&&events[0].from==100);assert(bridge.poll().empty());
 assert(bridge.send(p));bridge.flush(time_now);std::string first;{std::ifstream f(out);std::getline(f,first);}auto seq=first.substr(0,first.find('|'));if(seq.empty())std::wcerr<<L"out="<<out.wstring()<<L" exists="<<std::filesystem::exists(out)<<L" winerr="<<GetLastError()<<L" id="<<bridge.my_id()<<L"\n";assert(!seq.empty());
 append("VTVSYNC|SENT|0|1\n");bridge.poll();time_now+=2001;bridge.flush(time_now);std::string retry;{std::ifstream f(out);std::getline(f,retry);}assert(first==retry);
 append("VTVSYNC|SENT|0|"+seq+"\n");bridge.poll();assert(bridge.send(p,123));time_now+=201;bridge.flush(time_now);{std::ifstream f(out);std::getline(f,retry);}assert(retry!=first&&retry.find("|123|")!=retry.npos);
 time_now+=6001;assert(!bridge.online()&&bridge.my_id()==0);
 append("VTVSYNC|HB|0|10\n");assert(bridge.poll().empty());assert(bridge.online()&&bridge.my_id()==0);
 append("VTVSYNC|HB|43|10\n");bridge.poll();std::ofstream(storage)<<"{\"vtv_inbox\":\"10|1|100|"<<hex(*wire)<<"\"}";assert(bridge.poll().size()==1);
 std::ofstream(log,std::ios::trunc)<<"";bridge.poll();assert(!bridge.online());append("VTVSYNC|HB|44|11\n");bridge.poll();assert(bridge.my_id()==44);
 append("VTVSYNC|HB|44|11|1|0|1\n");bridge.poll();assert(bridge.lobby().reported&&bridge.lobby().active&&bridge.lobby().hosting&&bridge.lobby().others==0);
 append("VTVSYNC|HB|44|11|1|2|1\n");bridge.poll();assert(bridge.lobby().others==2);
 append("VTVSYNC|HB|0|11|0|0|0\n");bridge.poll();assert(bridge.lobby().reported&&!bridge.lobby().active&&bridge.my_id()==0);
 assert(std::filesystem::last_write_time(package_marker)==marker_time);
 {std::ifstream f(package_marker);std::string text;std::getline(f,text);assert(text=="static package marker");}
 assert(std::filesystem::equivalent(root.parent_path(),std::filesystem::temp_directory_path()));std::filesystem::remove_all(root);
 std::cout<<"Bridge partial lines, boot/reload, private payload, ack/retry, heartbeat expiry and truncation passed\n";
}
