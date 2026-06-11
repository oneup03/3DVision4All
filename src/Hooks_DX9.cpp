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

// DX9 hook chain — installs trampolines on the D3D9 export, the
// IDirect3D9 vtable, and each created IDirect3DDevice9's vtable to drive
// the NvAPI reverse-stereo-blit capture and publish the resulting SbS
// frame to the D3D11 overlay (Output_Overlay.cpp).

#include "Core.h"

#include <process.h>


// Two-texture capture path. Collapsing these into a single surface
// silently breaks the reverse-stereo-blit on at least some driver
// versions — the driver writes mono content and gives no error.
//
//   g_stereoTex / g_stereoStage — NON-shared RT, sized 2W×H, in the BB's
//     own format. Destination of reverse-stereo-blit. Must NOT be a
//     shared-handle texture.
//
//   g_sharedTex / g_sharedSurface — RT in A8R8G8B8 (the universally-
//     shareable lowest common denominator). Receives a StretchRect from
//     g_stereoStage that handles swap_eyes and format conversion in one
//     go. With alternate_capture_mode=1 (Ex device) and the driver
//     willing, this is created with a non-null pSharedHandle so Device B
//     can OpenSharedResource against it directly. Otherwise it falls
//     through to the CPU-readback path (see g_sysmemSurface below).
static IDirect3DTexture9* g_stereoTex     = nullptr;
static IDirect3DSurface9* g_stereoStage   = nullptr;

// Cross-API handoff surfaces. Double-buffered ONLY when on the CPU-readback
// path: the slow GetRenderTargetData / Lock / memcpy chain runs on a
// dedicated capture thread (see s_captureThread below), so the game's
// Present thread can write the NEXT frame's stereo into the OTHER buffer
// while the capture thread is still draining the previous one. The GPU
// shared-handle path leaves index 1 nullptr and uses only index 0.
static IDirect3DTexture9* g_sharedTex[2]     = { nullptr, nullptr };
static IDirect3DSurface9* g_sharedSurface[2] = { nullptr, nullptr };
static IDirect3DSurface9* g_sysmemSurface[2] = { nullptr, nullptr };
static CRITICAL_SECTION   s_bufLock[2]       = {};
static bool               s_bufLockInit      = false;

// Capture-thread plumbing. The game thread publishes the index of the
// buffer it just finished writing via s_pendingIdx and signals
// s_captureEvent; the capture thread atomically takes the index and does
// the GetRenderTargetData + Lock + memcpy out of band. s_lastProducedIdx
// is the game thread's local "alternate from this next time" hint.
static IDirect3DDevice9* s_captureDevice    = nullptr;
static HANDLE            s_captureThread    = nullptr;
static HANDLE            s_captureEvent     = nullptr;
static volatile LONG     s_captureShutdown  = 0;
static volatile LONG     s_lastProducedIdx  = 0;
static volatile LONG     s_pendingIdx       = -1;

// Published for Output_Overlay.cpp / Device B to pick up. Cleared on Reset.
HANDLE g_stagingSharedHandle = nullptr;
UINT   g_stagingWidth        = 0;
UINT   g_stagingHeight       = 0;
UINT   g_stagingD3DFormat    = 0;
HWND   g_gameFocusHwnd       = nullptr;

void*            g_stagingCpuBuffer = nullptr;
UINT             g_stagingCpuPitch  = 0;
CRITICAL_SECTION g_stagingCpuLock   = {};
volatile LONG    g_stagingCpuFresh  = 0;
static bool      g_stagingCpuLockInit = false;


// Hook trampolines — set by g_nktInProc.Hook().
static IDirect3D9* (__stdcall *pOrigDirect3DCreate9)(UINT SDKVersion) = nullptr;
static HRESULT     (__stdcall *pOrigDirect3DCreate9Ex)(UINT SDKVersion, IDirect3D9Ex** ppDX9Ex) = nullptr;

static HRESULT (__stdcall *pOrigCreateDevice)(
    IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD,
    D3DPRESENT_PARAMETERS*, IDirect3DDevice9**) = nullptr;

static HRESULT (__stdcall *pOrigGetAdapterDisplayMode)(
    IDirect3D9*, UINT, D3DDISPLAYMODE*) = nullptr;

static HRESULT (__stdcall *pOrigEnumAdapterModes)(
    IDirect3D9*, UINT, D3DFORMAT, UINT, D3DDISPLAYMODE*) = nullptr;

static UINT    (__stdcall *pOrigGetAdapterModeCount)(
    IDirect3D9*, UINT, D3DFORMAT) = nullptr;

static HRESULT (__stdcall *pOrigPresent)(
    IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*) = nullptr;

static HRESULT (__stdcall *pOrigReset)(
    IDirect3DDevice9*, D3DPRESENT_PARAMETERS*) = nullptr;

static HRESULT (__stdcall *pOrigCreateAdditionalSwapChain)(
    IDirect3DDevice9*, D3DPRESENT_PARAMETERS*, IDirect3DSwapChain9**) = nullptr;

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

