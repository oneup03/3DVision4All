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

// Overlay-window output path (Device B) — D3D11 + DXGI flip-model.
//
// Why D3D11 instead of DX9Ex:
// 3D Vision Discover anaglyph is applied at process scope by the DX9
// stereo driver, against any DX9 swap chain — so a DX9Ex Device B got
// painted with Discover even though it never created a stereo handle.
// DXGI flip-model swap chains are not touched by the DX9 stereo driver,
// so a D3D11 Device B presents clean pixels.
//
// Owns:
//   - a borderless WS_POPUP topmost click-through window covering the
//     monitor that contains the game's window
//   - an ID3D11Device + ID3D11DeviceContext + IDXGISwapChain1
//   - the DX9Ex staging texture re-opened on D3D11 via the cross-API
//     shared handle published by Hooks_DX9.cpp (g_stagingSharedHandle)
//   - a present thread that waits on a per-frame signal, composes via
//     Compose_D3D11_Run, and Presents the swap chain.

#include "Core.h"

#include <process.h>

#include <d3d11.h>
#include <dxgi1_2.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")


// Published by Hooks_DX9.cpp.
extern HANDLE g_stagingSharedHandle;
extern UINT   g_stagingWidth;
extern UINT   g_stagingHeight;


// --------------------------------------------------------------------------
// Per-process state. All access from the present thread; the game's render
// thread only touches s_frameEvent and the start-once gate.

static LONG                    s_startOnce     = 0;
static volatile bool           s_shutdown      = false;
static HANDLE                  s_thread        = nullptr;
static HANDLE                  s_frameEvent    = nullptr;
static HWND                    s_gameHwnd      = nullptr;
static HWND                    s_overlayHwnd   = nullptr;

static ID3D11Device*           s_deviceB       = nullptr;
static ID3D11DeviceContext*    s_contextB      = nullptr;
static IDXGISwapChain1*        s_swapChain     = nullptr;
static ID3D11RenderTargetView* s_backBufRTV    = nullptr;
static UINT                    s_bbWidth       = 0;
static UINT                    s_bbHeight      = 0;

static ID3D11Texture2D*           s_sharedTex   = nullptr;
static ID3D11ShaderResourceView*  s_sharedSRV   = nullptr;
static HANDLE                     s_openedHandle = nullptr;

// Full-SbS intermediate for the LeiaSR path. The SR weaver does NOT
// auto-upscale in our DX11 build of SDK 1.34.10 — it samples the input
// texture at panel-pixel coordinates and writes to the bound RTV.
// Feeding it the raw 2*gameW × gameH staging puts the image in the
// top-left of the panel. So we run the Sbs compose shader (linear
// sampler) into a panel-sized intermediate first, then hand THAT to
// the weaver.
//
// Dimensions: 2 × panelW × panelH — i.e. each eye in the SbS source is
// (panelW × panelH), which is the "full-SbS" convention. The SR
// wrapper's SetInputTexture passes the FULL combined width and height
// to setInputViewTexture (see third_party/SR-lib/SR.cpp:235), and the
// leiasr-integration skill recommends full-SbS — at panel-half rate
// the weaver bilinear-upscales internally; with full-rate input it
// samples per-eye-column 1:1.
static ID3D11Texture2D*           s_leiaSrcTex  = nullptr;
static ID3D11ShaderResourceView*  s_leiaSrcSRV  = nullptr;
static ID3D11RenderTargetView*    s_leiaSrcRTV  = nullptr;

static const wchar_t kOverlayClassName[] = L"Stereo3D_OverlayWindow";
static ATOM         s_classAtom          = 0;


// --------------------------------------------------------------------------
// Window-side helpers.

static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    // HTTRANSPARENT only routes hit-tests to underlying windows IN THE SAME
    // THREAD. The real cross-thread / cross-process click-through is the
    // WS_EX_LAYERED | WS_EX_TRANSPARENT combo on the window itself (set in
    // CreateOverlayWindow). HTTRANSPARENT is kept as belt-and-suspenders for
    // any message that does reach us. MA_NOACTIVATE blocks focus-steal on
    // the off chance one slips through.
    if (msg == WM_NCHITTEST)    return HTTRANSPARENT;
    if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW(hwnd, msg, wp, lp);
}


// Build an all-transparent 32x32 HCURSOR. Used as the overlay class
// cursor when confine_cursor is on so Windows draws nothing for the
// cursor over our region (the class-cursor = NULL default leaves the
// last-set cursor visible).
static HCURSOR MakeInvisibleCursor()
{
    BYTE andMask[32 * 4];   // 32 rows * 32 bits = 32 * 4 bytes
    BYTE xorMask[32 * 4];
    memset(andMask, 0xFF, sizeof(andMask));  // AND=1 + XOR=0 → transparent
    memset(xorMask, 0x00, sizeof(xorMask));
    return CreateCursor(GetModuleHandleW(nullptr), 0, 0, 32, 32, andMask, xorMask);
}


