#!/usr/bin/env python3
"""HEPGO bayram verisini (static/data/holidays/<yıl>.json; python-holidays ile üretilmiş) CAYSIM'e sıkıştırarak aktarır:
Content/Simumav/Holidays/<yıl>.json  →  {"y": yıl, "ll": {ülke: yerel dil}, "c": {ülke: [[tarih, tema, ad (en), yerel ad, yarım gün], ...]}}
Kullanım: python3 Tools/holiday/tatil_aktar.py [kaynak_klasör]"""
import json, os, sys

SRC = sys.argv[1] if len(sys.argv) > 1 else '/opt/hepgo/fabrika-yonetim/static/data/holidays'
DST = os.path.join(os.path.dirname(__file__), '..', '..', 'Content', 'Simumav', 'Holidays')
os.makedirs(DST, exist_ok=True)
for f in sorted(os.listdir(SRC)):
    if not f[:4].isdigit():
        continue
    d = json.load(open(os.path.join(SRC, f), encoding='utf-8'))
    out = {'y': d['year'], 'll': d.get('local_lang', {}), 'c': {}}
    for c, items in d.get('countries', {}).items():
        out['c'][c] = [[i['d'], i['t'], i.get('n', ''), (i.get('l') or {}).get('_', ''), 1 if i.get('h') else 0] for i in items]
    json.dump(out, open(os.path.join(DST, f), 'w', encoding='utf-8'), ensure_ascii=False, separators=(',', ':'))
    print(f, len(out['c']))
