/**
 * Auto Clicker M3 Pro — C++ / MSVC / WebView2
 *
 * Modelo (igual ao Python):
 *   1) Duplo-clique físico (< 300 ms) em botão habilitado
 *   2) Manter pressionado → injeta cliques na CPS
 *   3) Soltar → para imediatamente
 *
 * Anti-feedback: ignore_next_press / ignore_next_release antes de SendInput.
 *
 * Build:
 *   cmake -S cpp -B cpp/build -G "Visual Studio 18 2026" -A x64
 *   cmake --build cpp/build --config Release
 */

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <mmsystem.h>
#include <wrl.h>

#include <WebView2.h>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <deque>
#include <mutex>
#include <random>
#include <string>
#include <thread>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "winmm.lib")

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

// ---------------------------------------------------------------------------
// Tipos
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
static constexpr UINT WM_APP_STATUS = WM_APP + 1;
static constexpr UINT WM_APP_WEBMSG = WM_APP + 2;

static constexpr COLORREF kGreen = RGB(0, 230, 118);
static constexpr COLORREF kGray = RGB(158, 158, 158);

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

static std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

static std::string Narrow(const std::wstring& s) {
    if (s.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}

static bool JsonGetBool(const std::string& json, const char* key, bool fallback = false) {
    const std::string needle = std::string("\"") + key + "\"";
    const auto pos = json.find(needle);
    if (pos == std::string::npos) return fallback;
    const auto colon = json.find(':', pos + needle.size());
    if (colon == std::string::npos) return fallback;
    const auto t = json.find("true", colon);
    const auto f = json.find("false", colon);
    if (t != std::string::npos && (f == std::string::npos || t < f)) return true;
    if (f != std::string::npos) return false;
    return fallback;
}

static int JsonGetInt(const std::string& json, const char* key, int fallback = 0) {
    const std::string needle = std::string("\"") + key + "\"";
    const auto pos = json.find(needle);
    if (pos == std::string::npos) return fallback;
    const auto colon = json.find(':', pos + needle.size());
    if (colon == std::string::npos) return fallback;
    try {
        return std::stoi(json.substr(colon + 1));
    } catch (...) {
        return fallback;
    }
}

static std::string JsonGetString(const std::string& json, const char* key) {
    const std::string needle = std::string("\"") + key + "\"";
    const auto pos = json.find(needle);
    if (pos == std::string::npos) return {};
    const auto colon = json.find(':', pos + needle.size());
    if (colon == std::string::npos) return {};
    const auto q1 = json.find('"', colon + 1);
    if (q1 == std::string::npos) return {};
    const auto q2 = json.find('"', q1 + 1);
    if (q2 == std::string::npos) return {};
    return json.substr(q1 + 1, q2 - q1 - 1);
}

static std::wstring ExeDir() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring full(path);
    const auto slash = full.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : full.substr(0, slash);
}

