# Renderer replacement for PR #447

## Scope and history

Starting point: `2380f1f008bedf29c3e6e59b0eec3cc547e36abc` on
`claude/ship-audio-r18-20261021`. The 26 October 2026 review flagged
`rsx_vulkan_msaa.inc`, `rsx_vulkan_msaa_upload.inc` and
`rsx_shadow_sampling.h`. This topic replaces the two MSAA modules and the
shadow helper, rather than removing licence notices from their implementations.
The old files and their notices remain in Git history. No RPCS3 source was
imported as a replacement reference. The flagged files were inspected to
identify interfaces, dependencies and the provenance problem: this is **not a
claim of a legally supervised clean-room process**.

FPSCR, third-party manifests, dependency/build splits, audio and unrelated
renderer features are outside this topic. Earlier local PC patches are not
present in this checkout and are neither recreated nor superseded here.

## Replacement design contract

1. The host represents each logical pixel with two or four independent
   samples. Its existing expanded-image interface uses two columns and
   `sample_count / 2` rows per logical pixel. Define the slot number in row
   order: `slot = column + 2 * row`. This is a host storage convention, **not
   proof of RSX physical sample order**. Use the same explicit convention for
   upload and extraction; never average or replicate samples.
2. Extract a sample with an integer multisample-image fetch. Depth extraction
   reads only the depth aspect and writes an R32 float colour image. Stencil
   is untouched. Colour extraction preserves RGBA8 and RGBA16F.
3. Upload with one draw per slot. The fragment shader writes a one-bit
   `SampleMask` (`SV_Coverage`) and the corresponding texel. Unlike the old
   code, all slots share one pipeline per format/sample count; neither
   sample-rate shading nor a pipeline per slot is required.
4. Expansion descriptors/framebuffers stay with their target and remain
   immutable while commands are pending. Upload is synchronous and waits
   before updating its reusable descriptor. Resource creation failures must
   leave a retryable state; invalid extents/formats are rejected before GPU
   mutation. Check integer overflow before calculating expanded dimensions.
5. The shadow helper implements the existing 2D, single-level R32-float
   interface. Project xyz before sampling; convert xy to texel units; choose
   min/mag from derivatives before divergent control flow. Compare each
   texel independently and accumulate bilinear weights. The sampled float
   reference is not clamped. Return the scalar in all four channels. This
   float-texture host policy is not a claim to implement every OpenGL depth
   format or every NV40 precision rule.

## Primary references and their limits

No specification text or example program is vendored in the implementation.
These references describe operations; the new implementation is contributed
under this repository's MIT licence. The [Khronos specification notice](https://registry.khronos.org/speccopyright.html)
permits using specifications to implement their functionality; it is not an
MIT licence to redistribute the specifications themselves.

