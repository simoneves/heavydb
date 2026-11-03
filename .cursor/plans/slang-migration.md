---
name: Slang migration
overview: Replace glslang with Slang as the renderer's shader compiler, in five PRs. The renderer's shaders stay GLSL, compiled through Slang's GLSL support; every material compiles through Slang, glslang leaves the code and the deps, and the reflection the renderer binds by is unchanged. Written retrospectively, to match the final code and the rebuilt PR sequence. The development history, with its plans as they evolved, is on the original branch, simoneves_renderer_upgrade_2026.
todos:
  - id: pr1-fixes
    content: "PR 1: Compiler-independent fixes, on master: the wind barb quantizeDirection bugs, deterministic shader assembly, descriptor pool sizing, readonly vertex-stage buffers, a dead shader, GLSL source fixes. Tested on master's deps. Pairs with the be-render-tests wind barb tests"
    status: pending
  - id: pr2-deps
    content: "PR 2: Vulkan SDK 1.4.363 deps, with Slang built as a static library alongside glslang, and the shader compiler tests moved from byte-exact SPIR-V to reflection goldens first. Its deps container, with both compilers, tests PR 2 to PR 5"
    status: pending
  - id: pr3-groundwork
    content: "PR 3: Shader compiler groundwork, still on glslang: SPIR-V 1.6, textual subroutines, one compile per material, glslang's resolver with the redecorator as a reader, array and push constant reflection, a backend interface, compiler-neutral shader changes, and push constant ranges derived from reflection"
    status: pending
  - id: pr4-slang
    content: "PR 4: Compile every shader with Slang and remove glslang: the Slang backend and reflection (Slang required), every shader converted and the switch made, glslang removed from the code, then from the deps"
    status: pending
  - id: pr5-performance
    content: "PR 5: Compile performance and diagnostics: an in-memory material cache, pipeline creation timing, and a harness comparing glslang and Slang on captured shaders"
    status: pending
isProject: false
---

# Slang migration

The renderer compiled its shaders with glslang, through a wrapper that reached into glslang's private headers, its AST and its I/O mapper. This replaces glslang with Slang. The shaders stay GLSL, compiled through Slang's GLSL support; the parts of GLSL Slang does not model are written in Slang's own forms, inside the same GLSL sources. Everything the renderer sees, its `ShaderReflection`, is unchanged, and the be-render-tests images with it.

Rewriting the shaders in Slang's own idioms, modules and generics in place of the Builder's text splicing, is not part of this; see "Deferred" below.

## The PRs

Five PRs, in order. PR 1 is based on master and independent of the rest; PRs 2 to 5 form a chain.

| PR | What | Builds with |
|---|---|---|
| 1 | Compiler-independent fixes | master's deps |
| 2 | Vulkan SDK 1.4.363 deps, with static Slang alongside glslang | PR 2's deps |
| 3 | Shader compiler groundwork, on glslang | PR 2's deps |
| 4 | Every shader through Slang; glslang removed | PR 2's deps, then the final deps |
| 5 | Compile performance and diagnostics | the final deps |

**Deps containers.** PR 2's end state builds the 1.4.363 SDK with both glslang and a static Slang; a container from it builds and tests everything up to the last commit of PR 4. That commit stops the deps building glslang, and a container from it, with Slang only, is the final one.

### PR 1 - Compiler-independent fixes

Fixes that need nothing from the rest, each a commit of its own:

- **The wind barb Mark's quantizeDirection.** Two bugs in one property, a `BoolRenderProperty`, which is typed INT: its default was initialized with `true`, stored as a bool under the INT label, so a Vega leaving the property out failed to render; and a constant direction read it with `getUniformValue<float>()`, whose type CHECK aborted the server whatever its value. The default is `1` and the read is an int. The be-render-tests branch `seves/windbarb_tests` adds the first wind barb render tests, which found the second bug.
- **Deterministic shader assembly.** The Marks generate shader source by iterating sets of their render properties: type #defines, uniform block members, vertex attributes, getters. The sets were unordered sets of pointers, so the order followed where the properties were allocated, and the same material was assembled differently, sometimes into different SPIR-V, on every run, which defeated the driver's shader cache. The sets now iterate in insertion order (`InsertionOrderedSet`), which follows the marks' property vectors. The set never dereferences its pointers: the buffer state can hold pointers to row id properties already destroyed, which it later erases by pointer, so an ordering that read through them would be a use-after-free. The projection policy's loop iterates its by-name index.
- **Descriptor pool sizing.** A material's pool counted one descriptor per binding of each type, not one per array element.
- **Read-only vertex-stage storage buffers declared `readonly`.** Without `vertexPipelineStoresAndAtomics`, a vertex stage's storage buffers must be NonWritable.
- **The dead `accumulationComposite.frag`** and its manifest entry.
- **GLSL source fixes**: `LF` suffixes on double constants, cook_torrance's `kS` declared as the scalar it is, the core `gl_DrawID` rather than `gl_DrawIDARB`.

### PR 2 - Vulkan SDK 1.4.363 deps, with static Slang

1. **Reflection goldens instead of byte-exact SPIR-V.** The shader compiler tests compared SPIR-V byte for byte, which only ever says that the bytes changed, so every toolchain update meant regenerating them blind. They now check that the SPIR-V passes `spirv-val` under the renderer's layout rules, and that its reflection matches a text golden (`.reflect`, `ShaderReflection::serialize`), so a diff shows which binding, offset or location moved. Done first, so the SDK update moves no golden.
2. **The 1.4.363 SDK build script**, forked from LunarG's into `ThirdParty/vulkan`, with its modifications recorded in its header: no WSI in the loader and layers; the loader installed where `FindVulkan` looks; Slang built as one self-contained static library, `libslang-compiler.a`, without its debug info and position-independent, its shared libraries left out. `install_vulkan` builds the loader, glslang, SPIRV-Cross, the utility libraries, the layers and Slang.
3. **Update for glslang 16.6.0.**
4. **Update for the 1.4.363 validation layers**, which a Debug build's tests fail on: the best practices suppressions in the debug callback match by name, since the layers renamed both messages and so changed their ID numbers; `bdaTest.comp`'s 64-bit buffer references are 8-byte aligned; and `pointTemplate.mesh` composes its colour before writing it, rather than reading back a mesh output.

### PR 3 - Shader compiler groundwork, on glslang

Everything here either survives into the final code or is a stage the rest builds on.

