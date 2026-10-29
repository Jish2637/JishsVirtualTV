#pragma once
#include <atomic>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace virtualtv {
struct ResolvedMedia {
    std::string video, audio, user_agent, referer;
};
struct PlaylistEntry { std::string url, title; };
struct ResolveResult {
    ResolvedMedia media;
    std::vector<PlaylistEntry> entries;
    bool playlist{};
    std::string error; // Fixed diagnostics only: never raw helper output or URLs.
    unsigned exit_code{};
    bool ok{}, cancelled{};
};
bool youtube_url(const std::string& url);
bool youtube_playlist_url(const std::string& url);
ResolveResult parse_playlist(const std::string& json);
bool media_http_url(const std::string& url);
std::wstring quote_process_arg(const std::wstring& value);
ResolveResult parse_resolved_media(const std::string& json);
ResolveResult resolve_url(const std::filesystem::path& directory,const std::string& url,
                          const std::atomic<bool>& cancel,unsigned timeout_ms=90000,bool listing=false);
class UrlResolver {
    std::atomic<bool> cancel_{};
    std::thread worker_;
    std::mutex mutex_;
    std::optional<ResolveResult> result_;
public:
    ~UrlResolver(){cancel();}
    void start(const std::filesystem::path&,std::string,bool playlist=false);
    void cancel();
    std::optional<ResolveResult> take();
};
}
