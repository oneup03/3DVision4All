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

// DX9 hook chain lifted from DeviarePlugin/InProc_DX9.cpp.
//
// Key refactors vs. the original:
//  * No cross-process IPC: dropped CaptureSetupMutex / ReleaseSetupMutex /
//    gMappedView writes / gGameSharedHandle / D3DSHAREDHANDLE arguments.
//  * No secondary shared RenderTarget — StretchRect destination on Present
//    is now the game's own backbuffer (the SbS override).
//  * The 2W×H stereo staging surface is local to this process and used only
//    as the StretchRect source for the squeeze pass.

#include "Core.h"


// Two-texture capture path (per bo3b's hard-won finding in the original
// DeviarePlugin/InProc_DX9.cpp:181-238):
//
//   g_stereoTex / g_stereoStage — NON-shared RT, sized 2W×H. Reverse-stereo
//     -blit writes into this. Critical: the destination of reverse-blit
//     CANNOT be a shared-handle texture; the driver silently fails and
//     stores mono content.
//
//   g_sharedTex / g_sharedSurface — SHARED RT, sized 2W×H, created with a
//     non-null pSharedHandle so DX9Ex returns a kernel handle Device B can
//     open. We StretchRect from g_stereoStage into this every frame
//     (still under reverse-blit-control = true), and the cross-device
//     share carries the data to Device B.
//
// Collapsing these two into one (which we tried first) loses stereo content.
static IDirect3DTexture9* g_stereoTex     = nullptr;
static IDirect3DSurface9* g_stereoStage   = nullptr;
static IDirect3DTexture9* g_sharedTex     = nullptr;
static IDirect3DSurface9* g_sharedSurface = nullptr;

// Published for Output_Overlay.cpp / Device B to pick up. Cleared on Reset.
HANDLE g_stagingSharedHandle = nullptr;
UINT   g_stagingWidth        = 0;
UINT   g_stagingHeight       = 0;
UINT   g_stagingD3DFormat    = 0;
HWND   g_gameFocusHwnd       = nullptr;


// Hook trampolines — set by g_nktInProc.Hook().
static IDirect3D9* (__stdcall *pOrigDirect3DCreate9)(UINT SDKVersion) = nullptr;

static HRESULT (__stdcall *pOrigCreateDevice)(
    IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD,
    D3DPRESENT_PARAMETERS*, IDirect3DDevice9**) = nullptr;

static HRESULT (__stdcall *pOrigPresent)(
    IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*) = nullptr;

static HRESULT (__stdcall *pOrigReset)(
    IDirect3DDevice9*, D3DPRESENT_PARAMETERS*) = nullptr;

static HRESULT (__stdcall *pOrigCreateTexture)(
    IDirect3DDevice9*, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL,
    IDirect3DTexture9**, HANDLE*) = nullptr;

static HRESULT (__stdcall *pOrigCreateCubeTexture)(
    IDirect3DDevice9*, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL,
    IDirect3DCubeTexture9**, HANDLE*) = nullptr;

static HRESULT (__stdcall *pOrigCreateVolumeTexture)(
    IDirect3DDevice9*, UINT, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL,
    IDirect3DVolumeTexture9**, HANDLE*) = nullptr;

static HRESULT (__stdcall *pOrigCreateOffscreenPlainSurface)(
    IDirect3DDevice9*, UINT, UINT, D3DFORMAT, D3DPOOL,
    IDirect3DSurface9**, HANDLE*) = nullptr;

static HRESULT (__stdcall *pOrigCreateVertexBuffer)(
    IDirect3DDevice9*, UINT, DWORD, DWORD, D3DPOOL,
    IDirect3DVertexBuffer9**, HANDLE*) = nullptr;

static HRESULT (__stdcall *pOrigCreateIndexBuffer)(
    IDirect3DDevice9*, UINT, DWORD, D3DFORMAT, D3DPOOL,
    IDirect3DIndexBuffer9**, HANDLE*) = nullptr;


