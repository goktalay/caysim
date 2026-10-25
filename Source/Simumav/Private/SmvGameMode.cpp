#include "SmvGameMode.h"
#include "SmvLaunchRig.h"
#include "SmvGameRulesActor.h"
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
#include "SmvCityLife.h"
#include "SmvFpvActor.h"
#include "SmvPilotFigure.h"
#include "SmvSpeech.h"
#include "SmvVideoRec.h"
#include "SmvSiteActor.h"
#include "SmvHoliday.h"
#include "Engine/StaticMeshActor.h"
#include "SmvMusicSynth.h"
#include "SmvUiPrefs.h"
#include "SmvUiKit.h"
#include "CesiumWorldSetup.h"
#include "HudWidget.h"
#include "TelemetryBuffer.h"
#include "SmvGpws.h"
#include "SmvAlertPolicy.h"
#include "SmvRace.h"
#include "SmvBot.h"
#include "SmvTargets.h"
#include "SmvSitlConfig.h"
#include "SmvFabContent.h"
#include "SmvMapRegistry.h"
#include "SmvPhysics.h"
#include "SmvScenery.h"
#include "SmvLoadGate.h"
#include "SmvLocalFlightLink.h"
#include "Engine/PostProcessVolume.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/SWindow.h"
#include "Framework/Application/SlateApplication.h"
#include "SmvInputGlyphs.h"
#include "Materials/Material.h"
#include "Misc/PackageName.h"
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
#include "HAL/IConsoleManager.h"

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
	// geliştirme derlemesinde ekrana basılan VSM taşma uyarısı (çok yerel ışık) arayüzün üstüne yazılmasın
	if (IConsoleVariable* Cv = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shadow.Virtual.AllowScreenOverflowMessages"))) Cv->Set(0, ECVF_SetByCode);
	Super::BeginPlay();
	USmvGameInstance* GI = USmvGameInstance::GetSmv(this);
	if (!GI) { UE_LOG(LogSimumav, Error, TEXT("SmvGameInstance yok — DefaultEngine.ini GameInstanceClass")); return; }
	Flow = GI->GetFlow();
	Flow->OnScreenChanged.AddDynamic(this, &ASmvGameMode::HandleScreen);
	Flow->OnToast.AddDynamic(this, &ASmvGameMode::HandleToast);
	Flow->SummaryAugmenter = [this](FSmvFlightSummary& S) { AugmentSummary(S); };
	// Yükleniyor → Uçuş: bağlantı hazır olsa da sahne yükleme kapısı (harita, dekor, atmosfer, SITL GPS/EKF, PSO, doku, ön ısınma) bitmeli
	Flow->FlightGate = [this]() { return !bScene || Gate.IsDone(); };

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
	// akışla gelen seviyeler (Fab haritası + alt seviyeler + LevelInstance + şehir binaları): kapalı çarpışmaları aç
	LevelAddedHandle = FWorldDelegates::LevelAddedToWorld.AddUObject(this, &ASmvGameMode::HandleLevelAdded);
	HandleScreen(Flow->GetScreen());
}

void ASmvGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
	FSmvUiSound::SetSink(nullptr);
	if (LevelAddedHandle.IsValid()) { FWorldDelegates::LevelAddedToWorld.Remove(LevelAddedHandle); LevelAddedHandle.Reset(); }
	if (MenuScene) { MenuScene->Destroy(); MenuScene = nullptr; }
	StopScene();
	if (Flow.IsValid())
	{
		Flow->OnScreenChanged.RemoveAll(this);
		Flow->OnToast.RemoveAll(this);
		Flow->SummaryAugmenter = nullptr;
		Flow->FlightGate = nullptr;
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
	EnsureMouse();
	if (ASmvPilotPawn* P = GetPilot()) P->SetFlying(Screen == ESmvScreen::Flight);
	if (Screen == ESmvScreen::Flight && bScene && FlightEnterAt < SceneT0)
	{
		// yükleme kapısı bitti: giriş anı (kabuk kararmadan açılır), hızlı göz uyumu kaldırılır, tanıtım kamerası şimdi oynar
		FlightEnterAt = FPlatformTime::Seconds();
		Gate.LogSummary();
		SMV_KLOG(TEXT("SMVLOAD [%s] uçuşa giriş: %.2f sn (sahne kurulumundan)"), *Gate.Label(), FlightEnterAt - SceneT0);
		if (LoadPp) { LoadPp->Destroy(); LoadPp = nullptr; }
		if (bIntroPending) if (ASmvPilotPawn* P = GetPilot()) P->PlayIntro(3.7f);
		bIntroPending = false;
	}
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
	// yükleme kapısı: Uçuş ekranı ancak tüm aşamalar bitince (TickLoadGate)
	Gate.Begin(TEXT("ucus"), { ESmvLoad::World, ESmvLoad::Scenery, ESmvLoad::Atmos, ESmvLoad::Sitl, ESmvLoad::Shaders, ESmvLoad::Streaming, ESmvLoad::Warmup });
	AtmosT0 = WarmT0 = StreamT0 = 0; AtmosF0 = WarmF0 = 0; WarmRecap = 0; StreamPeak = ShaderPeak = 0;
	RimAt = 0; RimStartAt = 0; bRimStarted = false; bTilesSkipped = false; FabPlacedBox = FBox(ForceInit); FabLevelPath.Reset();
	SmvPhysics::SetClipRegion(nullptr, FBox2D(ForceInit));
	FlightEnterAt = -100.0;
	bIntroPending = false;
	// yükleme boyunca göz uyumu hızlı: giriş karesinde pozlama oturmuş olsun (yalnız hız alanları ezilir; UDS pozlama eğrisi korunur)
	{
		FActorSpawnParameters Sp;
		Sp.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		LoadPp = W->SpawnActor<APostProcessVolume>(APostProcessVolume::StaticClass(), FTransform::Identity, Sp);
		if (LoadPp)
		{
			LoadPp->bUnbound = true;
			LoadPp->Priority = 1000.f;
			LoadPp->BlendWeight = 1.f;
			LoadPp->Settings.bOverride_AutoExposureSpeedUp = true; LoadPp->Settings.AutoExposureSpeedUp = 30.f;
			LoadPp->Settings.bOverride_AutoExposureSpeedDown = true; LoadPp->Settings.AutoExposureSpeedDown = 30.f;
		}
	}

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
	const bool bCesiumOk = ACaySimWorldSetup::CesiumCompiled() && !WorldSetup->CesiumIonToken.IsEmpty();
	if (bReal && bCesiumOk) Src = Req.Track.WorldMode == TEXT("google3d") ? ECaySimWorldSource::GooglePhotoreal : ECaySimWorldSource::CesiumIon;
	// Fab haritası gerçek bir bölgenin içine yerleştirilir (kayıt tablosu demiri): çevre Google Photorealistic 3D Tiles (ya da World Terrain +
	// Bing uydu), harita alanı karolardan kırpılır, kenar yükseklikleri eşlenir (TickLoadGate → AlignRealRim). Ayarlar → Görüntü → Gerçek dünya.
	const FSmvMapDef* FabDef = FSmvFabContent::IsFabWorld(Req.Track.WorldMode) ? FSmvMapRegistry::Find(Req.Track.WorldMode.Mid(4)) : nullptr;
	const FString RealPref = FSmvUiPrefs::Get().RealWorld;
	bRealSurround = false; bRimPending = false; bRimDone = false;
	if (FabDef && FabDef->Lat != 0.0 && bCesiumOk && RealPref != TEXT("off"))
	{
		Src = RealPref == TEXT("terrain") ? ECaySimWorldSource::CesiumIon : ECaySimWorldSource::GooglePhotoreal;
		bRealSurround = true;
		// harita zemini (kalkış) = Unreal Z 0 = ev MSL; yalnız gerçek dünya haritasında merkez yüzeyi ölçülür
		if (!FabDef->bRealOnly) WorldSetup->SetFixedGroundOffset(WorldSetup->GeoidOffsetM);
	}
	WorldSetup->Build(S, nullptr, Src);
	const bool bFlat = WorldSetup->GetSource() == ECaySimWorldSource::Flat;
	SMV_KLOG(TEXT("Uçuş dünyası: %s (parkur %s, gerçek çevre %s, Cesium %s, belirteç %s) — %.5f, %.5f, zemin %.0f m MSL"), *WorldSetup->SourceName(), *Req.Track.WorldMode,
		bRealSurround ? TEXT("evet") : TEXT("hayır"), ACaySimWorldSetup::CesiumCompiled() ? TEXT("derlemede") : TEXT("yok"), WorldSetup->CesiumIonToken.IsEmpty() ? TEXT("yok") : TEXT("var"), Req.Track.Lat, Req.Track.Lon, GroundMsl);
	if (bFlat && bReal) PushEvent(FSmvI18n::T(TEXT("flight.no_cesium")).ToString(), 4);
	// Fab haritası (satın alınmış içerik; pakette varsa; kayıt tablosu SmvMapRegistry.cpp) — yoksa (düz dünyada) prosedürel dünya
	bool bFab = false;
	ClearFabWorld();
	if (FSmvFabContent::IsFabWorld(Req.Track.WorldMode))
	{
		if (FabDef && FabDef->bRealOnly && !bFlat)
		{
			// haritanın kendi sahnesi yok: gerçek bölge (3D Tiles) olduğu gibi
			FabMapKey = FabDef->Key;
			bFab = true;
		}
		else if (const FSmvFabMap* FM = FSmvFabContent::FindMap(Req.Track.WorldMode))
		{
			FabMapKey = FM->Key;
			if (FM->Kind == ESmvMapKind::ProcCity)
			{
				SpawnFabCity();
				bFab = true;
				if (!bFlat) bRimPending = true;   // şehir bloğu: kırpma + kenar eşleme (kutu: şehir dekoru)
			}
			else bFab = LoadFabLevel(FM->Level);
		}
		if (!bFab) PushEvent(FSmvI18n::T(TEXT("flight.fab_missing")).ToString(), 4);
	}
	if (!bFab && bFlat)
	{
		Terrain = W->SpawnActor<ASmvProcTerrain>();
		if (Terrain) Terrain->Build(Req.Track, GroundMsl, WorldSetup);
		SpawnFabDecor();
	}
	// atmosfer (düz dünya ve Cesium gerçek dünya; SmvAtmosphere): Ultra Dynamic Sky + Weather varsa yerleşik güneş/gök/sis/bulut yerine;
	// saat (gerçek güneş konumu), hava, rüzgâr parkurdan; post-process hacmi (Lumen, pozlama, renk) her durumda
	{
		TArray<AActor*> SkyActors;
		if (FSmvFabContent::SpawnSky(W, Req.Track, SkyActors)) WorldSetup->DisableBuiltinSky();
		for (AActor* A : SkyActors) FabSkyActors.Add(A);
	}
	// gökte uzak trafik (Flying AI ağı; yalnız görsel — davranış / hedef yok)
	if (FSmvScenery::Enabled() && WorldSetup->GetSource() == ECaySimWorldSource::Flat)
	{
		FActorSpawnParameters Sp;
		Sp.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (ASmvSkyTraffic* St = W->SpawnActor<ASmvSkyTraffic>(ASmvSkyTraffic::StaticClass(), FTransform::Identity, Sp))
		{
			if (St->Build(Geo(Req.Track.Lat, Req.Track.Lon, GroundMsl), 4, 170000.0, 24000.0, 60000.0, Req.Track.World.Seed)) FabDecor.Add(St);
			else St->Destroy();
		}
	}

	// ---- dron ----
	Drone = W->SpawnActor<ASmvDroneActor>();
	Drone->SetGeoTransformActor(WorldSetup);
	Drone->Setup(S, nullptr);
	Drone->ConfigureVisual(Req.Drone.Type, Req.Drone.ModelKey, SmvColor(Req.Drone.Color));
	// gerçek çarpışma: gövde şekli her karede SITL pozuna süpürülür (SmvPhysics); hayalet/hedef dronlarda kapalı
	Drone->EnablePhysicsContact(true);
	ProxLine.Reset(); ProxLevel = 0; LastSensorAt = 0; LastGrazeEventAt = 0;
	TWeakObjectPtr<ASmvGameMode> Weak(this);
	if (USmvFlightLink* L = Link())
	{
		TWeakObjectPtr<USmvFlightLink> WL(L);
		Drone->SetClockSource([WL]() { return WL.IsValid() ? WL->Now() : 0.0; });
		L->OnTele.AddUObject(this, &ASmvGameMode::HandleTele);
		L->OnMessage.AddUObject(this, &ASmvGameMode::HandleMessage);
	}
	bGlbPending = true;
	Data->FetchModelGlb(Req.Drone.ModelKey, [Weak](bool bOk, const TArray<uint8>& Bytes)
	{
		ASmvGameMode* Self = Weak.Get();
		if (!Self) return;
		Self->bGlbPending = false;
		if (bOk && Self->Drone) Self->Drone->LoadGlbBytes(Bytes);
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
		P->SetCamType(Req.Camera == TEXT("thermal") ? ESmvCamType::Thermal : Req.Camera == TEXT("night") ? ESmvCamType::Night
			: Req.Camera == TEXT("multispectral") ? ESmvCamType::Multispectral : ESmvCamType::Day);
		P->OnRc.AddLambda([Weak](const TArray<int32>& Rc)
		{
			ASmvGameMode* Self = Weak.Get();
			if (Self && Self->Link() && !Self->bPauseMenu) Self->Link()->SendRc(Rc);
		});
		P->OnAction.AddUObject(this, &ASmvGameMode::HandleAction);
		// tanıtım kamerası (100 m yukarıdan inen süzülüş): yükleme ekranı sahneyi takip kamerasının son konumunda ısıttığı için varsayılan
		// kapalı — giriş karesi ısınmış görünümle aynı olsun. [/Script/Simumav.SmvGameMode] FlightIntro=True ile girişte oynar.
		bool bIntroCfg = false;
		if (GConfig) GConfig->GetBool(TEXT("/Script/Simumav.SmvGameMode"), TEXT("FlightIntro"), bIntroCfg, GGameIni);
		bIntroPending = bIntroCfg && Flow->Race.Kind == TEXT("none") && !(Flow->GetMatch() && Flow->GetMatch()->HasMatch());
	}
	// video kaydı (Ayarlar / sihirbaz: uçuş başında başlar)
	if (FSmvUiPrefs::Get().bRecordVideo) ToggleVideo();
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
		if (PilotFigure) if (ASmvPilotPawn* Pp = GetPilot()) Pp->SetPilotEye(P0, PilotFigure->HeadHeightCm());
	}
	// sabit kanat kalkış düzeneği: rampa / araç üstü / elden atış / pist şeridi (SITL kalkış yönünde; SmvLocalFlightLink HomeHeading ile aynı)
	if (Req.Drone.Type == TEXT("plane"))
	{
		const FString Me = Req.Launch.Method.IsEmpty() ? FString(TEXT("wheels")) : Req.Launch.Method;
		const double Hd = (Me == TEXT("ramp") && Req.Launch.AimAzDeg > 0) ? Req.Launch.AimAzDeg : Req.Track.World.RunwayHeading;
		double La2, Lo2;
		SmvCore::LatLon(Req.Track.Lat, Req.Track.Lon, FMath::Sin(FMath::DegreesToRadians(Hd)) * 10.0, FMath::Cos(FMath::DegreesToRadians(Hd)) * 10.0, La2, Lo2);
		const FVector H0 = Geo(Req.Track.Lat, Req.Track.Lon, GroundMsl), H1 = Geo(La2, Lo2, GroundMsl);
		const float HYaw = (float)(H1 - H0).Rotation().Yaw;
		FActorSpawnParameters Sp;
		Sp.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		LaunchRig = W->SpawnActor<ASmvLaunchRig>(ASmvLaunchRig::StaticClass(), FTransform(H0), Sp);
		if (LaunchRig)
		{
			LaunchRig->Build(Me, H0, HYaw, (float)Req.Launch.AngleDeg, Drone);
			// elden atış: pilot uçağın hemen arkasında, kalkış yönüne bakar
			if (Me == TEXT("hand") && PilotFigure)
			{
				PilotFigure->SetActorLocationAndRotation(LaunchRig->PilotSpot(), FRotator(0.f, HYaw, 0.f));
				if (ASmvPilotPawn* Pp = GetPilot()) Pp->SetPilotEye(LaunchRig->PilotSpot(), PilotFigure->HeadHeightCm());
			}
		}
	}

	// ---- HUD ----
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		Hud = CreateWidget<UCaySimHudWidget>(PC, UCaySimHudWidget::StaticClass());
		// alt bant (≈ 48 px) HUD yazılarının üstüne binmesin
		if (Hud) { Hud->AddToViewport(5); Hud->SetVisibility(ESlateVisibility::HitTestInvisible); Hud->SetPadding(FMargin(0.f, 0.f, 0.f, 48.f)); }
		Overlay = CreateWidget<USmvHudOverlay>(PC, USmvHudOverlay::StaticClass());
		if (Overlay) { Overlay->AddToViewport(6); Overlay->SetVisibility(ESlateVisibility::HitTestInvisible); Overlay->SetPadding(FMargin(0.f, 0.f, 0.f, 48.f)); }
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
	delete AlertPolicy; AlertPolicy = new SmvCore::FAlertPolicy();
	LaunchPhaseUi.Reset();

	// ---- hedefler ----
	for (int32 i = 0; i < Req.Targets.Num() && i < 8; ++i)
	{
		if (ASmvDroneActor* T = SpawnGhost(TEXT("plane"), FLinearColor(0.9f, 0.2f, 0.9f))) TargetActors.Add(T);
	}

	// ---- oyun türü (yol haritası): yük bırakma, tarla ilaçlama, kritik tesis / gemi savunma ----
	if (Req.GameMode == TEXT("cargo_drop") || Req.GameMode == TEXT("spraying") || Req.GameMode == TEXT("facility_defense") || Req.GameMode == TEXT("ship_defense"))
	{
		FActorSpawnParameters Rp;
		Rp.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Rules = W->SpawnActor<ASmvGameRulesActor>(ASmvGameRulesActor::StaticClass(), FTransform::Identity, Rp);
		if (Rules)
		{
			const double Lat0 = Req.Track.Lat, Lon0 = Req.Track.Lon, Msl0 = GroundMsl;
			Rules->OnEvent = [this](const FString& Txt, int32 Sev) { PushEvent(Txt, Sev); };
			Rules->OnCommand = [this](const FString& Name, TSharedPtr<FJsonObject> P) { Command(Name, P); };
			Rules->Setup(Req, Flow.Get(), Drone, [this, Lat0, Lon0, Msl0](double E, double N, double Up)
			{
				double La = Lat0, Lo = Lon0;
				SmvCore::TargetLatLon(Lat0, Lon0, E, N, La, Lo);
				return Geo(La, Lo, Msl0 + Up);
			});
		}
	}
	else if (Drone) Drone->SetPayloadAttached(true);

	// ---- halkalar / yarış ----
	SetupRace();
	if (Flow->GetMatch() && Flow->GetMatch()->HasMatch())
	{
		Flow->GetMatch()->OnPositions.AddUObject(this, &ASmvGameMode::HandleMatchPositions);
	}
	// ---- rüzgâr tulumu: kalkışın 12 m sağında ----
	{
		const double Hd = FMath::DegreesToRadians(Req.Track.World.RunwayHeading);
		double La, Lo;
		SmvCore::LatLon(Req.Track.Lat, Req.Track.Lon, 12.0 * FMath::Cos(Hd), -12.0 * FMath::Sin(Hd), La, Lo);
		FActorSpawnParameters Sp;
		Sp.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Sock = W->SpawnActor<ASmvWindSock>(ASmvWindSock::StaticClass(), FTransform(Geo(La, Lo, GroundMsl)), Sp);
		if (Sock) Sock->SetWind((float)Req.Track.WindMs, (float)Req.Track.WindDir);
	}
	// ---- bayram süsleri (tema etkinse) ----
	{
		const FString Th = SmvHoliday::ActiveTheme();
		if (!Th.IsEmpty())
		{
			FActorSpawnParameters Sp;
			Sp.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Decor = W->SpawnActor<ASmvHolidayDecor>(ASmvHolidayDecor::StaticClass(), FTransform::Identity, Sp);
			TWeakObjectPtr<ASmvGameMode> WG(this);
			if (Decor) Decor->Build(Th, Req.Track.World.RunwayHeading, [WG](double E, double N, double Up)
			{
				ASmvGameMode* G = WG.Get();
				if (!G) return FVector(E * 100.0, -N * 100.0, Up * 100.0);
				double La, Lo;
				SmvCore::LatLon(G->Req.Track.Lat, G->Req.Track.Lon, E, N, La, Lo);
				return G->Geo(La, Lo, G->GroundMsl + Up);
			});
		}
	}
	// ---- yer kontrol sahası (istasyon / radar / EO) ----
	if (Req.Site.Any())
	{
		FActorSpawnParameters Sp;
		Sp.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Site = W->SpawnActor<ASmvSiteActor>(ASmvSiteActor::StaticClass(), FTransform::Identity, Sp);
		if (Site)
		{
			TWeakObjectPtr<ASmvGameMode> WG(this);
			Site->Build(Req.Site, Req.Track.World.RunwayHeading, [WG](double E, double N, double Up)
			{
				ASmvGameMode* G = WG.Get();
				if (!G) return FVector(E * 100.0, -N * 100.0, Up * 100.0);
				double La, Lo;
				SmvCore::LatLon(G->Req.Track.Lat, G->Req.Track.Lon, E, N, La, Lo);
				return G->Geo(La, Lo, G->GroundMsl + Up);
			});
		}
		EoSel = 0; EoTypeIdx = 0; bRadarOn = bEoOn = true;
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
	Gate.Clear();
	if (LoadPp) { LoadPp->Destroy(); LoadPp = nullptr; }
	if (VideoRec.IsValid() && VideoRec->IsOn()) { const FString P = VideoRec->Stop(); if (!P.IsEmpty() && Flow.IsValid()) Flow->Toast(FSmvI18n::Format(TEXT("video.saved"), { { TEXT("p"), P } }).ToString(), 6); }
	if (USmvFlightLink* L = Link()) { L->OnTele.RemoveAll(this); L->OnMessage.RemoveAll(this); }
	if (Flow.IsValid() && Flow->GetMatch()) Flow->GetMatch()->OnPositions.RemoveAll(this);
	if (ASmvPilotPawn* P = GetPilot()) { P->OnRc.Clear(); P->OnAction.RemoveAll(this); P->SetFlying(false); P->SetTarget(nullptr, TEXT("multirotor")); }
	if (Hud) { Hud->RemoveFromParent(); Hud = nullptr; }
	if (Overlay) { Overlay->RemoveFromParent(); Overlay = nullptr; }
	if (JetHud) { JetHud->RemoveFromParent(); JetHud = nullptr; }
	bJetHudTried = false;
	if (FlightUiHost.IsValid() && GEngine && GEngine->GameViewport) GEngine->GameViewport->RemoveViewportWidgetContent(FlightUiHost.ToSharedRef());
	FlightUiHost.Reset();
	FlightUi.Reset();
	for (TObjectPtr<ASmvGateActor>& G : Gates) if (G) G->Destroy();
	for (TObjectPtr<ASmvDroneActor>& G : Ghosts) if (G) G->Destroy();
	for (TObjectPtr<ASmvDroneActor>& G : TargetActors) if (G) G->Destroy();
	Gates.Reset(); Ghosts.Reset(); TargetActors.Reset(); GhostIndex.Reset();
	if (Rules) { Rules->Destroy(); Rules = nullptr; }
	if (Drone) { Drone->Destroy(); Drone = nullptr; }
	if (FpvActor) { FpvActor->Destroy(); FpvActor = nullptr; }
	if (PilotFigure) { PilotFigure->Destroy(); PilotFigure = nullptr; }
	if (Site) { Site->Destroy(); Site = nullptr; }
	if (Sock) { Sock->Destroy(); Sock = nullptr; }
	if (Decor) { Decor->Destroy(); Decor = nullptr; }
	for (TObjectPtr<AActor>& Db : Debris) if (Db) Db->Destroy();
	Debris.Reset(); DebrisVel.Reset();
	if (Terrain) { Terrain->Destroy(); Terrain = nullptr; }
	ClearFabWorld();
	if (LaunchRig) { LaunchRig->Destroy(); LaunchRig = nullptr; }
	if (WorldSetup) { WorldSetup->Destroy(); WorldSetup = nullptr; }
	if (Tones) Tones->SetAlarm(0, 0.f);
	FreeRace();
	delete Gpws; Gpws = nullptr; delete AlertPolicy; AlertPolicy = nullptr;
}

// ---------------------------------------------------------------------------
// bağlantı olayları
// ---------------------------------------------------------------------------

void ASmvGameMode::PushEvent(const FString& Text, int32 Sev, bool bStrip)
{
	if (Text.IsEmpty()) return;
	if (bStrip)
	{
		Events.Add(TPair<FString, int32>(Text, Sev));
		if (Events.Num() > 40) Events.RemoveAt(0);
	}
	FSmvUiLog E;
	E.Text = Text; E.Sev = Sev; E.Wall = FPlatformTime::Seconds();
	E.Clock = FDateTime::Now().ToString(TEXT("%H:%M:%S"));
	UiLog.Add(MoveTemp(E));
	if (UiLog.Num() > 60) UiLog.RemoveAt(0);
}

// ArduPilot / SITL metin iletileri: PreArm denetimleri ve EKF/AHRS/GPS hizalama bilgileri uyarı şeridine çıkmaz (yalnız günlük paneli);
// ARM gerçekten reddedilince tek özet ("ARM reddedildi: <neden>", 15 sn içinde tekrar yok)
void ASmvGameMode::HandleApText(const FString& Text, int32 Sev)
{
	if (Text.IsEmpty()) return;
	const double Now = FPlatformTime::Seconds();
	if (Text.StartsWith(TEXT("PreArm:")))
	{
		LastPreArm = Text.Mid(7).TrimStartAndEnd();
		LastPreArmAt = Now;
		PushEvent(Text, 6, false);
		return;
	}
	if (Text.StartsWith(TEXT("Arm:")) || Text.Contains(TEXT("Arming failed")) || Text.Contains(TEXT("arming checks failed")))
	{
		ReportArmFail(Text.StartsWith(TEXT("Arm:")) ? Text.Mid(4).TrimStartAndEnd() : (LastPreArm.IsEmpty() ? Text : LastPreArm));
		return;
	}
	static const TCHAR* Info[] = { TEXT("EKF2"), TEXT("EKF3"), TEXT("AHRS:"), TEXT("GPS 1:"), TEXT("GPS 2:"), TEXT("u-blox"), TEXT("Field Elevation"), TEXT("Barometer"),
		TEXT("IMU0:"), TEXT("IMU1:"), TEXT("RCOut:"), TEXT("Frame:"), TEXT("ArduPlane V"), TEXT("ArduCopter V"), TEXT("ArduRover V"), TEXT("ChibiOS"), TEXT("Param space"),
		TEXT("Initialising"), TEXT("Ready to FLY"), TEXT("Calibrating"), TEXT("GPS: u-blox"), TEXT("EKF primary"), TEXT("Throttle disarmed") };
	for (const TCHAR* K : Info)
	{
		if (Text.Contains(K)) { PushEvent(Text, 6, false); return; }
	}
	PushEvent(Text, Sev);
}

void ASmvGameMode::ReportArmFail(const FString& Why)
{
	const double Now = FPlatformTime::Seconds();
	if (Now - LastArmFailAt < 15.0 && Why == LastArmFail) return;
	LastArmFailAt = Now; LastArmFail = Why;
	PushEvent(FSmvI18n::Format(TEXT("gpws.prearm_fail"), { { TEXT("msg"), Why } }).ToString(), 3);
}

// Kalkış / fırlatma aşaması (yerel SITL dizisi: armed → launch → released): olay metni (web ile aynı), HUD durumu, 3B düzenek
void ASmvGameMode::HandleLaunchMsg(const TSharedPtr<FJsonObject>& M)
{
	const FString Ph = SmvJson::Str(M, TEXT("phase"));
	const FString Me = SmvJson::Str(M, TEXT("method"), TEXT("wheels"));
	LaunchMethodUi = Me;
	LaunchPhaseUi = Ph == TEXT("released") ? FString() : Ph;
	LaunchPhaseAt = FPlatformTime::Seconds();
	FString Txt = FSmvI18n::T(FString::Printf(TEXT("launch.ev.%s.%s"), *Me, *Ph)).ToString();
	if (Txt.StartsWith(TEXT("launch.ev."))) Txt = FString::Printf(TEXT("Kalkış: %s"), *Ph);
	if (Ph == TEXT("released") && Link()) Txt += FString::Printf(TEXT(" · %.0f km/h"), Link()->LastTele().Groundspeed * 3.6);
	PushEvent(Txt, Ph == TEXT("launch") ? 5 : 6);
	SMV_KLOG(TEXT("Kalkış aşaması: %s / %s"), *Me, *Ph);
	OnLaunchPhase(Me, Ph);
}

void ASmvGameMode::OnLaunchPhase(const FString& Method, const FString& Phase)
{
	if (LaunchRig) LaunchRig->SetPhase(Phase);
}

void ASmvGameMode::HandleTele(const FCaySimTelemetry& T, const FSmvTeleExtra& X)
{
	if (!bScene || !Drone) return;
	Drone->PushTelemetry(T);
	if (Rules) Rules->OnTele(T);
	Drone->SetServoOutputs(X.Servo, X.ServoFn);   // GLB gövdesi: motor/gaz → pervane devri, kanal 1/2/4 → kontrol yüzeyleri
	UpdateGpws(T, X);
	// 5 Hz iz (kayıt / rota haritası / rapor) — kayıttan oynatmada yazılmaz
	if (Flow.IsValid() && !Flow->IsReplay())
	{
		const double Now = FPlatformTime::Seconds();
		if (Flow->LastPath.Num() == 0) PathT0 = Now;
		if (Now - PathAt >= 0.2 && Flow->LastPath.Num() < 20000 && (FMath::Abs(T.Lat) > 1e-4 || FMath::Abs(T.Lon) > 1e-4))
		{
			PathAt = Now;
			FSmvReplayFrame F;
			F.T = Now - PathT0;
			F.Lat = T.Lat; F.Lon = T.Lon; F.AltMsl = T.AltMsl; F.AltRel = T.AltRel;
			F.Roll = T.Roll; F.Pitch = T.Pitch; F.Yaw = T.Yaw; F.Vn = T.Vn; F.Ve = T.Ve; F.Vd = T.Vd;
			F.As = T.Airspeed; F.Gs = T.Groundspeed; F.Hdg = T.Heading; F.Climb = T.Climb; F.Thr = T.Throttle;
			F.Batt = X.bElec ? (float)X.ElecPct : T.BattPct; F.bArmed = T.bArmed; F.Mode = T.Mode;
			Flow->LastPath.Add(F);
		}
	}
}

void ASmvGameMode::HandleMessage(const TSharedPtr<FJsonObject>& M)
{
	const FString T = SmvJson::Str(M, TEXT("t"));
	if (T == TEXT("event")) { HandleApText(SmvJson::Str(M, TEXT("text")), (int32)SmvJson::Num(M, TEXT("severity"), 6)); return; }
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
	if (T == TEXT("launch")) { HandleLaunchMsg(M); return; }
	if (T == TEXT("res") && !SmvJson::Bool(M, TEXT("ok")) && (SmvJson::Str(M, TEXT("name")) == TEXT("arm") || SmvJson::Str(M, TEXT("name")) == TEXT("takeoff"))
		&& !LastPreArm.IsEmpty() && FPlatformTime::Seconds() - LastPreArmAt < 10.0)
	{
		ReportArmFail(LastPreArm);
		return;
	}
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
	if (A == TEXT("Radar")) { bRadarOn = !bRadarOn; FSmvUiSound::Play(ESmvSnd::Toggle); return; }
	if (A == TEXT("Eo")) { bEoOn = !bEoOn; FSmvUiSound::Play(ESmvSnd::Toggle); return; }
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
	if (A == TEXT("Ack")) { if (Gpws) Gpws->Ack(); bAlertUnacked = false; if (Tones) Tones->SetAlarm(0, 0.f); SmvSpeech::Stop(); return; }
	if ((A == TEXT("Arm") || A == TEXT("Takeoff")) && bRaceLocked) { HandleToast(FSmvI18n::T(TEXT("race.wait_start")).ToString(), 4); return; }
	if (A == TEXT("Arm")) Command(TEXT("arm"));
	else if (A == TEXT("Disarm")) Command(TEXT("disarm"));
	else if (A == TEXT("Takeoff"))
	{
		// yerel SITL konum tahmini henüz yok: komut bağlantıda kuyruğa alınır, hazır olunca kendiliğinden kalkar
		if (Link() && Link()->LastExtra().PosReady == 0) HandleToast(FSmvI18n::T(TEXT("fui.prep_queued")).ToString(), 5);
		Command(TEXT("takeoff"));
	}
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
		if (Now - LastReset < 2.0)
		{
			Command(TEXT("reset")); bCrashed = false; LastReset = 0;
			if (Drone && Drone->GetContactSolver()) Drone->GetContactSolver()->Reset();
		}
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

// Fare her ekranda (uçuş dahil) serbest ve görünür: sol tık Slate arayüzüne, görünüm yalnız sağ tuş basılıyken yakalar,
// fare pencereye kilitlenmez. Bir şey (piyon sahiplenme, giriş kipi değişimi, odak) bunu bozarsa her karede geri kurulur.
void ASmvGameMode::EnsureMouse()
{
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0)) if (!PC->bShowMouseCursor) PC->bShowMouseCursor = true;
	if (UGameViewportClient* Vc = GEngine ? GEngine->GameViewport : nullptr)
	{
		if (Vc->GetMouseCaptureMode() != EMouseCaptureMode::CaptureDuringRightMouseDown) Vc->SetMouseCaptureMode(EMouseCaptureMode::CaptureDuringRightMouseDown);
		if (Vc->GetMouseLockMode() != EMouseLockMode::DoNotLock) Vc->SetMouseLockMode(EMouseLockMode::DoNotLock);
		Vc->SetHideCursorDuringCapture(false);
	}
	// arayüze tıklandıktan sonra klavye odağı oyuna döner (WASD / kısayollar); yazı alanı odaktaysa dokunulmaz
	if (bScene && !bPauseMenu && FSmvInputGlyphs::ConsumeMouseUp() && FSlateApplication::IsInitialized())
	{
		FSlateApplication& Sa = FSlateApplication::Get();
		const TSharedPtr<SWidget> Fw = Sa.GetKeyboardFocusedWidget();
		const bool bTyping = Fw.IsValid() && (Fw->GetType() == FName(TEXT("SEditableText")) || Fw->GetType() == FName(TEXT("SMultiLineEditableText")));
		if (!bTyping) Sa.SetAllUserFocusToGameViewport();
	}
}

