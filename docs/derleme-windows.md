# Şirket bilgisayarı kurulumu, Windows'ta derleme ve paketleme

Simumav (Steam) ve CAYSIM (HEPGO kurumsal) aynı Unreal projesidir: `Simumav.uproject` (UE **5.8**). Geliştirme sunucusunda Unreal/GPU yoktur;
derleme, varlık üretimi ve paketleme **şirket bilgisayarında** (Windows, güçlü NVIDIA) yapılır. Bu makine GitHub'a **self-hosted runner** olarak
bağlanır ve `.github/workflows/build-windows.yml` her `main` itmesinde paketi üretir.

> Sürüm seçimi: UE 5.8 (5.x serisinin son kararlı sürümü; 5.8.3 düzeltmesi yayımlandı). Cesium for Unreal **2.30** UE 5.6 / 5.7 / 5.8'i destekler.
> Başka sürüme geçiş: `Simumav.uproject` › `EngineAssociation`, CI `vars.UE_ROOT`, Cesium eklentisinin o sürüm paketi. Hedef dosyaları
> `BuildSettingsVersion.Latest` / `EngineIncludeOrderVersion.Latest` kullanır (sürüm numarası yazmaz).

## 1. Adım adım kurulum (bir kez)

### 1.1 Windows ve sürücü
1. **Windows 11** (23H2+) x64; Windows Update tamamlanmış. Güç planı: *Yüksek performans* (derleme sırasında uyku kapalı).
2. **NVIDIA sürücüsü**: en güncel *Studio Driver* (ya da Game Ready). `nvidia-smi` ile doğrula. DX12 + donanım ışın izleme (Lumen HWRT) için RTX 20xx+.
3. Disk: motor ~60 GB + proje/DDC 50–100 GB → **NVMe SSD**, en az 250 GB boş.
4. **Uzun yol desteği** (UE yolları uzun) — yönetici PowerShell:
   ```powershell
   New-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem" -Name LongPathsEnabled -Value 1 -PropertyType DWORD -Force
   git config --system core.longpaths true
   ```

### 1.2 Epic Games Launcher + Unreal Engine 5.8
1. https://store.epicgames.com/download → Epic Games Launcher → Epic hesabıyla giriş.
2. *Unreal Engine › Library › Engine Versions › +* → **5.8** → *Options*: Starter Content kapalı, *Target Platforms*: **Windows** (Linux isteğe
   bağlı), *Editor symbols for debugging* (isteğe bağlı, ~60 GB). Varsayılan yol: `C:\Program Files\Epic Games\UE_5.8`.
3. Kurulumdan sonra Editor'u bir kez açıp kapat.

### 1.3 Visual Studio 2022
1. https://visualstudio.microsoft.com → **Visual Studio 2022** (17.10+).
2. İş yükü **Game development with C++**; bileşenler: MSVC v143 x64/x86 (en güncel), **Windows 11 SDK** (10.0.22621+), *Unreal Engine installer*,
   **.NET Framework 4.8.1 SDK** ve **.NET 8 Runtime** (UnrealBuildTool / AutomationTool).

### 1.4 Git + Git LFS + PowerShell 7
```powershell
winget install --id Git.Git -e
winget install --id GitHub.GitLFS -e
winget install --id Microsoft.PowerShell -e
git lfs install --system
```

