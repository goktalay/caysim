#!/usr/bin/env python3
"""SmvCollision (Source/Simumav/Private/Core/SmvCollision.cpp) sınaması — Unreal'sız.

Fizik çarpışmasının saf mantığı: temas sınıflandırması (normal + çarpma hızı → sürtünme / sert / tavan / iniş / düşüş / su),
sürekli temas yükseltmesi, 360° yakınlık sektör eşlemesi, MAVLink mesafe kodlaması (cm; menzil dışı = azami + 1) ve SITL sensör
parametreleri. C++ g++ ile derlenir; sonuçlar bu dosyadaki bağımsız Python referansıyla ve elle yazılmış tablo durumlarıyla karşılaştırılır.
HEPGO eşikleri: flightsim_mission.SITL_HIT_HIZ_MIN = 3,0 m/s, flightsim3d.js COLL_SPEED_MIN = 2 m/s.
Kullanım:  python3 tests/cpp/test_carpisma.py
"""
import math
import os
import random
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
KOK = os.path.dirname(os.path.abspath(__file__))
KAYNAK = os.path.normpath(os.path.join(KOK, '..', '..', 'Source', 'Simumav', 'Private', 'Core', 'SmvCollision.cpp'))
SURUCU = os.path.join(KOK, 'test_carpisma_main.cpp')
HATA = []

# --- Python referansı (SmvCollision.h FContactCfg varsayılanları) ---
CFG = dict(floor=0.7, ceil=-0.7, graze=2.0, graze_t=8.0, land_vz=3.0, land_vt_v=3.0, land_vt_p=60.0, ceil_hard=1.5, water=0.0)
KRIZ = {'hard', 'ceiling_hit', 'fall', 'water'}


def sinifla(n, v, arac, su):
    ln = math.sqrt(sum(x * x for x in n))
    n = [x / ln for x in n] if ln > 1e-9 else [0.0, 0.0, 1.0]
    dot = sum(a * b for a, b in zip(v, n))
    vn = -dot
    t = [a - dot * b for a, b in zip(v, n)]
    vt = math.sqrt(sum(x * x for x in t))
    hiz = math.sqrt(sum(x * x for x in v))
    vnp = max(vn, 0.0)
    if su:
        s = 'water' if hiz >= CFG['water'] else 'landing'
    elif n[2] >= CFG['floor']:
        if arac == 2:
            s = 'none'
        else:
            vtm = CFG['land_vt_p'] if arac == 1 else CFG['land_vt_v']
            s = 'landing' if (vnp < CFG['land_vz'] and vt <= vtm) else 'fall'
    elif n[2] <= CFG['ceil']:
        s = 'ceiling_hit' if vnp >= CFG['ceil_hard'] else 'ceiling_touch'
    else:
        s = 'graze' if (vnp < CFG['graze'] and vt <= CFG['graze_t']) else 'hard'
    return s, vn, vt, hiz


def derle(hedef):
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-O1', '-o', hedef, SURUCU, KAYNAK], check=True)


def calistir(exe, girdi):
    return subprocess.run([exe], input=girdi, capture_output=True, text=True, check=True).stdout.splitlines()


def esit(etiket, a, b):
    if a != b:
        HATA.append(f'{etiket}: C++={a!r} beklenen={b!r}')


