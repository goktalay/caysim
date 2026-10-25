# Fab içerikleri nasıl eklenir

Fab'den satın alınan paketler (dron modeli, haritalar, ses, gök vb.) **lisanslıdır ve git deposuna konmaz**. Derleme
şirket bilgisayarındaki runner'da yapılırken `Tools/fab/fab-hazirla.ps1` bu içerikleri diskten projeye alır
(iş akışındaki "Fab içerikleri" adımı). Hiçbir içerik bulunamazsa derleme yine başarılı olur; oyun BasicShapes
gövdeleri ve prosedürel dünya ile çalışır.

## İçerik nereden alınır

| Kaynak | Ne yapılır |
|---|---|
| `D:\SimumavFab\Content\<paket>` (hazırlama projesi) | `Content\<paket>` altına kopyalanır (robocopy /MIR; yalnız değişenler) |
| `C:\ProgramData\Epic\EpicGamesLauncher\VaultCache\<paket>\data\Content` | aynı şekilde `Content\<paket>` altına kopyalanır |
| `VaultCache\FabLibrary\<ürün>\fbx\*.fbx` (ya da `obj`) | Editor Python (`Scripts/Python/fab_import.py`) ile içe aktarılır: quad dron → `/Game/Fab/Drone` (multirotor gövdesi), FPV/önleyici dron → `/Game/Fab/DroneFpv` (fpv ve interceptor tipleri), tank / hava savunma / anti-dron sistemi → `/Game/Fab/Props/<ad>` (statik **sahne dekoru**, yalnız görsel) |

Paket klasör adları korunur (`/Game/<paket>/...`): paketler birbirine bu yollarla başvurur, başka klasöre
taşımak başvuruları kırar. Content kökündeki harita dosyaları da alınır (KitBash3D Refineries: `Content\Main.umap` →
`/Game/Main`). Kopyalanan klasörler runner'da `.git/info/exclude` ile git dışında tutulur.

## Uçuş haritaları (kayıt tablosu)

