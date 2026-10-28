#!/usr/bin/env python3
"""MVT çözücüsü (Source/Simumav/Private/Core/SmvMvt) — mapbox_vector_tile kütüphanesine karşı. Kullanım:
python3 tests/cpp/test_mvt.py <karo.pbf ...>   (mapbox_vector_tile yoksa yalnız kırpma sınanır)"""
import os, shutil, subprocess, sys, tempfile
KOK = os.path.dirname(os.path.abspath(__file__))
CORE = os.path.normpath(os.path.join(KOK, '..', '..', 'Source', 'Simumav', 'Private', 'Core'))
if not shutil.which('g++'):
    print('ATLANDI: g++ yok'); sys.exit(0)
try:
    import mapbox_vector_tile as mvt
except ImportError:
    mvt = None
hata = 0
with tempfile.TemporaryDirectory() as d:
    exe = os.path.join(d, 'mvt')
    subprocess.check_call(['g++', '-std=c++17', '-O1', '-Wall', '-o', exe, os.path.join(KOK, 'test_mvt_main.cpp'), os.path.join(CORE, 'SmvMvt.cpp')])
    for yol in sys.argv[1:]:
        cikti = subprocess.check_output([exe, yol]).decode().split('\n')
        clip = [c for c in cikti if c.startswith('CLIP')][0].split()
        if clip[1:] != ['10000', '1', '0.0']:
            print('HATA kirpma', clip); hata += 1
        if not mvt:
            continue
        ref = mvt.decode(open(yol, 'rb').read(), default_options={'y_coord_down': True})
        biz = {c.split()[0]: c.split()[1:] for c in cikti if c and not c.startswith('CLIP')}
        for ad, katman in ref.items():
            nf = len(katman['features']); parca = nokta = 0; sx = sy = 0
            for f in katman['features']:
                g = f['geometry']; t = g['type']; c = g['coordinates']
                if t == 'Point': halkalar = [[c]]
                elif t == 'MultiPoint': halkalar = [[p] for p in c]
                elif t == 'LineString': halkalar = [c]
                elif t == 'MultiLineString': halkalar = c
                elif t == 'Polygon': halkalar = [r[:-1] for r in c]
                else: halkalar = [r[:-1] for p in c for r in p]
                for h in halkalar:
                    parca += 1; nokta += len(h)
                    for x, y in h: sx += x; sy += y
            b = biz.get(ad)
            if not b or int(b[0]) != nf or int(b[1]) != parca or int(b[2]) != nokta or abs(float(b[4]) - sx) > 1 or abs(float(b[5]) - sy) > 1:
                print('HATA', os.path.basename(yol), ad, 'biz', b, 'ref', nf, parca, nokta, sx, sy); hata += 1
        print(os.path.basename(yol), 'karşılaştırıldı:', len(ref), 'katman')
print('BAŞARISIZ' if hata else 'TAMAM', hata)
sys.exit(1 if hata else 0)