static bool EnsureWindowClass(HMODULE hSelf)
{
    if (s_classAtom) return true;

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = OverlayWndProc;
    wc.hInstance     = hSelf ? hSelf : GetModuleHandleW(nullptr);
    // Class cursor: NULL by default so the cursor the game set via
    // SetCursor stays visible. When hide_cursor is on, use a fully-
    // transparent cursor instead so the system arrow doesn't show over
    // our overlay region (games that only ShowCursor(FALSE) when they
    // believe they're FSE-active won't do it under force_windowed).
    wc.hCursor       = g_config.hide_cursor ? MakeInvisibleCursor() : nullptr;
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kOverlayClassName;

    s_classAtom = RegisterClassExW(&wc);
    if (!s_classAtom)
        KLOG(L"Output_Overlay: RegisterClassExW failed err=0x%x\n", GetLastError());
    return s_classAtom != 0;
}


static RECT GameMonitorRect(HWND gameHwnd)
{
    HMONITOR hMon = MonitorFromWindow(gameHwnd, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(hMon, &mi))
        return mi.rcMonitor;
    RECT r = { 0, 0, 1920, 1080 };
    return r;
}


static HWND CreateOverlayWindow(HMODULE hSelf, HWND gameHwnd)
{
    if (!EnsureWindowClass(hSelf)) return nullptr;

    RECT mr = GameMonitorRect(gameHwnd);
    int x = mr.left;
    int y = mr.top;
    int w = mr.right  - mr.left;
    int h = mr.bottom - mr.top;

    // WS_EX_LAYERED | WS_EX_TRANSPARENT is THE pattern for a true
    // cross-process click-through overlay. HTTRANSPARENT in the WndProc
    // only routes through same-thread windows, so without these extended
    // styles the game (different thread / different process) never sees
    // our clicks and Windows insists on drawing OUR cursor over it.
    //
    // SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA) keeps the
    // layered window fully opaque so our composed stereo output is
    // visible. WS_EX_LAYERED + DXGI FLIP_DISCARD is supported on
    // Win10 1809+ (user is on Win11) — DWM composites through the
    // redirection surface, same as it does for the game window when
    // defeat_directflip is on.
    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT,
        kOverlayClassName,
        L"3DVision4All overlay",
        WS_POPUP,
        x, y, w, h,
        nullptr, nullptr,
        hSelf ? hSelf : GetModuleHandleW(nullptr),
        nullptr);
    if (!hwnd) {
        KLOG(L"Output_Overlay: CreateWindowExW failed err=0x%x\n", GetLastError());
        return nullptr;
    }

    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);

    ShowWindow(hwnd, SW_SHOWNA);
    SetWindowPos(hwnd, HWND_TOPMOST, x, y, w, h,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);

    KLOG(L"Output_Overlay: overlay window %p at (%d,%d) %dx%d\n", hwnd, x, y, w, h);
    return hwnd;
}


// --------------------------------------------------------------------------
// D3D11 Device B + swap chain.

// Bare D3D11 device + immediate context, no window / no swap chain.
// Used in Katanga mode where the only output path is the cross-process
// shared texture handed to a VR viewer — no on-screen overlay needed.
// The device is still required (to open the cross-API staging SRV and
// run the Katanga publish shader).
static bool CreateDeviceB_Headless()
{
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL fl  = D3D_FEATURE_LEVEL_11_0;
    D3D_FEATURE_LEVEL got = D3D_FEATURE_LEVEL_9_1;
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        &fl, 1, D3D11_SDK_VERSION,
        &s_deviceB, &got, &s_contextB);
    if (FAILED(hr) || !s_deviceB) {
        KLOG(L"Output_Overlay: D3D11CreateDevice (headless) failed hr=0x%x\n", hr);
        return false;
    }
    KLOG(L"Output_Overlay: Device B headless (D3D11 fl=0x%x) for Katanga publish\n",
         (unsigned)got);
    return true;
}


