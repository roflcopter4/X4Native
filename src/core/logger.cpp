#include "Common.h"
#include "logger.h"
#include "game_api.h"

#include <x4_game_func_table.h>

#include <chrono>
#include <filesystem>
#include <format>
#include <memory>
#include <system_error>

namespace fs = std::filesystem;

namespace x4n {

std::vector<std::pair<LogLevel, std::string>> Logger::s_buffer;
Logger::HandleType Logger::s_handle = invalid_handle_value;
bool       Logger::s_buffering = true;
fs::path   Logger::s_mod_root;
fs::path   Logger::s_profile_dir;
std::mutex Logger::s_mutex;

static void output_debug_string(std::string const &msg)
{
#ifdef _WIN32
    wchar_t stack_buf[512];
    int size = ::MultiByteToWideChar(CP_UTF8, 0, msg.c_str(), static_cast<int>(msg.size()), stack_buf, 512);
    if (size > 0 && size < 512) {
        stack_buf[size] = L'\0';
        ::OutputDebugStringW(stack_buf);
    } else {
        // Fallback for long strings (rare)
        size = ::MultiByteToWideChar(CP_UTF8, 0, msg.c_str(), static_cast<int>(msg.size()), nullptr, 0);
        auto wbuf = std::make_unique<wchar_t[]>(size + 1);
        ::MultiByteToWideChar(CP_UTF8, 0, msg.c_str(), static_cast<int>(msg.size()), wbuf.get(), size);
        wbuf[size] = L'\0';
        ::OutputDebugStringW(wbuf.get());
    }
#else
    if (msg.ends_with('\n'))
        fwrite(msg.c_str(), 1, msg.size(), stderr);
    else
        fprintf(stderr, "%.*s\n", msg.size(), msg.c_str());
#endif
}

static constexpr char const *level_tag(LogLevel lv)
{
    switch (lv) {
    case LogLevel::Debug: return "debug";
    case LogLevel::Info:  return "info";
    case LogLevel::Warn:  return "warn";
    case LogLevel::Error: return "error";
    }
    return "?";
}

// Rotation failures that aren't "the file doesn't exist" are surfaced via
// OutputDebugStringA. We can't use the Logger itself here: open_log runs
// during Logger bring-up, so recursion would be unsafe.
static void log_rotate_error(char const *op, fs::path const &path, std::error_code const &ec)
{
    if (ec.value() == 0)
        return;
#ifdef _WIN32
    if (ec.value() == ERROR_FILE_NOT_FOUND || ec.value() == ERROR_PATH_NOT_FOUND)
        return;
#else
    if (ec.value() == ENOENT)
        return;
#endif

    std::string msg = std::format("X4Native: log {} '{}' failed ({} -> {})\n",
                                  op, path.string(), ec.value(), ec.message());
    output_debug_string(msg);
}

Logger::HandleType Logger::open_log(std::filesystem::path const &log_path)
{
    static constexpr int MAX_BACKUPS = 4;

    std::error_code ec;
    std::string base = fs::path(log_path).replace_extension().string();

    auto oldest = fs::path(base + ".4.log");
    if (!fs::remove(oldest, ec))
        log_rotate_error("delete", oldest, ec);

    for (int i = MAX_BACKUPS - 1; i >= 1; --i) {
        auto src = fs::path(base + '.' + std::to_string(i) + ".log");
        if (fs::exists(src)) {
            auto dst = fs::path(base + '.' + std::to_string(i + 1) + ".log");
            fs::rename(src, dst, ec);
            if (ec)
                log_rotate_error("rotate", src, ec);
        }
    }
    if (fs::exists(log_path)) {
        auto first_backup = fs::path(base + ".1.log");
        fs::rename(log_path, first_backup, ec);
        if (ec)
            log_rotate_error("rotate", log_path, ec);
    }

#ifdef _WIN32
    HANDLE h = CreateFileW(log_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        log_rotate_error("open", log_path, std::error_code(static_cast<int>(::GetLastError()), std::system_category()));
#else
    int h = open(log_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (h == -1)
        log_rotate_error("open", log_path, std::error_code(errno, std::system_category()));
#endif

    return h;
}

void Logger::init(fs::path const &mod_root)
{
    std::scoped_lock lock(s_mutex);
    s_mod_root  = mod_root;
    s_buffering = true;
    s_buffer.clear();
    s_handle = invalid_handle_value;
}

fs::path Logger::profile_log_dir()
{
    if (!s_profile_dir.empty())
        return s_profile_dir;

    auto *game = GameAPI::table();
    if (!game || !game->GetSaveFolderPath)
        return {};

    char const *raw = game->GetSaveFolderPath();
    if (!raw || !raw[0])
        return {};

    // GetSaveFolderPath returns "<profile>\save\" — strip the save/ suffix
    // to get the profile root, then append our own subfolder.
    fs::path save_folder = raw;
    fs::path profile = save_folder.has_filename()
        ? save_folder.parent_path()
        : save_folder.parent_path().parent_path();

    fs::path dir = profile / "x4native";
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec)
        return {};

    // Store with trailing separator so callers can concat a filename directly.
    s_profile_dir = std::move(dir);
    return s_profile_dir;
}

fs::path Logger::profile_ext_dir(std::string const &extension_id)
{
    if (extension_id.empty())
        return {};
    fs::path base = profile_log_dir();
    if (base.empty())
        return {};

    fs::path dir = base / extension_id;
    std::error_code ec;
    create_directories(dir, ec);
    if (ec)
        return {};
    return dir;
}

bool Logger::is_safe_relative_name(std::string const &name, char const *ctx)
{
    if (name.empty()) {
        warn("{}: empty filename rejected", ctx ? ctx : "path");
        return false;
    }
    auto path = fs::path(name);
    if (path.is_absolute()) {
        warn("{}: absolute path rejected: '{}'", ctx ? ctx : "path", name);
        return false;
    }
    static fs::path const dotdot = "..";
    if (std::ranges::any_of(path, [](fs::path const &part) { return part == dotdot; })) {
        warn("{}: '..' in path rejected: '{}'", ctx ? ctx : "path", name);
        return false;
    }
    return true;
}

void Logger::open_files()
{
    fs::path dir = profile_log_dir();
    if (dir.empty())
        dir = s_mod_root; // fallback: write next to the extension

    HandleType h = open_log(dir / "x4native.log");

    std::vector<std::pair<LogLevel, std::string>> pending;
    {
        std::scoped_lock lock(s_mutex);
        s_handle    = h;
        s_buffering = false;
        pending.swap(s_buffer);
    }
    if (h == invalid_handle_value) {
        output_debug_string("X4Native: Failed to open framework log file\n");
        return;
    }

    // Flush buffered entries to the freshly-opened file.
    for (auto const &[lv, msg] : pending)
        write(lv, msg);
}

void Logger::shutdown()
{
    std::scoped_lock lock(s_mutex);
    if (s_handle != invalid_handle_value) {
        flush_and_close_handle(s_handle);
        s_handle = invalid_handle_value;
    }
    s_buffer.clear();
    s_buffering = false;
    s_profile_dir.clear();
    s_mod_root.clear();
}

// Write `line` to `h` under `mtx`, then (outside the lock) optionally flush.
// Flushing outside the lock keeps high-volume log callers from serialising
// on disk sync — FlushFileBuffers is thread-safe per MSDN.
static void write_handle(std::mutex &mtx, Logger::HandleType h, LogLevel level, std::string const &line)
{
    if (h == Logger::invalid_handle_value)
        return;
    std::unique_lock lock(mtx);

#ifdef _WIN32
    ::DWORD written;
    ::WriteFile(h, line.data(), static_cast<::DWORD>(line.size()), &written, nullptr);
    lock.unlock();
    if (level >= LogLevel::Info)
        ::FlushFileBuffers(h);
#else
    ::write(h, line.data(), line.size());
    lock.unlock();
    if (level >= LogLevel::Info)
        ::fsync(h);
#endif
}

void Logger::write(LogLevel level, std::string_view const &msg)
{
    auto now  = std::chrono::system_clock::now();
    auto line = std::format("[{:%Y-%m-%d %H:%M:%S}] [{}] {}\n", now, level_tag(level), msg);

    // Decide buffer vs file in a single critical section so we never race
    // with open_files() swapping s_buffering/s_handle.
    HandleType h;
    {
        std::scoped_lock lock(s_mutex);
        if (s_buffering) {
            s_buffer.emplace_back(level, std::string(msg));
            output_debug_string(line);
            return;
        }
        h = s_handle;
    }
    write_handle(s_mutex, h, level, line);
    output_debug_string(line);
}

void Logger::write_to(HandleType h, LogLevel level, std::string_view const &msg)
{
    auto now  = std::chrono::system_clock::now();
    auto line = std::format("[{:%Y-%m-%d %H:%M:%S}] [{}] {}\n", now, level_tag(level), msg);
    write_handle(s_mutex, h, level, line);
    output_debug_string(line);
}


void Logger::flush_handle(HandleType h)
{
    if (h == invalid_handle_value)
        return;
#ifdef _WIN32
    ::FlushFileBuffers(h);
#else
    ::fsync(h);
#endif
}

void Logger::close_handle(HandleType h)
{
    if (h == invalid_handle_value)
        return;
#ifdef _WIN32
    ::CloseHandle(h);
#else
    ::fsync(h);
#endif
}

void Logger::flush_and_close_handle(HandleType h)
{
    if (h == invalid_handle_value)
        return;
#ifdef _WIN32
    ::FlushFileBuffers(h);
    ::CloseHandle(h);
#else
    ::close(h);
    ::fsync(h);
#endif
}


} // namespace x4n
