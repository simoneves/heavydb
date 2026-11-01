---
name: PR 6 shader compiler backend
overview: Put a ShaderCompilerBackend interface between ShaderManager and GlslangWrapper, and add a Slang implementation that compiles the existing GLSL through Slang's GLSL compatibility mode for the shader families PR 0b showed are ready, selected per material and off by default. glslang stays the default and the only backend for the Marks. Reflection still comes from ShaderRedecorator reading the SPIR-V, so both backends share one reflection path and can be compared directly. Developed now; merging waits on Slang OSRB approval.
todos:
  - id: 6a-interface
    content: "6a: Extract ShaderCompilerBackend from GlslangWrapper's existing glslToSpirv seam, with StageSource and CompileResults moving to the interface's header. glslang only; no behaviour change, no golden moves. Done in the commit "Put a ShaderCompilerBackend interface in front of GlslangWrapper": render suite 85/85, no golden moved, be-render-tests unchanged at 441 passing"
    status: completed
  - id: 6b-build
    content: "6b: Optional Slang dependency in CMake (ENABLE_SLANG, default OFF until OSRB), via the slang CMake package in the deps prefix. Link libslang; confirm libslang-glsl-module is found at runtime"
    status: pending
  - id: 6c-slang-backend
    content: "6c: SlangBackend implementing the interface: one global session per ShaderManager, a session with the PR 0b options, an ISlangFileSystem over Library, one module per stage from the assembled source, entry point by name and stage"
    status: pending
  - id: 6d-reflection
    content: "6d: Make ShaderRedecorator produce identical ShaderReflection from Slang's SPIR-V: undo Slang's block naming and the .data wrapping of array members, and decide what to do about resources Slang drops as unused"
    status: pending
  - id: 6e-selection
    content: "6e: Per-material backend selection: a server/test option choosing glslang (default) or slang, and a per-library-item opt-in in the shader manifests. A material uses Slang only if the option is on and every stage has opted in"
    status: pending
  - id: 6f-compute
    content: "6f: Opt in the single-stage compute shaders and prove them: ShaderCompilerTest goldens through both backends, reflection parity, and be-render-tests golden images with the option on"
    status: pending
  - id: 6g-multistage
    content: "6g: Spike, then decide: how Slang assigns auto bindings across the stages of a multi-stage material, which glslang does with one shared resolver. Gates opting in any vertex+fragment family"
    status: pending
  - id: 6h-postprocess
    content: "6h: Opt in the post-processing family (SMAA, accumulator passes, fullScreenTriangle) once 6g is settled, excluding SeparateMultiSample.frag until Slang's texelFetch sample-index bug is fixed"
    status: pending
isProject: false
---

# PR 6: a shader compiler backend interface, with Slang alongside glslang

Builds on [remaining-slang-migration-work.md](remaining-slang-migration-work.md), whose PR 0b section supplies every fact about Slang used here. PR 0b changed what PR 6 has to be: the original plan was a backend flag for the whole library, but the Marks cannot go through Slang's GLSL mode at all (buffer device addresses, geometry and mesh I/O), so the switch has to work per material, and compiling clean turned out not to mean correct.

## Goal and non-goals

**Goal:** a merged, default-off path that compiles the shader families PR 0b found ready through Slang, proven equivalent to glslang by reflection and by rendered images, so that PR 7 starts from working code rather than from a spike.

**Not in PR 6:**
- Any Mark. Every Mark vertex, geometry and mesh template is in the PR 8 bucket; their fragment templates are mechanical but share materials with stages that are not.
- Making Slang the default, deleting glslang, `ShaderRedecorator` or `ResourceLimits`. That is PR 7.
- Slang's own reflection API. Reflection stays with `ShaderRedecorator` reading SPIR-V, deliberately, so one reflection path serves both backends and any difference is a backend difference.
- Shipping. Merging waits on OSRB; the build option stays off until then.

## The seam that already exists

`ShaderManager::buildSpirv` assembles every stage's GLSL (`processOperators`, `rebindSubroutineCalls`), makes one call, `GlslangWrapper::glslToSpirv(const std::vector<StageSource>&)`, and gets back SPIR-V per stage plus error text. Everything after that (the redecorator's reflection, the external uniform buffer check, `spirv-opt`, artifacts, the error path) works on SPIR-V and does not care where it came from. So the interface is that one call:

```cpp
class ShaderCompilerBackend {
 public:
  virtual ~ShaderCompilerBackend() = default;
  // One material: every stage compiled together, results in the same order
  virtual CompileResults compileMaterial(const std::vector<StageSource>& stages) = 0;
};
```

`StageSource` and `CompileResults` move from `GlslangWrapper` to the interface's header unchanged. `GlslangWrapper` implements it; `ShaderManager.h`'s `GlslangWrapperUqPtr glslang_wrapper_` (line 318 at the time of writing) becomes the default backend plus an optional Slang one.

