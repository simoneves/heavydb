---
name: vulkan sdk 1.4.363 fork
overview: Fork the LunarG 1.4.363.0 vulkansdk build script with our WSI and loader-path modifications, add Slang to the build, and update common-functions.sh so the PR 0 spike can attempt the SDK bump. Done; the spike succeeded. Slang was left out of the target list when this landed and restored afterwards; see the status note.
todos:
  - id: fork-script
    content: Create ThirdParty/vulkan/vulkansdk-1.4.363.0 from the upstream 1.4.363 script with corrected provenance header, the three WSI flags re-applied to build_loader and build_validation_layers, and build_loader's --install-prefix reverted to $ARCHDIR
    status: completed
  - id: common-functions
    content: "Update install_vulkan in scripts/common-functions.sh: bump VULKAN_VERSION to 1.4.363.0, replace the pin comment, add slang to the target list, and add mkdir -p for the glslang private-header destination directories. As landed (27c0a246a1, Add Vulkan 1.4.363.0 build script fork, and adapt install_vulkan) the target list did not include slang; it was restored later by a separate change"
    status: completed
  - id: licenses
    content: "Split in two. The Vulkan SDK licence summary never needed OSRB and is tracked by the sdk-licence item in pr-895-review-followups.md. The Slang licence file and index entries remain deferred pending OSRB"
    status: pending
  - id: run-spike
    content: "Hand off to the user to run the deps build and the heavydb configure/build, since the agent cannot build here; they report back the FindGlslang and private-header failures verbatim as input to PRs 3, 4, and 5. Outcome: no FindGlslang failure and no private-header failure; two small adaptation commits were enough (4c7d873623, Update for glslang 16.6.0; 548b87e64d, Update SPV comparison files)"
    status: completed
isProject: false
---

# Vulkan SDK 1.4.363.0 fork with Slang, to enable the PR 0 spike

Scope: get a buildable 1.4.363.0 dependency tree with Slang installed, so the PR 0 spike in [.cursor/plans/glslang-to-slang-migration.md](.cursor/plans/glslang-to-slang-migration.md) can collect real compiler and CMake errors. This deliberately does not fix any of the errors it will surface.

Division of labour: the agent writes the forked script and the `common-functions.sh` changes (Tasks 1 and 2). The agent cannot build in this environment, so the user runs the deps build and the heavydb configure/build (Task 4) and reports the diagnostics back.

Status: Tasks 1, 2 and 4 are done. Task 3 is split: the SDK summary is tracked in [pr-895-review-followups.md](pr-895-review-followups.md), and the Slang attribution remains deferred until Slang has OSRB approval.

Slang history: Task 2 below says to add `slang` to the target list, but the commit that landed (`27c0a246a1`, Add Vulkan 1.4.363.0 build script fork, and adapt install_vulkan) left it out, so Slang was not built for the whole of PR 895's original review. It was restored afterwards by re-adding `slang` to the `./vulkansdk` line. Read Task 2 as the intent, not as what first landed.

Commit hashes here are as of the rebase onto `master` that preceded the PR 895 review. They change on every rebase; find a commit by its subject, which is given alongside each hash.

## Verified: the existing fork's only functional modification is the WSI flags

Upstream 1.3.275.0 is no longer downloadable, so the fork was compared against upstream 1.3.296.0 at `/tmp/vulkansdk-1.3.296.0/vulkansdk`. Upstream `build_loader` (lines 111-123) and `build_validation_layers` (lines 150-168) carry no WSI flags, while our fork adds three to each:

```127:129:ThirdParty/vulkan/vulkansdk-1.3.275.0
          -DBUILD_WSI_XCB_SUPPORT=OFF \
          -DBUILD_WSI_XLIB_SUPPORT=OFF \
          -DBUILD_WSI_WAYLAND_SUPPORT=OFF
```

This confirms the fork header's claim. Remaining differences between the fork and 1.3.296 are upstream drift across 275 to 296, not local changes.

## Verified: Slang is already a first-class SDK target in 1.4.363

No new build function is needed. Upstream `/tmp/vulkansdk-1.4.363.0/vulkansdk` already has:

- `build_slang()` at lines 460-483, cloning `https://github.com/shader-slang/slang.git` at ref `vulkan-sdk-1.4.363`, configured with `-GNinja`, `-DSLANG_SLANG_LLVM_FLAVOR="DISABLE"`, tests and examples off, and `-DCMAKE_INSTALL_INCLUDEDIR="${INCLUDEDIR}/slang/"`
- `SLANG_DIR` (line 628), `BUILD_SLANG` (line 674), inclusion in `build_all` (line 700), the `Slang | slang` argument case (lines 857-860), and dispatch at line 897
- `[slang]` advertised in usage (line 37)

So enabling Slang is purely a matter of adding `slang` to our invocation in `install_vulkan`.

## The change that would otherwise break the build silently

1.4.363 moved the loader out of the SDK root:

```145:145:/tmp/vulkansdk-1.4.363.0/vulkansdk
    --install-prefix "${LIBDIR}/VulkanLoader/"
```

