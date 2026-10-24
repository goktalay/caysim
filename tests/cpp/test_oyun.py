#!/usr/bin/env python3
"""SmvProgress (pilot seviye / XP / manevra) + SmvGameRules (yük bırakma, ilaçlama, savunma) sınaması — Unreal'sız.

Seviye eğrisi sunucu app/simumav/xp.py ile birebir: gereken(L) = round(100 × (L−1)^1.8); madalya eşikleri 5/10/25/50/75/100.
Tür puanı bileşenleri app/simumav/modes.py (ucus_bilesenleri, puanla, mac_puani) kurallarıyla aynı. Sunucu kaynağı varsa
(SIMUMAV_SUNUCU, varsayılan /home/gelistirici/mt-work/fs-wt/smvapi ya da /opt/hepgo/fabrika-yonetim) eğri oradan da karşılaştırılır.
Kullanım: python3 tests/cpp/test_oyun.py
"""
import importlib.util
import os
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
KOK = os.path.dirname(os.path.abspath(__file__))
CORE = os.path.normpath(os.path.join(KOK, '..', '..', 'Source', 'Simumav', 'Private', 'Core'))
HATA = []


def esit(etiket, a, b):
    if a != b:
        HATA.append(f'{etiket}: {a!r} != {b!r}')


def sunucu_xp():
    for kok in (os.environ.get('SIMUMAV_SUNUCU', ''), '/home/gelistirici/mt-work/fs-wt/smvapi', '/opt/hepgo/fabrika-yonetim'):
        yol = os.path.join(kok, 'app', 'simumav', 'xp.py') if kok else ''
        if yol and os.path.isfile(yol):
            src = open(yol, encoding='utf-8').read()
            # yalnız saf işlev (gereken) — modül bağımlılıkları (db, modes) yüklenmez
            ns = {}
            bas = src.index('def gereken')
            son = src.index('\n\n\n', bas)
            exec('MAX_LEVEL = 100\n' + src[bas:son], ns)
            return ns['gereken']
    return None


def main():
    with tempfile.TemporaryDirectory() as d:
        exe = os.path.join(d, 'oyun')
        subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-O1', '-o', exe, os.path.join(KOK, 'test_oyun_main.cpp'),
                        os.path.join(CORE, 'SmvProgress.cpp'), os.path.join(CORE, 'SmvGameRules.cpp')], check=True)
        out = subprocess.run([exe], capture_output=True, text=True, check=True).stdout.splitlines()
    sat = [s.split() for s in out]
    egri = {int(s[1]): int(s[2]) for s in sat if s[0] == 'curve'}
    ref = sunucu_xp()
    for L in range(1, 101):
        beklenen = int(round(100 * (L - 1) ** 1.8))
        esit(f'eğri L{L}', egri.get(L), beklenen)
        if ref:
            esit(f'eğri (sunucu) L{L}', egri.get(L), ref(L))
    for s in sat:
        if s[0] == 'level':
            xp, lv = int(s[1]), int(s[2])
            bek = max(L for L in range(1, 101) if xp >= int(round(100 * (L - 1) ** 1.8)))
            esit(f'seviye {xp}', lv, bek)
    esit('madalya', [(s[1], int(s[2])) for s in sat if s[0] == 'medal'],
         [('wings_bronze', 5), ('wings_silver', 10), ('wings_gold', 25), ('wings_platinum', 50), ('wings_diamond', 75), ('wings_legend', 100)])
    k = {(s[0], s[1]): int(s[2]) for s in sat if s[0] in ('free', 'rings', 'cargo', 'defense')}
    esit('serbest hava', k[('free', 'air_time')], min(270, 400 // 10))
    esit('serbest mesafe', k[('free', 'distance')], 200)
    esit('serbest disiplin', k[('free', 'discipline')], int(88 * 0.5))
    esit('serbest iniş', k[('free', 'soft_landing')], 40)
    esit('halka ×0,5', (k[('rings', 'rings')], k[('rings', 'match_rank')], k[('rings', 'win')]), (20, 50, 25))
    esit('yük isabet', (k[('cargo', 'release')], k[('cargo', 'accuracy')]), (40, 120))
    esit('savunma', k[('defense', 'defense')], 380)
    man = {s[1]: (int(s[2]), int(s[3])) for s in sat if s[0] == 'man'}
    for ad in ('sharp_turn', 'roll', 'loop', 'climb', 'dive', 'low_pass', 'hover', 'soft_landing'):
        if ad not in man:
            HATA.append(f'manevra tanınmadı: {ad}')
    v = {s[0]: s[1:] for s in sat}
    esit('yük bölge', int(v['drop_zones'][0]), 4)
    inis = [s for s in sat if s[0] == 'drop_land']
    esit('yük iniş sayısı', len(inis), 2)
    if inis and float(inis[0][2]) > 1.0:
        HATA.append(f'dik bırakılan yük bölge merkezine düşmeli: {inis[0][2]}')
    if float(v['spray_cov'][0]) < 0.95:
        HATA.append(f'ilaçlama kapsaması düşük: {v["spray_cov"][0]}')
    di = v['def_idle']
    esit('savunma boşta: sağlık', int(di[1]), 0)
    esit('savunma boşta: başarı', int(di[9]), 0)
    da = v['def_active']
    esit('savunma etkin: etkisiz', int(da[3]), 9)
    esit('savunma etkin: başarı', int(da[9]), 1)
    if abs(float(v['ship_e'][0]) - 30.0) > 0.01:
        HATA.append(f'gemi hareketi: {v["ship_e"]}')
    esit('tür kimlikleri', v['kinds'], ['cargo_drop', 'ship_defense', 'free', '1', '1', '9'])
    for h in HATA:
        print('HATA', h)
    print(f'oyun / ilerleme çekirdeği sınaması: {len(HATA)} hata' + (' (sunucu eğrisiyle karşılaştırıldı)' if ref else ''))
    return 1 if HATA else 0


if __name__ == '__main__':
    sys.exit(main())
