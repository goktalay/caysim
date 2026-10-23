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

**Fizik notu:** SITL düz zemin bilir; harita binaları/ağaçları görseldir (dron içlerinden geçebilir).

## Gök, hava, ses

**Ultra Dynamic Sky** (`Content\UltraDynamicSky`) varsa her düz dünyada (prosedürel, Fab haritaları, şehir) yerleşik
güneş/gök/sis/bulut yerine kullanılır; parkurun saat (gündüz/alacakaranlık/gece → *Time of Day* 1300/1845/2300) ve hava
(açık/bulutlu/yağmur/sis/kar → *Cloud Coverage*, *Fog* ve Ultra Dynamic Weather ön ayarı Clear_Skies / Cloudy / Rain /
Foggy / Snow; rastgele hava değişimi kapalı) ayarları UDS'ye bağlıdır. Cesium gerçek arazide Cesium güneş-gök sistemi kalır.
Kapatmak: `UseUltraDynamicSky=False`.

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
