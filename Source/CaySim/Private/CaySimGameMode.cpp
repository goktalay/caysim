#include "CaySimGameMode.h"
#include "CaySim.h"
#include "CaySimGameInstance.h"
#include "CaySimCameraPawn.h"
#include "HepgoClient.h"
#include "SessionWizard.h"
#include "ReplayPlayer.h"
#include "DroneActor.h"
#include "CesiumWorldSetup.h"
#include "LaunchRig.h"
#include "HudWidget.h"
#include "Blueprint/UserWidget.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"

ACaySimGameMode::ACaySimGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	DefaultPawnClass = ACaySimCameraPawn::StaticClass();
	DroneClass = ACaySimDroneActor::StaticClass();
	WorldSetupClass = ACaySimWorldSetup::StaticClass();
	LaunchRigClass = ACaySimLaunchRig::StaticClass();
	HudClass = UCaySimHudWidget::StaticClass();
}

void ACaySimGameMode::BeginPlay()
{
	Super::BeginPlay();
	UCaySimGameInstance* GI = UCaySimGameInstance::Get(this);
	if (!GI) return;
	Hepgo = GI->GetHepgo();
	Wizard = GI->GetWizard();
	Replay = GI->GetReplay();

	if (Wizard.IsValid()) Wizard->OnStepChanged.AddDynamic(this, &ACaySimGameMode::HandleStep);
	if (Hepgo.IsValid())
	{
		Hepgo->OnTelemetry.AddDynamic(this, &ACaySimGameMode::HandleTelemetry);
		Hepgo->OnLaunch.AddDynamic(this, &ACaySimGameMode::HandleLaunch);
		Hepgo->OnEvent.AddDynamic(this, &ACaySimGameMode::HandleEvent);
		Hepgo->OnEnd.AddDynamic(this, &ACaySimGameMode::HandleEnd);
		Hepgo->OnReplayControl.AddDynamic(this, &ACaySimGameMode::HandleReplayControl);
	}
	if (Replay.IsValid()) Replay->OnLoaded.AddDynamic(this, &ACaySimGameMode::HandleReplayLoaded);

	// Sihirbaz arayüzü
	if (WizardWidgetClass)
	{
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
		{
			WizardWidget = CreateWidget<UUserWidget>(PC, WizardWidgetClass);
			if (WizardWidget) WizardWidget->AddToViewport(10);
			PC->bShowMouseCursor = true;
		}
	}
	// Kayıtlı istemci kodu varsa doğrudan bağlan
	if (Wizard.IsValid() && !Wizard->TrySavedClientCode())
	{
		UE_LOG(LogCaySim, Log, TEXT("Kayıtlı istemci kodu yok; sihirbaz kod adımında"));
	}
}

void ACaySimGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
	StopScene();
	if (Wizard.IsValid()) Wizard->OnStepChanged.RemoveAll(this);
	if (Hepgo.IsValid())
	{
		Hepgo->OnTelemetry.RemoveAll(this);
		Hepgo->OnLaunch.RemoveAll(this);
		Hepgo->OnEvent.RemoveAll(this);
		Hepgo->OnEnd.RemoveAll(this);
		Hepgo->OnReplayControl.RemoveAll(this);
	}
	if (Replay.IsValid()) Replay->OnLoaded.RemoveAll(this);
	Super::EndPlay(Reason);
}

void ACaySimGameMode::HandleStep(ECaySimWizardStep Step)
{
	if (Step == ECaySimWizardStep::Running && !bSceneRunning) StartScene();
	else if (Step != ECaySimWizardStep::Running && bSceneRunning) StopScene();
	if (WizardWidget) WizardWidget->SetVisibility(Step == ECaySimWizardStep::Running ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0)) PC->bShowMouseCursor = (Step != ECaySimWizardStep::Running);
}

