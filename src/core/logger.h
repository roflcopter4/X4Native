#pragma once
// ---------------------------------------------------------------------------
// x4native_core.dll — Logger
//
// Lightweight logger with two sinks:
//   1. File sink  → <mod_root>/x4native.log   (rotated each init: keeps x4native.1-4.log)
//   2. MSVC sink  → OutputDebugString          (visible in debugger / DebugView)
//
// Uses C++23 std::format — no external dependencies.
// ---------------------------------------------------------------------------

#include "Common.h"

#include <filesystem>
#include <format>
#include <mutex>
#include <utility>
#include <vector>

namespace x4n {

enum class LogLevel { Debug, Info, Warn, Error };

class Logger
{
  public:
#ifdef _WIN32
    using HandleType = HANDLE;
    static constexpr HandleType invalid_handle_value = reinterpret_cast<HandleType>(-1LL);
#else
    using HandleType = int;
    static constexpr HandleType invalid_handle_value = -1;
#endif

    Logger() = delete;

    // Two-phase init: start in memory-buffering mode so early messages
    // (before GameAPI is up and we can resolve the profile path) are not lost.
    // `mod_root` is the extension folder — kept as a fallback path in case
    // the profile can't be resolved later.
    static void init(std::filesystem::path const &mod_root);

    // Resolve <profile>\x4native\ via GameAPI's GetSaveFolderPath, open
    // x4native.log there, and flush any messages buffered during init().
    // Must be called after GameAPI::init(). Falls back to `mod_root` if the
    // profile path can't be resolved.
    static void open_files();

    // Return "<profile>\x4native\" (with trailing separator), or empty if
    // the profile path isn't resolvable yet. Creates the directory on first
    // successful call. Cached.
    static std::filesystem::path profile_log_dir();

    // Return "<profile>\x4native\<extension_id>\" (with trailing separator),
    // or empty on failure. Creates the subdirectory if needed.
    static std::filesystem::path profile_ext_dir(std::string const &extension_id);

    // Validate a relative filename intended to live under a per-extension
    // log directory. Returns true iff `name` is non-empty, not absolute, and
    // contains no `..` segments. On rejection, logs a warning via `ctx` for
    // diagnostics and returns false.
    static bool is_safe_relative_name(std::string const &name, char const *ctx);

    static void shutdown();

    // Open (and rotate) a log file at an absolute path.
    // Strips the .log suffix to form the backup base name, shifts .1-.4 backups,
    // then creates a fresh file. Returns INVALID_HANDLE_VALUE on failure.
    // Used by both the framework log and per-extension logs.
    static HandleType open_log(std::filesystem::path const &log_path);

    static void write(LogLevel level, std::string_view const &msg);

    // Write to an arbitrary HANDLE — used by per-extension log routing.
    static void write_to(HandleType h, LogLevel level, std::string_view const &msg);

    template <typename... Args>
    static void debug([[maybe_unused]] std::format_string<Args...> fmt, [[maybe_unused]] Args &&...args)
    {
#ifndef NDEBUG
        write(LogLevel::Debug, std::format(fmt, std::forward<Args>(args)...));
#endif
    }

    template <typename... Args>
    static void info(std::format_string<Args...> fmt, Args &&...args)
    {
        write(LogLevel::Info, std::format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args>
    static void warn(std::format_string<Args...> fmt, Args &&...args)
    {
        write(LogLevel::Warn, std::format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args>
    static void error(std::format_string<Args...> fmt, Args &&...args)
    {
        write(LogLevel::Error, std::format(fmt, std::forward<Args>(args)...));
    }

    static void flush_handle(HandleType h);
    static void close_handle(HandleType h);
    static void flush_and_close_handle(HandleType h);

  private:
    static HandleType s_handle;
    static std::mutex s_mutex;

    // Buffer active between init() and open_files(): early lines land here
    // and get flushed to the file once opened. Discarded on open failure.
    static std::vector<std::pair<LogLevel, std::string>> s_buffer;
    static bool s_buffering;
    static std::filesystem::path s_mod_root;              // extension folder — fallback path
    static std::filesystem::path s_profile_dir; // "<profile>\x4native\" once resolved
};

} // namespace x4n
