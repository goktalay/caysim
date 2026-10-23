#!/usr/bin/env python3
"""Araç çeşitlerini GERÇEK ArduPilot SITL (Linux x86-64 4.7.2) ile uçurur — C++ çekirdeğin (SmvVehicles + SmvSitlConfig) ürettiği
ikili / --model / varsayilan.parm / çerçeve-kanat JSON / ilk HEARTBEAT parametreleriyle (tests/sitl/varyant_cli.cpp).
Senaryo: çok rotor / heli → GUIDED kalkış 20 m, 15 sn askı (gaz, yatış/yunuslama sapması), 20 sn 20 m/s kuzey hız komutu;
sabit kanat / VTOL → AUTO görevi (NAV_TAKEOFF ya da VTOL_TAKEOFF + 500 m kare tur). Helikopterde RC8 motor kilidi (ARM'da yüksek) sürülür.
Gereksinim: pymavlink, g++, SITL ikilileri (arducopter, arducopter-heli, arduplane) — SIMUMAV_SITL_BIN (varsayılan /tmp/arac/bin).
Kullanım:  <venv>/bin/python -I tests/sitl/sitl_varyant.py [anahtar ...]      (ör. glb:iris smv:octa)
Çıktı: her çeşit için JSON satırı + özet tablo; ikililer yoksa ATLANDI (çıkış 0)."""
import json, math, os, subprocess, sys, tempfile, time
from pymavlink import mavutil

KOK = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
CORE = os.path.join(KOK, 'Source', 'Simumav', 'Private', 'Core')
BIN = os.environ.get('SIMUMAV_SITL_BIN', '/tmp/arac/bin')
HOME = (39.9334, 32.8597, 100.0)


def mission_items(m, kind, alt=60):
    lat0, lon0 = HOME[0], HOME[1]
    def off(n, e):
        return lat0 + n / 111320.0, lon0 + e / (111320.0 * math.cos(math.radians(lat0)))
    it = [(0, mavutil.mavlink.MAV_CMD_NAV_WAYPOINT, 0, 0, 0, 0, lat0, lon0, 0)]
    if kind == 'plane':
        it.append((0, mavutil.mavlink.MAV_CMD_NAV_TAKEOFF, 12, 0, 0, 0, lat0, lon0, 40))
    else:
        it.append((0, mavutil.mavlink.MAV_CMD_NAV_VTOL_TAKEOFF, 0, 0, 0, 0, lat0, lon0, 30))
    for n, e in [(500, 0), (500, 500), (0, 500), (0, 0)]:
        la, lo = off(n, e)
        it.append((0, mavutil.mavlink.MAV_CMD_NAV_WAYPOINT, 0, 0, 0, 0, la, lo, alt))
    it.append((0, mavutil.mavlink.MAV_CMD_DO_JUMP, 2, 10, 0, 0, 0, 0, 0))
    return it


def upload(m, items):
    m.mav.mission_count_send(1, 1, len(items))
    sent = set()
    t0 = time.time()
    while time.time() - t0 < 20:
        r = m.recv_match(type=['MISSION_REQUEST', 'MISSION_REQUEST_INT', 'MISSION_ACK'], blocking=True, timeout=2)
        if r is None:
            continue
        if r.get_type() == 'MISSION_ACK':
            if r.type != 0: print('MISSION_ACK', r.type, file=sys.stderr)
            return r.type == 0
        s = r.seq
        f, cmd, p1, p2, p3, p4, la, lo, al = items[s]
        m.mav.mission_item_int_send(1, 1, s, mavutil.mavlink.MAV_FRAME_GLOBAL_RELATIVE_ALT_INT, cmd, 0, 1, p1, p2, p3, p4,
                                    int(la * 1e7), int(lo * 1e7), al)
        sent.add(s)
    return False


