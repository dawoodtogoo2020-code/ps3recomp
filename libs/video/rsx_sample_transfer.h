/* SPDX-License-Identifier: MIT
 * Host sample-storage contract, independently implemented for PR #447.
 * References, history and hardware-evidence limits: docs/RENDERER-PROVENANCE.md.
 */
#ifndef RSX_SAMPLE_TRANSFER_H
#define RSX_SAMPLE_TRANSFER_H

#include <stdint.h>

static inline int rsx_sample_extent(uint32_t width, uint32_t height,
                                    uint32_t count, uint32_t* x, uint32_t* y)
{
    if (!x || !y) return 0;
    *x = *y = 0;
    if ((count != 2 && count != 4) || !width || !height) return 0;
    if (width > UINT32_MAX / 2 || height > UINT32_MAX / (count / 2)) return 0;
    *x = width * 2;
    *y = height * (count / 2);
    return 1;
}

/* All three shaders use the same explicit row-order slot convention. */
static const char rsx_sample_vertex_hlsl[] =
    "float4 main(uint vertex : SV_VertexID) : SV_Position {\n"
    "  if (vertex == 0) return float4(-1, -1, 0, 1);\n"
    "  if (vertex == 1) return float4( 3, -1, 0, 1);\n"
    "  return float4(-1, 3, 0, 1);\n"
    "}\n";

static const char rsx_sample_extract_hlsl[] =
    "Texture2DMS<float4> samples : register(t0);\n"
    "float4 main(float4 position : SV_Position) : SV_Target0 {\n"
    "  uint width, height, count;\n"
    "  samples.GetDimensions(width, height, count);\n"
    "  uint2 address = uint2(position.xy);\n"
    "  uint rows = count / 2;\n"
    "  uint slot = (address.x & 1u) + 2u * (address.y % rows);\n"
    "  return samples.Load(int2(address.x / 2, address.y / rows), slot);\n"
    "}\n";

static const char rsx_sample_insert_hlsl[] =
    "Texture2D<float4> storage : register(t0);\n"
    "struct Transfer { uint slot; uint rows; uint reserved0; uint reserved1; };\n"
    "[[vk::push_constant]] ConstantBuffer<Transfer> transfer;\n"
    "struct Result { float4 color : SV_Target0; uint coverage : SV_Coverage; };\n"
    "Result main(float4 position : SV_Position) {\n"
    "  uint2 pixel = uint2(position.xy);\n"
    "  int2 address = int2(2u * pixel.x + (transfer.slot & 1u),\n"
    "                      transfer.rows * pixel.y + transfer.slot / 2u);\n"
    "  Result result;\n"
    "  result.color = storage.Load(int3(address, 0));\n"
    "  result.coverage = 1u << transfer.slot;\n"
    "  return result;\n"
    "}\n";

#endif
