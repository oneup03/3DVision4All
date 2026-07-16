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

// D3D11 SbS / TaB / interlaced / checkerboard composite for the overlay
// (Device B) output path.
//
// Runs on the D3D11 Device B owned by Output_Overlay.cpp. Reads from a
// shared-resource SRV of the DX9Ex staging texture (2W × H, L view in
// u ∈ [0, 0.5), R view in u ∈ [0.5, 1)), runs a fullscreen-triangle
// shader keyed off StereoMode, and writes the mode-appropriate framebuffer
// into the overlay backbuffer.
//
// swap_eyes is handled upstream in Hooks_DX9.cpp::CopyStageToShared_MaybeSwap
// so the shader path is mode-agnostic on that axis.
//
// One pixel shader per StereoMode, compiled lazily via D3DCompile.

#include "Core.h"
#include "CursorShaderHLSL.h"   // kHLSL_CursorArrow — shared stereo-cursor shape

#include <string.h>   // C header only — do NOT include <string>; the C++ STL
                       // header embeds a detect_mismatch("RuntimeLibrary", ...)
                       // directive that collides with SR-md.lib's /MD build at
                       // link time (Core is /MT). Plain char buffers keep this
                       // TU free of that directive.

#include <d3d11.h>
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")


static ID3D11VertexShader*    g_vs                                = nullptr;
static ID3D11PixelShader*     g_psByMode[(int)StereoMode::LeiaSR] = {};
static ID3D11SamplerState*    g_sampler                           = nullptr;
static ID3D11Buffer*          g_cbuf                              = nullptr;
static ID3D11RasterizerState* g_rs                                = nullptr;


// Fullscreen triangle from SV_VertexID — no vertex buffer or input layout
// needed.  Draw(3, 0) with TRIANGLELIST.
static const char kHLSL_VS[] =
    "void main(in uint vid : SV_VertexID,\n"
    "          out float4 pos : SV_Position,\n"
    "          out float2 uv  : TEXCOORD0)\n"
    "{\n"
    "    float2 ndc = float2((vid << 1) & 2, vid & 2);\n"
    "    uv  = float2(ndc.x, 1.0 - ndc.y);  // (0,0) at top-left\n"
    "    pos = float4(ndc * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";


// Each PS samples `s0` (staging SRV) via sampler `ss`.
// uv ∈ [0,1] over the output backbuffer.
//
// Every mode body resolves the current output pixel to (eyeUV, isLeft):
// the normalized coordinate WITHIN one eye's image, and which staging eye
// (left half of the 2W×H staging) it displays. It then calls ApplyCursor()
// before returning. That keeps the stereo-cursor logic in one shared helper
// and mode-agnostic — each mode only has to say where its pixel lands in
// per-eye space.
//
// All PS shaders force alpha=1.0 in the output. The DX9 backbuffer's
// alpha channel comes through reverse-blit; depending on the game it may
// be undefined or zero, and a zero alpha can cause DWM to composite our
// overlay as transparent even with DXGI_ALPHA_MODE_IGNORE on some paths.


// Prologue prepended when stereo_cursor is ON. Declares the extended constant
// buffer; the real ApplyCursor (the navigation-arrow shape) is appended from
// kHLSL_CursorArrow by BuildPixelShaderSource so it stays identical to the
// Katanga publish path.
static const char kHLSL_CursorEnabled[] =
    "cbuffer Params : register(b0) {\n"
    "    float4 c_size;       // outW, outH, -, -\n"
    "    float4 c_cursor;     // mouseU, mouseV, separation, active\n"
    "    float4 c_cursorSz;   // sizeU, sizeV, -, -\n"
    "    float4 c_cursorCol;  // reserved (arrow self-shades)\n"
    "};\n";

// Prologue prepended when stereo_cursor is OFF. Keeps the original single-
// float4 cbuffer and turns ApplyCursor into an identity macro, so the eye-
// coord math in each body is dead code the compiler eliminates — the
// resulting shader is equivalent to the pre-cursor version.
static const char kHLSL_CursorDisabled[] =
    "cbuffer Sz : register(b0) { float4 c_size; }\n"
    "#define ApplyCursor(base, eyeUV, isLeft) (base)\n";


static const char kHLSL_Sbs[] =
    "Texture2D    s0 : register(t0);\n"
    "SamplerState ss : register(s0);\n"
    "float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target\n"
    "{\n"
    "    float3 base = s0.Sample(ss, uv).rgb;\n"
    "    float  isLeft = uv.x < 0.5 ? 1.0 : 0.0;\n"
    "    float2 eyeUV = float2(isLeft > 0.5 ? uv.x * 2.0 : (uv.x - 0.5) * 2.0, uv.y);\n"
    "    return float4(ApplyCursor(base, eyeUV, isLeft), 1.0);\n"
    "}\n";

