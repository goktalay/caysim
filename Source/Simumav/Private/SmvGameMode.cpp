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
#include "SmvMenuScene.h"
#include "SmvFpvActor.h"
#include "SmvPilotFigure.h"
#include "Engine/StaticMeshActor.h"
#include "SmvMusicSynth.h"
#include "SmvUiPrefs.h"
#include "SmvUiKit.h"
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
#include "Engine/StaticMeshActor.h"
#include "GameFramework/PlayerStart.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/ConfigCacheIni.h"
#include "Engine/StaticMesh.h"
#include "Blueprint/UserWidget.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
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
		UiTones = NewObject<USmvToneSynth>(ToneHost, TEXT("UiTones"));
		UiTones->SetupAttachment(R);
		UiTones->RegisterComponent();
		UiTones->Start();
		Music = NewObject<USmvMusicSynth>(ToneHost, TEXT("Music"));
		Music->SetupAttachment(R);
		Music->RegisterComponent();
		Music->Start();
		TWeakObjectPtr<ASmvGameMode> WG(this);
		FSmvUiSound::SetSink([WG](int32 Kind) { if (WG.IsValid()) WG->PlayUiSound(Kind); });
	}
	ApplyUiPrefs();
	HandleScreen(Flow->GetScreen());
}

void ASmvGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
	FSmvUiSound::SetSink(nullptr);
	if (MenuScene) { MenuScene->Destroy(); MenuScene = nullptr; }
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
	UpdateMenuScene(Screen);
	UpdateMusic(Screen);
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
		// Fab haritası (satın alınmış içerik; pakette varsa; kayıt tablosu SmvMapRegistry.cpp) — yoksa prosedürel dünya
		bool bFab = false;
		ClearFabWorld();
		if (FSmvFabContent::IsFabWorld(Req.Track.WorldMode))
		{
			if (const FSmvFabMap* FM = FSmvFabContent::FindMap(Req.Track.WorldMode))
			{
				FabMapKey = FM->Key;
				if (FM->Kind == ESmvMapKind::ProcCity)
				{
					SpawnFabCity();
					bFab = true;
				}
				else
				{
					bool bOk = false;
					ULevelStreamingDynamic* Lv = ULevelStreamingDynamic::LoadLevelInstance(W, FM->Level, FVector::ZeroVector, FRotator::ZeroRotator, bOk);
					if (bOk && Lv)
					{
						Lv->OnLevelShown.AddDynamic(this, &ASmvGameMode::HandleFabLevelShown);
						FabLevels.Add(Lv);
						bFab = true;
						UE_LOG(LogSimumav, Log, TEXT("Fab haritası yükleniyor: %s (%s)"), *FM->Level, *FM->Key);
					}
				}
			}
			if (!bFab) PushEvent(FSmvI18n::T(TEXT("flight.fab_missing")).ToString(), 4);
		}
		if (!bFab)
		{
			Terrain = W->SpawnActor<ASmvProcTerrain>();
			if (Terrain) Terrain->Build(Req.Track, GroundMsl, WorldSetup);
			SpawnFabDecor();
		}
		// Fab Ultra Dynamic Sky (varsa) yerleşik güneş/gök/sis/bulut yerine; saat ve hava parkurdan
		TArray<AActor*> SkyActors;
		if (FSmvFabContent::SpawnSky(W, Req.Track, SkyActors))
		{
			WorldSetup->DisableBuiltinSky();
			for (AActor* A : SkyActors) FabSkyActors.Add(A);
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
		P->StickMode = Set.StickMode;
		P->RemapKeys();
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
		if (Flow->Race.Kind == TEXT("none") && !(Flow->GetMatch() && Flow->GetMatch()->HasMatch())) P->PlayIntro(3.7f);
	}
	// RC kumanda tutan 3B pilot: ev konumunun 8 m gerisinde, 3 m solunda (pist yönüne göre), drona bakar
	{
		const double Yaw = Req.Track.World.RunwayHeading;
		const double Fe = FMath::Sin(FMath::DegreesToRadians(Yaw)), Fn = FMath::Cos(FMath::DegreesToRadians(Yaw));
		const double Re = Fn, Rn = -Fe;
		const double E0 = -Fe * 8.0 - Re * 3.0, N0 = -Fn * 8.0 - Rn * 3.0;
		double La, Lo, La2, Lo2;
		SmvCore::LatLon(Req.Track.Lat, Req.Track.Lon, E0, N0, La, Lo);
		SmvCore::LatLon(Req.Track.Lat, Req.Track.Lon, E0 + Fe * 10.0, N0 + Fn * 10.0, La2, Lo2);
		const FVector P0 = Geo(La, Lo, GroundMsl), P1 = Geo(La2, Lo2, GroundMsl);
		FActorSpawnParameters Sp;
		Sp.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		PilotFigure = W->SpawnActor<ASmvPilotFigure>(ASmvPilotFigure::StaticClass(), FTransform(FRotator(0.0, (P1 - P0).Rotation().Yaw, 0.0), P0), Sp);
		if (PilotFigure) PilotFigure->Build(Req.Pilot.Avatar, true);
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
	// ---- hazır FPV parkuru (zaman denemesi) ----
	FpvEndAt = 0;
	if (Flow->Fpv.IsActive())
	{
		if (const FSmvFpvCourse* Fc = SmvFpv::Find(Flow->Fpv.CourseId))
		{
			FActorSpawnParameters Sp;
			Sp.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			FpvActor = W->SpawnActor<ASmvFpvCourseActor>(ASmvFpvCourseActor::StaticClass(), FTransform::Identity, Sp);
			if (FpvActor)
			{
				TWeakObjectPtr<ASmvGameMode> WG(this);
				FpvActor->Init(*Fc, Flow->Fpv, [WG](double E, double N, double Up)
				{
					ASmvGameMode* G = WG.Get();
					if (!G) return FVector(E * 100.0, -N * 100.0, Up * 100.0);
					double La, Lo;
					SmvCore::LatLon(G->Req.Track.Lat, G->Req.Track.Lon, E, N, La, Lo);
					return G->Geo(La, Lo, G->GroundMsl + Up);
				}, Req.Drone.Type);
			}
		}
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
	bRaceStarted = bRaceDone = false; MyFinishT = -1;
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
	if (FpvActor) { FpvActor->Destroy(); FpvActor = nullptr; }
	if (PilotFigure) { PilotFigure->Destroy(); PilotFigure = nullptr; }
	for (TObjectPtr<AActor>& Db : Debris) if (Db) Db->Destroy();
	Debris.Reset(); DebrisVel.Reset();
	if (Terrain) { Terrain->Destroy(); Terrain = nullptr; }
	ClearFabWorld();
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
		CrashFx();
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
		if (Tones) Tones->Sfx(18);
		PushEvent(FString::Printf(TEXT("ÖNLEME T%d — %.1f m%s"), (int32)SmvJson::Num(M, TEXT("i")), SmvJson::Num(M, TEXT("dist_m")), SmvJson::Bool(M, TEXT("locked")) ? TEXT(" (kilitli)") : TEXT("")), 5);
		return;
	}
	if (T == TEXT("engage")) { if (Tones) Tones->Sfx(15); PushEvent(FString::Printf(TEXT("ANGAJMAN %s T%d"), *SmvJson::Str(M, TEXT("kind")), (int32)SmvJson::Num(M, TEXT("i"))), 5); return; }
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
		PushEvent(FSmvI18n::Format(TEXT("fui.mode_req"), { { TEXT("mode"), Arg } }).ToString(), 6);
		return;
	}
	if (Name == TEXT("IntroBeep")) { if (Tones) Tones->Blip(Arg == TEXT("go") ? 4 : 3); return; }
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
	// radyal mod menüsü açıkken: A / F seçer, B / Esc kapatır
	if (FlightUi.IsValid() && FlightUi->IsRadialOpen())
	{
		if (A == TEXT("Arm") || A == TEXT("ModeMenu")) { FlightUi->ConfirmRadial(); return; }
		if (A == TEXT("Ack") || A == TEXT("Pause")) { FlightUi->CloseRadial(); return; }
	}
	if (A == TEXT("Pause") && Shell.IsValid() && Shell->IsOverlayOpen()) { Shell->ToggleHelp(); return; }
	if (A == TEXT("Help")) { if (Shell.IsValid()) Shell->ToggleHelp(); return; }
	if (A == TEXT("Cockpit")) { if (FlightUi.IsValid()) FlightUi->ToggleCockpit(); return; }
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
			PushEvent(FSmvI18n::T(TEXT("fui.motor_cut_glide")).ToString(), 4);
			return;
		}
		if (!bPlaneLike && Tl.AltRel > 1.5 && Now > ArmConfirmUntil)
		{
			ArmConfirmUntil = Now + 2.5;
			HandleToast(FSmvI18n::T(TEXT("fui.disarm_confirm")).ToString(), 4);
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
	TickDebris(Dt);
	UpdateRace(Dt);
	UpdateTargets();
	// ses
	if (Drone)
	{
		const FVector L = GetPilot() ? GetPilot()->GetActorLocation() : Drone->GetActorLocation();
		const USmvGameInstance* GI = USmvGameInstance::GetSmv(this);
		const FSmvSettings& S = GI->GetData()->Settings();
		Drone->UpdateAudio(L, S.bSound ? (float)S.Volume * FSmvUiPrefs::Get().VolMotor : 0.f);
		for (TObjectPtr<ASmvDroneActor>& G : Ghosts) if (G) G->UpdateAudio(L, S.bSound ? (float)S.Volume * FSmvUiPrefs::Get().VolMotor * 0.6f : 0.f);
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
	if (Tones) Tones->SetAlarm(S.GpwsMode == TEXT("full") && S.bAlertTones ? Alarm : 0, (S.bSound ? (float)S.Volume : 0.f) * FSmvUiPrefs::Get().VolAlerts);
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
			if (E.Kind == "ring") { if (Tones) Tones->Sfx(E.I >= Rings->Num() ? 12 : 11, 1.f + 0.05f * E.I); PushEvent(FString::Printf(TEXT("HALKA %d/%d"), E.I, Rings->Num()), 6); }
			if (E.Kind == "wrong_way") { if (Tones) Tones->Sfx(14); }
			if (E.Kind == "wrong_way") PushEvent(FString::Printf(TEXT("TERS YÖN — halka %d"), E.I), 4);
			if (E.Kind == "finish")
			{
				PushEvent(FString::Printf(TEXT("BİTİŞ %.1f sn"), Tt), 5);
				MyFinishT = Tt;
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
					if (E.Kind == "lock_on" && Tones) Tones->Sfx(15);
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
	UpdateFpv(T, FPlatformTime::Seconds());
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
		U.StickMode = Set.StickMode;
		U.TrackName = Req.Track.Name;
		U.WindMs = (float)Req.Track.WindMs; U.WindDir = (float)Req.Track.WindDir; U.WindGust = (float)Req.Track.WindGust;
		U.bIntroAllowed = Flow->Race.Kind == TEXT("none") && !(Flow->GetMatch() && Flow->GetMatch()->HasMatch());
		if (FpvActor && FpvActor->GetTracker())
		{
			const FSmvFpvTracker& Tk = *FpvActor->GetTracker();
			const double Now = FPlatformTime::Seconds();
			U.bFpv = true;
			U.bFpvFinished = Tk.Finished();
			U.FpvLap = Tk.Lap(); U.FpvLaps = Tk.Cfg().Laps;
			U.FpvNext = Tk.NextIndex() + 1; U.FpvTotalCp = Tk.Total();
			U.FpvLapT = Tk.LapStart() >= 0 && !Tk.Finished() ? (float)(Now - Tk.LapStart()) : 0.f;
			U.FpvBest = (float)Tk.BestLap();
			U.FpvTotal = Tk.Finished() ? (float)Tk.TotalS() : (float)(Now - SceneT0 - 3.7 + Tk.Penalty());
			U.FpvPen = (float)Tk.Penalty(); U.FpvDist = (float)Tk.DistNext();
			U.FpvMisses = Tk.Misses(); U.FpvHits = Tk.Hits();
			const double Gd = FpvActor->GhostDelta();
			U.FpvGhost = Gd > 1e8 ? 1e9f : (float)Gd;
			U.FpvSectorCount = FMath::Max(1, Tk.Course().Sectors);
			if (Tk.SectorTimes().Num())
			{
				const TArray<double>& Cur = Tk.SectorTimes().Last();
				const FSmvFpvRecord& Rec = FpvActor->GetRecord();
				for (int32 k = 0; k < Cur.Num(); ++k)
				{
					const double* Best = Tk.BestSectors().Find(k + 1);
					U.FpvSectors.Add(Best && FMath::IsNearlyEqual(*Best, Cur[k], 1e-3) ? (Rec.TotalS < 0 ? 2 : 1) : 3);
				}
			}
		}
		if (Pw && PilotFigure) PilotFigure->SetPose(Pw->GetStickL(), Pw->GetStickR(), Drone->GetActorLocation(), true);
		if (Pw)
		{
			U.PadRight = Pw->GetPadRight();
			Pw->bSuppressRightPad = FlightUi->IsRadialOpen();
		}
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
	if (Flow.IsValid() && Flow->LastFpv.bValid && Flow->LastFpv.bNewPb) S.Badges.AddUnique(TEXT("fpv_pb"));
	if (Tones && Flow.IsValid() && Flow->Race.Kind != TEXT("none")) Tones->Sfx(RaceResultBadge.IsEmpty() ? 17 : 16);
	// podyum + oyun puanı (yerel yarış / it dalaşı)
	if (Flow.IsValid())
	{
		Flow->LastPodium.Reset();
		Flow->LastScore = -1;
		Flow->LastRaceKind = Flow->Race.Kind;
		if (MyTracker && Rings && Rings->Num() && Flow->Race.Kind == TEXT("rings"))
		{
			FSmvPodiumRow Me;
			Me.Name = Req.Pilot.Name.IsEmpty() ? FString(TEXT("Pilot")) : Req.Pilot.Name;
			Me.Passed = MyTracker->Passed(); Me.Total = Rings->Num(); Me.FinishT = MyTracker->Finished() ? MyFinishT : -1; Me.bMe = true;
			Flow->LastPodium.Add(Me);
			for (const FBot& B : Bots)
			{
				if (!B.Tracker) continue;
				FSmvPodiumRow R; R.Name = B.Name; R.Passed = B.Tracker->Passed(); R.Total = Rings->Num(); R.FinishT = B.FinishT;
				Flow->LastPodium.Add(R);
			}
			Flow->LastPodium.Sort([](const FSmvPodiumRow& A, const FSmvPodiumRow& B)
			{
				const bool Fa = A.FinishT >= 0, Fb = B.FinishT >= 0;
				if (Fa != Fb) return Fa;
				if (Fa) return A.FinishT < B.FinishT;
				return A.Passed > B.Passed;
			});
			int32 Rank = 1;
			for (int32 i = 0; i < Flow->LastPodium.Num(); ++i) if (Flow->LastPodium[i].bMe) Rank = i + 1;
			const bool bFin = Me.FinishT >= 0;
			Flow->LastScore = Me.Passed * 100 + (bFin ? 250 + (Rank == 1 ? 150 : Rank == 2 ? 100 : Rank == 3 ? 50 : 0) : 0);
		}
		else if (Dogfight)
		{
			const bool bWon = RaceResultBadge == TEXT("dogfight_win");
			Flow->LastScore = (bWon ? 1000 : 0) + (bPlayerChaser ? FMath::Min(500, FMath::RoundToInt(Dogfight->HoldS() * 100.0)) : 0);
		}
	}
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

// Tüm Fab seviyeleri görünür: kendi gök/ışık aktörlerini kaldır (Ultra Dynamic Sky / dünya kurulumu güneş-gök-sisi sağlar),
// kalkış noktasını bul ve onu orijine (SITL ev konumu), oradaki zemini Z=0'a (SITL'in düz zemini) taşı.
// Kalkış noktası sırası: ini MapStart=<anahtar>|X,Y (harita cm) → haritanın PlayerStart'ı (üstü açıksa) → merkeze en yakın
// düz ve üstü açık zemin (arazi haritasında Landscape üstünde) → sınır kutusu merkezi; ardından kayıt tablosu kaydırması (StartE/N).
void ASmvGameMode::PlaceFabLevels()
{
	UWorld* W = GetWorld();
	if (!W) return;
	bFabPlaced = true;
	FBox Box(ForceInit);
	TArray<AActor*> Drop;
	TArray<ULevel*> Levels;
	TArray<FVector> PlayerStarts;
	bool bLandscape = false;
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
				|| Cn.Contains(TEXT("SkySphere")) || Cn.Contains(TEXT("Sky_Sphere")) || Cn.Contains(TEXT("Ultra_Dynamic")) || Cn.StartsWith(TEXT("BP_Sky")))
			{
				Drop.Add(A);
				continue;
			}
			// gök kubbesi/küresi ağları (ör. US_Military MM_Sky; ağ ya da malzeme adında "Sky") — Ultra Dynamic Sky'ı örtmesin
			bool bSkyMesh = false;
			{
				TInlineComponentArray<UStaticMeshComponent*> SkyComps(A);
				for (UStaticMeshComponent* SC : SkyComps)
				{
					if (!SC || !SC->GetStaticMesh()) continue;
					const FString Mn = SC->GetStaticMesh()->GetName().ToLower();
					const UMaterialInterface* Mat = SC->GetMaterial(0);
					const FString Mt = Mat ? Mat->GetName().ToLower() : FString();
					const bool bNamed = (Mn.Contains(TEXT("sky")) && !Mn.Contains(TEXT("scraper"))) || ((Mt.StartsWith(TEXT("mm_sky")) || Mt.StartsWith(TEXT("m_sky")) || Mt.StartsWith(TEXT("mi_sky"))));
					if (bNamed || SC->Bounds.GetBox().GetSize().GetMax() > 2.0e6) { bSkyMesh = true; break; }
				}
			}
			if (bSkyMesh)
			{
				Drop.Add(A);
				continue;
			}
			if (A->IsA<APlayerStart>()) PlayerStarts.Add(A->GetActorLocation());
			if (Cn.Contains(TEXT("Landscape"))) bLandscape = true;
			TInlineComponentArray<UStaticMeshComponent*> Comps(A);
			for (UStaticMeshComponent* SC : Comps)
			{
				if (!SC || !SC->IsRegistered() || !SC->GetStaticMesh()) continue;
				const FBox SB = SC->Bounds.GetBox();
				if (SB.IsValid && SB.GetSize().GetMax() < 2.0e6) Box += SB;   // 20 km'den büyük (gök küresi vb.) sayılmaz
			}
			if (Cn.Contains(TEXT("Landscape")))
			{
				const FBox LB = A->GetComponentsBoundingBox(true);
				if (LB.IsValid && LB.GetSize().GetMax() < 2.0e6) Box += LB;
			}
		}
	}
	for (AActor* A : Drop) A->Destroy();
	if (!Box.IsValid)
	{
		UE_LOG(LogSimumav, Warning, TEXT("Fab haritası boş görünüyor (statik ağ yok)"));
		return;
	}
	FCollisionQueryParams Q(SCENE_QUERY_STAT(SmvFabGround), true);
	const double ZTop = Box.Max.Z + 100.0, ZBot = Box.Min.Z - 100.0;
	auto Down = [&](double X, double Y, FHitResult& H) { return W->LineTraceSingleByChannel(H, FVector(X, Y, ZTop), FVector(X, Y, ZBot), ECC_WorldStatic, Q); };
	auto IsLand = [](const FHitResult& H) { return H.GetComponent() && H.GetComponent()->GetClass()->GetName().Contains(TEXT("Landscape")); };
	// zemin + üstü açık mı (ilk isabet yukarıdan; 6 m çevresi düz)
	auto Clear = [&](double X, double Y, double& OutZ) -> bool
	{
		FHitResult H;
		if (!Down(X, Y, H) || H.ImpactNormal.Z < 0.95) return false;
		if (bLandscape && !IsLand(H)) return false;
		for (const FVector2D& O : { FVector2D(600, 0), FVector2D(-600, 0), FVector2D(0, 600), FVector2D(0, -600) })
		{
			FHitResult H2;
			if (!Down(X + O.X, Y + O.Y, H2) || FMath::Abs(H2.ImpactPoint.Z - H.ImpactPoint.Z) > (bLandscape ? 120.0 : 60.0)) return false;
		}
		OutZ = H.ImpactPoint.Z;
		return true;
	};
	const FSmvMapDef* Def = FSmvMapRegistry::Find(FabMapKey);
	FVector Start = Box.GetCenter();
	double Zg = Box.Min.Z;
	FString How = TEXT("sınır kutusu merkezi");
	bool bFound = false;
	// 1) ayar: [/Script/Simumav.SmvFabContent] +MapStart=<anahtar>|X,Y   (harita koordinatı, cm)
	TArray<FString> StartLines;
	if (GConfig) GConfig->GetArray(TEXT("/Script/Simumav.SmvFabContent"), TEXT("MapStart"), StartLines, GGameIni);
	for (const FString& Ln : StartLines)
	{
		FString K, V, Xs, Ys;
		if (Ln.Split(TEXT("|"), &K, &V) && K.TrimStartAndEnd() == FabMapKey && V.Split(TEXT(","), &Xs, &Ys))
		{
			Start = FVector(FCString::Atod(*Xs), FCString::Atod(*Ys), 0.0);
			FHitResult H;
			Zg = Down(Start.X, Start.Y, H) ? H.ImpactPoint.Z : Box.Min.Z;
			How = TEXT("ayar (MapStart)");
			bFound = true;
		}
	}
	// 2) haritanın PlayerStart'ı — altında zemin ve üstü açıksa (bina içindeki başlangıç dron için uygun değil)
	for (int32 i = 0; i < PlayerStarts.Num() && !bFound; ++i)
	{
		const FVector P = PlayerStarts[i];
		FHitResult Hd, Hu;
		if (!W->LineTraceSingleByChannel(Hd, P + FVector(0, 0, 200), P - FVector(0, 0, 5000), ECC_WorldStatic, Q)) continue;
		if (W->LineTraceSingleByChannel(Hu, Hd.ImpactPoint + FVector(0, 0, 150), Hd.ImpactPoint + FVector(0, 0, 6000), ECC_WorldStatic, Q)) continue;
		Start = Hd.ImpactPoint; Zg = Hd.ImpactPoint.Z; How = TEXT("PlayerStart"); bFound = true;
	}
	// 3) merkeze en yakın düz, üstü açık zemin (20 m adımlı halkalar, 300 m'ye dek)
	for (int32 r = 0; r <= 15 && !bFound; ++r)
	{
		const int32 N = r == 0 ? 1 : 8 * r;
		for (int32 k = 0; k < N && !bFound; ++k)
		{
			const double Ang = 2.0 * UE_DOUBLE_PI * k / N;
			const double X = Box.GetCenter().X + r * 2000.0 * FMath::Cos(Ang), Y = Box.GetCenter().Y + r * 2000.0 * FMath::Sin(Ang);
			double Z = 0;
			if (Clear(X, Y, Z)) { Start = FVector(X, Y, Z); Zg = Z; How = FString::Printf(TEXT("düz açıklık (merkezden %d m)"), r * 20); bFound = true; }
		}
	}
	if (!bFound)
	{
		FHitResult Hit;
		if (Down(Start.X, Start.Y, Hit)) Zg = Hit.ImpactPoint.Z;
	}
	// 4) kayıt tablosu kaydırması (doğu = +X, kuzey = −Y)
	if (Def && (Def->StartE != 0.0 || Def->StartN != 0.0))
	{
		Start.X += Def->StartE * 100.0;
		Start.Y -= Def->StartN * 100.0;
		FHitResult H;
		if (Down(Start.X, Start.Y, H)) Zg = H.ImpactPoint.Z;
		How += TEXT(" + tablo kaydırması");
	}
	const FVector Off(-Start.X, -Start.Y, -Zg);
	for (ULevel* L : Levels) L->ApplyWorldOffset(Off, false);
	UE_LOG(LogSimumav, Log, TEXT("Fab haritası yerleştirildi (%s): %d seviye, kutu %s, kalkış (%.0f, %.0f, %.0f) — %s; %d gök/ışık aktörü kaldırıldı"),
		*FabMapKey, Levels.Num(), *Box.GetSize().ToString(), Start.X, Start.Y, Zg, *How, Drop.Num());
	if (Def && Def->bDecor) SpawnFabDecor(FVector::ZeroVector, true);
}

