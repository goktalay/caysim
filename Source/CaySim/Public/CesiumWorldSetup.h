// Dünya kurulumu — Cesium georeference + 3D Tiles (Cesium World Terrain / Google Photorealistic), gök/atmosfer, saat/sis/rüzgâr
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CaySimTypes.h"
#include "CaySimGeo.h"
#include "CesiumWorldSetup.generated.h"

class UHepgoClient;
class UStaticMesh;
class ADirectionalLight;
class ASkyLight;
class ASkyAtmosphere;
class AExponentialHeightFog;
class AVolumetricCloud;
#if CAYSIM_WITH_CESIUM
class ACesiumGeoreference;
class ACesium3DTileset;
class ACesiumSunSky;
class ACesiumCreditSystem;
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
	void ApplyWorldSettings(const FCaySimWorld& InWorld);
	/** Yerleşik gök/ışık aktörlerini (güneş, gök ışığı, atmosfer, sis, bulut; Cesium güneş-gök) kaldırır —
	 *  dışarıdan bir gök sistemi (ör. Simumav'da Fab Ultra Dynamic Sky) kurulduğunda. */
	void DisableBuiltinSky();

	UFUNCTION(BlueprintPure, Category = "CAYSIM|Dünya") bool IsReady() const { return bReady; }
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Dünya") ECaySimWorldSource GetSource() const { return ActiveSource; }
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Dünya") FCaySimCenter GetCenter() const { return Center; }
	/** Kurulu zemin yüksekliği (MSL m) — merkezde. */
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Dünya") double GetCenterGroundMsl() const { return CenterGroundMsl; }
	/** Yükleme yüzdesi (3D Tiles). */
	UFUNCTION(BlueprintPure, Category = "CAYSIM|Dünya") float GetLoadProgress() const { return LoadProgress; }
	/** Derlemede Cesium for Unreal var mı (CAYSIM_WITH_CESIUM) */
	static bool CesiumCompiled();
	/** Ek kamera (ör. uçuş haritası sahne yakalaması): Cesium karo seçimi bu görüş için de yapılır — yoksa yalnız oyuncu kamerasının
	 *  gördüğü karolar yüklenir, harita penceresi siyah / yavaş dolar. Id<0 → eklenir; döndürülen kimlik saklanır. */
	static int32 UpsertExtraCamera(const UObject* Ctx, int32 Id, const FVector2D& Size, const FVector& Loc, const FRotator& Rot, double FovDeg);
	static void RemoveExtraCamera(const UObject* Ctx, int32 Id);
	/** Etkin 3D Tiles kaynağının kısa adı (günlük / arayüz): google3d | ion_terrain | flat */
	FString SourceName() const;
	/** Yer ofsetini sabitle (otomatik ölçüm kapalı): MSL → elipsoid farkı (m). Fab haritası gerçek dünyaya oturtulurken. */
	void SetFixedGroundOffset(double OffsetM);
	/** Georeference merkezini düşeyde kaydır (m; +: 3D Tiles aşağı iner). Yer ofseti de aynı miktarda değişir → MSL yükseklikler Unreal'da sabit kalır. */
	void ShiftVertical(double DzM);
	/** Yalnız 3D Tiles yüzeyine aşağı ışın (Fab seviyeleri / aktörler yok sayılır). */
	bool TraceTiles(const FVector& Top, const FVector& Bottom, FHitResult& Out) const;
	/** 3D Tiles içinde verilen çokgeni (Unreal dünya XY, saat yönü ya da tersi) kırp — Fab haritası bu alanı doldurur. */
	bool ClipPolygon(const TArray<FVector>& UnrealPoints);
	/** Kaynak, ilk karolar gelmediği için yedeğe düştü mü (Google 3B → World Terrain + Bing) */
	bool DidFallback() const { return bFellBack; }
	/** Unreal XY noktalarında 3D Tiles yüzeyinin Unreal Z'si (Cesium SampleHeightMostDetailed; zaman uyumsuz, oyun iş parçacığında geri çağrı).
	 *  Karolar görünmese / çarpışma ağı olmasa da çalışır. Cesium yoksa hemen boş döner. */
	void SampleSurfaceZ(const TArray<FVector>& UnrealPoints, TFunction<void(const TArray<double>& UnrealZ, const TArray<bool>& bOk)> Done);
	/** En ayrıntılı arazi yüksekliği (coğrafi; ekrandaki karo LOD'undan bağımsız — kamera hareketinde değişmez). LonLat: (boylam, enlem);
	 *  sonuç GeoToUnreal'a verilecek yükseklik (elipsoid, m). Karo yoksa bOk false. */
	void SampleGroundMostDetailed(const TArray<FVector2D>& LonLat, TFunction<void(const TArray<double>& HeightM, const TArray<bool>& bOk)> Done);
	/** Etkin 3D Tiles aktörü (yoksa nullptr) */
	AActor* GetTilesActor() const;

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
	/** Cesium OSM Buildings (ion 96188): arazi + uydu kipinde 3B binalar (çarpışmalı); 0 = kapalı */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM|Dünya|Cesium", Config) int64 CesiumBuildingsAssetId = 96188;
	/** Google Photorealistic 3D Tiles ion varlık kimliği (ion hesabı üzerinden; HEPGO anahtarı gerekmez). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM|Dünya|Cesium", Config) int64 GoogleIonAssetId = 2275207;
	/** Google 3B bu sürede hiç karo yüklemezse World Terrain + Bing'e düşülür (sn). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM|Dünya|Cesium", Config) double GoogleFallbackS = 40.0;
	/** Cesium karo seçim istatistikleri günlüğe (tanı; komut satırı -CesiumStats) */
	bool bLogTileStats = false;
	/** Google 3B karolarında fizik ağı — binalar gerçekten çarpışır (kapalıyken çarpışma yalnız gizli World Terrain'den: binaların içinden geçilirdi) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM|Dünya|Cesium", Config) bool bGooglePhysics = true;
	/** 3D Tiles ekran-uzayı hata sınırı (küçük = daha ayrıntılı). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM|Dünya|Cesium", Config) double TilesScreenSpaceError = 4.0;   // web Cesium 2; 12'de arazi + uydu bulanık / birbirine girmiş görünüyordu
	/** Geoid ayrımı (MSL → WGS84 elipsoid) m; Türkiye için ≈ 30–40 m. Ev irtifası ile 3D Tiles yüzeyi arası farktan kalkışta otomatik düzeltilir. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM|Dünya") double GeoidOffsetM = 35.0;
	/** Kalkışa dek yer ofseti otomatik ölçülsün (merkez ışını ile 3D Tiles yüzeyi). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM|Dünya") bool bAutoGroundOffset = true;

	/** Düz zemin: yer düzlemi aktörü (Blueprint'te atanır; yoksa görünmez zemin). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CAYSIM|Dünya|Düz") TObjectPtr<UStaticMesh> FlatGroundMesh;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
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
	// UHT, WITH_EDITORONLY_DATA dışındaki #if blokları içinde UPROPERTY kabul etmez → yansıtılmayan zayıf işaretçiler.
	// Aktörler dünyaya (seviyeye) ait olduğundan çöp toplayıcıdan korunmaları için UPROPERTY gerekmez.
	TWeakObjectPtr<ACesiumGeoreference> Georeference;
	TWeakObjectPtr<ACesium3DTileset> Terrain;
	TWeakObjectPtr<ACesium3DTileset> GoogleTiles;
	TWeakObjectPtr<ACesium3DTileset> Buildings;
	TWeakObjectPtr<ACesiumSunSky> SunSky;
	TWeakObjectPtr<AActor> ClipActor;
	TWeakObjectPtr<ACesiumCreditSystem> CreditSys;
	TWeakObjectPtr<ACesium3DTileset> CollisionTiles;   // Google 3B'de gizli World Terrain (çarpışma / yükseklik)
	ACesium3DTileset* ActiveTiles() const;
	ACesium3DTileset* SpawnTiles(ACesiumGeoreference* Geo);
#endif

	void SetupLighting();
	void SetupFlat();
	void SetupCesiumIon();
	void SetupGoogle(const FString& Key);
	void SetupGoogleIon();
	void MeasureGroundOffset();
	bool bFellBack = false;
	double SourceT0 = 0, LastDiagAt = 0;
	/** yükleme ilerlemesi durağanlık takibi (son kayda değer artış ve zamanı) */
	float StallProg = 0.f; double StallAt = 0; bool bStallLogged = false;
	void Finish(bool bOk);
	static float TimeOfDayHours(const FString& Time);
};
