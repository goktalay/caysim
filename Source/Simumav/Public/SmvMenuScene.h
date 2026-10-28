// Menü 3B sahnesi (web SIMUMAV menu/flight.js eşi): kabuk ekranlarının ARKASINDA canlı dünya — prosedürel arazi (TR TEST Kalecik),
// alçak irtifada sekizli tur atan sabit kanat dron, sinematik kamera döngüsü (takip → yan pan → yörünge → yerden izleme).
// Ayrıca hangar / sihirbaz için 3B ÖNİZLEME: seçili araç sahnenin çok üstünde, yalnız kendisini gösteren SceneCapture ile render hedefine
// çizilir (Slate fırçası), yavaşça döner. Ayarlar → Görüntü'den kapatılabilir (FSmvUiPrefs::bMenuScene / bPreview3d).
// Uçuş başlarken GameMode bu aktörü yok eder (görüş hedefi piyona döner).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SimumavTypes.h"
#include "SmvLoadGate.h"
#include "SmvMenuScene.generated.h"

class ACaySimWorldSetup;
class ASmvProcTerrain;
class ASmvDroneActor;
class ACameraActor;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class UPointLightComponent;
class ASmvPilotFigure;
class UTexture2D;

/** Dönen küçük görüntünün kareleri (arka plansız, alfa kanallı) */
USTRUCT()
struct FSmvSpinSet
{
	GENERATED_BODY()
	UPROPERTY() TArray<TObjectPtr<UTexture2D>> Frames;
	/** tamamlanan kare sayısı hedefi (SpinFrameCount) */
	int32 Want = 0;
};

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
	/** Dönen küçük görüntüler (hangar dron listesi, mürettebat pilot listesi, pilot rozeti, TOP10): konu bir kez, ayrı bir noktada
	 *  kendi ışıklarıyla (2. ışık kanalı) 24 açıdan çekilir, arka plan saydam (SceneColorHDR ters örtme alfası) → UTexture2D kareleri.
	 *  Sonra yalnız kareler oynatılır (her karede sahne yakalaması yok). Anahtar döner; kareler SpinFrames ile okunur. */
	FString RequestDroneSpin(const FString& Type, const FString& ModelKey, const FString& ColorHex);
	FString RequestAvatarSpin(const FSmvAvatar& A);
	/** nullptr: anahtar bilinmiyor; Frames.Num() < Want: çekim sürüyor */
	const FSmvSpinSet* SpinFrames(const FString& Key) const { return Spins.Find(Key); }
	static constexpr int32 SpinFrameCount = 24;
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
	/** Harita (yeniden) yüklemesinin hazırlık kapısı: harita akışı, atmosfer ilk karesi, gölgelendirici/PSO, doku akışı, ön ısınma.
	 *  Açılış logosu ve harita turu yükleme katmanı bu kapı bitene dek sahneyi örter. */
	const FSmvLoadGate& GetLoadGate() const { return Gate; }
	bool IsSceneReady() const { return !bLive || !Gate.IsActive() || Gate.IsDone(); }
	/** Parkur kartı: canlı dünyadan ikinci görünüm (harita içinde alçak yörünge; 640×360) — Karşılaşma kartının "video döngüsü" */
	UTextureRenderTarget2D* GetCardTarget();
	void SetCardActive(bool bOn);
	// ---- menü arka plan videosu: Menu istasyonunun sinematik döngüsü bir kez kaydedilir (Content/Movies/SmvMenu.mp4, 84 sn kusursuz döngü;
	//      .github/workflows/menu-video.yml) ve ana menüde canlı dünya yerine oynatılır (dünya çizimi kapanır — her seferinde hesaplama yok).
	//      Dosya yoksa / [/Script/Simumav.SmvMenuScene] MenuVideo=False ise canlı sahne. ----
	/** Oynayan video dokusu (yoksa nullptr) */
	class UTexture* GetMenuVideo() const;
	bool IsMenuVideoPlaying() const;
	/** Kayıt kipi (-SmvRecordMenu): sahne saati sıfırlanır; döngü süresi (sn) */
	void BeginRecord();
	static constexpr double LoopSeconds = 84.0;

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
	/** Fab haritasının alt seviyeleri (kalıcı seviyenin akış listesi: Modern City blokları, US_Military ışık/dekor) — seviye örneği
	 *  olarak yüklenen dünyanın alt seviyeleri kendiliğinden gelmez (uçuştaki ASmvGameMode::HandleFabLevelShown ile aynı) */
	UPROPERTY() TArray<TObjectPtr<class ULevelStreamingDynamic>> MenuSubs;
	bool bMenuSubsReq = false;
	double MenuSubsAt = 0;
	TArray<ULevel*> MenuLevels() const;
	void DropMenuLevels();
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
	UPROPERTY() TObjectPtr<class UMediaPlayer> BgPlayer;
	UPROPERTY() TObjectPtr<class UMediaTexture> BgTex;
	UPROPERTY() TObjectPtr<class UFileMediaSource> BgSrc;
	void StartMenuVideo();
	UPROPERTY() TMap<FString, FSmvSpinSet> Spins;
	UPROPERTY() TObjectPtr<AActor> SpinSubject;
	UPROPERTY() TObjectPtr<ASmvPilotFigure> SpinFig;
	UPROPERTY() TObjectPtr<UTextureRenderTarget2D> SpinRtL;
	UPROPERTY() TObjectPtr<UTextureRenderTarget2D> SpinRtH;
	UPROPERTY() TArray<TObjectPtr<UPointLightComponent>> SpinLights;
	UPROPERTY() TObjectPtr<ASmvProcTerrain> WorldPrev;
	/** Dünya önizlemesinin kendi güneşi (1. ışık kanalı) ve gök kartı (yalnız yakalamada görünür) */
	UPROPERTY() TObjectPtr<class UDirectionalLightComponent> PrevSun;
	UPROPERTY() TObjectPtr<class UStaticMeshComponent> SkyCard;
	UPROPERTY() TObjectPtr<ASmvDroneActor> Showcase;
	UPROPERTY() TObjectPtr<USceneCaptureComponent2D> CardCap;
	UPROPERTY() TObjectPtr<UTextureRenderTarget2D> CardRT;
	UPROPERTY() TObjectPtr<ASmvPilotFigure> ShowPilot;

