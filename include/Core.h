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
struct ID3D11ShaderResourceView;
struct ID3D11RenderTargetView;


// --------------------------------------------------------------------------
// Stereo output mode (read from 3dvision4all.ini at init).

enum class StereoMode {
    SbsHalf = 0,        // W × H output; each half = one eye (3DTV SbS input,
                        //                                   AR-glasses 32:9 panel)
    Tab,                // W × H output; top half = L, bottom half = R
    RowInterlaced,      // W × H output; even rows = L, odd rows = R
    ColumnInterlaced,   // W × H output; even cols = L, odd cols = R
    Checkerboard,       // W × H output; (x+y) odd/even = L/R
    LeiaSR,             // SR weaver to a Leia / Simulated Reality display
};

struct Config {
    StereoMode mode = StereoMode::SbsHalf;

    // Reverse-stereo-blit captures (and Direct Mode SetActiveEye copies)
    // hand us right-view on the left half and left-view on the right half
    // of the 2W×H staging. Default ON undoes that so every downstream
    // consumer (compose shader, LeiaSR weaver) reads L on left, R on right.
    // Set to 0 in the rare case where the game's stereo already comes back
    // in natural order.
    bool       swap_eyes = true;

    wchar_t    log_path[MAX_PATH] = L"";
    int        log_level = 1; // 0=off, 1=info, 2=verbose

    // Add WS_EX_LAYERED + LWA_ALPHA(255) to the GAME's window after
    // CreateDevice/Reset. DWM can't DirectFlip a layered window's swap
    // chain, so this forces composition through the redirection surface —
    // at which point our topmost overlay actually shows over the game.
    // Default ON because most fullscreen-borderless DX9 games go into
    // DirectFlip and the overlay is invisible without this.
    int        defeat_directflip = 1;

    // Optional: stamp this into the game's CreateDevice/Reset present params.
    // 0,0 = leave the game's requested BackBufferWidth/Height alone (still
    // force windowed). The overlay always upscales the captured staging to
    // the panel's native resolution via the compose shader's linear sampler,
    // so interlaced / checkerboard / LeiaSR patterns line up pixel-perfectly
    // regardless of the game's render resolution.
    //
    // Defaults to OFF because many games (notably UE3-derived titles such
    // as Brothers - A Tale of Two Sons) cache their viewport / projection
    // matrix from their requested resolution and don't re-query the BB after
    // CreateDevice returns. Forcing a smaller BB on those games clips their
    // render to the top-left of the BB — visible as "only the top-left
    // quarter of the game world is rendered" across every stereo mode.
    // Set non-zero only if you've confirmed the specific game cooperates.
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
void DX9_InstallVtableHooksOn(IDirect3D9Ex* pDX9Ex);


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
// Overlay output (Device B) — defined in Output_Overlay.cpp.

void Overlay_StartOnce(HWND gameHwnd);
void Overlay_NotifyFrame();
void Overlay_ShutdownOnce();


// --------------------------------------------------------------------------
// Cross-device shared staging texture published by Device A's capture path
// for Device B to open. See Hooks_DX9.cpp::EnsureStereoStage. Set once on
// first capture, cleared on Hooked_Reset.
extern HANDLE g_stagingSharedHandle;
extern UINT   g_stagingWidth;
extern UINT   g_stagingHeight;
extern UINT   g_stagingD3DFormat;
extern HWND   g_gameFocusHwnd;


// --------------------------------------------------------------------------
// Vtable extractors from Addresses.c — declared extern "C" because they're
// compiled as straight C using CINTERFACE.

extern "C" LPVOID lpvtbl_CreateDevice(IDirect3D9* pDX9);
extern "C" LPVOID lpvtbl_GetAdapterDisplayMode(IDirect3D9* pDX9);
extern "C" LPVOID lpvtbl_EnumAdapterModes(IDirect3D9* pDX9);
extern "C" LPVOID lpvtbl_GetAdapterModeCount(IDirect3D9* pDX9);
extern "C" LPVOID lpvtbl_Present_DX9(IDirect3DDevice9* pDX9Device);
extern "C" LPVOID lpvtbl_Reset(IDirect3DDevice9* pDX9Device);
extern "C" LPVOID lpvtbl_CreateTexture(IDirect3DDevice9* pDX9Device);
extern "C" LPVOID lpvtbl_CreateCubeTexture(IDirect3DDevice9* pDX9Device);
extern "C" LPVOID lpvtbl_CreateVolumeTexture(IDirect3DDevice9* pDX9Device);
extern "C" LPVOID lpvtbl_CreateOffscreenPlainSurface(IDirect3DDevice9* pDX9Device);
extern "C" LPVOID lpvtbl_CreateVertexBuffer(IDirect3DDevice9* pDX9Device);
extern "C" LPVOID lpvtbl_CreateIndexBuffer(IDirect3DDevice9* pDX9Device);
