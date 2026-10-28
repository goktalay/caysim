// Simumav veri modeli — simumav.com API JSON şekilleriyle birebir (docs/simumav-api-esleme.md). Aynı yapılar misafir kipte
// yerel profilde (Saved/Simumav/<profil>/*.json) aynı JSON biçimiyle saklanır → giriş sonrası hesaba aktarma doğrudan POST'tur.
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "SimumavTypes.generated.h"

/** Uygulama kipi: misafir (varsayılan; her şey yerel) ya da simumav.com hesabı (bulut senkron + çok oyunculu + TOP 10). */
UENUM(BlueprintType)
enum class ESmvAccountMode : uint8
{
	Guest   UMETA(DisplayName = "Misafir"),
	Online  UMETA(DisplayName = "simumav.com hesabı")
};

/** Uçuşun fiziği nerede çalışıyor. */
UENUM(BlueprintType)
enum class ESmvFlightBackend : uint8
{
	/** Oyunla dağıtılan ArduPilot SITL bu bilgisayarda (misafir kipte tek seçenek) */
	LocalSitl  UMETA(DisplayName = "Bu bilgisayarda (yerel SITL)"),
	/** simumav.com sunucusundaki SITL ("Bulutta uç"; giriş gerekir) */
	Cloud      UMETA(DisplayName = "Bulutta (simumav.com)")
};

USTRUCT(BlueprintType)
struct SIMUMAV_API FSmvUser
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") int64 Id = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") FString Email;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") FString DisplayName;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") FString Lang;
	static FSmvUser FromJson(const TSharedPtr<FJsonObject>& O);
};

/** Dron güç ayarı (`power`): azami hız (uçak), yakıt, batarya. Negatif = verilmedi/yok. */
USTRUCT(BlueprintType)
struct SIMUMAV_API FSmvPowerSettings
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double MaxSpeedMs = -1;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double FuelL = -1;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double FuelKmPerL = -1;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") int32 ElecCells = -1;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double ElecMah = -1;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double ElecW = -1;
	TSharedRef<FJsonObject> ToJson() const;
	static FSmvPowerSettings FromJson(const TSharedPtr<FJsonObject>& O);
};

/** Çıkartma / logo (sunucu `livery.decals[]` {id, slot, scale, rot}); yerelde PNG dosyası profil klasöründe (decals/<ad>.png). */
USTRUCT(BlueprintType)
struct SIMUMAV_API FSmvDecal
{
	GENERATED_BODY()
	/** Sunucu çıkartma kimliği (0 = yalnız yerel) */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") int64 Id = 0;
	/** Yerel PNG (tam yol); sunucuda yüklüyse boş olabilir */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString File;
	/** top | bottom | left | right | front | rear | tail */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Slot = TEXT("top");
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double Scale = 1.0;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double Rot = 0.0;
	/** logo | sticker */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Kind = TEXT("sticker");
};

/** Hangar donanımı (sunucu `drones.equipment`, app/simumav/equipment.py): kamera türü, eklentiler, görünüm, azami irtifa. */
USTRUCT(BlueprintType)
struct SIMUMAV_API FSmvEquipment
{
	GENERATED_BODY()
	/** day | night | thermal | multispectral */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Camera = TEXT("day");
	// ---- eklentiler (görünür 3B parça + SITL SPRAY_* / GRIP_* / CHUTE_*) ----
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") bool bSprayer = false;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double SprayTankL = 10.0;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double SprayWidthM = 4.0;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double SprayPumpRate = 10.0;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") bool bGripper = false;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double PayloadKg = 1.0;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") bool bParachute = false;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double ChuteAltMinM = 10.0;
	// ---- görünüm ----
	/** solid | stripe | split | camo | checker */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Pattern = TEXT("solid");
	/** ikinci renk (#rrggbb; boş = yok) */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Color2;
	/** gövde çağrı adı (1–8, A–Z 0–9 -) */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Callsign;
	/** gloss | matte | metal | carbon (yalnız istemci; malzeme pürüzlülük/metal) */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Finish = TEXT("gloss");
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") TArray<FSmvDecal> Decals;
	/** azami irtifa (m; ≤ 0 yok) → FENCE_ALT_MAX */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double MaxAltM = -1.0;
	/** Eklentinin aracın tipine takılabilirliği (equipment.ADDONS types) */
	static bool AddonFits(const FString& Addon, const FString& Type);
	/** Eklentilerin SITL parametreleri (equipment.sitl_params; kanal 9–12) */
	TMap<FString, double> SitlParams(const FString& Type) const;
	TSharedRef<FJsonObject> ToJson(bool bForServer) const;
	static FSmvEquipment FromJson(const TSharedPtr<FJsonObject>& O);
};