// Forward decl: capture thread proc, defined below.
static unsigned __stdcall CaptureThreadProc(void* param);


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

    // Optional per-eye downsample into the shared / CPU staging path.
    // g_stereoStage (the reverse-stereo-blit destination, in BB format)
    // stays at the full 2W×H dims — NvAPI's reverse-blit destination has
    // to match the BB. But the StretchRect into g_sharedSurface[] can
    // downsample on the GPU, which is the cheap part; the downstream
    // readback / upload pipeline then carries far fewer pixels. See the
    // copy_* comment in Core.h.
    UINT stagingWidth  = width;
    UINT stagingHeight = height;
    if (g_config.copy_width  > 0 &&
        g_config.copy_height > 0) {
        UINT cw = g_config.copy_width  * 2;
        UINT ch = g_config.copy_height;
        if (cw < stagingWidth && ch < stagingHeight) {
            stagingWidth  = cw & ~1U;          // keep even for the half-split math
            stagingHeight = ch;
        }
    }

    KLOG(L"EnsureStereoStage: capture %ux%u format=%d, staging %ux%u\n",
         width, height, desc.Format, stagingWidth, stagingHeight);

    // 1) Non-shared RT — destination of reverse-stereo-blit, in the BB
    // format. Single instance (the reverse-stereo-blit always lands here
    // before we hand off to the shared/sysmem path).
    hr = device->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET,
                               desc.Format, D3DPOOL_DEFAULT,
                               &g_stereoTex, nullptr);
    if (FAILED(hr)) {
        KLOG(L"EnsureStereoStage: CreateTexture(stereoTex) failed hr=0x%x\n", hr);
        return;
    }
    hr = g_stereoTex->GetSurfaceLevel(0, &g_stereoStage);
    if (FAILED(hr)) {
        KLOG(L"EnsureStereoStage: GetSurfaceLevel(stereoStage) failed hr=0x%x\n", hr);
        g_stereoTex->Release();
        g_stereoTex = nullptr;
        return;
    }

    // 2) A8R8G8B8 DEFAULT-pool RT used as the swap + format-conversion
    // target for the StretchRect from g_stereoStage. Two creation paths:
    //
    //   GPU SHARED-HANDLE: when the device is Ex AND the driver lets us
    //   share an A8R8G8B8 RT at these dims, we ask for a kernel handle
    //   on buffer[0] so Device B can OpenSharedResource against it.
    //   Buffer[1] stays null — no capture thread needed.
    //
    //   CPU READBACK: shared creation failed (or was bypassed). Create
    //   buffer[0] AND buffer[1] without shared handles, plus a matching
    //   pair of SYSTEMMEM surfaces. The capture thread does the slow
    //   GetRenderTargetData / Lock / memcpy out of band so the game's
    //   Present thread doesn't stall.
    //
    // 8-bit BGRA is the lowest-common-denominator shareable format —
    // NVIDIA's D3D9 driver refuses to share 10-bit HDR formats
    // (D3DFMT_A2R10G10B10 at large dims) at all, and downstream
    // consumers (compose shader, LeiaSR weaver) all run at 8-bit.
    HANDLE sharedHandle = nullptr;
    hr = device->CreateTexture(stagingWidth, stagingHeight, 1, D3DUSAGE_RENDERTARGET,
                               D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
                               &g_sharedTex[0], &sharedHandle);
    if (FAILED(hr) || !sharedHandle) {
        KLOG(L"EnsureStereoStage: CreateTexture(shared) failed hr=0x%x handle=%p "
             L"— falling back to CPU-readback handoff\n", hr, sharedHandle);
        if (g_sharedTex[0]) { g_sharedTex[0]->Release(); g_sharedTex[0] = nullptr; }
        sharedHandle = nullptr;
        // Retry without the shared handle so we still get the swap +
        // format-conversion intermediate.
        hr = device->CreateTexture(stagingWidth, stagingHeight, 1, D3DUSAGE_RENDERTARGET,
                                   D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
                                   &g_sharedTex[0], nullptr);
        if (FAILED(hr)) {
            KLOG(L"EnsureStereoStage: CreateTexture(buf0 non-shared) failed hr=0x%x\n", hr);
            g_sharedTex[0] = nullptr;
        }
    }
    if (g_sharedTex[0]) {
        hr = g_sharedTex[0]->GetSurfaceLevel(0, &g_sharedSurface[0]);
        if (FAILED(hr)) {
            KLOG(L"EnsureStereoStage: GetSurfaceLevel(buf0) failed hr=0x%x\n", hr);
            g_sharedTex[0]->Release();
            g_sharedTex[0] = nullptr;
            sharedHandle = nullptr;
        }
    }

    // CPU-readback path: allocate buffer[1] + both SYSTEMMEM surfaces +
    // the per-buffer locks and the capture thread.
    if (!sharedHandle && g_sharedSurface[0]) {
        hr = device->CreateTexture(stagingWidth, stagingHeight, 1, D3DUSAGE_RENDERTARGET,
                                   D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
                                   &g_sharedTex[1], nullptr);
        if (SUCCEEDED(hr) && g_sharedTex[1]) {
            hr = g_sharedTex[1]->GetSurfaceLevel(0, &g_sharedSurface[1]);
            if (FAILED(hr)) {
                g_sharedTex[1]->Release(); g_sharedTex[1] = nullptr;
                g_sharedSurface[1] = nullptr;
            }
        } else {
            KLOG(L"EnsureStereoStage: CreateTexture(buf1) failed hr=0x%x — "
                 L"capture thread will run with single buffer (reduced parallelism)\n", hr);
            g_sharedTex[1] = nullptr;
        }

        for (int i = 0; i < 2; ++i) {
            if (!g_sharedSurface[i]) continue;
            hr = device->CreateOffscreenPlainSurface(stagingWidth, stagingHeight,
                                                     D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM,
                                                     &g_sysmemSurface[i], nullptr);
            if (FAILED(hr) || !g_sysmemSurface[i]) {
                KLOG(L"EnsureStereoStage: CreateOffscreenPlainSurface(sysmem[%d]) failed hr=0x%x\n", i, hr);
                g_sysmemSurface[i] = nullptr;
            }
        }

        // Allocate the CPU staging buffer. Init the lock BEFORE publishing
        // so any consumer that races in sees a usable lock alongside the
        // non-null buffer pointer.
        if (g_sysmemSurface[0]) {
            if (!g_stagingCpuLockInit) {
                InitializeCriticalSection(&g_stagingCpuLock);
                g_stagingCpuLockInit = true;
            }
            UINT pitch  = stagingWidth * 4;
            SIZE_T bytes = (SIZE_T)pitch * stagingHeight;
            void*  buf  = HeapAlloc(GetProcessHeap(), 0, bytes);
            if (!buf) {
                KLOG(L"EnsureStereoStage: HeapAlloc(%zu) failed\n", bytes);
            } else {
                InterlockedExchange(&g_stagingCpuFresh, 0);
                EnterCriticalSection(&g_stagingCpuLock);
                g_stagingCpuBuffer = buf;
                g_stagingCpuPitch  = pitch;
                LeaveCriticalSection(&g_stagingCpuLock);
                KLOG(L"EnsureStereoStage: CPU-readback buffer ready (%u bytes, pitch=%u, %s)\n",
                     (unsigned)bytes, pitch,
                     g_sharedSurface[1] ? L"double-buffered" : L"single-buffered");
            }

            // Init per-buffer locks (used to serialize game-thread writes
            // against capture-thread reads on the SAME buffer index).
            if (!s_bufLockInit) {
                InitializeCriticalSection(&s_bufLock[0]);
                InitializeCriticalSection(&s_bufLock[1]);
                s_bufLockInit = true;
            }

            // Spawn the capture thread (once per process). It loops on
            // s_captureEvent until s_captureShutdown is set.
            s_captureDevice = device;
            InterlockedExchange(&s_pendingIdx, -1);
            InterlockedExchange(&s_lastProducedIdx, 0);
            if (!s_captureEvent)
                s_captureEvent = CreateEventW(nullptr, FALSE /*auto-reset*/, FALSE, nullptr);
            if (!s_captureThread) {
                InterlockedExchange(&s_captureShutdown, 0);
                s_captureThread = (HANDLE)_beginthreadex(
                    nullptr, 0, CaptureThreadProc, nullptr, 0, nullptr);
                if (!s_captureThread)
                    KLOG(L"EnsureStereoStage: _beginthreadex(capture) failed\n");
                else
                    KLOG(L"EnsureStereoStage: capture thread spawned\n");
            }
        }
    }

    // Publish for Device B (Output_Overlay.cpp). Format is the SHARED
    // texture's format (always A8R8G8B8), not the BB format. sharedHandle
    // == nullptr signals the CPU path is active.
    g_stagingSharedHandle = sharedHandle;
    g_stagingWidth        = stagingWidth;
    g_stagingHeight       = stagingHeight;
    g_stagingD3DFormat    = (UINT)D3DFMT_A8R8G8B8;

    KLOG(L"EnsureStereoStage: published shared handle=%p (buf0=%p, buf1=%p)\n",
         g_stagingSharedHandle, g_sharedTex[0], g_sharedTex[1]);

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


