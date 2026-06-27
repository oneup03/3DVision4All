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
//      sized to **8 bytes**. The original Katanga DeviarePlugin sized
//      it at sizeof(UINT) = 4 (DeviarePlugin.cpp:121), and Katanga's
//      own Unity plugin reads 4 bytes (UnityNativePlugin/RenderAPI_D3D11.cpp:482).
//      But VRScreenCap reads it as a `usize` — 8 bytes on x64 —
//      via `MapViewOfFile(.., size_of::<usize>())` in
//      katanga_loader.rs:52. MapViewOfFile requires the requested
//      size to be ≤ the CreateFileMapping size (MS docs: "All bytes
//      must be within the maximum size specified by CreateFileMapping").
//      A 4-byte mapping silently breaks VRScreenCap on import.
//      Super-VRExport (Super-VRExport-Addon-main/VRExport/dllmain.cpp:412)
//      caught the same issue and uses sizeof(uint64_t). We do the
//      same: 8-byte MMF, write a uint64_t with the 32-bit handle in
//      the low bits and zeros in the high bits. Katanga.exe still
//      reads only the low 4 and is happy; VRScreenCap reads 8 and
//      gets the handle value.
//
//      Neither Katanga.exe's Unity plugin (RenderAPI_D3D11.cpp:486)
//      nor VRScreenCap's katanga_loader.rs:42 create the MMF — they
//      only OPEN it. So without the producer creating it the MMF
//      never exists and the consumer's polling silently waits
//      forever.
//   2. Consumer creates a named mutex "KatangaSetupMutex"
//      (UnityNativePlugin/RenderAPI_D3D11.cpp:396). We use CreateMutexW
//      with bInitialOwner=FALSE so launch order doesn't matter —
//      whoever runs first creates it, the other side attaches.
//   3. Producer (us) creates an ID3D11Texture2D with
//        Width  = 2 × game width   (full-SbS, eyes side-by-side)
//        Height = game height
//        Format = DXGI_FORMAT_B8G8R8A8_UNORM   ← FIXED, see below
//        MiscFlags = D3D11_RESOURCE_MISC_SHARED
//        BindFlags = SHADER_RESOURCE | RENDER_TARGET
//   4. Producer calls IDXGIResource::GetSharedHandle to obtain the
//      cross-process HANDLE, stores it as a 32-bit UINT into the MMF.
//   5. Per frame, the producer runs a fullscreen-triangle shader that
//      reads the staging SRV (in whatever format the cross-API import
//      landed on) and writes the half-swapped result into the shared
//      texture. No per-frame mutex sync — the consumer tolerates
//      mid-frame tearing via its reproject path.
//   6. On any recreate (resolution change) the producer grabs
//      KatangaSetupMutex, recreates the texture, writes the new handle
//      into the MMF, and releases the mutex.
//
// Why a shader instead of CopySubresourceRegion: VRScreenCap's
// DXGI→wgpu format allowlist (VRScreenCap/src/conversions.rs:25-89)
// rejects anything outside its hardcoded set with a hard `panic!` on
// import. Critically that list does NOT include DXGI_FORMAT_B8G8R8X8_UNORM
// — but D3DFMT_X8R8G8B8 (the common DX9 backbuffer-without-alpha
// format) lands on B8G8R8X8_UNORM when opened cross-API on D3D11. If
// we mirrored the staging format into the shared texture (the obvious
// approach), every DX9 game with an X8R8G8B8 backbuffer would crash
// VRScreenCap at import. CopyResource forbids format conversion across
// typeless families (BGRA and BGRX are different families), so we
// can't fix the format on the copy. Instead we sample the staging via
// SRV — which decodes any source format to float4 — and write to a
// fixed B8G8R8A8_UNORM RTV. As a bonus the same shader does the
// R-on-LEFT half-swap, eliminating the two separate copy calls.
//
// Eye layout: per the user-confirmed design we publish R-on-LEFT /
// L-on-RIGHT (the Katanga ecosystem convention) regardless of the
// 3DVision4All `swap_eyes` knob. That matches Katanga.exe's Unity
// scene (sbsShader.shader's `unity_StereoEyeIndex ? 0.0 : 0.5` offset)
// and VRScreenCap's `swap_eyes = true` default in config.rs:31. Our
// staging is L-on-LEFT after Hooks_DX9's capture-side fixup, so the PS
// samples src.x = frac(uv.x + 0.5) to perform the swap. A user who
// wants natural-order output sets --swap-eyes=false on vr-screen-cap.
//
// Connection lifecycle: the consumer can launch before or after the
// game. We create the MMF + mutex on the first frame after entering
// Katanga mode. If the consumer dies mid-session we keep publishing —
// relaunching the consumer alone is enough to reconnect.

