#include "capture.h"

std::vector<CapturedRegion> Capture::captureRegions(const std::vector<OcrRegion>& regions) {
    std::vector<CapturedRegion> results;
    results.reserve(regions.size());

    HDC screenDC = GetDC(nullptr);

    for (const auto& region : regions) {
        if (region.w <= 0 || region.h <= 0) continue;

        HDC    memDC     = CreateCompatibleDC(screenDC);
        HBITMAP bitmap   = CreateCompatibleBitmap(screenDC, region.w, region.h);
        HGDIOBJ oldBmp   = SelectObject(memDC, bitmap);

        BitBlt(memDC, 0, 0, region.w, region.h,
               screenDC, region.x, region.y, SRCCOPY);

        BITMAPINFOHEADER bi = {};
        bi.biSize        = sizeof(bi);
        bi.biWidth       = region.w;
        bi.biHeight      = -region.h;   // negative → top-down
        bi.biPlanes      = 1;
        bi.biBitCount    = 32;
        bi.biCompression = BI_RGB;

        CapturedRegion cap;
        cap.label  = region.label;
        cap.width  = region.w;
        cap.height = region.h;
        cap.pixels.resize(static_cast<size_t>(region.w) * region.h * 4);

        GetDIBits(memDC, bitmap, 0, static_cast<UINT>(region.h),
                  cap.pixels.data(),
                  reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);

        results.push_back(std::move(cap));

        SelectObject(memDC, oldBmp);
        DeleteObject(bitmap);
        DeleteDC(memDC);
    }

    ReleaseDC(nullptr, screenDC);
    return results;
}