// --------------------------------------------------------------------------
// 2W×H stereo staging creation. Sized from the current backbuffer at
// first Present (or after Reset). Mirrors CreateSharedRenderTarget in the
// original, but drops the shared-handle side.

static void EnsureStereoStage(IDirect3DDevice9* device)
{
    if (g_stereoStage)
        return;

    HRESULT hr;
    IDirect3DSurface9* pBackBuffer = nullptr;
    hr = device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer);
    if (FAILED(hr) || !pBackBuffer) {
        KLOG(L"EnsureStereoStage: GetBackBuffer failed hr=0x%x\n", hr);
        return;
    }

    D3DSURFACE_DESC desc;
    hr = pBackBuffer->GetDesc(&desc);
    pBackBuffer->Release();
    if (FAILED(hr)) {
        KLOG(L"EnsureStereoStage: GetDesc failed hr=0x%x\n", hr);
        return;
    }

    UINT width  = desc.Width * 2;
    UINT height = desc.Height;

    KLOG(L"EnsureStereoStage: %dx%d format=%d (two-texture: non-shared capture + shared handoff)\n",
         width, height, desc.Format);

    // 1) Non-shared RT — destination of reverse-stereo-blit.
    hr = device->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET,
                               desc.Format, D3DPOOL_DEFAULT,
                               &g_stereoTex, nullptr);
    if (FAILED(hr)) {
        KLOG(L"EnsureStereoStage: CreateTexture(non-shared) failed hr=0x%x\n", hr);
        return;
    }
    hr = g_stereoTex->GetSurfaceLevel(0, &g_stereoStage);
    if (FAILED(hr)) {
        KLOG(L"EnsureStereoStage: GetSurfaceLevel(non-shared) failed hr=0x%x\n", hr);
        g_stereoTex->Release();
        g_stereoTex = nullptr;
        return;
    }

    // 2) Shared RT — Device B opens this via the kernel handle.
    HANDLE sharedHandle = nullptr;
    hr = device->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET,
                               desc.Format, D3DPOOL_DEFAULT,
                               &g_sharedTex, &sharedHandle);
    if (FAILED(hr) || !sharedHandle) {
        KLOG(L"EnsureStereoStage: CreateTexture(shared) failed hr=0x%x handle=%p — overlay won't have data\n",
             hr, sharedHandle);
        // Capture still works for in-process use; just no cross-device.
        sharedHandle = nullptr;
    } else {
        hr = g_sharedTex->GetSurfaceLevel(0, &g_sharedSurface);
        if (FAILED(hr)) {
            KLOG(L"EnsureStereoStage: GetSurfaceLevel(shared) failed hr=0x%x\n", hr);
            g_sharedTex->Release();
            g_sharedTex = nullptr;
            sharedHandle = nullptr;
        }
    }

    // Publish for Device B (Output_Overlay.cpp).
    g_stagingSharedHandle = sharedHandle;
    g_stagingWidth        = width;
    g_stagingHeight       = height;
    g_stagingD3DFormat    = (UINT)desc.Format;

    KLOG(L"EnsureStereoStage: published shared handle=%p (non-shared tex=%p, shared tex=%p)\n",
         g_stagingSharedHandle, g_stereoTex, g_sharedTex);

    // First-time NvAPI handle creation against this device.
    if (!g_nvapi) {
        NvAPI_Status nvres = NvAPI_Initialize();
        if (nvres != NVAPI_OK) {
            KLOG(L"EnsureStereoStage: NvAPI_Initialize failed %d\n", nvres);
        } else {
            nvres = NvAPI_Stereo_CreateHandleFromIUnknown(device, &g_nvapi);
            if (nvres != NVAPI_OK) {
                KLOG(L"EnsureStereoStage: NvAPI_Stereo_CreateHandleFromIUnknown failed %d\n", nvres);
            }
        }
    }
}


