---
name: All shaders through Slang
overview: What it takes to compile every shader in the library with Slang, building on PR 6, which opted in fifteen production stages. The remaining 41 production stages are blocked by four things of different kinds - an unresolved design question about resources Slang drops as unused, GLSL features Slang's GLSL mode does not model at all (buffer device addresses, geometry and mesh I/O), a short list of mechanical source changes, and two Slang bugs - plus missing test coverage and a compile-time cost that matters once the Marks are involved. This is mostly PR 7 and PR 8 of remaining-slang-migration-work.md, sequenced so that the decision everything depends on comes first.
todos:
  - id: unused-resources-decision
    content: "Decided 2026-11-01: take reflection from Slang's program layout. The spike confirmed it works: on accumulatorScale_2ndPass's failing variant the layout reports the unused block with a binding of its own (0), and the used resources' layout bindings agree with the SPIR-V's"
    status: completed
  - id: reflection-from-layout
    content: "Build ShaderReflection from Slang's program layout for Slang-compiled materials, instead of from the SPIR-V. Open design point: the layout covers the whole material, ShaderReflection is per stage, so each stage's declared parameters have to be attributed to it (likely from its own translation unit) while bindings come from the combined program. Reproduce the redecorator's conventions: names, struct flattening, array dimensions, push constant spans, the type whitelist, the clash check Done 2026-11-01 in \"Reflect Slang materials from Slang's program layout\": stages attributed through each translation unit's module; storage block members laid out from the generic's element type, since Slang gives a GLSL buffer no element layout; array sizes taken as length times stride, Slang's own omitting the trailing padding; and every stage cross-checked against reflection of its own SPIR-V, failing the material on any disagreement. accumulatorScale_2ndPass.frag then opted in and passed be-render-tests."
    status: completed
  - id: mechanical-changes
    content: "Glslang-neutral source changes: block-level interface locations in GlslStructBuilder (all 17 Mark stages), padding instead of layout(offset) in PolyMark's split push constant block, and a memory-model review of the two coherent image atomics. Each landed with glslang's SPIR-V shown unchanged or equivalent Done 2026-11-01. Block-level locations where every member takes one location (all Mark blocks); push constant offsets as [[vk::offset(N)]] behind #ifdef __SLANG__ rather than padding, since padding would move the block range Vulkan checks; coherent dropped from two images where it changes nothing. Two more Slang gaps turned up: gl_SampleMaskIn (now an SV_Coverage parameter under __SLANG__) and coherent rejected by every image function, not only atomics."
    status: completed
  - id: explicit-binding-audit
    content: "Audit every multi-stage material for a resource shared between stages without an explicit binding, and give each one an explicit binding. Glslang-neutral Done 2026-11-01: no resource shared within a material without an explicit binding among the shaders be-render-tests assembles. Enforced rather than trusted: the layout reflection now refuses a material where one does."
    status: completed
  - id: slang-syntax-spike
    content: "Done 2026-11-01: yes. Slang syntax for buffer device addresses, geometry entry points and mesh entry points compiles inside a GLSL translation unit through the existing compile request; Slang defines __SLANG__ there, so one template can carry both forms and still compile with glslang; and a Slang-syntax geometry stage links by location with GLSL vertex and fragment stages in one material. Local edits, not a conversion to Slang modules"
    status: completed
  - id: dual-source-marks
    content: "Rewrite the not-modelled parts of the Marks behind #ifdef __SLANG__: slab reads in slabAddressTable.glsl as Slang pointers, the four geometry templates and two mesh templates as Slang-syntax entry points with their helpers taking the output stream or arrays as parameters, and GlslStructBuilder emitting Slang structs with [[vk::location]] for those stages. Prove one family end to end first"
    status: pending
  - id: coverage
    content: "Tests for the production stages nothing exercises: windbarbs, tableFunctionTest, shadingMain.vert, every scale variant (unique artifact names per compile), and multiGpuCompositeIDs where a second GPU is available"
    status: pending
  - id: compile-time
    content: "Measure Slang compile time on real Mark materials, which are assembled per Vega render, and decide whether a compiled-shader cache keyed on the assembled source is needed before the Marks move"
    status: pending
  - id: slang-bugs
    content: "Report upstream: texelFetch on sampler2DMS drops the sample index; the module API drops GLSL local_size; PreserveParameters has no effect on GLSL input. Work around SeparateMultiSample.frag by writing it in Slang syntax if no fix arrives"
    status: pending
  - id: marks
    content: "Move the Marks family by family, each proven per test against glslang: reflection, execution modes, and be-render-tests failing tests rerun against each backend"
    status: pending
  - id: static-slang
    content: "Link Slang statically, as glslang is. The backend already needs nothing at run time but the compiler library (commit 'Need only Slang\'s compiler library at run time'). What is left is the deps build: build Slang with -DSLANG_LIB_TYPE=STATIC, then find out which static archives it installs, list them in FindSlang, and check for symbol clashes with heavydb's static glslang and SPIRV-Tools and for the size it adds (the shared library is 34 MB). User-run, since it needs a deps rebuild Script and CMake done 2026-11-01 (commits \"Build Slang as one self-contained static library\" and \"Find and link a static Slang\"), proven against a scratch build: 59 MB archive, server 33 MB larger with no Slang run-time dependency, be-render-tests 441 passing with Slang preferred. What remains is the user's deps rebuild, then reconfiguring build directories with -USlang_LIBRARY -USlang_INCLUDE_DIR. Completed 2026-11-02: the script also drops the two runtime-loaded shared libraries Slang still builds, FindSlang accepts only the static archive, and the build container's deps prefix was switched to the static install by hand and re-proven (FindSlang discovers it, be-render-tests 441 passing with Slang preferred). A fresh deps build gets the same from the script."
    status: completed
  - id: remaining-stages
    content: "geoCount, accumulationCompositePeer, accumulatorScale_2ndPass, multiGpuCompositeIDs, the shading family and the test shaders, as the items above unblock them"
    status: pending
