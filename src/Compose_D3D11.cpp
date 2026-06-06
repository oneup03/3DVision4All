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

// All PS shaders force alpha=1.0 in the output. The DX9 backbuffer's
// alpha channel comes through reverse-blit; depending on the game it may
// be undefined or zero, and a zero alpha can cause DWM to composite our
// overlay as transparent even with DXGI_ALPHA_MODE_IGNORE on some paths.

static const char kHLSL_SbsHalf[] =
    "Texture2D    s0 : register(t0);\n"
    "SamplerState ss : register(s0);\n"
    "float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target\n"
    "{ return float4(s0.Sample(ss, uv).rgb, 1.0); }\n";

static const char kHLSL_SbsFull[] =
    "Texture2D    s0 : register(t0);\n"
    "SamplerState ss : register(s0);\n"
    "float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target\n"
    "{ return float4(s0.Sample(ss, uv).rgb, 1.0); }\n";

static const char kHLSL_Tab[] =
    "Texture2D    s0 : register(t0);\n"
    "SamplerState ss : register(s0);\n"
    "float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target\n"
    "{\n"
    "    float2 src;\n"
    "    if (uv.y < 0.5) { src.x = uv.x * 0.5;       src.y = uv.y * 2.0; }\n"
    "    else            { src.x = uv.x * 0.5 + 0.5; src.y = (uv.y - 0.5) * 2.0; }\n"
    "    return float4(s0.Sample(ss, src).rgb, 1.0);\n"
    "}\n";

static const char kHLSL_RowInterlaced[] =
    "Texture2D    s0 : register(t0);\n"
    "SamplerState ss : register(s0);\n"
    "cbuffer Sz : register(b0) { float4 c_size; }\n"
    "float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target\n"
    "{\n"
    "    float row  = floor(uv.y * c_size.y);\n"
    "    float side = fmod(row, 2.0);\n"
    "    float u_off = side * 0.5;\n"
    "    return float4(s0.Sample(ss, float2(uv.x * 0.5 + u_off, uv.y)).rgb, 1.0);\n"
    "}\n";

static const char kHLSL_ColumnInterlaced[] =
    "Texture2D    s0 : register(t0);\n"
    "SamplerState ss : register(s0);\n"
    "cbuffer Sz : register(b0) { float4 c_size; }\n"
    "float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target\n"
    "{\n"
    "    float col  = floor(uv.x * c_size.x);\n"
    "    float side = fmod(col, 2.0);\n"
    "    float u_off = side * 0.5;\n"
    "    return float4(s0.Sample(ss, float2(uv.x * 0.5 + u_off, uv.y)).rgb, 1.0);\n"
    "}\n";

static const char kHLSL_Checkerboard[] =
    "Texture2D    s0 : register(t0);\n"
    "SamplerState ss : register(s0);\n"
    "cbuffer Sz : register(b0) { float4 c_size; }\n"
    "float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target\n"
    "{\n"
    "    float col  = floor(uv.x * c_size.x);\n"
    "    float row  = floor(uv.y * c_size.y);\n"
    "    float side = fmod(col + row, 2.0);\n"
    "    float u_off = side * 0.5;\n"
    "    return float4(s0.Sample(ss, float2(uv.x * 0.5 + u_off, uv.y)).rgb, 1.0);\n"
    "}\n";


static const char* HlslForMode(StereoMode m)
{
    switch (m) {
    case StereoMode::SbsHalf:          return kHLSL_SbsHalf;
    case StereoMode::SbsFull:          return kHLSL_SbsFull;
    case StereoMode::Tab:              return kHLSL_Tab;
    case StereoMode::RowInterlaced:    return kHLSL_RowInterlaced;
    case StereoMode::ColumnInterlaced: return kHLSL_ColumnInterlaced;
    case StereoMode::Checkerboard:     return kHLSL_Checkerboard;
    default:                            return kHLSL_SbsHalf;
    }
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
    if (!CompileBlob(HlslForMode(m), "ps_4_0", &blob)) return nullptr;
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
    bd.ByteWidth      = 16;  // single float4
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
                       UINT outW, UINT outH)
{
    if (!device || !ctx || !stagingSRV || !backBufRTV) return;

    ID3D11VertexShader*    vs  = EnsureVS(device);
    ID3D11PixelShader*     ps  = EnsurePS(device, g_config.mode);
    ID3D11SamplerState*    smp = EnsureSampler(device);
    ID3D11Buffer*          cb  = EnsureCBuf(device);
    ID3D11RasterizerState* rs  = EnsureRS(device);
    if (!vs || !ps || !smp || !cb || !rs) return;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(ctx->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        float* p = (float*)mapped.pData;
        p[0] = (float)outW;
        p[1] = (float)outH;
        p[2] = 0.0f;
        p[3] = 0.0f;
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
