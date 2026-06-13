## Nvidia Control Panel
- Open Nvidia Control Panel
- Select `Set up stereoscopic 3D`
- Select `3D Vision Discover` as the display type (anaglyph mode — works on any display, no EDID override required)
- Set Depth to `100%`
- Set Keyboard Shortcuts -> `Enable advanced in-game settings`
- Check `Enable stereoscopic 3D` and click `Apply`
- Close Nvidia Control Panel


## Install and Verify the 3D Fix
- Install the game's 3D fix using 3D Fix Manager or manually from <a href="https://helixmod.blogspot.com/2013/10/game-list-automatically-updated.html" target="_blank" rel="noopener noreferrer">HelixMod</a>
- Launch the game and confirm the fix is working — you should see Anaglyph 3D (red/cyan) with depth
- In the game's video settings, set the resolution to fullscreen `1920x1080` for the best font scaling and performance
  - The injector upscales each eye to your display's native resolution, so 1080p source is fine for 4k panels, SR displays, AR glasses, etc.
- Exit the game


## 3DVision4All Injector Installation
- The injector is a proxy DLL dropped next to the game's executable. It hooks DirectX 9 and NvAPI Stereo to capture each eye and composes the stereo output for your display — no separate Shader Glass window required, no display EDID override required, no extra hardware (EDID emulator, dummy plug, capture card, secondary display) required
- **DirectX 9 / DX9Ex only.** The injector does not work with DX10, DX11, or DX12 games. For DX10–12 titles use one of the legacy methods (Shader Glass for non-native displays, native 3DVision via the Old Driver path on RTX 20-series or older) instead
- Download the latest [3DVision4All injector](https://github.com/oneup03/3DVision4All/releases/latest) zip and extract it
  - For 32-bit games (most older DX9 games), use the files in `Win32-Release`
  - For 64-bit games, use the files in `x64-Release`
- Copy ONE of the proxy DLLs (`dinput8.dll`, `dsound.dll`, `version.dll`, `winmm.dll`) from the extracted folder into the same folder as the game's EXE
  - Pick one the game doesn't already ship with so you don't overwrite anything important — `dinput8.dll` is usually safe
- Copy `3dvision4all.ini` from the extracted zip into the same folder, next to the proxy DLL


## Injector Options Reference
`3dvision4all.ini` sits next to the proxy DLL. Every option also has an inline comment in the file itself. Per-guide `mode` selection is in the next section.

### `[stereo]`
- `mode` — stereo output format. Pick the value matching the guide you're following:
  - `sbs` — Side-by-Side (3D TVs in "Side by Side (Half)", AR glasses on 32:9 panels)
  - `tab` — Top-and-Bottom (3D TVs in TaB input mode)
  - `row_interlaced` — even rows = left eye, odd rows = right (most passive 3D displays)
  - `column_interlaced` — column variant (rare passive setups)
  - `checkerboard` — DLP 3D-Ready TVs (Mitsubishi / Samsung DLP)
  - `leiasr` — LeiaSR / Simulated Reality autostereoscopic displays
  - `katanga` — publish the stereo image over Katanga IPC for a VR viewer (Katanga.exe, VRScreenCap)
- `swap_eyes` — flip left/right. Set to `1` if 3D looks reversed

### `[render]`
- `defeat_directflip` (default `1`) — keeps the overlay visible over fullscreen-borderless games. Leave on unless you're isolating a crash
- `force_windowed` (default `1`) — forces the game into windowed mode so the overlay can sit on top. Set to `0` only for games whose startup breaks when forced windowed
- `disable_vsync` (default `0`) — frees the game's frame loop from refresh-rate; the overlay still presents at refresh rate so the on-screen output stays smooth
- `confine_cursor` (default `0`) — locks the OS cursor inside the game window while focused; released on alt-tab
- `hide_cursor` (default `0`) — hides the OS cursor while it's over the game window
- `alternate_capture_mode` (default `0`) — how frames are handed off to the overlay:
  - `0` CPU copy — widest compatibility, slower (one extra GPU→CPU→GPU round-trip per frame, mostly noticeable above 1080p)
  - `1` GPU-shared — faster and supports higher resolutions with no readback cost, but some games crash or glitch under it. Try `0` first; switch only if the game is stable
- `render_width` / `render_height` (default `0,0`) — force the game to render at this resolution. Leave at `0` to let the game decide. Useful for "Full SbS" (per-eye width × full height) or as a performance lever
  - WARNING: some games clip into the top-left corner if you force a smaller resolution than they cached at startup. If you see clipping, set both back to `0`
- `copy_width` / `copy_height` (default `1920,1080`, CPU-copy path only) — downsample each eye's readback to these dimensions. Cuts the 4K capture stall from ~16 ms/frame to almost nothing. The overlay resamples anyway, so as long as the per-eye dimensions are at least your panel half-width × panel height, the cap is essentially lossless. Leave at `0,0` for no cap

### `[debug]`
- `log_file` — log path (relative to the game's EXE if not absolute). Cleared each run
- `log_level` — `0` off, `1` info (default), `2` verbose
- `install_device_hooks`, `install_d3d9_vtable_hooks`, `install_d3d9_display_mode_hooks` — all default `1`. Diagnostic bisect switches for crashes; leave alone unless you're tracking down a regression
