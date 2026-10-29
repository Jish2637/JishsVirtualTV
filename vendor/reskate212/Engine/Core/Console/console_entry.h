#pragma once

#include "Engine/Core/Log/types.h"
#include <cstdint>
#include <string>

namespace dingosdk {
using ConsoleSeverity = logging::Level;
using ConsoleSource = logging::Channel;

struct ConsoleLogLine {
    std::uint64_t sequence{};
    std::string text;
    std::uint64_t elapsed_ms{};
    ConsoleSeverity severity{ConsoleSeverity::info};
    ConsoleSource source{ConsoleSource::runtime};
    logging::Context context{logging::default_context(source)};
};

std::uint64_t console_timestamp_ms() noexcept;
const char* console_source_name(ConsoleSource source) noexcept;
std::string format_console_line(const ConsoleLogLine& line, bool timestamp = true);
}