bool ASmvGameMode::SmokeClick(float Fx, float Fy, const TCHAR* What)
{
	if (!FSlateApplication::IsInitialized() || !GEngine || !GEngine->GameViewport) return false;
	TSharedPtr<SWindow> Win = GEngine->GameViewport->GetWindow();
	if (!Win.IsValid()) return false;
	FVector2D Vs;
	GEngine->GameViewport->GetViewportSize(Vs);
	const FVector2D Scr = Win->GetPositionInScreen() + FVector2D(Vs.X * Fx, Vs.Y * Fy) / FMath::Max(0.01f, Win->GetDPIScaleFactor()) * Win->GetDPIScaleFactor();
	FSlateApplication& Sa = FSlateApplication::Get();
	TSet<FKey> Pressed;
	Sa.ProcessMouseMoveEvent(FPointerEvent(0, Scr, Scr, Pressed, FKey(), 0.f, FModifierKeysState()));
	Pressed.Add(EKeys::LeftMouseButton);
	const bool bDown = Sa.ProcessMouseButtonDownEvent(nullptr, FPointerEvent(0, Scr, Scr, Pressed, EKeys::LeftMouseButton, 0.f, FModifierKeysState()));
	Pressed.Remove(EKeys::LeftMouseButton);
	const bool bUp = Sa.ProcessMouseButtonUpEvent(FPointerEvent(0, Scr, Scr, Pressed, EKeys::LeftMouseButton, 0.f, FModifierKeysState()));
	const UGameViewportClient* Vc = GEngine->GameViewport;
	SMV_KLOG(TEXT("SMOKE mouse %s (%.0f, %.0f): down=%d up=%d imlec=%d yakalama=%d"), What, Scr.X, Scr.Y, bDown ? 1 : 0, bUp ? 1 : 0,
		UGameplayStatics::GetPlayerController(this, 0) && UGameplayStatics::GetPlayerController(this, 0)->bShowMouseCursor ? 1 : 0, (int32)Vc->GetMouseCaptureMode());
	return bDown || bUp;
}

bool ASmvGameMode::SmokePanelOpen() const
{
	return FlightUi.IsValid() && FlightUi->IsPanelOpen();
}

void ASmvGameMode::Tick(float Dt)
{
	Super::Tick(Dt);
	EnsureMouse();
	if (!bScene) return;
	if (EndAt > 0 && FPlatformTime::Seconds() >= EndAt) { EndAt = 0; if (Flow.IsValid()) Flow->EndFlight(EndReasonPending); }
	if (VideoRec.IsValid() && VideoRec->IsOn()) VideoRec->Tick(Dt);
	if (Flow.IsValid() && Flow->GetScreen() == ESmvScreen::Loading) TickLoadGate();
	TickPhysics(Dt);
	TickDebris(Dt);
	TickCityWindows();
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

// ---------------------------------------------------------------------------
// yükleme kapısı: Yükleniyor ekranı açıkken her karede aşamalar yoklanır (oyun iş parçacığı kilitlenmez; sahne arkada çizilir)
// ---------------------------------------------------------------------------

void ASmvGameMode::TickLoadGate()
{
	if (!Gate.IsActive() || Gate.IsFailed()) return;
	UWorld* W = GetWorld();
	const double Now = FPlatformTime::Seconds();
	auto Fmt = [](const TCHAR* Key, const TMap<FString, FString>& A) { return FSmvI18n::Format(Key, A).ToString(); };
	// 1) harita + alt seviyeler (seviye akışı / World Partition)
	if (!Gate.Done(ESmvLoad::World))
	{
		float P = 1.f; bool bReady = true; FString D;
		const TArray<TObjectPtr<ULevelStreamingDynamic>>& Lvs = FabLevels.Num() ? FabLevels : CityLevels;
		if (Lvs.Num())
		{
			float Sum = 0.f; int32 Vis = 0;
			for (const TObjectPtr<ULevelStreamingDynamic>& Lv : Lvs)
			{
				float Pl = 1.f;
				if (Lv && Lv->IsLevelVisible()) ++Vis;
				else if (Lv && Lv->HasLoadedLevel()) Pl = 0.9f;
				else if (Lv)
				{
					float A = GetAsyncLoadPercentage(Lv->GetWorldAssetPackageFName());
					if (A < 0.f && !Lv->PackageNameToLoad.IsNone()) A = GetAsyncLoadPercentage(Lv->PackageNameToLoad);
					Pl = A > 0.f ? 0.85f * A / 100.f : 0.03f;
				}
				Sum += Pl;
			}
			const bool bPlacedOk = FabLevels.Num() ? bFabPlaced : (Vis == Lvs.Num());
			P = bPlacedOk ? 1.f : 0.95f * Sum / Lvs.Num() * (FabLevels.Num() && !bFabSubsRequested ? 0.5f : 1.f);
			bReady = bPlacedOk;
			D = Fmt(TEXT("load.d.levels"), { { TEXT("a"), FString::FromInt(Vis) }, { TEXT("b"), FString::FromInt(Lvs.Num()) } });
		}
		else D = FSmvI18n::T(WorldSetup && WorldSetup->GetSource() != ECaySimWorldSource::Flat ? TEXT("load.d.real") : TEXT("load.d.procedural")).ToString();
		const bool bLevelsOk = bReady;
		// gerçek dünya (Cesium 3D Tiles): karolar yüklenmeli; Fab haritası varsa yerleşince kırpma + kenar eşleme, ardından karolar yeniden otursun
		const bool bTiles = WorldSetup && WorldSetup->GetSource() != ECaySimWorldSource::Flat;
		if (bTiles)
		{
			const float Tp = FMath::Clamp(WorldSetup->GetLoadProgress() / 100.f, 0.f, 1.f);
			const bool bTilesReady = WorldSetup->IsReady();
			const double El = Gate.Elapsed();
			if (bTilesReady && bRimPending && !bRimStarted && bLevelsOk)
			{
				bRimStarted = true; RimStartAt = Now;
				AlignRealRim();   // zaman uyumsuz: yükseklik örnekleri gelince bRimDone
			}
			if (bRimStarted && !bRimDone && Now - RimStartAt > 20.0) { SMV_KLOG(TEXT("Gerçek dünya kenar eşleme 20 sn'de bitmedi — kaydırmasız sürdürülüyor")); bRimDone = true; RimAt = Now; }
			// karolar gelmiyor ama haritanın kendi zemini var: gerçek çevresiz sürdür (günlükte uyarı)
			if (!bTilesReady && bLevelsOk && FabLevels.Num() && El > 75.0 && !bRimDone)
			{
				SMV_KLOG(TEXT("SMVLOAD [ucus] 3D Tiles %.0f sn'de yüklenmedi (%%%.0f) — gerçek dünya çevresi olmadan sürdürülüyor"), El, Tp * 100.f);
				bRimDone = true; RimAt = Now - 10.0; bTilesSkipped = true;
			}
			const bool bTilesOk = bTilesSkipped || (bTilesReady && (!bRimPending || (bRimDone && Now - RimAt > 1.2)));
			P = FMath::Min(P, 0.35f + 0.6f * Tp);
			if (!bTilesOk) D = Fmt(TEXT("load.d.tiles"), { { TEXT("n"), FString::FromInt(FMath::RoundToInt(Tp * 100.f)) }, { TEXT("src"), WorldSetup->SourceName() == TEXT("google3d") ? FString(TEXT("Google 3D")) : FString(TEXT("Cesium")) } });
			bReady = bReady && bTilesOk;
		}
		bReady = bReady && FSmvLoadGate::WorldPartitionDone(W) && !(W && W->IsVisibilityRequestPending());
		// son denetim: kalkış noktasının altında gerçekten zemin var mı (harita akışta kaldıysa / boşsa uçuşa girilmez)
		bool bGround = true;
		if (bReady && W)
		{
			FHitResult Gh;
			FCollisionQueryParams Gq(SCENE_QUERY_STAT(SmvGateGround), true, Drone);
			if (PilotFigure) Gq.AddIgnoredActor(PilotFigure);
			if (Sock) Gq.AddIgnoredActor(Sock);
			bGround = W->LineTraceSingleByChannel(Gh, FVector(0, 0, 60000.0), FVector(0, 0, -60000.0), ECC_Visibility, Gq)
				|| W->LineTraceSingleByObjectType(Gh, FVector(0, 0, 60000.0), FVector(0, 0, -60000.0), FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllObjects), Gq);
			if (!bGround) D = FSmvI18n::T(TEXT("load.d.no_ground")).ToString();
		}
		if (Gate.Elapsed() > 90.0 && !(bReady && bGround))
		{
			// zaman aşımında uçuşa GİRİLMEZ: neden + Yeniden dene / Menü (yükleme ekranı)
			FString Why;
			if (!bLevelsOk && FabLevels.Num())
			{
				int32 Vis = 0; for (const TObjectPtr<ULevelStreamingDynamic>& Lv : FabLevels) if (Lv && Lv->IsLevelVisible()) ++Vis;
				Why = FSmvI18n::Format(TEXT("load.fail.levels"), { { TEXT("a"), FString::FromInt(Vis) }, { TEXT("b"), FString::FromInt(FabLevels.Num()) }, { TEXT("map"), FabMapKey } }).ToString();
			}
			else if (bTiles && !WorldSetup->IsReady()) Why = FSmvI18n::T(TEXT("load.fail.tiles")).ToString();
			else if (!bGround) Why = FSmvI18n::T(TEXT("load.fail.ground")).ToString();
			else Why = FSmvI18n::T(TEXT("load.fail.stream")).ToString();
			Gate.Fail(ESmvLoad::World, Why);
			return;
		}
		Gate.Update(ESmvLoad::World, P, bReady && bGround, D, 1.0e9, 3);
	}
	const bool bWorld = Gate.Done(ESmvLoad::World);
	// 2) dekor / kalabalık / trafik / araç gövdesi (Fab dekoru harita yerleşince kurulur; GLB gövdesi geri çağrıyla; kalan paket yüklemeleri)
	if (!Gate.Done(ESmvLoad::Scenery))
	{
		const int32 Pk = FSmvLoadGate::AsyncPackages();
		const bool bCity = CityLevels.Num() > 0;
		const bool bWin = !bCity || bCityWindowsDone;
		const float P = (bWorld ? 0.4f : 0.f) + (bGlbPending ? 0.f : 0.3f) + (Pk == 0 ? 0.2f : 0.f) + (bWin ? 0.1f : 0.f);
		const FString D = Pk > 0 ? Fmt(TEXT("load.d.packages"), { { TEXT("n"), FString::FromInt(Pk) } }) : FSmvI18n::T(TEXT("load.d.body")).ToString();
		Gate.Update(ESmvLoad::Scenery, P, bWorld && !bGlbPending && Pk == 0 && bWin, D, 60.0, 5);
	}
	// 3) atmosfer: harita yerleşince (haritanın kendi gök/ışık aktörleri kaldırıldıktan sonra) gök ışığı yeniden yakalanır, ilk kareler çizilir
	if (!Gate.Done(ESmvLoad::Atmos))
	{
		const bool bUds = FabSkyActors.Num() > 0;
		float P = 0.25f;
		bool bReady = false;
		if (bWorld)
		{
			if (AtmosT0 <= 0) { AtmosT0 = Now; AtmosF0 = GFrameCounter; const int32 N = FSmvLoadGate::RecaptureSky(W); SMV_KLOG(TEXT("SMVLOAD [ucus] gök ışığı yakalama: %d bileşen (%s)"), N, bUds ? TEXT("UDS") : TEXT("yerleşik")); }
			const double K = FMath::Min((double)(GFrameCounter - AtmosF0) / 24.0, (Now - AtmosT0) / 0.8);
			P = 0.25f + 0.75f * (float)FMath::Clamp(K, 0.0, 1.0);
			bReady = K >= 1.0;
		}
		Gate.Update(ESmvLoad::Atmos, P, bReady, FSmvI18n::T(bUds ? TEXT("load.d.sky_uds") : TEXT("load.d.sky_builtin")).ToString(), 60.0);
	}
	// 4) ArduPilot SITL: bağlantı (HAZIR) + GPS 3B kilit + EKF konum tahmini (pos_ready); bulut/kayıt oynatmada yalnız bağlantı
	if (!Gate.Done(ESmvLoad::Sitl))
	{
		USmvFlightLink* L = Link();
		const int32 Pr = L ? L->LastExtra().PosReady : -1;
		const FCaySimTelemetry& T = L ? L->LastTele() : FCaySimTelemetry();
		const bool bLocal = Cast<USmvLocalFlightLink>(L) != nullptr;
		float P = 0.08f; FString D;
		if (!Flow->bLinkHazir) D = FSmvI18n::T(bLocal ? TEXT("load.d.sitl_start") : TEXT("load.d.link")).ToString();
		else if (Pr == 0)
		{
			P = T.GpsFix >= 3 ? 0.75f : 0.45f;
			D = Fmt(TEXT("load.d.gps"), { { TEXT("fix"), T.GpsFix >= 3 ? FString(TEXT("3D")) : FString::FromInt(T.GpsFix) }, { TEXT("n"), FString::FromInt(FMath::Max(0, T.Sats)) } });
		}
		else { P = 1.f; D = FSmvI18n::T(TEXT("load.d.pos_ready")).ToString(); }
		Gate.Update(ESmvLoad::Sitl, P, Flow->bLinkHazir && Pr != 0, D, 120.0, 2);
	}
	// 5) gölgelendirici + PSO (harita ve dekor yüklendikten sonra; yeni malzemeler gelmesin)
	const bool bContent = bWorld && Gate.Done(ESmvLoad::Scenery);
	if (!Gate.Done(ESmvLoad::Shaders))
	{
		const int32 J = FSmvLoadGate::ShaderJobs() + FSmvLoadGate::PsoPending();
		ShaderPeak = FMath::Max(ShaderPeak, J);
		const float P = !bContent ? 0.f : ShaderPeak > 0 ? 1.f - (float)J / ShaderPeak : 1.f;
		Gate.Update(ESmvLoad::Shaders, P, bContent && J == 0, Fmt(TEXT("load.d.jobs"), { { TEXT("n"), FString::FromInt(J) } }), 100.0, 10);
	}
	// 6) doku / ağ akışı (kamera kalkış noktasında; 8 sn sonra çok küçük kalan kuyruk da kabul)
	if (!Gate.Done(ESmvLoad::Streaming))
	{
		const int32 Np = FSmvLoadGate::StreamingPending();
		float P = 0.f; bool bReady = false;
		if (bContent && Gate.Done(ESmvLoad::Atmos))
		{
			if (StreamT0 <= 0) StreamT0 = Now;
			StreamPeak = FMath::Max(StreamPeak, Np);
			P = StreamPeak > 0 ? 1.f - (float)Np / StreamPeak : 1.f;
			const int32 Thr = Now - StreamT0 > 8.0 ? FMath::Max(2, StreamPeak / 50) : 0;
			bReady = Np <= Thr;
		}
		Gate.Update(ESmvLoad::Streaming, P, bReady, Fmt(TEXT("load.d.stream"), { { TEXT("n"), FString::FromInt(Np) } }), 110.0, 15);
	}
	// 7) son ön ısınma: diğer her şey bitti, kamera son konumunda → gök ışığı yeniden yakalanır, Lumen yüzey önbelleği / hacimsel bulut /
	//    göz uyumu için en az 45 kare + 1,5 sn çizilir (ortada bir yakalama daha)
	bool bOthers = true;
	for (int32 i = 0; i < (int32)ESmvLoad::Warmup; ++i) if (!Gate.Done((ESmvLoad)i)) bOthers = false;
	if (bOthers && !Gate.Done(ESmvLoad::Warmup))
	{
		if (WarmT0 <= 0) { WarmT0 = Now; WarmF0 = GFrameCounter; WarmRecap = FSmvLoadGate::RecaptureSky(W); }
		if (WarmRecap >= 0 && Now - WarmT0 > 0.7) { FSmvLoadGate::RecaptureSky(W); WarmRecap = -1; }
		const uint64 Fr = GFrameCounter - WarmF0;
		const double K = FMath::Min((double)Fr / 45.0, (Now - WarmT0) / 1.5);
		Gate.Update(ESmvLoad::Warmup, (float)FMath::Clamp(K, 0.0, 1.0), K >= 1.0, Fmt(TEXT("load.d.frames"), { { TEXT("n"), FString::Printf(TEXT("%llu"), Fr) } }),
			FMath::Max(150.0, Gate.Elapsed() + (WarmT0 > 0 ? 8.0 - (Now - WarmT0) : 8.0)));
	}
	if (Gate.IsDone())
	{
		Gate.LogSummary();
		if (Flow->bLinkHazir) Flow->SetScreen(ESmvScreen::Flight);
	}
}