// Sahne dekoru: Fab statik modelleri (ör. tank, hava savunma, anti-dron sistemi) kalkış noktasının doğusunda
// park hâlinde durur. Yalnız görsel — davranış, hedef/atış/güdüm mantığı ya da etkileşim YOKTUR.
// Ayar: [/Script/Simumav.SmvFabContent] PropsEnabled
void ASmvGameMode::SpawnFabDecor(const FVector& Origin, bool bTraceGround)
{
	UWorld* W = GetWorld();
	bool bOn = true;
	if (GConfig) GConfig->GetBool(TEXT("/Script/Simumav.SmvFabContent"), TEXT("PropsEnabled"), bOn, GGameIni);
	if (!W || !bOn) return;
	const TArray<UStaticMesh*>& Props = FSmvFabContent::Props();
	FCollisionQueryParams Q(SCENE_QUERY_STAT(SmvFabDecorGround), true);
	int32 Placed = 0;
	for (int32 i = 0; i < Props.Num() && i < 8; ++i)
	{
		UStaticMesh* M = Props[i];
		const FBoxSphereBounds B = M->GetBounds();
		const double L = 2.0 * FMath::Max3(B.BoxExtent.X, B.BoxExtent.Y, B.BoxExtent.Z);
		// gerçekçi araç boyu 2–30 m değilse 8 m'ye ölçekle (birim farkı)
		const double S = (L < 200.0 || L > 3000.0) ? 800.0 / FMath::Max(L, 1.0) : 1.0;
		const double E = 110.0 + 25.0 * i, N = 70.0;   // m (doğu, kuzey); düz ev alanı 300 m içinde
		FVector Loc = Origin + FVector(E * 100.0, -N * 100.0, 0.0);
		if (bTraceGround)
		{
			FHitResult H;
			if (W->LineTraceSingleByChannel(H, Loc + FVector(0, 0, 30000), Loc - FVector(0, 0, 30000), ECC_WorldStatic, Q)) Loc.Z = H.ImpactPoint.Z;
		}
		Loc.Z += -(B.Origin.Z - B.BoxExtent.Z) * S;
		AStaticMeshActor* A = W->SpawnActor<AStaticMeshActor>(Loc, FRotator(0.0, 90.0 + 20.0 * i, 0.0));
		if (!A || !A->GetStaticMeshComponent()) continue;
		A->SetMobility(EComponentMobility::Movable);
		A->GetStaticMeshComponent()->SetStaticMesh(M);
		A->SetActorScale3D(FVector(S));
		FabDecor.Add(A);
		++Placed;
	}
	if (Placed) UE_LOG(LogSimumav, Log, TEXT("Fab sahne dekoru: %d model (yalnız görsel)"), Placed);
}

