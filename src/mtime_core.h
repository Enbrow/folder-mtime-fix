#pragma once

#include <windows.h>
#include <string>
#include <vector>

namespace fmtfix {

enum class Mode {
    DryRun,
    Apply
};

enum class EntryStatus {
    WouldChange,
    Changed,
    Unchanged,
    SkippedEmpty,
    Error
};

struct LogEntry {
    std::wstring directory;
    std::wstring sourceType;
    std::wstring sourcePath;
    FILETIME before{};
    FILETIME target{};
    FILETIME after{};
    bool hasBefore = false;
    bool hasTarget = false;
    bool hasAfter = false;
    EntryStatus status = EntryStatus::Error;
    std::wstring error;
};

struct Summary {
    size_t directories = 0;
    size_t changedOrWouldChange = 0;
    size_t unchanged = 0;
    size_t skipped = 0;
    size_t errors = 0;
};

struct Result {
    std::wstring root;
    Mode mode = Mode::DryRun;
    bool ignoreDirectSubdirectoryTimes = true;
    std::vector<LogEntry> entries;
    Summary summary;
};

// Restores directory LastWriteTime using the rule agreed with the user:
//   - Process child directories before parents.
//   - If direct files exist and ignoreDirectSubdirectoryTimes is true
//     (default), use only the newest direct file and ignore direct child
//     directory times.
//   - If there are no direct files, use the newest direct child's effective
//     post-processing time; this preserves propagation through fileless levels.
//   - If ignoreDirectSubdirectoryTimes is false, compare direct files and
//     direct child directories together and use whichever is newest.
//   - Empty directories are unchanged.
//   - Dot-prefixed entries (e.g. .git) and reparse points are ignored.
//   - Excluded directories are not processed recursively; their current
//     directory timestamp can still be used as a direct parent's reference.
Result ProcessTree(
    const std::wstring& root,
    Mode mode,
    const std::vector<std::wstring>& excludedDirectories = {},
    bool ignoreDirectSubdirectoryTimes = true);

std::wstring FormatFileTimeLocal(const FILETIME& ft);
std::wstring FormatLog(const Result& result);
std::wstring StatusText(EntryStatus status);

bool IsPathInside(const std::wstring& candidate, const std::wstring& root);

} // namespace fmtfix
