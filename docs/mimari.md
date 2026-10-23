# Mimari — Simumav (Steam) ve CAYSIM (HEPGO kurumsal)

Bu depo iki ürünü tek Unreal projesinde (`Simumav.uproject`, UE 5.8) taşır:

- **Simumav** — Steam'de satılan, simumav.com'un eşi olan dron uçuş simülatörü (modül `Source/Simumav`, birincil oyun modülü).
- **CAYSIM** — HEPGO ERP'ye bağlanan kurumsal görselleştirme istemcisi (modül `Source/CaySim`; aşağıdaki §A sonrası bölümler). Simumav, CAYSIM'in
  dünya kurulumunu (Cesium), dron aktörünü, telemetri tamponunu ve askerî HUD'unu ortak çekirdek olarak kullanır.

## A. Simumav

### A.1 Temel karar: fizik her zaman ArduPilot SITL — iki yerde çalışabilir

```
                      ┌──────────────────────── Simumav (Windows, Steam) ────────────────────────┐
 MİSAFİR (varsayılan) │ USmvLocalFlightLink ──TCP 127.0.0.1:15670── ArduCopter/Plane/Rover.exe   │
 giriş yok            │   (MAVLink: SmvMav)        (alt süreç, temiz eeprom, varsayilan.parm)     │
                      │   oturum mantığı: SmvCore (güç, GPWS, hedefler, servo tetik, yarış, bot)  │
                      │   Mission Planner ⇄ UDP 14550 (ham köprü, GCS kilidi)                     │
                      │                                                                           │
 HESAPLA (isteğe bağlı)│ USmvCloudFlightLink ──WSS /ws/fly/{sim_id} (Cookie smsid)──► simumav.com │
 "Bulutta uç"         │ USmvMatchClient ─────WSS /ws/match/{id} + REST /api/match                 │
                      │ USmvDataService ─────REST /api/hangar|crew|tracks|missions|settings      │
                      │                                                                           │
 ortak                │ USmvFlightLink (soyut) → OnTele/OnMessage → ASmvGameMode → dron/HUD/ses   │
                      └───────────────────────────────────────────────────────────────────────────┘
```

- **Giriş zorunlu değildir.** Oyun misafir kipte açılır; profil Steam kimliğiyle yereldedir (`Saved/Simumav/Profiles/<steam_id>/`).
- İki bağlantı da **sunucu WS sözleşmesindeki ileti şekillerini** üretir (`tele`, `event`, `state`, `launch`, `crash`, `warn`, `target`, `engage`,
  `servo_trigger`, `res`, `end`); oyun katmanı fiziğin nerede olduğunu bilmez.
- Sunucu oturum mantığının istemci karşılığı **saf C++ çekirdektedir** (`Private/Core`, `Private/Mavlink`, `Private/Crypto`; Unreal'a bağımlı
  değil) ve `tests/cpp` altında sunucunun Python modüllerine / JS değerlendiricisine / pymavlink'e karşı **birebir** sınanır.

### A.2 Sınıflar

