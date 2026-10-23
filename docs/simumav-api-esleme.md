# Simumav istemcisi ↔ simumav.com API eşlemesi

Kaynak sözleşme: `fabrika-yonetim` `app/simumav/routers/*.py`, `static/simumav/app/*.js`, `docs/simumav.md` §4 (salt okunur).
Doğrulama: `tests/test_sozlesme_simumav.py` — (A) C++'taki tüm `/api/…`, `/ws/…` yolları sunucu yönlendiricilerinde var; C++ hangar kataloğu sunucu
kataloğuyla aynı; (B) SIMUMAV yerelde geçici veriyle başlatılıp C++ gövdelerinin birebir kopyalarıyla çağrılır → **0 hata** (2026-10-23).

Giriş zorunlu değildir: misafir kipte yalnız oturumsuz uçlar (`/api/meta`, `/api/leaderboard`, statik GLB) kullanılır; fizik yereldir
(`USmvLocalFlightLink`, aşağıda "Yerel karşılık" sütunu).

## Kimlik

| Adım | İstek | Yanıt / alanlar | C++ | Not |
|---|---|---|---|---|
| Meta | `GET /api/meta` | `mt_url`, `mt_app` ('sv'), `limits`, `types`… | `USimumavClient::FetchMeta` | oturumsuz |
| Ön giriş | `POST {mt_url}/api/sv/auth/prelogin {email}` (+`x-sshm: 1`) | `kdf{alg, iter, salt}` | `Login` | mastertool |
| Özet | — | `auth_hash = b64(PBKDF2(PBKDF2(şifre, salt, iter, 32), şifre, 1, 32))` | `SmvKdf::ComputeAuthHash` | Python hashlib'e karşı sınandı (`tests/cpp/test_kdf.py`) |
| Giriş | `POST {mt_url}/api/sv/auth/login {email, auth_hash, device, app_version}` | `token` | `Login` | device = "Simumav Steam (Windows)", app_version = "steam-0.2.0" |
| Oturum | `POST /api/session {token}` + `Origin` | `Set-Cookie: smsid=…`; `user{id,email,display_name,…}` | `OpenSessionWithToken`, `ApplySession` | çerez + belirteç Kimlik Bilgisi Yöneticisi'nde |
| Sürdür | `GET /api/session` (Cookie) | aynı | `ResumeSession` | 401 → belirteçle yeniden `POST /api/session` |
| Çıkış | `DELETE /api/session {token}` | `{ok}` | `Logout` | MT belirteci de kapanır |

**CSRF:** sunucu yazma isteklerinde `Origin` (izinli köken) ister. Tarayıcı dışı istemci `Origin: https://simumav.com` gönderir
(`[/Script/Simumav.SimumavClient] OriginHeader`). Origin'siz yazma 403 `csrf` (testte doğrulandı).
**Hatalar:** `{detail:{error, message}}` → `FSmvRestDone(Status, Json, ErrCode, ErrMsg)`; 0 = ağ.

## Kaynaklar