static void ReleaseStereoStage()
{
    if (g_stereoStage)   { g_stereoStage->Release();   g_stereoStage   = nullptr; }
    if (g_stereoTex)     { g_stereoTex->Release();     g_stereoTex     = nullptr; }
    if (g_sharedSurface) { g_sharedSurface->Release(); g_sharedSurface = nullptr; }
    if (g_sharedTex)     { g_sharedTex->Release();     g_sharedTex     = nullptr; }
    g_stagingSharedHandle = nullptr;
    g_stagingWidth = g_stagingHeight = 0;
    g_stagingD3DFormat = 0;
}


// Copy the non-shared 2W×H capture into the shared 2W×H texture for
// Device B. When swap_eyes is on, swap the L/R halves during this copy so
// every consumer downstream (compose shader and the LeiaSR weaver alike)
// reads L-view on the left and R-view on the right. Doing the swap here
// means swap_eyes works uniformly across all output modes — including
// LeiaSR, which doesn't go through our compose shader.
static void CopyStageToShared_MaybeSwap(IDirect3DDevice9* device)
{
    if (!g_stereoStage || !g_sharedSurface) return;

    if (!g_config.swap_eyes) {
        device->StretchRect(g_stereoStage, nullptr, g_sharedSurface, nullptr, D3DTEXF_NONE);
        return;
    }

    D3DSURFACE_DESC stDesc;
    if (FAILED(g_stereoStage->GetDesc(&stDesc))) return;
    LONG halfW = (LONG)stDesc.Width / 2;
    LONG h     = (LONG)stDesc.Height;

    RECT leftHalf  = { 0,     0, halfW,             h };
    RECT rightHalf = { halfW, 0, (LONG)stDesc.Width, h };

    // L source -> R destination
    device->StretchRect(g_stereoStage, &leftHalf,  g_sharedSurface, &rightHalf, D3DTEXF_NONE);
    // R source -> L destination
    device->StretchRect(g_stereoStage, &rightHalf, g_sharedSurface, &leftHalf,  D3DTEXF_NONE);
}


// --------------------------------------------------------------------------
// Hooked Present — the SbS override happens here.

static unsigned long s_frameCount = 0;

static HRESULT __stdcall Hooked_Present(IDirect3DDevice9* This,
                                         const RECT*    pSourceRect,
                                         const RECT*    pDestRect,
                                         HWND           hDestWindowOverride,
                                         const RGNDATA* pDirtyRegion)
{
    EnsureStereoStage(This);

    if (g_stereoStage) {
        IDirect3DSurface9* backBuffer = nullptr;
        HRESULT hr = This->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer);
        if (SUCCEEDED(hr) && backBuffer) {

            if (g_directMode) {
                // Direct Mode: per-eye copy via SetActiveEye. Each eye fills
                // half of the 2W×H non-shared staging surface.
                D3DSURFACE_DESC bbDesc;
                backBuffer->GetDesc(&bbDesc);

                RECT destRect = { 0, 0, (LONG)bbDesc.Width, (LONG)bbDesc.Height };

                NvAPI_Stereo_SetActiveEye(g_nvapi, NVAPI_STEREO_EYE_RIGHT);
                This->StretchRect(backBuffer, nullptr, g_stereoStage, &destRect, D3DTEXF_NONE);

                destRect.left  = bbDesc.Width;
                destRect.right = bbDesc.Width * 2;
                NvAPI_Stereo_SetActiveEye(g_nvapi, NVAPI_STEREO_EYE_LEFT);
                This->StretchRect(backBuffer, nullptr, g_stereoStage, &destRect, D3DTEXF_NONE);

                // Then propagate to the shared texture for Device B,
                // swapping halves if the user has set swap_eyes.
                CopyStageToShared_MaybeSwap(This);
            }
            else {
                // Automatic Mode: reverse-stereo-blit captures both eyes
                // into the non-shared 2W×H staging surface in one
                // StretchRect, then we copy to the shared texture for
                // Device B. Both StretchRects happen while ReverseStereo-
                // BlitControl is TRUE, per bo3b's original pattern (see
                // InProc_DX9.cpp:319-329 in the original DeviarePlugin).
                NvAPI_Stereo_ReverseStereoBlitControl(g_nvapi, true);
                This->StretchRect(backBuffer, nullptr, g_stereoStage, nullptr, D3DTEXF_NONE);
                CopyStageToShared_MaybeSwap(This);
                NvAPI_Stereo_ReverseStereoBlitControl(g_nvapi, false);
            }

            // Device A's backbuffer is left as-is; the D3D11 overlay
            // window composes the visible image from the staging texture.
            backBuffer->Release();
        }
    }

    // Kick off the overlay output thread (idempotent, only first call
    // constructs anything).
    if (g_gameFocusHwnd)
        Overlay_StartOnce(g_gameFocusHwnd);

    s_frameCount++;

    HRESULT presentHr = pOrigPresent(This, pSourceRect, pDestRect,
                                     hDestWindowOverride, pDirtyRegion);

    // Notify the overlay AFTER pOrigPresent so the DX9 command queue
    // (including our StretchRect to the shared texture) is flushed before
    // Device B starts sampling. Without this, Device B can race ahead and
    // sample uninitialized / partial content from the shared texture even
    // though Device A's queued StretchRects haven't reached the GPU yet.
    Overlay_NotifyFrame();

    return presentHr;
}


