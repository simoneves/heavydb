#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Compare glslang and Slang on the shaders the renderer actually assembles.

The server compiles every material with Slang. glslang is no longer built, but a
standalone glslangValidator can still compile the same assembled GLSL, which is what
this compares: compile time, and what each compiler's SPIR-V says to the renderer.

Run it inside the build container, after sourcing the deps environment, so that
slangc, spirv-val and the SpirvReflect tool are the build's own.

1. Capture a corpus: run the server with
     OMNISCI_shader_artifact_path=<dir> OMNISCI_always_save_shader_artifacts=all
   and drive it, with be-render-tests for instance. Each stage the server compiles
   leaves <name>.<stage>.glsl, the assembled source, and <name>.<stage>.builder.
   Artifacts are named by template, so a corpus holds the last variant of each.

2. Build glslang separately, at the tag the deps used to pin, for instance
     git clone --branch vulkan-sdk-1.4.363 https://github.com/KhronosGroup/glslang
     cmake -S glslang -B build -DENABLE_OPT=OFF -DGLSLANG_TESTS=OFF \\
       -DCMAKE_INSTALL_PREFIX=<prefix> && cmake --build build && cmake --install build

3. Run
     compare_compilers.py --corpus <dir> --glslang <prefix>/bin/glslang --out <dir>

For each stage it compiles the assembled GLSL with both, glslang as GlslangWrapper
did (Vulkan 1.3, SPIR-V 1.6, bindings and locations mapped automatically) and
slangc as SlangBackend does (GLSL input, SPIR-V 1.6, scalar layout, row-major
matrices, entry point names kept, no optimization, unformatted images Unknown). It
times each compile as the fastest of several runs, less the same compiler's time
for a trivial shader, which is the cost of starting it. It validates each module
with spirv-val, and prints each module's reflection with SpirvReflect, in the
.reflect golden format, and diffs the two.

What it cannot say:
- The server compiles a material's stages together, so that bindings left to the
  compiler are assigned across the material; this compiles one stage at a time,
  with each compiler, so auto-assigned bindings can differ from the server's.
- The server reflects Slang materials from Slang's layout, which keeps resources the
  entry point never reads; this reflects both from SPIR-V, where Slang drops them.
- The shader sources no longer have glslang forms of the Slang-only constructs
  (geometry and mesh I/O, buffer device addresses and so on), so glslang rejects
  the stages that use them. Those are reported, not counted as failures.
- Slang's time here includes compiling a single stage from a cold process; the
  server keeps one global session, so its per-material cost is lower.
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]

STAGES = {
    "vert": ("vert", "vertex"),
    "frag": ("frag", "fragment"),
    "geom": ("geom", "geometry"),
    "comp": ("comp", "compute"),
    "mesh": ("mesh", "mesh"),
    "task": ("task", "amplification"),
    "tesc": ("tesc", "hull"),
    "tese": ("tese", "domain"),
}

TRIVIAL = {
    "frag": "#version 460\nlayout(location = 0) out vec4 color;\n"
            "void main() { color = vec4(1.0); }\n",
}


def build_include_tree(root):
    """Lays out every manifest's sources at their Library internal paths, as the
    Library serves #include, plus the typeDefines the ShaderManager generates."""
    if root.exists():
        shutil.rmtree(root)
    root.mkdir(parents=True)
    manifests = subprocess.run(["git", "-C", str(REPO), "ls-files", "*ShaderManifest.json"],
                               check=True, capture_output=True, text=True).stdout.split()
    count = 0
    for manifest in manifests:
        manifest = REPO / manifest
        for source in json.loads(manifest.read_text())["sources"]:
            src = (manifest.parent / source["file_path"] / source["filename"]).resolve()
            dst = root / source.get("internal_path", "") / source["filename"]
            if src.exists() and not dst.exists():
                dst.parent.mkdir(parents=True, exist_ok=True)
                dst.symlink_to(src)
                count += 1
    manager = (REPO / "GfxDriver/ShaderCompiler/ShaderManager.cpp").read_text()
    match = re.search(r'type_defines_string\[\] = R"\((.*?)\)";', manager, re.S)
    if not match:
        sys.exit("Cannot find type_defines_string in ShaderManager.cpp")
    (root / "Marks").mkdir(exist_ok=True)
    (root / "Marks/typeDefines.glsl").write_text(match.group(1))
    return count + 1


