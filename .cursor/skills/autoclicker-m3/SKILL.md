---
name: autoclicker-m3
description: >-
  Domain knowledge for Auto Clicker M3 Pro (Python primary + C++ secondary).
  Explains architecture, double-click+hold trigger, ClickEngine anti-feedback,
  UI controls, build/release, and constraints. Use when changing python/main.py,
  cpp/main.cpp, python/build.py, click behavior, CPS, triggers, overlay, UI, or packaging.
---

# Auto Clicker M3 Pro

## Product model

Auto-clicker for Windows. Trigger is **double-click then hold** on an enabled mouse button — not keyboard hotkeys, not fixed coordinates.

1. Physical double-click on an enabled button (interval < 0.3 s)
2. Keep holding → inject clicks at configured CPS
3. Release → stop immediately

## Architecture

Two implementations at the same hierarchy level:

| Path | Role |
|------|------|
| `python/main.py` | **Primary** — Python / CustomTkinter / pynput |
| `cpp/main.cpp` | **Secondary** — C++ / Win32+GDI+ / `WH_MOUSE_LL` + `SendInput` |

### Python (`python/main.py`)

| Class | Role |
|-------|------|
| `ClickEngine` | Global mouse listener, click loop, real CPS |
| `OverlayWindow` | Always-on-top CPS HUD |
| `AutoClickerApp` | CustomTkinter UI |

Threads: pynput `Listener`, daemon `_click_loop`, Tk mainloop. UI updates from engine use `self.after(0, ...)`.

### C++ (`cpp/main.cpp`)

Same roles in one translation unit: `ClickEngine` (LL hook + click thread), `OverlayWindow`, custom GDI+ `App` UI (rounded cards, switches, slider). Status updates via `PostMessage(WM_APP+1)`.

## Invariants (do not break)

- Simulated press/release must set `ignore_next_press` / `ignore_next_release` **before** injection so the listener does not treat script clicks as user input.
- Releasing the physical button must always clear `clicking_state` and stop clicking.
- Respect `master_enabled` and `active_triggers` before activating or injecting.
- Settings are **in-memory only** (no disk persistence today).
- Keep Python and C++ behavior aligned when changing the trigger/CPS model.

## Stack

- **Primary:** Python 3.12 (CI), `customtkinter` + `tkinter`, `pynput`
  - Build: `cd python && python build.py` → `python/dist/AutoClickerM3.exe` (PyInstaller onefile, windowed)
  - Release: `.github/workflows/release.yml` on tag `v*` or manual dispatch
- **Secondary:** C++17, Win32 + GDI+ (no third-party deps)
  - Build: `cmake -S cpp -B cpp/build -G "MinGW Makefiles"` then `cmake --build cpp/build`
  - Output: `cpp/build/bin/AutoClickerM3Cpp.exe`
  - Run: root `run.bat` / `run.sh` → Python; `python/run.*` and `cpp/run.*` for each stack

## When editing

1. Read [reference.md](reference.md) for full behavior, UI defaults, timing, and limitations.
2. Preserve the double-click+hold model unless the user explicitly asks to change it.
3. Keep Portuguese UI strings consistent with existing labels.
4. After engine changes, mentally verify: activate (double-click+hold) → inject → physical release stops → simulated events ignored.
5. If changing trigger/CPS/anti-feedback, update **both** `python/main.py` and `cpp/main.cpp` unless the user asks for only one.