### 1.5 Cesium for Unreal (gerçek arazi)
1. Epic Games Launcher › **Fab** › "Cesium for Unreal" (ücretsiz) › *Add to My Library* › **Install to Engine 5.8**
   (alternatif: https://github.com/CesiumGS/cesium-unreal/releases → UE 5.8 paketini `Plugins/CesiumForUnreal/` altına).
2. https://ion.cesium.com → *Access Tokens* → yeni belirteç (Cesium World Terrain `1`, Bing Maps Aerial `2`). Pakete gömülecek belirteci **yalnız bu
   uygulamaya kısıtlı** oluşturun → CI sırrı `CESIUM_ION_TOKEN`. Oyuncu kendi belirtecini Ayarlar'dan da girebilir. Belirteç yoksa oyun
   **prosedürel dünyaya** düşer.
3. Eklenti yoksa proje yine derlenir (`.uproject`'te *Optional*; `CaySim.Build.cs` → `CAYSIM_WITH_CESIUM=0`).

### 1.6 glTFRuntime (isteğe bağlı — gerçekçi dron GLB'leri)
https://github.com/rdeioris/glTFRuntime (MIT) → `Plugins/glTFRuntime/`. Varsa SIMUMAV GLB modelleri (simumav.com statiklerinden indirilip
`Saved/Simumav/ModelCache`'e alınır) çalışma anında yüklenir; yoksa prosedürel gövde çizilir.

### 1.7 Depo
```powershell
mkdir C:\src; cd C:\src
git clone https://github.com/goktalay/caysim.git
cd caysim
powershell -NoProfile -ExecutionPolicy Bypass -File Tools/sitl/sitl-indir.ps1        # yerel ArduPilot SITL 4.7.2 (sha256 doğrulamalı) → ThirdParty/ArduPilotSITL/Win64
```

### 1.8 Paylaşımlı DDC (Derived Data Cache)
```powershell
New-Item -ItemType Directory D:\UE-DDC -Force
[Environment]::SetEnvironmentVariable("UE-SharedDataCachePath", "D:\UE-DDC", "Machine")
```
Runner hizmet hesabı da bu klasöre yazabilmeli. Ağdaki başka makinelerle paylaşım: `\\sunucu\UE-DDC`.

## 2. GitHub self-hosted runner

### 2.1 Kayıt
1. GitHub › `goktalay/caysim` › *Settings › Actions › Runners › New self-hosted runner* › **Windows x64**:
   ```powershell
   mkdir C:\r; cd C:\r          # KISA YOL: iş dizini C:\r\_work\caysim\caysim (UE/MSBuild uzun yol sınırı)
   Invoke-WebRequest -Uri <sayfadaki actions-runner-win-x64-*.zip> -OutFile runner.zip
   Expand-Archive runner.zip -DestinationPath .
   .\config.cmd --url https://github.com/goktalay/caysim --token <SAYFADAKİ_TOKEN> --labels self-hosted,windows,unreal --name sirket-rtx --work _work --runasservice
   ```
2. `--runasservice` → **Windows hizmeti** (oturum kapalıyken de çalışır). Hizmet hesabı: yönetici olmayan ayrı hesap (ör. `ue-runner`); UE dizinine
   okuma, `C:\r` ve `D:\UE-DDC`'ye yazma izni.
3. Etiketler: **self-hosted, windows, unreal**.

### 2.2 Güvenlik
- Runner **yalnız bu özel depoya** kaydedilir (kuruluş düzeyinde değil).
- *Settings › Actions › General*: **Require approval for all outside collaborators**; fork PR iş akışları kapalı. İş akışı `pull_request`'te
  çalışmaz ve `github.repository_owner == 'goktalay'` ister.
- Runner makinesinde sır dosyası tutulmaz; Steam/Cesium sırları yalnız GitHub *Secrets*'ta.
- İnternet çıkışı: github.com, objects.githubusercontent.com, firmware.ardupilot.org, steamcdn-a.akamaihd.net + Steam partner uçları (yükleme).

### 2.3 Değişkenler ve sırlar (*Settings › Secrets and variables › Actions*)
| Ad | Tür | Not |
|---|---|---|
| `UE_ROOT` | Variable | `C:\Program Files\Epic Games\UE_5.8` |
| `STEAM_APP_ID` | Variable | gerçek App ID (yoksa 480 = Spacewar) |
| `STEAM_DEPOT_ID` | Variable | Windows depot |
| `STEAM_USERNAME` | Secret | yalnız yükleme yetkili hesap |
| `STEAM_CONFIG_VDF` | Secret | base64 `config.vdf` (docs/steam-yayin.md §6) |
| `CESIUM_ION_TOKEN` | Secret | isteğe bağlı |
| `BLENDER_EXE` | Variable | isteğe bağlı: `blender.exe` tam yolu (yalnız `.blend` inen Fab modelleri için; yoksa kayıt defteri + `C:\Program Files\Blender Foundation` aranır) |
| `RUNNER_LABEL` | Variable | push derlemelerinin runner etiketi (ör. `unreal5080`) |

## 3. Yerel derleme (geliştirici)
```powershell
$UE = "C:\Program Files\Epic Games\UE_5.8"
& "$UE\Engine\Build\BatchFiles\Build.bat" -projectfiles -project="$PWD\Simumav.uproject" -game -engine
& "$UE\Engine\Build\BatchFiles\Build.bat" SimumavEditor Win64 Development -Project="$PWD\Simumav.uproject" -WaitMutex
# içerik (harita + malzemeler) KODDAN — bir kez ya da betik değişince:
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\Simumav.uproject" -ExecutePythonScript="$PWD\Scripts\Python\simumav_varliklar.py" -unattended -nop4 -nosplash -NullRHI -stdout
```
Ardından `Simumav.uproject` → Editor → *Play (Standalone Game)*. Steam istemcisi açıksa 480 (Spacewar) kimliğiyle başarımlar denenebilir.

## 4. Paketleme
```powershell
& "$UE\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project="$PWD\Simumav.uproject" -noP4 -platform=Win64 -clientconfig=Shipping `
  -build -cook -stage -pak -iostore -prereqs -archive -archivedirectory="$PWD\Paketler\Shipping" -utf8output -unattended -nodebuginfo
```
Çıktı: `Paketler\Shipping\Windows\Simumav.exe` + `Simumav\` (pak'ler, `ThirdParty\ArduPilotSITL\Win64` NonUFS, `Content\Simumav\Lang`).

**CI (build-windows) çıktısı:** paketler runner diskinde kalır — `D:\r\_work\caysim\caysim\Paketler\Shipping\Windows\Simumav.exe`
(ve `...\Development\Windows`); her derleme üstüne yazar. GitHub artefaktı yalnız **Shipping** yüklenir (Fab haritalarıyla
paket birkaç GB; yükleme ağ hatasında derlemeyi düşürmez — "Paket konumu ve boyutu" adımı yolu ve boyutu yazar).
Development artefaktı gerekirse: Actions → build-windows → Run workflow → *Development paketini de artefakt olarak yükle*.

## 5. CAYSIM (HEPGO kurumsal) ile ilişki
`Source/CaySim` (HEPGO sözleşmesi v1 istemcisi, Cesium dünya kurulumu, dron aktörü, askerî HUD) Simumav'ın ortak çekirdeğidir. Kurumsal istemci:
`Simumav.exe /Game/Maps/SimumavMain?game=/Script/CaySim.CaySimGameMode` (CAYSIM sihirbaz arayüzü UMG varlıkları ister — docs/mimari.md).

## 6. Sık sorunlar
| Belirti | Çözüm |
|---|---|
| `Unable to find plugin 'CesiumForUnreal'` | Eklenti *Optional*; motor eklenti dizini bulunamıyorsa `Plugins/CesiumForUnreal` altına koyun |
| `C4456/C4458` gölgeleme hatası | Yerel değişken adı üye/üst kapsamla çakışıyor → yeniden adlandırın (UE bunu hata sayar) |
| `MakeLines` aşırı yük hatası | `TArray<FVector2D>` → `TArray<FVector2f>` (CAYSIM `HudWidget.cpp`'deki eski kullanımlar) |
| "SITL dosyaları yok" | `powershell -NoProfile -ExecutionPolicy Bypass -File Tools/sitl/sitl-indir.ps1`; pakette `Simumav\ThirdParty\ArduPilotSITL\Win64\ArduCopter.exe` olmalı |
| SITL başlıyor ama bağlanmıyor | Güvenlik duvarı 127.0.0.1:15670–17660 TCP; `Saved\Simumav\SitlRun\<port>\sitl.log` |
| CI'da "SHA256 UYUŞMAZ" | firmware.ardupilot.org klasörü güncellenmiş → `Tools/sitl/sitl-manifest.json`'u bilinçli güncelleyin ya da ayna (`-BaseUrl`) |
| Harita üretilemedi (CI) | PythonScriptPlugin + EditorScriptingUtilities açık mı; `Saved\Logs` |
| Mission Planner veri görmüyor | Ayarlar → Mission Planner UDP çıkışı; MP'de UDP 14550; uçuş başladıktan sonra bağlanın |
| Gerçek arazi yerine prosedürel | Cesium belirteci yok/yanlış (Ayarlar → Cesium ion belirteci) |
| Pişirme saatlerce sessiz (günlük bir doku satırında duruyor) | Bellek tükenmesi: 9cada62'de US_Military `Showreel_Scene_BuiltData`'nın 541 lightmap dokusu aynı anda inşa edildi (tahmini ~95 GB, boş sanal bellek < 1 GB) ve pişirme 3 saat asılı kaldı. Önlem 1: `fab-hazirla.ps1` haritaların `*_BuiltData` (ışık haritası) paketlerini kopyalamaz — proje `r.AllowStaticLighting=False` (Lumen + UDS), çalışma anında okunmazlar. Önlem 2: paketleme adımları `Tools/ci/bekci.ps1` ile çalışır: 30 dk (`SMV_BEKCI_DK`) yeni satır yoksa bellek / süreç dökümü, süreç ağacı sonlandırılır ve doku derleme eşzamanlılığı 4'e sınırlanarak BİR KEZ yeniden denenir. Sayfa dosyasını sistem yönetimli (≥ 32 GB) tutun |
| Blender kurulu ama "Blender: yok" | Runner NETWORK SERVICE: Microsoft Store / kullanıcı profili kurulumunu göremez. blender.org MSI'ı "tüm kullanıcılar" için `C:\Program Files\Blender Foundation` altına kurun ya da `BLENDER_EXE` değişkenini verin |
