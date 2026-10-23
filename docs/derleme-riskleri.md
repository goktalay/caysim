# İlk derlemede bakılacaklar (UE 5.8 + VS 2022)

Kod geliştirme sunucusunda **hiç derlenmedi** (Unreal yok). Derleme öncesi satır satır inceleme yapıldı. Kesin hatalar düzeltildi.
Aşağıdakiler **emin olunamayan** noktalar. Her biri için konum, olası hata ve alternatif verildi.
Derlemeden önce `python3 tests/ue_denetim.py` çalıştırın; 0 hata vermeli.

Bu betik sezgiseldir, bir derleyicinin yerini tutmaz. Şunları denetler:
- `.generated.h` son include mi
- Build.cs modülleri
- gölgeleme (C4456/C4457/C4458)
- `[=]` lambda
- unity ad çakışması
- kaldırılmış API'ler
- eklenti ↔ .uproject tutarlılığı

## Genel
| Konu | Olası hata | Alternatif |
|---|---|---|
| Uyarı → hata politikası | UE, C4456/C4457/C4458'i (gölgeleme) hata sayar. C4244/C4267 (daraltma) normalde uyarıdır. | Daraltma uyarısı hata olursa açık `(float)` / `(int32)` dönüşümü ekleyin. |
| Printf/UE_LOG biçim denetimi (5.4+) | `%f`'ye float/bool, `%lld`'ye `long long` verilmesi (`SimumavTypes.cpp:57`, `SmvGameMode.cpp:697,913`) | `(double)` ya da `(int64)` dönüşümü ekleyin. |
| Unity derleme | Windows başlıklarının (`SimumavSecrets.cpp`) aynı unity dosyasındaki diğer .cpp'lere `min`/`max`/`LoadImage` gibi makrolar sızdırması | UE `NOMINMAX` tanımlar. Sorun çıkarsa saf çekirdekte `(std::min)(a,b)` yazın ya da dosyayı unity dışına alın (Build.cs `bUseUnity=false` yalnız teşhis için). |
| OnlineSubsystem (OSSv1) | 5.8'de "deprecated" uyarısı çıkabilir. | `PRAGMA_DISABLE_DEPRECATION_WARNINGS` ya da Online Services (OSSv2) geçişi. |