- **Vestigial code removed**, and latent defects found with it fixed.
- **SPIR-V 1.6**, with the instance and device Vulkan versions decided separately. The device requires and enables `shaderDemoteToHelperInvocation` and `maintenance4`, both required by Vulkan 1.3, since glslang then compiles `discard` to demote and gives mesh and task shaders a `LocalSizeId`.
- **Disassembly through SPIRV-Tools**, not glslang's private header.
- **Subroutines resolved textually.** The Builder's subroutine and function-call rebinding becomes a pass over the assembled source, resolving targets through the Library, and `TShaderIRUtils`, which edited glslang's AST, is deleted. All bindings apply in one pass, so they cannot chain, and a prototype is not a target.
- **One compile per material**: every stage assembled, then compiled together.
- **glslang's resolver assigns bindings and locations.** `IoMapResolver`'s sentinels and the redecorator's allocation and SPIR-V patching go; `ShaderRedecorator` becomes a reader of SPIR-V into `ShaderReflection` with a clash check. It also fixes an array of resources getting a binding per element.
- **Array dimensions reflected** (`array_length`, `array_stride`), a multi-dimensional member flattened to element count and stride, as `PPLLRender` reads it.
- **Push constants reflected** (`push_constants`, `push_constant_attrs`, both a block's names, its span) and every push checked against them, accepting a push that any stage covers.
- **`createCache` covered** by a test.
- **A `ShaderCompilerBackend` interface** in front of glslang.
- **Compiler-neutral shader changes**: interface block locations on the block where that changes nothing, `coherent` dropped where unneeded, the test shaders' `imageSize` uniform renamed `viewportSize`.
- **Each material's compile time logged** at debug level 1, with a hash of its source.
- **Push constant ranges derived from reflection.** `Material::getPushConstantRanges` gives each stage that declares a block one range over its span, and every pipeline's layout takes its ranges from it; the hand-written ranges, and the parameters that carried them, are gone. First, the PPLL composite pipeline gets its tile index pushed itself: it had read the capture pipeline's, through a shared hand-written range set, and its second pass, after a flush, read none.

### PR 4 - Every shader through Slang

**D1. The Slang backend, with Slang required.** glslang stays the compiler, so nothing changes yet.

- `FindSlang` accepts only the static `libslang-compiler.a`. Slang is required wherever rendering is built; there is no option to leave it out.
- `SlangBackend` compiles a material as one compile request: a GLSL translation unit and an entry point per stage, so that bindings are laid out across the material. A compile request rather than the module API, which loses GLSL's `local_size`. Its settings: SPIR-V 1.6 emitted directly, entry point names kept, scalar block layout, row-major matrices (Slang's row-major is GLSL's column-major), no optimization (which would need a separate shared library), unformatted images given format Unknown rather than one guessed from the element type, warning 39001 (an explicit binding shared between stages, which is how the library shares resources) off. Includes are served from the Library, angle-bracket ones too. Slang's GLSL module is compiled once per backend.
- **Reflection** (`SlangReflection`) is built from Slang's program layout, which keeps the resources the SPIR-V drops as unused, and checked against `ShaderRedecorator`'s reading of each stage's SPIR-V: everything the SPIR-V has must be in the layout's reflection, identically. Where the SPIR-V has them it supplies what Slang's reflection API gets wrong: a used buffer block's size and member layout (the API lays out with scalar alignment) and push constant member offsets (it ignores `[[vk::offset]]`). Specialization constants are skipped; a rasterizer-ordered texture is a storage image; a resource shared between stages without an explicit binding is refused, since Slang gives each stage its own. `ShaderRedecorator` learns Slang's names: `SLANG_ParameterGroup_` and the layout suffixes stripped, `_Array_` wrappers unwrapped.
- **A cross-stage varying check.** Slang compiles each stage on its own and gives an unlocated varying a location by declaration order, so two stages could disagree silently. Each stage's inputs are matched, from the SPIR-V, against the previous stage's outputs by location and type (a wider output vector allowed), built-ins and per-vertex arrays excepted.
- **Tests**: the backend, its reflection against the cases above, a later stage's explicit binding kept clear of an earlier stage's assigned one, sampled integer image formats. The shaders written inline in the tests get `.reflect` goldens written from glslang, which Slang is held to after glslang has gone.

**D2. Every shader converted, and Slang the compiler.** In one commit, since the shaders' final forms compile only with Slang.

- **Buffer device addresses**: a reference type becomes a struct holding a pointer, with an `__init` from the address, so `Type(address).member[i]` reads as before; the slab reads go through `SLAB_LOAD`.
- **Geometry shaders** keep their GLSL bodies: `Slang/slangGeometry.glsl` declares the inputs and outputs as the globals the body uses and maps `EmitVertex` and `EndPrimitive` onto Slang's stream; `GlslStructBuilder::createSlangStructString` writes the structs.
- **Mesh and task shaders** likewise, through `Slang/slangMesh.glsl`.
- **Ray tracing** through Slang's `TraceRay` (`SLANG_TRACE_RAY`), its payload passed by variable.
- **Others**: the pixel interlock image as a globally coherent rasterizer-ordered texture; `SeparateMultiSample`'s sample read through `Load`; push constant offsets with `[[vk::offset]]`.
- **The switch**: every material compiles through Slang, which `ShaderManager` holds as its one backend.
- **Tests**: the FromFile cases compile through Slang and must be SPIR-V 1.6; a varying with no matching output is refused; a later stage's failure fails the material and names that stage; the shaders nothing else reaches are compiled.

**D3. glslang removed**: `GlslangWrapper`, `ResourceLimits`, glslang's find module (replaced by `FindSpirvTools`, for validation, disassembly and the optimizer), and the glslang halves of the tests. Regenerating every golden from Slang changes none.

**D4. The deps stop building glslang**: `install_vulkan` builds SPIRV-Tools without it and no longer copies glslang's private headers, and the validation layers are built without it (they need it only for their tests). A container from here, with Slang only, is the final one.

### PR 5 - Compile performance and diagnostics

- **An in-memory material cache** in `SlangBackend`, keyed by everything that decides a material's output (each stage's source, entry point, stage and name, in full rather than hashed), holding up to 512 materials, least recently used dropped. A backend's Library never changes, so it cannot go stale.
- **Pipeline creation timed** at debug level 1, a compute pipeline with its specialization constants.
- **`compare_compilers.py`**, comparing a standalone glslang with `slangc` on shaders captured from a server run, on compile time, `spirv-val` and reflection; and **`SpirvReflect`**, printing any module's reflection in the `.reflect` format.

