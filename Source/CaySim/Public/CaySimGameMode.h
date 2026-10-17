// Oyun kipi — oturum sahnesini kurar: dünya, dron, kalkış donanımı, kamera, HUD; WS olaylarını dağıtır
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CaySimTypes.h"
#include "SessionWizard.h"
#include "ReplayPlayer.h"
#include "CaySimGameMode.generated.h"

class ACaySimDroneActor;
class ACaySimWorldSetup;
class ACaySimLaunchRig;
class UCaySimHudWidget;
class UHepgoClient;
class ACaySimCameraPawn;

UCLASS()
class CAYSIM_API ACaySimGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACaySimGameMode();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	/** Sihirbaz Running'e geçince çağrılır: sahneyi kurar. */
	UFUNCTION(BlueprintCallable, Category = "CAYSIM") void StartScene();
	UFUNCTION(BlueprintCallable, Category = "CAYSIM") void StopScene();

	UFUNCTION(BlueprintPure, Category = "CAYSIM") ACaySimDroneActor* GetDrone() const { return Drone; }
	UFUNCTION(BlueprintPure, Category = "CAYSIM") ACaySimWorldSetup* GetWorldSetup() const { return WorldSetup; }
	UFUNCTION(BlueprintPure, Category = "CAYSIM") ACaySimLaunchRig* GetLaunchRig() const { return LaunchRig; }
	UFUNCTION(BlueprintPure, Category = "CAYSIM") UCaySimHudWidget* GetHud() const { return Hud; }
	UFUNCTION(BlueprintPure, Category = "CAYSIM") bool IsSceneRunning() const { return bSceneRunning; }

	/** Blueprint alt sınıfında atanır (BP_Drone, BP_WorldSetup, BP_LaunchRig, WBP_Hud, WBP_Wizard). */
	UPROPERTY(EditDefaultsOnly, Category = "CAYSIM|Sınıflar") TSubclassOf<ACaySimDroneActor> DroneClass;
	UPROPERTY(EditDefaultsOnly, Category = "CAYSIM|Sınıflar") TSubclassOf<ACaySimWorldSetup> WorldSetupClass;
	UPROPERTY(EditDefaultsOnly, Category = "CAYSIM|Sınıflar") TSubclassOf<ACaySimLaunchRig> LaunchRigClass;
	UPROPERTY(EditDefaultsOnly, Category = "CAYSIM|Sınıflar") TSubclassOf<UCaySimHudWidget> HudClass;
	UPROPERTY(EditDefaultsOnly, Category = "CAYSIM|Sınıflar") TSubclassOf<UUserWidget> WizardWidgetClass;

protected:
	UPROPERTY() TObjectPtr<ACaySimDroneActor> Drone;
	UPROPERTY() TObjectPtr<ACaySimWorldSetup> WorldSetup;
	UPROPERTY() TObjectPtr<ACaySimLaunchRig> LaunchRig;
	UPROPERTY() TObjectPtr<UCaySimHudWidget> Hud;
	UPROPERTY() TObjectPtr<UUserWidget> WizardWidget;

private:
	bool bSceneRunning = false;
	bool bGcsEverConnected = false;
	double SessionT0 = 0.0;
	TWeakObjectPtr<UHepgoClient> Hepgo;
	TWeakObjectPtr<UCaySimSessionWizard> Wizard;
	TWeakObjectPtr<UCaySimReplayPlayer> Replay;

	UFUNCTION() void HandleStep(ECaySimWizardStep Step);
	UFUNCTION() void HandleTelemetry(const FCaySimTelemetry& Tele);
	UFUNCTION() void HandleLaunch(const FCaySimLaunchEvent& Launch);
	UFUNCTION() void HandleEvent(const FCaySimEvent& Event);
	UFUNCTION() void HandleEnd(const FString& Reason);
	UFUNCTION() void HandleWorldReady(bool bOk);
	UFUNCTION() void HandleReplayLoaded(bool bOk, const FCaySimReplayHeader& Header);
	UFUNCTION() void HandleReplayControl(const FString& Action);
	void UpdateHud();
};
