#include "SmvGameMode.h"
#include "SimumavModule.h"
#include "SmvGameInstance.h"
#include "SmvFlow.h"
#include "SmvFlightLink.h"
#include "SmvCloudFlightLink.h"
#include "SmvDataService.h"
#include "SimumavClient.h"
#include "SmvPilotPawn.h"
#include "SmvWorldActors.h"
#include "SmvHudOverlay.h"
#include "SmvToneSynth.h"
#include "SmvMotorSynth.h"
#include "SmvI18n.h"
#include "SSmvShell.h"
#include "SSmvFlightUi.h"
#include "CesiumWorldSetup.h"
#include "HudWidget.h"
#include "TelemetryBuffer.h"
#include "SmvGpws.h"
#include "SmvRace.h"
#include "SmvBot.h"
#include "SmvTargets.h"
#include "SmvSitlConfig.h"
#include "SmvFabContent.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/Level.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Blueprint/UserWidget.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Widgets/SWeakWidget.h"
#include "Misc/DateTime.h"

ASmvGameMode::ASmvGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	DefaultPawnClass = ASmvPilotPawn::StaticClass();
}

USmvFlightLink* ASmvGameMode::Link() const { return Flow.IsValid() ? Flow->GetLink() : nullptr; }

ASmvPilotPawn* ASmvGameMode::GetPilot() const
{
	return Cast<ASmvPilotPawn>(UGameplayStatics::GetPlayerPawn(this, 0));
}

FString ASmvGameMode::MissionPlannerLine() const
{
	return Link() ? Link()->MissionPlannerInfo() : FString();
}

// ---------------------------------------------------------------------------
// yaşam döngüsü
// ---------------------------------------------------------------------------

void ASmvGameMode::BeginPlay()
{
	Super::BeginPlay();
	USmvGameInstance* GI = USmvGameInstance::GetSmv(this);
	if (!GI) { UE_LOG(LogSimumav, Error, TEXT("SmvGameInstance yok — DefaultEngine.ini GameInstanceClass")); return; }
	Flow = GI->GetFlow();
	Flow->OnScreenChanged.AddDynamic(this, &ASmvGameMode::HandleScreen);
	Flow->OnToast.AddDynamic(this, &ASmvGameMode::HandleToast);
	Flow->SummaryAugmenter = [this](FSmvFlightSummary& S) { AugmentSummary(S); };

	if (GEngine && GEngine->GameViewport)
	{
		Shell = SNew(SSmvShell).Flow(Flow.Get()).GameMode(this);
		GEngine->GameViewport->AddViewportWidgetContent(SNew(SWeakWidget).Visibility(EVisibility::SelfHitTestInvisible).PossiblyNullContent(Shell.ToSharedRef()), 50);
	}
	ToneHost = GetWorld()->SpawnActor<AActor>();
	if (ToneHost)
	{
		USceneComponent* R = NewObject<USceneComponent>(ToneHost, TEXT("Root"));
		ToneHost->SetRootComponent(R);
		R->RegisterComponent();
		Tones = NewObject<USmvToneSynth>(ToneHost, TEXT("Tones"));
		Tones->SetupAttachment(R);
		Tones->RegisterComponent();
		Tones->Start();
	}
	HandleScreen(Flow->GetScreen());
}

void ASmvGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
	StopScene();
	if (Flow.IsValid())
	{
		Flow->OnScreenChanged.RemoveAll(this);
		Flow->OnToast.RemoveAll(this);
		Flow->SummaryAugmenter = nullptr;
	}
	if (Shell.IsValid() && GEngine && GEngine->GameViewport) GEngine->GameViewport->RemoveAllViewportWidgets();
	Shell.Reset();
	Super::EndPlay(Reason);
}

void ASmvGameMode::HandleToast(const FString& Text, int32 Severity)
{
	ToastText = Text; ToastSev = Severity; ToastUntil = FPlatformTime::Seconds() + 5.0;
	if (Shell.IsValid()) Shell->ShowToast(Text, Severity);
}

void ASmvGameMode::HandleScreen(ESmvScreen Screen)
{
	const bool bFlightScreens = Screen == ESmvScreen::Loading || Screen == ESmvScreen::Flight;
	if (bFlightScreens && !bScene) BuildScene();
	if (!bFlightScreens && bScene) StopScene();
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		// uçuşta da imleç görünür (alt bant düğmeleri, sanal çubuklar); klavye odağı oyun görünümünde kalır,
		// 3B görünümde fareyle sürükleme kamerayı döndürür (yakalama yalnız tuş basılıyken)
		PC->bShowMouseCursor = true;
		FInputModeGameAndUI M;
		M.SetHideCursorDuringCapture(false);
		M.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(M);
	}
	if (ASmvPilotPawn* P = GetPilot()) P->SetFlying(Screen == ESmvScreen::Flight);
	if (Shell.IsValid()) Shell->Refresh();
}

// ---------------------------------------------------------------------------
// sahne
// ---------------------------------------------------------------------------

FVector ASmvGameMode::Geo(double Lat, double Lon, double AltMsl) const
{
	return WorldSetup ? WorldSetup->GeoToUnreal(Lon, Lat, WorldSetup->MslToWorldHeight(Lat, Lon, AltMsl)) : FVector::ZeroVector;
}

static ECaySimDroneType SmvCaysimType(const FString& T)
{
	if (T == TEXT("plane")) return ECaySimDroneType::FixedWing;
	if (T == TEXT("vtol") || T == TEXT("interceptor")) return ECaySimDroneType::Vtol;
	if (T == TEXT("heli")) return ECaySimDroneType::Helicopter;
	return ECaySimDroneType::Multirotor;
}

static FLinearColor SmvColor(const FString& Hex)
{
	if (Hex.Len() == 7 && Hex[0] == TEXT('#')) return FLinearColor::FromSRGBColor(FColor::FromHex(Hex));
	return FLinearColor(0.8f, 0.1f, 0.1f);
}

ASmvDroneActor* ASmvGameMode::SpawnGhost(const FString& Type, const FLinearColor& Color)
{
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASmvDroneActor* G = GetWorld()->SpawnActor<ASmvDroneActor>(ASmvDroneActor::StaticClass(), FTransform::Identity, P);
	if (!G) return nullptr;
	G->SetGeoTransformActor(WorldSetup);
	FCaySimSession S;
	S.Type = SmvCaysimType(Type);
	G->Setup(S, nullptr);
	G->ConfigureVisual(Type, FString(), Color);
	return G;
}

