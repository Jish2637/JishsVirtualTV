#include "url_resolver.h"
#include "vlc_player.h"
#include <windows.h>
#include <cassert>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <string_view>
#include <dbghelp.h>
#pragma comment(lib,"dbghelp.lib")
using namespace virtualtv;
static LONG WINAPI trace_failure(EXCEPTION_POINTERS* e){
    const auto process=GetCurrentProcess();SymInitialize(process,nullptr,TRUE);
    CONTEXT context=*e->ContextRecord;STACKFRAME64 frame{};frame.AddrPC={context.Rip,AddrModeFlat};frame.AddrStack={context.Rsp,AddrModeFlat};frame.AddrFrame={context.Rbp,AddrModeFlat};
    std::cerr<<"Exception 0x"<<std::hex<<e->ExceptionRecord->ExceptionCode<<'\n';
    for(unsigned i=0;i<30&&frame.AddrPC.Offset;++i){
        char storage[sizeof(SYMBOL_INFO)+MAX_SYM_NAME]{};auto symbol=reinterpret_cast<SYMBOL_INFO*>(storage);symbol->SizeOfStruct=sizeof(SYMBOL_INFO);symbol->MaxNameLen=MAX_SYM_NAME;DWORD64 displacement{};
        HMODULE mod{};GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(frame.AddrPC.Offset),&mod);wchar_t name[MAX_PATH]{};GetModuleFileNameW(mod,name,MAX_PATH);
        std::wcerr<<name<<L" +0x"<<std::hex<<(frame.AddrPC.Offset-reinterpret_cast<std::uintptr_t>(mod))<<L" ";
        if(SymFromAddr(process,frame.AddrPC.Offset,&displacement,symbol))std::cerr<<symbol->Name<<"+0x"<<displacement;std::cerr<<'\n';
        if(!StackWalk64(IMAGE_FILE_MACHINE_AMD64,process,GetCurrentThread(),&frame,&context,nullptr,SymFunctionTableAccess64,SymGetModuleBase64,nullptr))break;
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
int main(int argc,char** argv){
    if(!GetEnvironmentVariableW(L"TV_TEST_NO_FILTER",nullptr,0))SetUnhandledExceptionFilter(trace_failure);
    std::cout<<std::unitbuf;
    // The test executable doubles as a deterministic child-process fixture.
    if(argc>2&&std::string_view(argv[1])=="--ignore-config"){
        const std::string last=argv[argc-1];
        if(last.find("sleep")!=std::string::npos){Sleep(30000);return 0;}
        if(std::find_if(argv+1,argv+argc,[](const char* item){return std::string_view(item)=="--flat-playlist";})!=argv+argc){
            std::cout<<R"({"_type":"playlist","entries":[{"id":"12345678901","title":"First"},{"id":"23456789012","title":"Second"},{"id":"bad!","title":"Unavailable"}]})";
            return 0;
        }
        assert(std::find_if(argv+1,argv+argc,[](const char* item){return std::string_view(item)=="--simulate";})!=argv+argc);
        assert(std::find_if(argv+1,argv+argc,[](const char* item){return std::string_view(item)=="--print";})!=argv+argc);
        if(last.find("large")!=std::string::npos){
            const auto bytes=last.find("too-large")!=std::string::npos?33u*1024*1024:9u*1024*1024;
            std::cout<<"{\"description\":\""<<std::string(bytes,'x')<<"\",\"url\":\"https://media.example/video\",\"vcodec\":\"h264\",\"acodec\":\"aac\"}";return 0;
        }
        if(last.find("noisy")!=std::string::npos)std::cerr<<std::string(256*1024,'x');
        if(last.find("denied")!=std::string::npos){std::cerr<<"Sign in to confirm. sensitive-token-must-not-escape";return 1;}
        std::cout<<R"({"url":"https://media.example/video","vcodec":"h264","acodec":"aac","has_drm":null})";return 0;
    }
    if(argc>1&&std::string_view(argv[1])=="--youtube"){
        assert(argc==3);std::atomic<bool> cancel{};const auto root=std::filesystem::current_path();
        auto result=resolve_url(root,argv[2],cancel);std::cout<<"resolution_ok="<<result.ok<<" separate_audio="<<!result.media.audio.empty()<<" error="<<result.error<<'\n';if(!result.ok)return 1;
        VlcPlayer player;player.volume(0);std::wstring error;if(!player.play_resolved(result.media,error)){std::wcerr<<error<<'\n';return 2;}
        const auto deadline=GetTickCount64()+40000;std::uint64_t first{},last{};int tracks=-1;
        while(GetTickCount64()<deadline){VlcPlayer::Frame frame;if(player.copy_frame(frame)){if(!first)first=frame.sequence;last=frame.sequence;}tracks=player.audio_track_count();if(last>first+30&&tracks>1&&player.audio_active()&&player.audio_peak()>0)break;if(player.playback_state()==7)break;Sleep(100);}
        const auto state=player.playback_state();std::cout<<"vlc_state="<<state<<" advancing_frames="<<(last>first+30)<<" audio_tracks="<<tracks<<'\n';std::cout<<player.audio_diagnostics()<<'\n';const bool audio_ok=player.audio_active()&&player.audio_peak()>0;player.stop();return last>first+30&&tracks>1&&audio_ok?0:3;
    }
    assert(youtube_url("https://youtu.be/123?list=abc"));
    assert(youtube_playlist_url("https://www.youtube.com/playlist?list=PL1234"));
    assert(youtube_playlist_url("https://www.youtube.com/watch?v=123&list=PL1234"));
    assert(!youtube_playlist_url("https://www.youtube.com/watch?v=123"));
    assert(!youtube_playlist_url("https://www.youtube.com/watch?v=123&playlist=x"));
    auto parsed_list=parse_playlist(R"({"_type":"playlist","entries":[{"id":"12345678901","title":"First"},{"id":"23456789012","title":"Second"},{"id":"private","title":"Blocked"}]})");
    assert(parsed_list.ok&&parsed_list.playlist&&parsed_list.entries.size()==2);
    assert(parsed_list.entries[0].url=="https://www.youtube.com/watch?v=12345678901");
    assert(!parse_playlist(R"({"_type":"playlist","entries":[]})").ok);assert(youtube_url("https://www.youtube.com/watch?v=123"));assert(youtube_url("https://music.youtube.com/watch?v=123"));
    assert(!youtube_url("https://youtube.com.evil.example/watch"));assert(!youtube_url("https://youtube.com@evil.example/watch"));assert(!youtube_url("http://localhost:8096/Download?api_key=private"));
    assert(!media_http_url("https://example.com/\n--exec"));assert(!media_http_url("file:///movie.mp4"));assert(!media_http_url("https://"));
    assert(quote_process_arg(L"a b")==L"\"a b\"");assert(quote_process_arg(L"a\"b")==L"\"a\\\"b\"");assert(quote_process_arg(L"C:\\path\\")==L"\"C:\\path\\\\\"");
    auto single=parse_resolved_media(R"({"url":"https://media.example/v?token=secret","vcodec":"h264","acodec":"aac","has_drm":null,"http_headers":{"User-Agent":"test/1","Referer":"https://example.com/"}})");assert(single.ok&&single.media.audio.empty()&&single.media.user_agent=="test/1");
    auto split=parse_resolved_media(R"({"requested_formats":[{"url":"https://media.example/v","vcodec":"h264","acodec":"none"},{"url":"https://media.example/a","vcodec":"none","acodec":"aac"}]})");assert(split.ok&&!split.media.audio.empty());
    for(const auto* invalid:{"not json",R"({"_type":"playlist"})",R"({"url":"file:///private","vcodec":"h264"})",R"({"url":"https://example.com/a","vcodec":"none","acodec":"aac"})",R"({"url":"https://example.com/v","vcodec":"h264","http_headers":{"Authorization":"secret"}})",R"({"url":"https://example.com/v","vcodec":"h264","http_headers":{"User-Agent":"bad\r\nheader"}})"}){auto r=parse_resolved_media(invalid);assert(!r.ok&&r.error.find("secret")==std::string::npos);}
    wchar_t self[32768]{};GetModuleFileNameW(nullptr,self,32768);
    const auto root=std::filesystem::temp_directory_path()/(L"TV resolver tests "+std::to_wstring(GetCurrentProcessId()));std::filesystem::create_directories(root/L"resolver");
    std::filesystem::copy_file(self,root/L"resolver/yt-dlp.exe",std::filesystem::copy_options::overwrite_existing);std::ofstream(root/L"resolver/deno.exe").put('x');
    std::atomic<bool> cancel{};assert(resolve_url(root,"https://example.com/good?value=\"quoted\"&next=one",cancel).ok);
    auto listed=resolve_url(root,"https://www.youtube.com/playlist?list=PL1234",cancel,90000,true);
    assert(listed.ok&&listed.playlist&&listed.entries.size()==2);
    auto denied=resolve_url(root,"https://example.com/denied",cancel);assert(!denied.ok&&denied.exit_code==1&&denied.error.find("sensitive")==std::string::npos);
    auto large=resolve_url(root,"https://example.com/large",cancel);assert(large.ok);
    auto noisy=resolve_url(root,"https://example.com/noisy",cancel);assert(noisy.ok);
    auto too_large=resolve_url(root,"https://example.com/too-large",cancel);assert(!too_large.ok&&too_large.error.find("size limit")!=std::string::npos);
    auto timeout=resolve_url(root,"https://example.com/sleep",cancel,100);assert(!timeout.ok&&timeout.error.find("timed out")!=std::string::npos);
    UrlResolver worker;worker.start(root,"https://example.com/sleep");Sleep(50);auto began=GetTickCount64();worker.cancel();assert(GetTickCount64()-began<5000&&!worker.take());
    worker.start(root,"https://example.com/good");std::optional<ResolveResult> ready;for(unsigned i=0;i<200&&!ready;++i){Sleep(10);ready=worker.take();}assert(ready&&ready->ok);worker.cancel();
    // Independent preparation cannot block current/foreground resolution.
    UrlResolver background,foreground;
    background.start(root,"https://example.com/sleep");
    foreground.start(root,"https://example.com/good");ready.reset();
    for(unsigned i=0;i<200&&!ready;++i){Sleep(10);ready=foreground.take();}
    assert(ready&&ready->ok&&!background.take());
    background.cancel();foreground.cancel();
    // Replacing a worker clears old completion and cancellation cannot publish stale success.
    background.start(root,"https://example.com/good");Sleep(100);
    background.start(root,"https://example.com/denied");ready.reset();
    for(unsigned i=0;i<200&&!ready;++i){Sleep(10);ready=background.take();}
    assert(ready&&!ready->ok&&ready->exit_code==1);background.cancel();assert(!background.take());
    // Only delete the exact fixture directory we created beneath the temp root.
    assert(std::filesystem::equivalent(root.parent_path(),std::filesystem::temp_directory_path()));std::filesystem::remove_all(root);
    std::cout<<"Resolver routing, parsing, quoting, hidden process IPC, errors, timeout and cancellation passed\n";
}
