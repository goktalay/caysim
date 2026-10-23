#!/usr/bin/env python3
"""Simumav (UE istemcisi) ↔ SIMUMAV sunucusu SÖZLEŞME TESTİ — Unreal olmadan.

Ne yapar:
  A) Statik eşleme: C++ kaynağındaki tüm "/api/..." ve "/ws/..." yolları sunucu yönlendiricilerinde (app/simumav/routers/*.py) var mı;
     C++ hangar kataloğundaki model anahtarları sunucu kataloğunda (bridge.GLB_MODELS/PROC_MODELS) var mı.
  B) Canlı: SIMUMAV'ı bu makinede GEÇİCİ veri diziniyle başlatır (uvicorn app.simumav.main:app; dev giriş, sahte kinematik HEPGO_SITL=0),
     C++ istemcisinin gönderdiği JSON gövdelerinin birebir kopyalarıyla (SimumavTypes.cpp ToJson/ToStartJson) uçları çağırır, yanıt
     alanlarının C++ FromJson'ların okuduğu adlarla geldiğini doğrular; WS /ws/fly/{sim_id} (Cookie: smsid) hello + tele alanları,
     rc, cmd/res; stop + özet; WS /ws/match yapay zekâ karşılaşması; CSRF (Origin'siz yazma 403).
Referans depo SALT OKUNUR (SIMUMAV_REF, varsayılan /opt/hepgo/fabrika-yonetim); bytecode yazılmaz; sunucu süreci test sonunda kapanır.
Kullanım:  /opt/hepgo/fabrika-yonetim/.venv/bin/python -I tests/test_sozlesme_simumav.py      (requests + websockets gerekir)
"""
import json
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time

sys.dont_write_bytecode = True
KOK = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
FY = os.environ.get('SIMUMAV_REF', '/opt/hepgo/fabrika-yonetim')
HATA = []


def esit(etiket, kosul, ayrinti=''):
    if not kosul:
        HATA.append(f'{etiket} {ayrinti}')
        print('  HATA', etiket, ayrinti)
    else:
        print('  ok  ', etiket)


# ---------------------------------------------------------------------------
# A) statik eşleme
# ---------------------------------------------------------------------------

def sunucu_yollari():
    yollar = []
    rd = os.path.join(FY, 'app', 'simumav', 'routers')
    for f in sorted(os.listdir(rd)):
        if not f.endswith('.py'):
            continue
        t = open(os.path.join(rd, f), encoding='utf-8').read()
        on = {m.group(1): m.group(2) for m in re.finditer(r"^(\w+)\s*=\s*APIRouter\((?:prefix=)?'([^']*)'", t, flags=re.M)}
        on.update({m.group(1): '' for m in re.finditer(r'^(\w+)\s*=\s*APIRouter\(\)', t, flags=re.M)})
        for m in re.finditer(r"@(\w+)\.(get|post|put|delete|websocket|api_route)\('([^']*)'", t):
            yollar.append((m.group(2), on.get(m.group(1), '') + m.group(3)))
    return yollar


def cpp_yollari():
    out = set()
    for kok, _, ds in os.walk(os.path.join(KOK, 'Source', 'Simumav')):
        for f in ds:
            if f.endswith(('.cpp', '.h')):
                t = open(os.path.join(kok, f), encoding='utf-8').read()
                for m in re.finditer(r'TEXT\("(/(?:api|ws)/[^"?]*)', t):
                    out.add(m.group(1))
    return out


def yol_eslesir(cpp, sunucu):
    # C++ printf kalıbı (%s) ↔ sunucu {param}
    c = re.sub(r'%[sd]', 'X', cpp).rstrip('/')
    for _, s in sunucu:
        rx = '^' + re.sub(r'\\\{[^}]+\\\}', '[^/]+', re.escape(s.rstrip('/'))) + '$'
        if re.match(rx, c):
            return True
    return False


