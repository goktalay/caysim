# HEPGO bağlantısı — CAYSIM tarafı

Sözleşmenin tam metni: **[docs/caysim-api.md](caysim-api.md)** (HEPGO deposundaki `docs/caysim-api.md` ile birebir aynı; HEPGO oturumu
güncelledikçe buraya da kopyalanır). Bu sayfa, sözleşmenin CAYSIM kodunda nereye düştüğünü özetler.

## İstemci kodu

1. HEPGO'da **Dron Yönetimi › Uçuş Simülatörü › CAYSIM istemcileri › İstemci ekle** → bir kez gösterilen kod `<domain>/<key>`
   (ör. `caymaz.app/hV3k…`, anahtar 43 karakter `[A-Za-z0-9_-]`).
2. CAYSIM açılış ekranına yapıştırılır → `FHepgoClientCode::Parse` ilk `/` konumundan böler; `https://<domain>` + `wss://<domain>`;
   yerel ağ/port (`10.x`, `192.168.x`, `localhost`, `:port`) ya da `http://` ön eki → düz http/ws.
3. Kod `UCaySimSaveGame` ile makineye bağlı karartılarak saklanır (günlüğe yazılmaz); bir sonraki açılışta doğrudan bağlanır.
   **Kodu yenile** (rekey) yapılırsa 401/4403 gelir → sihirbaz kod adımına döner.

## Uç → sınıf eşlemesi

| Sözleşme | CAYSIM |
|---|---|
| `GET /hello` (+ `X-Caysim-Host/-Ver/-Platform`) | `UHepgoClient::Hello` → `FCaySimHello` (şirket adı/ünvan/logo/tema rengi, caps, limits, host, google3d, sitl, ws_url); `v ≠ 1` uyarısı |
| `GET /brand/logo` | `UCaySimSessionWizard::LoadImage` (Bearer ile `FetchBytes` → `FImageUtils::ImportBufferAsTexture2D`; SVG desteklenmez → PNG/JPEG/WebP beklenir) |
| `GET /centers` `{centers}` | `FetchCenters` → `OnCenters` (`alt` null → `bHasAlt=false`) |
| `GET /types` `{types}` | `FetchTypes`; `key` → `ECaySimDroneType` (`multirotor, hexarotor, helikopter, sabit_kanat, vtol`) |
| `GET /models?type=` `{models, defaults}` | `FetchModels` → `OnModels` + `GetDefaultModelFor(type)`; `ready=false` → dahili gövde |
| `GET /models/{id}/glb`, `/thumb` | `ACaySimDroneActor::Setup` (GLB, glTFRuntime varsa) · sihirbaz küçük resimleri |
| `GET /world-defaults` | `FetchWorldDefaults` (`world_defaults` alanı) |
| `GET /terrain`, `/tile`, `/osm` | `FetchTerrain/FetchOsm` (ham JSON) — `ECaySimWorldSource::HepgoTerrain` ileride |
| `GET /g3d-key` | `FetchGoogle3dKey` (404 sessiz) → `ACaySimWorldSetup::SetupGoogle` |
| `GET /logs?q=` `{logs}` | `FetchLogs` → sihirbaz Log adımı |
| `GET/POST /sessions`, `GET/DELETE /sessions/{id}` | `FetchSessions/CreateSitlSession/CreateReplaySession/FetchSession/DeleteSession` → `FCaySimSession` (`state` BASLIYOR/HAZIR/HATA/KAPANDI) |
| `POST /sessions/{id}/cmd` | `SendCommandRest` (WS kapalıyken yedek) |
| `GET /sessions/{id}/replay` (gzip) | `UCaySimReplayPlayer::Load` (409 → 2 sn'de bir yeniden dener; zlib `inflateInit2(16+MAX_WBITS)`) |
| `GET /sessions/{id}/mavlink` | `FetchMavlinkInfo` → `FCaySimMavlinkInfo` |
| Hata `{"detail":{"error","message"}}` | `ParseError` → `OnError(kod, metin)`; 429 `rate_limited`, 403 `client_inactive` |

## WebSocket

- URL `wss://<domain>/ws/caysim/v1/sessions/<id>?key=<key>`; bağlanınca ayrıca `{"t":"auth","key"}` çerçevesi (sunucu ikisini de kabul eder).
- Sunucu → istemci: `hello {session, can_command, v, server_time}` → `OnWsHello`; `tele` → `OnTelemetryNative` (C++) + `OnTelemetry` (Blueprint);
  `event {text, severity, ts}`; `state {state, replay?}`; `end {reason}` (kapatildi/sure_doldu/bosta/sunucu_kapaniyor/hata) → yeniden denenmez;
  `res {name, rid, ok, error?}`; `pong`; `replay {action:'reset'}`; `error {error}`. `launch` v1'de gelmez (ayrılmış; `LaunchRig` telemetriden türetir).
- İstemci → sunucu: `cmd {name, params, rid}` (`SendCommand`, `CmdArm/CmdMode/CmdTakeoff/CmdRtl/CmdReset/CmdSetHome/CmdCrash`),
  `rc {ch:[8]}` (`SendRc`; `limits.rc_hz` × 0,625 ≈ 25 Hz'e kısılır; `[0]*8` serbest bırakma sınırlanmaz), `ping` 15 sn'de bir.
- Kapanış kodları: `4400` → yeniden dene; `4403` (anahtar/pasif/rekey) ve `4404` → kalıcı `Failed`; `4429` → 60 sn bekle; `1000` → `end` sonrası.
- Yeniden bağlanma: 0,5 → 1 → 2 → 4 → 8 → 10 sn; her denemede önce `GET /sessions/{id}` (KAPANDI → `OnEnd`). 5 sn hiç çerçeve gelmezse
  (bekçi ≥ 4 Hz olmalı) bağlantı ölü sayılır.
- Saat: `tele.ts` − yerel UTC farkı EMA (0,02) ile izlenir → `NowServer()`; `FTelemetryBuffer` kareleri bu zamanda tutar; `tbm` 1 sn'den çok
  geri giderse (SITL reset) tampon sıfırlanır.

## Sözleşme değişikliğinde

`docs/caysim-api.md` güncellenir → `CaySimTypes.h/.cpp` (alanlar), `HepgoClient.cpp` (uçlar/hata/WS), bu sayfa. `hello.v` artarsa
`/api/caysim/v2` için yeni taban yolu `FHepgoClientCode::HttpBase/WsBase` içinde değiştirilir.
