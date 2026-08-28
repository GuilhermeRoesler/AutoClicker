/**
 * Auto Clicker M3 Pro — versão C++ (secundária)
 *
 * UI: Win32 + GDI+ (tema escuro estilo M3), sem Qt/WebView2.
 * Motor: mesmo modelo do python/main.py (duplo-clique + hold + anti-feedback).
 */

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>

#include "resource.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#ifdef _MSC_VER
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "msimg32.lib")
#endif

using namespace Gdiplus;

// ---------------------------------------------------------------------------
// Constantes
// ---------------------------------------------------------------------------

enum MouseBtn : int {
    BTN_LEFT = 0, BTN_RIGHT, BTN_MIDDLE, BTN_X1, BTN_X2, BTN_COUNT
};

static constexpr double kDoubleClickThreshold = 0.3;
static constexpr UINT_PTR kTimerOverlay = 1;
static constexpr UINT_PTR kTimerRipple = 2;
static constexpr UINT WM_ENGINE_STATE = WM_APP + 1;

static constexpr int kWinW = 500;
static constexpr int kWinH = 750;

namespace Theme {
static const Color Bg(255, 18, 18, 20);
static const Color Surface(255, 32, 32, 36);
static const Color Card(255, 42, 42, 48);
static const Color CardBorder(255, 58, 58, 66);
static const Color Text(255, 236, 236, 240);
static const Color TextDim(255, 150, 150, 158);
static const Color Accent(255, 41, 121, 255);      // blue M3-ish
static const Color AccentSoft(255, 30, 80, 180);
static const Color Green(255, 0, 230, 118);
static const Color Red(255, 239, 83, 80);
static const Color Track(255, 60, 60, 68);
static const Color Canvas(255, 14, 14, 16);
static const Color TabIdle(255, 48, 48, 54);
}

// ---------------------------------------------------------------------------
// Util
// ---------------------------------------------------------------------------

static double NowSec() {
    using clock = std::chrono::steady_clock;
    static const auto start = clock::now();
    return std::chrono::duration<double>(clock::now() - start).count();
}

static double WallSec() {
    using clock = std::chrono::system_clock;
    return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

static std::mt19937& Rng() {
    static thread_local std::mt19937 gen{std::random_device{}()};
    return gen;
}

static double RandUniform(double a, double b) {
    std::uniform_real_distribution<double> dist(a, b);
    return dist(Rng());
}

static void SleepSec(double seconds) {
    if (seconds <= 0.0) return;
    Sleep(static_cast<DWORD>(seconds * 1000.0));
}

static bool PtIn(const RectF& r, int x, int y) {
    return x >= r.X && y >= r.Y && x < r.X + r.Width && y < r.Y + r.Height;
}

static void RoundRectPath(GraphicsPath& path, RectF r, float radius) {
    const float d = radius * 2.f;
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    path.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    path.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    path.CloseFigure();
}

static void FillRoundRect(Graphics& g, const Brush& brush, RectF r, float radius) {
    GraphicsPath path;
    RoundRectPath(path, r, radius);
    g.FillPath(&brush, &path);
}

static void DrawRoundRect(Graphics& g, const Pen& pen, RectF r, float radius) {
    GraphicsPath path;
    RoundRectPath(path, r, radius);
    g.DrawPath(&pen, &path);
}

static void DrawStringLeft(Graphics& g, const wchar_t* text, Font& font, const Brush& brush, RectF r) {
    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentNear);
    fmt.SetLineAlignment(StringAlignmentCenter);
    fmt.SetTrimming(StringTrimmingEllipsisCharacter);
    g.DrawString(text, -1, &font, r, &fmt, &brush);
}

static void DrawStringCenter(Graphics& g, const wchar_t* text, Font& font, const Brush& brush, RectF r) {
    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentCenter);
    fmt.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(text, -1, &font, r, &fmt, &brush);
}

// ---------------------------------------------------------------------------
// ClickEngine (inalterado em comportamento)
// ---------------------------------------------------------------------------

