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

// Shared DllMain for all four 3DVision4All proxy DLLs (dinput8, dsound,
// winmm, version). Each proxy's .vcxproj compiles this same file alongside
// its own .def-equivalent (Exports_*.cpp pragmas) which forwards exports
// to the real DLL.
//
// The proxy DLL has no exports of its own beyond the forwarders. Its
// only job is to invoke Injector_EnsureInit() on attach, which spawns
// the init thread that installs all the D3D9 / NvAPI hooks.
//
// EXCEPTION: when the proxy is a DSOUND.DLL replacement and the game's EXE
// has been patched (e.g. by HelixMod / 3D-fix installers) to pull
// Direct3DCreate9 / Direct3DCreate9Ex through DSOUND instead of through
// d3d9.dll directly, the EXE will fail to start unless DSOUND.DLL exports
// those two symbols. The Proxy_Direct3DCreate9{,Ex} functions below are
// /EXPORT'd from DSOUND (via Exports_dsound.cpp pragmas) as Direct3DCreate9
// and Direct3DCreate9Ex. They load d3d9.dll by short name — DLL search
// picks a game-folder d3d9.dll (HelixMod's per-game shader-fix wrapper)
// if present, otherwise system d3d9.dll — call into it, then install our
// IDirect3D9 vtable hooks on the returned object before handing it back.
// HelixMod's per-game fix AND our SbS-overlay composition both run.
//
// The proxies that don't /EXPORT these symbols (dinput8, winmm, version)
// still compile the functions; they're just dead-stripped or sit unused.

#include "Core.h"

#include <d3d9.h>


extern "C" void Injector_EnsureInit(HMODULE hSelf);


BOOL APIENTRY DllMain(HMODULE hSelf, DWORD reason, LPVOID /*reserved*/)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hSelf);
        Injector_EnsureInit(hSelf);
    }
    return TRUE;
}


// Resolve a d3d9.dll function by short name. The first LoadLibraryW finds
// game-folder d3d9.dll if present (HelixMod and similar wrappers live there),
// otherwise system d3d9.dll. Cached for subsequent calls.
template <typename T>
static T ResolveD3D9(const char* name)
{
    static HMODULE hD3D9 = nullptr;
    if (!hD3D9) hD3D9 = LoadLibraryW(L"d3d9.dll");
    if (!hD3D9) return nullptr;
    return reinterpret_cast<T>(GetProcAddress(hD3D9, name));
}


// Real exported Direct3DCreate9Ex. /EXPORT'd from DSOUND.DLL via the
// pragma in Exports_dsound.cpp. See file header comment for context.
extern "C" __declspec(dllexport) HRESULT WINAPI
Proxy_Direct3DCreate9Ex(UINT SDKVersion, IDirect3D9Ex** ppDX9Ex)
{
    KLOG(L"Proxy_Direct3DCreate9Ex called SDK=%u\n", SDKVersion);
    typedef HRESULT (WINAPI *pfn_t)(UINT, IDirect3D9Ex**);
    static pfn_t pFn = nullptr;
    if (!pFn) {
        pFn = ResolveD3D9<pfn_t>("Direct3DCreate9Ex");
        KLOG(L"  resolved Direct3DCreate9Ex -> %p\n", pFn);
    }
    if (!pFn) return E_FAIL;

    HRESULT hr = pFn(SDKVersion, ppDX9Ex);
    KLOG(L"  inner Direct3DCreate9Ex returned hr=0x%x ppDX9Ex=%p\n",
         hr, ppDX9Ex ? *ppDX9Ex : nullptr);
    if (SUCCEEDED(hr) && ppDX9Ex && *ppDX9Ex)
        DX9_InstallVtableHooksOn(*ppDX9Ex);
    return hr;
}


// Real exported Direct3DCreate9. Same pattern as above. We still upgrade to
// Ex internally (matching the behavior of our installed Hooked_Direct3DCreate9
// trampoline) so NvAPI_Stereo_CreateHandleFromIUnknown gets an Ex object.
extern "C" __declspec(dllexport) IDirect3D9* WINAPI
Proxy_Direct3DCreate9(UINT SDKVersion)
{
    KLOG(L"Proxy_Direct3DCreate9 called SDK=%u\n", SDKVersion);
    // Try the Ex path first to give NvAPI an Ex device.
    typedef HRESULT (WINAPI *pfn_ex_t)(UINT, IDirect3D9Ex**);
    static pfn_ex_t pFnEx = nullptr;
    if (!pFnEx) {
        pFnEx = ResolveD3D9<pfn_ex_t>("Direct3DCreate9Ex");
        KLOG(L"  resolved Direct3DCreate9Ex -> %p\n", pFnEx);
    }
    if (pFnEx) {
        IDirect3D9Ex* pEx = nullptr;
        if (SUCCEEDED(pFnEx(SDKVersion, &pEx)) && pEx) {
            KLOG(L"  upgraded to Ex %p\n", pEx);
            DX9_InstallVtableHooksOn(pEx);
            return reinterpret_cast<IDirect3D9*>(pEx);
        }
    }

    // Fallback: plain Direct3DCreate9. We don't install vtable hooks here
    // because NvAPI stereo capture needs an Ex device anyway and this path
    // is for compatibility with hardware that genuinely doesn't support Ex.
    typedef IDirect3D9* (WINAPI *pfn_t)(UINT);
    static pfn_t pFn = nullptr;
    if (!pFn) {
        pFn = ResolveD3D9<pfn_t>("Direct3DCreate9");
        KLOG(L"  resolved Direct3DCreate9 -> %p (plain fallback)\n", pFn);
    }
    if (!pFn) return nullptr;
    IDirect3D9* p = pFn(SDKVersion);
    KLOG(L"  inner Direct3DCreate9 returned %p\n", p);
    return p;
}