isProject: false
---

# Compiling every shader with Slang

Builds on [pr6-shader-compiler-backend.md](pr6-shader-compiler-backend.md) and the PR 0b results in [remaining-slang-migration-work.md](remaining-slang-migration-work.md). Every claim about Slang below comes from one of those, or is marked as needing a spike.

"Compile with Slang" here means through PR 6's backend, opted in per stage and proven equivalent to glslang. It is not the same as deleting glslang, which is PR 7, nor as rewriting the library in Slang's own idioms, which is PR 8; but the Marks cannot get there without some of PR 8.

## Done: every shader through Slang (2026-11-02)

All 147 stages in every manifest are opted in, production and test alike. With Slang preferred no material is compiled by glslang. Both compilers pass the render suite (every binary), and be-render-tests gives 441 passing on both servers, the two failures being the suite's own. Materials no test reaches are proven by `SlangUnreachedMaterialsTest`, which builds them through each compiler and requires identical reflection and execution modes: the peer accumulation and multi-GPU ID composites, the table function test, and the windowed tests' shaders. The wind barb, which nothing renders, was rendered once by hand on each server: the images are byte-identical. The rest of this document is the record of how it got here.

**How the features Slang's GLSL mode lacks were bridged**, keeping one source for both compilers behind `#ifdef __SLANG__`, with function bodies unchanged:
- **Geometry shaders:** `Slang/slangGeometry.glsl`. A Slang entry point takes the input primitive and a `TriangleStream`. The include mirrors the inputs and outputs as the globals the GLSL body uses, `gl_in` included, and defines `EmitVertex` and `EndPrimitive` on the stream. Only the stream is threaded through the functions that emit (`SLANG_GEOMETRY_STREAM`, empty for glslang).
- **Mesh and task shaders:** `Slang/slangMesh.glsl` maps `gl_MeshVerticesEXT`, the primitive index arrays, `SetMeshOutputsEXT` and `EmitMeshTasksEXT` onto Slang's output parameters, `SetMeshOutputCounts` and `DispatchMesh`. Every mesh shader here writes its outputs in `main`, which this relies on.
- **Interface blocks for those stages:** `GlslStructBuilder::createSlangStructString` writes the Slang struct, with the same locations, `SV_Position` and optionally `SV_PointSize`, and an X-macro listing the members. The Marks fill new `<...Slang>` tags with it.
- **Buffer device addresses:** the slab table's `SLAB_LOAD`, and elsewhere a struct holding a pointer with an `__init` from the address, so `Type(address).member[i]` is unchanged.
- **Ray tracing:** `traceRayEXT` hits an internal error in Slang. `rtTraceRay.glsl` defines it through `TraceRay` with the payload variable, which Slang copies in and out.
- **Pixel interlock:** a `globallycoherent RasterizerOrderedTexture2D`. Without `globallycoherent` it renders wrong.
- **Multisampled fetch:** `SeparateMultiSample` uses the texture's `Load`, which keeps the sample index that `texelFetch` drops.

