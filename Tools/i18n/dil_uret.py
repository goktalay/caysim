#!/usr/bin/env python3
"""Content/Simumav/Lang/<dil>.json üretir (7 dil). Kaynak: Tools/i18n/metinler.py (tr+en) + SIMUMAV web çevirileri (eşleşen Türkçe/İngilizce
metin) + Tools/i18n/ceviriler.py (elle). Kullanım: python3 Tools/i18n/dil_uret.py [--simumav /opt/hepgo/fabrika-yonetim/static/simumav/app/i18n]"""
import argparse
import json
import os
import sys

KOK = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, KOK)
from metinler import M  # noqa: E402

DILLER = ['tr', 'en', 'de', 'fr', 'es', 'ru', 'ar']


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--simumav', default='/opt/hepgo/fabrika-yonetim/static/simumav/app/i18n')
    ap.add_argument('--rapor', action='store_true')
    a = ap.parse_args()
    web = {}
    if os.path.isdir(a.simumav):
        for d in DILLER:
            p = os.path.join(a.simumav, d + '.json')
            if os.path.isfile(p):
                web[d] = json.load(open(p, encoding='utf-8'))
    tr_idx, en_idx = {}, {}
    for k, v in web.get('tr', {}).items():
        tr_idx.setdefault(v.strip().casefold(), k)
    for k, v in web.get('en', {}).items():
        en_idx.setdefault(v.strip().casefold(), k)
    try:
        from ceviriler import C  # noqa: E402
    except ImportError:
        C = {}
    out = {d: {} for d in DILLER}
    eslesmedi = []
    for key, (tr, en) in M.items():
        out['tr'][key] = tr
        out['en'][key] = en
        wk = tr_idx.get(tr.strip().casefold()) or en_idx.get(en.strip().casefold())
        for d in DILLER[2:]:
            v = None
            if key in C and d in C[key]:
                v = C[key][d]
            elif wk and d in web and wk in web[d]:
                v = web[d][wk]
            if v is None:
                eslesmedi.append((key, d))
                continue
            out[d][key] = v
    # arayüz metinleri (7 dil hazır; metinler.py'de olmayan anahtarlar)
    try:
        from arayuz_metinleri import U  # noqa: E402
    except ImportError:
        U = {}
    for key, vals in U.items():
        if key in M:
            continue
        for i, d in enumerate(DILLER):
            out[d][key] = vals[i]
    hedef = os.path.normpath(os.path.join(KOK, '..', '..', 'Content', 'Simumav', 'Lang'))
    os.makedirs(hedef, exist_ok=True)
    for d in DILLER:
        with open(os.path.join(hedef, d + '.json'), 'w', encoding='utf-8') as f:
            json.dump(dict(sorted(out[d].items())), f, ensure_ascii=False, indent=1)
            f.write('\n')
    eksik_anahtar = sorted({k for k, _ in eslesmedi})
    print(f'{len(M)} anahtar; çevirisi eksik (İngilizceye düşer): {len(eslesmedi)} hücre, {len(eksik_anahtar)} anahtar')
    if a.rapor:
        for k in eksik_anahtar:
            print(k, '|', M[k][1])


if __name__ == '__main__':
    main()
