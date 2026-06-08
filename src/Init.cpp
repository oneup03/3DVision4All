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

#include "Core.h"

#include <process.h>


// Globals owned here (extern in Core.h).
CNktHookLib   g_nktInProc;
StereoHandle  g_nvapi = nullptr;
bool          g_directMode = false;
Config g_config;

// Multi-proxy guard. If two proxies (e.g. dinput8 + dsound) end up loaded in
// the same process, only the first one's call to Injector_EnsureInit
// actually runs the hook install.
static LONG s_initOnce = 0;
static HMODULE s_selfModule = nullptr;


// --------------------------------------------------------------------------
// NvAPI SetDriverMode hook — API-agnostic. We only need this to flip
// g_directMode so the Present hook chooses the per-eye SetActiveEye
// path instead of the ReverseStereoBlit path.

typedef NvAPI_Status(__cdecl *tNvAPI_Stereo_SetDriverMode)(NV_STEREO_DRIVER_MODE mode);
static tNvAPI_Stereo_SetDriverMode pOrigNvAPI_Stereo_SetDriverMode = nullptr;

static NvAPI_Status __cdecl Hooked_NvAPI_Stereo_SetDriverMode(NV_STEREO_DRIVER_MODE mode)
{
    if (mode == NVAPI_STEREO_DRIVER_MODE_DIRECT)
        g_directMode = true;

    NvAPI_Status ret = pOrigNvAPI_Stereo_SetDriverMode(mode);
    KLOG(L"Hooked_NvAPI_Stereo_SetDriverMode mode=%d ret=%d\n", mode, ret);
    return ret;
}


typedef void* (__cdecl *t_nvapi_QueryInterface)(UINT32 offset);
static t_nvapi_QueryInterface pOrig_nvapi_QueryInterface = nullptr;
static const UINT32 SetDriverMode_Offset = 0x5E8F0BEC;


void NvApi_HookSetDriverMode()
{
#if defined(_WIN64)
    const wchar_t* nvapiDll = L"nvapi64.dll";
#else
    const wchar_t* nvapiDll = L"nvapi.dll";
#endif

    HMODULE hNvapi = LoadLibraryW(nvapiDll);
    if (!hNvapi) {
        KLOG(L"NvApi_HookSetDriverMode: LoadLibrary %s failed err=0x%x\n",
             nvapiDll, GetLastError());
        return;
    }

    FARPROC pQueryInterface = GetProcAddress(hNvapi, "nvapi_QueryInterface");
    if (!pQueryInterface) {
        KLOG(L"NvApi_HookSetDriverMode: nvapi_QueryInterface not found err=0x%x\n",
             GetLastError());
        return;
    }

    if (pOrigNvAPI_Stereo_SetDriverMode != nullptr)
        return;  // already hooked

    pOrig_nvapi_QueryInterface = reinterpret_cast<t_nvapi_QueryInterface>(pQueryInterface);
    void* pSetDriverMode = pOrig_nvapi_QueryInterface(SetDriverMode_Offset);
    if (!pSetDriverMode) {
        KLOG(L"NvApi_HookSetDriverMode: SetDriverMode offset returned null\n");
        return;
    }

    SIZE_T hook_id = 0;
    DWORD dwOsErr = g_nktInProc.Hook(&hook_id, (void**)&pOrigNvAPI_Stereo_SetDriverMode,
                                     pSetDriverMode, Hooked_NvAPI_Stereo_SetDriverMode, 0);
    if (FAILED(dwOsErr))
        KLOG(L"NvApi_HookSetDriverMode: Hook failed 0x%x\n", dwOsErr);
    else
        KLOG(L"NvApi_HookSetDriverMode: hooked SetDriverMode @ %p\n", pSetDriverMode);
}


// --------------------------------------------------------------------------
// Init thread. Must run on its own thread, not in DllMain — calling
// LoadLibrary from DllMain is unsafe (loader lock).

