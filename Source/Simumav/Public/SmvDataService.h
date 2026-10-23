// Veri hizmeti: hangar / mürettebat / parkur / görev / ayar / uçuş kayıtları için TEK arayüz.
//   Misafir kip  → yerel profil: Saved/Simumav/Profiles/<steam_id|local>/{hangar,crew,tracks,missions,settings,flights}.json
//   Hesap kipi   → simumav.com (REST) + yerel önbellek; ayarların yalnız-istemci alanları yine yerelde.
// Misafir verisi giriş sonrası hesaba aktarılabilir (ImportGuestToAccount: her kayıt sunucu uçlarına POST edilir).
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "SimumavTypes.h"
#include "SmvDataService.generated.h"

class USimumavClient;

/** Hangar kataloğu öğesi (sunucu bridge.GLB_MODELS + PROC_MODELS ile aynı anahtarlar). */
USTRUCT(BlueprintType)
struct SIMUMAV_API FSmvCatalogModel
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") FString Key;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") FString Label;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") FString Type;
	/** /static/simumav/models/<ad>.glb (boş → prosedürel gövde) */
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") FString GlbPath;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") FString Airframe;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") FString Drive;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") FString Desc;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") bool bHasProp = true;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") TMap<FString, double> Defaults;

	// ---- araç kataloğu (docs/arac-katalogu.md): gövde, fizik, istatistik ----
	/** Pakete gömülü GLB (Content/Simumav/ModelsSrc/<dosya>; UFS). Boş → GlbPath'ten indirme / prosedürel gövde. */
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") FString GlbFile;
	/** GLB burun düzeltmesi (glTF Y ekseni etrafında, derece; Caymaz +Z burunlu → 90) */
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") float GlbYawDeg = 0.f;
	/** Fab gövdesi seçeneği: /Game/Fab/Drone (Quadcopter) | /Game/Fab/DroneFpv (FPV Interceptor); boş değilse GLB yerine bu */
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") FString FabDir;
	/** Yalnız yerel SITL (simumav.com kataloğunda yok → hesap kipinde sunucuya kaydedilemez / bulutta uçulamaz) */
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") bool bLocalOnly = false;
	/** VTOL kaldırma rotorları modelde yok → kollarla eklenir */
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") bool bAddLiftRotors = false;
	/** Ses: multi | prop | pusher | jet | heli | electric_heli | rover */
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") FString Sound;
	/** ArduPilot SITL: ikili (ArduCopter | ArduHeli | ArduPlane | ArduRover), --model değeri, okunur özet */
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") FString SitlBinary;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") FString SitlModel;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") FString Physics;
	/** Tanıtım istatistikleri (0 = bilinmiyor) */
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") float MassKg = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") float SpanM = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") float LengthM = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") float CruiseKmh = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") float MaxKmh = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") float EnduranceMin = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") float PayloadKg = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") int32 Motors = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Simumav") FString Battery;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSmvDataChanged);
DECLARE_DELEGATE_TwoParams(FSmvDone, bool /*bOk*/, const FString& /*Message*/);

UCLASS()
class SIMUMAV_API USmvDataService : public UObject
{
	GENERATED_BODY()

public:
	void Init(USimumavClient* InClient, const FString& InProfileKey, const FString& DisplayName);
	UFUNCTION(BlueprintPure, Category = "Simumav") ESmvAccountMode GetMode() const;

	/** Yerel profili yükler (yoksa varsayılanlarla oluşturur). */
	void LoadLocal();
	/** Hesap kipinde sunucudan çeker (hangar, mürettebat, parkur, görev, ayar). */
	void RefreshOnline(FSmvDone Done);

	// ---- okuma ----
	const TArray<FSmvDrone>& Drones() const { return DronesCache; }
	const TArray<FSmvPilot>& Pilots() const { return PilotsCache; }
	const TArray<FSmvTrack>& Tracks() const { return TracksCache; }
	const TArray<FSmvMission>& Missions() const { return MissionsCache; }
	const TArray<FSmvFlightSummary>& Flights() const { return FlightsCache; }
	const FSmvSettings& Settings() const { return SettingsCache; }
	FSmvSettings& MutableSettings() { return SettingsCache; }
	const FSmvDrone* DefaultDrone() const;
	const FSmvPilot* DefaultPilot() const;
	const FSmvTrack* DefaultTrack() const;

	// ---- yazma (kipten bağımsız) ----
	void SaveDrone(const FSmvDrone& D, FSmvDone Done);
	void DeleteDrone(const FString& Id, FSmvDone Done);
	void SavePilot(const FSmvPilot& P, FSmvDone Done);
	void DeletePilot(const FString& Id, FSmvDone Done);
	void SaveTrack(const FSmvTrack& T, FSmvDone Done);
	void DeleteTrack(const FString& Id, FSmvDone Done);
	void SaveMission(const FSmvMission& M, FSmvDone Done);
	void DeleteMission(const FString& Id, FSmvDone Done);
	void SaveSettings(FSmvDone Done);
	void AddFlightRecord(const FSmvFlightSummary& S);
	void SetDefault(const FString& Kind, const FString& Id);

	/** Misafir verisini hesaba aktarır (giriş sonrası teklif edilir). Yerel kopya korunur. */
	void ImportGuestToAccount(FSmvDone Done);
	bool HasGuestData() const;

	// ---- katalog / modeller ----
	static const TArray<FSmvCatalogModel>& Catalog();
	static const FSmvCatalogModel* FindModel(const FString& Key);
	static FString DefaultModelFor(const FString& Type);
	/** Tipin katalog modelleri (katalog sırasıyla; bOnlineOnly → yalnız simumav.com'da da olanlar). */
	static TArray<const FSmvCatalogModel*> ModelsForType(const FString& Type, bool bOnlineOnly = false);
	/** Hedef / dekor modelleri (tgt_*): yalnız görsel hedef nesnesi — hangar ve SITL eşlemesi yok. */
	static const TArray<FSmvCatalogModel>& TargetModels();
	/** Pakete gömülü GLB baytları (Content/Simumav/ModelsSrc/<dosya>; bellekte önbellekli). */
	static bool LoadBundledGlb(const FString& File, TArray<uint8>& Out);
	/** GLB baytları (önbellek: Saved/Simumav/ModelCache). simumav.com'dan oturumsuz indirilir. */
	void FetchModelGlb(const FString& ModelKey, TFunction<void(bool, const TArray<uint8>&)> Done);

	UPROPERTY(BlueprintAssignable, Category = "Simumav") FSmvDataChanged OnChanged;

private:
	UPROPERTY() TObjectPtr<USimumavClient> Client;
	FString ProfileKey;
	FString ProfileName;
	TArray<FSmvDrone> DronesCache;
	TArray<FSmvPilot> PilotsCache;
	TArray<FSmvTrack> TracksCache;
	TArray<FSmvMission> MissionsCache;
	TArray<FSmvFlightSummary> FlightsCache;
	FSmvSettings SettingsCache;

	bool IsOnline() const;
	FString ProfileDir() const;
	FString GuestDir() const;
	void SaveLocal() const;
	void SeedDefaults();
	static FString NewLocalId() ;
	template <typename T> static void Upsert(TArray<T>& Arr, const T& Item);
	template <typename T> static void RemoveById(TArray<T>& Arr, const FString& Id);
	void SaveSettingsLocalOnly() const;
	void LoadSettingsLocal();
};