// --------------------------------------------------------------------------
// Hooked Reset — staging is invalidated on device reset; recreate lazily
// from the next Present.

static HRESULT __stdcall Hooked_Reset(IDirect3DDevice9* This,
                                       D3DPRESENT_PARAMETERS* pPresentationParameters)
{
    KLOG(L"Hooked_Reset called\n");
    if (pPresentationParameters) {
        KLOG(L"  %dx%d format=%d windowed=%d\n",
             pPresentationParameters->BackBufferWidth,
             pPresentationParameters->BackBufferHeight,
             pPresentationParameters->BackBufferFormat,
             pPresentationParameters->Windowed);
    }

    ReleaseStereoStage();

    HRESULT resetHr = pOrigReset(This, pPresentationParameters);

    // Reset may have stripped WS_EX_LAYERED; re-assert.
    if (g_config.defeat_directflip && g_gameFocusHwnd) {
        LONG exStyle = GetWindowLongW(g_gameFocusHwnd, GWL_EXSTYLE);
        if (!(exStyle & WS_EX_LAYERED)) {
            SetWindowLongW(g_gameFocusHwnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
            SetLayeredWindowAttributes(g_gameFocusHwnd, 0, 255, LWA_ALPHA);
            KLOG(L"  Reset: re-applied WS_EX_LAYERED on game HWND %p\n", g_gameFocusHwnd);
        }
    }

    return resetHr;
}


// --------------------------------------------------------------------------
// DX9Ex compat hooks lifted verbatim from InProc_DX9.cpp.
// These force D3DPOOL_MANAGED → D3DPOOL_DEFAULT and add D3DUSAGE_DYNAMIC
// where required, because the game's CreateDevice silently became an Ex
// device and Ex doesn't accept MANAGED pool.

static HRESULT __stdcall Hooked_CreateTexture(IDirect3DDevice9* This,
    UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format,
    D3DPOOL Pool, IDirect3DTexture9** ppTexture, HANDLE* pSharedHandle)
{
    if (Pool == D3DPOOL_MANAGED) Pool = D3DPOOL_DEFAULT;
    int renderOrStencil = Usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL);
    if (!renderOrStencil) Usage |= D3DUSAGE_DYNAMIC;
    return pOrigCreateTexture(This, Width, Height, Levels, Usage, Format, Pool, ppTexture, pSharedHandle);
}

static HRESULT __stdcall Hooked_CreateCubeTexture(IDirect3DDevice9* This,
    UINT EdgeLength, UINT Levels, DWORD Usage, D3DFORMAT Format,
    D3DPOOL Pool, IDirect3DCubeTexture9** ppCubeTexture, HANDLE* pSharedHandle)
{
    if (Pool == D3DPOOL_MANAGED) Pool = D3DPOOL_DEFAULT;
    int renderOrStencil = Usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL);
    if (!renderOrStencil) Usage |= D3DUSAGE_DYNAMIC;
    return pOrigCreateCubeTexture(This, EdgeLength, Levels, Usage, Format, Pool, ppCubeTexture, pSharedHandle);
}