static unsigned __stdcall InitThreadProc(void* /*param*/)
{
    Config_Load(g_config);
    Log_Init(g_config.log_path);

    KLOG(L"3DVision4All init thread started\n");
    KLOG(L"  mode          = %d\n", (int)g_config.mode);
    KLOG(L"  swap_eyes     = %d\n", g_config.swap_eyes ? 1 : 0);
    KLOG(L"  defeat_directflip = %d\n", g_config.defeat_directflip);
    KLOG(L"  force_windowed = %d\n", g_config.force_windowed);
    KLOG(L"  disable_vsync = %d\n", g_config.disable_vsync);
    KLOG(L"  confine_cursor = %d\n", g_config.confine_cursor);
    KLOG(L"  hide_cursor    = %d\n", g_config.hide_cursor);
    KLOG(L"  install_device_hooks = %d\n", g_config.install_device_hooks);
    KLOG(L"  install_d3d9_vtable_hooks = %d\n", g_config.install_d3d9_vtable_hooks);
    KLOG(L"  install_d3d9_display_mode_hooks = %d\n", g_config.install_d3d9_display_mode_hooks);
    KLOG(L"  alternate_capture_mode = %d\n", g_config.alternate_capture_mode);
    KLOG(L"  render        = %ux%u (0,0 = no resolution override)\n",
         g_config.render_width, g_config.render_height);
    KLOG(L"  staging_per_eye = %ux%u (0,0 = no cap)\n",
         g_config.staging_per_eye_width, g_config.staging_per_eye_height);
    KLOG(L"  log_path      = %s\n", g_config.log_path);

#ifdef _DEBUG
    g_nktInProc.SetEnableDebugOutput(TRUE);
#endif

    DX9_InstallHooks();
    NvApi_HookSetDriverMode();
    Win32_HookDisplayModeApis();

    KLOG(L"3DVision4All init thread complete\n");
    return 0;
}


// --------------------------------------------------------------------------
// Win32 display-mode enumeration hooks. Lie to the game about the desktop
// resolution when render_width/height are both non-zero. Some games
// ignore their saved-config resolution and probe the desktop via these
// APIs — making the desktop "look smaller" forces them to render at the
// configured size.

typedef int  (WINAPI *t_GetSystemMetrics)(int);
typedef BOOL (WINAPI *t_EnumDisplaySettingsW)(LPCWSTR, DWORD, DEVMODEW*);
typedef BOOL (WINAPI *t_EnumDisplaySettingsA)(LPCSTR,  DWORD, DEVMODEA*);
typedef int  (WINAPI *t_GetDeviceCaps)(HDC, int);

static t_GetSystemMetrics     pOrigGetSystemMetrics     = nullptr;
static t_EnumDisplaySettingsW pOrigEnumDisplaySettingsW = nullptr;
static t_EnumDisplaySettingsA pOrigEnumDisplaySettingsA = nullptr;
static t_GetDeviceCaps        pOrigGetDeviceCaps        = nullptr;

static bool ResOverride() { return g_config.render_width > 0 && g_config.render_height > 0; }


static int WINAPI Hooked_GetSystemMetrics(int nIndex)
{
    int val = pOrigGetSystemMetrics(nIndex);
    if (!ResOverride()) return val;
    switch (nIndex) {
    case SM_CXSCREEN:
    case SM_CXFULLSCREEN:
    case SM_CXVIRTUALSCREEN:
        KLOG_V(L"Hooked_GetSystemMetrics(%d): %d -> %u\n", nIndex, val, g_config.render_width);
        return (int)g_config.render_width;
    case SM_CYSCREEN:
    case SM_CYFULLSCREEN:
    case SM_CYVIRTUALSCREEN:
        KLOG_V(L"Hooked_GetSystemMetrics(%d): %d -> %u\n", nIndex, val, g_config.render_height);
        return (int)g_config.render_height;
    }
    return val;
}


// Both EnumDisplaySettings overrides leave non-resolution fields (refresh,
// orientation, color depth, etc.) alone and only rewrite width/height. We
// override for ANY iModeNum — including ENUM_CURRENT_SETTINGS (the "what
// is the desktop right now" query) and 0..N enumeration callbacks.
static BOOL WINAPI Hooked_EnumDisplaySettingsW(LPCWSTR lpszDeviceName,
                                                DWORD iModeNum,
                                                DEVMODEW* lpDevMode)
{
    BOOL ok = pOrigEnumDisplaySettingsW(lpszDeviceName, iModeNum, lpDevMode);
    if (ok && lpDevMode && ResOverride()) {
        KLOG_V(L"Hooked_EnumDisplaySettingsW(mode=0x%x): %ux%u -> %ux%u\n",
               iModeNum, lpDevMode->dmPelsWidth, lpDevMode->dmPelsHeight,
               g_config.render_width, g_config.render_height);
        lpDevMode->dmPelsWidth  = g_config.render_width;
        lpDevMode->dmPelsHeight = g_config.render_height;
    }
    return ok;
}

static BOOL WINAPI Hooked_EnumDisplaySettingsA(LPCSTR lpszDeviceName,
                                                DWORD iModeNum,
                                                DEVMODEA* lpDevMode)
{
    BOOL ok = pOrigEnumDisplaySettingsA(lpszDeviceName, iModeNum, lpDevMode);
    if (ok && lpDevMode && ResOverride()) {
        KLOG_V(L"Hooked_EnumDisplaySettingsA(mode=0x%x): %lux%lu -> %ux%u\n",
               iModeNum, lpDevMode->dmPelsWidth, lpDevMode->dmPelsHeight,
               g_config.render_width, g_config.render_height);
        lpDevMode->dmPelsWidth  = g_config.render_width;
        lpDevMode->dmPelsHeight = g_config.render_height;
    }
    return ok;
}