**Fixed in the backend and reflection on the way:**
- **Buffer block layout:** Slang's reflection API lays out a GLSL buffer block's element type with scalar alignment, whatever the block declares. So a block's size and members now come from the SPIR-V whenever the entry point uses it. A block it never touches is still laid out by the API, and could be wrong where std430 and scalar alignment differ; nothing binds such a block by member today.
- **Push constants:** member offsets come from the SPIR-V, since the layout ignores `[[vk::offset]]`.
- **Specialization constants** are skipped in the layout reflection.
- **Names and types:** the `_natural` name suffix of scalar storage blocks is stripped. A rasterizer-ordered texture is reflected as a storage image.
- **Array inside a struct member:** an `int pad[3]` in a struct member is accepted.
- **Includes:** angle-bracket includes are resolved.
- **Diagnostics:** a layout and SPIR-V disagreement now shows both sides.

**Shader fixes that glslang compiles identically:**
- `imageSize` renamed to `viewportSize`, since it is a built-in function.
- `coherent` dropped where an image is only used atomically.
- Explicit locations on unlocated vertex inputs where reflection parity needed them: Slang numbers them from 1.
- `.st` swizzles replaced with `.xy`.
- `float(uvec3)` replaced with the component itself.
- A mesh output composed before it is written, since Slang cannot read one back.
- The ray tracing acceleration structure bound explicitly, since Slang matches shared resources only by binding.

**Test fixtures that test glslang itself** (the redecorator, the unlocated-varying check) now set the compiler they test, rather than relying on not being opted in. The link failure test runs through both compilers.

**Opt-in flags and the switch removed (2026-11-02).** `"slang"` is gone from the manifests, the schema, the generated library and `Library::Item`, and so is `OMNISCI_shader_compiler_backend`. A build with Slang compiles every material with Slang, and one without compiles with glslang. `ShaderManager::setCompilerBackend` remains, for the tests that compare the two compilers or test glslang itself. A consequence is that in an `ENABLE_SLANG` build the render suite and be-render-tests no longer run through glslang at all; only an `ENABLE_SLANG=off` build does, and that build has not been run since.

**glslang removed (2026-11-02).** PR 7 landed: see remaining-slang-migration-work.md. Every material is compiled by Slang, the shaders have no `__SLANG__` conditionals left, and the server links no glslang library. be-render-tests: 441 passing, the two known failures.

**Left to do:**
- **Compile time** (section 6): an in-memory material cache landed (`55fbb601eb`), taking be-render-tests' compile time from 30.8 to 21.4 s. The driver's pipeline builds were measured (`b28c6906d1`): 4.0 s with NVIDIA's disk cache, 13.0 s without, so no `VkPipelineCache` yet. The PR 8 spike found that modules alone do not cut the remaining cost; see "Spike findings" under PR 8+ in remaining-slang-migration-work.md.
- **Upstream Slang reports**, now with these reproducers:
  - `traceRayEXT` internal error;
  - `texelFetch` multisample index;
  - module API `local_size`;
  - `PreserveParameters`;
  - scalar layout of GLSL buffer blocks in reflection;
  - mesh output read-back;
  - inheritance internal error.
- **Saved Manual builders** for the line and fastSymbol templates predate the `<...Slang>` tags, and stay commented out of the Legacy cases. The point template's two do compile through Slang. The others would need recapturing.
- **Full deps rebuild**: done (2026-11-02). The deps container no longer builds glslang (`df8fdff404`), and a fresh full build and both test suites pass against it.
- **Debug build with validation layers**: done (2026-11-02), and it found four problems no Release run had shown, now fixed. See "Debug build with validation layers" in remaining-slang-migration-work.md.
- **A new manifest entry needs CMake re-run**: the generated shader sources are listed at configure time. Making the manifests configure dependencies would fix that.
- **Unrelated bug found:** a wind barb Vega that omits `quantizeDirection` fails, because the property is declared `INT` but defaults to `true`.

## Where things stand (before 2026-11-02)