## Verification

At the end of each PR: both builds, the render suite on both, and be-render-tests on both, the Debug build's validation layers reporting nothing. A Release build does not run them, and Debug runs have found what no Release run did: on the original branch, the demote feature, the NonWritable rule, the descriptor pool and image formats; on PR 2, the three problems its fourth commit fixes; on PR 3, the two device features SPIR-V 1.6 needs.

Goldens move only where expected: their creation in PR 2, the resolver, array dimension and push constant commits in PR 3. At every other boundary they must not.

The rebuilt branch's final tree must equal the original branch's, apart from `.cursor/plans`, comment rewording, formatting left as master has it, and PR 2's validation layer fixes.

Final state, on Release and Debug: the render suite passes 96 of 96, be-render-tests 447 (with the wind barb tests), failing only the two known `subquery_hittest_tests_7` and `_8`; Debug reports no validation messages; two separate runs assemble byte-identical shaders.

## Compile cost

Slang compiles a material in about 47 ms on average, nearly all of it inside Slang's front end; glslang took about a thirteenth of that. On a be-render-tests run:

| | Slang compile | Driver pipeline creation (NVIDIA disk cache warm) |
|---|---|---|
| Slang, no cache | 30.8 s | 4.0 s |
| with the material cache (PR 5) | 21.4 s | 4.0 s |
| with deterministic assembly (PR 1) | 18.0 s | 0.09 s |

The deterministic assembly helps both: repeated materials now hit the material cache, and every pipeline hits the driver's shader cache, which a material assembled differently on every run had missed.

## Deferred

- **Shaders as Slang modules and generics** (the old PR 8). Two spikes, kept on branch `simoneves_pr8_spike`, found that moving shared code into modules does not reduce compile time, since the cost is checking each material's own body, but that a whole template as a module, with each material reduced to its declarations and glue, roughly halves it: the point Mark's vertex stage took 7 to 8 ms against 17, once its module was loaded. It would need `SlangBackend` to keep one session across materials, interfaces designed for every template, and the Builder generating glue rather than splicing. Worth doing if first-render latency or the Builder's complexity becomes the problem.
- **A file cache of compiled materials**, for compiles across restarts.
- **A `VkPipelineCache`**, only where NVIDIA's own disk cache is unavailable, as in a container without a persistent, writable home directory.
- **Upstream Slang reports**: `traceRayEXT`'s internal error, `texelFetch` on a multisampled image dropping the sample index, the module API losing `local_size`, `PreserveParameters` not keeping unused GLSL resources, scalar layout of GLSL buffer blocks in reflection, an inheritance internal error, guessed formats for sampled integer images, and the NonWritable analysis.
- **The GfxWSI sandboxes'** Slang changes, built only with a display, have not been compiled.
- **OSRB approval** for Slang.