static bool CreateDeviceB(HWND overlayHwnd, UINT width, UINT height)
{
    HRESULT hr;

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL fl  = D3D_FEATURE_LEVEL_11_0;
    D3D_FEATURE_LEVEL got = D3D_FEATURE_LEVEL_9_1;
    hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        &fl, 1, D3D11_SDK_VERSION,
        &s_deviceB, &got, &s_contextB);
    if (FAILED(hr) || !s_deviceB) {
        KLOG(L"Output_Overlay: D3D11CreateDevice failed hr=0x%x\n", hr);
        return false;
    }

    IDXGIDevice* dxgiDev = nullptr;
    if (FAILED(s_deviceB->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDev)) || !dxgiDev) {
        KLOG(L"Output_Overlay: QI IDXGIDevice failed\n");
        return false;
    }
    IDXGIAdapter* adapter = nullptr;
    dxgiDev->GetAdapter(&adapter);
    dxgiDev->Release();
    if (!adapter) {
        KLOG(L"Output_Overlay: GetAdapter failed\n");
        return false;
    }
    IDXGIFactory2* factory = nullptr;
    adapter->GetParent(__uuidof(IDXGIFactory2), (void**)&factory);
    adapter->Release();
    if (!factory) {
        KLOG(L"Output_Overlay: GetParent IDXGIFactory2 failed\n");
        return false;
    }

    // FLIP_DISCARD flip-model swap chain. Win10+ requirement; the user is on
    // Win11 per environment.  This is the modern path DWM composites
    // efficiently — and crucially NOT through the DX9 stereo driver.
    DXGI_SWAP_CHAIN_DESC1 scd = {};
    scd.Width            = width;
    scd.Height           = height;
    scd.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.SampleDesc.Count = 1;
    scd.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount      = 2;
    scd.Scaling          = DXGI_SCALING_STRETCH;
    scd.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.AlphaMode        = DXGI_ALPHA_MODE_IGNORE;

    hr = factory->CreateSwapChainForHwnd(s_deviceB, overlayHwnd, &scd,
                                         nullptr, nullptr, &s_swapChain);
    factory->Release();
    if (FAILED(hr) || !s_swapChain) {
        KLOG(L"Output_Overlay: CreateSwapChainForHwnd failed hr=0x%x\n", hr);
        return false;
    }

    ID3D11Texture2D* bb = nullptr;
    hr = s_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb);
    if (FAILED(hr) || !bb) {
        KLOG(L"Output_Overlay: swap GetBuffer hr=0x%x\n", hr);
        return false;
    }
    hr = s_deviceB->CreateRenderTargetView(bb, nullptr, &s_backBufRTV);
    bb->Release();
    if (FAILED(hr) || !s_backBufRTV) {
        KLOG(L"Output_Overlay: CreateRenderTargetView hr=0x%x\n", hr);
        return false;
    }

    s_bbWidth  = width;
    s_bbHeight = height;
    KLOG(L"Output_Overlay: Device B (D3D11 fl=0x%x) %ux%u hwnd=%p flip-discard\n",
         (unsigned)got, width, height, overlayHwnd);
    return true;
}


// Cached dims for the CPU-upload path so EnsureStagingOnB can detect when
// the producer's staging size changed (Reset, new game resolution) and
// recreate s_sharedTex.
static UINT s_cpuTexWidth  = 0;
static UINT s_cpuTexHeight = 0;