/** Hangar dronu (`drone`). Id: sunucuda tam sayı (metin), yerelde "l-<guid>". */
USTRUCT(BlueprintType)
struct SIMUMAV_API FSmvDrone
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Id;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Name;
	/** multirotor | fpv | plane | interceptor | heli | vtol | rover */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Type = TEXT("multirotor");
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString ModelKey = TEXT("glb:iris");
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Color = TEXT("#e53935");
	/** classic | elevon | vtail (sabit kanat) */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Airframe = TEXT("classic");
	/** ackermann | skid (rover) */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Drive;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") bool bIsDefault = false;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") TMap<FString, double> Params;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FSmvPowerSettings Power;
	/** Donanım: kamera, eklentiler, görünüm, azami irtifa (sunucu `equipment`) */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FSmvEquipment Equipment;
	/** ArduPilot Lua betiği (SCR_ENABLE; yerel SITL'de scripts/simumav.lua) — kalıcı */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Script;
	TSharedRef<FJsonObject> ToJson(bool bForCreate) const;
	static FSmvDrone FromJson(const TSharedPtr<FJsonObject>& O);
};

/** 3B avatar (`avatar` v2 — §28 karakter yaratma; sunucu app/simumav/avatar.py doğrular, ≤ 1500 bayt). v1 alanları + beden/yüz/saç/kıyafet/başlık. */
USTRUCT(BlueprintType)
struct SIMUMAV_API FSmvAvatar
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Helmet = TEXT("#1e88e5");
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Visor = TEXT("#263238");
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") bool bMirror = true;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Suit = TEXT("#37474f");
	/** s1–s8 */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Skin = TEXT("s3");
	/** none|star|wing|bolt|chevron|target|shield|cross */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Patch = TEXT("wing");
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString PatchColor = TEXT("#ffc107");
	/** none|mic|crest|antenna|stripe */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Accessory = TEXT("mic");
	// §28 — body m|f · build slim|avg|broad · face oval|round|square|long|heart · eyes almond|round|narrow|wide · brows straight|arched|angled|soft
	//       brow_w thin|medium|thick · nose small|straight|wide|pointed · mouth neutral|smile|grin|serious · beard none|stubble|mustache|goatee|full
	//       glasses none|aviator|sun · hair short|buzz|spiky|sidepart|curly|mohawk|bald|long|ponytail|bun|bob · headgear helmet|none|cap|headset
	//       outfit suit|jacket|casual
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Body = TEXT("m");
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Build = TEXT("avg");
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Face = TEXT("oval");
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Eyes = TEXT("almond");
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString EyeColor = TEXT("#5b3a1e");
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Brows = TEXT("straight");
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString BrowW = TEXT("medium");
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Nose = TEXT("straight");
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Mouth = TEXT("smile");
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Beard = TEXT("none");
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Glasses = TEXT("none");
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Hair = TEXT("short");
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString HairColor = TEXT("#3b2a1e");
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Headgear = TEXT("none");   // kask yalnız kullanıcı seçerse (kullanıcı, 2026-10-28)
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Outfit = TEXT("suit");
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") bool bFreckles = false;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") bool bMole = false;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") bool bVisorOpen = false;
	TSharedRef<FJsonObject> ToJson() const;
	static FSmvAvatar FromJson(const TSharedPtr<FJsonObject>& O);
};