void ASmvGameMode::BuildScene()
{
	if (!Flow.IsValid()) return;
	USmvGameInstance* GI = USmvGameInstance::GetSmv(this);
	USmvDataService* Data = GI->GetData();
	const FSmvSettings& Set = Data->Settings();
	Req = Flow->Draft;
	bScene = true;
	bCrashed = false;
	bPauseMenu = false;
	bHasLastPos = false;
	Events.Reset();
	SceneT0 = FPlatformTime::Seconds();
	GroundMsl = Req.Track.GroundMsl > -1000 ? Req.Track.GroundMsl : 100.0;
	UWorld* W = GetWorld();

	// ---- dünya ----
	FCaySimSession S;
	S.Center.Lat = Req.Track.Lat;
	S.Center.Lon = Req.Track.Lon;
	S.Center.Alt = GroundMsl;
	S.Center.bHasAlt = true;
	S.World.Mode = Req.Track.WorldMode;
	S.World.Time = Req.Track.TimeOfDay;
	S.World.WindMs = Req.Track.WindMs;
	S.World.WindDir = Req.Track.WindDir;
	S.World.SizeM = Req.Track.World.SizeM;
	S.World.Seed = Req.Track.World.Seed;
	S.World.Fog = Req.Track.Weather == TEXT("fog") ? 0.9f : Req.Track.Weather == TEXT("rain") || Req.Track.Weather == TEXT("snow") ? 0.5f
		: Req.Track.Weather == TEXT("cloudy") ? 0.35f : Req.Track.World.Fog;
	S.World.bRunway = Req.Track.World.bRunway;
	S.World.RunwayHeading = Req.Track.World.RunwayHeading;
	S.Type = SmvCaysimType(Req.Drone.Type);
	S.Model.Orient.Forward = TEXT("+x");
	S.Model.Orient.Up = TEXT("+y");
	S.Model.Orient.Scale = 1.f;

	WorldSetup = W->SpawnActor<ACaySimWorldSetup>();
	if (!Set.CesiumToken.IsEmpty()) WorldSetup->CesiumIonToken = Set.CesiumToken;
	ECaySimWorldSource Src = ECaySimWorldSource::Flat;
	const bool bReal = Req.Track.WorldMode == TEXT("real") || Req.Track.WorldMode == TEXT("google3d");
	if (bReal && !WorldSetup->CesiumIonToken.IsEmpty()) Src = ECaySimWorldSource::CesiumIon;   // Google 3B: ion üzerinden planlandı
	WorldSetup->Build(S, nullptr, Src);
	if (WorldSetup->GetSource() == ECaySimWorldSource::Flat)
	{
		if (bReal) PushEvent(FSmvI18n::T(TEXT("flight.no_cesium")).ToString(), 4);
		// Fab haritası (satın alınmış içerik; pakette varsa) — yoksa prosedürel dünya
		bool bFab = false;
		if (FSmvFabContent::IsFabWorld(Req.Track.WorldMode))
		{
			if (const FSmvFabMap* FM = FSmvFabContent::FindMap(Req.Track.WorldMode))
			{
				bool bOk = false;
				FabLevels.Reset();
				bFabSubsRequested = false;
				bFabPlaced = false;
				ULevelStreamingDynamic* Lv = ULevelStreamingDynamic::LoadLevelInstance(W, FM->Level, FVector::ZeroVector, FRotator::ZeroRotator, bOk);
				if (bOk && Lv)
				{
					Lv->OnLevelShown.AddDynamic(this, &ASmvGameMode::HandleFabLevelShown);
					FabLevels.Add(Lv);
					bFab = true;
					UE_LOG(LogSimumav, Log, TEXT("Fab haritası yükleniyor: %s"), *FM->Level);
				}
			}
			if (!bFab) PushEvent(FSmvI18n::T(TEXT("flight.fab_missing")).ToString(), 4);
		}
		if (!bFab)
		{
			Terrain = W->SpawnActor<ASmvProcTerrain>();
			if (Terrain) Terrain->Build(Req.Track, GroundMsl, WorldSetup);
		}
	}

	// ---- dron ----
	Drone = W->SpawnActor<ASmvDroneActor>();
	Drone->SetGeoTransformActor(WorldSetup);
	Drone->Setup(S, nullptr);
	Drone->ConfigureVisual(Req.Drone.Type, Req.Drone.ModelKey, SmvColor(Req.Drone.Color));
	TWeakObjectPtr<ASmvGameMode> Weak(this);
	if (USmvFlightLink* L = Link())
	{
		TWeakObjectPtr<USmvFlightLink> WL(L);
		Drone->SetClockSource([WL]() { return WL.IsValid() ? WL->Now() : 0.0; });
		L->OnTele.AddUObject(this, &ASmvGameMode::HandleTele);
		L->OnMessage.AddUObject(this, &ASmvGameMode::HandleMessage);
	}
	Data->FetchModelGlb(Req.Drone.ModelKey, [Weak](bool bOk, const TArray<uint8>& Bytes)
	{
		ASmvGameMode* Self = Weak.Get();
		if (Self && bOk && Self->Drone) Self->Drone->LoadGlbBytes(Bytes);
	});

	// ---- pilot piyonu ----
	if (ASmvPilotPawn* P = GetPilot())
	{
		P->SetTarget(Drone, Req.Drone.Type);
		P->SetMode(ESmvCamMode::Chase);
		P->InputDevice = Set.InputDevice;
		P->bStickMode2 = Set.bStickMode2;
		P->Expo = (float)Set.StickExpo;
		P->RcAxisMap = Set.RcAxisMap;
		P->RcInvert = Set.RcInvert;
		P->SetCamType(Req.Camera == TEXT("thermal") ? ESmvCamType::Thermal : Req.Camera == TEXT("night") ? ESmvCamType::Night : ESmvCamType::Day);
		P->OnRc.AddLambda([Weak](const TArray<int32>& Rc)
		{
			ASmvGameMode* Self = Weak.Get();
			if (Self && Self->Link() && !Self->bPauseMenu) Self->Link()->SendRc(Rc);
		});
		P->OnAction.AddUObject(this, &ASmvGameMode::HandleAction);
	}

	// ---- HUD ----
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		Hud = CreateWidget<UCaySimHudWidget>(PC, UCaySimHudWidget::StaticClass());
		if (Hud) { Hud->AddToViewport(5); Hud->SetVisibility(ESlateVisibility::HitTestInvisible); }
		Overlay = CreateWidget<USmvHudOverlay>(PC, USmvHudOverlay::StaticClass());
		if (Overlay) { Overlay->AddToViewport(6); Overlay->SetVisibility(ESlateVisibility::HitTestInvisible); }
	}
	// ---- uçuş arayüzü (dron paneli, kadranlar, alt bant, sanal kumanda) ----
	UiLog.Reset();
	ArmConfirmUntil = 0;
	if (GEngine && GEngine->GameViewport)
	{
		FlightUi = SNew(SSmvFlightUi).GameMode(this);
		FlightUiHost = SNew(SWeakWidget).Visibility(EVisibility::SelfHitTestInvisible).PossiblyNullContent(FlightUi);
		GEngine->GameViewport->AddViewportWidgetContent(FlightUiHost.ToSharedRef(), 20);
	}

	// ---- uyarılar ----
	SmvCore::FGpwsCfg Gc;
	Gc.Mode = TCHAR_TO_UTF8(*Set.GpwsMode);
	Gc.Sens = TCHAR_TO_UTF8(*Set.GpwsSens);
	Gc.BankCopter = Set.BankCopter;
	Gc.BankPlane = Set.BankPlane;
	delete Gpws;
	Gpws = new SmvCore::FGpws(SmvCore::SitlTypeFromSmv(std::string(TCHAR_TO_UTF8(*Req.Drone.Type))), Gc,
		Req.Power.MaxSpeedMs > 3 ? Req.Power.MaxSpeedMs : (Req.Drone.Power.MaxSpeedMs > 3 ? Req.Drone.Power.MaxSpeedMs : SmvCore::NaN), SmvCore::NaN);
	PendingGpws.Reset();
	ActiveAlerts.Reset();

	// ---- hedefler ----
	for (int32 i = 0; i < Req.Targets.Num() && i < 8; ++i)
	{
		if (ASmvDroneActor* T = SpawnGhost(TEXT("plane"), FLinearColor(0.9f, 0.2f, 0.9f))) TargetActors.Add(T);
	}

	// ---- halkalar / yarış ----
	SetupRace();
	if (Flow->GetMatch() && Flow->GetMatch()->HasMatch())
	{
		Flow->GetMatch()->OnPositions.AddUObject(this, &ASmvGameMode::HandleMatchPositions);
	}
	UE_LOG(LogSimumav, Log, TEXT("Sahne kuruldu: %s / %s (%s)"), *Req.Drone.Name, *Req.Track.Name, *Req.Track.WorldMode);
}

void ASmvGameMode::FreeRace()
{
	delete Rings; Rings = nullptr;
	delete MyTracker; MyTracker = nullptr;
	delete Dogfight; Dogfight = nullptr;
	for (FBot& B : Bots) { delete B.Pilot; delete B.Tracker; }
	Bots.Reset();
	bRaceStarted = bRaceDone = false;
	RaceResultBadge.Empty();
}

void ASmvGameMode::SetupRace()
{
	FreeRace();
	const FSmvLocalRace& R = Flow->Race;
	const SmvCore::EVClass Cl = SmvCore::ClassOfType(TCHAR_TO_UTF8(*Req.Drone.Type));
	Rings = new TArray<SmvCore::FRing>();
	const bool bLocalRace = Req.Backend == ESmvFlightBackend::LocalSitl && R.Kind != TEXT("none");
	const bool bOnlineMatch = Flow->GetMatch() && Flow->GetMatch()->HasMatch() && Req.Backend == ESmvFlightBackend::Cloud;

	if (bLocalRace && R.Kind == TEXT("rings"))
	{
		SmvCore::FRingCfg C = SmvCore::FRingCfg::Defaults(Cl);
		C.Count = R.RingCount;
		C.Seed = R.Seed > 0 ? R.Seed : FMath::RandRange(1, 2147483646);
		C.Clamp();
		std::vector<SmvCore::FStart> St = SmvCore::StartPoints(R.Bots + 1, Cl, Req.Track.World.RunwayHeading);
		const SmvCore::FStart P0 = St[0];
		for (SmvCore::FStart& X : St) { X.X -= P0.X; X.Y -= P0.Y; }   // oyuncu ev noktasında (SITL home)
		for (const SmvCore::FRing& G : SmvCore::GenerateRings(C, Req.Track.Lat, Req.Track.Lon, GroundMsl, St)) Rings->Add(G);
		static const TCHAR* Names[] = { TEXT("Şahin"), TEXT("Kartal"), TEXT("Doğan"), TEXT("Atmaca"), TEXT("Kerkenez"), TEXT("Albatros"), TEXT("Akbaba") };
		static const TCHAR* Colors[] = { TEXT("#ef4444"), TEXT("#8b5cf6"), TEXT("#14b8a6"), TEXT("#f59e0b"), TEXT("#ec4899"), TEXT("#64748b"), TEXT("#84cc16") };
		for (int32 i = 0; i < R.Bots && i < 7; ++i)
		{
			FBot B;
			B.Pilot = new SmvCore::FBotPilot(TCHAR_TO_UTF8(*R.Level), Cl, (uint64)(C.Seed + i + 1));
			B.Pilot->Place(St[i + 1].X, St[i + 1].Y, 0, St[i + 1].YawDeg);
			B.Tracker = new SmvCore::FRingTracker(std::vector<SmvCore::FRing>(Rings->GetData(), Rings->GetData() + Rings->Num()));
			B.Name = FString::Printf(TEXT("%s (%s)"), Names[i], *R.Level);
			Bots.Add(B);
			if (ASmvDroneActor* G = SpawnGhost(Req.Drone.Type, SmvColor(Colors[i]))) Ghosts.Add(G);
		}
	}
	else if (bLocalRace && R.Kind == TEXT("dogfight"))
	{
		SmvCore::FDogfightCfg C = SmvCore::FDogfightCfg::Defaults(Cl);
		C.Seed = R.Seed > 0 ? R.Seed : FMath::RandRange(1, 2147483646);
		C.Roles = TCHAR_TO_UTF8(*R.Roles);
		C.Normalize(false);
		bPlayerChaser = SmvCore::HostIsChaser(C);
		SmvCore::FStart Ch, Ev;
		SmvCore::DogfightStarts(C, Req.Track.World.RunwayHeading, Ch, Ev);
		const SmvCore::FStart Me = bPlayerChaser ? Ch : Ev, Bt = bPlayerChaser ? Ev : Ch;
		FBot B;
		B.Pilot = new SmvCore::FBotPilot(TCHAR_TO_UTF8(*R.Level), Cl, (uint64)C.Seed);
		B.Pilot->Place(Bt.X - Me.X, Bt.Y - Me.Y, 0, Bt.YawDeg);
		B.Name = FString::Printf(TEXT("Şahin (%s)"), *R.Level);
		Bots.Add(B);
		Dogfight = new SmvCore::FDogfight(C, 0.0);
		if (ASmvDroneActor* G = SpawnGhost(Req.Drone.Type, FLinearColor(0.94f, 0.27f, 0.27f))) Ghosts.Add(G);
	}
	else if (bOnlineMatch)
	{
		for (const FSmvGate& G : Flow->GetMatch()->Current().Rings)
		{
			SmvCore::FRing X;
			X.I = G.I; X.Lat = G.Lat; X.Lon = G.Lon; X.AglM = G.AglM; X.AltMsl = GroundMsl + G.AglM;
			SmvCore::Enu(Req.Track.Lat, Req.Track.Lon, G.Lat, G.Lon, X.X, X.Y);
			X.Nx = FMath::Sin(FMath::DegreesToRadians(G.YawDeg)); X.Ny = FMath::Cos(FMath::DegreesToRadians(G.YawDeg));
			X.RadiusM = G.RadiusM;
			Rings->Add(X);
		}
	}
	else
	{
		// parkur kapıları (tek tur)
		for (int32 i = 0; i < Req.Track.Gates.Num(); ++i)
		{
			const FSmvGate& G = Req.Track.Gates[i];
			SmvCore::FRing X;
			X.I = i + 1; X.Lat = G.Lat; X.Lon = G.Lon; X.AglM = G.AglM; X.AltMsl = GroundMsl + G.AglM;
			SmvCore::Enu(Req.Track.Lat, Req.Track.Lon, G.Lat, G.Lon, X.X, X.Y);
			X.Nx = FMath::Sin(FMath::DegreesToRadians(G.YawDeg)); X.Ny = FMath::Cos(FMath::DegreesToRadians(G.YawDeg));
			X.RadiusM = G.RadiusM;
			Rings->Add(X);
		}
	}
	if (Rings->Num()) MyTracker = new SmvCore::FRingTracker(std::vector<SmvCore::FRing>(Rings->GetData(), Rings->GetData() + Rings->Num()));
	PlaceRingActors();
	// yarışlarda başlangıç kapısı: geri sayım uçuş HAZIR olunca
	bRaceStarted = !(bLocalRace || bOnlineMatch);
}