def run(v, speedup=5, dur=None, keep=False):
    wd = tempfile.mkdtemp(prefix='sitlv_')
    parm = os.path.join(wd, 'varsayilan.parm')
    open(parm, 'w').write(v['parm'])
    model = v['model']
    if v.get('json'):
        open(os.path.join(wd, v.get('json_name', 'cerceve.json')), 'w').write(v['json'])
    port = v.get('port', 25760)
    binmap = {'ArduCopter': 'arducopter', 'ArduHeli': 'arducopter-heli', 'ArduPlane': 'arduplane'}
    cmd = [os.path.join(BIN, binmap.get(v['bin'], v['bin'])), '--model', model, '--home', '%f,%f,%f,0' % HOME, '--base-port', str(port), '--defaults', parm,
           '--sysid', '1', '--speedup', str(speedup), '--rc-in-port', str(port + 7), '--irlock-port', str(port + 8)]
    log = open(os.path.join(wd, 'sitl.log'), 'w')
    p = subprocess.Popen(cmd, cwd=wd, stdout=log, stderr=subprocess.STDOUT)
    res = {'name': v['name'], 'ok': False}
    try:
        m = None
        for _ in range(60):
            try:
                m = mavutil.mavlink_connection('tcp:127.0.0.1:%d' % port, source_system=255, autoreconnect=False)
                break
            except Exception:
                time.sleep(0.5)
        hb = m.wait_heartbeat(timeout=30)
        if not hb:
            res['err'] = 'heartbeat yok'
            return res
        m.mav.request_data_stream_send(1, 1, mavutil.mavlink.MAV_DATA_STREAM_ALL, 10, 1)
        for k, val in v.get('hb', []):
            m.mav.param_set_send(1, 1, k.encode(), float(val), mavutil.mavlink.MAV_PARAM_TYPE_REAL32)
            time.sleep(0.02)
        kind = v['kind']
        modes = m.mode_mapping()
        # arm döngüsü
        if kind in ('plane', 'vtol'):
            items = mission_items(m, kind)
            okm = False
            for _ in range(15):
                if upload(m, items):
                    okm = True
                    break
                time.sleep(1)
            if not okm:
                res['err'] = 'görev yüklenemedi'
        t0 = time.time()
        armed = False
        msgs = []
        heli = v['bin'] == 'ArduHeli'
        import threading
        rcst = {'v': 1000, 'run': heli}
        def rcloop():
            while rcst['run']:
                m.mav.rc_channels_override_send(1, 1, 65535, 65535, 65535, 65535, 65535, 65535, 65535, rcst['v'])
                time.sleep(0.05)
        threading.Thread(target=rcloop, daemon=True).start()
        def rc8(val):
            rcst['v'] = val
        while time.time() - t0 < 120 / max(1, speedup / 2):
            rc8(1000)
            if kind == 'copter':
                m.set_mode(modes['GUIDED'])
            else:
                m.set_mode(modes.get('FBWA', 5) if kind == 'plane' else modes.get('QLOITER', 19))
            m.mav.command_long_send(1, 1, mavutil.mavlink.MAV_CMD_COMPONENT_ARM_DISARM, 0, 1, 0, 0, 0, 0, 0, 0)
            tt = time.time()
            while time.time() - tt < 2:
                r = m.recv_match(blocking=True, timeout=0.5)
                if r is None:
                    continue
                if r.get_type() == 'STATUSTEXT':
                    msgs.append(r.text)
                if r.get_type() == 'HEARTBEAT' and r.get_srcSystem() == 1 and r.base_mode & 128:
                    armed = True
                    rc8(2000)
                    break
            if armed:
                break
        res['arm_s'] = round(time.time() - t0, 1)
        if not armed:
            res['err'] = 'arm olmadı: ' + ' | '.join(msgs[-5:])
            return res
        st = {'alt': [], 'thr': [], 'roll': [], 'pitch': [], 'gs': [], 'as': [], 'mode': None}
        def pump(sec, phase):
            tt = time.time()
            last = 0
            while time.time() - tt < sec / speedup:
                if time.time() - last > 0.1:
                    rc8(2000); last = time.time()
                r = m.recv_match(blocking=True, timeout=0.5)
                if r is None:
                    continue
                ty = r.get_type()
                if ty == 'VFR_HUD':
                    st['thr'].append((phase, r.throttle)); st['gs'].append((phase, r.groundspeed)); st['as'].append((phase, r.airspeed))
                elif ty == 'GLOBAL_POSITION_INT':
                    st['alt'].append((phase, r.relative_alt / 1000.0))
                elif ty == 'ATTITUDE':
                    st['roll'].append((phase, math.degrees(r.roll))); st['pitch'].append((phase, math.degrees(r.pitch)))
                elif ty == 'STATUSTEXT':
                    msgs.append(r.text)
        if kind == 'copter':
            tko = 0
            for _ in range(80):
                rc8(2000)
                m.mav.command_long_send(1, 1, mavutil.mavlink.MAV_CMD_NAV_TAKEOFF, 0, 0, 0, 0, 0, 0, 0, 20)
                tt = time.time()
                while time.time() - tt < 0.4:
                    a = m.recv_match(type=['COMMAND_ACK', 'STATUSTEXT'], blocking=True, timeout=0.2)
                    if a and a.get_type() == 'STATUSTEXT':
                        msgs.append(a.text)
                    elif a and a.command == mavutil.mavlink.MAV_CMD_NAV_TAKEOFF and a.result == 0:
                        tko = 1
                if tko:
                    break
                if not heli:
                    m.mav.command_long_send(1, 1, mavutil.mavlink.MAV_CMD_COMPONENT_ARM_DISARM, 0, 1, 0, 0, 0, 0, 0, 0)
                elif a is not None and a.get_type() == 'COMMAND_ACK':
                    msgs.append('ack%d=%d' % (a.command, a.result))
            res['takeoff_ack'] = tko
            pump(25, 'climb')
            pump(15, 'hover')
            # ileri hız: 15 m/s kuzey
            tt = time.time()
            while time.time() - tt < 20 / speedup:
                m.mav.set_position_target_local_ned_send(0, 1, 1, mavutil.mavlink.MAV_FRAME_LOCAL_NED, 0b0000111111000111, 0, 0, 0, 20, 0, 0, 0, 0, 0, 0, 0)
                pump(1, 'fwd')
            m.set_mode(modes['LAND'])
            pump(5, 'land')
        else:
            m.set_mode(modes['AUTO'])
            pump(dur or 120, 'auto')
        def ph(lst, phase):
            return [x for p_, x in lst if p_ == phase]
        def mean(a):
            return round(sum(a) / len(a), 1) if a else None
        def sd(a):
            if len(a) < 2: return None
            mu = sum(a) / len(a); return round(math.sqrt(sum((x - mu) ** 2 for x in a) / len(a)), 2)
        if kind == 'copter':
            res.update({'alt_climb_end': (ph(st['alt'], 'climb') or [None])[-1], 'hover_alt': mean(ph(st['alt'], 'hover')),
                        'hover_thr': mean(ph(st['thr'], 'hover')), 'hover_roll_sd': sd(ph(st['roll'], 'hover')), 'hover_pitch_sd': sd(ph(st['pitch'], 'hover')),
                        'fwd_gs_max': max(ph(st['gs'], 'fwd') or [0]), 'fwd_pitch': mean(ph(st['pitch'], 'fwd')), 'fwd_thr': mean(ph(st['thr'], 'fwd'))})
            res['ok'] = bool(res['hover_alt'] and res['hover_alt'] > 15)
        else:
            alts = ph(st['alt'], 'auto')
            res.update({'alt_max': round(max(alts or [0]), 1), 'alt_end': (alts or [None])[-1], 'as_mean_late': mean(ph(st['as'], 'auto')[len(ph(st['as'], 'auto')) // 2:]),
                        'as_max': max(ph(st['as'], 'auto') or [0]), 'thr_mean': mean(ph(st['thr'], 'auto')), 'roll_sd': sd(ph(st['roll'], 'auto')),
                        'pitch_mean': mean(ph(st['pitch'], 'auto'))})
            res['ok'] = res['alt_max'] > 25 and (res['alt_end'] or 0) > 15
        rcst['run'] = False
        res['msgs'] = [x for x in msgs if 'PreArm' not in x and 'Interlock Enabled' not in x][-12:]
        return res
    finally:
        p.terminate()
        try:
            p.wait(5)
        except Exception:
            p.kill()
        res['wd'] = wd
        if not keep and res.get('ok'):
            subprocess.run(['rm', '-rf', wd])


def cesitler():
    with tempfile.TemporaryDirectory() as td:
        exe = os.path.join(td, 'cli')
        kaynak = [os.path.join(CORE, f) for f in ('SmvVehicles.cpp', 'SmvSitlConfig.cpp', 'SmvPower.cpp', 'SmvRace.cpp', 'SmvPyRandom.cpp')]
        subprocess.run(['g++', '-std=c++17', '-O1', '-I', CORE, '-o', exe, os.path.join(KOK, 'tests', 'sitl', 'varyant_cli.cpp')] + kaynak, check=True)
        return json.loads(subprocess.run([exe], capture_output=True, text=True, check=True).stdout)


if __name__ == '__main__':
    if not all(os.path.isfile(os.path.join(BIN, b)) for b in ('arducopter', 'arducopter-heli', 'arduplane')):
        print(f'ATLANDI: SITL ikilileri yok ({BIN})')
        sys.exit(0)
    from concurrent.futures import ThreadPoolExecutor
    vs = [v for v in cesitler() if v['kind'] != 'rover']
    sec = sys.argv[1:]
    if sec:
        vs = [v for v in vs if v['name'] in sec]
    for i, v in enumerate(vs):
        v['port'] = 25760 + 10 * i
    def bir(v):
        t = time.time()
        try:
            r = run(v)
        except Exception as e:  # noqa: BLE001
            r = {'name': v['name'], 'ok': False, 'err': repr(e)}
        r['sec'] = round(time.time() - t)
        r['model'] = v['model']
        r['bin'] = v['bin']
        print(json.dumps(r, ensure_ascii=False), flush=True)
        return r
    with ThreadPoolExecutor(max_workers=int(os.environ.get('SIMUMAV_SITL_PARALEL', '6'))) as ex:
        sonuc = list(ex.map(bir, vs))
    print()
    print(f"{'anahtar':18} {'ikili':10} {'--model':34} {'sonuç':6} ölçüm")
    for r in sonuc:
        if 'hover_thr' in r:
            olc = f"askı gazı %{r.get('hover_thr')}, yatış σ {r.get('hover_roll_sd')}°, azami yer hızı {round(r.get('fwd_gs_max') or 0, 1)} m/s"
        else:
            olc = f"azami irtifa {r.get('alt_max')} m, seyir hava hızı {r.get('as_mean_late')} m/s, gaz %{r.get('thr_mean')}"
        print(f"{r['name']:18} {r['bin']:10} {r['model']:34} {'TAMAM' if r.get('ok') else 'HATA':6} {olc if r.get('ok') else r.get('err', '')}")
    sys.exit(0 if all(r.get('ok') for r in sonuc) else 1)
