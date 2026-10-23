#!/usr/bin/env python3
"""SIMUMAV C++ çekirdeği (Source/Simumav/Private/Core) ↔ sunucu referansı çapraz sınaması — Unreal'sız.

Referanslar (SALT OKUNUR; dosyaya yazılmaz, bytecode yazımı kapalı):
  <FY>/app/flightsim_match_core.py   halkalar, başlangıçlar, geçiş tespiti, it dalaşı
  <FY>/app/flightsim_power.py        güç / yakıt
  <FY>/app/flightsim_gpws.py         uçuş disiplini özeti / puan
  <FY>/app/flightsim_targets.py      hedef yörüngesi, önleme, angajman, puan
  <FY>/app/flightsim.py, drone_sitl.py, flightsim_icept.py  (yalnız AST ile seçilen saf fonksiyon/sabitler: launch, esinti, parm)
  <FY>/static/js/modules/flightsim_gpws.js   değerlendirici (Node ile)
<FY> = SIMUMAV_REF (varsayılan /opt/hepgo/fabrika-yonetim). Referans yoksa sınama atlanır (çıkış 0, "ATLANDI").
Kullanım: python3 tests/cpp/test_core.py
"""
import ast
import importlib.util
import json
import math
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
KOK = os.path.dirname(os.path.abspath(__file__))
CORE = os.path.normpath(os.path.join(KOK, '..', '..', 'Source', 'Simumav', 'Private', 'Core'))
FY = os.environ.get('SIMUMAV_REF', '/opt/hepgo/fabrika-yonetim')
HATA = []


def yukle(ad):
    yol = os.path.join(FY, 'app', ad + '.py')
    spec = importlib.util.spec_from_file_location('ref_' + ad, yol)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def ozut(dosya, adlar, ek=None):
    """Büyük modülden (paket içe aktarmaları olan) yalnız istenen üst düzey tanımları çalıştırır."""
    agac = ast.parse(open(os.path.join(FY, 'app', dosya), encoding='utf-8').read())
    ns = {'math': math, 're': re, **(ek or {})}
    for n in agac.body:
        hedef = None
        if isinstance(n, (ast.FunctionDef, ast.ClassDef)):
            hedef = n.name
        elif isinstance(n, ast.Assign) and len(n.targets) == 1 and isinstance(n.targets[0], ast.Name):
            hedef = n.targets[0].id
        elif isinstance(n, ast.Assign) and isinstance(n.targets[0], ast.Tuple):
            hedef = tuple(e.id for e in n.targets[0].elts if isinstance(e, ast.Name))
        if hedef in adlar or (isinstance(hedef, tuple) and set(hedef) & set(adlar)):
            exec(compile(ast.Module([n], []), dosya, 'exec'), ns)
    return ns


def esit(etiket, a, b, tol=0.0):
    if isinstance(a, float) or isinstance(b, float):
        if a is None or b is None or abs(float(a) - float(b)) > tol:
            HATA.append(f'{etiket}: C++={a!r} py={b!r}')
            return False
        return True
    if a != b:
        HATA.append(f'{etiket}: C++={a!r} py={b!r}')
        return False
    return True


def calistir(exe, girdi):
    return subprocess.run([exe], input=girdi, capture_output=True, text=True, check=True).stdout.splitlines()