static const char kHLSL_Tab[] =
    "Texture2D    s0 : register(t0);\n"
    "SamplerState ss : register(s0);\n"
    "float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target\n"
    "{\n"
    "    float2 src;\n"
    "    float  isLeft;\n"
    "    if (uv.y < 0.5) { src.x = uv.x * 0.5;       src.y = uv.y * 2.0;         isLeft = 1.0; }\n"
    "    else            { src.x = uv.x * 0.5 + 0.5; src.y = (uv.y - 0.5) * 2.0; isLeft = 0.0; }\n"
    "    float3 base = s0.Sample(ss, src).rgb;\n"
    "    float2 eyeUV = float2(uv.x, src.y);\n"
    "    return float4(ApplyCursor(base, eyeUV, isLeft), 1.0);\n"
    "}\n";

static const char kHLSL_RowInterlaced[] =
    "Texture2D    s0 : register(t0);\n"
    "SamplerState ss : register(s0);\n"
    "float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target\n"
    "{\n"
    "    float row  = floor(uv.y * c_size.y);\n"
    "    float side = fmod(row, 2.0);\n"
    "    float u_off = side * 0.5;\n"
    "    float3 base = s0.Sample(ss, float2(uv.x * 0.5 + u_off, uv.y)).rgb;\n"
    "    return float4(ApplyCursor(base, float2(uv.x, uv.y), side < 0.5 ? 1.0 : 0.0), 1.0);\n"
    "}\n";

static const char kHLSL_ColumnInterlaced[] =
    "Texture2D    s0 : register(t0);\n"
    "SamplerState ss : register(s0);\n"
    "float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target\n"
    "{\n"
    "    float col  = floor(uv.x * c_size.x);\n"
    "    float side = fmod(col, 2.0);\n"
    "    float u_off = side * 0.5;\n"
    "    float3 base = s0.Sample(ss, float2(uv.x * 0.5 + u_off, uv.y)).rgb;\n"
    "    return float4(ApplyCursor(base, float2(uv.x, uv.y), side < 0.5 ? 1.0 : 0.0), 1.0);\n"
    "}\n";

static const char kHLSL_Checkerboard[] =
    "Texture2D    s0 : register(t0);\n"
    "SamplerState ss : register(s0);\n"
    "float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target\n"
    "{\n"
    "    float col  = floor(uv.x * c_size.x);\n"
    "    float row  = floor(uv.y * c_size.y);\n"
    "    float side = fmod(col + row, 2.0);\n"
    "    float u_off = side * 0.5;\n"
    "    float3 base = s0.Sample(ss, float2(uv.x * 0.5 + u_off, uv.y)).rgb;\n"
    "    return float4(ApplyCursor(base, float2(uv.x, uv.y), side < 0.5 ? 1.0 : 0.0), 1.0);\n"
    "}\n";


static const char* HlslForMode(StereoMode m)
{
    switch (m) {
    case StereoMode::Sbs:              return kHLSL_Sbs;
    case StereoMode::Tab:              return kHLSL_Tab;
    case StereoMode::RowInterlaced:    return kHLSL_RowInterlaced;
    case StereoMode::ColumnInterlaced: return kHLSL_ColumnInterlaced;
    case StereoMode::Checkerboard:     return kHLSL_Checkerboard;
    default:                            return kHLSL_Sbs;
    }
}


// Assemble the full PS source into `out`: cursor prologue (real or identity
// stub, chosen once from the init-time config) + the mode body. Because the
// choice is fixed for the process lifetime, each mode's cached shader is
// compiled exactly once with or without cursor code — no runtime branch,
// no cost when disabled. Returns false if the buffer is too small.
static bool BuildPixelShaderSource(StereoMode m, char* out, size_t cap)
{
    const char* body = HlslForMode(m);
    size_t need = strlen(body) + 1;
    if (g_config.stereo_cursor)
        need += strlen(kHLSL_CursorEnabled) + strlen(kHLSL_CursorArrow);
    else
        need += strlen(kHLSL_CursorDisabled);
    if (need > cap) return false;

    out[0] = '\0';
    if (g_config.stereo_cursor) {
        strcat_s(out, cap, kHLSL_CursorEnabled);   // cbuffer decl
        strcat_s(out, cap, kHLSL_CursorArrow);     // real ApplyCursor (arrow)
    } else {
        strcat_s(out, cap, kHLSL_CursorDisabled);  // cbuffer + identity macro
    }
    strcat_s(out, cap, body);                      // mode body
    return true;
}


