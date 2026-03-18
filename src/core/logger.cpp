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

void Logger::init(const std::string& mod_root) {
    std::string path = mod_root + "x4native.log";
    s_handle = CreateFileA(
        path.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        OPEN_ALWAYS,        // append — don't lose entries across re-init
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (s_handle == INVALID_HANDLE_VALUE) {
        OutputDebugStringA("X4Native: Failed to open log file\n");
        return;
    }

    // Seek to end for append mode
    SetFilePointer(s_handle, 0, nullptr, FILE_END);

    // Write a separator so each init is visually distinct
    const char* sep = "\n========== X4Native core_init ==========\n";
    DWORD written;
    WriteFile(s_handle, sep, static_cast<DWORD>(strlen(sep)), &written, nullptr);
    FlushFileBuffers(s_handle);
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
