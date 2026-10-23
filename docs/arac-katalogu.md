# Araç kataloğu — modeller, gövde davranışı, SITL fiziği, atmosfer

Simumav'da seçilebilen her araç (dron, uçak, VTOL, helikopter, kara aracı) üç katmandan oluşur:

1. **Gövde (görsel)** — pakete gömülü GLB: `Content/Simumav/ModelsSrc/<dosya>.glb` (UFS; kaynak/lisans aynı klasörde `LICENSES.md`).
   Çalışma anında `SmvCore::FGlbModel` (saf C++, `Source/Simumav/Private/Core/SmvGlb.*`) okur, `USmvGlbBody` ProceduralMesh ile kurar
   ve düğüm adlarından hareketli parçaları çıkarır. Derlemede ayrıca editor Python'u (Interchange glTF) her GLB'yi
   `/Game/Simumav/Models/<dosya>` altına statik ağ olarak içe aktarır (`Scripts/Python/simumav_varliklar.py` → `modeller()`; idempotent,
   kaynak sha1 etiketi; günlükte `MODEL <ad>: …` satırları).
2. **Fizik** — gerçek ArduPilot SITL (4.7.2): ikili, `--model` çerçevesi, çerçeve/kanat JSON'u ve parametre seti tek tabloda:
   `Source/Simumav/Private/Core/SmvVehicles.cpp` (`FVehicleSpec`). Oturum anahtarı `FSitlSession::Vehicle` (= katalog anahtarı);
   `SessionBinary / SessionModel / SessionJson` (SmvSitlConfig) komut satırını ve SITL çalışma dizinine yazılan JSON'u üretir.
3. **Katalog kaydı** — `USmvDataService::Catalog()` (`Source/Simumav/Private/SmvDataService.cpp`): etiket, tip, açıklama, varsayılan
   kullanıcı parametreleri, kontrol düzeni, sürüş, GLB dosyası, ses türü + yukarıdaki tablodan fizik özeti ve istatistikler.

Anahtar önekleri: `glb:` / `proc:` = simumav.com kataloğuyla birebir (bridge.py `GLB_MODELS` / `PROC_MODELS`); `smv:` = yalnız Simumav
(yerel SITL; **`bLocalOnly`**); `fab:` = Fab gövdesi seçeneği (Quadcopter / FPV Interceptor; içerik pakette yoksa yer tutucu gövde).

## Modeller ve çeşitler

