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

// Katanga shared-texture IPC publisher.
//
// Mirrors the producer side of the Katanga protocol used by Katanga.exe
// (Unity VR viewer) and VRScreenCap (Rust/OpenXR viewer). The protocol
// is reverse-engineered from katanga/DeviarePlugin/{DeviarePlugin,InProc_DX11}.cpp
// and katanga/UnityNativePlugin/RenderAPI_D3D11.cpp:
//
//   1. Producer (us) creates a named MMF "Local\KatangaMappedFile"
//      sized to sizeof(UINT) = 4 bytes (DeviarePlugin.cpp:121 — the
//      game-side plugin owns the MMF). Both Katanga.exe's Unity plugin
//      (UnityNativePlugin/RenderAPI_D3D11.cpp:486) and VRScreenCap's
//      katanga_loader.rs:42 only OPEN it, so without the producer
//      creating it the MMF never exists and the consumer's polling
//      silently waits forever — manifesting as "VR viewer doesn't see
//      our stereo image" with no obvious failure mode.
//   2. Consumer creates a named mutex "KatangaSetupMutex"
//      (UnityNativePlugin/RenderAPI_D3D11.cpp:396). We use CreateMutexW
//      with bInitialOwner=FALSE so the launch order doesn't matter —
//      whoever runs first creates it, the other side attaches to the
//      existing one. The mutex synchronizes the texture-recreate
//      window (resolution change, format change); per-frame CopyResource
//      runs unsynchronized.
//   3. Producer (us) creates an ID3D11Texture2D with
//        Width  = 2 × game width   (full-SbS, eyes side-by-side)
//        Height = game height
//        Format = backbuffer format with sRGB stripped to linear
//        MiscFlags = D3D11_RESOURCE_MISC_SHARED
//        BindFlags = SHADER_RESOURCE | RENDER_TARGET
//   4. Producer calls IDXGIResource::GetSharedHandle to obtain the
//      cross-process HANDLE, stores it as a 32-bit UINT into the MMF
//      (the legacy KMT shared-handle space is 32-bit on every platform).
//   5. Per frame, the producer copies the latest stereo image into the
//      shared texture and Presents normally — no per-frame
//      synchronization beyond the shared-handle write is required (the
//      consumer polls the MMF for handle changes and just samples the
//      texture; mid-frame tearing is tolerated by the VR consumer's
//      reproject path).
//   6. On any recreate (resolution change, format change, restart) the
//      producer grabs KatangaSetupMutex, recreates the texture, writes
//      the new handle into the MMF, and releases the mutex.
//
// Eye layout: per the user-confirmed design, we publish R-on-LEFT /
// L-on-RIGHT (the Katanga ecosystem convention) regardless of the
// 3DVision4All `swap_eyes` knob. That matches Katanga.exe's Unity scene
// and VRScreenCap's default (`swap_eyes = true` in config.rs:31). Our
// own staging is L-on-LEFT after Hooks_DX9's capture-side fixup, so we
// perform two CopySubresourceRegion calls to swap halves into the
// shared texture. If a user wants natural-order output instead, they
// can set --swap-eyes=false on vr-screen-cap (or the equivalent on
// Katanga.exe) to undo our convention swap.
//
// Connection lifecycle: the consumer can launch before or after the
// game. We create the MMF + mutex on the first frame after entering
// Katanga mode. Named-object semantics make launch order irrelevant —
// CreateFileMappingW / CreateMutexW attach to the existing object if
// the consumer was already running. Once attached we publish the
// shared-texture handle into the MMF; the consumer's poll loop picks
// it up and starts sampling. If the consumer dies mid-session we keep
// publishing — relaunching the consumer alone is enough to reconnect,
// since the MMF and texture handle are still live in our address
// space.

#include "Core.h"

#include <d3d11.h>
#include <dxgi.h>


static const wchar_t kKatangaMmfName[]   = L"Local\\KatangaMappedFile";
static const wchar_t kKatangaMutexName[] = L"KatangaSetupMutex";


// IPC state. Created on the first publish call (named-object semantics
// attach to an already-running consumer's mutex if one exists, or
// create fresh objects if not), then kept open for the session.
static HANDLE s_kMappedFile  = nullptr;
static LPVOID s_kMappedView  = nullptr;
static HANDLE s_kSetupMutex  = nullptr;
static bool   s_kIpcReady    = false;

// Shared texture on Device B. Recreated whenever the staging dims change.
static ID3D11Texture2D* s_kSharedTex    = nullptr;
static HANDLE           s_kSharedHandle = nullptr;
static UINT             s_kTexWidth     = 0;
static UINT             s_kTexHeight    = 0;
static DXGI_FORMAT      s_kTexFormat    = DXGI_FORMAT_UNKNOWN;


// Strip sRGB → linear so a VR consumer doing its own tonemap doesn't
// double up the gamma curve. Katanga's plugin does the same fixup at
// InProc_DX11.cpp:183-186 for the same reason.
static DXGI_FORMAT KatangaStripSrgb(DXGI_FORMAT f)
{
    switch (f) {
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM;
        case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8X8_UNORM;
        default:                              return f;
    }
}


