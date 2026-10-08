#define UNICODE
#define _UNICODE
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <windows.h>

#include <chrono>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "tree_grid_logic.h"

namespace {

struct TestNode {
    std::wstring path;
    std::wstring referenceSourceName;
    FILETIME targetTime{};
    bool willChange = false;
    bool hasTarget = false;
    bool isReference = false;
    std::vector<std::unique_ptr<TestNode>> children;
};

TestNode* AddChild(TestNode& parent, const std::wstring& path) {
    auto child = std::make_unique<TestNode>();
    child->path = path;
    TestNode* raw = child.get();
    parent.children.push_back(std::move(child));
    return raw;
}

std::wstring BaseName(const std::wstring& path) {
    const auto pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? path : path.substr(pos + 1);
}

bool Expect(bool condition, const char* description) {
    if (!condition) {
        std::cerr << "FAIL: " << description << "\n";
    }
    return condition;
}

bool TestExclusions() {
    bool ok = true;
    std::set<std::wstring> exclusions{
        L"C:\\Root\\Child",
        L"C:\\Root\\Child\\Keep",
        L"C:\\Root\\Children",
        L"C:\\Root\\Other",
        L"D:\\Elsewhere"
    };
    treegrid::RetainProperDescendantExclusions(
        exclusions, L"c:\\ROOT\\child\\");
    ok &= Expect(
        exclusions.size() == 1 &&
            exclusions.count(L"C:\\Root\\Child\\Keep") == 1,
        "Selected root must not stay excluded, while proper descendants remain");

    exclusions = {L"C:\\Root\\Child"};
    treegrid::RetainProperDescendantExclusions(
        exclusions, L"c:\\root\\CHILD");
    ok &= Expect(exclusions.empty(),
                 "Case-insensitive root equality must drop stale exclusion");

    exclusions = {
        L"C:\\Root\\Children",
        L"C:\\Root\\Child\\Nested",
        L"C:\\Root\\Child"
    };
    treegrid::RetainProperDescendantExclusions(
        exclusions, L"C:\\Root");
    ok &= Expect(exclusions.size() == 3,
                 "Changing to a parent keeps all proper descendant exclusions");
    return ok;
}

bool TestMatching() {
    bool ok = true;
    TestNode root;
    root.path = L"C:\\Test";
    auto* directory = AddChild(root, L"C:\\Test\\Dir");
    auto* file = AddChild(*directory, L"C:\\Test\\Dir\\source.txt");
    auto* untouched = AddChild(*directory, L"C:\\Test\\Dir\\other.txt");

    fmtfix::Result result;
    fmtfix::LogEntry entry;
    entry.directory = directory->path;
    entry.status = fmtfix::EntryStatus::WouldChange;
    entry.hasTarget = true;
    entry.target.dwLowDateTime = 123456;
    entry.sourcePath = file->path;
    result.entries.push_back(entry);

    treegrid::ApplyResultToNodes(&root, result, BaseName);
    ok &= Expect(directory->willChange && directory->hasTarget &&
                     directory->targetTime.dwLowDateTime == 123456,
                 "Target/status must be copied to matching directory");
    ok &= Expect(directory->referenceSourceName == L"source.txt",
                 "Source display name must remain unchanged");
    ok &= Expect(file->isReference && !untouched->isReference,
                 "Only referenced source node should be blue");
    ok &= Expect(!root.willChange && !file->willChange,
                 "Non-directory nodes should not acquire directory status");

    fmtfix::Result changed;
    auto changedEntry = entry;
    changedEntry.status = fmtfix::EntryStatus::Changed;
    changed.entries.push_back(changedEntry);
    TestNode applied;
    applied.path = directory->path;
    treegrid::ApplyResultToNodes(&applied, changed, BaseName);
    ok &= Expect(applied.willChange, "Apply must mark Changed status");

    fmtfix::Result unchanged;
    auto unchangedEntry = entry;
    unchangedEntry.status = fmtfix::EntryStatus::Unchanged;
    unchanged.entries.push_back(unchangedEntry);
    TestNode stable;
    stable.path = directory->path;
    treegrid::ApplyResultToNodes(&stable, unchanged, BaseName);
    ok &= Expect(!stable.willChange && stable.hasTarget,
                 "Unchanged results can retain a target without highlighting");

    return ok;
}

bool TestLargeTree() {
    // 10,000 directory entries and 100,000 files. The former nested scan
    // required about 1.1 billion path comparisons for this fixture.
    constexpr int kDirectories = 10000;
    constexpr int kFilesPerDirectory = 10;

    TestNode root;
    root.path = L"C:\\Synthetic";
    fmtfix::Result result;
    result.entries.reserve(kDirectories);
    root.children.reserve(kDirectories);

    for (int d = 0; d < kDirectories; ++d) {
        const std::wstring dirPath =
            root.path + L"\\dir" + std::to_wstring(d);
        TestNode* directory = AddChild(root, dirPath);
        directory->children.reserve(kFilesPerDirectory);

        fmtfix::LogEntry entry;
        entry.directory = dirPath;
        entry.sourcePath = dirPath + L"\\file0.txt";
        entry.status = fmtfix::EntryStatus::WouldChange;
        entry.hasTarget = true;
        entry.target.dwLowDateTime = static_cast<DWORD>(d + 1);
        result.entries.push_back(std::move(entry));

        for (int f = 0; f < kFilesPerDirectory; ++f) {
            AddChild(*directory,
                     dirPath + L"\\file" + std::to_wstring(f) + L".txt");
        }
    }

    const auto start = std::chrono::steady_clock::now();
    treegrid::ApplyResultToNodes(&root, result, BaseName);
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    bool ok = Expect(elapsedMs < 15000,
                     "110k-node result mapping must finish within 15 seconds");
    size_t changed = 0;
    size_t referenceFiles = 0;
    for (const auto& directory : root.children) {
        changed += directory->willChange ? 1 : 0;
        for (const auto& file : directory->children) {
            referenceFiles += file->isReference ? 1 : 0;
        }
    }
    ok &= Expect(changed == kDirectories &&
                     referenceFiles == kDirectories,
                 "All 10k directory targets and reference files must match");
    std::cout << "Tree-Grid indexed mapping: " << elapsedMs
              << " ms for 110001 nodes / 10000 entries\n";
    return ok;
}

} // namespace

int main() {
    bool ok = TestExclusions();
    ok &= TestMatching();
    ok &= TestLargeTree();
    if (!ok) {
        return 1;
    }
    std::cout << "All Tree-Grid regression tests passed.\n";
    return 0;
}