void ASmvGameMode::PlaceRingActors()
{
	for (TObjectPtr<ASmvGateActor>& G : Gates) if (G) G->Destroy();
	Gates.Reset();
	if (!Rings || !WorldSetup) return;
	for (const SmvCore::FRing& R : *Rings)
	{
		const FVector C = Geo(R.Lat, R.Lon, R.AltMsl);
		double La2, Lo2;
		SmvCore::LatLon(Req.Track.Lat, Req.Track.Lon, R.X + R.Nx * 10.0, R.Y + R.Ny * 10.0, La2, Lo2);
		const FVector Ahead = Geo(La2, Lo2, R.AltMsl);
		ASmvGateActor* A = GetWorld()->SpawnActor<ASmvGateActor>(C, FRotator(0, (Ahead - C).Rotation().Yaw, 0));
		if (!A) continue;
		A->Build((float)R.RadiusM, FMath::Clamp((float)R.RadiusM * 0.08f, 0.15f, 1.5f), FLinearColor(1.f, 0.45f, 0.05f));
		Gates.Add(A);
	}
	if (Gates.Num()) Gates[0]->SetState(1);
}

void ASmvGameMode::StopScene()
{
	if (!bScene) return;
	bScene = false;
	if (USmvFlightLink* L = Link()) { L->OnTele.RemoveAll(this); L->OnMessage.RemoveAll(this); }
	if (Flow.IsValid() && Flow->GetMatch()) Flow->GetMatch()->OnPositions.RemoveAll(this);
	if (ASmvPilotPawn* P = GetPilot()) { P->OnRc.Clear(); P->OnAction.RemoveAll(this); P->SetFlying(false); P->SetTarget(nullptr, TEXT("multirotor")); }
	if (Hud) { Hud->RemoveFromParent(); Hud = nullptr; }
	if (Overlay) { Overlay->RemoveFromParent(); Overlay = nullptr; }
	if (FlightUiHost.IsValid() && GEngine && GEngine->GameViewport) GEngine->GameViewport->RemoveViewportWidgetContent(FlightUiHost.ToSharedRef());
	FlightUiHost.Reset();
	FlightUi.Reset();
	for (TObjectPtr<ASmvGateActor>& G : Gates) if (G) G->Destroy();
	for (TObjectPtr<ASmvDroneActor>& G : Ghosts) if (G) G->Destroy();
	for (TObjectPtr<ASmvDroneActor>& G : TargetActors) if (G) G->Destroy();
	Gates.Reset(); Ghosts.Reset(); TargetActors.Reset(); GhostIndex.Reset();
	if (Drone) { Drone->Destroy(); Drone = nullptr; }
	if (Terrain) { Terrain->Destroy(); Terrain = nullptr; }
	for (TObjectPtr<ULevelStreamingDynamic>& Lv : FabLevels)
	{
		if (!Lv) continue;
		Lv->OnLevelShown.RemoveAll(this);
		Lv->SetIsRequestingUnloadAndRemoval(true);
	}
	FabLevels.Reset();
	if (WorldSetup) { WorldSetup->Destroy(); WorldSetup = nullptr; }
	if (Tones) Tones->SetAlarm(0, 0.f);
	FreeRace();
	delete Gpws; Gpws = nullptr;
}

// ---------------------------------------------------------------------------
// bağlantı olayları
// ---------------------------------------------------------------------------

void ASmvGameMode::PushEvent(const FString& Text, int32 Sev)
{
	if (Text.IsEmpty()) return;
	Events.Add(TPair<FString, int32>(Text, Sev));
	if (Events.Num() > 40) Events.RemoveAt(0);
	FSmvUiLog E;
	E.Text = Text; E.Sev = Sev; E.Wall = FPlatformTime::Seconds();
	E.Clock = FDateTime::Now().ToString(TEXT("%H:%M:%S"));
	UiLog.Add(MoveTemp(E));
	if (UiLog.Num() > 60) UiLog.RemoveAt(0);
}

void ASmvGameMode::HandleTele(const FCaySimTelemetry& T, const FSmvTeleExtra& X)
{
	if (!bScene || !Drone) return;
	Drone->PushTelemetry(T);
	UpdateGpws(T, X);
}

void ASmvGameMode::HandleMessage(const TSharedPtr<FJsonObject>& M)
{
	const FString T = SmvJson::Str(M, TEXT("t"));
	if (T == TEXT("event")) { PushEvent(SmvJson::Str(M, TEXT("text")), (int32)SmvJson::Num(M, TEXT("severity"), 6)); return; }
	if (T == TEXT("state") && SmvJson::Str(M, TEXT("state")) == TEXT("HAZIR") && !bRaceStarted && Flow->Race.Kind != TEXT("none"))
	{
		RaceStartWall = FPlatformTime::Seconds() + SmvCore::COUNTDOWN_S;   // yerel yarış geri sayımı
	}
	if (T == TEXT("crash"))
	{
		bCrashed = true;
		PushEvent(FString::Printf(TEXT("ÇARPIŞMA: %s, %.1f m/s"), *SmvJson::Str(M, TEXT("reason")), SmvJson::Num(M, TEXT("speed"))), 2);
		if (Gpws) { SmvCore::FGpwsInput I; I.T = Link() ? Link()->LastTele().Tbm / 1000.0 : 0; I.bCrashed = true; Gpws->Step(I); }
		return;
	}
	if (T == TEXT("warn")) { PushEvent(SmvJson::Str(M, TEXT("text")), 2); return; }
	if (T == TEXT("servo_trigger") && SmvJson::Str(M, TEXT("phase")) == TEXT("fired"))
	{
		bCrashed = true;
		EndAt = FPlatformTime::Seconds() + 2.5; EndReasonPending = TEXT("servo_trigger");
		return;
	}
	if (T == TEXT("target") && SmvJson::Str(M, TEXT("kind")) == TEXT("intercept"))
	{
		if (Tones) Tones->Blip(1);
		PushEvent(FString::Printf(TEXT("ÖNLEME T%d — %.1f m%s"), (int32)SmvJson::Num(M, TEXT("i")), SmvJson::Num(M, TEXT("dist_m")), SmvJson::Bool(M, TEXT("locked")) ? TEXT(" (kilitli)") : TEXT("")), 5);
		return;
	}
	if (T == TEXT("engage")) { if (Tones) Tones->Blip(2); PushEvent(FString::Printf(TEXT("ANGAJMAN %s T%d"), *SmvJson::Str(M, TEXT("kind")), (int32)SmvJson::Num(M, TEXT("i"))), 5); return; }
	if (T == TEXT("launch")) { PushEvent(FString::Printf(TEXT("Kalkış: %s"), *SmvJson::Str(M, TEXT("phase"))), 6); return; }
	if (T == TEXT("res") && !SmvJson::Bool(M, TEXT("ok"))) { PushEvent(FString::Printf(TEXT("%s: %s"), *SmvJson::Str(M, TEXT("name")), *SmvJson::Str(M, TEXT("error"))), 4); return; }
	if (T == TEXT("end")) { if (Flow.IsValid()) Flow->EndFlight(SmvJson::Str(M, TEXT("reason"), TEXT("end"))); }
}