static bool CompileBlob(const char* src, const char* target, ID3DBlob** out)
{
    ID3DBlob* errs = nullptr;
    HRESULT hr = D3DCompile(src, strlen(src), "Compose", nullptr, nullptr,
                            "main", target, 0, 0, out, &errs);
    if (FAILED(hr)) {
        KLOG(L"Compose_D3D11: D3DCompile %S failed hr=0x%x errs=%S\n",
             target, hr, errs ? (const char*)errs->GetBufferPointer() : "(none)");
        if (errs) errs->Release();
        return false;
    }
    if (errs) errs->Release();
    return true;
}


static ID3D11VertexShader* EnsureVS(ID3D11Device* device)
{
    if (g_vs) return g_vs;
    ID3DBlob* blob = nullptr;
    if (!CompileBlob(kHLSL_VS, "vs_4_0", &blob)) return nullptr;
    HRESULT hr = device->CreateVertexShader(blob->GetBufferPointer(),
                                            blob->GetBufferSize(),
                                            nullptr, &g_vs);
    blob->Release();
    if (FAILED(hr)) {
        KLOG(L"Compose_D3D11: CreateVertexShader hr=0x%x\n", hr);
        g_vs = nullptr;
    }
    return g_vs;
}


static ID3D11PixelShader* EnsurePS(ID3D11Device* device, StereoMode m)
{
    int slot = (int)m;
    if (slot < 0 || slot >= (int)(sizeof(g_psByMode) / sizeof(g_psByMode[0])))
        return nullptr;
    if (g_psByMode[slot]) return g_psByMode[slot];

    ID3DBlob* blob = nullptr;
    char src[4096];
    if (!BuildPixelShaderSource(m, src, sizeof(src))) {
        KLOG(L"Compose_D3D11: shader source too large for mode=%d\n", slot);
        return nullptr;
    }
    if (!CompileBlob(src, "ps_4_0", &blob)) return nullptr;
    HRESULT hr = device->CreatePixelShader(blob->GetBufferPointer(),
                                           blob->GetBufferSize(),
                                           nullptr, &g_psByMode[slot]);
    blob->Release();
    if (FAILED(hr) || !g_psByMode[slot]) {
        KLOG(L"Compose_D3D11: CreatePixelShader mode=%d hr=0x%x\n", slot, hr);
        g_psByMode[slot] = nullptr;
        return nullptr;
    }
    KLOG(L"Compose_D3D11: pixel shader compiled for mode=%d\n", slot);
    return g_psByMode[slot];
}


static ID3D11SamplerState* EnsureSampler(ID3D11Device* device)
{
    if (g_sampler) return g_sampler;
    D3D11_SAMPLER_DESC sd = {};
    sd.Filter   = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD   = D3D11_FLOAT32_MAX;
    HRESULT hr = device->CreateSamplerState(&sd, &g_sampler);
    if (FAILED(hr)) {
        KLOG(L"Compose_D3D11: CreateSamplerState hr=0x%x\n", hr);
        g_sampler = nullptr;
    }
    return g_sampler;
}


// Default rasterizer state culls CCW (FrontCounterClockwise=FALSE, CullMode
// =CULL_BACK). Our fullscreen triangle is CCW in render-target Y-down space
// — without an explicit CULL_NONE state it gets back-face-culled and nothing
// is drawn.
static ID3D11RasterizerState* EnsureRS(ID3D11Device* device)
{
    if (g_rs) return g_rs;
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode              = D3D11_FILL_SOLID;
    rd.CullMode              = D3D11_CULL_NONE;
    rd.FrontCounterClockwise = FALSE;
    rd.DepthClipEnable       = TRUE;
    HRESULT hr = device->CreateRasterizerState(&rd, &g_rs);
    if (FAILED(hr)) {
        KLOG(L"Compose_D3D11: CreateRasterizerState hr=0x%x\n", hr);
        g_rs = nullptr;
    }
    return g_rs;
}


static ID3D11Buffer* EnsureCBuf(ID3D11Device* device)
{
    if (g_cbuf) return g_cbuf;
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth      = 64;  // c_size + c_cursor + c_cursorSz + c_cursorCol
                             // (four float4s). The cursor-disabled shader
                             // only declares the first float4; binding a
                             // larger buffer than the shader reads is legal.
    bd.Usage          = D3D11_USAGE_DYNAMIC;
    bd.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    HRESULT hr = device->CreateBuffer(&bd, nullptr, &g_cbuf);
    if (FAILED(hr)) {
        KLOG(L"Compose_D3D11: CreateBuffer hr=0x%x\n", hr);
        g_cbuf = nullptr;
    }
    return g_cbuf;
}