static HRESULT __stdcall Hooked_CreateVolumeTexture(IDirect3DDevice9* This,
    UINT Width, UINT Height, UINT Depth, UINT Levels, DWORD Usage,
    D3DFORMAT Format, D3DPOOL Pool, IDirect3DVolumeTexture9** ppVolumeTexture, HANDLE* pSharedHandle)
{
    if (Pool == D3DPOOL_MANAGED) Pool = D3DPOOL_DEFAULT;
    int renderOrStencil = Usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL);
    if (!renderOrStencil) Usage |= D3DUSAGE_DYNAMIC;
    return pOrigCreateVolumeTexture(This, Width, Height, Depth, Levels, Usage, Format, Pool, ppVolumeTexture, pSharedHandle);
}

static HRESULT __stdcall Hooked_CreateOffscreenPlainSurface(IDirect3DDevice9* This,
    UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool,
    IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle)
{
    if (Pool == D3DPOOL_MANAGED) Pool = D3DPOOL_DEFAULT;
    return pOrigCreateOffscreenPlainSurface(This, Width, Height, Format, Pool, ppSurface, pSharedHandle);
}

static HRESULT __stdcall Hooked_CreateVertexBuffer(IDirect3DDevice9* This,
    UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool,
    IDirect3DVertexBuffer9** ppVertexBuffer, HANDLE* pSharedHandle)
{
    if (Pool == D3DPOOL_MANAGED) Pool = D3DPOOL_DEFAULT;
    Usage |= D3DUSAGE_DYNAMIC;
    return pOrigCreateVertexBuffer(This, Length, Usage, FVF, Pool, ppVertexBuffer, pSharedHandle);
}

static HRESULT __stdcall Hooked_CreateIndexBuffer(IDirect3DDevice9* This,
    UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
    IDirect3DIndexBuffer9** ppIndexBuffer, HANDLE* pSharedHandle)
{
    if (Pool == D3DPOOL_MANAGED) Pool = D3DPOOL_DEFAULT;
    Usage |= D3DUSAGE_DYNAMIC;
    return pOrigCreateIndexBuffer(This, Length, Usage, Format, Pool, ppIndexBuffer, pSharedHandle);
}


// --------------------------------------------------------------------------
// Hooked CreateDevice — chains Present / Reset / Create* hooks off the
// returned device.