## Simumav — Windows Kimlik Bilgisi Yöneticisi
| Konum | Olası hata | Alternatif |
|---|---|---|
| `Source/Simumav/Private/SimumavSecrets.cpp:15-18` | `Windows/AllowWindowsPlatformTypes.h` → `Windows/WindowsHWrapper.h` → `<wincred.h>` → `Windows/HideWindowsPlatformTypes.h` deseni. `DWORD`/`LPWSTR`/`CREDENTIALW` kullanımı Allow/Hide bloğunun **dışında** kalıyor. `HideWindowsPlatformTypes` sonrası bu tipler tanımsız olursa C2065 çıkar. | Kullanım gövdelerini Allow/Hide arasına alın ya da `::DWORD` yerine `uint32`, `LPWSTR` yerine `(wchar_t*)` kullanın. `wincred.h` için `#include "Windows/MinWindows.h"` alternatifi var. Bağlama: `Advapi32.lib` (Build.cs'te var). |

## Simumav — Steam (OSS yolu; doğrudan steam_api yok)
| Konum | Olası hata | Alternatif |
|---|---|---|
| `SmvSteam.cpp` | `FOnlineAchievementsWrite::SetFloatStat(FName, float)` 5.8'de bulunmayabilir. | `WriteObject->Properties.Add(FName(Id), FVariantData(100.f))` |
| Build.cs / .uproject | Steamworks ThirdParty modülü **gerekmez**: yalnız OSS arayüzleri kullanılıyor. OnlineSubsystemSteam, `DynamicallyLoadedModuleNames` ve uproject Plugins üzerinden yükleniyor. | Doğrudan `SteamUserStats()` gerekirse `AddEngineThirdPartyPrivateStaticDependencies(Target, "Steamworks")` ekleyin. |
| `Config/DefaultEngine.ini` | `SteamDevAppId=480` geliştirme yer tutucusu. Başarımlar `[OnlineSubsystemSteam] Achievement_N_Id` altında. | Gerçek App ID gelince değiştirin. |

## Simumav — Enhanced Input (kodla kurulan IA/IMC)
| Konum | Olası hata | Alternatif |
|---|---|---|
| `SmvPilotPawn.cpp:78` `Imc->MapKey` | 5.6+'da eşlemeler varsayılan/geçersiz kılma olarak ayrıldı. MapKey kullanımdan kalktı uyarısı verebilir. | Uyarıyı bastırın ya da `UEnhancedInputLibrary::RequestRebuildControlMappingsForContext` ile yeni API'ye geçin. |
| `SmvPilotPawn.cpp:118` `FKey("GenericUSBController_AxisN")` | Derlenir. RawInput eklentisi kapalıysa çalışma anında geçersiz anahtar uyarısı çıkar. | Eklenti `.uproject`'te açık (yalnız Win64). |
| `CaySimCameraPawn.cpp` (eski `BindAction(FName, IE_Pressed…)`) | Derlenir. Giriş bileşeni EnhancedInputComponent olduğu için ini ActionMappings çalışmayabilir (çalışma anı sorunu). | CAYSIM piyonunu da kodla IA/IMC'ye geçirin. |

## Slate (UE 5.2+ FVector2f geçişi)
| Konum | Olası hata | Alternatif |
|---|---|---|
| `SmvHudOverlay.cpp:85,90,205`, `HudWidget.cpp` `ToPaintGeometry(Size, FSlateLayoutTransform(FVector2f))` | `FDeprecateVector2DParameter` aşırı yük belirsizliği. | Boyutu da `FVector2f(...)` ile geçin ya da `FSlateLayoutTransform(1.f, FVector2f(...))` kullanın. |
| `SmvHudOverlay.cpp:74` `PipBrush.ImageSize = FVector2D(...)` | Atama kabul edilmeyebilir. | `FVector2f((float)X, (float)Y)` |
| `HudWidget.cpp` `FontMeasure->Measure(...)` → `FVector2D` | 5.8'de dönüş tipi `FVector2f` olabilir. | `const FVector2D Sz(Measure->Measure(S, F));` |
| `SSmvShell.cpp:191-192` `SComboBox::OptionsSource(Store.Get())` | 5.4+ `SLATE_ITEMS_SOURCE_ARGUMENT`. | `const TArray<TSharedPtr<FString>>*` açık dönüşümü. |

## CAYSIM — eklentiye bağlı (yalnız eklenti varsa derlenir)
| Konum | Olası hata | Alternatif |
|---|---|---|
| `CesiumWorldSetup.cpp` | Doğrulanamayanlar: `UCesiumRasterOverlay::MaterialLayerKey`, `Refresh()`, `IonAccessToken` (ileride `CesiumIonServer`'a taşınabilir). | Cesium 2.x sürüm notlarına bakın. Gerekirse `CAYSIM_WITH_CESIUM=0` ile geçici olarak kapatın. |
| `DroneActor.cpp LoadGlb` (`CAYSIM_WITH_GLTFRUNTIME=1`) | `FglTFRuntimeConfig::TransformBaseType`, `glTFLoadAssetFromData(Bytes, Cfg)` eklenti sürümüne bağlı. | glTFRuntime örneklerine bakın. Eklenti yoksa bu kod derlenmez. |
| `CesiumWorldSetup.h` | Cesium aktör referansları UPROPERTY değil, `TWeakObjectPtr`. UHT `#if CAYSIM_WITH_CESIUM` içinde UPROPERTY'ye izin vermez. | Davranış aynı. Aktörler seviyeye ait. |

## CAYSIM — diğer
| Konum | Olası hata | Alternatif |
|---|---|---|
| `HepgoClient.cpp CloseWs` `Ws->OnConnected().Clear()` | IWebSocket olayları `DECLARE_EVENT`; `Clear` erişilemez olabilir. | `RemoveAll(this)` |
| `HepgoClient.cpp:213` `HasTypedField<EJson::Array>` | Şablon sürümü kaldırılmış olabilir. | `HasTypedField(TEXT("detail"), EJson::Array)` |
| `CaySimSaveGame.cpp` | `FPlatformMisc::GetLoginId()` kullanımdan kalkmış olabilir (uyarı). | `FPlatformMisc::GetDeviceId` / `FGenericPlatformMisc::GetLoginId` |
| `ReplayPlayer.cpp` | zlib doğrudan kullanılıyor (`AddEngineThirdPartyPrivateStaticDependencies(Target, "zlib")`). | Açılmış boyut biliniyorsa `FCompression::UncompressMemory(NAME_Gzip, ...)` |

## Diğer (düşük risk)
- **`SmvLocalFlightLink.h`:** UCLASS içinde yansıtılmayan iç `struct FTimed` var. UHT sorun çıkarırsa sınıf dışına taşıyın.
- **`SmvSteam.h` / `SimumavTypes.h`:** UHT başlıklarında yansıtılmayan `namespace` blokları var. UE5 UHT bunları atlar.
- **`SmvPilotPawn.h` `ESmvCamType::None`:** UENUM değeri `None` (EnhancedInput'ta da örneği var). UHT uyarı verirse `NoCam` yapın.
- **`SmvDataService.cpp:14-31`:** TMap initializer_list ile kuruluyor. Derlenmezse `Add` dizisine çevirin.
- **Çalışma anı:** `SmvGameMode.cpp:92` `RemoveAllViewportWidgets()` CAYSIM HUD'unu da siler.
