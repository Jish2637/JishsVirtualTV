#include "tv_runtime.h"
#include <fstream>
#include <cassert>
#include <iostream>
int main(){
 auto path=std::filesystem::temp_directory_path()/("vtv-log-"+std::to_string(GetCurrentProcessId())+".txt");
 virtualtv::initialize_log(path);virtualtv::set_debug_logging(false);
 for(int i=0;i<100;++i)virtualtv::debug_log("routine scan");
 virtualtv::log("TV playback started");for(int i=0;i<100;++i)virtualtv::log("Repeated failure");
 virtualtv::set_debug_logging(true);virtualtv::debug_log("detailed diagnostics");
 virtualtv::flush_log();std::ifstream in(path);std::string all((std::istreambuf_iterator<char>(in)),{});in.close();
 assert(all.find("routine scan")==all.npos&&all.find("TV playback started")!=all.npos&&all.find("detailed diagnostics")!=all.npos);
 auto first=all.find("Repeated failure");assert(first!=all.npos&&all.find("Repeated failure",first+1)==all.npos);
 virtualtv::close_log();std::filesystem::remove(path);std::cout<<"Normal log filtering, repeat suppression and debug logging passed\n";
}
