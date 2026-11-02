#!/usr/bin/env python3
# PR 8 spike: rewrite an assembled shader so that its spliced quantitative scales call
# the generic quantitativeScale module instead of carrying the scale's code.
#
# Each "// BEGIN SCALE quantitativeScaleTemplate.vert" ... "// END SCALE" block becomes
# its #defines and uniform block, unchanged, so the reflected names stay the same, and
# a wrapper keeping evalQuantitativeScale_<name>(value) for the shader's body. The
# transform, null check and interpolation the Builder bound are read from the block.
#
# Usage: make_module_variant.py <assembled.glsl> <out.glsl>
import re
import sys

TRANSFORMS = {"passThru": "PassThruTransform", "log": "LogTransform",
              "pow": "PowTransform", "sqrt": "SqrtTransform"}
NULL_CHECKS = {"isNullVal": "NullCheck", "isNullValPassThru": "NoNullCheck",
               "isNullValFunc": "NoNullCheck"}
INTERPS = {"default": "DefaultInterp"}


def rewrite_block(block):
    defines = dict(re.findall(r"#define (\w+?)_\w+ (\S+)", block))
    name = re.search(r"#define domainType_(\w+) ", block).group(1)
    ubo = re.search(r"(layout\(std430\) uniform QUANTITATIVE_SCALE_UBO_TYPE_\w+ \{.*?\};)",
                    block, re.S).group(1)
    transform = re.search(r"transformedVal = (\w+)Transform_" + name, block).group(1)
    null_check = re.search(r"if \((\w+)_" + name + r"\(domainVal\)\)", block).group(1)
    interp = re.search(r"return (\w+)Interp_" + name + r"\(uRanges", block).group(1)
    d, r = defines["domainType"], defines["rangeType"]
    header = "\n".join(line for line in block.splitlines() if line.startswith("#define"))
    clamp = "true" if defines["useClamp"] == "1" else "false"
    return f"""// SCALE {name}, from the quantitativeScale module
{header}

{ubo}

{r} evalQuantitativeScale_{name}(in {d} domainVal) {{
  return evalQuantitativeScale<{d}, {r}, {defines['numDomains']}, {defines['numRanges']},
      {TRANSFORMS[transform]}<{d}>, {NULL_CHECKS[null_check]}<{d}>,
      {INTERPS[interp]}<{r}, {d}>, {clamp}>(
      uDomains_{name}, nullDomainVal_{name}, uRanges_{name}, nullRangeVal_{name},
      uExponent_{name}, domainVal);
}}
"""


def main():
    source = open(sys.argv[1]).read()
    pattern = re.compile(r"// BEGIN SCALE quantitativeScaleTemplate\.vert\n(.*?)"
                         r"// END SCALE quantitativeScaleTemplate\.vert\n", re.S)
    count = len(pattern.findall(source))
    source = pattern.sub(lambda m: rewrite_block(m.group(1)), source)
    # The import goes after the last #include, before any declaration
    last_include = max(m.end() for m in re.finditer(r"^#include .*$", source, re.M))
    source = source[:last_include] + "\nimport quantitativeScale;" + source[last_include:]
    open(sys.argv[2], "w").write(source)
    print(f"Rewrote {count} quantitative scale(s)")


if __name__ == "__main__":
    main()
