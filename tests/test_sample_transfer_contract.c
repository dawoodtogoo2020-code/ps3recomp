/* SPDX-License-Identifier: MIT
 * Pure host checks and an exporter for compiling the exact production shaders.
 */
#include "../libs/video/rsx_sample_transfer.h"
#include "../libs/video/rsx_shadow_sampling.h"
#include <stdio.h>
#include <string.h>

#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "failed: %s\n", #x); return 1; } } while (0)

int main(int argc, char** argv)
{
    if (argc == 2) {
        if (!strcmp(argv[1], "vertex")) fputs(rsx_sample_vertex_hlsl, stdout);
        else if (!strcmp(argv[1], "extract")) fputs(rsx_sample_extract_hlsl, stdout);
        else if (!strcmp(argv[1], "insert")) fputs(rsx_sample_insert_hlsl, stdout);
        else if (!strcmp(argv[1], "shadow")) {
            fputs(rsx_shadow_hlsl, stdout);
            /* Runtime parameters retain every filter/compare branch. */
            fputs("Texture2D depth : register(t0); SamplerState addressing : register(s0);\n"
                  "cbuffer Parameters : register(b0) { uint4 flags; uint operation; };\n"
                  "float4 main(float4 p:SV_Position):SV_Target0 {\n"
                  " return rsx_shadow_sample(depth,addressing,p,operation,flags.x,flags.y,flags.z,flags.w);}\n",
                  stdout);
        } else return 2;
        return 0;
    }
    uint32_t w = 99, h = 99;
    REQUIRE(rsx_sample_extent(3, 5, 2, &w, &h) && w == 6 && h == 5);
    REQUIRE(rsx_sample_extent(3, 5, 4, &w, &h) && w == 6 && h == 10);
    REQUIRE(rsx_sample_extent(UINT32_MAX / 2, UINT32_MAX, 2, &w, &h));
    REQUIRE(w == UINT32_MAX - 1 && h == UINT32_MAX);
    REQUIRE(!rsx_sample_extent(UINT32_MAX / 2 + 1, 1, 2, &w, &h));
    REQUIRE(w == 0 && h == 0);
    REQUIRE(!rsx_sample_extent(1, UINT32_MAX / 2 + 1, 4, &w, &h));
    REQUIRE(w == 0 && h == 0);
    REQUIRE(!rsx_sample_extent(0, 1, 4, &w, &h));
    REQUIRE(!rsx_sample_extent(1, 0, 4, &w, &h));
    REQUIRE(!rsx_sample_extent(1, 1, 0, &w, &h));
    REQUIRE(!rsx_sample_extent(1, 1, 1, &w, &h));
    REQUIRE(!rsx_sample_extent(1, 1, 3, &w, &h));
    REQUIRE(!rsx_sample_extent(1, 1, 8, &w, &h));
    REQUIRE(!rsx_sample_extent(1, 1, 4, NULL, &h));
    REQUIRE(!rsx_sample_extent(1, 1, 4, &w, NULL));
    puts("sample transfer: 16 extent/overflow/interface checks passed");
    return 0;
}
