# CLAUDE.md

HyperHEN: modular PS5 homebrew payload stack (C/C++, CMake, PS5 Payload SDK).
One entry ELF (`build/bin/HyperHEN.elf`) routes by exact firmware profile,
prepares the system, injects a ShellUI Toolbox, then runs overlays, cheats,
plugins and services. Overview: `README.md`, `docs/COMPONENTS_AND_TECHNICAL_OVERVIEW.md`,
`docs/arch.md` (Chinese, most detailed).

## Build / test

- Full build: `./scripts/build.sh --jobs 8` (or `docker compose run --rm hyperhen-build`).
- Host unit tests (no PS5 SDK): `cd source/util/tests && make test`.
  FPS-related suites: `test_fps_formula.cpp`, `test_overlay_layout.cpp`, `test_overlay_text_metrics.cpp`.
- `third_party/` is vendored; don't edit it for HyperHEN changes.

## Layout

- `source/bootstrapper`, `source/unpacker` — entry ELF and embed chain.
- `source/daemon` — privileged background process (IPC, injection, FPS sampler, plugins).
- `source/shellui` — PRX injected into SceShellUI (Mono hooks, Toolbox pages, game overlay bar).
- `source/util` — utility process (cheats, etc.) + host tests.
- `source/libonion_*` — shared static libs (see table in `docs/arch.md` §2.6).
- Config: `config.ini` on console; schema in `libonion_settings`, sample `config.ini.example` (`[overlay]`).
- Cross-process state lives under `/system_tmp/hyperhen/` (`source/include/onion/system_tmp.h`).

## FPS overlay — how it works

Two halves, connected only by a 128-byte shared-memory file. The game is never
ELF/SPRX-injected for FPS (see `docs/sprx-loader.md` "Why this is not wired into FPS").

### 1. Daemon sampler (`source/daemon/source/daemon_fps.cpp`, lib `source/libonion_fps`)

Two threads started in `source/daemon/source/main.cpp`, both tick every 33.3 ms,
both idle when `overlay.enabled && overlay.show_fps` is false or no big app runs:

- `vsync_fps_sampler_thread` — Tier 1A scanout: `/dev/dce` ioctl
  (`fps_dce.cpp`, flip counter at outbuf+8; retries sign-extended request on EINVAL).
  Rate is handed to the render thread through an in-process seqlock (`VsyncBus`),
  keyed by title-id hash, stale after 2 s. Skipped for PS4 BC titles.
- `fps_sampler_thread` — the only writer of the published record. Per title type:
  - **PS5 native (`PPSA`/`PPSB`)**: reads counters from the game's memory via
    DMAP physical translation (`fps_agc.cpp`): Tier 1E DCB ring at fixed VA
    `0xFE03B72EC`, Tier 1F global submit counter at `libSceAgcDriver.sprx` base + `0x1AAC0`.
  - **PS4 BC (`CUSA`, `PCAS`, `PCJS`, `PCKS`, `CUHJ`)**: `fps_bc.cpp` patches a
    14-byte jmp detour into `sceGnmSubmitAndFlipCommandBuffers*` (resolved by NID in
    `libSceGnmDriver`) via libhijacker; trampoline does `inc qword [counter]`. Daemon
    reads the counter. Patch stays until the game exits; install retried every 500 ms.
- Rate = Δcount/Δt (`fps_formula.cpp::hz_from_delta`, accepts 1–10000 Hz raw).
- `compose()` picks the value: ring > global for render rate; after 100-sample
  calibration, if avg render/scanout ratio > 2.5 the title is "multi-pass" and
  scanout is used instead. If render is missing/out of 1–240 range, scanout is the
  fallback. Output smoothed by a 5-sample rolling mean. `source` bitmask
  (`OnionFpsSource`) records which path produced it.
- Publish: `fps_publish.cpp` mmaps `/system_tmp/hyperhen/fps_sample` and writes an
  `OnionFpsSample` under a seqlock.

### 2. ShellUI display (`source/shellui/src/prx_overlay.cpp`)

- `OnRender_Hook` hooks Mono `Application.Update` (installed in `source/shellui/src/prx.cpp`).
- After 30 frames, `init_overlay_once` builds widgets (`CreateGameWidget(CREATE_FPS_OVERLAY)`
  etc., `shellui_overlay_widgets.cpp`).
- Every 2 frames: `format_fps_text` → `onion_fps_read` (header-only reader in
  `libonion_fps/include/onion/fps_sample.h`; ShellUI must NOT link `libonion_fps`).
  Invalid / wrong magic / older than 2 s → `"--"`; ≥100 → `%.0f`, else `%.1f`.
- Every 60 frames: CPU/GPU/RAM/IP/fan metrics sampled directly in ShellUI.
- `layout_bar_labels` measures text and lays out the bar (`overlay_layout.hpp`,
  `overlay_metrics.hpp`).

### Standalone FPS monitor (`source/fps_monitor/` → `build/bin/fps-monitor.elf`)

- Host (`source/main.cpp`): compiles `daemon_fps.cpp` unchanged, provides the
  globals it expects (`g_settings`, `LoadSettings`, `g_stack_shutting_down`,
  `kernel_base`), and once per second injects `monitor_shellui.elf` into SceShellUI
  via `ToolboxInject(ONION_READY_FPS_MONITOR_UI)` + libNineS `Inject_Toolbox`
  (covers rest-mode resume and ShellUI restart). Exits if HyperHEN or another
  monitor instance is running.
- ShellUI side (`shellui/main.cpp`): hooks only `PUI.Application.Update`, then reuses
  `prx_overlay.cpp` built with `HYPERHEN_FPS_MONITOR` (Toolbox polls compiled out).
  It shares `mono_widgets.cpp` and `overlay_layout_apply.cpp` with HyperHEN's shellui.
- Policy: always FPS on, IP/fan off; CPU/GPU/MEM and layout come from `[overlay]` in
  `config.ini` if present (never created). Logs: `/data/fps-monitor/fps-monitor.log`.
- Build alone: `cmake --build build --target fps_monitor`.

### Debugging FPS

Daemon logs `fps-diag:` lines every 2 s (status enums per source: DCE, AGC ring/global,
BC hook, vsync bus, calibration). `scripts/daemon_log.py` streams the console log over the network.
Sampling approach credited to PHU Games Tools (ArkSama) — "PHU" in comments refers to it.