void Compose_D3D11_Release()
{
    if (g_rs)      { g_rs->Release();      g_rs      = nullptr; }
    if (g_cbuf)    { g_cbuf->Release();    g_cbuf    = nullptr; }
    if (g_sampler) { g_sampler->Release(); g_sampler = nullptr; }
    for (auto& ps : g_psByMode) { if (ps) { ps->Release(); ps = nullptr; } }
    if (g_vs)      { g_vs->Release();      g_vs      = nullptr; }
}


void Compose_D3D11_Run(ID3D11Device*             device,
                       ID3D11DeviceContext*      ctx,
                       ID3D11ShaderResourceView* stagingSRV,
                       ID3D11RenderTargetView*   backBufRTV,
                       UINT outW, UINT outH,
                       StereoMode mode,
                       const CursorState&        cursor)
{
    if (!device || !ctx || !stagingSRV || !backBufRTV) return;

    ID3D11VertexShader*    vs  = EnsureVS(device);
    ID3D11PixelShader*     ps  = EnsurePS(device, mode);
    ID3D11SamplerState*    smp = EnsureSampler(device);
    ID3D11Buffer*          cb  = EnsureCBuf(device);
    ID3D11RasterizerState* rs  = EnsureRS(device);
    if (!vs || !ps || !smp || !cb || !rs) return;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(ctx->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        float* p = (float*)mapped.pData;
        // c_size
        p[0] = (float)outW;
        p[1] = (float)outH;
        p[2] = 0.0f;
        p[3] = 0.0f;
        // c_cursor: mouse UV, per-eye disparity, active flag. Ignored by the
        // cursor-disabled shader (which only declares c_size), but cheap to
        // always write and required by the enabled one.
        p[4] = cursor.u;
        p[5] = cursor.v;
        p[6] = g_config.cursor_separation;
        p[7] = cursor.active ? 1.0f : 0.0f;
        // c_cursorSz: arrow size as a fraction of one eye's width/height.
        // cursor_size is the arrow height in game pixels; the per-eye image is
        // (staging_width/2 × staging_height), so dividing by those keeps the
        // arrow the same on-screen size and correct aspect regardless of mode.
        float eyeW = (g_stagingWidth  > 0) ? (float)g_stagingWidth * 0.5f : (float)outW;
        float eyeH = (g_stagingHeight > 0) ? (float)g_stagingHeight        : (float)outH;
        if (eyeW < 1.0f) eyeW = 1.0f;
        if (eyeH < 1.0f) eyeH = 1.0f;
        p[8]  = (float)g_config.cursor_size / eyeW;
        p[9]  = (float)g_config.cursor_size / eyeH;
        p[10] = 0.0f;
        p[11] = 0.0f;
        // c_cursorCol: reserved. The navigation arrow computes its own facet
        // shading and dark outline, so no fill colour is needed here.
        p[12] = 0.0f;
        p[13] = 0.0f;
        p[14] = 0.0f;
        p[15] = 0.0f;
        ctx->Unmap(cb, 0);
    }

    ID3D11RenderTargetView* rtv = backBufRTV;
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    D3D11_VIEWPORT vp = { 0.0f, 0.0f, (float)outW, (float)outH, 0.0f, 1.0f };
    ctx->RSSetViewports(1, &vp);

    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->IASetInputLayout(nullptr);
    ID3D11Buffer* nullVB = nullptr;
    UINT zero = 0;
    ctx->IASetVertexBuffers(0, 1, &nullVB, &zero, &zero);
    ctx->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);

    ctx->VSSetShader(vs, nullptr, 0);
    ctx->PSSetShader(ps, nullptr, 0);
    ctx->PSSetSamplers(0, 1, &smp);
    ctx->PSSetShaderResources(0, 1, &stagingSRV);
    ctx->PSSetConstantBuffers(0, 1, &cb);

    ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    ctx->OMSetDepthStencilState(nullptr, 0);
    ctx->RSSetState(rs);

    ctx->Draw(3, 0);

    // Unbind the SRV so the next frame's OpenSharedResource/Update path
    // isn't blocked by a stale binding.
    ID3D11ShaderResourceView* nullSRV = nullptr;
    ctx->PSSetShaderResources(0, 1, &nullSRV);
}
