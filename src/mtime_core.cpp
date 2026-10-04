#include "mtime_core.h"

#include <algorithm>
#include <cwctype>
#include <sstream>
#include <utility>

namespace fmtfix {
namespace {

struct ChildInfo {
    std::wstring path;
    FILETIME discoveredTime{};
};

struct ProcessOutcome {
    bool hasEffectiveTime = false;
    FILETIME effectiveTime{};
};

ULARGE_INTEGER ToUInt64(const FILETIME& ft) {
    ULARGE_INTEGER value{};
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    return value;
}

bool FileTimeGreater(const FILETIME& a, const FILETIME& b) {
    const auto ua = ToUInt64(a);
    const auto ub = ToUInt64(b);
    return ua.QuadPart > ub.QuadPart;
}

bool FileTimeEqual(const FILETIME& a, const FILETIME& b) {
    const auto ua = ToUInt64(a);
    const auto ub = ToUInt64(b);
    return ua.QuadPart == ub.QuadPart;
}

std::wstring Win32ErrorMessage(DWORD code) {
    LPWSTR buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER |
                        FORMAT_MESSAGE_FROM_SYSTEM |
                        FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD len = FormatMessageW(
        flags,
        nullptr,
        code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr);

    std::wstring message;
    if (len != 0 && buffer != nullptr) {
        message.assign(buffer, len);
        LocalFree(buffer);
        while (!message.empty() &&
               (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' ' || message.back() == L'\t')) {
            message.pop_back();
        }
    } else {
        std::wstringstream ss;
        ss << L"Win32 error " << code;
        message = ss.str();
    }
    return message;
}

bool StartsWithDot(const std::wstring& name) {
    return !name.empty() && name.front() == L'.';
}

std::wstring TrimTrailingSlashes(std::wstring path) {
    // Preserve drive roots like C:\ and UNC share roots as much as possible.
    while (path.size() > 3 && (path.back() == L'\\' || path.back() == L'/')) {
        path.pop_back();
    }
    return path;
}

std::wstring ToExtendedPath(const std::wstring& input) {
    if (input.rfind(L"\\\\?\\", 0) == 0) {
        return input;
    }
    if (input.rfind(L"\\\\", 0) == 0) {
        return L"\\\\?\\UNC\\" + input.substr(2);
    }
    return L"\\\\?\\" + input;
}

std::wstring CombinePath(const std::wstring& parent, const std::wstring& child) {
    if (parent.empty()) {
        return child;
    }
    if (parent.back() == L'\\' || parent.back() == L'/') {
        return parent + child;
    }
    return parent + L"\\" + child;
}

bool GetPathLastWriteTime(const std::wstring& path, FILETIME& out, std::wstring& error) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(ToExtendedPath(path).c_str(), GetFileExInfoStandard, &data)) {
        error = Win32ErrorMessage(GetLastError());
        return false;
    }
    out = data.ftLastWriteTime;
    return true;
}

bool SetDirectoryLastWriteTime(const std::wstring& path, const FILETIME& target, std::wstring& error) {
    HANDLE handle = CreateFileW(
        ToExtendedPath(path).c_str(),
        FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr);

    if (handle == INVALID_HANDLE_VALUE) {
        error = Win32ErrorMessage(GetLastError());
        return false;
    }

    const BOOL ok = SetFileTime(handle, nullptr, nullptr, &target);
    const DWORD err = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(handle);

    if (!ok) {
        error = Win32ErrorMessage(err);
        return false;
    }
    return true;
}

bool CanSetDirectoryLastWriteTime(const std::wstring& path, std::wstring& error) {
    HANDLE handle = CreateFileW(
        ToExtendedPath(path).c_str(),
        FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr);

    if (handle == INVALID_HANDLE_VALUE) {
        error = Win32ErrorMessage(GetLastError());
        return false;
    }

    CloseHandle(handle);
    return true;
}

