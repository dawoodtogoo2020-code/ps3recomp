/* SPDX-License-Identifier: MIT
 * Software PCF for the renderer's single-level, 2D float depth snapshots.
 * Fresh implementation from comparison operators and separable tent weights.
 * See docs/RENDERER-PROVENANCE.md for primary specifications and limitations.
 * This helper does not determine whether an RSX texture enables comparison.
 * Call from uniform flow: derivatives precede the filter-dependent loop.
 * Integer-centred fetches preserve the supplied sampler's address modes.
 */
#ifndef RSX_SHADOW_SAMPLING_H
#define RSX_SHADOW_SAMPLING_H

/* Existing reference-first host ABI, not NV40's named RCOMP enum. */
#define RSX_SHADOW_NEVER    0u
#define RSX_SHADOW_LESS     1u
#define RSX_SHADOW_EQUAL    2u
#define RSX_SHADOW_LEQUAL   3u
#define RSX_SHADOW_GREATER  4u
#define RSX_SHADOW_NOTEQUAL 5u
#define RSX_SHADOW_GEQUAL   6u
#define RSX_SHADOW_ALWAYS   7u

static const char rsx_shadow_hlsl[] =
"float rsx_shadow_compare(float reference, float stored, uint operation)\n"
"{\n"
"    switch (operation & 7u) {\n"
"    case 0u: return 0.0;\n"
"    case 1u: return float(reference < stored);\n"
"    case 2u: return float(reference == stored);\n"
"    case 3u: return float(reference <= stored);\n"
"    case 4u: return float(reference > stored);\n"
"    case 5u: return float(reference != stored);\n"
"    case 6u: return float(reference >= stored);\n"
"    default: return 1.0;\n"
"    }\n"
"}\n"
"float4 rsx_shadow_sample(Texture2D image, SamplerState addressing, float4 coordinate,\n"
"    uint operation, uint project, uint texel_coordinates, uint min_linear, uint mag_linear)\n"
"{\n"
"    uint width, height;\n"
"    image.GetDimensions(width, height);\n"
"    float2 dimensions = float2(width, height);\n"
"    float3 location = coordinate.xyz / (project != 0u ? coordinate.w : 1.0);\n"
"    float2 position = location.xy * (texel_coordinates != 0u ? float2(1, 1) : dimensions);\n"
"    float2 dx = ddx(position), dy = ddy(position);\n"
"    bool minifying = max(dot(dx, dx), dot(dy, dy)) > 1.0;\n"
"    bool use_linear = (minifying ? min_linear : mag_linear) != 0u;\n"
"    float2 origin = floor(position - (use_linear ? 0.5 : 0.0));\n"
"    float2 fraction = position - 0.5 - origin;\n"
"    float result = 0.0;\n"
"    uint taps = use_linear ? 4u : 1u;\n"
"    for (uint tap = 0u; tap < taps; ++tap) {\n"
"        float2 offset = float2(tap & 1u, tap >> 1u);\n"
"        float2 uv = (origin + offset + 0.5) / dimensions;\n"
"        float stored = image.SampleLevel(addressing, uv, 0.0).r;\n"
"        float2 weight = lerp(1.0 - fraction, fraction, offset);\n"
"        float contribution = use_linear ? weight.x * weight.y : 1.0;\n"
"        result += contribution * rsx_shadow_compare(location.z, stored, operation);\n"
"    }\n"
"    return result.xxxx;\n"
"}\n"
;

#endif
