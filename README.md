# Simumav (Steam) · CAYSIM

Bu depo iki ürünü tek Unreal Engine 5.8 projesinde (`Simumav.uproject`) taşır:

- **Simumav** — Steam'de yayımlanacak dron uçuş simülatörü; simumav.com'un eşi. Fizik **gerçek ArduPilot SITL**: misafir kipte (varsayılan,
  giriş gerekmez) oyunla dağıtılan SITL bu bilgisayarda çalışır, simumav.com hesabıyla isteğe bağlı "Bulutta uç", çevrim içi karşılaşma, TOP 10 ve
  bulut eşitleme açılır. Modül: `Source/Simumav`.
- **CAYSIM** — HEPGO ERP'nin kurumsal yüksek gerçekçilik istemcisi (aşağıdaki bölümler). Modül: `Source/CaySim` (Simumav'ın da ortak çekirdeği).

| Belge | İçerik |
|---|---|
| [docs/mimari.md](docs/mimari.md) | Simumav + CAYSIM mimarisi, yerel SITL yaşam döngüsü |
| [docs/ozellik-esligi.md](docs/ozellik-esligi.md) | SIMUMAV web ↔ Simumav UE özellik eşliği matrisi |
| [docs/simumav-api-esleme.md](docs/simumav-api-esleme.md) | simumav.com API / WS ↔ C++ eşlemesi, yerel karşılıklar |
| [docs/derleme-windows.md](docs/derleme-windows.md) | Şirket bilgisayarı kurulumu, self-hosted runner, derleme, paketleme |
| [docs/steam-yayin.md](docs/steam-yayin.md) | Steamworks, App ID, depot, mağaza varlıkları, yaş derecelendirme, yükleme |
| [docs/ekranlar/README.md](docs/ekranlar/README.md) | **Ekran görüntüleri galerisi** (otomatik duman testi; yol haritası ekranları, Fab haritaları, şehir trafiği/kalabalığı, gemi savunma, yükleme ekranları ve uçuşa giriş kareleri, Tactical Soldier Outfit giydirilmiş MetaHuman pilot) |
| [docs/fab-icerik.md](docs/fab-icerik.md) | Fab içerikleri: hazırlama, haritalar, şehir hayatı (City Traffic Pro, kalabalık), yeni dron gövdeleri, Mogami |

