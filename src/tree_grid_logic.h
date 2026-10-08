#pragma once

#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "mtime_core.h"

namespace treegrid {

// IsPathInside also returns true for equal paths. An excluded directory must
// stop being excluded when it becomes the selected root; keep only children.
inline void RetainProperDescendantExclusions(
    std::set<std::wstring>& exclusions, const std::wstring& root) {
    for (auto it = exclusions.begin(); it != exclusions.end();) {
        if (!fmtfix::IsPathInside(*it, root) ||
            fmtfix::IsPathInside(root, *it)) {
            it = exclusions.erase(it);
        } else {
            ++it;
        }
    }
}

template <typename Node, typename DisplayName>
void ApplyIndexedResultToNodes(
    Node* node,
    const std::unordered_map<std::wstring, const fmtfix::LogEntry*>& entries,
    const std::unordered_set<std::wstring>& sources,
    const DisplayName& displayName) {
    if (node == nullptr) {
        return;
    }

    const auto found = entries.find(node->path);
    if (found != entries.end()) {
        const auto& entry = *found->second;
        node->willChange =
            entry.status == fmtfix::EntryStatus::WouldChange ||
            entry.status == fmtfix::EntryStatus::Changed;
        node->hasTarget = entry.hasTarget;
        if (entry.hasTarget) {
            node->targetTime = entry.target;
        }
        if (!entry.sourcePath.empty()) {
            node->referenceSourceName = displayName(entry.sourcePath);
        }
    }

    if (sources.find(node->path) != sources.end()) {
        node->isReference = true;
    }

    for (auto& child : node->children) {
        ApplyIndexedResultToNodes(child.get(), entries, sources, displayName);
    }
}

// Index result paths only once, not once per filesystem node. This retains the
// original case-sensitive equality used for Tree-Grid result matching.
template <typename Node, typename DisplayName>
void ApplyResultToNodes(
    Node* root, const fmtfix::Result& result, DisplayName displayName) {
    if (root == nullptr) {
        return;
    }

    std::unordered_map<std::wstring, const fmtfix::LogEntry*> entries;
    std::unordered_set<std::wstring> sources;
    entries.reserve(result.entries.size());
    sources.reserve(result.entries.size());

    for (const auto& entry : result.entries) {
        entries[entry.directory] = &entry;
        if (!entry.sourcePath.empty()) {
            sources.insert(entry.sourcePath);
        }
    }

    ApplyIndexedResultToNodes(root, entries, sources, displayName);
}

} // namespace treegrid
