# PR 8 spike: Slang modules and generics for the shader templates

Branch `simoneves_pr8_spike`, from `b28c6906d1`. Nothing here is built by the project;
the experiments run inside the build container against the deps' Slang.

PR 8 proposes moving the shader templates to Slang modules and generics, replacing
the Builder's text splicing and the textual subroutine pass. The spike took the
costliest Mark stage seen in be-render-tests, `fastSymbolTemplate.vert` as the server
assembled it, and moved its two spliced quantitative scales into a generic module.
It asked four questions.

## Files

- `quantitativeScale.slang` - the quantitative scale as a generic module. Domain and
  range types are type parameters; the transform, null check and interpolation are
  interface implementations passed as type arguments, where the GLSL template has stub
  functions the Builder retargets. Floating-point domains and ranges only.
- `quantitativeScaleConcrete.slang` - the same logic as one ordinary function, for the
  one combination the stage uses, to separate the cost of generics from that of a module.
- `make_module_variant.py` - rewrites an assembled shader's spliced quantitative scales
  into its unchanged uniform blocks plus a wrapper calling the module.
- `fastSymbol_spliced.vert` - the stage as the server assembled it (a capture from
  be-render-tests), with two quantitative scales spliced in: 921 lines.
- `fastSymbol_module.vert` - the same through the generic module: 478 lines.
- `fastSymbol_concrete.vert`, `fastSymbol_noscale.vert` - through the concrete module,
  and with each scale replaced by returning its input, for attribution.
- `compile_timing.cpp` - compiles shaders through a compile request with
  SlangBackend's settings, timing a new session per compile (as SlangBackend does
  today) against one session throughout.
- `main.vert`, `scale.slang` - the first, minimal check that a GLSL shader can import
  a Slang module.

Run in the container, after sourcing the deps environment, with the include tree from
`compare_compilers.py` (`<out>/include`):

```
g++ -std=c++17 -O2 -DSLANG_STATIC -I/usr/local/mapd-deps/include/slang compile_timing.cpp \
  -o compile_timing /usr/local/mapd-deps/lib/libslang-compiler.a -lpthread -ldl
./compile_timing vertex 10 <include tree> . -- fastSymbol_*.vert
```

## Findings

**1. Coexistence: yes.** A shader in GLSL syntax, compiled as the server compiles it,
can `import` a Slang module and call a generic function with interface type arguments.
So families can move one at a time, with no flag: a template stays GLSL and imports
what has moved.

**2. Generics can replace the subroutine retargeting.** The scale's transform, null
check and interpolation become type arguments, `evalQuantitativeScale<double, float, 2,
2, PassThruTransform<double>, NullCheck<double>, DefaultInterp<float, double>, false>`,
chosen by the material. The choice is still written into the material's shader, but as
a short call rather than 250 spliced lines. Converting between two generic
floating-point types needs `__realCast`; a plain constructor call is ambiguous. Not
covered: integer and 64-bit integer domains, the colour interpolations, and the
accumulation path. Not checked: that the generic scale renders identically; only that
it compiles, validates and reflects identically.

**3. Reflection need not change.** With each scale's uniform block left in the
material's shader, declared exactly as today, and only the logic in the module, the
stage's reflection is identical to the spliced stage's (`SpirvReflect`): same blocks,
names and offsets. Material and the Vega property writers would not notice.

**4. Performance: the scale code is not where the time goes.** Per compile of the
fastSymbol vertex stage, release Slang, ten compiles each:

| Variant | New session per compile | One session, after the first |
|---|---|---|
| spliced, as today | 31 ms | 29.7 ms |
| generic module | 64 ms | 30.9 ms |
| concrete module | 30 ms | 26.0 ms |
| no scale at all | 56 ms | 23.5 ms |

- The two scales cost about 6 ms of the 30. The rest, 23.5 ms, is the material's own
  body and includes, which differ per material and are checked from scratch each time.
  Earlier profiling put two-thirds of a stage's time in semantic checking; that is
  checking of the material's body, not of the shared scale code, and moving shared
  code into modules does not remove it. The plan's expectation that two-thirds of the
  cost would be paid once was wrong.
- Generics cost about what splicing does (30.9 against 29.7 ms). A concrete module
  function saves about 4 ms.
- A module costs about 30 ms to load, once per session. With today's session per
  material, the module variants cost twice as much, so moving to modules requires
  SlangBackend to keep one session across materials. That needs thought about module
  names, memory growth, and starting again when the Library is replaced.

## What this means for PR 8

Moving shared code into modules is sound for maintainability, and the mechanism
works, but on its own it will not make materials compile faster. The cost that remains
per material is Slang's front end on each material's own text.

The way to remove that is link-time specialization, which the plan names: compile each
template once, as a module, and express everything that varies between materials
(types, choices, counts, which properties are uniform or per-vertex) as generic or
specialization parameters rather than text. A material then costs only linking and
SPIR-V generation, which an earlier profile put at 9 to 18 ms of a 30 to 36 ms stage,
roughly half. That needs a template's whole body off text splicing, the Builder's tags
included, which is PR 8's real scope.

Suggested next step, if PR 8 goes ahead: port one whole small template, such as a
post-processing pass or the point template, to a module with a generic entry point,
and measure what specializing and generating code for one material costs in a
persistent session. That number decides whether PR 8 is justified by compile time, or
by the Builder's complexity alone. The in-memory material cache (`55fbb601eb`) already
removes the repeats.
