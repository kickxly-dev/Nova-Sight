#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>    // GET_X_LPARAM / GET_Y_LPARAM
#include <tlhelp32.h>
#include <dwmapi.h>
#include "overlay.h"
#include <thread>
#include <chrono>
#include <string>
#include <algorithm>

// ── Colour palette ──────────────────────────────────────────────────────────
//   Background  #0a0a0a  ~90 % opaque
//   Accent      #00aaff  full opaque
//   Text        #e6e6e6
//   Dim         #7a7a7a
//   Border      #1a2a3a

static constexpr D2D1_COLOR_F COL_BG     = {0.039f, 0.039f, 0.039f, 0.90f};
static constexpr D2D1_COLOR_F COL_ACCENT = {0.000f, 0.667f, 1.000f, 1.00f};
static constexpr D2D1_COLOR_F COL_TEXT   = {0.902f, 0.902f, 0.902f, 1.00f};
static constexpr D2D1_COLOR_F COL_DIM    = {0.478f, 0.478f, 0.478f, 1.00f};
static constexpr D2D1_COLOR_F COL_BORDER = {0.102f, 0.165f, 0.227f, 1.00f};

static constexpr wchar_t CLASS_NAME[] = L"NovaSightOverlay";
static constexpr UINT_PTR RENDER_TIMER_ID = 1;
static constexpr UINT     RENDER_INTERVAL_MS = 62;  // ~16 fps

// ── Constructor / Destructor ────────────────────────────────────────────────

Overlay::Overlay(HINSTANCE hInstance, Config& config)
    : m_hInstance(hInstance), m_config(config) {
    if (!config.getGroqApiKey().empty())
        m_ai = new AiCoach(config.getGroqApiKey(), config.getGroqModel());
}

Overlay::~Overlay() {
    m_running = false;

    auto safeRelease = [](auto*& p) { if (p) { p->Release(); p = nullptr; } };
    safeRelease(m_brushBorder);
    safeRelease(m_brushDim);
    safeRelease(m_brushText);
    safeRelease(m_brushAccent);
    safeRelease(m_brushBg);
    safeRelease(m_titleFormat);
    safeRelease(m_textFormat);
    safeRelease(m_renderTarget);
    safeRelease(m_dwFactory);
    safeRelease(m_d2dFactory);

    delete m_ai;
}

// ── init() ──────────────────────────────────────────────────────────────────

bool Overlay::init() {
    WNDCLASSEXW wc   = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = m_hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    const int screenW = GetSystemMetrics(SM_CXSCREEN);
    const int screenH = GetSystemMetrics(SM_CYSCREEN);

    // WS_EX_LAYERED  — per-pixel alpha via UpdateLayeredWindow
    // WS_EX_TOPMOST  — always on top of other windows
    // WS_EX_NOACTIVATE — never steals keyboard focus from the game
    // WS_EX_TOOLWINDOW — hidden from Alt-Tab switcher
    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        CLASS_NAME, L"Nova-Sight",
        WS_POPUP,
        0, 0, screenW, screenH,
        nullptr, nullptr, m_hInstance, this
    );
    if (!m_hwnd) return false;

    if (!initD2D()) return false;

    m_ocr.init();  // failure is non-fatal; extract() returns empty map

    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);

    startWorker();
    SetTimer(m_hwnd, RENDER_TIMER_ID, RENDER_INTERVAL_MS, nullptr);

    return true;
}

// ── Direct2D initialisation ─────────────────────────────────────────────────