// ---------------------------------------------------------------------------
// gerçek fizik: temas olayları, engel sensörleri (SmvPhysics; docs/fizik-carpisma.md)
// ---------------------------------------------------------------------------

void ASmvGameMode::TickPhysics(float /*Dt*/)
{
	FSmvContactSolver* Sol = Drone ? Drone->GetContactSolver() : nullptr;
	if (!Sol) return;
	// sorgularda yok sayılanlar: hayalet/rakip/hedef dronlar, 3B pilot, enkaz, pilot piyonu
	TArray<TWeakObjectPtr<AActor>>& Ig = Sol->IgnoreList();
	Ig.Reset();
	for (const TObjectPtr<ASmvDroneActor>& G : Ghosts) if (G) Ig.Add(G.Get());
	for (const TObjectPtr<ASmvDroneActor>& G : TargetActors) if (G) Ig.Add(G.Get());
	for (const TObjectPtr<AActor>& Db : Debris) if (Db) Ig.Add(Db.Get());
	if (PilotFigure) Ig.Add(PilotFigure.Get());
	if (ASmvPilotPawn* Pw = GetPilot()) Ig.Add(Pw);
	for (const FSmvContactEvent& E : Sol->DrainEvents()) HandleContact(E);

	const FSmvPhysicsSettings& PS = FSmvPhysicsSettings::Get();
	const int32 Dbg = FSmvPhysicsSettings::DebugLevel();
	const FVector P = Drone->GetActorLocation();
	const FQuat Rot = Drone->GetActorQuat();
	if (Dbg >= 1) Sol->DrawDebug(GetWorld(), P, Rot);
	const double Now = FPlatformTime::Seconds();
	if (Now - LastSensorAt < 1.0 / PS.SensorHz) return;
	LastSensorAt = Now;
	const FQuat Enu = WorldSetup ? WorldSetup->EnuToUnrealRotation(P) : FQuat::Identity;
	const FCollisionQueryParams Q = SmvPhysics::MakeQuery(TEXT("SmvSensor"), Drone, &Ig);
	FSmvSensorScan Scan;
	SmvPhysics::Scan(GetWorld(), P, Rot, Enu, Q, Scan, Dbg >= 2);
	if (!Scan.bValid) return;
	USmvFlightLink* L = Link();
	if (L && PS.bSitlSensors && L->AcceptsRangeSensors())
	{
		FSmvRangeScan R;
		R.DownM = Scan.DownM; R.UpM = Scan.UpM; R.SectorM = Scan.SectorM;
		R.DownMaxM = PS.RngMaxM; R.ProxMaxM = PS.ProxMaxM;
		L->SendRangeSensors(R);
	}
	// HUD yakınlık (yalnız gösterge): en yakın yatay engel ya da tavan
	ProxLine.Reset(); ProxLevel = 0;
	const bool bAir = L && (L->LastTele().bArmed || L->LastTele().AltRel > 1.0);
	if (bAir && Scan.NearestM >= 0 && Scan.NearestM < PS.ProxWarnM)
	{
		const float B = Scan.NearestBearingDeg;
		const TCHAR* Dir = B < 22.5f || B >= 337.5f ? TEXT("ÖN") : B < 67.5f ? TEXT("SAĞ ÖN") : B < 112.5f ? TEXT("SAĞ") : B < 157.5f ? TEXT("SAĞ ARKA")
			: B < 202.5f ? TEXT("ARKA") : B < 247.5f ? TEXT("SOL ARKA") : B < 292.5f ? TEXT("SOL") : TEXT("SOL ÖN");
		ProxLine = FString::Printf(TEXT("ENGEL %.1f m · %s"), Scan.NearestM, Dir);
		ProxLevel = Scan.NearestM < 2.f ? 2 : 1;
	}
	if (bAir && Scan.UpM >= 0 && Scan.UpM < FMath::Min(PS.ProxWarnM, 3.f))
	{
		ProxLine += FString::Printf(TEXT("%sTAVAN %.1f m"), ProxLine.IsEmpty() ? TEXT("") : TEXT("   "), Scan.UpM);
		// tanı (uçuş başına bir kez): tavanı oluşturan aktör / bileşen
		static TWeakObjectPtr<AActor> LastCeilDrone;
		if (LastCeilDrone.Get() != Drone)
		{
			LastCeilDrone = Drone;
			FHitResult H;
			const FVector Up = Enu.RotateVector(FVector::UpVector);
			if (GetWorld()->LineTraceSingleByChannel(H, P, P + Up * 400.0, PS.Channel, Q))
				UE_LOG(LogSimumav, Warning, TEXT("Tavan tanısı: %s / %s (%s) %.2f m, dron Z %.0f"), H.GetActor() ? *H.GetActor()->GetName() : TEXT("-"),
					H.GetComponent() ? *H.GetComponent()->GetName() : TEXT("-"), H.GetComponent() ? *H.GetComponent()->GetClass()->GetName() : TEXT("-"), H.Distance / 100.0, P.Z);
		}
		ProxLevel = FMath::Max(ProxLevel, Scan.UpM < 1.5f ? 2 : 1);
	}
}

void ASmvGameMode::HandleContact(const FSmvContactEvent& E)
{
	UE_LOG(LogSimumav, Log, TEXT("Fizik teması: %s"), *E.Describe());
	USmvFlightLink* L = Link();
	if (!L) return;
	const double Now = FPlatformTime::Seconds();
	if (Now - SceneT0 < 2.0) return;   // sahne kurulumu / ilk tele yerleşimi
	const FCaySimTelemetry& T = L->LastTele();
	if (E.bRestDisarm)
	{
		if (L->Backend() == ESmvFlightBackend::LocalSitl && !bCrashed) Command(TEXT("land_rest"));
		return;
	}
	if (SmvCore::ContactIsCrash(E.Class))
	{
		if (bCrashed) return;
		// kolsuz ve yavaş (yerde itilen / sürüklenen) araç çarpışma sayılmaz; kolsuz düşen araç (≥ 3 m/s) sayılır
		if (!T.bArmed && E.Speed < 3.0) return;
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("kind"), E.Kind);
		P->SetNumberField(TEXT("speed"), FMath::RoundToDouble(E.Speed * 10.0) / 10.0);
		P->SetBoolField(TEXT("physics"), true);
		P->SetStringField(TEXT("class"), UTF8_TO_TCHAR(SmvCore::ContactName(E.Class)));
		P->SetNumberField(TEXT("vn"), FMath::RoundToDouble(E.Vn * 100.0) / 100.0);
		P->SetNumberField(TEXT("vt"), FMath::RoundToDouble(E.Vt * 100.0) / 100.0);
		P->SetNumberField(TEXT("normal_z"), FMath::RoundToDouble(E.Nz * 100.0) / 100.0);
		P->SetStringField(TEXT("object"), E.Actor);
		Command(TEXT("crash"), P);
		return;
	}
	if ((E.Class == SmvCore::EContact::Graze || E.Class == SmvCore::EContact::CeilingTouch) && Now - LastGrazeEventAt > 2.0)
	{
		LastGrazeEventAt = Now;
		PushEvent(FString::Printf(TEXT("TEMAS: %s (%s) %.1f m/s — yüzeyden itildi"),
			E.Class == SmvCore::EContact::CeilingTouch ? TEXT("tavan") : TEXT("yanal sürtünme"), *E.Kind, E.Speed), 4);
	}
}

void ASmvGameMode::HandleLevelAdded(ULevel* Level, UWorld* InWorld)
{
	if (!bScene || !Level || InWorld != GetWorld() || Level->IsPersistentLevel()) return;
	PreparePhysicsLevel(Level);
}

void ASmvGameMode::PreparePhysicsLevel(ULevel* Level)
{
	if (!Level) return;
	for (const TWeakObjectPtr<ULevel>& L : PhysPreparedLevels) if (L.Get() == Level) return;
	PhysPreparedLevels.RemoveAll([](const TWeakObjectPtr<ULevel>& L) { return !L.IsValid(); });
	PhysPreparedLevels.Add(Level);
	UE_LOG(LogSimumav, Log, TEXT("Fizik seviye hazırlığı — %s"), *SmvPhysics::PrepareLevelCollision(Level));
}

