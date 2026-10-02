#define UNICODE
#define _UNICODE
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shobjidl.h>

#include <algorithm>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "mtime_core.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "uuid.lib")

namespace {

constexpr wchar_t kWindowClass[] = L"FolderMTimeFixWindow";
constexpr wchar_t kAppTitle[] = L"FolderMTimeFix - 文件夹时间修复";

constexpr int IDC_PATH = 1001;
constexpr int IDC_BROWSE = 1002;
constexpr int IDC_DRYRUN = 1003;
constexpr int IDC_APPLY = 1004;
constexpr int IDC_COPY = 1005;
constexpr int IDC_SAVE = 1006;
constexpr int IDC_LOG = 1007;
constexpr int IDC_STATUS = 1008;
constexpr int IDC_RULE = 1009;
constexpr int IDC_CURRENT_LABEL = 1010;
constexpr int IDC_CURRENT_LIST = 1011;
constexpr int IDC_TARGET_LABEL = 1012;
constexpr int IDC_TARGET_LIST = 1013;
constexpr int IDC_LOG_LABEL = 1014;

HWND gPath = nullptr;
HWND gBrowse = nullptr;
HWND gDryRun = nullptr;
HWND gApply = nullptr;
HWND gCopy = nullptr;
HWND gSave = nullptr;
HWND gLog = nullptr;
HWND gStatus = nullptr;
HWND gRule = nullptr;
HWND gCurrentLabel = nullptr;
HWND gCurrentList = nullptr;
HWND gTargetLabel = nullptr;
HWND gTargetList = nullptr;
HWND gLogLabel = nullptr;
HFONT gFont = nullptr;
HFONT gMonoFont = nullptr;
std::wstring gLastLog;
std::wstring gLastRoot;

std::wstring GetWindowTextString(HWND hwnd) {
    const int len = GetWindowTextLengthW(hwnd);
    if (len <= 0) {
        return {};
    }
    std::wstring value(static_cast<size_t>(len + 1), L'\0');
    const int copied = GetWindowTextW(hwnd, value.data(), len + 1);
    value.resize(copied > 0 ? static_cast<size_t>(copied) : 0);
    return value;
}

void SetStatus(const std::wstring& text) {
    SetWindowTextW(gStatus, text.c_str());
    UpdateWindow(gStatus);
}

void SetControlsEnabled(bool enabled) {
    EnableWindow(gPath, enabled);
    EnableWindow(gBrowse, enabled);
    EnableWindow(gDryRun, enabled);
    EnableWindow(gApply, enabled);
    EnableWindow(gCopy, enabled && !gLastLog.empty());
    EnableWindow(gSave, enabled && !gLastLog.empty());
}

int ScaleForDpi(HWND hwnd, int value) {
    UINT dpi = GetDpiForWindow(hwnd);
    if (dpi == 0) {
        dpi = 96;
    }
    return MulDiv(value, static_cast<int>(dpi), 96);
}

HFONT CreateAppFont(HWND hwnd, const wchar_t* face, int pointSize, int weight = FW_NORMAL) {
    UINT dpi = GetDpiForWindow(hwnd);
    if (dpi == 0) {
        dpi = 96;
    }
    return CreateFontW(
        -MulDiv(pointSize, static_cast<int>(dpi), 72),
        0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
}

std::wstring ToExtendedPathForApi(const std::wstring& input) {
    if (input.rfind(L"\\\\?\\", 0) == 0) {
        return input;
    }
    if (input.rfind(L"\\\\", 0) == 0) {
        return L"\\\\?\\UNC\\" + input.substr(2);
    }
    return L"\\\\?\\" + input;
}

std::wstring NormalizeAbsolutePath(const std::wstring& input, std::wstring& error) {
    if (input.empty()) {
        error = L"请选择一个文件夹。";
        return {};
    }

    DWORD needed = GetFullPathNameW(input.c_str(), 0, nullptr, nullptr);
    if (needed == 0) {
        error = L"无法解析路径。";
        return {};
    }

    std::wstring full(static_cast<size_t>(needed), L'\0');
    DWORD written = GetFullPathNameW(input.c_str(), needed, full.data(), nullptr);
    if (written == 0 || written >= needed) {
        error = L"无法解析路径。";
        return {};
    }
    full.resize(written);

    DWORD attrs = GetFileAttributesW(ToExtendedPathForApi(full).c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        error = L"文件夹不存在或无法访问。";
        return {};
    }
    if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        error = L"所选路径不是文件夹。";
        return {};
    }
    if ((attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        error = L"为避免误操作，不允许把符号链接或 Junction 作为根目录。";
        return {};
    }

    while (full.size() > 3 && (full.back() == L'\\' || full.back() == L'/')) {
        full.pop_back();
    }
    return full;
}

bool BrowseForFolder(HWND owner, std::wstring& path) {
    IFileDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_FileOpenDialog,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&dialog));