/** Yerel XP kaydı (misafir uçuşları; hesaba bağlanınca POST /api/crew/{id}/xp/import ile aktarılabilir) */
USTRUCT(BlueprintType)
struct SIMUMAV_API FSmvXpEntry
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") int32 Xp = 0;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString At;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Mode;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") int32 DurationS = 0;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") bool bImported = false;
};

/** Mürettebat pilotu (`pilot`). */
USTRUCT(BlueprintType)
struct SIMUMAV_API FSmvPilot
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Id;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Name;
	/** p01–p10 */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Character = TEXT("p01");
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") bool bIsDefault = false;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") int32 Flights = 0;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double Hours = 0;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FSmvAvatar Avatar;
	/** Deneyim puanı (toplam; hesapta sunucu: doğrulanmış + aktarılan) ve seviye 1…100 (app/simumav/xp.py eğrisi) */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") int64 Xp = 0;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") int64 XpUnverified = 0;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") int32 Level = 1;
	/** Yerel XP defteri (yalnız misafir/yerel) */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") TArray<FSmvXpEntry> XpLog;
	TSharedRef<FJsonObject> ToJson() const;
	static FSmvPilot FromJson(const TSharedPtr<FJsonObject>& O);
};

/** Parkur kapısı/halkası (`gates[]`). */
USTRUCT(BlueprintType)
struct SIMUMAV_API FSmvGate
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") int32 I = 0;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double Lat = 0;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double Lon = 0;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double AglM = 10;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double YawDeg = 0;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double RadiusM = 5;
	/** ring | gate */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Kind = TEXT("ring");
};

/** Parkur dünyası (`track.world`) — prosedürel dünya ayarları. */
USTRUCT(BlueprintType)
struct SIMUMAV_API FSmvTrackWorld
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") int32 Seed = 12345;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double SizeM = 4000;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double TreeDensity = 0.3;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double MountainHeight = 300;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") int32 MountainCount = 4;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") int32 BuildingCount = 40;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") bool bRunway = true;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double RunwayHeading = 0;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double Water = 0.1;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double Fog = 0.2;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double LandingCircleM = 0;
	/** Özel parkur biyomu (yol haritası "Parkur › Özel"): forest | city | mountain (boş = eski serbest dünya) */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Biome;
	/** Şehir: sahnede dekor olarak araç ve insan sayısı (yalnız görsel) */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") int32 Vehicles = 0;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") int32 People = 0;
};

/** Parkur (`track`): konum, rüzgâr, hava, saat, dünya kipi, kapılar. */
USTRUCT(BlueprintType)
struct SIMUMAV_API FSmvTrack
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Id;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Name;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double Lat = 39.9334;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double Lon = 32.8597;
	/** Zemin irtifası MSL (bilinmiyorsa < -1000 → yerel kipte 0, ArduPilot ev irtifası 100) */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double GroundMsl = -10000;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double RadiusM = 2000;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double WindMs = 3;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double WindDir = 270;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double WindGust = 0;
	/** day | dusk | night */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString TimeOfDay = TEXT("day");
	/** clear | cloudy | rain | fog | snow */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Weather = TEXT("clear");
	/** real | google3d | procedural */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString WorldMode = TEXT("procedural");
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FSmvTrackWorld World;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") TArray<FSmvGate> Gates;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") bool bIsDefault = false;
	/** Parkur sınıfı (yol haritası "Parkur"): custom (Özel — prosedürel biyom) | real (Gerçek Dünya — harita konumu) | "" (eski kayıt) */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString ParkurKind;
	/** Özel / Gerçek Dünya parkuru mu (eski kayıtlar dünya kipinden çıkarılır) */
	bool IsCustom() const { return ParkurKind == TEXT("custom") || (ParkurKind.IsEmpty() && WorldMode == TEXT("procedural")); }
	bool IsReal() const { return ParkurKind == TEXT("real") || (ParkurKind.IsEmpty() && (WorldMode == TEXT("real") || WorldMode == TEXT("google3d"))); }
	TSharedRef<FJsonObject> ToJson() const;
	static FSmvTrack FromJson(const TSharedPtr<FJsonObject>& O);
};

