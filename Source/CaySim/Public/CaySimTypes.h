// CAYSIM — HEPGO API sözleşmesi v1 veri tipleri (docs/hepgo-baglanti.md)
// Sözleşme belgesi (HEPGO docs/caysim-api.md) kesinleşince alan adları birebir buna uyarlanır.
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "CaySimTypes.generated.h"

/** Dron tipi (HEPGO `types[].key`). Bilinmeyen anahtarlar Multirotor sayılır. */
UENUM(BlueprintType)
enum class ECaySimDroneType : uint8
{
	FixedWing   UMETA(DisplayName = "Sabit kanat"),
	Multirotor  UMETA(DisplayName = "Multikopter"),
	Hexarotor   UMETA(DisplayName = "Hexarotor"),
	Vtol        UMETA(DisplayName = "VTOL"),
	Helicopter  UMETA(DisplayName = "Helikopter")
};

/** Oturum kipi. */
UENUM(BlueprintType)
enum class ECaySimSessionMode : uint8
{
	Sitl    UMETA(DisplayName = "Canlı (SITL)"),
	Replay  UMETA(DisplayName = "Kayıttan oynatma")
};

/** HEPGO oturum durumu (`session.state`). */
UENUM(BlueprintType)
enum class ECaySimSessionState : uint8
{
	Starting  UMETA(DisplayName = "BASLIYOR"),
	Ready     UMETA(DisplayName = "HAZIR"),
	Error     UMETA(DisplayName = "HATA"),
	Closed    UMETA(DisplayName = "KAPANDI"),
	Unknown   UMETA(DisplayName = "Bilinmiyor")
};

/** Kalkış yöntemi (görsel donanım; tetik telemetri/`launch` olayından). */
UENUM(BlueprintType)
enum class ECaySimLaunchMethod : uint8
{
	Runway   UMETA(DisplayName = "Tekerlekli pist koşusu"),
	Ramp     UMETA(DisplayName = "Pnömatik fırlatma rampası"),
	Vehicle  UMETA(DisplayName = "Araç üstünden bırakma"),
	Vertical UMETA(DisplayName = "Dikey kalkış")
};

/** Dünya ayarları — `world` nesnesi (sunucu doğrular ve saklar). */
USTRUCT(BlueprintType)
struct CAYSIM_API FCaySimWorld
{
	GENERATED_BODY()

	/** procedural | real | google3d */
	UPROPERTY(BlueprintReadWrite, Category = "CAYSIM") FString Mode = TEXT("real");
	UPROPERTY(BlueprintReadWrite, Category = "CAYSIM") int32 Seed = 12345;
	UPROPERTY(BlueprintReadWrite, Category = "CAYSIM") float SizeM = 4000.f;
	UPROPERTY(BlueprintReadWrite, Category = "CAYSIM") float TreeDensity = 0.3f;
	UPROPERTY(BlueprintReadWrite, Category = "CAYSIM") float MountainHeight = 300.f;
	UPROPERTY(BlueprintReadWrite, Category = "CAYSIM") int32 MountainCount = 4;
	UPROPERTY(BlueprintReadWrite, Category = "CAYSIM") int32 BuildingCount = 40;
	UPROPERTY(BlueprintReadWrite, Category = "CAYSIM") bool bRunway = true;
	UPROPERTY(BlueprintReadWrite, Category = "CAYSIM") float RunwayHeading = 0.f;
	UPROPERTY(BlueprintReadWrite, Category = "CAYSIM") float Water = 0.1f;
	/** day | dusk | night */
	UPROPERTY(BlueprintReadWrite, Category = "CAYSIM") FString Time = TEXT("day");
	UPROPERTY(BlueprintReadWrite, Category = "CAYSIM") float WindMs = 3.f;
	UPROPERTY(BlueprintReadWrite, Category = "CAYSIM") float WindDir = 270.f;
	UPROPERTY(BlueprintReadWrite, Category = "CAYSIM") float Fog = 0.2f;

	TSharedRef<FJsonObject> ToJson() const;
	static FCaySimWorld FromJson(const TSharedPtr<FJsonObject>& Obj);
};

/** Şirket kimliği (`hello.company`). */
USTRUCT(BlueprintType)
struct CAYSIM_API FCaySimCompany
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString LegalName;
	/** göreli yol (`/api/caysim/v1/brand/logo`); Bearer ister */
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString LogoUrl;
	/** tema rengi hex (ör. #1e88e5) ya da boş */
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString PrimaryColor;
};

