#!/usr/bin/env python3
"""SmvMavlink (Source/Simumav/Private/Mavlink) ↔ pymavlink çapraz sınaması.

Unreal olmadan çalışır: SmvMavlink.cpp g++ ile derlenir, pymavlink'in ürettiği v1/v2 çerçeveleri C++ çözücüsüne verilir ve
alanlar karşılaştırılır; ardından C++ kodlayıcısının ürettiği çerçeveler pymavlink ile çözülür.
Kullanım:  python3 tests/cpp/test_mavlink.py   (pymavlink kurulu olmalı: pip install pymavlink)
"""
import os
import random
import subprocess
import sys
import tempfile

from pymavlink.dialects.v20 import ardupilotmega as mav

KOK = os.path.dirname(os.path.abspath(__file__))
KAYNAK = os.path.normpath(os.path.join(KOK, '..', '..', 'Source', 'Simumav', 'Private', 'Mavlink', 'SmvMavlink.cpp'))
SURUCU = os.path.join(KOK, 'test_mavlink_main.cpp')


def derle(hedef):
    cmd = ['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-O1', '-o', hedef, SURUCU, KAYNAK]
    subprocess.run(cmd, check=True)


def ornek_mesajlar(rnd):
    m = mav.MAVLink(None, srcSystem=1, srcComponent=1)
    out = []
    for i in range(40):
        out.append(m.heartbeat_encode(2, 3, rnd.choice([81, 209, 217]), rnd.randint(0, 30), 4, 3))
        out.append(m.attitude_encode(rnd.randint(0, 2**31), rnd.uniform(-3, 3), rnd.uniform(-1.5, 1.5), rnd.uniform(-3, 3),
                                     rnd.uniform(-1, 1), rnd.uniform(-1, 1), rnd.uniform(-1, 1)))
        out.append(m.global_position_int_encode(rnd.randint(0, 2**31), rnd.randint(-900000000, 900000000), rnd.randint(-1800000000, 1800000000),
                                                rnd.randint(-1000, 900000), rnd.randint(-1000, 900000), rnd.randint(-3000, 3000),
                                                rnd.randint(-3000, 3000), rnd.randint(-3000, 3000), rnd.randint(0, 35999)))
        out.append(m.vfr_hud_encode(airspeed=rnd.uniform(0, 60), groundspeed=rnd.uniform(0, 60), alt=rnd.uniform(-10, 2000), climb=rnd.uniform(-10, 10),
                                    heading=rnd.randint(0, 359), throttle=rnd.randint(0, 100)))
        out.append(m.sys_status_encode(onboard_control_sensors_present=1, onboard_control_sensors_enabled=1, onboard_control_sensors_health=1,
                                       load=rnd.randint(0, 1000), voltage_battery=rnd.randint(0, 25000), current_battery=rnd.randint(-1, 3000),
                                       battery_remaining=rnd.randint(-1, 100), drop_rate_comm=0, errors_comm=0, errors_count1=0, errors_count2=0,
                                       errors_count3=0, errors_count4=0))
        out.append(m.gps_raw_int_encode(time_usec=rnd.randint(0, 2**40), fix_type=rnd.randint(0, 6), lat=rnd.randint(-900000000, 900000000),
                                        lon=rnd.randint(-1800000000, 1800000000), alt=rnd.randint(0, 900000), eph=100, epv=100,
                                        vel=rnd.randint(0, 5000), cog=rnd.randint(0, 35999), satellites_visible=rnd.randint(0, 20)))
        out.append(m.statustext_encode(rnd.randint(0, 7), ('Uyari %d ArmCheck' % i).encode()))
        out.append(m.command_ack_encode(rnd.choice([400, 176, 22]), rnd.randint(0, 5)))
        out.append(m.servo_output_raw_encode(rnd.randint(0, 2**31), rnd.randint(0, 3), *[rnd.randint(900, 2100) for _ in range(8)], *[rnd.randint(0, 2100) for _ in range(8)]))
        out.append(m.rc_channels_encode(rnd.randint(0, 2**31), 16, *[rnd.randint(900, 2100) for _ in range(18)], rnd.randint(0, 255)))
        out.append(m.param_value_encode(param_id=b'SIM_WIND_SPD', param_value=rnd.uniform(0, 20), param_type=9, param_count=1234, param_index=rnd.randint(0, 1233)))
        out.append(m.home_position_encode(rnd.randint(-900000000, 900000000), rnd.randint(-1800000000, 1800000000), rnd.randint(0, 900000), 0, 0, 0, [1, 0, 0, 0], 0, 0, 0))
        out.append(m.battery_status_encode(0, 0, 1, 2500, [12600] + [65535] * 9, rnd.randint(-1, 3000), rnd.randint(0, 50000), -1, rnd.randint(0, 100)))
        out.append(m.extended_sys_state_encode(rnd.randint(0, 4), rnd.randint(0, 4)))
        out.append(m.wind_encode(rnd.uniform(-180, 180), rnd.uniform(0, 20), rnd.uniform(-2, 2)))
    return m, out


