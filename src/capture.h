#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vector>
#include <cstdint>
#include "config.h"

struct CapturedRegion {
    std::string label;
    std::vector<uint8_t> pixels;  // 32-bit BGRA, top-down
    int width;
    int height;
};

class Capture {
public:
    // Capture each OcrRegion from the screen and return pixel data.
    std::vector<CapturedRegion> captureRegions(const std::vector<OcrRegion>& regions);
};