// Set up the producer side of the Katanga IPC. We own the MMF and
// initialize its handle slot to 0 so an already-polling consumer sees
// "not ready yet" instead of stale data. For the mutex, CreateMutexW
// attaches to the consumer's existing object if it ran first, or
// creates the kernel object ourselves if not — bInitialOwner=FALSE in
// either case (we acquire it explicitly during recreate).
//
// Returns true once both objects are live. On any failure we tear
// down whatever we partially created and return false; the caller can
// retry next frame (cheap — failure here means a permissions issue or
// out-of-memory, neither of which is going to fix itself, but at
// least we won't crash).
static bool SetupKatangaIpc()
{
    HANDLE mmf = CreateFileMappingW(
        INVALID_HANDLE_VALUE,   // backed by paging file
        nullptr,                // default security
        PAGE_READWRITE,
        0, sizeof(UINT),        // 4-byte object (the 32-bit shared handle)
        kKatangaMmfName);
    if (!mmf) {
        KLOG(L"Katanga: CreateFileMappingW failed err=0x%x\n", GetLastError());
        return false;
    }
    bool mmfPreExisted = (GetLastError() == ERROR_ALREADY_EXISTS);

    LPVOID view = MapViewOfFile(mmf, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(UINT));
    if (!view) {
        KLOG(L"Katanga: MapViewOfFile failed err=0x%x\n", GetLastError());
        CloseHandle(mmf);
        return false;
    }
    // Reset the slot to 0 so the consumer's "handle changed" detector
    // fires on our first real publish, even if a previous producer
    // left a stale value in there.
    *(PUINT)view = 0;

    HANDLE mutex = CreateMutexW(nullptr, FALSE /*bInitialOwner*/, kKatangaMutexName);
    if (!mutex) {
        KLOG(L"Katanga: CreateMutexW failed err=0x%x\n", GetLastError());
        UnmapViewOfFile(view);
        CloseHandle(mmf);
        return false;
    }
    bool mutexPreExisted = (GetLastError() == ERROR_ALREADY_EXISTS);

    s_kMappedFile = mmf;
    s_kMappedView = view;
    s_kSetupMutex = mutex;
    s_kIpcReady   = true;
    KLOG(L"Katanga: IPC ready (mmf=%p%s mutex=%p%s view=%p)\n",
         mmf, mmfPreExisted ? L" pre-existing" : L" created",
         mutex, mutexPreExisted ? L" pre-existing" : L" created",
         view);
    return true;
}


// Tear down the shared texture and clear the published handle so a
// reconnecting consumer doesn't read a stale value.
static void ReleaseSharedTexture()
{
    if (s_kSharedTex) { s_kSharedTex->Release(); s_kSharedTex = nullptr; }
    s_kSharedHandle = nullptr;
    s_kTexWidth = s_kTexHeight = 0;
    s_kTexFormat = DXGI_FORMAT_UNKNOWN;
    if (s_kMappedView) *(PUINT)s_kMappedView = 0;
}


// (Re)create the shared texture and publish its handle. Called whenever
// the staging dims/format change. Runs under KatangaSetupMutex so the
// consumer's draw thread can't sample the texture mid-recreate.
//
// Returns true on success. On any failure the existing shared texture is
// torn down (and the MMF handle cleared) so we don't leave the consumer
// pointing at a dangling object.
static bool RecreateSharedTexture(ID3D11Device* device,
                                  UINT          width,
                                  UINT          height,
                                  DXGI_FORMAT   fmt)
{
    DWORD waitRes = WaitForSingleObject(s_kSetupMutex, 1000);
    if (waitRes != WAIT_OBJECT_0) {
        KLOG(L"Katanga: WaitForSingleObject(setup mutex) 0x%x -- consumer may be gone\n",
             waitRes);
        return false;
    }

    bool ok = false;
    ReleaseSharedTexture();

    DXGI_FORMAT useFmt = KatangaStripSrgb(fmt);

    D3D11_TEXTURE2D_DESC td = {};
    td.Width            = width;
    td.Height           = height;
    td.MipLevels        = 1;
    td.ArraySize        = 1;
    td.Format           = useFmt;
    td.SampleDesc.Count = 1;
    td.Usage            = D3D11_USAGE_DEFAULT;
    td.BindFlags        = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    td.MiscFlags        = D3D11_RESOURCE_MISC_SHARED;

    HRESULT hr = device->CreateTexture2D(&td, nullptr, &s_kSharedTex);
    if (FAILED(hr) || !s_kSharedTex) {
        KLOG(L"Katanga: CreateTexture2D failed hr=0x%x %ux%u fmt=%d\n",
             hr, width, height, (int)useFmt);
        s_kSharedTex = nullptr;
        ReleaseMutex(s_kSetupMutex);
        return false;
    }

    IDXGIResource* dxgiRes = nullptr;
    hr = s_kSharedTex->QueryInterface(__uuidof(IDXGIResource), (void**)&dxgiRes);
    if (FAILED(hr) || !dxgiRes) {
        KLOG(L"Katanga: QI IDXGIResource hr=0x%x\n", hr);
        ReleaseSharedTexture();
        ReleaseMutex(s_kSetupMutex);
        return false;
    }
    hr = dxgiRes->GetSharedHandle(&s_kSharedHandle);
    dxgiRes->Release();
    if (FAILED(hr) || !s_kSharedHandle) {
        KLOG(L"Katanga: GetSharedHandle hr=0x%x\n", hr);
        ReleaseSharedTexture();
        ReleaseMutex(s_kSetupMutex);
        return false;
    }

    // Stamp the 32-bit shared-handle value into the MMF. Per the
    // Katanga protocol comment in InProc_DX11.cpp:213-217 the legacy
    // KMT shared-handle namespace is 32-bit on every platform, so a
    // PtrToUint is loss-free even in a 64-bit producer process.
    *(PUINT)s_kMappedView = PtrToUint(s_kSharedHandle);

    s_kTexWidth  = width;
    s_kTexHeight = height;
    s_kTexFormat = useFmt;
    ok = true;
    KLOG(L"Katanga: shared texture published %ux%u fmt=%d handle=%p (32b=0x%x)\n",
         width, height, (int)useFmt, s_kSharedHandle, PtrToUint(s_kSharedHandle));

    ReleaseMutex(s_kSetupMutex);
    return ok;
}


