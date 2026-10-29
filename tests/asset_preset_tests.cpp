#include "tv_asset_presets.h"
#include "tv_sync_status.h"
#include <cassert>
#include <sstream>
#include <iostream>
int main(){
    using namespace virtualtv;
    presets::Library a,b,c;
    presets::Rules wanted,restored;
    Screen one{};one.width=3.14f;one.offset={1.2f,-0.5f,4.f};one.horizontal_curve_deg=65;one.vertical_curve_deg=-25;
    a[presets::normalize("objects/Skate TV")]=one;
    wanted["objects/skate tv"]=true;
    std::stringstream stream;assert(presets::write(stream,a,wanted));assert(presets::read(stream,b,restored));
    assert(presets::automatic(restored,"OBJECTS/skate TV"));
    assert(!presets::automatic(restored,"not a preset"));
    // A personal override can explicitly disable auto-attach from a bundled preset.
    presets::Rules bundled{{"objects/skate tv",true}},personal{{"objects/skate tv",false}};
    for(const auto& [name,enabled]:personal)bundled[name]=enabled;
    assert(!presets::automatic(bundled,"objects/skate tv"));
    std::stringstream disabled_stream;
    assert(presets::write(disabled_stream,a,bundled));
    presets::Library disabled_library;presets::Rules disabled_rules;
    assert(presets::read(disabled_stream,disabled_library,disabled_rules));
    assert(disabled_rules.find("objects/skate tv")!=disabled_rules.end()&&!presets::automatic(disabled_rules,"objects/skate tv"));
    std::stringstream legacy("JISH_TV_ASSET_PRESETS_1\n\"wood\" 2 1 0 0 0 0 0 0 1\n");
    presets::Library older;assert(presets::read(legacy,older)&&older.at("wood").horizontal_curve_deg==0);
    std::stringstream oldv2("JISH_TV_ASSET_PRESETS_2\n\"old-tv\" 2 1 0 0 0 0 0 0 1 22 -12\n");
    presets::Library older2;presets::Rules no_autorules;
    assert(presets::read(oldv2,older2,no_autorules));
    assert(older2.at("old-tv").horizontal_curve_deg==22&&!presets::automatic(no_autorules,"old-tv"));
    Screen found;assert(presets::find(b,"OBJECTS/skate tv",found));assert(found.width==one.width&&found.horizontal_curve_deg==65&&found.vertical_curve_deg==-25);
    assert(found.offset.y==-0.5f);assert(!presets::normalize("bad\nasset").size());
    c[presets::normalize("objects/skate tv")]=Screen{};
    assert(presets::merged(b,c).begin()->second.width==1.4f);
    std::uint64_t boot{};sync::LobbyInfo lobby;
    assert(sync::heartbeat_info("19|1|0|1",boot,lobby));assert(boot==19&&lobby.hosting&&lobby.others==0);
    assert(sync::lobby_status(true,lobby,42).find("waiting for players")!=std::string::npos);
    assert(sync::heartbeat_info("19|0|0|0",boot,lobby));assert(sync::lobby_status(true,lobby,0).find("No players in session")!=std::string::npos);
    assert(sync::heartbeat_info("19",boot,lobby)&&!lobby.reported);
    assert(!sync::heartbeat_info("19|1|10000|0",boot,lobby));
    std::cout<<"asset presets + sync statuses OK\n";
}