// Tear down the capture thread (called from ReleaseStereoStage and
// process shutdown). Joins the thread so it can't touch surfaces we're
// about to release.
static void StopCaptureThread()
{
    if (!s_captureThread) return;
    InterlockedExchange(&s_captureShutdown, 1);
    if (s_captureEvent) SetEvent(s_captureEvent);
    WaitForSingleObject(s_captureThread, 1000);
    CloseHandle(s_captureThread);
    s_captureThread = nullptr;
    InterlockedExchange(&s_captureShutdown, 0);
}


static void ReleaseStereoStage()
{
    // Stop the capture thread first so we can release surfaces without
    // racing it.
    StopCaptureThread();
    s_captureDevice = nullptr;
    InterlockedExchange(&s_pendingIdx, -1);

    if (g_stereoStage)   { g_stereoStage->Release();   g_stereoStage   = nullptr; }
    if (g_stereoTex)     { g_stereoTex->Release();     g_stereoTex     = nullptr; }
    for (int i = 0; i < 2; ++i) {
        if (g_sharedSurface[i]) { g_sharedSurface[i]->Release(); g_sharedSurface[i] = nullptr; }
        if (g_sharedTex[i])     { g_sharedTex[i]->Release();     g_sharedTex[i]     = nullptr; }
        if (g_sysmemSurface[i]) { g_sysmemSurface[i]->Release(); g_sysmemSurface[i] = nullptr; }
    }
    if (g_stagingCpuBuffer) {
        if (g_stagingCpuLockInit) EnterCriticalSection(&g_stagingCpuLock);
        HeapFree(GetProcessHeap(), 0, g_stagingCpuBuffer);
        g_stagingCpuBuffer = nullptr;
        g_stagingCpuPitch  = 0;
        if (g_stagingCpuLockInit) LeaveCriticalSection(&g_stagingCpuLock);
    }
    InterlockedExchange(&g_stagingCpuFresh, 0);
    g_stagingSharedHandle = nullptr;
    g_stagingWidth = g_stagingHeight = 0;
    g_stagingD3DFormat = 0;
}


// Game-thread step of the cross-API handoff: StretchRect g_stereoStage
// (BB-format, reverse-stereo-blit destination) into g_sharedSurface[idx]
// (A8R8G8B8). Handles the swap_eyes flip as part of the same blit.
//
// The reverse-stereo-blit capture path hands us R-view on the left half
// and L-view on the right; the default code path here swaps the halves
// during this copy so every consumer downstream (compose shader, LeiaSR
// weaver) reads L on left and R on right. Doing the swap here means the
// fix lands uniformly across all output modes — including LeiaSR, which
// doesn't go through our compose shader. The INI knob "swap_eyes"
// CANCELS this default swap for the rare game whose capture already
// comes back in natural order.
//
// All work here is GPU command-queue ops — returns quickly. The slow
// part (GetRenderTargetData + Lock + memcpy) is handled by the capture
// thread, see CaptureThreadProc.
static void CopyStereoToSharedSlot(IDirect3DDevice9* device, int idx)
{
    if (!g_stereoStage || idx < 0 || idx >= 2 || !g_sharedSurface[idx]) return;

    if (g_config.swap_eyes) {
        device->StretchRect(g_stereoStage, nullptr, g_sharedSurface[idx], nullptr, D3DTEXF_LINEAR);
        return;
    }

    D3DSURFACE_DESC srcDesc, dstDesc;
    if (FAILED(g_stereoStage->GetDesc(&srcDesc)))            return;
    if (FAILED(g_sharedSurface[idx]->GetDesc(&dstDesc)))     return;

    LONG srcHalfW = (LONG)srcDesc.Width / 2;
    LONG srcH     = (LONG)srcDesc.Height;
    LONG dstHalfW = (LONG)dstDesc.Width / 2;
    LONG dstH     = (LONG)dstDesc.Height;

    RECT srcLeft  = { 0,        0, srcHalfW,           srcH };
    RECT srcRight = { srcHalfW, 0, (LONG)srcDesc.Width, srcH };
    RECT dstLeft  = { 0,        0, dstHalfW,           dstH };
    RECT dstRight = { dstHalfW, 0, (LONG)dstDesc.Width, dstH };

    // L source -> R destination (with swap_eyes off by default; one
    // StretchRect per half, downsampling when staging is capped).
    device->StretchRect(g_stereoStage, &srcLeft,  g_sharedSurface[idx], &dstRight, D3DTEXF_LINEAR);
    device->StretchRect(g_stereoStage, &srcRight, g_sharedSurface[idx], &dstLeft,  D3DTEXF_LINEAR);
}


