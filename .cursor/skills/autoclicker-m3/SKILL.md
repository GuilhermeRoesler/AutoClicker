---
name: autoclicker-m3
description: >-
  Domain knowledge for Auto Clicker M3 Pro (Python desktop auto-clicker).
  Explains architecture, double-click+hold trigger, ClickEngine anti-feedback,
  UI controls, build/release, and constraints. Use when changing main.py,
  build.py, click behavior, CPS, triggers, overlay, UI, or packaging.
---

# Auto Clicker M3 Pro

## Product model

Auto-clicker for Windows. Trigger is **double-click then hold** on an enabled mouse button — not keyboard hotkeys, not fixed coordinates.

1. Physical double-click on an enabled button (interval < 0.3 s)
2. Keep holding → inject clicks at configured CPS
3. Release → stop immediately

## Architecture

All app logic lives in `main.py`:

| Class | Role |
|-------|------|
| `ClickEngine` | Global mouse listener, click loop, real CPS |
| `OverlayWindow` | Always-on-top CPS HUD |
| `AutoClickerApp` | CustomTkinter UI |

Threads: pynput `Listener`, daemon `_click_loop`, Tk mainloop. UI updates from engine use `self.after(0, ...)`.

## Invariants (do not break)

- Simulated press/release must set `ignore_next_press` / `ignore_next_release` **before** injection so the listener does not treat script clicks as user input.
- Releasing the physical button must always clear `clicking_state` and stop clicking.
- Respect `master_enabled` and `active_triggers` before activating or injecting.
- Settings are **in-memory only** (no disk persistence today).

## Stack

- Python 3.12 (CI), `customtkinter` + `tkinter`, `pynput`
- Build: `python build.py` → `dist/AutoClickerM3.exe` (PyInstaller onefile, windowed)
- Release: `.github/workflows/release.yml` on tag `v*` or manual dispatch

## When editing

1. Read [reference.md](reference.md) for full behavior, UI defaults, timing, and limitations.
2. Preserve the double-click+hold model unless the user explicitly asks to change it.
3. Keep Portuguese UI strings consistent with existing labels.
4. After engine changes, mentally verify: activate (double-click+hold) → inject → physical release stops → simulated events ignored.
