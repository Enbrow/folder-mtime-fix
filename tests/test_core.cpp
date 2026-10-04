#define UNICODE
#define _UNICODE
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <windows.h>

#include <cstdlib>
#include <cwchar>
#include <iostream>
#include <string>
#include <vector>

#include "mtime_core.h"

namespace {

std::wstring Join(const std::wstring& a, const std::wstring& b) {
    if (!a.empty() && (a.back() == L'\\' || a.back() == L'/')) {
        return a + b;
    }
    return a + L"\\" + b;
}

bool MakeDir(const std::wstring& path) {
    return CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

bool MakeFile(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    const char data[] = "x";
    DWORD written = 0;
    const BOOL ok = WriteFile(h, data, 1, &written, nullptr);
    CloseHandle(h);
    return ok != FALSE;
}

FILETIME TimeUtc(WORD year, WORD month, WORD day, WORD hour = 12) {
    SYSTEMTIME st{};
    st.wYear = year;
    st.wMonth = month;
    st.wDay = day;
    st.wHour = hour;
    FILETIME ft{};
    if (!SystemTimeToFileTime(&st, &ft)) {
        std::abort();
    }
    return ft;
}

bool EqualTime(const FILETIME& a, const FILETIME& b) {
    ULARGE_INTEGER ua{}, ub{};
    ua.LowPart = a.dwLowDateTime;
    ua.HighPart = a.dwHighDateTime;
    ub.LowPart = b.dwLowDateTime;
    ub.HighPart = b.dwHighDateTime;
    return ua.QuadPart == ub.QuadPart;
}

bool SetPathTime(const std::wstring& path, const FILETIME& ft, bool directory) {
    HANDLE h = CreateFileW(
        path.c_str(),
        FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        directory ? FILE_FLAG_BACKUP_SEMANTICS : FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    const BOOL ok = SetFileTime(h, nullptr, nullptr, &ft);
    CloseHandle(h);
    return ok != FALSE;
}

bool GetPathTime(const std::wstring& path, FILETIME& ft) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        return false;
    }
    ft = data.ftLastWriteTime;
    return true;
}

const fmtfix::LogEntry* FindEntry(const fmtfix::Result& result, const std::wstring& dir) {
    for (const auto& e : result.entries) {
        if (_wcsicmp(e.directory.c_str(), dir.c_str()) == 0) {
            return &e;
        }
    }
    return nullptr;
}

bool RemoveTree(const std::wstring& path) {
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(Join(path, L"*").c_str(), &data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            std::wstring name = data.cFileName;
            if (name == L"." || name == L"..") continue;
            std::wstring child = Join(path, name);
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                RemoveTree(child);
            } else {
                SetFileAttributesW(child.c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(child.c_str());
            }
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
    return RemoveDirectoryW(path.c_str()) != FALSE;
}

struct TempDir {
    std::wstring path;
    ~TempDir() {
        if (!path.empty()) {
            RemoveTree(path);
        }
    }
};

TempDir MakeTempRoot() {
    wchar_t temp[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, temp)) {
        std::abort();
    }
    wchar_t name[MAX_PATH]{};
    if (!GetTempFileNameW(temp, L"FMT", 0, name)) {
        std::abort();
    }
    DeleteFileW(name);
    if (!CreateDirectoryW(name, nullptr)) {
        std::abort();
    }
    return TempDir{ name };
}

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        return false;
    }
    return true;
}

} // namespace

