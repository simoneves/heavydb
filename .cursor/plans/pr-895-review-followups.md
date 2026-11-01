---
name: PR 895 review follow-ups
overview: Act on the review of PR 895 (branch simoneves_renderer_upgrade_2026 at 29f3f9c88b). Three code fixes with tests, three smaller code cleanups, a full reconciliation of the existing plan files with what the branch actually contains, the SDK licence update that no longer waits on OSRB, and a proposed split of the PR into four. Slang is being re-added to the install_vulkan target list by the user, which unblocks PR 0b, but PR 0b itself is out of scope here.
todos:
  - id: slang-rebuild
    content: "User: re-add slang to the install_vulkan target list and provide a build container with the rebuilt deps prefix. Gates the verification steps of every other item, and makes PR 0b possible afterwards Done: container laughing_noyce has slangc and include/slang in the prefix. The script change landed as 61bc82fc65 (Add Slang build)."
    status: completed
  - id: fix-lost-link-error
    content: "Fix 1: a link or getIntermediate failure in stage i > 0 of glslToSpirv surfaces as a generic error on stage 0, losing the real message. Make every early return carry the failure to the whole material. Add a ShaderCompilerTest case with a second stage that parses but fails to link Done in 5edddf49f3 (Report a later stage's link failure instead of a generic error), test LinkFailureInLaterStageTest."
    status: completed
  - id: fix-push-constant-lookup
    content: "Fix 2: Material::validatePushConstantRange accepts or rejects on the first stage that knows the name, so two stages sharing an instance name with different spans (PolyMark's capture shaders both use pushConstants) reject a valid push. Accept if any stage covers the range; move the logic somewhere testable without a device and test the shared-instance-name case Done in f40ae23ef9 (Accept a push that any stage of the material covers): the logic is now the free function validate_push_constant_range in ShaderReflection, test PushConstantSharedNameTest."
    status: completed
  - id: fix-subroutine-chaining
    content: "Fix 3: rebindSubroutineCalls applies the rebind map one entry at a time over an unordered_map, so A->B plus B->C chains or not depending on iteration order. Find all call sites against the unmodified text first, then apply every replacement in one back-to-front pass. Test with a two-entry chain Done in c6d95bee06 (Apply all subroutine bindings in one pass so they cannot chain). The test, SubroutineSwapTest, uses a swap rather than a chain, since a chain goes wrong in only one of its two iteration orders and so could pass by luck."
    status: completed
  - id: definition-heuristic-note
    content: "Cleanup: find_function_references treats a forward declaration as a definition and a call preceded by an identifier (e.g. inside a macro body) as a definition. Exclude prototypes from has_function_definition; document the macro limitation next to the existing preprocessor note Done in ecf3fa5aed (Do not treat a function prototype as a rebinding target), test SubroutinePrototypeTest: a declaration counts as a definition only if the first { or ; after its parameter list is a {, skipping comments and preprocessor lines."
    status: completed
  - id: stale-comments
    content: "Cleanup: remove references to the plan's PR numbering from production code and tests, which will dangle once the DROP commits are dropped, and correct test comments that still describe sentinels, unconditional vertex attribute reassignment from 0, and redecorateInternal's set parameter Done in e0e8d87820 (Correct stale comments about the redecorator and its tests)."
    status: completed
  - id: artifact-extension
    content: "Cleanup: write the kSpvReflect artifact as .reflect rather than .reflection so it matches the golden extension, and update the ShaderCompiler README Done in 3fe928e579 (Name the reflection artifact .reflect to match the goldens). The SPIRV-Cross dump keeps its pre-existing .spv.reflection name."
    status: completed
  - id: reconcile-plans
    content: "Docs: bring the three existing plan files into line with the branch: Slang build state, rebased commit hashes, stale todo statuses and superseded sections, wrong line numbers, and an internal contradiction in the remaining-work file Done in the (DROP) commit that carries this update."
    status: completed
  - id: sdk-licence
    content: "Licences: update ThirdParty/licenses for the Vulkan SDK 1.4.363.0, which never needed OSRB. With Slang back in the build, also prepare (but do not merge) the Slang attribution, which does"
    status: pending
  - id: pr-split
    content: "Propose the four-PR split of 895 and which of the fixes above fold into which; the user decides and performs the split"
    status: pending
