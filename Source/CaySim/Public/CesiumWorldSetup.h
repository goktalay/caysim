// Dünya kurulumu — Cesium georeference + 3D Tiles (Cesium World Terrain / Google Photorealistic), gök/atmosfer, saat/sis/rüzgâr
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CaySimTypes.h"
#include "CaySimGeo.h"
#include "CesiumWorldSetup.generated.h"

class UHepgoClient;
class ADirectionalLight;
class ASkyLight;
class ASkyAtmosphere;
class AExponentialHeightFog;
class AVolumetricCloud;
#if CAYSIM_WITH_CESIUM
class ACesiumGeoreference;
class ACesium3DTileset;
class ACesiumSunSky;
#endif

/** Dünya kaynağı seçimi (sihirbaz "Dünya" adımı). */
UENUM(BlueprintType)
enum class ECaySimWorldSource : uint8
{
	/** Cesium World Terrain + Bing uydu (Cesium ion belirteci gerekir) */
	CesiumIon        UMETA(DisplayName = "Cesium World Terrain (ion)"),
	/** Google Photorealistic 3D Tiles (HEPGO `/g3d-key`) */
	GooglePhotoreal  UMETA(DisplayName = "Google Photorealistic 3D Tiles"),
	/** HEPGO arazi vekili (`/terrain`, `/tile/...`) — ileride; şimdilik düz zemin */
	HepgoTerrain     UMETA(DisplayName = "HEPGO arazi vekili"),
	/** Düz yer düzlemi (Cesium yok / çevrimdışı) */
	Flat             UMETA(DisplayName = "Düz zemin")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCaySimWorldReadyDelegate, bool, bOk);

/**
 * Seçilen merkez etrafında dünyayı kurar. Georeference merkezi = merkez (lon, lat, yükseklik).
 * ICaySimGeoTransform uygular; DroneActor ve LaunchRig buna bağlanır.
 */
UCLASS(Blueprintable, Config = Game)
class CAYSIM_API ACaySimWorldSetup : public AActor, public ICaySimGeoTransform
{
	GENERATED_BODY()

public:
	ACaySimWorldSetup();

	/** Oturum bilgisine göre dünyayı kurar (merkez, world ayarları). Asenkron: OnWorldReady ile biter. */
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|Dünya")
	void Build(const FCaySimSession& Session, UHepgoClient* Client, ECaySimWorldSource Source);

	/** Saat/sis/rüzgâr değişikliklerini canlı uygular. */
	UFUNCTION(BlueprintCallable, Category = "CAYSIM|Dünya")
	void ApplyWorldSettings(const FCaySimWorld& World);

	UFUNCTION(BlueprintPure, Category = "CAYSIM|Dünya") bool IsReady() const { return bReady; }
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Dünya") ECaySimWorldSource GetSource() const { return ActiveSource; }
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Dünya") FCaySimCenter GetCenter() const { return Center; }
	/** Kurulu zemin yüksekliği (MSL m) — merkezde. */
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Dünya") double GetCenterGroundMsl() const { return CenterGroundMsl; }
	/** Yükleme yüzdesi (3D Tiles). */
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Dünya") float GetLoadProgress() const { return LoadProgress; }

	// ICaySimGeoTransform
	virtual FVector GeoToUnreal(double Lon, double Lat, double HeightM) const override;
	virtual void UnrealToGeo(const FVector& Unreal, double& Lon, double& Lat, double& HeightM) const override;
	virtual FQuat EnuToUnrealRotation(const FVector& UnrealLocation) const override;
	virtual double MslToWorldHeight(double Lat, double Lon, double MslM) const override;
	virtual bool SampleGroundHeight(double Lat, double Lon, double& OutHeightM) const override;

	UPROPERTY(BlueprintAssignable, Category = "CAYSIM|Dünya") FCaySimWorldReadyDelegate OnWorldReady;

	/** Cesium ion erişim belirteci (Config/DefaultGame.ini [/Script/CaySim.CaySimWorldSetup] ya da ayarlardan). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM|Dünya|Cesium", Config)
	FString CesiumIonToken;
	/** Cesium World Terrain varlık kimliği (ion). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM|Dünya|Cesium") int64 CesiumTerrainAssetId = 1;
	/** Bing Maps Aerial imagery varlık kimliği (ion). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM|Dünya|Cesium") int64 CesiumImageryAssetId = 2;
	/** Geoid ayrımı (MSL → WGS84 elipsoid) m; Türkiye için ≈ 30–40 m. Ev irtifası ile 3D Tiles yüzeyi arası farktan kalkışta otomatik düzeltilir. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM|Dünya") double GeoidOffsetM = 35.0;
	/** Kalkışa dek yer ofseti otomatik ölçülsün (merkez ışını ile 3D Tiles yüzeyi). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM|Dünya") bool bAutoGroundOffset = true;

	/** Düz zemin: yer düzlemi aktörü (Blueprint'te atanır; yoksa görünmez zemin). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM|Dünya|Düz") TObjectPtr<UStaticMesh> FlatGroundMesh;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	FCaySimCenter Center;
	FCaySimWorld World;
	ECaySimWorldSource ActiveSource = ECaySimWorldSource::Flat;
	bool bReady = false;
	float LoadProgress = 0.f;
	double CenterGroundMsl = 0.0;
	double MeasuredGroundOffsetM = 0.0; // elipsoid yüzeyi − MSL (kalkışa dek ölçülür)
	bool bGroundOffsetMeasured = false;
	TWeakObjectPtr<UHepgoClient> Hepgo;

	// Sahne aktörleri (yoksa oluşturulur)
	UPROPERTY() TObjectPtr<ADirectionalLight> Sun;
	UPROPERTY() TObjectPtr<ASkyLight> Sky;
	UPROPERTY() TObjectPtr<ASkyAtmosphere> Atmosphere;
	UPROPERTY() TObjectPtr<AExponentialHeightFog> Fog;
	UPROPERTY() TObjectPtr<AVolumetricCloud> Clouds;
	UPROPERTY() TObjectPtr<AActor> FlatGround;
#if CAYSIM_WITH_CESIUM
	UPROPERTY() TObjectPtr<ACesiumGeoreference> Georeference;
	UPROPERTY() TObjectPtr<ACesium3DTileset> Terrain;
	UPROPERTY() TObjectPtr<ACesium3DTileset> GoogleTiles;
	UPROPERTY() TObjectPtr<ACesiumSunSky> SunSky;
#endif

	void SetupLighting();
	void SetupFlat();
	void SetupCesiumIon();
	void SetupGoogle(const FString& Key);
	void MeasureGroundOffset();
	void Finish(bool bOk);
	static float TimeOfDayHours(const FString& Time);
};