/** `GET /hello` yanıtı. */
USTRUCT(BlueprintType)
struct CAYSIM_API FCaySimHello
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") int32 Version = 0;
	/** ISO-8601 sunucu yerel saati (yalnız gösterim; saat eşitleme `tele.ts` ile) */
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString ServerTime;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") int32 ClientId = 0;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString ClientName;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString ClientOwnerName;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FCaySimCompany Company;
	/** sessions, cmd, rc, tele50, models, terrain, osm, replay, mavlink_udp, google3d */
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") TArray<FString> Caps;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") int32 MaxSessions = 0;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") int32 MaxMinutes = 0;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") int32 RcHz = 40;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") int32 TeleHz = 50;
	/** SITL UDP ana makinesi (Mission Planner'a verilecek) */
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString Host;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") bool bGoogle3dEnabled = false;
	/** false yalnız test kipi */
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") bool bSitlEnabled = true;
	/** `/ws/caysim/v1/sessions/{id}` */
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString WsUrl;

	bool HasCap(const FString& Cap) const { return Caps.Num() == 0 || Caps.Contains(Cap); }
};

/** Merkez (`GET /centers`). */
USTRUCT(BlueprintType)
struct CAYSIM_API FCaySimCenter
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") int32 Id = 0;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") double Lat = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") double Lon = 0.0;
	/** MSL m; HEPGO boş gönderirse NaN kalır → arazi yüksekliği sunucudan/oturumdan alınır */
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") double Alt = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") bool bHasAlt = false;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString LocationName;
};

/** Dron tipi (`GET /types`). */
USTRUCT(BlueprintType)
struct CAYSIM_API FCaySimDroneTypeInfo
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString Key;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") bool bBinaryAvailable = true;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") ECaySimDroneType Type = ECaySimDroneType::Multirotor;
};

/** Model yönelimi (`orient`): burun/yukarı ekseni, ölçek, ofset. */
USTRUCT(BlueprintType)
struct CAYSIM_API FCaySimModelOrient
{
	GENERATED_BODY()
	/** +x, -x, +y, -y, +z, -z (GLB dosya ekseni) */
	UPROPERTY(BlueprintReadWrite, Category = "CAYSIM") FString Forward = TEXT("+z");
	UPROPERTY(BlueprintReadWrite, Category = "CAYSIM") FString Up = TEXT("+y");
	/** dosya birimi → metre */
	UPROPERTY(BlueprintReadWrite, Category = "CAYSIM") float Scale = 1.f;
	UPROPERTY(BlueprintReadWrite, Category = "CAYSIM") FVector Offset = FVector::ZeroVector;
	/** none | tail | nose | manual */
	UPROPERTY(BlueprintReadWrite, Category = "CAYSIM") FString PropMode = TEXT("none");
	UPROPERTY(BlueprintReadWrite, Category = "CAYSIM") FVector PropPos = FVector::ZeroVector;
	UPROPERTY(BlueprintReadWrite, Category = "CAYSIM") float PropRadius = 0.15f;

	TSharedRef<FJsonObject> ToJson() const;
	static FCaySimModelOrient FromJson(const TSharedPtr<FJsonObject>& Obj);
};

/** Görsel model (`GET /models?type=`). */
USTRUCT(BlueprintType)
struct CAYSIM_API FCaySimModelInfo
{
	GENERATED_BODY()
	/** tam sayı ya da `builtin:<ad>` */
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString Id;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString Name;
	/** builtin | upload | design */
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString Source;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString TypeKey;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString ThumbUrl;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString GlbUrl;
	/** glb | gltf | step | obj | procedural */
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString Format;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") int64 Size = 0;
	/** GLB sunucuda hazır mı (hazır değilse CAYSIM dahili yer tutucu gövde kullanır) */
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") bool bReady = false;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") bool bHasProp = false;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FCaySimModelOrient Orient;
};

/** Kayıtlı uçuş logu (`GET /logs`). */
USTRUCT(BlueprintType)
struct CAYSIM_API FCaySimLogInfo
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") int32 Id = 0;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString FileName;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString Ext;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") int64 Size = 0;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString UploadedAt;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString Aircraft;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString Pilot;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString Location;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString TaskCode;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString Vehicle;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString TypeKey;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") float DurationS = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") double HomeLat = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") double HomeLon = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") bool bCached = false;
};

/** Yer istasyonu (Mission Planner) bağlantı durumu. */
USTRUCT(BlueprintType)
struct CAYSIM_API FCaySimGcsState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") bool bConnected = false;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString Addr;
};