// Kodla kurulan şehir bloğu: 5×5 adalık ızgara (ada 70 m, cadde 30 m), ortadaki ada kalkış meydanı (boş).
// İç halka: European Buildings ağları (adada 2×2 bina); dış halka: City Sample bina seviyeleri (yoksa yine European ağları).
// Binalar yalnız görseldir (SITL zemini düzdür; çarpışma fiziği yok).
void ASmvGameMode::SpawnFabCity()
{
	UWorld* W = GetWorld();
	if (!W) return;
	const TArray<UStaticMesh*>& Meshes = FSmvFabContent::CityMeshes();
	const TArray<FString> Towers = FSmvFabContent::CityTowerLevels();
	const double Cell = 10000.0, Road = 3000.0;
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	auto Slab = [&](const FVector& Ctr, const FVector& SizeCm, const FLinearColor& C, bool bCollide = false)
	{
		if (!Cube) return;
		AStaticMeshActor* A = W->SpawnActor<AStaticMeshActor>(Ctr, FRotator::ZeroRotator);
		if (!A || !A->GetStaticMeshComponent()) return;
		A->SetMobility(EComponentMobility::Movable);
		UStaticMeshComponent* SC = A->GetStaticMeshComponent();
		SC->SetStaticMesh(Cube);
		if (!bCollide) SC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		A->SetActorScale3D(SizeCm / 100.0);   // BasicShapes küpü 100 cm
		if (BaseMat)
		{
			UMaterialInstanceDynamic* D = UMaterialInstanceDynamic::Create(BaseMat, SC);
			D->SetVectorParameterValue(TEXT("Color"), C);
			SC->SetMaterial(0, D);
		}
		FabDecor.Add(A);
	};
	// zemin (3 km; üstü Z 0 = SITL düz zemini), caddeler (koyu asfalt) ve kalkış meydanı (açık beton)
	const double Span = 5.0 * Cell + Road;
	Slab(FVector(0, 0, -50), FVector(300000, 300000, 100), FLinearColor(0.16f, 0.2f, 0.12f), true);
	for (int32 k = -2; k <= 3; ++k)
	{
		const double C = (k - 0.5) * Cell;
		Slab(FVector(C, 0, 2), FVector(Road, Span, 4), FLinearColor(0.035f, 0.035f, 0.04f));
		Slab(FVector(0, C, 2), FVector(Span, Road, 4), FLinearColor(0.035f, 0.035f, 0.04f));
	}
	Slab(FVector(0, 0, 3), FVector(Cell - Road, Cell - Road, 4), FLinearColor(0.42f, 0.42f, 0.4f));
	int32 NMesh = 0, NTower = 0, Pick = 0;
	auto PlaceMesh = [&](const FVector& Ctr, double MaxFoot, int32 Yaw90)
	{
		if (Meshes.Num() == 0) return;
		UStaticMesh* M = Meshes[(Pick++ * 7 + 3) % Meshes.Num()];
		const FBoxSphereBounds B = M->GetBounds();
		const double Foot = 2.0 * FMath::Max(B.BoxExtent.X, B.BoxExtent.Y);
		double S = (Foot < 300.0 || Foot > 20000.0) ? 2500.0 / FMath::Max(Foot, 1.0) : 1.0;   // birim farkı → 25 m
		if (Foot * S > MaxFoot) S = MaxFoot / Foot;
		const FRotator Rot(0.0, 90.0 * Yaw90, 0.0);
		FVector Loc = Ctr - Rot.RotateVector(FVector(B.Origin.X, B.Origin.Y, 0.0) * S);
		Loc.Z = -(B.Origin.Z - B.BoxExtent.Z) * S;
		AStaticMeshActor* A = W->SpawnActor<AStaticMeshActor>(Loc, Rot);
		if (!A || !A->GetStaticMeshComponent()) return;
		A->SetMobility(EComponentMobility::Movable);
		A->GetStaticMeshComponent()->SetStaticMesh(M);
		A->SetActorScale3D(FVector(S));
		FabDecor.Add(A);
		++NMesh;
	};
	for (int32 i = -2; i <= 2; ++i)
	{
		for (int32 j = -2; j <= 2; ++j)
		{
			if (i == 0 && j == 0) continue;   // kalkış meydanı
			const FVector Ctr(i * Cell, j * Cell, 0.0);
			const int32 Ring = FMath::Max(FMath::Abs(i), FMath::Abs(j));
			const int32 Yaw90 = ((i * 3 + j * 5) % 4 + 4) % 4;
			if (Ring == 2 && Towers.Num() > 0)
			{
				bool bOk = false;
				const FString& Pkg = Towers[(NTower * 3 + i + j + 8) % Towers.Num()];
				ULevelStreamingDynamic* Lv = ULevelStreamingDynamic::LoadLevelInstance(W, Pkg, Ctr, FRotator(0.0, 90.0 * Yaw90, 0.0), bOk);
				if (bOk && Lv) { CityLevels.Add(Lv); ++NTower; continue; }
			}
			// ada başına 2×2 bina (her biri en çok 30 m taban)
			for (int32 q = 0; q < 4; ++q)
			{
				const FVector Sub((q & 1) ? 1750.0 : -1750.0, (q & 2) ? 1750.0 : -1750.0, 0.0);
				PlaceMesh(Ctr + Sub, 3200.0, (Yaw90 + q) % 4);
			}
		}
	}
	UE_LOG(LogSimumav, Log, TEXT("Şehir bloğu kuruldu: %d bina ağı, %d City Sample bina seviyesi"), NMesh, NTower);
}

