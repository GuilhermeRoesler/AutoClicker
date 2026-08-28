/**
 * Auto Clicker M3 Pro — versão C++ (secundária)
 *
 * Espelha o modelo do main.py:
 *   1) Duplo-clique físico (< 300 ms) em botão habilitado
 *   2) Manter pressionado → injeta cliques na CPS configurada
 *   3) Soltar → para imediatamente
 *
 * Anti-feedback: ignore_next_press / ignore_next_release por botão,
 * setados ANTES de cada SendInput.
 *
 * Build (MinGW):
 *   cmake -S cpp -B cpp/build -G "MinGW Makefiles"
 *   cmake --build cpp/build
 */

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <commctrl.h>
#include <windowsx.h>

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
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "winmm.lib")
#endif

// ---------------------------------------------------------------------------
// Constantes / IDs
// ---------------------------------------------------------------------------

enum MouseBtn : int {
    BTN_LEFT = 0,
    BTN_RIGHT,
    BTN_MIDDLE,
    BTN_X1,
    BTN_X2,
    BTN_COUNT
};

static constexpr double kDoubleClickThreshold = 0.3;
static constexpr UINT_PTR kTimerOverlay = 1;
static constexpr UINT_PTR kTimerRipple = 2;

static constexpr int IDC_MASTER = 1001;
static constexpr int IDC_CPS_SLIDER = 1002;
static constexpr int IDC_CPS_LABEL = 1003;
static constexpr int IDC_HUMANIZED = 1004;
static constexpr int IDC_TRIGGER_BASE = 1010;  // +0..4
static constexpr int IDC_OVERLAY = 1020;
static constexpr int IDC_STATUS = 1030;
static constexpr int IDC_TABS = 1040;
static constexpr int IDC_TEST_HINT = 1050;

static constexpr COLORREF kBg = RGB(30, 30, 30);
static constexpr COLORREF kCard = RGB(45, 45, 45);
static constexpr COLORREF kText = RGB(230, 230, 230);
static constexpr COLORREF kGreen = RGB(0, 230, 118);
static constexpr COLORREF kRed = RGB(239, 83, 80);
static constexpr COLORREF kGray = RGB(158, 158, 158);
static constexpr COLORREF kCanvasBg = RGB(18, 18, 18);

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