bool EnumerateDirectChildren(
    const std::wstring& directory,
    std::vector<ChildInfo>& files,
    std::vector<ChildInfo>& directories,
    std::wstring& error) {

    const std::wstring pattern = CombinePath(ToExtendedPath(directory), L"*");

    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileExW(
        pattern.c_str(),
        FindExInfoBasic,
        &data,
        FindExSearchNameMatch,
        nullptr,
        FIND_FIRST_EX_LARGE_FETCH);

    // Some filesystems do not support FIND_FIRST_EX_LARGE_FETCH. Retry with
    // ordinary enumeration instead of treating that as a hard failure.
    if (find == INVALID_HANDLE_VALUE && GetLastError() == ERROR_INVALID_PARAMETER) {
        find = FindFirstFileExW(
            pattern.c_str(),
            FindExInfoBasic,
            &data,
            FindExSearchNameMatch,
            nullptr,
            0);
    }

    if (find == INVALID_HANDLE_VALUE) {
        const DWORD code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND) {
            return true;
        }
        error = Win32ErrorMessage(code);
        return false;
    }

    do {
        const std::wstring name = data.cFileName;
        if (name == L"." || name == L"..") {
            continue;
        }

        // Match the original shell behavior: ignore dot-prefixed entries,
        // including .git and dotfiles. Windows HIDDEN alone is not excluded.
        if (StartsWithDot(name)) {
            continue;
        }

        // Do not traverse or use symlinks/junctions/other reparse points.
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            continue;
        }

        ChildInfo item;
        item.path = CombinePath(directory, name);
        item.discoveredTime = data.ftLastWriteTime;

        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            directories.push_back(std::move(item));
        } else {
            files.push_back(std::move(item));
        }
    } while (FindNextFileW(find, &data));

    const DWORD endCode = GetLastError();
    FindClose(find);

    if (endCode != ERROR_NO_MORE_FILES) {
        error = Win32ErrorMessage(endCode);
        return false;
    }

    return true;
}

class Processor {
public:
    Processor(
        std::wstring root,
        Mode mode,
        std::vector<std::wstring> excludedDirectories)
        : root_(std::move(root)),
          mode_(mode),
          excludedDirectories_(std::move(excludedDirectories)) {
        for (auto& path : excludedDirectories_) {
            path = TrimTrailingSlashes(std::move(path));
        }
        result_.root = root_;
        result_.mode = mode_;
    }

    Result Run() {
        ProcessDirectory(root_);
        return std::move(result_);
    }

private:
    bool IsExcluded(const std::wstring& directory) const {
        for (const auto& excluded : excludedDirectories_) {
            if (_wcsicmp(directory.c_str(), excluded.c_str()) == 0) {
                return true;
            }

            std::wstring prefix = excluded;
            if (!prefix.empty() && prefix.back() != L'\\') {
                prefix.push_back(L'\\');
            }

            if (directory.size() > prefix.size() &&
                _wcsnicmp(directory.c_str(), prefix.c_str(), prefix.size()) == 0) {
                return true;
            }
        }
        return false;
    }