void ASmvGameMode::HandleMatchPositions(const TArray<FSmvGhostPos>& P)
{
	USmvGameInstance* GI = USmvGameInstance::GetSmv(this);
	const int64 Me = GI && GI->GetClient() ? GI->GetClient()->GetUser().Id : 0;
	for (const FSmvGhostPos& G : P)
	{
		if (G.Id == Me) continue;
		int32* Idx = GhostIndex.Find(G.Id);
		if (!Idx)
		{
			ASmvDroneActor* A = SpawnGhost(Req.Drone.Type, FLinearColor(0.2f, 0.5f, 1.f));
			if (!A) continue;
			Ghosts.Add(A);
			Idx = &GhostIndex.Add(G.Id, Ghosts.Num() - 1);
		}
		ASmvDroneActor* A = Ghosts.IsValidIndex(*Idx) ? Ghosts[*Idx].Get() : nullptr;
		if (!A) continue;
		FCaySimTelemetry T;
		T.Ts = G.Ts; T.Lat = G.Lat; T.Lon = G.Lon; T.AltMsl = G.AltMsl; T.Roll = (float)G.Roll; T.Pitch = (float)G.Pitch; T.Yaw = (float)G.Yaw;
		T.Groundspeed = (float)G.Gs; T.bArmed = true; T.Throttle = 60.f;
		A->PushTelemetry(T);
	}
}

// ---------------------------------------------------------------------------
// eylemler
// ---------------------------------------------------------------------------

void ASmvGameMode::Command(const FString& Name, TSharedPtr<FJsonObject> Params)
{
	if (USmvFlightLink* L = Link()) L->Command(Name, Params.IsValid() ? Params : MakeShared<FJsonObject>());
}

/** Uçuş modu listesi (web flightsim_fltbar FLIGHT_MODES ile aynı). */
static TArray<FString> SmvFlightModes(const FString& Kind)
{
	if (Kind == TEXT("plane")) return { TEXT("MANUAL"), TEXT("STABILIZE"), TEXT("ACRO"), TEXT("FBWA"), TEXT("FBWB"), TEXT("CRUISE"), TEXT("TAKEOFF"), TEXT("AUTO"), TEXT("LOITER"), TEXT("CIRCLE"), TEXT("GUIDED"), TEXT("RTL") };
	if (Kind == TEXT("vtol")) return { TEXT("MANUAL"), TEXT("FBWA"), TEXT("FBWB"), TEXT("CRUISE"), TEXT("TAKEOFF"), TEXT("AUTO"), TEXT("LOITER"), TEXT("CIRCLE"), TEXT("GUIDED"), TEXT("RTL"),
		TEXT("QSTABILIZE"), TEXT("QHOVER"), TEXT("QLOITER"), TEXT("QACRO"), TEXT("QLAND"), TEXT("QRTL") };
	if (Kind == TEXT("icept")) return { TEXT("FBWA"), TEXT("FBWB"), TEXT("CRUISE"), TEXT("AUTO"), TEXT("LOITER"), TEXT("GUIDED"), TEXT("RTL"), TEXT("QSTABILIZE"), TEXT("QHOVER"), TEXT("QLOITER"), TEXT("QLAND"), TEXT("QRTL") };
	if (Kind == TEXT("rover")) return { TEXT("MANUAL"), TEXT("ACRO"), TEXT("STEERING"), TEXT("HOLD"), TEXT("LOITER"), TEXT("FOLLOW"), TEXT("SIMPLE"), TEXT("AUTO"), TEXT("RTL"), TEXT("SMART_RTL"), TEXT("GUIDED") };
	return { TEXT("STABILIZE"), TEXT("ALT_HOLD"), TEXT("LOITER"), TEXT("POSHOLD"), TEXT("ACRO"), TEXT("GUIDED"), TEXT("AUTO"), TEXT("BRAKE"), TEXT("CIRCLE"), TEXT("RTL"), TEXT("SMART_RTL"), TEXT("LAND") };
}

static FString SmvModeKind(const FString& Type)
{
	if (Type == TEXT("rover")) return TEXT("rover");
	if (Type == TEXT("interceptor")) return TEXT("icept");
	if (Type == TEXT("vtol")) return TEXT("vtol");
	if (Type == TEXT("plane")) return TEXT("plane");
	return TEXT("copter");
}

void ASmvGameMode::UiAction(const FString& Name, const FString& Arg)
{
	if (!Flow.IsValid() || Flow->GetScreen() != ESmvScreen::Flight) return;
	if (Name == TEXT("Mode"))
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("mode"), Arg);
		Command(TEXT("mode"), P);
		PushEvent(FString::Printf(TEXT("Mod isteği: %s"), *Arg), 6);
		return;
	}
	if (Name == TEXT("CamCycle")) { if (ASmvPilotPawn* P = GetPilot()) P->CycleMode(); return; }
	HandleAction(FName(*Name));
}

void ASmvGameMode::TogglePause()
{
	bPauseMenu = !bPauseMenu;
	HandleScreen(Flow.IsValid() ? Flow->GetScreen() : ESmvScreen::Flight);
}

void ASmvGameMode::HandleAction(FName A)
{
	if (!Flow.IsValid() || Flow->GetScreen() != ESmvScreen::Flight) return;
	static double LastReset = 0;
	const bool bRaceLocked = !bRaceStarted;
	const bool bMatch = Flow->GetMatch() && Flow->GetMatch()->HasMatch();
	if (A == TEXT("Pause")) { TogglePause(); return; }
	if (A == TEXT("Panel")) { if (FlightUi.IsValid()) FlightUi->TogglePanel(); return; }
	if (A == TEXT("Sticks")) { if (FlightUi.IsValid()) FlightUi->ToggleSticks(); return; }
	if (A == TEXT("ModeMenu")) { if (FlightUi.IsValid()) FlightUi->ToggleModeMenu(); return; }
	if (A == TEXT("ArmToggle"))
	{
		// web flightsim_fltbar disarmAction: sabit kanat havada → motor kes (süzülür); kopter havada → onay (araç düşer); yerde gerçek DISARM
		USmvFlightLink* Lk = Link();
		if (!Lk) return;
		const FCaySimTelemetry& Tl = Lk->LastTele();
		const bool bPlaneLike = Req.Drone.Type == TEXT("plane") || Req.Drone.Type == TEXT("vtol") || Req.Drone.Type == TEXT("interceptor");
		const double Now = FPlatformTime::Seconds();
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		if (Lk->LastExtra().bMotorCut) { P->SetBoolField(TEXT("on"), false); Command(TEXT("motor_cut"), P); return; }
		if (!Tl.bArmed)
		{
			if (bRaceLocked) { HandleToast(FSmvI18n::T(TEXT("race.wait_start")).ToString(), 4); return; }
			Command(TEXT("arm"));
			return;
		}
		if (bPlaneLike && Tl.AltRel > 3.0)
		{
			P->SetBoolField(TEXT("on"), true);
			Command(TEXT("motor_cut"), P);
			PushEvent(TEXT("MOTOR KESİK · SÜZÜLÜYOR — yeniden basınca motor verilir"), 4);
			return;
		}
		if (!bPlaneLike && Tl.AltRel > 1.5 && Now > ArmConfirmUntil)
		{
			ArmConfirmUntil = Now + 2.5;
			HandleToast(TEXT("Havada DISARM aracı düşürür — onaylamak için 2 sn içinde yeniden basın (X)"), 4);
			return;
		}
		ArmConfirmUntil = 0;
		Command(TEXT("disarm"));
		return;
	}
	if (A == TEXT("Ack")) { if (Gpws) Gpws->Ack(); bAlertUnacked = false; if (Tones) Tones->SetAlarm(0, 0.f); return; }
	if ((A == TEXT("Arm") || A == TEXT("Takeoff")) && bRaceLocked) { HandleToast(FSmvI18n::T(TEXT("race.wait_start")).ToString(), 4); return; }
	if (A == TEXT("Arm")) Command(TEXT("arm"));
	else if (A == TEXT("Disarm")) Command(TEXT("disarm"));
	else if (A == TEXT("Takeoff")) Command(TEXT("takeoff"));
	else if (A == TEXT("Rtl")) Command(TEXT("rtl"));
	else if (A == TEXT("Land")) Command(TEXT("land"));
	else if (A == TEXT("MotorCut"))
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetBoolField(TEXT("on"), !(Link() && Link()->LastExtra().bMotorCut));
		Command(TEXT("motor_cut"), P);
	}
	else if (A == TEXT("Reset"))
	{
		if (bMatch && Flow->GetMatch()->Current().Kind == TEXT("dogfight")) { HandleToast(FSmvI18n::T(TEXT("flight.no_reset_dogfight")).ToString(), 4); return; }
		const double Now = FPlatformTime::Seconds();
		if (Now - LastReset < 2.0) { Command(TEXT("reset")); bCrashed = false; LastReset = 0; }
		else { LastReset = Now; HandleToast(FSmvI18n::T(TEXT("flight.reset_confirm")).ToString(), 4); }
	}
	else if (A == TEXT("SpeedUp") || A == TEXT("SpeedDown"))
	{
		if (bMatch || Flow->Race.Kind != TEXT("none")) { HandleToast(FSmvI18n::T(TEXT("flight.speed_locked")).ToString(), 4); return; }
		const std::vector<double>& Sp = SmvCore::Speedups();
		const double Cur = Link() ? Link()->LastExtra().Speedup : 1.0;
		int32 Idx = 2;
		for (int32 i = 0; i < (int32)Sp.size(); ++i) if (FMath::IsNearlyEqual(Sp[i], Cur)) Idx = i;
		Idx = FMath::Clamp(Idx + (A == TEXT("SpeedUp") ? 1 : -1), 0, (int32)Sp.size() - 1);
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetNumberField(TEXT("factor"), Sp[Idx]);
		Command(TEXT("speed"), P);
	}
	else if (A == TEXT("Lock"))
	{
		// en yakın ekrandaki hedef → kilit; kilitliyse bırak
		if (TargetActors.Num() == 0) return;
		static int32 Locked = 0;
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		if (Locked > 0) { P->SetStringField(TEXT("action"), TEXT("release")); P->SetStringField(TEXT("reason"), TEXT("released")); Locked = 0; }
		else
		{
			float Best = 1e9f; int32 BestI = 0;
			if (Drone)
				for (int32 i = 0; i < TargetActors.Num(); ++i)
				{
					const float D = (float)FVector::Dist(TargetActors[i]->GetActorLocation(), Drone->GetActorLocation());
					if (D < Best) { Best = D; BestI = i + 1; }
				}
			Locked = BestI;
			P->SetStringField(TEXT("action"), TEXT("lock"));
			P->SetNumberField(TEXT("i"), BestI);
		}
		Command(TEXT("engage"), P);
	}
	else if (A == TEXT("CamType"))
	{
		if (ASmvPilotPawn* Pw = GetPilot())
		{
			TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
			P->SetStringField(TEXT("type"), Pw->GetCamType() == ESmvCamType::Thermal ? TEXT("thermal") : Pw->GetCamType() == ESmvCamType::Night ? TEXT("night") : TEXT("day"));
			Command(TEXT("camera"), P);
		}
	}
}