isProject: false
---

# PR 895 review follow-ups

Source: the review of PR 895 at head `29f3f9c88b`. The branch builds and passes the render suite per the PR description; nothing below was found by a failing test. Fixes 1 to 3 are latent defects with a concrete trigger, the rest is cleanup and documentation.

## Working arrangement

The user re-adds `slang` to the `./vulkansdk ... loader glslang spirvcross vul layers` line in `install_vulkan` and provides a build container with the rebuilt prefix. Every code item is verified there by:

1. A full heavydb build.
2. `ShaderCompilerTest`, including any new cases, with **no** `.reflect` golden moving unless the item says it should.
3. `GfxRenderTest` and `RaytracingTest`, since fix 2 changes what every `setPushConstants` call goes through and fix 3 changes what every subroutine binding goes through.
4. The render sanity tests before the plan is closed out.

Code changes land as separate commits on `simoneves_renderer_upgrade_2026`, one per item, so they can be folded into whichever split PR they belong to (see the last section). No pushing; the user does that.

## Fix 1 - Preserve the real error when a later stage fails to link

**Where:** `GfxDriver/ShaderCompiler/GlslangWrapper.cpp`, the second loop of `glslToSpirv` (per-stage `link` and `getIntermediate`), and the consumer in `ShaderManager::buildSpirv`.

**Defect.** If stage *i* > 0 fails `link()` or `getIntermediate()`, the function returns with `results.stages[i].second` set but every earlier stage holding neither SPIR-V nor an error, and `material_error` is only ever written by the parse loop. `buildSpirv` processes stages in order and throws at stage 0 with "Error generating Spir-V for <stage 0>". The message naming the actual failure is never seen.

The mapIO loop does not have this problem, because every earlier stage has already produced SPIR-V by the time a later one fails there. Only the link loop does.

**Fix.** Make "one failure stops the whole material" true of the error as well as of the compile. Preferred shape: a small helper in `glslToSpirv` that records a stage's error and appends it to `material_error` in one go, used by every failure site (parse, link, getIntermediate, mapIO, resolver error, unlocated varyings, buildReflection). `buildSpirv`'s existing fallback from the stage error to `material_error` then does the right thing without change.

Do not instead change `buildSpirv` to scan for the first non-empty stage error: that would work, but leaves `CompileResults` with a field whose name promises more than it delivers.

**Test.** New `GeneralShaderCompilerTest` case, shaped like `UnlocatedVaryingTest`: a vertex shader that compiles cleanly and a fragment shader that parses but fails to link. A fragment shader with no `main` is the simplest candidate; confirm glslang 16.6.0 accepts it at parse and rejects it at link before relying on it, since that is precisely the distinction the test depends on. Assert the thrown message names the fragment shader and contains the link diagnostic, and does not consist of the generic "Error generating Spir-V" text. Add the two shaders to `Tests/RenderTests/ShaderCompiler/shaders/ShaderManifest.json`.

## Fix 2 - Push constant validation must consider every stage

**Where:** `GfxDriver/Pipeline/Material.cpp`, `Material::validatePushConstantRange`.

**Defect.** The loop returns on the first stage whose reflection knows `name`, passing or throwing on that stage's span alone. Push constant blocks are recorded under both their type name and their instance name, and instance names are not unique across stages: `ppllPolyTemplate_capture.vert` and `ppllPolyTemplate_capture.frag` both name their blocks `pushConstants`, spanning bytes 0-4 and 4-8 respectively. A push to `"pushConstants"` at offset 4 resolves against the vertex stage and throws, although the fragment stage declares exactly that range. No current caller does this; every one uses a member or type name. But the dual-name recording exists precisely so callers need not know which name to use.