// Ensure s_sharedTex / s_sharedSRV are populated and current.
//
// Two paths:
//   1. SHARED-HANDLE (the Ex path). g_stagingSharedHandle is non-null;
//      DX9Ex's pSharedHandle is a legacy (KMT) shared handle, which
//      D3D11 accepts via ID3D11Device::OpenSharedResource.
//   2. CPU-READBACK (when shared-handle creation isn't available).
//      g_stagingSharedHandle is null but g_stagingCpuBuffer is set; we
//      create a DYNAMIC BGRA8 staging texture sized to match, and the
//      per-frame UpdateStagingFromCpuBufferIfFresh helper Map's it with
//      WRITE_DISCARD and memcpys from the published CPU buffer.
static bool EnsureStagingOnB()
{
    HANDLE pub = g_stagingSharedHandle;

    if (pub) {
        if (s_sharedTex && s_openedHandle == pub)
            return true;

        if (s_sharedSRV) { s_sharedSRV->Release(); s_sharedSRV = nullptr; }
        if (s_sharedTex) { s_sharedTex->Release(); s_sharedTex = nullptr; }
        s_openedHandle = nullptr;
        s_cpuTexWidth = s_cpuTexHeight = 0;

        HRESULT hr = s_deviceB->OpenSharedResource(pub, __uuidof(ID3D11Texture2D),
                                                   (void**)&s_sharedTex);
        if (FAILED(hr) || !s_sharedTex) {
            // Log once per distinct (handle, hr) — the producer republishes
            // the same handle every frame until Reset, so without dedup the
            // overlay present thread hammers the log with the same line at
            // refresh-rate.
            static HANDLE  s_lastFailHandle = nullptr;
            static HRESULT s_lastFailHr     = S_OK;
            if (pub != s_lastFailHandle || hr != s_lastFailHr) {
                KLOG(L"Output_Overlay: OpenSharedResource failed hr=0x%x handle=%p\n", hr, pub);
                s_lastFailHandle = pub;
                s_lastFailHr     = hr;
            }
            s_sharedTex = nullptr;
            return false;
        }

        D3D11_TEXTURE2D_DESC td = {};
        s_sharedTex->GetDesc(&td);

        D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
        sd.Format                    = td.Format;
        sd.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MostDetailedMip = 0;
        sd.Texture2D.MipLevels       = 1;
        hr = s_deviceB->CreateShaderResourceView(s_sharedTex, &sd, &s_sharedSRV);
        if (FAILED(hr) || !s_sharedSRV) {
            KLOG(L"Output_Overlay: CreateShaderResourceView hr=0x%x format=%d\n",
                 hr, (int)td.Format);
            s_sharedTex->Release(); s_sharedTex = nullptr;
            return false;
        }

        s_openedHandle = pub;
        KLOG(L"Output_Overlay: opened shared staging on D3D11 (%ux%u fmt=%d) handle=%p\n",
             td.Width, td.Height, (int)td.Format, pub);
        return true;
    }

    // CPU-readback path.
    if (!g_stagingCpuBuffer || !g_stagingWidth || !g_stagingHeight)
        return false;

    if (s_sharedTex && s_openedHandle == nullptr &&
        s_cpuTexWidth == g_stagingWidth && s_cpuTexHeight == g_stagingHeight)
        return true;

    if (s_sharedSRV) { s_sharedSRV->Release(); s_sharedSRV = nullptr; }
    if (s_sharedTex) { s_sharedTex->Release(); s_sharedTex = nullptr; }
    s_openedHandle = nullptr;
    s_cpuTexWidth = s_cpuTexHeight = 0;

    D3D11_TEXTURE2D_DESC td = {};
    td.Width            = g_stagingWidth;
    td.Height           = g_stagingHeight;
    td.MipLevels        = 1;
    td.ArraySize        = 1;
    td.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage            = D3D11_USAGE_DYNAMIC;
    td.BindFlags        = D3D11_BIND_SHADER_RESOURCE;
    td.CPUAccessFlags   = D3D11_CPU_ACCESS_WRITE;
    HRESULT hr = s_deviceB->CreateTexture2D(&td, nullptr, &s_sharedTex);
    if (FAILED(hr) || !s_sharedTex) {
        KLOG(L"Output_Overlay: CPU-path CreateTexture2D hr=0x%x %ux%u\n",
             hr, g_stagingWidth, g_stagingHeight);
        s_sharedTex = nullptr;
        return false;
    }
    hr = s_deviceB->CreateShaderResourceView(s_sharedTex, nullptr, &s_sharedSRV);
    if (FAILED(hr) || !s_sharedSRV) {
        KLOG(L"Output_Overlay: CPU-path CreateSRV hr=0x%x\n", hr);
        s_sharedTex->Release(); s_sharedTex = nullptr;
        return false;
    }

    s_cpuTexWidth  = g_stagingWidth;
    s_cpuTexHeight = g_stagingHeight;
    KLOG(L"Output_Overlay: CPU-upload staging texture created %ux%u (dynamic BGRA8)\n",
         g_stagingWidth, g_stagingHeight);
    return true;
}


// CPU-path only. If the producer set the "fresh" flag, Map s_sharedTex
// (DYNAMIC, WRITE_DISCARD) and memcpy in the current frame from the
// published CPU buffer. Cheap when the flag is clear (no Map call).
static void UpdateStagingFromCpuBufferIfFresh()
{
    if (g_stagingSharedHandle) return;      // shared-handle path; nothing to upload
    if (!s_sharedTex || !g_stagingCpuBuffer) return;
    if (InterlockedExchange(&g_stagingCpuFresh, 0) == 0) return;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = s_contextB->Map(s_sharedTex, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) {
        KLOG(L"Output_Overlay: CPU-path Map hr=0x%x\n", hr);
        return;
    }
    EnterCriticalSection(&g_stagingCpuLock);
    if (g_stagingCpuBuffer) {
        if (mapped.RowPitch == g_stagingCpuPitch) {
            memcpy(mapped.pData, g_stagingCpuBuffer,
                   (SIZE_T)g_stagingCpuPitch * g_stagingHeight);
        } else {
            BYTE*       dst       = (BYTE*)mapped.pData;
            const BYTE* src       = (const BYTE*)g_stagingCpuBuffer;
            UINT        copyBytes = mapped.RowPitch < g_stagingCpuPitch
                                        ? mapped.RowPitch : g_stagingCpuPitch;
            for (UINT y = 0; y < g_stagingHeight; ++y) {
                memcpy(dst, src, copyBytes);
                dst += mapped.RowPitch;
                src += g_stagingCpuPitch;
            }
        }
    }
    LeaveCriticalSection(&g_stagingCpuLock);
    s_contextB->Unmap(s_sharedTex, 0);
}