/** Görev (`mission`): plan sunucu biçiminde ham JSON metni olarak taşınır. */
USTRUCT(BlueprintType)
struct SIMUMAV_API FSmvMission
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Id;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString TrackId;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Name;
	/** corridor | free | spray | fpv_flags | fpv_gates | survey | defend */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Kind = TEXT("free");
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString PlanJson = TEXT("{}");
	TSharedRef<FJsonObject> ToJson() const;
	static FSmvMission FromJson(const TSharedPtr<FJsonObject>& O);
};

/** Ayarlar: sunucu alanları (`/api/settings`) + yalnız bu istemcide tutulanlar (grafik, kumanda, Mission Planner çıkışı). */
USTRUCT(BlueprintType)
struct SIMUMAV_API FSmvSettings
{
	GENERATED_BODY()
	// ---- sunucuyla eşitlenen ----
	/** tr | en | de | fr | es | ru | ar (boş = sistem dili) */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Lang;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Theme = TEXT("dark");
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") bool bSound = true;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") bool bAnalogFpv = false;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") bool bMusic = true;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double Volume = 0.8;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") bool bReducedMotion = false;
	/** metric | imperial */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Units = TEXT("metric");
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") bool bLeaderboardHidden = false;
	// ---- yalnız yerel ----
	/** off | visual | full */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString GpwsMode = TEXT("full");
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString GpwsSens = TEXT("normal");
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString GpwsLang = TEXT("aviation");
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") int32 BankCopter = 35;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") int32 BankPlane = 45;
	/** HUD/OSD gösterge teması: military | classic | fpv_osd | minimal */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString GaugeTheme = TEXT("military");
	/** Grafik kalitesi 0–4 (Ölçeklenebilirlik) */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") int32 GraphicsQuality = 3;
	/** Mission Planner'a UDP çıkışı (yerel SITL) */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") bool bMpUdpOut = false;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString MpUdpHost = TEXT("127.0.0.1");
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") int32 MpUdpPort = 14550;
	/** Kumanda: keyboard | gamepad | rc (RC verici HID) */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString InputDevice = TEXT("keyboard");
	/** RC kanal eşlemesi (0 tabanlı eksen numarası; -1 yok) ve ters çevirme */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") TArray<int32> RcAxisMap;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") TArray<bool> RcInvert;
	/** Mod-2 (sol çubuk gaz/yön) — false: Mod-1 */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") bool bStickMode2 = true;
	/** Kumanda modu 1–4 (HEPGO §27.1.1; web `fs.rc.mode`) — bStickMode2 eski alanla uyumlu tutulur (Mode 2 ↔ true) */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") int32 StickMode = 2;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double StickExpo = 0.3;
	/** Cesium ion erişim belirteci (gerçek arazi). Boşsa paketteki yapılandırma (Config/UserSecrets.ini) kullanılır; o da yoksa prosedürel dünya. */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString CesiumToken;
	/** HUD/OSD ses seviyesi çarpanı ve uyarı tonları */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") bool bAlertTones = true;
	TSharedRef<FJsonObject> ToServerJson() const;
	void ApplyServerJson(const TSharedPtr<FJsonObject>& O);
};

/** Kalkış ayarı (`launch`). */
USTRUCT(BlueprintType)
struct SIMUMAV_API FSmvLaunch
{
	GENERATED_BODY()
	/** wheels | ramp | vehicle | hand | pod4 | pod8 | tube | handheld (boş = tipin varsayılanı: dikey kalkış) */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Method;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double AngleDeg = 15;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double ReleaseKmh = 70;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double HandSpeedMs = 7;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double HandPitchDeg = 12;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") int32 Loaded = 1;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double AimAzDeg = 0;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double AimElDeg = 55;
	TSharedPtr<FJsonObject> ToJson() const;
};

/** Hedef (§24). */
USTRUCT(BlueprintType)
struct SIMUMAV_API FSmvTarget
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Model = TEXT("tgt:sw300");
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double AltM = 120;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double DiameterM = 800;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double SpeedMs = 28;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double EastM = 0;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double NorthM = 0;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") bool bCw = true;
};