    if (FAILED(hr) || dialog == nullptr) {
        return false;
    }

    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(L"选择要修复修改时间的根文件夹");

    hr = dialog->Show(owner);
    if (SUCCEEDED(hr)) {
        IShellItem* item = nullptr;
        hr = dialog->GetResult(&item);
        if (SUCCEEDED(hr) && item != nullptr) {
            PWSTR raw = nullptr;
            hr = item->GetDisplayName(SIGDN_FILESYSPATH, &raw);
            if (SUCCEEDED(hr) && raw != nullptr) {
                path = raw;
                CoTaskMemFree(raw);
            }
            item->Release();
        }
    }

    dialog->Release();
    return !path.empty();
}

std::wstring CurrentTimestamp() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"%04u%02u%02u-%02u%02u%02u",
               st.wYear, st.wMonth, st.wDay,
               st.wHour, st.wMinute, st.wSecond);
    return buffer;
}

bool WriteUtf8File(const std::wstring& path, const std::wstring& text, std::wstring& error) {
    if (text.empty()) {
        error = L"没有可保存的日志。";
        return false;
    }

    int bytes = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) {
        error = L"日志编码转换失败。";
        return false;
    }

    std::string utf8(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), utf8.data(), bytes, nullptr, nullptr);

    HANDLE file = CreateFileW(
        ToExtendedPathForApi(path).c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (file == INVALID_HANDLE_VALUE) {
        error = L"无法创建日志文件。";
        return false;
    }

    // UTF-8 BOM makes Notepad detection reliable on older systems too.
    const BYTE bom[] = {0xEF, 0xBB, 0xBF};
    DWORD written = 0;
    BOOL ok = WriteFile(file, bom, sizeof(bom), &written, nullptr);
    if (ok) {
        ok = WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    }
    CloseHandle(file);

    if (!ok) {
        error = L"写入日志文件失败。";
        return false;
    }
    return true;
}

std::wstring MakeTempLogPath(const std::wstring& root) {
    wchar_t tempBuffer[MAX_PATH + 1]{};
    const DWORD written = GetTempPathW(MAX_PATH + 1, tempBuffer);
    if (written == 0 || written > MAX_PATH) {
        return {};
    }

    std::wstring temp(tempBuffer, written);
    std::wstring path = temp + L"FolderMTimeFix-" + CurrentTimestamp() + L".log";
    if (fmtfix::IsPathInside(path, root)) {
        return {};
    }
    return path;
}

std::wstring AutoSaveTempLog(const std::wstring& root) {
    const std::wstring path = MakeTempLogPath(root);
    if (path.empty()) {
        return L"未自动写入磁盘日志（TEMP 位于目标目录树内或不可用）。";
    }

    std::wstring error;
    if (WriteUtf8File(path, gLastLog, error)) {
        return L"日志：" + path;
    }
    return L"自动保存日志失败：" + error;
}