// Lazy-create the LeiaSR upscale intermediate at panel-native SbS size
// (s_bbWidth × s_bbHeight). Recreated if BB dims change.
static bool EnsureLeiaSrcTexture()
{
    if (s_leiaSrcTex && s_leiaSrcSRV && s_leiaSrcRTV)
        return true;
    if (!s_deviceB || s_bbWidth == 0 || s_bbHeight == 0)
        return false;

    D3D11_TEXTURE2D_DESC td = {};
    td.Width            = s_bbWidth * 2;   // full-SbS: 2 × panel width
    td.Height           = s_bbHeight;
    td.MipLevels        = 1;
    td.ArraySize        = 1;
    td.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;  // matches BB / staging
    td.SampleDesc.Count = 1;
    td.Usage            = D3D11_USAGE_DEFAULT;
    td.BindFlags        = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    HRESULT hr = s_deviceB->CreateTexture2D(&td, nullptr, &s_leiaSrcTex);
    if (FAILED(hr) || !s_leiaSrcTex) {
        KLOG(L"Output_Overlay: LeiaSR intermediate CreateTexture2D hr=0x%x\n", hr);
        return false;
    }
    hr = s_deviceB->CreateShaderResourceView(s_leiaSrcTex, nullptr, &s_leiaSrcSRV);
    if (FAILED(hr) || !s_leiaSrcSRV) {
        KLOG(L"Output_Overlay: LeiaSR intermediate CreateSRV hr=0x%x\n", hr);
        s_leiaSrcTex->Release(); s_leiaSrcTex = nullptr;
        return false;
    }
    hr = s_deviceB->CreateRenderTargetView(s_leiaSrcTex, nullptr, &s_leiaSrcRTV);
    if (FAILED(hr) || !s_leiaSrcRTV) {
        KLOG(L"Output_Overlay: LeiaSR intermediate CreateRTV hr=0x%x\n", hr);
        s_leiaSrcSRV->Release(); s_leiaSrcSRV = nullptr;
        s_leiaSrcTex->Release(); s_leiaSrcTex = nullptr;
        return false;
    }
    KLOG(L"Output_Overlay: LeiaSR full-SbS intermediate created %ux%u (per-eye %ux%u)\n",
         s_bbWidth * 2, s_bbHeight, s_bbWidth, s_bbHeight);
    return true;
}


static void ReleaseDeviceB()
{
    // SR weaver holds D3D11 pointers — tear it down first.
    LeiaSR_Shutdown();
    // Katanga IPC also publishes a Device-B-owned texture; release it
    // before we drop the device so the consumer sees the handle clear.
    Katanga_Shutdown();
    Compose_D3D11_Release();
    if (s_leiaSrcRTV) { s_leiaSrcRTV->Release(); s_leiaSrcRTV = nullptr; }
    if (s_leiaSrcSRV) { s_leiaSrcSRV->Release(); s_leiaSrcSRV = nullptr; }
    if (s_leiaSrcTex) { s_leiaSrcTex->Release(); s_leiaSrcTex = nullptr; }
    if (s_sharedSRV)  { s_sharedSRV->Release();  s_sharedSRV  = nullptr; }
    if (s_sharedTex)  { s_sharedTex->Release();  s_sharedTex  = nullptr; }
    if (s_backBufRTV) { s_backBufRTV->Release(); s_backBufRTV = nullptr; }
    if (s_swapChain)  { s_swapChain->Release();  s_swapChain  = nullptr; }
    if (s_contextB)   { s_contextB->Release();   s_contextB   = nullptr; }
    if (s_deviceB)    { s_deviceB->Release();    s_deviceB    = nullptr; }
    s_openedHandle = nullptr;
}


// --------------------------------------------------------------------------
// Present thread. Two flavours:
//
//   Windowed modes (Sbs / Tab / interlaced / Checkerboard / LeiaSR):
//     Owns the click-through topmost overlay window AND the device. The
//     same thread that owns the HWND must pump its messages and is the
//     one whose SetWindowPos calls don't get deferred via cross-thread
//     SendMessage. The per-frame loop composes the stereo image into the
//     swap-chain backbuffer and Presents.
//
//   Katanga mode:
//     Headless. No HWND, no swap chain, no compose pass, no Present.
//     The only output is the cross-process shared texture handed to a VR
//     viewer (Katanga.exe / VRScreenCap / Osiris) over the Katanga IPC.
//     Drawing anything to the desktop here would just be wasted GPU
//     work that the user can't see anyway (they're in a headset). All
//     the window-management code (foreground gate, cursor confine/hide,
//     HWND_TOPMOST re-assert) is skipped — none of those make sense
//     without a window, and forcing topmost would actively interfere
//     with the user's VR-viewer GUI / OBS / control panels.

