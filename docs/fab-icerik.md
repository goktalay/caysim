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
| `VaultCache\FabLibrary\<ürün>\fbx\*.fbx` (ya da `obj`) | Editor Python (`Scripts/Python/fab_import.py`) ile `/Game/Fab/<ad>` altına içe aktarılır; dron ürünleri `/Game/Fab/Drone` |

Paket klasör adları korunur (`/Game/<paket>/...`): paketler birbirine bu yollarla başvurur, başka klasöre
taşımak başvuruları kırar. Kopyalanan klasörler runner'da `.git/info/exclude` ile git dışında tutulur.
Her paketten bir gösterim haritası (adı Demo/Showcase/Overview/Map içeren, yoksa en büyüğü) pakete pişirilir ve
uç sihirbazında **Parkur** listesinde "<ad> · Fab haritası" olarak görünür (yalnız yerel SITL ile; harita merkezi
SITL ev konumuna oturtulur). Ruined Construction "Yıkık İnşaat" adıyla listelenir.

## Adım adım (şirket bilgisayarı)

1. **Hazırlama projesini bir kez oluşturun**
   Epic Games Launcher → Unreal Engine → Kitaplık → **5.8 Başlat** → Oyunlar → **Boş** şablon,
   *Starter Content olmadan*, konum `D:\`, ad **SimumavFab** → Oluştur. Editör açılınca kapatabilirsiniz.
   Sonuç: `D:\SimumavFab\SimumavFab.uproject` ve `D:\SimumavFab\Content`.
2. **Paketleri projeye ekleyin**
   Launcher → Unreal Engine → **Kitaplık** → **Fab Kitaplığı** → paketi bulun → **Projeye Ekle**
   (*Add to Project*) → listede **SimumavFab**'ı seçin (görünmüyorsa "Tüm projeleri göster" kutusunu işaretleyin;
   motor sürümü uyarısında 5.8'e en yakın sürümü seçin) → Projeye Ekle. İndirme bitene kadar bekleyin.
   Ör.: Ruined Construction, Brushify Forest Pack, Game Ready City Pack, KitBash3D Refineries, Drone Sounds,
   Aircraft Engine SFX.
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
   hangi paketlerin kopyalandığı, içe aktarılan modelin kemik adları ve DefaultGame.ini'ye eklenen satırlar görünür.

## Notlar

- Büyük paketler ilk derlemede pişirme süresini belirgin uzatır (gölgelendirici derlemesi); sonraki derlemelerde
  yalnız değişenler kopyalanır.
- Haritası olmayan paketler (ses, malzeme kütüphanesi) kopyalanır ama koddan başvurulmadıkça pakete girmez.
- Dron gövdesi ayarları (`Config/DefaultGame.ini` → `[/Script/Simumav.SmvFabContent]`): `DroneSpanM` (gerçek
  boyut, varsayılan 0,6 m; görünürlük ×2,5 ayrıca uygulanır), `DroneYawDeg` (burun yönü düzeltmesi).
- Hazırlama projesinin yeri farklıysa depo değişkeni `FAB_PROJE` (Settings → Variables) ile değiştirilebilir.
