# ShakeFindCursor — Shake mouse to enlarge pointer (macOS-style)

English | [简体中文](README.md)

[![CI](https://github.com/avello1000/ShakeFindCursor/actions/workflows/ci.yml/badge.svg)](https://github.com/avello1000/ShakeFindCursor/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/avello1000/ShakeFindCursor)](https://github.com/avello1000/ShakeFindCursor/releases)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

A tiny Windows tray utility that mimics macOS **"Shake mouse pointer to locate"**: **shake your mouse back and forth quickly** and the pointer smoothly enlarges to ~3× so you can find it; it smoothly shrinks back when you stop.

## Features

- 🔍 **Shake to locate** — aligned with the macOS feel: a turning-point based oscillation analysis (x/y axes counted separately, each swing must retreat ≥ 24 px from its extreme, speed ≥ 550 px/s, net displacement / path ≤ 0.45). **Moving in one direction — no matter how fast — never triggers.**
- 🎬 **Smooth zoom** — easeInOutCubic easing (zero velocity at both ends); animation frame rate locked to the refresh rate of the monitor the pointer is on (absolute time grid, no drift, frames are dropped rather than slowed).
- 🖥️ **Multi-monitor marquee** — with 2+ monitors, the screen the pointer is on lights up along all four edges with a **Siri-style RGB flowing glow** (full rainbow hue rotating around the screen + traveling bright spots + near-white filament core). Feathered corners via overlapping edge strips; exact arc-length continuity — the rainbow truly wraps around corners. Doesn't appear on single-monitor setups.
- 🪶 **Imperceptible idle cost** — no `timeBeginPeriod` (doesn't raise the whole system's clock resolution); a high-resolution waitable timer drives everything; when the cursor doesn't move, all kinematics are skipped. Idle CPU below the measurement granularity.
- 🎨 **Crisp pointer** — GDI+ anti-aliased vector arrow (white fill + dark outline + soft shadow), bitmaps cached per size.
- 🌐 **Tray resident** — right-click menu: enable/disable shake-zoom, multi-monitor marquee toggle, run at startup, exit.
- 🛟 **Never strands an enlarged pointer** — four layers of restoration: tray exit, logoff/shutdown, crash filter, and a **watchdog child process** that restores the cursor even when the app is killed via Task Manager. Also self-heals on startup.
- 🧯 **One-click restore** — double-click `restore-cursor.bat`, or run `ShakeFindCursor.exe --restore`.
- ✅ **No admin rights needed** — `SetSystemCursor` works from a normal process; the manifest is `asInvoker`, so no UAC prompt.

![Siri-style RGB border glow (difference image — only the glow itself)](docs/screenshot-marquee.png)

![Pointer rendering at five sizes; red crosses mark the hotspot](docs/screenshot-pointer.png)

## Build

```bat
build.bat
```

Produces `ShakeFindCursor.exe` (version info + app icon from `app.rc`). Requires Visual Studio 2022 (any edition with the **Desktop development with C++** workload); the script locates MSVC via `vswhere`. Every push to `main` is built automatically by [GitHub Actions](.github/workflows/ci.yml).

Alternatively, use CMake:

```bat
cmake -S . -B build -A x64
cmake --build build --config Release
```

## Usage

```bat
ShakeFindCursor.exe              :: runs in the tray; right-click for the menu
ShakeFindCursor.exe --restore    :: restore the cursor immediately (no UAC)
```

If the pointer ever stays enlarged, double-click **`restore-cursor.bat`**.

## Trigger logic: shake only (v4.3)

macOS's shake-to-find has **exactly one trigger path — rapid back-and-forth movement**. This tool does the same. The oscillation count uses turning-point analysis (`CountOscillations`): x and y axes are scored separately and the larger value counts, so horizontal, vertical, diagonal and circular shaking are all recognized.

| Condition | Threshold | Purpose |
|---|---|---|
| Valid swings in window | ≥ `kShakeReversals` (3) | at least 3 swings within `kShakeWindowSec` (0.55 s) |
| Per-swing amplitude | ≥ `kShakeMinAmpPx` (24 px) | must retreat ≥ 24 px from an extreme — hand tremor never qualifies |
| Recent speed | ≥ `kShakeSpeed` (550 px/s) | it must be a *fast* shake |
| Net displacement / path | ≤ `kShakeMaxNetRatio` (0.45) | excludes dragging in one direction |

A second channel ("fast straight swipe", `kEnableSwipe`) exists but is **off by default** — macOS has no such mechanism, and normal pointer travel (~1250 px/s) would trigger constantly.

**Recovery**: when the short-window speed (0.22 s window) stays below `kCalmSpeed` (150 px/s) for `kCalmDelayMs` (180 ms) and at least `kMinHoldMs` (300 ms) have passed since the trigger, the pointer smoothly shrinks back. Moving again mid-shrink immediately re-enlarges.

## Tuning

All parameters live in `namespace Config` at the top of `main.cpp` (see the Chinese README for the full table). The single most effective knob against false triggers is `kShakeMinAmpPx` (raise to 35–45; lower to 15 if shakes don't register).

## Performance (measured on a 165 Hz panel)

| Metric | Value |
|---|---|
| Frame interval target / actual | 6.0606 ms / 5.55–6.62 ms |
| **Idle CPU** | below measurement granularity (0.0 ms over 6 s) |
| CPU per zoom animation | ~60 ms (was ~97 ms) |

Per-frame cost was cut by caching rendered bitmaps per size: `HCURSOR` handles cannot be cached (`SetSystemCursor` destroys them), but the bitmaps inside `ICONINFO` can — `CreateIconIndirect` copies them. Draw cost dropped from ~52 ms to ~9 ms per animation; the remaining `SetSystemCursor` (~50 ms) is an irreducible system call.

## How the restoration guarantee works (v4.4)

`SetSystemCursor` replaces the system cursor **globally** — if the process dies without restoring, the enlarged cursor stays. Verified end-to-end: graceful exit already restores correctly (`32px → 96px → 32px`); only a hard kill leaves residue. Four layers of protection:

| Layer | Scenario | Implementation |
|---|---|---|
| 1 | Tray exit / `WM_DESTROY` | `RestoreSystemCursor()` |
| 2 | Logoff / shutdown | `WM_QUERYENDSESSION` / `WM_ENDSESSION` |
| 3 | Crash | `SetUnhandledExceptionFilter` restores first |
| 4 | **Killed via Task Manager** | **watchdog child process** (`--watchdog <pid>`) waits for the parent and restores |

The app also restores once on startup, healing any residue from a previous hard kill.

> ⚠️ Key detail: per MSDN, `SetSystemCursor` **destroys** the passed `HCURSOR` on success — handles must be created fresh every time (caching them is what broke the very first version). Only the bitmaps are cacheable.

## Multi-monitor marquee (v4.6 / v4.7)

Each screen edge gets a layered, click-through, topmost window (`WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | ...`) composited via `UpdateLayeredWindow` with per-pixel alpha. All four strips run the full length of their edge and **overlap at the corners** — the over-composite approximates a union (`1-(1-a)(1-b) ≥ max(a,b)`), which is exactly what a neon tube bent around a corner looks like. Arc-length parameters are continuous around the loop (L = 2(W+H)−4), so hue and brightness phase never jump at corners.

The marquee runs at 50 fps (deliberately not tied to the refresh rate — it's a slow glow). Strips are hidden, not destroyed, and reused.

> ⚠️ Measurement pitfalls (learned the hard way): screen-capture verification tools **must** declare per-monitor DPI awareness (otherwise a 200%-scaled primary monitor hands them a scaled, misaligned desktop copy), and corner-smoothness scans must walk the saturated glow region, not the white-hot filament core (pale colors over a white taskbar produce deceptively small per-channel diffs).

## Limitations

- Only `OCR_NORMAL` (the main arrow) is replaced; text beams, hands etc. stay system-sized.
- Slider-like operations that naturally move back and forth can trigger the zoom — inherent to shake detection (same on macOS).
- The bottom glow band overlays the taskbar (TOPMOST) without intercepting clicks.
- A watchdog process with the same name appears in Task Manager (~10 MB) — by design.

## Verification tools

Two test programs ship with the repo (build with the same `cl` lines as CI):

- **`e2e-test`** — end-to-end regression: `shake` (enlarge + auto-shrink), `shakekill` (kill while enlarged, verify watchdog), `exit` (graceful path), `marquee` (enumerate marquee strips), `width` (report arrow-slot bitmap size).
- **`marquee-test`** — visual verification: screen captures → difference image (desktop content cancels out) → per-edge sampling → per-corner smoothness scan.

> When measuring the cursor, read the **`IDC_ARROW` slot** (`LoadImageW(nullptr, IDC_ARROW, ...)`) — the "currently displayed cursor" is unreliable.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Security reports: [SECURITY.md](SECURITY.md). Changelog: [CHANGELOG.md](CHANGELOG.md).

## License

[MIT](LICENSE) © 2026 avello1000