| Uç | C++ gönderilen gövde (SimumavTypes.cpp) | C++ okunan alanlar | Yerel karşılık (misafir) |
|---|---|---|---|
| `GET /api/hangar/catalog` | — | `types[].models[].key` | `USmvDataService::Catalog()` (aynı anahtarlar, gömülü) |
| `GET /api/hangar` | — | `drones[]` → `FSmvDrone::FromJson` (id, name, type, model_key, color, airframe, drive, is_default, params, power) | `guest/hangar.json` |
| `POST /api/hangar` | `FSmvDrone::ToJson(true)`: name, type, model_key, color, airframe (plane), params, power | 201 drone | yerel ekleme (`l-…` kimlik) |
| `PUT /api/hangar/{id}` | `ToJson(false)`: + id, is_default (type yok) | drone | yerel güncelleme |
| `DELETE /api/hangar/{id}` | — | `{ok}`; 409 `in_use` | en az bir dron kalır |
| `GET/POST/PUT/DELETE /api/crew[/id]` | `FSmvPilot::ToJson`: name, character, avatar{v,helmet,visor,mirror,suit,skin,patch,patch_color,accessory} | `pilots[]` | `guest/crew.json` |
| `GET/POST/PUT/DELETE /api/tracks[/id]` | `FSmvTrack::ToJson`: name, lat, lon, radius_m, wind{speed_ms,dir_deg,gust}, time_of_day, weather, world_mode, world{…}, gates[{i,lat,lon,agl_m,yaw_deg,radius_m,kind}] | `tracks[]` | `guest/tracks.json` (+ yerel `ground_msl`) |
| `GET/POST/PUT/DELETE /api/missions[/id]` | `FSmvMission::ToJson`: track_id, name, kind, plan | `missions[]` | `guest/missions.json` |
| `GET/PUT /api/settings` | `FSmvSettings::ToServerJson`: lang, theme, sound, analog_fpv, music, volume, reduced_motion, units, leaderboard_hidden | aynı | `Profiles/<id>/settings.json` (+ yerel alanlar; Cesium belirteci gizli depoda) |
| `GET /api/leaderboard?period=30d\|all` | — | `pilots[]{rank,name,score,wins,matches,flights,demo}` | oturumsuz görüntüleme |

## Uçuş

| Uç / ileti | C++ | Yerel karşılık |
|---|---|---|
| `POST /api/fly/start` ← `FSmvFlightRequest::ToStartJson` (drone_id, pilot_id, track_id, mission_id?, launch?, record{video,bin}, power, speedup?, camera, targets?, drive?) | `USmvCloudFlightLink::Start` | `USmvLocalFlightLink::Start` (aynı istek yapısı) |
| 201 `{flight_id, sim_id, ws, mp{host,port,proto}, state}` | `FSmvFlightInfo::FromJson` | yerel `l-…` kimlik, `MpProto=udp` |
| 409 `flight_open{flight_id, sim_id}` | açık uçuşa bağlanılır (web ile aynı) | — |
| `WS /ws/fly/{sim_id}` (Cookie: smsid, Origin) | `FSmvSocket` (geri çekilme 0,5→10 sn; 4403/4404 kalıcı; 4429 → 5 sn) | TCP 127.0.0.1:<taban> (SITL SERIAL0) |
| ← `hello{session, can_command, gates, flight_id, …}` | hazır | yerel `hello` üretilir |
| ← `tele` (ts, tbm, lat, lon, alt_msl, alt_rel, roll, pitch, yaw, rs, ps, ys, vn, ve, vd, airspeed, groundspeed, heading, climb, throttle, mode, armed, gps_fix, sats, batt_v, batt_pct, rc[8], home, speedup, fuel{l,pct,lpm,range_km,empty}, elec{wh,pct,w,min_left,cells,v,empty}, wind_spd, wind_dir, motor_cut, servo[8], servo_fn[8], gcs_connected, sens_bad?, ekf?, vibe?, terrain_agl?) | `FCaySimTelemetry::FromJson` + `FSmvTeleExtra::FromJson` | MAVLink → aynı alanlar (`HandleFrame`/`Publish`: ATTITUDE, GLOBAL_POSITION_INT, VFR_HUD, SYS_STATUS, GPS_RAW_INT, RC_CHANNELS, SERVO_OUTPUT_RAW, HOME_POSITION, PARAM_VALUE); 18 ms SITL saati kapısı, 0,25 sn bekçi |
| ← `event{text, severity}`, `state{state}`, `launch{method, phase}`, `crash{…}`, `warn{kind}`, `target`, `engage`, `servo_trigger{phase}`, `end{reason}`, `res{rid, name, ok, error}`, `pong` | `HandleWs` → `OnMessage` | aynı şekiller yerelde üretilir |
| → `{t:'rc', ch:[8]}` 20 Hz (0 = dokunma, 8×0 = bırak; pwm = 1500 + v·500) | `SendRc` | RC_CHANNELS_OVERRIDE (sysid 255/191; 0 → 65535 "dokunma", 8×0 → 0 "bırak") |
| → `{t:'cmd', rid, name, params}`: arm, disarm, mode{mode}, takeoff{alt}, rtl, set_home, speed{factor}, servo_set{ch,pwm}, servo_trigger{on,ch,pwm,cmp,tol,delay_ms}, motor_cut{on}, reset, crash{kind,speed}, engage{action,i}, camera{type}, gpws{events,cfg} | `Command` | COMMAND_LONG 400 (21196), 176, 22, 20, 179, 183, 218 (31), PARAM_SET SIM_SPEEDUP; reset = SITL yeniden başlat (eeprom korunur) |
| → `{t:'ping'}` 10 sn | `FSmvSocket` | GCS HEARTBEAT 1 Hz |
| `POST /api/fly/{fid}/stop {reason, gates{passed,missed,laps,best_lap_s}}` → `{ok, flight}` | `Stop` → `FSmvFlightSummary::FromServerFlight` | `Finish()` → yerel özet (+ .BIN taşınır) |
| `GET /api/fly/{fid}` (karşılaşma uçuşu) | `AttachExisting` | — |
| `GET /api/ops/{id}` | `FetchSummary` | `guest/flights.json` |