static std::wstring UiIndexPath() {
    return ExeDir() + L"\\ui\\index.html";
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
        if (hwnd_ui) PostMessageW(hwnd_ui, WM_APP_STATUS, 0, 0);
    }

    static int ButtonFromMsg(WPARAM wParam, const MSLLHOOKSTRUCT* info) {
        switch (wParam) {
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP: return BTN_LEFT;
            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP: return BTN_RIGHT;
            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP: return BTN_MIDDLE;
            case WM_XBUTTONDOWN:
            case WM_XBUTTONUP: {
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
            case BTN_X1:
            case BTN_X2: return MOUSEEVENTF_XDOWN;
            default: return 0;
        }
    }

    static DWORD UpFlag(int btn) {
        switch (btn) {
            case BTN_LEFT: return MOUSEEVENTF_LEFTUP;
            case BTN_RIGHT: return MOUSEEVENTF_RIGHTUP;
            case BTN_MIDDLE: return MOUSEEVENTF_MIDDLEUP;
            case BTN_X1:
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
                const double base = 1.0 / (std::max)(1, cps.load());
                double sleep_time = humanized.load() ? RandUniform(base * 0.7, base * 1.3) : base;
                SleepSec((std::max)(0.001, sleep_time - 0.025));
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
    HFONT font = nullptr;
    std::wstring text_ = L"CPS: 0";
    COLORREF color_ = kGreen;

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
            GetSystemMetrics(SM_CXSCREEN) - 250, 50, 220, 70,
            nullptr, nullptr, inst, this);

        SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);
        font = CreateFontW(36, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, FF_SWISS, L"Segoe UI");
        ShowWindow(hwnd, SW_HIDE);
        return hwnd != nullptr;
    }

    void Destroy() {
        if (font) { DeleteObject(font); font = nullptr; }
        if (hwnd) { DestroyWindow(hwnd); hwnd = nullptr; }
    }

    void Show(bool visible) {
        if (hwnd) ShowWindow(hwnd, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
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
            RECT rc{};
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
};

// ---------------------------------------------------------------------------
// App (WebView2 host)
// ---------------------------------------------------------------------------

struct App {
    HINSTANCE inst = nullptr;
    HWND hwnd = nullptr;
    OverlayWindow overlay;
    bool overlay_on = false;

    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;

    bool Create(HINSTANCE hInstance) {
        inst = hInstance;

        WNDCLASSW wc{};
        wc.lpfnWndProc = &App::WndProc;
        wc.hInstance = inst;
        wc.lpszClassName = L"ACM3WebHost";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        RegisterClassW(&wc);

        hwnd = CreateWindowExW(
            0, L"ACM3WebHost", L"Auto Clicker M3 Pro",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
            CW_USEDEFAULT, CW_USEDEFAULT, 520, 780,
            nullptr, nullptr, inst, this);

        if (!hwnd) return false;

        overlay.Create(inst);
        g_engine.Start(hwnd);
        SetTimer(hwnd, kTimerOverlay, 100, nullptr);
        InitWebView();

        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
        return true;
    }

    void Destroy() {
        KillTimer(hwnd, kTimerOverlay);
        g_engine.Stop();
        overlay.Destroy();
        webview = nullptr;
        controller = nullptr;
    }

    void InitWebView() {
        const std::wstring userData = ExeDir() + L"\\webview2-data";
        CreateDirectoryW(userData.c_str(), nullptr);

        CreateCoreWebView2EnvironmentWithOptions(
            nullptr, userData.c_str(), nullptr,
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [this](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                    if (FAILED(result) || !env) {
                        MessageBoxW(hwnd,
                            L"Falha ao criar o ambiente WebView2.\n"
                            L"Instale o Microsoft Edge WebView2 Runtime.",
                            L"Auto Clicker M3 Pro", MB_ICONERROR);
                        return result;
                    }
                    return env->CreateCoreWebView2Controller(
                        hwnd,
                        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [this](HRESULT result, ICoreWebView2Controller* ctrl) -> HRESULT {
                                if (FAILED(result) || !ctrl) return result;
                                controller = ctrl;
                                controller->get_CoreWebView2(&webview);
                                ResizeWebView();

                                ComPtr<ICoreWebView2Settings> settings;
                                webview->get_Settings(&settings);
                                if (settings) {
                                    settings->put_AreDevToolsEnabled(FALSE);
                                    settings->put_AreDefaultContextMenusEnabled(FALSE);
                                    settings->put_IsStatusBarEnabled(FALSE);
                                }

                                webview->add_WebMessageReceived(
                                    Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                        [this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                            LPWSTR raw = nullptr;
                                            if (SUCCEEDED(args->TryGetWebMessageAsString(&raw)) && raw) {
                                                std::wstring msg(raw);
                                                CoTaskMemFree(raw);
                                                auto* heap = new std::wstring(std::move(msg));
                                                PostMessageW(hwnd, WM_APP_WEBMSG, 0, reinterpret_cast<LPARAM>(heap));
                                            }
                                            return S_OK;
                                        }).Get(),
                                    nullptr);

                                const std::wstring index = UiIndexPath();
                                if (GetFileAttributesW(index.c_str()) == INVALID_FILE_ATTRIBUTES) {
                                    MessageBoxW(hwnd,
                                        L"UI nao encontrada (ui/index.html ao lado do .exe).",
                                        L"Auto Clicker M3 Pro", MB_ICONERROR);
                                    return S_OK;
                                }
                                const std::wstring uri = L"file:///" + index;
                                std::wstring fixed = uri;
                                for (auto& ch : fixed) if (ch == L'\\') ch = L'/';
                                webview->Navigate(fixed.c_str());
                                return S_OK;
                            }).Get());
                }).Get());
    }

    void ResizeWebView() {
        if (!controller) return;
        RECT rc{};
        GetClientRect(hwnd, &rc);
        controller->put_Bounds(rc);
    }

    void PostToWeb(const std::string& jsonUtf8) {
        if (!webview) return;
        webview->PostWebMessageAsJson(Widen(jsonUtf8).c_str());
    }

    void PushStatus() {
        std::string state = "inactive";
        if (!g_engine.master_enabled.load()) state = "off";
        else if (g_engine.AnyClicking()) state = "active";
        PostToWeb(std::string("{\"type\":\"status\",\"value\":\"") + state + "\"}");
    }

    void UpdateOverlay() {
        if (!overlay_on) return;
        if (g_engine.AnyClicking()) {
            overlay.SetText(L"CPS: " + std::to_wstring(g_engine.GetRealCps()), kGreen);
        } else {
            overlay.SetText(L"CPS: 0", kGray);
        }
    }

    void HandleWebMessage(const std::string& json) {
        const std::string type = JsonGetString(json, "type");
        if (type == "ready") {
            PushStatus();
            return;
        }
        if (type == "setMaster") {
            const bool on = JsonGetBool(json, "value", true);
            g_engine.master_enabled = on;
            if (!on) {
                for (int i = 0; i < BTN_COUNT; ++i) g_engine.clicking_state[i] = false;
            }
            PushStatus();
            return;
        }
        if (type == "setCps") {
            g_engine.cps = (std::max)(1, (std::min)(100, JsonGetInt(json, "value", 12)));
            return;
        }
        if (type == "setHumanized") {
            g_engine.humanized = JsonGetBool(json, "value", false);
            return;
        }
        if (type == "setOverlay") {
            overlay_on = JsonGetBool(json, "value", false);
            overlay.Show(overlay_on);
            UpdateOverlay();
            return;
        }
        if (type == "setTrigger") {
            const std::string button = JsonGetString(json, "button");
            const bool value = JsonGetBool(json, "value", false);
            int idx = -1;
            if (button == "left") idx = BTN_LEFT;
            else if (button == "right") idx = BTN_RIGHT;
            else if (button == "middle") idx = BTN_MIDDLE;
            else if (button == "x1") idx = BTN_X1;
            else if (button == "x2") idx = BTN_X2;
            if (idx >= 0) g_engine.active_triggers[idx] = value;
        }
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
            case WM_SIZE:
                if (self) self->ResizeWebView();
                return 0;
            case WM_APP_STATUS:
                if (self) self->PushStatus();
                return 0;
            case WM_APP_WEBMSG: {
                auto* heap = reinterpret_cast<std::wstring*>(lParam);
                if (self && heap) self->HandleWebMessage(Narrow(*heap));
                delete heap;
                return 0;
            }
            case WM_TIMER:
                if (self && wParam == kTimerOverlay) self->UpdateOverlay();
                return 0;
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
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) return 1;

    timeBeginPeriod(1);

    App app;
    if (!app.Create(hInstance)) {
        timeEndPeriod(1);
        CoUninitialize();
        return 1;
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    timeEndPeriod(1);
    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
