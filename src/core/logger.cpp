#include "logger.h"

#include <chrono>
#include <format>

namespace x4n {

HANDLE     Logger::s_handle = INVALID_HANDLE_VALUE;
std::mutex Logger::s_mutex;

static constexpr const char* level_tag(LogLevel lv) {
    switch (lv) {
        case LogLevel::Debug: return "debug";
        case LogLevel::Info:  return "info";
        case LogLevel::Warn:  return "warn";
        case LogLevel::Error: return "error";
    }
    return "?";
}

static void rotate_logs(const std::string& mod_root) {
    // Rotate: x4native.4.log is dropped, then 3→4, 2→3, 1→2, current→1
    static constexpr int MAX_BACKUPS = 4;
    std::string base = mod_root + "x4native";

    DeleteFileA((base + ".4.log").c_str());
    for (int i = MAX_BACKUPS - 1; i >= 1; --i) {
        std::string from = base + "." + std::to_string(i) + ".log";
        std::string to   = base + "." + std::to_string(i + 1) + ".log";
        MoveFileA(from.c_str(), to.c_str());
    }
    MoveFileA((base + ".log").c_str(), (base + ".1.log").c_str());
}

void Logger::init(const std::string& mod_root) {
    rotate_logs(mod_root);

    std::string path = mod_root + "x4native.log";
    s_handle = CreateFileA(
        path.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (s_handle == INVALID_HANDLE_VALUE)
        OutputDebugStringA("X4Native: Failed to open log file\n");
}

void Logger::shutdown() {
    if (s_handle != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(s_handle);
        CloseHandle(s_handle);
        s_handle = INVALID_HANDLE_VALUE;
    }
}

void Logger::write(LogLevel level, std::string_view msg) {
    auto now = std::chrono::system_clock::now();
    auto line = std::format("[{:%Y-%m-%d %H:%M:%S}] [{}] {}\n", now, level_tag(level), msg);

    {
        std::lock_guard lock(s_mutex);
        if (s_handle != INVALID_HANDLE_VALUE) {
            DWORD written;
            WriteFile(s_handle, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
            // Flush on info+ so important messages survive crashes
            if (level >= LogLevel::Info)
                FlushFileBuffers(s_handle);
        }
    }

    OutputDebugStringA(line.c_str());
}

} // namespace x4n
