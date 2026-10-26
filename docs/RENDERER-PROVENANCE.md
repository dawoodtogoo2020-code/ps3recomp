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
under this repository's MIT licence.

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

Validation results and runnable commands are recorded below after execution.