    ProcessOutcome ProcessDirectory(const std::wstring& directory) {
        if (IsExcluded(directory)) {
            FILETIME current{};
            std::wstring error;
            ProcessOutcome outcome;
            if (GetPathLastWriteTime(directory, current, error)) {
                outcome.hasEffectiveTime = true;
                outcome.effectiveTime = current;
            }
            return outcome;
        }

        result_.summary.directories++;

        FILETIME before{};
        std::wstring getTimeError;
        const bool hasBefore = GetPathLastWriteTime(directory, before, getTimeError);

        std::vector<ChildInfo> files;
        std::vector<ChildInfo> childDirs;
        std::wstring enumError;

        if (!EnumerateDirectChildren(directory, files, childDirs, enumError)) {
            LogEntry entry;
            entry.directory = directory;
            entry.hasBefore = hasBefore;
            entry.before = before;
            entry.status = EntryStatus::Error;
            entry.error = L"无法读取目录内容：" + enumError;
            result_.entries.push_back(std::move(entry));
            result_.summary.errors++;

            ProcessOutcome outcome;
            if (hasBefore) {
                outcome.hasEffectiveTime = true;
                outcome.effectiveTime = before;
            }
            return outcome;
        }

        // Process children first. Their returned effective time is what the
        // parent sees when the parent has no direct files.
        struct ChildOutcome {
            std::wstring path;
            FILETIME effectiveTime{};
            bool valid = false;
        };
        std::vector<ChildOutcome> childOutcomes;
        childOutcomes.reserve(childDirs.size());

        for (const auto& child : childDirs) {
            const ProcessOutcome outcome = ProcessDirectory(child.path);
            ChildOutcome childOutcome;
            childOutcome.path = child.path;
            childOutcome.valid = outcome.hasEffectiveTime;
            if (outcome.hasEffectiveTime) {
                childOutcome.effectiveTime = outcome.effectiveTime;
            } else {
                // Best-effort fallback to the timestamp discovered during the
                // parent's enumeration.
                childOutcome.valid = true;
                childOutcome.effectiveTime = child.discoveredTime;
            }
            childOutcomes.push_back(std::move(childOutcome));
        }

        if (!hasBefore) {
            LogEntry entry;
            entry.directory = directory;
            entry.status = EntryStatus::Error;
            entry.error = L"无法读取目录修改时间：" + getTimeError;
            result_.entries.push_back(std::move(entry));
            result_.summary.errors++;
            return {};
        }

        bool hasTarget = false;
        FILETIME target{};
        std::wstring sourceType;
        std::wstring sourcePath;

        if (!files.empty()) {
            // If direct files exist, they win. Child-directory times are
            // intentionally ignored; this is the requested Windows-like rule.
            const ChildInfo* newest = &files.front();
            for (const auto& file : files) {
                if (FileTimeGreater(file.discoveredTime, newest->discoveredTime)) {
                    newest = &file;
                }
            }
            hasTarget = true;
            target = newest->discoveredTime;
            sourceType = L"file";
            sourcePath = newest->path;
        } else if (!childOutcomes.empty()) {
            const ChildOutcome* newest = nullptr;
            for (const auto& child : childOutcomes) {
                if (!child.valid) {
                    continue;
                }
                if (newest == nullptr || FileTimeGreater(child.effectiveTime, newest->effectiveTime)) {
                    newest = &child;
                }
            }
            if (newest != nullptr) {
                hasTarget = true;
                target = newest->effectiveTime;
                sourceType = L"directory";
                sourcePath = newest->path;
            }
        }

        LogEntry entry;
        entry.directory = directory;
        entry.before = before;
        entry.hasBefore = true;

        if (!hasTarget) {
            entry.status = EntryStatus::SkippedEmpty;
            result_.entries.push_back(std::move(entry));
            result_.summary.skipped++;

            ProcessOutcome outcome;
            outcome.hasEffectiveTime = true;
            outcome.effectiveTime = before;
            return outcome;
        }

        entry.sourceType = sourceType;
        entry.sourcePath = sourcePath;
        entry.target = target;
        entry.hasTarget = true;

        if (FileTimeEqual(before, target)) {
            entry.status = EntryStatus::Unchanged;
            entry.after = before;
            entry.hasAfter = true;
            result_.entries.push_back(std::move(entry));
            result_.summary.unchanged++;

            ProcessOutcome outcome;
            outcome.hasEffectiveTime = true;
            outcome.effectiveTime = before;
            return outcome;
        }

        std::wstring writeError;
        if (mode_ == Mode::DryRun) {
            // Dry-run also verifies that a FILE_WRITE_ATTRIBUTES handle can be
            // opened. This makes preview closer to a real Apply operation.
            if (!CanSetDirectoryLastWriteTime(directory, writeError)) {
                entry.status = EntryStatus::Error;
                entry.error = L"无法取得修改目录时间所需权限：" + writeError;
                result_.entries.push_back(std::move(entry));
                result_.summary.errors++;

                ProcessOutcome outcome;
                outcome.hasEffectiveTime = true;
                outcome.effectiveTime = before;
                return outcome;
            }

            entry.status = EntryStatus::WouldChange;
            result_.entries.push_back(std::move(entry));
            result_.summary.changedOrWouldChange++;

            ProcessOutcome outcome;
            outcome.hasEffectiveTime = true;
            outcome.effectiveTime = target;
            return outcome;
        }

        if (!SetDirectoryLastWriteTime(directory, target, writeError)) {
            entry.status = EntryStatus::Error;
            entry.error = L"设置目录修改时间失败：" + writeError;
            result_.entries.push_back(std::move(entry));
            result_.summary.errors++;

            // On failure, propagate the actual timestamp that remains.
            FILETIME actual{};
            std::wstring ignored;
            ProcessOutcome outcome;
            if (GetPathLastWriteTime(directory, actual, ignored)) {
                outcome.hasEffectiveTime = true;
                outcome.effectiveTime = actual;
            } else {
                outcome.hasEffectiveTime = true;
                outcome.effectiveTime = before;
            }
            return outcome;
        }

        FILETIME after{};
        std::wstring afterError;
        if (!GetPathLastWriteTime(directory, after, afterError)) {
            entry.status = EntryStatus::Error;
            entry.error = L"修改成功，但重新读取目录时间失败：" + afterError;
            result_.entries.push_back(std::move(entry));
            result_.summary.errors++;

            ProcessOutcome outcome;
            outcome.hasEffectiveTime = true;
            outcome.effectiveTime = target;
            return outcome;
        }

        entry.after = after;
        entry.hasAfter = true;
        entry.status = EntryStatus::Changed;
        result_.entries.push_back(std::move(entry));
        result_.summary.changedOrWouldChange++;

        ProcessOutcome outcome;
        outcome.hasEffectiveTime = true;
        outcome.effectiveTime = after;
        return outcome;
    }

