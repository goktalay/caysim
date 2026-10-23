#!/usr/bin/env python3
"""SmvGlb (C++ GLB okuyucu) ↔ bağımsız saf Python glTF okuyucu çapraz sınaması — Content/Simumav/ModelsSrc/*.glb.
Denetlenen: düğüm/ağ/malzeme/görsel sayısı, üçgen sayısı, dünya sınır kutusu (KHR_mesh_quantization dahil), adlı düğümlerin dünya konumu.
Kullanım: python3 tests/cpp/test_glb.py   (g++ gerekir; numpy gerekmez)"""
import glob
import json
import math
import os
import struct
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
KOK = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
CORE = os.path.join(KOK, 'Source', 'Simumav', 'Private', 'Core')
HATA = []


def matmul(a, b):
    return [sum(a[k * 4 + r] * b[c * 4 + k] for k in range(4)) for c in range(4) for r in range(4)]


def yerel(n):
    if 'matrix' in n:
        return list(n['matrix'])
    t = n.get('translation', [0, 0, 0]); x, y, z, w = n.get('rotation', [0, 0, 0, 1]); s = n.get('scale', [1, 1, 1])
    R = [1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w), 2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w),
         2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)]
    M = [0.0] * 16
    for c in range(3):
        for r in range(3):
            M[c * 4 + r] = R[c * 3 + r] * s[c]
    M[12:15] = t
    M[15] = 1.0
    return M


FMT = {5120: ('b', 1), 5121: ('B', 1), 5122: ('h', 2), 5123: ('H', 2), 5125: ('I', 4), 5126: ('f', 4)}


def oku(yol):
    d = open(yol, 'rb').read()
    jl = struct.unpack_from('<I', d, 12)[0]
    j = json.loads(d[20:20 + jl])
    bo = 20 + ((jl + 3) & ~3)
    bl = struct.unpack_from('<I', d, bo)[0]
    b = d[bo + 8:bo + 8 + bl]
    def acc(i):
        a = j['accessors'][i]; bv = j['bufferViews'][a['bufferView']]
        f, sz = FMT[a['componentType']]; n = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}[a['type']]
        st = bv.get('byteStride', sz * n); o = bv.get('byteOffset', 0) + a.get('byteOffset', 0)
        return [struct.unpack_from('<' + f * n, b, o + k * st) for k in range(a['count'])]
    world = {}
    def gez(i, P):
        W = matmul(P, yerel(j['nodes'][i])); world[i] = W
        for c in j['nodes'][i].get('children', []):
            gez(c, W)
    I = [1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0]
    for r in j['scenes'][j.get('scene', 0)]['nodes']:
        gez(r, I)
    lo, hi, tri = [1e30] * 3, [-1e30] * 3, 0
    for i, W in world.items():
        n = j['nodes'][i]
        if 'mesh' not in n:
            continue
        for p in j['meshes'][n['mesh']]['primitives']:
            if p.get('mode', 4) != 4:
                continue
            pos = acc(p['attributes']['POSITION'])
            tri += (len(acc(p['indices'])) if 'indices' in p else len(pos)) // 3
            for v in pos:
                for r in range(3):
                    x = W[r] * v[0] + W[4 + r] * v[1] + W[8 + r] * v[2] + W[12 + r]
                    lo[r] = min(lo[r], x); hi[r] = max(hi[r], x)
    named = {j['nodes'][i].get('name'): [W[12], W[13], W[14]] for i, W in world.items() if j['nodes'][i].get('name')}
    return {'nodes': len(j['nodes']), 'meshes': len(j.get('meshes', [])), 'mats': len(j.get('materials', [])), 'images': len(j.get('images', [])),
            'tris': tri, 'lo': lo, 'hi': hi, 'named': named}


def main():
    dosyalar = sorted(glob.glob(os.path.join(KOK, 'Content', 'Simumav', 'ModelsSrc', '*.glb')))
    if not dosyalar:
        print('ATLANDI: Content/Simumav/ModelsSrc/*.glb yok')
        return 0
    with tempfile.TemporaryDirectory() as td:
        exe = os.path.join(td, 'glb')
        subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-O1', '-I', CORE, '-o', exe,
                        os.path.join(KOK, 'tests', 'cpp', 'test_glb_main.cpp'), os.path.join(CORE, 'SmvGlb.cpp')], check=True)
        out = subprocess.run([exe] + dosyalar, capture_output=True, text=True, check=True).stdout.splitlines()
    for satir, yol in zip(out, dosyalar):
        c = json.loads(satir)
        ad = os.path.basename(yol)
        if 'err' in c:
            HATA.append(f'{ad}: {c["err"]}')
            continue
        p = oku(yol)
        for k in ('nodes', 'meshes', 'mats', 'images', 'tris'):
            if c[k] != p[k]:
                HATA.append(f'{ad} {k}: C++={c[k]} py={p[k]}')
        for k in ('lo', 'hi'):
            if any(abs(a - b) > 1e-3 for a, b in zip(c[k], p[k])):
                HATA.append(f'{ad} {k}: C++={c[k]} py={p[k]}')
        for n, v in p['named'].items():
            if n in c['named'] and any(abs(a - b) > 1e-3 for a, b in zip(c['named'][n], v)):
                HATA.append(f'{ad} düğüm {n}: C++={c["named"][n]} py={v}')
        print(f'  {ad}: {c["nodes"]} düğüm, {c["tris"]} üçgen, {c["images"]} görsel, boyut {[round(h - l, 2) for l, h in zip(c["lo"], c["hi"])]} m')
    print(f'GLB sınaması: {len(HATA)} hata')
    for h in HATA[:30]:
        print('  ' + h)
    return 1 if HATA else 0


if __name__ == '__main__':
    sys.exit(main())