struct ClickEngine {
    std::atomic<bool> running{true};
    std::atomic<bool> master_enabled{true};
    std::atomic<int> cps{12};
    std::atomic<bool> humanized{false};

    bool active_triggers[BTN_COUNT] = {true, false, false, false, false};
    std::atomic<bool> clicking_state[BTN_COUNT];
    std::atomic<bool> ignore_next_press[BTN_COUNT];
    std::atomic<bool> ignore_next_release[BTN_COUNT];
    double last_click_time[BTN_COUNT] = {};

    std::mutex history_mu;
    std::deque<double> click_history;

    HWND hwnd_ui = nullptr;
    std::thread click_thread;
    HHOOK mouse_hook = nullptr;

    ClickEngine() {
        for (int i = 0; i < BTN_COUNT; ++i) {
            clicking_state[i] = false;
            ignore_next_press[i] = false;
            ignore_next_release[i] = false;
        }
    }

    void Start(HWND ui) {
        hwnd_ui = ui;
        mouse_hook = SetWindowsHookExW(WH_MOUSE_LL, &ClickEngine::LowLevelMouseProc, GetModuleHandleW(nullptr), 0);
        click_thread = std::thread([this] { ClickLoop(); });
    }

    void Stop() {
        running = false;
        if (mouse_hook) {
            UnhookWindowsHookEx(mouse_hook);
            mouse_hook = nullptr;
        }
        if (click_thread.joinable()) click_thread.join();
    }

    bool AnyClicking() const {
        for (int i = 0; i < BTN_COUNT; ++i)
            if (clicking_state[i].load()) return true;
        return false;
    }

    int GetRealCps() {
        const double now = WallSec();
        std::lock_guard<std::mutex> lock(history_mu);
        while (!click_history.empty() && click_history.front() < now - 1.0)
            click_history.pop_front();
        return static_cast<int>(click_history.size());
    }

    void NotifyUi() {
        if (hwnd_ui) PostMessageW(hwnd_ui, WM_ENGINE_STATE, 0, 0);
    }

    static int ButtonFromMsg(WPARAM wParam, const MSLLHOOKSTRUCT* info) {
        switch (wParam) {
            case WM_LBUTTONDOWN: case WM_LBUTTONUP: return BTN_LEFT;
            case WM_RBUTTONDOWN: case WM_RBUTTONUP: return BTN_RIGHT;
            case WM_MBUTTONDOWN: case WM_MBUTTONUP: return BTN_MIDDLE;
            case WM_XBUTTONDOWN: case WM_XBUTTONUP: {
                const WORD xb = HIWORD(info->mouseData);
                if (xb == XBUTTON1) return BTN_X1;
                if (xb == XBUTTON2) return BTN_X2;
                return -1;
            }
            default: return -1;
        }
    }

    static bool IsPressMsg(WPARAM wParam) {
        return wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN ||
               wParam == WM_MBUTTONDOWN || wParam == WM_XBUTTONDOWN;
    }

    static DWORD DownFlag(int btn) {
        switch (btn) {
            case BTN_LEFT: return MOUSEEVENTF_LEFTDOWN;
            case BTN_RIGHT: return MOUSEEVENTF_RIGHTDOWN;
            case BTN_MIDDLE: return MOUSEEVENTF_MIDDLEDOWN;
            case BTN_X1: case BTN_X2: return MOUSEEVENTF_XDOWN;
            default: return 0;
        }
    }

    static DWORD UpFlag(int btn) {
        switch (btn) {
            case BTN_LEFT: return MOUSEEVENTF_LEFTUP;
            case BTN_RIGHT: return MOUSEEVENTF_RIGHTUP;
            case BTN_MIDDLE: return MOUSEEVENTF_MIDDLEUP;
            case BTN_X1: case BTN_X2: return MOUSEEVENTF_XUP;
            default: return 0;
        }
    }

    static DWORD XData(int btn) {
        if (btn == BTN_X1) return XBUTTON1;
        if (btn == BTN_X2) return XBUTTON2;
        return 0;
    }