Of 56 production stages, 15 are opted in: the nine single-stage compute shaders (`ppllDebugVisualizer`, `ppllStats`, `ppllStatsTiled`, `ppllStatsTiledBatches_Stage1/2`, `accumulatorTx_findExtents`, `accumulatorTx_findStdDev`, `clearQueryOutputBuffer`, `MSMultiGpuComposite`) and six post-processing stages (`SMAAPassThru.vert`, the three SMAA passes, `fullScreenTriangle.vert`, `accumulatorIDPass.frag`). PR 6's own write-up says fourteen; that is the number be-render-tests sent through Slang, `MSMultiGpuComposite` being proven by its golden instead.

The remaining 41, by family:

| Family | Stages | Blocked by |
|---|---|---|
| Marks: fastSymbol, line, point, symbol, windBarb | 17 | Interface block locations (all); buffer device addresses (5 vertex templates, both mesh templates); geometry I/O (4); mesh I/O (2); unused resources (likely, as heavily templated) |
| Marks: mesh2d, PolyMark PPLL | 6 | Push constant `layout(offset)`; coherent image atomic (`ppllPolyTemplate_composite`); unused resources (likely) |
| Scales | 6 | Spliced into Mark vertex shaders, so they move with them; `accumulatorScale_2ndPass.frag` has variants that declare a block they never read |
| `geoCount_build`, `geoCount_count` | 2 | Buffer device addresses |
| `SeparateMultiSample.frag` | 1 | Slang bug: `texelFetch` on `sampler2DMS` drops the sample index |
| `accumulationCompositePeer.frag` | 1 | Coherent image atomic |
| `multiGpuCompositeIDs.comp` | 1 | Untested: needs a second GPU |
| Shading, table function test, `ppllCountFragments.frag` | the rest | Mostly untested rather than known to fail; `shadingMain.frag`'s blocker was fixed |

Plus 89 test stages, which do not ship and can stay on glslang until glslang goes. Some hit the same blockers, plus `pixel_interlock` and a user identifier named `imageSize`.

## 1. Unused resources: reflection from Slang's layout (decided)

**Decision (2026-11-01): take reflection from Slang's program layout for Slang-compiled materials.**

**Spike result.** On the `pct_accum_test_0` variant of `accumulatorScale_2ndPass.frag`, compiled with `fullScreenTriangle.vert` as one program through a compile request:

| | `ACCUMULATOR_SCALE_2ND_PASS_UBO` (unused) | `inTxArrayPixelCounter` | `THRESHOLD_SCALE_UBO` |
|---|---|---|---|
| Slang program layout | 0 | 1 | 2 |
| Slang SPIR-V | absent | 1 | 2 |
| glslang reflection | 2 | 0 | 1 |

So Slang did not renumber anything because the block was unused: it reserved binding 0 for it and left it out of the SPIR-V. What broke the accumulation tests was that reflection read from the SPIR-V had no block at all, so the material had nothing to bind it to. Reflection from the layout includes it, and binding a descriptor the shader never reads is harmless. The numbers differ from glslang's, which does not matter, since the runtime binds whatever the reflection says.

**Design point to settle when building it.** The program layout lists the parameters of the whole material; `ShaderReflection` is per stage. Each stage's declared parameters have to be attributed to it, most likely by reflecting each translation unit on its own for the names it declares and taking the binding numbers from the combined program. Everything `ShaderRedecorator` encodes has to be reproduced from the layout's type information instead: names (no `SLANG_ParameterGroup_` normalisation needed, since the layout reports declared names), dotted UBO member names and bare SSBO ones, array dimensions and their flattening, push constant spans and dual naming, the type whitelist, fragment output locations, and the clash check. This is the reflection half of PR 7, brought forward.

The rest of this section is the analysis that led to the decision.

### Background

Slang drops any resource the entry point never reads; glslang keeps every declared one, because `IoMapResolver` forces liveness. `PreserveParameters` does not change Slang's behaviour for GLSL input. Where a variant leaves a declared block unread, Slang's reflection lacks it and the bindings differ from glslang's. That is what kept `accumulatorScale_2ndPass.frag` out: 19 accumulation tests rendered wrong in the variants that do not read `ACCUMULATOR_SCALE_2ND_PASS_UBO_TYPE`. The Marks and Scales are assembled from templates and subroutine bindings in the same way, so the same problem should be expected across them.

**Spike first:** does Slang's reflection API (`IComponentType::getLayout`) report parameters the entry point does not use, with the bindings Slang gave them? Compile `accumulatorScale_2ndPass.frag`'s failing variant through the compile request and inspect its program layout. This decides which of the options below is cheap.

