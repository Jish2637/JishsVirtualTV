#pragma once
#include <filesystem>
#include <fstream>
#include <string_view>
namespace jishsfirstpersonmod {
inline constexpr std::uintmax_t trace_log_limit=8*1024*1024;
inline constexpr std::uintmax_t text_log_limit=1024*1024;
inline constexpr unsigned log_archives=3;
inline std::filesystem::path log_archive(const std::filesystem::path& path,unsigned index) {
    auto archive=path;archive+=std::filesystem::path("."+std::to_string(index));return archive;
}
// Caller serializes writes to this file. No sticky full/disabled state; a failed
// rotation falls back to appending, and later calls retry normal maintenance.
inline bool append_rotating_log(const std::filesystem::path& path,std::string_view payload,
    std::uintmax_t limit,unsigned archives=log_archives,std::string_view header={}) noexcept {
    try {
        std::filesystem::create_directories(path.parent_path());
        auto size=std::filesystem::exists(path) ? std::filesystem::file_size(path):0;
        if(size && size+payload.size()>limit) {
            try {
                for(unsigned i=archives;i>0;--i) {
                    const auto target=log_archive(path,i);
                    const auto source=i==1 ? path:log_archive(path,i-1);
                    if(std::filesystem::exists(target)) std::filesystem::remove(target);
                    if(std::filesystem::exists(source)) std::filesystem::rename(source,target);
                }
                size=std::filesystem::exists(path) ? std::filesystem::file_size(path):0;
            } catch(...) {
                size=std::filesystem::exists(path) ? std::filesystem::file_size(path):0;
            }
        }
        std::ofstream file(path,std::ios::app|std::ios::binary);
        if(!file) return false;
        if(!size) file.write(header.data(),static_cast<std::streamsize>(header.size()));
        file.write(payload.data(),static_cast<std::streamsize>(payload.size()));
        file.flush();return bool(file);
    } catch(...) { return false; }
}
}
