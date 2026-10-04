#define UNICODE
#define _UNICODE
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shobjidl.h>
#include <shellapi.h>

#include <algorithm>
#include <cstring>
#include <cwctype>
#include <memory>
#include <set>
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
constexpr int IDC_TREEGRID = 1011;
constexpr int IDC_TARGET_LABEL = 1012;
constexpr int IDC_CHANGE_LIST = 1013;
constexpr int IDC_LOG_LABEL = 1014;
constexpr int IDC_RIGHT_TOGGLE = 1015;
constexpr int IDC_SUMMARY = 1016;

constexpr UINT IDM_EXPAND_ALL = 2001;
constexpr UINT IDM_COLLAPSE_ALL = 2002;
constexpr UINT IDM_EXCLUDE = 2003;
constexpr UINT IDM_INCLUDE = 2004;
constexpr UINT IDM_REFRESH = 2005;

HWND gPath = nullptr;
HWND gBrowse = nullptr;
HWND gDryRun = nullptr;
HWND gApply = nullptr;
HWND gCopy = nullptr;
HWND gSave = nullptr;
HWND gLog = nullptr;
HWND gStatus = nullptr;
HWND gRule = nullptr;
HWND gSummary = nullptr;
HWND gCurrentLabel = nullptr;
HWND gTreeGrid = nullptr;
HWND gTargetLabel = nullptr;
HWND gChangeList = nullptr;
HWND gRightToggle = nullptr;
HWND gLogLabel = nullptr;
HFONT gFont = nullptr;
HFONT gMonoFont = nullptr;
HIMAGELIST gSystemImageList = nullptr;
std::wstring gLastLog;
std::wstring gLastRoot;
bool gRightCollapsed = false;
int gSortColumn = 0;
bool gSortAscending = true;
std::set<std::wstring> gExpandedPaths;
std::set<std::wstring> gExcludedPaths;

struct FsNode {
    std::wstring path;
    std::wstring name;
    std::wstring type;
    std::wstring scanError;
    FILETIME lastWrite{};
    FILETIME targetTime{};
    bool hasTarget = false;
    bool isDirectory = false;
    bool willChange = false;
    bool isReference = false;
    bool excluded = false;
    bool expanded = false;
    ULONGLONG size = 0;
    size_t directFiles = 0;
    size_t directDirectories = 0;
    int iconIndex = -1;
    FsNode* parent = nullptr;
    std::vector<std::unique_ptr<FsNode>> children;
};

std::unique_ptr<FsNode> gRootNode;
std::vector<FsNode*> gVisibleNodes;

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
                const std::wstring& third,
                const std::wstring& fourth = L"") {
    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.iItem = ListView_GetItemCount(list);
    item.iSubItem = 0;
    item.pszText = const_cast<LPWSTR>(first.c_str());
    const int row = ListView_InsertItem(list, &item);
    if (row >= 0) {
        ListView_SetItemText(list, row, 1, const_cast<LPWSTR>(second.c_str()));
        ListView_SetItemText(list, row, 2, const_cast<LPWSTR>(third.c_str()));
        if (ListView_GetHeader(list) != nullptr &&
            Header_GetItemCount(ListView_GetHeader(list)) > 3) {
            ListView_SetItemText(list, row, 3, const_cast<LPWSTR>(fourth.c_str()));
        }
    }
}

ULONGLONG FileTimeValue(const FILETIME& ft) {
    ULARGE_INTEGER value{};
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    return value.QuadPart;
}

std::wstring FormatSize(ULONGLONG bytes) {
    static const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
    double value = static_cast<double>(bytes);
    size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < (sizeof(units) / sizeof(units[0]))) {
        value /= 1024.0;
        ++unit;
    }

    wchar_t buffer[64]{};
    if (unit == 0) {
        swprintf_s(buffer, L"%llu B", bytes);
    } else if (value >= 100.0) {
        swprintf_s(buffer, L"%.0f %s", value, units[unit]);
    } else if (value >= 10.0) {
        swprintf_s(buffer, L"%.1f %s", value, units[unit]);
    } else {
        swprintf_s(buffer, L"%.2f %s", value, units[unit]);
    }
    return buffer;
}

std::wstring CountText(const FsNode& node) {
    if (!node.isDirectory) {
        return {};
    }
    std::wstringstream out;
    out << node.directFiles << L" 文件 / "
        << node.directDirectories << L" 文件夹";
    return out.str();
}

std::wstring FileTypeText(const std::wstring& name, bool isDirectory) {
    if (isDirectory) {
        return L"文件夹";
    }

    const size_t slash = name.find_last_of(L"\\/");
    const size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos ||
        (slash != std::wstring::npos && dot < slash) ||
        dot + 1 >= name.size()) {
        return L"文件";
    }

    std::wstring ext = name.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towupper(c));
    });
    return ext + L" 文件";
}

bool StartsWithDotName(const std::wstring& name) {
    return !name.empty() && name.front() == L'.';
}

int ShellIconIndex(const std::wstring& path, bool isDirectory) {
    SHFILEINFOW info{};
    const DWORD attrs =
        isDirectory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    HIMAGELIST images = reinterpret_cast<HIMAGELIST>(
        SHGetFileInfoW(
            path.c_str(),
            attrs,
            &info,
            sizeof(info),
            SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES));
    if (images != nullptr && gSystemImageList == nullptr) {
        gSystemImageList = images;
        if (gTreeGrid != nullptr) {
            ListView_SetImageList(gTreeGrid, gSystemImageList, LVSIL_SMALL);
        }
    }
    return images != nullptr ? info.iIcon : -1;
}

bool IsExcludedPath(const std::wstring& path) {
    for (const auto& excluded : gExcludedPaths) {
        if (fmtfix::IsPathInside(path, excluded)) {
            return true;
        }
    }
    return false;
}