void ASmvGameMode::ClearFabWorld()
{
	for (TArray<TObjectPtr<ULevelStreamingDynamic>>* Arr : { &FabLevels, &CityLevels })
	{
		for (TObjectPtr<ULevelStreamingDynamic>& Lv : *Arr)
		{
			if (!Lv) continue;
			Lv->OnLevelShown.RemoveAll(this);
			Lv->SetIsRequestingUnloadAndRemoval(true);
		}
		Arr->Reset();
	}
	for (TObjectPtr<AActor>& A : FabSkyActors) if (A) A->Destroy();
	for (TObjectPtr<AActor>& A : FabDecor) if (A) A->Destroy();
	FabSkyActors.Reset();
	FabDecor.Reset();
	bFabSubsRequested = false;
	bFabPlaced = false;
	FabMapKey.Reset();
}

// ---------------------------------------------------------------------------
// arayüz: menü 3B sahnesi, müzik, tercihler
// ---------------------------------------------------------------------------

void ASmvGameMode::UpdateMenuScene(ESmvScreen Screen)
{
	const bool bMenu = Screen != ESmvScreen::Loading && Screen != ESmvScreen::Flight;
	if (bMenu && !MenuScene)
	{
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		MenuScene = GetWorld()->SpawnActor<ASmvMenuScene>(ASmvMenuScene::StaticClass(), FTransform::Identity, P);
	}
	else if (!bMenu && MenuScene)
	{
		MenuScene->Destroy();
		MenuScene = nullptr;
	}
}