Sınamalar (Unreal'sız, bu sunucuda): `tests/cpp/test_mavlink.py` (pymavlink), `tests/cpp/test_kdf.py` (hashlib), `tests/cpp/test_core.py`
(sunucu Python modülleri + flightsim_gpws.js), `tests/ue_denetim.py` (UHT/include/modül), `tests/test_sozlesme_simumav.py` (yerel SIMUMAV ile canlı sözleşme).

---

# CAYSIM (HEPGO kurumsal)

**CAYSIM**, HEPGO ERP'nin *Uçuş Simülatörü* modülü için Unreal Engine 5 tabanlı, ayrı çalışan (Windows .exe) **yüksek gerçekçilik
görselleştirme ve kumanda istemcisidir**. Tarayıcı sürümü (three.js) HEPGO içinde çalışmaya devam eder; CAYSIM aynı sunucu
altyapısına bağlanan "yüksek gerçekçilik" kardeşidir.

SKYREX HAVACILIK / GÖKKUBBE Teknoloji — özel lisans.

## Ne yapar, ne yapmaz

- **Yapar:** Cesium for Unreal ile gerçek dünya (Cesium World Terrain + uydu ya da Google Photorealistic 3D Tiles), Lumen/Nanite, Sky
  Atmosphere, hacimsel bulut, gece/gündüz; dron modeli (HEPGO model kütüphanesinden GLB), pervane/kontrol yüzeyi animasyonu; FPV kamera +
  askeri HUD; takip/serbest/yer kameraları; kalkış donanımı (pist koşusu, pnömatik rampa, araç üstünden bırakma); kayıttan oynatma.
- **Yapmaz:** Fizik ve uçuş dinamiği. ArduPilot SITL, Mission Planner UDP köprüsü, komut yürütme ve 50 Hz telemetri **HEPGO sunucusunda**
  çalışır. CAYSIM telemetriyi WebSocket'ten alır, komut ve RC'yi WebSocket'e yazar.

## Mimari (özet)

```
HEPGO sunucusu                                   CAYSIM (Windows)
ArduPilot SITL ⇄ MAVLink köprüsü ──WSS 50 Hz──►  HepgoClient → TelemetryBuffer (80 ms, slerp) → DroneActor → Cesium dünyası
Mission Planner ⇄ UDP                 ◄──REST──  SessionWizard (istemci kodu → şirket → merkez → tip → model → dünya → kalkış → başlat)
merkezler · tipler · GLB · loglar                HudWidget · CameraPawn · LaunchRig · ReplayPlayer
```

Ayrıntı: [docs/mimari.md](docs/mimari.md). API sözleşmesi (HEPGO ile birebir): [docs/caysim-api.md](docs/caysim-api.md);
CAYSIM tarafı eşlemesi: [docs/hepgo-baglanti.md](docs/hepgo-baglanti.md).

## Depo düzeni

```
Simumav.uproject           UE 5.8 projesi (Simumav + CaySim modülleri; Cesium/glTFRuntime isteğe bağlı)
Source/CaySim/             C++ modülü (Build.cs, Public/*.h, Private/*.cpp)
Config/                    DefaultEngine.ini (Lumen/Nanite/DX12), DefaultGame.ini, DefaultInput.ini, UserSecrets.ini.ornek
Content/                   Editor'da üretilen varlıklar (Git LFS): Maps, Drones, Launch, UI, Input
Plugins/                   CesiumForUnreal / glTFRuntime (motora kurulmadıysa buraya)
docs/                      mimari.md · caysim-api.md · hepgo-baglanti.md · derleme-windows.md
```

## Kurulum ve derleme

Windows 10/11, Unreal Engine **5.8**, Visual Studio 2022 (C++ oyun geliştirme), Cesium for Unreal eklentisi, Git LFS.
Adım adım: [docs/derleme-windows.md](docs/derleme-windows.md).

Kısa yol:
```powershell
git lfs install && git clone https://github.com/goktalay/caysim.git && cd caysim
copy Config\UserSecrets.ini.ornek Config\UserSecrets.ini     # Cesium ion belirteci
# Simumav.uproject › sağ tık › Generate Visual Studio project files › Simumav.sln › Development Editor › Build
```

## HEPGO'ya bağlanma — istemci kodu

1. HEPGO'da **Dron Yönetimi › Uçuş Simülatörü › CAYSIM istemcileri › İstemci ekle** (ad: ör. "Simülatör PC-1").
2. **Bir kez** gösterilen kodu kopyalayın: `<alan adı>/<anahtar>` (ör. `caymaz.app/hV3k…`, anahtar 43 karakter).
3. CAYSIM'i açın, açılış ekranına kodu yapıştırın → **Bağlan**. Şirket adı ve logosu görünür; kod bu bilgisayarda saklanır
   (makineye bağlı karartma; günlüğe yazılmaz). Yanlış/yenilenmiş kodda 401 → kod adımına dönülür.
4. Sihirbaz: **Kip** (canlı SITL / kayıttan oynatma) → **Merkez** (kalkış koordinatı) → **Dron tipi** → **Model** (kütüphaneden küçük
   resimli) → **Dünya** (Cesium / Google 3B, saat, sis, rüzgâr, pist) → **Kalkış yöntemi** (sabit kanatta pist / rampa açısı / araç hızı) →
   **Başlat**. SITL sunucuda açılır (10–45 sn), telemetri gelir.
5. Mission Planner'ı HEPGO'nun bildirdiği `host:udp_port` adresine bağlayın (özet ekranında yazar); USB kumandayı Mission Planner joystick
   ile kullanın. İsteğe bağlı: gamepad'i doğrudan CAYSIM'den RC olarak gönderme (`BP_CameraPawn › Gamepad Rc`).

Tuşlar: **C** kamera döngüsü (takip → dron kamerası/HUD → serbest → yer), **H** HUD aç/kapa, fare FPV'de gimbal, WASD+E/Q serbest kamera,
tekerlek yakınlaştırma.

## Durum

İskelet aşaması: tüm C++ sınıfları ve belgeler yazıldı; **henüz derlenmedi** (bu ortamda Unreal yok). İlk Windows derlemesinde UE/Cesium
API ayrıntılarında küçük düzeltmeler beklenir. Varlıklar (`Content/`) Editor'da üretilecek.

## Sonraki adımlar

1. Windows'ta ilk derleme ve derleme hatalarının giderilmesi.
2. Editor'da `CaySimMain` haritası, `BP_GameMode`, `BP_Drone` (yer tutucu gövdeler), `BP_WorldSetup`, `WBP_Wizard`, `WBP_Hud`, Enhanced Input.
3. HEPGO DEV sunucusuna canlı bağlantı testi (hello → centers → session → tele) ve koordinat işaretlerinin sahada doğrulanması.
4. Kararlar (kullanıcı onayı): Cesium ion hesabı/belirteci; glTFRuntime (ücretli Marketplace ya da MIT kaynak); CAYMAZ GLB'nin derleme
   öncesi Content'e alınması.
5. Kalkış donanımı görsel varlıkları; `launch` olayı HEPGO tarafında eklenince `OnLaunch` zaten bağlı.
6. HEPGO arazi vekili (`/terrain`, `/tile`, `/osm`) ile Cesium'suz gerçek arazi kipi.