Options, to choose between once the spike answers:
- **Reflection from Slang's layout rather than from the SPIR-V.** Already PR 7's plan for reflection. If the layout covers unused parameters and the runtime binds by that layout, the SPIR-V simply omits what it never reads, which is harmless. Largest change, and the one that also removes `ShaderRedecorator`'s SPIRV-Cross path.
- **Runtime tolerance.** Let `Material` and `buildSpirv` accept a named resource being absent, skipping it. Smallest change, but it weakens checks that also protect glslang materials (`buildSpirv` currently aborts on a missing external uniform buffer), so it needs to be scoped to resources that are declared but unused, not to names that are wrong.
- **Make every declared resource reachable in every variant.** Template changes across the Marks and Scales. Probably the least attractive, but it needs no runtime change.

Whatever is chosen, find out first why a missing block made the accumulation output wrong rather than raising an error: that is the failure mode to close regardless.

### Built (2026-11-01)

`SlangReflection.cpp` builds the reflection; `SlangBackend` returns it with the SPIR-V and `buildSpirv` uses it. What building it found:
- **Stage attribution** works as planned: the program layout lists each translation unit's parameters in order, and each unit's module layout names them, so they are matched in sequence and the attribution checked.
- **A GLSL buffer block has no element layout in Slang's reflection.** Its type is `GLSLShaderStorageBuffer<T, L>`, and every accessor for its element returns nothing. `T` is reachable through the generic and laid out with `getTypeLayout`, but that ignores `L`, the block's data layout. For a scalar-declared block of structs the two could differ.
- **Slang reports an array's size without the padding after its last element**, where SPIRV-Cross's declared size, which the goldens record, includes it. The cross-check against the SPIR-V caught this on its first run.
- **So every stage is cross-checked:** reflected from its SPIR-V the old way as well, with everything found there required to appear identically in the layout's reflection. A disagreement fails the material instead of rendering wrong. What only the layout reports are the resources the shader never reads, whose offsets cannot matter.

Sixteen production stages are now opted in, `accumulatorScale_2ndPass.frag` among them, and be-render-tests against a Slang server gives 441 passing, as glslang does.

## Marks: point and symbol (2026-11-02)

The first Marks are on Slang. `slabAddressTable.glsl` reads slabs through a `SLAB_LOAD` macro, a Slang pointer under `__SLANG__` and the `buffer_reference` types otherwise; that unblocked all five Mark vertex templates that read slabs. Point and symbol need nothing else, being vertex and fragment only, so `pointTemplate.vert/.frag` and `symbolTemplate.vert/.frag` are opted in (`pointTemplate.mesh` is not, so the mesh variant stays on glslang). Twenty production stages are now opted in.

Found on the way:
- **`mainTemplate_Accumulation.glsl` declared its image coherent**, and is spliced into every Mark fragment shader that accumulates, so every accumulation variant failed through Slang; the static scans had missed it because it is an include. It only uses atomics, so the qualifier went. No shader in the library now declares a coherent image.
- **The Manual point builders were stale:** saved `.builder` files replay the interface block text of the day they were saved. Refreshed to today's form.
- **Auto-assigned bindings can be numbered differently** by the two compilers within one material: `OUTPUT_ID_UBO_TYPE` in `pointTemplate.frag` is binding 5 under glslang and 4 under Slang. Harmless, since the runtime binds by reflection, but it means parity across compilers has to allow for renumbered unbound resources, not demand identical bindings.
- **A full capture cannot always compare like with like for Marks**, since their properties are emitted in a different order from run to run; `symbolTemplate.vert` was captured as different variants in the two runs. For Marks the in-run cross-check between layout and SPIR-V, plus be-render-tests, is the evidence.

be-render-tests with Slang preferred: 441 passing, as with glslang.

### Compile time on real Marks (measured 2026-11-02)

With the compile time and a source hash logged at debug level 1, be-render-tests against a Slang server compiled 332 materials: 101 through Slang taking 3.87 s, 231 through glslang taking 0.83 s, over a 2-minute suite (121 s against 115 s with glslang alone).
- **Per material:** point and symbol about 49 ms through Slang (max about 70) against 3.2 ms through glslang, about 15 times. `fullScreenTriangle` with a post-processing pass 31 ms against 2.7. Single-stage compute 4 to 28 ms against under 1.
- **Marks are not reused:** a Mark material is compiled again for each new or changed layer. `pointTemplate` compiled 24 times, `symbolTemplate` 23, `fastSymbolTemplate` (still glslang) 91.
- **28% of compiles repeat a source** already compiled in the same process, and so 28% of Slang's compile time, 1.08 of 3.87 s. be-render-tests is mostly distinct Vegas by design, so an interactive workload re-rendering the same charts likely repeats more.
- **Projected:** moving fastSymbol, line and the poly Marks at the same ratio would add about 10 s of compiling to the suite, about 50 ms per Mark layer per changed render.

