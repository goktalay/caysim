// Simumav oyun kipi — arayüz kabuğunu (SSmvShell) açar; akış Uçuş'a geçince sahneyi kurar: dünya (Cesium gerçek arazi / prosedürel),
// dron, pilot piyonu, HUD, halkalar/kapılar, hedefler, yerel yarış botları ya da çevrim içi rakipler, uçuş uyarıları, ses; bağlantı
// olaylarını dağıtır; çarpışmayı 3B dünyadan algılar (bina/su kesin, arazi aday — sunucu kuralı).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CaySimTypes.h"
#include "SimumavTypes.h"
#include "SmvMatchClient.h"
#include "SmvFlow.h"
#include "SmvVideoRec.h"
#include "SSmvFlightUi.h"
#include "SmvGameMode.generated.h"

class ACaySimWorldSetup;
class ASmvDroneActor;
class ASmvGateActor;
class ASmvProcTerrain;
class ASmvPilotPawn;
class ULevelStreamingDynamic;
class UCaySimHudWidget;
class USmvHudOverlay;
class USmvFlow;
class USmvFlightLink;
class USmvToneSynth;
class USmvMusicSynth;
class USoundBase;
class ASmvMenuScene;
class ASmvFpvCourseActor;
class ASmvPilotFigure;
class SSmvShell;
class SWidget;
namespace SmvCore { class FGpws; class FRingTracker; class FDogfight; class FBotPilot; struct FRing; }

UCLASS()
class SIMUMAV_API ASmvGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASmvGameMode();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

	// ---- duraklatma menüsü / kabuk eylemleri ----
	void TogglePause();
	bool IsPausedMenu() const { return bPauseMenu; }
	void Command(const FString& Name, TSharedPtr<FJsonObject> Params = nullptr);
	ASmvPilotPawn* GetPilot() const;
	/** Uçuş olay akışına bildirim (arayüzden) */
	void UiEvent(const FString& Text, int32 Sev) { PushEvent(Text, Sev); }
	FString MissionPlannerLine() const;
	/** Uçuş arayüzü düğmeleri (SSmvFlightUi): Mode{arg}, CamCycle, IntroBeep{count|go}, Help ya da pilot eylem adları (ArmToggle, Takeoff, Rtl, Land, Reset, Pause …). */
	void UiAction(const FString& Name, const FString& Arg = FString());
	/** Menü 3B sahnesi (uçuşta yok) */
	ASmvMenuScene* GetMenuScene() const { return MenuScene; }
	/** Arayüz tercihlerini uygula: ses mikseri (arayüz / müzik / uyarı / motor), kumanda modu, tuş atamaları, hareket azaltma */
	void ApplyUiPrefs();

protected:
	UPROPERTY() TObjectPtr<ACaySimWorldSetup> WorldSetup;
	UPROPERTY() TObjectPtr<ASmvProcTerrain> Terrain;
	/** Fab haritası (parkur dünya kipi "fab:<anahtar>") — [0] kalıcı seviye, ardından onun alt seviyeleri; hepsi görünür olunca
	 *  merkezi orijine (SITL ev konumu) oturtulur */
	UPROPERTY() TArray<TObjectPtr<ULevelStreamingDynamic>> FabLevels;
	bool bFabSubsRequested = false;
	bool bFabPlaced = false;
	UFUNCTION() void HandleFabLevelShown();
	/** Fab Ultra Dynamic Sky aktörleri (düz dünyada yerleşik gök yerine) ve sahne dekoru (yalnız görsel statik ağlar) */
	UPROPERTY() TArray<TObjectPtr<AActor>> FabSkyActors;
	UPROPERTY() TArray<TObjectPtr<AActor>> FabDecor;
	/** Kodla kurulan şehirde yüklenen City Sample bina seviyeleri (yerleri baştan verilir; merkezleme yok) */
	UPROPERTY() TArray<TObjectPtr<ULevelStreamingDynamic>> CityLevels;
	/** Uçulan harita anahtarı (kayıt tablosu; başlangıç kaydırması / dekor için) */
	FString FabMapKey;
	/** Sahne dekoru: Origin (cm) çevresine; bTraceGround ise her modelin zemini aşağı ışınla bulunur (Fab haritası) */
	void SpawnFabDecor(const FVector& Origin = FVector::ZeroVector, bool bTraceGround = false);
	void PlaceFabLevels();
	/** Kodla kurulan şehir bloğu (European Buildings ağları + City Sample bina seviyeleri) */
	void SpawnFabCity();
	/** Fab/şehir aktörlerini ve seviyelerini kaldırır (uçuş sonu / yeni uçuş) */
	void ClearFabWorld();
	UPROPERTY() TObjectPtr<ASmvDroneActor> Drone;
	UPROPERTY() TArray<TObjectPtr<ASmvGateActor>> Gates;
	UPROPERTY() TArray<TObjectPtr<ASmvDroneActor>> Ghosts;       // botlar / rakipler
	UPROPERTY() TArray<TObjectPtr<ASmvDroneActor>> TargetActors; // hedefler
	UPROPERTY() TObjectPtr<UCaySimHudWidget> Hud;
	UPROPERTY() TObjectPtr<USmvHudOverlay> Overlay;
	UPROPERTY() TObjectPtr<USmvToneSynth> Tones;
	UPROPERTY() TObjectPtr<AActor> ToneHost;
	/** Arayüz sesleri (ayrı kazanç) ve menü müziği (sentez) */
	UPROPERTY() TObjectPtr<USmvToneSynth> UiTones;
	UPROPERTY() TObjectPtr<USmvMusicSynth> Music;
	UPROPERTY() TObjectPtr<ASmvMenuScene> MenuScene;
	/** Arayüz sesleri: Fab Airplane Controller & Instruments düğme/anahtar sesleri pakette varsa onlar (tık, aç/kapa, açılış),
	 *  yoksa / diğerleri için prosedürel ton */
	UPROPERTY() TMap<int32, TObjectPtr<USoundBase>> UiSounds;
	bool bUiSoundsLoaded = false;
	void PlayUiSound(int32 Kind);
	void UpdateMenuScene(ESmvScreen Screen);
	void UpdateMusic(ESmvScreen Screen);