// Sample the OS cursor position over the game's client area and normalize
// to UV for the stereo-cursor draw. Returns an inactive state (no cursor
// drawn) when stereo_cursor is off, the game HWND is unknown, or the cursor
// is outside the client rect — so the caller/shader skip all cursor work.
// Cheap early-out: when the feature is off this touches nothing but a config
// int, so the disabled path costs a single branch per frame.
static CursorState ComputeCursorState()
{
    CursorState cs;
    if (!g_config.stereo_cursor || !s_gameHwnd)
        return cs;

    POINT pt = {};
    if (!GetCursorPos(&pt) || !ScreenToClient(s_gameHwnd, &pt))
        return cs;

    RECT cr = {};
    if (!GetClientRect(s_gameHwnd, &cr))
        return cs;

    float w = (float)(cr.right - cr.left);
    float h = (float)(cr.bottom - cr.top);
    if (w <= 0.0f || h <= 0.0f)
        return cs;

    float u = (float)pt.x / w;
    float v = (float)pt.y / h;
    if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)
        return cs;   // cursor is outside the game window

    cs.active = true;
    cs.u      = u;
    cs.v      = v;
    return cs;
}


// The overlay window, its swap chain, the full-SbS intermediate and the
// weave viewport are ALL sized from the monitor rect / window client rect
// (GameMonitorRect -> GetClientRect below). Those APIs return DPI-*virtualized*
// (logical) pixels when the calling thread is DPI-unaware: on a 3840x2160
// panel at 150% scale they hand back 2560x1440. SbS output tolerates the DWM
// rescale, but the LeiaSR weave is subpixel-exact against the physical
// lenticular grid, so a logical-pixel output weaves to garbage. Make THIS
// thread (which owns the overlay window and every measurement below)
// per-monitor DPI aware so it sizes in true physical pixels and the backbuffer
// maps 1:1 to the panel. Per-thread => the injected game's process-wide
// awareness is untouched. Loaded dynamically: graceful no-op on < Win10 1607.
static void MakeThreadPerMonitorDpiAware()
{
    typedef HANDLE (WINAPI *PFN_SetThreadDpiAwarenessContext)(HANDLE);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return;
    auto pSet = (PFN_SetThreadDpiAwarenessContext)
        GetProcAddress(user32, "SetThreadDpiAwarenessContext");
    if (!pSet) {
        KLOG(L"Output_Overlay: SetThreadDpiAwarenessContext unavailable -- overlay may be DPI-scaled\n");
        return;
    }
    // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 == (DPI_AWARENESS_CONTEXT)-4
    HANDLE prev = pSet((HANDLE)-4);
    KLOG(L"Output_Overlay: present thread set per-monitor-v2 DPI aware (prev=%p)\n", prev);
}