// Capture thread — drains the CPU-readback path out of band so the game's
// Present thread doesn't have to wait for GetRenderTargetData (which
// implicit-syncs the GPU and stalls the caller until the readback is
// physically in SYSTEMMEM). Picks up whichever buffer the game just
// finished writing via s_pendingIdx, takes the matching s_bufLock so the
// game can't reuse that buffer mid-copy, does the GRTData + Lock + memcpy
// into the CPU staging buffer, then signals the overlay via the existing
// g_stagingCpuFresh flag.
static unsigned __stdcall CaptureThreadProc(void* /*param*/)
{
    KLOG(L"CaptureThread: started\n");
    while (!InterlockedCompareExchange(&s_captureShutdown, 0, 0)) {
        DWORD wait = WaitForSingleObject(s_captureEvent, 50);
        if (InterlockedCompareExchange(&s_captureShutdown, 0, 0)) break;
        if (wait == WAIT_TIMEOUT) continue;
        if (wait != WAIT_OBJECT_0) break;

        LONG idx = InterlockedExchange(&s_pendingIdx, -1);
        if (idx < 0 || idx >= 2) continue;

        IDirect3DDevice9* device = s_captureDevice;
        if (!device || !g_sharedSurface[idx] || !g_sysmemSurface[idx]) continue;

        EnterCriticalSection(&s_bufLock[idx]);
        // Re-verify after acquiring the lock — release path may have run
        // while we were waiting.
        if (!s_captureDevice || !g_sharedSurface[idx] || !g_sysmemSurface[idx] ||
            !g_stagingCpuBuffer) {
            LeaveCriticalSection(&s_bufLock[idx]);
            continue;
        }

        HRESULT hr = device->GetRenderTargetData(g_sharedSurface[idx], g_sysmemSurface[idx]);
        if (FAILED(hr)) {
            KLOG_V(L"CaptureThread: GetRenderTargetData(idx=%d) hr=0x%x\n", idx, hr);
            LeaveCriticalSection(&s_bufLock[idx]);
            continue;
        }

        D3DLOCKED_RECT lr;
        hr = g_sysmemSurface[idx]->LockRect(&lr, nullptr, D3DLOCK_READONLY);
        if (FAILED(hr)) {
            KLOG_V(L"CaptureThread: LockRect(idx=%d) hr=0x%x\n", idx, hr);
            LeaveCriticalSection(&s_bufLock[idx]);
            continue;
        }

        EnterCriticalSection(&g_stagingCpuLock);
        if (g_stagingCpuBuffer) {
            if ((UINT)lr.Pitch == g_stagingCpuPitch) {
                memcpy(g_stagingCpuBuffer, lr.pBits,
                       (SIZE_T)g_stagingCpuPitch * g_stagingHeight);
            } else {
                const BYTE* src = (const BYTE*)lr.pBits;
                BYTE*       dst = (BYTE*)g_stagingCpuBuffer;
                UINT copyBytes = (UINT)(lr.Pitch < (INT)g_stagingCpuPitch
                                            ? lr.Pitch : g_stagingCpuPitch);
                for (UINT y = 0; y < g_stagingHeight; ++y) {
                    memcpy(dst, src, copyBytes);
                    src += lr.Pitch;
                    dst += g_stagingCpuPitch;
                }
            }
        }
        LeaveCriticalSection(&g_stagingCpuLock);
        g_sysmemSurface[idx]->UnlockRect();
        LeaveCriticalSection(&s_bufLock[idx]);
        InterlockedExchange(&g_stagingCpuFresh, 1);
    }
    KLOG(L"CaptureThread: exiting\n");
    return 0;
}


// Game-thread producer entry. Picks the buffer the capture thread isn't
// holding (so the StretchRect doesn't wait on a slow GRTData), does the
// StretchRect (swap + format conversion), and, on the CPU-readback path,
// publishes the chosen index for the capture thread to drain.
//
// Buffer selection uses TryEnterCriticalSection on the alternate buffer
// first — if the capture thread has it, fall back to the buffer we wrote
// last time (capture thread won't be there, it only holds one buffer at
// a time). On the GPU shared-handle path, buffer[1] is unallocated, so
// we always use buffer[0] and skip the publish.
static void PublishStereoFrame(IDirect3DDevice9* device)
{
    if (!g_stereoStage) return;

    bool cpuPath  = (g_stagingSharedHandle == nullptr);
    bool dualBuf  = cpuPath && (g_sharedSurface[1] != nullptr) && s_bufLockInit;

    int idx = 0;
    if (dualBuf) {
        int prefer = (InterlockedCompareExchange(&s_lastProducedIdx, 0, 0) + 1) & 1;
        if (TryEnterCriticalSection(&s_bufLock[prefer])) {
            idx = prefer;
        } else {
            EnterCriticalSection(&s_bufLock[1 - prefer]);
            idx = 1 - prefer;
        }
    } else if (s_bufLockInit) {
        EnterCriticalSection(&s_bufLock[0]);
    }

    CopyStereoToSharedSlot(device, idx);

    if (dualBuf || s_bufLockInit) {
        if (dualBuf) LeaveCriticalSection(&s_bufLock[idx]);
        else         LeaveCriticalSection(&s_bufLock[0]);
    }

    InterlockedExchange(&s_lastProducedIdx, idx);

    if (cpuPath && s_captureEvent && s_captureThread) {
        InterlockedExchange(&s_pendingIdx, idx);
        SetEvent(s_captureEvent);
    }
}


// --------------------------------------------------------------------------
// Force the game into windowed mode at the configured render resolution.
// The overlay's compose shader linearly upscales to panel-native res — this
// is what makes interlaced / checkerboard / LeiaSR patterns align with the
// physical pixel grid even when the game wanted to render at the panel's
// full native resolution.
//
// Windowed=TRUE requires FullScreen_RefreshRateInHz=0; DX9 will reject
// non-zero refresh in windowed mode. PresentationInterval is left alone.
//
// Sticky viewport lock. When the game's FIRST CreateDevice asked for
// Windowed=TRUE, the engine has just declared its own resolution — and
// many engines cache their viewport / projection
// matrix at that moment and never re-query the BB. We record the fact in
// s_viewportLockedFromInitialWindowed and thereafter SUPPRESS the BB-dim
// override on every later CreateDevice/Reset so the engine's cached
// viewport keeps matching the BB and we don't produce the "picture in
// picture" / "top-left quadrant" failure. Games that go straight to
// fullscreen still get the override — that's the case the override
// exists for. Set in Hooked_CreateDevice.
static bool s_viewportLockedFromInitialWindowed = false;

static void ApplyPresentParamOverrides(D3DPRESENT_PARAMETERS* pp)
{
    if (!pp) return;
    if (g_config.force_windowed) {
        pp->Windowed                   = TRUE;
        pp->FullScreen_RefreshRateInHz = 0;
    }
    if (g_config.disable_vsync) {
        pp->PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    }
    if (s_viewportLockedFromInitialWindowed) return;
    if (g_config.render_width > 0 && g_config.render_height > 0) {
        pp->BackBufferWidth  = g_config.render_width;
        pp->BackBufferHeight = g_config.render_height;
    }
}


static bool ResOverrideActive()
{
    return g_config.render_width > 0 && g_config.render_height > 0;
}


