#pragma once
// ---------------------------------------------------------------------------
// Game Version Detection
//
// Reads X4.exe's file version info (VS_FIXEDFILEINFO) at startup.
// This is used for:
//   - Offset database selection (Phase 4)
//   - Logging / diagnostics
//   - Extension compatibility checks
// ---------------------------------------------------------------------------

#include "Common.h"
#include <string>

namespace x4n {

class Version
{
  public:
    Version() = delete;

    /// Detect X4.exe version from file version resource.
    /// Returns a string like "9.00" or "unknown" on failure.
    static std::string detect();

    /// Raw build number from version.dat (e.g. "900"). Empty if unknown.
    /// Only valid after detect() has been called.
    static std::string const &build();
};

} // namespace x4n