| Anahtar | Ad | Tip | İkili | `--model` (+ JSON) | Çerçeve parametreleri | Kütle / boyut | Gövde |
|---|---|---|---|---|---|---|---|
| `glb:iris_fpv` | Iris FPV | fpv | ArduCopter | `x:cerceve.json` | FRAME_CLASS 1, TYPE 1 | 1,05 kg / 0,55 m | iris_fpv.glb |
| `proc:quad250` | FPV Quad 250 | fpv | ArduCopter | `x:cerceve.json` (ArduPilot freestyle 5") | 1 / 1 | 0,8 kg / 0,25 m | prosedürel |
| `fab:fpv` | FPV Interceptor (Fab) | fpv | ArduCopter | `x:cerceve.json` | 1 / 1 | 0,9 kg / 0,35 m | /Game/Fab/DroneFpv |
| `glb:iris` | 3DR Iris | multirotor | ArduCopter | `x:cerceve.json` | 1 / 1 | 1,5 kg / 0,55 m | iris.glb |
| `glb:x500` | X500 | multirotor | ArduCopter | `x:cerceve.json` | 1 / 1 | 2,0 kg / 0,61 m | x500.glb |
| `fab:quad` | Quadcopter (Fab) | multirotor | ArduCopter | `x:cerceve.json` | 1 / 1 | 1,8 kg / 0,52 m | /Game/Fab/Drone |
| `glb:typhoon_h480` | Typhoon H480 | multirotor | ArduCopter | `hexax:cerceve.json` | 2 / 1 | 1,95 kg / 0,52 m | typhoon_h480.glb |
| `smv:octa` | Octa 8 | multirotor | ArduCopter | `octa:cerceve.json` | 3 / 0 | 5 kg / 1,3 m, 3 kg yük | smv_octa.glb |
| `smv:y6` | Y6 koaksiyel | multirotor | ArduCopter | `y6:cerceve.json` | 5 / 0 | 2,5 kg / 0,9 m | smv_y6.glb |
| `smv:cargo_x8` | Kargo X8 | multirotor | ArduCopter | `octa-quad:cerceve.json` (Callisto) | 4 / 1 + Callisto.param | 32,5 kg / 2,1 m, 10 kg yük | smv_cargo_x8.glb |
| `smv:caymaz` | **CAYMAZ V3** | plane | ArduPlane | `plane-elevon` (rampada `-catapult`) | düzen: elevon | 3,03 m açıklık | caymaz.glb (burun +Z → yaw 90) |
| `glb:rc_cessna` | RC Cessna | plane | ArduPlane | `plane` | classic | 1,1 m | rc_cessna.glb |
| `smv:trainer` | Eğitim uçağı | plane | ArduPlane | `plane:kanat.json` (0,62 m², 1,6 m, sürüklemeli) | classic + yumuşak sınırlar | 1,6 m | smv_trainer.glb |
| `glb:believer` | Believer | plane | ArduPlane | `plane:kanat.json` (0,55 m², 1,96 m) | classic | 1,96 m | believer.glb |
| `glb:talon_vtail` | Talon V-kuyruk | plane | ArduPlane | `plane-vtail:kanat.json` | vtail | 1,72 m | talon_vtail.glb |
| `glb:zephyr` | Zephyr | plane | ArduPlane | `plane-elevon:kanat.json` | elevon | 1,5 m | zephyr.glb |
| `glb:jet` | Jet | plane | ArduPlane | `plane-jet` (22 kg) + plane-jet.parm | classic | 3,6 m | jet.glb |
| `glb:standard_vtol` | Standard VTOL | vtol | ArduPlane | `quadplane` + quadplane.parm | Q_FRAME 1 / 1 | 2,15 m | standard_vtol.glb |
| `smv:caymaz_vtol` | **VTOL-CAYMAZ V3** | vtol | ArduPlane | `quadplane-elevon:cerceve.json` (kaldırma + kanat katsayıları tek JSON) | Q_FRAME 1 / 1 + elevon | 8,5 kg / 3,03 m | caymaz.glb + eklenen 4 kaldırma rotoru |
| `smv:tailsitter` | Tailsitter (kanatlı) | vtol | ArduPlane | `quadplane-copter_tailsitter` + autotest tailsitter seti | Q_FRAME 1 / 0 | 3 kg / 1,2 m | smv_tailsitter.glb |
| `glb:icept_x4`, `glb:p1sun`, `glb:sting` | INTERCEPTOR | interceptor | ArduPlane | `quadplane-copter_tailsitter` + INTERCEPTOR seti (HEPGO) | — | — | icept_x4 / p1sun / sting.glb |
| `glb:heli` | Tek rotorlu heli | heli | **ArduHeli** | `heli` + copter-heli.parm | FRAME_CLASS 6 | 4,54 kg / 1,75 m | heli.glb |
| `smv:heli_tandem` | Tandem kargo heli | heli | **ArduHeli** | `heli-dual` + copter-heli-dual.parm | FRAME_CLASS 11 | 9,08 kg / 2,6 m | smv_heli_tandem.glb |
| `smv:coax` | Koaksiyel mini heli | heli | ArduCopter | `coaxcopter` + copter-single/coax.parm | FRAME_CLASS 9 | 2 kg / 0,9 m | smv_coax.glb |
| `glb:rover_4x4` / `glb:rover_ugv` / `glb:rover_bot` | Kara araçları | rover | ArduRover | `rover` / `rover-skid` | — | — | rover_*.glb |

Hedef / dekor modelleri (`USmvDataService::TargetModels()`: `tgt:lancet`, `tgt:octopus`, `tgt:sw300`, `tgt:altius`) **yalnız görsel hedef
nesnesidir** — hangarda yoktur, SITL eşlemesi, güdüm ya da angajman davranışı yoktur.

**Kaynak ve lisans:** `glb:*` dosyaları simumav.com `static/simumav/models` ile aynıdır (PX4 BSD-3, Gazebo Fuel CC-BY/CC0, Poly Pizza
CC0/CC-BY; ayrıntı `LICENSES.md`). `smv_*.glb` Simumav'ın kendi prosedürel üretimidir (`Tools/models/arac_modelleri.py`, dış kaynak yok).
`caymaz.glb` = HEPGO `static/models/caymaz-sim.glb` (şirketin kendi STEP modeli). `p1sun`, `sting`, `tgt_*` "kullanıcı tarafından sağlandı —
lisans teyit edilmeli" satırlarıdır; Steam'de dağıtımdan önce teyit gerekir.

### Uçuş farkları (Linux SITL 4.7.2 ile ölçüldü — `tests/sitl/sitl_varyant.py`)

| Anahtar | Ölçüm |
|---|---|
| glb:iris_fpv | askı gazı %25, 20 m/s komutta ~20 m/s |
| proc:quad250 | askı gazı %12 (5" yarışçı), çok yüksek oran döngüleri |
| fab:fpv | askı gazı %11 |
| glb:iris / fab:quad / glb:x500 | askı gazı %46 / %42 / %41; ~14–16 m/s (eğim sınırı) |
| glb:typhoon_h480 | hexa X, askı %46 |
| smv:octa | octa, askı %43, 17 m/s |
| smv:y6 | Y6 koaksiyel, askı %51 (koaksiyel verim kaybı) |
| smv:cargo_x8 | 32,5 kg, askı %37, yumuşak (yavaş) yönelim |
| smv:caymaz / glb:rc_cessna | AUTO tur ~22 m/s |
| smv:trainer | ~12 m/s (yavaş, affedici) |
| glb:believer / glb:talon_vtail / glb:zephyr | ~26 / ~25 / ~34 m/s |
| glb:jet / proc:interceptor | ~67 / ~72 m/s, 200 m+ tırmanış |
| glb:standard_vtol / smv:caymaz_vtol | VTOL kalkış + geçiş, ~25 / ~22 m/s |
| smv:tailsitter | kuyruk üstünde kalkış + yatay seyir ~23 m/s |
| glb:heli / smv:heli_tandem | rotor devri tamamlanınca kalkış, askı %50 |
| smv:coax | askı %46 |

Sınama: `python3 -I tests/sitl/sitl_varyant.py [anahtar …]` (pymavlink, g++ ve `SIMUMAV_SITL_BIN` altında `arducopter`, `arducopter-heli`,
`arduplane` Linux ikilileri; ikili yoksa ATLANDI). Windows paketinde aynı parametreler ve JSON kullanılır (`ArduCopter.exe`,
`ArduHeli.exe`, `ArduPlane.exe`; `Tools/sitl/sitl-manifest.json`).

**Helikopter motor kilidi:** ArduHeli her zaman RC8 MOTOR_INTERLOCK kullanır. Yerel bağlantı (`USmvLocalFlightLink`) ARM'sızken RC8 =
1000, ARM'lıyken 2000 yazar (0,25 sn'de bir; pilot kanalları ezilmez); GUIDED kalkışta rotor devri tamamlanana dek NAV_TAKEOFF yinelenir.

## Gövde davranışı (`USmvGlbBody`)

Eksen: glTF (sağ el, Y yukarı, burun +X, sağ +Z, metre) → Unreal (X ileri, Y sağ, Z yukarı, cm) = (x, z, y)·100; katalog `GlbYawDeg`
önce uygulanır. Sarım yönü eksen değişiminde (ve ayna dönüşümlü düğümlerde) ters çevrilir. Normal içermeyen ağlar (Gazebo STL türevi
modellerin çoğu; üçgen yönleri tutarsız) düz gölgelenir ve iki yüzlü malzemeyle (`M_SmvPbr2S`) çizilir. Görünürlük ölçeği: açıklık ≤ 0,64 m → ×2,5
(web MODEL_SCALE), 1,6 m'ye doğru ×1'e iner; kara aracı ×1.

| Düğüm adı | Davranış |
|---|---|
| `rotor_*`, `prop*`, `propeller`, `blade*` | Pervane: dönüş ekseni = ağın en ince boyutu (disk), pivot = sınır kutusu merkezi. Kaldırma (dikey eksen) rotorları SERVO_OUTPUT_RAW motor çıkışlarının (SERVOn_FUNCTION 33–40) ortalamasıyla, itici/çekici (ileri eksen) gaz kanalıyla (işlev 70) döner; VTOL'de ikisi ayrı. ARM'da rölanti; çok rotorda çevresel sırada karşıt dönüş, koaksiyel çiftlerde üst/alt karşıt. Hız %20'yi geçince yarı saydam bulanık disk. |
| helikopterde dikey eksenli rotor / yatay eksenli rotor | Ana rotor(lar) / kuyruk rotoru: ARM'da ~4 sn'de tam devir (RSC gibi), kapatınca ~8 sn'de durur; tandem/koaksiyel ana rotorlar karşıt. **Kuyruk rotoru artık döner.** |
| `aileron_l/r`, `elevon_l/r`, `elevator*`, `rudder`, `ruddervator_l/r`, `flap_l/r` | Kontrol yüzeyi: menteşe = ön kenar, eksen = açıklık (PCA). Sapma, SITL'in kendi kanal yorumuyla (SIM_Plane: kanal 1 aileron, 2 elevatör, 4 dümen; elevon ve V-kuyruk karışımı) ±25°; flap SERVOn_FUNCTION 2/3'ten. |
| `wheel_*`, `steer_*` | Teker yer hızıyla döner; direksiyon SERVO1'den ±28°. |
| `nav_*`, `beacon`, `strobe` | Seyir ışıkları: sol kırmızı, sağ yeşil, kuyruk beyaz, kırmızı çakar (ARM'da 1,2 sn'de bir). Modelde yoksa sınır kutusundan üretilir. Gündüz yalnız parlayan küre (yayılım + bloom); gece / alacakaranlıkta ayrıca gölgesiz küçük nokta ışık (yerel ışık sayısı sınırlı tutulur). |

Ses: katalog `Sound` (multi / prop / pusher / jet / heli / electric_heli / rover) ve motor sayısı prosedürel motor sesine ve Fab
uçak motor döngülerine bağlanır.

## Atmosfer (`FSmvAtmosphere`, `Source/Simumav/Private/SmvAtmosphere.cpp`)

Her uçuş sahnesinde (prosedürel, Fab haritası, şehir **ve Cesium gerçek dünya**) kurulur:

- **Ultra Dynamic Sky** (pakette varsa): `Time of Day` (0–2400) = gerçek güneşe göre yerel saat (gündüz: güneş öğlesi + 1 sa;
  alacakaranlık: gün batımı − 20 dk, altın saat; gece: gün batımı + 2,5 sa; NOAA hesabı `SmvCore::SolarDayEvents`), `Animate Time of Day`
  kapalı, `Simulate Real Sun / Moon / Stars`, `Latitude` / `Longitude` (parkurun), `Year` / `Month` / `Day` (bugün), `Time Zone`
  (yaklaşık; Türkiye UTC+3), `Daylight Savings Time` ve `Use System Time` kapalı, `Cloud Coverage`, `Fog`, `Dust Amount`,
  `Cloud Direction` = SITL rüzgârının gittiği yön (Unreal yaw), `Cloud Speed` irtifa rüzgârıyla orantılı, `Light Pollution Intensity`
  (gece 0,35), `Render Exponential Height Fog`, `Use Volumetric Fog` (Yüksek+), `Use Cloud Shadows`, `Enable Sun Lens Flare` (Epik+).
  Gök kipini (hacimsel / 2B bulut) UDS kendi `Sky Mode Scalability Map`'i ile ölçeklenebilirlik düzeyinden seçer; pozlamayı UDS yönetir
  (`Apply Exposure Settings`).
- **Ultra Dynamic Weather**: hava ön ayarı (Clear Skies / Cloudy / Rain / Foggy / Snow) + her değer "… - Manual Override" ile sabit:
  `Cloud Coverage`, `Fog`, `Rain`, `Snow`, `Thunder/Lightning` (yağmurda 2,5), `Dust`, `Wind Intensity` (SITL m/s ÷ 1,5),
  `Material Wetness` (yağmur 0,9), `Material Snow Coverage` (kar 0,85); `Wind Direction` = SITL rüzgârı.
- **Yerleşik gök** (UDS yoksa): güneş yönü gerçek konumdan, üstel yükseklik sisi + hacimsel sis (hava ve kaliteye göre), gerçek zamanlı
  gök ışığı; Cesium'da CesiumSunSky tarih/saat dilimi/yerel saat.
- **Post-process** (sınırsız hacim, her durumda): histogram otomatik pozlama (yalnız UDS yokken; EV100, gece +1,2 telafi), hafif renk düzeltme
  (doygunluk 1,06, kontrast 1,04), bloom 0,45, vinyet 0,22, AO 0,5; Lumen GI + yansıma (Düşük: ekran uzayı GI/yansıma).
- **Prosedürel zemin**: `M_SmvTerrainWx` — yağmurda koyulaşma + parlaklık + gürültü maskeli su birikintileri, karda yukarı bakan
  yüzeylerde kar örtüsü.
- **Grafik ön ayarı** (Düşük, Orta, Yüksek, Epik, Sinematik → 0..4; `USmvGameInstance::ApplyGraphicsQuality`): hacimsel sis ızgarası,
  hacimsel bulut ışın örnekleri, Lumen sahne/son toplama/yansıma kalitesi, UDS hacimsel sis / bulut gölgesi / mercek parlaması,
  hareket bulanıklığı (yalnız Sinematik).

Günlük: `Atmosfer: …`, `Atmosfer UDS ayarlanan: … | bulunamayan: …`, `Atmosfer UDW …`. UDS/UDW değişken adlarının tam listesi derleme
günlüğünde (`UDSPROP` satırları; editor Python `uds_dokum()` → `USmvAtmosphereLibrary::DumpUdsProperties`).

## Katalog API'si (menü / hangar arayüzü için)

```cpp
// SmvDataService.h
static const TArray<FSmvCatalogModel>& USmvDataService::Catalog();                       // tüm modeller (katalog sırası; tip grupları)
static const FSmvCatalogModel* USmvDataService::FindModel(const FString& Key);
static FString USmvDataService::DefaultModelFor(const FString& Type);                     // sunucuyla aynı varsayılanlar
static TArray<const FSmvCatalogModel*> USmvDataService::ModelsForType(const FString& Type, bool bOnlineOnly = false);
static const TArray<FSmvCatalogModel>& USmvDataService::TargetModels();                  // tgt:* (yalnız hedef/dekor)
static bool USmvDataService::LoadBundledGlb(const FString& File, TArray<uint8>& Out);     // gömülü GLB baytları
```

`FSmvCatalogModel` alanları: `Key, Label, Type, GlbPath, Airframe, Drive, Desc, bHasProp, Defaults` (eski) + `GlbFile, GlbYawDeg, FabDir,
bLocalOnly, bAddLiftRotors, Sound, SitlBinary, SitlModel, Physics, MassKg, SpanM, LengthM, CruiseKmh, MaxKmh, EnduranceMin, PayloadKg,
Motors, Battery`.

- Hesap kipinde (simumav.com) yalnız `bLocalOnly == false` modeller sunucuya kaydedilebilir / bulutta uçulabilir
  (`ModelsForType(Type, true)`); `smv:*` ve `fab:*` misafir/yerel SITL içindir.
- 3B önizleme: `ASmvDroneActor::ConfigureVisual(Type, Key, Color)` gömülü GLB'den eşzamanlı kurar (`IsUsingGlbBody()`); hangar sahnesi
  ek iş yapmadan yeni modelleri gösterir. `FetchModelGlb` önce gömülü dosyayı döndürür.
- İstatistik çubukları için `MaxKmh`, `EnduranceMin`, `MassKg`, `PayloadKg`, `Motors`; fizik etiketi için `Physics` / `SitlModel`.

## Yeni model eklemek

1. GLB'yi `Content/Simumav/ModelsSrc/` altına koyun (glTF Y yukarı, burun +X, metre; hareketli düğümleri yukarıdaki adlarla) ve
   `LICENSES.md`'ye kaynak/lisans satırı ekleyin. Prosedürel model için `Tools/models/arac_modelleri.py` (`MODELLER` sözlüğü).
2. Fizik: `SmvVehicles.cpp` `Build()` içine `FVehicleSpec` (çerçeve, JSON, parametreler, istatistikler).
3. Katalog: `SmvDataService.cpp` `Catalog()` içine `Add(TEXT("smv:<ad>"), …)` satırı.
4. Sınama: `python3 tests/cpp/test_glb.py`, `python3 tests/cpp/test_core.py`, `python3 -I tests/sitl/sitl_varyant.py smv:<ad>`.