void CopyLogToClipboard(HWND owner) {
    if (gLastLog.empty()) {
        return;
    }

    if (!OpenClipboard(owner)) {
        MessageBoxW(owner, L"无法打开剪贴板。", kAppTitle, MB_ICONERROR);
        return;
    }
    EmptyClipboard();

    const SIZE_T bytes = (gLastLog.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory != nullptr) {
        void* ptr = GlobalLock(memory);
        if (ptr != nullptr) {
            memcpy(ptr, gLastLog.c_str(), bytes);
            GlobalUnlock(memory);
            if (SetClipboardData(CF_UNICODETEXT, memory) != nullptr) {
                memory = nullptr; // clipboard owns it now
            }
        }
    }

    if (memory != nullptr) {
        GlobalFree(memory);
    }
    CloseClipboard();
    SetStatus(L"日志已复制到剪贴板。");
}

void SaveLogAs(HWND owner) {
    if (gLastLog.empty()) {
        return;
    }

    wchar_t fileName[MAX_PATH] = L"FolderMTimeFix.log";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"日志文件 (*.log)\0*.log\0文本文件 (*.txt)\0*.txt\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"log";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

    if (!GetSaveFileNameW(&ofn)) {
        return;
    }

    const std::wstring destination = fileName;
    if (!gLastRoot.empty() && fmtfix::IsPathInside(destination, gLastRoot)) {
        MessageBoxW(
            owner,
            L"日志不能保存在刚刚处理的目录树里面。\n\n"
            L"保存日志本身会再次改变该目录的修改时间，请选择目标目录之外的位置。",
            kAppTitle,
            MB_ICONWARNING);
        return;
    }

    std::wstring error;
    if (!WriteUtf8File(destination, gLastLog, error)) {
        MessageBoxW(owner, error.c_str(), kAppTitle, MB_ICONERROR);
        return;
    }
    SetStatus(L"日志已保存到：" + destination);
}

std::wstring TimeText(bool hasTime, const FILETIME& time) {
    if (!hasTime) {
        return L"—";
    }
    std::wstring text = fmtfix::FormatFileTimeLocal(time);
    // The tables favor readability; the detailed log keeps full precision.
    if (text.size() > 19 && text[4] == L'-' && text[10] == L' ') {
        text.resize(19);
    }
    return text;
}

void AddListColumn(HWND list, int index, const wchar_t* title) {
    LVCOLUMNW column{};
    column.mask = LVCF_TEXT | LVCF_SUBITEM;
    column.iSubItem = index;
    column.pszText = const_cast<LPWSTR>(title);
    ListView_InsertColumn(list, index, &column);
}

void AddListRow(HWND list,
                const std::wstring& first,
                const std::wstring& second,
                const std::wstring& third) {
    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.iItem = ListView_GetItemCount(list);
    item.iSubItem = 0;
    item.pszText = const_cast<LPWSTR>(first.c_str());
    const int row = ListView_InsertItem(list, &item);
    if (row >= 0) {
        ListView_SetItemText(list, row, 1, const_cast<LPWSTR>(second.c_str()));
        ListView_SetItemText(list, row, 2, const_cast<LPWSTR>(third.c_str()));
    }
}

void ClearComparisonViews() {
    if (gCurrentList != nullptr) {
        ListView_DeleteAllItems(gCurrentList);
    }
    if (gTargetList != nullptr) {
        ListView_DeleteAllItems(gTargetList);
    }
    SetWindowTextW(gCurrentLabel, L"当前状态 / 参考来源");
    SetWindowTextW(gTargetLabel, L"变更预览");
}