def main():
    with tempfile.TemporaryDirectory() as td:
        exe = os.path.join(td, 'carpisma')
        derle(exe)

        # 1) elle yazılmış durumlar (normal yüzeyden araca; z yukarı; hız m/s)
        tablo = [
            # (açıklama, normal, hız, araç, su, beklenen sınıf)
            ('duvara yavaş yanal temas', (1, 0, 0), (-1.2, 0.5, 0), 0, 0, 'graze'),
            ('duvara 5 m/s dalış', (1, 0, 0), (-5, 0, 0), 0, 0, 'hard'),
            ('duvara sıyırarak 12 m/s (pervane)', (1, 0, 0), (-0.5, 12, 0), 0, 0, 'hard'),
            ('tavana yavaş', (0, 0, -1), (0, 0, 0.8), 0, 0, 'ceiling_touch'),
            ('tavana hızlı', (0, 0, -1), (0, 0, 2.5), 0, 0, 'ceiling_hit'),
            ('çatıya yumuşak iniş', (0, 0, 1), (0.3, 0, -0.8), 0, 0, 'landing'),
            ('sert iniş sınırı altı', (0, 0, 1), (0, 0, -2.9), 0, 0, 'landing'),
            ('düşüş', (0, 0, 1), (0, 0, -3.2), 0, 0, 'fall'),
            ('kopter yerde savrulma', (0, 0, 1), (5, 0, -0.5), 0, 0, 'fall'),
            ('uçak iniş koşusu', (0, 0, 1), (22, 0, -1.2), 1, 0, 'landing'),
            ('uçak burun dalışı', (0, 0, 1), (20, 0, -6), 1, 0, 'fall'),
            ('kara aracı zemin', (0, 0, 1), (8, 0, -1), 2, 0, 'none'),
            ('kara aracı duvar', (0.9, 0.1, 0.2), (-4, 0, 0), 2, 0, 'hard'),
            ('su', (0, 0, 1), (3, 0, -0.5), 0, 1, 'water'),
            ('45° eğim (zemin sınırı üstü)', (0, 0.6, 0.8), (0, -1, -1), 0, 0, 'landing'),
            ('dik yamaç (duvar sayılır)', (0, 0.8, 0.6), (0, -3, -1), 0, 0, 'hard'),
            ('uzaklaşan temas', (1, 0, 0), (3, 0, 0), 0, 0, 'graze'),
            ('sıfır normal', (0, 0, 0), (0, 0, -1), 0, 0, 'landing'),
        ]
        girdi = ''.join(f'CLS {n[0]} {n[1]} {n[2]} {v[0]} {v[1]} {v[2]} {a} {s}\n' for _, n, v, a, s, _b in tablo)
        out = calistir(exe, girdi)
        for (ad, n, v, a, s, bek), satir in zip(tablo, out):
            p = satir.split()
            esit(f'tablo: {ad}', p[1], bek)
            esit(f'tablo kriz: {ad}', p[5] == '1', bek in KRIZ)

        # 2) rastgele: Python referansıyla birebir
        rnd = random.Random(20261023)
        durumlar = []
        for _ in range(3000):
            n = (rnd.uniform(-1, 1), rnd.uniform(-1, 1), rnd.uniform(-1, 1))
            v = (rnd.uniform(-15, 15), rnd.uniform(-15, 15), rnd.uniform(-8, 8))
            durumlar.append((n, v, rnd.randint(0, 2), 1 if rnd.random() < 0.05 else 0))
        out = calistir(exe, ''.join(f'CLS {n[0]!r} {n[1]!r} {n[2]!r} {v[0]!r} {v[1]!r} {v[2]!r} {a} {s}\n' for n, v, a, s in durumlar))
        for k, ((n, v, a, s), satir) in enumerate(zip(durumlar, out)):
            p = satir.split()
            sb, vn, vt, hz = sinifla(n, v, a, s)
            # eşik üstünde/altında kayan nokta farkı olmasın diye eşiklere çok yakın durumlar atlanır
            yakin = any(abs(x - e) < 1e-6 for x, e in ((max(vn, 0), CFG['graze']), (max(vn, 0), CFG['land_vz']), (max(vn, 0), CFG['ceil_hard']), (vt, CFG['graze_t']), (vt, CFG['land_vt_v'])))
            if not yakin:
                esit(f'rastgele #{k} sınıf', p[1], sb)
            for i, (c, py) in enumerate(((float(p[2]), vn), (float(p[3]), vt), (float(p[4]), hz))):
                if abs(c - py) > 1e-5:
                    HATA.append(f'rastgele #{k} alan {i}: C++={c} py={py}')

        # 3) sürekli temas yükseltmesi
        for ad, pen, sure, bek in (('graze', 0.2, 0.5, 'graze'), ('graze', 1.0, 0.1, 'hard'), ('graze', 0.35, 3.5, 'hard'), ('graze', 0.1, 9.0, 'graze'),
                                   ('ceiling_touch', 1.2, 0.0, 'ceiling_hit'), ('landing', 5.0, 9.0, 'landing')):
            esit(f'yükseltme {ad} {pen} {sure}', calistir(exe, f'ESC {ad} {pen} {sure}\n')[0].split()[1], bek)

        # 4) sektörler (72 × 5°, 8 × 45°; 0 = burun, saat yönü +)
        for n, b, bi in ((72, 0, 0), (72, 2.4, 0), (72, 2.6, 1), (72, 357.6, 0), (72, -3, 71), (72, 180, 36), (8, 22.4, 0), (8, 22.6, 1), (8, 270, 6), (8, 721, 0)):
            p = calistir(exe, f'SECT {n} {b}\n')[0].split()
            esit(f'sektör {n} {b}', int(p[1]), bi)
            esit(f'sektör merkezi {n} {b}', round(float(p[2]), 6), round(bi * 360.0 / n, 6))
        p = [float(x) for x in calistir(exe, 'SUB 0 8 5\n')[0].split()[1:]]
        esit('alt ışınlar 8/5', [round(x, 4) for x in p], [round(x % 360, 4) for x in (-18, -9, 0, 9, 18)])
        esit('alt ışın 72/1', calistir(exe, 'SUB 5 72 1\n')[0].split()[1:], ['25.000000'])

        # 5) mesafe kodlaması
        for d, bek in ((-1, 3001), (0.05, 20), (0.2, 20), (12.345, 1235), (30.0, 3000), (30.01, 3001), (99, 3001)):
            esit(f'mesafe {d}', int(calistir(exe, f'RNG {d} 0.2 30\n')[0].split()[1]), bek)
        p = [int(x) for x in calistir(exe, 'OBS 8 1.0 -1 0.1 29.999 31 5.555 0.2 2\n')[0].split()[1:]]
        esit('engel dizisi', p, [100, 3001, 20, 3000, 3001, 556, 20, 200] + [65535] * 64)

        # 6) SITL sensör parametreleri
        def parm(t, av):
            return dict(x.split('=') for x in calistir(exe, f'PARM {t} {av}\n')[0].split()[1:])
        q = parm('multirotor', 1)
        esit('kopter RNGFND', (q.get('RNGFND1_TYPE'), q.get('RNGFND1_ORIENT'), q.get('RNGFND1_MAX')), ('10', '25', '40'))
        esit('kopter PRX', (q.get('PRX1_TYPE'), q.get('AVOID_ENABLE')), ('2', '3'))
        esit('kopter kaçınma kapalı', parm('multirotor', 0).get('AVOID_ENABLE'), '1')
        u = parm('sabit_kanat', 1)
        esit('uçak: yakınlık yok, iniş menzil bulucu', ('PRX1_TYPE' in u, u.get('RNGFND_LANDING')), (False, '1'))
        r = parm('rover', 1)
        esit('kara aracı: menzil bulucu yok, yakınlık var', ('RNGFND1_TYPE' in r, r.get('PRX1_TYPE')), (False, '2'))

    for h in HATA[:40]:
        print('HATA', h)
    print(f'çarpışma çekirdeği sınaması: {len(HATA)} hata')
    return 1 if HATA else 0


if __name__ == '__main__':
    sys.exit(main())
