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

// This file is compiled as a C file only, not C++. We use the C-style
// CINTERFACE for d3d9.h to expose the lpVtbl pointer directly so we can
// extract function addresses for hooking without magic offsets.
//
// Warning: the C interface for DX9Ex is broken in older Windows SDKs
// (missing RegisterSoftwareDevice in IDirect3D9Ex), so we set
// D3D_DISABLE_9EX to keep the C interface clean. Pass IDirect3D9Ex
// pointers in as the IDirect3D9 base class — the vtable address is the
// same.

#define CINTERFACE
#define D3D_DISABLE_9EX

#ifdef _DEBUG
    #define D3D_DEBUG_INFO
#endif

#include <d3d9.h>


LPVOID lpvtbl_CreateDevice(IDirect3D9* pDX9)
{
    if (!pDX9)
        return NULL;
    return pDX9->lpVtbl->CreateDevice;
}

LPVOID lpvtbl_GetAdapterDisplayMode(IDirect3D9* pDX9)
{
    if (!pDX9)
        return NULL;
    return pDX9->lpVtbl->GetAdapterDisplayMode;
}

LPVOID lpvtbl_EnumAdapterModes(IDirect3D9* pDX9)
{
    if (!pDX9)
        return NULL;
    return pDX9->lpVtbl->EnumAdapterModes;
}

LPVOID lpvtbl_GetAdapterModeCount(IDirect3D9* pDX9)
{
    if (!pDX9)
        return NULL;
    return pDX9->lpVtbl->GetAdapterModeCount;
}

LPVOID lpvtbl_Present_DX9(IDirect3DDevice9* pDX9Device)
{
    if (!pDX9Device)
        return NULL;
    return pDX9Device->lpVtbl->Present;
}

LPVOID lpvtbl_Reset(IDirect3DDevice9* pDX9Device)
{
    if (!pDX9Device)
        return NULL;
    return pDX9Device->lpVtbl->Reset;
}

LPVOID lpvtbl_CreateAdditionalSwapChain(IDirect3DDevice9* pDX9Device)
{
    if (!pDX9Device)
        return NULL;
    return pDX9Device->lpVtbl->CreateAdditionalSwapChain;
}

LPVOID lpvtbl_CreateTexture(IDirect3DDevice9* pDX9Device)
{
    if (!pDX9Device)
        return NULL;
    return pDX9Device->lpVtbl->CreateTexture;
}

LPVOID lpvtbl_CreateCubeTexture(IDirect3DDevice9* pDX9Device)
{
    if (!pDX9Device)
        return NULL;
    return pDX9Device->lpVtbl->CreateCubeTexture;
}

LPVOID lpvtbl_CreateVolumeTexture(IDirect3DDevice9* pDX9Device)
{
    if (!pDX9Device)
        return NULL;
    return pDX9Device->lpVtbl->CreateVolumeTexture;
}

LPVOID lpvtbl_CreateOffscreenPlainSurface(IDirect3DDevice9* pDX9Device)
{
    if (!pDX9Device)
        return NULL;
    return pDX9Device->lpVtbl->CreateOffscreenPlainSurface;
}

LPVOID lpvtbl_CreateVertexBuffer(IDirect3DDevice9* pDX9Device)
{
    if (!pDX9Device)
        return NULL;
    return pDX9Device->lpVtbl->CreateVertexBuffer;
}

LPVOID lpvtbl_CreateIndexBuffer(IDirect3DDevice9* pDX9Device)
{
    if (!pDX9Device)
        return NULL;
    return pDX9Device->lpVtbl->CreateIndexBuffer;
}

// CreateDeviceEx lives on IDirect3D9Ex (not the plain IDirect3D9 base) at
// vtable index 20. We extract it by manual offset because this TU sets
// D3D_DISABLE_9EX at the top to keep the C interface clean across SDK
// versions, which hides the Ex vtable struct. Caller must guarantee the
// passed pointer is actually an IDirect3D9Ex (verify via QueryInterface).
//
// Vtable layout: IUnknown(0..2) + IDirect3D9(3..16) + IDirect3D9Ex(17..21):
//   17 GetAdapterModeCountEx
//   18 EnumAdapterModesEx
//   19 GetAdapterDisplayModeEx
//   20 CreateDeviceEx
//   21 GetAdapterLUID
LPVOID lpvtbl_CreateDeviceEx(IDirect3D9* pDX9Ex)
{
    LPVOID* vtbl;
    if (!pDX9Ex)
        return NULL;
    vtbl = *(LPVOID**)pDX9Ex;
    return vtbl[20];
}

// PresentEx and ResetEx live on IDirect3DDevice9Ex. Caller must guarantee
// the passed pointer is actually an IDirect3DDevice9Ex (verify via
// QueryInterface). Same D3D_DISABLE_9EX manual-offset reason as above.
//
// IDirect3DDevice9Ex vtable, after the 3 IUnknown slots and 116 plain
// IDirect3DDevice9 slots (positions 0..118), continues with 15 Ex-only
// methods starting at slot 119:
//   119 SetConvolutionMonoKernel
//   120 ComposeRects
//   121 PresentEx
//   122 GetGPUThreadPriority
//   123 SetGPUThreadPriority
//   124 WaitForVBlank
//   125 CheckResourceResidency
//   126 SetMaximumFrameLatency
//   127 GetMaximumFrameLatency
//   128 CheckDeviceState
//   129 CreateRenderTargetEx
//   130 CreateOffscreenPlainSurfaceEx
//   131 CreateDepthStencilSurfaceEx
//   132 ResetEx
//   133 GetDisplayModeEx
LPVOID lpvtbl_PresentEx_DX9(IDirect3DDevice9* pDX9DeviceEx)
{
    LPVOID* vtbl;
    if (!pDX9DeviceEx)
        return NULL;
    vtbl = *(LPVOID**)pDX9DeviceEx;
    return vtbl[121];
}

LPVOID lpvtbl_ResetEx(IDirect3DDevice9* pDX9DeviceEx)
{
    LPVOID* vtbl;
    if (!pDX9DeviceEx)
        return NULL;
    vtbl = *(LPVOID**)pDX9DeviceEx;
    return vtbl[132];
}

#undef CINTERFACE