void ASmvGameMode::UpdateGpws(const FCaySimTelemetry& T, const FSmvTeleExtra& X)
{
	if (!Gpws || !Drone || !AlertPolicy) return;
	SmvCore::FGpwsInput I;
	I.T = T.Tbm > 0 ? T.Tbm / 1000.0 : FPlatformTime::Seconds() - SceneT0;
	I.bArmed = T.bArmed;
	I.Mode = TCHAR_TO_UTF8(*T.Mode);
	// AGL: 3B dünyadan (yer ışını), yoksa SITL göreli irtifası
	const FVector P = Drone->GetActorLocation();
	FHitResult Hit;
	FCollisionQueryParams Q(TEXT("SmvAgl"), true, Drone);
	SmvPhysics::IgnoreClippedTiles(Q, P);
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
	I.Phase = TCHAR_TO_UTF8(*LaunchPhaseUi);
	const SmvCore::FGpwsStepResult R = Gpws->Step(I);
	// gösterim politikası (SmvAlertPolicy): evre farkındalığı, otomatik kip sınırları, gecikme, tekrar bastırma, kademe (Ayarlar → Uyarılar)
	const FSmvSettings& S = USmvGameInstance::GetSmv(this)->GetData()->Settings();
	const bool bLaunching = LaunchPhaseUi == TEXT("armed") || LaunchPhaseUi == TEXT("launch");
	const double BankCfg = SmvCore::IsPlaneType(SmvCore::SitlTypeFromSmv(std::string(TCHAR_TO_UTF8(*Req.Drone.Type)))) ? S.BankPlane : S.BankCopter;
	const std::vector<SmvCore::FAlertShow> Shown = AlertPolicy->Filter(I, R, Gpws->VClass(), TCHAR_TO_UTF8(*FSmvUiPrefs::Get().AlertLevel), BankCfg, bLaunching);
	ActiveAlerts.Reset();
	const bool bAviation = S.GpwsLang != TEXT("ui");
	bool bAnyUnacked = false;
	int32 Alarm = 0;
	for (const SmvCore::FAlertShow& A : Shown)
	{
		const SmvCore::FGpwsCodeDef* D = SmvCore::GpwsCode(A.Code);
		if (!D) continue;   // bilinmeyen kod (çekirdek tablosuyla uyumsuzluk) — çökme yerine atla
		const bool bWarn = A.Level == "warning";
		ActiveAlerts.Add(TPair<FString, bool>(bAviation ? FString(UTF8_TO_TCHAR(D->Av)) : FSmvI18n::T(FString(TEXT("gpws.c_")) + UTF8_TO_TCHAR(D->Code)).ToString(), bWarn));
		if (!A.bAcked) { bAnyUnacked = true; if (!Alarm) Alarm = bWarn ? 1 : 2; }
	}
	bAlertUnacked = bAnyUnacked;
	if (Tones) Tones->SetAlarm(S.GpwsMode == TEXT("full") && S.bAlertTones ? Alarm : 0, (S.bSound ? (float)S.Volume : 0.f) * FSmvUiPrefs::Get().VolAlerts);
	for (const SmvCore::FAlertShow& A : Shown)
	{
		if (!A.bFresh) continue;
		const SmvCore::FGpwsCodeDef* D = SmvCore::GpwsCode(A.Code);
		if (!D) continue;
		SMV_KLOG(TEXT("Uyarı: %s (%s, evre %s, kip %s, yatış %.0f°, AGL %.0f m)"), UTF8_TO_TCHAR(D->Code), UTF8_TO_TCHAR(D->Level), UTF8_TO_TCHAR(AlertPolicy->Phase().c_str()), *T.Mode, I.Roll, I.Agl);
		PushEvent(FString::Printf(TEXT("%s · %s"), std::string(D->Level) == "warning" ? TEXT("WARNING") : TEXT("CAUTION"), UTF8_TO_TCHAR(D->Av)), std::string(D->Level) == "warning" ? 3 : 4);
		// sesli uyarı (tam kip): havacılık kalıbı İngilizce, arayüz dilinde küçük harfle (motor harf harf okumasın)
		if (S.GpwsMode == TEXT("full") && S.bSound && FSmvUiPrefs::Get().bGpwsSpeech)
		{
			const FString Txt = bAviation ? FString(UTF8_TO_TCHAR(D->Say)) : FSmvI18n::T(FString(TEXT("gpws.c_")) + UTF8_TO_TCHAR(D->Code)).ToString().ToLower();
			SmvSpeech::Say(Txt, bAviation ? FString(TEXT("en")) : FSmvI18n::Language(), (float)S.Volume * FSmvUiPrefs::Get().VolAlerts, 800);
		}
	}
	for (const SmvCore::FGpwsEpisode& E : R.Ended)
	{
		bool bWasShown = false;
		for (const SmvCore::FGpwsEpisode& Ep : AlertPolicy->ShownEpisodes()) if (Ep.Code == E.Code && Ep.T0 == E.T0) { bWasShown = true; break; }
		if (!bWasShown) continue;   // ekrana çıkmamış (bastırılmış) bölüm sunucuya / özete yazılmaz
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

bool ASmvGameMode::SmokeTele(float& OutAltRel, bool& bOutArmed, int32& OutPosReady) const
{
	USmvFlightLink* L = Link();
	if (!L || !L->IsReady()) return false;
	OutAltRel = (float)L->LastTele().AltRel;
	bOutArmed = L->LastTele().bArmed;
	OutPosReady = L->LastExtra().PosReady;
	return true;
}

void ASmvGameMode::SmokeTakeoff(float AltM)
{
	TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
	P->SetNumberField(TEXT("alt"), AltM);
	Command(TEXT("takeoff"), P);
}

void ASmvGameMode::SmokeCam(int32 Mode)
{
	if (ASmvPilotPawn* P = GetPilot()) P->SetMode(Mode == 1 ? ESmvCamMode::Fpv : Mode == 7 ? ESmvCamMode::Pilot : ESmvCamMode::Chase);
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
		// HUD metinleri sanal kumanda (sol alt) ve dron paneli (sağ üst) altında kalmasın
		Hud->LeftInset = FlightUi.IsValid() && FlightUi->SticksShown() ? 240.f : 0.f;
		Hud->RightInset = FlightUi.IsValid() && FlightUi->IsPanelOpen() ? 340.f : 0.f;
		if (Hud->WarnLabels.Num() == 0 || HudLang != FSmvI18n::Language())
		{
			HudLang = FSmvI18n::Language();
			Hud->WarnLabels = { FSmvI18n::T(TEXT("hud.w_crash")).ToString(), FSmvI18n::T(TEXT("hud.w_norc")).ToString(), FSmvI18n::T(TEXT("hud.w_batt")).ToString(),
				FSmvI18n::T(TEXT("hud.w_gcs")).ToString(), FSmvI18n::T(TEXT("hud.w_low")).ToString() };
		}
		const bool bHudMode = Pw && Pw->IsHudOn() && (Pw->GetMode() == ESmvCamMode::Fpv || Pw->GetMode() == ESmvCamMode::Gimbal) && Set.GaugeTheme == TEXT("military");
		Hud->SetVisibility(bHudMode && !(JetHud && JetHud->IsVisible()) ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	UpdateJetHud(T, Pw);
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
	// fırlatma geri sayımı / durumu (web launchHud / handHud): elden atışta "3 · 2 · 1 · AT!", diğerlerinde yöntem satırı
	if (!LaunchPhaseUi.IsEmpty())
	{
		const double Tl = FPlatformTime::Seconds() - LaunchPhaseAt;
		const double Gs = Link() ? Link()->LastTele().Groundspeed * 3.6 : 0.0;
		FString LaunchLine;
		if (LaunchMethodUi == TEXT("hand"))
		{
			const FString Cnt = LaunchPhaseUi == TEXT("armed") ? FString(Tl < 1.0 ? TEXT("3") : Tl < 2.0 ? TEXT("2") : TEXT("1")) : FSmvI18n::T(TEXT("launch.hud.throw")).ToString();
			LaunchLine = FString::Printf(TEXT("%s   ·   LAUNCH HAND · %.0f m/s · %.0f°"), *Cnt, Req.Launch.HandSpeedMs, Req.Launch.HandPitchDeg);
		}
		else if (LaunchMethodUi == TEXT("ramp")) LaunchLine = FString::Printf(TEXT("LAUNCH RAMP %.0f° · %s"), Req.Launch.AngleDeg, *FSmvI18n::T(LaunchPhaseUi == TEXT("launch") ? TEXT("launch.ev.ramp.launch") : TEXT("launch.ev.ramp.armed")).ToString());
		else if (LaunchMethodUi == TEXT("vehicle")) LaunchLine = FString::Printf(TEXT("LAUNCH VEH · %.0f / %.0f km/h"), Gs, Req.Launch.ReleaseKmh);
		else if (LaunchMethodUi == TEXT("wheels")) LaunchLine = FString::Printf(TEXT("LAUNCH RWY · %.0f km/h"), Gs);
		if (!LaunchLine.IsEmpty()) { S.Toast = LaunchLine; S.ToastSeverity = 5; }
	}
	S.bCrashed = bCrashed;
	S.ProxLine = ProxLine; S.ProxLevel = ProxLevel;
	if (bPauseMenu) S.Paused = FSmvI18n::T(TEXT("flight.paused_hint")).ToString();
	// PiP artık uçuş arayüzünde (sürükle / boyutlandır); HUD katmanı çizmez
	UTextureRenderTarget2D* PipRt = S.Pip;
	S.Pip = nullptr;
	Overlay->SetState(S);

	// ---- uçuş arayüzü ----
	if (FlightUi.IsValid())
	{
		FSmvFlightUiState U;
		U.Title = Req.Drone.Name.IsEmpty() ? Req.Drone.Type : Req.Drone.Name;
		if (USmvReplayFlightLink* Rl = Cast<USmvReplayFlightLink>(Link()))
		{
			U.bReplay = true; U.ReplayT = (float)Rl->GetPlayT(); U.ReplayDur = (float)Rl->GetDuration(); U.ReplaySpeed = Rl->GetSpeed();
			U.Title = FSmvI18n::T(TEXT("replay.badge")).ToString() + TEXT(" · ") + U.Title;
		}
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
		U.PosReady = X.PosReady; U.bCmdQueued = X.bCmdQueued;
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
		U.Pip = PipRt;
		U.PipLabel = ASmvPilotPawn::ModeLabel(ESmvCamMode::Gimbal);
		UpdateSite(U);
		if (Sock && Link() && Link()->LastExtra().WindSpd > 0.0) Sock->SetWind((float)Link()->LastExtra().WindSpd, (float)Link()->LastExtra().WindDir);
		FlightUi->SetState(U);
	}
}

// ---------------------------------------------------------------------------
// özet
// ---------------------------------------------------------------------------

void ASmvGameMode::AugmentSummary(FSmvFlightSummary& S)
{
	if (Gpws && AlertPolicy)
	{
		{
			SmvCore::FGpwsStepResult Cr;
			Cr.Ended = Gpws->Close(SmvCore::NaN);
			SmvCore::FGpwsInput Ci; Ci.T = Link() ? Link()->LastTele().Tbm / 1000.0 : 0;
			AlertPolicy->Filter(Ci, Cr, Gpws->VClass(), "off", 45.0, false);   // açık kalan gösterilmiş bölümler özete
		}
		// puan / özet: yalnız gösterilmiş (eyleme dönük) uyarı bölümleri
		const SmvCore::FGpwsSummary G = SmvCore::SummarizeAlerts(AlertPolicy->ShownEpisodes(), Gpws->Cfg().Mode);
		if (S.AlertScore < 0 && G.Score >= 0) { S.AlertScore = G.Score; S.AlertGrade = UTF8_TO_TCHAR(G.Grade.c_str()); S.Warnings = G.Warnings; S.Cautions = G.Cautions; }
	}
	if (MyTracker && Rings)
	{
		if (S.GatesPassed == 0) S.GatesPassed = MyTracker->Passed();
		if (S.GatesMissed == 0 && !MyTracker->Finished()) S.GatesMissed = Rings->Num() - MyTracker->Passed();
		if (MyTracker->Finished()) S.Laps = FMath::Max(S.Laps, 1);
	}
	if (!RaceResultBadge.IsEmpty()) S.Badges.AddUnique(RaceResultBadge);
	if (Rules) Rules->Augment(S);
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

// Gerçek dünya içine oturtulan Fab haritası: harita kutusunu 3D Tiles'tan kırp, kenar boyunca harita zemini (içte) ile karo yüzeyi (dışta)
// arasındaki düşey farkın ortancası kadar georeference'ı kaydır — haritanın kenarı gerçek araziye bağlanır. Karolar yüklendikten sonra bir kez.
bool ASmvGameMode::AlignRealRim()
{
	UWorld* W = GetWorld();
	if (!W || !WorldSetup) return false;
	FBox B = FabPlacedBox;
	TArray<const AActor*> Own;   // harita geometrisi (iç ışınlar yalnız bunlara)
	if (!B.IsValid)
	{
		// kodla kurulan şehir: dekor aktörlerinin sınırı
		for (const TObjectPtr<AActor>& A : FabDecor)
		{
			if (!A) continue;
			Own.Add(A);
			TInlineComponentArray<UPrimitiveComponent*> Pc(A);
			for (UPrimitiveComponent* C : Pc) if (C && C->IsRegistered() && C->Bounds.BoxExtent.GetMax() < 60000.0 && C->Bounds.GetBox().IsValid) B += C->Bounds.GetBox();
		}
	}
	if (!B.IsValid || B.GetSize().X < 500.0 || B.GetSize().Y < 500.0) { SMV_KLOG(TEXT("Gerçek dünya kenar eşleme: harita kutusu yok / çok küçük")); bRimDone = true; RimAt = FPlatformTime::Seconds(); return false; }
	AActor* Tiles = WorldSetup->GetTilesActor();
	TSet<ULevel*> FabLv;
	for (const TObjectPtr<ULevelStreamingDynamic>& Lv : FabLevels) if (Lv && Lv->GetLoadedLevel()) FabLv.Add(Lv->GetLoadedLevel());
	auto OwnHit = [&](const FVector& Top, const FVector& Bot, double& OutZ) -> bool
	{
		TArray<FHitResult> Hits;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(SmvRimFab), true);
		if (Tiles) Q.AddIgnoredActor(Tiles);
		if (Drone) Q.AddIgnoredActor(Drone);
		if (PilotFigure) Q.AddIgnoredActor(PilotFigure);
		W->LineTraceMultiByObjectType(Hits, Top, Bot, FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllStaticObjects), Q);
		for (const FHitResult& H : Hits)
		{
			const AActor* A = H.GetActor();
			if (!A) continue;
			if (FabLv.Num() ? FabLv.Contains(A->GetLevel()) : Own.Contains(A) || A->GetLevel() == W->PersistentLevel) { OutZ = H.ImpactPoint.Z; return true; }
		}
		return false;
	};
	// kırpma: kutu (biraz içeri; harita zemininin kenarı ile karo arasında boşluk kalmasın)
	const FVector C = B.GetCenter(), Hs = B.GetExtent();
	const double In = 0.985;
	TArray<FVector> Poly = { FVector(C.X - Hs.X * In, C.Y - Hs.Y * In, 0.0), FVector(C.X + Hs.X * In, C.Y - Hs.Y * In, 0.0), FVector(C.X + Hs.X * In, C.Y + Hs.Y * In, 0.0), FVector(C.X - Hs.X * In, C.Y + Hs.Y * In, 0.0) };
	const bool bClip = WorldSetup->ClipPolygon(Poly);
	SmvPhysics::SetClipRegion(Tiles, FBox2D(FVector2D(C.X - Hs.X * In, C.Y - Hs.Y * In), FVector2D(C.X + Hs.X * In, C.Y + Hs.Y * In)));
	// kenar örnekleri: içte harita zemini (ışın), dışta 3D Tiles yüzeyi (Cesium SampleHeightMostDetailed — çarpışma ağı / görünürlükten bağımsız)
	TArray<double> Zin;
	TArray<FVector> Out;
	const int32 N = 40;
	const double ZTop = B.Max.Z + 300000.0, ZBot = B.Min.Z - 300000.0;
	for (int32 i = 0; i < N; ++i)
	{
		const double T = (double)i / N * 4.0;
		const int32 Side = FMath::Min(3, (int32)T);
		const double F = T - Side;
		FVector2D P, Nrm;
		switch (Side)
		{
		case 0: P = FVector2D(C.X - Hs.X + 2.0 * Hs.X * F, C.Y - Hs.Y); Nrm = FVector2D(0, -1); break;
		case 1: P = FVector2D(C.X + Hs.X, C.Y - Hs.Y + 2.0 * Hs.Y * F); Nrm = FVector2D(1, 0); break;
		case 2: P = FVector2D(C.X + Hs.X - 2.0 * Hs.X * F, C.Y + Hs.Y); Nrm = FVector2D(0, 1); break;
		default: P = FVector2D(C.X - Hs.X, C.Y + Hs.Y - 2.0 * Hs.Y * F); Nrm = FVector2D(-1, 0); break;
		}
		const double Span = FMath::Abs(Nrm.X) > 0 ? Hs.X : Hs.Y;
		const FVector2D Pi = P - Nrm * FMath::Max(800.0, Span * 0.06);
		const FVector2D Po = P + Nrm * FMath::Max(1500.0, Span * 0.08);
		double Zi = 0;
		if (!OwnHit(FVector(Pi.X, Pi.Y, ZTop), FVector(Pi.X, Pi.Y, ZBot), Zi)) continue;
		Zin.Add(Zi);
		Out.Add(FVector(Po.X, Po.Y, Zi));
	}
	if (Zin.Num() < 6)
	{
		SMV_KLOG(TEXT("Gerçek dünya kenar eşleme: harita kenarında zemin örneği az (%d/%d) — kırpma %s"), Zin.Num(), N, bClip ? TEXT("tamam") : TEXT("yok"));
		bRimDone = true; RimAt = FPlatformTime::Seconds();
		return bClip;
	}
	TWeakObjectPtr<ASmvGameMode> Weak(this);
	const FString Key = FabMapKey;
	const FVector Bs = B.GetSize();
	WorldSetup->SampleSurfaceZ(Out, [Weak, Zin, Key, Bs, bClip](const TArray<double>& Zo, const TArray<bool>& Ok)
	{
		ASmvGameMode* Self = Weak.Get();
		if (!Self || !Self->WorldSetup) return;
		TArray<double> Dz;
		for (int32 i = 0; i < Zo.Num() && i < Zin.Num(); ++i) if (Ok[i]) Dz.Add((Zo[i] - Zin[i]) / 100.0);
		if (Dz.Num() >= 6)
		{
			Dz.Sort();
			// ortanca fark; haritanın kenarı tepe halkasıysa (Orman) fark büyüktür — gerçek araziyi yüzlerce metre kaydırmak uçuş yüksekliklerini
			// bozar: ±60 m ile sınırlı (kalan fark doğal yamaç gibi görünür)
			const double Med = FMath::Clamp(Dz[Dz.Num() / 2], -60.0, 60.0);
			Self->WorldSetup->ShiftVertical(Med);
			SMV_KLOG(TEXT("Gerçek dünya kenar eşleme (%s, %s): kutu %.0f×%.0f m, %d örnek, fark ortanca %.1f m (aralık %.1f…%.1f) → karolar kaydırıldı; kırpma %s"),
				*Key, *Self->WorldSetup->SourceName(), Bs.X / 100.0, Bs.Y / 100.0, Dz.Num(), Med, Dz[0], Dz.Last(), bClip ? TEXT("tamam") : TEXT("yok"));
		}
		else SMV_KLOG(TEXT("Gerçek dünya kenar eşleme (%s): karo yüksekliği örneklenemedi (%d/%d)"), *Key, Dz.Num(), Zo.Num());
		Self->bRimDone = true;
		Self->RimAt = FPlatformTime::Seconds();
	});
	return true;
}

// Fab haritası seviyesini seviye örneği olarak yükle (görünür olunca HandleFabLevelShown → PlaceFabLevels)
bool ASmvGameMode::LoadFabLevel(const FString& Level)
{
	UWorld* W = GetWorld();
	if (!W || Level.IsEmpty()) return false;
	bool bOk = false;
	ULevelStreamingDynamic* Lv = ULevelStreamingDynamic::LoadLevelInstance(W, Level, FVector::ZeroVector, FRotator::ZeroRotator, bOk);
	if (bOk && Lv)
	{
		Lv->OnLevelShown.AddDynamic(this, &ASmvGameMode::HandleFabLevelShown);
		FabLevels.Add(Lv);
		FabLevelPath = Level;
		SMV_KLOG(TEXT("Fab haritası yükleniyor: %s (%s)"), *Level, *FabMapKey);
		return true;
	}
	SMV_KLOG(TEXT("Fab haritası yüklenemedi (LoadLevelInstance): %s (%s)"), *Level, *FabMapKey);
	return false;
}

// Yükleme ekranı "Yeniden dene": harita seviyelerini boşalt, yeniden yükle ve kapıyı baştan başlat (SITL / dron / arayüz yerinde kalır)
void ASmvGameMode::RetryWorldLoad()
{
	UWorld* W = GetWorld();
	if (!W || !bScene) return;
	SMV_KLOG(TEXT("SMVLOAD [ucus] yeniden deneniyor: harita %s (%s)"), *FabMapKey, *FabLevelPath);
	for (TObjectPtr<ULevelStreamingDynamic>& Lv : FabLevels)
	{
		if (!Lv) continue;
		Lv->OnLevelShown.RemoveAll(this);
		Lv->SetShouldBeVisible(false);
		Lv->SetShouldBeLoaded(false);
		Lv->SetIsRequestingUnloadAndRemoval(true);
	}
	FabLevels.Reset();
	bFabSubsRequested = false;
	bFabPlaced = false;
	bRimPending = bRimDone = bRimStarted = false; bTilesSkipped = false; RimAt = 0; FabPlacedBox = FBox(ForceInit);
	SmvPhysics::SetClipRegion(nullptr, FBox2D(ForceInit));
	if (!FabLevelPath.IsEmpty()) LoadFabLevel(FabLevelPath);
	Gate.Begin(TEXT("ucus"), { ESmvLoad::World, ESmvLoad::Scenery, ESmvLoad::Atmos, ESmvLoad::Sitl, ESmvLoad::Shaders, ESmvLoad::Streaming, ESmvLoad::Warmup });
	AtmosT0 = WarmT0 = StreamT0 = 0; AtmosF0 = WarmF0 = 0; WarmRecap = 0; StreamPeak = ShaderPeak = 0;
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
					SMV_KLOG(TEXT("Fab alt seviyesi yükleniyor: %s"), *Pkg);
				}
				else SMV_KLOG(TEXT("Fab alt seviyesi yüklenemedi: %s"), *Pkg);
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
	TArray<UStaticMeshComponent*> BigComps;
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
				// tek başına 800 m'den büyük ağlar (uzak dağ / ufuk arka planı, dev zemin düzlemi, su) oyun alanı kutusuna sayılmaz
				// (Brushify Forest'ın Background_Mesh dağları kutuyu 28 × 36 km yapıyordu: kalkış dağ ağına düşüyor, gerçek dünya kırpması tüm bölgeyi siliyordu)
				if (SB.IsValid && SC->Bounds.BoxExtent.GetMax() > 40000.0) { BigComps.Add(SC); continue; }
				if (SB.IsValid) Box += SB;
			}
			if (Cn.Contains(TEXT("Landscape")))
			{
				const FBox LB = A->GetComponentsBoundingBox(true);
				if (LB.IsValid && LB.GetSize().GetMax() < 2.0e6) Box += LB;
			}
		}
	}
	for (AActor* A : Drop) A->Destroy();
	// gerçek dünya çevresi: oyun alanının dışına taşan arka plan ağları (sahte ufuk dağları) gizlenir — ufku 3D Tiles verir
	if (bRealSurround && Box.IsValid)
	{
		const FBox Keep = Box.ExpandBy(Box.GetExtent().GetMax() * 0.15);
		int32 Hidden = 0;
		for (UStaticMeshComponent* SC : BigComps)
		{
			if (!SC || Keep.IsInsideXY(SC->Bounds.GetBox())) continue;
			SC->SetHiddenInGame(true);
			SC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			++Hidden;
		}
		if (Hidden) SMV_KLOG(TEXT("Gerçek dünya: %d arka plan ağı (oyun alanı dışı) gizlendi"), Hidden);
	}
	// gerçek çarpışma: kalkış araması ve uçuş öncesi kapalı çarpışmaları aç (seviye eklenince zaten yapılır; burada eksik kalan olmasın)
	for (ULevel* L : Levels) PreparePhysicsLevel(L);
	if (!Box.IsValid)
	{
		SMV_KLOG(TEXT("Fab haritası boş görünüyor (statik ağ yok)"));
		return;
	}
	FCollisionQueryParams Q = SmvPhysics::MakeQuery(TEXT("SmvFabGround"), Drone, nullptr);
	if (WorldSetup && WorldSetup->GetTilesActor()) Q.AddIgnoredActor(WorldSetup->GetTilesActor());   // kalkış araması yalnız harita geometrisinde
	const double ZTop = Box.Max.Z + 100.0, ZBot = Box.Min.Z - 100.0;
	const ECollisionChannel Ch = FSmvPhysicsSettings::Get().Channel;
	auto Down = [&](double X, double Y, FHitResult& H) { return W->LineTraceSingleByChannel(H, FVector(X, Y, ZTop), FVector(X, Y, ZBot), Ch, Q); };
	// kalkış noktası FİZİKLE: aşağı ışın → zemin, yukarı küre süpürmesi → üstü açık (tavan/dal), araç boyutunda kapsül → boşluk, çevre düzlüğü
	const float SpotR = Drone ? Drone->GetVisualRadiusM() * 100.f : 150.f;
	const float SpotH = FMath::Max(80.f, SpotR * 0.6f);
	int32 Rejected = 0;
	FString LastWhy;
	auto Clear = [&](double X, double Y, double& OutZ) -> bool
	{
		FString Why;
		const bool bOk = SmvPhysics::EvaluateSpot(W, X, Y, ZTop, ZBot, SpotR, SpotH, bLandscape, Q, OutZ, &Why);
		if (!bOk) { ++Rejected; LastWhy = Why; }
		return bOk;
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
	// 2) haritanın PlayerStart'ı — altında zemin, üstü açık ve araç sığıyorsa (bina içindeki başlangıç dron için uygun değil)
	for (int32 i = 0; i < PlayerStarts.Num() && !bFound; ++i)
	{
		const FVector Ps = PlayerStarts[i];
		double Z = 0;
		FString Why;
		if (!SmvPhysics::EvaluateSpot(W, Ps.X, Ps.Y, Ps.Z + 200.0, Ps.Z - 5000.0, SpotR, SpotH, false, Q, Z, &Why))
		{
			UE_LOG(LogSimumav, Log, TEXT("PlayerStart (%.0f, %.0f) kalkışa uygun değil: %s"), Ps.X, Ps.Y, *Why);
			continue;
		}
		Start = FVector(Ps.X, Ps.Y, Z); Zg = Z; How = TEXT("PlayerStart (fizik denetimi)"); bFound = true;
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
	if (Drone && Drone->GetContactSolver()) Drone->GetContactSolver()->Reset();   // harita kaydı: dron yeni geometriye ışınlanır (olay yok)
	SMV_KLOG(TEXT("Fab haritası yerleştirildi (%s): %d seviye, kutu %s, kalkış (%.0f, %.0f, %.0f) — %s; %d gök/ışık aktörü kaldırıldı; fizik kalkış araması %d aday eledi (son: %s)"),
		*FabMapKey, Levels.Num(), *Box.GetSize().ToString(), Start.X, Start.Y, Zg, *How, Drop.Num(), Rejected, LastWhy.IsEmpty() ? TEXT("-") : *LastWhy);
	if (Def && Def->bDecor) SpawnFabDecor(FVector::ZeroVector, true);
	// gerçek dünya çevresi (Cesium): harita kutusu karolardan kırpılır, kenar yükseklikleri eşlenir (karolar yüklenince TickLoadGate → AlignRealRim)
	if (bRealSurround)
	{
		FabPlacedBox = Box.ShiftBy(Off);
		bRimPending = true;
	}
	// "havada ada" haritaları (kayıt tablosu Surround biyomu; gerçek dünya yoksa): çevreye Hyper malzemeli arazi diski + kaya/çalı + uzak dağlar
	else if (Def && Def->Surround && *Def->Surround && FSmvScenery::Enabled())
	{
		FActorSpawnParameters Sp;
		Sp.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (ASmvScenery* Sc = W->SpawnActor<ASmvScenery>(ASmvScenery::StaticClass(), FTransform::Identity, Sp))
		{
			TArray<AActor*> Ig;
			if (Drone) Ig.Add(Drone);
			if (PilotFigure) Ig.Add(PilotFigure);
			for (const TObjectPtr<AActor>& X : FabDecor) if (X) Ig.Add(X);
			if (Sc->BuildSurround(Levels, Def->Surround, 11, Ig)) FabDecor.Add(Sc);
			else Sc->Destroy();
		}
	}
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
	UMaterialInterface* Street = FSmvScenery::StreetMaterial();
	UMaterialInterface* Hyper = FSmvScenery::TerrainMaterial();
	// Mat: 0 düz renk, 1 sokak malzemesi (asfalt / kaldırım taşı; TileCm > 0 karo derzi), 2 Hyper arazi (park çimi)
	auto Slab = [&](const FVector& Ctr, const FVector& SizeCm, const FLinearColor& C, bool bCollide = false, int32 Mat = 0, float TileCm = 0.f, float Rough = 0.85f)
	{
		if (!Cube) return;
		AStaticMeshActor* A = W->SpawnActor<AStaticMeshActor>(Ctr, FRotator::ZeroRotator);
		if (!A || !A->GetStaticMeshComponent()) return;
		A->SetMobility(EComponentMobility::Movable);
		UStaticMeshComponent* SC = A->GetStaticMeshComponent();
		SC->SetStaticMesh(Cube);
		if (!bCollide) SC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		A->SetActorScale3D(SizeCm / 100.0);   // BasicShapes küpü 100 cm
		UMaterialInterface* Src = Mat == 1 && Street ? Street : Mat == 2 && Hyper ? Hyper : SmvSolidMat(BaseMat);
		if (Src)
		{
			UMaterialInstanceDynamic* D = UMaterialInstanceDynamic::Create(Src, SC);
			if (Src == Street) { D->SetVectorParameterValue(TEXT("Color"), C); D->SetScalarParameterValue(TEXT("TileCm"), TileCm); D->SetScalarParameterValue(TEXT("Rough"), Rough); }
			else if (Src == Hyper) D->SetVectorParameterValue(TEXT("Tint"), C);
			else SmvSetSolidColor(D, C);
			SC->SetMaterial(0, D);
		}
		FabDecor.Add(A);
	};
	// zemin (24 km; üstü Z 0 = SITL düz zemini; şehir dışı park çimi; ufuk dağları üstünde), caddeler (asfalt), adalarda kaldırım taşı (12 cm bordür),
	// ortada kalkış meydanı (iri karo, üstü +2 cm)
	const double Span = 5.0 * Cell + Road;
	// gerçek dünya çevresinde zemin yalnız şehir bloğu kadar (çevresi 3D Tiles); yoksa 24 km park çimi
	const double GroundCm = bRealSurround ? Span + 8000.0 : 2400000.0;
	Slab(FVector(0, 0, -50), FVector(GroundCm, GroundCm, 100), Hyper ? FLinearColor(0.9f, 0.95f, 0.85f) : FLinearColor(0.16f, 0.2f, 0.12f), true, 2);
	for (int32 k = -2; k <= 3; ++k)
	{
		const double C = (k - 0.5) * Cell;
		Slab(FVector(C, 0, 2), FVector(Road, Span, 4), FLinearColor(0.05f, 0.05f, 0.055f), false, 1, 0.f, 0.78f);
		Slab(FVector(0, C, 2.2), FVector(Span, Road, 4), FLinearColor(0.05f, 0.05f, 0.055f), false, 1, 0.f, 0.78f);
	}
	// gerçekçi albedo (doğrusal): beton kaldırım ~0,17, meydan taşı ~0,21 — eski açık tonlar (0,38 / 0,5) bulutlu havada kar gibi bembeyaz görünüyordu
	Slab(FVector(0, 0, 0), FVector(Cell - Road, Cell - Road, 4), FLinearColor(0.21f, 0.2f, 0.185f), false, 1, 140.f, 0.7f);
	for (int32 i = -2; i <= 2; ++i)
		for (int32 j = -2; j <= 2; ++j)
			if (i != 0 || j != 0) Slab(FVector(i * Cell, j * Cell, 6), FVector(Cell - Road, Cell - Road, 12), FLinearColor(0.17f, 0.165f, 0.155f), false, 1, 60.f, 0.8f);
	SpawnCityStreetscape(Cell, Road);
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
	// şehir hayatı (yalnız görsel): City Traffic Pro araçlarıyla şerit trafiği + kaldırımlarda yürüyen, meydanda duran yayalar
	FActorSpawnParameters Lp;
	Lp.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (ASmvCityLife* Life = W->SpawnActor<ASmvCityLife>(ASmvCityLife::StaticClass(), FTransform::Identity, Lp))
	{
		FabDecor.Add(Life);
		TArray<double> Coords;
		for (int32 k = -2; k <= 3; ++k) Coords.Add((k - 0.5) * Cell);
		const int32 NCars = Req.Track.World.Vehicles > 0 ? Req.Track.World.Vehicles : (int32)FSmvCityLife::GetFloat(TEXT("CityCars"), 72.f);
		// OverCrowd VAT kalabalığı ucuz (örnekli statik ağ, iskelet yok) → varsayılan çok daha kalabalık; iskeletli yedekte 120
		const bool bVatCrowd = FSmvCityLife::VatCrowdMeshes().Num() > 0;
		const int32 NPeople = Req.Track.World.People > 0 ? Req.Track.World.People
			: (int32)FSmvCityLife::GetFloat(bVatCrowd ? TEXT("CityPeopleVat") : TEXT("CityPeople"), bVatCrowd ? 360.f : 120.f);
		Life->BuildGridTraffic(Coords, Road, 2.5 * Cell + Road * 0.5, NCars, Req.Track.World.Seed ? Req.Track.World.Seed : 7);
		// kaldırım halkaları (her adanın bordürünün 1,5 m içi; ada üstü Z 12 cm), meydanda grup noktaları (kalkış noktasından ≥ 15 m)
		TArray<TArray<FVector>> Walk;
		TArray<FVector> Spots;
		const double Hs = (Cell - Road) * 0.5 - 150.0;
		for (int32 i = -2; i <= 2; ++i)
			for (int32 j = -2; j <= 2; ++j)
			{
				if (i == 0 && j == 0) continue;
				const FVector C(i * Cell, j * Cell, 12.0);
				Walk.Add({ C + FVector(-Hs, -Hs, 0), C + FVector(Hs, -Hs, 0), C + FVector(Hs, Hs, 0), C + FVector(-Hs, Hs, 0) });
			}
		const int32 NSpot = bVatCrowd ? 16 : 8;
		for (int32 k = 0; k < NSpot; ++k)
		{
			const double A = 2.0 * UE_DOUBLE_PI * (k + 0.5) / NSpot;
			Spots.Add(FVector(FMath::Cos(A), FMath::Sin(A), 0.0) * (1500.0 + 1000.0 * (k % 2)) + FVector(0, 0, 2.0));
		}
		Life->BuildCrowd(Walk, Spots, NPeople, 11);
	}
}

// Şehir sokak dokusu (yalnız görsel): yol orta şeridi kesikli çizgileri, kavşak yaya geçitleri, ada köşelerinde sokak lambaları
// (alacakaranlık/gece başları parlar), kalkış meydanı çevresinde ağaçlar (Brushify), şehir dışında park ağaçları/kayalar ve ufuk dağları.
void ASmvGameMode::SpawnCityStreetscape(double Cell, double Road)
{
	UWorld* W = GetWorld();
	if (!W) return;
	FActorSpawnParameters Sp;
	Sp.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASmvScenery* Sc = W->SpawnActor<ASmvScenery>(ASmvScenery::StaticClass(), FTransform::Identity, Sp);
	if (!Sc) return;
	FabDecor.Add(Sc);
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Cyl = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UStaticMesh* Sph = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UMaterialInterface* Solid = SmvSolidMat(LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")));
	auto Mid = [&](const FLinearColor& C, const FLinearColor& Em = FLinearColor::Black) -> UMaterialInterface*
	{
		if (!Solid) return nullptr;
		UMaterialInstanceDynamic* D = UMaterialInstanceDynamic::Create(Solid, Sc);
		SmvSetSolidColor(D, C);
		if (!Em.Equals(FLinearColor::Black)) D->SetVectorParameterValue(TEXT("Emissive"), Em);
		return D;
	};
	const double Half = 2.5 * Cell + Road * 0.5;   // şehir kenarı (cm, merkezden)
	// yol çizgileri: her caddenin ortasında 3 m çizgi / 6 m boşluk, 15 cm genişlik; kavşak içleri boş
	TArray<FTransform> Dash, Zebra;
	for (int32 k = -2; k <= 3; ++k)
	{
		const double C = (k - 0.5) * Cell;
		for (double t = -Half; t <= Half; t += 900.0)
		{
			const double Rel = FMath::Fmod(FMath::Abs(t) + Cell * 0.5, Cell);
			if (Rel < Road * 0.5 + 300.0 || Rel > Cell - Road * 0.5 - 300.0) continue;   // kavşak
			Dash.Add(FTransform(FRotator::ZeroRotator, FVector(C, t, 4.6), FVector(0.15, 3.0, 0.01)));
			Dash.Add(FTransform(FRotator::ZeroRotator, FVector(t, C, 4.8), FVector(3.0, 0.15, 0.01)));
		}
		// yaya geçitleri: kavşaktan çıkışta cadde genişliğince 50 cm şeritler (50 cm aralık)
		for (int32 m = -2; m <= 3; ++m)
		{
			const double D = (m - 0.5) * Cell;
			const int32 NZ = FMath::Max(1, (int32)((Road - 400.0) / 100.0));
			for (int32 z = 0; z < NZ; ++z)
			{
				const double Off = -Road * 0.5 + 200.0 + z * 100.0 + 25.0;
				const double Ax = C + Off;
				Zebra.Add(FTransform(FRotator::ZeroRotator, FVector(Ax, D + Road * 0.5 + 250.0, 4.7), FVector(0.5, 3.0, 0.01)));
				Zebra.Add(FTransform(FRotator::ZeroRotator, FVector(D + Road * 0.5 + 250.0, Ax, 4.9), FVector(3.0, 0.5, 0.01)));
			}
		}
	}
	UMaterialInterface* White = Mid(FLinearColor(0.75f, 0.75f, 0.72f));
	Sc->AddMeshInstances(Cube, Dash, false, 700.0, White, false);
	Sc->AddMeshInstances(Cube, Zebra, false, 700.0, White, false);
	// sokak lambaları: ada köşelerinde (bordür içinde 1 m), 8 m direk + kol + baş
	const FString Tod = Req.Track.TimeOfDay;
	const bool bLit = Tod == TEXT("night") || Tod == TEXT("dusk");
	TArray<FTransform> Poles, Arms, Heads;
	for (int32 i = -2; i <= 2; ++i)
	{
		for (int32 j = -2; j <= 2; ++j)
		{
			const double Hs = (Cell - Road) * 0.5 - 100.0;
			for (int32 q = 0; q < 4; ++q)
			{
				const double Sx = (q & 1) ? 1.0 : -1.0, Sy = (q & 2) ? 1.0 : -1.0;
				const FVector Base(i * Cell + Sx * Hs, j * Cell + Sy * Hs, 12.0);
				Poles.Add(FTransform(FRotator::ZeroRotator, Base + FVector(0, 0, 400.0), FVector(0.14, 0.14, 8.0)));
				const FVector Out = FVector(Sx, Sy, 0.0).GetSafeNormal();
				Arms.Add(FTransform(Out.Rotation(), Base + FVector(0, 0, 790.0) + Out * 90.0, FVector(1.8, 0.08, 0.08)));
				Heads.Add(FTransform(FRotator::ZeroRotator, Base + FVector(0, 0, 780.0) + Out * 180.0, FVector(0.5, 0.3, 0.16)));
			}
		}
	}
	Sc->AddMeshInstances(Cyl, Poles, true, 2500.0, Mid(FLinearColor(0.08f, 0.085f, 0.09f)));
	Sc->AddMeshInstances(Cube, Arms, false, 1500.0, Mid(FLinearColor(0.08f, 0.085f, 0.09f)));
	Sc->AddMeshInstances(Sph, Heads, false, 3000.0, Mid(FLinearColor(0.9f, 0.85f, 0.7f), bLit ? FLinearColor(6.f, 4.6f, 2.6f) : FLinearColor(0.02f, 0.02f, 0.02f)), false);
	// kalkış meydanı çevresi ağaçları (Brushify; meydanın kenarından 3 m içeride, 12 m arayla — ortası açık)
	const TArray<UStaticMesh*>& Trees = FSmvScenery::Meshes(ESmvSceneryKind::Tree);
	if (Trees.Num() > 0)
	{
		TArray<UStaticMesh*> Pick;
		for (UStaticMesh* M : Trees) if (M->GetName().Contains(TEXT("Medium")) || M->GetName().Contains(TEXT("Small"))) Pick.Add(M);
		if (Pick.Num() == 0) Pick = Trees;
		TMap<UStaticMesh*, TArray<FTransform>> ByMesh;
		const double Hp = (Cell - Road) * 0.5 - 300.0;
		int32 n = 0;
		for (double t = -Hp; t <= Hp + 1.0; t += 1200.0)
		{
			for (int32 e = 0; e < 4; ++e)
			{
				const FVector P = e == 0 ? FVector(t, -Hp, 2.0) : e == 1 ? FVector(t, Hp, 2.0) : e == 2 ? FVector(-Hp, t, 2.0) : FVector(Hp, t, 2.0);
				UStaticMesh* Tm = Pick[(n * 5 + 1) % Pick.Num()];
				ByMesh.FindOrAdd(Tm).Add(FTransform(FRotator(0.0, n * 47.0, 0.0), P, FVector(0.75)));
				++n;
			}
		}
		for (TPair<UStaticMesh*, TArray<FTransform>>& It : ByMesh) Sc->AddMeshInstances(It.Key, It.Value, true, 3000.0);
		// şehir dışı park: ağaç / çalı / kaya (şehir kenarından 1,2 km'ye)
		FSmvScatterDensity D;
		D.Trees = 900; D.Bushes = 300; D.Rocks = 80; D.Grass = 0;
		const double Edge = Half / 100.0 + 40.0;
		if (!bRealSurround) Sc->Scatter(Edge, 1450.0, D, 23, [](double E, double N, FVector& P, FVector& Up) { P = FVector(E * 100.0, -N * 100.0, 0.0); Up = FVector::UpVector; return true; });
	}
	// sahte ufuk dağları yalnız gerçek dünya yokken (gerçek ufuk 3D Tiles'tan)
	if (!bRealSurround) Sc->AddMountains(FVector::ZeroVector, 600000.0, 1100000.0, -2000.0, 12, TEXT("Rocky"), 31, 50000.0, 150000.0);
	UE_LOG(LogSimumav, Log, TEXT("Şehir sokak dokusu: %d çizgi, %d yaya geçidi şeridi, %d lamba (%s), %d manzara örneği"), Dash.Num(), Zebra.Num(), Heads.Num(), bLit ? TEXT("yanık") : TEXT("sönük"), Sc->GetInstanceCount());
}

void ASmvGameMode::UpdateJetHud(const FCaySimTelemetry& T, ASmvPilotPawn* Pw)
{
	const bool bPlane = Req.Drone.Type == TEXT("plane") || Req.Drone.Type == TEXT("vtol");
	bool bOn = true;
	if (GConfig) GConfig->GetBool(TEXT("/Script/Simumav.SmvScenery"), TEXT("JetHud"), bOn, GGameIni);
	const bool bWant = bOn && bPlane && Pw && Pw->IsHudOn() && Pw->GetMode() == ESmvCamMode::Fpv;
	if (!JetHud && bWant && !bJetHudTried)
	{
		bJetHudTried = true;
		if (FPackageName::DoesPackageExist(TEXT("/Game/Aircraft_HUD/UI/HUD/WBP_Jet_HUD")))
		{
			if (UClass* C = LoadClass<UUserWidget>(nullptr, TEXT("/Game/Aircraft_HUD/UI/HUD/WBP_Jet_HUD.WBP_Jet_HUD_C"), nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				JetHud = CreateWidget<UUserWidget>(GetWorld(), C);
				if (JetHud) JetHud->AddToViewport(4);
				UE_LOG(LogSimumav, Log, TEXT("Jet HUD (Fab Aircraft HUD): %s"), JetHud ? TEXT("kuruldu") : TEXT("kurulamadı"));
			}
		}
	}
	if (!JetHud) return;
	JetHud->SetVisibility(bWant ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (!bWant || !Drone) return;
	UFunction* F = JetHud->FindFunction(FName(TEXT("Update HUD")));
	if (!F) return;
	float RollR = 0.f, PitchR = 0.f, YawR = 0.f;
	Drone->GetRenderedPose(RollR, PitchR, YawR);
	const float SpdKmh = (T.Airspeed > 0.1f ? T.Airspeed : T.Groundspeed) * 3.6f;
	TArray<uint8> Buf;
	Buf.SetNumZeroed(F->ParmsSize);
	for (TFieldIterator<FProperty> It(F); It && It->HasAnyPropertyFlags(CPF_Parm); ++It) It->InitializeValue_InContainer(Buf.GetData());
	auto Num = [&](const TCHAR* N, double V)
	{
		if (FNumericProperty* P = CastField<FNumericProperty>(F->FindPropertyByName(FName(N)))) { void* Ptr = P->ContainerPtrToValuePtr<void>(Buf.GetData()); if (P->IsFloatingPoint()) P->SetFloatingPointPropertyValue(Ptr, V); else P->SetIntPropertyValue(Ptr, (int64)V); }
	};
	auto Txt = [&](const TCHAR* N, const FString& V)
	{
		if (FTextProperty* P = CastField<FTextProperty>(F->FindPropertyByName(FName(N)))) P->SetPropertyValue_InContainer(Buf.GetData(), FText::FromString(V));
	};
	auto Vis = [&](const TCHAR* N, ESlateVisibility V)
	{
		FProperty* P = F->FindPropertyByName(FName(N));
		if (FEnumProperty* E = CastField<FEnumProperty>(P)) E->GetUnderlyingProperty()->SetIntPropertyValue(E->ContainerPtrToValuePtr<void>(Buf.GetData()), (int64)V);
		else if (FByteProperty* B = CastField<FByteProperty>(P)) B->SetIntPropertyValue(B->ContainerPtrToValuePtr<void>(Buf.GetData()), (int64)V);
	};
	Num(TEXT("Altitude"), T.AltRel);
	Num(TEXT("Pitch Value"), FMath::RadiansToDegrees(PitchR));
	Num(TEXT("Roll Value"), FMath::RadiansToDegrees(RollR));
	Num(TEXT("Speed"), SpdKmh);
	Num(TEXT("Yaw Value"), T.Heading);
	Txt(TEXT("Altitude_T"), FString::Printf(TEXT("%.0f"), T.AltRel));
	Txt(TEXT("Speed_T"), FString::Printf(TEXT("%.0f"), SpdKmh));
	Vis(TEXT("WRN_V"), ActiveAlerts.Num() > 0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	Vis(TEXT("InAir_V"), T.AltRel > 1.0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	// hedef kilidi göstergeleri kullanılmaz (yalnız uçuş göstergesi)
	Vis(TEXT("Lock_V"), ESlateVisibility::Collapsed);
	Vis(TEXT("Lock_A"), ESlateVisibility::Collapsed);
	Vis(TEXT("Lock_B"), ESlateVisibility::Collapsed);
	JetHud->ProcessEvent(F, Buf.GetData());
	for (TFieldIterator<FProperty> It(F); It && It->HasAnyPropertyFlags(CPF_Parm); ++It) It->DestroyValue_InContainer(Buf.GetData());
}

void ASmvGameMode::TickCityWindows()
{
	if (bCityWindowsDone || CityLevels.Num() == 0) return;
	bool bAll = true;
	for (const TObjectPtr<ULevelStreamingDynamic>& Lv : CityLevels) if (Lv && !Lv->IsLevelVisible()) bAll = false;
	if (!bAll && FPlatformTime::Seconds() - SceneT0 < 45.0) return;
	bCityWindowsDone = true;
	bool bSwap = true;
	if (GConfig) GConfig->GetBool(TEXT("/Script/Simumav.SmvScenery"), TEXT("CityWindowSwap"), bSwap, GGameIni);
	TArray<UMaterialInterface*> Ics;
	static const TCHAR* Presets[] = { TEXT("MI_ICS_Offices_M_2k"), TEXT("MI_ICS_Offices_L_2k"), TEXT("MI_ICS_Rooms_L_2k"), TEXT("MI_ICS_Offices_S3_1k"), TEXT("MI_ICS_Rooms_S_2k"), TEXT("MI_ICS_Lobbies_M_4k") };
	for (const TCHAR* Pr : Presets)
	{
		const FString Pk = FString(TEXT("/Game/InteriorCubemapSystem/Presets/")) + Pr;
		if (!FPackageName::DoesPackageExist(Pk)) continue;
		if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, *(Pk + TEXT(".") + Pr), nullptr, LOAD_NoWarn | LOAD_Quiet)) Ics.Add(M);
	}
	auto LogParams = [](UMaterialInterface* M)
	{
		TArray<FMaterialParameterInfo> Infos;
		TArray<FGuid> Ids;
		M->GetAllScalarParameterInfo(Infos, Ids);
		FString Out;
		for (const FMaterialParameterInfo& I : Infos)
		{
			float V = 0.f;
			M->GetScalarParameterValue(FHashedMaterialParameterInfo(I), V);
			Out += FString::Printf(TEXT("%s=%.3g "), *I.Name.ToString(), V);
		}
		UE_LOG(LogSimumav, Log, TEXT("Pencere malzemesi %s (taban %s): %s"), *M->GetName(), M->GetMaterial() ? *M->GetMaterial()->GetName() : TEXT("-"), *Out);
	};
	for (UMaterialInterface* M : Ics) LogParams(M);
	// gece/alacakaranlıkta özgün pencere ışıması kısılır (değiştirme kapalıysa)
	const float Dim = FSmvScenery::GetFloat(TEXT("CityWindowEmissive"), 0.3f);
	int32 Swapped = 0, Dimmed = 0;
	TSet<FString> Seen;
	for (const TObjectPtr<ULevelStreamingDynamic>& Lv : CityLevels)
	{
		ULevel* L = Lv ? Lv->GetLoadedLevel() : nullptr;
		if (!L) continue;
		for (AActor* A : L->Actors)
		{
			if (!IsValid(A)) continue;
			TInlineComponentArray<UStaticMeshComponent*> Comps(A);
			for (UStaticMeshComponent* C : Comps)
			{
				if (!C) continue;
				for (int32 i = 0; i < C->GetNumMaterials(); ++i)
				{
					UMaterialInterface* M = C->GetMaterial(i);
					if (!M) continue;
					const UMaterial* Base = M->GetMaterial();
					const FString Mn = M->GetName();
					const bool bWin = (Base && Base->GetName().Contains(TEXT("Window"))) || Mn.Contains(TEXT("Window")) || Mn.Contains(TEXT("Glass"));
					if (!bWin) continue;
					if (!Seen.Contains(Mn)) { Seen.Add(Mn); LogParams(M); }
					if (bSwap && Ics.Num() > 0)
					{
						C->SetMaterial(i, Ics[(C->GetUniqueID() / 7 + i) % Ics.Num()]);
						++Swapped;
					}
					else
					{
						UMaterialInstanceDynamic* D = UMaterialInstanceDynamic::Create(M, C);
						TArray<FMaterialParameterInfo> Infos;
						TArray<FGuid> Ids;
						M->GetAllScalarParameterInfo(Infos, Ids);
						for (const FMaterialParameterInfo& I : Infos)
						{
							const FString N = I.Name.ToString();
							if (!(N.Contains(TEXT("Emiss")) || N.Contains(TEXT("Bright")) || N.Contains(TEXT("Intens")) || N.Contains(TEXT("Light")))) continue;
							float V = 0.f;
							if (M->GetScalarParameterValue(FHashedMaterialParameterInfo(I), V)) D->SetScalarParameterValue(I.Name, V * Dim);
						}
						C->SetMaterial(i, D);
						++Dimmed;
					}
				}
			}
		}
	}
	UE_LOG(LogSimumav, Log, TEXT("Şehir pencereleri: %d yuva Interior Cubemap ile değişti, %d kısıldı (%d ön ayar, %d farklı pencere malzemesi)"), Swapped, Dimmed, Ics.Num(), Seen.Num());
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
	bCityWindowsDone = false;
	FabMapKey.Reset();
	SmvPhysics::SetClipRegion(nullptr, FBox2D(ForceInit));
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

// ---------------------------------------------------------------------------
// video kaydı
// ---------------------------------------------------------------------------

void ASmvGameMode::ToggleVideo()
{
	if (!VideoRec.IsValid()) VideoRec = MakeUnique<FSmvVideoRec>();
	if (VideoRec->IsOn())
	{
		const FString P = VideoRec->Stop();
		if (!P.IsEmpty()) PushEvent(FSmvI18n::Format(TEXT("video.saved"), { { TEXT("p"), FPaths::GetCleanFilename(P) } }).ToString(), 6);
		return;
	}
	if (VideoRec->Start(15, 1280)) PushEvent(FSmvI18n::T(TEXT("video.started")).ToString(), 6);
	else PushEvent(FSmvI18n::T(TEXT("video.failed")).ToString(), 4);
}

bool ASmvGameMode::IsRecording() const { return VideoRec.IsValid() && VideoRec->IsOn(); }
double ASmvGameMode::RecSeconds() const { return VideoRec.IsValid() ? VideoRec->Seconds() : 0.0; }

// ---------------------------------------------------------------------------
// yer kontrol sahası: radar temasları (kule merkezli ENU), EO izleme ve yakalama
// ---------------------------------------------------------------------------

void ASmvGameMode::UpdateSite(FSmvFlightUiState& U)
{
	if (!Site) return;
	U.bRadar = Req.Site.bRadar; U.bEo = Req.Site.bEo;
	U.bRadarOn = bRadarOn; U.bEoOn = bEoOn;
	U.RadarKm = (float)Req.Site.RadarKm; U.EoKm = (float)Req.Site.EoKm;
	U.Sweep = (float)Site->SweepDeg();
	const FVector Base = Site->GetActorLocation();
	auto Enu = [&Base](const FVector& Wp) { const FVector D = (Wp - Base) / 100.0; return FVector(D.X, -D.Y, D.Z); };
	TArray<TPair<int32, FVector>> Objs;   // id → dünya
	if (Drone) Objs.Add(TPair<int32, FVector>(0, Drone->GetActorLocation()));
	for (int32 i = 0; i < TargetActors.Num(); ++i) if (TargetActors[i]) Objs.Add(TPair<int32, FVector>(i + 1, TargetActors[i]->GetActorLocation()));
	for (int32 i = 0; i < Ghosts.Num(); ++i) if (Ghosts[i]) Objs.Add(TPair<int32, FVector>(100 + i, Ghosts[i]->GetActorLocation()));
	U.Blips.Reset();
	const double Cov = Req.Site.RadarKm * 1000.0;
	FVector EoTargetW = FVector::ZeroVector;
	bool bEoHas = false;
	for (const TPair<int32, FVector>& O : Objs)
	{
		const FVector E = Enu(O.Value);
		if (FVector2D(E.X, E.Y).Size() > Cov) continue;
		FSmvFlightUiState::FBlip B;
		B.E = (float)E.X; B.N = (float)E.Y; B.Alt = (float)E.Z; B.Id = O.Key; B.bOwn = O.Key == 0; B.bSel = O.Key == EoSel;
		U.Blips.Add(B);
		if (O.Key == EoSel) { EoTargetW = O.Value; bEoHas = true; }
	}
	if (!bEoHas && Drone) { EoTargetW = Drone->GetActorLocation(); bEoHas = EoSel == 0; }
	if (!Req.Site.bEo) return;
	static const TCHAR* Types[] = { TEXT("site.day"), TEXT("site.thermal"), TEXT("site.night") };
	U.EoType = FSmvI18n::T(Types[FMath::Clamp(EoTypeIdx, 0, 2)]).ToString();
	U.EoRt = Site->GetEoTarget();
	if (!bEoHas) { U.EoState = FSmvI18n::T(TEXT("site.eo_idle")).ToString(); return; }
	const FVector From = Site->EoWorld();
	const double Rng = FVector::Dist(From, EoTargetW) / 100.0;
	const FVector D = EoTargetW - From;
	const double Az = FMath::Fmod(FMath::RadiansToDegrees(FMath::Atan2(D.X, -D.Y)) + 360.0, 360.0);
	const double El = FMath::RadiansToDegrees(FMath::Atan2(D.Z, FVector2D(D.X, D.Y).Size()));
	U.EoInfo = FString::Printf(TEXT("%s %05.1f°  %s %+05.1f°  %s %.0f m"), *FSmvI18n::T(TEXT("site.az")).ToString(), Az, *FSmvI18n::T(TEXT("site.el")).ToString(), El, *FSmvI18n::T(TEXT("site.rng")).ToString(), Rng);
	if (Rng > Req.Site.EoKm * 1000.0) { U.EoState = FSmvI18n::T(TEXT("site.eo_out")).ToString(); return; }
	U.EoState = FSmvI18n::T(TEXT("site.eo_track")).ToString() + (EoSel == 0 ? TEXT(" · ") + FSmvI18n::T(TEXT("site.own")).ToString() : FString::Printf(TEXT(" · T%d"), EoSel));
	// otomatik yakınlaştırma: hedef ~8 m'lik alanı doldursun
	const float Fov = FMath::Clamp((float)FMath::RadiansToDegrees(2.0 * FMath::Atan(8.0 / FMath::Max(1.0, Rng))), 0.35f, 48.f);
	U.EoFov = Fov;
	const bool bCap = bEoOn && (++EoFrame % 2 == 0);
	if (bCap && (EoFrame % 60 == 2))
	{
		FPostProcessSettings P;
		if (EoTypeIdx == 1) { P.bOverride_ColorSaturation = true; P.ColorSaturation = FVector4(0, 0, 0, 1); P.bOverride_ColorContrast = true; P.ColorContrast = FVector4(1.8f, 1.8f, 1.8f, 1.f); }
		else if (EoTypeIdx == 2) { P.bOverride_ColorSaturation = true; P.ColorSaturation = FVector4(0, 0, 0, 1); P.bOverride_ColorGain = true; P.ColorGain = FVector4(0.35f, 1.f, 0.4f, 1.f); P.bOverride_AutoExposureBias = true; P.AutoExposureBias = 3.f; }
		Site->SetEoPost(P);
	}
	Site->AimEo(EoTargetW, Fov, bCap);
}

void ASmvGameMode::RadarPick(int32 Id)
{
	EoSel = Id;
	if (Id > 0 && Id < 100)
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("action"), TEXT("lock"));
		P->SetNumberField(TEXT("i"), Id);
		Command(TEXT("engage"), P);
	}
	FSmvUiSound::Play(ESmvSnd::Click);
}

void ASmvGameMode::CycleEoType() { EoTypeIdx = (EoTypeIdx + 1) % 3; EoFrame = 1; FSmvUiSound::Play(ESmvSnd::Toggle); }

void ASmvGameMode::ConfirmUi(const FText& Title, const FText& Body, const FText& Yes, bool bDanger, TFunction<void()> OnYes)
{
	if (Shell.IsValid()) Shell->Confirm(Title, Body, Yes, bDanger, MoveTemp(OnYes));
	else if (OnYes) OnYes();
}
