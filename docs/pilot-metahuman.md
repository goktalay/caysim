# Pilot görünümü → MetaHuman (MCC – Meta Character Creator) entegrasyonu

Durum: **MCC entegre** (Fab paketi `MetaCharacterCreator`, UE 5.7 projesi; VaultCache'ten derleme anında `Content/MetaCharacterCreator`).
Kit yoksa (eski paket, `[/Script/Simumav.SmvPilot] MetaHuman=False`) pilot eskisi gibi temel şekillerden (`ASmvPilotFigure` temel yolu) kurulur.

## Uygulama (bu sürüm)

| Parça | Yer | Ne yapar |
| --- | --- | --- |
| Kit üretimi | `Scripts/Python/simumav_varliklar.py` → `pilot_kiti()` | MCC'den seçilenleri `/Game/Simumav/Pilot/DA_SmvPilotKit` (`USmvPilotKit`) içine **yumuşak başvuru** olarak yazar: 2 gövde (`SKM_Human_Male/Female`), 12 gerçekçi yüz (`MCC_YUZLER`: ad, kadın mı, yaklaşık ten kademesi), 21 groom (`MCC_GROOMLAR`) ve yüz başına groom bağlamaları (`GB_<groom>_<yüz>`). Yalnız bunlar pakete pişirilir; MCC'nin geri kalanı (~15 GB; anime / kara elf / goblin yüzleri, mayo / kulak / kuyruk aksesuarları, sahneler) pakete girmez |
| Seçim | `Source/Simumav/Private/SmvMetaPilot.cpp` (`FSmvMetaPilot::Resolve`) | cinsiyet → gövde; ten `s1–s8` → en yakın ten kademesindeki yüzler, aralarından yüz/burun/göz/ağız seçimine göre; saç modeli → MCC saç groom'u (yalnız kasksızken), kaş kalınlığı → `Eyebrows_M_Thin/Natural/Thick`, sakal → `Beard_S_Stubble` / `Mustache_S_Full` / `Goatee_M_Pointed` / `Beard_S_Full`, kirpik |
| Figür | `SmvPilotFigure.cpp` → `BuildMetaHuman` | `UPoseableMeshComponent` gövde (tek parça uçuş tulumu rengi, `Suit`), lider pozlu yüz (`SetLeaderPoseComponent`), `UGroomComponent` tüyler (saç rengi `hairMelanin` / `hairRedness`); kask, vizör, şapka, kulaklık, gözlük, mikrofon temel şekil parçaları baş kemiğini izler (`HeadFwdCm`, `HeadUpCm`) |
| Poz | `PoseMhArms` | kemikler bileşen uzayında **çocuk kemiğe yönlendirilir** (kemik eksenlerinden bağımsız): RC tutma (dirsek gövdeye yakın, önkol öne-içe, eller kumandada) ya da serbest duruş. Baş ve boyun (%40) drona bakar (`SetPose`) |
| Küçük resimler | `SmvMenuScene.cpp` → `TickSnaps` | `bLowDetail` → temel şekil (64 küçük resim için yüz ağı yüklenmez) |

Eklentiler (`Simumav.uproject`): `HairStrands`, `RigLogic` (isteğe bağlı). `DefaultEngine.ini`: `r.GPUSkin.Support16BitBoneIndex`,
`r.GPUSkin.UnlimitedBoneInfluences`, `SkeletalMesh.UseExperimentalChunking` (MetaHuman yüz ağları için). MCC'nin kendi karakter Blueprint'i
(`BP_ThirdPersonCharacter` + `BP_CP_CustomizationComponent`) kullanılmaz: girdi eşlemesi / kamera / oyun modu getirir; parçalar doğrudan kurulur.
Ten kademeleri yüz adına göre yaklaşıktır (`MCC_YUZLER`), gerekirse oradan düzeltilir.

## İlk plan (referans)

## Bugünkü akış (bağlantı noktaları)

| Yer | Dosya | Ne yapar |
| --- | --- | --- |
| Veri modeli | `Source/Simumav/Public/SimumavTypes.h` → `FSmvAvatar` | simumav.com ile aynı JSON (`ToJson` / `FromJson`); sunucu sözleşmesi değişmez |
| Figür kurulumu | `Source/Simumav/Private/SmvPilotFigure.cpp` → `ASmvPilotFigure::Build(Avatar, bWithRc)` | tek giriş noktası; tüm görünüm buradan kurulur |
| Mürettebat önizlemesi | `SmvMenuScene.cpp` → `SetPilotPreview`, `Tick` (PilotPrev) | stüdyo yakalaması (duvar + parlak zemin + 3 ışık), tam boy / yüz yakınlaştırma |
| Avatar küçük resimleri | `SmvMenuScene.cpp` → `AvatarSnap`, `TickSnaps` | TOP 10, ana menü pilot kartı, salon |
| Uçuşta pilot | `SmvGameMode.cpp` (`PilotFigure`, RC tutan figür) → `ASmvPilotFigure::Build(Req.Pilot.Avatar, true)` + `SetPose` | başparmaklar çubukları izler, baş aracı izler |

Kural: MCC gelince **yalnız `ASmvPilotFigure`'ün içi değişir**; çağıranlar (`Build`, `SetPose`, `HeadHeightCm`) aynı kalır.
Eklenti yoksa (ör. eski paket) mevcut temel şekil figürü yedek olarak kalır — `CAYSIM_WITH_*` kalıbındaki gibi koşullu derleme
(`SIMUMAV_WITH_MCC`) ve çalışma anında varlık denetimi.

## Önerilen yapı

1. `Simumav.Build.cs`: eklenti varsa (`Plugins/<MCC>/*.uplugin` ya da motor eklentisi) modül bağımlılığı + `SIMUMAV_WITH_MCC=1`.
2. `ASmvPilotFigure` içinde iki kurulum yolu:
   * `BuildMetaHuman(const FSmvAvatar&)` — MetaHuman gövde/yüz bileşenleri (SkeletalMesh + Groom), MCC'nin çalışma anı API'si ile parametre yazımı;
   * `BuildPrimitive(const FSmvAvatar&)` — bugünkü kod (yedek).
3. Kask / vizör / pilot tulumu / göğüs takımı: MetaHuman giysisi olarak ayrı statik ya da iskeletli ağlar (soket: `head`, `spine_05`).
   Fab'den gelecek pilot ekipman paketi yoksa bugünkü temel şekil kask parçaları başa (`head` soketine) iliştirilebilir.
4. Uçuş pozu: RC tutan duruş için basit bir Control Rig / Anim BP (`bWithRc`), başparmak kemikleri `SetPose` girdisiyle (−1..1).
5. Performans: önizleme ve uçuşta LOD 1–2; küçük resimlerde (`AvatarSnap`) LOD 3 + groom kapalı.

## Alan eşlemesi: `FSmvAvatar` → MetaHuman

| FSmvAvatar | Değerler | MetaHuman / MCC karşılığı |
| --- | --- | --- |
| `Body` | `m` / `f` | gövde ön ayarı (erkek / kadın temel gövde) |
| `Build` | `slim` / `avg` / `broad` | gövde tipi (boy sabit ~178 cm; ağırlık: ince / orta / iri) |
| `Skin` | `s1`–`s8` | ten tonu kaydırıcısı (8 kademe; `PfSkin` renk tablosu referans) |
| `Face` | `oval` / `round` / `square` / `long` / `heart` | yüz şekli ön ayarı (çene genişliği, yanak, alın oranları) |
| `Eyes` | `almond` / `round` / `narrow` / `wide` | göz şekli ön ayarı |
| `EyeColor` | `#rrggbb` | iris rengi |
| `Brows`, `BrowW` | `straight/arched/angled/soft`, `thin/medium/thick` | kaş şekli + kalınlık (groom) |
| `Nose` | `small` / `straight` / `wide` / `pointed` | burun ön ayarı |
| `Mouth` | `neutral` / `smile` / `grin` / `serious` | ifade (yüz animasyonu pozu; önizlemede sabit) |
| `Beard` | `none/stubble/mustache/goatee/full` (yalnız `m`) | sakal groom'u |
| `Hair`, `HairColor` | 11 model, `#rrggbb` | saç groom'u + renk (melanin / kırmızılık yaklaşık dönüşümü) |
| `Glasses` | `none` / `aviator` / `sun` | aksesuar ağı (`head` soketi) |
| `bFreckles`, `bMole` | bool | cilt ayrıntı katmanı |
| `Headgear` | `helmet` / `none` / `cap` / `headset` | başlık ağı; `helmet` iken saç groom'u gizlenir |
| `Helmet`, `Visor`, `bMirror`, `bVisorOpen` | renk, renk, bool, bool | kask malzemesi; vizör: ayna = metalik 0,85 / pürüzlülük 0,05; açık vizör = vizör kemiği/ağı yukarı |
| `Suit`, `Outfit` | renk, `suit` / `jacket` / `casual` | giysi seti + ana renk |
| `Patch`, `PatchColor` | arma tipi, renk | göğüs + sol omuz arma çıkartması (decal ya da küçük ağ) |
| `Accessory` | `none/mic/crest/antenna/stripe` | kask aksesuar ağları |

Sözleşme notu: `FSmvAvatar` simumav.com ile ortaktır (docs/simumav-api-esleme.md). MetaHuman'a özgü yeni alan gerekirse
(ör. yaş, ince yüz kaydırıcıları) sunucu tarafı onaylanana dek **yalnız yerel** (`Saved/Simumav`) ek alan olarak tutulmalı; sunucuya giden
`ToJson` değişmemeli.

## Geçici iyileştirmeler (MCC öncesi, bu sürüm)

* Oranlar: bot (gövde + burun + taban), baldır / diz koruyucu / uyluk + yan cep, kalça, kemer + toka, karın, omuzlara genişleyen göğüs,
  hafif A-duruşu kollar (dirsek, manşet, eldiven), çene / yanak hacmi ve kulaklar.
* Tulum ayrıntıları: göğüs takımı kayışları + toka, fermuar, yaka, göğüs ve sol omuz arması (hafif ışıma).
* Kask: parlak kabuk, kenar bandı, kulaklık çanakları, vizör rayları, NVG yuvası, çene kayışı; vizör aynalıysa metalik ve pürüzsüz.
* Stüdyo: lacivert duvar (hafif ışıma) + duvar halesi ışığı, parlak yansıtan zemin; anahtar / kenar ışığı şiddeti uzaklığın karesiyle ölçeklenir
  (tam boy figürde ışıklar 5–6 m uzakta olduğu için önizleme kararıyordu).
