#!/usr/bin/env python3
"""Hazır FPV parkurlarını (sunucu fabrika-yonetim app/flightsim_fpv.py — kapılar, engeller, dekor, başlangıç, ortam) UE istemcisinin okuduğu
Content/Simumav/Fpv/courses.json dosyasına aktarır. Sunucu kodu değişince yeniden çalıştırın:
  cd /opt/hepgo/fabrika-yonetim && .venv/bin/python ~/mt-work/caysim/Tools/fpv/parkur_aktar.py"""
import json
import os
import sys

sys.path.insert(0, os.getcwd())
from app import flightsim_fpv as F  # noqa: E402

out = []
for cid in F.PARKUR_IDLER:
    c = dict(F.parkur(cid))
    c.pop('line', None)
    out.append(c)
hedef = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'Content', 'Simumav', 'Fpv', 'courses.json')
with open(hedef, 'w', encoding='utf-8') as f:
    json.dump({'v': 1, 'source': 'fabrika-yonetim app/flightsim_fpv.py', 'courses': out}, f, ensure_ascii=False, separators=(',', ':'))
print(len(out), 'parkur ->', os.path.normpath(hedef))
