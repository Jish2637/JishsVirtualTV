#pragma once
#include "tv_sync.h"
#include "tv_sync_status.h"
#include "sync_selection.h"
#include <sstream>
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>
#include <deque>
#include <charconv>
#include <algorithm>
namespace virtualtv::sync {
struct RelayEvent { std::uint64_t from{}; Packet packet; };
struct MenuCommand { std::string name, argument; std::uint64_t sequence{}; };
class Bridge {
    using Clock=ULONGLONG(WINAPI*)();Clock now_=&GetTickCount64;
    std::filesystem::path outbound_,logfile_,storage_,menu_commands_,menu_state_,bridge_state_;
    std::vector<Peer> peers_;std::uint64_t status_serial_{},session_epoch_{};
    std::uint64_t menu_received_boot_{},menu_received_sequence_{};
    std::uint64_t seq_{},offset_{},myself_{},last_write_{},awaiting_ack_{},heartbeat_{},boot_{},received_{};
    struct Pending {std::string text;std::size_t bytes{};int priority{};};
    std::deque<Pending> pending_;
    std::uint64_t world_id_{};
    std::deque<std::pair<std::uint64_t,std::size_t>> traffic_;std::size_t traffic_bytes_{};
    std::string partial_;
    LobbyInfo lobby_{};
    static bool number(std::string_view text,std::uint64_t& value){auto r=std::from_chars(text.data(),text.data()+text.size(),value);return !text.empty()&&r.ec==std::errc{}&&r.ptr==text.data()+text.size();}
    static std::string hex(std::string_view value) {
        constexpr char digits[]="0123456789ABCDEF";
        std::string out;out.reserve(value.size()*2);
        for(unsigned char c:value){out.push_back(digits[c>>4]);out.push_back(digits[c&15]);}
        return out;
    }
    static std::optional<std::string> unhex(std::string_view value) {
        if(value.size()%2||value.size()>3000)return std::nullopt;
        const auto nib=[](char c)->int{if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;};
        std::string out;out.reserve(value.size()/2);
        for(std::size_t i=0;i<value.size();i+=2){int a=nib(value[i]),b=nib(value[i+1]);if(a<0||b<0)return std::nullopt;out.push_back(char((a<<4)|b));}
        return out;
    }