def main():
    if not os.path.isdir(os.path.join(FY, 'app')):
        print(f'ATLANDI: referans depo yok ({FY})')
        return 0
    with tempfile.TemporaryDirectory() as td:
        exe = os.path.join(td, 'core')
        kaynaklar = [os.path.join(CORE, f) for f in sorted(os.listdir(CORE)) if f.endswith('.cpp')]
        subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-O1', '-o', exe, os.path.join(KOK, 'test_core_main.cpp'), *kaynaklar], check=True)
        mc = yukle('flightsim_match_core')
        pw = yukle('flightsim_power')
        gp = yukle('flightsim_gpws')
        tg = yukle('flightsim_targets')
        rnd = random.Random(99)

        # 1) Python uyumlu MT19937
        for seed in (1, 7, 12345, 2 ** 31 - 2, 2 ** 40 + 5):
            out = calistir(exe, f'RAND {seed} 20\nSHUF {seed} 13\n')
            r = random.Random(seed)
            py = [r.random() for _ in range(20)]
            c = [float(x) for x in out[0].split()[1:]]
            esit(f'random({seed})', c, py)
            r = random.Random(seed)
            v = list(range(13)); r.shuffle(v)
            esit(f'shuffle({seed})', [int(x) for x in out[1].split()[1:]], v)

        # 2) halkalar
        for k in range(40):
            tip = rnd.choice(['multirotor', 'sabit_kanat'])
            ayar = mc.halka_ayar_dogrula({'count': rnd.randint(1, 30), 'seed': rnd.randint(1, 2 ** 31 - 2),
                                          'radius_m': rnd.uniform(80, 3000), 'diameter_m': rnd.uniform(4, 80)}, tip)
            merkez = {'lat': rnd.uniform(-60, 60), 'lon': rnd.uniform(-170, 170), 'alt': rnd.choice([0.0, 812.4, None])}
            bas = [(rnd.uniform(-50, 50), rnd.uniform(-50, 50)) for _ in range(rnd.randint(0, 4))]
            py = mc.halkalar_uret(ayar, merkez, bas)
            g = f"RINGS {ayar['count']} {ayar['min_alt_m']!r} {ayar['max_alt_m']!r} {ayar['diameter_m']!r} {ayar['radius_m']!r} {ayar['seed']} " \
                f"{merkez['lat']!r} {merkez['lon']!r} {float(merkez['alt'] or 0)!r} {len(bas)} " + ' '.join(f'{x!r} {y!r}' for x, y in bas) + '\n'
            out = [s.split()[1:] for s in calistir(exe, g) if s.startswith('RING')]
            esit(f'halka sayısı #{k}', len(out), len(py))
            for h, c in zip(py, out):
                bek = [h['i'], h['lat'], h['lon'], h['x'], h['y'], h['agl_m'], h['alt_msl'], h['yaw_deg'], h['nx'], h['ny'], h['radius_m']]
                for ad, a, b in zip(['i', 'lat', 'lon', 'x', 'y', 'agl', 'alt_msl', 'yaw', 'nx', 'ny', 'r'], c, bek):
                    esit(f'halka #{k} {ad}', float(a), float(b), 1e-9)

        # 3) başlangıçlar + it dalaşı başlangıçları
        for n in range(1, 9):
            for tip, cl in (('multirotor', 'dikey'), ('sabit_kanat', 'ucak')):
                rwy = rnd.uniform(0, 360)
                hedef = (rnd.uniform(-300, 300), rnd.uniform(-300, 300)) if n % 2 else None
                py = mc.baslangic_noktalari(n, tip, rwy, hedef=hedef)
                g = f'STARTS {n} {cl} {rwy!r} ' + (f'1 {hedef[0]!r} {hedef[1]!r}' if hedef else '0') + '\n'
                out = [tuple(float(x) for x in s.split()[1:]) for s in calistir(exe, g) if s.startswith('ST ')]
                for a, b in zip(out, py):
                    for i in range(3):
                        esit(f'başlangıç n={n} {cl}', a[i], float(b[i]), 1e-9)
        for k in range(20):
            tip = rnd.choice(['multirotor', 'sabit_kanat'])
            ayar = mc.it_ayar_dogrula({'seed': rnd.randint(1, 2 ** 31 - 2), 'distance_m': rnd.uniform(300, 1500), 'roles': rnd.choice(mc.IT_ROLLER)}, tip)
            rwy = rnd.uniform(0, 360)
            py = mc.it_baslangiclari(ayar, tip, rwy)
            out = calistir(exe, f"DFSTART {ayar['distance_m']!r} {mc.sinif(tip)} {ayar['seed']} {rwy!r} {ayar['roles']}\n")[0].split()[1:]
            c, e = py[mc.KOVALAYAN], py[mc.KACAN]
            for a, b in zip(out[:6], [*c, *e]):
                esit(f'it başlangıç #{k}', float(a), float(b), 1e-9)
            if ayar['roles'] == 'random':
                esit(f'rol #{k}', out[6] == '1', random.Random(int(ayar['seed'])).random() < 0.5)

        # 4) geçiş izleyici
        for k in range(15):
            ayar = mc.halka_ayar_dogrula({'count': 6, 'seed': rnd.randint(1, 10 ** 6), 'radius_m': 200, 'diameter_m': 30}, 'multirotor')
            hs = mc.halkalar_uret(ayar, {'lat': 39.9, 'lon': 32.8, 'alt': 900.0}, [(0, 0)])
            # halkalardan geçen (ve bazen ıskalayan / ters geçen) gürültülü yol
            pts, t = [], 0.0
            prev = (0.0, 0.0, 905.0)
            for h in hs:
                for f in (-1.2, -0.4, 0.3, 1.1):
                    d = f * 20 * (1 if rnd.random() > 0.15 else -1)
                    p = (h['x'] + h['nx'] * d + rnd.uniform(-8, 8), h['y'] + h['ny'] * d + rnd.uniform(-8, 8), h['alt_msl'] + rnd.uniform(-10, 10))
                    t += 1.0
                    pts.append((*p, t))
                    prev = p
                if rnd.random() < 0.1:
                    t += 1
                    pts.append((prev[0] + 400, prev[1], prev[2], t))   # sıçrama
            iz = mc.HalkaIzleyici(hs)
            py_ev, py_d = [], []
            for i, (x, y, z, tt) in enumerate(pts):
                for e in iz.konum(x, y, z, tt):
                    py_ev.append((i, e['kind'], e.get('i', 0), round(e.get('t', 0.0), 3)))
                py_d.append(iz.mesafe)
            py_ev = [(a, b, c, 0.0 if b == 'wrong_way' else d) for a, b, c, d in py_ev]
            g = f'TRACK {len(hs)} ' + ' '.join(f"{h['i']} {h['x']!r} {h['y']!r} {h['alt_msl']!r} {h['nx']!r} {h['ny']!r} {h['radius_m']!r}" for h in hs) + \
                f' {len(pts)} ' + ' '.join(f'{x!r} {y!r} {z!r} {tt!r}' for x, y, z, tt in pts) + '\n'
            out = calistir(exe, g)
            c_ev = [(int(s.split()[1]), s.split()[2], int(s.split()[3]), 0.0 if s.split()[2] == 'wrong_way' else float(s.split()[4])) for s in out if s.startswith('EV ')]
            esit(f'geçiş olayları #{k}', c_ev, py_ev)
            c_d = [float(s.split()[2]) for s in out if s.startswith('D ')]
            for a, b in zip(c_d, py_d):
                esit(f'mesafe #{k}', a, b, 1e-6)

        # 5) it dalaşı izleyici
        for k in range(10):
            tip = rnd.choice(['multirotor', 'sabit_kanat'])
            a = mc.it_ayar_dogrula({'seed': k + 1}, tip)
            ref = mc.ItDalasi(a, 0.0)
            seq, t = [], 0.0
            cx, cy, ex, ey, z = -200.0, 0.0, 200.0, 0.0, 10.0
            for i in range(400):
                t += rnd.choice([0.1, 0.2, 0.3])
                z = min(z + 1.5, a['alt_m'] * 1.1)
                ex += rnd.uniform(-3, 6); ey += rnd.uniform(-4, 4)
                cx += (ex - cx) * 0.05 + rnd.uniform(-1, 1); cy += (ey - cy) * 0.05 + rnd.uniform(-1, 1)
                hc = math.degrees(math.atan2(ex - cx, ey - cy)) % 360 + rnd.uniform(-20, 20)
                for rol, x, y, h in ((mc.KOVALAYAN, cx, cy, hc), (mc.KACAN, ex, ey, rnd.uniform(0, 360))):
                    seq.append((rol, x, y, z, h, rnd.uniform(-0.3, 0.3), t))
            py_ev = []
            for i, (rol, x, y, zz, h, p, tt) in enumerate(seq):
                for e in ref.konum(rol, x, y, zz, h, p, tt):
                    py_ev.append((i, e['kind'], round(e.get('held_s', 0.0), 2)))
            rol_no = {mc.KOVALAYAN: 0, mc.KACAN: 1}
            g = f"DOGF {a['vclass']} {a['alt_m']!r} {a['range_m']!r} {a['cone_deg']!r} {a['hold_s']!r} {a['area_radius_m']!r} 0 {len(seq)} " + \
                ' '.join(f'{rol_no[r]} {x!r} {y!r} {zz!r} {h!r} {p!r} {tt!r}' for r, x, y, zz, h, p, tt in seq) + '\n'
            out = calistir(exe, g)
            c_ev = [(int(s.split()[1]), s.split()[2], float(s.split()[3])) for s in out if s.startswith('EV ')]
            esit(f'it dalaşı olayları #{k}', c_ev, py_ev)

        # 6) güç / yakıt
        for k in range(12):
            tip = rnd.choice(['sabit_kanat', 'multirotor', 'interceptor', 'rover'])
            ayar = pw.dogrula({'fuel_l': rnd.choice([0, 0.05, 2.0]), 'elec_mah': rnd.choice([100, 2000, 10000])}, tip)
            ref = pw.GucDurumu(ayar)
            satir, beklenen = [], []
            tbm = rnd.randint(0, 5000)
            servo = [1500] * 8
            for i in range(600):
                tbm += rnd.choice([20, 20, 40, 100, 7000 if rnd.random() < 0.01 else 20])
                if rnd.random() < 0.005:
                    tbm = rnd.randint(0, 100)
                servo = [max(800, min(2200, s + rnd.randint(-80, 80))) for s in servo]
                te = {'armed': i > 30 and rnd.random() > 0.05, 'groundspeed': rnd.uniform(0, 40), 'throttle': rnd.uniform(0, 100), 'servo': servo}
                biten = ref.adim(te, tbm)
                f = ref.tele_alanlari()
                beklenen.append((biten, f))
                satir.append(f"{1 if te['armed'] else 0} {te['groundspeed']!r} {te['throttle']!r} {tbm} " + ' '.join(map(str, servo)))
            ms = ayar['max_speed_ms'] if ayar['max_speed_ms'] is not None else -1
            g = f"POWER {ms!r} {ayar['fuel_l']!r} {ayar['fuel_km_per_l']!r} {ayar['elec_cells']} {ayar['elec_mah']!r} {ayar['elec_w']!r} {len(satir)} " + ' '.join(satir) + '\n'
            out = calistir(exe, g)
            c_bos = [(int(s.split()[1]), s.split()[2]) for s in out if s.startswith('EMPTY')]
            py_bos = [(i, b) for i, (bl, _) in enumerate(beklenen) for b in bl]
            esit(f'güç biten #{k}', c_bos, py_bos)
            pts = [s.split()[2:] for s in out if s.startswith('PT ')]
            for i, (c, (_, f)) in enumerate(zip(pts, beklenen)):
                if f['fuel'] is not None:
                    fu = f['fuel']
                    for a, b in zip(c[:5], [fu['l'], fu['pct'], fu['lpm'], fu['range_km'], 1 if fu['empty'] else 0]):
                        esit(f'yakıt #{k}.{i}', float(a), float(b), 1e-9)
                    c = c[5:]
                else:
                    c = c[1:]
                if f['elec'] is not None:
                    el = f['elec']
                    for a, b in zip(c, [el['wh'], el['pct'], el['w'], el['min_left'], el['v'], 1 if el['empty'] else 0]):
                        esit(f'batarya #{k}.{i}', None if a == 'null' else float(a), None if b is None else float(b), 1e-9)
            oz = ref.ozet()
            ps = [s for s in out if s.startswith('PS ')][0].split()[1:]
            bek = [oz['dist_m'], oz['time_s'], oz['armed_s'], oz['max_speed_seen'], ref.fuel_used,
                   oz['fuel']['km_per_l'] if oz['fuel'] else None, oz['elec']['w_avg'] if oz['elec'] else None]
            for ad, a, b in zip(['dist', 'time', 'armed', 'vmax', 'used', 'kml', 'wavg'], ps, bek):
                if ad == 'used':
                    esit(f'özet {ad} #{k}', float(a), round(b, 3), 1e-9)
                elif ad in ('kml', 'wavg') and (b is None or a == 'null'):
                    esit(f'özet {ad} #{k}', a == 'null', b is None)
                else:
                    esit(f'özet {ad} #{k}', float(a), float(b), 1e-9)

        # 7) uçuş disiplini özeti
        for k in range(30):
            ev = [{'code': rnd.choice(list(gp.KODLAR)), 't0': round(rnd.uniform(0, 900), 2), 'start_s': round(rnd.uniform(0, 900), 2), 'dur_s': round(rnd.uniform(0, 30), 2)}
                  for _ in range(rnd.randint(0, 25))]
            mod = rnd.choice(['full', 'full', 'visual', 'off'])
            py = gp.ozetle(ev, mod)
            out = calistir(exe, f'GSUM {mod} {len(ev)} ' + ' '.join(f"{e['code']} {e['t0']!r} {e['start_s']!r} {e['dur_s']!r}" for e in ev) + '\n')
            gs = out[0].split()[1:]
            esit(f'puan #{k}', int(gs[0]) if gs[0] != '-1' else None, py['score'])
            esit(f'not #{k}', None if gs[1] == 'null' else gs[1], py['grade'])
            esit(f'uyarı/dikkat #{k}', (int(gs[2]), int(gs[3])), (py['warnings'], py['cautions']))
            esit(f'toplam #{k}', float(gs[4]), py['total_s'], 1e-9)
            for s in out:
                if s.startswith('BC '):
                    _, kod, n, tot, first = s.split()
                    b = py['by_code'][kod]
                    esit(f'by_code {kod} #{k}', (int(n), float(tot), float(first)), (b['n'], b['total_s'], b['first_s']))

        # 8) GPWS değerlendirici (Node + JS modülü)
        node = shutil.which('node')
        js = os.path.join(FY, 'static', 'js', 'modules', 'flightsim_gpws.js')
        if node and os.path.isfile(js):
            for k in range(25):
                arac = rnd.choice(['multirotor', 'sabit_kanat', 'vtol', 'interceptor', 'helikopter', 'rover'])
                cfg = {'mode': rnd.choice(['full', 'full', 'visual', 'off']), 'sens': rnd.choice(['low', 'normal', 'high']),
                       'bank_copter': rnd.randint(20, 60), 'bank_plane': rnd.randint(30, 70), 'off': []}
                maxs = rnd.choice([None, 30.0, 60.0])
                fence = rnd.choice([None, 300.0])
                kareler, t, agl, armed = [], 0.0, 0.0, False
                modlar = ['STABILIZE', 'LOITER', 'RTL', 'LAND', 'AUTO', 'FBWA', 'QHOVER', 'TAKEOFF', 'ACRO', None]
                mode = rnd.choice(modlar)
                for i in range(700):
                    t += rnd.choice([0.02, 0.05, 0.1, 0.2])
                    if i == 20:
                        armed = True
                    if rnd.random() < 0.01:
                        armed = not armed
                    if rnd.random() < 0.02:
                        mode = rnd.choice(modlar)
                    if rnd.random() < 0.003:
                        t -= 2.0   # zaman geri gitti (Sıfırla)
                    vs = rnd.uniform(-15, 8) if rnd.random() < 0.3 else rnd.uniform(-2, 3)
                    agl = max(0.0, agl + vs * 0.1 + (2 if armed and agl < 30 else 0))
                    rates = None if rnd.random() < 0.2 else {'p': rnd.uniform(-500, 500) if rnd.random() < 0.1 else rnd.uniform(-60, 60),
                                                             'q': rnd.uniform(-60, 60), 'r': rnd.uniform(-250, 250) if rnd.random() < 0.1 else rnd.uniform(-30, 30)}
                    I = {'t': round(t, 3), 'armed': armed, 'mode': mode, 'agl': None if rnd.random() < 0.05 else round(agl, 2),
                         'aglAhead': None if rnd.random() < 0.5 else round(max(0.0, agl - rnd.uniform(-5, 60)), 2), 'vs': round(vs, 2),
                         'gs': None if rnd.random() < 0.05 else round(rnd.uniform(0, 45), 2), 'as': None if rnd.random() < 0.3 else round(rnd.uniform(0, 45), 2),
                         'roll': round(rnd.uniform(-80, 80), 2), 'pitch': round(rnd.uniform(-50, 50), 2) if rnd.random() > 0.05 else None,
                         'rates': rates, 'battPct': None if rnd.random() < 0.3 else round(rnd.uniform(0, 100), 1), 'battEmpty': rnd.random() < 0.02,
                         'fuelPct': None if rnd.random() < 0.5 else round(rnd.uniform(0, 100), 1), 'fuelEmpty': rnd.random() < 0.02,
                         'gpsFix': rnd.choice([None, 0, 2, 3, 6, 6, 6]), 'sats': 12, 'sensBad': rnd.choice([0, 0, 0, 0, 1, 0x200000]),
                         'ekf': None if rnd.random() < 0.5 else round(rnd.uniform(0, 1), 2), 'vibe': None if rnd.random() < 0.5 else round(rnd.uniform(0, 40), 1),
                         'wind': None if rnd.random() < 0.3 else round(rnd.uniform(0, 20), 1), 'motorCut': rnd.random() < 0.05,
                         'phase': rnd.choice([None, None, None, 'armed', 'launch', 'released']), 'distHome': None if rnd.random() < 0.3 else round(rnd.uniform(0, 500), 1),
                         'linkAge': None if rnd.random() < 0.3 else rnd.choice([100, 900, 2000]), 'crashed': rnd.random() < 0.01}
                    kareler.append(I)
                betik = os.path.join(td, f'gpws_{k}.mjs')
                with open(betik, 'w') as f:
                    f.write(f"import {{ createGpws }} from {json.dumps('file://' + js)};\n"
                            f"const g = createGpws({{ vehicle: {json.dumps(arac)}, cfg: {json.dumps(cfg)}, maxSpeed: {json.dumps(maxs)}, fence: {json.dumps(fence)} }});\n"
                            f"const kareler = {json.dumps(kareler)};\nconst akt = [];\n"
                            "for (const I of kareler) { const r = g.step(I); akt.push(r.active.map(a => a.code)); }\n"
                            "g.close();\nconsole.log(JSON.stringify({ akt, eps: g.episodes() }));\n")
                js_out = json.loads(subprocess.run([node, betik], capture_output=True, text=True, check=True).stdout)

                def tok(v):
                    if v is None:
                        return 'null'
                    if isinstance(v, bool):
                        return '1' if v else '0'
                    return repr(v)
                satirlar = []
                for I in kareler:
                    r = I['rates']
                    satirlar.append(' '.join([tok(I['t']), tok(I['armed']), I['mode'] or 'null', tok(I['agl']), tok(I['aglAhead']), tok(I['vs']), tok(I['gs']), tok(I['as']),
                                              tok(I['roll']), tok(I['pitch']), tok(r['p'] if r else None), tok(r['q'] if r else 0), tok(r['r'] if r else 0),
                                              tok(I['battPct']), tok(I['battEmpty']), tok(I['fuelPct']), tok(I['fuelEmpty']), tok(I['gpsFix']), tok(I['sats']),
                                              tok(I['sensBad']), tok(I['ekf']), tok(I['vibe']), tok(I['wind']), tok(I['motorCut']), I['phase'] or 'null',
                                              tok(I['distHome']), tok(I['linkAge']), tok(I['crashed'])]))
                g = f"GPWS {arac} {cfg['mode']} {cfg['sens']} {cfg['bank_copter']} {cfg['bank_plane']} {tok(maxs)} {tok(fence)} {len(kareler)}\n" + '\n'.join(satirlar) + '\n'
                out = calistir(exe, g)
                c_akt = [s.split()[2:] for s in out if s.startswith('A ')]
                fark = [i for i, (a, b) in enumerate(zip(c_akt, js_out['akt'])) if a != b]
                if fark:
                    i = fark[0]
                    HATA.append(f'GPWS etkin #{k} ({arac}) kare {i}: C++={c_akt[i]} js={js_out["akt"][i]}')
                c_ep = [(s.split()[1], *map(float, s.split()[2:])) for s in out if s.startswith('EP ')]
                j_ep = [(e['code'], float(e['t0']), float(e['start_s']), float(e['dur_s'])) for e in js_out['eps']]
                esit(f'GPWS bölümler #{k} ({arac})', c_ep, j_ep)
        else:
            print('UYARI: node ya da flightsim_gpws.js yok — GPWS değerlendirici sınaması atlandı')

        # 9) hedefler
        for k in range(10):
            n = rnd.randint(1, 4)
            items = [{'i': i + 1, 'name': f'T{i + 1}', 'model': 'm', 'alt_m': rnd.uniform(10, 200), 'diameter_m': rnd.uniform(50, 600), 'speed_ms': rnd.uniform(5, 40),
                      'east_m': rnd.uniform(-200, 200), 'north_m': rnd.uniform(-200, 200), 'phase_deg': (i * 137.5) % 360, 'dir': rnd.choice(['cw', 'ccw'])} for i in range(n)]
            cfg = {'radius_m': rnd.choice([10.0, 25.0, 60.0]), 'items': items}
            ref = tg.HedefIzleyici(cfg)
            ops, py_ev = [], []
            t = 0.0
            for j in range(900):
                if rnd.random() < 0.01:
                    i = rnd.randint(1, n); ops.append(f'L {i}')
                    for e in ref.kilit_ac(i):
                        py_ev.append((len(ops) - 1, e['kind'], e['i']))
                    continue
                if rnd.random() < 0.004:
                    ops.append('R lost')
                    for e in ref.kilit_birak('lost'):
                        py_ev.append((len(ops) - 1, e['kind'], e['i'], e['reason']))
                    continue
                t += 0.1 if rnd.random() > 0.002 else -3
                h = items[rnd.randint(0, n - 1)]
                st = tg.durum(h, t)
                x, y, z = st['x'] + rnd.uniform(-40, 40), st['y'] + rnd.uniform(-40, 40), st['z'] + rnd.uniform(-20, 20)
                arm = j > 50
                ops.append(f'S {t!r} {x!r} {y!r} {z!r} {1 if arm else 0}')
                for e in ref.adim(t, x, y, z, arm):
                    if 'kind' in e:
                        py_ev.append((len(ops) - 1, e['kind'], e['i'], e['reason']))
                        continue
                    py_ev.append((len(ops) - 1, 'intercept', e['i'], round(e['dist_m'], 1), e['n']))
                    if e.get('engagement'):
                        py_ev.append((len(ops) - 1, 'success', e['i']))
            g = f"TGT {n} {cfg['radius_m']!r} " + ' '.join(f"{h['i']} {h['alt_m']!r} {h['diameter_m']!r} {h['speed_ms']!r} {h['east_m']!r} {h['north_m']!r} {h['phase_deg']!r} {1 if h['dir'] == 'cw' else 0}" for h in items) + \
                f' {len(ops)} ' + ' '.join(ops) + '\n'
            out = calistir(exe, g)
            c_ev = []
            for s in out:
                if s.startswith('EV '):
                    p = s.split()
                    if p[2] == 'intercept':
                        c_ev.append((int(p[1]), 'intercept', int(p[3]), float(p[4]), int(p[5])))
                    elif p[2] == 'cut' and len(p) > 4:
                        c_ev.append((int(p[1]), 'cut', int(p[3]), p[4]))
                    elif p[2] in ('start', 'cut', 'success'):
                        c_ev.append((int(p[1]), p[2], int(p[3])))
            py_ev2 = [e for e in py_ev if not (e[1] == 'cut' and len(e) == 3)]
            c_ev2 = [e for e in c_ev if not (e[1] == 'cut' and len(e) == 3)]
            esit(f'hedef olayları #{k}', c_ev2, py_ev2)
            oz = ref.ozet()
            sc = [s for s in out if s.startswith('SC ')][0].split()[1:]
            esit(f'hedef puanı #{k}', (int(sc[0]), sc[1:]), (oz['score'], oz['badges']))
            for s in out:
                if s.startswith('TS '):
                    _, i, x, y, hd, roll = s.split()
                    st = tg.durum(items[int(i) - 1], 12.5)
                    for a, b in zip([x, y, hd, roll], [st['x'], st['y'], st['heading'], st['roll']]):
                        esit(f'hedef durumu #{k}', float(a), b, 1e-5)

        # 10) SITL yapılandırması (AST özütü: saf fonksiyon ve sabitler)
        fs = ozut('flightsim.py', {'esinti_carpani', 'launch_parametreleri', 'PLANE_YER_PARAMLARI', 'RAMP_ACCEL', 'LAUNCH_SERVO', 'LAYOUT_PARAMLARI'})
        for tt in [0.0, 0.37, 5.0, 11.3, 123.456, 999.9]:
            out = calistir(exe, f'GUST {tt!r}\n')[0].split()[1]
            esit(f'esinti {tt}', float(out), round(1.0 * fs['esinti_carpani'](tt), 2), 1e-9)
        for m in ('wheels', 'ramp', 'vehicle'):
            for alt in (-1, 60.0):
                py = fs['launch_parametreleri']({'method': m, 'angle_deg': 22.5, 'release_kmh': 81.0}, None if alt < 0 else alt)
                out = [s.split()[1:] for s in calistir(exe, f'LAUNCH {m} 22.5 81 {alt}\n') if s.startswith('LP ')]
                esit(f'kalkış seti {m}', [(a, round(float(b), 6)) for a, b in out], [(a, round(float(b), 6)) for a, b in py])
        ic = ozut('flightsim_icept.py', {'PARM', 'parm_satirlari'})
        ds = ozut('drone_sitl.py', {'ROVER_PARM', 'ROVER_SKID_PARM', 'ROVER_ACKERMANN_PARM', 'ROVER_SKID_MODEL', 'rover_parametreleri'})

        def parm_ciftleri(satirlar):
            out = []
            for s in satirlar:
                s = s.split('#')[0].strip()
                if s:
                    a, b = s.split()[:2]
                    out.append((a, round(float(b), 6)))
            return out
        taban = [('FRAME_CLASS', 1.0), ('FS_GCS_ENABLE', 0.0), ('MAV_SYSID', 1.0), ('ARMING_SKIPCHK', 16.0), ('LOG_BACKEND_TYPE', 1.0), ('LOG_DISARMED', 0.0), ('LOG_FILE_DSRMROT', 1.0)]
        for tip, model, bek in (('interceptor', 'quadplane-copter_tailsitter', parm_ciftleri(ic['parm_satirlari']())),
                                ('rover', 'rover', [(a, round(float(v), 6)) for a, v in ds['rover_parametreleri']('rover')]),
                                ('rover', 'rover-skid', [(a, round(float(v), 6)) for a, v in ds['rover_parametreleri']('rover-skid')])):
            drive = 'skid' if model == 'rover-skid' else 'ackermann'
            out = [s[3:] for s in calistir(exe, f'PARM {tip} classic {drive} 1 1 0\n') if s.startswith('PL ')]
            esit(f'parm {model}', parm_ciftleri(out), taban + bek)
        sf = ozut('flightsim.py', {'LAYOUT_PARAMLARI'})
        for lay in ('classic', 'elevon', 'vtail'):
            out = [s[3:] for s in calistir(exe, f'PARM sabit_kanat {lay} ackermann 1 1 1 SERVO4_REVERSED 1\n') if s.startswith('PL ')]
            bek = taban + [('SERVO4_REVERSED', 1.0)] + [(a, round(float(v), 6)) for a, v in sf['LAYOUT_PARAMLARI'][lay] if a != 'SERVO4_REVERSED']
            esit(f'parm düzen {lay}', parm_ciftleri(out), bek)
        cl = calistir(exe, 'CMDLINE sabit_kanat vtail ramp 39.1234567 32.7654321 912.34 370 15670\n')[0].split()[1:]
        esit('komut satırı', cl, ['--model', 'plane-vtail-catapult', '--home', '39.1234567,32.7654321,912.3,10', '--base-port', '15670', '--defaults', 'D', '--sysid', '1'])

        # 11) yapay zekâ pilot duman sınaması
        for lv in ('easy', 'normal', 'hard'):
            for cl in ('dikey', 'ucak'):
                b = calistir(exe, f'BOT {lv} {cl}\n')[0].split()
                gecen, toplam = map(int, b[3].split('/'))
                if gecen < toplam:
                    HATA.append(f'bot {lv}/{cl} parkuru bitiremedi: {b[3]} ({b[4]} sn)')
                else:
                    print(f'  bot {lv}/{cl}: {b[3]} halka, {b[4]} sn')

    for h in HATA[:40]:
        print('HATA', h)
    print(f'çekirdek sınaması: {len(HATA)} hata')
    return 1 if HATA else 0


if __name__ == '__main__':
    sys.exit(main())