// Subclass the game HWND's WndProc so we can intercept WM_SETCURSOR and
// force-hide the cursor. Required for hide_cursor support on games whose
// own class cursor is IDC_APPSTARTING / IDC_WAIT / similar and gets drawn
// by Windows whenever the cursor is hit-tested onto the game HWND —
// which it always is, because our WS_EX_LAYERED|WS_EX_TRANSPARENT overlay
// routes hit-tests through to the game window beneath. The overlay class
// cursor never gets a chance to apply.
//
// Subclassing the game's WndProc (we're in its process, so this is just
// SetWindowLongPtr) lets us swallow WM_SETCURSOR before the game's own
// handler runs, call SetCursor(NULL), and return TRUE so the OS uses our
// (invisible) choice. Original WndProc is saved for chain-through and
// restored on Reset / shutdown.
static WNDPROC s_origGameWndProc = nullptr;
static HWND    s_subclassedHwnd  = nullptr;

static LRESULT CALLBACK SubclassedGameWndProc(HWND hwnd, UINT msg,
                                              WPARAM wp, LPARAM lp)
{
    if (msg == WM_SETCURSOR && g_config.hide_cursor) {
        SetCursor(nullptr);
        return TRUE;
    }
    if (s_origGameWndProc)
        return CallWindowProcW(s_origGameWndProc, hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void MaybeSubclassGameHwnd(HWND hwnd)
{
    if (!hwnd || !g_config.hide_cursor) return;
    if (s_subclassedHwnd == hwnd) return;

    s_origGameWndProc = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC,
                                                  (LONG_PTR)SubclassedGameWndProc);
    if (!s_origGameWndProc) {
        KLOG(L"  MaybeSubclassGameHwnd: SetWindowLongPtr failed err=0x%x\n", GetLastError());
        return;
    }
    s_subclassedHwnd = hwnd;
    KLOG(L"  subclassed game HWND %p for WM_SETCURSOR interception (hide_cursor)\n", hwnd);

    // Also swap the game's class cursor to invisible. DefWindowProc's
    // default WM_SETCURSOR handler uses the WINDOW CLASS's hCursor (not
    // anything WndProc-routed) — so for games whose class cursor is
    // IDC_APPSTARTING / IDC_WAIT, our WndProc intercept alone isn't
    // enough; we need to overwrite the class entry so the OS default
    // path also picks our invisible one. SetClassLongPtrW on a window
    // we don't own is allowed because we're injected into the game's
    // process.
    BYTE andMask[32 * 4];
    BYTE xorMask[32 * 4];
    memset(andMask, 0xFF, sizeof(andMask));
    memset(xorMask, 0x00, sizeof(xorMask));
    HCURSOR hInvisible = CreateCursor(GetModuleHandleW(nullptr), 0, 0,
                                      32, 32, andMask, xorMask);
    if (hInvisible) {
        SetClassLongPtrW(hwnd, GCLP_HCURSOR, (LONG_PTR)hInvisible);
        KLOG(L"  set game class hCursor to invisible (was overriding IDC_APPSTARTING)\n");
    }
}


// When the render override is active, the game's BB is smaller than the
// monitor and most games keep their HWND at the BB size — landing the
// HWND in the top-left corner of the panel. The overlay (HTTRANSPARENT)
// covers the whole monitor, so clicks at the perceived position on the
// upscaled overlay fall through the overlay to the desktop instead of
// the tiny game HWND. Resizing the HWND to fill the monitor makes those
// clicks land in the game's client area. Well-behaved games scale mouse
// coords by client_width / world_width, so a 4K HWND with a 1080p
// internal world produces the matching divisor.
//
// Only fires when render override is on. Without override the HWND is
// already monitor-sized and re-asserting it is unnecessary.
static void MaybeResizeGameHwndToMonitor(HWND hwnd)
{
    if (!hwnd || !ResOverrideActive()) return;

    HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hMon, &mi)) return;

    int x = mi.rcMonitor.left;
    int y = mi.rcMonitor.top;
    int w = mi.rcMonitor.right  - mi.rcMonitor.left;
    int h = mi.rcMonitor.bottom - mi.rcMonitor.top;

    RECT cur = {};
    GetWindowRect(hwnd, &cur);
    if (cur.left == x && cur.top == y &&
        (cur.right - cur.left) == w && (cur.bottom - cur.top) == h)
        return;  // already covers the monitor — nothing to do

    SetWindowPos(hwnd, nullptr, x, y, w, h,
                 SWP_NOACTIVATE | SWP_NOZORDER);
    KLOG(L"  resized game HWND %p to monitor rect (%d,%d) %dx%d\n", hwnd, x, y, w, h);
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
                PublishStereoFrame(This);
            }
            else {
                // Automatic Mode: reverse-stereo-blit captures both eyes
                // into the non-shared 2W×H staging surface in one
                // StretchRect, then we copy to the shared texture for
                // Device B. Both StretchRects happen while ReverseStereo-
                // BlitControl is TRUE.
                NvAPI_Stereo_ReverseStereoBlitControl(g_nvapi, true);
                This->StretchRect(backBuffer, nullptr, g_stereoStage, nullptr, D3DTEXF_NONE);
                PublishStereoFrame(This);
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
        KLOG(L"  game asked: %dx%d format=%d windowed=%d\n",
             pPresentationParameters->BackBufferWidth,
             pPresentationParameters->BackBufferHeight,
             pPresentationParameters->BackBufferFormat,
             pPresentationParameters->Windowed);
    }

    ApplyPresentParamOverrides(pPresentationParameters);
    if (pPresentationParameters) {
        KLOG(L"  override:   %dx%d windowed=%d refresh=%d\n",
             pPresentationParameters->BackBufferWidth,
             pPresentationParameters->BackBufferHeight,
             pPresentationParameters->Windowed,
             pPresentationParameters->FullScreen_RefreshRateInHz);
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

    // Reset can shrink the HWND back to the new BB size — re-assert.
    MaybeResizeGameHwndToMonitor(g_gameFocusHwnd);

    // Re-assert subclass (defensive — Reset shouldn't swap WndProcs, but
    // if the game's UI thread did anything to its window during reset
    // we want to be sure we're still on the chain).
    MaybeSubclassGameHwnd(g_gameFocusHwnd);

    return resetHr;
}


// --------------------------------------------------------------------------
// Hooked CreateAdditionalSwapChain — separate vtable slot from Reset, so
// games that open a second swap chain (multi-monitor splash, side
// minimap, secondary render target window) bypass our Reset windowed-
// override entirely. Apply the same overrides here. The first swap chain
// (which the game keeps for its main render) is the one CreateDevice
// builds and is already covered by Hooked_CreateDevice.

static HRESULT __stdcall Hooked_CreateAdditionalSwapChain(IDirect3DDevice9* This,
    D3DPRESENT_PARAMETERS* pPresentationParameters,
    IDirect3DSwapChain9** ppSwapChain)
{
    ApplyPresentParamOverrides(pPresentationParameters);
    return pOrigCreateAdditionalSwapChain(This, pPresentationParameters, ppSwapChain);
}


