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
// NvAPI SetDriverMode hook — API-agnostic, lifted from
// DeviarePlugin/InProc_DX11.cpp:665-739. We only need this to flip
// g_directMode so the Present hook chooses the per-eye SetActiveEye path
// instead of the ReverseStereoBlit path.

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
    KLOG(L"  ar_per_eye    = %ux%u (mon=%d)\n",
         g_config.ar_per_eye_width, g_config.ar_per_eye_height, g_config.ar_monitor_index);
    KLOG(L"  defeat_directflip = %d\n", g_config.defeat_directflip);
    KLOG(L"  log_path      = %s\n", g_config.log_path);

#ifdef _DEBUG
    g_nktInProc.SetEnableDebugOutput(TRUE);
#endif

    DX9_InstallHooks();
    NvApi_HookSetDriverMode();

    KLOG(L"3DVision4All init thread complete\n");
    return 0;
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