**Fix.** Accept if any stage that knows the name covers the pushed range; throw only if none does. The error should then report every stage's declared span for that name, not just one, since "falls outside the 4 bytes at offset 0" is misleading when another stage declares the bytes that were pushed.

Move the decision into a free function over a set of reflections (in `ShaderReflection.{h,cpp}` or alongside `Material`), with `Material::validatePushConstantRange` reduced to collecting its caches' reflections and calling it. That makes it testable in `ShaderCompilerTest` without a device.

**Test.** A new vert/frag pair in `Tests/RenderTests/ShaderCompiler/shaders/` whose push constant blocks have different type names, the same instance name, and adjacent non-overlapping spans, mirroring PolyMark. Assert: a push by the shared instance name into either span passes; a push straddling both spans fails; an unknown name fails. Do **not** rework `shaderRedecoratorTest.frag` for this: its anonymous push constant block is the only coverage for the no-instance-name case.

The existing assertions in `GfxRenderTest`'s `PushConstantTest` must still hold unchanged.

## Fix 3 - Apply subroutine rebindings in one pass

**Where:** `GfxDriver/ShaderCompiler/ShaderManager.cpp`, `rebindSubroutineCalls`.

**Defect.** `SubroutineMap` is an `std::unordered_map`, and each entry is found and replaced against the text as already modified by earlier entries. Given `A -> B` and `B -> C`, calls to `A` end up calling `C` if `A` is processed first and `B` otherwise. The AST pass this replaced looked each call up once and never chained, so the textual pass silently diverges whenever a chain exists. None is known to exist today.

**Fix.** Two phases. First, for every map entry, compute call sites and the `is_defined` check against the unmodified `final_glsl`. Then merge all the resulting `(position, length, target)` edits, sort by position, and apply back to front in one pass. Positions from different entries cannot overlap, since each is the start of a distinct whole identifier. The `is_required` assertion and the "unused binding is not checked" rule stay exactly as they are.

**Test.** A `GeneralShaderCompilerTest` case using `createBuilder` plus `Builder::addSubroutineBinding` (public) to set up `A -> B` and `B -> C` on a small fixture shader where `A`, `B` and `C` are all defined and return distinguishable values. Assert on the generated GLSL (`ShaderCache::getGlsl`) that the original call to `A` now calls `B`, and the original call to `B` now calls `C`. Because the bug depends on hash order, the test is only meaningful if it fails against the current code; confirm that before the fix by temporarily forcing the problematic order if necessary, rather than trusting a pass.

## Cleanup - Definition detection

**Where:** `find_function_references` and `has_function_definition` in `ShaderManager.cpp`.

- A forward declaration such as `vec4 f(vec4 c);` is classified as a definition, so an `is_required` binding passes when the target was only declared. Have `has_function_definition` require that a body follows the parameter list. This is the one place where looking past the parameter list is necessary; the comment's warning about `#if` inside parameter lists applies, so the check has to tolerate preprocessor lines between `)` and `{` and treat a `;` as the only proof of a prototype.
- A call preceded by an identifier, as inside a `#define` body, is also classified as a definition and is not rebound. Not worth handling; nothing in the library does it. Add one sentence beside the existing `quantitativeScaleTemplate.vert` note so nobody needs to rediscover it.

Verify no `.reflect` golden moves and the render suite still passes, since this touches every subroutine-using shader.

## Cleanup - Stale comments and plan references

The plan files arrive in commits marked DROP, so references to their numbering will dangle upstream. Reword to say what is meant, without the numbering:

