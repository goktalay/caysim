# Fizik çarpışması ve engel sensörleri

Simumav'da duvar, tavan, bina, ağaç, arazi ve su temasları **Unreal'ın fizik/çarpışma sorgularıyla** bulunur; sınır kutusu,
yükseklik örneği ya da ad tabanlı tahmin kullanılmaz. Uçuş dinamiği ArduPilot SITL'de kalır (SITL konumu "istenen konum"dur),
Unreal geometriyi bilir: dronun gövdesi SITL'in verdiği konuma her karede fizikle taşınır, temas olursa sınıflanır ve sonucu SITL'e iletilir.

Kod: `Source/Simumav/Private/SmvPhysics.{h,cpp}` (Unreal tarafı), `Source/Simumav/Private/Core/SmvCollision.{h,cpp}` (saf mantık;
`tests/cpp/test_carpisma.py`), `SmvWorldActors.cpp` (`ASmvDroneActor::ResolveRenderLocation`), `SmvGameMode.cpp` (`TickPhysics`,
`HandleContact`, kalkış noktası), `SmvLocalFlightLink.cpp` (SITL tepkisi, sensör iletimi), `Scripts/Python/fab_carpisma.py` (derleme anı).

## Önceki tahmine dayalı kısımlar → yerine gelen

| Yer | Önce (tahmin) | Şimdi (fizik) |
|---|---|---|
| `ASmvGameMode::DetectCollision` | Önceki konumdan yeni konuma **tek ışın** (`ECC_Visibility`; gövde boyutu yok, hız > 1,5 m/s şartı, 3 sn'de bir); tür bileşen etiketinden, gerisi "terrain" (aday) | Gövde şekli **süpürmesi** (`SweepMultiByChannel`, kutu bileşimi, pervane diskleri dahil) + başlangıç çakışması MTD'si; temas normali + çarpma hızıyla sınıf (`SmvCore::ClassifyContact`) |
| `ACaySimDroneActor::ApplyPose` "yer ile kesişmeyi engelle" | Düz dünyada sabit merkez yüksekliği (`SampleGroundHeight` → `CenterGroundMsl`), Cesium'da 3 km yukarıdan aşağı ışın (çatıya/ağaç tepesine oturtma); gövde altı `GroundOffsetM` | Oyuncu dronunda `ResolveRenderLocation` (alt sınıf kancası): çözülmüş konum = fiziğin izin verdiği konum. Hayalet/rakip/hedef dronlar eski düzeltmeyi kullanır |
| `ASmvGameMode::PlaceFabLevels` kalkış noktası | Aşağı ışın + "ilk isabet Landscape mi" + 4 komşu ışın; PlayerStart için tek yukarı ışın | `SmvPhysics::EvaluateSpot`: aşağı ışın → zemin ve eğim, araç genişliğinde **küre yukarı süpürmesi** (tavan, saçak, dal; 60 m), araç boyutunda **kapsül çakışması** (boşluk), çevre düzlüğü; elenen aday sayısı ve son sebep günlükte |
| Arazi çarpışması (yerel) | `crash{kind:terrain}` yalnız **aday**; SITL "Hit ground" 3 sn içinde teyit etmezse yok sayılır (3B arazi ↔ SITL düz zemin farkında dron dağın içinden geçiyordu) | `crash{…, physics:true}` yerelde **kesin** (Unreal gerçek geometriye değdi). Bulutta sunucu kuralı değişmedi (arazi aday) |
| Fab haritaları | "Harita binaları/ağaçları görseldir (dron içlerinden geçebilir)" | Seviye eklenince kapalı çarpışmalar açılır (ISM/HISM/Foliage dahil); derleme anında basit çarpışmasız ağlara `UseComplexAsSimple` |

## Mimari

```
SITL (ArduPilot) ── MAVLink tele ──► TelemetryBuffer (enterpolasyon) ──► istenen konum + yönelim
                                                                          │
                     ASmvDroneActor::ResolveRenderLocation ◄──────────────┘
                     FSmvContactSolver::Resolve:
                       1) önceki ÇÖZÜLMÜŞ konumdan istenen konuma gövde kutuları süpürülür (sürekli çarpışma, tünelleme yok;
                          uzun yol ≤ 4 parçaya bölünür; temas yüzey boyunca kaydırır)
                       2) başlangıç çakışması → MTD ile dışarı (akışla beliren geometri / dönen gövde; olay üretmez)
                       3) temas normali (ENU'ya çevrilir) + SITL hızı → ClassifyContact → olay kuyruğu
                     ─► çizilen konum (kamera, HUD, sensörler bunu kullanır)
ASmvGameMode::TickPhysics:
   olaylar → HandleContact (günlük satırı; çarpışma → cmd crash{kind, speed, physics:true, class, vn, vt, normal_z, object};
             sürtünme → olay şeridi; yapı üstüne iniş → cmd land_rest)
   SensorHz'de SmvPhysics::Scan → SITL'e DISTANCE_SENSOR / OBSTACLE_DISTANCE (yalnız yerel), HUD yakınlık satırı
```

**Gövde şekli** — `ASmvDroneActor::RebuildPhysicsShape`: görünen bileşenlerin aktör uzayındaki sınırları (pervaneler, kızaklar,
kanatlar dahil). Çok rotor / heli / FPV: pervane disklerini kapsayan yassı kutu + gövde/kızak çekirdeği; uçak / VTOL / önleyici:
gövde kutusu (burundan kuyruğa) + kanat kutusu; kara aracı: tabanı %15 yukarıda tek kutu. Varsayılan ölçek **görünen gövde**
(görünürlük ×2,5 dahil — ekranda duvara değen gövde gerçekten değer); `ShapeScale=0.4` gerçek boyuta yaklaştırır.

**Sorgular** — kanal `Visibility` (görünen her şey; tetik hacimleri ve görünmez duvarlar engel sayılmaz), `bTraceComplex=true`
(üçgen ağı: pencere çerçevesi, dal), fiziksel malzeme ve yüz indeksi döner (günlükteki malzeme adı). Landscape yükseklik alanı,
Cesium 3D Tiles fizik ağları (`SetCreatePhysicsMeshes(true)`), prosedürel arazi/ağaç/bina örnekleri ve Fab ağları aynı yoldan.

## Temas sınıfları (SmvCore::ClassifyContact)

Normal yüzeyden araca, z yukarı; `Vn` yüzeye dik yaklaşma hızı, `Vt` yüzeye paralel hız (SITL hızı, m/s).

| Yüzey | Kural | Sınıf | Sonuç |
|---|---|---|---|
| Su (etiket/ad) | her temas | `water` | çarpışma |
| Zemin benzeri (`n·yukarı ≥ 0,7`) | `Vn < 3,0` ve `Vt ≤ 3` (dikey araç) / `≤ 60` (uçak koşusu) | `landing` | iniş / yerde durma |
| | aksi | `fall` | çarpışma (düşüş, savrulma) |
| | kara aracı | — | temas değil |
| Tavan (`n·yukarı ≤ −0,7`) | `Vn ≥ 1,5` | `ceiling_hit` | çarpışma (`kind: ceiling`) |
| | aksi | `ceiling_touch` | aşağı itilir, olay |
| Duvar / gövde / dal | `Vn < 2,0` ve `Vt ≤ 8` | `graze` | yüzeyden itilir, olay |
| | aksi (pervane çarpması dahil) | `hard` | çarpışma |
| Sürekli temas | sürtünme/tavan temasında SITL konumu yüzeyin ≥ 1 m içine girer ya da ≥ 3 sn sürer (≥ 0,3 m) | `hard` / `ceiling_hit` | çarpışma ("sürekli temas") |

Eşikler HEPGO ile tutarlı: `flightsim_mission.SITL_HIT_HIZ_MIN = 3,0` (altı iniş, üstü düşüş), `flightsim3d.js COLL_SPEED_MIN = 2`
(altındaki temas çarpışma değil). Kolsuz ve 3 m/s'den yavaş araç çarpışma kaydı üretmez (yerde itilen dron); kolsuz düşen araç üretir.
Yüzey türü (`kind`): bileşen etiketi (`water`, `building`, `tree`, `ground`), Landscape → `terrain`, Cesium karosu → yönüne göre
`terrain`/`building`, Foliage → `tree`, ağ/aktör adı (tree/trunk/branch…, build/wall/roof…, rock/cliff/road…), yoksa `obstacle`.

Her temas günlüğe yazılır (`%LOCALAPPDATA%\Simumav\Saved\Logs\Simumav.log`, "Fizik teması:"): sınıf, tür, aktör, bileşen, malzeme,
fiziksel malzeme, normal, `n·yukarı`, `Vn`, `Vt`, hız, batma, süre, nokta.

## SITL'e tepki ve uzlaştırma

SITL fiziği Unreal geometrisini bilmez (yerel SITL'in zemini ev irtifasında düz bir düzlemdir). Seçenekler değerlendirildi:

| Yöntem | Durum |
|---|---|
| **Sert çarpmada MAVLink ile zorla disarm** (`COMPONENT_ARM_DISARM` 21196) + çarpışma kaydı | **Uygulandı** — `crash{physics:true}` yerelde kesin; motorlar kesilir, araç SITL'de düşer. Görüntüde gövde temas noktasında durur, duvar boyunca kayarak zemine iner (süpürme), enkaz efekti çalışır |
| **Hafif temasta görüntüyü yüzeyden dışarı itme** (SITL konumu yüzeyin içinde kalabilir) | **Uygulandı** — çizilen konum fiziğin izin verdiği konum; SITL içeri itmeyi sürdürürse "sürekli temas" sert çarpmaya yükselir. Pilot / ArduPilot geri çekilince ikisi yeniden çakışır |
| **Gerçek geometriyle sensörler → ArduPilot'un kendi tepkisi** | **Uygulandı** — `RNGFND1` (aşağı, MAVLink) irtifa tutma/yüzey izleme/iniş çatıya, ağaca, araziye göre; `PRX1` (MAVLink, 360°) + `AVOID_ENABLE=3` LOITER/ALT_HOLD/GUIDED'da duvara yaklaşmayı kendisi durdurur/kaydırır (ACRO/STABILIZE'de kaçınma yoktur) |
| **Yapı üstüne iniş** | **Uygulandı** — dikey araç zeminde kollu ve SITL yüzeyin 0,5 m altına inmeyi `RestDisarmS` (1,5 sn) sürdürürse `land_rest`: zorla disarm (gerçek yüzeyde iniş algılayıcısının yapacağı), SITL'in kendi zeminine düşüşü ("Hit ground") çarpışma sayılmaz; kollama/kalkışta biter |
| SITL'i temas noktasına yeniden konumlandırma | **Yapılamaz** — ArduPilot SITL'de çalışan aracı ışınlayan bir MAVLink komutu yok (`SIM_OPOS_*` yalnız açılışta); `reset` yeniden başlatır |
| SITL `SIM_*` ile itme (rüzgâr, motor arızası) | Reddedildi — `SIM_WIND_*` tüm aracı etkiler, temas kuvveti gibi davranmaz; doğrulanamaz |
| **ArduPilot JSON fizik arka ucu** (`--model JSON`; dinamik Unreal'da, SITL yalnız otopilot) | Gelecek iş — gerçek temas kuvveti/zemin reaksiyonu için en doğru yol; çok rotor/uçak/VTOL/kara aracı dinamiğinin (itki, sürükleme, motor modeli) Unreal'da yazılması ve HEPGO sunucu parite testlerinin yenilenmesi gerekir |

**Sınırlar:** (1) SITL zemininden **alçak** arazi (vadi, ev konumundan alçak Fab arazisi): SITL aracı kendi zemininin altına inmez,
dron orada havada "inmiş" görünür. (2) Yapı üstüne inişten sonra kalkışta SITL kendi zemininden tırmanır; gövde çatıdan, SITL konumu
çatı yüksekliğini geçince ayrılır. (3) Bulut uçuşunda sensör iletimi yoktur (sunucu SITL'i) ve sunucu arazi temasını aday sayar.
(4) Çok hızlı dönüşlerde süpürme yalnız ötelemeyi kapsar (yönelim kare sonunda uygulanır; dönen gövde çakışırsa MTD ile çıkarılır).

## Engel sensörleri (fizikle)

`SmvPhysics::Scan`, `SensorHz` (15 Hz) — çizilen gövde merkezinden ışın izleri, karmaşık çarpışma:

| Sensör | Işın | MAVLink (yerel SITL'e) | SITL parametresi |
|---|---|---|---|
| Aşağı menzil bulucu | gövde ekseni boyunca aşağı, 40 m (ArduPilot eğim düzeltmesini kendisi yapar) | `DISTANCE_SENSOR` yön 25 (PITCH_270), id 0 | `RNGFND1_TYPE=10`, `RNGFND1_ORIENT=25`, `RNGFND1_MIN=0.2`, `RNGFND1_MAX=40`, `RNGFND1_GNDCLEAR=0.1` (uçak tiplerinde `RNGFND_LANDING=1`) |
| Yukarı | dünya yukarı, 30 m | `DISTANCE_SENSOR` yön 24 (PITCH_90), id 1 | `PRX1_TYPE=2` |
| 360° yakınlık | burun yönünün yatay izdüşümü, saat yönünde 72 × 5° (ya da 8 × 45°, sektör başına 5 ışın, en yakın) | `OBSTACLE_DISTANCE` gövde çerçevesi (`MAV_FRAME_BODY_FRD`), cm; engel yok = azami + 1, kullanılmayan = 65535 | `PRX1_TYPE=2`, `PRX1_ORIENT=0`, `AVOID_ENABLE=3`, `AVOID_MARGIN=2`, `AVOID_BEHAVE=0` |

Parametreler yeniden başlatma gerektirdiği için `varsayilan.parm`'a yazılır (`SmvCore::PhysicsSensorParams`; kullanıcının kayıtlı
parametresinde aynı ad varsa onunki kalır). Menzil bulucu: çok rotor, heli, uçak, VTOL, önleyici; yakınlık + kaçınma: çok rotor,
heli, kara aracı. Ad sürümü: ArduPilot 4.6+ (`RNGFND1_MIN/MAX` metre; 4.5 ve öncesi `_CM` adları kullanırdı — yerel SITL 4.7.2).

HUD: havadayken en yakın yatay engel `ProxWarnM` (5 m) altındaysa "ENGEL 3,2 m · SAĞ ÖN", tavan 3 m altındaysa "TAVAN 1,4 m"
(2 m / 1,5 m altı kırmızı). Yalnız göstergedir; kumandaya dokunmaz.

## Fab ağlarının çarpışması

- **Derleme anı** (`Tools/fab/fab-hazirla.ps1` e) → `Scripts/Python/fab_carpisma.py`): pişirilecek uçuş haritalarından (alt seviyeler,
  LevelInstance haritaları, World Partition harici aktör paketleri dahil) ve şehir ağ klasörlerinden ulaşılan statik ağlar taranır;
  basit çarpışması (kutu/küre/kapsül/dışbükey) olmayanlara `CollisionTraceFlag = UseComplexAsSimple` yazılır. Çim/çiçek/30 cm altı
  süsler atlanır. Günlük ("Fab içerikleri" adımı): `[fab_carpisma] <paket>: N ag - basit X, yalniz karmasik Y, carpismasiz ag sayisi Z
  (duzeltilen W), onceden duzeltilmis V, kucuk/cim K` ve TOPLAM. Düzeltilen dosyalar `robocopy /XO` ile ezilmez; haritalar/paketler
  değişmedikçe adım atlanır (`Content\Fab\.carpisma-imza`, son sonuç `Content\Fab\.carpisma-sonuc.json`).
- **Oyun anı** (`SmvPhysics::PrepareLevelCollision`, her seviye dünyaya eklenince — Fab haritası, alt seviyeler, LevelInstance,
  World Partition hücreleri, şehir bina seviyeleri): görünür statik ağ bileşenlerinin (ISM/HISM/Foliage dahil) sorgu çarpışması
  kapalıysa `QueryOnly` açılır, `Visibility` kanalı `Block` yapılır; Landscape de denetlenir. Günlük: "Fizik seviye hazırlığı — <seviye>:
  N ağ bileşeni (M örnekli), L Landscape; çarpışması açılan …, benzersiz ağ … — basit çarpışmalı …, yalnız karmaşık …, çarpışmasız ağ
  sayısı … (ör. …)".

Paket notları (doğrulama runner günlüğünden yapılır): **Brushify** ağaç/kaya — Foliage örnekleri genelde `NoCollision` ya da yalnız
gövde kapsülü; oyun anında açılır, gövde dışındaki dallar karmaşık çarpışmayla. **KitBash3D** — çoğu ağda basit çarpışma yok → derleme
anında `UseComplexAsSimple`. **City Sample** — LevelInstance binaları; seviye eklenince hazırlanır. **US_Military**, **Ruined Construction**
— alt seviyeleriyle birlikte. Cesium 3D Tiles: `SetCreatePhysicsMeshes(true)` (CesiumWorldSetup), karolar akışla gelir; beliren karo
gövdeyle çakışırsa olay üretmeden dışarı itilir.

## Kullanıcı bulgusu "binanın içinden geçtim" (2026-10-26) — kök nedenler ve düzeltmeler

Kullanıcının kendi günlükleri (`C:\claude\Simumav-dev\Simumav\Saved\Logs`, `gunluk-al` iş akışı) ve bilerek çarpma duman uçuşları:

1. **Işınlama hatası** (`FSmvContactSolver::Resolve`): "büyük sıçrama" denetimi istenen (SITL) konumu **çözülmüş** konumla
   karşılaştırıyordu. Duvara dayanan aracın SITL konumu binanın içinden / arkasından uzaklaşmayı sürdürür; fark 50 m'yi geçince çözücü
   aracı istenen konuma ışınlıyordu — araç duvarın öbür yüzüne, Orman'da arazinin **altına** geçiyordu (kullanıcı günlüğünde
   `ceiling_hit … Landscape … normal=(…, -1.00)`: gövde arazinin altından tavana değiyor). Artık ışınlama yalnız SITL konumunun
   **kendi** sıçramasında (tek karede ≥ 200 m: yeniden başlatma / ışınlama).
2. **Şehir binaları sorguya kapalı**: kodla kurulan blok şehirde (Şehir parkuru) 10 m irtifada 72 yönde yatay ışın **hiçbir şeye
   değmiyordu**. `AStaticMeshActor` bileşeni ağın varsayılan çarpışma profilini kullanır (`bUseDefaultCollision`), Fab paketlerinin
   bina ağlarında bu çoğu kez `NoCollision`. `SmvPhysics::PrepareActorsCollision`: profil `BlockAll`, sorgu açık, `Visibility` Block;
   **pişmiş çarpışma verisi olmayan** (basit şekil yok, üçgen ağ yok) ≥ 2 m ağlara sınır kutusu çarpışması (`EnsureMeshCollision`;
   seviyelerde de). "Yalnız karmaşık" sayımı artık pişmiş üçgen ağ gerçekten varsa.
   Ayrıca City Sample kule ağları (`UseComplexAsSimple`, pişmiş üçgen ağ var) **karmaşık** sorguda hiç isabet vermiyor, basit sorguda
   veriyor: gövde süpürmesi, MTD, engel sensörleri ve çarpma hedefi artık karmaşık + basit **ikili** sorgu (`SmvPhysics::LineBoth` /
   `SweepBoth`, en yakın isabet). Kalkış noktası araması tek kip kaldı (ikili sorgu ağaç taçlarının basit şekline takılıyordu).
3. **Google 3B karolarında fizik ağı kapalıydı** (önceki tur): Google binaları çarpışmıyordu. `bGooglePhysics=True` (varsayılan).
   Ayrıca paketli oyunda glTF dönüştürücüleri kayıtsız olduğundan Google karoları hiç oluşmuyordu (CaySim modül başlangıcında kaydedilir).
   Doğrulama: Manhattan'da Google binasına `hard` çarpma → disarm → gövde 0,4 m üstte. Türkiye'de (Kızılay, Taksim, Levent) Google
   karoları 2,5B — alçak irtifada çarpılacak bina ağı yok.
4. **Kaçınma**: çok rotorda `AVOID_ENABLE=3` LOITER / ALT_HOLD / GUIDED'da duvardan 2 m önce durdurur — "çarpamıyorum" algısı
   buradan gelebilir; ACRO / STABILIZE'de kaçınma yoktur. Duman testindeki çarpma koşusu kaçınmayı kapatır.

Duman testi (`-SmvSmokeOnly=11,12,13,14`): f11 şehir binası, f12 Fab binası (Askerî Üs), f13 Google 3B (Kızılay), f14 yamaç
(Orman). Akış: 10 m askı → 72 yönde yatay ışınla en yakın hedef (bina: dik yüzey; yamaç: arazi) → `CONDITION_YAW` ile burun hedefe →
`AVOID_ENABLE 0`, ALT_HOLD, RC ileri yunuslama → temas / çarpışma / disarm → +3 sn ve +7 sn "altındaki yüzeye … m".
Görüntüler `_ram0` (koşu öncesi), `_ram1` (çarpışma anı; Development'ta gövde kutuları kırmızı), `_ram2`, `_ram3` (yüzeyde duruş).
Günlük: `SMOKE ram …`, `SMOKE ram temas: …`, `SMOKE ram ÇARPIŞMA …`, `SMOKE ram son …`; hedef bulunamazsa `SMOKE ram tanı:` satırları
en yakın ağların çarpışma durumunu (sorgu, Visibility yanıtı, profil, basit / üçgen, fizik durumu, doğrudan ışın) yazar.

## Ayarlar ve hata ayıklama

`Config/DefaultGame.ini` → `[/Script/Simumav.SmvPhysics]`: `Enabled`, `ShapeScale` (1 görünen gövde), `SkinCm`, `TraceComplex`,
`Channel` (Visibility | WorldDynamic | Camera | WorldStatic), `Sectors` (72 | 8), `ProxMaxM`, `RngMaxM`, `SensorHz`, `SitlSensors`,
`SitlAvoid`, `PenCrashM`, `SustainCrashS`, `RestDisarmS`, `ProxWarnM`, `Debug` (0 | 1 | 2).

Çizim: konsol (Development paketi, `~`) `smv.Fizik.Ciz 1` gövde kutuları (yeşil serbest, sarı temas, kırmızı çarpma sınıfı) + temas
noktası ve normali; `smv.Fizik.Ciz 2` + sensör ışınları (turuncu isabet, mavi boş); `-1` ayara döner. Shipping pakette çizim derlenmez.

## Sınama

`python3 tests/cpp/test_carpisma.py` (sınıflandırma tablosu + 3000 rastgele durum bağımsız Python referansıyla, sürekli temas,
sektör eşleme, mesafe kodlaması, SITL parametreleri), `python3 tests/cpp/test_mavlink.py` (DISTANCE_SENSOR / OBSTACLE_DISTANCE
pymavlink ile çözülür), `python3 tests/cpp/test_core.py` (çekirdek; SmvCollision.cpp derlemeye dahil).
