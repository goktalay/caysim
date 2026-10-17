# Windows'ta derleme ve paketleme

CAYSIM yalnız Windows 10/11 x64 hedefler. Bu ortamda Unreal Engine yoktur; derleme kullanıcının Windows makinesinde yapılır.

## 1. Gereksinimler

| Bileşen | Sürüm | Not |
|---|---|---|
| Unreal Engine | **5.4** (5.5 de uyumlu; `CAYSIM.uproject` › `EngineAssociation` değiştirilir) | Epic Games Launcher › Unreal Engine › Library › **+** |
| Visual Studio 2022 | 17.8+ | İş yükü: **Game development with C++**; bileşenler: MSVC v143, Windows 10/11 SDK, .NET Framework 4.8 SDK, **Unreal Engine installer** seçeneği |
| Cesium for Unreal | 2.x (UE 5.4 için) | Fab/Marketplace'ten motora kur **ya da** `Plugins/CesiumForUnreal` altına kaynaktan |
| glTFRuntime (isteğe bağlı) | güncel | HEPGO GLB modellerini çalışma zamanında yüklemek için; `Plugins/glTFRuntime` |
| Git + Git LFS | güncel | `git lfs install` (ikili varlıklar) |
| Disk | ~60 GB | motor + DerivedDataCache |

Donanım: DX12 destekli GPU (RTX 2060 / RX 6600 ve üstü önerilir; Lumen + Nanite), 16 GB RAM, SSD.

## 2. Depoyu alma

```powershell
git lfs install
git clone https://github.com/goktalay/caysim.git
cd caysim
copy Config\UserSecrets.ini.ornek Config\UserSecrets.ini   # Cesium ion belirteci buraya (gitignore'da)
```

## 3. Eklentiler

### Cesium for Unreal
- **Yol A (önerilen):** Epic Games Launcher › Fab › "Cesium for Unreal" › Install to Engine (5.4). Motor eklentisi olduğu için proje
  `Plugins/` dizini boş kalır; `CaySim.Build.cs` motor eklenti dizinini de tarar ve `CAYSIM_WITH_CESIUM=1` tanımlar.
- **Yol B:** https://github.com/CesiumGS/cesium-unreal sürüm paketini `Plugins/CesiumForUnreal/` altına açın.
- **Cesium ion belirteci:** https://ion.cesium.com › Access Tokens › yeni belirteç (varlıklar: Cesium World Terrain `1`, Bing Maps Aerial `2`).
  `Config/UserSecrets.ini` içine `[/Script/CaySim.CaySimWorldSetup] CesiumIonToken=…` yazın; ya da Editor'da `BP_WorldSetup › Cesium Ion Token`.
  Belirteç yoksa Google 3D Tiles (HEPGO `/g3d-key`) ya da düz zemin kullanılır.

### glTFRuntime (isteğe bağlı)
- Fab'den satın alıp motora kurun **ya da** https://github.com/rdeioris/glTFRuntime (MIT) kaynağını `Plugins/glTFRuntime/` altına koyun.
- `CaySim.Build.cs` `Plugins/glTFRuntime` varsa `CAYSIM_WITH_GLTFRUNTIME=1` tanımlar; motor eklentisi olarak kurduysanız Build.cs'teki
  dizin denetimini motor yoluyla genişletin (Cesium satırındaki gibi).
- Yoksa dron, `BP_Drone › Builtin Meshes` içindeki yer tutucu gövdelerle çizilir.

## 4. Proje dosyalarını üretme ve derleme

1. `CAYSIM.uproject` › sağ tık › **Generate Visual Studio project files** (ya da
   `"C:\Program Files\Epic Games\UE_5.4\Engine\Build\BatchFiles\Build.bat" -projectfiles -project="%CD%\CAYSIM.uproject" -game -engine`).