// ---------------------------------------------------------------------------
// her kare
// ---------------------------------------------------------------------------

void ASmvGameMode::Tick(float Dt)
{
	Super::Tick(Dt);
	if (!bScene) return;
	if (EndAt > 0 && FPlatformTime::Seconds() >= EndAt) { EndAt = 0; if (Flow.IsValid()) Flow->EndFlight(EndReasonPending); }
	DetectCollision();
	UpdateRace(Dt);
	UpdateTargets();
	// ses
	if (Drone)
	{
		const FVector L = GetPilot() ? GetPilot()->GetActorLocation() : Drone->GetActorLocation();
		const USmvGameInstance* GI = USmvGameInstance::GetSmv(this);
		const FSmvSettings& S = GI->GetData()->Settings();
		Drone->UpdateAudio(L, S.bSound ? (float)S.Volume : 0.f);
		for (TObjectPtr<ASmvDroneActor>& G : Ghosts) if (G) G->UpdateAudio(L, S.bSound ? (float)S.Volume * 0.6f : 0.f);
	}
	// bulut: biten uyarı bölümlerini sunucuya bildir (cmd gpws; ≤ 50 / komut)
	if (PendingGpws.Num() && Link() && Link()->Backend() == ESmvFlightBackend::Cloud && FPlatformTime::Seconds() - LastGpwsFlush > 2.0)
	{
		LastGpwsFlush = FPlatformTime::Seconds();
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Ev;
		for (int32 i = 0; i < PendingGpws.Num() && i < 50; ++i) Ev.Add(MakeShared<FJsonValueObject>(PendingGpws[i]));
		PendingGpws.RemoveAt(0, FMath::Min(50, PendingGpws.Num()));
		P->SetArrayField(TEXT("events"), Ev);
		TSharedRef<FJsonObject> Cfg = MakeShared<FJsonObject>();
		Cfg->SetStringField(TEXT("mode"), USmvGameInstance::GetSmv(this)->GetData()->Settings().GpwsMode);
		Cfg->SetArrayField(TEXT("off"), TArray<TSharedPtr<FJsonValue>>());
		P->SetObjectField(TEXT("cfg"), Cfg);
		Command(TEXT("gpws"), P);
	}
	UpdateHud();
}

void ASmvGameMode::DetectCollision()
{
	if (!Drone || bCrashed || !Link()) return;
	const FVector Pos = Drone->GetActorLocation();
	if (!bHasLastPos) { LastDronePos = Pos; bHasLastPos = true; return; }
	const FCaySimTelemetry& T = Link()->LastTele();
	const float Spd = FMath::Max(T.Groundspeed, FMath::Abs(T.Climb));
	FHitResult Hit;
	FCollisionQueryParams Q(TEXT("SmvCrash"), true, Drone);
	for (const TObjectPtr<ASmvDroneActor>& G : Ghosts) Q.AddIgnoredActor(G);
	for (const TObjectPtr<ASmvDroneActor>& G : TargetActors) Q.AddIgnoredActor(G);
	static double LastSent = 0;
	if (T.bArmed && Spd > 1.5f && FVector::DistSquared(Pos, LastDronePos) > 1.f
		&& GetWorld()->LineTraceSingleByChannel(Hit, LastDronePos, Pos, ECC_Visibility, Q) && FPlatformTime::Seconds() - LastSent > 3.0)
	{
		LastSent = FPlatformTime::Seconds();
		const UPrimitiveComponent* C = Hit.GetComponent();
		const FString Kind = C && C->ComponentHasTag(TEXT("building")) ? TEXT("building") : C && C->ComponentHasTag(TEXT("water")) ? TEXT("water") : TEXT("terrain");
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("kind"), Kind);
		P->SetNumberField(TEXT("speed"), Spd);
		Command(TEXT("crash"), P);
	}
	LastDronePos = Pos;
}

void ASmvGameMode::UpdateGpws(const FCaySimTelemetry& T, const FSmvTeleExtra& X)
{
	if (!Gpws || !Drone) return;
	SmvCore::FGpwsInput I;
	I.T = T.Tbm > 0 ? T.Tbm / 1000.0 : FPlatformTime::Seconds() - SceneT0;
	I.bArmed = T.bArmed;
	I.Mode = TCHAR_TO_UTF8(*T.Mode);
	// AGL: 3B dünyadan (yer ışını), yoksa SITL göreli irtifası
	const FVector P = Drone->GetActorLocation();
	FHitResult Hit;
	FCollisionQueryParams Q(TEXT("SmvAgl"), true, Drone);
	double Agl = T.AltRel;
	if (GetWorld()->LineTraceSingleByChannel(Hit, P, P - FVector(0, 0, 300000.0), ECC_Visibility, Q)) Agl = (P.Z - Hit.ImpactPoint.Z) / 100.0;
	I.Agl = Agl;
	const double Look = SmvCore::IsPlaneType(SmvCore::SitlTypeFromSmv(std::string(TCHAR_TO_UTF8(*Req.Drone.Type)))) ? 8.0 : 4.0;
	const FVector Vel = CaySimGeo::NedVelocityToUnreal(FVector(T.Vn, T.Ve, T.Vd)) ;   // cm/s, Cesium/ENU eksen sözleşmesi
	const FVector Ahead = P + FVector(Vel.X, Vel.Y, 0.0) * Look;
	if (GetWorld()->LineTraceSingleByChannel(Hit, Ahead + FVector(0, 0, 200000.0), Ahead - FVector(0, 0, 300000.0), ECC_Visibility, Q)) I.AglAhead = (P.Z - Hit.ImpactPoint.Z) / 100.0;
	I.Vs = T.Climb;
	I.Gs = T.Groundspeed;
	I.As = T.Airspeed;
	I.Roll = FMath::RadiansToDegrees(T.Roll);
	I.Pitch = FMath::RadiansToDegrees(T.Pitch);
	I.bRates = true; I.P = FMath::RadiansToDegrees(T.RollSpeed); I.Q = FMath::RadiansToDegrees(T.PitchSpeed); I.R = FMath::RadiansToDegrees(T.YawSpeed);
	I.BattPct = X.bElec ? X.ElecPct : (T.BattPct >= 0 ? T.BattPct : SmvCore::NaN);
	I.bBattEmpty = X.bElecEmpty;
	I.FuelPct = X.bFuel ? X.FuelPct : SmvCore::NaN;
	I.bFuelEmpty = X.bFuelEmpty;
	I.GpsFix = T.GpsFix;
	I.Sats = T.Sats >= 0 ? T.Sats : SmvCore::NaN;
	I.SensBad = X.SensBad;
	I.Ekf = X.Ekf >= 0 ? X.Ekf : SmvCore::NaN;
	I.Vibe = X.Vibe >= 0 ? X.Vibe : SmvCore::NaN;
	I.Wind = X.WindSpd;
	I.bMotorCut = X.bMotorCut;
	if (T.bHasHome) I.DistHome = CaySimGeo::HaversineM(T.Lat, T.Lon, T.HomeLat, T.HomeLon);
	I.LinkAgeMs = Link() ? Link()->LinkAgeMs() : SmvCore::NaN;
	I.bCrashed = bCrashed;
	const SmvCore::FGpwsStepResult R = Gpws->Step(I);
	ActiveAlerts.Reset();
	const bool bAviation = USmvGameInstance::GetSmv(this)->GetData()->Settings().GpwsLang != TEXT("ui");
	bool bAnyUnacked = false;
	int32 Alarm = 0;
	for (const SmvCore::FGpwsActive& A : R.Active)
	{
		const SmvCore::FGpwsCodeDef* D = SmvCore::GpwsCode(A.Code);
		if (!D) continue;   // bilinmeyen kod (çekirdek tablosuyla uyumsuzluk) — çökme yerine atla
		const bool bWarn = A.Level == "warning";
		ActiveAlerts.Add(TPair<FString, bool>(bAviation ? FString(UTF8_TO_TCHAR(D->Av)) : FSmvI18n::T(FString(TEXT("gpws.c_")) + UTF8_TO_TCHAR(D->Code)).ToString(), bWarn));
		if (!A.bAcked) { bAnyUnacked = true; if (!Alarm) Alarm = bWarn ? 1 : 2; }
	}
	bAlertUnacked = bAnyUnacked;
	const FSmvSettings& S = USmvGameInstance::GetSmv(this)->GetData()->Settings();
	if (Tones) Tones->SetAlarm(S.GpwsMode == TEXT("full") && S.bAlertTones ? Alarm : 0, (float)S.Volume);
	for (const std::string& F : R.Fresh)
	{
		const SmvCore::FGpwsCodeDef* D = SmvCore::GpwsCode(F);
		if (!D) continue;
		PushEvent(FString::Printf(TEXT("%s · %s"), std::string(D->Level) == "warning" ? TEXT("WARNING") : TEXT("CAUTION"), UTF8_TO_TCHAR(D->Av)), std::string(D->Level) == "warning" ? 3 : 4);
	}
	for (const SmvCore::FGpwsEpisode& E : R.Ended)
	{
		TSharedPtr<FJsonObject> J = MakeShared<FJsonObject>();
		J->SetStringField(TEXT("code"), UTF8_TO_TCHAR(E.Code.c_str()));
		J->SetStringField(TEXT("level"), UTF8_TO_TCHAR(E.Level.c_str()));
		J->SetNumberField(TEXT("t0"), E.T0);
		J->SetNumberField(TEXT("start_s"), E.StartS);
		J->SetNumberField(TEXT("dur_s"), E.DurS);
		PendingGpws.Add(J);
	}
}