    void line(std::string_view line){
        if(status_serial_)return;
        auto at=line.find("VTVSYNC|");if(at==std::string_view::npos)return;
        auto text=line.substr(at+8);if(!text.empty()&&text.back()=='\r')text.remove_suffix(1);
        auto a=text.find('|');if(a==text.npos)return;auto kind=text.substr(0,a);text.remove_prefix(a+1);
        auto b=text.find('|');if(b==text.npos)return;auto from=text.substr(0,b);auto body=text.substr(b+1);
        if(kind=="HB"){std::uint64_t id{},boot{};LobbyInfo next; if(number(from,id)&&heartbeat_info(body,boot,next)){if(boot!=boot_){boot_=boot;received_=0;}myself_=id;lobby_=next;heartbeat_=now_();}}
        else if(kind=="SENT"){std::uint64_t ack{};if(number(body,ack)&&ack==awaiting_ack_&&ack){awaiting_ack_=0;if(!pending_.empty())pending_.pop_front();}}
    }
public:
    explicit Bridge(Clock clock=&GetTickCount64):now_(clock){}
    bool initialize(const std::filesystem::path& dll_directory,const std::filesystem::path& test_storage={}){
        auto root=dll_directory;
        for(int i=0;i<6&&!root.empty();root=root.parent_path(),++i){
            auto package=root/L"Mods/JishsVirtualTVBridge/content/out.txt",log=root/L"logs/ReSkate.log";
            if(std::filesystem::is_regular_file(package)&&std::filesystem::is_regular_file(log)){
                // Never write to the watched plugin package: that causes a reload on every send.
                outbound_=root/L"Mods/.reskate_plugin_data/JishsVirtualTVBridge/sync_outbox.txt";
                std::error_code folder_error;std::filesystem::create_directories(outbound_.parent_path(),folder_error);
                if(folder_error){outbound_.clear();return false;}
                logfile_=log.make_preferred();
                menu_commands_=root/L"Mods/.reskate_plugin_data/JishsVirtualTVBridge/ui_commands.txt";
                menu_state_=root/L"Mods/.reskate_plugin_data/JishsVirtualTVBridge/ui_state.txt";
                bridge_state_=root/L"Mods/.reskate_plugin_data/JishsVirtualTVBridge/bridge_state.txt";std::error_code ec;offset_=std::filesystem::file_size(log,ec);if(ec)offset_=0;
                if(!test_storage.empty())storage_=test_storage;
                else{PWSTR local{};if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local))&&local)storage_=std::filesystem::path(local)/L"ReSkate/Plugins/JishsVirtualTVBridge/storage.json";else storage_=root/L"ReSkate.Plugins/JishsVirtualTVBridge/storage.json";CoTaskMemFree(local);}
                seq_=now_()*1000+GetCurrentProcessId()%1000;return true;
            }
            if(root==root.parent_path())break;
        }return false;
    }
    std::vector<Peer> peers()const{return online()?peers_:std::vector<Peer>{};}
    std::uint64_t session_epoch()const{return online()&&lobby_.active?(boot_*0x9e3779b97f4a7c15ull)^session_epoch_:0;}
    std::uint64_t world_id()const{return online()&&lobby_.active?world_id_:0;}
    std::size_t pending_count()const{return pending_.size();}
    void clear_pending(){pending_.clear();awaiting_ack_=0;}
    void poll_status(){
        HANDLE handle=CreateFileW(bridge_state_.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(handle==INVALID_HANDLE_VALUE)return;
        LARGE_INTEGER size{};std::string content;DWORD read{};
        if(!GetFileSizeEx(handle,&size)||size.QuadPart<0||size.QuadPart>65536){CloseHandle(handle);return;}
        content.resize(static_cast<std::size_t>(size.QuadPart));
        const bool ok=!!ReadFile(handle,content.data(),static_cast<DWORD>(content.size()),&read,nullptr);CloseHandle(handle);
        if(!ok||read!=content.size())return;
        std::istringstream file(content);
        std::string row;std::getline(file,row);std::replace(row.begin(),row.end(),'|',' ');
        std::istringstream header(row);std::string magic;std::uint64_t boot{},serial{},id{},epoch{},ack{};unsigned active{},others{},hosting{};
        if(!(header>>magic>>boot>>serial>>id>>active>>others>>hosting>>epoch>>ack)||(magic!="VTVB2"&&magic!="VTVB3")||!boot||!serial||active>1||hosting>1||others>256)return;
        std::uint64_t world{};if(magic=="VTVB3"){std::string world_text;if(!(header>>world_text)||world_text.size()>1024)return;auto decoded=unhex(world_text);if(!decoded)return;if(*decoded!="0"&&!decoded->empty()){world=1469598103934665603ull;for(unsigned char c:*decoded){world^=c;world*=1099511628211ull;}}}
        std::vector<Peer> peers;bool complete=false;
        while(std::getline(file,row)){
            if(!row.empty()&&row.back()=='\r')row.pop_back();
            if(row=="END|"+std::to_string(serial)){complete=true;break;}
            auto sep=row.find('|');std::uint64_t peer{};if(sep==row.npos||!number(std::string_view(row).substr(0,sep),peer)||!peer||peer==id||peers.size()>=256)return;
            auto name=unhex(std::string_view(row).substr(sep+1));if(!name||name->size()>128)return;
            for(char& c:*name)if(static_cast<unsigned char>(c)<32||c==127)c=' ';
            if(std::any_of(peers.begin(),peers.end(),[&](const Peer& p){return p.id==peer;}))return;
            peers.push_back({peer,name->empty()?"Player "+std::to_string(peer):*name});
        }
        if(!complete||peers.size()!=others)return;
        if(boot==boot_&&serial<=status_serial_)return;
        if(boot!=boot_){received_=0;clear_pending();}
        if(session_epoch_&&session_epoch_!=epoch){received_=0;clear_pending();}
        world_id_=world;boot_=boot;status_serial_=serial;session_epoch_=epoch;myself_=id;
        lobby_={active!=0,hosting!=0,true,others};peers_=std::move(peers);heartbeat_=now_();
        if(ack&&ack==awaiting_ack_){awaiting_ack_=0;if(!pending_.empty())pending_.pop_front();}
    }
    bool available()const{return !outbound_.empty();}
    bool online()const{return heartbeat_&&now_()-heartbeat_<6000;}
    std::uint64_t my_id()const{return online()?myself_:0;}
    LobbyInfo lobby()const{return online()?lobby_:LobbyInfo{};}
    bool send(const Packet& packet,std::uint64_t recipient=0){
        if(!available())return false;auto wire=encode(packet);if(!wire||wire->size()>1500)return false;
        const int priority=packet.kind==Kind::catalog?2:(packet.kind==Kind::setup_snapshot||packet.kind==Kind::setup_delta?1:0);
        if(pending_.size()>=(priority?24u:32u))return false;
        Pending next{std::to_string(recipient)+"|"+hex(*wire),wire->size()+16,priority};
        auto at=pending_.begin();if(awaiting_ack_&&at!=pending_.end())++at;
        while(at!=pending_.end()&&at->priority<=priority)++at;
        pending_.insert(at,std::move(next));return true;
    }

    // Local UI transport: ReSkate's documented reskate.files API owns this
    // folder, outside the mod package. No plugin reload, console text, client
    // memory access, or arbitrary Lua process execution is required.
    std::vector<MenuCommand> poll_menu_commands(){
        std::vector<MenuCommand> result;
        if(menu_commands_.empty()||!online()||!boot_)return result;
        std::ifstream file(menu_commands_,std::ios::binary|std::ios::ate);
        if(!file)return result;
        auto length=file.tellg();if(length<0||length>65536)return result;
        file.seekg(0);std::string data(static_cast<std::size_t>(length),'\0');
        file.read(data.data(),static_cast<std::streamsize>(data.size()));
        if(data.rfind("VTVM1|",0)!=0)return result;
        data.erase(0,6);
        for(unsigned n=0;n<48&&!data.empty();++n){
            const auto end=data.find(';');const auto row=data.substr(0,end);
            if(end==data.npos)data.clear();else data.erase(0,end+1);
            auto a=row.find('|');if(a==row.npos)continue;
            auto b=row.find('|',a+1);if(b==row.npos)continue;
            auto c=row.find('|',b+1);if(c==row.npos)continue;
            std::uint64_t sender_boot{},sequence{};
            if(!number(std::string_view(row).substr(0,a),sender_boot)||
               !number(std::string_view(row).substr(a+1,b-a-1),sequence)||
               sender_boot!=boot_||sequence==0)continue;
            if(menu_received_boot_!=boot_){menu_received_boot_=boot_;menu_received_sequence_=0;}
            if(sequence<=menu_received_sequence_)continue;
            auto op=row.substr(b+1,c-b-1);
            if(op.empty()||op.size()>32||!std::all_of(op.begin(),op.end(),[](char v){return v>='A'&&v<='Z'||v=='_';}))continue;
            auto arg=unhex(std::string_view(row).substr(c+1));
            if(!arg||arg->size()>800)continue;
            menu_received_sequence_=sequence;
            result.push_back({std::move(op),std::move(*arg),sequence});
        }
        return result;
    }
    bool publish_menu_state(const std::string& state){
        if(menu_state_.empty()||state.size()>60000)return false;
        std::error_code error;
        std::filesystem::create_directories(menu_state_.parent_path(),error);
        if(error)return false;
        auto temp=menu_state_;temp+=L".tmp";
        {std::ofstream out(temp,std::ios::binary|std::ios::trunc);if(!out)return false;
         out<<"VTVUI1|"<<menu_received_boot_<<'|'<<menu_received_sequence_<<'\n'<<state;
         out.flush();if(!out)return false;}
        return !!MoveFileExW(temp.c_str(),menu_state_.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
    }
    void flush(std::uint64_t now){
        if(pending_.empty()||!available()||!my_id())return;
        if(now-last_write_<(awaiting_ack_?250u:25u))return;
        while(!traffic_.empty()&&now-traffic_.front().first>=1000){traffic_bytes_-=traffic_.front().second;traffic_.pop_front();}
        if(traffic_bytes_+pending_.front().bytes>8192)return;
        auto temp=outbound_;temp+=L".tmp";const auto sending=awaiting_ack_?awaiting_ack_:++seq_;
        {std::ofstream out(temp,std::ios::binary|std::ios::trunc);if(!out)return;out<<sending<<'|'<<pending_.front().text<<'\n';out.flush();if(!out)return;}
        if(MoveFileExW(temp.c_str(),outbound_.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){traffic_.emplace_back(now,pending_.front().bytes);traffic_bytes_+=pending_.front().bytes;awaiting_ack_=sending;last_write_=now;}
    }
    std::vector<RelayEvent> poll(){
        std::vector<RelayEvent> events;if(!available())return events;
        poll_status();
        std::ifstream input(logfile_,std::ios::binary|std::ios::ate);if(!input)return events;
        const auto size=input.tellg();if(size<0)return events;auto end=static_cast<std::uint64_t>(size);
        if(end<offset_){offset_=0;partial_.clear();if(!status_serial_){heartbeat_=myself_=0;lobby_={};}}
        if(end-offset_>32768){offset_=end-32768;partial_.clear();}
        input.seekg(static_cast<std::streamoff>(offset_));std::string chunk(static_cast<size_t>(end-offset_),'\0');input.read(chunk.data(),static_cast<std::streamsize>(chunk.size()));chunk.resize(static_cast<size_t>(input.gcount()));offset_+=chunk.size();partial_+=chunk;
        for(auto n=partial_.find('\n');n!=partial_.npos;n=partial_.find('\n')){line(std::string_view(partial_).substr(0,n));partial_.erase(0,n+1);}if(partial_.size()>32768)partial_.clear();
        if(!online())return events;
        // Packets live in private plugin storage, never in console/diagnostic logs.
        std::ifstream saved(storage_,std::ios::binary|std::ios::ate);if(!saved)return events;auto bytes=saved.tellg();if(bytes<0||bytes>65536)return events;
        saved.seekg(0);std::string json(static_cast<size_t>(bytes),'\0');saved.read(json.data(),static_cast<std::streamsize>(json.size()));
        auto key=json.find("\"vtv_inbox\"");if(key==json.npos)return events;auto colon=json.find(':',key+11);auto start=json.find('"',colon);if(colon==json.npos||start==json.npos)return events;auto stop=json.find('"',start+1);if(stop==json.npos)return events;
        auto records=std::string_view(json).substr(start+1,stop-start-1);
        while(!records.empty()){
            auto sep=records.find(';');auto row=records.substr(0,sep);if(sep==records.npos)records={};else records.remove_prefix(sep+1);
            std::uint64_t values[3]{};bool ok=true;for(auto& value:values){auto at=row.find('|');if(at==row.npos||!number(row.substr(0,at),value)){ok=false;break;}row.remove_prefix(at+1);}
            if(!ok||values[0]!=boot_||values[1]<=received_||!values[2])continue;
            auto raw=unhex(row);if(!raw)continue;auto packet=decode(*raw);if(!packet||packet->sender!=values[2])continue;
            received_=values[1];events.push_back({values[2],std::move(*packet)});if(events.size()==32)break;
        }return events;
    }
};
}
