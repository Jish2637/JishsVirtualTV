#include "tv_runtime.h"
#include <fstream>
#include <iomanip>
#include <cmath>
#include <sstream>
#include <algorithm>
#include <map>
namespace virtualtv {
namespace { std::mutex log_mutex; std::filesystem::path log_path; std::atomic<bool> verbose_logging{false};std::ofstream log_stream;std::map<std::string,ULONGLONG> recent_logs; }
void set_debug_logging(bool enabled) noexcept {verbose_logging.store(enabled,std::memory_order_release);try{flush_log();}catch(...){}}
bool debug_logging() noexcept {return verbose_logging.load(std::memory_order_acquire);}
void initialize_log(const std::filesystem::path& p) { std::lock_guard lock(log_mutex);log_path=p;log_stream.close();log_stream.clear();log_stream.open(log_path,std::ios::trunc);recent_logs.clear(); }
void close_log(){std::lock_guard lock(log_mutex);log_stream.close();log_path.clear();recent_logs.clear();}
void flush_log(){std::lock_guard lock(log_mutex);if(log_stream)log_stream.flush();}
void debug_log(const std::string& message){if(debug_logging())log(message);}
void log(const std::string& message) { std::lock_guard lock(log_mutex);if(log_path.empty())return;auto& recent=recent_logs;const auto now=GetTickCount64();if(!debug_logging()){auto it=recent.find(message);if(it!=recent.end()&&now-it->second<5000)return;if(recent.size()>256)recent.clear();recent[message]=now;}auto& f=log_stream;if(f){SYSTEMTIME t{};GetLocalTime(&t);f<<std::setfill('0')<<std::setw(4)<<t.wYear<<'-'<<std::setw(2)<<t.wMonth<<'-'<<std::setw(2)<<t.wDay<<' '<<std::setw(2)<<t.wHour<<':'<<std::setw(2)<<t.wMinute<<':'<<std::setw(2)<<t.wSecond<<'.'<<std::setw(3)<<t.wMilliseconds<<" tick="<<GetTickCount64()<<" pid="<<GetCurrentProcessId()<<" tid="<<GetCurrentThreadId()<<" "<<message<<'\n';if(!debug_logging())f.flush();} }
Quat euler_degrees(Vec3 d) {
    constexpr float k=3.14159265359f/360;
    const float x=d.x*k,y=d.y*k,z=d.z*k,cx=std::cos(x),sx=std::sin(x),cy=std::cos(y),sy=std::sin(y),cz=std::cos(z),sz=std::sin(z);
    return {sx*cy*cz-cx*sy*sz,cx*sy*cz+sx*cy*sz,cx*cy*sz-sx*sy*cz,cx*cy*cz+sx*sy*sz};
}
bool load_settings(std::istream& in,Settings& out) {
    Settings next;std::string first;if(!(in>>first))return false;
    if(first=="JISH_TV_1"||first=="JISH_TV_2"||first=="JISH_TV_3") {
        if(!(in>>next.screen.width>>next.screen.height>>next.screen.offset.x>>next.screen.offset.y>>next.screen.offset.z
             >>next.screen.rotation.x>>next.screen.rotation.y>>next.screen.rotation.z>>next.screen.rotation.w
             >>next.saved.entity>>next.saved.instance>>next.saved.session>>next.saved.drop))return false;
        if(first=="JISH_TV_2"||first=="JISH_TV_3"){
            int directional{};
            if(!(in>>directional>>next.audio.range>>next.audio.volume)||(directional!=0&&directional!=1)||!valid_audio(next.audio))return false;
            next.audio.directional=directional!=0;
            if(first=="JISH_TV_3"&&!(in>>next.screen.horizontal_curve_deg>>next.screen.vertical_curve_deg))return false;
        }
    } else {
        std::istringstream width(first);if(!(width>>next.screen.width)||width.peek()!=EOF)return false;
        if(!(in>>next.screen.height>>next.screen.offset.x>>next.screen.offset.y>>next.screen.offset.z))return false;
    }
    if(!valid(Placement{},next.screen))return false;
    std::string extra;if(in>>extra)return false;
    out=next;return true;
}
void write_settings(std::ostream& out,const Settings& s) {
    out<<"JISH_TV_3\n"<<std::setprecision(9)<<s.screen.width<<' '<<s.screen.height<<' '<<s.screen.offset.x<<' '<<s.screen.offset.y<<' '<<s.screen.offset.z<<' '
       <<s.screen.rotation.x<<' '<<s.screen.rotation.y<<' '<<s.screen.rotation.z<<' '<<s.screen.rotation.w<<'\n'
       <<s.saved.entity<<' '<<s.saved.instance<<' '<<s.saved.session<<' '<<s.saved.drop<<'\n'
       <<s.audio.directional<<' '<<s.audio.range<<' '<<s.audio.volume<<'\n'
       <<s.screen.horizontal_curve_deg<<' '<<s.screen.vertical_curve_deg<<'\n';
}
bool can_restore(const Identity& saved,const std::vector<Identity>& live) {
    if(!saved.entity||!saved.instance||!saved.session)return false;
    unsigned matches=0;for(const auto& i:live)if(i==saved)++matches;return matches==1;
}
void filter_nearby(std::vector<NearbyObject>& rows,Vec3 player) {
    if(!std::isfinite(player.x)||!std::isfinite(player.y)||!std::isfinite(player.z)){rows.clear();return;}
    rows.erase(std::remove_if(rows.begin(),rows.end(),[&](auto& r){
        const auto p=r.placement.position;const float x=p.x-player.x,y=p.y-player.y,z=p.z-player.z;
        r.distance=std::sqrt(x*x+y*y+z*z);
        return !r.identity.entity||!r.identity.instance||!r.identity.session||!valid(r.placement,Screen{})||!std::isfinite(r.distance)||r.distance>100;
    }),rows.end());
    std::sort(rows.begin(),rows.end(),[](const auto& a,const auto& b){return a.identity.entity<b.identity.entity;});
    std::vector<NearbyObject> unique;unique.reserve(rows.size());
    for(std::size_t i=0;i<rows.size();){auto end=i+1;while(end<rows.size()&&rows[end].identity.entity==rows[i].identity.entity)++end;
        bool ambiguous=false;for(auto j=i+1;j<end;++j)if(!(rows[j].identity==rows[i].identity))ambiguous=true;
        if(!ambiguous)unique.push_back(rows[i]);i=end;
    }
    std::sort(unique.begin(),unique.end(),[](const auto& a,const auto& b){return a.distance==b.distance?a.identity.entity<b.identity.entity:a.distance<b.distance;});rows=std::move(unique);
}
}