// --------------------------------------------------------------------------
// DX9Ex compat hooks. Hooked_Direct3DCreate9 silently upgrades the game's
// IDirect3D9 to Ex when alternate_capture_mode=1 (the default — needed
// for the shared-handle handoff and for NvAPI on older driver builds).
// Ex rejects D3DPOOL_MANAGED, so we rewrite MANAGED → DEFAULT for every
// game-side resource creation AND add D3DUSAGE_DYNAMIC to non-RT/non-DS
// resources so the resulting DEFAULT-pool resource remains CPU-Lockable
// (games originally choosing MANAGED typically Lock during init and
// sometimes again to refresh).
//
// When alternate_capture_mode=0 the game's device stays non-Ex, MANAGED
// is legal, and the entire rewrite is a pure pass-through.

static HRESULT __stdcall Hooked_CreateTexture(IDirect3DDevice9* This,
    UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format,
    D3DPOOL Pool, IDirect3DTexture9** ppTexture, HANDLE* pSharedHandle)
{
    if (g_config.alternate_capture_mode) {
        if (Pool == D3DPOOL_MANAGED) Pool = D3DPOOL_DEFAULT;
        int renderOrStencil = Usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL);
        if (!renderOrStencil) Usage |= D3DUSAGE_DYNAMIC;
    }
    return pOrigCreateTexture(This, Width, Height, Levels, Usage, Format, Pool, ppTexture, pSharedHandle);
}

static HRESULT __stdcall Hooked_CreateCubeTexture(IDirect3DDevice9* This,
    UINT EdgeLength, UINT Levels, DWORD Usage, D3DFORMAT Format,
    D3DPOOL Pool, IDirect3DCubeTexture9** ppCubeTexture, HANDLE* pSharedHandle)
{
    if (g_config.alternate_capture_mode) {
        if (Pool == D3DPOOL_MANAGED) Pool = D3DPOOL_DEFAULT;
        int renderOrStencil = Usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL);
        if (!renderOrStencil) Usage |= D3DUSAGE_DYNAMIC;
    }
    return pOrigCreateCubeTexture(This, EdgeLength, Levels, Usage, Format, Pool, ppCubeTexture, pSharedHandle);
}

static HRESULT __stdcall Hooked_CreateVolumeTexture(IDirect3DDevice9* This,
    UINT Width, UINT Height, UINT Depth, UINT Levels, DWORD Usage,
    D3DFORMAT Format, D3DPOOL Pool, IDirect3DVolumeTexture9** ppVolumeTexture, HANDLE* pSharedHandle)
{
    if (g_config.alternate_capture_mode) {
        if (Pool == D3DPOOL_MANAGED) Pool = D3DPOOL_DEFAULT;
        int renderOrStencil = Usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL);
        if (!renderOrStencil) Usage |= D3DUSAGE_DYNAMIC;
    }
    return pOrigCreateVolumeTexture(This, Width, Height, Depth, Levels, Usage, Format, Pool, ppVolumeTexture, pSharedHandle);
}

static HRESULT __stdcall Hooked_CreateOffscreenPlainSurface(IDirect3DDevice9* This,
    UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool,
    IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle)
{
    if (g_config.alternate_capture_mode && Pool == D3DPOOL_MANAGED) Pool = D3DPOOL_DEFAULT;
    return pOrigCreateOffscreenPlainSurface(This, Width, Height, Format, Pool, ppSurface, pSharedHandle);
}

static HRESULT __stdcall Hooked_CreateVertexBuffer(IDirect3DDevice9* This,
    UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool,
    IDirect3DVertexBuffer9** ppVertexBuffer, HANDLE* pSharedHandle)
{
    if (g_config.alternate_capture_mode) {
        if (Pool == D3DPOOL_MANAGED) Pool = D3DPOOL_DEFAULT;
        Usage |= D3DUSAGE_DYNAMIC;
    }
    return pOrigCreateVertexBuffer(This, Length, Usage, FVF, Pool, ppVertexBuffer, pSharedHandle);
}

static HRESULT __stdcall Hooked_CreateIndexBuffer(IDirect3DDevice9* This,
    UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
    IDirect3DIndexBuffer9** ppIndexBuffer, HANDLE* pSharedHandle)
{
    if (g_config.alternate_capture_mode) {
        if (Pool == D3DPOOL_MANAGED) Pool = D3DPOOL_DEFAULT;
        Usage |= D3DUSAGE_DYNAMIC;
    }
    return pOrigCreateIndexBuffer(This, Length, Usage, Format, Pool, ppIndexBuffer, pSharedHandle);
}


// --------------------------------------------------------------------------
// Display-mode enumeration hooks — lie to the game about what the desktop
// resolution is and which modes are available, so games that auto-pick from
// these APIs (instead of cooperating with BackBufferWidth/Height in their
// CreateDevice request) get nudged toward our render_width/height.
//
// Active only when render_width × render_height are both non-zero. With
// 0,0 (default), each hook is a transparent pass-through.
//
// Some games read a saved-config resolution but then also probe the
// desktop via these APIs and prefer whichever is larger. Lying about
// the desktop size when render_width/height are set forces those games
// to render at the configured resolution.

static HRESULT __stdcall Hooked_GetAdapterDisplayMode(IDirect3D9* This,
                                                      UINT Adapter,
                                                      D3DDISPLAYMODE* pMode)
{
    HRESULT hr = pOrigGetAdapterDisplayMode(This, Adapter, pMode);
    if (SUCCEEDED(hr) && pMode && ResOverrideActive()) {
        KLOG_V(L"Hooked_GetAdapterDisplayMode: %ux%u -> %ux%u (adapter=%u)\n",
               pMode->Width, pMode->Height,
               g_config.render_width, g_config.render_height, Adapter);
        pMode->Width  = g_config.render_width;
        pMode->Height = g_config.render_height;
    }
    return hr;
}

static UINT __stdcall Hooked_GetAdapterModeCount(IDirect3D9* This,
                                                  UINT Adapter,
                                                  D3DFORMAT Format)
{
    UINT count = pOrigGetAdapterModeCount(This, Adapter, Format);
    if (ResOverrideActive() && count > 0) {
        KLOG_V(L"Hooked_GetAdapterModeCount: %u -> 1 (adapter=%u format=%d)\n",
               count, Adapter, (int)Format);
        return 1;
    }
    return count;
}

