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

static const wchar_t kOverlayClassName[] = L"Stereo3D_OverlayWindow";
static ATOM         s_classAtom          = 0;


// --------------------------------------------------------------------------
// Window-side helpers.

static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    // WM_NCHITTEST = HTTRANSPARENT makes mouse input fall through to the
    // window beneath us (the game). WM_MOUSEACTIVATE = MA_NOACTIVATE keeps
    // clicks from stealing focus on the off chance they reach us.
    if (msg == WM_NCHITTEST)    return HTTRANSPARENT;
    if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW(hwnd, msg, wp, lp);
}


static bool EnsureWindowClass(HMODULE hSelf)
{
    if (s_classAtom) return true;

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = OverlayWndProc;
    wc.hInstance     = hSelf ? hSelf : GetModuleHandleW(nullptr);
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
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

    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_NOACTIVATE,
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

    ShowWindow(hwnd, SW_SHOWNA);
    SetWindowPos(hwnd, HWND_TOPMOST, x, y, w, h,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);

    KLOG(L"Output_Overlay: overlay window %p at (%d,%d) %dx%d\n", hwnd, x, y, w, h);
    return hwnd;
}


// --------------------------------------------------------------------------
// D3D11 Device B + swap chain.

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


// Open (or re-open) the DX9Ex shared staging texture on D3D11. DX9Ex's
// pSharedHandle returns an old-style (KMT) shared handle, which D3D11
// accepts via ID3D11Device::OpenSharedResource.
static bool EnsureStagingOnB()
{
    HANDLE pub = g_stagingSharedHandle;
    if (!pub) return false;

    if (s_sharedTex && s_openedHandle == pub)
        return true;

    if (s_sharedSRV) { s_sharedSRV->Release(); s_sharedSRV = nullptr; }
    if (s_sharedTex) { s_sharedTex->Release(); s_sharedTex = nullptr; }
    s_openedHandle = nullptr;

    HRESULT hr = s_deviceB->OpenSharedResource(pub, __uuidof(ID3D11Texture2D),
                                               (void**)&s_sharedTex);
    if (FAILED(hr) || !s_sharedTex) {
        KLOG(L"Output_Overlay: OpenSharedResource failed hr=0x%x handle=%p\n", hr, pub);
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


static void ReleaseDeviceB()
{
    // SR weaver holds D3D11 pointers — tear it down first.
    LeiaSR_Shutdown();
    Compose_D3D11_Release();
    if (s_sharedSRV)  { s_sharedSRV->Release();  s_sharedSRV  = nullptr; }
    if (s_sharedTex)  { s_sharedTex->Release();  s_sharedTex  = nullptr; }
    if (s_backBufRTV) { s_backBufRTV->Release(); s_backBufRTV = nullptr; }
    if (s_swapChain)  { s_swapChain->Release();  s_swapChain  = nullptr; }
    if (s_contextB)   { s_contextB->Release();   s_contextB   = nullptr; }
    if (s_deviceB)    { s_deviceB->Release();    s_deviceB    = nullptr; }
    s_openedHandle = nullptr;
}


// --------------------------------------------------------------------------
// Present thread. Owns the window AND the device — the same thread that
// owns the HWND must pump its messages and is the one whose SetWindowPos
// calls don't get deferred via cross-thread SendMessage.

static unsigned __stdcall PresentThreadProc(void* /*param*/)
{
    KLOG(L"Output_Overlay: present thread started (D3D11)\n");

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

    while (!s_shutdown) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        DWORD wait = WaitForSingleObject(s_frameEvent, 16);
        if (s_shutdown) break;
        if (wait == WAIT_TIMEOUT) continue;
        if (wait != WAIT_OBJECT_0) break;
        ResetEvent(s_frameEvent);

        if (!s_swapChain || !s_contextB) continue;

        // Hide the overlay when the game isn't the foreground process so
        // alt-tabbed-to-other-apps don't sit behind a topmost stereo
        // composite. Comparing process IDs (not HWNDs) handles game-spawned
        // dialogs, Steam overlay in same process, etc.
        {
            static bool s_overlayShown = true;
            HWND fg = GetForegroundWindow();
            DWORD fgPid = 0;
            if (fg) GetWindowThreadProcessId(fg, &fgPid);
            bool wantShown = (fgPid == GetCurrentProcessId());
            if (wantShown != s_overlayShown) {
                ShowWindow(s_overlayHwnd, wantShown ? SW_SHOWNA : SW_HIDE);
                if (wantShown)
                    SetWindowPos(s_overlayHwnd, HWND_TOPMOST, 0, 0, 0, 0,
                                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
                s_overlayShown = wantShown;
            }
            if (!wantShown) continue;
        }

        if (!EnsureStagingOnB()) continue;

        bool didWeave = false;
        if (g_config.mode == StereoMode::LeiaSR) {
            if (LeiaSR_TryInit(s_deviceB, s_contextB, s_overlayHwnd, s_sharedSRV)) {
                s_contextB->OMSetRenderTargets(1, &s_backBufRTV, nullptr);
                D3D11_VIEWPORT vp = { 0.0f, 0.0f, (float)s_bbWidth, (float)s_bbHeight, 0.0f, 1.0f };
                s_contextB->RSSetViewports(1, &vp);
                LeiaSR_Weave();
                didWeave = true;
            }
        }
        if (!didWeave) {
            Compose_D3D11_Run(s_deviceB, s_contextB, s_sharedSRV, s_backBufRTV,
                              s_bbWidth, s_bbHeight);
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

    if (!g_stagingSharedHandle || g_stagingWidth == 0 || g_stagingHeight == 0) {
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