**Recommendation:** a compiled-material cache in `ShaderManager` before moving more Marks, keyed on every stage's assembled source and entry point and on the compiler, holding the SPIR-V and reflection, bounded. It saves the repeats for both compilers, and the same log line measures its hit rate. A persistent on-disk cache, keyed also by Slang's version, would extend that across restarts, if the in-memory one proves worth it.

## 2. Mechanical, glslang-neutral changes

**Done (2026-11-01).** Each verified with be-render-tests on glslang unchanged at 441 passing, and the affected shaders recaptured and compiled through Slang.
- **Interface block locations:** `GlslStructBuilder` emits one `layout (location = 0)` on a block whose members each take one location, which every Mark block does, and a location per member otherwise. Every Mark fragment template the suite reaches now compiles through Slang.
- **Push constant offsets:** not padding after all. A padding member at offset 0 moves the block's declared range, which Vulkan requires the pipeline's range for that stage to cover, so the fragment range would have to overlap the vertex one and every vertex push would need the fragment stage flag. Slang accepts `[[vk::offset(N)]]` on the member instead, so the two PolyMark shaders carry both forms behind `#ifdef __SLANG__` and glslang compiles exactly what it did.
- **Coherent:** Slang rejects any image function on a coherent image, `imageLoad` and `imageStore` included, not just atomics. `accumulationCompositePeer.frag` only uses atomics, which are coherent anyway; `ppllPolyTemplate_composite.comp` never used an atomic (its earlier listing came from a static pattern, not a Slang error) but reads and writes only its own pixel. Both dropped the qualifier.
- **New: `gl_SampleMaskIn`** is not in Slang's GLSL support at all. PolyMark's capture shader takes it as an `SV_Coverage` parameter of `main` under `__SLANG__`, the same `SampleMask` built-in.
- **Explicit bindings:** no resource is shared within a material without one, among the shaders the suite assembles. The layout reflection now refuses any material that does it, naming the resource.

With these, the only Mark stages that still fail through Slang are those using buffer device addresses or geometry or mesh I/O, which is section 3.

### Original analysis

Each verified the way the earlier shader fixes were: glslang's SPIR-V unchanged or equivalent, the render suite, and be-render-tests.

- **Interface block locations.** `GlslStructBuilder` puts `layout(location = N)` on each member of every Mark interface block, which Slang rejects. A block-level location, or loose varyings, compiles. One change, in one place, reaching all 17 Mark stages. PR 4b made per-member locations load-bearing, and `GlslangWrapper`'s varying check already exempts a block carrying one location for the whole block; keep it passing.
- **Push constant offsets.** PolyMark's split push constant block uses `layout(offset = N)`, which Slang rejects. Padding members compile, but the reflected span would then start at 0, not at the offset; `validate_push_constant_range` and the span rule in `ShaderRedecorator` have to agree with the hand-written ranges at the call sites.
- **Coherent image atomics.** `ppllPolyTemplate_composite.comp` and `accumulationCompositePeer.frag` call `imageAtomic*` on a `coherent` image, which Slang rejects. Dropping `coherent` compiles. Review the memory model first: atomics are coherent anyway, but other accesses to the same image in those shaders may rely on it.
- **Explicit bindings for shared resources.** Compiled as one program, Slang gives a resource shared between stages a binding per stage unless the binding is explicit; glslang matches it by name. Audit every multi-stage material for this, and make each shared binding explicit.

## 3. Features Slang's GLSL mode does not model (spike first)

PR 0b confirmed each of these on a minimal shader: no GLSL-level workaround exists.
- **Buffer device addresses** (`layout(buffer_reference)`): `slabAddressTable.glsl`, which five of the seven Mark vertex-stage templates and both mesh templates include, and both `geoCount` passes. Slang's replacement is its pointer types.
- **Geometry shader I/O** (`layout(lines_adjacency) in`, `layout(triangle_strip, max_vertices = N) out`): four production geometry shaders.
- **Mesh and task shader I/O** (`layout(..., max_primitives = N) out`, `taskPayloadSharedEXT`): two production mesh shaders.

