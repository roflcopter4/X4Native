#include "version.h"
#include "logger.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <vector>

namespace x4n {

/// Primary: read <game_root>/version.dat — a plain-text file containing
/// the build number as a single line (e.g. "900" for v9.00).
static std::string read_version_dat() {
    // Resolve game root from X4.exe's location
    char exe_path[MAX_PATH];
    if (!GetModuleFileNameA(nullptr, exe_path, MAX_PATH))
        return {};

    std::string dir(exe_path);
    auto pos = dir.rfind('\\');
    if (pos == std::string::npos) return {};
    dir = dir.substr(0, pos + 1);  // includes trailing backslash

    std::ifstream f(dir + "version.dat");
    if (!f.is_open()) return {};

    std::string line;
    std::getline(f, line);

    // Trim whitespace
    line.erase(line.begin(), std::find_if(line.begin(), line.end(),
               [](unsigned char c) { return !std::isspace(c); }));
    line.erase(std::find_if(line.rbegin(), line.rend(),
               [](unsigned char c) { return !std::isspace(c); }).base(), line.end());

    return line;
}

/// Format the raw build number (e.g. "900") into a readable version
/// string (e.g. "9.00").
static std::string format_version(const std::string& raw) {
    // The version.dat value is an integer: major * 100 + minor.
    // Examples: 900 → 9.00, 750 → 7.50, 800 → 8.00
    try {
        int v = std::stoi(raw);
        int major = v / 100;
        int minor = v % 100;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%d.%02d", major, minor);
        return buf;
    } catch (...) {
        return raw;  // Couldn't parse — return as-is
    }
}

static std::string s_build_number;

std::string Version::detect() {
    // 1. Try version.dat (simple, reliable)
    std::string raw = read_version_dat();
    if (!raw.empty()) {
        s_build_number = raw;
        std::string ver = format_version(raw);
        Logger::info("Detected game version: {} (build {})", ver, raw);
        return ver;
    }

    // 2. Fallback: X4.exe PE file version
    char exe_path[MAX_PATH];
    if (!GetModuleFileNameA(nullptr, exe_path, MAX_PATH))
        return "unknown";

    DWORD dummy = 0;
    DWORD size  = GetFileVersionInfoSizeA(exe_path, &dummy);
    if (size == 0) {
        Logger::warn("Version: no version info found");
        return "unknown";
    }

    std::vector<BYTE> buf(size);
    if (!GetFileVersionInfoA(exe_path, 0, size, buf.data()))
        return "unknown";

    VS_FIXEDFILEINFO* ffi = nullptr;
    UINT ffi_len = 0;
    if (!VerQueryValueA(buf.data(), "\\",
                        reinterpret_cast<void**>(&ffi), &ffi_len) || !ffi)
        return "unknown";

    int major = HIWORD(ffi->dwFileVersionMS);
    int minor = LOWORD(ffi->dwFileVersionMS);
    char ver[32];
    std::snprintf(ver, sizeof(ver), "%d.%02d", major, minor);
    Logger::info("Detected game version (from PE): {}", ver);
    return ver;
}

const std::string& Version::build() {
    return s_build_number;
}

} // namespace x4n