#include "Core.h"

#include <stdint.h>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#pragma comment(lib, "d3dcompiler.lib")


static const wchar_t kKatangaMmfName[]   = L"Local\\KatangaMappedFile";
static const wchar_t kKatangaMutexName[] = L"KatangaSetupMutex";

// Output format published to the Katanga shared texture. Chosen to be
// in every known consumer's allowlist: Katanga.exe's Unity scene
// handles DXGI 87 (B8G8R8A8_UNORM, see LaunchAndPlay.cs:237) and
// VRScreenCap's unmap_texture_format handles B8G8R8A8_UNORM →
// wgpu::Bgra8Unorm (conversions.rs:53). BGRA8 also matches the BB
// format we'd produce in the rest of the overlay pipeline so there's
// no surprise to the user reading the log.
static const DXGI_FORMAT kKatangaSharedFormat = DXGI_FORMAT_B8G8R8A8_UNORM;


// IPC state. Created on the first publish call (named-object semantics
// attach to an already-running consumer's mutex if one exists, or
// create fresh objects if not), then kept open for the session.
static HANDLE s_kMappedFile  = nullptr;
static LPVOID s_kMappedView  = nullptr;
static HANDLE s_kSetupMutex  = nullptr;
static bool   s_kIpcReady    = false;

// Shared texture on Device B. Recreated whenever the staging dims change.
static ID3D11Texture2D*        s_kSharedTex    = nullptr;
static ID3D11RenderTargetView* s_kSharedRTV    = nullptr;
static HANDLE                  s_kSharedHandle = nullptr;
static UINT                    s_kTexWidth     = 0;
static UINT                    s_kTexHeight    = 0;

// Shader pipeline state. Created lazily on the first publish call;
// reused for the lifetime of Device B.
static ID3D11VertexShader*    s_kVS      = nullptr;
static ID3D11PixelShader*     s_kPS      = nullptr;
static ID3D11SamplerState*    s_kSampler = nullptr;
static ID3D11RasterizerState* s_kRS      = nullptr;


// Fullscreen triangle generated from SV_VertexID — no VB / IA layout
// needed, just Draw(3, 0) with TRIANGLELIST. Standard trick.
static const char kHLSL_KatangaVS[] =
    "void main(in uint vid : SV_VertexID,\n"
    "          out float4 pos : SV_Position,\n"
    "          out float2 uv  : TEXCOORD0)\n"
    "{\n"
    "    float2 ndc = float2((vid << 1) & 2, vid & 2);\n"
    "    uv  = float2(ndc.x, 1.0 - ndc.y);\n"
    "    pos = float4(ndc * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

// PS: sample the staging SRV, swapping halves on the way through. The
// `frac(uv.x + 0.5)` wraparound is the half-swap; the source is
// L-on-LEFT so output_x in [0, 0.5) should sample source_x in
// [0.5, 1) (R image), and output_x in [0.5, 1) should sample source_x
// in [0, 0.5) (L image). `frac(uv.x + 0.5)` does both in one
// expression. Force alpha=1.0 because the source may be a BGRX
// (alpha-less) format whose alpha bits are undefined.
static const char kHLSL_KatangaPS[] =
    "Texture2D    s0 : register(t0);\n"
    "SamplerState ss : register(s0);\n"
    "float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target\n"
    "{\n"
    "    float2 src = float2(frac(uv.x + 0.5), uv.y);\n"
    "    return float4(s0.Sample(ss, src).rgb, 1.0);\n"
    "}\n";


