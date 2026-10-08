#define UNICODE
#define _UNICODE
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <shellapi.h>

#include <cstdint>
#include <iostream>
#include <vector>

#include "tree_grid_icons.h"

namespace {

bool Expect(bool condition, const char* description) {
    if (!condition) {
        std::cerr << "FAIL: " << description << "\n";
    }
    return condition;
}

// Render the actual GUI helper and the real closed Windows system icon into
// the same 32-bit DIB. Different icon indices alone are not a valid test:
// Windows 11 can return different indices for pixel-identical folder icons.
bool RenderFolderPixels(
    bool open, int size, std::vector<std::uint32_t>& result) {
    HDC screen = GetDC(nullptr);
    if (screen == nullptr) return false;

    HDC dc = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bitmap = dc != nullptr
        ? CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0)
        : nullptr;
    if (bitmap == nullptr || bits == nullptr) {
        if (bitmap != nullptr) DeleteObject(bitmap);
        if (dc != nullptr) DeleteDC(dc);
        ReleaseDC(nullptr, screen);
        return false;
    }

    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
    RECT area{0, 0, size, size};
    FillRect(dc, &area, GetSysColorBrush(COLOR_WINDOW));

    bool ok = true;
    if (open) {
        treegrid::DrawOpenFolderIcon(dc, 0, 0, size);
    } else {
        SHSTOCKICONINFO stock{};
        stock.cbSize = sizeof(stock);
        if (SUCCEEDED(SHGetStockIconInfo(
                SIID_FOLDER,
                SHGSI_ICON | (size > 16 ? SHGSI_LARGEICON : SHGSI_SMALLICON),
                &stock)) && stock.hIcon != nullptr) {
            ok = DrawIconEx(dc, 0, 0, stock.hIcon, size, size,
                            0, nullptr, DI_NORMAL) != FALSE;
            DestroyIcon(stock.hIcon);
        } else {
            ok = false;
        }
    }

    if (ok) {
        GdiFlush();
        auto* pixels = static_cast<std::uint32_t*>(bits);
        result.assign(pixels, pixels + size * size);
    }

    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    return ok;
}

bool CompareIcons(int size) {
    std::vector<std::uint32_t> closed;
    std::vector<std::uint32_t> opened;
    if (!RenderFolderPixels(false, size, closed) ||
        !RenderFolderPixels(true, size, opened)) {
        return Expect(false, "Failed to render open/closed icons");
    }

    size_t differingPixels = 0;
    for (size_t i = 0; i < closed.size(); ++i) {
        if ((closed[i] & 0x00FFFFFFu) != (opened[i] & 0x00FFFFFFu)) {
            ++differingPixels;
        }
    }

    std::cout << "Folder icon " << size << "x" << size
              << ": distinct pixels = " << differingPixels
              << "/" << closed.size() << "\n";
    return Expect(
        differingPixels >= closed.size() / 5,
        "Expanded icon must visibly differ from Windows closed-folder icon");
}

} // namespace

int main() {
    bool ok = true;
    ok &= Expect(
        treegrid::UseOpenFolderIcon(true, true, true),
        "An expanded non-empty folder must use its open icon");
    ok &= Expect(
        !treegrid::UseOpenFolderIcon(true, false, true),
        "A collapsed folder must retain its system icon");
    ok &= Expect(
        !treegrid::UseOpenFolderIcon(false, true, true),
        "A file must retain its own system icon");
    ok &= Expect(
        !treegrid::UseOpenFolderIcon(true, true, false),
        "An empty folder must retain its system icon");

    ok &= CompareIcons(16);
    ok &= CompareIcons(32);

    if (ok) {
        std::cout << "Folder icon visual regression tests passed.\n";
    }
    return ok ? 0 : 1;
}
