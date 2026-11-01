"""PR 0b spike harness: compile the shader corpus through slangc -allow-glsl.

Runs inside the build container, fed over stdin. Writes everything under
build_release/pr0b/. Pass A compiles the assembled GLSL captured from the render
suite; pass B compiles every raw stage source listed in the manifests, with the
runtime header prepended, as a lower bound for the shaders pass A did not reach.
"""
import collections
import json
import os
import re
import subprocess

ROOT = '/home/seves/work/simoneves/heavydb'
P = ROOT + '/build_release/pr0b'
CAPTURED = ROOT + '/build_release/pr0b_artifacts'

# The header ShaderManager prepends (spirv_shader_header in ShaderManager.cpp)
HEADER = """#version 460 core
#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_ARB_gpu_shader_int64 : require
#extension GL_EXT_shader_atomic_int64 : require
#extension GL_EXT_scalar_block_layout : require
"""

CLASS_TO_STAGE = {
    'vertex': 'vertex', 'fragment': 'fragment', 'geometry': 'geometry',
    'compute': 'compute', 'mesh': 'mesh', 'task': 'amplification',
    'ray_gen': 'raygeneration', 'ray_closest_hit': 'closesthit',
    'ray_miss': 'miss', 'ray_intersection': 'intersection',
    'ray_callable': 'callable', 'ray_any_hit': 'anyhit',
}
EXT_TO_STAGE = {
    'vert': 'vertex', 'frag': 'fragment', 'geom': 'geometry', 'comp': 'compute',
    'mesh': 'mesh', 'task': 'amplification', 'raygen': 'raygeneration',
    'closest': 'closesthit', 'miss': 'miss', 'isect': 'intersection',
    'call': 'callable', 'anyhit': 'anyhit',
}
STAGE_TO_CLASS = {v: k for k, v in CLASS_TO_STAGE.items()}

SLANGC = ['slangc', '-allow-glsl', '-lang', 'glsl', '-target', 'spirv',
          '-fvk-use-scalar-layout', '-matrix-layout-column-major']

ERR_RE = re.compile(r'^(error|warning)\[(E\d+|W\d+)\]: (.*)$')
LOC_RE = re.compile(r'^\s*--> (.*?):(\d+):(\d+)')


def build_corpus():
    os.makedirs(P + '/inc', exist_ok=True)
    manifests = subprocess.check_output(
        ['git', '-c', 'safe.directory=*', 'ls-files', '*ShaderManifest.json'],
        cwd=ROOT, text=True).split()
    rows = []
    for m in manifests:
        base = os.path.dirname(os.path.join(ROOT, m))
        for e in json.load(open(os.path.join(ROOT, m)))['sources']:
            src = os.path.normpath(os.path.join(base, e['file_path'], e['filename']))
            lib = e['internal_path'] + e['filename']
            dst = os.path.join(P, 'inc', lib)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            if os.path.exists(src):
                with open(dst, 'w') as f:
                    f.write(split_header(open(src).read())[2])
            rows.append({'manifest': m, 'lib': lib, 'src': os.path.relpath(src, ROOT),
                         'class': e['class'], 'tests': m.startswith('Tests/'),
                         'exists': os.path.exists(src)})
    # Generated at runtime by ShaderManager rather than read from a manifest
    sm = open(ROOT + '/GfxDriver/ShaderCompiler/ShaderManager.cpp').read()
    m = re.search(r'constexpr char type_defines_string\[\] = R"\((.*?)\)";', sm, re.S)
    with open(P + '/inc/Marks/typeDefines.glsl', 'w') as f:
        f.write(m.group(1))
    return rows


def split_header(code):
    """Mimic Library::updateDictionaries: pull out the //<extensions> and
    //<includes> blocks and trim everything up to the end of the last of them."""
    exts, incs, end = [], [], 0
    for tag, out in (('extensions', exts), ('includes', incs)):
        m = re.search(r'//<%s>(.*?)//</%s>[^\n]*\n?' % (tag, tag), code, re.S)
        if m:
            end = max(end, m.end())
            body = m.group(1)
            if tag == 'extensions':
                out += re.findall(r'#extension\s+(\w+)', body)
            else:
                out += re.findall(r'"([^"]+)"', body)
    return exts, incs, code[end:]