def glslang_command(glslang, stage, entry, include_root, src, out):
    command = [glslang, "-V", "--target-env", "vulkan1.3", "--auto-map-bindings",
               "--auto-map-locations", "-S", STAGES[stage][0], f"-I{include_root}",
               "-o", str(out), str(src)]
    if entry != "main":
        command[1:1] = ["-e", entry, "--source-entrypoint", entry]
    return command


def slang_command(slangc, stage, entry, include_root, src, out):
    return [slangc, str(src), "-allow-glsl", "-lang", "glsl", "-stage", STAGES[stage][1],
            "-entry", entry, "-target", "spirv", "-profile", "spirv_1_6",
            "-emit-spirv-directly", "-fvk-use-entrypoint-name", "-force-glsl-scalar-layout",
            "-matrix-layout-row-major", "-O0", "-default-image-format-unknown",
            "-warnings-disable", "39001", "-I", str(include_root), "-o", str(out)]


def run_timed(command, repeats):
    """Runs a compile `repeats` times, returning the last result and the fastest time
    in milliseconds, or None if it failed."""
    best = None
    result = None
    for _ in range(repeats):
        start = time.perf_counter()
        result = subprocess.run(command, capture_output=True, text=True)
        elapsed = (time.perf_counter() - start) * 1000.0
        if result.returncode != 0:
            return result, None
        best = elapsed if best is None else min(best, elapsed)
    return result, best


def first_error(result):
    for line in (result.stderr + result.stdout).splitlines():
        if "error" in line.lower():
            return line.strip()[:200]
    lines = (result.stderr + result.stdout).strip().splitlines()
    return lines[0][:200] if lines else f"exit code {result.returncode}"


def validate(spirv_val, module):
    # The layout rules the renderer runs with, as ShaderCompilerTest validates
    result = subprocess.run([spirv_val, "--target-env", "vulkan1.3", "--scalar-block-layout",
                             "--uniform-buffer-standard-layout", str(module)],
                            capture_output=True, text=True)
    return result.returncode == 0, (result.stderr + result.stdout).strip()[:300]


def reflect(spirv_reflect, module, out):
    result = subprocess.run([spirv_reflect, str(module)], capture_output=True, text=True)
    if result.returncode != 0:
        return None, result.stderr.strip()[:300]
    out.write_text(result.stdout)
    return result.stdout, ""


