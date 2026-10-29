#pragma once
#include "tv_runtime.h"
#include "tv_sync.h"
#include <map>
#include <tuple>
#include <set>
#include <cstring>
#include <cmath>
#include <algorithm>
namespace virtualtv::fullsync {
inline constexpr std::size_t max_document=128*1024, chunk_size=1400;
struct Ref {
    std::uint64_t owner{},epoch{},object{};
    bool operator<(const Ref& b)const{return std::tie(owner,epoch,object)<std::tie(b.owner,b.epoch,b.object);}
    bool operator==(const Ref& b)const{return owner==b.owner&&epoch==b.epoch&&object==b.object;}
};
struct Descriptor {Ref ref;Placement pose;Vec3 scale{1,1,1};std::string asset,name;};
struct Output {Descriptor object;Screen screen;OutputKind kind{};bool primary{};};
struct Setup {std::uint64_t revision{};bool directional{true};float range{35};std::vector<Output> outputs;};
struct Writer {
    std::string bytes;
    void integer(std::uint64_t v){sync::u64(bytes,v);}
    void real(float v){std::uint32_t bits;std::memcpy(&bits,&v,4);for(unsigned i=0;i<4;++i)bytes.push_back(char(bits>>(8*i)));}
    void vec(Vec3 v){real(v.x);real(v.y);real(v.z);}
    void quat(Quat q){real(q.x);real(q.y);real(q.z);real(q.w);}
    void text(const std::string& s){integer(s.size());bytes+=s;}
    void ref(Ref r){integer(r.owner);integer(r.epoch);integer(r.object);}
    void descriptor(const Descriptor& d){ref(d.ref);vec(d.pose.position);quat(d.pose.rotation);vec(d.scale);text(d.asset);text(d.name);}
    void output(const Output& o){descriptor(o.object);integer(o.kind==OutputKind::Speaker);integer(o.primary);vec(o.screen.offset);quat(o.screen.rotation);real(o.screen.width);real(o.screen.height);real(o.screen.horizontal_curve_deg);real(o.screen.vertical_curve_deg);}
};
struct Reader {
    std::string_view bytes;std::size_t at{};bool ok{true};
    std::uint64_t integer(){if(at+8>bytes.size()){ok=false;return 0;}auto v=sync::read64(bytes,at);at+=8;return v;}
    float real(){if(at+4>bytes.size()){ok=false;return 0;}std::uint32_t bits{};for(unsigned i=0;i<4;++i)bits|=std::uint32_t((unsigned char)bytes[at++])<<(8*i);float v;std::memcpy(&v,&bits,4);if(!std::isfinite(v))ok=false;return v;}
    Vec3 vec(){return {real(),real(),real()};} Quat quat(){return {real(),real(),real(),real()};}
    std::string text(std::size_t max){auto n=integer();if(n>max||n>bytes.size()-std::min(at,bytes.size())){ok=false;return {};};auto s=std::string(bytes.substr(at,n));at+=n;for(auto c:s)if((unsigned char)c<32)ok=false;return s;}
    Ref ref(){return {integer(),integer(),integer()};}
    Descriptor descriptor(){Descriptor d;d.ref=ref();d.pose.position=vec();d.pose.rotation=quat();d.scale=vec();d.asset=text(256);d.name=text(128);return d;}
    Output output(){Output o;o.object=descriptor();auto role=integer(),primary=integer();if(role>1||primary>1)ok=false;o.kind=role?OutputKind::Speaker:OutputKind::Screen;o.primary=primary!=0;o.screen.offset=vec();o.screen.rotation=quat();o.screen.width=real();o.screen.height=real();o.screen.horizontal_curve_deg=real();o.screen.vertical_curve_deg=real();return o;}
    bool end()const{return ok&&at==bytes.size();}
};
inline bool valid_descriptor(const Descriptor& d){auto norm=[](Quat q){return q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;};return (d.ref.owner||((d.ref.object>>63)&&d.asset.empty()&&d.name.empty()))&&d.ref.epoch&&d.ref.object&&d.asset.size()<=256&&d.name.size()<=128&&valid(d.pose,Screen{})&&attachment_store::reasonable(d.pose.position)&&norm(d.pose.rotation)>.98f&&norm(d.pose.rotation)<1.02f&&d.scale.x>0&&d.scale.y>0&&d.scale.z>0&&d.scale.x<=10000&&d.scale.y<=10000&&d.scale.z<=10000&&std::isfinite(d.scale.x+d.scale.y+d.scale.z);}
inline bool valid_setup(const Setup& s){if(!s.revision||!std::isfinite(s.range)||s.range<5||s.range>1000||s.outputs.size()>128)return false;unsigned screens{},speakers{},primary{};std::set<Ref> seen;
    for(const auto& o:s.outputs){if(!valid_descriptor(o.object)||!valid(o.object.pose,o.screen)||std::abs(o.screen.rotation.x*o.screen.rotation.x+o.screen.rotation.y*o.screen.rotation.y+o.screen.rotation.z*o.screen.rotation.z+o.screen.rotation.w*o.screen.rotation.w-1)>0.02f||o.screen.width>10000||o.screen.height>10000||!attachment_store::reasonable(o.screen.offset)||!seen.insert(o.object.ref).second)return false;if(o.kind==OutputKind::Speaker){++speakers;if(o.primary)return false;}else ++screens;primary+=o.primary;}
    return screens<=64&&speakers<=64&&primary<=1;}
inline std::string encode_setup(const Setup& s){Writer w;w.bytes="FS2S";w.integer(s.revision);w.integer(s.directional);w.real(s.range);w.integer(s.outputs.size());for(const auto& o:s.outputs)w.output(o);return w.bytes;}
inline std::optional<Setup> decode_setup(std::string_view bytes){if(bytes.size()>max_document||bytes.substr(0,4)!="FS2S")return {};Reader r{bytes.substr(4)};Setup s;s.revision=r.integer();auto dir=r.integer();s.directional=dir!=0;s.range=r.real();auto n=r.integer();if(n>128||dir>1)return {};for(unsigned i=0;i<n;++i)s.outputs.push_back(r.output());if(!r.end()||!valid_setup(s))return {};return s;}
inline std::string encode_delta(const Setup& before,const Setup& after){Writer w;w.bytes="FS2D";w.integer(before.revision);w.integer(after.revision);w.integer(after.directional);w.real(after.range);std::vector<Ref> removed;std::vector<Output> changed;
    for(const auto& o:before.outputs)if(std::none_of(after.outputs.begin(),after.outputs.end(),[&](const auto& n){return n.object.ref==o.object.ref;}))removed.push_back(o.object.ref);
    for(const auto& o:after.outputs){auto it=std::find_if(before.outputs.begin(),before.outputs.end(),[&](const auto& n){return n.object.ref==o.object.ref;});Writer a,b;a.output(o);if(it!=before.outputs.end()){auto prior=*it;prior.object=o.object;b.output(prior);}if(a.bytes!=b.bytes)changed.push_back(o);}
    w.integer(removed.size());for(auto ref:removed)w.ref(ref);w.integer(changed.size());for(const auto& o:changed)w.output(o);return w.bytes;}
inline std::optional<Setup> apply_delta(const Setup& before,std::string_view bytes){if(bytes.size()>max_document||bytes.substr(0,4)!="FS2D")return {};Reader r{bytes.substr(4)};if(r.integer()!=before.revision)return {};Setup s=before;s.revision=r.integer();auto dir=r.integer();s.directional=dir!=0;s.range=r.real();if(s.revision<=before.revision||dir>1)return {};auto n=r.integer();if(n>128)return {};std::set<Ref> touched;for(unsigned i=0;i<n;++i){auto ref=r.ref();if(!touched.insert(ref).second)return {};auto it=std::find_if(s.outputs.begin(),s.outputs.end(),[&](auto& o){return o.object.ref==ref;});if(it==s.outputs.end())return {};s.outputs.erase(it);}n=r.integer();if(n>128)return {};for(unsigned i=0;i<n;++i){auto o=r.output();if(!touched.insert(o.object.ref).second)return {};auto it=std::find_if(s.outputs.begin(),s.outputs.end(),[&](auto& a){return a.object.ref==o.object.ref;});if(it==s.outputs.end())s.outputs.push_back(o);else *it=o;}if(!r.end()||!valid_setup(s))return {};return s;}
inline bool same_pose(const Descriptor& d,const OutputAnchor& a){const auto p=d.pose.position,q=a.placement.position;float dx=p.x-q.x,dy=p.y-q.y,dz=p.z-q.z;if(dx*dx+dy*dy+dz*dz>.0025f)return false;const auto x=d.pose.rotation,y=a.placement.rotation;float dot=std::abs(x.x*y.x+x.y*y.y+x.z*y.z+x.w*y.w);float nx=x.x*x.x+x.y*x.y+x.z*x.z+x.w*x.w,ny=y.x*y.x+y.y*y.y+y.z*y.z+y.w*y.w;if(nx<.98f||ny<.98f||dot/std::sqrt(nx*ny)<.999847695f)return false;auto scale=[](float x,float y){return x>0&&y>0&&std::abs(x-y)<=.01f*std::max(x,y);};return scale(d.scale.x,a.scale.x)&&scale(d.scale.y,a.scale.y)&&scale(d.scale.z,a.scale.z);}
class Binder {
    std::map<Ref,Identity> bound_;
public:
    void clear(){bound_.clear();}
    std::optional<Identity> bind(const Descriptor& d,const std::vector<OutputAnchor>& local){if(!d.ref.owner)return {};auto old=bound_.find(d.ref);if(old!=bound_.end()){if(std::any_of(local.begin(),local.end(),[&](auto& a){return a.identity==old->second;}))return old->second;bound_.erase(old);}
        const OutputAnchor* match{};for(const auto& a:local)if(same_pose(d,a)){if(match)return {};match=&a;}if(!match)return {};bound_[d.ref]=match->identity;return match->identity;}
};
inline std::uint32_t checksum(std::string_view s){std::uint32_t h=2166136261u;for(unsigned char c:s){h^=c;h*=16777619u;}return h;}
inline std::vector<std::string> chunks(std::string_view data,std::uint64_t transfer){if(data.empty()||data.size()>max_document)return {};std::vector<std::string> out;auto n=(data.size()+chunk_size-1)/chunk_size;for(std::size_t i=0;i<n;++i){Writer w;w.bytes="FS2C";w.integer(transfer);w.integer(i);w.integer(n);w.integer(data.size());w.integer(checksum(data));w.bytes+=data.substr(i*chunk_size,chunk_size);out.push_back(std::move(w.bytes));}return out;}
class Assembly {
    std::uint64_t transfer_{},started_{},count_{},size_{},hash_{};std::vector<std::string> parts_;std::size_t received_{};
public:
    void clear(){*this=Assembly{};}
    bool pending()const{return transfer_!=0;}
    bool expired(std::uint64_t now)const{return pending()&&now-started_>=30000;}
    std::optional<std::string> accept(std::string_view wire,std::uint64_t now){if(wire.substr(0,4)!="FS2C"||wire.size()>1455)return {};Reader r{wire.substr(4)};auto transfer=r.integer(),index=r.integer(),count=r.integer(),size=r.integer(),hash=r.integer();if(!r.ok||!transfer||!size||size>max_document||count!=(size+chunk_size-1)/chunk_size||index>=count)return {};auto data=std::string(r.bytes.substr(r.at));if(data.size()!=std::min<std::uint64_t>(chunk_size,size-index*chunk_size))return {};
        if(transfer_!=transfer){if(pending())return {};transfer_=transfer;started_=now;count_=count;size_=size;hash_=hash;parts_.resize(count);}
        if(expired(now)||count_!=count||size_!=size||hash_!=hash){clear();return {};}
        if(!parts_[index].empty()){if(parts_[index]!=data)clear();return {};}
        received_+=data.size();parts_[index]=std::move(data);if(received_!=size_)return {};std::string complete;for(const auto& p:parts_)complete+=p;auto expected=hash_;clear();if(checksum(complete)!=expected)return {};return complete;}
};
}
