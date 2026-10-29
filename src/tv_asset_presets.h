#pragma once
// Portable, shareable screen alignment presets. Asset keys come from the
// ReSkate editor catalogue (EditorObject.item), not display names/instance IDs.
#include "tv_geometry.h"
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <istream>
#include <map>
#include <set>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>

namespace virtualtv::presets {
using Library=std::map<std::string, Screen>;
using Rules=std::map<std::string,bool>; // Preset-specific opt-in, including explicit false overrides.
inline constexpr const char* header="JISH_TV_ASSET_PRESETS_3";
inline constexpr const char* v2_header="JISH_TV_ASSET_PRESETS_2";
inline constexpr const char* legacy_header="JISH_TV_ASSET_PRESETS_1";
inline std::string normalize(std::string key) {
    if(key.empty()||key.size()>256)return {};
    for(char& c:key){const auto u=static_cast<unsigned char>(c);
        if(u<0x20||u==0x7f)return {};
        if(u>='A'&&u<='Z')c=static_cast<char>(u-'A'+'a');
    }
    return key;
}
inline bool read(std::istream& file,Library& out,Rules& rules) {
    std::string line;
    if(!std::getline(file,line)||(line!=header&&line!=v2_header&&line!=legacy_header))return false;
    const bool curves=line!=legacy_header, automode=line==header;
    Library items;Rules options;
    while(std::getline(file,line)){
        if(line.empty()||line[0]=='#')continue;
        std::istringstream in(line);std::string key;Screen value;
        if(!(in>>std::quoted(key)>>value.width>>value.height
            >>value.offset.x>>value.offset.y>>value.offset.z
            >>value.rotation.x>>value.rotation.y>>value.rotation.z>>value.rotation.w))continue;
        if(curves&&!(in>>value.horizontal_curve_deg>>value.vertical_curve_deg))continue;
        int automatic=0;
        if(automode&&(!(in>>automatic)||(automatic!=0&&automatic!=1)))continue;
        std::string excess;
        if(in>>excess)continue;
        key=normalize(std::move(key));
        if(!key.empty()&&valid(Placement{},value)&&items.size()<1024){items[key]=value;if(automode)options[key]=(automatic!=0);}
    }
    out=std::move(items);rules=std::move(options);return true;
}
inline bool read(std::istream& file,Library& out){Rules unused;return read(file,out,unused);}
inline bool write(std::ostream& file,const Library& data,const Rules& rules) {
    file<<header<<'\n'<<std::setprecision(9);
    for(const auto& [key,value]:data) {
        if(normalize(key).empty()||!valid(Placement{},value))continue;
        const auto option=rules.find(key);
        const bool automatic=option!=rules.end()&&option->second;
        file<<std::quoted(key)<<' '<<value.width<<' '<<value.height<<' '
            <<value.offset.x<<' '<<value.offset.y<<' '<<value.offset.z<<' '
            <<value.rotation.x<<' '<<value.rotation.y<<' '<<value.rotation.z<<' '<<value.rotation.w<<' '
            <<value.horizontal_curve_deg<<' '<<value.vertical_curve_deg<<' '<<int(automatic)<<'\n';
    }
    file.flush();return static_cast<bool>(file);
}
inline bool write(std::ostream& file,const Library& data){return write(file,data,{});}
inline Library merged(const Library& defaults,const Library& custom){
    Library result=defaults;
    for(const auto& [key,screen]:custom)result[key]=screen;
    return result;
}
inline bool automatic(const Rules& rules,const std::string& key){const auto it=rules.find(normalize(key));return it!=rules.end()&&it->second;}
inline bool find(const Library& library,const std::string& key,Screen& screen){
    const auto it=library.find(normalize(key));
    if(it==library.end())return false;
    screen=it->second;return true;
}
}