void ASmvGameMode::UpdateMusic(ESmvScreen Screen)
{
	if (!Music) return;
	USmvGameInstance* GI = USmvGameInstance::GetSmv(this);
	if (!GI || !GI->GetData()) return;
	const FSmvSettings& S = GI->GetData()->Settings();
	const FSmvUiPrefs& P = FSmvUiPrefs::Get();
	float Ctx = 1.f;
	if (Screen == ESmvScreen::Loading || Screen == ESmvScreen::Flight) Ctx = 0.f;
	else if (Screen == ESmvScreen::Wizard || Screen == ESmvScreen::Lobby) Ctx = 0.7f;
	const float Lv = (S.bSound && S.bMusic && P.bMenuMusic) ? (float)S.Volume * P.VolMusic * Ctx : 0.f;
	Music->SetLevel(Lv * 0.8f);
}

void ASmvGameMode::ApplyUiPrefs()
{
	USmvGameInstance* GI = USmvGameInstance::GetSmv(this);
	if (!GI || !GI->GetData()) return;
	const FSmvSettings& S = GI->GetData()->Settings();
	const FSmvUiPrefs& P = FSmvUiPrefs::Get();
	SmvUi::ReducedMotion() = S.bReducedMotion;
	SmvUi::ApplyTheme(S.Theme);
	if (UiTones) UiTones->SetUiGain((S.bSound ? (float)S.Volume : 0.f) * P.VolUi);
	UpdateMusic(Flow.IsValid() ? Flow->GetScreen() : ESmvScreen::Menu);
	if (ASmvPilotPawn* Pw = GetPilot())
	{
		Pw->StickMode = S.StickMode;
		Pw->InputDevice = S.InputDevice;
		Pw->Expo = (float)S.StickExpo;
		Pw->RcAxisMap = S.RcAxisMap;
		Pw->RcInvert = S.RcInvert;
		Pw->RemapKeys();
	}
}