// --------------------------------------------------------------------------
// Public entry points.

void Katanga_PublishFrame(ID3D11Device*        device,
                          ID3D11DeviceContext* ctx,
                          ID3D11Texture2D*     stagingTex,
                          UINT                 stagingWidth,
                          UINT                 stagingHeight)
{
    if (!device || !ctx || !stagingTex || stagingWidth == 0 || stagingHeight == 0)
        return;

    // One-time IPC setup. We own both the MMF and (effectively) the
    // mutex via CreateMutexW's named-object attach semantics, so this
    // succeeds on the first call regardless of whether the consumer is
    // already running. Subsequent frames are a cheap predictable
    // branch.
    if (!s_kIpcReady && !SetupKatangaIpc())
        return;

    // Match the staging texture's actual format so CopySubresourceRegion
    // has no implicit conversion path (D3D11 requires bit-compatible
    // formats for resource copies). Strip sRGB on the way through for
    // the same gamma reason Katanga's own plugin does.
    D3D11_TEXTURE2D_DESC srcDesc = {};
    stagingTex->GetDesc(&srcDesc);
    DXGI_FORMAT wantFmt = KatangaStripSrgb(srcDesc.Format);

    if (!s_kSharedTex ||
        s_kTexWidth  != stagingWidth ||
        s_kTexHeight != stagingHeight ||
        s_kTexFormat != wantFmt) {
        if (!RecreateSharedTexture(device, stagingWidth, stagingHeight, srcDesc.Format))
            return;
    }

    // Half-swap copy. Staging holds L-on-LEFT (Hooks_DX9 already undid
    // the reverse-blit's right-on-left layout), but the Katanga
    // ecosystem expects R-on-LEFT / L-on-RIGHT, so put the staging's
    // right half into the shared texture's left half and vice versa.
    // Same shape as katanga/DeviarePlugin/InProc_DX11.cpp:296-300's
    // direct-mode path.
    UINT halfW = stagingWidth / 2;

    D3D11_BOX leftHalf  = { 0,     0, 0, halfW,         stagingHeight, 1 };
    D3D11_BOX rightHalf = { halfW, 0, 0, stagingWidth,  stagingHeight, 1 };

    // Staging's R half → shared's left half  (R-on-LEFT)
    ctx->CopySubresourceRegion(s_kSharedTex, 0, 0,     0, 0, stagingTex, 0, &rightHalf);
    // Staging's L half → shared's right half (L-on-RIGHT)
    ctx->CopySubresourceRegion(s_kSharedTex, 0, halfW, 0, 0, stagingTex, 0, &leftHalf);
}


void Katanga_Shutdown()
{
    // Grab the setup mutex one last time so a still-running consumer
    // doesn't sample our texture during its release. Best-effort —
    // skip the wait if the consumer is already gone.
    if (s_kIpcReady && s_kSetupMutex) {
        if (WaitForSingleObject(s_kSetupMutex, 250) == WAIT_OBJECT_0) {
            ReleaseSharedTexture();
            ReleaseMutex(s_kSetupMutex);
        } else {
            ReleaseSharedTexture();
        }
    } else {
        ReleaseSharedTexture();
    }

    if (s_kSetupMutex) { CloseHandle(s_kSetupMutex);   s_kSetupMutex = nullptr; }
    if (s_kMappedView) { UnmapViewOfFile(s_kMappedView); s_kMappedView = nullptr; }
    if (s_kMappedFile) { CloseHandle(s_kMappedFile);   s_kMappedFile = nullptr; }
    s_kIpcReady = false;
}
