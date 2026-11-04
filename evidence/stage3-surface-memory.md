# Stage 3 — surface lifetime, memory floor, and the first real presents

All facts below were observed on the device (`root@192.168.50.135`, TrimUI Smart Pro, Mali-G57).

## 1. `EGL_BAD_SURFACE` on the second `nvnQueueSubmitCommands` — fixed

Diagnostic (`NVN_SURFACE_DIAG`) at the failing acquire:

```
tag=fail engine=multi device=0x55bb6d4540 window=0x55bb6d4540 same=1
cur-draw=(nil) cur-read=(nil) cur-ctx=(nil) query=0/0/0 size=-1x-1 cfg=-1 bind-error=0x300d
```

- `device == window`: the queue was already pointing at the window surface, not the 8x8 pbuffer.
- `eglQuerySurface` on that very surface failed with `0x300d`, so the surface was no longer valid —
  our pointer (taken when the window was built) had gone stale.
- The submit also runs on a different guest thread that has no current context (`cur-ctx=(nil)`), so
  the failure is the surface, not the context.

Cause: SDL owns the window's EGLSurface and rebinds/destroys it when a foreign context becomes
current inside our present path. Fix in `nvn_present.h`: after every swap, re-publish the live
surface (`eglGetCurrentSurface(EGL_DRAW)`) into the window/device/diagnostic slots.

Result: `NVN_WINDOW_SURFACE_REPUBLISH old=0x55978f6540 live=0x7f74a84090`, and the following submits
succeed — the runtime now presents frames 1..120 instead of stopping at frame 1.

## 2. `NVN_COMMAND_REUSE_BEFORE_COMPLETION` (handle=2) — workaround

With the automatic GPU pipeline the lists are submitted asynchronously, but the completion poll
(`nvn_command_pending_poll`) is only installed on the LC2 async path. In synchronous mode
`nvn_replay.h` marks every list completed at submit end, so `T2_GPU_PIPELINE=0` clears the guard.
This is a bring-up setting, not a fix for the async path.

## 3. `T2_MEMORY_FLOOR`

Observed footprint while loading: `vmrss 689 MiB`, 92 GL textures totalling 385 MiB (three
1920x1080 colour targets plus a 2048x4096 = 32 MiB texture), `MemAvailable` falling to 62 MiB.
The default floor of 140 MiB stopped the run during asset load. `T2_MEMORY_FLOOR_MIB=32` survives
(no OOM, no kswapd stall in the log), and `MemAvailable` settles at ~273 MiB once loading pauses.

## 4. Loader watchdog

`MEGAMAN_PROBE_SECONDS` defaults to 20 s; the first 30 s run ended in `TIME_LIMIT` while the game
was still loading. Raised to 240 s for bring-up.

## 5. Current picture state

- 120 frames presented, every single one `nonblack=0 max=0` → `NVN_PRESENT_BLACK` stop.
- Frame period is ~10 s early on (asset decode); draws per frame grow 1 → 40.
- cbuf check: the emitted ESSL for these programs only contains literal constant indices 0..10
  (checked with `tools/shader_literals.py`), so the zero-padded 64 KiB tail of the RGBA32F bank is
  never read; the 48/64/384-byte guest ranges are read from offset 0 as they are bound.
- `kmsgrab` cannot capture while the game runs ("No active framebuffers found on planes"), so pixel
  evidence stays the runtime's own backbuffer readback until the DRM plane is re-attached.