static unsigned __stdcall PresentThreadProc(void* /*param*/)
{
    const bool katangaMode = (g_config.mode == StereoMode::Katanga);
    KLOG(L"Output_Overlay: present thread started (D3D11, %s)\n",
         katangaMode ? L"Katanga headless" : L"windowed overlay");

    // Must run before any monitor/window measurement so the overlay is sized
    // in physical pixels (see note above). Harmless for the katanga headless
    // path, which owns no window.
    MakeThreadPerMonitorDpiAware();

    if (katangaMode) {
        if (!CreateDeviceB_Headless()) {
            InterlockedExchange(&s_startOnce, 0);
            return 1;
        }
    } else {
        HMODULE hSelf = GetModuleHandleW(nullptr);
        s_overlayHwnd = CreateOverlayWindow(hSelf, s_gameHwnd);
        if (!s_overlayHwnd) {
            InterlockedExchange(&s_startOnce, 0);
            return 1;
        }
        RECT cr;
        GetClientRect(s_overlayHwnd, &cr);
        UINT bbW = (UINT)(cr.right - cr.left);
        UINT bbH = (UINT)(cr.bottom - cr.top);
        if (bbW == 0 || bbH == 0) { bbW = g_stagingWidth / 2; bbH = g_stagingHeight; }
        if (!CreateDeviceB(s_overlayHwnd, bbW, bbH)) {
            DestroyWindow(s_overlayHwnd); s_overlayHwnd = nullptr;
            InterlockedExchange(&s_startOnce, 0);
            return 1;
        }
    }

    while (!s_shutdown) {
        if (!katangaMode) {
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }

        DWORD wait = WaitForSingleObject(s_frameEvent, 16);
        if (s_shutdown) break;
        if (wait == WAIT_TIMEOUT) continue;
        if (wait != WAIT_OBJECT_0) break;
        ResetEvent(s_frameEvent);

        if (!s_contextB) continue;

        // Katanga headless path: cursor management still applies (the
        // user is playing on the desktop monitor + VR headset, so
        // they still want ClipCursor / hide_cursor on the game
        // window). Then open staging, publish, loop. No swap chain,
        // no compose pass, no Present, no overlay window — those
        // would just be wasted GPU work for output the user is
        // watching in VR instead.
        if (katangaMode) {
            if (g_config.confine_cursor || g_config.hide_cursor) {
                static bool s_kCursorClipped = false;
                HWND fg = GetForegroundWindow();
                DWORD fgPid = 0;
                if (fg) GetWindowThreadProcessId(fg, &fgPid);
                bool gameFg = (fgPid == GetCurrentProcessId());
                if (g_config.confine_cursor) {
                    if (gameFg && s_gameHwnd) {
                        RECT r = {};
                        if (GetWindowRect(s_gameHwnd, &r))
                            ClipCursor(&r);
                        s_kCursorClipped = true;
                    } else if (s_kCursorClipped) {
                        ClipCursor(nullptr);
                        s_kCursorClipped = false;
                    }
                }
                // Belt-and-suspenders over the Hooks_DX9 WndProc
                // subclass + class-cursor swap, for the same reason
                // as in the windowed path: some games have a busy UI
                // thread whose WM_SETCURSOR doesn't fire promptly,
                // and SetCursor from this responsive thread updates
                // the global cursor shape directly.
                if (gameFg && g_config.hide_cursor)
                    SetCursor(nullptr);
            }

            if (!EnsureStagingOnB()) continue;
            UpdateStagingFromCpuBufferIfFresh();
            Katanga_PublishFrame(s_deviceB, s_contextB, s_sharedSRV,
                                 g_stagingWidth, g_stagingHeight,
                                 ComputeCursorState());
            continue;
        }

        if (!s_swapChain) continue;

        // Hide the overlay when the game isn't the foreground process so
        // alt-tabbed-to-other-apps don't sit behind a topmost stereo
        // composite. Comparing process IDs (not HWNDs) handles game-spawned
        // dialogs, Steam overlay in same process, etc.
        //
        // Re-assert HWND_TOPMOST every frame while shown. The transition-
        // only call wasn't enough: some games call SetWindowPos /
        // BringWindowToTop on their own HWND after gaining focus, which
        // pushes our overlay below right after alt-tab. Re-asserting each
        // frame is cheap (a few µs) and ironclad — same pattern RTSS uses.
        {
            static bool s_overlayShown = true;
            static bool s_cursorClipped = false;
            HWND fg = GetForegroundWindow();
            DWORD fgPid = 0;
            if (fg) GetWindowThreadProcessId(fg, &fgPid);
            bool wantShown = (fgPid == GetCurrentProcessId());
            if (wantShown != s_overlayShown) {
                ShowWindow(s_overlayHwnd, wantShown ? SW_SHOWNA : SW_HIDE);
                s_overlayShown = wantShown;
            }
            // Cursor confinement: emulate the FSE-style ClipCursor that
            // force_windowed disables. Apply only while the game is the
            // foreground process so alt-tab still releases the cursor.
            // Re-asserted each frame because Windows itself releases the
            // clip when foreground changes, which means we need to re-grab
            // it on the way back in.
            if (g_config.confine_cursor) {
                if (wantShown && s_gameHwnd) {
                    RECT r = {};
                    if (GetWindowRect(s_gameHwnd, &r))
                        ClipCursor(&r);
                    s_cursorClipped = true;
                } else if (s_cursorClipped) {
                    ClipCursor(nullptr);
                    s_cursorClipped = false;
                }
            }
            // Per-frame SetCursor(NULL) from this (responsive) thread.
            // Belt-and-suspenders over the WndProc subclass + class-cursor
            // swap, for games whose UI thread is unresponsive enough that
            // DWM shows the busy cursor independently of WM_SETCURSOR. The
            // cursor is a global GDI resource — SetCursor from any thread
            // updates the visible shape.
            if (wantShown && g_config.hide_cursor)
                SetCursor(nullptr);
            if (!wantShown) continue;
            SetWindowPos(s_overlayHwnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }

        if (!EnsureStagingOnB()) continue;
        UpdateStagingFromCpuBufferIfFresh();

        CursorState cursor = ComputeCursorState();

        bool didWeave = false;
        if (g_config.mode == StereoMode::LeiaSR) {
            // Two-pass: upscale staging → full-SbS intermediate (2 × panel
            // width × panel height) via the Sbs passthrough shader, then
            // hand the intermediate's SRV to the weaver which writes the
            // final woven output into the BB (panel dims). Compose
            // viewport matches the intermediate (full-SbS); weave
            // viewport matches the BB (panel).
            if (EnsureLeiaSrcTexture()) {
                D3D11_VIEWPORT vpFull  = { 0.0f, 0.0f, (float)(s_bbWidth * 2), (float)s_bbHeight, 0.0f, 1.0f };
                D3D11_VIEWPORT vpPanel = { 0.0f, 0.0f, (float)s_bbWidth,       (float)s_bbHeight, 0.0f, 1.0f };

                s_contextB->OMSetRenderTargets(1, &s_leiaSrcRTV, nullptr);
                s_contextB->RSSetViewports(1, &vpFull);
                Compose_D3D11_Run(s_deviceB, s_contextB, s_sharedSRV, s_leiaSrcRTV,
                                  s_bbWidth * 2, s_bbHeight, StereoMode::Sbs, cursor);

                if (LeiaSR_TryInit(s_deviceB, s_contextB, s_overlayHwnd, s_leiaSrcSRV)) {
                    s_contextB->OMSetRenderTargets(1, &s_backBufRTV, nullptr);
                    s_contextB->RSSetViewports(1, &vpPanel);
                    LeiaSR_Weave();
                    didWeave = true;
                }
            }
        }
        if (!didWeave) {
            Compose_D3D11_Run(s_deviceB, s_contextB, s_sharedSRV, s_backBufRTV,
                              s_bbWidth, s_bbHeight, g_config.mode, cursor);
        }

        HRESULT pr = s_swapChain->Present(1, 0);
        if (FAILED(pr)) {
            KLOG(L"Output_Overlay: Present failed hr=0x%x\n", pr);
            if (pr == DXGI_ERROR_DEVICE_REMOVED || pr == DXGI_ERROR_DEVICE_RESET) {
                ReleaseDeviceB();
                if (s_overlayHwnd) { DestroyWindow(s_overlayHwnd); s_overlayHwnd = nullptr; }
                InterlockedExchange(&s_startOnce, 0);
                break;
            }
        }
    }

    KLOG(L"Output_Overlay: present thread exiting\n");
    return 0;
}


// --------------------------------------------------------------------------
// Public entry points (declared in Core.h).

void Overlay_StartOnce(HWND gameHwnd)
{
    if (InterlockedCompareExchange(&s_startOnce, 1, 0) != 0)
        return;

    if (!gameHwnd) {
        InterlockedExchange(&s_startOnce, 0);
        return;
    }

    // Need either path published (Ex shared-handle, or CPU-readback buffer)
    // and known dims before we can size Device B's swap chain.
    bool stagingReady = (g_stagingSharedHandle != nullptr) ||
                        (g_stagingCpuBuffer != nullptr);
    if (!stagingReady || g_stagingWidth == 0 || g_stagingHeight == 0) {
        InterlockedExchange(&s_startOnce, 0);
        return;
    }

    s_gameHwnd = gameHwnd;

    s_frameEvent = CreateEventW(nullptr, TRUE /*manual reset*/, FALSE, nullptr);
    if (!s_frameEvent) {
        KLOG(L"Output_Overlay: CreateEvent failed err=0x%x\n", GetLastError());
        InterlockedExchange(&s_startOnce, 0);
        return;
    }

    s_shutdown = false;
    s_thread = (HANDLE)_beginthreadex(nullptr, 0, PresentThreadProc, nullptr, 0, nullptr);
    if (!s_thread) {
        KLOG(L"Output_Overlay: _beginthreadex failed\n");
        CloseHandle(s_frameEvent); s_frameEvent = nullptr;
        InterlockedExchange(&s_startOnce, 0);
        return;
    }

    KLOG(L"Output_Overlay: kicked off present thread\n");
}


void Overlay_NotifyFrame()
{
    if (s_frameEvent) SetEvent(s_frameEvent);
}


void Overlay_ShutdownOnce()
{
    if (!s_thread && !s_overlayHwnd) return;

    // Release any cursor clip we set so the user gets their cursor back
    // if the overlay was shutting down with the game still in foreground.
    if (g_config.confine_cursor) ClipCursor(nullptr);

    s_shutdown = true;
    if (s_frameEvent) SetEvent(s_frameEvent);
    if (s_thread) {
        WaitForSingleObject(s_thread, 1000);
        CloseHandle(s_thread);
        s_thread = nullptr;
    }
    if (s_frameEvent) { CloseHandle(s_frameEvent); s_frameEvent = nullptr; }

    ReleaseDeviceB();
    if (s_overlayHwnd) { DestroyWindow(s_overlayHwnd); s_overlayHwnd = nullptr; }
}
