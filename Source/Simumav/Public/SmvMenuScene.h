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
	const FText& GetMapName() const { return MapName; }

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
	FText MapName;
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

	void BuildWorld();
	FVector EnuToWorld(double E, double N, double Agl) const;
	void ApplyPreviewChannels();
	void UpdateCamera(float Dt, const FVector& DronePos, const FVector& DroneFwd);
};