def deger_metni(msg):
    n = msg.get_type()
    d = msg.to_dict()
    if n == 'HEARTBEAT':
        return f"HEARTBEAT custom_mode={d['custom_mode']} type={d['type']} autopilot={d['autopilot']} base_mode={d['base_mode']} system_status={d['system_status']}"
    if n == 'ATTITUDE':
        return 'ATTITUDE time_boot_ms=%d roll=%.6f pitch=%.6f yaw=%.6f rollspeed=%.6f pitchspeed=%.6f yawspeed=%.6f' % (
            d['time_boot_ms'], d['roll'], d['pitch'], d['yaw'], d['rollspeed'], d['pitchspeed'], d['yawspeed'])
    if n == 'GLOBAL_POSITION_INT':
        return 'GLOBAL_POSITION_INT time_boot_ms=%d lat=%d lon=%d alt=%d relative_alt=%d vx=%d vy=%d vz=%d hdg=%d' % tuple(
            d[k] for k in ('time_boot_ms', 'lat', 'lon', 'alt', 'relative_alt', 'vx', 'vy', 'vz', 'hdg'))
    if n == 'VFR_HUD':
        return 'VFR_HUD airspeed=%.6f groundspeed=%.6f alt=%.6f climb=%.6f heading=%d throttle=%d' % tuple(
            d[k] for k in ('airspeed', 'groundspeed', 'alt', 'climb', 'heading', 'throttle'))
    if n == 'SYS_STATUS':
        return 'SYS_STATUS load=%d voltage_battery=%d current_battery=%d battery_remaining=%d' % tuple(
            d[k] for k in ('load', 'voltage_battery', 'current_battery', 'battery_remaining'))
    if n == 'GPS_RAW_INT':
        return 'GPS_RAW_INT lat=%d lon=%d alt=%d fix_type=%d satellites_visible=%d vel=%d cog=%d' % tuple(
            d[k] for k in ('lat', 'lon', 'alt', 'fix_type', 'satellites_visible', 'vel', 'cog'))
    if n == 'STATUSTEXT':
        return f"STATUSTEXT severity={d['severity']} text={d['text']}"
    if n == 'COMMAND_ACK':
        return f"COMMAND_ACK command={d['command']} result={d['result']}"
    if n == 'SERVO_OUTPUT_RAW':
        return f"SERVO_OUTPUT_RAW servo1_raw={d['servo1_raw']} servo3_raw={d['servo3_raw']} servo9_raw={d['servo9_raw']} port={d['port']}"
    if n == 'RC_CHANNELS':
        return f"RC_CHANNELS chan1_raw={d['chan1_raw']} chan8_raw={d['chan8_raw']} chan18_raw={d['chan18_raw']} chancount={d['chancount']} rssi={d['rssi']}"
    if n == 'PARAM_VALUE':
        return 'PARAM_VALUE param_id=%s param_value=%.6f param_count=%d param_index=%d param_type=%d' % (
            d['param_id'], d['param_value'], d['param_count'], d['param_index'], d['param_type'])
    if n == 'HOME_POSITION':
        return f"HOME_POSITION latitude={d['latitude']} longitude={d['longitude']} altitude={d['altitude']}"
    if n == 'BATTERY_STATUS':
        return f"BATTERY_STATUS current_consumed={d['current_consumed']} current_battery={d['current_battery']} battery_remaining={d['battery_remaining']} voltages0={d['voltages'][0]}"
    if n == 'EXTENDED_SYS_STATE':
        return f"EXTENDED_SYS_STATE vtol_state={d['vtol_state']} landed_state={d['landed_state']}"
    if n == 'WIND':
        return 'WIND direction=%.6f speed=%.6f speed_z=%.6f' % (d['direction'], d['speed'], d['speed_z'])
    raise ValueError(n)