/** Yer kontrol sahası (§26): istasyon, radar (1–50 km), elektro-optik (0,5–15 km) — kalkış noktasının yanında kule. */
USTRUCT(BlueprintType)
struct SIMUMAV_API FSmvSite
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") bool bStation = false;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") bool bRadar = false;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double RadarKm = 20;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") bool bEo = false;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double EoKm = 5;
	bool Any() const { return bStation || bRadar || bEo; }
};

/** Uç sihirbazının çıktısı → `POST /api/fly/start` gövdesi ya da yerel oturum. */
USTRUCT(BlueprintType)
struct SIMUMAV_API FSmvFlightRequest
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FSmvDrone Drone;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FSmvPilot Pilot;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FSmvTrack Track;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FSmvMission Mission;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") bool bHasMission = false;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FSmvLaunch Launch;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FSmvPowerSettings Power;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double Speedup = 1;
	/** day | thermal | night | none */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString Camera = TEXT("day");
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") TArray<FSmvTarget> Targets;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") double TargetRadiusM = 25;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FSmvSite Site;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") bool bRecordBin = true;
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") ESmvFlightBackend Backend = ESmvFlightBackend::LocalSitl;
	/** Oyun türü (sunucu modes.MODES): free | rings | cargo_drop | spraying | dogfight | facility_defense | ship_defense */
	UPROPERTY(BlueprintReadWrite, Category = "Simumav") FString GameMode = TEXT("free");
	/** Bulut isteği gövdesi (`drone_id/pilot_id/track_id/...`). */
	TSharedRef<FJsonObject> ToStartJson() const;
};

/** `POST /api/fly/start` yanıtı ya da yerel karşılığı. */
USTRUCT(BlueprintType)
struct SIMUMAV_API FSmvFlightInfo
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") FString FlightId;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") FString SimId;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") FString WsPath;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") FString MpHost;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") int32 MpPort = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") FString MpProto;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") FString State;
	static FSmvFlightInfo FromJson(const TSharedPtr<FJsonObject>& O);
};

/** WS `tele` karesinin FCaySimTelemetry dışında kalan SIMUMAV alanları. */
USTRUCT(BlueprintType)
struct SIMUMAV_API FSmvTeleExtra
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") double Speedup = 1;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") bool bFuel = false;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") double FuelL = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") double FuelPct = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") double RangeKm = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") bool bFuelEmpty = false;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") bool bElec = false;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") double ElecWh = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") double ElecPct = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") double ElecW = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") double ElecMinLeft = -1;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") double ElecV = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") bool bElecEmpty = false;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") double WindSpd = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") double WindDir = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") bool bMotorCut = false;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") TArray<int32> Servo;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") TArray<int32> ServoFn;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") double SensBad = 0;
	/** < 0 → yok */
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") double Ekf = -1;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") double Vibe = -1;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") double TerrainAgl = -100000;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") bool bGcsConnected = false;
	/** Konum hazırlığı (yerel SITL): −1 bilinmiyor (bulut), 0 GPS/EKF bekleniyor, 1 hazır */
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") int32 PosReady = -1;
	/** Hazır olmadan verilen kalkış / ARM kuyrukta */
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") bool bCmdQueued = false;
	static FSmvTeleExtra FromJson(const TSharedPtr<FJsonObject>& O);
};

