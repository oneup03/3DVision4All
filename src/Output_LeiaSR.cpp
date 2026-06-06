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
// Uses SR-lib's SRInterfaceDX11 (c:\Users\oneup\source\repos\oneup03\SR-lib)
// so we can stay on the same D3D11 Device B that the other stereo modes use.
// SR-lib is statically linked; the SR runtime DLLs are delay-loaded by the
// proxy projects so the build still runs on machines without SR installed.
// If the runtime is missing, CreateSRInterfaceDX11 returns failure and we
// fall back to the regular compose shader.
//
// Lifecycle (called from the overlay present thread):
//   1. Per-frame: LeiaSR_TryInit(device, ctx, hwnd, stagingSRV).
//      First call creates the weaver and binds the input SRV. Subsequent
//      calls are cheap (cached state, rebind if SRV changed).
//   2. Per-frame: bind backbuffer as RT, then LeiaSR_Weave().
//      The weaver writes the lenticular-woven stereo into the bound RT.
//   3. Teardown: LeiaSR_Shutdown().

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

    HRESULT hr = SimulatedReality::CreateSRInterfaceDX11(ctx, hwnd, &g_sr);
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


void LeiaSR_Weave()
{
    if (g_sr && g_srActive) g_sr->Weave();
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
