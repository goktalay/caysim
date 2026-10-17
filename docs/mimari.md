# CAYSIM mimarisi

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