Two things stay outside the interface on purpose. Template assembly is ours and stays in `ShaderManager`; and `ShaderRedecorator`, which already runs on SPIR-V, needs only to know which backend produced it (see 6d).

## 6a: extract the interface (no behaviour change)

Pure refactor. Verified by the render suite, ShaderCompilerTest's goldens not moving, and be-render-tests. Land first and separately so the refactor is reviewable on its own.

## 6b: build

- `ENABLE_SLANG` CMake option, default OFF until OSRB clears. With it off, nothing Slang-related is compiled or linked, and the backend selection option rejects `slang` with a clear message.
- `find_package(slang CONFIG)`: the deps prefix has `lib/cmake/slang/slangConfig.cmake`.
- Slang ships only as shared libraries: `libslang.so` and `libslang-compiler.so`, plus `libslang-glsl-module-<ver>.so`, which is what `-allow-glsl` loads at runtime, and `libslang-glslang-<ver>.so`, which only the `-emit-spirv-via-glsl` path uses. Confirm which are loaded when compiling GLSL to SPIR-V directly, and make sure packaging carries exactly those; that list is also what OSRB is being asked about.

## 6c: the Slang backend

Uses only API confirmed present in the installed `slang.h`: `createGlobalSession`, `loadModuleFromSourceString`, `findAndCheckEntryPoint`, `createCompositeComponentType`, `getEntryPointCode` / `getTargetCode`, session options `AllowGLSL`, `GLSLForceScalarLayout`, `PreserveParameters`, `VulkanUseEntryPointName`, `defaultMatrixLayoutMode`, and a `fileSystem` in the session description.

Settings, all from PR 0b:
- Target SPIR-V 1.6 for Vulkan 1.3, emitted directly.
- `AllowGLSL`; `GLSLForceScalarLayout`, since the library declares uniform blocks `std430`.
- **`SLANG_MATRIX_LAYOUT_ROW_MAJOR`.** Slang names matrix layouts the other way round from GLSL; column-major put every `mat3x2` and `dmat3x2` at the wrong stride. Row-major matched glslang exactly.
- Entry point by name and stage from `StageSource`. Not always `main`: `SMAAEdgeDetection.frag` is built three times with three entry points.

Sessions: one global session per `ShaderManager`, created once (it is expensive), and a session per material compile so nothing leaks between materials. `ShaderManager` has no locking, so the plan assumes it is used from one thread at a time; confirm that before relying on it, since a shared Slang session is not safe to use concurrently.

Includes: an `ISlangFileSystem` over `Library`, serving each item's code the way `GlslangIncluder` does. The assembled source names its includes rather than containing them, so without this most functions are undefined. `Library` has already stripped each item's header blocks, and the builder emits the transitive include set once, so include-once behaviour comes for free; PR 0b's first attempt duplicated every nested include by reading the raw files instead.

Errors map onto `CompileResults` the way glslang's do: per stage where Slang attributes them, `material_error` otherwise, so `LinkFailureInLaterStageTest`'s guarantee holds for both backends. Slang's warnings are logged, not dropped: the double-literal narrowing was only a warning.

## 6d: reflection from Slang's SPIR-V

The redecorator has to produce the same `ShaderReflection` from either backend, because `Material` and the Vega property writers look things up by name. PR 0b's parity pass found exactly these differences, once the matrix layout is right:

| Slang does | Effect on reflection | Handling |
|---|---|---|
| Names a block's type `SLANG_ParameterGroup_<name>_std430` | Uniform and storage buffers are recorded under their type name, so every lookup misses | Strip it in the redecorator when the SPIR-V came from Slang |
| Names stage outputs `entryPointParam_<entry>.<name>` | Fragment output reflection keys on location, not name, so likely harmless; confirm | Strip it alongside, for logging and artifacts |
| Wraps an array member of a block in a struct with a `data` member | `tiles.x` becomes `tiles.data.x`; offsets and strides are identical | Unwrap in `reflect_buffers` |
| Drops resources the entry point does not use | Missing from the reflection | See below |
| Numbers auto-assigned bindings per stage | Different binding numbers | Not a reflection problem; see 6g |

**Dropped resources need a decision.** glslang keeps every declared resource; `IoMapResolver` forces liveness precisely so that resources a stage never reads still get a binding and still appear in the reflection. Slang drops them, and the CLI's `-preserve-params` did not change that in PR 0b. That case was the `texelFetch` miscompile, though, so try `PreserveParameters` again on a correct shader before concluding. If it does not work, every caller that names a resource has to tolerate its absence: `buildSpirv`'s `CHECK` on `external_uniform_buffer_names_` aborts the process, and `validate_push_constant_range` throws. Decide which is right before opting in a shader that declares something it does not use.