/** Kayıttan oynatma özeti (`session.replay`). */
USTRUCT(BlueprintType)
struct CAYSIM_API FCaySimReplayInfo
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") int32 LogId = 0;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString FileName;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") float Progress = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") int32 Frames = 0;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") float DurationS = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") float Hz = 25.f;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") float TakeoffT = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString Vehicle;
};

/** Oturum (`session`). */
USTRUCT(BlueprintType)
struct CAYSIM_API FCaySimSession
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString Id;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") ECaySimSessionMode Mode = ECaySimSessionMode::Sitl;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString TypeKey;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") ECaySimDroneType Type = ECaySimDroneType::Multirotor;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FCaySimModelInfo Model;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FCaySimCenter Center;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FCaySimWorld World;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") ECaySimSessionState State = ECaySimSessionState::Unknown;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") int32 UdpPort = 0;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString Host;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString OwnerName;
	/** Oturumu açan CAYSIM istemcisi (web oturumunda boş) */
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString ClientName;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString CreatedAt;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FCaySimGcsState Gcs;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString SitlError;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FCaySimReplayInfo Replay;
	/** `hello.can_command` — bu istemci komut verebilir mi */
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") bool bCanCommand = false;

	static FCaySimSession FromJson(const TSharedPtr<FJsonObject>& Obj);
};

/** Tek telemetri karesi (`tele`; açılar rad NED, hızlar m/s, heading derece). */
USTRUCT(BlueprintType)
struct CAYSIM_API FCaySimTelemetry
{
	GENERATED_BODY()
	/** sunucu alım zamanı (Unix sn) */
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") double Ts = 0.0;
	/** SITL time_boot_ms */
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") int64 Tbm = 0;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") double Lat = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") double Lon = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") double AltMsl = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") double AltRel = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") float Roll = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") float Pitch = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") float Yaw = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") float RollSpeed = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") float PitchSpeed = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") float YawSpeed = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") float Vn = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") float Ve = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") float Vd = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") float Airspeed = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") float Groundspeed = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") float Heading = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") float Climb = 0.f;
	/** ArduPilot mod adı; ilk kalp atışına dek boş */
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString Mode;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") bool bArmed = false;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") int32 GpsFix = 0;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") int32 Sats = -1;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") float BattV = -1.f;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") float BattPct = -1.f;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") float Throttle = 0.f;
	/** 8 kanal 1000–2000 (0 = yok) */
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") TArray<int32> Rc;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") bool bHasHome = false;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") double HomeLat = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") double HomeLon = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") double HomeAlt = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") bool bGcsConnected = false;

	static FCaySimTelemetry FromJson(const TSharedPtr<FJsonObject>& Obj);
};

/** Sunucu olayı (`event`). */
USTRUCT(BlueprintType)
struct CAYSIM_API FCaySimEvent
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString Text;
	/** MAVLink STATUSTEXT önem derecesi (0 acil … 7 hata ayıklama); köprü olayları 6 */
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") int32 Severity = 6;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") double T = 0.0;
};

/** Ham MAVLink UDP bilgisi (`GET /sessions/{id}/mavlink`, §8). */
USTRUCT(BlueprintType)
struct CAYSIM_API FCaySimMavlinkInfo
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString Host;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") int32 UdpPort = 0;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") bool bGcsLocked = false;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString GcsAddr;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString Note;
};

/** Kalkış olayı (`launch`): v1'de sunucu göndermez (ayrılmış ad); CAYSIM telemetriden türetir. */
USTRUCT(BlueprintType)
struct CAYSIM_API FCaySimLaunchEvent
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") ECaySimLaunchMethod Method = ECaySimLaunchMethod::Runway;
	/** armed | release | airborne */
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") FString Phase;
	/** rampa açısı (derece) */
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") float AngleDeg = 15.f;
	/** araç hızı ya da rampa çıkış hızı (m/s) */
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") float SpeedMs = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "CAYSIM") float HeadingDeg = 0.f;
};

/** Yardımcılar: dizge ↔ enum dönüşümleri. */
namespace CaySimTypes
{
	CAYSIM_API ECaySimDroneType DroneTypeFromKey(const FString& Key);
	CAYSIM_API FString DroneTypeToKey(ECaySimDroneType Type);
	CAYSIM_API ECaySimSessionState StateFromString(const FString& S);
	CAYSIM_API ECaySimLaunchMethod LaunchMethodFromString(const FString& S);
	CAYSIM_API FString LaunchMethodToString(ECaySimLaunchMethod M);
}