void PopulateComparisonViews(const fmtfix::Result& result) {
    SendMessageW(gCurrentList, WM_SETREDRAW, FALSE, 0);
    SendMessageW(gTargetList, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(gCurrentList);
    ListView_DeleteAllItems(gTargetList);

    size_t sourceRows = 0;
    size_t changeRows = 0;

    for (const auto& entry : result.entries) {
        AddListRow(
            gCurrentList,
            L"文件夹",
            entry.directory,
            TimeText(entry.hasBefore, entry.before));

        if (!entry.sourcePath.empty() && entry.hasTarget) {
            const bool sourceIsFile = entry.sourceType == L"file";
            const std::wstring type =
                sourceIsFile ? L"↳ 参考文件" : L"↳ 参考目录";

            FILETIME sourceOriginal = entry.target;
            bool hasSourceOriginal = true;
            if (!sourceIsFile) {
                hasSourceOriginal = false;
                for (const auto& candidate : result.entries) {
                    if (candidate.directory == entry.sourcePath && candidate.hasBefore) {
                        sourceOriginal = candidate.before;
                        hasSourceOriginal = true;
                        break;
                    }
                }
            }

            AddListRow(
                gCurrentList,
                type,
                entry.sourcePath,
                TimeText(hasSourceOriginal, sourceOriginal));
            ++sourceRows;
        }

        if (entry.status == fmtfix::EntryStatus::WouldChange ||
            entry.status == fmtfix::EntryStatus::Changed) {
            AddListRow(
                gTargetList,
                entry.directory,
                TimeText(entry.hasBefore, entry.before),
                TimeText(entry.hasTarget, entry.target));
            ++changeRows;
        }
    }

    if (changeRows == 0) {
        AddListRow(gTargetList, L"（没有需要修改的目录）", L"", L"");
    }

    std::wstringstream leftTitle;
    leftTitle << L"当前状态 / 参考来源（"
              << result.entries.size() << L" 个目录";
    if (sourceRows > 0) {
        leftTitle << L"，" << sourceRows << L" 个参考对象";
    }
    leftTitle << L"）";
    SetWindowTextW(gCurrentLabel, leftTitle.str().c_str());

    std::wstringstream rightTitle;
    rightTitle << (result.mode == fmtfix::Mode::DryRun
                       ? L"Dry Run 变更预览"
                       : L"已应用的修改")
               << L"（" << changeRows << L" 项）";
    SetWindowTextW(gTargetLabel, rightTitle.str().c_str());

    SendMessageW(gCurrentList, WM_SETREDRAW, TRUE, 0);
    SendMessageW(gTargetList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(gCurrentList, nullptr, TRUE);
    InvalidateRect(gTargetList, nullptr, TRUE);
}

void RunOperation(HWND owner, fmtfix::Mode mode) {
    std::wstring pathText = GetWindowTextString(gPath);
    std::wstring pathError;
    const std::wstring root = NormalizeAbsolutePath(pathText, pathError);
    if (root.empty()) {
        MessageBoxW(owner, pathError.c_str(), kAppTitle, MB_ICONWARNING);
        return;
    }

    if (mode == fmtfix::Mode::Apply) {
        std::wstring message =
            L"即将实际修改以下目录树中的文件夹“修改时间”：\n\n" + root +
            L"\n\n规则：\n"
            L"• 有直属文件：使用最新直属文件时间，忽略子目录时间。\n"
            L"• 没有直属文件：使用最新直属子目录的处理后时间。\n"
            L"• 空目录不修改。\n"
            L"• 忽略 .git 等以点开头的项目和所有 Reparse Point。\n\n"
            L"建议先执行 Dry Run。确定继续吗？";

        if (MessageBoxW(owner, message.c_str(), L"确认应用修改", MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) != IDYES) {
            return;
        }
    }

    SetControlsEnabled(false);
    ClearComparisonViews();
    SetWindowTextW(gLog, L"");
    SetStatus(mode == fmtfix::Mode::DryRun ? L"正在预览，请稍候..." : L"正在修改，请稍候...");
    UpdateWindow(owner);

    const fmtfix::Result result = fmtfix::ProcessTree(root, mode);
    gLastRoot = root;
    gLastLog = fmtfix::FormatLog(result);
    PopulateComparisonViews(result);

    std::wstringstream summary;
    summary << (mode == fmtfix::Mode::DryRun ? L"Dry Run 完成" : L"应用完成")
            << L"：目录 " << result.summary.directories
            << L"，" << (mode == fmtfix::Mode::DryRun ? L"预计修改 " : L"已修改 ") << result.summary.changedOrWouldChange
            << L"，未变化 " << result.summary.unchanged
            << L"，空目录 " << result.summary.skipped
            << L"，错误 " << result.summary.errors << L"。";

    const std::wstring logStatus = AutoSaveTempLog(root);
    gLastLog += L"\r\n# " + logStatus + L"\r\n";
    SetWindowTextW(gLog, gLastLog.c_str());
    SetStatus(summary.str());
    SetControlsEnabled(true);

    if (result.summary.errors > 0) {
        MessageBoxW(
            owner,
            L"处理完成，但存在错误。请查看下方日志中的 result=error 项目。\n\n"
            L"发生错误的目录不会被当作已成功修改来继续向父目录传播。",
            kAppTitle,
            MB_ICONWARNING);
    }
}

void ApplyFont(HWND hwnd, HFONT font) {
    if (hwnd != nullptr && font != nullptr) {
        SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
}

void RefreshFonts(HWND hwnd) {
    HFONT oldFont = gFont;
    HFONT oldMonoFont = gMonoFont;

    gFont = CreateAppFont(hwnd, L"Segoe UI", 10);
    gMonoFont = CreateAppFont(hwnd, L"Consolas", 9);

    for (HWND child : {
             gRule, gPath, gBrowse, gDryRun, gApply, gCopy, gSave, gStatus,
             gCurrentLabel, gCurrentList, gTargetLabel, gTargetList, gLogLabel}) {
        ApplyFont(child, gFont);
    }
    ApplyFont(gLog, gMonoFont != nullptr ? gMonoFont : gFont);

    if (oldFont != nullptr) {
        DeleteObject(oldFont);
    }
    if (oldMonoFont != nullptr) {
        DeleteObject(oldMonoFont);
    }
}

void ResizeListColumns(HWND hwnd, int leftWidth, int rightWidth) {
    if (gCurrentList == nullptr || gTargetList == nullptr) {
        return;
    }

    const int typeWidth = ScaleForDpi(hwnd, 84);
    const int timeWidth = ScaleForDpi(hwnd, 154);
    const int minPathWidth = ScaleForDpi(hwnd, 150);
    const int innerPad = ScaleForDpi(hwnd, 10);

    const int leftPathWidth =
        std::max(minPathWidth, leftWidth - typeWidth - timeWidth - innerPad);
    ListView_SetColumnWidth(gCurrentList, 0, typeWidth);
    ListView_SetColumnWidth(gCurrentList, 1, leftPathWidth);
    ListView_SetColumnWidth(gCurrentList, 2, timeWidth);

    const int rightPathWidth =
        std::max(minPathWidth, rightWidth - timeWidth * 2 - innerPad);
    ListView_SetColumnWidth(gTargetList, 0, rightPathWidth);
    ListView_SetColumnWidth(gTargetList, 1, timeWidth);
    ListView_SetColumnWidth(gTargetList, 2, timeWidth);
}

void LayoutControls(HWND hwnd) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;

    const int margin = ScaleForDpi(hwnd, 16);
    const int gap = ScaleForDpi(hwnd, 10);
    const int splitGap = ScaleForDpi(hwnd, 12);
    const int rowH = ScaleForDpi(hwnd, 34);
    const int ruleH = ScaleForDpi(hwnd, 48);
    const int labelH = ScaleForDpi(hwnd, 24);
    const int statusH = ScaleForDpi(hwnd, 28);
    const int browseW = ScaleForDpi(hwnd, 136);

    int y = margin;
    MoveWindow(gRule, margin, y, std::max(1, w - margin * 2), ruleH, TRUE);
    y += ruleH + gap;

    MoveWindow(
        gPath, margin, y,
        std::max(1, w - margin * 2 - browseW - gap), rowH, TRUE);
    MoveWindow(gBrowse, w - margin - browseW, y, browseW, rowH, TRUE);
    y += rowH + gap;

    int x = margin;
    const int dryW = ScaleForDpi(hwnd, 144);
    const int applyW = ScaleForDpi(hwnd, 124);
    const int copyW = ScaleForDpi(hwnd, 116);
    const int saveW = ScaleForDpi(hwnd, 124);
    MoveWindow(gDryRun, x, y, dryW, rowH, TRUE);
    x += dryW + gap;
    MoveWindow(gApply, x, y, applyW, rowH, TRUE);
    x += applyW + gap;
    MoveWindow(gCopy, x, y, copyW, rowH, TRUE);
    x += copyW + gap;
    MoveWindow(gSave, x, y, saveW, rowH, TRUE);
    y += rowH + gap;

    MoveWindow(gStatus, margin, y, std::max(1, w - margin * 2), statusH, TRUE);
    y += statusH + gap;

    const int contentBottom = std::max(y, h - margin);
    const int availableH = std::max(ScaleForDpi(hwnd, 260), contentBottom - y);
    const int logMinH = ScaleForDpi(hwnd, 150);
    const int listMinH = ScaleForDpi(hwnd, 150);
    int logH = std::max(logMinH, availableH * 32 / 100);
    int listH = availableH - labelH * 2 - gap - logH;
    if (listH < listMinH) {
        listH = listMinH;
        logH = std::max(ScaleForDpi(hwnd, 90),
                        availableH - labelH * 2 - gap - listH);
    }

    const int contentW = std::max(1, w - margin * 2);
    const int leftW = std::max(1, (contentW - splitGap) / 2);
    const int rightW = std::max(1, contentW - splitGap - leftW);

    MoveWindow(gCurrentLabel, margin, y, leftW, labelH, TRUE);
    MoveWindow(gTargetLabel, margin + leftW + splitGap, y, rightW, labelH, TRUE);
    y += labelH;

    MoveWindow(gCurrentList, margin, y, leftW, listH, TRUE);
    MoveWindow(gTargetList, margin + leftW + splitGap, y, rightW, listH, TRUE);
    ResizeListColumns(hwnd, leftW, rightW);
    y += listH + gap;

    MoveWindow(gLogLabel, margin, y, contentW, labelH, TRUE);
    y += labelH;

    MoveWindow(
        gLog, margin, y, contentW,
        std::max(ScaleForDpi(hwnd, 80), h - margin - y), TRUE);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        gRule = CreateWindowExW(
            0, L"STATIC",
            L"规则：有直属文件 → 取最新直属文件时间；无直属文件 → 取最新直属子目录的处理后时间。\r\n"
            L"空目录不修改；忽略 .git 等点开头项目及 Junction / 符号链接。",
            WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_RULE), nullptr, nullptr);

        gPath = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_PATH), nullptr, nullptr);

        gBrowse = CreateWindowExW(
            0, L"BUTTON", L"选择文件夹...",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_BROWSE), nullptr, nullptr);

        gDryRun = CreateWindowExW(
            0, L"BUTTON", L"Dry Run 预览",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_DRYRUN), nullptr, nullptr);

        gApply = CreateWindowExW(
            0, L"BUTTON", L"应用修改",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_APPLY), nullptr, nullptr);

        gCopy = CreateWindowExW(
            0, L"BUTTON", L"复制日志",
            WS_CHILD | WS_VISIBLE | WS_DISABLED | BS_PUSHBUTTON,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_COPY), nullptr, nullptr);

        gSave = CreateWindowExW(
            0, L"BUTTON", L"保存日志...",
            WS_CHILD | WS_VISIBLE | WS_DISABLED | BS_PUSHBUTTON,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_SAVE), nullptr, nullptr);

        gStatus = CreateWindowExW(
            0, L"STATIC", L"先选择文件夹，再执行 Dry Run。",
            WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_STATUS), nullptr, nullptr);

        gCurrentLabel = CreateWindowExW(
            0, L"STATIC", L"当前状态 / 参考来源",
            WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, hwnd,
            reinterpret_cast<HMENU>(IDC_CURRENT_LABEL), nullptr, nullptr);

        gTargetLabel = CreateWindowExW(
            0, L"STATIC", L"变更预览",
            WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, hwnd,
            reinterpret_cast<HMENU>(IDC_TARGET_LABEL), nullptr, nullptr);

        const DWORD listStyle =
            WS_CHILD | WS_VISIBLE | WS_TABSTOP |
            LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS;

        gCurrentList = CreateWindowExW(
            WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
            listStyle,
            0, 0, 0, 0, hwnd,
            reinterpret_cast<HMENU>(IDC_CURRENT_LIST), nullptr, nullptr);

        gTargetList = CreateWindowExW(
            WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
            listStyle,
            0, 0, 0, 0, hwnd,
            reinterpret_cast<HMENU>(IDC_TARGET_LIST), nullptr, nullptr);

        const DWORD listExtendedStyle =
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER;
        ListView_SetExtendedListViewStyle(gCurrentList, listExtendedStyle);
        ListView_SetExtendedListViewStyle(gTargetList, listExtendedStyle);

        AddListColumn(gCurrentList, 0, L"类型");
        AddListColumn(gCurrentList, 1, L"路径");
        AddListColumn(gCurrentList, 2, L"原修改时间");
        AddListColumn(gTargetList, 0, L"目录");
        AddListColumn(gTargetList, 1, L"修改前");
        AddListColumn(gTargetList, 2, L"修改后");

        gLogLabel = CreateWindowExW(
            0, L"STATIC", L"详细日志",
            WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, hwnd,
            reinterpret_cast<HMENU>(IDC_LOG_LABEL), nullptr, nullptr);

        gLog = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
            ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_READONLY,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_LOG), nullptr, nullptr);
        SendMessageW(gLog, EM_SETLIMITTEXT, 50u * 1024u * 1024u, 0);

        RefreshFonts(hwnd);

        // Start with the current working directory as a convenience.
        DWORD needed = GetCurrentDirectoryW(0, nullptr);
        if (needed > 0) {
            std::wstring cwd(static_cast<size_t>(needed), L'\0');
            DWORD written = GetCurrentDirectoryW(needed, cwd.data());
            if (written > 0 && written < needed) {
                cwd.resize(written);
                SetWindowTextW(gPath, cwd.c_str());
            }
        }

        LayoutControls(hwnd);
        return 0;
    }

    case WM_SIZE:
        LayoutControls(hwnd);
        return 0;

    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = ScaleForDpi(hwnd, 900);
        info->ptMinTrackSize.y = ScaleForDpi(hwnd, 680);
        return 0;
    }

    case WM_DPICHANGED: {
        const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(
            hwnd, nullptr,
            suggested->left, suggested->top,
            suggested->right - suggested->left,
            suggested->bottom - suggested->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        RefreshFonts(hwnd);
        LayoutControls(hwnd);
        return 0;
    }

    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        switch (id) {
        case IDC_BROWSE: {
            std::wstring chosen;
            if (BrowseForFolder(hwnd, chosen)) {
                SetWindowTextW(gPath, chosen.c_str());
            }
            return 0;
        }
        case IDC_DRYRUN:
            RunOperation(hwnd, fmtfix::Mode::DryRun);
            return 0;
        case IDC_APPLY:
            RunOperation(hwnd, fmtfix::Mode::Apply);
            return 0;
        case IDC_COPY:
            CopyLogToClipboard(hwnd);
            return 0;
        case IDC_SAVE:
            SaveLogAs(hwnd);
            return 0;
        default:
            break;
        }
        break;
    }

    case WM_DESTROY:
        if (gFont != nullptr) {
            DeleteObject(gFont);
            gFont = nullptr;
        }
        if (gMonoFont != nullptr) {
            DeleteObject(gMonoFont);
            gMonoFont = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&controls);

    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kWindowClass;

    if (!RegisterClassExW(&wc)) {
        if (SUCCEEDED(com)) {
            CoUninitialize();
        }
        return 1;
    }

    HWND hwnd = CreateWindowExW(
        0,
        kWindowClass,
        kAppTitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1180,
        820,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (hwnd == nullptr) {
        if (SUCCEEDED(com)) {
            CoUninitialize();
        }
        return 1;
    }

    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (SUCCEEDED(com)) {
        CoUninitialize();
    }
    return static_cast<int>(msg.wParam);
}
