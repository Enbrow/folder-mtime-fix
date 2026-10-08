#pragma once

#include <windows.h>

namespace treegrid {

// Only a genuinely expanded directory with visible children needs an open icon.
inline bool UseOpenFolderIcon(
    bool isDirectory, bool expanded, bool hasChildren) {
    return isDirectory && expanded && hasChildren;
}

// The stock SIID_FOLDEROPEN small/large icons are pixel-identical to
// SIID_FOLDER on some Windows 11 installations. Draw a distinct open folder
// directly, so expanding the tree changes its appearance on every machine.
// The 16x16 geometry scales with the existing DPI-aware Tree-Grid icon size.
inline void DrawOpenFolderIcon(HDC dc, int x, int y, int size) {
    if (dc == nullptr || size < 8) {
        return;
    }

    const int saved = SaveDC(dc);
    if (saved == 0) {
        return;
    }

    const auto p = [=](int px, int py) -> POINT {
        return POINT{x + MulDiv(px, size, 16),
                     y + MulDiv(py, size, 16)};
    };
    const int stroke = size >= 32 ? 2 : 1;
    HPEN outline = CreatePen(PS_SOLID, stroke, RGB(169, 110, 22));
    HPEN flapLine = CreatePen(PS_SOLID, stroke, RGB(191, 127, 32));
    HBRUSH back = CreateSolidBrush(RGB(240, 167, 36));
    HBRUSH inside = CreateSolidBrush(RGB(255, 242, 193));
    HBRUSH flap = CreateSolidBrush(RGB(255, 203, 77));

    if (outline != nullptr && flapLine != nullptr &&
        back != nullptr && inside != nullptr && flap != nullptr) {
        SelectObject(dc, outline);
        SelectObject(dc, back);
        POINT backShape[] = {
            p(2, 3), p(7, 3), p(9, 5),
            p(13, 5), p(13, 12), p(2, 12)};
        Polygon(dc, backShape, 6);

        SelectObject(dc, inside);
        POINT paperShape[] = {
            p(4, 6), p(13, 6), p(12, 11), p(3, 11)};
        Polygon(dc, paperShape, 4);

        SelectObject(dc, flapLine);
        SelectObject(dc, flap);
        POINT openFlap[] = {
            p(3, 8), p(15, 8), p(12, 14), p(1, 14)};
        Polygon(dc, openFlap, 4);
    }

    // Restore the original DC objects before deleting the temporary brushes.
    RestoreDC(dc, saved);
    if (outline != nullptr) DeleteObject(outline);
    if (flapLine != nullptr) DeleteObject(flapLine);
    if (back != nullptr) DeleteObject(back);
    if (inside != nullptr) DeleteObject(inside);
    if (flap != nullptr) DeleteObject(flap);
}

} // namespace treegrid