def statik():
    print('A) statik eşleme')
    sy = sunucu_yollari()
    for p in sorted(cpp_yollari()):
        if p in ('/api/', '/ws/'):
            continue
        esit(f'yol {p}', yol_eslesir(p, sy), '— sunucuda karşılığı yok')
    t = open(os.path.join(FY, 'app', 'simumav', 'bridge.py'), encoding='utf-8').read()
    sunucu_modeller = set(re.findall(r"'((?:glb|proc):[a-z0-9_]+)'\s*:\s*\{", t))
    cpp = open(os.path.join(KOK, 'Source', 'Simumav', 'Private', 'SmvDataService.cpp'), encoding='utf-8').read()
    cpp_modeller = set(re.findall(r'Add\(TEXT\("((?:glb|proc):[a-z0-9_]+)"\)', cpp))
    esit('katalog anahtarları sunucuda', cpp_modeller <= sunucu_modeller, str(sorted(cpp_modeller - sunucu_modeller)))
    esit('sunucu GLB modelleri C++ kataloğunda', {m for m in sunucu_modeller if m.startswith('glb:')} <= cpp_modeller,
         str(sorted({m for m in sunucu_modeller if m.startswith('glb:')} - cpp_modeller)))


# ---------------------------------------------------------------------------
# C++ JSON gövdelerinin birebir kopyaları (SimumavTypes.cpp)
# ---------------------------------------------------------------------------

def drone_create_json(name='Iris', typ='multirotor', model='glb:iris', color='#e53935', params=None):   # FSmvDrone::ToJson(true)
    o = {'name': name[:40], 'type': typ, 'model_key': model, 'color': color, 'params': params or {}, 'power': {'elec_cells': 4, 'elec_mah': 5000.0}}
    if typ == 'plane':
        o['airframe'] = 'classic'
    return o


def pilot_json(name='Ali'):   # FSmvPilot::ToJson
    return {'name': name, 'character': 'p01', 'avatar': {'v': 1, 'helmet': '#1e88e5', 'visor': '#263238', 'mirror': True, 'suit': '#37474f', 'skin': 's3',
                                                         'patch': 'wing', 'patch_color': '#ffc107', 'accessory': 'mic'}}


def track_json(name='Eğitim'):   # FSmvTrack::ToJson (prosedürel: ağ gerektirmez)
    return {'name': name, 'lat': 39.9334, 'lon': 32.8597, 'radius_m': 2000.0, 'wind': {'speed_ms': 3.0, 'dir_deg': 270.0, 'gust': 0.0},
            'time_of_day': 'day', 'weather': 'clear', 'world_mode': 'procedural',
            'world': {'seed': 12345, 'size_m': 4000.0, 'tree_density': 0.3, 'mountain_height': 300.0, 'mountain_count': 4, 'building_count': 40,
                      'runway': True, 'runway_heading': 0.0, 'water': 0.1, 'fog': 0.2},
            'gates': [{'i': 0, 'lat': 39.9340, 'lon': 32.8597, 'agl_m': 15.0, 'yaw_deg': 0.0, 'radius_m': 6.0, 'kind': 'ring'}]}


def start_json(drone_id, pilot_id, track_id):   # FSmvFlightRequest::ToStartJson
    return {'drone_id': drone_id, 'pilot_id': pilot_id, 'track_id': track_id, 'record': {'video': False, 'bin': True},
            'power': {'elec_cells': 4, 'elec_mah': 5000.0}, 'camera': 'day'}


# ---------------------------------------------------------------------------
# B) canlı
# ---------------------------------------------------------------------------

def bos_port():
    s = socket.socket()
    s.bind(('127.0.0.1', 0))
    p = s.getsockname()[1]
    s.close()
    return p


