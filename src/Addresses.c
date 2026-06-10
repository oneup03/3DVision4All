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

LPVOID lpvtbl_TextureLockRect(IDirect3DTexture9* pTexture)
{
    if (!pTexture)
        return NULL;
    return pTexture->lpVtbl->LockRect;
}

LPVOID lpvtbl_TextureUnlockRect(IDirect3DTexture9* pTexture)
{
    if (!pTexture)
        return NULL;
    return pTexture->lpVtbl->UnlockRect;
}

LPVOID lpvtbl_VertexBufferLock(IDirect3DVertexBuffer9* pVB)
{
    if (!pVB)
        return NULL;
    return pVB->lpVtbl->Lock;
}

LPVOID lpvtbl_VertexBufferUnlock(IDirect3DVertexBuffer9* pVB)
{
    if (!pVB)
        return NULL;
    return pVB->lpVtbl->Unlock;
}

LPVOID lpvtbl_IndexBufferLock(IDirect3DIndexBuffer9* pIB)
{
    if (!pIB)
        return NULL;
    return pIB->lpVtbl->Lock;
}

LPVOID lpvtbl_IndexBufferUnlock(IDirect3DIndexBuffer9* pIB)
{
    if (!pIB)
        return NULL;
    return pIB->lpVtbl->Unlock;
}

LPVOID lpvtbl_TextureRelease(IDirect3DTexture9* pTexture)
{
    if (!pTexture)
        return NULL;
    return pTexture->lpVtbl->Release;
}

LPVOID lpvtbl_VertexBufferRelease(IDirect3DVertexBuffer9* pVB)
{
    if (!pVB)
        return NULL;
    return pVB->lpVtbl->Release;
}

LPVOID lpvtbl_IndexBufferRelease(IDirect3DIndexBuffer9* pIB)
{
    if (!pIB)
        return NULL;
    return pIB->lpVtbl->Release;
}

#undef CINTERFACE