| Reference | Used for | Licence / limitation |
| --- | --- | --- |
| [Vulkan specification, Fragment Operations](https://docs.vulkan.org/spec/latest/chapters/fragops.html#fragops-samplemask) | Output SampleMask intersects coverage; per-slot writes | Khronos specification terms; implementation reference, not copied source |
| [Vulkan specification, Render Pass](https://docs.vulkan.org/spec/latest/chapters/renderpass.html) | Attachment load/store and framebuffer lifetime | Khronos specification terms; no copied text |
| [Microsoft Texture2DMS.Load](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/texture2dms-load) | HLSL integer texel/sample fetch interface | Microsoft documentation, CC BY 4.0; API facts only |
| [ARB_texture_multisample, revision 12](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_texture_multisample.txt) | Distinct samples and integer sample indexing | Khronos specification copyright; does not describe RSX memory layout |
| [ARB_shadow, 21 January 2002](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_shadow.txt) and [EXT_shadow_funcs, NVIDIA revision 5](https://registry.khronos.org/OpenGL/extensions/EXT/EXT_shadow_funcs.txt) | Reference/depth comparison direction and eight comparison functions | Primary extension specifications; no pseudocode transcribed. ARB_shadow's fixed-depth reference clamp is not adopted for the float helper |
| [ARB_fragment_program_shadow, revision 6](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_fragment_program_shadow.txt) | Projected shadow lookup and vector result interface | Primary specification, not evidence of RSX comparison-enable registers |
| [envytools NV30/NV40 register database, f102b823](https://github.com/envytools/envytools/blob/f102b82381f3f11cee113d16374c87091db039d9/rnndb/graph/nv30-40_3d.xml) | Audit of TEX_WRAP RCOMP field, not implementation source | MIT-licensed primary reverse-engineering database. Its comparison names reverse LESS/GREATER relative to this helper's reference-first convention; it does not establish the operand order or comparison-enable heuristic by itself |

## Explicit unresolved evidence

The draw engine's `eng_shadow_state` still documents an RPCS3-derived
classification rule: depth formats, alpha-kill disabled, compare values 1..6,
with 0/7 selecting raw sampling. The references above do **not** independently
establish that rule. Its attribution must remain until public primary RSX
documentation or independently recorded hardware experiments establish a
replacement. Likewise, native RSX sample permutations/locations (including
the distinction between the two four-sample modes) are not proven by a host
round trip. The existing surface-layout adapter is preserved, not certified.

Consequently this replacement is a reviewable implementation step, **not a
declaration that PR #447's renderer licence/provenance hold is cleared**.
An independent provenance review and real-hardware/gameplay comparison remain
necessary before acceptance. Other RPCS3 mentions elsewhere in the large
contribution have not been certified by this topic.

## Review / reuse

Keep this contract, the replacement, and regression evidence as separate
commits. Apply this topic onto the working Folklore line without duplicating
the dependency/FPSCR patches. A pull request against that line is deliberately
small; retargeting it directly to upstream master would drag in the entire
original contribution. After the relevant renderer foundation lands upstream,
cherry-pick the replacement commits and resolve the integration there.

## Validation, 27 October 2026 (Qatar)

Environment: Linux x86-64, GCC 13.3, glslang 15.1.0, SPIRV-Tools 2025.1,
Mesa lavapipe 25.2.8 / LLVM 20.1.2, Khronos validation layer 1.3.275.
Test inputs are synthetic, with no game, firmware or captured shader bytes.

| Check | Result |
| --- | --- |
| `python3 tests/check_renderer_shaders.py` | PASS: 16 extent/overflow/interface checks; all four production shaders compile to Vulkan 1.0 SPIR-V and pass `spirv-val` |
| Extent test with GCC AddressSanitizer + UndefinedBehaviorSanitizer | PASS with `ASAN_OPTIONS=detect_leaks=0`; LeakSanitizer itself cannot inspect processes in this runner |
| `test_vulkan_msaa_gpu --samples=4` | 1,051 numerical/interface checks PASS; 1x/4x colour/depth, RGBA16F, patch pitches, cache invalidation, independent sample-index oracle, failed-begin recovery |
| `test_vulkan_shadow_gpu` | 5,144 checks across 96 cases PASS; all compare functions, projected/texel coordinates, wrap/filter selection, asymmetric PCF weights and NaN reference cases |
| Same two fixtures with `PS3RECOMP_VK_BATCH=0` | Same check counts PASS |
| Vulkan validation, default batching | FAIL: five device-teardown lifetime errors per fixture, all `VUID-vkDestroyDevice-device-05137`; no transfer/shader validation errors in the 1x/4x runs |
| Baseline shadow fixture at `2380f1f` with the same build guard | 4,850 checks / 90 cases PASS, **the same five teardown errors**; establishes that this teardown defect predates this topic |
| Vulkan validation, batching disabled | FAIL: three device-teardown lifetime errors per fixture |
| Original full MSAA fixture | Not a valid pass: lavapipe advertises only 1x/4x, yet the existing backend attempts 2x creation; validation rejects that usage and the fixture's edge assertion fails |
| Existing depth-as-ARGB fixture | Numerical run returns zero, but includes unsupported 2x image creation; **not counted as a clean validation pass** |
| `git diff --check` | PASS |

The unmodified base does not compile with CACAO disabled: `sink_end` accesses
`eng_pipeline.position_known` and `position_rows` outside their CACAO guard.
This fails before testing either renderer implementation. To obtain the
results above, **both baseline and replacement builds used the same temporary
validation-only guard** around the position-row arm, leaving the pipeline-key
fallback available. The source was restored afterwards, and that guard is
not part of this topic (a compile-fix patch was already prepared separately).
These results therefore do not claim that a fresh checkout builds without
the separately prepared build fix. No CMake, dependency, or FPSCR file is
changed here. Existing unrelated compiler warnings remain.

After applying that separately prepared compile fix, the ordinary commands are:

```sh
python3 tests/check_renderer_shaders.py
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DPS3RECOMP_RSX_VULKAN=ON -DPS3RECOMP_BUILD_TESTS=ON
cmake --build build --target test_vulkan_msaa_gpu test_vulkan_shadow_gpu
VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation ./build/test_vulkan_msaa_gpu --samples=4
VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation ./build/test_vulkan_shadow_gpu
```

The `--samples=4` option is an explicit restriction, not an automatic success
on unsupported hardware. Omit it on a device supporting native 2x and 4x.
Inspect validation output as well as exit status: these existing fixtures
count pixel checks, and the layer's teardown errors do not change their exit
codes. Run again with `PS3RECOMP_VK_BATCH=0` to exercise synchronous submission.

## Remaining acceptance evidence

1. Run native 2x/4x tests on the Windows/NVIDIA port device and a second Vulkan
   driver; compare Folklore boot, gameplay, shadow maps, movies and upscalers.
   No Windows, Android, Steam Deck or game execution occurred in this run.
2. Resolve the existing CACAO build guard and device-teardown leaks in their
   own changes, then require zero validation errors for the complete fixtures.
3. Record an independent RSX experiment for the comparison-enable rule:
   synthetic 2x2 depth values, reference below/equal/above each value, every
   compare code 0..7, each depth format, alpha-kill on/off, and TEX/TXP.
   Read all four output channels. Record raw register values, expected
   alternatives and actual output; do not use RPCS3 implementation code as
   the expected-value generator.
4. Record each supported RSX AA mode with one sample-mask bit enabled at a
   time and read the resulting storage bytes. Include 2x and both 4x modes,
   odd extents, colour and depth. Distinguish sample *index order* from sample
   *positions*. Host pack/extract round trips cannot establish either one.
5. Have the maintainer review provenance and the remaining adapter assumptions
   before describing the renderer as cleared for MIT distribution. Keep the
   original notices/history discoverable; this topic does not relicense old
   revisions or the untouched FPSCR files.