bool Overlay::initD2D() {
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_d2dFactory);
    if (FAILED(hr)) return false;

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                              __uuidof(IDWriteFactory),
                              reinterpret_cast<IUnknown**>(&m_dwFactory));
    if (FAILED(hr)) return false;

    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );
    hr = m_d2dFactory->CreateDCRenderTarget(&rtProps, &m_renderTarget);
    if (FAILED(hr)) return false;

    // Text formats — prefer DM Mono, fall back to Consolas
    m_dwFactory->CreateTextFormat(
        L"DM Mono", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        12.0f, L"en-us", &m_textFormat);
    if (!m_textFormat)
        m_dwFactory->CreateTextFormat(
            L"Consolas", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            12.0f, L"en-us", &m_textFormat);

    m_dwFactory->CreateTextFormat(
        L"DM Mono", nullptr,
        DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        14.0f, L"en-us", &m_titleFormat);
    if (!m_titleFormat)
        m_dwFactory->CreateTextFormat(
            L"Consolas", nullptr,
            DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            14.0f, L"en-us", &m_titleFormat);

    m_renderTarget->CreateSolidColorBrush(COL_BG,     &m_brushBg);
    m_renderTarget->CreateSolidColorBrush(COL_ACCENT, &m_brushAccent);
    m_renderTarget->CreateSolidColorBrush(COL_TEXT,   &m_brushText);
    m_renderTarget->CreateSolidColorBrush(COL_DIM,    &m_brushDim);
    m_renderTarget->CreateSolidColorBrush(COL_BORDER, &m_brushBorder);

    return m_brushBg && m_brushAccent && m_brushText && m_brushDim && m_brushBorder
        && m_textFormat && m_titleFormat;
}

// ── Message loop ─────────────────────────────────────────────────────────────

int Overlay::run() {
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    m_running = false;
    return static_cast<int>(msg.wParam);
}

// ── Rendering ────────────────────────────────────────────────────────────────

void Overlay::render() {
    if (!m_renderTarget) return;

    const int screenW = GetSystemMetrics(SM_CXSCREEN);
    const int screenH = GetSystemMetrics(SM_CYSCREEN);

    // Create a DIB section that Direct2D will draw into
    HDC screenDC = GetDC(nullptr);
    HDC memDC    = CreateCompatibleDC(screenDC);

    BITMAPINFO bmi         = {};
    bmi.bmiHeader.biSize   = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth  = screenW;
    bmi.bmiHeader.biHeight = -screenH;   // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void*   bits    = nullptr;
    HBITMAP hBitmap = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ oldBmp  = SelectObject(memDC, hBitmap);

    RECT rcBind = {0, 0, screenW, screenH};
    m_renderTarget->BindDC(memDC, &rcBind);

    m_renderTarget->BeginDraw();
    m_renderTarget->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));  // fully transparent

    drawPanel();

    m_renderTarget->EndDraw();

    // Compose the layered window
    POINT ptSrc  = {0, 0};
    POINT ptDst  = {0, 0};
    SIZE  szWnd  = {screenW, screenH};
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(m_hwnd, screenDC, &ptDst, &szWnd, memDC, &ptSrc, 0, &blend, ULW_ALPHA);

    SelectObject(memDC, oldBmp);
    DeleteObject(hBitmap);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);
}

// ── Panel drawing ─────────────────────────────────────────────────────────────