Combined with `-DCMAKE_INSTALL_LIBDIR="lib"`, `libvulkan.so` lands at `x86_64/lib/VulkanLoader/lib/`. After `install_vulkan`'s `rsync -av ${VULKAN_VERSION}/${ARCH}/* ${PREFIX}` it would sit at `$PREFIX/lib/VulkanLoader/lib/libvulkan.so`, but [cmake/Modules/FindVulkan.cmake](cmake/Modules/FindVulkan.cmake) does a non-recursive lookup:

```57:58:cmake/Modules/FindVulkan.cmake
    find_library(VULKAN_LIBRARY NAMES vulkan HINTS
        "$ENV{VULKAN_SDK}/lib")
```

`find_library` does not recurse, so Vulkan would silently fail to be found. Fix in the fork by reverting that one line to `--install-prefix "$ARCHDIR"`, matching 1.3.x behaviour. This is safe because the other consumers of `${LIBDIR}/VulkanLoader/` (`build_tools`, `build_lunarg_tools`, `build_vulkan_profiles`, `build_vulkancapsviewer`) are not in our target list, and 1.4.363's `build_validation_layers` no longer references the loader at all.

## Task 1 - Create ThirdParty/vulkan/vulkansdk-1.4.363.0 (done)

Copied `/tmp/vulkansdk-1.4.363.0/vulkansdk` verbatim with the executable bit matched to the old fork, then applied exactly three modifications. `diff` against upstream confirms nothing else changed, and `sh -n` passes.

1. Add the fork-provenance header comment, modelled on the existing one but corrected. The current fork cites `ThirdParty/licenses/vulkan-sdk.txt`, which does not exist; the real file is `ThirdParty/licenses/Vulkan_SDK__vulkan-1.3.275.0-linux-license-summary.txt`. Point at the new 1.4.363.0 summary instead, and document both modifications below.
2. Add the three `-DBUILD_WSI_*_SUPPORT=OFF` flags to `build_loader` (after line 144) and `build_validation_layers` (after line 178).
3. Change `build_loader`'s `--install-prefix` from `"${LIBDIR}/VulkanLoader/"` to `"$ARCHDIR"`.

Leave `build_slang` untouched. Trimming it (for example disabling gfx to cut build time) would require also removing the `cp` of `gfx.slang` / `slang.slang` and the `mv` of `slang-gfx.h`, which run under `set -eu`. Not worth the risk for a spike.

For reference, other notable upstream deltas from 1.3.275 that come along for free: glslang is now built twice (shared then static) with `-DGLSLANG_TESTS=OFF` and `-DCMAKE_PREFIX_PATH` replacing `-Dspirv-tools_SOURCE_DIR`; robin-hood-hashing is gone; `yaml-cpp` and `CrashDiagnosticLayer` are new; `--keep-going` plus a `--internal-run-step` re-exec harness was added; `CMAKE_INSTALL_LIBDIR="lib"` is set throughout; and `clean_nonsdk_files` no longer deletes any glslang headers.

## Task 2 - Update scripts/common-functions.sh (done)

In `install_vulkan`:

- Set `VULKAN_VERSION=1.4.363.0` and replace the pin comment above it with a note that the fork adds Slang and that glslang private headers are still copied pending PRs 3 and 4 of the migration plan. (As landed, the pin comment was simply removed.)
- Add `slang` to the target list: `./vulkansdk --maxjobs --skip-deps loader glslang spirvcross vul layers slang`. (Not in the commit that landed; restored later, see the status note.)
- Keep all the glslang private-header copies for now. (There were 16, counting `build_info.h`, which the 15 below leave out because it is copied from the build tree. The branch has since cut them to 14 in `acbd16b1f6`, Drop two glslang private-header dependencies, which removed `build_info.h` and `SPIRV/disassemble.h`, and to 13 in `c16ae41c47`, Resolve shader subroutines textually, delete TShaderIRUtils, which removed `InfoSink.h`.) The spike needs the tree to build far enough to reach our own compiler errors, and `TShaderIRUtils` plus `IoMapResolver` still require them. Add `mkdir -p` for `${ARCH}/include/glslang/Include`, `${ARCH}/include/glslang/MachineIndependent`, and `${ARCH}/include/glslang/SPIRV` before the copies, because glslang 14+ tightened which headers it installs and the destination directories may no longer exist. The `build_info.h` and `SPIRV/disassemble.h` copies are now redundant (1.4.363 stopped deleting them) but harmless; leave them so the diff stays minimal.

Ninja is required by `build_slang`. `install_ninja` runs at `scripts/mapd-deps-ubuntu.sh:170` and `scripts/mapd-deps-rockylinux.sh:184`, both well before `install_vulkan` (line 243 in the Ubuntu script), so it will be present in `$PREFIX/bin`. Confirm `$PREFIX/bin` is on `PATH` at that point in the deps build.

## Task 3 - Licenses and attribution (deferred, pending OSRB approval)

Deliberately not done in this change. Shipping Slang needs OSRB approval first, so attribution is a
separate piece of work once that clears. `ThirdParty/licenses/` is untouched and the fork's header
comment carries a `@TODO` in place of a file reference, rather than citing a file that does not
exist (the mistake the 1.3.275.0 fork made with `vulkan-sdk.txt`).