def canli():
    import requests
    from websockets.sync.client import connect as ws_connect
    print('B) canlı sözleşme (yerel SIMUMAV, geçici veri)')
    d = tempfile.mkdtemp(prefix='simumav-ue-sozlesme-')
    port = bos_port()
    base = f'http://127.0.0.1:{port}'
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', SIMUMAV_DATA_DIR=d, SIMUMAV_DEV_LOGIN='1', HEPGO_SITL='0', SIMUMAV_SECURE_COOKIE='0',
               CAYMAZ_FORCE_SECURE_COOKIE='0', SIMUMAV_ENV_FILE=os.path.join(d, 'yok.env'), SIMUMAV_MT_INTERNAL='http://127.0.0.1:1',
               SIMUMAV_UDP_BASE=str(30000 + port % 1000), SIMUMAV_MP_PORT_BASE=str(31000 + (port % 100) * 40), SIMUMAV_MP_PORT_COUNT='40',
               SIMUMAV_MAX_FLIGHTS='2', GOOGLE_MAPS_API_KEY='')
    log = open(os.path.join(d, 'sunucu.log'), 'w')
    p = subprocess.Popen([os.path.join(FY, '.venv', 'bin', 'python'), '-B', '-m', 'uvicorn', 'app.simumav.main:app', '--host', '127.0.0.1', '--port', str(port)],
                         cwd=FY, env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
    try:
        for _ in range(120):
            try:
                if requests.get(base + '/healthz', timeout=1).status_code == 200:
                    break
            except Exception:
                time.sleep(0.5)
        else:
            raise RuntimeError('sunucu açılmadı: ' + open(os.path.join(d, 'sunucu.log')).read()[-2000:])
        S = requests.Session()
        H = {'Origin': base}   # C++: OriginHeader (üretimde https://simumav.com)

        r = S.get(base + '/api/meta')
        m = r.json()
        esit('meta 200 + mt_url/mt_app', r.status_code == 200 and 'mt_url' in m and m.get('mt_app') == 'sv', r.text[:200])
        r = S.post(base + '/api/session', json={'dev_user': 'uetest'})
        esit('CSRF: Origin olmadan yazma 403', r.status_code == 403, str(r.status_code))
        r = S.post(base + '/api/session', json={'token': 'A' * 40}, headers=H)
        esit('MT belirteci doğrulanamazsa 503 account_service (istemci giriş hatası kodu)', r.status_code == 503 and r.json()['detail']['error'] == 'account_service', r.text[:200])
        r = S.post(base + '/api/session', json={'dev_user': 'uetest'}, headers=H)
        sc = r.headers.get('set-cookie', '')
        esit('oturum: Set-Cookie smsid (C++ çerez ayrıştırması)', r.status_code == 200 and 'smsid=' in sc, sc[:80])
        smsid = re.search(r'smsid=([^;]+)', sc).group(1)
        u = r.json()['user']
        esit('oturum user{id, email, display_name}', all(k in u for k in ('id', 'email', 'display_name')), str(u.keys()))
        C = {'Cookie': f'smsid={smsid}'}
        CW = dict(C, **H)
        r = requests.get(base + '/api/session', headers=C)
        esit('GET /api/session (ResumeSession)', r.status_code == 200, str(r.status_code))

        cat = requests.get(base + '/api/hangar/catalog', headers=C).json()
        esit('katalog types[].models[].key', any(mm['key'] == 'glb:iris' for t in cat['types'] for mm in t['models']))
        r = requests.post(base + '/api/hangar', json=drone_create_json(), headers=CW)
        esit('POST /api/hangar (FSmvDrone::ToJson(true))', r.status_code == 201, r.text[:300])
        dr = r.json()
        esit('drone alanları (FromJson)', all(k in dr for k in ('id', 'name', 'type', 'model_key', 'color', 'params', 'power', 'is_default')), str(dr.keys()))
        upd = dict(drone_create_json(name='Iris-2'), id=dr['id'], is_default=True)
        upd.pop('type')
        r = requests.put(base + f"/api/hangar/{dr['id']}", json=upd, headers=CW)
        esit('PUT /api/hangar/{id} (FSmvDrone::ToJson(false))', r.status_code == 200, r.text[:300])
        r = requests.post(base + '/api/hangar', json=drone_create_json('Cessna', 'plane', 'glb:rc_cessna'), headers=CW)
        esit('POST /api/hangar plane+airframe', r.status_code == 201, r.text[:300])
        hl = requests.get(base + '/api/hangar', headers=C).json()
        esit('GET /api/hangar → drones[]', isinstance(hl.get('drones'), list) and len(hl['drones']) >= 2)

        r = requests.post(base + '/api/crew', json=pilot_json(), headers=CW)
        esit('POST /api/crew (FSmvPilot::ToJson + avatar v1)', r.status_code == 201, r.text[:300])
        pl = r.json()
        esit('pilot alanları', all(k in pl for k in ('id', 'name', 'character', 'avatar')), str(pl.keys()))
        r = requests.post(base + '/api/tracks', json=track_json(), headers=CW)
        esit('POST /api/tracks (FSmvTrack::ToJson)', r.status_code == 201, r.text[:300])
        tr = r.json()
        esit('parkur alanları', all(k in tr for k in ('id', 'lat', 'lon', 'radius_m', 'wind', 'time_of_day', 'weather', 'world_mode', 'world', 'gates')), str(tr.keys()))
        r = requests.post(base + '/api/missions', json={'track_id': tr['id'], 'name': 'Serbest', 'kind': 'free', 'plan': {'items': []}}, headers=CW)
        esit('POST /api/missions (FSmvMission::ToJson)', r.status_code in (200, 201), r.text[:300])
        st = requests.get(base + '/api/settings', headers=C).json()
        esit('GET /api/settings alanları', all(k in st for k in ('lang', 'theme', 'sound', 'music', 'volume', 'units')), str(st.keys()))
        body = {'lang': 'tr', 'theme': 'dark', 'sound': True, 'analog_fpv': False, 'music': True, 'volume': 0.8, 'reduced_motion': False, 'units': 'metric', 'leaderboard_hidden': False}
        r = requests.put(base + '/api/settings', json=body, headers=CW)
        esit('PUT /api/settings (FSmvSettings::ToServerJson)', r.status_code == 200, r.text[:300])
        lb = requests.get(base + '/api/leaderboard?period=30d').json()
        esit('leaderboard pilots[]{rank,name,score,wins,matches,flights}', all(k in lb['pilots'][0] for k in ('rank', 'name', 'score', 'wins', 'matches', 'flights')) if lb.get('pilots') else 'pilots' in lb)

        # ---- uçuş
        r = requests.post(base + '/api/fly/start', json=start_json(dr['id'], pl['id'], tr['id']), headers=CW)
        esit('POST /api/fly/start (ToStartJson) 201', r.status_code == 201, r.text[:400])
        fs = r.json()
        esit('start yanıtı {flight_id, sim_id, ws, mp}', all(k in fs for k in ('flight_id', 'sim_id', 'ws', 'mp')), str(fs.keys()))
        r2 = requests.post(base + '/api/fly/start', json=start_json(dr['id'], pl['id'], tr['id']), headers=CW)
        det = r2.json().get('detail', {})
        esit('409 flight_open {flight_id, sim_id} (C++ açık uçuşa bağlanır)', r2.status_code == 409 and det.get('error') == 'flight_open' and 'sim_id' in det, r2.text[:300])
        ws_url = base.replace('http://', 'ws://') + fs['ws']
        tele = None
        res = None
        with ws_connect(ws_url, additional_headers={'Cookie': f'smsid={smsid}', 'Origin': base}, open_timeout=10) as ws:
            hello = json.loads(ws.recv(timeout=10))
            esit('WS hello', hello.get('t') == 'hello' and 'session' in hello, str(hello.get('t')))
            ws.send(json.dumps({'t': 'rc', 'ch': [1500, 1500, 1000, 1500, 1000, 0, 0, 0]}))
            ws.send(json.dumps({'t': 'cmd', 'rid': 'r1', 'name': 'arm', 'params': {}}))
            ws.send(json.dumps({'t': 'ping'}))
            son = time.time() + 15
            pong = False
            while time.time() < son and (tele is None or res is None or not pong):
                try:
                    m2 = json.loads(ws.recv(timeout=2))
                except TimeoutError:
                    continue
                if m2.get('t') == 'tele' and tele is None:
                    tele = m2
                if m2.get('t') == 'res' and m2.get('rid') == 'r1':
                    res = m2
                if m2.get('t') == 'pong':
                    pong = True
            esit('WS pong', pong)
        esit('WS tele geldi', tele is not None)
        if tele:
            # FCaySimTelemetry::FromJson + FSmvTeleExtra::FromJson okuduğu alanlar
            gerekli = ['ts', 'tbm', 'lat', 'lon', 'alt_msl', 'alt_rel', 'roll', 'pitch', 'yaw', 'rs', 'ps', 'ys', 'vn', 've', 'vd', 'airspeed', 'groundspeed',
                       'heading', 'climb', 'throttle', 'mode', 'armed', 'gps_fix', 'sats', 'batt_v', 'batt_pct', 'rc', 'home', 'speedup', 'fuel', 'elec',
                       'wind_spd', 'wind_dir', 'motor_cut', 'gcs_connected']
            eksik = [k for k in gerekli if k not in tele]
            esit('tele alanları (C++ ayrıştırıcıları)', not eksik, str(eksik))
            if tele.get('elec'):
                esit('tele.elec{wh,pct,w,min_left,v,empty}', all(k in tele['elec'] for k in ('wh', 'pct', 'w', 'min_left', 'v', 'empty')), str(tele['elec']))
        esit('cmd → res{rid,name,ok}', res is not None and 'ok' in res, str(res))
        r = requests.get(base + f"/api/fly/{fs['flight_id']}", headers=C)
        esit('GET /api/fly/{fid} {flight_id, sim_id, ws} (AttachExisting)', r.status_code == 200 and all(k in r.json() for k in ('flight_id', 'sim_id', 'ws')), r.text[:200])
        r = requests.post(base + f"/api/fly/{fs['flight_id']}/stop", json={'reason': 'user', 'gates': {'passed': 1, 'missed': 0, 'laps': 1}}, headers=CW)
        esit('POST /api/fly/{fid}/stop → {ok, flight}', r.status_code == 200 and 'flight' in r.json(), r.text[:300])
        fl = r.json().get('flight', {})
        esit('flight{id,result,duration_s,summary}', all(k in fl for k in ('id', 'result', 'duration_s', 'summary')), str(fl.keys()))
        r = requests.get(base + f"/api/ops/{fs['flight_id']}", headers=C)
        esit('GET /api/ops/{id} (FetchSummary)', r.status_code == 200 and 'summary' in r.json(), r.text[:200])

        # ---- karşılaşma (yapay zekâ)
        r = requests.post(base + '/api/match/ai', json={'drone_id': dr['id'], 'track_id': tr['id'], 'camera': 'day', 'kind': 'rings', 'level': 'easy', 'bots': 2}, headers=CW)
        esit('POST /api/match/ai (USmvMatchClient::CreateAi)', r.status_code == 201, r.text[:300])
        if r.status_code == 201:
            mt = r.json()
            esit('match anlık görüntü {id,code,state,kind,players,rings}', all(k in mt for k in ('id', 'code', 'state', 'kind', 'players', 'rings')), str(mt.keys()))
            with ws_connect(base.replace('http://', 'ws://') + f"/ws/match/{mt['id']}", additional_headers={'Cookie': f'smsid={smsid}', 'Origin': base}, open_timeout=10) as ws:
                h = json.loads(ws.recv(timeout=10))
                esit('WS /ws/match hello{match, me}', h.get('t') == 'hello' and 'match' in h and 'me' in h, str(h.keys()))
            requests.post(base + f"/api/match/{mt['id']}/cancel", json={}, headers=CW)
        r = requests.delete(base + '/api/session', json={}, headers=CW)
        esit('DELETE /api/session (Logout)', r.status_code == 200, r.text[:200])
    finally:
        os.killpg(p.pid, signal.SIGTERM)
        try:
            p.wait(timeout=15)
        except subprocess.TimeoutExpired:
            os.killpg(p.pid, signal.SIGKILL)
        log.close()
        shutil.rmtree(d, ignore_errors=True)


def main():
    if not os.path.isdir(os.path.join(FY, 'app', 'simumav')):
        print(f'ATLANDI: SIMUMAV referansı yok ({FY})')
        return 0
    statik()
    canli()
    print(f'sözleşme testi: {len(HATA)} hata')
    return 1 if HATA else 0


if __name__ == '__main__':
    sys.exit(main())