static HRESULT __stdcall Hooked_EnumAdapterModes(IDirect3D9* This,
                                                  UINT Adapter,
                                                  D3DFORMAT Format,
                                                  UINT Mode,
                                                  D3DDISPLAYMODE* pMode)
{
    HRESULT hr = pOrigEnumAdapterModes(This, Adapter, Format, Mode, pMode);
    if (SUCCEEDED(hr) && pMode && ResOverrideActive()) {
        KLOG_V(L"Hooked_EnumAdapterModes: idx=%u %ux%u -> %ux%u\n",
               Mode, pMode->Width, pMode->Height,
               g_config.render_width, g_config.render_height);
        pMode->Width  = g_config.render_width;
        pMode->Height = g_config.render_height;
    }
    return hr;
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
        KLOG(L"  game asked: %dx%d format=%d windowed=%d swap_effect=%d\n",
             pPresentationParameters->BackBufferWidth,
             pPresentationParameters->BackBufferHeight,
             pPresentationParameters->BackBufferFormat,
             pPresentationParameters->Windowed,
             pPresentationParameters->SwapEffect);
    }

    // Sticky viewport lock — see comment on s_viewportLockedFromInitialWindowed.
    // Captured here (not in ApplyPresentParamOverrides) so the lock is anchored
    // to the FIRST CreateDevice's game intent, regardless of how many Resets
    // happen later.
    static bool s_capturedInitial = false;
    if (!s_capturedInitial && pPresentationParameters) {
        s_capturedInitial = true;
        if (pPresentationParameters->Windowed) {
            s_viewportLockedFromInitialWindowed = true;
            KLOG(L"  initial CreateDevice was windowed — "
                 L"viewport-locked, BB dim override suppressed for this process\n");
        }
    }

    ApplyPresentParamOverrides(pPresentationParameters);
    if (pPresentationParameters) {
        KLOG(L"  override:   %dx%d windowed=%d refresh=%d\n",
             pPresentationParameters->BackBufferWidth,
             pPresentationParameters->BackBufferHeight,
             pPresentationParameters->Windowed,
             pPresentationParameters->FullScreen_RefreshRateInHz);
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

    MaybeSubclassGameHwnd(g_gameFocusHwnd);
    MaybeResizeGameHwndToMonitor(g_gameFocusHwnd);

    if (!g_config.install_device_hooks) {
        KLOG(L"  install_device_hooks=0 — skipping Present/Reset/Create* hooks\n");
    }
    if (g_config.install_device_hooks && pOrigPresent == nullptr && pDevice9) {
        SIZE_T hook_id = 0;
        DWORD dwOsErr;

        dwOsErr = g_nktInProc.Hook(&hook_id, (void**)&pOrigPresent,
                                   lpvtbl_Present_DX9(pDevice9), Hooked_Present, 0);
        if (FAILED(dwOsErr)) KLOG(L"Failed to hook Present 0x%x\n", dwOsErr);
        else                 KLOG(L"Hooked Present\n");

        dwOsErr = g_nktInProc.Hook(&hook_id, (void**)&pOrigReset,
                                   lpvtbl_Reset(pDevice9), Hooked_Reset, 0);
        if (FAILED(dwOsErr)) KLOG(L"Failed to hook Reset 0x%x\n", dwOsErr);

        dwOsErr = g_nktInProc.Hook(&hook_id, (void**)&pOrigCreateAdditionalSwapChain,
                                   lpvtbl_CreateAdditionalSwapChain(pDevice9),
                                   Hooked_CreateAdditionalSwapChain, 0);
        if (FAILED(dwOsErr)) KLOG(L"Failed to hook CreateAdditionalSwapChain 0x%x\n", dwOsErr);

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


// Install the CreateDevice + display-mode-enumeration vtable hooks on the
// returned IDirect3D9 (or Ex) object. Idempotent — guarded by
// pOrigCreateDevice so the second caller (whichever of Direct3DCreate9 /
// Direct3DCreate9Ex runs second) no-ops. Only touches vtable entries that
// exist on both interfaces.
//
// Exposed via Core.h as DX9_InstallVtableHooksOn so the proxy DLL's real
// Direct3DCreate9 / Direct3DCreate9Ex exports can install our hooks on the
// object before handing it back to the EXE.
void DX9_InstallVtableHooksOn(IDirect3D9* pDX9)
{
    if (pOrigCreateDevice != nullptr || !pDX9) return;
    if (!g_config.install_d3d9_vtable_hooks) {
        KLOG(L"DX9_InstallVtableHooksOn: install_d3d9_vtable_hooks=0 — skipping\n");
        return;
    }

    SIZE_T hook_id = 0;
    DWORD dwOsErr;

    dwOsErr = g_nktInProc.Hook(&hook_id, (void**)&pOrigCreateDevice,
                               lpvtbl_CreateDevice(pDX9),
                               Hooked_CreateDevice, 0);
    if (FAILED(dwOsErr)) KLOG(L"Failed to hook IDirect3D9::CreateDevice 0x%x\n", dwOsErr);
    else                 KLOG(L"Hooked IDirect3D9::CreateDevice\n");

    if (!g_config.install_d3d9_display_mode_hooks) {
        KLOG(L"DX9_InstallVtableHooksOn: install_d3d9_display_mode_hooks=0 "
             L"— skipping GetAdapterDisplayMode/EnumAdapterModes/GetAdapterModeCount\n");
        return;
    }

    dwOsErr = g_nktInProc.Hook(&hook_id, (void**)&pOrigGetAdapterDisplayMode,
                               lpvtbl_GetAdapterDisplayMode(pDX9),
                               Hooked_GetAdapterDisplayMode, 0);
    if (FAILED(dwOsErr)) KLOG(L"Failed to hook GetAdapterDisplayMode 0x%x\n", dwOsErr);
    else                 KLOG(L"Hooked IDirect3D9::GetAdapterDisplayMode\n");

    dwOsErr = g_nktInProc.Hook(&hook_id, (void**)&pOrigEnumAdapterModes,
                               lpvtbl_EnumAdapterModes(pDX9),
                               Hooked_EnumAdapterModes, 0);
    if (FAILED(dwOsErr)) KLOG(L"Failed to hook EnumAdapterModes 0x%x\n", dwOsErr);
    else                 KLOG(L"Hooked IDirect3D9::EnumAdapterModes\n");

    dwOsErr = g_nktInProc.Hook(&hook_id, (void**)&pOrigGetAdapterModeCount,
                               lpvtbl_GetAdapterModeCount(pDX9),
                               Hooked_GetAdapterModeCount, 0);
    if (FAILED(dwOsErr)) KLOG(L"Failed to hook GetAdapterModeCount 0x%x\n", dwOsErr);
    else                 KLOG(L"Hooked IDirect3D9::GetAdapterModeCount\n");
}


// --------------------------------------------------------------------------
// Hooked Direct3DCreate9. With alternate_capture_mode=1 (default) we
// quietly upgrade the game's IDirect3D9 to IDirect3D9Ex (castable to
// plain IDirect3D9 from the game's perspective). Two reasons:
//   1. CreateTexture with pSharedHandle != NULL — the cross-API handoff
//      to Device B — is a D3D9Ex-only feature; non-Ex devices return
//      D3DERR_INVALIDCALL when you ask for a shared resource.
//   2. Some NvAPI driver builds want an Ex device for stereo handle
//      creation.
// Side effect: Ex rejects D3DPOOL_MANAGED. The hooked Create* methods
// below rewrite MANAGED → DEFAULT (+ DYNAMIC on non-RT/non-DS) to keep
// games that think they're on plain DX9 working.
//
// With alternate_capture_mode=0, we hand back the plain non-Ex device
// untouched. The shared-handle path becomes unavailable, so
// EnsureStereoStage falls through to the CPU-readback handoff (see
// Hooks_DX9.cpp:: g_sysmemSurface / g_stagingCpuBuffer).

static IDirect3D9* __stdcall Hooked_Direct3DCreate9(UINT SDKVersion)
{
    KLOG(L"Hooked_Direct3DCreate9 SDK=%d\n", SDKVersion);

    if (!g_config.alternate_capture_mode) {
        KLOG(L"  alternate_capture_mode=0 — keeping plain DX9 device\n");
        IDirect3D9* pDX9 = pOrigDirect3DCreate9(SDKVersion);
        if (!pDX9) {
            KLOG(L"  pOrigDirect3DCreate9 returned NULL\n");
            return nullptr;
        }
        DX9_InstallVtableHooksOn(pDX9);
        return pDX9;
    }

    // Resolve Direct3DCreate9Ex from system d3d9.dll via GetProcAddress
    // rather than linking d3d9.lib for it. Linking the import conflicts
    // with our proxy DLL's exported Proxy_Direct3DCreate9Ex (which is
    // /EXPORT'd under the same name) — MSVC can't simultaneously import
    // and export the same symbol cleanly.
    typedef HRESULT (WINAPI *t_Direct3DCreate9Ex)(UINT, IDirect3D9Ex**);
    static t_Direct3DCreate9Ex pSysCreate9Ex = nullptr;
    if (!pSysCreate9Ex) {
        wchar_t path[MAX_PATH] = L"";
        GetSystemDirectoryW(path, MAX_PATH);
        wcscat_s(path, MAX_PATH, L"\\d3d9.dll");
        HMODULE h = LoadLibraryW(path);
        if (h) pSysCreate9Ex = (t_Direct3DCreate9Ex)GetProcAddress(h, "Direct3DCreate9Ex");
    }

    IDirect3D9Ex* pDX9Ex = nullptr;
    HRESULT hr = pSysCreate9Ex ? pSysCreate9Ex(SDKVersion, &pDX9Ex) : E_FAIL;
    if (FAILED(hr) || !pDX9Ex) {
        KLOG(L"  Direct3DCreate9Ex failed hr=0x%x, falling back to plain DX9\n", hr);
        return pOrigDirect3DCreate9(SDKVersion);
    }

    DX9_InstallVtableHooksOn((IDirect3D9*)pDX9Ex);
    return (IDirect3D9*)pDX9Ex;
}


// Hooked Direct3DCreate9Ex — the game already wants Ex, so just chain
// through and install our vtable hooks on the returned object. Required
// for games that go straight to Ex (modern DX9 titles) and for games whose
// EXE import table has been patched to pull D3D9 through a side DLL like
// our DSOUND proxy — that path resolves to a real Direct3DCreate9Ex call
// on either system d3d9 or a game-folder d3d9 wrapper (e.g. HelixMod),
// and we want our trampoline either way.
static HRESULT __stdcall Hooked_Direct3DCreate9Ex(UINT SDKVersion, IDirect3D9Ex** ppDX9Ex)
{
    KLOG(L"Hooked_Direct3DCreate9Ex SDK=%d\n", SDKVersion);
    HRESULT hr = pOrigDirect3DCreate9Ex(SDKVersion, ppDX9Ex);
    if (FAILED(hr) || !ppDX9Ex || !*ppDX9Ex) {
        KLOG(L"  Direct3DCreate9Ex failed hr=0x%x\n", hr);
        return hr;
    }
    DX9_InstallVtableHooksOn(*ppDX9Ex);
    return hr;
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

    FARPROC sysDirect3DCreate9   = GetProcAddress(hSystemD3D9, "Direct3DCreate9");
    FARPROC sysDirect3DCreate9Ex = GetProcAddress(hSystemD3D9, "Direct3DCreate9Ex");
    if (!sysDirect3DCreate9 || !sysDirect3DCreate9Ex) {
        KLOG(L"DX9_InstallHooks: Direct3DCreate9 (%p) or 9Ex (%p) not found\n",
             sysDirect3DCreate9, sysDirect3DCreate9Ex);
        return;
    }

    if (pOrigDirect3DCreate9 != nullptr && pOrigDirect3DCreate9Ex != nullptr)
        return;  // already hooked

    SIZE_T hook_id = 0;
    DWORD dwOsErr;

    if (pOrigDirect3DCreate9 == nullptr) {
        dwOsErr = g_nktInProc.Hook(&hook_id, (void**)&pOrigDirect3DCreate9,
                                   sysDirect3DCreate9, Hooked_Direct3DCreate9, 0);
        if (FAILED(dwOsErr)) {
            KLOG(L"DX9_InstallHooks: Hook(Direct3DCreate9) failed 0x%x\n", dwOsErr);
        } else {
            KLOG(L"DX9_InstallHooks: hooked Direct3DCreate9 @ %p\n", sysDirect3DCreate9);
        }
    }

    if (pOrigDirect3DCreate9Ex == nullptr) {
        dwOsErr = g_nktInProc.Hook(&hook_id, (void**)&pOrigDirect3DCreate9Ex,
                                   sysDirect3DCreate9Ex, Hooked_Direct3DCreate9Ex, 0);
        if (FAILED(dwOsErr)) {
            KLOG(L"DX9_InstallHooks: Hook(Direct3DCreate9Ex) failed 0x%x\n", dwOsErr);
        } else {
            KLOG(L"DX9_InstallHooks: hooked Direct3DCreate9Ex @ %p\n", sysDirect3DCreate9Ex);
        }
    }
}