## Karşılaşma (yalnız hesapla)

| Uç | C++ |
|---|---|
| `POST /api/match {drone_id, track_id, launch?, camera, kind, rings?, dogfight?, password, max_players}` | `USmvMatchClient::Create` |
| `POST /api/match/ai {…, level, bots}` | `CreateAi` |
| `POST /api/match/join {code, password}` | `Join` |
| `POST /api/match/{id}/ready|start|leave|cancel|bot` | `SetReady`, `Start`, `Leave`, `Cancel`, `AddBot` |
| `GET /api/match/{id}` | `Refresh` |
| `WS /ws/match/{id}`: `hello{match, me}`, `match`, `pos{p[{id,lat,lon,alt_msl,yaw,roll,pitch,gs,rings}]}` 10 Hz, `ring`, `finish`, `state`, `countdown`, `dogfight`, `result` | `HandleWs` → `OnPositions` (rakip aktörleri), `OnUpdated` |
| Sunucu uçuşu açınca (HAZIRLIK) `players[me].flight_id` → `USmvCloudFlightLink::AttachExisting` | `USmvFlow::EnsureMatchClient` |

## Yerel SITL (misafir) — sunucu mantığının istemci karşılığı

| Sunucu (`app/…`) | Simumav | Sınama |
|---|---|---|
| `drone_sitl._baslat_port` komut satırı, 10'luk port bloğu, temiz eeprom, `varsayilan.parm` | `SmvCore::CommandLine`, `DefaultsParmText`, `USmvLocalFlightLink::LaunchSitl/FindFreePortBlock` | `test_core.py` (parm metni, komut satırı) |
| `flightsim._ilk_heartbeat` parametre sırası | `SmvCore::FirstHeartbeatParams` | kısmi (kalkış seti, esinti formülü) |
| `launch_parametreleri`, `LAYOUT_PARAMLARI`, rover/INTERCEPTOR setleri | `LaunchParams`, `LayoutParams`, `RoverParams`, `InterceptorParams` | `test_core.py` (birebir) |
| `flightsim_power.GucDurumu` | `SmvCore::FPowerState` | `test_core.py` (600 adım × 12 senaryo) |
| `flightsim_gpws.ozetle` + `flightsim_gpws.js` | `SmvCore::SummarizeAlerts`, `FGpws` | `test_core.py` (Python + Node) |
| `flightsim_targets` | `SmvCore::FTargetTracker`, `TargetAt` | `test_core.py` |
| `flightsim_servotrig` | `SmvCore::FServoTrigger` | gözden geçirme |
| `flightsim_match_core` (halkalar, geçiş, it dalaşı) | `SmvCore::GenerateRings`, `FRingTracker`, `FDogfight` | `test_core.py` (Python MT19937 birebir) |
| `flightsim_match_bot` | `SmvCore::FBotPilot` (kinematik) | duman (`BOT`: tüm seviyeler parkuru bitirir) |
| MAVLink | `SmvMav` (v1/v2, CRC_EXTRA) | `tests/cpp/test_mavlink.py` (pymavlink, 600 çerçeve) |
