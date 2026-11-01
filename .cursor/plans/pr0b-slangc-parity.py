"""PR 0b, second pass: compile server-captured shaders through slangc and compare
the reflection of Slang's SPIR-V with glslang's.

Runs inside the build container, fed over stdin, after pr0b-slangc-spike.py has
built build_release/pr0b/inc. Reads artifacts written by a server run with
OMNISCI_always_save_shader_artifacts=all into build_release/pr0b_server_artifacts.
"""
import json
import os
import re
import subprocess

ROOT = '/home/seves/work/simoneves/heavydb'
P = ROOT + '/build_release/pr0b'
ART = ROOT + '/build_release/pr0b_server_artifacts'
OUT = P + '/parity'

EXT_TO_STAGE = {
    'vert': 'vertex', 'frag': 'fragment', 'geom': 'geometry', 'comp': 'compute',
    'mesh': 'mesh', 'task': 'amplification', 'raygen': 'raygeneration',
    'closest': 'closesthit', 'miss': 'miss', 'isect': 'intersection',
    'call': 'callable', 'anyhit': 'anyhit',
}
SLANGC = ['slangc', '-allow-glsl', '-lang', 'glsl', '-target', 'spirv',
          '-fvk-use-scalar-layout', '-matrix-layout-row-major']  # Slang's row-major is GLSL's column-major memory layout
ERR_RE = re.compile(r'^error\[(E\d+)\]: (.*)$')
RESOURCE_KINDS = ('ubos', 'ssbos', 'push_constants', 'textures', 'separate_images',
                  'separate_samplers', 'images', 'acceleration_structures',
                  'subpass_inputs')


def normalise(name):
    name = re.sub(r'^entryPointParam_\w+\.', '', name)
    name = re.sub(r'^SLANG_ParameterGroup_', '', name)
    return re.sub(r'_(std430|std140|scalar)$', '', name)


def reflect(spv):
    js = spv + '.json'
    r = subprocess.run(['spirv-cross', spv, '--reflect', '--output', js],
                       capture_output=True, text=True)
    if r.returncode != 0:
        return None
    return json.load(open(js))


def type_members(d, type_id, depth=0):
    """Flatten a block type into (path, offset, array, array_stride, matrix_stride)."""
    t = d.get('types', {}).get(type_id)
    if t is None or depth > 8:
        return []
    out = []
    for m in t.get('members', []):
        name = m.get('name', '?')
        out.append((name, m.get('offset'), tuple(m.get('array', [])),
                    m.get('array_stride'), m.get('matrix_stride')))
        if m.get('type', '').startswith('_'):
            out += [(name + '.' + n, *rest)
                    for n, *rest in type_members(d, m['type'], depth + 1)]
    return out


def summarise(d):
    s = {}
    for kind in RESOURCE_KINDS:
        for r in d.get(kind, []):
            key = (kind, normalise(r.get('name', '')))
            s[key] = {
                'set': r.get('set'), 'binding': r.get('binding'),
                'array': tuple(r.get('array', [])),
                'block_size': r.get('block_size'),
                'members': type_members(d, r.get('type', '')),
            }
    for kind in ('inputs', 'outputs'):
        for r in d.get(kind, []):
            if r.get('name', '').startswith('gl_'):
                continue
            s[(kind, normalise(r.get('name', '')))] = {
                'location': r.get('location'), 'array': tuple(r.get('array', []))}
    for sc in d.get('specialization_constants', []):
        s[('spec', sc.get('name', ''))] = {'id': sc.get('id')}
    return s


def compare(a, b):
    """Differences between glslang (a) and Slang (b) summaries."""
    diffs = []
    for key in sorted(set(a) | set(b)):
        if key not in b:
            diffs.append(('missing in slang', key, a[key]))
        elif key not in a:
            diffs.append(('extra in slang', key, b[key]))
        else:
            for field in sorted(set(a[key]) | set(b[key])):
                if a[key].get(field) != b[key].get(field):
                    diffs.append(('differs', key, field, a[key].get(field),
                                  b[key].get(field)))
    return diffs


def main():
    os.makedirs(OUT, exist_ok=True)
    results = []
    for name in sorted(os.listdir(ART)):
        if not name.endswith('.glsl') or name.endswith('.spv.glsl'):
            continue
        base = name[:-len('.glsl')]
        ext = base.rsplit('.', 1)[1]
        stage = EXT_TO_STAGE[ext]
        entry = 'main'
        builder = os.path.join(ART, base + '.builder')
        if os.path.exists(builder):
            try:
                entry = json.load(open(builder)).get('entryPoint') or 'main'
            except ValueError:
                pass
        out_spv = os.path.join(OUT, base + '.slang.spv')
        r = subprocess.run(SLANGC + ['-stage', stage, '-entry', entry, '-I', P + '/inc',
                                     '-o', out_spv, os.path.join(ART, name)],
                           capture_output=True, text=True, timeout=300)
        errors = [{'code': m.group(1), 'msg': m.group(2)}
                  for m in map(ERR_RE.match, (r.stdout + r.stderr).splitlines()) if m]
        res = {'file': name, 'stage': stage, 'entry': entry,
               'ok': r.returncode == 0 and not errors, 'errors': errors,
               'log': (r.stdout + r.stderr)[-3000:] if errors else ''}
        glslang_spv = os.path.join(ART, base + '.spv')
        if res['ok'] and os.path.exists(glslang_spv):
            a, b = reflect(glslang_spv), reflect(out_spv)
            if a is None or b is None:
                res['parity'] = 'reflect failed'
            else:
                res['diffs'] = [list(map(str, x)) for x in compare(summarise(a), summarise(b))]
                res['parity'] = 'match' if not res['diffs'] else 'differs'
        results.append(res)
    json.dump(results, open(OUT + '/results.json', 'w'), indent=1)
    print('compiled', len(results), 'ok', sum(r['ok'] for r in results),
          'parity match', sum(r.get('parity') == 'match' for r in results),
          'differs', sum(r.get('parity') == 'differs' for r in results))


main()