private:
	TWeakObjectPtr<USmvFlow> Flow;
	TSharedPtr<SSmvShell> Shell;
	TSharedPtr<SSmvFlightUi> FlightUi;
	TSharedPtr<SWidget> FlightUiHost;
	TArray<FSmvUiLog> UiLog;
	double ArmConfirmUntil = 0;
	bool bScene = false;
	bool bPauseMenu = false;
	bool bCrashed = false;
	double SceneT0 = 0;
	FVector LastDronePos = FVector::ZeroVector;
	bool bHasLastPos = false;
	FSmvFlightRequest Req;

	// uyarılar
	SmvCore::FGpws* Gpws = nullptr;
	TArray<TSharedPtr<FJsonObject>> PendingGpws;
	double LastGpwsFlush = 0;
	bool bAlertUnacked = false;
	TArray<TPair<FString, bool>> ActiveAlerts;

	// halkalar (parkur kapıları / yerel yarış / çevrim içi karşılaşma)
	TArray<SmvCore::FRing>* Rings = nullptr;
	SmvCore::FRingTracker* MyTracker = nullptr;
	double RaceStartWall = 0;
	bool bRaceStarted = false;
	bool bRaceDone = false;
	double MyFinishT = -1;
	/** Hazır FPV parkuru (zaman denemesi) */
	UPROPERTY() TObjectPtr<ASmvFpvCourseActor> FpvActor;
	double FpvEndAt = 0;
	/** Sahnede RC kumanda tutan 3B pilot (ev konumunun 8 m gerisinde, 3 m solunda) */
	UPROPERTY() TObjectPtr<ASmvPilotFigure> PilotFigure;
	/** Çarpma efekti: enkaz parçaları (elle balistik) */
	UPROPERTY() TArray<TObjectPtr<AActor>> Debris;
	TArray<FVector> DebrisVel;
	double DebrisGroundZ = 0, DebrisUntil = 0;
	void CrashFx();
	UPROPERTY() TObjectPtr<class ASmvSiteActor> Site;
	UPROPERTY() TObjectPtr<class ASmvWindSock> Sock;
	UPROPERTY() TObjectPtr<class ASmvHolidayDecor> Decor;
	bool bRadarOn = true, bEoOn = true;
	int32 EoSel = 0;          // 0 = kendi İHA, i>0 = hedef i
	int32 EoTypeIdx = 0;      // 0 gündüz, 1 termal, 2 gece
	uint32 EoFrame = 0;
	void UpdateSite(struct FSmvFlightUiState& U);
	double PathAt = 0, PathT0 = 0;
	TUniquePtr<FSmvVideoRec> VideoRec;
public:
	/** Video kaydını aç/kapat (uçuş arayüzü düğmesi) */
	void ToggleVideo();
	/** Radar ekranında temas seç (EO izler; hedefse kilit) */
	void RadarPick(int32 Id);
	void CycleEoType();
	/** Uçuşta onay diyaloğu (kabuk katmanı) */
	void ConfirmUi(const FText& Title, const FText& Body, const FText& Yes, bool bDanger, TFunction<void()> OnYes);
	bool IsRecording() const;
	double RecSeconds() const;
private:
	void TickDebris(float Dt);
	void UpdateFpv(const FCaySimTelemetry& T, double Now);
	FString RaceResultBadge;
	// yerel botlar
	struct FBot { SmvCore::FBotPilot* Pilot = nullptr; SmvCore::FRingTracker* Tracker = nullptr; FString Name; double FinishT = -1; };
	TArray<FBot> Bots;
	SmvCore::FDogfight* Dogfight = nullptr;
	bool bPlayerChaser = true;
	double GroundMsl = 0;
	// çevrim içi rakipler
	TMap<int64, int32> GhostIndex;

	// olay şeridi + mesaj
	TArray<TPair<FString, int32>> Events;
	FString ToastText; int32 ToastSev = 6; double ToastUntil = 0;
	double EndAt = 0; FString EndReasonPending;

	UFUNCTION() void HandleScreen(ESmvScreen Screen);
	UFUNCTION() void HandleToast(const FString& Text, int32 Severity);
	void BuildScene();
	void StopScene();
	void SetupRace();
	void HandleTele(const FCaySimTelemetry& T, const FSmvTeleExtra& X);
	void HandleMessage(const TSharedPtr<FJsonObject>& M);
	void HandleAction(FName Action);
	void HandleMatchPositions(const TArray<FSmvGhostPos>& P);
	void UpdateGpws(const FCaySimTelemetry& T, const FSmvTeleExtra& X);
	void UpdateRace(float Dt);
	void UpdateTargets();
	void UpdateHud();
	void DetectCollision();
	void AugmentSummary(FSmvFlightSummary& S);
	void PushEvent(const FString& Text, int32 Sev);
	void FreeRace();
	USmvFlightLink* Link() const;
	FVector Geo(double Lat, double Lon, double AltMsl) const;
	ASmvDroneActor* SpawnGhost(const FString& Type, const FLinearColor& Color);
	void PlaceRingActors();
};