static HRESULT __stdcall Hooked_CreateDevice(IDirect3D9* This,
    UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags,
    D3DPRESENT_PARAMETERS* pPresentationParameters,
    IDirect3DDevice9** ppReturnedDeviceInterface)
{
    KLOG(L"Hooked_CreateDevice\n");
    if (pPresentationParameters) {
        KLOG(L"  %dx%d format=%d windowed=%d swap_effect=%d\n",
             pPresentationParameters->BackBufferWidth,
             pPresentationParameters->BackBufferHeight,
             pPresentationParameters->BackBufferFormat,
             pPresentationParameters->Windowed,
             pPresentationParameters->SwapEffect);
    }

    // Capture the focus HWND for the overlay-window output path. Prefer
    // hDeviceWindow (the swap chain's window); fall back to hFocusWindow.
    HWND gameHwnd = (pPresentationParameters && pPresentationParameters->hDeviceWindow)
                  ? pPresentationParameters->hDeviceWindow
                  : hFocusWindow;
    if (gameHwnd && gameHwnd != g_gameFocusHwnd) {
        g_gameFocusHwnd = gameHwnd;
        KLOG(L"  game focus HWND=%p\n", g_gameFocusHwnd);
    }

    BehaviorFlags |= D3DCREATE_MULTITHREADED;

    HRESULT hr = pOrigCreateDevice(This, Adapter, DeviceType, hFocusWindow,
                                   BehaviorFlags, pPresentationParameters,
                                   ppReturnedDeviceInterface);
    if (FAILED(hr)) {
        KLOG(L"Hooked_CreateDevice: original failed hr=0x%x\n", hr);
        return hr;
    }

    IDirect3DDevice9* pDevice9 = ppReturnedDeviceInterface ? *ppReturnedDeviceInterface : nullptr;
    KLOG(L"  CreateDevice returned device=%p\n", pDevice9);

    // Defeat DirectFlip / Independent Flip on the game's window. DWM cannot
    // DirectFlip a layered window's swap chain, so it routes the game's
    // swap chain through the redirection surface — and our topmost overlay
    // window can then composite over it. LWA_ALPHA=255 keeps the game's
    // window fully opaque.
    if (g_config.defeat_directflip && g_gameFocusHwnd) {
        LONG exStyle = GetWindowLongW(g_gameFocusHwnd, GWL_EXSTYLE);
        if (!(exStyle & WS_EX_LAYERED)) {
            SetWindowLongW(g_gameFocusHwnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
            SetLayeredWindowAttributes(g_gameFocusHwnd, 0, 255, LWA_ALPHA);
            KLOG(L"  defeat_directflip: added WS_EX_LAYERED on game HWND %p\n", g_gameFocusHwnd);
        }
    }

    if (pOrigPresent == nullptr && pDevice9) {
        SIZE_T hook_id = 0;
        DWORD dwOsErr;

        dwOsErr = g_nktInProc.Hook(&hook_id, (void**)&pOrigPresent,
                                   lpvtbl_Present_DX9(pDevice9), Hooked_Present, 0);
        if (FAILED(dwOsErr)) KLOG(L"Failed to hook Present 0x%x\n", dwOsErr);
        else                 KLOG(L"Hooked Present\n");

        dwOsErr = g_nktInProc.Hook(&hook_id, (void**)&pOrigReset,
                                   lpvtbl_Reset(pDevice9), Hooked_Reset, 0);
        if (FAILED(dwOsErr)) KLOG(L"Failed to hook Reset 0x%x\n", dwOsErr);

        dwOsErr = g_nktInProc.Hook(&hook_id, (void**)&pOrigCreateTexture,
                                   lpvtbl_CreateTexture(pDevice9), Hooked_CreateTexture, 0);
        if (FAILED(dwOsErr)) KLOG(L"Failed to hook CreateTexture 0x%x\n", dwOsErr);

        dwOsErr = g_nktInProc.Hook(&hook_id, (void**)&pOrigCreateCubeTexture,
                                   lpvtbl_CreateCubeTexture(pDevice9), Hooked_CreateCubeTexture, 0);
        if (FAILED(dwOsErr)) KLOG(L"Failed to hook CreateCubeTexture 0x%x\n", dwOsErr);

        dwOsErr = g_nktInProc.Hook(&hook_id, (void**)&pOrigCreateVolumeTexture,
                                   lpvtbl_CreateVolumeTexture(pDevice9), Hooked_CreateVolumeTexture, 0);
        if (FAILED(dwOsErr)) KLOG(L"Failed to hook CreateVolumeTexture 0x%x\n", dwOsErr);

        dwOsErr = g_nktInProc.Hook(&hook_id, (void**)&pOrigCreateOffscreenPlainSurface,
                                   lpvtbl_CreateOffscreenPlainSurface(pDevice9), Hooked_CreateOffscreenPlainSurface, 0);
        if (FAILED(dwOsErr)) KLOG(L"Failed to hook CreateOffscreenPlainSurface 0x%x\n", dwOsErr);

        dwOsErr = g_nktInProc.Hook(&hook_id, (void**)&pOrigCreateVertexBuffer,
                                   lpvtbl_CreateVertexBuffer(pDevice9), Hooked_CreateVertexBuffer, 0);
        if (FAILED(dwOsErr)) KLOG(L"Failed to hook CreateVertexBuffer 0x%x\n", dwOsErr);

        dwOsErr = g_nktInProc.Hook(&hook_id, (void**)&pOrigCreateIndexBuffer,
                                   lpvtbl_CreateIndexBuffer(pDevice9), Hooked_CreateIndexBuffer, 0);
        if (FAILED(dwOsErr)) KLOG(L"Failed to hook CreateIndexBuffer 0x%x\n", dwOsErr);

        // NvAPI handle is created lazily in EnsureStereoStage on first Present.
    }

    return hr;
}


// --------------------------------------------------------------------------
// Hooked Direct3DCreate9 — quietly upgrades the game's IDirect3D9 to
// IDirect3D9Ex. The returned Ex object is castable to plain IDirect3D9 from
// the game's perspective. Required so the device that comes out of
// CreateDevice is Ex (NvAPI_Stereo_CreateHandleFromIUnknown demands Ex).

static IDirect3D9* __stdcall Hooked_Direct3DCreate9(UINT SDKVersion)
{
    KLOG(L"Hooked_Direct3DCreate9 SDK=%d\n", SDKVersion);

    IDirect3D9Ex* pDX9Ex = nullptr;
    HRESULT hr = Direct3DCreate9Ex(SDKVersion, &pDX9Ex);
    if (FAILED(hr) || !pDX9Ex) {
        KLOG(L"  Direct3DCreate9Ex failed hr=0x%x, falling back to plain DX9\n", hr);
        return pOrigDirect3DCreate9(SDKVersion);
    }

    // Hook CreateDevice on the Ex object (vtable address is shared with
    // plain IDirect3D9 so cast is safe).
    if (pOrigCreateDevice == nullptr) {
        SIZE_T hook_id = 0;
        DWORD dwOsErr = g_nktInProc.Hook(&hook_id, (void**)&pOrigCreateDevice,
                                         lpvtbl_CreateDevice((IDirect3D9*)pDX9Ex),
                                         Hooked_CreateDevice, 0);
        if (FAILED(dwOsErr)) KLOG(L"Failed to hook IDirect3D9::CreateDevice 0x%x\n", dwOsErr);
        else                 KLOG(L"Hooked IDirect3D9::CreateDevice\n");
    }

    return (IDirect3D9*)pDX9Ex;
}


// --------------------------------------------------------------------------
// Hook installer — entry point called from Init.cpp's init thread.

void DX9_InstallHooks()
{
    WCHAR d3d9Path[MAX_PATH] = L"";
    UINT size = GetSystemDirectoryW(d3d9Path, MAX_PATH);
    if (size == 0) {
        KLOG(L"DX9_InstallHooks: GetSystemDirectory failed err=0x%x\n", GetLastError());
        return;
    }
    wcscat_s(d3d9Path, MAX_PATH, L"\\d3d9.dll");

    HMODULE hSystemD3D9 = LoadLibraryW(d3d9Path);
    if (!hSystemD3D9) {
        KLOG(L"DX9_InstallHooks: LoadLibrary %s failed err=0x%x\n", d3d9Path, GetLastError());
        return;
    }
    KLOG(L"DX9_InstallHooks: loaded %s @ %p\n", d3d9Path, hSystemD3D9);

    FARPROC sysDirect3DCreate9 = GetProcAddress(hSystemD3D9, "Direct3DCreate9");
    if (!sysDirect3DCreate9) {
        KLOG(L"DX9_InstallHooks: Direct3DCreate9 not found err=0x%x\n", GetLastError());
        return;
    }

    if (pOrigDirect3DCreate9 != nullptr)
        return;  // already hooked

    SIZE_T hook_id = 0;
    DWORD dwOsErr = g_nktInProc.Hook(&hook_id, (void**)&pOrigDirect3DCreate9,
                                     sysDirect3DCreate9, Hooked_Direct3DCreate9, 0);
    if (FAILED(dwOsErr)) {
        KLOG(L"DX9_InstallHooks: Hook(Direct3DCreate9) failed 0x%x\n", dwOsErr);
        return;
    }

    KLOG(L"DX9_InstallHooks: hooked Direct3DCreate9 @ %p\n", sysDirect3DCreate9);
}
