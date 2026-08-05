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

// LeiaSR (Simulated Reality) weaver hand-off for the overlay output path.
//
// Uses SR-lib's SRInterfaceDX11 (third_party/SR-lib, api_expansion branch) so
// we can stay on the same D3D11 Device B that the other stereo modes use.
// SR-lib is statically linked; the SR runtime DLLs are delay-loaded by the
// proxy projects so the build still runs on machines without SR installed.
// SR-lib probes both SimulatedRealityCore.dll and the backend weaver DLL
// (SimulatedRealityDirectX.dll) with LoadLibrary before it touches the SDK,
// so a missing runtime surfaces as CreateSRInterfaceDX11 returning
// E_NOINTERFACE rather than an uncatchable delay-load SEH — we then fall back
// to the regular compose shader.
//
// Lifecycle (called from the overlay present thread):
//   1. Per-frame: LeiaSR_TryInit(device, ctx, hwnd, stagingSRV).
//      First call creates the weaver and binds the input SRV. Subsequent
//      calls are cheap (cached state, rebind if SRV changed).
//   2. Per-frame: bind backbuffer as RT, then LeiaSR_Weave().
//      The weaver writes the lenticular-woven stereo into the bound RT.
//      Returns false if the weave didn't happen, so the caller can compose
//      the frame normally instead of Presenting an untouched backbuffer.
//   3. Teardown: LeiaSR_Shutdown().
//
// Exceptions: SR-lib contains the ones thrown during context creation, but
// weave() is unguarded there and does throw when the SR display is unplugged
// mid-session. We catch that here and disable the weaver for the rest of the
// session (rather than crashing the injected game) — this TU is compiled with
// /EHsc for exactly that, see Core.vcxproj.

#include "Core.h"

#include "SR.hpp"

#include <d3d11.h>


static SimulatedReality::SRInterfaceDX11* g_sr           = nullptr;
static bool                               g_srInitTried  = false;
static bool                               g_srActive     = false;
static ID3D11ShaderResourceView*          g_srInputSRV   = nullptr;


bool LeiaSR_TryInit(ID3D11Device*             /*device*/,
                    ID3D11DeviceContext*      ctx,
                    HWND                      hwnd,
                    ID3D11ShaderResourceView* stagingSRV)
{
    if (g_srInitTried) {
        if (g_srActive && g_sr && stagingSRV && stagingSRV != g_srInputSRV) {
            g_sr->SetInputTexture(stagingSRV);
            g_srInputSRV = stagingSRV;
            KLOG(L"LeiaSR: rebound input SRV %p\n", stagingSRV);
        }
        return g_srActive;
    }

    g_srInitTried = true;

    if (!ctx || !hwnd || !stagingSRV) {
        KLOG(L"LeiaSR_TryInit: missing ctx/hwnd/srv\n");
        return false;
    }

    HRESULT hr = E_FAIL;
    try {
        hr = SimulatedReality::CreateSRInterfaceDX11(ctx, hwnd, &g_sr);
    } catch (...) {
        hr = E_FAIL;
        g_sr = nullptr;
    }
    if (FAILED(hr) || !g_sr) {
        KLOG(L"LeiaSR: CreateSRInterfaceDX11 hr=0x%x (likely SR runtime missing); falling back\n", hr);
        g_sr = nullptr;
        g_srActive = false;
        return false;
    }

    g_sr->SetInputTexture(stagingSRV);
    g_srInputSRV = stagingSRV;
    g_srActive = true;
    KLOG(L"LeiaSR: weaver initialized on D3D11 Device B (hwnd=%p, srv=%p)\n", hwnd, stagingSRV);
    return true;
}


bool LeiaSR_Weave()
{
    if (!g_sr || !g_srActive) return false;

    try {
        g_sr->Weave();
    } catch (...) {
        // Most likely the SR display was unplugged (or the SR service died)
        // mid-session. Drop the weaver and stay down for the rest of the
        // session — retrying every frame would just throw every frame. The
        // caller composes this frame the normal way.
        KLOG(L"LeiaSR: weave threw (display unplugged / service gone); falling back to compose\n");
        g_srActive = false;
        if (g_sr) {
            try { g_sr->Delete(); } catch (...) {}
            g_sr = nullptr;
        }
        g_srInputSRV = nullptr;
        return false;
    }
    return true;
}


bool LeiaSR_IsActive()
{
    return g_srActive;
}


void LeiaSR_Shutdown()
{
    if (g_sr) {
        g_sr->Delete();
        g_sr = nullptr;
    }
    g_srInputSRV  = nullptr;
    g_srActive    = false;
    g_srInitTried = false;
}