void Overlay::drawPanel() {
    std::lock_guard<std::mutex> lock(m_dataMutex);

    const float x   = static_cast<float>(m_panelX);
    const float y   = static_cast<float>(m_panelY);
    const float w   = static_cast<float>(m_panelW);
    const float pad = 10.0f;
    const float lh  = 18.0f;  // line height

    // Compute panel height dynamically
    int   numStats  = static_cast<int>(m_stats.size());
    float panelH    = pad
                    + 22.0f          // title row
                    + pad * 0.5f     // gap
                    + lh             // game name
                    + lh * 0.4f      // separator gap
                    + lh * numStats  // stat rows
                    + (numStats > 0 ? lh * 0.4f : 0.0f)
                    + lh             // "TIP:" label
                    + lh * 2.0f      // tip text (up to 2 lines)
                    + pad;
    m_panelH = static_cast<int>(panelH);

    // ── Background ──────────────────────────────────────────────────────────
    D2D1_ROUNDED_RECT bgRect = {
        {x, y, x + w, y + panelH},
        8.0f, 8.0f
    };
    m_renderTarget->FillRoundedRectangle(bgRect, m_brushBg);
    m_renderTarget->DrawRoundedRectangle(bgRect, m_brushBorder, 1.0f);

    // ── Accent bar (top edge) ────────────────────────────────────────────────
    D2D1_ROUNDED_RECT accentBar = {
        {x + 1.0f, y + 1.0f, x + w - 1.0f, y + 4.0f},
        7.0f, 7.0f
    };
    m_renderTarget->FillRoundedRectangle(accentBar, m_brushAccent);

    float cy = y + pad;

    // ── Title ────────────────────────────────────────────────────────────────
    {
        static const wchar_t kTitle[] = L"NOVA-SIGHT";
        D2D1_RECT_F r = {x + pad, cy, x + w - pad, cy + 22.0f};
        m_renderTarget->DrawText(kTitle, ARRAYSIZE(kTitle) - 1, m_titleFormat, r, m_brushAccent);
    }
    cy += 22.0f + pad * 0.5f;

    // ── Game name ────────────────────────────────────────────────────────────
    {
        std::wstring line = m_currentGame.empty()
            ? L"No game detected"
            : std::wstring(m_currentGame.begin(), m_currentGame.end());
        D2D1_RECT_F r = {x + pad, cy, x + w - pad, cy + lh};
        auto* brush = m_currentGame.empty() ? m_brushDim : m_brushText;
        m_renderTarget->DrawText(line.c_str(), static_cast<UINT32>(line.size()),
                                  m_textFormat, r, brush);
    }
    cy += lh;

    // ── Separator ────────────────────────────────────────────────────────────
    m_renderTarget->DrawLine(
        {x + pad, cy + lh * 0.2f},
        {x + w - pad, cy + lh * 0.2f},
        m_brushBorder, 1.0f);
    cy += lh * 0.4f;

    // ── Stats ────────────────────────────────────────────────────────────────
    for (const auto& [label, value] : m_stats) {
        std::wstring wlabel(label.begin(), label.end());
        std::wstring wvalue(value.begin(), value.end());
        std::wstring line = wlabel + L": " + wvalue;

        D2D1_RECT_F rl = {x + pad,           cy, x + w * 0.5f, cy + lh};
        D2D1_RECT_F rv = {x + w * 0.5f, cy, x + w - pad,  cy + lh};

        // Label in dim colour, value in text colour
        m_renderTarget->DrawText(wlabel.c_str(),  static_cast<UINT32>(wlabel.size()),
                                  m_textFormat, rl, m_brushDim);
        m_renderTarget->DrawText(wvalue.c_str(),  static_cast<UINT32>(wvalue.size()),
                                  m_textFormat, rv, m_brushText);
        cy += lh;
    }

    if (!m_stats.empty()) {
        m_renderTarget->DrawLine(
            {x + pad, cy + lh * 0.2f},
            {x + w - pad, cy + lh * 0.2f},
            m_brushBorder, 1.0f);
        cy += lh * 0.4f;
    }

    // ── AI tip ───────────────────────────────────────────────────────────────
    {
        static const wchar_t kTipLabel[] = L"TIP";
        D2D1_RECT_F rl = {x + pad, cy, x + pad + 30.0f, cy + lh};
        m_renderTarget->DrawText(kTipLabel, ARRAYSIZE(kTipLabel) - 1,
                                  m_textFormat, rl, m_brushAccent);
        cy += lh;

        std::wstring tip(m_aiTip.begin(), m_aiTip.end());
        D2D1_RECT_F r = {x + pad, cy, x + w - pad, y + panelH - pad};
        m_renderTarget->DrawText(tip.c_str(), static_cast<UINT32>(tip.size()),
                                  m_textFormat, r, m_brushText);
    }
}

// ── Hit testing ──────────────────────────────────────────────────────────────

bool Overlay::isInPanel(int sx, int sy) const {
    return sx >= m_panelX && sx <= m_panelX + m_panelW
        && sy >= m_panelY && sy <= m_panelY + m_panelH;
}

// ── Window procedure ─────────────────────────────────────────────────────────

