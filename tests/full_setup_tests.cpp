#include "setup_sync_model.h"
#include <cassert>
#include <iostream>
#include <limits>
using namespace virtualtv;
using namespace virtualtv::fullsync;
static Descriptor descriptor(unsigned id=1){Descriptor d;d.ref={11,2,id};d.pose.position={10,20,30};d.scale={2,3,4};d.asset="asset/key";d.name="Object title";return d;}
int main(){
    auto d=descriptor();OutputAnchor a{{123,456,789,1},d.pose,d.scale,{},OutputKind::Screen};
    assert(same_pose(d,a));a.placement.position.x+=.049f;assert(same_pose(d,a));a.placement.position.x+=.002f;assert(!same_pose(d,a));a.placement=d.pose;
    a.placement.rotation={0,0,.017f,.9998555f};assert(same_pose(d,a));a.placement.rotation={0,0,.018f,.999838f};assert(!same_pose(d,a));a.placement=d.pose;
    a.scale.x=2.019f;assert(same_pose(d,a));a.scale.x=2.021f;assert(!same_pose(d,a));a.scale=d.scale;
    auto unnamed=d;unnamed.asset.clear();unnamed.name.clear();Binder anonymous;assert(anonymous.bind(unnamed,{a}));unnamed.ref={0,2,1ull<<63};assert(!anonymous.bind(unnamed,{a})&&valid_descriptor(unnamed));
    Binder binder;assert(!binder.bind(d,{}));auto overlap=a;overlap.identity.entity=124;assert(!binder.bind(d,{a,overlap}));assert(binder.bind(d,{a})==a.identity);
    a.placement.position={100,200,300};assert(binder.bind(d,{a,overlap})==a.identity); // retain verified local identity during motion
    assert(!binder.bind(d,{}));assert(!binder.bind(d,{a}));a.placement=d.pose;assert(binder.bind(d,{a}));
    auto recreated=a;recreated.identity.instance++;assert(binder.bind(d,{recreated})==recreated.identity);binder.clear();
    Setup setup;setup.revision=1;setup.range=71;setup.outputs.push_back({d,{},OutputKind::Screen,true});
    auto bytes=encode_setup(setup);assert(decode_setup(bytes));assert(!decode_setup(bytes+"junk"));
    auto decoded=*decode_setup(bytes);assert(decoded.outputs[0].object.ref==d.ref&&decoded.range==71); // no client handles or volume in model
    auto next=setup;next.revision=2;next.outputs[0].screen.offset={1,2,3};next.outputs[0].screen.horizontal_curve_deg=30;next.outputs.push_back({descriptor(2),{},OutputKind::Speaker,false});next.directional=false;next.range=300;
    auto delta=encode_delta(setup,next);auto applied=apply_delta(setup,delta);assert(applied&&applied->outputs.size()==2&&applied->range==300&&!applied->directional&&applied->outputs[0].screen.offset.y==2);assert(!apply_delta(next,delta));
    auto moving=next;moving.revision++;moving.outputs[0].object.pose.position.x+=10;assert(encode_delta(next,moving).size()==48); // native following sends no setup mutation
    auto removed=next;removed.revision=3;removed.outputs.erase(removed.outputs.begin());auto speaker_only=apply_delta(next,encode_delta(next,removed));assert(speaker_only&&speaker_only->outputs.size()==1&&speaker_only->outputs[0].kind==OutputKind::Speaker);
    auto bad=setup;bad.outputs.push_back(bad.outputs[0]);assert(!decode_setup(encode_setup(bad)));bad=setup;bad.range=std::numeric_limits<float>::infinity();assert(!decode_setup(encode_setup(bad)));
    bad=setup;bad.outputs[0].object.pose.rotation.w=0;assert(!decode_setup(encode_setup(bad)));
    Setup maximum;maximum.revision=10;for(unsigned i=1;i<=128;++i)maximum.outputs.push_back({descriptor(i),{},i<=64?OutputKind::Screen:OutputKind::Speaker,i==1});assert(valid_setup(maximum));bad=maximum;bad.outputs.push_back({descriptor(129),{},OutputKind::Speaker,false});assert(!valid_setup(bad));bad=maximum;bad.outputs[65].kind=OutputKind::Screen;assert(!valid_setup(bad));
    auto document=encode_setup(maximum);auto parts=chunks(document,1);assert(parts.size()>1);Assembly assembly;
    std::optional<std::string> result;for(auto it=parts.rbegin();it!=parts.rend();++it){assert(sync::encode({sync::Kind::setup_snapshot,1,1,11,0,0,*it})->size()<=1500);auto value=assembly.accept(*it,100);if(value)result=value;}
    assert(result&&*result==document&&!assembly.pending());
    assert(!assembly.accept(parts[0],100));assert(!assembly.accept(parts[0],200));assert(!assembly.expired(30099)&&assembly.expired(30100));assert(!assembly.accept(parts[1],30100));assert(!assembly.pending());
    assembly.accept(parts[0],1);auto corrupt=parts[0];corrupt.back()^=1;assert(!assembly.accept(corrupt,2)&&!assembly.pending());
    assembly.accept(parts[0],1);auto competing=chunks(document,2);assert(!assembly.accept(competing[1],2));result.reset();for(std::size_t i=1;i<parts.size();++i){auto value=assembly.accept(parts[i],3);if(value)result=value;}assert(result&&*result==document);
    assembly.clear();auto damaged=parts;damaged.back().back()^=1;result.reset();for(auto& part:damaged)if(auto value=assembly.accept(part,1))result=value;assert(!result);
    assert(chunks(std::string(max_document+1,'x'),1).empty());assert(!decode_setup(document.substr(0,document.size()-1)));
    std::cout<<"Full setup: unique pose binding, native movement, deletion/recreation, nonuniform scale, authority model, delta, speaker-only, limits, atomic chunks, timeout/conflict/CRC passed\n";
}
