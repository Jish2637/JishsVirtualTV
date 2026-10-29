#include "url_resolver.h"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <vector>
#include "../vendor/nlohmann/json.hpp"

namespace virtualtv {
namespace {
struct Handle {
    HANDLE value{};
    explicit Handle(HANDLE h=nullptr):value(h){}
    ~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
};
bool clean(const std::string& s){return s.size()<=32768&&std::none_of(s.begin(),s.end(),[](unsigned char c){return c<32||c==127;});}
std::string lower(std::string s){for(auto& c:s)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return s;}
std::wstring wide(const std::string& s){
    const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);
    if(!n)return {};std::wstring out(n,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),out.data(),n);return out;
}
ResolveResult failure(const char* text){ResolveResult r;r.error=text;return r;}
ResolveResult helper_failure(const std::string& raw,unsigned code){
    auto text=lower(raw);auto result=failure("Resolver failed. Check network access or update the bundled resolver.");result.exit_code=code;
    if(text.find("sign in")!=std::string::npos||text.find("login")!=std::string::npos||text.find("cookies")!=std::string::npos)
        result.error="The site requires sign-in or verification; this version supports public videos without browser cookies.";
    else if(text.find("drm")!=std::string::npos)result.error="This video is protected by DRM and cannot be played through this resolver.";
    else if(text.find("unsupported url")!=std::string::npos)result.error="This webpage is not supported by the bundled resolver.";
    else if(text.find("403")!=std::string::npos)result.error="The site denied access (HTTP 403). The resolver may need updating or the video may be restricted.";
    else if(text.find("not available")!=std::string::npos||text.find("unavailable")!=std::string::npos)result.error="The video is unavailable or restricted in this location.";
    else if(text.find("timed out")!=std::string::npos||text.find("unable to download")!=std::string::npos)result.error="The resolver could not retrieve the video. Check network access and try again.";
    return result;
}
}
bool media_http_url(const std::string& url){
    if(!clean(url))return false;
    const auto prefix=url.rfind("https://",0)==0?8u:url.rfind("http://",0)==0?7u:0u;
    if(!prefix||url.size()<=prefix)return false;
    auto end=url.find_first_of("/?#",prefix);auto host=url.substr(prefix,end-prefix);
    return !host.empty()&&host.find_first_of(" \\") == std::string::npos;
}
bool youtube_url(const std::string& url){
    if(!media_http_url(url))return false;const auto start=url.find("://")+3;
    const auto end=url.find_first_of("/?#",start);auto host=lower(url.substr(start,end-start));
    if(host.find('@')!=std::string::npos)return false;const auto port=host.find(':');if(port!=std::string::npos)host.resize(port);
    return host=="youtu.be"||host=="youtube.com"||(host.size()>12&&host.compare(host.size()-12,12,".youtube.com")==0);
}
bool youtube_playlist_url(const std::string& url){
    if(!youtube_url(url))return false;
    const auto q=url.find('?');if(q==std::string::npos)return false;
    const auto end=url.find('#',q);const auto args=url.substr(q+1,end==std::string::npos?std::string::npos:end-q-1);
    std::size_t at=0;
    while(at<args.size()){
        const auto next=args.find('&',at);
        const auto part=args.substr(at,next==std::string::npos?std::string::npos:next-at);
        if(part.rfind("list=",0)==0&&part.size()>5&&part.size()<160)return true;
        if(next==std::string::npos)break;at=next+1;
    }
    return false;
}
ResolveResult parse_playlist(const std::string& text){
    try{
        const auto info=nlohmann::json::parse(text);
        if(!info.is_object()||!info.contains("entries")||!info["entries"].is_array())return failure("This URL did not return a playlist.");
        ResolveResult result;result.playlist=true;
        for(const auto& item:info["entries"]){
            if(result.entries.size()>=300)break;
            if(!item.is_object())continue;
            const auto field=[&](const char* key)->std::string{
                return item.contains(key)&&item[key].is_string()?item[key].get<std::string>():std::string{};
            };
            const auto id=field("id");
            std::string address=field("webpage_url");
            if(!youtube_url(address))address=field("url");
            if(!youtube_url(address)){
                if(id.size()!=11||id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-",0)!=std::string::npos)continue;
                address="https://www.youtube.com/watch?v="+id;
            }
            if(address.size()>500||!media_http_url(address))continue;
            auto title=field("title");
            if(title.empty())title="Untitled video";
            if(title.size()>240)title.resize(240);
            for(char& c:title)if(static_cast<unsigned char>(c)<32)c=' ';
            result.entries.push_back({std::move(address),std::move(title)});
        }
        if(result.entries.empty())return failure("No public playable videos were listed in this playlist.");
        result.ok=true;return result;
    }catch(...){return failure("The resolver returned an invalid playlist listing.");}
}
std::wstring quote_process_arg(const std::wstring& value){
    std::wstring out=L"\"";unsigned slashes=0;
    for(auto c:value){if(c==L'\\'){++slashes;continue;}if(c==L'\"')out.append(slashes*2+1,L'\\');else out.append(slashes,L'\\');slashes=0;out+=c;}
    out.append(slashes*2,L'\\');out+=L'\"';return out;
}
ResolveResult parse_resolved_media(const std::string& text){
    try{
        const auto info=nlohmann::json::parse(text);
        if(!info.is_object()||info.value("_type",std::string{})=="playlist")return failure("Choose a single video URL rather than a playlist or room link.");
        ResolveResult result;std::vector<nlohmann::json> streams;
        if(info.contains("requested_formats")){if(!info["requested_formats"].is_array()||info["requested_formats"].size()>2)return failure("Unsupported resolver stream layout.");for(const auto& item:info["requested_formats"])streams.push_back(item);}
        else streams.push_back(info);
        for(const auto& stream:streams){
            if(stream.contains("has_drm")&&stream["has_drm"]==true)return failure("This video is protected by DRM.");
            const auto address=stream.value("url",std::string{});if(!media_http_url(address))return failure("Resolver returned an invalid media address.");
            const bool video=stream.value("vcodec",std::string{"none"})!="none";
            if(video){if(!result.media.video.empty())return failure("Resolver returned multiple video streams.");result.media.video=address;}
            else if(stream.value("acodec",std::string{"none"})!="none")result.media.audio=address;
            const auto headers=stream.value("http_headers",info.value("http_headers",nlohmann::json::object()));
            for(auto it=headers.begin();it!=headers.end();++it){
                const auto key=lower(it.key());const auto value=it.value().get<std::string>();if(!clean(value))return failure("Resolver returned invalid HTTP headers.");
                auto* dest=key=="user-agent"?&result.media.user_agent:key=="referer"?&result.media.referer:nullptr;
                if(dest){if(!dest->empty()&&*dest!=value)return failure("Video and audio require incompatible HTTP headers.");*dest=value;}
                else if(key!="accept"&&key!="accept-language"&&key!="accept-encoding"&&key!="sec-fetch-mode")return failure("This site requires HTTP headers not supported by the current VLC integration.");
            }
        }
        if(result.media.video.empty())return failure("No playable video stream was found.");
        result.ok=true;return result;
    }catch(...){return failure("Resolver returned an invalid or unsupported response.");}
}
ResolveResult resolve_url(const std::filesystem::path& directory,const std::string& url,const std::atomic<bool>& cancel,unsigned timeout_ms,bool listing){
    if(!media_http_url(url))return failure("Enter an http:// or https:// video address.");
    const auto exe=directory/L"resolver/yt-dlp.exe",deno=directory/L"resolver/deno.exe";
    if(!std::filesystem::is_regular_file(exe)||!std::filesystem::is_regular_file(deno))return failure("Resolver files are missing. Extract the complete mod package, including its resolver folder.");
    const auto input=wide(url);if(input.empty())return failure("Video address is not valid UTF-8.");
    std::vector<std::wstring> args={exe.wstring(),L"--ignore-config",L"--no-plugin-dirs",L"--no-playlist",L"--no-progress",L"--no-warnings",L"--no-cache-dir",L"--check-formats",L"--socket-timeout",L"15",L"--retries",L"1",L"--extractor-retries",L"1",L"--no-js-runtimes",L"--js-runtimes",L"deno:"+deno.wstring(),L"--format",L"bv[height<=1080][vcodec^=avc1]+ba[ext=m4a]/b[height<=1080]/bv[height<=1080]+ba/b",L"--simulate",L"--print",L"%(.{_type,url,vcodec,acodec,has_drm,http_headers,requested_formats})j",L"--",input};
    if(listing){
        if(!youtube_playlist_url(url))return failure("Paste a YouTube playlist URL with a list= identifier.");
        args={exe.wstring(),L"--ignore-config",L"--no-plugin-dirs",L"--yes-playlist",L"--flat-playlist",L"--playlist-end",L"300",L"--no-progress",L"--no-warnings",L"--no-cache-dir",L"--socket-timeout",L"15",L"--retries",L"1",L"--extractor-retries",L"1",L"--dump-single-json",L"--",input};
    }
    std::wstring command;for(const auto& arg:args){if(!command.empty())command+=L' ';command+=quote_process_arg(arg);}if(command.size()>=32767)return failure("Video address is too long.");
    SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};HANDLE rd{},wr{};
    if(!CreatePipe(&rd,&wr,&sa,64*1024))return failure("Cannot create resolver output pipe.");Handle output(rd),writer(wr);
    if(!SetHandleInformation(rd,HANDLE_FLAG_INHERIT,0))return failure("Cannot protect resolver pipe handles.");
    Handle null(CreateFileW(L"NUL",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr));
    if(null.value==INVALID_HANDLE_VALUE)return failure("Cannot create resolver standard handles.");
    SIZE_T attr_size{};InitializeProcThreadAttributeList(nullptr,1,0,&attr_size);std::vector<unsigned char> attributes(attr_size);
    auto* list=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());if(!InitializeProcThreadAttributeList(list,1,0,&attr_size))return failure("Cannot initialize resolver process attributes.");
    struct Attributes {LPPROC_THREAD_ATTRIBUTE_LIST p;~Attributes(){DeleteProcThreadAttributeList(p);}} cleanup{list};
    Handle job(CreateJobObjectW(nullptr,nullptr));JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!job.value||!SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof limits))return failure("Cannot create resolver lifetime guard.");
    STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof startup;startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;startup.StartupInfo.wShowWindow=SW_HIDE;startup.StartupInfo.hStdInput=null.value;startup.StartupInfo.hStdOutput=wr;startup.StartupInfo.hStdError=null.value;startup.lpAttributeList=list;
    // Stderr uses a separate pipe so warnings cannot corrupt JSON. Never log it.
    HANDLE er{},ew{};if(!CreatePipe(&er,&ew,&sa,64*1024))return failure("Cannot create resolver diagnostic pipe.");Handle errors(er),error_writer(ew);
    if(!SetHandleInformation(er,HANDLE_FLAG_INHERIT,0))return failure("Cannot protect resolver diagnostic handle.");
    HANDLE handles[]{wr,ew,null.value};if(!UpdateProcThreadAttribute(list,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles,sizeof handles,nullptr,nullptr))return failure("Cannot restrict resolver diagnostic inheritance.");startup.StartupInfo.hStdError=ew;
    PROCESS_INFORMATION process{};
    if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT,nullptr,directory.c_str(),&startup.StartupInfo,&process))return failure("Cannot launch bundled resolver. Check that security software has not blocked it.");
    Handle child(process.hProcess),thread(process.hThread);
    if(!AssignProcessToJobObject(job.value,child.value)){TerminateProcess(child.value,1);WaitForSingleObject(child.value,5000);return failure("Cannot guard the resolver process lifetime.");}
    CloseHandle(writer.value);writer.value=nullptr;CloseHandle(error_writer.value);error_writer.value=nullptr;
    if(ResumeThread(thread.value)==DWORD(-1))return failure("Cannot start resolver worker.");
    std::string json,diagnostics;const auto began=GetTickCount64();bool overflow=false;
    auto drain=[&](HANDLE pipe,std::string& dest,std::size_t limit,bool required){
        DWORD available{};std::size_t drained{};
        while(drained<256*1024&&PeekNamedPipe(pipe,nullptr,0,nullptr,&available,nullptr)&&available){
            char buffer[8192];DWORD n{};if(!ReadFile(pipe,buffer,std::min<DWORD>(available,sizeof buffer),&n,nullptr)||!n)break;
            drained+=n;const auto keep=std::min<std::size_t>(n,limit-dest.size());dest.append(buffer,keep);
            if(keep<n&&required)overflow=true; // Extra stderr is discarded, not mistaken for oversized JSON.
        }
        return drained;
    };
    for(;;){
        const auto drained=drain(rd,json,32*1024*1024,true)+drain(er,diagnostics,128*1024,false);
        if(cancel.load()||GetTickCount64()-began>=timeout_ms||overflow){TerminateJobObject(job.value,1);WaitForSingleObject(child.value,5000);auto result=failure(cancel.load()?"Resolution cancelled.":overflow?"Resolver response exceeded its size limit.":"Video resolution timed out. Try again.");result.cancelled=cancel.load();return result;}
        const auto state=WaitForSingleObject(child.value,drained?1:20);if(state==WAIT_OBJECT_0)break;if(state==WAIT_FAILED)return failure("Resolver process wait failed.");
    }
    drain(rd,json,32*1024*1024,true);drain(er,diagnostics,128*1024,false);DWORD code{};if(!GetExitCodeProcess(child.value,&code))return failure("Cannot read resolver exit status.");
    if(overflow)return failure("Resolver response exceeded its size limit.");if(code)return helper_failure(diagnostics,code);
    return listing?parse_playlist(json):parse_resolved_media(json);
}
void UrlResolver::cancel(){cancel_=true;if(worker_.joinable())worker_.join();std::lock_guard lock(mutex_);result_.reset();}
void UrlResolver::start(const std::filesystem::path& directory,std::string url,bool playlist){
    cancel();cancel_=false;worker_=std::thread([this,directory,url=std::move(url),playlist]{
        ResolveResult result;try{result=resolve_url(directory,url,cancel_,90000,playlist);}catch(...){result=failure("Resolver stopped after an internal failure.");}
        if(!cancel_){std::lock_guard lock(mutex_);result_=std::move(result);}
    });
}
std::optional<ResolveResult> UrlResolver::take(){std::lock_guard lock(mutex_);auto result=std::move(result_);result_.reset();return result;}
}