LRESULT CALLBACK Overlay::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self = reinterpret_cast<Overlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self) return self->handleMessage(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT Overlay::handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    // ── Click-through / hit-test ─────────────────────────────────────────────
    case WM_NCHITTEST: {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        return isInPanel(pt.x, pt.y) ? HTCLIENT : HTTRANSPARENT;
    }

    // ── Dragging ─────────────────────────────────────────────────────────────
    case WM_LBUTTONDOWN: {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (isInPanel(pt.x, pt.y)) {
            m_dragging     = true;
            m_dragOffset   = {pt.x - m_panelX, pt.y - m_panelY};
            SetCapture(hwnd);
        }
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (m_dragging) {
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            const int screenW = GetSystemMetrics(SM_CXSCREEN);
            const int screenH = GetSystemMetrics(SM_CYSCREEN);
            m_panelX = std::clamp(pt.x - m_dragOffset.x, 0, screenW - m_panelW);
            m_panelY = std::clamp(pt.y - m_dragOffset.y, 0, screenH - m_panelH);
            render();
        }
        return 0;
    }
    case WM_LBUTTONUP:
        if (m_dragging) {
            m_dragging = false;
            ReleaseCapture();
        }
        return 0;

    // ── Right-click context menu ─────────────────────────────────────────────
    case WM_RBUTTONUP: {
        POINT pt;
        GetCursorPos(&pt);
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, 1, L"Exit Nova-Sight");
        int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                  pt.x, pt.y, 0, hwnd, nullptr);
        DestroyMenu(menu);
        if (cmd == 1) PostQuitMessage(0);
        return 0;
    }

    // ── Render timer ─────────────────────────────────────────────────────────
    case WM_TIMER:
        if (wParam == RENDER_TIMER_ID) render();
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, RENDER_TIMER_ID);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ── Worker thread (game detection + OCR + AI) ────────────────────────────────

static std::string detectRunningGame(const Config& config) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return {};

    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);

    std::string found;
    if (Process32FirstW(snap, &pe)) {
        do {
            // Convert wide process name to narrow for lookup
            int len = WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1,
                                           nullptr, 0, nullptr, nullptr);
            std::string name(len - 1, '\0');
            WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1,
                                 name.data(), len, nullptr, nullptr);

            if (config.findGame(name)) {
                found = name;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

void Overlay::startWorker() {
    std::thread([this]() { workerLoop(); }).detach();
}

void Overlay::workerLoop() {
    using namespace std::chrono_literals;

    while (m_running) {
        // ── 1. Game detection ────────────────────────────────────────────────
        const std::string process   = detectRunningGame(m_config);
        const GameConfig* gameCfg   = process.empty() ? nullptr
                                                       : m_config.findGame(process);

        {
            std::lock_guard<std::mutex> lk(m_dataMutex);
            m_currentGame = gameCfg ? gameCfg->name : "";
            if (!gameCfg) {
                m_stats.clear();
                m_aiTip = "Waiting for a supported game...";
            }
        }

        if (gameCfg && !gameCfg->regions.empty()) {
            // ── 2. Screen capture + OCR ──────────────────────────────────────
            auto captured = m_capture.captureRegions(gameCfg->regions);
            auto stats    = m_ocr.extract(captured);

            {
                std::lock_guard<std::mutex> lk(m_dataMutex);
                m_stats = stats;
            }

            // ── 3. AI coaching tip (throttled — one request at a time) ───────
            if (m_ai && !m_aiPending && !stats.empty()) {
                m_aiPending = true;
                std::string name = gameCfg->name;
                m_ai->requestTip(name, stats, [this](const std::string& tip) {
                    std::lock_guard<std::mutex> lk(m_dataMutex);
                    m_aiTip    = tip;
                    m_aiPending = false;
                });
            } else if (!m_ai) {
                std::lock_guard<std::mutex> lk(m_dataMutex);
                m_aiTip = "Set groq_api_key in games.json for AI tips.";
            }
        }

        std::this_thread::sleep_for(1500ms);
    }
}
