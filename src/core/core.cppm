module;

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <string>
#include <string_view>

export module finkit.core;

export namespace finkit::core {

using std::string_view;
namespace fs = std::filesystem;

// ============================================================================
// Path utilities
// ============================================================================

[[nodiscard]] auto expand_path(fs::path p) -> fs::path {
    if (!p.empty() && p.native()[0] == '~') {
        const char* home = std::getenv("HOME");
        if (home != nullptr) {
            if (p.native().size() == 1) {
                return fs::path{home}; // Just "~"
            } else if (p.native().size() > 1 && p.native()[1] == '/') {
                return fs::path{home} / p.native().substr(2); // "~/something"
            }
        }
    }
    return p;
}

// ============================================================================
// Logging
// ============================================================================

enum class LogLevel { Trace, Debug, Info, Warn, Error };

void set_log_level(LogLevel level) {
    switch (level) {
    case LogLevel::Trace:
        spdlog::set_level(spdlog::level::trace);
        break;
    case LogLevel::Debug:
        spdlog::set_level(spdlog::level::debug);
        break;
    case LogLevel::Info:
        spdlog::set_level(spdlog::level::info);
        break;
    case LogLevel::Warn:
        spdlog::set_level(spdlog::level::warn);
        break;
    case LogLevel::Error:
        spdlog::set_level(spdlog::level::err);
        break;
    }
}

void set_log_level(string_view level) {
    if (level == "trace")
        set_log_level(LogLevel::Trace);
    else if (level == "debug")
        set_log_level(LogLevel::Debug);
    else if (level == "info")
        set_log_level(LogLevel::Info);
    else if (level == "warn")
        set_log_level(LogLevel::Warn);
    else if (level == "error")
        set_log_level(LogLevel::Error);
    else
        set_log_level(LogLevel::Info);
}

} // namespace finkit::core
