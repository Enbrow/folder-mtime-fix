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
    std::vector<LogEntry> entries;
    Summary summary;
};

// Restores directory LastWriteTime using the rule agreed with the user:
//   - Process child directories before parents.
//   - If a directory has one or more direct, non-dot, non-reparse files,
//     use the newest of those files and ignore child-directory times.
//   - Otherwise, if it has direct child directories, use the newest child's
//     effective post-processing time. This can propagate through a chain of
//     fileless directories, but stops at any directory that has direct files.
//   - Empty directories are unchanged.
//   - Dot-prefixed entries (e.g. .git) and reparse points are ignored.
Result ProcessTree(const std::wstring& root, Mode mode);

std::wstring FormatFileTimeLocal(const FILETIME& ft);
std::wstring FormatLog(const Result& result);
std::wstring StatusText(EntryStatus status);

bool IsPathInside(const std::wstring& candidate, const std::wstring& root);

} // namespace fmtfix