void ASmvGameMode::UpdateRace(float Dt)
{
	if (!Rings || !Link()) return;
	const FCaySimTelemetry& T = Link()->LastTele();
	const double Now = FPlatformTime::Seconds();
	// çevrim içi karşılaşma: başlangıç sunucudan (start_at, Unix sn); yarış dışı komutları sunucu da reddeder
	if (Flow->GetMatch() && Flow->GetMatch()->HasMatch() && Req.Backend == ESmvFlightBackend::Cloud)
	{
		const FSmvMatch& M = Flow->GetMatch()->Current();
		if (M.StartAt > 0 && RaceStartWall <= 0) RaceStartWall = Now + (M.StartAt - (Link()->Now()));
		if (M.State == TEXT("YARIS") && !bRaceStarted) { bRaceStarted = true; if (Tones) Tones->Blip(4); }
	}
	// bulut uçuşu: halka sonucu stop gövdesinde (`gates`) gider
	if (USmvCloudFlightLink* Cl = Cast<USmvCloudFlightLink>(Link()))
	{
		if (MyTracker) { Cl->GatesPassed = MyTracker->Passed(); Cl->GatesMissed = MyTracker->Finished() ? 0 : Rings->Num() - MyTracker->Passed(); Cl->Laps = MyTracker->Finished() ? 1 : 0; }
	}
	if (!bRaceStarted && RaceStartWall > 0)
	{
		const double Left = RaceStartWall - Now;
		if (Left <= 0) { bRaceStarted = true; RaceStartWall = Now; if (Tones) Tones->Blip(4); PushEvent(FSmvI18n::T(TEXT("race.go")).ToString(), 5); }
	}
	// oyuncu
	if (MyTracker && T.Lat != 0.0 && bRaceStarted)
	{
		double X, Y;
		SmvCore::Enu(Req.Track.Lat, Req.Track.Lon, T.Lat, T.Lon, X, Y);
		const double Tt = Now - RaceStartWall;
		for (const SmvCore::FRingEvent& E : MyTracker->Update(X, Y, T.AltMsl, Tt))
		{
			if (E.Kind == "ring") { if (Tones) Tones->Blip(1); PushEvent(FString::Printf(TEXT("HALKA %d/%d"), E.I, Rings->Num()), 6); }
			if (E.Kind == "wrong_way") PushEvent(FString::Printf(TEXT("TERS YÖN — halka %d"), E.I), 4);
			if (E.Kind == "finish")
			{
				PushEvent(FString::Printf(TEXT("BİTİŞ %.1f sn"), Tt), 5);
				bool bFirst = true;
				for (const FBot& B : Bots) if (B.FinishT >= 0) bFirst = false;
				if (Flow->Race.Kind == TEXT("rings") && !bRaceDone) { bRaceDone = true; if (bFirst && Bots.Num()) RaceResultBadge = TEXT("race_win"); }
			}
		}
		for (int32 i = 0; i < Gates.Num(); ++i) if (Gates[i]) Gates[i]->SetState(i < MyTracker->NextIndex() ? 2 : i == MyTracker->NextIndex() ? 1 : 0);
	}
	// yerel botlar
	if (!bRaceStarted) return;
	const double Tt = Now - RaceStartWall;
	// FBotPilot::StepRings std::vector ister (Rings bir TArray) — kare başına bir kez kopyala
	const std::vector<SmvCore::FRing> RingVec = Dogfight ? std::vector<SmvCore::FRing>() : std::vector<SmvCore::FRing>(Rings->GetData(), Rings->GetData() + Rings->Num());
	for (int32 i = 0; i < Bots.Num(); ++i)
	{
		FBot& B = Bots[i];
		if (Dogfight)
		{
			double Px, Py;
			SmvCore::Enu(Req.Track.Lat, Req.Track.Lon, T.Lat, T.Lon, Px, Py);
			SmvCore::FBotPose Opp; Opp.X = Px; Opp.Y = Py; Opp.Z = T.AltMsl - GroundMsl; Opp.HeadingDeg = T.Heading; Opp.Speed = T.Groundspeed;
			const SmvCore::FDogfightCfg Dc = SmvCore::FDogfightCfg::Defaults(SmvCore::ClassOfType(TCHAR_TO_UTF8(*Req.Drone.Type)));
			B.Pilot->StepDogfight(Dt, Tt, Opp, !bPlayerChaser, Dc.AreaRadiusM, Dc.RangeM, Dc.AltM);
			if (!bRaceDone && T.bArmed)
			{
				const int32 MeRole = bPlayerChaser ? SmvCore::FDogfight::Chaser : SmvCore::FDogfight::Evader;
				const int32 BotRole = bPlayerChaser ? SmvCore::FDogfight::Evader : SmvCore::FDogfight::Chaser;
				std::vector<SmvCore::FDogfightEvent> Ev = Dogfight->Update(MeRole, Px, Py, T.AltMsl - GroundMsl, T.Heading, T.Pitch, Tt);
				const SmvCore::FBotPose& Bp = B.Pilot->Pose();
				std::vector<SmvCore::FDogfightEvent> Ev2 = Dogfight->Update(BotRole, Bp.X, Bp.Y, Bp.Z, Bp.HeadingDeg, Bp.PitchRad, Tt);
				Ev.insert(Ev.end(), Ev2.begin(), Ev2.end());
				for (const SmvCore::FDogfightEvent& E : Ev)
				{
					if (E.Kind == "lock_on" && Tones) Tones->Blip(2);
					if (E.Kind == "engage") { bRaceDone = true; if (bPlayerChaser) RaceResultBadge = TEXT("dogfight_win"); PushEvent(bPlayerChaser ? TEXT("İT DALAŞI KAZANILDI") : TEXT("VURULDUNUZ"), bPlayerChaser ? 5 : 3); }
					if (E.Kind == "area") { bRaceDone = true; const bool bMeOut = E.Role == MeRole; if (!bMeOut) RaceResultBadge = TEXT("dogfight_win"); PushEvent(bMeOut ? TEXT("ALAN DIŞI — kaybettiniz") : TEXT("Rakip alan dışı — kazandınız"), 4); }
				}
				const SmvCore::FDogfightCfg Dc2 = SmvCore::FDogfightCfg::Defaults(SmvCore::ClassOfType(TCHAR_TO_UTF8(*Req.Drone.Type)));
				if (!bRaceDone && Tt > Dc2.TimeLimitMin * 60.0) { bRaceDone = true; if (!bPlayerChaser) RaceResultBadge = TEXT("dogfight_win"); PushEvent(TEXT("SÜRE DOLDU"), 4); }
			}
		}
		else
		{
			B.Pilot->StepRings(Dt, RingVec, B.Tracker->NextIndex());
			const SmvCore::FBotPose& Bp = B.Pilot->Pose();
			for (const SmvCore::FRingEvent& E : B.Tracker->Update(Bp.X, Bp.Y, GroundMsl + Bp.Z, Tt))
			{
				if (E.Kind == "finish")
				{
					B.FinishT = Tt;
					PushEvent(FString::Printf(TEXT("%s bitirdi (%.1f sn)"), *B.Name, Tt), 4);
					if (!bRaceDone) bRaceDone = true;   // ilk bitiren kazanır
				}
			}
		}
		// hayalet aktörü
		if (Ghosts.IsValidIndex(i) && Ghosts[i])
		{
			const SmvCore::FBotPose& Bp = B.Pilot->Pose();
			double La, Lo;
			SmvCore::LatLon(Req.Track.Lat, Req.Track.Lon, Bp.X, Bp.Y, La, Lo);
			FCaySimPose Pose;
			Pose.Lat = La; Pose.Lon = Lo; Pose.AltMsl = GroundMsl + Bp.Z; Pose.AltRel = Bp.Z;
			Pose.Roll = (float)Bp.RollRad; Pose.Pitch = (float)Bp.PitchRad; Pose.Yaw = FMath::DegreesToRadians((float)Bp.HeadingDeg);
			Pose.QNed = FTelemetryBuffer::NedEulerToQuat(Pose.Roll, Pose.Pitch, Pose.Yaw);
			Pose.Throttle = B.Pilot->bAirborne ? 65.f : 10.f;
			Pose.bValid = true;
			Ghosts[i]->ApplyPoseDirect(Pose);
		}
	}
}