// Set up the producer side of the Katanga IPC. We own the MMF and
// initialize its handle slot to 0 so an already-polling consumer sees
// "not ready yet" instead of stale data. For the mutex, CreateMutexW
// attaches to the consumer's existing object if it ran first, or
// creates the kernel object ourselves if not.
static bool SetupKatangaIpc()
{
    // 8-byte mapping (see file-header comment for VRScreenCap vs
    // Katanga.exe size mismatch). If a previous producer with a
    // 4-byte mapping had created the named object, ours opens
    // theirs and inherits the smaller size — but Super-VRExport's
    // approach of recreating-on-each-publish would just bounce the
    // name. Simpler: we own the size, MapViewOfFile-of-8 succeeds
    // on a fresh 8-byte mapping, and the worst case (4-byte
    // pre-existing object) only loses VRScreenCap compatibility,
    // which was the pre-existing producer's bug, not ours.
    HANDLE mmf = CreateFileMappingW(
        INVALID_HANDLE_VALUE,   // backed by paging file
        nullptr,
        PAGE_READWRITE,
        0, sizeof(uint64_t),    // 8-byte object — see file-header note
        kKatangaMmfName);
    if (!mmf) {
        KLOG(L"Katanga: CreateFileMappingW failed err=0x%x\n", GetLastError());
        return false;
    }
    bool mmfPreExisted = (GetLastError() == ERROR_ALREADY_EXISTS);

    LPVOID view = MapViewOfFile(mmf, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(uint64_t));
    if (!view) {
        KLOG(L"Katanga: MapViewOfFile failed err=0x%x\n", GetLastError());
        CloseHandle(mmf);
        return false;
    }
    *(volatile uint64_t*)view = 0;

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


// Lazy-compile the VS / PS / sampler / rasterizer state. Returns false
// only on shader-compile failure, which shouldn't happen in practice.
static bool EnsurePipeline(ID3D11Device* device)
{
    if (s_kVS && s_kPS && s_kSampler && s_kRS) return true;

    HRESULT hr;

    if (!s_kVS) {
        ID3DBlob* blob = nullptr;
        ID3DBlob* errs = nullptr;
        hr = D3DCompile(kHLSL_KatangaVS, sizeof(kHLSL_KatangaVS) - 1,
                        "KatangaVS", nullptr, nullptr,
                        "main", "vs_4_0", 0, 0, &blob, &errs);
        if (FAILED(hr)) {
            KLOG(L"Katanga: VS compile failed hr=0x%x errs=%S\n",
                 hr, errs ? (const char*)errs->GetBufferPointer() : "(none)");
            if (errs) errs->Release();
            return false;
        }
        if (errs) errs->Release();
        hr = device->CreateVertexShader(blob->GetBufferPointer(),
                                        blob->GetBufferSize(),
                                        nullptr, &s_kVS);
        blob->Release();
        if (FAILED(hr) || !s_kVS) {
            KLOG(L"Katanga: CreateVertexShader hr=0x%x\n", hr);
            s_kVS = nullptr;
            return false;
        }
    }

    if (!s_kPS) {
        ID3DBlob* blob = nullptr;
        ID3DBlob* errs = nullptr;
        hr = D3DCompile(kHLSL_KatangaPS, sizeof(kHLSL_KatangaPS) - 1,
                        "KatangaPS", nullptr, nullptr,
                        "main", "ps_4_0", 0, 0, &blob, &errs);
        if (FAILED(hr)) {
            KLOG(L"Katanga: PS compile failed hr=0x%x errs=%S\n",
                 hr, errs ? (const char*)errs->GetBufferPointer() : "(none)");
            if (errs) errs->Release();
            return false;
        }
        if (errs) errs->Release();
        hr = device->CreatePixelShader(blob->GetBufferPointer(),
                                       blob->GetBufferSize(),
                                       nullptr, &s_kPS);
        blob->Release();
        if (FAILED(hr) || !s_kPS) {
            KLOG(L"Katanga: CreatePixelShader hr=0x%x\n", hr);
            s_kPS = nullptr;
            return false;
        }
    }

    if (!s_kSampler) {
        D3D11_SAMPLER_DESC sd = {};
        sd.Filter   = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD   = D3D11_FLOAT32_MAX;
        hr = device->CreateSamplerState(&sd, &s_kSampler);
        if (FAILED(hr) || !s_kSampler) {
            KLOG(L"Katanga: CreateSamplerState hr=0x%x\n", hr);
            s_kSampler = nullptr;
            return false;
        }
    }

    if (!s_kRS) {
        // CULL_NONE — the fullscreen triangle is CCW in render-target
        // Y-down space, the default CULL_BACK would discard it.
        D3D11_RASTERIZER_DESC rd = {};
        rd.FillMode              = D3D11_FILL_SOLID;
        rd.CullMode              = D3D11_CULL_NONE;
        rd.FrontCounterClockwise = FALSE;
        rd.DepthClipEnable       = TRUE;
        hr = device->CreateRasterizerState(&rd, &s_kRS);
        if (FAILED(hr) || !s_kRS) {
            KLOG(L"Katanga: CreateRasterizerState hr=0x%x\n", hr);
            s_kRS = nullptr;
            return false;
        }
    }

    return true;
}


// Tear down the shared texture, RTV, and the published handle so a
// reconnecting consumer doesn't read a stale value.
static void ReleaseSharedTexture()
{
    if (s_kSharedRTV) { s_kSharedRTV->Release(); s_kSharedRTV = nullptr; }
    if (s_kSharedTex) { s_kSharedTex->Release(); s_kSharedTex = nullptr; }
    s_kSharedHandle = nullptr;
    s_kTexWidth = s_kTexHeight = 0;
    if (s_kMappedView) *(volatile uint64_t*)s_kMappedView = 0;
}


// (Re)create the shared texture at fixed B8G8R8A8_UNORM and publish
// its handle. Called on first publish and whenever the staging dims
// change. Runs under KatangaSetupMutex so the consumer can't sample
// mid-recreate.
static bool RecreateSharedTexture(ID3D11Device* device,
                                  UINT          width,
                                  UINT          height)
{
    DWORD waitRes = WaitForSingleObject(s_kSetupMutex, 1000);
    if (waitRes != WAIT_OBJECT_0) {
        KLOG(L"Katanga: WaitForSingleObject(setup mutex) 0x%x -- consumer may be gone\n",
             waitRes);
        return false;
    }

    ReleaseSharedTexture();

    D3D11_TEXTURE2D_DESC td = {};
    td.Width            = width;
    td.Height           = height;
    td.MipLevels        = 1;
    td.ArraySize        = 1;
    td.Format           = kKatangaSharedFormat;
    td.SampleDesc.Count = 1;
    td.Usage            = D3D11_USAGE_DEFAULT;
    td.BindFlags        = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    td.MiscFlags        = D3D11_RESOURCE_MISC_SHARED;

    HRESULT hr = device->CreateTexture2D(&td, nullptr, &s_kSharedTex);
    if (FAILED(hr) || !s_kSharedTex) {
        KLOG(L"Katanga: CreateTexture2D failed hr=0x%x %ux%u fmt=%d\n",
             hr, width, height, (int)kKatangaSharedFormat);
        s_kSharedTex = nullptr;
        ReleaseMutex(s_kSetupMutex);
        return false;
    }

    hr = device->CreateRenderTargetView(s_kSharedTex, nullptr, &s_kSharedRTV);
    if (FAILED(hr) || !s_kSharedRTV) {
        KLOG(L"Katanga: CreateRenderTargetView hr=0x%x\n", hr);
        ReleaseSharedTexture();
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

    // Stamp the shared handle into the 8-byte MMF slot. The KMT
    // handle namespace is 32-bit on every platform (Katanga's
    // InProc_DX11.cpp:213-217 comment), so the high 32 bits stay
    // zero. A 64-bit-reading consumer (VRScreenCap, usize) gets
    // exactly the 32-bit handle value; a 32-bit-reading consumer
    // (Katanga.exe, UINT) gets the low 4 bytes which IS the handle.
    *(volatile uint64_t*)s_kMappedView =
        (uint64_t)(uintptr_t)s_kSharedHandle;

    s_kTexWidth  = width;
    s_kTexHeight = height;
    KLOG(L"Katanga: shared texture published %ux%u fmt=BGRA8 handle=%p (32b=0x%x)\n",
         width, height, s_kSharedHandle, PtrToUint(s_kSharedHandle));

    ReleaseMutex(s_kSetupMutex);
    return true;
}


// --------------------------------------------------------------------------
// Public entry points.

void Katanga_PublishFrame(ID3D11Device*              device,
                          ID3D11DeviceContext*       ctx,
                          ID3D11ShaderResourceView*  stagingSRV,
                          UINT                       stagingWidth,
                          UINT                       stagingHeight)
{
    if (!device || !ctx || !stagingSRV || stagingWidth == 0 || stagingHeight == 0)
        return;

    if (!s_kIpcReady && !SetupKatangaIpc())
        return;
    if (!EnsurePipeline(device))
        return;
    if (!s_kSharedTex || s_kTexWidth != stagingWidth || s_kTexHeight != stagingHeight) {
        if (!RecreateSharedTexture(device, stagingWidth, stagingHeight))
            return;
    }

    // Single fullscreen-triangle pass: staging SRV → shared RTV with
    // half-swap and fixed BGRA8 output. We save no state — the
    // overlay's present loop will re-bind whatever it needs next
    // frame for its compose pass and Present.
    D3D11_VIEWPORT vp = { 0.0f, 0.0f, (float)stagingWidth, (float)stagingHeight, 0.0f, 1.0f };
    ctx->OMSetRenderTargets(1, &s_kSharedRTV, nullptr);
    ctx->RSSetViewports(1, &vp);
    ctx->RSSetState(s_kRS);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->IASetInputLayout(nullptr);
    ctx->VSSetShader(s_kVS, nullptr, 0);
    ctx->PSSetShader(s_kPS, nullptr, 0);
    ctx->PSSetShaderResources(0, 1, &stagingSRV);
    ctx->PSSetSamplers(0, 1, &s_kSampler);
    ctx->Draw(3, 0);

    // Unbind the staging SRV so the next Compose pass on Device B can
    // bind the same texture as something else (the overlay's compose
    // also samples staging — same SRV in the typical case, but worth
    // being tidy).
    ID3D11ShaderResourceView* nullSRV = nullptr;
    ctx->PSSetShaderResources(0, 1, &nullSRV);

    // Flush the immediate context. In the windowed output modes the
    // overlay's swap-chain Present implicitly flushes after the
    // compose pass, so the draw above lands on the GPU within the
    // frame. In Katanga headless mode there is no Present — without
    // this explicit flush the publish draws sit in the command queue
    // until the driver decides to flush on its own (cmd buffer full,
    // GPU idle timeout), which manifests to VR consumers as
    // "occasional single frame, then nothing" because they read
    // whatever the cross-process shared texture happens to contain
    // each poll and that only updates when the driver flushes.
    ctx->Flush();
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

    if (s_kRS)      { s_kRS->Release();      s_kRS      = nullptr; }
    if (s_kSampler) { s_kSampler->Release(); s_kSampler = nullptr; }
    if (s_kPS)      { s_kPS->Release();      s_kPS      = nullptr; }
    if (s_kVS)      { s_kVS->Release();      s_kVS      = nullptr; }

    if (s_kSetupMutex) { CloseHandle(s_kSetupMutex);     s_kSetupMutex = nullptr; }
    if (s_kMappedView) { UnmapViewOfFile(s_kMappedView); s_kMappedView = nullptr; }
    if (s_kMappedFile) { CloseHandle(s_kMappedFile);     s_kMappedFile = nullptr; }
    s_kIpcReady = false;
}