Tests: run ShaderCompilerTest's `FromFile` cases through both backends and compare the reflection. The `.reflect` goldens are exactly the format to diff, and today's parity script becomes a test rather than a spike.

## 6e: selecting the backend per material

- A runtime option, `shader-compiler-backend = glslang | slang`, default `glslang`, settable from the server config and from tests.
- A per-item opt-in in the shader manifests, for example `"slang": true` next to `"class"`. Reviewable as data, and greppable.
- A material compiles through Slang only if the option says `slang` **and** every stage in it has opted in. Anything else falls back to glslang silently. A mixed material is never split across backends, because cross-stage I/O and bindings have to come from one compiler.
- Artifacts record which backend produced them, so a comparison never mixes the two up.

## 6f: first family, the single-stage compute shaders

Single-stage materials sidestep the cross-stage binding question entirely, which is why they go first. Candidates, from PR 0b: `ppllDebugVisualizer`, `ppllStats`, `ppllStatsTiled`, `ppllStatsTiledBatches_Stage1/2`, `accumulatorTx_findExtents`, `accumulatorTx_findStdDev`, `clearQueryOutputBuffer`, `MSMultiGpuComposite`. Most matched glslang's reflection exactly; the `ppllStatsTiled*` ones differ only by the `.data` wrapping 6d removes. Not `geoCount_*`, which use buffer device addresses, and not `multiGpuCompositeIDs`, which nothing here can exercise without a second GPU.

Proof, all three required:
1. ShaderCompilerTest: the reflection from Slang equals the golden.
2. A reflection diff of every opted-in shader against glslang's, as in PR 0b, with no differences other than the documented ones.
3. be-render-tests with the option on, against the same golden images. Its PPLL and accumulation groups exercise all of these except `MSMultiGpuComposite`, which only the render suite reaches.

## 6g: cross-stage bindings (spike first)

glslang assigns a material's auto bindings with one resolver shared across its stages, so a resource declared in two stages gets one binding and different resources get different ones. Slang given one stage at a time numbers each from 0. PR 0b saw exactly that in the SMAA passes, and the redecorator's clash check would reject the material.

Composing every stage's entry point into one Slang program is the obvious fix, but each stage is its own source with its own `main`, so each is its own module, and Slang treats a same-named global in two modules as two parameters. That reproduces glslang's sharing only if the shared resources are explicitly bound, which the library mostly does already: every explicit binding matched in PR 0b. So the spike should find out, on the SMAA and accumulator materials:
- whether a composite of per-stage modules gives distinct auto bindings across stages;
- whether a resource declared in two stages without an explicit binding ever occurs in the families in scope. If not, the answer may simply be to require explicit bindings for cross-stage resources in Slang-routed families, which the clash check would enforce.

This decides 6h's design, and is the same question PR 7 has to answer for everything, so write the answer up for PR 7 as well.

## 6h: second family, post-processing

`SMAAPassThru.vert`, `fullScreenTriangle.vert`, the three SMAA passes, `accumulatorIDPass.frag`, `accumulatorScale_2ndPass.frag`, once 6g says how. Excluded: **`SeparateMultiSample.frag`**, which Slang miscompiles (`texelFetch` on `sampler2DMS` drops the sample index), until that is fixed upstream; and `accumulationCompositePeer.frag` until its `coherent` image atomic has had a memory-model review.

## Verification for every step

- The render suite, ShaderCompilerTest goldens not moving for glslang.
- be-render-tests twice, with the option off and on. Same 441 passing either way; the two `subquery_hittest_tests` failures are the suite's local branch, not shaders.
- For opted-in shaders, a reflection diff against glslang. Compiling clean is not evidence on its own: the `texelFetch` miscompile compiled clean.
- Generated Mark shaders are not byte-stable between runs (property order varies), so compare meaning, not bytes, wherever Marks are involved.

## Risks and open questions

- **OSRB.** Development only until it clears, so `ENABLE_SLANG` stays off by default and nothing is packaged.
- **Dropped resources** (6d) and **cross-stage bindings** (6g) are the two places PR 6 can turn out larger than this plan. Both are spiked before anything depends on them.
- **Compile time.** Slang is not obviously as fast as glslang for small shaders, and materials are compiled at render time. Measure with `GENERATE_COMPILE_STATS` on the opted-in families before 6f lands.
- **Slang version.** The deps build pins Slang to the `vulkan-sdk-1.4.363` tag (2026.17). Any upstream fix for the `texelFetch` bug arrives with a future SDK, not independently.
- **The two silent miscompiles found so far were found by reading output, not by tests.** Expect more; the per-family proof is the defence.

## Order

6a and 6b are independent and can land in either order. 6c needs 6b. 6d and 6e need 6c. 6f needs both. 6g can start any time after 6c and gates 6h.