/** Uçuş sonu özeti (özet ekranı + yerel kayıt). */
USTRUCT(BlueprintType)
struct SIMUMAV_API FSmvFlightSummary
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") FString FlightId;
	/** ok | crash | abort */
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") FString Result = TEXT("ok");
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") FString EndReason;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") double DurationS = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") double DistM = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") double MaxSpeedMs = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") double ArmedS = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") int32 AlertScore = -1;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") FString AlertGrade;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") int32 Warnings = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") int32 Cautions = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") int32 GatesPassed = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") int32 GatesMissed = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") int32 Laps = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") double BestLapS = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") double FuelUsedL = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") double WhUsed = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") int32 TargetScore = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") TArray<FString> Badges;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") bool bServoTriggerFired = false;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") double ServoTriggerAglM = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") double ServoTriggerDistM = 0;
	/** Uçuş kaydı listesi için: "araç · parkur" ve yerel tarih (yerel kayıt; eski kayıtlarda boş) */
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") FString Label;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") FString When;
	/** Oyun türü, tür puanı, kazanılan XP, tanınan manevralar ("anahtar:sayı"), pilot (yol haritası: pilota göre uçuşlar) */
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") FString GameMode;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") int32 GameScore = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") int32 Xp = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") TArray<FString> Maneuvers;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") FString PilotId;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") FString PilotName;
	/** Mevcut özetin tamamı (yerel kayıt / sunucu `summary`) */
	FString RawJson;
	static FSmvFlightSummary FromServerFlight(const TSharedPtr<FJsonObject>& Flight);
	TSharedRef<FJsonObject> ToJson() const;
};

/** TOP 10 satırı (`/api/leaderboard`). */
USTRUCT(BlueprintType)
struct SIMUMAV_API FSmvLeaderRow
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") int32 Rank = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") int32 Score = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") int32 Wins = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") int32 Matches = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") int32 Flights = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") bool bDemo = false;
	/** Sunucu maskeli e-posta (ör. a***@g***.com) */
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") FString Email;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") FSmvAvatar Avatar;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") bool bHasAvatar = false;
	/** Puan bileşenleri: match, pace, discipline, intercept, fpv (fpv varsa 0 da gösterilir) */
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") TMap<FString, int32> Parts;
	static FSmvLeaderRow FromJson(const TSharedPtr<FJsonObject>& O);
};

/** Karşılaşma oyuncusu (`players[]`). */
USTRUCT(BlueprintType)
struct SIMUMAV_API FSmvMatchPlayer
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") int64 Id = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") bool bHost = false;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") bool bBot = false;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") bool bReady = false;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") bool bOnline = false;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") FString Status;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") FString Role;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") FString Color;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") int32 Rings = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") int32 Total = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") FString FlightId;
	static FSmvMatchPlayer FromJson(const TSharedPtr<FJsonObject>& O);
};

/** Karşılaşma anlık görüntüsü. */
USTRUCT(BlueprintType)
struct SIMUMAV_API FSmvMatch
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") FString Id;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") FString Code;
	/** LOBI | HAZIRLIK | GERI_SAYIM | YARIS | BITTI | IPTAL */
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") FString State;
	/** rings | dogfight */
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") FString Kind = TEXT("rings");
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") int64 HostId = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") int32 MaxPlayers = 8;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") bool bCanStart = false;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") double StartAt = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") TArray<FSmvMatchPlayer> Players;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") TArray<FSmvGate> Rings;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") int64 Winner = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") FString MyFlightId;
	FString RawJson;
	static FSmvMatch FromJson(const TSharedPtr<FJsonObject>& O);
};

/** Küçük JSON yardımcıları (null-güvenli). */
namespace SmvJson
{
	SIMUMAV_API double Num(const TSharedPtr<FJsonObject>& O, const TCHAR* K, double Def = 0.0);
	SIMUMAV_API FString Str(const TSharedPtr<FJsonObject>& O, const TCHAR* K, const FString& Def = FString());
	SIMUMAV_API bool Bool(const TSharedPtr<FJsonObject>& O, const TCHAR* K, bool Def = false);
	SIMUMAV_API TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonObject>& O, const TCHAR* K);
	/** Sayı ya da metin kimliği → metin ("12" / "l-…") */
	SIMUMAV_API FString Id(const TSharedPtr<FJsonObject>& O, const TCHAR* K);
	SIMUMAV_API FString ToString(const TSharedRef<FJsonObject>& O, bool bPretty = false);
	SIMUMAV_API TSharedPtr<FJsonObject> Parse(const FString& S);
	/** Sunucu kimliği sayısal mı (yerel "l-" ön ekli değil) */
	SIMUMAV_API bool IsServerId(const FString& InId);
}
