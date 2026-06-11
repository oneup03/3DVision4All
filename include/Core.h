/*
 * This file is part of 3DVision4All.
 *
 * 3DVision4All is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * 3DVision4All is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with 3DVision4All. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

// 3DVision4All core declarations shared across the static library
// compilation units (Init.cpp, Hooks_DX9.cpp, Compose_D3D11.cpp, Config.cpp,
// Log.cpp, Output_Overlay.cpp, Output_LeiaSR.cpp) and the proxy DllMain.cpp
// wrappers.

#include <WinSDKVer.h>

#define WINVER       _WIN32_WINNT_WIN7
#define _WIN32_WINNT _WIN32_WINNT_WIN7
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef _DEBUG
    #define D3D_DEBUG_INFO
#endif

#include <d3d9.h>

#include "NktHookLib.h"
#include "nvapi.h"


// Forward-declare D3D11 interfaces so Core.h doesn't drag d3d11.h
// into every TU. The D3D11 device + context live entirely inside the
// overlay output path (Output_Overlay.cpp / Compose_D3D11.cpp /
// Output_LeiaSR.cpp), which include d3d11.h directly.
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;
struct ID3D11ShaderResourceView;
struct ID3D11RenderTargetView;


// --------------------------------------------------------------------------
// Stereo output mode (read from 3dvision4all.ini at init).

enum class StereoMode {
    Sbs = 0,            // Side-by-side. Each half of the output is one eye.
                        // Covers 3DTV SbS-in mode, AR-glasses 32:9 panels
                        // (Xreal/Viture/Rokid), and "full-SbS" if the game
                        // is rendering at half the panel width via the
                        // [render] override (so the upscale to panel-native
                        // produces a true 2W×H per-eye signal).
    Tab,                // W × H output; top half = L, bottom half = R
    RowInterlaced,      // W × H output; even rows = L, odd rows = R
    ColumnInterlaced,   // W × H output; even cols = L, odd cols = R
    Checkerboard,       // W × H output; (x+y) odd/even = L/R
    LeiaSR,             // SR weaver to a Leia / Simulated Reality display
    Katanga,            // Publish a full-SbS DX11 shared texture (R-on-left
                        // half, L-on-right half) over the Katanga IPC
                        // protocol so a separate VR viewer (Katanga.exe,
                        // VRScreenCap) can pick it up and display it in HMD.
                        // The overlay window still shows a regular SbS
                        // preview so the user can confirm capture is alive
                        // even without a VR consumer attached. See
                        // Output_Katanga.cpp for the protocol details.
};

struct Config {
    StereoMode mode = StereoMode::Sbs;

    // INI knob "swap_eyes" — request an EXTRA swap on top of the default
    // capture-side fixup. The default capture-side path already swaps the
    // halves to undo the reverse-stereo-blit's right-on-left layout, so
    // swap_eyes=0 (default) produces natural L-on-left output for every
    // downstream consumer (compose shader, LeiaSR weaver). Set to 1 only
    // in the rare case where the game's stereo capture is ALREADY in
    // natural order, in which case the default fix would reverse them and
    // this knob cancels it back out.
    bool       swap_eyes = false;

    wchar_t    log_path[MAX_PATH] = L"";
    int        log_level = 1; // 0=off, 1=info, 2=verbose

    // Add WS_EX_LAYERED + LWA_ALPHA(255) to the game's window after
    // CreateDevice/Reset. DWM can't DirectFlip a layered window's swap
    // chain, so this forces composition through the redirection surface —
    // at which point our topmost overlay actually shows over the game.
    // Default ON because most fullscreen-borderless DX9 games go into
    // DirectFlip and the overlay is invisible without this.
    int        defeat_directflip = 1;

    // Force pp->Windowed = TRUE in CreateDevice/Reset. Default ON because
    // our overlay (a layered topmost window) cannot composite over an
    // FSE swap chain — FSE bypasses DWM entirely. Set to 0 for games
    // whose init path is FSE-only and breaks when yanked into windowed;
    // with this off the overlay won't appear over a FSE game, but at
    // least the game launches.
    int        force_windowed = 1;

    // Override pp->PresentationInterval to D3DPRESENT_INTERVAL_IMMEDIATE
    // in CreateDevice/Reset. With NvAPI 3D Vision active the driver
    // halves the present rate (frame-sequential stereo means each game
    // frame becomes two driver frames), so vsync'd games at 60 Hz cap
    // out at 30 fps. Disabling vsync at the wrapper layer lets the game
    // render thread free-run. The overlay still goes through DWM and
    // composes at refresh rate, so on-screen output stays smooth.
    // Default 0 (respect the game's setting).
    int        disable_vsync = 0;

    // Cursor confinement and visibility. The two knobs are independent:
    //
    // confine_cursor (default 0): ClipCursor the cursor to the game HWND
    // each frame while the game is foreground, emulating the FSE clip
    // that force_windowed disables. Released on alt-tab.
    //
    // hide_cursor (default 0): hide the OS cursor while it's over the
    // game. Three layers of defense:
    //   1. The overlay window class uses a fully-transparent cursor so
    //      Windows doesn't draw the system arrow over the overlay region.
    //   2. The game's window class hCursor is swapped to an invisible
    //      cursor (SetClassLongPtr) so DefWindowProc's WM_SETCURSOR path
    //      can't reach the game's original cursor either.
    //   3. The game's WndProc is subclassed to swallow WM_SETCURSOR
    //      directly, and the overlay thread calls SetCursor(NULL) per
    //      frame as belt-and-suspenders.
    // Enable for games that don't manage cursor visibility themselves
    // under windowed mode. Leave at 0 for games whose in-game cursor
    // should remain visible.
    int        confine_cursor = 0;
    int        hide_cursor    = 0;

    // Capture-mode selector (see Hooks_DX9.cpp::EnsureStereoStage and the
    // Hooked_Direct3DCreate9 chain). The wrapper has two cross-API
    // handoff paths from Device A (D3D9 capture) to Device B (D3D11
    // overlay):
    //
    // alternate_capture_mode = 0 (default): the device stays plain
    // (non-Ex), the pool rewrite is skipped, and the staging is copied
    // each frame via GetRenderTargetData → SYSTEMMEM → CPU buffer →
    // D3D11 dynamic texture Map/Unmap. Widest compatibility. Costs one
    // extra GPU→CPU + CPU→GPU round-trip per frame; usually invisible
    // at <= 1080p, measurable at 4K but still functional (use
    // copy_width/copy_height to cap readback size).
    //
    // alternate_capture_mode = 1: the D3D9 device is silently upgraded
    // to IDirect3D9Ex, NvAPI's reverse-stereo-blit lands in an Ex
    // shared-handle render target, and Device B opens that texture
    // directly via OpenSharedResource. Fastest path; zero CPU readback;
    // supports higher resolutions cleanly. Side effect: Ex rejects
    // D3DPOOL_MANAGED, so the hooked Create* methods rewrite MANAGED →
    // DEFAULT and add D3DUSAGE_DYNAMIC. Some games show graphical
    // glitches (flicker, missing/black textures, geometry distortion)
    // or crash outright under this path.
    int        alternate_capture_mode = 0;

    // Diagnostic knobs — three points where we can cut off our hook
    // surface to bisect a crash. All default 1 (full hook coverage).
    //
    //   install_device_hooks: when 0, skip the per-device vtable hooks
    //     (Present / Reset / Create*). Game runs as if we only observed
    //     CreateDevice; no stereo capture.
    //   install_d3d9_vtable_hooks: when 0, skip the IDirect3D9 vtable
    //     hooks entirely (CreateDevice + the display-mode enumerators).
    //     Game's CreateDevice runs untouched.
    //   install_d3d9_display_mode_hooks: when 0, install the CreateDevice
    //     hook but skip the three display-mode enumerator hooks. Set
    //     this when a game's display-mode init flow crashes inside the
    //     NktHookLib trampoline for those calls.
    int        install_device_hooks            = 1;
    int        install_d3d9_vtable_hooks       = 1;
    int        install_d3d9_display_mode_hooks = 1;

    // Per-eye downsample resolution for the cross-API staging texture.
    // The staging is normally 2 × BB_width by BB_height — at 4K BB that's
    // 7680x2160, ~66 MB. The CPU-readback path's GetRenderTargetData has
    // to drain that surface each frame while holding the D3D9 device
    // lock (D3DCREATE_MULTITHREADED), stalling the game's next
    // StretchRect for ~16 ms and halving framerate.
    //
    // When BOTH per-eye dims are non-zero, the StretchRect into the
    // staging downsamples on the GPU to (2*copy_width,
    // copy_height), so the readback drains much faster.
    // Each panel-half in the compose shader already gets resampled from
    // (staging_width/2, staging_height) to panel-half, so as long as
    // copy_width is at least the panel-half width and
    // copy_height is at least the panel height, the cap is
    // essentially lossless (e.g. on a 4K panel, 1920x2160 per eye is
    // pixel-perfect; 1920x1080 trades half the vertical detail for a
    // ~4× readback speedup).
    //
    // Default 0,0 = no cap.
    UINT       copy_width  = 0;
    UINT       copy_height = 0;

    // Optional: stamp this into the game's CreateDevice/Reset present
    // params, and feed the same dims back to display-mode probes
    // (GetSystemMetrics, EnumAdapterModes, GetDeviceCaps,
    // EnumDisplaySettings) so games that auto-detect the desktop pick
    // the configured resolution instead of the panel's native.
    //
    // 0,0 (default) = leave the game's requested BackBufferWidth/Height
    // alone. The overlay always upscales the captured staging to the
    // panel's native via the compose shader's linear sampler, so
    // interlaced / checkerboard / LeiaSR patterns line up regardless of
    // what the game renders at — this knob is therefore a perf lever.
    //
    // Many games cache their viewport / projection matrix from the
    // resolution they asked for at CreateDevice and never re-query the
    // BB. Forcing a smaller BB on those games clips their render to the
    // top-left of the BB ("top-left quarter" failure across every stereo
    // mode). Set non-zero only when the specific game cooperates.
    UINT       render_width  = 0;
    UINT       render_height = 0;
};


// --------------------------------------------------------------------------
// Public entry — every proxy's DllMain calls this from DLL_PROCESS_ATTACH.

extern "C" void Injector_EnsureInit(HMODULE hSelf);


// --------------------------------------------------------------------------
// Globals owned by Init.cpp, referenced by Hooks_DX9 / etc.

extern CNktHookLib   g_nktInProc;
extern StereoHandle  g_nvapi;
extern bool          g_directMode;
extern Config g_config;


// --------------------------------------------------------------------------
// Logging — defined in Log.cpp.

void Log_Init(const wchar_t* logPath);
void Log_Close();
void Log_Write(const wchar_t* fmt, ...);
void Log_Fatal(const wchar_t* msg, HRESULT code);

#define KLOG(fmt, ...)  do { Log_Write(fmt, ##__VA_ARGS__); } while (0)

// Verbose-only log line. Compiled in, but writes only when
// [debug] log_level >= 2 in the INI. Use for high-frequency hooks
// (display-mode probes that fire hundreds of times per second) so the
// default log_level=1 install gets a readable log without spam.
#define KLOG_V(fmt, ...) do { if (g_config.log_level >= 2) Log_Write(fmt, ##__VA_ARGS__); } while (0)


// --------------------------------------------------------------------------
// Config — defined in Config.cpp.

void Config_Load(Config& cfg);


// --------------------------------------------------------------------------
// DX9 hook install — defined in Hooks_DX9.cpp.

void DX9_InstallHooks();

// Install the CreateDevice + display-mode-enumeration vtable hooks on an
// IDirect3D9Ex returned by some real-export-side path (e.g. the
// Proxy_Direct3DCreate9Ex export below). Idempotent.
// Accepts either IDirect3D9 or IDirect3D9Ex — the only vtable entries we
// touch are the ones shared between the two interfaces (CreateDevice plus
// display-mode enumeration), so a plain non-Ex object works fine here.
void DX9_InstallVtableHooksOn(IDirect3D9* pDX9);


// --------------------------------------------------------------------------
// NvAPI SetDriverMode hook — defined in Init.cpp.

void NvApi_HookSetDriverMode();


// --------------------------------------------------------------------------
// Win32 display-mode enumeration hooks — defined in Init.cpp. When
// render_width/height are both non-zero, these lie to the game about the
// desktop resolution. Active only on demand; transparent pass-through when
// the override is off.

void Win32_HookDisplayModeApis();


// --------------------------------------------------------------------------
// Win32 display-mode CHANGE hooks — defined in Init.cpp. Neuter
// ChangeDisplaySettings(Ex)(W/A) and SetDisplayConfig so games can't
// switch the desktop into a different mode behind our back — the FSE
// counterpart to force_windowed (which only covers the D3D9
// PresentParameters path; ChangeDisplaySettings goes through Win32
// directly and bypasses the D3D9 hook surface). Gated on
// force_windowed; transparent pass-through when force_windowed=0.

void Win32_HookChangeDisplaySettings();


// --------------------------------------------------------------------------
// D3D11 compose dispatch — defined in Compose_D3D11.cpp.
// Runs on Device B (D3D11). Samples the staging SRV (opened cross-API from
// the DX9Ex shared handle) and writes the mode-appropriate stereo composite
// into the overlay backbuffer's RTV.

void Compose_D3D11_Run(ID3D11Device*             device,
                       ID3D11DeviceContext*      ctx,
                       ID3D11ShaderResourceView* stagingSRV,
                       ID3D11RenderTargetView*   backBufRTV,
                       UINT outW, UINT outH,
                       StereoMode mode);

void Compose_D3D11_Release();


// --------------------------------------------------------------------------
// LeiaSR / Simulated Reality weaver hand-off — defined in Output_LeiaSR.cpp.

bool LeiaSR_TryInit(ID3D11Device*             device,
                    ID3D11DeviceContext*      ctx,
                    HWND                      hwnd,
                    ID3D11ShaderResourceView* stagingSRV);
void LeiaSR_Weave();
bool LeiaSR_IsActive();
void LeiaSR_Shutdown();


// --------------------------------------------------------------------------
// Katanga IPC publish — defined in Output_Katanga.cpp.
//
// Hands the captured full-SbS frame off to a separate VR viewer process
// (Katanga.exe, VRScreenCap) over the Katanga shared-texture protocol:
// named MMF "Local\KatangaMappedFile" carries a 32-bit DXGI shared handle,
// and named mutex "KatangaSetupMutex" gates the recreate window. Called
// from the overlay present thread once per frame when the mode is
// StereoMode::Katanga. No-op (with periodic re-open attempts) until a VR
// consumer is detected.
void Katanga_PublishFrame(ID3D11Device*        device,
                          ID3D11DeviceContext* ctx,
                          ID3D11Texture2D*     stagingTex,
                          UINT                 stagingWidth,
                          UINT                 stagingHeight);
void Katanga_Shutdown();


// --------------------------------------------------------------------------
// Overlay output (Device B) — defined in Output_Overlay.cpp.

void Overlay_StartOnce(HWND gameHwnd);
void Overlay_NotifyFrame();
void Overlay_ShutdownOnce();


// --------------------------------------------------------------------------
// Cross-device staging handoff published by Device A's capture path for
// Device B to consume. See Hooks_DX9.cpp::EnsureStereoStage. Set once on
// first capture, cleared on Hooked_Reset.
//
// Two paths, selected at staging-setup time based on alternate_capture_mode
// (and a fallback if shared-handle creation fails):
//
//   SHARED-HANDLE path (Ex device): g_stagingSharedHandle is a kernel
//     handle Device B opens via OpenSharedResource. Zero CPU readback.
//
//   CPU-READBACK path (non-Ex device or shared creation refused — for
//     example NVIDIA's D3D9 driver refusing to share 10-bit HDR formats
//     at large dims): g_stagingSharedHandle is nullptr, g_stagingCpuBuffer
//     holds the most recent frame, guarded by g_stagingCpuLock and
//     announced by g_stagingCpuFresh.
extern HANDLE g_stagingSharedHandle;
extern UINT   g_stagingWidth;
extern UINT   g_stagingHeight;
extern UINT   g_stagingD3DFormat;
extern HWND   g_gameFocusHwnd;

extern void*            g_stagingCpuBuffer;
extern UINT             g_stagingCpuPitch;
extern CRITICAL_SECTION g_stagingCpuLock;
extern volatile LONG    g_stagingCpuFresh;


// --------------------------------------------------------------------------
// Vtable extractors from Addresses.c — declared extern "C" because they're
// compiled as straight C using CINTERFACE.

extern "C" LPVOID lpvtbl_CreateDevice(IDirect3D9* pDX9);
extern "C" LPVOID lpvtbl_GetAdapterDisplayMode(IDirect3D9* pDX9);
extern "C" LPVOID lpvtbl_EnumAdapterModes(IDirect3D9* pDX9);
extern "C" LPVOID lpvtbl_GetAdapterModeCount(IDirect3D9* pDX9);
extern "C" LPVOID lpvtbl_Present_DX9(IDirect3DDevice9* pDX9Device);
extern "C" LPVOID lpvtbl_Reset(IDirect3DDevice9* pDX9Device);
extern "C" LPVOID lpvtbl_CreateAdditionalSwapChain(IDirect3DDevice9* pDX9Device);
extern "C" LPVOID lpvtbl_CreateTexture(IDirect3DDevice9* pDX9Device);
extern "C" LPVOID lpvtbl_CreateCubeTexture(IDirect3DDevice9* pDX9Device);
extern "C" LPVOID lpvtbl_CreateVolumeTexture(IDirect3DDevice9* pDX9Device);
extern "C" LPVOID lpvtbl_CreateOffscreenPlainSurface(IDirect3DDevice9* pDX9Device);
extern "C" LPVOID lpvtbl_CreateVertexBuffer(IDirect3DDevice9* pDX9Device);
extern "C" LPVOID lpvtbl_CreateIndexBuffer(IDirect3DDevice9* pDX9Device);
