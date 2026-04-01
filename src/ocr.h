#pragma once
#include <string>
#include <map>
#include <vector>
#include "capture.h"

// Thin wrapper around Tesseract. Each call to extract() is synchronous and
// should be called from a worker thread — never the render thread.
class OcrEngine {
public:
    OcrEngine();
    ~OcrEngine();

    // tessDataPath: directory containing tessdata/ (e.g. "." resolves to ./tessdata/).
    bool init(const std::string& tessDataPath = ".");

    // Returns label → recognised text for every region.
    std::map<std::string, std::string> extract(const std::vector<CapturedRegion>& regions);

private:
    struct Impl;
    Impl* m_impl{nullptr};
};
