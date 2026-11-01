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
    content: "Glslang-neutral source changes: block-level interface locations in GlslStructBuilder (all 17 Mark stages), padding instead of layout(offset) in PolyMark's split push constant block, and a memory-model review of the two coherent image atomics. Each landed with glslang's SPIR-V shown unchanged or equivalent"
    status: pending
  - id: explicit-binding-audit
    content: "Audit every multi-stage material for a resource shared between stages without an explicit binding, and give each one an explicit binding. Glslang-neutral"
    status: pending
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
  - id: remaining-stages
    content: "geoCount, accumulationCompositePeer, accumulatorScale_2ndPass, multiGpuCompositeIDs, the shading family and the test shaders, as the items above unblock them"
    status: pending
isProject: false
---

# Compiling every shader with Slang

Builds on [pr6-shader-compiler-backend.md](pr6-shader-compiler-backend.md) and the PR 0b results in [remaining-slang-migration-work.md](remaining-slang-migration-work.md). Every claim about Slang below comes from one of those, or is marked as needing a spike.

"Compile with Slang" here means through PR 6's backend, opted in per stage and proven equivalent to glslang. It is not the same as deleting glslang, which is PR 7, nor as rewriting the library in Slang's own idioms, which is PR 8; but the Marks cannot get there without some of PR 8.

## Where things stand

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

## 2. Mechanical, glslang-neutral changes

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

## What this does not cover

- **Shipping.** OSRB approval, packaging `libslang`, `libslang-compiler` and `libslang-glsl-module-<ver>` (the last loaded at runtime), and turning `ENABLE_SLANG` on by default.
- **Deleting glslang**, `ResourceLimits` and the private header copies: PR 7 and PR 5c part 3, which can start only once nothing is left on glslang.
- **Retiring the textual subroutine pass and the string-splicing builder**: PR 8, though section 3's spike may pull some of it forward.

## Order

1 and 3 are spikes and can run in parallel; both gate the Marks. 2, 4 and 5 are independent and can land any time. 6 needs real Mark materials compiling through Slang, so it follows whichever of 1 and 3 gets a Mark compiling first. 7 needs all of them.
