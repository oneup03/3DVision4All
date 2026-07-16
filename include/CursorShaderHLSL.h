#pragma once

// --------------------------------------------------------------------------
// Shared HLSL for the stereo software cursor.
//
// Draws a folded 3D navigation arrow — the same shape used by UEVR-3D's Flat3D
// cursor. The arrow points UP-LEFT with its TIP (hotspot) at the origin and its
// body extending down-right; the two facets shade light/dark across a diagonal
// crease so it reads as a solid, foldable pointer rather than a flat blob.
//
// HOTSPOT / CLICK ALIGNMENT: the tip is placed exactly at the OS cursor
// position (c_cursor.xy). Windows reports GetCursorPos at the arrow cursor's
// hotspot — its top-left tip — which is where click events land, so drawing the
// tip there makes the visible tip the true click point. The body extends
// down-right just like the real Windows arrow.
//
// The shape itself is drawn IDENTICALLY in both eyes (no per-eye squeeze); the
// whole arrow is shifted horizontally by ±separation/2 per eye so it fuses at a
// chosen depth with a rock-steady hotspot.
//
// This string is concatenated AFTER a constant-buffer declaration that provides
// at least:
//     float4 c_cursor;    // mouseU, mouseV, separation, active
//     float4 c_cursorSz;  // sizeU, sizeV, -, -   (arrow size as a fraction of
//                         //                       one eye's width / height)
// and BEFORE the mode's float4 main(). Both the overlay compose path
// (Compose_D3D11.cpp) and the Katanga publish path (Output_Katanga.cpp) include
// this header so the arrow can never drift between them.
//
// ApplyCursor(base, eyeUV, isLeft): eyeUV is the per-eye [0,1] coordinate of the
// current output pixel; isLeft selects the eye for the horizontal disparity sign
// (>0.5 = left eye). Returns the composited RGB.

static const char kHLSL_CursorArrow[] =
    "float cursor_cross2(float2 a, float2 b) { return a.x * b.y - a.y * b.x; }\n"
    "bool cursor_in_tri(float2 p, float2 a, float2 b, float2 c) {\n"
    "    float c1 = cursor_cross2(b - a, p - a);\n"
    "    float c2 = cursor_cross2(c - b, p - b);\n"
    "    float c3 = cursor_cross2(a - c, p - c);\n"
    "    return (c1 <= 0.0 && c2 <= 0.0 && c3 <= 0.0) || (c1 >= 0.0 && c2 >= 0.0 && c3 >= 0.0);\n"
    "}\n"
    // Arrow silhouette: tip at origin, split by a diagonal crease into two
    // triangles (tip->wingL->notch and tip->notch->wingR).
    "static const float2 kNavTip   = float2(0.0,     0.0);\n"
    "static const float2 kNavNotch = float2(0.495,   0.495);\n"
    "static const float2 kNavWingL = float2(0.375,   0.969);\n"
    "static const float2 kNavWingR = float2(0.969,   0.375);\n"
    "static const float2 kNavAxis  = float2(0.70711, 0.70711);\n"   // tip -> back
    "static const float2 kNavPerp  = float2(-0.70711, 0.70711);\n"  // across the crease
    "bool cursor_nav_any(float2 p) {\n"
    "    return cursor_in_tri(p, kNavTip, kNavWingL, kNavNotch)\n"
    "        || cursor_in_tri(p, kNavTip, kNavNotch, kNavWingR);\n"
    "}\n"
    "float3 ApplyCursor(float3 base, float2 eyeUV, float isLeft)\n"
    "{\n"
    "    if (c_cursor.w < 0.5) return base;               // inactive this frame\n"
    "    float off = (isLeft > 0.5 ? -1.0 : 1.0) * c_cursor.z * 0.5;\n"
    "    float2 ctr = float2(c_cursor.x + off, c_cursor.y);\n"
    // p: position in arrow units, tip at (0,0), wings near 1.0.
    "    float2 p = (eyeUV - ctr) / max(c_cursorSz.xy, float2(1e-6, 1e-6));\n"
    "    if (p.x < -0.4 || p.x > 1.5 || p.y < -0.4 || p.y > 1.5) return base;\n"
    // One output pixel expressed in arrow units, for edge anti-aliasing.
    "    float2 fw = fwidth(p);\n"
    "    float  pxu = max(max(fw.x, fw.y), 1e-5);\n"
    // Solid silhouette + outline from the UNWARPED shape (identical in both
    // eyes -> fuses as one flat cutout). 4x rotated-grid supersample.
    "    const float2 J[4] = { float2(0.125, 0.375), float2(0.375, -0.125),\n"
    "                          float2(-0.125, -0.375), float2(-0.375, 0.125) };\n"
    "    float fill = 0.0, edge = 0.0;\n"
    "    [unroll]\n"
    "    for (int i = 0; i < 4; ++i) {\n"
    "        float2 s = p + J[i] * (pxu * 2.0);\n"
    "        if (cursor_nav_any(s))               fill += 0.25;\n"
    "        if (cursor_nav_any(s * (1.0 / 1.32))) edge += 0.25;\n"  // enlarged = fill+outline
    "    }\n"
    "    float outline = saturate(edge - fill);\n"
    // Two-facet shading + crease highlight for the folded LOOK (drawn the same
    // in both eyes -> no stereo disparity on the shape).
    "    float side  = dot(p, kNavPerp);\n"   // signed distance across the crease
    "    float axial = dot(p, kNavAxis);\n"   // 0 at tip -> ~0.95 at wings
    "    float crease = saturate(1.0 - abs(side) / 0.42)\n"
    "                 * smoothstep(0.0, 0.18, axial) * (1.0 - smoothstep(0.62, 0.95, axial));\n"
    "    float t  = saturate(abs(side) * 2.4);\n"   // 0 at crease -> 1 at wing
    "    float vv = saturate(axial / 0.95);\n"      // 0 at tip -> 1 at base
    "    float shade = ((side > 0.0) ? 0.98 : 0.80) - 0.10 * t - 0.12 * vv;\n"
    "    shade = saturate(shade + smoothstep(0.06, 0.0, abs(side)) * crease * 0.12);\n"
    "    float3 fill_col = float3(shade, shade, shade);\n"
    "    float3 dark     = float3(0.03, 0.03, 0.03);\n"
    // Composite: dark outline behind, then the facet fill on top.
    "    float3 col = lerp(base, dark, outline);\n"
    "    col = lerp(col, fill_col, fill);\n"
    "    return col;\n"
    "}\n";