void ASmvGameMode::UpdateTargets()
{
	if (TargetActors.Num() == 0 || !Link()) return;
	const FCaySimTelemetry& T = Link()->LastTele();
	const double Tt = T.Tbm / 1000.0;
	const double HomeAlt = T.bHasHome ? T.HomeAlt : GroundMsl;
	for (int32 i = 0; i < TargetActors.Num() && i < Req.Targets.Num(); ++i)
	{
		const FSmvTarget& Tg = Req.Targets[i];
		SmvCore::FTargetCfg C;
		C.I = i + 1; C.AltM = Tg.AltM; C.DiameterM = Tg.DiameterM; C.SpeedMs = FMath::Max(1.0, Tg.SpeedMs); C.EastM = Tg.EastM; C.NorthM = Tg.NorthM;
		C.PhaseDeg = SmvCore::DefaultPhase(i); C.bCw = Tg.bCw;
		const SmvCore::FTargetState S = SmvCore::TargetAt(C, Tt);
		double La, Lo;
		SmvCore::TargetLatLon(Req.Track.Lat, Req.Track.Lon, S.X, S.Y, La, Lo);
		FCaySimPose Pose;
		Pose.Lat = La; Pose.Lon = Lo; Pose.AltMsl = HomeAlt + S.Z; Pose.AltRel = S.Z;
		Pose.Roll = (float)S.RollRad; Pose.Pitch = 0.f; Pose.Yaw = (float)S.YawRad;
		Pose.QNed = FTelemetryBuffer::NedEulerToQuat(Pose.Roll, 0.f, Pose.Yaw);
		Pose.Throttle = 70.f;
		Pose.bValid = true;
		if (TargetActors[i]) TargetActors[i]->ApplyPoseDirect(Pose);
	}
}