- `ShaderRedecorator.h`: "The name is left alone because PR 7 of the Slang migration deletes the class outright."
- `ShaderCompilerTest.cpp`: "Nothing below here was checked before PR 1 of the glslang-to-slang migration", "PRs 4 and 7 of the glslang-to-slang migration replace this allocator entirely", and "since PR 1 of the glslang-to-slang migration moved the FromFile cases".

And correct test comments describing behaviour that no longer exists:

- "That agreement is the whole reason the sentinel scheme exists" - the sentinels are gone.
- "Vertex attributes are reassigned unconditionally from 0" - glslang assigns them now; the reflection reads what it assigned.
- "redecorateInternal is only ever called with set 0" - it no longer takes a set.

Grep the whole diff against `master` for `PR [0-9]`, `sentinel` and `migration` once done, excluding `.cursor/`.

## Cleanup - One extension for reflection text

`write_spirv_artifacts` writes `<name>.reflection`; the goldens are `<name>.reflect`. Same format, purposely so. Rename the artifact to `.reflect` so the two can be diffed or copied across without renaming, and update the sentence in `Tests/RenderTests/ShaderCompiler/README.md`.

## Docs - Reconcile the existing plans with the branch

These are edits to `.cursor/plans/`, which travel in DROP commits; do them as one commit with the conventional `(DROP)` suffix.

**All three files:**

- Slang. At the time of review it was not built: the target list in `27c0a246a1` never included it, though the fork plan said to add it. Once the user's change re-adds it, the plans' description becomes true again, but the history should say it was deferred and restored rather than imply it was always there. PR 0b's "unblocked since PR 5a" becomes "unblocked once Slang was restored to the build".
- Commit hashes. Every hash cited for landed work is from before the rebase and no longer on any branch. Replace with the current ones. Mapping, by commit subject:

  | Cited | Current | Subject |
  |---|---|---|
  | `95fb438ec0` | `183c9fd7e4` | Replace byte-exact SPIR-V goldens with semantic assertions |
  | `34eed87b9a` | `22e8ab86d6` | Remove vestigial shader compiler code and fix latent defects |
  | `4752a50653` | `35c779bebc` | Target SPIR-V 1.6 and separate the Vulkan version decisions |
  | `d4019d84e6` | `acbd16b1f6` | Drop two glslang private-header dependencies |
  | `9b8be38b08` | `c16ae41c47` | Resolve shader subroutines textually, delete TShaderIRUtils |
  | `d2fbb038a8` | `8386189a0b` | Compile a material's shader stages in one call |
  | `f0bea7b8b7` | `03bafb57a2` | Assign bindings and locations with glslang's resolver |
  | `f335583933` | `8bbb7388ba` | Reject an inter-stage varying with no explicit location |
  | `cb950d202f` | `6ceeabcdb2` | Cover ShaderManager::createCache |
  | `b4c9acc131` | `578855b252` | Reflect the array dimension of a buffer member |
  | `02527b6f9b` | `1bdb9c6ce7` | Reflect push constants and check every push against them |
  | `31cc64861c` | `1ef9490600` | Delete the dead accumulationComposite.frag |

  Re-derive by subject with `git log --format='%h %s' master..HEAD` rather than trusting this table if anything has been rebased again since. `f20941c8ef`, `5612fa7547` and `055c5b645a` refer to history outside this branch and need checking individually; the first two are on `internal/master` and are fine.
- Add a note that hashes change on rebase, and prefer citing by subject alongside the hash from now on.

**`glslang-to-slang-migration.md`:** mark it explicitly as the historical record, superseded for status by `remaining-slang-migration-work.md`, at the top of the body and in `overview`. Then either update or annotate in place: the PR 3 and PR 4 todos (landed), "ShaderReflection has no push-constant support" (it has), the PR 7 bullet on array dimensions (landed separately), and the PR 4 section's "primary approach" of one `TProgram` with `mapIO(nullptr, nullptr)`, which PR 4a showed cannot work. Annotate rather than rewrite where the original reasoning is worth keeping.