def assemble(src_code, sources):
    """What the builder emits, short of processOperators: the header, the
    extensions and the transitive includes in dependency order, then the code."""
    exts, incs, body = split_header(src_code)
    order, seen = [], set()

    def visit(lib):
        if lib in seen:
            return
        seen.add(lib)
        if lib in sources:
            e, i, _ = split_header(sources[lib])
            for x in e:
                if x not in exts:
                    exts.append(x)
            for x in i:
                visit(x)
        order.append(lib)

    for lib in incs:
        visit(lib)
    return (HEADER + ''.join('#extension %s : require\n' % e for e in exts) +
            ''.join('#include "%s"\n' % i for i in order) + body)


def compile_one(path, stage):
    # slangc cannot write to /dev/null, so give every output a real file
    os.makedirs(P + '/out', exist_ok=True)
    out = os.path.join(P, 'out', os.path.basename(path) + '.spv')
    cmd = SLANGC + ['-stage', stage, '-entry', 'main', '-I', P + '/inc',
                    '-o', out, path]
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
    errors, cur = [], None
    for line in (r.stdout + r.stderr).splitlines():
        m = ERR_RE.match(line)
        if m:
            cur = {'kind': m.group(1), 'code': m.group(2), 'msg': m.group(3)}
            if cur['kind'] == 'error':
                errors.append(cur)
            continue
        m = LOC_RE.match(line)
        if m and cur is not None and 'loc' not in cur:
            cur['loc'] = '%s:%s' % (os.path.basename(m.group(1)), m.group(2))
    # slangc exits 0 even when it reports errors, so judge by the errors
    ok = r.returncode == 0 and not errors
    return {'ok': ok, 'rc': r.returncode, 'errors': errors,
            'raw': (r.stdout + r.stderr)[-4000:] if not ok else ''}


def main():
    rows = build_corpus()
    by_base = collections.defaultdict(list)
    for r in rows:
        if r['class'] != 'glsl':
            by_base[os.path.basename(r['lib']).split('.')[0]].append(r)

    results = {'corpus': rows, 'assembled': [], 'raw': []}

    # Pass A: assembled GLSL from the render suite
    for name in sorted(os.listdir(CAPTURED)):
        if not name.endswith('.glsl') or name.endswith('.spv.glsl'):
            continue
        stem, ext = name[:-len('.glsl')].rsplit('.', 1)
        stage = EXT_TO_STAGE[ext]
        matches = [r for r in by_base.get(stem, [])
                   if r['class'] == STAGE_TO_CLASS.get(stage)]
        res = compile_one(os.path.join(CAPTURED, name), stage)
        res.update({'file': name, 'stage': stage,
                    'lib': [m['lib'] for m in matches],
                    'tests': all(m['tests'] for m in matches) if matches else None})
        results['assembled'].append(res)

    # Pass B: every stage source, assembled as the builder would but with its
    # template tags left unsubstituted
    os.makedirs(P + '/raw', exist_ok=True)
    sources = {r['lib']: open(os.path.join(ROOT, r['src'])).read()
               for r in rows if r['exists']}
    for r in rows:
        if r['class'] == 'glsl' or not r['exists']:
            continue
        stage = CLASS_TO_STAGE[r['class']]
        out = os.path.join(P, 'raw', r['lib'].replace('/', '__') + '.glsl')
        with open(out, 'w') as f:
            f.write(assemble(sources[r['lib']], sources))
        res = compile_one(out, stage)
        res.update({'lib': r['lib'], 'stage': stage, 'tests': r['tests']})
        results['raw'].append(res)

    json.dump(results, open(P + '/results.json', 'w'), indent=1)
    for key in ('assembled', 'raw'):
        rs = results[key]
        print(key, 'total', len(rs), 'ok', sum(x['ok'] for x in rs))


main()