def main():
    rnd = random.Random(20261023)
    with tempfile.TemporaryDirectory() as td:
        exe = os.path.join(td, 'smvmav')
        derle(exe)
        m, msgs = ornek_mesajlar(rnd)
        satirlar, beklenen = [], []
        v1_sayisi = 0
        for i, msg in enumerate(msgs):
            # v2 ve v1 karışık; v1 yalnız kimliği < 256 olan ve uzantısız alanları sığan mesajlarda
            v1 = (i % 3 == 0 and msg.get_msgId() < 256)
            buf = msg.pack(mav.MAVLink(None, srcSystem=1, srcComponent=1), force_mavlink1=v1)
            if v1:
                assert buf[0] == 0xFE
                v1_sayisi += 1
            # pymavlink'in kendi çözümüyle beklenen değer (float yuvarlaması dahil)
            dec = mav.MAVLink(None).parse_char(buf)
            beklenen.append(deger_metni(dec))
            satirlar.append(f'{msg.get_type()} {buf.hex()}')
        # gürültü: geçersiz baytlar araya karışsın
        satirlar.insert(5, 'GURULTU 00ff55aa')
        girdi = '\n'.join(satirlar) + '\nENC\n'
        cikti = subprocess.run([exe], input=girdi, capture_output=True, text=True, check=True).stdout.splitlines()
        cozulen = [s for s in cikti if not s.split(' ')[0] in ('HEARTBEAT_ENC',) and not s.startswith(('COMMAND_LONG ', 'PARAM_SET ', 'PARAM_REQUEST_READ ', 'RC_CHANNELS_OVERRIDE ', 'REQUEST_DATA_STREAM ', 'SET_MODE ', 'DISTANCE_SENSOR ', 'OBSTACLE_DISTANCE '))]
        enc = [s for s in cikti if s.startswith(('COMMAND_LONG ', 'PARAM_SET ', 'PARAM_REQUEST_READ ', 'RC_CHANNELS_OVERRIDE ', 'REQUEST_DATA_STREAM ', 'SET_MODE ', 'DISTANCE_SENSOR ', 'OBSTACLE_DISTANCE '))]
        # ENC çıktısındaki HEARTBEAT satırı hex içerir (alan değil) — ayır
        enc_hb = [s for s in cozulen if s.startswith('HEARTBEAT ') and '=' not in s]
        cozulen = [s for s in cozulen if s not in enc_hb and not s.startswith('FAIL GURULTU')]
        enc += enc_hb
        hata = 0
        if len(cozulen) != len(beklenen):
            print(f'HATA: {len(beklenen)} mesaj beklendi, {len(cozulen)} çözüldü')
            hata += 1
        for a, b in zip(cozulen, beklenen):
            if a != b:
                print(f'FARK:\n  C++ : {a}\n  py  : {b}')
                hata += 1
        # C++ kodlayıcısı → pymavlink
        beklenen_enc = {
            'HEARTBEAT': dict(type=6, autopilot=8, base_mode=0, custom_mode=0, system_status=4, mavlink_version=3),
            'COMMAND_LONG': dict(command=400, param1=1.0, param2=21196.0, target_system=1, target_component=1),
            'PARAM_SET': dict(param_id='SIM_WIND_SPD', param_value=7.5, param_type=9, target_system=1),
            'PARAM_REQUEST_READ': dict(param_id='SIM_SPEEDUP', param_index=-1),
            'RC_CHANNELS_OVERRIDE': dict(chan1_raw=1500, chan2_raw=1400, chan3_raw=1100, chan4_raw=1600, chan7_raw=0, chan8_raw=65535, chan9_raw=1234, chan18_raw=1999),
            'REQUEST_DATA_STREAM': dict(req_stream_id=0, req_message_rate=25, start_stop=1),
            'SET_MODE': dict(custom_mode=5, base_mode=1, target_system=1),
            'DISTANCE_SENSOR': dict(time_boot_ms=123456, min_distance=20, max_distance=4000, current_distance=1234, type=0, id=1, orientation=25,
                                    covariance=255, horizontal_fov=0.0, vertical_fov=0.0, quaternion=[0.0, 0.0, 0.0, 0.0], signal_quality=0),
            'OBSTACLE_DISTANCE': dict(time_usec=9876543210, min_distance=20, max_distance=3000, sensor_type=0, increment=45, increment_f=45.0,
                                      angle_offset=0.0, frame=12,
                                      distances=[100, 137, 174, 3001, 248, 285, 322, 359] + [65535] * 64),
        }
        for s in enc:
            ad, hx = s.split(' ', 1)
            msg = mav.MAVLink(None).parse_char(bytes.fromhex(hx))
            if msg is None or msg.get_type() != ad:
                print(f'HATA: {ad} pymavlink ile çözülemedi')
                hata += 1
                continue
            d = msg.to_dict()
            for k, v in beklenen_enc[ad].items():
                if d.get(k) != v:
                    print(f'FARK {ad}.{k}: {d.get(k)!r} != {v!r}')
                    hata += 1
        if len(enc) != len(beklenen_enc):
            print(f'HATA: {len(beklenen_enc)} kodlanmış çerçeve beklendi, {len(enc)} geldi')
            hata += 1
        print(f'{len(beklenen)} çözme (v1: {v1_sayisi}) + {len(enc)} kodlama denetlendi; hata {hata}')
        return 1 if hata else 0


if __name__ == '__main__':
    sys.exit(main())