    std::wstring root_;
    Mode mode_;
    std::vector<std::wstring> excludedDirectories_;
    Result result_;
};

std::wstring NormalizeForCompare(std::wstring path) {
    path = TrimTrailingSlashes(std::move(path));
    std::transform(path.begin(), path.end(), path.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towlower(c));
    });
    return path;
}

} // namespace

Result ProcessTree(
    const std::wstring& root,
    Mode mode,
    const std::vector<std::wstring>& excludedDirectories) {
    Processor processor(
        TrimTrailingSlashes(root),
        mode,
        excludedDirectories);
    return processor.Run();
}

std::wstring FormatFileTimeLocal(const FILETIME& ft) {
    FILETIME local{};
    SYSTEMTIME st{};
    if (!FileTimeToLocalFileTime(&ft, &local) || !FileTimeToSystemTime(&local, &st)) {
        return L"(invalid time)";
    }

    wchar_t buffer[64]{};
    swprintf_s(
        buffer,
        L"%04u-%02u-%02u %02u:%02u:%02u.%03u",
        st.wYear,
        st.wMonth,
        st.wDay,
        st.wHour,
        st.wMinute,
        st.wSecond,
        st.wMilliseconds);
    return buffer;
}

std::wstring StatusText(EntryStatus status) {
    switch (status) {
    case EntryStatus::WouldChange: return L"would-change";
    case EntryStatus::Changed: return L"changed";
    case EntryStatus::Unchanged: return L"unchanged";
    case EntryStatus::SkippedEmpty: return L"skipped-empty";
    case EntryStatus::Error: return L"error";
    }
    return L"unknown";
}

std::wstring FormatLog(const Result& result) {
    std::wstringstream out;
    out << L"# FolderMTimeFix\r\n";
    out << L"# mode=" << (result.mode == Mode::DryRun ? L"dry-run" : L"apply") << L"\r\n";
    out << L"# root=\"" << result.root << L"\"\r\n\r\n";

    for (const auto& entry : result.entries) {
        out << L"[";
        if (entry.status == EntryStatus::WouldChange) {
            out << L"DRY-RUN";
        } else if (result.mode == Mode::Apply) {
            out << L"APPLY";
        } else {
            out << L"DRY-RUN";
        }
        out << L"] dir=\"" << entry.directory << L"\"";

        if (!entry.sourceType.empty()) {
            out << L" | source_type=" << entry.sourceType;
        }
        if (!entry.sourcePath.empty()) {
            out << L" | source=\"" << entry.sourcePath << L"\"";
        }
        if (entry.hasBefore) {
            out << L" | before=" << FormatFileTimeLocal(entry.before);
        }
        if (entry.hasTarget) {
            out << L" | target=" << FormatFileTimeLocal(entry.target);
        }
        if (entry.hasAfter) {
            out << L" | after=" << FormatFileTimeLocal(entry.after);
        }
        out << L" | result=" << StatusText(entry.status);
        if (!entry.error.empty()) {
            out << L" | error=\"" << entry.error << L"\"";
        }
        out << L"\r\n";
    }

    out << L"\r\n# summary"
        << L" | directories=" << result.summary.directories
        << L" | changed_or_would_change=" << result.summary.changedOrWouldChange
        << L" | unchanged=" << result.summary.unchanged
        << L" | skipped=" << result.summary.skipped
        << L" | errors=" << result.summary.errors
        << L"\r\n";

    return out.str();
}

bool IsPathInside(const std::wstring& candidate, const std::wstring& root) {
    std::wstring c = NormalizeForCompare(candidate);
    std::wstring r = NormalizeForCompare(root);

    if (c == r) {
        return true;
    }
    if (!r.empty() && r.back() != L'\\') {
        r.push_back(L'\\');
    }
    return c.rfind(r, 0) == 0;
}

} // namespace fmtfix