void ASmvGameMode::PlayUiSound(int32 Kind)
{
	if (!bUiSoundsLoaded)
	{
		bUiSoundsLoaded = true;
		// Fab "Airplane Controller & Instruments" ses ipuçları (hazırlama projesinden Content/AirplaneInstruments olarak gelir)
		struct FCue { int32 Key; const TCHAR* Value; };
		const FCue Map[] = {
			{ 6, TEXT("PlasticButton/PlasticButton_Cue") }, { 9, TEXT("PlasticSwitch/PlasticSwitch_Cue") }, { 8, TEXT("MetalSwitch/MetalSwitch_Cue") } };
		for (const FCue& M : Map)
		{
			for (const TCHAR* Root : { TEXT("/Game/AirplaneInstruments"), TEXT("/Game/Fab/AirplaneInstruments") })
			{
				const FString Rel(M.Value);
				FString Name;
				Rel.Split(TEXT("/"), nullptr, &Name);
				const FString Path = FString::Printf(TEXT("%s/Sound/ButtonsAndSwitches/%s.%s"), Root, *Rel, *Name);
				if (USoundBase* S = LoadObject<USoundBase>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet))
				{
					UiSounds.Add(M.Key, S);
					break;
				}
			}
		}
		if (UiSounds.Num()) UE_LOG(LogSimumav, Log, TEXT("Arayüz sesleri: %d Fab ses ipucu"), UiSounds.Num());
	}
	if (const TObjectPtr<USoundBase>* S = UiSounds.Find(Kind))
	{
		USmvGameInstance* GI = USmvGameInstance::GetSmv(this);
		const FSmvSettings* Set = GI && GI->GetData() ? &GI->GetData()->Settings() : nullptr;
		const float Vol = Set && Set->bSound ? (float)Set->Volume * FSmvUiPrefs::Get().VolUi : 0.f;
		if (Vol > 0.01f && *S) UGameplayStatics::PlaySound2D(this, *S, Vol);
		return;
	}
	if (UiTones) UiTones->Blip(Kind);
}