**`vulkan-sdk-1.4.363-fork.md`:** close `run-spike` as completed with its actual outcome (no `FindGlslang` failure; two small adaptation commits). Correct the claim that the 1.3.275.0 fork and summary were retained for a one-line revert: the fork was deleted in `7092b5cf79`. Correct "keep all 15" to the 16-then-14-then-13 history. Update the licences todo per the licence item below.

**`remaining-slang-migration-work.md`:**

- Around line 151, "Nothing checks for that; adding a check is cheap if it ever seems worth it" contradicts the varying-check item that landed. Point at it instead.
- PR 5c part 3's line references: the copy block is now at about `scripts/common-functions.sh:1088-1103`, and the pin comment it says to delete no longer exists. Cite by content, not line number, since the user's Slang change will move them again.
- Add "restore Slang to the build" to the dependency graph as the predecessor of PR 0b and PR 6, marked done once the user's change lands.
- Add the review fixes from this plan to "What has already landed" once they do.
- Add a line under PR 7 noting the binding-order limitation found in review: stages resolve sequentially, so an auto-assigned binding in an earlier stage can take a number a later stage declares explicitly. The clash check catches it and the old allocator had the same limitation, so it is not a regression, but Slang resolving the composed program at once removes it, and PR 7 should confirm that rather than assume it.

## Licences

The SDK attribution was bundled with Slang's under "deferred pending OSRB", but only Slang needs OSRB. `ThirdParty/licenses/index.md` and `index.txt` still link the 1.3.275.0 summary while the tree builds 1.4.363.0.

- Re-probe `https://vulkan.lunarg.com/software/license/vulkan-1.4.363.0-linux-license-summary.txt`. If published, add it following the existing `Vulkan_SDK__vulkan-1.3.275.0-linux-license-summary.txt` naming, update both index files, remove the 1.3.275.0 summary, and replace the `@TODO(se)` in the fork script's header with the file reference.
- If still unpublished, ask the user whether to cite the newest published summary (1.4.350.0 when last checked) as an interim, rather than deciding unilaterally.
- Slang: prepare `slang__LICENSE` (Apache-2.0 WITH LLVM-exception, from the `vulkan-sdk-1.4.363` tag) and the index entries as a separate commit that is **not** to be merged until OSRB clears. Building Slang into the deps prefix without linking or shipping it is the user's call on whether that already needs approval; flag it rather than assume.

## Proposed split of PR 895

The PR description anticipates about four PRs. One split that keeps each independently buildable and the test safety net ahead of the behavioural changes:

1. **SDK bump.** `27c0a246a1`, `4c7d873623`, `548b87e64d`, `7092b5cf79`, plus the user's Slang restoration and the SDK licence update.
2. **Test safety net and hygiene.** `183c9fd7e4`, `22e8ab86d6`, `35c779bebc`, `acbd16b1f6`. No golden moves after the first commit, which is the evidence for the rest.
3. **Compiler restructure.** `c16ae41c47`, `8386189a0b`, `03bafb57a2`, `8bbb7388ba`, `6ceeabcdb2`. Fix 1, fix 3 and the definition-detection cleanup belong here.
4. **Reflection additions.** `578855b252`, `1bdb9c6ce7`, `1ef9490600`. Fix 2 and the artifact extension cleanup belong here.

The stale-comment cleanup touches files in 2, 3 and 4, so it either splits three ways or rides with 4.

The fixes are written as standalone commits regardless. Whether they are then squashed into their parents or kept separate, and whether the split happens at all, is the user's decision.

## Out of scope

- PR 0b itself (the `slangc -allow-glsl` corpus spike). Unblocked by the Slang restoration and the obvious next step after this plan, but a separate piece of work.
- PR 5d (instance to Vulkan 1.4). Unchanged: optional, and not wanted without a reason.
- The oldest-driver (535) render smoke run requested in review by patdevinwilson. Deferred by the user, to be done later.