2. `CAYSIM.sln` › Visual Studio › yapılandırma **Development Editor** / **Win64** › **Build**.
   - İlk derleme 10–30 dk (shader'lar ayrıca Editor açılışında).
3. `CAYSIM.uproject` çift tık → Editor açılır.

Komut satırından:
```powershell
$UE="C:\Program Files\Epic Games\UE_5.4"
& "$UE\Engine\Build\BatchFiles\Build.bat" CAYSIMEditor Win64 Development -project="$PWD\CAYSIM.uproject" -waitmutex
```

## 5. Editor'da ilk kurulum (bir kez; varlıklar Git LFS'e commit edilir)

1. **Harita:** `Content/Maps/CaySimMain` boş seviye; ekle: `CesiumGeoreference`, `CesiumSunSky` (Cesium panelinden), `ExponentialHeightFog`,
   `VolumetricCloud`. World Settings › GameMode Override: `BP_GameMode` (ACaySimGameMode alt sınıfı).
2. **BP_GameMode:** `Drone Class = BP_Drone`, `World Setup Class = BP_WorldSetup`, `Launch Rig Class = BP_LaunchRig`, `Hud Class = WBP_Hud`,
   `Wizard Widget Class = WBP_Wizard`, `Default Pawn Class = ACaySimCameraPawn` (ya da BP alt sınıfı; Enhanced Input varlıkları burada atanır).
3. **BP_Drone:** `Builtin Meshes` (quad/hexa/heli/sabit kanat gövdeleri + pervane). Basit başlangıç: Editor'un Shapes/Cube-Cylinder'ları;
   CAYMAZ V3 GLB'si HEPGO'dan indirilip `Content/Drones/` altına içe aktarılabilir (glTF importer motorla gelir).
4. **BP_LaunchRig:** rampa tabanı/ray/taşıyıcı, pickup gövde/tekerlek, pist şeridi mesh'leri (1×1 m düzlem; `Setup` ölçekler).
5. **WBP_Wizard:** `UCaySimGameInstance::Get › GetWizard` üzerinden `OnStepChanged`, `OnError`, `OnStatus`, `OnLogoReady`, `OnDataChanged`
   olaylarına bağlanan ekranlar (`ECaySimWizardStep` başına bir Widget Switcher sayfası). Düğmeler `SubmitClientCode`, `ChooseMode`,
   `ChooseCenter`, `ChooseLog`, `ChooseType`, `ChooseModel`, `SetWorld`, `SetLaunch`, `Start`, `Back`'i çağırır.
6. **WBP_Hud:** `UCaySimHudWidget` alt sınıfı (çizim C++'ta; yalnız yazı tipi/kalınlık).
7. **Input:** `IMC_CaySim` + `IA_Look` (Mouse XY), `IA_Move` (WASD 2D), `IA_Ascend` (E/Q), `IA_Zoom` (tekerlek), `IA_CycleCamera` (C),
   `IA_ToggleHud` (H), `IA_RcRollPitch` / `IA_RcThrottleYaw` (gamepad sol/sağ çubuk).

## 6. Paketleme (.exe)

Editor › **Platforms › Windows › Package Project** (Shipping; `DefaultGame.ini` içinde `StagingDirectory=Paketler`). Komut satırı:
```powershell
& "$UE\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project="$PWD\CAYSIM.uproject" -platform=Win64 -clientconfig=Shipping `
  -cook -build -stage -pak -iostore -archive -archivedirectory="$PWD\Paketler" -noP4 -utf8output
```
Çıktı: `Paketler\Windows\CAYSIM.exe` (+ `CAYSIM\Content\Paks`). Dağıtımda **Cesium ion belirteci paket içine gömülür** (UserSecrets.ini
cook edilir) — kurumsal dağıtımda belirteci alan adı/uygulama kısıtlı oluşturun.

## 7. Sık sorunlar

| Belirti | Çözüm |
|---|---|
| `CesiumRuntime` modülü bulunamadı | Eklenti kurulu değil → Build.cs `CAYSIM_WITH_CESIUM=0` ile düz zemin; kurduktan sonra proje dosyalarını yeniden üret |
| `glTFRuntime.h` bulunamadı | `Plugins/glTFRuntime` yok ama `CAYSIM_WITH_GLTFRUNTIME=1` → dizin adını denetle |
| WebSocket bağlanmıyor, REST çalışıyor | Ters vekil (nginx) `Upgrade` başlıklarını iletmiyor; `?key=` sorgusunun düşürülmediğini denetle |
| Dron yerin altında/üstünde | `BP_WorldSetup › Geoid Offset M` (TR ≈ 30–40 m); `bAutoGroundOffset` açıkken 3D Tiles yüklenince kendiliğinden ölçülür |
| Dron ters yönde uçuyor | `BP_Drone › Eksen › bInvertRoll / bInvertPitch`; heading 90° → +X olmalı |
| 401 `auth` | İstemci kodu yanlış ya da HEPGO'da **Kodu yenile** yapıldı → yeni kodu girin |