// ---------------------------------------------------------------------------
// hazır FPV parkuru: yarış başlangıcı (giriş sekansı sonrası), kapı / çarpma olayları, hayalet, bitiş ve süre sınırı
// ---------------------------------------------------------------------------

void ASmvGameMode::UpdateFpv(const FCaySimTelemetry& T, double Now)
{
	if (!FpvActor || !Flow.IsValid()) return;
	const double Fs = Now - SceneT0;
	if (!FpvActor->IsRacing())
	{
		if (Fs >= 3.7 && T.Lat != 0.0) { FpvActor->StartRace(Now); PushEvent(FSmvI18n::T(TEXT("fpv.go")).ToString(), 5); }
		return;
	}
	const FSmvFpvTracker* Tk = FpvActor->GetTracker();
	if (FpvEndAt > 0)
	{
		if (Now >= FpvEndAt) { FpvEndAt = 0; Flow->EndFlight(TEXT("fpv_finish")); }
		return;
	}
	if (!Tk || T.Lat == 0.0) return;
	double X, Y;
	SmvCore::Enu(Req.Track.Lat, Req.Track.Lon, T.Lat, T.Lon, X, Y);
	for (const FSmvFpvEvent& E : FpvActor->Feed(X, Y, T.AltRel, T.Heading, Now))
	{
		auto Fmt = [](double S) { return FString::Printf(TEXT("%d:%05.2f"), (int32)(S / 60.0), FMath::Fmod(S, 60.0)); };
		if (E.Kind == TEXT("ring")) { if (Tones) Tones->Sfx(11, 1.f + 0.04f * E.I); }
		else if (E.Kind == TEXT("miss")) PushEvent(FSmvI18n::Format(TEXT("fpv.missed"), { { TEXT("s"), FString::Printf(TEXT("%.0f"), E.PenS) } }).ToString(), 4);
		else if (E.Kind == TEXT("hit")) PushEvent(E.PenS > 0 ? FSmvI18n::Format(TEXT("fpv.hit"), { { TEXT("s"), FString::Printf(TEXT("%.0f"), E.PenS) } }).ToString() : FSmvI18n::T(TEXT("fpv.hit0")).ToString(), 3);
		else if (E.Kind == TEXT("reset")) { Command(TEXT("reset")); PushEvent(FSmvI18n::T(TEXT("fpv.reset")).ToString(), 4); }
		else if (E.Kind == TEXT("wrong_way")) PushEvent(FSmvI18n::T(TEXT("fpv.wrong")).ToString(), 4);
		else if (E.Kind == TEXT("sector") && E.bBest) PushEvent(FSmvI18n::T(TEXT("fpv.sector_best")).ToString(), 5);
		else if (E.Kind == TEXT("lap"))
		{
			PushEvent(FSmvI18n::Format(TEXT("fpv.lap_done"), { { TEXT("n"), FString::FromInt(E.Lap) }, { TEXT("t"), Fmt(E.S) } }).ToString(), 5);
			if (E.bBest && E.Lap > 1) PushEvent(FSmvI18n::T(TEXT("fpv.best_lap_new")).ToString(), 5);
		}
		else if (E.Kind == TEXT("finish"))
		{
			PushEvent(FSmvI18n::Format(TEXT("fpv.finished"), { { TEXT("t"), Fmt(E.TotalS) } }).ToString(), 5);
			const double PrevPb = FpvActor->GetRecord().TotalS;
			const bool bPb = FpvActor->SaveIfRecord();
			if (bPb) PushEvent(FSmvI18n::T(TEXT("fpv.new_pb")).ToString(), 5);
			if (Tones) Tones->Sfx(bPb ? 20 : 12);
			USmvFlow::FFpvResult& R = Flow->LastFpv;
			R = USmvFlow::FFpvResult();
			R.bValid = true; R.bFinished = true; R.bNewPb = bPb; R.PrevPbS = PrevPb;
			R.CourseId = FpvActor->GetCourse().Id; R.Laps = Tk->Cfg().Laps;
			R.TotalS = Tk->TotalS(); R.PenS = Tk->Penalty(); R.BestLapS = Tk->BestLap();
			R.Misses = Tk->Misses(); R.Hits = Tk->Hits();
			R.LapTimes = Tk->Laps(); R.Sectors = Tk->SectorTimes();
			R.Score = SmvFpv::Score(FpvActor->GetCourse(), R.TotalS, R.Laps);
			FpvEndAt = Now + 4.0;
		}
	}
	FpvActor->UpdateGhost(Now);
	// süre sınırı
	if (!Tk->Finished() && Fs > 3.7 + Tk->Cfg().TimeLimitMin * 60.0)
	{
		USmvFlow::FFpvResult& R = Flow->LastFpv;
		R = USmvFlow::FFpvResult();
		R.bValid = true; R.bFinished = false; R.CourseId = FpvActor->GetCourse().Id; R.Laps = Tk->Cfg().Laps;
		R.PenS = Tk->Penalty(); R.BestLapS = Tk->BestLap(); R.Misses = Tk->Misses(); R.Hits = Tk->Hits();
		R.LapTimes = Tk->Laps(); R.Sectors = Tk->SectorTimes();
		FpvEndAt = Now + 0.5;
	}
}

