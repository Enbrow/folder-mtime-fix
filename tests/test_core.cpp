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
    // A has a direct old file, B has a newer file.
    // B should become 2025; A must stay governed by its direct 2024 file.
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
    ok &= Expect(eA && eA->hasTarget && EqualTime(eA->target, t2024), "A preview target must be direct file time (2024)");
    ok &= Expect(eB && eB->hasTarget && EqualTime(eB->target, t2025), "B preview target must be direct file time (2025)");

    FILETIME beforeA{}, beforeB{};
    GetPathTime(A, beforeA); GetPathTime(B, beforeB);
    ok &= Expect(EqualTime(beforeA, t2026) && EqualTime(beforeB, t2026), "Dry-run must not modify directory times");

    const auto apply1 = fmtfix::ProcessTree(A, fmtfix::Mode::Apply);
    FILETIME afterA{}, afterB{};
    GetPathTime(A, afterA); GetPathTime(B, afterB);
    ok &= Expect(EqualTime(afterA, t2024), "A apply result must be 2024");
    ok &= Expect(EqualTime(afterB, t2025), "B apply result must be 2025");
    ok &= Expect(apply1.summary.errors == 0, "Scenario 1 should have no errors");

    // Scenario 2:
    // C and D have no direct files; D contains deep.txt.
    // The time is allowed to propagate through a chain of fileless directories.
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
    ok &= Expect(eD && eD->hasTarget && EqualTime(eD->target, t2025), "D preview target must be deep file time");
    ok &= Expect(eC && eC->hasTarget && EqualTime(eC->target, t2025), "C preview must simulate D post-processing time");

    fmtfix::ProcessTree(C, fmtfix::Mode::Apply);
    FILETIME afterC{}, afterD{};
    GetPathTime(C, afterC); GetPathTime(D, afterD);
    ok &= Expect(EqualTime(afterD, t2025), "D apply result must be 2025");
    ok &= Expect(EqualTime(afterC, t2025), "C apply result must be 2025");

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
    // Its current directory time may still act as the parent's reference time.
    const std::wstring E = Join(temp.path, L"E");
    const std::wstring F = Join(E, L"F");
    const std::wstring excludedFile = Join(F, L"excluded.txt");
    MakeDir(E); MakeDir(F); MakeFile(excludedFile);
    SetPathTime(excludedFile, t2025, false);
    SetPathTime(F, t2023, true);
    SetPathTime(E, t2026, true);

    const auto excludedResult =
        fmtfix::ProcessTree(E, fmtfix::Mode::Apply, {F});
    FILETIME afterE{}, afterF{};
    GetPathTime(E, afterE); GetPathTime(F, afterF);
    ok &= Expect(
        EqualTime(afterF, t2023),
        "Excluded directory must keep its original time");
    ok &= Expect(
        EqualTime(afterE, t2023),
        "Parent may use excluded child's current time as reference");
    ok &= Expect(
        FindEntry(excludedResult, F) == nullptr,
        "Excluded directory must not be logged as processed");

    if (!ok) {
        return 1;
    }

    std::cout << "All FolderMTimeFix integration tests passed.\n";
    return 0;
}
