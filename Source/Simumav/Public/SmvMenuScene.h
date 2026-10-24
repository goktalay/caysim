// Menü 3B sahnesi (web SIMUMAV menu/flight.js eşi): kabuk ekranlarının ARKASINDA canlı dünya — prosedürel arazi (TR TEST Kalecik),
// alçak irtifada sekizli tur atan sabit kanat dron, sinematik kamera döngüsü (takip → yan pan → yörünge → yerden izleme).
// Ayrıca hangar / sihirbaz için 3B ÖNİZLEME: seçili araç sahnenin çok üstünde, yalnız kendisini gösteren SceneCapture ile render hedefine
// çizilir (Slate fırçası), yavaşça döner. Ayarlar → Görüntü'den kapatılabilir (FSmvUiPrefs::bMenuScene / bPreview3d).
// Uçuş başlarken GameMode bu aktörü yok eder (görüş hedefi piyona döner).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SimumavTypes.h"
#include "SmvMenuScene.generated.h"

class ACaySimWorldSetup;
class ASmvProcTerrain;
class ASmvDroneActor;
class ACameraActor;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class UPointLightComponent;
class ASmvPilotFigure;

UCLASS()
class SIMUMAV_API ASmvMenuScene : public AActor
{
	GENERATED_BODY()

public:
	ASmvMenuScene();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** Önizlenecek araç (tip, katalog modeli, gövde rengi #rrggbb) */
	void SetPreview(const FString& Type, const FString& ModelKey, const FString& ColorHex);
	/** Önizleme görünür mü (görünmüyorsa yakalama durur) */
	void SetPreviewActive(bool bOn);
	/** Pilot önizlemesi (tam boy 3B figür); bFace: yüze yakın çerçeve */
	void SetPilotPreview(const FSmvAvatar& A, bool bFace);
	/** Fareyle sürükleyerek döndürme (derece) — otomatik dönüş bir süre durur */
	void AddPreviewYaw(float Deg);
	UTextureRenderTarget2D* GetPreviewTarget() const { return PreviewRT; }
	/** Avatar anlık görüntüsü (baş-omuz, 160×160; TOP10 / listeler): önbellekte yoksa sıraya alınır, kare başına bir tane çizilir */
	UTextureRenderTarget2D* AvatarSnap(const FSmvAvatar& A);
	/** Dron anlık görüntüsü (hangar şeridi mini kartları, 240×150): sıraya alınır; model yüklenince yeniden çekilir */
	UTextureRenderTarget2D* DroneSnap(const FString& Type, const FString& ModelKey, const FString& ColorHex);
	/** "Hangara indi": önizlemedeki araç yukarıdan süzülerek iner (1,6 sn) */
	void PlayLanding();
	/** Dünya oluşturucu canlı 3B önizlemesi: parkurun prosedürel dünyası uzak bir noktada kurulur, önizleme yakalaması yörüngede döner */
	void SetWorldPreview(const FSmvTrack& Track);
	bool IsLive() const { return bLive; }
	/** Canlı sahnenin haritası (Fab harita adı; yoksa boş → prosedürel arazi) */
	FText GetMapName() const;

	// ---- yol haritası: gerçek 3B sahne arka planları (stüdyo yerine canlı dünyada kamera istasyonları) ----
	/** Kamera istasyonu: Menu sinematik döngü · Hangar vitrin aracı çevresinde yörünge · Crew pilot portresi · Parkur yüksek kuş bakışı ·
	 *  Fly kalkış alanında alçak kahraman açısı · Tour harita içinden kamera turu (karşılaşma parkuru önizlemesi) */
	enum class EStation : uint8 { Menu, Hangar, Crew, Parkur, Fly, Tour };
	void SetStation(EStation S);
	EStation GetStation() const { return Station; }
	/** Vitrin aracı (gerçek dünyada kalkış alanında; Lumen ışığı, gölge, hava) — donanım (eklenti parçaları, çıkartma, desen) dahil */
	void SetShowcase(const FSmvDrone& D);
	void ClearShowcase();
	/** Vitrin pilotu (kalkış alanında RC kumandayla); bFace: yüze yakın kadraj */
	void SetShowcasePilot(const FSmvAvatar& A, bool bFace);
	/** Menü sahnesinin haritası (Fab kayıt anahtarı; boş/proc → prosedürel arazi) — karşılaşma parkuru turu ve duman testi için */
	void SetMap(const FString& Key);
	const FString& GetMapKey() const { return MapKey; }
	/** Harita akışı bitti mi (tur başlığı "yükleniyor" yazısı) */
	bool IsMapReady() const;
	/** Parkur kartı: canlı dünyadan ikinci görünüm (harita içinde alçak yörünge; 640×360) — Karşılaşma kartının "video döngüsü" */
	UTextureRenderTarget2D* GetCardTarget();
	void SetCardActive(bool bOn);
	/** Hangar → Kamera adımı: seçilen kamera türünün son işlemi sahne kamerasına (gece görüşü / termal / multispektral önizleme; day = kapalı) */
	void SetCameraFx(const FString& Kind);

protected:
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneCaptureComponent2D> Capture;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> KeyLight;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> RimLight;
	/** Stüdyo duvarında figürün/aracın arkasında yumuşak ışık halesi (siyah fon yerine derinlik) */
	UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> WallLight;
	/** Menü kamerasının önünde süzülen ışık zerreleri (Niagara; varlık yoksa yok) */
	UPROPERTY() TObjectPtr<class UNiagaraComponent> Motes;
	UPROPERTY() TObjectPtr<UTextureRenderTarget2D> PreviewRT;
	UPROPERTY() TObjectPtr<ACaySimWorldSetup> WorldSetup;
	UPROPERTY() TObjectPtr<ASmvProcTerrain> Terrain;
	UPROPERTY() TObjectPtr<ASmvDroneActor> Flyer;
	/** Ek süzülen araçlar (katalogdan: kopter yörüngede, VTOL uzun tur) */
	UPROPERTY() TArray<TObjectPtr<ASmvDroneActor>> Extras;
	/** Fab haritası (varsa menü arka planı) ve gök */
	UPROPERTY() TObjectPtr<class ULevelStreamingDynamic> MenuLevel;
	UPROPERTY() TArray<TObjectPtr<AActor>> SkyActors;
	/** Fab haritası çevresi (havada ada görünmesin; kayıt tablosu Surround) ve gökte uzak trafik (yalnız görsel) */
	UPROPERTY() TArray<TObjectPtr<AActor>> SceneryActors;
	bool bSceneryDone = false;
	void TickScenery();
	UPROPERTY() TObjectPtr<ACameraActor> Cam;
	UPROPERTY() TObjectPtr<ASmvDroneActor> Preview;
	UPROPERTY() TObjectPtr<ASmvPilotFigure> PilotPrev;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneCaptureComponent2D> SnapCap;
	/** Önizleme stüdyosu: arka duvar + zemin diski (siyah boşluk yerine) */
	UPROPERTY(VisibleAnywhere) TObjectPtr<class UStaticMeshComponent> StudioWall;
	UPROPERTY(VisibleAnywhere) TObjectPtr<class UStaticMeshComponent> StudioFloor;
	void PlaceStudio(float FloorZ);
	/** Anahtar / kenar ışığını konumlar; şiddet hedefe uzaklığın karesiyle ölçeklenir (uzak ışık figürü karartmasın) */
	void PlaceLights(const FVector& Target, const FVector& KeyAt, const FVector& RimAt, float Scale = 1.f);
	UPROPERTY() TObjectPtr<ASmvPilotFigure> SnapFig;
	UPROPERTY() TMap<FString, TObjectPtr<UTextureRenderTarget2D>> Snaps;
	UPROPERTY() TObjectPtr<ASmvDroneActor> DSnapActor;
	UPROPERTY() TObjectPtr<ASmvProcTerrain> WorldPrev;
	UPROPERTY() TObjectPtr<ASmvDroneActor> Showcase;
	UPROPERTY() TObjectPtr<USceneCaptureComponent2D> CardCap;
	UPROPERTY() TObjectPtr<UTextureRenderTarget2D> CardRT;
	UPROPERTY() TObjectPtr<ASmvPilotFigure> ShowPilot;

private:
	bool bLive = false;
	bool bPreviewOn = false;
	double T0 = 0;
	float PreviewYaw = 0.f;
	float PreviewDistCm = 300.f;
	double LastChannelFix = 0;
	FString PrevType, PrevModel, PrevColor;
	double Lat0 = 40.0990, Lon0 = 33.4101, GroundMsl = 100.0;
	FVector CamPos = FVector::ZeroVector;
	FRotator CamRot = FRotator::ZeroRotator;
	bool bCamInit = false;
	bool bFaceZoom = false;
	float FaceK = 0.f;
	double ManualUntil = 0;
	FString PilotKey;
	bool bFabMap = false;
	FString MapKey;
	double FabStartE = 0, FabStartN = 0;
	TMap<int64, float> GroundCache;
	float GroundAt(double E, double N);
	ASmvDroneActor* SpawnFlyer(const FString& Type, const FLinearColor& Color);
	TArray<TPair<FString, FSmvAvatar>> SnapQueue;
	FString SnapPending;
	int32 SnapPhase = 0;
	struct FDroneJob { FString Key, Type, Model, Color; };
	TArray<FDroneJob> DQueue;
	FString DPending;
	int32 DFrames = 0;
	/** GLB gövdesi yüklendiği kare (−1 bekleniyor; modelsiz işte 0) */
	int32 DGlbFrame = -1;
	bool bDHasModel = false;
	double LandAt = -100.0;
	double WpLat = 0, WpLon = 0, WpSize = 4000;
	void TickSnaps();
	void TickDroneSnaps();

	EStation Station = EStation::Menu;
	bool bCardOn = false;
	FString CamFx = TEXT("day");
	FPostProcessSettings BasePp;
	bool bBasePp = false;
	double StationAt = 0;
	FString ShowKey, ShowPilotKey;
	bool bShowFace = false;
	float ShowFaceK = 0.f;
	float ShowOrbit = 0.f;
	FVector StationFocus = FVector::ZeroVector;
	/** Vitrin noktası (ENU m; kalkış alanı yanı) */
	double ShowE = -24.0, ShowN = 18.0;
	FVector ShowGround();
	void UpdateStationCamera(float Dt);
	void SettleShowcase();
	void LoadMenuMap(const FString& Key);
	void BuildWorld();
	FVector EnuToWorld(double E, double N, double Agl) const;
	void ApplyPreviewChannels();
	void UpdateCamera(float Dt, const FVector& DronePos, const FVector& DroneFwd);
};