void ASmvGameMode::UpdateHud()
{
	USmvGameInstance* GI = USmvGameInstance::GetSmv(this);
	USmvFlightLink* L = Link();
	ASmvPilotPawn* Pw = GetPilot();
	if (!GI || !L || !Drone) return;
	const FCaySimTelemetry& T = L->LastTele();
	const FSmvTeleExtra& X = L->LastExtra();
	const FSmvSettings& Set = GI->GetData()->Settings();
	if (Hud)
	{
		FCaySimHudContext Ctx;
		Drone->GetRenderedPose(Ctx.RollRad, Ctx.PitchRad, Ctx.YawRad);
		Ctx.GimbalDeg = Pw ? Pw->GetGimbalPitchDeg() : 0.f;
		Ctx.FovDeg = Pw ? Pw->GetFov() : 75.f;
		Ctx.bSitlMode = true;
		Ctx.bGcsEverConnected = X.bGcsConnected;
		Ctx.WindMs = (float)X.WindSpd;
		Ctx.WindDir = (float)X.WindDir;
		Ctx.T0 = L->Now() - (FPlatformTime::Seconds() - SceneT0);
		Ctx.bCollision = bCrashed;
		Hud->Update(T);
		Hud->SetContext(Ctx);
		const bool bHudMode = Pw && Pw->IsHudOn() && (Pw->GetMode() == ESmvCamMode::Fpv || Pw->GetMode() == ESmvCamMode::Gimbal) && Set.GaugeTheme == TEXT("military");
		Hud->SetVisibility(bHudMode ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (!Overlay) return;
	FSmvHudState S;
	const double LinkAge = L->LinkAgeMs();
	USimumavClient* C = GI->GetClient();
	S.AccountLine = C->IsLoggedIn() ? FString::Printf(TEXT("simumav.com — %s"), *C->GetUser().DisplayName)
		: FSmvI18n::Format(TEXT("hud.guest"), { { TEXT("name"), Req.Pilot.Name } }).ToString();
	S.BackendLine = FString::Printf(TEXT("%s · %s×"), L->Backend() == ESmvFlightBackend::Cloud ? *FSmvI18n::T(TEXT("hud.cloud")).ToString() : *FSmvI18n::T(TEXT("hud.local")).ToString(),
		*FString::SanitizeFloat(X.Speedup));
	S.MpLine = L->MissionPlannerInfo();
	S.ModeLine = FString::Printf(TEXT("%s · %s"), T.Mode.IsEmpty() ? TEXT("—") : *T.Mode, T.bArmed ? TEXT("ARMED") : TEXT("DISARMED"));
	S.Alerts = ActiveAlerts;
	S.bAlertUnacked = bAlertUnacked;
	S.bElec = X.bElec; S.ElecPct = (float)X.ElecPct;
	S.ElecText = FString::Printf(TEXT("%.0f%%  %.1f V  %.0f W%s"), X.ElecPct, X.ElecV, X.ElecW, X.ElecMinLeft >= 0 ? *FString::Printf(TEXT("  %.0f dk"), X.ElecMinLeft) : TEXT(""));
	S.bFuel = X.bFuel; S.FuelPct = (float)X.FuelPct;
	S.FuelText = FString::Printf(TEXT("%.2f L  %.0f%%  %.1f km"), X.FuelL, X.FuelPct, X.RangeKm);
	if (Rings && Rings->Num() && MyTracker)
	{
		S.bRace = true;
		const double Tt = bRaceStarted && RaceStartWall > 0 ? FPlatformTime::Seconds() - RaceStartWall : 0;
		S.RaceTitle = Flow->Race.Kind == TEXT("rings") ? FSmvI18n::T(TEXT("race.rings")).ToString() : FSmvI18n::T(TEXT("race.gates")).ToString();
		S.RaceLine1 = FString::Printf(TEXT("%d / %d   %02d:%04.1f"), MyTracker->Passed(), Rings->Num(), (int32)(Tt / 60), FMath::Fmod(Tt, 60.0));
		S.RaceLine2 = MyTracker->Finished() ? FSmvI18n::T(TEXT("race.finished")).ToString() : FString::Printf(TEXT("→ %.0f m"), MyTracker->DistNext());
		for (const FBot& B : Bots) if (B.Tracker) S.Standings.Add(FString::Printf(TEXT("%s  %d/%d%s"), *B.Name, B.Tracker->Passed(), Rings->Num(), B.FinishT >= 0 ? TEXT(" (bitti)") : TEXT("")));
	}
	if (Dogfight)
	{
		S.bDogfight = true; S.bRace = true;
		S.RaceTitle = FSmvI18n::T(bPlayerChaser ? TEXT("race.df_chaser") : TEXT("race.df_evader")).ToString();
		S.RaceLine1 = FString::Printf(TEXT("%.0f m · %.0f°"), Dogfight->LastDist(), Dogfight->LastOffDeg());
		S.RaceLine2 = Dogfight->Guard() ? FSmvI18n::T(TEXT("race.df_guard")).ToString() : FString();
		const SmvCore::FDogfightCfg Dc = SmvCore::FDogfightCfg::Defaults(SmvCore::ClassOfType(TCHAR_TO_UTF8(*Req.Drone.Type)));
		S.HoldFrac = (float)(Dogfight->HoldS() / Dc.HoldS);
		S.bInCone = Dogfight->InCone();
		S.DfLine = FString::Printf(TEXT("%.1f / %.0f sn"), Dogfight->HoldS(), Dc.HoldS);
	}
	if (!bRaceStarted && RaceStartWall > 0)
	{
		const double Left = RaceStartWall - FPlatformTime::Seconds();
		S.Countdown = Left > 3.0 ? FString() : FString::FromInt(FMath::CeilToInt(Left));
	}
	S.Events = Events;
	S.Rc = Pw ? Pw->LastRc() : TArray<int32>();
	S.bShowSticks = Set.InputDevice != TEXT("rc");
	S.Pip = Pw && Pw->IsPipOn() ? Pw->GetPipTarget() : nullptr;
	S.bOsd = Set.GaugeTheme == TEXT("fpv_osd") && Pw && Pw->IsHudOn();
	if (S.bOsd)
	{
		S.OsdLeft = FString::Printf(TEXT("%.0f km/h"), T.Groundspeed * 3.6f);
		S.OsdRight = FString::Printf(TEXT("%.0f m"), T.AltRel);
		S.OsdCenter = FString::Printf(TEXT("%s  %.1fV  %d%%"), *T.Mode, T.BattV, (int32)(X.bElec ? X.ElecPct : T.BattPct));
	}
	// hedef nişangâhı (en yakın hedef)
	if (TargetActors.Num() && Pw)
	{
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
		{
			float Best = 1e12f; FVector2D BestPos = FVector2D::ZeroVector; bool bAny = false; int32 BestI = 0;
			for (int32 i = 0; i < TargetActors.Num(); ++i)
			{
				FVector2D Sp;
				if (!TargetActors[i] || !PC->ProjectWorldLocationToScreen(TargetActors[i]->GetActorLocation(), Sp, true)) continue;
				const float D = (float)FVector::DistSquared(TargetActors[i]->GetActorLocation(), Drone->GetActorLocation());
				if (D < Best) { Best = D; BestPos = Sp; bAny = true; BestI = i + 1; }
			}
			S.bTargetOnScreen = bAny;
			S.TargetPos = BestPos;
			S.TargetLabel = FString::Printf(TEXT("T%d  %.0f m"), BestI, FMath::Sqrt(Best) / 100.f);
		}
	}
	if (FPlatformTime::Seconds() < ToastUntil) { S.Toast = ToastText; S.ToastSeverity = ToastSev; }
	S.bCrashed = bCrashed;
	if (bPauseMenu) S.Paused = FSmvI18n::T(TEXT("flight.paused_hint")).ToString();
	Overlay->SetState(S);

	// ---- uçuş arayüzü ----
	if (FlightUi.IsValid())
	{
		FSmvFlightUiState U;
		U.Title = Req.Drone.Name.IsEmpty() ? Req.Drone.Type : Req.Drone.Name;
		U.Account = S.AccountLine;
		U.Backend = S.BackendLine;
		U.MpLine = S.MpLine;
		U.CamLabel = Pw ? ASmvPilotPawn::ModeLabel(Pw->GetMode()) : FString();
		U.ModeKind = SmvModeKind(Req.Drone.Type);
		U.bPlane = U.ModeKind == TEXT("plane") || U.ModeKind == TEXT("vtol") || U.ModeKind == TEXT("icept");
		U.bRover = U.ModeKind == TEXT("rover");
		U.Modes = SmvFlightModes(U.ModeKind);
		U.bTele = LinkAge >= 0.0;
		U.bFresh = LinkAge >= 0.0 && LinkAge < 1500.0;
		U.bArmed = T.bArmed;
		U.bMotorCut = X.bMotorCut;
		U.bGcs = X.bGcsConnected;
		U.bHasHome = T.bHasHome;
		U.Mode = T.Mode;
		float Rr = 0, Rp = 0, Ry = 0;
		Drone->GetRenderedPose(Rr, Rp, Ry);
		U.Roll = Rr; U.Pitch = Rp;
		U.Heading = T.Heading;
		U.AltRel = (float)T.AltRel; U.AltMsl = (float)T.AltMsl;
		U.Gs = T.Groundspeed; U.As = T.Airspeed; U.Climb = T.Climb; U.Throttle = T.Throttle;
		U.BattV = T.BattV; U.BattPct = T.BattPct; U.Speedup = (float)X.Speedup;
		U.DistHome = T.bHasHome ? (float)CaySimGeo::HaversineM(T.Lat, T.Lon, T.HomeLat, T.HomeLon) : 0.f;
		U.GpsFix = T.GpsFix; U.Sats = T.Sats;
		U.bFuel = X.bFuel; U.FuelPct = (float)X.FuelPct; U.FuelL = (float)X.FuelL; U.RangeKm = (float)X.RangeKm;
		U.bElec = X.bElec; U.ElecPct = (float)X.ElecPct; U.ElecV = (float)X.ElecV; U.ElecW = (float)X.ElecW; U.ElecMin = (float)X.ElecMinLeft;
		const double MaxMs = Req.Power.MaxSpeedMs > 3 ? Req.Power.MaxSpeedMs : Req.Drone.Power.MaxSpeedMs > 3 ? Req.Drone.Power.MaxSpeedMs : (U.bPlane ? 45.0 : U.bRover ? 8.0 : 18.0);
		U.MaxKmh = (float)(MaxMs * 3.6);
		U.Log = UiLog;
		if (Pw)
		{
			U.StickL = Pw->GetStickL(); U.StickR = Pw->GetStickR();
			U.Ch5 = Pw->GetCh5(); U.bThrUsed = Pw->IsThrottleEngaged();
			U.bHideGauges = (Pw->GetMode() == ESmvCamMode::Fpv || Pw->GetMode() == ESmvCamMode::Gimbal) && Pw->IsHudOn();
		}
		U.bStickMode2 = Set.bStickMode2;
		U.bRcDevice = Set.InputDevice == TEXT("rc");
		U.FlightS = FPlatformTime::Seconds() - SceneT0;
		U.bMenuOpen = bPauseMenu;
		FlightUi->SetState(U);
	}
}

// ---------------------------------------------------------------------------
// özet
// ---------------------------------------------------------------------------

void ASmvGameMode::AugmentSummary(FSmvFlightSummary& S)
{
	if (Gpws)
	{
		Gpws->Close(SmvCore::NaN);
		const SmvCore::FGpwsSummary G = SmvCore::SummarizeAlerts(Gpws->Episodes(), Gpws->Cfg().Mode);
		if (S.AlertScore < 0 && G.Score >= 0) { S.AlertScore = G.Score; S.AlertGrade = UTF8_TO_TCHAR(G.Grade.c_str()); S.Warnings = G.Warnings; S.Cautions = G.Cautions; }
	}
	if (MyTracker && Rings)
	{
		if (S.GatesPassed == 0) S.GatesPassed = MyTracker->Passed();
		if (S.GatesMissed == 0 && !MyTracker->Finished()) S.GatesMissed = Rings->Num() - MyTracker->Passed();
		if (MyTracker->Finished()) S.Laps = FMath::Max(S.Laps, 1);
	}
	if (!RaceResultBadge.IsEmpty()) S.Badges.AddUnique(RaceResultBadge);
	if (Flow.IsValid() && Flow->GetMatch() && Flow->GetMatch()->HasMatch())
	{
		USmvGameInstance* GI = USmvGameInstance::GetSmv(this);
		const FSmvMatch& M = Flow->GetMatch()->Current();
		if (GI && M.Winner != 0 && M.Winner == GI->GetClient()->GetUser().Id) S.Badges.AddUnique(M.Kind == TEXT("dogfight") ? TEXT("dogfight_win") : TEXT("race_win"));
	}
}

// ---------------------------------------------------------------------------
// Fab haritası: görünür olunca kendi gök/ışık aktörlerini kaldır (dünya kurulumu güneş/gök/sisi zaten sağlar),
// sınır kutusu merkezini orijine (SITL ev konumu) ve merkezdeki zemini Z=0'a (düz dünya zemini) taşı.
// ---------------------------------------------------------------------------

void ASmvGameMode::HandleFabLevelShown()
{
	UWorld* W = GetWorld();
	if (!W || FabLevels.Num() == 0 || bFabPlaced) return;
	// kalıcı seviye ilk görünür olduğunda alt seviyelerini (kendi dünyasının akış listesi) aynı dönüşümle yükle:
	// seviye örneği olarak yüklenen bir dünyanın alt seviyeleri kendiliğinden gelmez
	if (!bFabSubsRequested)
	{
		bFabSubsRequested = true;
		ULevel* PL = FabLevels[0] ? FabLevels[0]->GetLoadedLevel() : nullptr;
		UWorld* LW = PL ? PL->GetTypedOuter<UWorld>() : nullptr;
		if (LW && LW != W)
		{
			for (ULevelStreaming* Sub : LW->GetStreamingLevels())
			{
				if (!Sub) continue;
				const FString Pkg = Sub->GetWorldAssetPackageName();
				if (Pkg.IsEmpty()) continue;
				bool bOk = false;
				ULevelStreamingDynamic* Lv = ULevelStreamingDynamic::LoadLevelInstance(W, Pkg, Sub->LevelTransform.GetLocation(), Sub->LevelTransform.Rotator(), bOk);
				if (bOk && Lv)
				{
					Lv->OnLevelShown.AddDynamic(this, &ASmvGameMode::HandleFabLevelShown);
					FabLevels.Add(Lv);
					UE_LOG(LogSimumav, Log, TEXT("Fab alt seviyesi yükleniyor: %s"), *Pkg);
				}
				else UE_LOG(LogSimumav, Warning, TEXT("Fab alt seviyesi yüklenemedi: %s"), *Pkg);
			}
		}
	}
	for (const TObjectPtr<ULevelStreamingDynamic>& Lv : FabLevels) if (!Lv || !Lv->IsLevelVisible()) return;
	PlaceFabLevels();
}

// Tüm Fab seviyeleri görünür: kendi gök/ışık aktörlerini kaldır (dünya kurulumu güneş/gök/sisi zaten sağlar),
// ortak sınır kutusunun merkezini orijine (SITL ev konumu) ve merkezdeki zemini Z=0'a (düz dünya zemini) taşı.
void ASmvGameMode::PlaceFabLevels()
{
	UWorld* W = GetWorld();
	if (!W) return;
	bFabPlaced = true;
	FBox Box(ForceInit);
	TArray<AActor*> Drop;
	TArray<ULevel*> Levels;
	for (const TObjectPtr<ULevelStreamingDynamic>& Lv : FabLevels)
	{
		ULevel* L = Lv ? Lv->GetLoadedLevel() : nullptr;
		if (!L) continue;
		Levels.Add(L);
		for (AActor* A : L->Actors)
		{
			if (!IsValid(A)) continue;
			const FString Cn = A->GetClass()->GetName();
			if (A->IsA<ADirectionalLight>() || A->IsA<ASkyLight>() || A->IsA<ASkyAtmosphere>() || A->IsA<AExponentialHeightFog>() || A->IsA<AVolumetricCloud>()
				|| Cn.Contains(TEXT("SkySphere")) || Cn.Contains(TEXT("Sky_Sphere")))
			{
				Drop.Add(A);
				continue;
			}
			TInlineComponentArray<UStaticMeshComponent*> Comps(A);
			for (UStaticMeshComponent* SC : Comps)
			{
				if (!SC || !SC->IsRegistered() || !SC->GetStaticMesh()) continue;
				const FBox SB = SC->Bounds.GetBox();
				if (SB.IsValid && SB.GetSize().GetMax() < 2.0e6) Box += SB;   // 20 km'den büyük (gök küresi vb.) sayılmaz
			}
		}
	}
	for (AActor* A : Drop) A->Destroy();
	if (!Box.IsValid)
	{
		UE_LOG(LogSimumav, Warning, TEXT("Fab haritası boş görünüyor (statik ağ yok)"));
		return;
	}
	const FVector Ctr = Box.GetCenter();
	double Zg = Box.Min.Z;
	FHitResult Hit;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(SmvFabGround), true);
	if (W->LineTraceSingleByChannel(Hit, FVector(Ctr.X, Ctr.Y, Box.Max.Z + 100.0), FVector(Ctr.X, Ctr.Y, Box.Min.Z - 100.0), ECC_WorldStatic, Q)) Zg = Hit.ImpactPoint.Z;
	const FVector Off(-Ctr.X, -Ctr.Y, -Zg);
	for (ULevel* L : Levels) L->ApplyWorldOffset(Off, false);
	UE_LOG(LogSimumav, Log, TEXT("Fab haritası yerleştirildi: %d seviye, kutu %s, merkez (%.0f, %.0f), zemin %.0f cm, %d gök/ışık aktörü kaldırıldı"),
		Levels.Num(), *Box.GetSize().ToString(), Ctr.X, Ctr.Y, Zg, Drop.Num());
}