**Spike result (2026-11-01): yes, as local edits.** Through a compile request with a GLSL translation unit, exactly as the backend compiles:
- **Buffer device addresses** in Slang syntax, `((double*)addr)[i]` or `Ptr<double>(addr)[i]`, compile alongside the GLSL uniform blocks and built-ins, and produce a real device-address load: `OpConvertUToPtr` to a `PhysicalStorageBuffer` pointer under the `PhysicalStorageBuffer64` memory model, as glslang's `buffer_reference` does.
- **A geometry stage** with a Slang-syntax entry point (`[maxvertexcount(8)]`, a `lineadj` input array, an `inout TriangleStream<T>`) and GLSL types and uniform blocks gives `InputLinesAdjacency`, `OutputVertices 8` and `OutputTriangleStrip`, and passes spirv-val.
- **A mesh stage** with a Slang-syntax entry point (`[outputtopology("point")]`, `[numthreads(32, 1, 1)]`, `out vertices` and `out indices` arrays) gives `OutputPoints`, `OutputVertices 32`, `OutputPrimitives 32` and `LocalSize 32 1 1`.
- **Slang defines `__SLANG__`** in a GLSL translation unit, so a template can carry both forms. A file with the slab read behind `#ifdef __SLANG__` compiled through Slang using pointers, and through `glslangValidator` using `buffer_reference`. Both backends keep working from one source while both exist.
- **Mixed materials link by location.** A GLSL vertex stage writing locations 0 and 1, a Slang-syntax geometry stage reading them through `[[vk::location(N)]]` struct fields and writing location 0, and a GLSL fragment stage reading location 0, compiled as one material, line up exactly.

What it costs, not yet measured on a real template:
- The geometry and mesh templates' entry points and I/O change shape, not just their declarations. `gl_in[i]`, `EmitVertex()` and `gl_MeshVerticesEXT[i]` become struct parameters, `stream.Append` and output arrays, so helpers that emit vertices today have to take the stream or arrays as parameters. That is a rewrite of each template's main and its emitting helpers, behind `#ifdef __SLANG__`, with the GLSL form kept beside it until glslang goes.
- `GlslStructBuilder` has to emit Slang structs with `[[vk::location(N)]]` fields for those stages, alongside the GLSL interface blocks it emits now.
- Nothing here has rendered: the spike compiled and linked minimal shaders. The first family converted is the real proof, and point (with its mesh template) or line (with its geometry templates) is the place to start.

The original question, kept for the record: Slang accepts GLSL syntax inside Slang modules when `allowGLSLSyntax` is set, but whether it accepts the reverse, Slang syntax in a GLSL translation unit, is untested. If it does, these become local edits to `slabAddressTable.glsl` and the geometry and mesh templates. If not, those templates become Slang modules, the builder has to splice into them, and this is PR 8 proper. The answer sizes most of what is left.

## 4. Slang bugs

- **`texelFetch` on `sampler2DMS` drops the sample index.** `SeparateMultiSample.frag` reads sample 0 regardless. Report upstream with that shader. Workaround if no fix arrives: write that small shader in Slang syntax, where the multisample load is a method taking the sample index explicitly.
- **The module API drops GLSL `local_size`.** Worked around by compiling through a compile request, which is declared in `slang-deprecated.h`. Report upstream, and recheck on every Slang upgrade.
- **`PreserveParameters` has no effect on GLSL input.** Report alongside; if fixed, it may resolve section 1 outright.

## 5. Coverage

Today's lesson from `accumulatorScale_2ndPass`: a full be-render-tests capture keeps only the last variant of each template, so a reflection match there is not proof. Every opt-in should be proven by rerunning each failing test against each backend, and every stage should be reachable by some test.

- **Unique artifact names per compile**, so a full run captures every variant rather than the last. A counter or a hash of the assembled source in the basename.
- **A windbarb spec** in be-render-tests: nothing renders one.
- **`tableFunctionTest.*`, `shadingMain.vert`**, and each scale type (ordinal, quantize, threshold, percentage) in at least one captured variant.
- **`multiGpuCompositeIDs.comp`** needs a second GPU; prove it on one before opting it in, or leave it on glslang.

## 6. Compile time