void ACaySimGameMode::StartScene()
{
	if (!Wizard.IsValid() || !Hepgo.IsValid()) return;
	const FCaySimSession S = Wizard->GetSession();
	UWorld* W = GetWorld();
	bSceneRunning = true;
	bGcsEverConnected = false;
	SessionT0 = Hepgo->NowServer();

	// Dünya
	if (!WorldSetup) WorldSetup = W->SpawnActor<ACaySimWorldSetup>(WorldSetupClass ? *WorldSetupClass : ACaySimWorldSetup::StaticClass());
	WorldSetup->OnWorldReady.RemoveAll(this);
	WorldSetup->OnWorldReady.AddDynamic(this, &ACaySimGameMode::HandleWorldReady);
	WorldSetup->Build(S, Hepgo.Get(), Wizard->GetWorldSource());

	// Dron
	if (!Drone) Drone = W->SpawnActor<ACaySimDroneActor>(DroneClass ? *DroneClass : ACaySimDroneActor::StaticClass());
	Drone->SetGeoTransformActor(WorldSetup);
	Drone->Setup(S, Hepgo.Get());

	// Kalkış donanımı (yalnız canlı kip; dikey kalkışta görünmez)
	if (S.Mode == ECaySimSessionMode::Sitl)
	{
		if (!LaunchRig) LaunchRig = W->SpawnActor<ACaySimLaunchRig>(LaunchRigClass ? *LaunchRigClass : ACaySimLaunchRig::StaticClass());
		const double Ground = S.Center.bHasAlt ? S.Center.Alt : 0.0;
		LaunchRig->Setup(Wizard->GetLaunch(), WorldSetup, Drone, S.Center.Lat, S.Center.Lon, Ground);
		LaunchRig->SetActorHiddenInGame(Wizard->GetLaunch().Method == ECaySimLaunchMethod::Vertical);
		if (S.Launch.bValid) LaunchRig->OnLaunchEvent(S.Launch); // yanıttaki `session.launch` (yöntem/açı/hız/evre)
	}
	else if (Replay.IsValid())
	{
		Replay->Load(Hepgo.Get(), S.Id);
	}

	// Kamera
	if (ACaySimCameraPawn* Cam = Cast<ACaySimCameraPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
	{
		Cam->SetTarget(Drone);
		Cam->SetMode(ECaySimCameraMode::Follow);
	}

	// HUD
	if (!Hud && HudClass)
	{
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
		{
			Hud = CreateWidget<UCaySimHudWidget>(PC, HudClass);
			if (Hud) Hud->AddToViewport(5);
		}
	}
	if (Hud) Hud->SetVisibility(ESlateVisibility::HitTestInvisible);
	UE_LOG(LogCaySim, Log, TEXT("Sahne kuruldu: oturum %s (%s)"), *S.Id, S.Mode == ECaySimSessionMode::Replay ? TEXT("kayıt") : TEXT("canlı"));
}

void ACaySimGameMode::StopScene()
{
	if (!bSceneRunning) return;
	bSceneRunning = false;
	if (Hud) Hud->SetVisibility(ESlateVisibility::Collapsed);
	if (Replay.IsValid()) Replay->Pause();
	if (LaunchRig) LaunchRig->SetActorHiddenInGame(true);
}

void ACaySimGameMode::HandleWorldReady(bool bOk)
{
	UE_LOG(LogCaySim, Log, TEXT("Dünya hazır: %s"), bOk ? TEXT("evet") : TEXT("hayır (düz zemin)"));
	if (LaunchRig && Drone) LaunchRig->Setup(LaunchRig->GetConfig(), WorldSetup, Drone, Wizard->GetSession().Center.Lat, Wizard->GetSession().Center.Lon, WorldSetup->GetCenterGroundMsl());
}

void ACaySimGameMode::HandleTelemetry(const FCaySimTelemetry& Tele)
{
	if (!bSceneRunning || !Drone) return;
	Drone->PushTelemetry(Tele);
	if (LaunchRig) LaunchRig->OnTelemetry(Tele);
	if (Tele.bGcsConnected) bGcsEverConnected = true;
}

void ACaySimGameMode::HandleLaunch(const FCaySimLaunchEvent& Launch)
{
	if (LaunchRig) LaunchRig->OnLaunchEvent(Launch);
}

void ACaySimGameMode::HandleEvent(const FCaySimEvent& Event)
{
	UE_LOG(LogCaySim, Log, TEXT("[olay %d] %s"), Event.Severity, *Event.Text);
}

void ACaySimGameMode::HandleEnd(const FString& Reason)
{
	UE_LOG(LogCaySim, Log, TEXT("Oturum bitti: %s"), *Reason);
}

void ACaySimGameMode::HandleReplayLoaded(bool bOk, const FCaySimReplayHeader& Header)
{
	if (!bOk) UE_LOG(LogCaySim, Warning, TEXT("Kayıt yüklenemedi"));
}

void ACaySimGameMode::HandleReplayControl(const FString& Action)
{
	if (Action == TEXT("reset") && Replay.IsValid()) Replay->Restart();
}

void ACaySimGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bSceneRunning) return;
	if (Replay.IsValid() && Replay->IsLoaded() && Drone)
	{
		Replay->Tick(DeltaSeconds);
		Drone->ApplyPoseDirect(Replay->Sample());
	}
	UpdateHud();
}

void ACaySimGameMode::UpdateHud()
{
	if (!Hud || !Drone) return;
	const FCaySimSession S = Wizard.IsValid() ? Wizard->GetSession() : FCaySimSession();
	FCaySimHudContext Ctx;
	Drone->GetRenderedPose(Ctx.RollRad, Ctx.PitchRad, Ctx.YawRad);
	Ctx.bReplay = (S.Mode == ECaySimSessionMode::Replay);
	Ctx.bSitlMode = !Ctx.bReplay;
	Ctx.bGcsEverConnected = bGcsEverConnected;
	Ctx.WindMs = S.World.WindMs;
	Ctx.WindDir = S.World.WindDir;
	Ctx.T0 = Ctx.bReplay && Replay.IsValid() ? Replay->GetHeader().StartUtc : SessionT0;
	Ctx.WallClock = Ctx.bReplay && Replay.IsValid() ? Replay->GetWallClock() : 0.0;
	Ctx.LatencyMs = Hepgo.IsValid() ? Hepgo->GetRttMs() : 0.f;
	if (ACaySimCameraPawn* Cam = Cast<ACaySimCameraPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
	{
		Ctx.GimbalDeg = Cam->GetGimbalPitch();
		Ctx.FovDeg = Cam->GetFov();
		Hud->SetVisibility(Cam->GetMode() == ECaySimCameraMode::Fpv && Cam->IsHudEnabled() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	Hud->Update(Drone->GetLatestTelemetry());
	Hud->SetContext(Ctx);
}