| Sınıf | Görev |
|---|---|
| `USmvGameInstance` (UCaySimGameInstance'tan) | uygulama ömrü: `USimumavClient`, `USmvDataService`, `USmvFlow` |
| `USmvSteamSubsystem` | Steam kimliği/adı, başarımlar (yerel kuyruk + yeniden deneme), Steam katmanında URL |
| `USimumavClient` | simumav.com REST/WS: mastertool PBKDF2 girişi, `smsid` çerezi, CSRF Origin, hata ayrıştırma |
| `FSmvSecrets` | belirteç/çerez: Windows Kimlik Bilgisi Yöneticisi; yedek AES dosyası |
| `FSmvSocket` | yeniden bağlanan WebSocket (geri çekilme, ping, ölü bağlantı bekçisi, 4403/4404 kalıcı) |
| `USmvDataService` | hangar/mürettebat/parkur/görev/ayar/kayıt — yerel JSON ya da REST; katalog; GLB önbelleği; misafir → hesap aktarımı |
| `USmvFlow` | ekran durum makinesi, sihirbaz taslağı, uçuş başlat/bitir, özet + başarım değerlendirmesi, liderlik, karşılaşma |
| `USmvFlightLink` / `USmvLocalFlightLink` / `USmvCloudFlightLink` | fizik bağlantısı (yerel SITL / sunucu) |
| `USmvMatchClient` | çevrim içi salon, YZ karşılaşması, `pos` → rakip aktörleri |
| `ASmvGameMode` | sahne: CAYSIM `ACaySimWorldSetup` (Cesium/düz) + `ASmvProcTerrain`, `ASmvDroneActor`, kapılar, hedefler, yerel botlar, GPWS, ses, çarpışma |
| `ASmvPilotPawn` | 7 kamera kipi, PiP, görüntü tipi, Enhanced Input (kodla) → RC kanalları, eylemler |
| `USmvHudOverlay` + CAYSIM `UCaySimHudWidget` | HUD (NativePaint; UMG varlığı yok) |
| `SSmvShell` | tüm menüler (Slate, kodla) |
| `USmvMotorSynth`, `USmvToneSynth` | prosedürel motor sesi ve uyarı tonları (USynthComponent) |
| `FSmvI18n` | 7 dil (`Content/Simumav/Lang/*.json`) |

### A.3 İçerik varlıkları
İkili `.uasset` elle üretilmez: harita (`/Game/Maps/SimumavMain`, boş) ve malzemeler (`M_SmvTerrain`, `M_SmvGlow`) `Scripts/Python/simumav_varliklar.py`
ile CI'da headless üretilir; geri kalan her şey (ışık, gök, bulut, sis, arazi, dron gövdesi, kapılar, arayüz, girdi) çalışma anında C++'tan kurulur.
Malzeme bulunamazsa motorun `BasicShapeMaterial`'ına düşülür.

### A.4 Yerel SITL yaşam döngüsü
1. Boş 10'luk port bloğu (15670 + 10·k), `Saved/Simumav/SitlRun/<port>/` çalışma dizini, temiz başlangıçta `eeprom.bin` + script dizinleri silinir.
2. `varsayilan.parm` (FRAME_CLASS, FS_GCS_ENABLE 0, MAV_SYSID, ARMING_SKIPCHK 16, LOG_*, tip seti, kayıtlı parametreler, düzen seti) yazılır;
   `<İkili>.exe --model … --home lat,lon,alt,yön --base-port P --defaults … --sysid 1`.
3. 1 sn aralıkla TCP bağlanma (≤ 40 sn); ilk araç HEARTBEAT'inde akış istekleri (SET_MESSAGE_INTERVAL; ArduPlane reddederse akış tabanlı),
   HOME, SERVOn_FUNCTION, ilk kalp atışı parametre seti (sunucuyla aynı sıra), `state HAZIR`.
4. 1 Hz GCS kalp atışı, 3 sn'de bir akış yenileme, esinti 1 Hz, 0,25 sn tele bekçisi.
5. Sıfırla: süreç durur, aynı port/sysid ile eeprom korunarak yeniden açılır; oturumda yazılan parametreler (MP dahil) yeniden yazılır.
6. Bitir: süreç kapanır, `.BIN` logları `Saved/Simumav/Logs`'a taşınır, özet üretilir.

### A.5 Bilinçli farklar (yerel ↔ sunucu)
Yerel resmi 4.7.2 ikilisi HEPGO yamalarını (`SIM_SRV_REV`, `SIM_PLN_THR_MUL`) tanımaz; arazi verisi (TERRAIN_DATA) sunulmaz; yerel botlar kinematiktir.
Ayrıntı: docs/ozellik-esligi.md.

---

# B. CAYSIM mimarisi (HEPGO kurumsal)

CAYSIM, HEPGO ERP'nin **Uçuş Simülatörü** modülünün Unreal Engine 5 tabanlı, ayrı çalışan (Windows .exe) yüksek gerçekçilik
görselleştirme ve kumanda istemcisidir. Tarayıcı sürümü (three.js) HEPGO içinde çalışmaya devam eder; CAYSIM onun "yüksek gerçekçilik"
kardeşidir ve **aynı sunucu altyapısını** kullanır.

## 1. Temel karar: fizik CAYSIM'de çalışmaz

```
┌────────────── HEPGO sunucusu ──────────────┐        ┌────────── CAYSIM (Windows) ──────────┐
│ ArduPilot SITL  ⇄  MAVLink köprüsü          │  WSS   │ HepgoClient ─► TelemetryBuffer (80 ms) │
│ (fizik, otopilot)   (50 Hz tele, cmd, rc)   │ ─────► │        │                │              │
│         ▲                   │ UDP           │  REST  │   SessionWizard    DroneActor ─► Cesium │
│   Mission Planner ◄─────────┘               │ ◄───── │   (UI akışı)       (transform, pervane) │
│ merkezler · tipler · model GLB · loglar     │        │   HudWidget · CameraPawn · LaunchRig    │
└─────────────────────────────────────────────┘        └──────────────────────────────────────────┘
```

- SITL süreci, UDP/Mission Planner köprüsü, komut yürütme ve telemetri üretimi **sunucuda** kalır (`app/flightsim.py`, `app/caysim.py`).
- CAYSIM yalnız **görselleştirir ve kumanda eder**: WebSocket'ten gelen 50 Hz telemetriyi gecikme tamponlu enterpolasyonla
  Unreal karesine taşır; komutları ve RC'yi WebSocket'e yazar.
- Mission Planner yine HEPGO'nun UDP portuna bağlanır. CAYSIM isteğe bağlı olarak aynı akışı izleyebilir (`GET /sessions/{id}/mavlink`),
  ancak oturumda tek UDP GCS olduğundan varsayılan yol WS telemetrisidir.

## 2. Modül ve sınıflar (`Source/CaySim`)

| Sınıf | Görev |
|---|---|
| `FCaySimModule` (`CaySim.h/.cpp`) | Modül giriş noktası, `LogCaySim` günlük kategorisi |
| `CaySimTypes.h` | Sözleşme v1 veri tipleri: `FCaySimHello`, `FCaySimSession`, `FCaySimTelemetry`, `FCaySimWorld`, `FCaySimModelInfo`, `FCaySimCenter`, `FCaySimLogInfo`, `FCaySimLaunchEvent`… JSON ↔ struct |
| `UHepgoClient` | REST (`Authorization: Bearer`, `X-Caysim-*`) + WebSocket (`?key=`), hata biçimi `detail.error/message`, yeniden bağlanma 0,5→10 sn + `GET /sessions/{id}`, ping/pong RTT, saat eşitleme (`tele.ts` EMA), RC kısma (`limits.rc_hz`) |
| `FTelemetryBuffer` | Sunucu zamanına göre sıralı kare tamponu; `Sample(now − 80 ms)` → konum doğrusal, yönelim **slerp**; tampon kuruyunca ≤ 250 ms dead-reckoning; `tbm` geri gidince sıfırlama |
| `CaySimGeo.h` + `ICaySimGeoTransform` | WGS84 ↔ ENU/ECEF, NED → Unreal (`Yaw = heading − 90°`, ESU çerçevesi), dünya kurulumunun sağladığı dönüşüm arayüzü |
| `ACaySimDroneActor` | Telemetri → transform (georeference üzerinden), yer kesişim koruması, pervane/kontrol yüzeyi animasyonu, HEPGO GLB (glTFRuntime) ya da dahili yer tutucu gövde, `orient` uygulaması |
| `ACaySimWorldSetup` | Cesium georeference (merkez = oturum merkezi), Cesium World Terrain + Bing ya da Google Photorealistic 3D Tiles (`/g3d-key`), Sky Atmosphere / sis / bulut / saat; Cesium yoksa düz zemin; `ICaySimGeoTransform` uygular; yer ofseti (MSL ↔ elipsoid) ölçümü |
| `ACaySimLaunchRig` | Kalkış donanımı: pist şeridi, 2 eksenli pnömatik rampa (açı), pickup araç (hız); evreler telemetriden (armed → hız eşiği → irtifa) ya da ileride `launch` olayından |
| `UCaySimReplayPlayer` | `GET /sessions/{id}/replay` gzip JSON → kare dizisi; oynat/duraklat/hız/zaman; enterpolasyon; olay şeridi |
| `UCaySimHudWidget` | Askeri FPV HUD (Slate çizimi): pitch merdiveni, roll yayı, heading şeridi, hız/irtifa bantları, uyarılar |
| `UCaySimSessionWizard` | UI durum makinesi: istemci kodu → şirket → kip → merkez/log → tip → model → dünya → kalkış → özet → oturum |
| `ACaySimCameraPawn` | Takip / FPV (gimbal) / serbest / yer kameraları; gamepad → RC (isteğe bağlı) |
| `ACaySimGameMode` | Sahneyi kurar, WS olaylarını dağıtır, HUD bağlamını günceller |
| `UCaySimGameInstance` | `UHepgoClient`, sihirbaz ve oynatıcıyı uygulama ömrünce tutar |
| `UCaySimSaveGame` | İstemci kodu (makineye bağlı karartma), HUD tercihleri |

## 3. Koordinat sözleşmesi

- **Georeference**: `ACesiumGeoreference` orijini = seçilen merkez (`lon, lat, alt_msl + geoid`). Cesium for Unreal çerçevesi **ESU**
  (sol el): `+X Doğu`, `+Y Güney`, `+Z Yukarı`, birim cm.
- **Yönelim** (NED → Unreal): `Rotator.Yaw = deg(yaw) − 90`, `Rotator.Pitch = deg(pitch)`, `Rotator.Roll = deg(roll)`; dünya eğriliği
  için gövde yönelimi `EnuToUnrealRotation(konum)` ile çarpılır (`ComputeEastSouthUpToUnrealTransformation`).
  Sağlama: heading 90 → +X, heading 0 → −Y, `vd < 0` → Z artar. İşaret anlaşmazlığında `bInvertRoll/bInvertPitch` anahtarları.
- **Hız**: `(X, Y, Z) = (ve, −vn, −vd) × 100` cm/s.
- **İrtifa**: `alt_msl` MSL; Cesium elipsoid ister. `GeoidOffsetM` (varsayılan 35 m) ile başlanır, 3D Tiles yüklenince merkez ışınıyla
  gerçek yer ofseti ölçülür (`MeasureGroundOffset`) ve ilk kalkışa dek düzeltilir.
- **Model**: GLB Y-yukarı sağ el; `orient {forward, up, scale, offset, prop}` → gövde çerçevesine dönüş; glTFRuntime `YForward` tabanı.

## 4. Zamanlama

- WS kareleri olay güdümlüdür (≈ 50 Hz, kopter 20 ms / uçak 25 ms). `FTelemetryBuffer` kareleri **sunucu zamanı** `ts` ile tutar;
  `UHepgoClient::NowServer()` yerel saat + EMA ofsettir. Render her karede `Sample(NowServer − 80 ms)`.
- Tampon kuruyunca (kayıp/gecikme) hız vektörüyle en çok 250 ms ileri taşıma, sonra son kare tutulur. 5 sn'den uzun boşlukta tutma.
- Kayıttan oynatma sabit `hz` (25) kare dizisidir; `UCaySimReplayPlayer::Tick` oynatma saatini `Speed` ile ilerletir, `SampleAt` iki komşu
  kareden enterpolasyon yapar; `DroneActor::ApplyPoseDirect` tamponu atlar.

## 5. Varlıklar (Content/)

Kaynak kodda **hiç ikili varlık yok**; Editor'da üretilir ve Git LFS ile saklanır:
- `Maps/CaySimMain.umap` — boş sahne + `BP_GameMode` (ya da C++ `ACaySimGameMode`), Cesium georeference, SunSky.
- `Drones/` — dahili yer tutucu gövdeler (quad, hexa, heli, sabit kanat) + pervane mesh'i → `BP_Drone` (ACaySimDroneActor alt sınıfı, `BuiltinMeshes` atanır).
- `Launch/` — rampa tabanı/ray/taşıyıcı, pickup gövde + tekerlek, pist şeridi → `BP_LaunchRig`.
- `UI/` — `WBP_Wizard` (sihirbaz ekranları; `UCaySimSessionWizard` olaylarına bağlanır), `WBP_Hud` (UCaySimHudWidget alt sınıfı).
- `Input/` — `IMC_CaySim` ve `IA_*` Enhanced Input varlıkları (kamera, serbest hareket, RC eksenleri).

## 6. İsteğe bağlı eklentiler ve kararlar

| Eklenti | Durum | Not |
|---|---|---|
| **Cesium for Unreal** | zorunlu (gerçek dünya) | Ücretsiz; Cesium World Terrain + Bing için **Cesium ion hesabı/belirteci** gerekir (ücretsiz katman yeterli). Yoksa Google 3D Tiles (HEPGO anahtarı) ya da düz zemin. |
| **glTFRuntime** | isteğe bağlı | HEPGO model kütüphanesindeki GLB'leri çalışma zamanında yüklemek için. Ücretli Marketplace eklentisi ya da GitHub'dan kaynak (MIT). Yoksa dahili yer tutucu gövdeler kullanılır; CAYMAZ GLB'si derleme öncesi Content'e aktarılabilir. |
| Enhanced Input | motorla gelir | Varlıklar atanmazsa `DefaultInput.ini` eski eşlemeleri çalışır. |

Ücretli varlık/eklenti ve Cesium ion hesabı gereksinimi kullanıcı onayına tabidir (bkz. README › Sonraki adımlar).
