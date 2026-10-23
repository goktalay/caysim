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
#include "SSmvFlightUi.h"
#include "SmvGameMode.generated.h"

class ACaySimWorldSetup;
class ASmvDroneActor;
class ASmvGateActor;
class ASmvProcTerrain;
class ASmvPilotPawn;
class UCaySimHudWidget;
class USmvHudOverlay;
class USmvFlow;
class USmvFlightLink;
class USmvToneSynth;
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
	FString MissionPlannerLine() const;
	/** Uçuş arayüzü düğmeleri (SSmvFlightUi): Mode{arg}, CamCycle ya da pilot eylem adları (ArmToggle, Takeoff, Rtl, Land, Reset, Pause …). */
	void UiAction(const FString& Name, const FString& Arg = FString());

protected:
	UPROPERTY() TObjectPtr<ACaySimWorldSetup> WorldSetup;
	UPROPERTY() TObjectPtr<ASmvProcTerrain> Terrain;
	UPROPERTY() TObjectPtr<ASmvDroneActor> Drone;
	UPROPERTY() TArray<TObjectPtr<ASmvGateActor>> Gates;
	UPROPERTY() TArray<TObjectPtr<ASmvDroneActor>> Ghosts;       // botlar / rakipler
	UPROPERTY() TArray<TObjectPtr<ASmvDroneActor>> TargetActors; // hedefler
	UPROPERTY() TObjectPtr<UCaySimHudWidget> Hud;
	UPROPERTY() TObjectPtr<USmvHudOverlay> Overlay;
	UPROPERTY() TObjectPtr<USmvToneSynth> Tones;
	UPROPERTY() TObjectPtr<AActor> ToneHost;

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