// ---------------------------------------------------------------------------
// çarpma efekti: darbe sesi, ekran parlaması, kamera sarsıntısı, enkaz parçaları
// ---------------------------------------------------------------------------

void ASmvGameMode::CrashFx()
{
	if (Tones) Tones->Sfx(19);
	if (FlightUi.IsValid()) FlightUi->Flash(FLinearColor(1.f, 0.55f, 0.3f, 0.75f));
	if (ASmvPilotPawn* P = GetPilot()) P->Shake(60.f, 0.8f);
	if (!Drone || !GetWorld()) return;
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!Cube) return;
	const FVector C = Drone->GetActorLocation();
	const double AltRel = Link() ? Link()->LastTele().AltRel : 0.0;
	DebrisGroundZ = C.Z - FMath::Max(0.0, AltRel) * 100.0;
	DebrisUntil = FPlatformTime::Seconds() + 8.0;
	for (int32 i = 0; i < 12; ++i)
	{
		FActorSpawnParameters Sp;
		Sp.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* A = GetWorld()->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform(FRotator(FMath::FRandRange(0.f, 360.f), FMath::FRandRange(0.f, 360.f), 0.f), C), Sp);
		if (!A || !A->GetStaticMeshComponent()) continue;
		A->SetMobility(EComponentMobility::Movable);
		A->GetStaticMeshComponent()->SetStaticMesh(Cube);
		A->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		const float Sz = FMath::FRandRange(0.04f, 0.16f);
		A->SetActorScale3D(FVector(Sz, Sz * FMath::FRandRange(0.5f, 1.5f), Sz * 0.4f));
		Debris.Add(A);
		DebrisVel.Add(FVector(FMath::FRandRange(-600.f, 600.f), FMath::FRandRange(-600.f, 600.f), FMath::FRandRange(200.f, 900.f)));
	}
}

void ASmvGameMode::TickDebris(float Dt)
{
	if (Debris.Num() == 0) return;
	if (FPlatformTime::Seconds() > DebrisUntil)
	{
		for (TObjectPtr<AActor>& Db : Debris) if (Db) Db->Destroy();
		Debris.Reset(); DebrisVel.Reset();
		return;
	}
	for (int32 i = 0; i < Debris.Num(); ++i)
	{
		AActor* A = Debris[i];
		if (!A || !DebrisVel.IsValidIndex(i)) continue;
		FVector P = A->GetActorLocation();
		if (P.Z <= DebrisGroundZ + 2.0) { DebrisVel[i] = FVector::ZeroVector; continue; }
		DebrisVel[i].Z -= 981.0 * Dt;
		P += DebrisVel[i] * Dt;
		if (P.Z < DebrisGroundZ) P.Z = DebrisGroundZ;
		A->SetActorLocationAndRotation(P, A->GetActorRotation() + FRotator(200.0 * Dt, 140.0 * Dt, 0.0));
	}
}