    void InjectPress(int btn) {
        INPUT in{};
        in.type = INPUT_MOUSE;
        in.mi.dwFlags = DownFlag(btn);
        in.mi.mouseData = XData(btn);
        SendInput(1, &in, sizeof(INPUT));
    }

    void InjectRelease(int btn) {
        INPUT in{};
        in.type = INPUT_MOUSE;
        in.mi.dwFlags = UpFlag(btn);
        in.mi.mouseData = XData(btn);
        SendInput(1, &in, sizeof(INPUT));
    }

    void ClickLoop() {
        while (running.load()) {
            if (!master_enabled.load()) {
                SleepSec(0.05);
                continue;
            }

            bool clicked_any = false;
            for (int btn = 0; btn < BTN_COUNT; ++btn) {
                if (!clicking_state[btn].load() || !active_triggers[btn]) continue;

                ignore_next_press[btn] = true;
                InjectPress(btn);
                SleepSec(RandUniform(0.01, 0.02));
                ignore_next_release[btn] = true;
                InjectRelease(btn);

                {
                    std::lock_guard<std::mutex> lock(history_mu);
                    click_history.push_back(WallSec());
                    while (click_history.size() > 200) click_history.pop_front();
                }
                clicked_any = true;
            }

            if (clicked_any) {
                const double base = 1.0 / std::max(1, cps.load());
                double sleep_time = humanized.load()
                    ? RandUniform(base * 0.7, base * 1.3)
                    : base;
                SleepSec(std::max(0.001, sleep_time - 0.025));
            } else {
                SleepSec(0.01);
            }
        }
    }

    void OnPhysical(int btn, bool pressed) {
        if (!master_enabled.load() || btn < 0 || btn >= BTN_COUNT) return;
        if (!active_triggers[btn]) return;

        if (pressed) {
            if (ignore_next_press[btn].exchange(false)) return;
            const double now = NowSec();
            if (now - last_click_time[btn] < kDoubleClickThreshold) {
                if (!clicking_state[btn].load()) {
                    clicking_state[btn] = true;
                    NotifyUi();
                }
            }
            last_click_time[btn] = now;
        } else {
            if (ignore_next_release[btn].exchange(false)) return;
            if (clicking_state[btn].load()) {
                clicking_state[btn] = false;
                NotifyUi();
            }
        }
    }

    static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam);
};

static ClickEngine g_engine;

LRESULT CALLBACK ClickEngine::LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        const auto* info = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
        const int btn = ButtonFromMsg(wParam, info);
        if (btn >= 0) g_engine.OnPhysical(btn, IsPressMsg(wParam));
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Overlay
// ---------------------------------------------------------------------------

struct OverlayWindow {
    HWND hwnd = nullptr;
    std::wstring text_ = L"CPS: 0";
    Color color_ = Theme::Green;

    bool Create(HINSTANCE inst) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = &OverlayWindow::WndProc;
        wc.hInstance = inst;
        wc.lpszClassName = L"ACM3Overlay";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassW(&wc);

        hwnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TRANSPARENT,
            L"ACM3Overlay", L"", WS_POPUP,
            GetSystemMetrics(SM_CXSCREEN) - 250, 50, 220, 72,
            nullptr, nullptr, inst, this);

        SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);
        ShowWindow(hwnd, SW_HIDE);
        return hwnd != nullptr;
    }

    void Destroy() {
        if (hwnd) { DestroyWindow(hwnd); hwnd = nullptr; }
    }

    void Show(bool visible) {
        if (hwnd) ShowWindow(hwnd, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
    }

    void SetText(const std::wstring& text, Color color) {
        text_ = text;
        color_ = color;
        if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
    }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        OverlayWindow* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<OverlayWindow*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        } else {
            self = reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        if (msg == WM_PAINT && self) {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            Graphics g(hdc);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
            g.Clear(Color(255, 0, 0, 0));

            RectF card(8, 8, 204, 56);
            SolidBrush bg(Color(220, 24, 24, 28));
            FillRoundRect(g, bg, card, 14.f);
            Pen border(Color(160, 0, 230, 118), 1.5f);
            DrawRoundRect(g, border, card, 14.f);

            FontFamily family(L"Segoe UI");
            Font font(&family, 22.f, FontStyleBold, UnitPixel);
            SolidBrush text(self->color_);
            DrawStringCenter(g, self->text_.c_str(), font, text, card);
            EndPaint(hwnd, &ps);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
};

// ---------------------------------------------------------------------------
// Ripple
// ---------------------------------------------------------------------------

struct Ripple {
    float x = 0, y = 0;
    float radius = 5;
    Color color = Theme::Green;
};

// ---------------------------------------------------------------------------
// App UI (GDI+ custom)
// ---------------------------------------------------------------------------

struct App {
    HINSTANCE inst = nullptr;
    HWND hwnd = nullptr;
    OverlayWindow overlay;

    FontFamily* family = nullptr;
    Font* font_title = nullptr;
    Font* font_ui = nullptr;
    Font* font_ui_bold = nullptr;
    Font* font_small = nullptr;
    Font* font_status = nullptr;

    int tab = 0;  // 0 config, 1 test
    bool overlay_on = false;
    bool dragging_slider = false;
    int hover_id = -1;

    // Hit regions (atualizados em Layout)
    RectF rc_master_sw{};
    RectF rc_tab0{}, rc_tab1{};
    RectF rc_slider{};
    RectF rc_human_sw{};
    RectF rc_triggers[BTN_COUNT]{};
    RectF rc_overlay_sw{};
    RectF rc_canvas{};
    RectF rc_content{};

    std::vector<Ripple> ripples;

    static Color RippleColor(int i) {
        static const Color colors[5] = {
            Color(255, 0, 230, 118), Color(255, 41, 182, 246), Color(255, 224, 64, 251),
            Color(255, 255, 87, 34), Color(255, 255, 235, 59)
        };
        return colors[i % 5];
    }

    static const wchar_t* TriggerName(int i) {
        static const wchar_t* names[BTN_COUNT] = {
            L"Botão Esquerdo", L"Botão Direito", L"Botão Meio",
            L"Botão Lateral 1 (X1)", L"Botão Lateral 2 (X2)"
        };
        return names[i];
    }

    bool Create(HINSTANCE hInstance) {
        inst = hInstance;
        family = new FontFamily(L"Segoe UI");
        font_title = new Font(family, 26.f, FontStyleBold, UnitPixel);
        font_ui = new Font(family, 14.f, FontStyleRegular, UnitPixel);
        font_ui_bold = new Font(family, 14.f, FontStyleBold, UnitPixel);
        font_small = new Font(family, 12.f, FontStyleRegular, UnitPixel);
        font_status = new Font(family, 17.f, FontStyleBold, UnitPixel);

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &App::WndProc;
        wc.hInstance = inst;
        wc.lpszClassName = L"ACM3Main";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.style = CS_DBLCLKS;
        wc.hIcon = static_cast<HICON>(LoadImageW(
            inst, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
            GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0));
        wc.hIconSm = static_cast<HICON>(LoadImageW(
            inst, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
        RegisterClassExW(&wc);

        RECT wr{0, 0, kWinW, kWinH};
        AdjustWindowRectEx(&wr, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0);

        hwnd = CreateWindowExW(
            0, L"ACM3Main", L"Auto Clicker M3 Pro (C++)",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
            CW_USEDEFAULT, CW_USEDEFAULT, wr.right - wr.left, wr.bottom - wr.top,
            nullptr, nullptr, inst, this);

        if (!hwnd) return false;

        if (wc.hIcon)
            SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(wc.hIcon));
        if (wc.hIconSm)
            SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(wc.hIconSm));

        Layout();
        overlay.Create(inst);
        g_engine.Start(hwnd);
        SetTimer(hwnd, kTimerOverlay, 100, nullptr);
        SetTimer(hwnd, kTimerRipple, 16, nullptr);

        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
        return true;
    }

    void Destroy() {
        KillTimer(hwnd, kTimerOverlay);
        KillTimer(hwnd, kTimerRipple);
        g_engine.Stop();
        overlay.Destroy();
        delete font_title; delete font_ui; delete font_ui_bold;
        delete font_small; delete font_status; delete family;
        font_title = font_ui = font_ui_bold = font_small = font_status = nullptr;
        family = nullptr;
    }

    void Layout() {
        const float pad = 20.f;
        const float content_top = 72.f;
        const float content_bottom = 56.f;
        rc_content = RectF(pad, content_top, kWinW - pad * 2, kWinH - content_top - content_bottom);

        rc_master_sw = RectF(kWinW - pad - 56.f, 22.f, 52.f, 28.f);

        const float tab_y = content_top;
        const float tab_h = 36.f;
        const float tab_w = (rc_content.Width - 8.f) * 0.5f;
        rc_tab0 = RectF(rc_content.X, tab_y, tab_w, tab_h);
        rc_tab1 = RectF(rc_content.X + tab_w + 8.f, tab_y, tab_w, tab_h);

        float y = tab_y + tab_h + 16.f;
        // Card CPS
        RectF card1(rc_content.X, y, rc_content.Width, 132.f);
        rc_slider = RectF(card1.X + 20.f, card1.Y + 52.f, card1.Width - 40.f, 28.f);
        rc_human_sw = RectF(card1.X + 20.f, card1.Y + 92.f, 52.f, 28.f);

        y = card1.Y + card1.Height + 12.f;
        RectF card2(rc_content.X, y, rc_content.Width, 230.f);
        for (int i = 0; i < BTN_COUNT; ++i) {
            rc_triggers[i] = RectF(card2.X + 20.f, card2.Y + 48.f + i * 34.f, card2.Width - 40.f, 28.f);
        }

        y = card2.Y + card2.Height + 12.f;
        RectF card3(rc_content.X, y, rc_content.Width, 64.f);
        rc_overlay_sw = RectF(card3.X + 20.f, card3.Y + 18.f, 52.f, 28.f);

        // Test canvas
        rc_canvas = RectF(rc_content.X, tab_y + tab_h + 56.f, rc_content.Width,
                          rc_content.GetBottom() - (tab_y + tab_h + 56.f));
    }

    void DrawSwitchFixed(Graphics& g, RectF r, bool on) {
        SolidBrush track(on ? Theme::Green : Theme::Track);
        FillRoundRect(g, track, r, r.Height * 0.5f);
        const float knob = r.Height - 6.f;
        const float kx = on ? (r.X + r.Width - knob - 3.f) : (r.X + 3.f);
        SolidBrush knobBrush(Color(255, 245, 245, 248));
        g.FillEllipse(&knobBrush, kx, r.Y + 3.f, knob, knob);
    }

    void DrawCheckbox(Graphics& g, RectF row, bool on, const wchar_t* label) {
        RectF box(row.X, row.Y + 4.f, 20.f, 20.f);
        if (on) {
            SolidBrush fill(Theme::Accent);
            FillRoundRect(g, fill, box, 5.f);
            Pen check(Color(255, 255, 255, 255), 2.2f);
            check.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
            g.DrawLine(&check, box.X + 4.f, box.Y + 10.f, box.X + 8.f, box.Y + 14.f);
            g.DrawLine(&check, box.X + 8.f, box.Y + 14.f, box.X + 15.f, box.Y + 6.f);
        } else {
            Pen border(Theme::CardBorder, 1.5f);
            DrawRoundRect(g, border, box, 5.f);
        }
        SolidBrush text(Theme::Text);
        DrawStringLeft(g, label, *font_ui, text, RectF(row.X + 32.f, row.Y, row.Width - 32.f, row.Height));
    }

    void DrawSlider(Graphics& g, RectF r, int value, int min_v, int max_v) {
        const float cy = r.Y + r.Height * 0.5f;
        RectF track(r.X, cy - 3.f, r.Width, 6.f);
        SolidBrush trackBrush(Theme::Track);
        FillRoundRect(g, trackBrush, track, 3.f);

        const float t = static_cast<float>(value - min_v) / static_cast<float>(max_v - min_v);
        RectF fill(r.X, cy - 3.f, r.Width * t, 6.f);
        SolidBrush fillBrush(Theme::Accent);
        FillRoundRect(g, fillBrush, fill, 3.f);

        const float kx = r.X + r.Width * t;
        SolidBrush knob(Color(255, 255, 255, 255));
        g.FillEllipse(&knob, kx - 9.f, cy - 9.f, 18.f, 18.f);
        SolidBrush knobInner(Theme::Accent);
        g.FillEllipse(&knobInner, kx - 5.f, cy - 5.f, 10.f, 10.f);
    }

    void Paint(HDC hdc) {
        RECT crc{};
        GetClientRect(hwnd, &crc);
        const int w = crc.right;
        const int h = crc.bottom;

        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
        HGDIOBJ old = SelectObject(mem, bmp);

        Graphics g(mem);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        g.Clear(Theme::Bg);

        SolidBrush textBrush(Theme::Text);
        SolidBrush dimBrush(Theme::TextDim);
        SolidBrush cardBrush(Theme::Card);
        Pen cardPen(Theme::CardBorder, 1.f);

        // Header
        DrawStringLeft(g, L"Auto Clicker", *font_title, textBrush, RectF(20, 16, 280, 40));
        DrawSwitchFixed(g, rc_master_sw, g_engine.master_enabled.load());
        const wchar_t* master_lbl = g_engine.master_enabled.load() ? L"LIGADO" : L"DESLIGADO";
        SolidBrush masterCol(g_engine.master_enabled.load() ? Theme::Green : Theme::TextDim);
        DrawStringLeft(g, master_lbl, *font_ui_bold, masterCol,
                       RectF(rc_master_sw.X - 92.f, rc_master_sw.Y, 88.f, rc_master_sw.Height));

        // Tabs
        auto drawTab = [&](RectF r, const wchar_t* label, bool active) {
            SolidBrush bg(active ? Theme::AccentSoft : Theme::TabIdle);
            FillRoundRect(g, bg, r, 10.f);
            if (active) {
                Pen p(Theme::Accent, 1.5f);
                DrawRoundRect(g, p, r, 10.f);
            }
            SolidBrush tb(active ? Theme::Text : Theme::TextDim);
            DrawStringCenter(g, label, *font_ui_bold, tb, r);
        };
        drawTab(rc_tab0, L"Configurações", tab == 0);
        drawTab(rc_tab1, L"Teste (Ripples)", tab == 1);

        if (tab == 0) {
            // Card CPS
            RectF card1(rc_content.X, rc_tab0.GetBottom() + 16.f, rc_content.Width, 132.f);
            FillRoundRect(g, cardBrush, card1, 16.f);
            DrawRoundRect(g, cardPen, card1, 16.f);

            std::wstring cps_label = L"Velocidade (CPS): " + std::to_wstring(g_engine.cps.load());
            DrawStringLeft(g, cps_label.c_str(), *font_ui_bold, textBrush,
                           RectF(card1.X + 20.f, card1.Y + 16.f, card1.Width - 40.f, 24.f));
            DrawSlider(g, rc_slider, g_engine.cps.load(), 1, 100);

            DrawSwitchFixed(g, rc_human_sw, g_engine.humanized.load());
            DrawStringLeft(g, L"Modo Humanizado (Aleatoriedade)", *font_ui, textBrush,
                           RectF(rc_human_sw.GetRight() + 12.f, rc_human_sw.Y, 280.f, rc_human_sw.Height));

            // Card triggers
            RectF card2(rc_content.X, card1.GetBottom() + 12.f, rc_content.Width, 230.f);
            FillRoundRect(g, cardBrush, card2, 16.f);
            DrawRoundRect(g, cardPen, card2, 16.f);
            DrawStringLeft(g, L"Ativar duplo-clique para os botões:", *font_ui_bold, textBrush,
                           RectF(card2.X + 20.f, card2.Y + 14.f, card2.Width - 40.f, 24.f));
            for (int i = 0; i < BTN_COUNT; ++i)
                DrawCheckbox(g, rc_triggers[i], g_engine.active_triggers[i], TriggerName(i));

            // Card overlay
            RectF card3(rc_content.X, card2.GetBottom() + 12.f, rc_content.Width, 64.f);
            FillRoundRect(g, cardBrush, card3, 16.f);
            DrawRoundRect(g, cardPen, card3, 16.f);
            DrawSwitchFixed(g, rc_overlay_sw, overlay_on);
            DrawStringLeft(g, L"Ativar Sobreposição de Tela (Overlay)", *font_ui, textBrush,
                           RectF(rc_overlay_sw.GetRight() + 12.f, rc_overlay_sw.Y, 320.f, rc_overlay_sw.Height));
        } else {
            DrawStringCenter(g,
                L"Dê um duplo-clique aqui para testar o CPS.\nCada clique gerará uma onda de água.",
                *font_small, dimBrush,
                RectF(rc_content.X, rc_tab0.GetBottom() + 12.f, rc_content.Width, 40.f));

            SolidBrush canvasBrush(Theme::Canvas);
            FillRoundRect(g, canvasBrush, rc_canvas, 14.f);
            Pen canvasPen(Theme::CardBorder, 1.f);
            DrawRoundRect(g, canvasPen, rc_canvas, 14.f);

            g.SetClip(rc_canvas);
            for (const auto& rip : ripples) {
                const float alpha = std::max(40.f, 220.f - rip.radius * 3.f);
                Color c(static_cast<BYTE>(alpha), rip.color.GetR(), rip.color.GetG(), rip.color.GetB());
                Pen pen(c, rip.radius > 40 ? 1.2f : 2.2f);
                g.DrawEllipse(&pen, rip.x - rip.radius, rip.y - rip.radius, rip.radius * 2, rip.radius * 2);
                SolidBrush core(rip.color);
                g.FillEllipse(&core, rip.x - 2.5f, rip.y - 2.5f, 5.f, 5.f);
            }
            g.ResetClip();
        }

        // Status
        std::wstring status;
        Color statusColor = Theme::Red;
        if (!g_engine.master_enabled.load()) {
            status = L"Status: DESLIGADO GERAL";
            statusColor = Theme::TextDim;
        } else if (g_engine.AnyClicking()) {
            status = L"Status: ATIVO";
            statusColor = Theme::Green;
        } else {
            status = L"Status: INATIVO";
            statusColor = Theme::Red;
        }
        SolidBrush statusBrush(statusColor);
        DrawStringCenter(g, status.c_str(), *font_status, statusBrush,
                         RectF(0, static_cast<REAL>(kWinH - 48), static_cast<REAL>(kWinW), 36));

        BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
    }

    int HitTest(int x, int y) {
        if (PtIn(rc_master_sw, x, y) ||
            PtIn(RectF(rc_master_sw.X - 92.f, rc_master_sw.Y, 148.f, rc_master_sw.Height), x, y))
            return 1;
        if (PtIn(rc_tab0, x, y)) return 2;
        if (PtIn(rc_tab1, x, y)) return 3;
        if (tab == 0) {
            if (PtIn(rc_slider, x, y) ||
                PtIn(RectF(rc_slider.X, rc_slider.Y - 8.f, rc_slider.Width, rc_slider.Height + 16.f), x, y))
                return 4;
            if (PtIn(RectF(rc_human_sw.X, rc_human_sw.Y, 300.f, rc_human_sw.Height), x, y)) return 5;
            for (int i = 0; i < BTN_COUNT; ++i)
                if (PtIn(rc_triggers[i], x, y)) return 10 + i;
            if (PtIn(RectF(rc_overlay_sw.X, rc_overlay_sw.Y, 360.f, rc_overlay_sw.Height), x, y)) return 6;
        } else if (PtIn(rc_canvas, x, y)) {
            return 7;
        }
        return 0;
    }

    void SetCpsFromX(int x) {
        float t = (static_cast<float>(x) - rc_slider.X) / rc_slider.Width;
        t = std::clamp(t, 0.f, 1.f);
        g_engine.cps = 1 + static_cast<int>(t * 99.f + 0.5f);
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    void AddRipple(int x, int y) {
        Ripple r;
        r.x = static_cast<float>(x);
        r.y = static_cast<float>(y);
        r.radius = 5;
        r.color = RippleColor(static_cast<int>(RandUniform(0, 4.999)));
        ripples.push_back(r);
    }

    void TickRipples() {
        if (ripples.empty()) return;
        for (auto& r : ripples) r.radius += 3.5f;
        ripples.erase(std::remove_if(ripples.begin(), ripples.end(),
                                     [](const Ripple& r) { return r.radius > 60.f; }),
                      ripples.end());
        if (tab == 1) InvalidateRect(hwnd, nullptr, FALSE);
    }

    void UpdateOverlay() {
        if (!overlay_on) return;
        if (g_engine.AnyClicking())
            overlay.SetText(L"CPS: " + std::to_wstring(g_engine.GetRealCps()), Theme::Green);
        else
            overlay.SetText(L"CPS: 0", Theme::TextDim);
    }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        App* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<App*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hwnd = hwnd;
        } else {
            self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        switch (msg) {
            case WM_ERASEBKGND:
                return 1;

            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                if (self) self->Paint(hdc);
                EndPaint(hwnd, &ps);
                return 0;
            }

            case WM_LBUTTONDOWN: {
                if (!self) return 0;
                const int x = GET_X_LPARAM(lParam);
                const int y = GET_Y_LPARAM(lParam);
                const int id = self->HitTest(x, y);
                if (id == 1) {
                    const bool on = !g_engine.master_enabled.load();
                    g_engine.master_enabled = on;
                    if (!on)
                        for (int i = 0; i < BTN_COUNT; ++i) g_engine.clicking_state[i] = false;
                    InvalidateRect(hwnd, nullptr, FALSE);
                } else if (id == 2) {
                    self->tab = 0;
                    InvalidateRect(hwnd, nullptr, FALSE);
                } else if (id == 3) {
                    self->tab = 1;
                    InvalidateRect(hwnd, nullptr, FALSE);
                } else if (id == 4) {
                    self->dragging_slider = true;
                    SetCapture(hwnd);
                    self->SetCpsFromX(x);
                } else if (id == 5) {
                    g_engine.humanized = !g_engine.humanized.load();
                    InvalidateRect(hwnd, nullptr, FALSE);
                } else if (id == 6) {
                    self->overlay_on = !self->overlay_on;
                    self->overlay.Show(self->overlay_on);
                    self->UpdateOverlay();
                    InvalidateRect(hwnd, nullptr, FALSE);
                } else if (id >= 10 && id < 10 + BTN_COUNT) {
                    const int btn = id - 10;
                    g_engine.active_triggers[btn] = !g_engine.active_triggers[btn];
                    InvalidateRect(hwnd, nullptr, FALSE);
                } else if (id == 7) {
                    self->AddRipple(x, y);
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }

            case WM_MOUSEMOVE: {
                if (!self) return 0;
                if (self->dragging_slider)
                    self->SetCpsFromX(GET_X_LPARAM(lParam));
                return 0;
            }

            case WM_LBUTTONUP:
                if (self && self->dragging_slider) {
                    self->dragging_slider = false;
                    ReleaseCapture();
                }
                return 0;

            case WM_ENGINE_STATE:
                if (self) InvalidateRect(hwnd, nullptr, FALSE);
                return 0;

            case WM_TIMER:
                if (!self) return 0;
                if (wParam == kTimerOverlay) self->UpdateOverlay();
                if (wParam == kTimerRipple) self->TickRipples();
                return 0;

            case WM_DESTROY:
                if (self) self->Destroy();
                PostQuitMessage(0);
                return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
};

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    ULONG_PTR gdiplusToken = 0;
    GdiplusStartupInput gdiplusStartupInput;
    if (GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr) != Ok)
        return 1;

    timeBeginPeriod(1);

    App app;
    const int ok = app.Create(hInstance) ? 0 : 1;
    if (ok == 0) {
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    timeEndPeriod(1);
    GdiplusShutdown(gdiplusToken);
    return ok == 0 ? 0 : 1;
}