Findings worth keeping for when this is picked up:

- LunarG has no published summary for 1.4.363.0. Probing `https://vulkan.lunarg.com/software/license/vulkan-<version>-linux-license-summary.txt` shows 1.4.350.0 is the newest that resolves; 1.4.354.0 through 1.4.363.1 all 404. Re-probe before assuming it is still missing.
- The bundle's own `LICENSE.txt` is a five-line disclaimer pointing back at that registry, not the component breakdown the existing 1.3.275.0 file contains, so it is not a substitute.
- Slang needs its own entry regardless of the SDK summary: `grep -i slang` against the 1.4.350.0 summary matches only glslang, so LunarG does not attribute Slang at all.
- Slang's license is Apache-2.0 WITH LLVM-exception, at `https://raw.githubusercontent.com/shader-slang/slang/vulkan-sdk-1.4.363/LICENSE` (byte-identical to `master` at the time of checking). The local naming convention is `<name>__<upstream filename>`, so `slang__LICENSE` with no extension.
- ~~The 1.3.275.0 summary and the `vulkansdk-1.3.275.0` fork are both retained, so reverting the spike is a one-line change to `VULKAN_VERSION`.~~ No longer true: the fork was deleted in `7092b5cf79` (Remove old Vulkan SDK build script), so reverting now needs that commit reverted too. The 1.3.275.0 summary is still in `ThirdParty/licenses/` and goes when the 1.4.363.0 one arrives.

## Task 4 - Run the spike (user-executed, done)

Outcome: neither of the failures predicted below happened. `install_vulkan` completed, and `find_package(Glslang)` succeeded unchanged against glslang 16.6.0, which still installs `libMachineIndependent.a`, `libGenericCodeGen.a` and `libOSDependent.a` alongside `libglslang`. Two small adaptation commits were all it took: `4c7d873623` (Update for glslang 16.6.0) and `548b87e64d` (Update SPV comparison files). The rest of this section is the plan as written beforehand.

The agent cannot build in this environment, so this task is a handoff. Tasks 1 through 3 produce the tree; the user runs the build and reports back.

What the user runs:

1. The deps build that invokes `install_vulkan` (`scripts/mapd-deps-ubuntu.sh` or the rockylinux equivalent), or `install_vulkan` on its own if that is practical.
2. A heavydb configure and build against the resulting prefix.

What to capture and report back, verbatim rather than summarised, since the exact diagnostics are what size PRs 3, 4, and 5:

- Any failure inside `install_vulkan` itself, especially from the glslang private-header copy block. That is the most likely first failure and the most informative (see Open risk below).
- The `find_package(Glslang REQUIRED)` failure at [GfxDriver/CMakeLists.txt](GfxDriver/CMakeLists.txt) line 113. Expected, because [cmake/Modules/FindGlslang.cmake](cmake/Modules/FindGlslang.cmake) hard-requires `MachineIndependent`, `GenericCodeGen`, and `OSDependent` as separate libraries in its `REQUIRED_VARS`, and glslang 14 consolidated them into one `glslang` target.
- Compile errors from [GfxDriver/ShaderCompiler/TShaderIRUtils.cpp](GfxDriver/ShaderCompiler/TShaderIRUtils.cpp) and [GfxDriver/ShaderCompiler/GlslangWrapper.cpp](GfxDriver/ShaderCompiler/GlslangWrapper.cpp) caused by private-header API drift. Getting this far requires working past the `find_package` failure first, so it may take a second pass.

Two quick checks that confirm the Slang half of the spike is unblocked:

- `slangc --version` runs from `$PREFIX/bin`
- `$PREFIX/include/slang/slang.h` exists

Those are the precondition for the migration plan's second spike, compiling the shader sources through Slang's GLSL compatibility mode.

The agent will interpret whatever comes back and fold it into PRs 3, 4, and 5.

## Retired risk: the glslang private headers are all still there

The original concern was whether the 15 glslang private headers still exist at the source paths the copies read from. All 15 were checked against the `vulkan-sdk-1.4.363` tag of `KhronosGroup/glslang` and every one returns HTTP 200:

- `glslang/Include/`: `InfoSink.h`, `intermediate.h`, `Common.h`, `arrays.h`, `BaseTypes.h`, `Types.h`, `PoolAlloc.h`, `SpirvIntrinsics.h`, `ConstantUnion.h`
- `glslang/MachineIndependent/`: `iomapper.h`, `gl_types.h`, `LiveTraverser.h`, `localintermediate.h`, `reflection.h`
- `SPIRV/disassemble.h`

So the copy block should not fail on missing sources. The `mkdir -p` lines guard the remaining exposure, which is that glslang 14+ may no longer install the destination directories. That means "glslang changes" in the old pin comment most likely referred to the library consolidation that breaks [cmake/Modules/FindGlslang.cmake](cmake/Modules/FindGlslang.cmake), not to the headers moving, which brings PR 3 forward as the real blocker.