// ---------------------------------------------------------------------------
// ClickEngine
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
        for (int i = 0; i < BTN_COUNT; ++i) {
            if (clicking_state[i].load()) return true;
        }
        return false;
    }

    int GetRealCps() {
        const double now = WallSec();
        std::lock_guard<std::mutex> lock(history_mu);
        while (!click_history.empty() && click_history.front() < now - 1.0) {
            click_history.pop_front();
        }
        return static_cast<int>(click_history.size());
    }

    void NotifyUi() {
        if (hwnd_ui) PostMessageW(hwnd_ui, WM_APP + 1, 0, 0);
    }

    static int ButtonFromMsg(WPARAM wParam, const MSLLHOOKSTRUCT* info) {
        switch (wParam) {
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
                return BTN_LEFT;
            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP:
                return BTN_RIGHT;
            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP:
                return BTN_MIDDLE;
            case WM_XBUTTONDOWN:
            case WM_XBUTTONUP: {
                const WORD xb = HIWORD(info->mouseData);
                if (xb == XBUTTON1) return BTN_X1;
                if (xb == XBUTTON2) return BTN_X2;
                return -1;
            }
            default:
                return -1;
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
            case BTN_X1: return MOUSEEVENTF_XDOWN;
            case BTN_X2: return MOUSEEVENTF_XDOWN;
            default: return 0;
        }
    }

    static DWORD UpFlag(int btn) {
        switch (btn) {
            case BTN_LEFT: return MOUSEEVENTF_LEFTUP;
            case BTN_RIGHT: return MOUSEEVENTF_RIGHTUP;
            case BTN_MIDDLE: return MOUSEEVENTF_MIDDLEUP;
            case BTN_X1: return MOUSEEVENTF_XUP;
            case BTN_X2: return MOUSEEVENTF_XUP;
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
                if (!clicking_state[btn].load()) continue;
                if (!active_triggers[btn]) continue;

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
        if (!master_enabled.load()) return;
        if (btn < 0 || btn >= BTN_COUNT) return;
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
        if (btn >= 0) {
            g_engine.OnPhysical(btn, IsPressMsg(wParam));
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Overlay
// ---------------------------------------------------------------------------

struct OverlayWindow {
    HWND hwnd = nullptr;
    HFONT font = nullptr;

    bool Create(HINSTANCE inst) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = &OverlayWindow::WndProc;
        wc.hInstance = inst;
        wc.lpszClassName = L"ACM3Overlay";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassW(&wc);

        const int x = GetSystemMetrics(SM_CXSCREEN) - 250;
        const int y = 50;

        hwnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TRANSPARENT,
            L"ACM3Overlay", L"",
            WS_POPUP,
            x, y, 220, 70,
            nullptr, nullptr, inst, this);

        SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);
        font = CreateFontW(36, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        ShowWindow(hwnd, SW_HIDE);
        return hwnd != nullptr;
    }

    void Destroy() {
        if (font) {
            DeleteObject(font);
            font = nullptr;
        }
        if (hwnd) {
            DestroyWindow(hwnd);
            hwnd = nullptr;
        }
    }

    void Show(bool visible) {
        if (!hwnd) return;
        ShowWindow(hwnd, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
    }

    void SetText(const std::wstring& text, COLORREF color) {
        text_ = text;
        color_ = color;
        if (hwnd) InvalidateRect(hwnd, nullptr, TRUE);
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
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, self->color_);
            HFONT old = static_cast<HFONT>(SelectObject(hdc, self->font));
            DrawTextW(hdc, self->text_.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, old);
            EndPaint(hwnd, &ps);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

private:
    std::wstring text_ = L"CPS: 0";
    COLORREF color_ = kGreen;
};

// ---------------------------------------------------------------------------
// Ripple
// ---------------------------------------------------------------------------

struct Ripple {
    int x = 0, y = 0;
    int radius = 5;
    COLORREF color = kGreen;
};

// ---------------------------------------------------------------------------
// App UI
// ---------------------------------------------------------------------------

struct App {
    HINSTANCE inst = nullptr;
    HWND hwnd = nullptr;
    HWND tabs = nullptr;
    HWND status = nullptr;
    HWND cps_label = nullptr;
    HWND cps_slider = nullptr;
    HWND canvas = nullptr;
    HWND page_config = nullptr;
    HWND page_test = nullptr;
    OverlayWindow overlay;

    HFONT font_title = nullptr;
    HFONT font_ui = nullptr;
    HFONT font_status = nullptr;
    HBRUSH brush_bg = nullptr;
    HBRUSH brush_card = nullptr;
    HBRUSH brush_canvas = nullptr;

    bool overlay_on = false;
    std::vector<Ripple> ripples;
    static constexpr COLORREF kRippleColors[5] = {
        RGB(0, 230, 118), RGB(41, 182, 246), RGB(224, 64, 251),
        RGB(255, 87, 34), RGB(255, 235, 59)
    };

    bool Create(HINSTANCE hInstance) {
        inst = hInstance;
        INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_BAR_CLASSES | ICC_TAB_CLASSES | ICC_STANDARD_CLASSES};
        InitCommonControlsEx(&icc);

        brush_bg = CreateSolidBrush(kBg);
        brush_card = CreateSolidBrush(kCard);
        brush_canvas = CreateSolidBrush(kCanvasBg);
        font_title = CreateFontW(26, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, FF_SWISS, L"Segoe UI");
        font_ui = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, FF_SWISS, L"Segoe UI");
        font_status = CreateFontW(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, FF_SWISS, L"Segoe UI");

        WNDCLASSW wc{};
        wc.lpfnWndProc = &App::WndProc;
        wc.hInstance = inst;
        wc.lpszClassName = L"ACM3Main";
        wc.hbrBackground = brush_bg;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassW(&wc);

        hwnd = CreateWindowExW(
            0, L"ACM3Main", L"Auto Clicker M3 Pro (C++)",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
            CW_USEDEFAULT, CW_USEDEFAULT, 516, 790,
            nullptr, nullptr, inst, this);

        if (!hwnd) return false;

        overlay.Create(inst);
        g_engine.Start(hwnd);
        SetTimer(hwnd, kTimerOverlay, 100, nullptr);
        SetTimer(hwnd, kTimerRipple, 20, nullptr);

        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
        return true;
    }

    void Destroy() {
        KillTimer(hwnd, kTimerOverlay);
        KillTimer(hwnd, kTimerRipple);
        g_engine.Stop();
        overlay.Destroy();
        if (font_title) DeleteObject(font_title);
        if (font_ui) DeleteObject(font_ui);
        if (font_status) DeleteObject(font_status);
        if (brush_bg) DeleteObject(brush_bg);
        if (brush_card) DeleteObject(brush_card);
        if (brush_canvas) DeleteObject(brush_canvas);
    }

    void BuildChildren() {
        // Header title
        HWND title = CreateWindowW(L"STATIC", L"Auto Clicker",
                                   WS_CHILD | WS_VISIBLE | SS_LEFT,
                                   20, 18, 260, 36, hwnd, nullptr, inst, nullptr);
        SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(font_title), TRUE);

        HWND master = CreateWindowW(L"BUTTON", L"LIGADO",
                                    WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                    360, 22, 120, 28, hwnd, reinterpret_cast<HMENU>(IDC_MASTER), inst, nullptr);
        SendMessageW(master, WM_SETFONT, reinterpret_cast<WPARAM>(font_ui), TRUE);
        SendMessageW(master, BM_SETCHECK, BST_CHECKED, 0);

        tabs = CreateWindowW(WC_TABCONTROLW, L"",
                             WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                             20, 60, 460, 620, hwnd, reinterpret_cast<HMENU>(IDC_TABS), inst, nullptr);
        SendMessageW(tabs, WM_SETFONT, reinterpret_cast<WPARAM>(font_ui), TRUE);

        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<wchar_t*>(L"Configurações");
        TabCtrl_InsertItem(tabs, 0, &item);
        item.pszText = const_cast<wchar_t*>(L"Teste (Ripples)");
        TabCtrl_InsertItem(tabs, 1, &item);

        RECT tr{};
        GetClientRect(tabs, &tr);
        TabCtrl_AdjustRect(tabs, FALSE, &tr);
        MapWindowPoints(tabs, hwnd, reinterpret_cast<LPPOINT>(&tr), 2);

        page_config = CreateWindowW(L"STATIC", L"",
                                    WS_CHILD | WS_VISIBLE,
                                    tr.left, tr.top, tr.right - tr.left, tr.bottom - tr.top,
                                    hwnd, nullptr, inst, nullptr);

        page_test = CreateWindowW(L"STATIC", L"",
                                  WS_CHILD,
                                  tr.left, tr.top, tr.right - tr.left, tr.bottom - tr.top,
                                  hwnd, nullptr, inst, nullptr);

        BuildConfigPage(page_config, tr.right - tr.left, tr.bottom - tr.top);
        BuildTestPage(page_test, tr.right - tr.left, tr.bottom - tr.top);

        status = CreateWindowW(L"STATIC", L"Status: INATIVO",
                               WS_CHILD | WS_VISIBLE | SS_CENTER,
                               20, 695, 460, 30, hwnd, reinterpret_cast<HMENU>(IDC_STATUS), inst, nullptr);
        SendMessageW(status, WM_SETFONT, reinterpret_cast<WPARAM>(font_status), TRUE);
    }

    void BuildConfigPage(HWND parent, int /*w*/, int /*h*/) {
        auto addLabel = [&](const wchar_t* text, int x, int y, int ww, int hh, int id = 0) {
            HWND h = CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
                                   x, y, ww, hh, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
            SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font_ui), TRUE);
            return h;
        };

        cps_label = addLabel(L"Velocidade (CPS): 12", 16, 16, 400, 24, IDC_CPS_LABEL);

        cps_slider = CreateWindowW(TRACKBAR_CLASSW, L"",
                                   WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | TBS_HORZ,
                                   16, 48, 400, 36, parent, reinterpret_cast<HMENU>(IDC_CPS_SLIDER), inst, nullptr);
        SendMessageW(cps_slider, TBM_SETRANGEMIN, TRUE, 1);
        SendMessageW(cps_slider, TBM_SETRANGEMAX, TRUE, 100);
        SendMessageW(cps_slider, TBM_SETPOS, TRUE, 12);
        SendMessageW(cps_slider, TBM_SETTICFREQ, 10, 0);

        HWND human = CreateWindowW(L"BUTTON", L"Modo Humanizado (Aleatoriedade)",
                                   WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                   16, 100, 400, 28, parent, reinterpret_cast<HMENU>(IDC_HUMANIZED), inst, nullptr);
        SendMessageW(human, WM_SETFONT, reinterpret_cast<WPARAM>(font_ui), TRUE);

        addLabel(L"Ativar duplo-clique para os botões:", 16, 150, 400, 24);

        static const wchar_t* names[BTN_COUNT] = {
            L"Botão Esquerdo", L"Botão Direito", L"Botão Meio",
            L"Botão Lateral 1 (X1)", L"Botão Lateral 2 (X2)"
        };
        for (int i = 0; i < BTN_COUNT; ++i) {
            HWND chk = CreateWindowW(L"BUTTON", names[i],
                                     WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                     16, 184 + i * 32, 400, 28, parent,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TRIGGER_BASE + i)),
                                     inst, nullptr);
            SendMessageW(chk, WM_SETFONT, reinterpret_cast<WPARAM>(font_ui), TRUE);
            if (i == BTN_LEFT) SendMessageW(chk, BM_SETCHECK, BST_CHECKED, 0);
        }

        HWND ov = CreateWindowW(L"BUTTON", L"Ativar Sobreposição de Tela (Overlay)",
                                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                16, 360, 400, 28, parent, reinterpret_cast<HMENU>(IDC_OVERLAY), inst, nullptr);
        SendMessageW(ov, WM_SETFONT, reinterpret_cast<WPARAM>(font_ui), TRUE);
    }

    void BuildTestPage(HWND parent, int w, int h) {
        HWND hint = CreateWindowW(L"STATIC",
                                  L"Dê um duplo-clique aqui para testar o CPS.\nCada clique gerará uma onda de água.",
                                  WS_CHILD | WS_VISIBLE | SS_CENTER,
                                  10, 8, w - 20, 40, parent, reinterpret_cast<HMENU>(IDC_TEST_HINT), inst, nullptr);
        SendMessageW(hint, WM_SETFONT, reinterpret_cast<WPARAM>(font_ui), TRUE);

        WNDCLASSW cwc{};
        cwc.lpfnWndProc = &App::CanvasProc;
        cwc.hInstance = inst;
        cwc.lpszClassName = L"ACM3Canvas";
        cwc.hbrBackground = brush_canvas;
        cwc.hCursor = LoadCursor(nullptr, IDC_CROSS);
        RegisterClassW(&cwc);

        canvas = CreateWindowW(L"ACM3Canvas", L"",
                               WS_CHILD | WS_VISIBLE | WS_BORDER,
                               10, 55, w - 20, h - 70, parent, nullptr, inst, this);
    }

    void ShowTab(int index) {
        ShowWindow(page_config, index == 0 ? SW_SHOW : SW_HIDE);
        ShowWindow(page_test, index == 1 ? SW_SHOW : SW_HIDE);
    }

    void UpdateStatus() {
        if (!g_engine.master_enabled.load()) {
            SetWindowTextW(status, L"Status: DESLIGADO GERAL");
            return;
        }
        if (g_engine.AnyClicking()) {
            SetWindowTextW(status, L"Status: ATIVO");
        } else {
            SetWindowTextW(status, L"Status: INATIVO");
        }
        InvalidateRect(status, nullptr, TRUE);
    }

    void UpdateOverlay() {
        if (!overlay_on) return;
        if (g_engine.AnyClicking()) {
            overlay.SetText(L"CPS: " + std::to_wstring(g_engine.GetRealCps()), kGreen);
        } else {
            overlay.SetText(L"CPS: 0", kGray);
        }
    }

    void AddRipple(int x, int y) {
        Ripple r;
        r.x = x;
        r.y = y;
        r.radius = 5;
        r.color = kRippleColors[static_cast<int>(RandUniform(0, 4.999))];
        ripples.push_back(r);
        if (canvas) InvalidateRect(canvas, nullptr, FALSE);
    }

    void TickRipples() {
        if (ripples.empty()) return;
        for (auto& r : ripples) r.radius += 4;
        ripples.erase(std::remove_if(ripples.begin(), ripples.end(),
                                     [](const Ripple& r) { return r.radius > 60; }),
                      ripples.end());
        if (canvas) InvalidateRect(canvas, nullptr, FALSE);
    }

    static LRESULT CALLBACK CanvasProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        App* self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_CREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return 0;
        }
        if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);

        if (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN) {
            self->AddRipple(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        }
        if (msg == WM_PAINT) {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, self->brush_canvas);

            for (const auto& r : self->ripples) {
                HPEN pen = CreatePen(PS_SOLID, r.radius > 40 ? 1 : 2, r.color);
                HGDIOBJ oldPen = SelectObject(hdc, pen);
                HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
                Ellipse(hdc, r.x - r.radius, r.y - r.radius, r.x + r.radius, r.y + r.radius);
                HBRUSH fill = CreateSolidBrush(r.color);
                SelectObject(hdc, fill);
                Ellipse(hdc, r.x - 2, r.y - 2, r.x + 2, r.y + 2);
                SelectObject(hdc, oldBrush);
                SelectObject(hdc, oldPen);
                DeleteObject(pen);
                DeleteObject(fill);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
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
            case WM_CREATE:
                if (self) self->BuildChildren();
                return 0;

            case WM_COMMAND: {
                if (!self) break;
                const int id = LOWORD(wParam);
                if (id == IDC_MASTER) {
                    const bool on = SendMessageW(GetDlgItem(hwnd, IDC_MASTER), BM_GETCHECK, 0, 0) == BST_CHECKED;
                    g_engine.master_enabled = on;
                    SetWindowTextW(GetDlgItem(hwnd, IDC_MASTER), on ? L"LIGADO" : L"DESLIGADO");
                    if (!on) {
                        for (int i = 0; i < BTN_COUNT; ++i) g_engine.clicking_state[i] = false;
                    }
                    self->UpdateStatus();
                } else if (id == IDC_HUMANIZED) {
                    g_engine.humanized =
                        SendMessageW(GetDlgItem(self->page_config, IDC_HUMANIZED), BM_GETCHECK, 0, 0) == BST_CHECKED;
                } else if (id == IDC_OVERLAY) {
                    self->overlay_on =
                        SendMessageW(GetDlgItem(self->page_config, IDC_OVERLAY), BM_GETCHECK, 0, 0) == BST_CHECKED;
                    self->overlay.Show(self->overlay_on);
                    self->UpdateOverlay();
                } else if (id >= IDC_TRIGGER_BASE && id < IDC_TRIGGER_BASE + BTN_COUNT) {
                    const int btn = id - IDC_TRIGGER_BASE;
                    g_engine.active_triggers[btn] =
                        SendMessageW(GetDlgItem(self->page_config, id), BM_GETCHECK, 0, 0) == BST_CHECKED;
                }
                return 0;
            }

            case WM_HSCROLL:
                if (self && reinterpret_cast<HWND>(lParam) == self->cps_slider) {
                    const int cps = static_cast<int>(SendMessageW(self->cps_slider, TBM_GETPOS, 0, 0));
                    g_engine.cps = cps;
                    const std::wstring text = L"Velocidade (CPS): " + std::to_wstring(cps);
                    SetWindowTextW(self->cps_label, text.c_str());
                }
                return 0;

            case WM_NOTIFY: {
                auto* hdr = reinterpret_cast<NMHDR*>(lParam);
                if (self && hdr->idFrom == IDC_TABS && hdr->code == TCN_SELCHANGE) {
                    self->ShowTab(TabCtrl_GetCurSel(self->tabs));
                }
                return 0;
            }

            case WM_APP + 1:
                if (self) self->UpdateStatus();
                return 0;

            case WM_TIMER:
                if (!self) return 0;
                if (wParam == kTimerOverlay) self->UpdateOverlay();
                if (wParam == kTimerRipple) self->TickRipples();
                return 0;

            case WM_CTLCOLORSTATIC: {
                HDC hdc = reinterpret_cast<HDC>(wParam);
                HWND ctrl = reinterpret_cast<HWND>(lParam);
                SetBkMode(hdc, TRANSPARENT);
                if (self && ctrl == self->status) {
                    if (!g_engine.master_enabled.load()) SetTextColor(hdc, kGray);
                    else if (g_engine.AnyClicking()) SetTextColor(hdc, kGreen);
                    else SetTextColor(hdc, kRed);
                    return reinterpret_cast<LRESULT>(self->brush_bg);
                }
                SetTextColor(hdc, kText);
                if (self && (GetParent(ctrl) == self->page_config || GetParent(ctrl) == self->page_test)) {
                    return reinterpret_cast<LRESULT>(self->brush_bg);
                }
                return reinterpret_cast<LRESULT>(self ? self->brush_bg : reinterpret_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH)));
            }

            case WM_CTLCOLORBTN: {
                HDC hdc = reinterpret_cast<HDC>(wParam);
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, kText);
                return reinterpret_cast<LRESULT>(self ? self->brush_bg : reinterpret_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH)));
            }

            case WM_ERASEBKGND: {
                RECT rc;
                GetClientRect(hwnd, &rc);
                FillRect(reinterpret_cast<HDC>(wParam), &rc, self ? self->brush_bg : reinterpret_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH)));
                return 1;
            }

            case WM_DESTROY:
                if (self) self->Destroy();
                PostQuitMessage(0);
                return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
};

// ---------------------------------------------------------------------------
// Entry
// ---------------------------------------------------------------------------

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    timeBeginPeriod(1);

    App app;
    if (!app.Create(hInstance)) {
        timeEndPeriod(1);
        return 1;
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    timeEndPeriod(1);
    return static_cast<int>(msg.wParam);
}
