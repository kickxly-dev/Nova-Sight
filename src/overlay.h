#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <map>
#include <mutex>
#include <atomic>
#include "config.h"
#include "capture.h"
#include "ocr.h"
#include "ai.h"

// Full-screen layered window drawn with Direct2D.
// Click-through everywhere except the draggable Nova-Sight panel.
class Overlay {
public:
    Overlay(HINSTANCE hInstance, Config& config);
    ~Overlay();

    bool init();
    int  run();

private:
    // Win32 plumbing
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // Rendering
    bool initD2D();
    void render();
    void drawPanel();

    // Worker thread (OCR + game detection + AI requests)
    void startWorker();
    void workerLoop();

    // Hit-test helper
    bool isInPanel(int screenX, int screenY) const;

    // ── Win32 handles ──────────────────────────────────────────────────────────
    HINSTANCE m_hInstance;
    HWND      m_hwnd{nullptr};

    // ── Direct2D / DirectWrite ─────────────────────────────────────────────────
    ID2D1Factory*          m_d2dFactory  {nullptr};
    ID2D1DCRenderTarget*   m_renderTarget{nullptr};
    IDWriteFactory*        m_dwFactory   {nullptr};
    IDWriteTextFormat*     m_textFormat  {nullptr};   // 12pt body
    IDWriteTextFormat*     m_titleFormat {nullptr};   // 14pt bold title
    ID2D1SolidColorBrush*  m_brushBg     {nullptr};
    ID2D1SolidColorBrush*  m_brushAccent {nullptr};
    ID2D1SolidColorBrush*  m_brushText   {nullptr};
    ID2D1SolidColorBrush*  m_brushDim    {nullptr};
    ID2D1SolidColorBrush*  m_brushBorder {nullptr};

    // ── Sub-systems ────────────────────────────────────────────────────────────
    Config&    m_config;
    Capture    m_capture;
    OcrEngine  m_ocr;
    AiCoach*   m_ai{nullptr};

    // ── Panel geometry (screen-space, draggable) ───────────────────────────────
    int    m_panelX{20};
    int    m_panelY{20};
    int    m_panelW{300};
    int    m_panelH{220};  // updated each frame by drawPanel()
    bool   m_dragging{false};
    POINT  m_dragOffset{};

    // ── Shared state (main thread reads, worker writes — protected by mutex) ───
    std::mutex m_dataMutex;
    std::string                       m_currentGame;
    std::map<std::string, std::string> m_stats;
    std::string                       m_aiTip{"Starting up..."};

    // ── Threading ──────────────────────────────────────────────────────────────
    std::atomic<bool> m_running  {true};
    std::atomic<bool> m_aiPending{false};
};