// GetDeviceCaps: HORZRES/VERTRES are logical pixels of the device. Many old
// games use these to size their viewport. DESKTOPHORZRES/DESKTOPVERTRES
// are the physical desktop size ignoring DPI scaling — also lied to so we
// stay consistent.
static int WINAPI Hooked_GetDeviceCaps(HDC hdc, int nIndex)
{
    int val = pOrigGetDeviceCaps(hdc, nIndex);
    if (!ResOverride()) return val;
    switch (nIndex) {
    case HORZRES:
    case DESKTOPHORZRES:
        KLOG_V(L"Hooked_GetDeviceCaps(%d): %d -> %u\n", nIndex, val, g_config.render_width);
        return (int)g_config.render_width;
    case VERTRES:
    case DESKTOPVERTRES:
        KLOG_V(L"Hooked_GetDeviceCaps(%d): %d -> %u\n", nIndex, val, g_config.render_height);
        return (int)g_config.render_height;
    }
    return val;
}


void Win32_HookDisplayModeApis()
{
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    HMODULE hGdi32  = GetModuleHandleW(L"gdi32.dll");
    if (!hUser32) hUser32 = LoadLibraryW(L"user32.dll");
    if (!hGdi32)  hGdi32  = LoadLibraryW(L"gdi32.dll");

    FARPROC pGSM = hUser32 ? GetProcAddress(hUser32, "GetSystemMetrics")     : nullptr;
    FARPROC pEDW = hUser32 ? GetProcAddress(hUser32, "EnumDisplaySettingsW") : nullptr;
    FARPROC pEDA = hUser32 ? GetProcAddress(hUser32, "EnumDisplaySettingsA") : nullptr;
    FARPROC pGDC = hGdi32  ? GetProcAddress(hGdi32,  "GetDeviceCaps")        : nullptr;

    SIZE_T hook_id = 0;
    DWORD dwOsErr;

    if (pGSM && !pOrigGetSystemMetrics) {
        dwOsErr = g_nktInProc.Hook(&hook_id, (void**)&pOrigGetSystemMetrics,
                                   pGSM, Hooked_GetSystemMetrics, 0);
        if (FAILED(dwOsErr)) KLOG(L"Win32_HookDisplayModeApis: GetSystemMetrics hook failed 0x%x\n", dwOsErr);
        else                 KLOG(L"Win32_HookDisplayModeApis: hooked GetSystemMetrics @ %p\n", pGSM);
    }
    if (pEDW && !pOrigEnumDisplaySettingsW) {
        dwOsErr = g_nktInProc.Hook(&hook_id, (void**)&pOrigEnumDisplaySettingsW,
                                   pEDW, Hooked_EnumDisplaySettingsW, 0);
        if (FAILED(dwOsErr)) KLOG(L"Win32_HookDisplayModeApis: EnumDisplaySettingsW hook failed 0x%x\n", dwOsErr);
        else                 KLOG(L"Win32_HookDisplayModeApis: hooked EnumDisplaySettingsW @ %p\n", pEDW);
    }
    if (pEDA && !pOrigEnumDisplaySettingsA) {
        dwOsErr = g_nktInProc.Hook(&hook_id, (void**)&pOrigEnumDisplaySettingsA,
                                   pEDA, Hooked_EnumDisplaySettingsA, 0);
        if (FAILED(dwOsErr)) KLOG(L"Win32_HookDisplayModeApis: EnumDisplaySettingsA hook failed 0x%x\n", dwOsErr);
        else                 KLOG(L"Win32_HookDisplayModeApis: hooked EnumDisplaySettingsA @ %p\n", pEDA);
    }
    if (pGDC && !pOrigGetDeviceCaps) {
        dwOsErr = g_nktInProc.Hook(&hook_id, (void**)&pOrigGetDeviceCaps,
                                   pGDC, Hooked_GetDeviceCaps, 0);
        if (FAILED(dwOsErr)) KLOG(L"Win32_HookDisplayModeApis: GetDeviceCaps hook failed 0x%x\n", dwOsErr);
        else                 KLOG(L"Win32_HookDisplayModeApis: hooked GetDeviceCaps @ %p\n", pGDC);
    }
}


extern "C" void Injector_EnsureInit(HMODULE hSelf)
{
    // First proxy to load wins.
    if (InterlockedCompareExchange(&s_initOnce, 1, 0) != 0)
        return;

    s_selfModule = hSelf;

    HANDLE h = (HANDLE)_beginthreadex(nullptr, 0, InitThreadProc, nullptr, 0, nullptr);
    if (h)
        CloseHandle(h);
}