int wmain() {
    bool ok = true;
    TempDir temp = MakeTempRoot();

    // Scenario 1:
    // A has a direct old file, while child directory B resolves to a newer
    // time. The default checked option must preserve the original rule: direct
    // files win and direct child-directory times are ignored.
    const std::wstring A = Join(temp.path, L"A");
    const std::wstring B = Join(A, L"B");
    const std::wstring oldFile = Join(A, L"old.txt");
    const std::wstring newFile = Join(B, L"new.txt");
    MakeDir(A); MakeDir(B); MakeFile(oldFile); MakeFile(newFile);

    const FILETIME t2024 = TimeUtc(2024, 1, 1);
    const FILETIME t2025 = TimeUtc(2025, 6, 1);
    const FILETIME t2026 = TimeUtc(2026, 10, 1);
    SetPathTime(oldFile, t2024, false);
    SetPathTime(newFile, t2025, false);
    SetPathTime(B, t2026, true);
    SetPathTime(A, t2026, true);

    const auto preview1 = fmtfix::ProcessTree(A, fmtfix::Mode::DryRun);
    const auto* eA = FindEntry(preview1, A);
    const auto* eB = FindEntry(preview1, B);
    ok &= Expect(
        preview1.ignoreDirectSubdirectoryTimes,
        "Direct child-directory times must be ignored by default");
    ok &= Expect(
        eA && eA->hasTarget && EqualTime(eA->target, t2024) &&
            _wcsicmp(eA->sourcePath.c_str(), oldFile.c_str()) == 0,
        "Default A target must come from its direct file (2024)");
    ok &= Expect(
        eB && eB->hasTarget && EqualTime(eB->target, t2025),
        "B preview target must be direct file time (2025)");

    FILETIME beforeA{}, beforeB{};
    GetPathTime(A, beforeA); GetPathTime(B, beforeB);
    ok &= Expect(
        EqualTime(beforeA, t2026) && EqualTime(beforeB, t2026),
        "Dry-run must not modify directory times");

    const auto apply1 = fmtfix::ProcessTree(A, fmtfix::Mode::Apply);
    FILETIME afterA{}, afterB{};
    GetPathTime(A, afterA); GetPathTime(B, afterB);
    ok &= Expect(EqualTime(afterA, t2024), "Default A apply result must be 2024");
    ok &= Expect(EqualTime(afterB, t2025), "Default B apply result must be 2025");
    ok &= Expect(apply1.summary.errors == 0, "Scenario 1 should have no errors");

    // Unchecking the option makes both direct files and direct child
    // directories candidates. B (2025) is newer than old.txt (2024), so A
    // must now reference B.
    const auto preview1WithChildDirs =
        fmtfix::ProcessTree(A, fmtfix::Mode::DryRun, {}, false);
    const auto* eAWithChildDirs = FindEntry(preview1WithChildDirs, A);
    ok &= Expect(
        !preview1WithChildDirs.ignoreDirectSubdirectoryTimes,
        "Combined file/directory mode must be recorded in the result");
    ok &= Expect(
        eAWithChildDirs && eAWithChildDirs->hasTarget &&
            EqualTime(eAWithChildDirs->target, t2025) &&
            eAWithChildDirs->sourceType == L"directory" &&
            _wcsicmp(eAWithChildDirs->sourcePath.c_str(), B.c_str()) == 0,
        "Unchecked mode must let newer child B beat direct old.txt");

    fmtfix::ProcessTree(A, fmtfix::Mode::Apply, {}, false);
    GetPathTime(A, afterA); GetPathTime(B, afterB);
    ok &= Expect(
        EqualTime(afterA, t2025) && EqualTime(afterB, t2025),
        "Unchecked mode must update A from the newer direct child directory");

    // Scenario 2:
    // C has no direct files and D contains deep.txt. The original/default rule
    // still propagates through fileless directory levels; the checkbox only
    // changes what happens when direct files and child directories coexist.
    const std::wstring C = Join(temp.path, L"C");
    const std::wstring D = Join(C, L"D");
    const std::wstring deep = Join(D, L"deep.txt");
    MakeDir(C); MakeDir(D); MakeFile(deep);
    const FILETIME t2023 = TimeUtc(2023, 2, 2);
    SetPathTime(deep, t2025, false);
    SetPathTime(D, t2023, true);
    SetPathTime(C, t2023, true);

    const auto preview2 = fmtfix::ProcessTree(C, fmtfix::Mode::DryRun);
    const auto* eC = FindEntry(preview2, C);
    const auto* eD = FindEntry(preview2, D);
    ok &= Expect(
        eD && eD->hasTarget && EqualTime(eD->target, t2025),
        "D preview target must be deep file time");
    ok &= Expect(
        eC && eC->hasTarget && EqualTime(eC->target, t2025) &&
            _wcsicmp(eC->sourcePath.c_str(), D.c_str()) == 0,
        "Default C preview must inherit D because C has no direct files");

    fmtfix::ProcessTree(C, fmtfix::Mode::Apply);
    FILETIME afterC{}, afterD{};
    GetPathTime(C, afterC); GetPathTime(D, afterD);
    ok &= Expect(
        EqualTime(afterD, t2025) && EqualTime(afterC, t2025),
        "Default mode must propagate through fileless levels");

    // Unchecked mode behaves the same here because C has no direct files.
    SetPathTime(D, t2023, true);
    SetPathTime(C, t2023, true);
    const auto preview2WithChildDirs =
        fmtfix::ProcessTree(C, fmtfix::Mode::DryRun, {}, false);
    const auto* eCWithChildDirs = FindEntry(preview2WithChildDirs, C);
    ok &= Expect(
        eCWithChildDirs && eCWithChildDirs->hasTarget &&
            EqualTime(eCWithChildDirs->target, t2025),
        "Unchecked mode must also propagate through fileless levels");

    // Scenario 3: an empty directory stays unchanged.
    const std::wstring Empty = Join(temp.path, L"Empty");
    MakeDir(Empty);
    SetPathTime(Empty, t2023, true);
    const auto emptyResult = fmtfix::ProcessTree(Empty, fmtfix::Mode::Apply);
    FILETIME emptyAfter{};
    GetPathTime(Empty, emptyAfter);
    ok &= Expect(EqualTime(emptyAfter, t2023), "Empty directory must remain unchanged");
    ok &= Expect(emptyResult.summary.skipped == 1, "Empty directory must be counted as skipped");

    // Scenario 4: dot-prefixed files are ignored.
    const std::wstring Dot = Join(temp.path, L"Dot");
    const std::wstring normal = Join(Dot, L"normal.txt");
    const std::wstring dotFile = Join(Dot, L".newer.txt");
    MakeDir(Dot); MakeFile(normal); MakeFile(dotFile);
    SetPathTime(normal, t2024, false);
    SetPathTime(dotFile, t2026, false);
    SetPathTime(Dot, t2023, true);
    fmtfix::ProcessTree(Dot, fmtfix::Mode::Apply);
    FILETIME dotAfter{};
    GetPathTime(Dot, dotAfter);
    ok &= Expect(EqualTime(dotAfter, t2024), "Dot-prefixed file must be ignored");

    // Scenario 5: excluded directory and its subtree are not modified.
    // Because E has no direct files, both checkbox modes may use excluded F's
    // current directory time as E's direct-child reference.
    const std::wstring E = Join(temp.path, L"E");
    const std::wstring F = Join(E, L"F");
    const std::wstring G = Join(F, L"G");
    const std::wstring excludedFile = Join(F, L"excluded.txt");
    const std::wstring nestedExcludedFile = Join(G, L"nested.txt");
    MakeDir(E); MakeDir(F); MakeDir(G);
    MakeFile(excludedFile); MakeFile(nestedExcludedFile);
    SetPathTime(excludedFile, t2025, false);
    SetPathTime(nestedExcludedFile, t2026, false);
    SetPathTime(G, t2024, true);
    SetPathTime(F, t2023, true);
    SetPathTime(E, t2026, true);

    const auto excludedPreview =
        fmtfix::ProcessTree(E, fmtfix::Mode::DryRun, {F});
    FILETIME previewE{}, previewF{}, previewG{};
    GetPathTime(E, previewE); GetPathTime(F, previewF); GetPathTime(G, previewG);
    const auto* previewExcludedParent = FindEntry(excludedPreview, E);
    ok &= Expect(
        EqualTime(previewE, t2026) && EqualTime(previewF, t2023) &&
            EqualTime(previewG, t2024),
        "Dry-run with exclusion must not modify parent or excluded subtree");
    ok &= Expect(
        previewExcludedParent && previewExcludedParent->hasTarget &&
            EqualTime(previewExcludedParent->target, t2023),
        "Default mode must use excluded F because E has no direct files");
    ok &= Expect(
        FindEntry(excludedPreview, F) == nullptr &&
            FindEntry(excludedPreview, G) == nullptr,
        "Excluded directory subtree must not be logged during dry-run");

    const auto excludedResult =
        fmtfix::ProcessTree(E, fmtfix::Mode::Apply, {F});
    FILETIME afterE{}, afterF{}, afterG{};
    GetPathTime(E, afterE); GetPathTime(F, afterF); GetPathTime(G, afterG);
    ok &= Expect(
        EqualTime(afterE, t2023) &&
            EqualTime(afterF, t2023) && EqualTime(afterG, t2024),
        "Default mode may update E but must preserve excluded subtree times");
    ok &= Expect(
        FindEntry(excludedResult, F) == nullptr &&
            FindEntry(excludedResult, G) == nullptr,
        "Excluded directory subtree must not be logged as processed");

    SetPathTime(E, t2026, true);
    const auto excludedCombinedPreview =
        fmtfix::ProcessTree(E, fmtfix::Mode::DryRun, {F}, false);
    const auto* combinedExcludedParent =
        FindEntry(excludedCombinedPreview, E);
    ok &= Expect(
        combinedExcludedParent &&
            combinedExcludedParent->hasTarget &&
            EqualTime(combinedExcludedParent->target, t2023),
        "Unchecked mode must behave the same when the parent has no files");

    if (!ok) {
        return 1;
    }

    std::cout << "All FolderMTimeFix integration tests passed.\n";
    return 0;
}