def entry_point(glsl):
    builder = glsl.with_name(glsl.name[: -len(".glsl")] + ".builder")
    if builder.exists():
        return json.loads(builder.read_text()).get("entryPoint", "main") or "main"
    return "main"


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--corpus", required=True, type=Path,
                        help="directory of captured shader artifacts")
    parser.add_argument("--glslang", required=True, help="the standalone glslang binary")
    parser.add_argument("--out", required=True, type=Path, help="directory for results")
    parser.add_argument("--slangc", default="slangc")
    parser.add_argument("--spirv-val", default="spirv-val")
    parser.add_argument("--spirv-reflect", type=Path,
                        default=REPO / "build_release/Tests/RenderTests/SpirvReflect")
    parser.add_argument("--repeats", type=int, default=5,
                        help="runs per compile; the fastest is reported")
    parser.add_argument("--filter", default="", help="only stages whose name contains this")
    args = parser.parse_args()

    for tool in (args.glslang, args.slangc, args.spirv_val, str(args.spirv_reflect)):
        if not shutil.which(tool):
            sys.exit(f"Cannot run {tool}")

    args.out.mkdir(parents=True, exist_ok=True)
    include_root = args.out / "include"
    print(f"Include tree: {build_include_tree(include_root)} files")

    # The cost of starting each compiler, from a trivial shader
    trivial = args.out / "trivial.frag"
    trivial.write_text(TRIVIAL["frag"])
    baseline = {}
    for name, make in (("glslang", lambda: glslang_command(
            args.glslang, "frag", "main", include_root, trivial, args.out / "trivial.g.spv")),
                       ("slang", lambda: slang_command(
            args.slangc, "frag", "main", include_root, trivial, args.out / "trivial.s.spv"))):
        result, ms = run_timed(make(), args.repeats)
        if ms is None:
            sys.exit(f"{name} cannot compile a trivial shader: {first_error(result)}")
        baseline[name] = ms
    print(f"Baseline (start-up): glslang {baseline['glslang']:.1f} ms, "
          f"Slang {baseline['slang']:.1f} ms")

    stages = sorted(p for p in args.corpus.glob("*.glsl")
                    if not p.name.endswith(".spv.glsl") and args.filter in p.name)
    rows = []
    for glsl in stages:
        name = glsl.name[: -len(".glsl")]
        stage = name.rsplit(".", 1)[-1]
        if stage not in STAGES:
            continue
        entry = entry_point(glsl)
        row = {"stage": name, "entry": entry}
        modules = {}
        for compiler, make in (("glslang", glslang_command), ("slang", slang_command)):
            module = args.out / f"{name}.{compiler}.spv"
            tool = args.glslang if compiler == "glslang" else args.slangc
            result, ms = run_timed(make(tool, stage, entry, include_root, glsl, module),
                                   args.repeats)
            if ms is None:
                row[compiler] = {"ok": False, "error": first_error(result)}
                (args.out / f"{name}.{compiler}.log").write_text(result.stderr + result.stdout)
                continue
            valid, messages = validate(args.spirv_val, module)
            text, reflect_error = reflect(args.spirv_reflect, module,
                                          args.out / f"{name}.{compiler}.reflect")
            row[compiler] = {"ok": True, "ms": ms, "net_ms": max(ms - baseline[compiler], 0.0),
                             "valid": valid, "validation": messages,
                             "bytes": module.stat().st_size, "reflect_error": reflect_error}
            modules[compiler] = text
        if len(modules) == 2 and None not in modules.values():
            same = modules["glslang"] == modules["slang"]
            row["reflection"] = "same" if same else "differs"
            if not same:
                diff = subprocess.run(["diff", "-u", str(args.out / f"{name}.glslang.reflect"),
                                       str(args.out / f"{name}.slang.reflect")],
                                      capture_output=True, text=True).stdout
                (args.out / f"{name}.reflect.diff").write_text(diff)
        rows.append(row)

    (args.out / "summary.json").write_text(json.dumps(
        {"baseline_ms": baseline, "stages": rows}, indent=2))

    # The table
    lines = [f"{'stage':44} {'glslang ms':>11} {'Slang ms':>10} {'ratio':>6}  reflection"]
    both = []
    for row in rows:
        g, s = row.get("glslang", {}), row.get("slang", {})
        g_text = f"{g['net_ms']:.1f}" if g.get("ok") else "rejects"
        s_text = f"{s['net_ms']:.1f}" if s.get("ok") else "FAILS"
        ratio = ""
        if g.get("ok") and s.get("ok"):
            both.append((g["net_ms"], s["net_ms"]))
            ratio = f"{s['net_ms'] / g['net_ms']:.1f}x" if g["net_ms"] > 0 else ""
        notes = row.get("reflection", "")
        for compiler, info in (("glslang", g), ("slang", s)):
            if info.get("ok") and not info["valid"]:
                notes += f" {compiler}:spirv-val"
        lines.append(f"{row['stage']:44} {g_text:>11} {s_text:>10} {ratio:>6}  {notes}")
    lines.append("")
    lines.append(f"Stages: {len(rows)}. glslang compiles "
                 f"{sum(r.get('glslang', {}).get('ok', False) for r in rows)}, Slang compiles "
                 f"{sum(r.get('slang', {}).get('ok', False) for r in rows)}.")
    if both:
        g_total = sum(g for g, _ in both)
        s_total = sum(s for _, s in both)
        lines.append(f"Compiled by both ({len(both)}): glslang {g_total:.0f} ms, "
                     f"Slang {s_total:.0f} ms, net of start-up, "
                     f"{s_total / g_total:.1f}x." if g_total else "")
        lines.append("Per-stage times of a few ms are within the noise of process start-up, "
                     "which is subtracted; the total is the figure to trust.")
        lines.append(f"Reflection identical for "
                     f"{sum(r.get('reflection') == 'same' for r in rows)} of {len(both)}; "
                     f"see *.reflect.diff for the rest.")
    rejected = [r for r in rows if not r.get("glslang", {}).get("ok", True)]
    if rejected:
        lines.append("")
        lines.append("glslang rejects (first error):")
        for row in rejected:
            lines.append(f"  {row['stage']}: {row['glslang']['error']}")
    failed = [r for r in rows if not r.get("slang", {}).get("ok", True)]
    if failed:
        lines.append("")
        lines.append("Slang FAILS, which the server would too (first error):")
        for row in failed:
            lines.append(f"  {row['stage']}: {row['slang']['error']}")
    report = "\n".join(lines) + "\n"
    (args.out / "summary.txt").write_text(report)
    print(report)


if __name__ == "__main__":
    main()