Slang is 13 to 37 times slower than glslang on the same source (`DISABLED_SlangCompileTimeTest`): 41 ms against 1.9 ms for the SMAA blending weight pass. That does not matter for shaders compiled once per context, which is all PR 6 moved. Marks are assembled per Vega render, with properties and scales spliced in, so a 40 ms material compile per layer would be visible. Measure on real Mark materials before moving them, and if it matters, add a compiled-shader cache keyed on the assembled source and entry points, or look at Slang's precompiled modules once the Marks are modules.

## 7. Move the Marks, then the rest

Once sections 1 to 3 have answers, move the Marks family by family: point, symbol and fastSymbol, line, mesh2d, PolyMark, windBarb. For each, opt in, then prove it with reflection and execution modes against glslang, the render suite in both modes, and be-render-tests with every failing test rerun against each backend. Then `geoCount`, `accumulationCompositePeer`, `accumulatorScale_2ndPass` (unblocked by section 1), `SeparateMultiSample` (section 4), and the test shaders.

## Linking Slang statically

glslang is linked into heavydb statically; Slang was linked as a shared library plus two more loaded at run time. Since commit "Need only Slang's compiler library at run time", the backend loads nothing but `libslang-compiler`:
- **The GLSL module** is compiled by the backend with `compileBuiltinModule`, rather than requested with `enableGLSL`, which looks for `libslang-glsl-module-<version>` and, without it, compiles the module anyway and writes a `slang-glsl-module.bin` cache next to whichever binary holds Slang: the server's executable, once Slang is static. About 0.4 seconds once per backend. A faster alternative, if that ever matters, is to embed the precompiled module (about 1 MB, `saveBuiltinModule`) at build time and pass it to `loadBuiltinModule`, at the cost of a build step tied to the exact Slang version.
- **Optimization is off.** Slang runs `spirv-opt` through `libslang-glslang-<version>`, and every optimized compile fails without it. Unoptimized output is identical with or without the library; glslang's output is not optimized either.
- **A GLSL module compiled from source leaks its built-in variables** into the program layout, `gl_Position` and the rest, as though the shader declared them; the precompiled one does not. The layout reflection skips them.

**Done in the script and CMake (2026-11-01).** `build_slang` now builds Slang static, position-independent and without Release debug info (Slang compiles Release with `-g`, which made the static archive 1.17 GB), then merges into `libslang-compiler.a` the internal static archives it needs and Slang does not install: `compiler-core`, `core`, `cmark-gfm`, `lz4` and `miniz`. The result is a self-contained 59 MB archive. `FindSlang` prefers it, still accepts a shared `libslang.so`, and adds Threads, libdl and `SLANG_STATIC` for the static case. A server built against a scratch build of it has no run-time dependency on Slang, is 33 MB larger, and passes be-render-tests with Slang preferred. No symbol clashed with heavydb's static glslang or SPIRV-Tools. Since then (2026-11-02): the script also removes the two shared libraries Slang builds even when static, which only load at run time, so the install is static libraries alone; `FindSlang` accepts only `libslang-compiler.a`; and the build container's deps prefix was switched to the static install by hand rather than rebuilt, then re-proven: `FindSlang` finds the archive there unaided, the server has no Slang run-time dependency, and be-render-tests with Slang preferred gives 441 passing. Other build directories need reconfiguring with `-USlang_LIBRARY -USlang_INCLUDE_DIR`, since CMake caches the old shared library's path.

Before that, what was left was the deps build: Slang's CMake has `SLANG_LIB_TYPE=STATIC` (the default is `SHARED`), and its comments say a static install now exports cleanly. Untried: which static archives it installs, symbol clashes with heavydb's static glslang and SPIRV-Tools, and the size it adds.

## What this does not cover

- **Shipping.** OSRB approval, packaging `libslang`, `libslang-compiler` and `libslang-glsl-module-<ver>` (the last loaded at runtime), and turning `ENABLE_SLANG` on by default.
- **Deleting glslang**, `ResourceLimits` and the private header copies: PR 7 and PR 5c part 3, which can start only once nothing is left on glslang.
- **Retiring the textual subroutine pass and the string-splicing builder**: PR 8, though section 3's spike may pull some of it forward.

## Order

1 and 3 are spikes and can run in parallel; both gate the Marks. 2, 4 and 5 are independent and can land any time. 6 needs real Mark materials compiling through Slang, so it follows whichever of 1 and 3 gets a Mark compiling first. 7 needs all of them.
