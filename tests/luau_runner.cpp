#include "lua.h"
#include "lualib.h"
#include "luacode.h"
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <stdexcept>
std::string read(const char* path){std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error("Cannot read test input");return {(std::istreambuf_iterator<char>(f)),{}};}
void run(lua_State* state,const std::string& source){size_t size{};char* code=luau_compile(source.data(),source.size(),nullptr,&size);if(!code)throw std::runtime_error("Luau compilation failed");int status=luau_load(state,"VirtualTV bridge test",code,size,0);free(code);if(!status)status=lua_pcall(state,0,0,0);if(status)throw std::runtime_error(lua_tostring(state,-1));}
int main(int argc,char** argv){if(argc!=3)return 2;auto* state=luaL_newstate();luaL_openlibs(state);try{run(state,read(argv[1]));run(state,read(argv[2]));run(state,"verify_bridge()");lua_close(state);return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';lua_close(state);return 1;}}