bool IsExplicitlyExcludedPath(const std::wstring& path) {
    for (const auto& excluded : gExcludedPaths) {
        if (_wcsicmp(path.c_str(), excluded.c_str()) == 0) {
            return true;
        }
    }
    return false;
}

void AddExclusionPath(const std::wstring& path) {
    // A parent exclusion already covers its descendants. Drop redundant child
    // exclusions so cancelling the parent later has predictable semantics.
    for (auto it = gExcludedPaths.begin(); it != gExcludedPaths.end();) {
        if (fmtfix::IsPathInside(*it, path)) {
            it = gExcludedPaths.erase(it);
        } else {
            ++it;
        }
    }
    gExcludedPaths.insert(path);
}

std::unique_ptr<FsNode> ScanDirectoryNode(
    const std::wstring& path,
    const std::wstring& displayName,
    FsNode* parent) {

    auto node = std::make_unique<FsNode>();
    node->path = path;
    node->name = displayName;
    node->type = L"文件夹";
    node->isDirectory = true;
    node->parent = parent;
    node->excluded = IsExcludedPath(path);
    node->expanded =
        parent == nullptr || gExpandedPaths.count(path) != 0;
    node->iconIndex = ShellIconIndex(path, true);

    WIN32_FILE_ATTRIBUTE_DATA rootData{};
    if (GetFileAttributesExW(
            ToExtendedPathForApi(path).c_str(),
            GetFileExInfoStandard,
            &rootData)) {
        node->lastWrite = rootData.ftLastWriteTime;
    }

    std::wstring pattern = ToExtendedPathForApi(path);
    if (!pattern.empty() && pattern.back() != L'\\') {
        pattern.push_back(L'\\');
    }
    pattern.push_back(L'*');

    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileExW(
        pattern.c_str(),
        FindExInfoBasic,
        &data,
        FindExSearchNameMatch,
        nullptr,
        FIND_FIRST_EX_LARGE_FETCH);

    if (find == INVALID_HANDLE_VALUE &&
        GetLastError() == ERROR_INVALID_PARAMETER) {
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
        if (code != ERROR_FILE_NOT_FOUND) {
            std::wstringstream err;
            err << L"无法读取（错误 " << code << L"）";
            node->scanError = err.str();
        }
        return node;
    }

    do {
        const std::wstring name = data.cFileName;
        if (name == L"." || name == L".." || StartsWithDotName(name)) {
            continue;
        }

        if ((data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            continue;
        }

        std::wstring childPath = path;
        if (!childPath.empty() && childPath.back() != L'\\') {
            childPath.push_back(L'\\');
        }
        childPath += name;

        const bool isDirectory =
            (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

        if (isDirectory) {
            ++node->directDirectories;
            auto child = ScanDirectoryNode(childPath, name, node.get());
            node->size += child->size;
            node->children.push_back(std::move(child));
        } else {
            ++node->directFiles;
            auto child = std::make_unique<FsNode>();
            child->path = childPath;
            child->name = name;
            child->type = FileTypeText(name, false);
            child->lastWrite = data.ftLastWriteTime;
            child->isDirectory = false;
            child->parent = node.get();
            child->excluded = node->excluded;
            child->iconIndex = ShellIconIndex(childPath, false);

            ULARGE_INTEGER fileSize{};
            fileSize.HighPart = data.nFileSizeHigh;
            fileSize.LowPart = data.nFileSizeLow;
            child->size = fileSize.QuadPart;
            node->size += child->size;

            node->children.push_back(std::move(child));
        }
    } while (FindNextFileW(find, &data));

    const DWORD endCode = GetLastError();
    FindClose(find);
    if (endCode != ERROR_NO_MORE_FILES) {
        std::wstringstream err;
        err << L"目录扫描未完整结束（错误 " << endCode << L"）";
        node->scanError = err.str();
    }
    return node;
}

std::wstring RootDisplayName(const std::wstring& root) {
    std::wstring trimmed = root;
    while (trimmed.size() > 3 &&
           (trimmed.back() == L'\\' || trimmed.back() == L'/')) {
        trimmed.pop_back();
    }
    const size_t slash = trimmed.find_last_of(L"\\/");
    if (slash == std::wstring::npos || slash + 1 >= trimmed.size()) {
        return trimmed;
    }
    return trimmed.substr(slash + 1);
}

void ApplyResultToNodes(FsNode* node, const fmtfix::Result& result) {
    if (node == nullptr) {
        return;
    }

    for (const auto& entry : result.entries) {
        if (entry.directory == node->path) {
            node->willChange =
                entry.status == fmtfix::EntryStatus::WouldChange ||
                entry.status == fmtfix::EntryStatus::Changed;
            node->hasTarget = entry.hasTarget;
            if (entry.hasTarget) {
                node->targetTime = entry.target;
            }
        }
        if (!entry.sourcePath.empty() && entry.sourcePath == node->path) {
            node->isReference = true;
        }
    }

    for (auto& child : node->children) {
        ApplyResultToNodes(child.get(), result);
    }
}

int CompareTextNoCase(const std::wstring& a, const std::wstring& b) {
    return _wcsicmp(a.c_str(), b.c_str());
}

std::wstring NodeRemark(const FsNode& node) {
    std::wstring remark;
    if (node.excluded) {
        remark = L"已排除（不修改）";
    }
    if (!node.scanError.empty()) {
        if (!remark.empty()) {
            remark += L"；";
        }
        remark += node.scanError;
    }
    if (node.isReference) {
        if (!remark.empty()) {
            remark += L"；";
        }
        remark += node.isDirectory ? L"参考目录" : L"参考文件";
    }
    return remark;
}

int CompareNodesByColumn(const FsNode* a, const FsNode* b) {
    // Keep folders before files regardless of ascending/descending order so
    // sorting never destroys the tree-grid's directory-first structure.
    if (a->isDirectory != b->isDirectory) {
        return a->isDirectory ? -1 : 1;
    }

    int cmp = 0;
    switch (gSortColumn) {
    case 1:
        cmp = a->size < b->size ? -1 : (a->size > b->size ? 1 : 0);
        break;
    case 2: {
        const size_t ac = a->directFiles + a->directDirectories;
        const size_t bc = b->directFiles + b->directDirectories;
        cmp = ac < bc ? -1 : (ac > bc ? 1 : 0);
        break;
    }
    case 3:
        cmp = CompareTextNoCase(a->type, b->type);
        break;
    case 4: {
        const ULONGLONG av = FileTimeValue(a->lastWrite);
        const ULONGLONG bv = FileTimeValue(b->lastWrite);
        cmp = av < bv ? -1 : (av > bv ? 1 : 0);
        break;
    }
    case 5: {
        if (a->willChange != b->willChange) {
            cmp = a->willChange ? -1 : 1;
        } else {
            const ULONGLONG av =
                a->hasTarget ? FileTimeValue(a->targetTime) : 0;
            const ULONGLONG bv =
                b->hasTarget ? FileTimeValue(b->targetTime) : 0;
            cmp = av < bv ? -1 : (av > bv ? 1 : 0);
        }
        break;
    }
    case 6:
        cmp = CompareTextNoCase(NodeRemark(*a), NodeRemark(*b));
        break;
    case 0:
    default:
        cmp = CompareTextNoCase(a->name, b->name);
        break;
    }

    cmp = gSortAscending ? cmp : -cmp;
    if (cmp == 0) {
        // Keep name as a deterministic tie-breaker without reversing the
        // directory/file grouping above.
        cmp = CompareTextNoCase(a->name, b->name);
        if (!gSortAscending) {
            cmp = -cmp;
        }
    }
    return cmp;
}

void UpdateTreeGridSortIndicator() {
    if (gTreeGrid == nullptr) {
        return;
    }

    HWND header = ListView_GetHeader(gTreeGrid);
    if (header == nullptr) {
        return;
    }

    const int count = Header_GetItemCount(header);
    for (int i = 0; i < count; ++i) {
        HDITEMW item{};
        item.mask = HDI_FORMAT;
        if (!Header_GetItemW(header, i, &item)) {
            continue;
        }
        item.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        if (i == gSortColumn) {
            item.fmt |= gSortAscending ? HDF_SORTUP : HDF_SORTDOWN;
        }
        Header_SetItemW(header, i, &item);
    }
}

void SortNodeChildren(FsNode* node) {
    if (node == nullptr || !node->isDirectory) {
        return;
    }

    std::stable_sort(
        node->children.begin(),
        node->children.end(),
        [](const std::unique_ptr<FsNode>& a,
           const std::unique_ptr<FsNode>& b) {
            return CompareNodesByColumn(a.get(), b.get()) < 0;
        });

    for (auto& child : node->children) {
        SortNodeChildren(child.get());
    }
}

void FlattenVisibleNodes(FsNode* node) {
    if (node == nullptr) {
        return;
    }
    gVisibleNodes.push_back(node);

    if (!node->isDirectory || !node->expanded) {
        return;
    }

    for (auto& child : node->children) {
        FlattenVisibleNodes(child.get());
    }
}

void RebuildTreeGrid() {
    if (gTreeGrid == nullptr) {
        return;
    }

    SendMessageW(gTreeGrid, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(gTreeGrid);
    gVisibleNodes.clear();

    if (gRootNode != nullptr) {
        SortNodeChildren(gRootNode.get());
        FlattenVisibleNodes(gRootNode.get());
    }

    for (size_t i = 0; i < gVisibleNodes.size(); ++i) {
        FsNode* node = gVisibleNodes[i];

        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = static_cast<int>(i);
        item.iSubItem = 0;
        item.pszText = const_cast<LPWSTR>(node->name.c_str());
        item.lParam = reinterpret_cast<LPARAM>(node);
        const int row = ListView_InsertItem(gTreeGrid, &item);
        if (row < 0) {
            continue;
        }

        const std::wstring sizeText = FormatSize(node->size);
        const std::wstring countText = CountText(*node);
        const std::wstring modified = TimeText(true, node->lastWrite);
        const std::wstring target =
            node->willChange && node->hasTarget
                ? TimeText(true, node->targetTime)
                : L"";
        const std::wstring remark = NodeRemark(*node);

        ListView_SetItemText(
            gTreeGrid, row, 1,
            const_cast<LPWSTR>(sizeText.c_str()));
        ListView_SetItemText(
            gTreeGrid, row, 2,
            const_cast<LPWSTR>(countText.c_str()));
        ListView_SetItemText(
            gTreeGrid, row, 3,
            const_cast<LPWSTR>(node->type.c_str()));
        ListView_SetItemText(
            gTreeGrid, row, 4,
            const_cast<LPWSTR>(modified.c_str()));
        ListView_SetItemText(
            gTreeGrid, row, 5,
            const_cast<LPWSTR>(target.c_str()));
        ListView_SetItemText(
            gTreeGrid, row, 6,
            const_cast<LPWSTR>(remark.c_str()));
    }

    SendMessageW(gTreeGrid, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(gTreeGrid, nullptr, TRUE);
}

void PopulateChangeList(const fmtfix::Result& result) {
    ListView_DeleteAllItems(gChangeList);
    size_t changes = 0;

    for (const auto& entry : result.entries) {
        if (entry.status != fmtfix::EntryStatus::WouldChange &&
            entry.status != fmtfix::EntryStatus::Changed) {
            continue;
        }

        std::wstring source;
        if (!entry.sourcePath.empty()) {
            source =
                (entry.sourceType == L"file" ? L"文件: " : L"目录: ") +
                entry.sourcePath;
        }

        AddListRow(
            gChangeList,
            entry.directory,
            TimeText(entry.hasBefore, entry.before),
            TimeText(entry.hasTarget, entry.target),
            source);
        ++changes;
    }

    if (changes == 0) {
        AddListRow(gChangeList, L"（没有需要修改的目录）", L"", L"", L"");
    }

    std::wstringstream title;
    title << (result.mode == fmtfix::Mode::DryRun ? L"即将修改（" : L"已修改（")
          << changes << L" 项）";
    SetWindowTextW(gTargetLabel, title.str().c_str());
}

void CountTree(const FsNode* node, size_t& files, size_t& directories) {
    if (node == nullptr) {
        return;
    }

    for (const auto& child : node->children) {
        if (child->isDirectory) {
            ++directories;
            CountTree(child.get(), files, directories);
        } else {
            ++files;
        }
    }
}

void UpdateRootSummary() {
    if (gRootNode == nullptr) {
        SetWindowTextW(gSummary, L"");
        return;
    }

    size_t files = 0;
    size_t directories = 0;
    CountTree(gRootNode.get(), files, directories);

    std::wstringstream out;
    out << L"选择：  " << gRootNode->path << L"\r\n"
        << L"总大小： " << FormatSize(gRootNode->size)
        << L"    文件： " << files
        << L"    文件夹： " << directories;
    if (!gExcludedPaths.empty()) {
        out << L"    已排除： " << gExcludedPaths.size();
    }
    SetWindowTextW(gSummary, out.str().c_str());
}

bool HasNextSibling(const FsNode* node) {
    if (node == nullptr || node->parent == nullptr) {
        return false;
    }

    const auto& siblings = node->parent->children;
    for (size_t i = 0; i < siblings.size(); ++i) {
        if (siblings[i].get() == node) {
            return i + 1 < siblings.size();
        }
    }
    return false;
}

std::wstring BranchPrefix(const FsNode* node) {
    if (node == nullptr || node->parent == nullptr) {
        return {};
    }

    std::vector<const FsNode*> ancestors;
    for (const FsNode* p = node->parent;
         p != nullptr && p->parent != nullptr;
         p = p->parent) {
        ancestors.push_back(p);
    }
    std::reverse(ancestors.begin(), ancestors.end());

    std::wstring prefix;
    for (const FsNode* ancestor : ancestors) {
        prefix += HasNextSibling(ancestor) ? L"│   " : L"    ";
    }
    prefix += HasNextSibling(node) ? L"├─ " : L"└─ ";
    return prefix;
}

SIZE TextExtent(HDC dc, HFONT font, const std::wstring& text) {
    SIZE size{};
    HGDIOBJ old = nullptr;
    if (font != nullptr) {
        old = SelectObject(dc, font);
    }
    GetTextExtentPoint32W(
        dc,
        text.c_str(),
        static_cast<int>(text.size()),
        &size);
    if (old != nullptr) {
        SelectObject(dc, old);
    }
    return size;
}

COLORREF NodeBackground(const FsNode* node, bool selected) {
    if (selected) {
        return GetSysColor(COLOR_HIGHLIGHT);
    }
    if (node != nullptr && node->excluded) {
        return RGB(242, 242, 242);
    }
    if (node != nullptr && node->willChange) {
        return RGB(255, 239, 184);
    }
    return GetSysColor(COLOR_WINDOW);
}

COLORREF NodeTextColor(const FsNode* node, bool selected) {
    if (selected) {
        return GetSysColor(COLOR_HIGHLIGHTTEXT);
    }
    if (node != nullptr && node->excluded) {
        return GetSysColor(COLOR_GRAYTEXT);
    }
    if (node != nullptr && node->willChange) {
        return RGB(145, 72, 0);
    }
    return GetSysColor(COLOR_WINDOWTEXT);
}

bool GetTreeCellRect(int row, RECT& rect) {
    if (gTreeGrid == nullptr || row < 0) {
        return false;
    }

    RECT rowRect{};
    if (!ListView_GetItemRect(gTreeGrid, row, &rowRect, LVIR_BOUNDS)) {
        return false;
    }

    HWND header = ListView_GetHeader(gTreeGrid);
    RECT columnRect{};
    if (header == nullptr || !Header_GetItemRect(header, 0, &columnRect)) {
        return false;
    }

    // Header_GetItemRect follows the current header order/position, so the
    // custom tree cell keeps working even after the user drags column 0.
    rect.left = columnRect.left;
    rect.right = columnRect.right;
    rect.top = rowRect.top;
    rect.bottom = rowRect.bottom;
    return true;
}

LRESULT HandleTreeGridCustomDraw(NMLVCUSTOMDRAW* draw) {
    if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) {
        return CDRF_NOTIFYITEMDRAW;
    }

    if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
        const int row = static_cast<int>(draw->nmcd.dwItemSpec);
        FsNode* node =
            row >= 0 && static_cast<size_t>(row) < gVisibleNodes.size()
                ? gVisibleNodes[static_cast<size_t>(row)]
                : nullptr;
        const bool selected =
            (ListView_GetItemState(gTreeGrid, row, LVIS_SELECTED) &
             LVIS_SELECTED) != 0;
        draw->clrTextBk = NodeBackground(node, selected);
        draw->clrText = NodeTextColor(node, selected);
        return CDRF_NOTIFYSUBITEMDRAW;
    }

    if (draw->nmcd.dwDrawStage ==
        (CDDS_ITEMPREPAINT | CDDS_SUBITEM)) {
        if (draw->iSubItem != 0) {
            return CDRF_DODEFAULT;
        }

        const int row = static_cast<int>(draw->nmcd.dwItemSpec);
        if (row < 0 ||
            static_cast<size_t>(row) >= gVisibleNodes.size()) {
            return CDRF_DODEFAULT;
        }

        FsNode* node = gVisibleNodes[static_cast<size_t>(row)];
        RECT rect{};
        if (!GetTreeCellRect(row, rect)) {
            return CDRF_DODEFAULT;
        }

        const bool selected =
            (ListView_GetItemState(gTreeGrid, row, LVIS_SELECTED) &
             LVIS_SELECTED) != 0;
        HBRUSH brush =
            CreateSolidBrush(NodeBackground(node, selected));
        FillRect(draw->nmcd.hdc, &rect, brush);
        DeleteObject(brush);

        SetBkMode(draw->nmcd.hdc, TRANSPARENT);
        SetTextColor(
            draw->nmcd.hdc,
            NodeTextColor(node, selected));

        int x = rect.left + ScaleForDpi(gTreeGrid, 5);
        const std::wstring prefix = BranchPrefix(node);
        if (!prefix.empty()) {
            const SIZE prefixSize =
                TextExtent(draw->nmcd.hdc, gMonoFont, prefix);
            HGDIOBJ oldFont = SelectObject(
                draw->nmcd.hdc,
                gMonoFont != nullptr ? gMonoFont : gFont);
            TextOutW(
                draw->nmcd.hdc,
                x,
                rect.top + (rect.bottom - rect.top - prefixSize.cy) / 2,
                prefix.c_str(),
                static_cast<int>(prefix.size()));
            SelectObject(draw->nmcd.hdc, oldFont);
            x += prefixSize.cx;
        }

        const int boxSize = ScaleForDpi(gTreeGrid, 11);
        const int boxY =
            rect.top + (rect.bottom - rect.top - boxSize) / 2;
        if (node->isDirectory && !node->children.empty()) {
            RECT box{x, boxY, x + boxSize, boxY + boxSize};
            FrameRect(
                draw->nmcd.hdc,
                &box,
                GetSysColorBrush(COLOR_BTNSHADOW));

            const int midX = (box.left + box.right) / 2;
            const int midY = (box.top + box.bottom) / 2;
            MoveToEx(draw->nmcd.hdc, box.left + 2, midY, nullptr);
            LineTo(draw->nmcd.hdc, box.right - 2, midY);
            if (!node->expanded) {
                MoveToEx(draw->nmcd.hdc, midX, box.top + 2, nullptr);
                LineTo(draw->nmcd.hdc, midX, box.bottom - 2);
            }
        }
        x += boxSize + ScaleForDpi(gTreeGrid, 5);

        const int iconSize = ScaleForDpi(gTreeGrid, 16);
        if (gSystemImageList != nullptr && node->iconIndex >= 0) {
            ImageList_Draw(
                gSystemImageList,
                node->iconIndex,
                draw->nmcd.hdc,
                x,
                rect.top + (rect.bottom - rect.top - iconSize) / 2,
                ILD_TRANSPARENT);
        }
        x += iconSize + ScaleForDpi(gTreeGrid, 5);

        RECT textRect = rect;
        textRect.left = x;
        DrawTextW(
            draw->nmcd.hdc,
            node->name.c_str(),
            -1,
            &textRect,
            DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS |
                DT_NOPREFIX);
        return CDRF_SKIPDEFAULT;
    }

    return CDRF_DODEFAULT;
}

int TreeToggleXForRow(int row) {
    if (row < 0 ||
        static_cast<size_t>(row) >= gVisibleNodes.size()) {
        return -1;
    }

    FsNode* node = gVisibleNodes[static_cast<size_t>(row)];
    RECT rect{};
    if (!GetTreeCellRect(row, rect)) {
        return -1;
    }

    HDC dc = GetDC(gTreeGrid);
    const std::wstring prefix = BranchPrefix(node);
    const SIZE prefixSize = TextExtent(dc, gMonoFont, prefix);
    ReleaseDC(gTreeGrid, dc);

    return rect.left +
           ScaleForDpi(gTreeGrid, 5) +
           prefixSize.cx;
}

void ToggleTreeNode(int row) {
    if (row < 0 ||
        static_cast<size_t>(row) >= gVisibleNodes.size()) {
        return;
    }

    FsNode* node = gVisibleNodes[static_cast<size_t>(row)];
    if (!node->isDirectory || node->children.empty()) {
        return;
    }

    node->expanded = !node->expanded;
    if (node->expanded) {
        gExpandedPaths.insert(node->path);
    } else {
        gExpandedPaths.erase(node->path);
    }
    RebuildTreeGrid();
}

void SetSubtreeExpanded(FsNode* node, bool expanded) {
    if (node == nullptr || !node->isDirectory) {
        return;
    }

    node->expanded = expanded;
    if (expanded) {
        gExpandedPaths.insert(node->path);
    } else {
        gExpandedPaths.erase(node->path);
    }

    for (auto& child : node->children) {
        if (child->isDirectory) {
            SetSubtreeExpanded(child.get(), expanded);
        }
    }
}

std::vector<std::wstring> CurrentExclusions() {
    return std::vector<std::wstring>(
        gExcludedPaths.begin(),
        gExcludedPaths.end());
}

void ResizeTreeGridColumns(HWND hwnd, int width) {
    if (gTreeGrid == nullptr) {
        return;
    }

    const int nameW = std::max(
        ScaleForDpi(hwnd, 250),
        width * 31 / 100);
    const int sizeW = ScaleForDpi(hwnd, 105);
    const int countW = ScaleForDpi(hwnd, 150);
    const int typeW = ScaleForDpi(hwnd, 110);
    const int timeW = ScaleForDpi(hwnd, 165);
    const int targetW = ScaleForDpi(hwnd, 165);
    const int remarkW = std::max(
        ScaleForDpi(hwnd, 130),
        width - nameW - sizeW - countW - typeW -
            timeW - targetW - ScaleForDpi(hwnd, 18));

    const int widths[] = {
        nameW, sizeW, countW, typeW,
        timeW, targetW, remarkW};
    for (int i = 0; i < 7; ++i) {
        ListView_SetColumnWidth(gTreeGrid, i, widths[i]);
    }
}

void ResizeChangeListColumns(HWND hwnd, int width) {
    if (gChangeList == nullptr) {
        return;
    }

    const int timeW = ScaleForDpi(hwnd, 155);
    const int sourceW = std::max(
        ScaleForDpi(hwnd, 180),
        width * 30 / 100);
    const int pathW = std::max(
        ScaleForDpi(hwnd, 180),
        width - timeW * 2 - sourceW -
            ScaleForDpi(hwnd, 18));

    ListView_SetColumnWidth(gChangeList, 0, pathW);
    ListView_SetColumnWidth(gChangeList, 1, timeW);
    ListView_SetColumnWidth(gChangeList, 2, timeW);
    ListView_SetColumnWidth(gChangeList, 3, sourceW);
}

UINT ShowTreeContextMenu(HWND owner, FsNode* node, POINT screenPoint) {
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return 0;
    }

    const bool isDirectory = node != nullptr && node->isDirectory;
    AppendMenuW(
        menu,
        MF_STRING | (isDirectory ? 0 : MF_GRAYED),
        IDM_EXPAND_ALL,
        L"展开此目录及所有子目录");
    AppendMenuW(
        menu,
        MF_STRING | (isDirectory ? 0 : MF_GRAYED),
        IDM_COLLAPSE_ALL,
        L"收缩此目录及所有子目录");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    if (isDirectory && node->parent != nullptr) {
        if (IsExplicitlyExcludedPath(node->path)) {
            AppendMenuW(
                menu,
                MF_STRING,
                IDM_INCLUDE,
                L"取消排除该目录");
        } else if (node->excluded) {
            AppendMenuW(
                menu,
                MF_STRING | MF_GRAYED,
                IDM_EXCLUDE,
                L"已随父目录排除");
        } else {
            AppendMenuW(
                menu,
                MF_STRING,
                IDM_EXCLUDE,
                L"排除该目录（本次不修改）");
        }
    } else {
        AppendMenuW(
            menu,
            MF_STRING | MF_GRAYED,
            IDM_EXCLUDE,
            L"排除该目录（本次不修改）");
    }

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(
        menu,
        MF_STRING,
        IDM_REFRESH,
        L"刷新目录并重新 Dry Run");

    const UINT command = TrackPopupMenu(
        menu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON,
        screenPoint.x,
        screenPoint.y,
        0,
        owner,
        nullptr);
    DestroyMenu(menu);
    return command;
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
    ListView_DeleteAllItems(gTreeGrid);
    ListView_DeleteAllItems(gChangeList);
    SetWindowTextW(gLog, L"");
    SetStatus(mode == fmtfix::Mode::DryRun
                  ? L"正在扫描目录并计算 Dry Run，请稍候..."
                  : L"正在修改并重新扫描目录，请稍候...");
    UpdateWindow(owner);

    for (auto it = gExcludedPaths.begin(); it != gExcludedPaths.end();) {
        if (!fmtfix::IsPathInside(*it, root)) {
            it = gExcludedPaths.erase(it);
        } else {
            ++it;
        }
    }
    gExpandedPaths.insert(root);

    const fmtfix::Result result =
        fmtfix::ProcessTree(root, mode, CurrentExclusions());

    gLastRoot = root;
    gLastLog = fmtfix::FormatLog(result);

    gRootNode = ScanDirectoryNode(
        root,
        RootDisplayName(root),
        nullptr);
    ApplyResultToNodes(gRootNode.get(), result);
    RebuildTreeGrid();
    PopulateChangeList(result);
    UpdateRootSummary();

    std::wstringstream leftTitle;
    leftTitle << L"目录树    （黄色行 = "
              << (mode == fmtfix::Mode::DryRun ? L"将修改" : L"已修改")
              << L"；灰色行 = 已排除）";
    SetWindowTextW(gCurrentLabel, leftTitle.str().c_str());

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
             gRule, gSummary, gPath, gBrowse, gDryRun, gApply, gCopy, gSave,
             gStatus, gCurrentLabel, gTreeGrid, gTargetLabel, gChangeList,
             gRightToggle, gLogLabel}) {
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

void LayoutControls(HWND hwnd) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;

    const int margin = ScaleForDpi(hwnd, 14);
    const int gap = ScaleForDpi(hwnd, 8);
    const int splitGap = ScaleForDpi(hwnd, 10);
    const int rowH = ScaleForDpi(hwnd, 34);
    const int topInfoH = ScaleForDpi(hwnd, 58);
    const int labelH = ScaleForDpi(hwnd, 26);
    const int statusH = ScaleForDpi(hwnd, 27);
    const int browseW = ScaleForDpi(hwnd, 138);
    const int toggleW = ScaleForDpi(hwnd, 120);

    int y = margin;

    const int infoW = std::max(1, w - margin * 2);
    const int ruleW = std::max(
        ScaleForDpi(hwnd, 420),
        infoW * 47 / 100);
    const int summaryW = std::max(1, infoW - ruleW - splitGap);

    MoveWindow(gRule, margin, y, ruleW, topInfoH, TRUE);
    MoveWindow(
        gSummary,
        margin + ruleW + splitGap,
        y,
        summaryW,
        topInfoH,
        TRUE);
    y += topInfoH + gap;

    MoveWindow(
        gPath,
        margin,
        y,
        std::max(1, w - margin * 2 - browseW - gap),
        rowH,
        TRUE);
    MoveWindow(
        gBrowse,
        w - margin - browseW,
        y,
        browseW,
        rowH,
        TRUE);
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

    MoveWindow(
        gStatus,
        margin,
        y,
        std::max(1, w - margin * 2),
        statusH,
        TRUE);
    y += statusH + gap;

    const int contentW = std::max(1, w - margin * 2);
    const int contentBottom = std::max(y, h - margin);
    const int availableH =
        std::max(ScaleForDpi(hwnd, 280), contentBottom - y);
    const int logMinH = ScaleForDpi(hwnd, 145);
    const int gridMinH = ScaleForDpi(hwnd, 190);
    int logH = std::max(logMinH, availableH * 28 / 100);
    int gridH = availableH - labelH * 2 - gap - logH;
    if (gridH < gridMinH) {
        gridH = gridMinH;
        logH = std::max(
            ScaleForDpi(hwnd, 90),
            availableH - labelH * 2 - gap - gridH);
    }

    int leftW = contentW;
    int rightW = 0;
    if (!gRightCollapsed) {
        rightW = std::max(
            ScaleForDpi(hwnd, 360),
            contentW * 35 / 100);
        leftW = std::max(
            ScaleForDpi(hwnd, 520),
            contentW - rightW - splitGap);
        rightW = std::max(1, contentW - leftW - splitGap);
    }

    MoveWindow(
        gCurrentLabel,
        margin,
        y,
        std::max(1, leftW - toggleW - gap),
        labelH,
        TRUE);

    if (gRightCollapsed) {
        ShowWindow(gTargetLabel, SW_HIDE);
        ShowWindow(gChangeList, SW_HIDE);
        SetWindowTextW(gRightToggle, L"← 显示变更");
        MoveWindow(
            gRightToggle,
            margin + contentW - toggleW,
            y,
            toggleW,
            labelH,
            TRUE);
    } else {
        const int rightX = margin + leftW + splitGap;
        ShowWindow(gTargetLabel, SW_SHOW);
        ShowWindow(gChangeList, SW_SHOW);
        SetWindowTextW(gRightToggle, L"隐藏变更 →");
        MoveWindow(
            gRightToggle,
            rightX + rightW - toggleW,
            y,
            toggleW,
            labelH,
            TRUE);
        MoveWindow(
            gTargetLabel,
            rightX,
            y,
            std::max(1, rightW - toggleW - gap),
            labelH,
            TRUE);
    }
    y += labelH;

    MoveWindow(gTreeGrid, margin, y, leftW, gridH, TRUE);
    ResizeTreeGridColumns(hwnd, leftW);

    if (!gRightCollapsed) {
        const int rightX = margin + leftW + splitGap;
        MoveWindow(gChangeList, rightX, y, rightW, gridH, TRUE);
        ResizeChangeListColumns(hwnd, rightW);
    }
    y += gridH + gap;

    MoveWindow(gLogLabel, margin, y, contentW, labelH, TRUE);
    y += labelH;

    MoveWindow(
        gLog,
        margin,
        y,
        contentW,
        std::max(ScaleForDpi(hwnd, 80), h - margin - y),
        TRUE);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        gRule = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"STATIC",
            L"时间规则\r\n"
            L"有直属文件 → 取最新直属文件；无直属文件 → 取最新直属子目录。"
            L"  空目录不修改；忽略 .git 与 Junction / 符号链接。",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 0, 0, hwnd,
            reinterpret_cast<HMENU>(IDC_RULE), nullptr, nullptr);

        gSummary = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"STATIC",
            L"选择：尚未扫描\r\n总大小：—",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 0, 0, hwnd,
            reinterpret_cast<HMENU>(IDC_SUMMARY), nullptr, nullptr);

        gPath = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            0, 0, 0, 0, hwnd,
            reinterpret_cast<HMENU>(IDC_PATH), nullptr, nullptr);

        gBrowse = CreateWindowExW(
            0, L"BUTTON", L"选择文件夹...",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0, hwnd,
            reinterpret_cast<HMENU>(IDC_BROWSE), nullptr, nullptr);

        gDryRun = CreateWindowExW(
            0, L"BUTTON", L"Dry Run 预览",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            0, 0, 0, 0, hwnd,
            reinterpret_cast<HMENU>(IDC_DRYRUN), nullptr, nullptr);

        gApply = CreateWindowExW(
            0, L"BUTTON", L"应用修改",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0, hwnd,
            reinterpret_cast<HMENU>(IDC_APPLY), nullptr, nullptr);

        gCopy = CreateWindowExW(
            0, L"BUTTON", L"复制日志",
            WS_CHILD | WS_VISIBLE | WS_DISABLED | BS_PUSHBUTTON,
            0, 0, 0, 0, hwnd,
            reinterpret_cast<HMENU>(IDC_COPY), nullptr, nullptr);

        gSave = CreateWindowExW(
            0, L"BUTTON", L"保存日志...",
            WS_CHILD | WS_VISIBLE | WS_DISABLED | BS_PUSHBUTTON,
            0, 0, 0, 0, hwnd,
            reinterpret_cast<HMENU>(IDC_SAVE), nullptr, nullptr);

        gStatus = CreateWindowExW(
            0, L"STATIC", L"先选择文件夹，再执行 Dry Run。",
            WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, hwnd,
            reinterpret_cast<HMENU>(IDC_STATUS), nullptr, nullptr);

        gCurrentLabel = CreateWindowExW(
            0, L"STATIC", L"目录树",
            WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, hwnd,
            reinterpret_cast<HMENU>(IDC_CURRENT_LABEL), nullptr, nullptr);

        gTargetLabel = CreateWindowExW(
            0, L"STATIC", L"即将修改（0 项）",
            WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, hwnd,
            reinterpret_cast<HMENU>(IDC_TARGET_LABEL), nullptr, nullptr);

        gRightToggle = CreateWindowExW(
            0, L"BUTTON", L"隐藏变更 →",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0, hwnd,
            reinterpret_cast<HMENU>(IDC_RIGHT_TOGGLE), nullptr, nullptr);

        const DWORD listStyle =
            WS_CHILD | WS_VISIBLE | WS_TABSTOP |
            LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS;

        gTreeGrid = CreateWindowExW(
            WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
            listStyle,
            0, 0, 0, 0, hwnd,
            reinterpret_cast<HMENU>(IDC_TREEGRID), nullptr, nullptr);

        gChangeList = CreateWindowExW(
            WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
            listStyle,
            0, 0, 0, 0, hwnd,
            reinterpret_cast<HMENU>(IDC_CHANGE_LIST), nullptr, nullptr);

        const DWORD listExtendedStyle =
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES |
            LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP;
        ListView_SetExtendedListViewStyle(gTreeGrid, listExtendedStyle);
        ListView_SetExtendedListViewStyle(gChangeList, listExtendedStyle);

        AddListColumn(gTreeGrid, 0, L"文件夹 / 文件");
        AddListColumn(gTreeGrid, 1, L"大小");
        AddListColumn(gTreeGrid, 2, L"数量");
        AddListColumn(gTreeGrid, 3, L"类型");
        AddListColumn(gTreeGrid, 4, L"修改时间");
        AddListColumn(gTreeGrid, 5, L"即将修改");
        AddListColumn(gTreeGrid, 6, L"备注");
        UpdateTreeGridSortIndicator();

        AddListColumn(gChangeList, 0, L"即将修改的目录");
        AddListColumn(gChangeList, 1, L"修改前");
        AddListColumn(gChangeList, 2, L"修改后");
        AddListColumn(gChangeList, 3, L"参考来源");

        gLogLabel = CreateWindowExW(
            0, L"STATIC", L"详细日志",
            WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, hwnd,
            reinterpret_cast<HMENU>(IDC_LOG_LABEL), nullptr, nullptr);

        gLog = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
            ES_MULTILINE | ES_AUTOVSCROLL |
            ES_AUTOHSCROLL | ES_READONLY,
            0, 0, 0, 0, hwnd,
            reinterpret_cast<HMENU>(IDC_LOG), nullptr, nullptr);
        SendMessageW(
            gLog,
            EM_SETLIMITTEXT,
            50u * 1024u * 1024u,
            0);

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
                gExpandedPaths.clear();
                gExcludedPaths.clear();
                gRootNode.reset();
                gVisibleNodes.clear();
                ListView_DeleteAllItems(gTreeGrid);
                ListView_DeleteAllItems(gChangeList);
                SetWindowTextW(
                    gSummary,
                    (L"选择：  " + chosen + L"\r\n总大小：尚未扫描").c_str());
                SetStatus(L"已选择文件夹，请执行 Dry Run。");
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
        case IDC_RIGHT_TOGGLE:
            gRightCollapsed = !gRightCollapsed;
            LayoutControls(hwnd);
            return 0;
        default:
            break;
        }
        break;
    }

    case WM_NOTIFY: {
        auto* header = reinterpret_cast<NMHDR*>(lParam);
        if (header == nullptr) {
            break;
        }

        if (header->hwndFrom == gTreeGrid) {
            if (header->code == NM_CUSTOMDRAW) {
                return HandleTreeGridCustomDraw(
                    reinterpret_cast<NMLVCUSTOMDRAW*>(lParam));
            }

            if (header->code == LVN_COLUMNCLICK) {
                auto* click = reinterpret_cast<NMLISTVIEW*>(lParam);
                if (gSortColumn == click->iSubItem) {
                    gSortAscending = !gSortAscending;
                } else {
                    gSortColumn = click->iSubItem;
                    gSortAscending = true;
                }
                UpdateTreeGridSortIndicator();
                RebuildTreeGrid();
                return 0;
            }

            if (header->code == NM_CLICK) {
                auto* click = reinterpret_cast<NMITEMACTIVATE*>(lParam);
                const int row = click->iItem;
                if (row >= 0 &&
                    click->iSubItem == 0 &&
                    static_cast<size_t>(row) < gVisibleNodes.size()) {
                    FsNode* node = gVisibleNodes[static_cast<size_t>(row)];
                    if (node->isDirectory && !node->children.empty()) {
                        const int toggleX = TreeToggleXForRow(row);
                        const int boxSize = ScaleForDpi(gTreeGrid, 14);
                        if (click->ptAction.x >= toggleX &&
                            click->ptAction.x <= toggleX + boxSize) {
                            ToggleTreeNode(row);
                        }
                    }
                }
                return 0;
            }

            if (header->code == NM_DBLCLK) {
                auto* click = reinterpret_cast<NMITEMACTIVATE*>(lParam);
                if (click->iItem >= 0 &&
                    click->iSubItem == 0 &&
                    static_cast<size_t>(click->iItem) < gVisibleNodes.size()) {
                    ToggleTreeNode(click->iItem);
                }
                return 0;
            }

            if (header->code == NM_RCLICK) {
                POINT pt{};
                GetCursorPos(&pt);

                POINT client = pt;
                ScreenToClient(gTreeGrid, &client);
                LVHITTESTINFO hit{};
                hit.pt = client;
                const int row = ListView_HitTest(gTreeGrid, &hit);

                FsNode* node = nullptr;
                if (row >= 0 &&
                    static_cast<size_t>(row) < gVisibleNodes.size()) {
                    node = gVisibleNodes[static_cast<size_t>(row)];
                    ListView_SetItemState(
                        gTreeGrid,
                        row,
                        LVIS_SELECTED | LVIS_FOCUSED,
                        LVIS_SELECTED | LVIS_FOCUSED);
                }

                const UINT command =
                    ShowTreeContextMenu(hwnd, node, pt);
                if (command == IDM_EXPAND_ALL && node != nullptr) {
                    SetSubtreeExpanded(node, true);
                    RebuildTreeGrid();
                } else if (command == IDM_COLLAPSE_ALL &&
                           node != nullptr) {
                    SetSubtreeExpanded(node, false);
                    RebuildTreeGrid();
                } else if (command == IDM_EXCLUDE &&
                           node != nullptr &&
                           node->isDirectory &&
                           node->parent != nullptr) {
                    AddExclusionPath(node->path);
                    RunOperation(hwnd, fmtfix::Mode::DryRun);
                } else if (command == IDM_INCLUDE &&
                           node != nullptr) {
                    gExcludedPaths.erase(node->path);
                    RunOperation(hwnd, fmtfix::Mode::DryRun);
                } else if (command == IDM_REFRESH) {
                    RunOperation(hwnd, fmtfix::Mode::DryRun);
                }
                return 0;
            }
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
    controls.dwICC = ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES;
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
