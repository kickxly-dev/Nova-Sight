#include "ocr.h"
#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>

struct OcrEngine::Impl {
    tesseract::TessBaseAPI api;
    bool initialized{false};
};

OcrEngine::OcrEngine() : m_impl(new Impl()) {}

OcrEngine::~OcrEngine() {
    if (m_impl->initialized)
        m_impl->api.End();
    delete m_impl;
}

bool OcrEngine::init(const std::string& tessDataPath) {
    int rc = m_impl->api.Init(tessDataPath.c_str(), "eng",
                               tesseract::OcrEngineMode::OEM_LSTM_ONLY);
    if (rc != 0) return false;

    m_impl->api.SetPageSegMode(tesseract::PageSegMode::PSM_SINGLE_LINE);
    // Whitelist digits, letters, common stat separators
    m_impl->api.SetVariable("tessedit_char_whitelist",
                             "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz/:-. ");
    m_impl->initialized = true;
    return true;
}

std::map<std::string, std::string> OcrEngine::extract(const std::vector<CapturedRegion>& regions) {
    std::map<std::string, std::string> results;
    if (!m_impl->initialized) return results;

    for (const auto& region : regions) {
        if (region.pixels.empty()) continue;

        // Convert BGRA → RGB (Tesseract expects RGB)
        const int npix = region.width * region.height;
        std::vector<uint8_t> rgb(npix * 3);
        for (int i = 0; i < npix; ++i) {
            rgb[i * 3 + 0] = region.pixels[i * 4 + 2]; // R
            rgb[i * 3 + 1] = region.pixels[i * 4 + 1]; // G
            rgb[i * 3 + 2] = region.pixels[i * 4 + 0]; // B
        }

        m_impl->api.SetImage(rgb.data(), region.width, region.height,
                              3, region.width * 3);

        char* raw = m_impl->api.GetUTF8Text();
        if (raw) {
            std::string s(raw);
            delete[] raw;

            // Trim trailing whitespace / newlines
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
                s.pop_back();

            if (!s.empty())
                results[region.label] = std::move(s);
        }
        m_impl->api.Clear();
    }
    return results;
}