private:
	FSmvLoadGate Gate;
	int32 GateCount = 0;
	double GAtmosT0 = 0, GWarmT0 = 0, GStreamT0 = 0;
	uint64 GAtmosF0 = 0, GWarmF0 = 0;
	int32 GStreamPeak = 0, GShaderPeak = 0, GWarmRecap = 0;
	void TickGate();
	/** Fab haritası içeriği orijinde değilse sahne merkezini (FabStartE/N) içeriğe kaydırır */
	void RecenterFabMap();
	bool bLive = false;
	bool bPreviewOn = false;
	double T0 = 0;
	/** sahne saati (Dt birikimi) ve yumuşatılmış uçuş irtifası */
	double SceneT = 0, FlyZ = 0, FlyVz = 0;
	bool bFlyZ = false;
	FVector LookS = FVector::ZeroVector;
	/** harita turu ölçeği (saha çitli haritalarda kamera saha içinde kalır): yörünge yarıçapı / irtifa (m) */
	double TourR = 280.0, TourAlt = 95.0;
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
	struct FSpinJob { FString Key, Type, Model, Color; FSmvAvatar Avatar; bool bAvatar = false; };
	TArray<FSpinJob> SpinQueue;
	FSpinJob SpinJob;
	int32 SpinTick = 0;
	/** GLB gövdesi yüklendiği kare (−1 bekleniyor) */
	int32 SpinGlbTick = -1;
	bool bSpinHasModel = false, bSpinRead = false, bSpinFramed = false;
	FVector SpinAt = FVector::ZeroVector, SpinLocalOff = FVector::ZeroVector;
	float SpinDist = 300.f;
	double LandAt = -100.0;
	double WpLat = 0, WpLon = 0, WpSize = 4000;
	void TickSpin();
	void EndSpinJob();
	void EnsureSpinLights();

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
	double ClearGrassAt = 0.0;
	void ClearShowcaseSpot();
	void ClearShowcaseGrass();
	FVector ShowGround();
	void UpdateStationCamera(float Dt);
	void SettleShowcase();
	void LoadMenuMap(const FString& Key);
	void BuildWorld();
	FVector EnuToWorld(double E, double N, double Agl) const;
	void ApplyPreviewChannels();
	void UpdateCamera(float Dt, const FVector& DronePos, const FVector& DroneFwd);
};
