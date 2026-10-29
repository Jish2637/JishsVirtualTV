#pragma once
#include "setup_sync_model.h"
#include "tv_sync_bridge.h"
#include "dx12_renderer.h"
#include <deque>
namespace virtualtv::fullsync {
// Worker-owned protocol state. Native reads remain on the existing host callback.
class Runtime {
    sync::Bridge& bridge_;
    std::uint64_t world_{},session_{},me_{},room_{},controller_{},watch_room_{},serial_{1},cap_at_{},catalog_at_{},edit_at_{},request_at_{},bind_at_{},names_at_{},catalog_reply_at_{},local_id_{};
    std::uint64_t observed_revision_{};unsigned attempts_{};bool enabled_{},watching_{},committed_{};
    ScreenMode local_mode_{ScreenMode::Hidden};
    std::set<std::uint64_t> roster_ids_;
    std::map<std::uint64_t,std::uint64_t> capabilities_, peer_epochs_, catalog_revisions_;
    std::map<Ref,Descriptor> catalog_;
    std::set<Ref> conflicts_;std::map<std::uint64_t,std::set<std::uint64_t>> retired_epochs_;
    std::map<Ref,std::string> sent_catalog_;std::size_t catalog_cursor_{};
    std::vector<std::pair<Identity,Ref>> identities_;
    std::map<std::tuple<std::uint64_t,unsigned,std::uint64_t>,Assembly> assemblies_;
    struct Job {sync::Packet packet;std::uint64_t recipient{};std::vector<std::string> parts;std::size_t at{};};
    std::deque<Job> jobs_;
    Binder binder_;Setup own_,watched_;
    std::size_t pending_{};std::string status_;
    bool roster(std::uint64_t id)const{return roster_ids_.count(id)!=0;}
    bool control(sync::Kind kind,std::uint64_t recipient,std::uint64_t room=0,std::string data={}){return bridge_.send({kind,room,0,me_,session_,world_,std::move(data)},recipient);}
    void job(sync::Kind kind,std::uint64_t room,std::uint64_t revision,std::string data,std::uint64_t recipient){
        if(jobs_.size()>=12)return;for(const auto& j:jobs_)if(j.packet.kind==kind&&j.packet.room==room&&j.packet.revision==revision&&j.recipient==recipient)return;
        auto parts=chunks(data,++serial_);if(parts.empty())return;
        jobs_.push_back({{kind,room,revision,me_,session_,world_,{}},recipient,std::move(parts),0});
    }
    std::vector<OutputAnchor> candidates()const{std::vector<OutputAnchor> out;auto add=[&](OutputAnchor a){if(std::none_of(out.begin(),out.end(),[&](auto& b){return a.identity==b.identity;}))out.push_back(a);};
        for(const auto& row:nearby_snapshot().objects)add({row.identity,row.placement,row.scale,{},{}});
        const auto scene=scene_snapshot();if(scene.attached)add({scene.identity,scene.placement,scene.scale,scene.screen,OutputKind::Screen});
        for(auto a:extra_outputs_snapshot())add(a);for(auto a:borrowed_outputs_snapshot())add(a.anchor);return out;}
    void remember(Descriptor d){auto it=catalog_.find(d.ref);if(it!=catalog_.end()&&!it->second.asset.empty()&&!d.asset.empty()&&(it->second.asset!=d.asset||it->second.name!=d.name))conflicts_.insert(d.ref);
        if(catalog_.size()<4096||it!=catalog_.end())catalog_[d.ref]=std::move(d);}
    Descriptor describe(const OutputAnchor& a,const std::vector<PlacementName>& names){Descriptor d;d.pose=a.placement;d.scale=a.scale;auto n=std::find_if(names.begin(),names.end(),[&](auto& n){return n.identity==a.identity;});
        if(n!=names.end()){d.asset=n->asset;d.name=n->name;if(n->owner)d.ref={n->owner,n->epoch,n->object};else if(n->object)d.ref={me_,(session_^n->epoch)|1ull,n->object};}
        if(!d.ref.owner){auto it=std::find_if(identities_.begin(),identities_.end(),[&](auto& p){return p.first==a.identity;});
            if(it==identities_.end()){Ref ref{0,session_,(1ull<<63)|++local_id_};identities_.push_back({a.identity,ref});d.ref=ref;}else d.ref=it->second;}
        return d;}
    Setup snapshot(const AudioSettings& audio){Setup s;s.directional=audio.directional;s.range=audio.range;s.revision=own_.revision?own_.revision:1;auto primary=scene_snapshot();auto names=placement_names_snapshot();
        if(primary.attached)s.outputs.push_back({describe({primary.identity,primary.placement,primary.scale,primary.screen,OutputKind::Screen},names),primary.screen,OutputKind::Screen,true});
        for(auto& a:extra_outputs_snapshot())s.outputs.push_back({describe(a,names),a.screen,a.kind,false});return s;}
    void request(std::uint64_t now){if(!watching_||!compatible(controller_)||attempts_>=3)return;
        if(control(sync::Kind::resync,controller_,watch_room_)){++attempts_;request_at_=now+30000+(attempts_-1)*2000;status_="Requesting complete broadcaster setup (attempt "+std::to_string(attempts_)+"/3).";}}
    void bind(std::uint64_t now){if(now<bind_at_)return;bind_at_=now+200;auto local=candidates();
        if(now>=names_at_){names_at_=now+1000;std::vector<PlacementName> names;
        std::map<Identity,std::vector<const Descriptor*>,IdentityLess> matches;
        const auto verified_names=placement_names_snapshot();
        for(const auto& [ref,d]:catalog_)if(!conflicts_.count(ref))if(auto id=binder_.bind(d,local)){
            const auto own_name=std::find_if(verified_names.begin(),verified_names.end(),[&](const auto& n){return !n.owner&&n.identity==*id;});
            if(own_name!=verified_names.end()&&!d.asset.empty()&&own_name->asset!=d.asset){conflicts_.insert(ref);continue;}
            matches[*id].push_back(&d);
        }
        for(const auto& [id,list]:matches){bool conflict=false;for(auto d:list)if(d->asset!=list.front()->asset||d->name!=list.front()->name)conflict=true;
            if(!conflict&&!list.front()->asset.empty()){const auto& d=*list.front();names.push_back({id,d.asset,d.name,d.ref.owner,d.ref.epoch,d.ref.object});}}
        publish_remote_names(std::move(names));}
        if(!watching_)return;std::vector<BorrowedOutput> outputs;pending_=0;std::set<Identity,IdentityLess> used;
        for(const auto& o:watched_.outputs){if(conflicts_.count(o.object.ref)){++pending_;continue;}auto current=catalog_.find(o.object.ref);const auto& descriptor=current==catalog_.end()?o.object:current->second;auto id=binder_.bind(descriptor,local);if(!id||!used.insert(*id).second){++pending_;continue;}
            outputs.push_back({{*id,{}, {1,1,1},o.screen,o.kind},o.primary});}
        set_borrowed_outputs(true,std::move(outputs));
        if(committed_&&!request_at_)status_="Watching broadcaster setup: "+std::to_string(watched_.outputs.size()-pending_)+" bound, "+std::to_string(pending_)+" pending (hidden/silent).";
    }
    struct IdentityLess {bool operator()(Identity a,Identity b)const{return std::tie(a.session,a.entity,a.instance,a.drop)<std::tie(b.session,b.entity,b.instance,b.drop);}};
public:
    explicit Runtime(sync::Bridge& bridge):bridge_(bridge){}
    bool compatible(std::uint64_t id)const{auto it=capabilities_.find(id);return world_&&it!=capabilities_.end()&&it->second==world_;}
    bool current_offer(const sync::Packet& p)const{auto it=peer_epochs_.find(p.sender);return compatible(p.sender)&&it!=peer_epochs_.end()&&p.timing_us==it->second&&p.position_ms==world_;}
    bool ready()const{return watching_&&committed_;}
    bool watching()const{return watching_;}
    bool directional()const{return watched_.directional;}
    float range()const{return watched_.range;}
    std::string status()const{return status_;}
    void leave(){if(watching_){set_borrowed_outputs(false,{});set_screen_mode(local_mode_);}watching_=committed_=false;controller_=watch_room_=0;watched_={};binder_.clear();catalog_.clear();conflicts_.clear();publish_remote_names({});assemblies_.clear();jobs_.clear();pending_=attempts_=0;observed_revision_=0;request_at_=0;status_.clear();}
    void stop_host(){room_=0;own_={};jobs_.clear();}
    void clear(){leave();stop_host();capabilities_.clear();peer_epochs_.clear();retired_epochs_.clear();catalog_revisions_.clear();catalog_.clear();sent_catalog_.clear();catalog_cursor_=0;identities_.clear();local_id_=0;publish_remote_names({});cap_at_=catalog_at_=names_at_=catalog_reply_at_=0;roster_ids_.clear();}
    void watch(std::uint64_t sender,std::uint64_t room,std::uint64_t now){if(watching_&&controller_==sender&&watch_room_==room)return;leave();local_mode_=current_screen_mode();watching_=true;controller_=sender;watch_room_=room;set_borrowed_outputs(true,{});set_screen_mode(ScreenMode::SyncedWorld);request(now);}
    void retry(std::uint64_t now){attempts_=0;assemblies_.clear();request(now);}
    void host(std::uint64_t room,const AudioSettings& audio){room_=room;own_=snapshot(audio);own_.revision=1;edit_at_=0;}
    void tick(std::uint64_t now,bool enabled,std::uint64_t session,const AudioSettings& audio){
        auto world=bridge_.world_id(),me=bridge_.my_id();if(world!=world_||session!=session_||me!=me_){clear();world_=world;session_=session;me_=me;}
        if(enabled_&&!enabled)clear();enabled_=enabled;if(!enabled||!world_||!me_){if(watching_)leave();return;}
        roster_ids_.clear();for(const auto& peer:bridge_.peers())roster_ids_.insert(peer.id);
        for(auto it=capabilities_.begin();it!=capabilities_.end();)if(!roster(it->first))it=capabilities_.erase(it);else ++it;
        for(auto it=catalog_.begin();it!=catalog_.end();)if(it->first.owner!=me_&&!roster(it->first.owner))it=catalog_.erase(it);else ++it;
        if(room_){auto peers=bridge_.peers();auto compatible_count=std::count_if(peers.begin(),peers.end(),[&](auto& p){return compatible(p.id);});status_="Sharing complete setup. Compatible peers: "+std::to_string(compatible_count)+" / "+std::to_string(peers.size())+". Both players need SelectiveSync5 DLL and bridge.";}
        if(now>=cap_at_){control(sync::Kind::capability,0,0,"FS3");cap_at_=now+5000;}
        for(auto it=assemblies_.begin();it!=assemblies_.end();)if(it->second.expired(now)){
            if(watching_&&std::get<0>(it->first)==controller_&&std::get<1>(it->first)!=unsigned(sync::Kind::catalog)&&attempts_<3)request_at_=now+1000;
            it=assemblies_.erase(it);
        }else ++it;
        if(watching_&&request_at_&&now>=request_at_)request(now);
        if(watching_&&request_at_&&now>=request_at_&&attempts_>=3)status_="Setup transfer failed after 3 attempts. Use Retry setup or reselect broadcaster.";
        if(room_&&now>=edit_at_){edit_at_=now+200;auto next=snapshot(audio);next.revision=own_.revision+1;auto delta=encode_delta(own_,next); // 40-byte globals plus two zero counts is the unchanged representation.
            if(delta.size()>48||next.directional!=own_.directional||next.range!=own_.range){own_=std::move(next);job(sync::Kind::setup_delta,room_,own_.revision,std::move(delta),0);}
            else {next.revision=own_.revision;own_=std::move(next);}}
        if(now>=catalog_at_&&jobs_.size()<4){catalog_at_=now+3000;auto local=candidates();std::vector<Descriptor> page;
            const auto names=placement_names_snapshot();
            for(const auto& a:local){auto n=std::find_if(names.begin(),names.end(),[&](auto& n){return n.identity==a.identity;});if(n==names.end()||n->owner)continue;auto d=describe(a,names);if(d.ref.owner!=me_)continue;Writer encoded;encoded.descriptor(d);auto it=sent_catalog_.find(d.ref);if(it==sent_catalog_.end()||it->second!=encoded.bytes)page.push_back(std::move(d));}
            if(!page.empty()){
                const auto start=catalog_cursor_%page.size();std::rotate(page.begin(),page.begin()+start,page.end());std::size_t sent{};
                for(std::size_t at=0;at<page.size()&&jobs_.size()<8;at+=64){Writer w;w.bytes="FS2N";auto end=std::min(page.size(),at+64);w.integer(end-at);for(std::size_t i=at;i<end;++i){w.descriptor(page[i]);Writer encoded;encoded.descriptor(page[i]);sent_catalog_[page[i].ref]=std::move(encoded.bytes);++sent;}job(sync::Kind::catalog,0,++serial_,std::move(w.bytes),0);}
                catalog_cursor_+=sent;
            }
            if(std::any_of(local.begin(),local.end(),[&](auto& a){return std::none_of(names.begin(),names.end(),[&](auto& n){return n.identity==a.identity;});}))control(sync::Kind::catalog_request,0);
        }
        bind(now);
        if(!jobs_.empty()&&bridge_.pending_count()<8){auto best=std::find_if(jobs_.begin(),jobs_.end(),[](auto& j){return j.packet.kind!=sync::Kind::catalog;});if(best==jobs_.end())best=jobs_.begin();auto packet=best->packet;packet.payload=best->parts[best->at];if(bridge_.send(packet,best->recipient)&&++best->at==best->parts.size())jobs_.erase(best);}
    }
    // Returns true for all setup protocol messages, including rejected ones.
    bool receive(const sync::RelayEvent& event,std::uint64_t allowed,std::uint64_t now){auto& p=event.packet;using K=sync::Kind;if(p.kind<K::capability||p.kind>K::resync)return false;
        if(!enabled_||!world_||p.sender!=event.from||p.sender==me_||!roster(p.sender)||p.position_ms!=world_)return true;
        if(p.kind==K::capability){if(p.payload=="FS3"&&p.timing_us&&!retired_epochs_[p.sender].count(p.timing_us)){bool fresh=!compatible(p.sender)||peer_epochs_[p.sender]!=p.timing_us;if(fresh&&peer_epochs_.count(p.sender)){if(retired_epochs_[p.sender].size()>=16)return true;retired_epochs_[p.sender].insert(peer_epochs_[p.sender]);}if(fresh&&watching_&&controller_==p.sender)leave();capabilities_[p.sender]=world_;peer_epochs_[p.sender]=p.timing_us;if(fresh){sent_catalog_.clear();catalog_at_=0;control(K::capability,p.sender,0,"FS3");}}return true;}
        if(!current_offer(p))return true;
        if(p.kind==K::catalog_request){if(now>=catalog_reply_at_){sent_catalog_.clear();catalog_at_=0;catalog_reply_at_=now+5000;}return true;}
        if(p.kind==K::resync){if(room_&&p.room==room_&&own_.revision){auto fresh=snapshot({own_.directional,own_.range,0});fresh.revision=own_.revision;own_=std::move(fresh);job(K::setup_snapshot,room_,own_.revision,encode_setup(own_),p.sender);}return true;}
        const bool setup=p.kind==K::setup_snapshot||p.kind==K::setup_delta;
        if(setup&&(!watching_||p.sender!=allowed||p.sender!=controller_||p.room!=watch_room_||!p.revision||p.revision<watched_.revision||(p.revision==watched_.revision&&!(p.kind==K::setup_snapshot&&request_at_))))return true;
        if(setup&&p.revision>observed_revision_){observed_revision_=p.revision;attempts_=1;request_at_=now+30000;if(committed_)status_="Receiving updated setup; retaining the last complete presentation.";}
        auto key=std::make_tuple(p.sender,unsigned(p.kind),p.revision);if(!assemblies_.count(key)&&assemblies_.size()>=8){
            if(!setup)return true;auto background=std::find_if(assemblies_.begin(),assemblies_.end(),[](auto& entry){return std::get<1>(entry.first)==unsigned(K::catalog);});if(background==assemblies_.end())return true;assemblies_.erase(background);
        }
        auto complete=assemblies_[key].accept(p.payload,now);if(!complete)return true;assemblies_.erase(key);
        if(setup){auto next=p.kind==K::setup_snapshot?decode_setup(*complete):apply_delta(watched_,*complete);
            if(!next||next->revision!=p.revision||(next->revision==watched_.revision&&encode_setup(*next)!=encode_setup(watched_))){if(attempts_<3){request_at_=now+1000;}return true;}
            watched_=std::move(*next);
            for(auto it=assemblies_.begin();it!=assemblies_.end();)if(std::get<0>(it->first)==controller_&&std::get<1>(it->first)!=unsigned(K::catalog)&&std::get<2>(it->first)<=watched_.revision)it=assemblies_.erase(it);else ++it;
            committed_=true;attempts_=0;request_at_=0;bind_at_=0;
            for(auto& o:watched_.outputs)remember(o.object);names_at_=0;bind(now);
        }else if(p.kind==K::catalog){if(p.revision<=catalog_revisions_[p.sender])return true;Reader r{*complete};if(r.bytes.substr(0,4)!="FS2N")return true;r.at=4;auto count=r.integer();if(count>64)return true;std::vector<Descriptor> rows;for(unsigned i=0;i<count;++i){auto d=r.descriptor();if(d.ref.owner!=p.sender||!valid_descriptor(d))return true;rows.push_back(std::move(d));}if(!r.end()||catalog_.size()+rows.size()>4096)return true;catalog_revisions_[p.sender]=p.revision;for(auto& d:rows)remember(std::move(d));names_at_=bind_at_=0;}
        return true;
    }
};
}