Seçilebilir haritalar oyun kodundaki **kayıt tablosunda** durur: `Source/Simumav/Private/SmvMapRegistry.cpp`
(ad / açıklama dil anahtarı, önizleme görseli, harita yolu adayları, tür, başlangıç kaydırması, sahne dekoru, Fab paketi).
Menü bu tabloyu `FSmvFabContent::AllMaps()` (pakette olmayanlar `bAvailable = false`) ve `Maps()` (yalnız olanlar) ile
okur; uç sihirbazında **Parkur** listesinde "<ad> · Fab haritası" olarak görünür (yalnız yerel SITL ile; Bulut'ta uçulmaz).
CI yalnız bu tablodaki haritaları pakete pişirir — paketlerin demo/showcase haritaları ve kullanılmayan varlıkları
pakete girmez (derleme süresi ve paket boyutu bunun için sınırlı).

| Anahtar | Ad | Kaynak paket | Harita |
|---|---|---|---|
| `orman` | Orman | Brushify Forest Pack | `/Game/Brushify/Maps/Forest/Forest` |
| `yikik` | Yıkık İnşaat | Ruined Construction | `/Game/Ruined_Construction/Levels/Master_Level/P_Ruined_Construction` (+ `Sub_Levels`) |
| `sehir` | Şehir | European Buildings + City Sample Buildings | kodla kurulan blok: 5×5 ada, caddeler, ortada kalkış meydanı; iç halkada European Buildings ağları, dış halkada City Sample NY bina seviyeleri |
| `rafineri` | Rafineri | KitBash3D Refineries | `/Game/Main` (paketin kendi sahnesi) |
| `askeri` | Askeri Üs | Military Base Megapack (US_Military) | `/Game/US_Military/Levels/Showreel_Scene` (yoksa `Overview`) + `SubLevels`; Tank / Patriot / anti-dron sistemi modelleri kalkış alanının doğusunda **yalnız dekor** |

Önizleme görselleri `Content/Simumav/MapPreviews/<anahtar>.png` (kodla çizilmiş sade görseller; `Tools/fab/onizleme_uret.py`
yeniden üretir; pakette UFS). **Yeni harita eklemek:** tabloya bir satır (tek satır, `TEXT("...")` alanları — CI satırı
ayrıştırır), `Tools/i18n/metinler.py`'ye `fabmap.<ad>` ve `fabmap.<ad>.desc`, gerekirse `ceviriler.py`; ardından
`python3 Tools/i18n/dil_uret.py` ve `python3 Tools/fab/onizleme_uret.py` (yeni çizim işlevi).

**Kalkış noktası:** harita yüklenince sırayla `+MapStart=<anahtar>|X,Y` ayarı → haritanın `PlayerStart`'ı (üstü açıksa) →
merkeze en yakın düz ve üstü açık zemin (arazili haritada Landscape üstü, 20 m adımlarla 300 m'ye dek) → sınır kutusu
merkezi aranır; tablodaki `StartE/StartN` kaydırması eklenir. Bu nokta SITL ev konumuna (orijin), oradaki zemin Z=0'a
oturtulur. Haritanın kendi güneş/gök/sis aktörleri kaldırılır (gök Ultra Dynamic Sky'dan gelir).

**Fizik:** harita binaları, ağaçları ve arazisi gerçek çarpışmadır (Unreal fizik sorguları; `docs/fizik-carpisma.md`). Derlemede
basit çarpışması olmayan ağlara `UseComplexAsSimple` yazılır (`Scripts/Python/fab_carpisma.py`; günlükte paket başına "carpismasiz ag
sayisi"), oyunda her seviye eklenince kapalı bileşen çarpışmaları (ISM/HISM/Foliage) açılır. Kalkış noktası adayları fizikle
denetlenir (zemin, üstü açık, araç boyutunda boşluk). SITL yine düz zemin bilir: ev konumundan alçak arazide dron havada "inmiş" görünür.

## Gerçek dünya çevresi (Cesium, 2026-10-25)

Fab haritaları birkaç yüz m – 2 km'lik sergi sahneleridir; uçak/VTOL birkaç saniyede dışına çıkar. Bu yüzden her harita, kayıt
tablosundaki **gerçek konumuna** (`FSmvMapDef::Lat/Lon/GroundMsl`) demirlenir ve çevresi Cesium for Unreal ile gerçek dünyadır:

| Harita | Konum | Not |
|---|---|---|
| Orman | Karabük Yenice ormanları (Filyos vadisi) 41.200, 32.400 (157 m) | çevre sırtları ~+230 m (haritanın tepe halkasıyla uyumlu); Brushify arka plan dağları gizlenir |
| Askerî Üs | Konya Karapınar bozkırı 37.730, 33.550 (995 m) | düz step (SRTM ±3 m) |
| Şehir | Ankara Yapracık 39.858, 32.640 (1031 m) | kodla kurulan şehir bloğu |
| Yıkık İnşaat | Ankara Etimesgut–Sincan 39.965, 32.580 (795 m) | şehir kenarı sanayi |
| Rafineri | Kocaeli Körfez 40.765, 29.775 (39 m) | yalnız gerçek dünya (paketin sahnesi boş; İzmit rafinerisi 3B karolarda) |
| Yarış Pisti | İstanbul Tuzla, İstanbul Park 40.952, 29.405 (130 m) | |
| Köşk | Sapanca Gölü kuzeyi 40.744, 30.300 (70 m) | |

- Kaynak (Ayarlar → Görüntü → **Gerçek dünya**): `terrain` (varsayılan; Cesium World Terrain 1 + Bing uydu 2), `google3d`
  (Google Photorealistic 3D Tiles, ion varlığı 2275207 — **deneysel**: ağ sağlam (glb 120–180 ms) ama paketli oyunda karolar Cesium işçi
  kuyruğunda takılıyor, görünür karo bileşeni oluşmuyor; 25 sn'de bileşen yoksa arazi + uyduya düşer; fizik ağı kapalı, çarpışma gizli
  World Terrain'den), `off` (eski düz çevre diski). Belirteç: `secrets.CESIUM_ION_TOKEN` (CI pakete yazar).
- Arka planı kendi başına bir dünya olan harita (≥ 10 km; Brushify Forest'ın 28 × 36 km dağ halkası) bütün korunur; gerçek dünya onun
  dışında başlar (kenar kaydırması yok). Diğerlerinde oyun alanı dışına taşan tekil büyük ağlar (sahte ufuk) gizlenir; örnekli bitki ağları
  hiçbir zaman gizlenmez.
- Ağ: `[HTTP.Curl] MaxHostConnections=8` (assets.ion.cesium.com'a çok sayıda eşzamanlı TLS el sıkışması "SSL connect error 35" veriyordu).
- Harita alanı karolardan kırpılır (`UCesiumPolygonRasterOverlay`, çokgen = oyun alanı kutusu); kırpılan alanda karo çarpışması yok
  sayılır (`SmvPhysics::SetClipRegion`). Kenar boyunca 40 örnekte harita zemini (içte) ile karo yüzeyi (dışta) farkının ortancası kadar
  georeference düşeyde kaydırılır (±60 m sınırlı) (`ACaySimWorldSetup::ShiftVertical`; yer ofseti birlikte değişir → MSL konumlar sabit kalır).
- Yükleme kapısı karolar yüklenip kenar eşlenmeden uçuşa geçmez; 75 sn'de karo gelmezse harita kendi zeminiyle sürer; harita / zemin
  90 sn'de yoksa **uçuşa girilmez** ("Harita yüklenemedi" + Yeniden dene / Menüye dön).
- Atıf: `CesiumCreditSystemBP` ekran atıfları (Google logosu + veri sağlayıcıları) — Google koşulları gereği görünür kalmalı.
- `Build.cs`: Launcher/Fab kurulumunda eklenti klasörü karışık adlıdır (`Marketplace/Cesiumfo…`); `.uplugin` aranır
  (öncesinde `CAYSIM_WITH_CESIUM` hep 0'dı, Cesium kodu hiç derlenmiyordu).

## Gök, hava, ses

**Ultra Dynamic Sky** (`Content\UltraDynamicSky`) varsa her uçuş sahnesinde (prosedürel, Fab haritaları, şehir ve Cesium gerçek
dünya) yerleşik güneş/gök/sis/bulut yerine kullanılır; saat gerçek güneş konumundan (parkur enlem/boylamı, bugünün tarihi), hava ön ayarı
(Clear_Skies / Cloudy / Rain / Foggy / Snow; rastgele değişim kapalı), yağmur/kar/şimşek ve bulut/rüzgâr yönü SITL rüzgârıyla aynı —
ayrıntı: `docs/arac-katalogu.md` → Atmosfer (`FSmvAtmosphere`). Kapatmak: `UseUltraDynamicSky=False`.

**Aircraft Engine Sounds** (`cplomedia_AircraftEngineSounds`) varsa CI seçilen motor döngülerini `/Game/Fab/Sounds/`
altına kopyalar (`SC_Engine_Prop` uçak, `SC_Engine_Pusher` VTOL itici motoru, `SC_Engine_Jet` jet modeli; döngü açık).
Uçak/VTOL'de gaz → perde (0,6–1,5 × Doppler) ve ses düzeyi (rölantide %30, tam gazda %100) değişir; VTOL'ün dikey
rotorları ve tüm dronlar (multirotor, FPV, önleyici, heli, kara aracı) prosedürel motor sesinde kalır. Başka bir ses
seçmek: `EngineSoundProp=/Game/cplomedia_AircraftEngineSounds/Cues/Cue_engine07_loop_Cue` (bu durumda o ses klasörünün
pakete girmesi için `+DirectoriesToAlwaysCook` gerekir) ya da `Scripts/Python/fab_import.py` → `SES_SECIMI` değiştirilip
`Content\Fab\Sounds\.ses-v1` silinir. "Fab içerikleri" günlüğünde tüm cue adları listelenir.

**Dron modelleri:** Quadcopter_Drone → multirotor gövdesi (`/Game/Fab/Drone`, pervane kemikleri döner), FPV_Interceptor_Drone →
FPV ve önleyici gövdesi (`/Game/Fab/DroneFpv`). Askeri modeller (Tank, MIM-104 Patriot, yüksek güçlü mikrodalga anti-dron
sistemi) yalnız **statik sahne dekorudur**: atış, güdüm, hedefleme ya da başka davranış yoktur.

## Manzara (ağaç, kaya, ot, uzak dağ, gök trafiği, şehir sokakları)

Kod: `Source/Simumav/Private/SmvScenery.cpp` (`FSmvScenery`, `ASmvScenery`, `ASmvSkyTraffic`). Hepsi **yalnız görsel**dir; paket yoksa
eski temel şekil dünyası kalır. Pakete pişirilen klasörler dosyadaki `MANZARA-KLASORLERI` bloğundadır (CI okur).

| Paket | Nerede kullanılır |
|---|---|
| Brushify Forest Pack | prosedürel dünyada ağaçlar (huş / sedir / akçaağaç), çalılar, orman kayaları; ufuk dağları (`DistanceMeshes`, biyom malzemesi `MI_<ağ>_<Desert/Rocky/Snow>`); şehir meydanı ve şehir dışı park ağaçları |
| ia_scatter | kaya (`rock1`, `rock2`) ve ot / eğrelti / yabani ot ağları. Eklentinin dağıtım Blueprint'i (`BP_ia_scatter`, kontrol paneli) editör aracıdır; çalışma anında dağıtımı `ASmvScenery::Scatter` yapar (tohumlu, pist/su alanı boş, kesme mesafeli HISM) |
| Hyper Auto Landscape Material v4 | dokuları `M_SmvTerrainHyper` malzemesinde (Editor Python üretir): eğime göre kaya, kar çizgisi + hava karı, çöl kumu (`Sand`), ıslaklık. Hyper'ın kendi malzemesi Landscape katman düğümleri kullandığından ProceduralMesh arazisine doğrudan uygulanmaz |
| Interior Cubemap System | şehirde City Sample pencere malzemesi (`M_Window`) bu paketin ön ayarlarıyla değiştirilir (`CityWindowSwap`); gece aşırı parlayan pencereler yerine iç mekân görünümü |
| Planes in the Sky + Flying AI | gökte uzak trafik: yolcu uçakları (SM_Airliner / SM_Regional_Jet) 2,5–5 km'de iz bulutuyla, Flying AI alçakta (`SkyTraffic`, `SkyTrafficLengthM`) |
| Niagara (Fab paketlerinden) | Editor Python tüm Fab Niagara sistemleri içinden toz / zerre / kor türünü `NS_SmvDust` olarak kopyalar; ana menü kamerasının önünde süzülür (`MenuDust`) |

**Askeri Üs** haritası boşlukta bir ada olduğu için kayıt tablosunda çevre biyomu `Desert` tanımlıdır: uçuşta ve ana menüde adanın altından
12 km'lik kum arazisi (Hyper malzemesi, kum tepeleri), kaya/çalı ve çöl dağları kurulur (`ASmvScenery::BuildSurround`; zemin yüksekliği adanın
alt %10 yüzeyinden, `SurroundPercentile`, `SurroundSinkCm`). Yeni bir "havada ada" haritası için tablo satırının sonuna biyom adı eklenir.

**Şehir:** asfalt ve kaldırım taşı (`M_SmvStreet`), 12 cm bordürlü adalar, orta şerit ve yaya geçidi çizgileri, ada köşelerinde sokak lambaları
(alacakaranlık/gece yanar), kalkış meydanı ağaçları, şehir dışında park ve ufuk dağları.

Ayarlar `Config/DefaultGame.ini` → `[/Script/Simumav.SmvScenery]`: `Enabled`, `HyperTerrain`, `TreeCullM` (ağaç kesme mesafesi, m),
`GrassCount`, `SkyTraffic`, `SkyTrafficLengthM`, `CityWindowSwap`, `CityWindowEmissive`, `SurroundRadiusM`, `SurroundPercentile`.

## Şehir hayatı, yeni gövdeler, gemi (2026-10)

Kod: `Source/Simumav/Private/SmvCityLife.cpp` (trafik + kalabalık), `SmvFabContent::BlueprintBody` (Blueprint gövdeleri),
`SmvGameRulesActor::BuildDefense` (gemi). Pişirilen klasörler `SmvCityLife.cpp` → `PISIR-KLASORLERI` bloğu (CI okur).

| Paket | Nerede | Not |
|---|---|---|
| City Traffic Pro | Şehir parkuru: 6×6 cadde ızgarasında şerit trafiği (yön başına 2 şerit, öndeki araçla aralık, iki evreli kavşak ışığı, tekerlek kemikleri döner); Özel parkur › Şehir: park etmiş araçlar | yalnız `Cars/Meshes/Skeletal_Meshes` (12 araç) pişirilir; paketin ChaosVehicles trafik Blueprint'leri kullanılmaz. Ayarlar `[/Script/Simumav.SmvCityLife]`: `CityCars` (72), `CarSpeedKmh` (38), `CarCullM` (900) |
| OverCrowd (motor eklentisi, 5.8) | `Simumav.uproject`'te etkin (isteğe bağlı) | OverCrowd bir **VAT araç takımı**dır: karakter içermez; iskeletli ağ + animasyondan VAT ağı/dokusu editörde üretilir. Üretilen VAT ağları `/Game/Fab/Crowd` altına konunca kalabalık onlarla kurulur; yoksa geçici olarak paketteki UE5 Manny/Quinn (FPV Drone paketi) yürüme/bekleme animasyonlu iskeletli ağlar kullanılır. Ayarlar: `CityPeople` (120), `PeopleCullM` (450) |
| Military Hexacopter (Bendylab) | katalog `fab:hexa` — hexa X (SITL `hexax`), 2,4 m | Blueprint yapım betiğinden parçalar; pervane bileşenleri döner |
| Interceptor Drone FPV (Bendylab) | katalog `fab:bls_icept` (önleyici tipi) | yalnız oyuncu kontrollü araç gövdesi |
| Drone Mega Pack | `fab:mega_a` (quad), `fab:mega_d` (FPV quad), `fab:mega_b` / `fab:mega_c` (sabit kanat İHA) | A/D iskeletli (kemik ipucu `sphere`), B/C Blueprint parçaları |
| FPV Drone (AT_Drone) | `fab:atdrone` (FPV quad, rotor kemikleri döner) | paketin Manny/Quinn ağları ve animasyonları kalabalıkta da kullanılır |
| JMSDF Mogami fırkateyni | Gemi savunmada korunacak gemi (`/Game/Fab/Ships`; OBJ, parça parça içe aktarılır) | yalnız görsel; boy `ShipLengthM` (133), su çekimi `ShipDraftM` (5) — `[/Script/Simumav.SmvFabContent]` |
| Drone 196 UAV, DJI Agras T50, Armored Vehicle, Satellite Dish | **kullanılamadı** | yalnız `.blend` biçiminde indirilmiş; runner'da Blender yok. Fab'den FBX/GLB biçimi indirilirse otomatik içe aktarılır |
| GERAN-2 + pickup rampası | `/Game/Fab/Props` (statik dekor) | yalnız `launcher.fbx` (rampalı pikap) içe aktarılır; savunma hedefi olarak kullanılmaz |

## Fab paketleri — kullanım durumu

| Paket | Durum | Not |
|---|---|---|
| MCC – Meta Character Creator | kullanılıyor | MetaHuman pilot (docs/pilot-metahuman.md); yalnız seçilen yüz/gövde/groom'lar pakete girer |
| Brushify, ia_scatter, Hyper Auto Landscape, Interior Cubemap, Planes in the Sky, Flying AI | kullanılıyor | yukarıdaki Manzara tablosu |
| Niagara Examples Pack | kısmen | menüde sis kümeleri (`BP_FogBankCard`, `MenuFogBanks`); uygun ortam tozu yok — menü zerresi Ultra Dynamic Sky `Particles/Dust` (`NS_SmvDust`) |
| Speed effects | kullanılıyor | `M_SpeedEffects_SpeedLines` uçuş kamerasında hıza bağlı (`SpeedLines`, `SpeedLinesMax`); menüde varsayılan kapalı (`MenuSpeedLines`) |
| Aircraft HUD | kullanılıyor | uçak/VTOL FPV'de `WBP_Jet_HUD` ("Update HUD"); hedef kilidi öğeleri hep gizli (`JetHud`) |
| Hyper Generic Radial Menu v4 | kullanılmıyor | yetenek/özellik sistemi içeren büyük Blueprint çerçevesi, mevcut Slate radyal tekerlekten iyi değil; ayrıca içerik klasörü `Hyper` adıyla Hyper Auto Landscape ile çakışıyor (ilk kaynak geçerli) |
| Universal Flat GUI UI Kit, Slider X | kullanılmıyor | fantastik/oyunsu simge ve UMG bileşenleri (iksir, kılıç, büyü kartı); Slate arayüzün havacılık diliyle uyumsuz |
| Crimson Experience (motor eklentisi) | kullanılmıyor | Lyra tarzı GameFeatures "Experience" oyun modu çerçevesi; Simumav C++/Slate akışında karşılığı yok |
| WebNativeBrowser (motor eklentisi) | kullanılmıyor | CEF tabanlı UMG tarayıcı (2,3 GB, lisans denetimli); destek / hesap ekranları yerel Slate + simumav API ile; gerekirse motorun WebBrowser eklentisi |
| Main Menu & Settings, Effect Forge, Lux | kullanılmıyor | önceki turlardan; arayüz Slate, Effect Forge oyun içi buff sistemi, Lux editör aracı |

## Adım adım (şirket bilgisayarı)

1. **Hazırlama projesini bir kez oluşturun**
   Epic Games Launcher → Unreal Engine → Kitaplık → **5.8 Başlat** → Oyunlar → **Boş** şablon,
   *Starter Content olmadan*, konum `D:\`, ad **SimumavFab** → Oluştur. Editör açılınca kapatabilirsiniz.
   Sonuç: `D:\SimumavFab\SimumavFab.uproject` ve `D:\SimumavFab\Content`.
2. **Paketleri projeye ekleyin**
   Launcher → Unreal Engine → **Kitaplık** → **Fab Kitaplığı** → paketi bulun → **Projeye Ekle**
   (*Add to Project*) → listede **SimumavFab**'ı seçin (görünmüyorsa "Tüm projeleri göster" kutusunu işaretleyin;
   motor sürümü uyarısında 5.8'e en yakın sürümü seçin) → Projeye Ekle. İndirme bitene kadar bekleyin.
   Ör.: Ruined Construction, Brushify Forest Pack, City Sample Buildings, European Buildings, KitBash3D Refineries,
   Military Base Megapack, Ultra Dynamic Sky, Aircraft Engine Sounds.
3. **Motor eklentisi olan ürünler** (ör. Cesium for Unreal, Ultra Dynamic Sky eklenti sürümüyse) için
   "Projeye Ekle" yerine **Motora Yükle** (*Install to Engine*) → **5.8** seçin. Bu eklentiler
   `D:\Epic Games\UE_5.8\Engine\Plugins\Marketplace` altına gider; projede etkinleştirmek kod değişikliği ister
   (bize bildirin).
4. **Yalnız FBX/OBJ olarak gelen modeller** (ör. Quadcopter Drone) için Fab Kitaplığı'nda **İndir** yeterli;
   dosyalar `VaultCache\FabLibrary` altına iner ve derlemede otomatik içe aktarılır. Dokular ayrı bir indirme
   biçimindeyse (ör. "textures"/"gltf") onu da indirin.
5. **Okuma izni** — runner servisi `NT AUTHORITY\NETWORK SERVICE` hesabıyla çalışır. Envanterde klasör görünmüyor ya
   da "erişim reddedildi" uyarısı varsa (Yönetici PowerShell):
   ```powershell
   icacls D:\SimumavFab /grant "*S-1-5-20:(OI)(CI)RX" /T
   icacls C:\ProgramData\Epic\EpicGamesLauncher\VaultCache /grant "*S-1-5-20:(OI)(CI)RX" /T
   ```
   (`S-1-5-20` = NETWORK SERVICE; Türkçe Windows'ta ad farklı yazıldığı için SID kullanılır.)
6. **Denetim** — GitHub → Actions → **fab-envanter** → Run workflow. Artefakttaki `fab-envanter.txt`
   "Fab hazırlama projesi" bölümünde paket klasörlerini, boyutlarını, harita sayılarını ve izinleri;
   "VaultCache UE paketleri" bölümünde indirmenin bitip bitmediğini (`data\Content VAR/YOK`) gösterir.
7. **Derleme** — `main`'e itme ya da build-windows → Run workflow. "Fab içerikleri" adımının günlüğünde
   hangi paketlerin kopyalandığı, içe aktarılan modelin kemik adları, motor sesi seçimi, her harita için
   `harita <anahtar> : <yol>` (ya da "pakette yok") ve DefaultGame.ini'ye eklenen satırlar görünür.
8. **Oyunda deneme** — paket runner'da `D:\r\_work\caysim\caysim\Paketler\Shipping\Windows\Simumav.exe` (ya da
   Actions artefaktı). Uç → Simülasyon: **Yerel** → Parkur listesinde Orman / Yıkık İnşaat / Şehir / Rafineri / Askeri Üs
   (· Fab haritası) → saat/hava seç → kalkış. Uçak ya da VTOL seçip gaz vererek motor sesini, quad/FPV seçerek Fab
   gövdelerini deneyin. Günlük: `%LOCALAPPDATA%\Simumav\Saved\Logs\Simumav.log` → "Uçuş haritası", "Fab haritası
   yerleştirildi", "Ultra Dynamic Sky kuruldu", "Fab motor sesi", "Şehir bloğu kuruldu" satırları.

## Notlar

- Büyük paketler ilk derlemede pişirme süresini belirgin uzatır (gölgelendirici derlemesi; yeni harita eklenince ilk
  derleme 2–4 saat sürebilir); sonraki derlemeler CI'nin Zen DDC'sinden (port 8559) gelir, kopyalama artımlıdır (robocopy).
- Paketlerin tamamı projeye kopyalanır ama yalnız kayıt tablosundaki haritalar, `/Game/Fab`, Ultra Dynamic Sky ve şehir
  ağ klasörü pişirilir; geri kalanı (demo haritaları, ses kütüphanesinin seçilmeyen sesleri, menü paketleri) koddan
  başvurulmadıkça pakete girmez.
- City Sample Buildings yalnız modüler kit + LevelInstance binalarıdır (hazır şehir haritası yok; `All_Buildings_Lineup_*`
  yalnız sergileme dizisi) — bu yüzden şehir kodla kurulur ve paketten 7 NY "Ref" binası kullanılır.
- Dron gövdesi ayarları (`Config/DefaultGame.ini` → `[/Script/Simumav.SmvFabContent]`): `DroneSpanM` (gerçek
  boyut, m; verilmezse model 25–150 cm ise olduğu gibi kalır, değilse 0,6 m'ye ölçeklenir; görünürlük ×2,5 ayrıca uygulanır), `DroneYawDeg` (burun yönü düzeltmesi; verilmezse `_F_`/`_B_` pervane kemik adlarından, o da yoksa kamera kemiğinden).
- Hazırlama projesinin yeri farklıysa depo değişkeni `FAB_PROJE` (Settings → Variables) ile değiştirilebilir.
