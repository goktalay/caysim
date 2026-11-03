#include "SmvMenuScene.h"
#include "SmvGlbBody.h"
#include "SimumavModule.h"
#include "SmvWorldActors.h"
#include "SmvGameInstance.h"
#include "SmvDataService.h"
#include "SmvMotorSynth.h"
#include "SmvUiPrefs.h"
#include "SmvPilotFigure.h"
#include "SmvUiKit.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "CesiumWorldSetup.h"
#include "CaySimTypes.h"
#include "Camera/CameraActor.h"
#include "GameFramework/PlayerStart.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/PointLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "UObject/UObjectIterator.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ConfigCacheIni.h"
#include "SmvFabContent.h"
#include "Engine/LevelStreamingDynamic.h"
#include "UObject/UObjectGlobals.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Particles/FXSystemComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "SmvScenery.h"
#include "Misc/PackageName.h"
#include "SmvMapRegistry.h"
#include "SmvKitYard.h"
#include "Components/AudioComponent.h"
#include "SmvGameRulesActor.h"
#include "MediaPlayer.h"
#include "MediaTexture.h"
#include "FileMediaSource.h"
#include "HAL/FileManager.h"

namespace
{
	const TCHAR* GSmvMenuSection = TEXT("/Script/Simumav.SmvMenuScene");
	/** Önizleme sahnenin çok üstünde (yalnız kendisi yakalanır; arazi/gök görünmez) */
	const FVector GSmvPreviewAt(0.0, 0.0, 400000.0);

	FLinearColor SmvHexColor(const FString& Hex)
	{
		return Hex.Len() == 7 ? FLinearColor::FromSRGBColor(FColor::FromHex(Hex)) : FLinearColor(0.9f, 0.25f, 0.2f);
	}

	ECaySimDroneType SmvMenuCaysimType(const FString& T)
	{
		if (T == TEXT("plane")) return ECaySimDroneType::FixedWing;
		if (T == TEXT("vtol") || T == TEXT("interceptor")) return ECaySimDroneType::Vtol;
		if (T == TEXT("heli")) return ECaySimDroneType::Helicopter;
		return ECaySimDroneType::Multirotor;
	}

	float SmvCfg(const TCHAR* Key, float Def)
	{
		float V = Def;
		if (GConfig) GConfig->GetFloat(GSmvMenuSection, Key, V, GGameIni);
		return V;
	}
}

ASmvMenuScene::ASmvMenuScene()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("PreviewCapture"));
	Capture->SetupAttachment(Root);
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->FOVAngle = 28.f;
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	KeyLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("KeyLight"));
	KeyLight->SetupAttachment(Root);
	RimLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("RimLight"));
	RimLight->SetupAttachment(Root);
	WallLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("WallLight"));
	WallLight->SetupAttachment(Root);
	StudioWall = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StudioWall"));
	StudioWall->SetupAttachment(Root);
	StudioFloor = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StudioFloor"));
	StudioFloor->SetupAttachment(Root);
	{
		static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
		static ConstructorHelpers::FObjectFinder<UStaticMesh> Cyl(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
		StudioWall->SetStaticMesh(Cube.Object);
		StudioFloor->SetStaticMesh(Cyl.Object);
		for (UStaticMeshComponent* C : { StudioWall.Get(), StudioFloor.Get() })
		{
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			C->SetCastShadow(false);
			C->SetLightingChannels(false, true, false);
			C->SetVisibleInSceneCaptureOnly(true);   // ana menü kamerası göğünde görünmesin
		}
	}
	SnapCap = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("SnapCapture"));
	SnapCap->SetupAttachment(Root);
	SnapCap->bCaptureEveryFrame = false;
	SnapCap->bCaptureOnMovement = false;
	SnapCap->FOVAngle = 26.f;
	SnapCap->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	SnapCap->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
}

void ASmvMenuScene::BeginPlay()
{
	Super::BeginPlay();
	T0 = FPlatformTime::Seconds();
	// ---- önizleme ----
	PreviewRT = NewObject<UTextureRenderTarget2D>(this, TEXT("SmvPreviewRT"));
	PreviewRT->ClearColor = FLinearColor(0.f, 0.f, 0.f, 0.f);
	PreviewRT->InitAutoFormat(960, 600);
	PreviewRT->UpdateResourceImmediate(true);
	Capture->TextureTarget = PreviewRT;
	Capture->ShowFlags.SetAtmosphere(false);
	Capture->ShowFlags.SetFog(false);
	Capture->ShowFlags.SetVolumetricFog(false);
	Capture->ShowFlags.SetCloud(false);
	Capture->ShowFlags.SetMotionBlur(false);
	FPostProcessSettings& PP = Capture->PostProcessSettings;
	PP.bOverride_AutoExposureMethod = true;
	PP.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	PP.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	PP.AutoExposureApplyPhysicalCameraExposure = false;
	PP.bOverride_AutoExposureBias = true;
	PP.AutoExposureBias = SmvCfg(TEXT("PreviewExposure"), 0.0f);
	Capture->PostProcessBlendWeight = 1.f;
	SnapCap->ShowFlags = Capture->ShowFlags;
	SnapCap->PostProcessSettings = Capture->PostProcessSettings;
	SnapCap->PostProcessBlendWeight = 1.f;
	for (UPointLightComponent* Lt : { KeyLight.Get(), RimLight.Get(), WallLight.Get() })
	{
		Lt->SetIntensityUnits(ELightUnits::Candelas);
		Lt->SetAttenuationRadius(4000.f);
		Lt->SetCastShadows(false);
		Lt->SetLightingChannels(false, true, false);
	}
	KeyLight->SetIntensity(SmvCfg(TEXT("PreviewLight"), 40.f));
	// stüdyo malzemeleri: lacivert duvar (hafif kendinden ışıklı — siyah fon olmasın) + parlak, yansıtan koyu zemin
	if (UMaterialInterface* Sm = SmvSolidMat(StudioWall->GetMaterial(0)))
	{
		UMaterialInstanceDynamic* Mw = UMaterialInstanceDynamic::Create(Sm, this); SmvSetSolidColor(Mw, FLinearColor(0.05f, 0.08f, 0.15f));
		Mw->SetVectorParameterValue(TEXT("Emissive"), FLinearColor(0.012f, 0.022f, 0.05f));
		StudioWall->SetMaterial(0, Mw);
		UMaterialInstanceDynamic* Mf = UMaterialInstanceDynamic::Create(Sm, this); SmvSetSolidColor(Mf, FLinearColor(0.04f, 0.065f, 0.11f));
		Mf->SetScalarParameterValue(TEXT("Roughness"), 0.16f);
		Mf->SetScalarParameterValue(TEXT("Metallic"), 0.2f);
		StudioFloor->SetMaterial(0, Mf);
	}
	WallLight->SetLightColor(FLinearColor(0.35f, 0.62f, 1.f));
	WallLight->SetAttenuationRadius(1600.f);
	WallLight->SetIntensity(SmvCfg(TEXT("PreviewWallLight"), 22.f));
	PlaceStudio(0.f);
	KeyLight->SetLightColor(FLinearColor(1.f, 0.96f, 0.9f));
	RimLight->SetIntensity(SmvCfg(TEXT("PreviewLight"), 40.f) * 0.7f);
	RimLight->SetLightColor(FLinearColor(0.35f, 0.75f, 1.f));

	// ---- canlı dünya ----
	if (FSmvUiPrefs::Get().bMenuScene) BuildWorld();
	StartMenuVideo();
}

void ASmvMenuScene::StartMenuVideo()
{
	bool bOn = true;
	if (GConfig) GConfig->GetBool(GSmvMenuSection, TEXT("MenuVideo"), bOn, GGameIni);
	if (!bOn || FParse::Param(FCommandLine::Get(), TEXT("SmvRecordMenu")) || FParse::Param(FCommandLine::Get(), TEXT("SmvNoMenuVideo"))) return;
	const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("Movies/SmvMenu.mp4"));
	if (!IFileManager::Get().FileExists(*Path)) { UE_LOG(LogSimumav, Log, TEXT("Menü videosu yok (%s) — canlı sahne"), *Path); return; }
	BgPlayer = NewObject<UMediaPlayer>(this, TEXT("SmvMenuPlayer"));
	BgPlayer->SetLooping(true);
	BgPlayer->PlayOnOpen = true;
	BgTex = NewObject<UMediaTexture>(this, TEXT("SmvMenuVideoTex"));
	BgTex->AutoClear = true;
	BgTex->ClearColor = FLinearColor::Black;
	BgTex->SetMediaPlayer(BgPlayer);
	BgTex->UpdateResource();
	BgSrc = NewObject<UFileMediaSource>(this, TEXT("SmvMenuVideoSrc"));
	BgSrc->SetFilePath(Path);
	BgPlayer->OnMediaOpened.AddDynamic(this, &ASmvMenuScene::HandleMenuVideoOpened);
	const bool bOk = BgPlayer->OpenSource(BgSrc);
	UE_LOG(LogSimumav, Log, TEXT("Menü videosu: %s %s"), *Path, bOk ? TEXT("açılıyor") : TEXT("AÇILAMADI — canlı sahne"));
	if (!bOk) { BgPlayer = nullptr; BgTex = nullptr; BgSrc = nullptr; }
}

void ASmvMenuScene::HandleMenuVideoOpened(FString Url)
{
	if (BgPlayer && BgPlayer->SupportsSeeking()) BgPlayer->Seek(FTimespan::FromSeconds(21.0));
}

UTexture* ASmvMenuScene::GetMenuVideo() const { return IsMenuVideoPlaying() ? BgTex.Get() : nullptr; }

bool ASmvMenuScene::IsMenuVideoPlaying() const
{
	// ilk kareler çözülene dek (≈0,3 sn) canlı sahne görünür kalır
	return BgPlayer && BgTex && BgPlayer->IsPlaying() && BgPlayer->GetTime() > FTimespan::FromSeconds(0.3);
}

void ASmvMenuScene::BeginRecord()
{
	SceneT = 0.0;
	bCamInit = false;
	bFlyZ = false;
	LookS = FVector::ZeroVector;
	UE_LOG(LogSimumav, Log, TEXT("Menü kaydı: sahne saati sıfırlandı (döngü %.0f sn)"), LoopSeconds);
}

void ASmvMenuScene::BuildWorld()
{
	UWorld* W = GetWorld();
	if (!W) return;
	FCaySimSession S;
	S.Center.Lat = Lat0;
	S.Center.Lon = Lon0;
	S.Center.Alt = GroundMsl;
	S.Center.bHasAlt = true;
	S.World.Mode = TEXT("procedural");
	S.World.Time = TEXT("dusk");
	S.World.WindMs = 3.0;
	S.World.WindDir = 270.0;
	S.World.SizeM = 4000.0;
	S.World.Seed = 7;
	S.World.Fog = 0.25f;
	S.World.bRunway = true;
	S.World.RunwayHeading = 70.0;
	S.Type = ECaySimDroneType::FixedWing;
	S.Model.Orient.Forward = TEXT("+x");
	S.Model.Orient.Up = TEXT("+y");
	S.Model.Orient.Scale = 1.f;

	WorldSetup = W->SpawnActor<ACaySimWorldSetup>();
	if (!WorldSetup) return;
	WorldSetup->Build(S, nullptr, ECaySimWorldSource::Flat);

	FSmvTrack Tr;
	Tr.Name = TEXT("Menu");
	Tr.Lat = Lat0; Tr.Lon = Lon0; Tr.GroundMsl = GroundMsl;
	// altın saat (gün batımından ~75 dk önce): sıcak, alçak güneş; araçlar ve harita belirgin
	Tr.TimeOfDay = TEXT("golden");
	Tr.World.Seed = 7;
	Tr.World.SizeM = 4000.0;
	Tr.World.MountainCount = 6;
	Tr.World.MountainHeight = 260.0;
	Tr.World.TreeDensity = 0.35;
	Tr.World.BuildingCount = 30;
	Tr.World.bRunway = true;
	Tr.World.RunwayHeading = 70.0;
	Tr.World.Water = 0.12;
	Tr.World.Fog = 0.03;   // menüde pus en az (UDS Fog 0,2): arka plan sahnesi net görünsün
	// Fab haritası (ini MenuMap = anahtar; boş → orman / rafineri / askeri üs önceliği) — yoksa prosedürel arazi
	{
		FString Want;
		if (GConfig) GConfig->GetString(GSmvMenuSection, TEXT("MenuMap"), Want, GGameIni);
		LoadMenuMap(Want);
	}
	// Ultra Dynamic Sky (varsa; alacakaranlık)
	{
		TArray<AActor*> Sky;
		if (FSmvFabContent::SpawnSky(W, Tr, Sky)) { WorldSetup->DisableBuiltinSky(); for (AActor* A : Sky) SkyActors.Add(A); }
	}

	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Flyer = SpawnFlyer(TEXT("plane"), FLinearColor(0.92f, 0.94f, 0.97f));
	if (ASmvDroneActor* Q = SpawnFlyer(TEXT("multirotor"), FLinearColor(0.15f, 0.16f, 0.18f))) Extras.Add(Q);
	if (ASmvDroneActor* V = SpawnFlyer(TEXT("vtol"), FLinearColor(0.85f, 0.25f, 0.2f))) Extras.Add(V);
	Cam = W->SpawnActor<ACameraActor>(FVector::ZeroVector, FRotator::ZeroRotator, P);
	if (Cam && Cam->GetCameraComponent())
	{
		UCameraComponent* Cc = Cam->GetCameraComponent();
		Cc->SetFieldOfView(62.f);
		Cc->bConstrainAspectRatio = false;
		// sinematik son işlem: alan derinliği (odak = takip edilen araç; Tick'te), bloom, vinyet, hafif film greni ve sıcak-soğuk renk ayarı
		FPostProcessSettings& Pp = Cc->PostProcessSettings;
		// hafif alan derinliği (f/5,6: uzak plan yumuşar, araç ve yakın arazi net — f/2,2 sahneyi bulanıklaştırıyordu)
		Pp.bOverride_DepthOfFieldFstop = true; Pp.DepthOfFieldFstop = 5.6f;
		Pp.bOverride_DepthOfFieldSensorWidth = true; Pp.DepthOfFieldSensorWidth = 36.f;
		Pp.bOverride_DepthOfFieldFocalDistance = true; Pp.DepthOfFieldFocalDistance = 2000.f;
		Pp.bOverride_BloomIntensity = true; Pp.BloomIntensity = 0.8f;
		Pp.bOverride_VignetteIntensity = true; Pp.VignetteIntensity = 0.3f;
		Pp.bOverride_FilmGrainIntensity = true; Pp.FilmGrainIntensity = 0.04f;
		// parlak ama okunaklı: pozlama biraz yukarı (kartların arkasındaki koyu degrade yalnız sol yarıda — SSmvBackdrop Cinematic)
		Pp.bOverride_AutoExposureBias = true; Pp.AutoExposureBias = 0.6f;
		Pp.bOverride_ColorContrast = true; Pp.ColorContrast = FVector4(1.f, 1.f, 1.f, 1.06f);
		Pp.bOverride_ColorSaturation = true; Pp.ColorSaturation = FVector4(1.f, 1.f, 1.f, 1.08f);
		Pp.bOverride_ColorGainShadows = true; Pp.ColorGainShadows = FVector4(0.92f, 0.98f, 1.08f, 1.f);
		Pp.bOverride_ColorGainHighlights = true; Pp.ColorGainHighlights = FVector4(1.06f, 1.0f, 0.92f, 1.f);
		Pp.bOverride_SceneFringeIntensity = true; Pp.SceneFringeIntensity = 0.6f;
		// hız çizgileri (Fab Speed Effects son işlem malzemesi; MenuSpeedLines ağırlığı) — duman testinde arayüzün üstünde fazla yoğun
		// göründüğü için menüde varsayılan kapalı (0); uçuş kamerasında hıza bağlı kullanılır
		const float SpeedW = SmvCfg(TEXT("MenuSpeedLines"), 0.f);
		if (SpeedW > 0.f && FPackageName::DoesPackageExist(TEXT("/Game/SpeedEffects/Assets/Materials/M_SpeedEffects_SpeedLines")))
		{
			if (UMaterialInterface* Sl = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/SpeedEffects/Assets/Materials/M_SpeedEffects_SpeedLines.M_SpeedEffects_SpeedLines"), nullptr, LOAD_NoWarn | LOAD_Quiet))
				Pp.WeightedBlendables.Array.Add(FWeightedBlendable(SpeedW, Sl));
		}
		Cc->PostProcessBlendWeight = 1.f;
	}
	// Niagara ışık zerreleri: derlemede motor şablonundan kopyalanan sistem (Scripts/Python/simumav_varliklar.py niagara_sablonu)
	// Motorla gelen tek uygun şablon "FountainLightweight" (fıskiye): kamera önünde iri beyaz küreler olarak göründü (duman testi) →
	// varsayılan kapalı; [/Script/Simumav.SmvMenuScene] MenuNiagara=True ile açılır. Uygun varlık için Epic "Niagara Examples Pack" önerilir.
	// Fab paketlerinden (Niagara Examples Pack / Ultra Dynamic Sky / Ruined Construction ...) seçilen toz-zerre sistemi NS_SmvDust varsa varsayılan açık
	bool bNiagara = false, bDust = true;
	if (GConfig) GConfig->GetBool(GSmvMenuSection, TEXT("MenuNiagara"), bNiagara, GGameIni);
	if (GConfig) GConfig->GetBool(GSmvMenuSection, TEXT("MenuDust"), bDust, GGameIni);
	UNiagaraSystem* DustFx = bDust && FPackageName::DoesPackageExist(TEXT("/Game/Simumav/FX/NS_SmvDust")) ? LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/Simumav/FX/NS_SmvDust.NS_SmvDust"), nullptr, LOAD_NoWarn | LOAD_Quiet) : nullptr;
	if (Cam && (bNiagara || DustFx))
	{
		if (UNiagaraSystem* Fx = DustFx ? DustFx : LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/Simumav/FX/NS_SmvMotes.NS_SmvMotes"), nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			Motes = NewObject<UNiagaraComponent>(Cam);
			Motes->SetAsset(Fx);
			Motes->SetupAttachment(Cam->GetRootComponent());
			Motes->SetRelativeLocation(FVector(900.0, 280.0, -40.0));
			Motes->RegisterComponent();
			Motes->Activate(true);
			UE_LOG(LogSimumav, Log, TEXT("Menü sahnesi: Niagara zerreleri (%s)"), *Fx->GetPathName());
		}
		else UE_LOG(LogSimumav, Log, TEXT("Menü sahnesi: Niagara varlığı yok (NS_SmvMotes) — Slate zerreleri kullanılır"));
	}
	bLive = Flyer && Cam;
	UE_LOG(LogSimumav, Log, TEXT("Menü 3B sahnesi kuruldu (%s)"), bLive ? TEXT("canlı") : TEXT("eksik"));
}

void ASmvMenuScene::EndPlay(const EEndPlayReason::Type Reason)
{
	ApplyShowcaseClear(false);   // uçuşa geçerken sis / bulut / efektler eski hâline
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		PC->bAutoManageActiveCameraTarget = true;
		if (Cam && PC->GetViewTarget() == Cam) PC->SetViewTarget(PC->GetPawn() ? (AActor*)PC->GetPawn() : (AActor*)PC);
	}
	if (Flyer) Flyer->Destroy();
	for (TObjectPtr<ASmvDroneActor>& X : Extras) if (X) X->Destroy();
	Extras.Reset();
	for (TObjectPtr<AActor>& X : SkyActors) if (X) X->Destroy();
	SkyActors.Reset();
	for (TObjectPtr<AActor>& X : SceneryActors) if (X) X->Destroy();
	SceneryActors.Reset(); FogBanks.Reset();
	DropMenuLevels();
	if (Preview) Preview->Destroy();
	if (PilotPrev) PilotPrev->Destroy();
	if (Showcase) Showcase->Destroy();
	if (ShowPilot) ShowPilot->Destroy();
	Showcase = nullptr; ShowPilot = nullptr;
	if (SpinSubject) SpinSubject->Destroy();
	if (SpinFig) SpinFig->Destroy();
	SpinSubject = nullptr; SpinFig = nullptr;
	if (WorldPrev) WorldPrev->Destroy();
	if (Cam) Cam->Destroy();
	if (Terrain) Terrain->Destroy();
	if (MissionWorld) { MissionWorld->Destroy(); MissionWorld = nullptr; }
	if (MissionDrone) { MissionDrone->Destroy(); MissionDrone = nullptr; }
	bMission = false; bMissionPlay = false;
	if (WorldSetup) WorldSetup->Destroy();
	Flyer = nullptr; Preview = nullptr; Cam = nullptr; Terrain = nullptr; WorldSetup = nullptr;
	Super::EndPlay(Reason);
}

FVector ASmvMenuScene::EnuToWorld(double E, double N, double Agl) const
{
	if (!WorldSetup) return FVector(E * 100.0, -N * 100.0, Agl * 100.0);
	const double Lat = Lat0 + FMath::RadiansToDegrees(N / 6378137.0);
	const double Lon = Lon0 + FMath::RadiansToDegrees(E / (6378137.0 * FMath::Cos(FMath::DegreesToRadians(Lat0))));
	if (bFabMap)
	{
		const double FLa = Lat0 + FMath::RadiansToDegrees((N + FabStartN) / 6378137.0);
		const double FLo = Lon0 + FMath::RadiansToDegrees((E + FabStartE) / (6378137.0 * FMath::Cos(FMath::DegreesToRadians(Lat0))));
		FVector P = WorldSetup->GeoToUnreal(FLo, FLa, WorldSetup->MslToWorldHeight(FLa, FLo, GroundMsl));
		P.Z = const_cast<ASmvMenuScene*>(this)->GroundAt(E, N) + Agl * 100.0;
		return P;
	}
	const double Gh = Terrain ? Terrain->HeightAt(E, N) : 0.0;
	return WorldSetup->GeoToUnreal(Lon, Lat, WorldSetup->MslToWorldHeight(Lat, Lon, GroundMsl + Gh + Agl));
}

void ASmvMenuScene::SetPreview(const FString& Type, const FString& ModelKey, const FString& ColorHex)
{
	if (!FSmvUiPrefs::Get().bPreview3d) return;
	if (Preview && Type == PrevType && ModelKey == PrevModel && ColorHex == PrevColor) return;
	UWorld* W = GetWorld();
	if (!W) return;
	if (Preview) { Preview->Destroy(); Preview = nullptr; }
	if (PilotPrev) { PilotPrev->Destroy(); PilotPrev = nullptr; PilotKey.Empty(); }
	if (WorldPrev) { WorldPrev->Destroy(); WorldPrev = nullptr; }
	PrevType = Type; PrevModel = ModelKey; PrevColor = ColorHex;
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Preview = W->SpawnActor<ASmvDroneActor>(ASmvDroneActor::StaticClass(), FTransform(GSmvPreviewAt), P);
	if (!Preview) return;
	FCaySimSession S;
	S.Type = SmvMenuCaysimType(Type);
	S.Model.Orient.Forward = TEXT("+x");
	S.Model.Orient.Up = TEXT("+y");
	S.Model.Orient.Scale = 1.f;
	Preview->Setup(S, nullptr);
	Preview->ConfigureVisual(Type, ModelKey, SmvHexColor(ColorHex));
	Preview->SetActorTickEnabled(false);
	Preview->SetActorLocation(GSmvPreviewAt);
	if (USmvMotorSynth* M = Preview->GetMotor()) M->Stop();
	PreviewDistCm = FMath::Clamp(Preview->GetVisualRadiusM() * 100.f / 0.2493f * 1.2f, 120.f, 4000.f);
	Capture->ShowOnlyActors.Reset();
	Capture->ShowOnlyActors.Add(this);   // stüdyo duvarı + zemini
	PlaceStudio(0.f);
	Capture->ShowOnlyActors.Add(Preview);
	ApplyPreviewChannels();
	if (USmvGameInstance* GI = USmvGameInstance::GetSmv(this))
	{
		TWeakObjectPtr<ASmvDroneActor> WP(Preview);
		TWeakObjectPtr<ASmvMenuScene> WS(this);
		if (GI->GetData() && !ModelKey.IsEmpty())
		{
			GI->GetData()->FetchModelGlb(ModelKey, [WP, WS](bool bOk, const TArray<uint8>& Bytes)
			{
				if (bOk && WP.IsValid()) WP->LoadGlbBytes(Bytes);
				if (WS.IsValid()) WS->ApplyPreviewChannels();
			});
		}
	}
}

void ASmvMenuScene::ApplyPreviewChannels()
{
	for (AActor* A : { (AActor*)Preview.Get(), (AActor*)PilotPrev.Get() })
	{
		if (!A) continue;
		TInlineComponentArray<UPrimitiveComponent*> Prims(A);
		for (UPrimitiveComponent* P : Prims) if (P) { P->SetLightingChannels(false, true, false); P->SetCastShadow(false); }
	}
}

void ASmvMenuScene::SetPilotPreview(const FSmvAvatar& A, bool bFace)
{
	if (!FSmvUiPrefs::Get().bPreview3d) return;
	bFaceZoom = bFace;
	UWorld* W = GetWorld();
	if (!W) return;
	const FString Key = SmvJson::ToString(A.ToJson());
	if (PilotPrev && Key == PilotKey) return;
	if (Preview) { Preview->Destroy(); Preview = nullptr; PrevType.Empty(); }
	if (WorldPrev) { WorldPrev->Destroy(); WorldPrev = nullptr; }
	if (!PilotPrev)
	{
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		PilotPrev = W->SpawnActor<ASmvPilotFigure>(ASmvPilotFigure::StaticClass(), FTransform(GSmvPreviewAt), P);
	}
	if (!PilotPrev) return;
	PilotKey = Key;
	// mürettebat önizlemesi: serbest duruş (RC kumanda gerçekçi görünmüyordu — kullanıcı, 2026-10-28; ayar CrewRcPose)
	bool bRc = false;
	if (GConfig) GConfig->GetBool(GSmvMenuSection, TEXT("CrewRcPose"), bRc, GGameIni);
	PilotPrev->Build(A, bRc);
	Capture->ShowOnlyActors.Reset();
	Capture->ShowOnlyActors.Add(this);   // stüdyo duvarı + zemini
	PlaceStudio(0.f);
	Capture->ShowOnlyActors.Add(PilotPrev);
	ApplyPreviewChannels();
}

void ASmvMenuScene::AddPreviewYaw(float Deg)
{
	PreviewYaw = FMath::Fmod(PreviewYaw + Deg + 360.f, 360.f);
	ManualUntil = FPlatformTime::Seconds() + 4.0;
}

void ASmvMenuScene::SetPreviewActive(bool bOn)
{
	bPreviewOn = bOn && FSmvUiPrefs::Get().bPreview3d;
	Capture->bCaptureEveryFrame = bPreviewOn && (Preview != nullptr || PilotPrev != nullptr || WorldPrev != nullptr);
}

void ASmvMenuScene::UpdateCamera(float Dt, const FVector& D, const FVector& Fwd)
{
	// sinematik kamera döngüsü: takip 18 sn → yan 12 sn → yörünge 14 sn → yerden 10 sn
	const double T = SceneT;
	const double Period = LoopSeconds * 0.5;   // 42 sn (84 sn video döngüsünün yarısı)
	const double U = FMath::Fmod(T, Period);
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Fwd).GetSafeNormal();
	FVector Want;
	FVector Look = D;
	if (U < 14.0)
	{
		Want = D - Fwd * 1400.0 + FVector(0, 0, 380.0) + Right * (220.0 * FMath::Sin(T * UE_DOUBLE_TWO_PI * 4.0 / LoopSeconds));
	}
	else if (U < 23.5)
	{
		const double K = (U - 14.0) / 9.5;
		Want = D + Right * 1700.0 - Fwd * FMath::Lerp(900.0, -900.0, K) + FVector(0, 0, 150.0);
	}
	else if (U < 34.5)
	{
		const double A = (U - 23.5) * 0.45;
		Want = D + FVector(FMath::Cos(A) * 1800.0, FMath::Sin(A) * 1800.0, 520.0);
	}
	else
	{
		// yerden izleme: sabit nokta (kalkış alanı yanı)
		Want = EnuToWorld(120.0, -160.0, 1.7);
		Look = D;
	}
	// kamera arazinin içine girmesin (takip açısı tepelerin içinden geçip arazinin alt yüzünü gösteriyordu — "ters tepe" karesi)
	if (UWorld* W = GetWorld())
	{
		FHitResult H;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(SmvMenuCam), false);
		if (Flyer) Q.AddIgnoredActor(Flyer);
		for (const TObjectPtr<ASmvDroneActor>& X : Extras) Q.AddIgnoredActor(X);
		if (W->LineTraceSingleByChannel(H, Want + FVector(0, 0, 200000.0), Want - FVector(0, 0, 200000.0), ECC_Visibility, Q))
			Want.Z = FMath::Max(Want.Z, H.ImpactPoint.Z + 1500.0);
	}
	if (!bCamInit || FVector::Dist(Want, CamPos) > 60000.0) { CamPos = Want; bCamInit = true; }
	const double K = 1.0 - FMath::Exp(-FMath::Clamp((double)Dt, 0.0, 0.1) * 2.2);   // kare hızından bağımsız
	CamPos = FMath::Lerp(CamPos, Want, K);
	// konu kadrajın sağ yarısında (sol yarı kartlarla kaplı): bakış sola kaydırılır, hafif yukarı
	FRotator WantRot = (Look - CamPos).Rotation();
	WantRot.Yaw -= 14.f;
	WantRot.Pitch += 3.f;
	CamRot = FQuat::Slerp(CamRot.Quaternion(), WantRot.Quaternion(), (float)(1.0 - FMath::Exp(-FMath::Clamp((double)Dt, 0.0, 0.1) * 5.0))).Rotator();
	CamRot.Roll = 0.f;
	Cam->SetActorLocationAndRotation(CamPos, CamRot);
}

void ASmvMenuScene::Tick(float Dt)
{
	Super::Tick(Dt);
	// sahne saati kare süresinden birikir (duvar saati + Dt'li kamera yumuşatması takılmalarda sarsıntı veriyordu)
	SceneT += FMath::Clamp((double)Dt, 0.0, 0.1);
	// görev düzenleyici kipi: menü kamerası / araçları yerine görev dünyası yörüngesi (SmvMenuMission.cpp)
	if (bMission) { TickMission(Dt); TickSpin(); return; }
	const double T = SceneT;
	if (bLive && Flyer && Cam)
	{
		// Gerono lemniskatı (web flightcore.js flightPath): rx 900, ry 520 m, periyot 84 sn, 46 m AGL
		const double Wv = UE_DOUBLE_TWO_PI / 84.0;
		auto Path = [Wv](double t, double& E, double& N) { const double A = t * Wv; E = 900.0 * FMath::Cos(A); N = 520.0 * FMath::Sin(A) * FMath::Cos(A); };
		double E0, N0, E1, N1, E2, N2;
		Path(T, E0, N0); Path(T + 0.5, E1, N1); Path(T + 1.0, E2, N2);
		const double Agl = 46.0 + 7.0 * FMath::Sin(T * UE_DOUBLE_TWO_PI * 5.0 / LoopSeconds);
		FVector P0 = EnuToWorld(E0, N0, Agl), P1 = EnuToWorld(E1, N1, Agl), P2 = EnuToWorld(E2, N2, Agl);
		// Fab haritasında yer yüksekliği 10 m hücrelerde iz düşümüyle (ağaç tepeleri dahil) bulunur: hücre geçişlerinde dron ve kamera
		// basamak basamak zıplıyordu ("arka plan uçağı titriyor, ekran sallanıyor"). İrtifa zamanla yumuşatılır; yön yatay düzlemde.
		{
			if (!bFlyZ) { FlyZ = P0.Z; bFlyZ = true; }
			const double MinZ = P0.Z - (Agl - 18.0) * 100.0;   // en az 18 m yer açıklığı
			const double Prev = FlyZ;
			FlyZ = FMath::Lerp(FlyZ, P0.Z, 1.0 - FMath::Exp(-FMath::Clamp((double)Dt, 0.0, 0.1) * 0.9));
			if (FlyZ < MinZ) FlyZ = FMath::Lerp(FlyZ, MinZ, 1.0 - FMath::Exp(-FMath::Clamp((double)Dt, 0.0, 0.1) * 4.0));
			FlyVz = FMath::Lerp(FlyVz, (FlyZ - Prev) / FMath::Max(1e-3, (double)Dt), 1.0 - FMath::Exp(-(double)Dt * 3.0));
			P0.Z = FlyZ; P1.Z = FlyZ; P2.Z = FlyZ;
		}
		const FVector Fwd = (P1 - P0).GetSafeNormal();
		const FVector Fwd2 = (P2 - P1).GetSafeNormal();
		const float Turn = (float)FMath::RadiansToDegrees(FMath::Atan2(FVector::CrossProduct(Fwd, Fwd2).Z, FVector::DotProduct(Fwd, Fwd2)));
		FRotator R = Fwd.Rotation();
		const double Hs = FVector::Dist(P1, P0) / 0.5;
		R.Pitch = FMath::Clamp((float)FMath::RadiansToDegrees(FMath::Atan2(FlyVz, FMath::Max(1.0, Hs))), -12.f, 12.f);
		R.Roll = FMath::Clamp(Turn * 9.f, -40.f, 40.f);
		Flyer->SetActorLocationAndRotation(P0, R);
		if (Station == EStation::Menu) UpdateCamera(Dt, P0, Fwd);
		else UpdateStationCamera(Dt);
		// kopter: kalkış alanı yakınında yavaş yörünge (18 m, 25 sn); VTOL: geniş tur (1,4 km, 120 sn) yüksekte
		if (Extras.Num() > 0 && Extras[0])
		{
			const double A = T * UE_DOUBLE_TWO_PI / (LoopSeconds / 4.0);   // 21 sn (video döngüsü)
			const FVector Q0 = EnuToWorld(140.0 + 18.0 * FMath::Cos(A), -120.0 + 18.0 * FMath::Sin(A), 14.0 + 3.0 * FMath::Sin(T * UE_DOUBLE_TWO_PI * 8.0 / LoopSeconds));
			Extras[0]->SetActorLocationAndRotation(Q0, FRotator(-6.0 * FMath::Cos(T * UE_DOUBLE_TWO_PI * 7.0 / LoopSeconds), FMath::RadiansToDegrees(A) + 90.0, 4.0 * FMath::Sin(A)));
		}
		if (Extras.Num() > 1 && Extras[1])
		{
			const double A = T * UE_DOUBLE_TWO_PI / LoopSeconds;
			const FVector V0 = EnuToWorld(1400.0 * FMath::Cos(A), 900.0 * FMath::Sin(A), 140.0), V1 = EnuToWorld(1400.0 * FMath::Cos(A + 0.01), 900.0 * FMath::Sin(A + 0.01), 140.0);
			FRotator Rv = (V1 - V0).Rotation(); Rv.Roll = -18.0;
			Extras[1]->SetActorLocationAndRotation(V0, Rv);
		}
		if (Station == EStation::Menu && Cam && Cam->GetCameraComponent())
		{
			Cam->GetCameraComponent()->PostProcessSettings.DepthOfFieldFocalDistance = (float)FVector::Dist(CamPos, P0);
			Cam->GetCameraComponent()->PostProcessSettings.DepthOfFieldFstop = 5.6f;
			// hareket bulanıklığı yok (menü videosu kaydında hızlı kamera geçişleri kareleri bulandırıyordu)
			Cam->GetCameraComponent()->PostProcessSettings.bOverride_MotionBlurAmount = true;
			Cam->GetCameraComponent()->PostProcessSettings.MotionBlurAmount = 0.f;
		}
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
		{
			if (PC->GetViewTarget() != Cam)
			{
				PC->bAutoManageActiveCameraTarget = false;
				PC->SetViewTarget(Cam);
			}
		}
	}
	TickGate();
	TickSpin();
	TickScenery();
	if (bSceneryDone) ClearShowcaseGrass();
	SettleShowcase();
	if (bCardOn && CardCap && bLive)
	{
		// karşılaşma kartı: harita içinde alçak, yavaş yörünge (vitrin aktörleri görünmez)
		const double A = (FPlatformTime::Seconds() - T0) * 0.07;
		const FVector At = EnuToWorld(FMath::Cos(A) * 240.0, FMath::Sin(A) * 240.0, 38.0);
		const FVector To = EnuToWorld(FMath::Cos(A + 0.9) * 60.0, FMath::Sin(A + 0.9) * 60.0, 6.0);
		CardCap->SetWorldLocationAndRotation(At, (To - At).Rotation());
	}
	if (WorldPrev && WorldSetup && !Preview && !PilotPrev)
	{
		// dünya önizlemesi: merkez çevresinde yavaş yörünge (eğik kuş bakışı)
		if (FPlatformTime::Seconds() > ManualUntil) PreviewYaw = FMath::Fmod(PreviewYaw + Dt * 6.f, 360.f);
		const FVector Ctr = WorldSetup->GeoToUnreal(WpLon, WpLat, WorldSetup->MslToWorldHeight(WpLat, WpLon, GroundMsl));
		const double R = WpSize * 100.0 * 0.62, A = FMath::DegreesToRadians(PreviewYaw);
		const FVector At = Ctr + FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, WpSize * 100.0 * 0.32);
		Capture->FOVAngle = 55.f;
		Capture->SetWorldLocationAndRotation(At, (Ctr - At).Rotation());
		if (SkyCard)
		{
			// gök kartı: arazinin arkasında, kameraya dönük
			const FVector Away = (Ctr - At).GetSafeNormal2D();
			SkyCard->SetWorldLocationAndRotation(Ctr + Away * R * 2.2 + FVector(0, 0, R * 0.6), FRotator(0.0, Away.Rotation().Yaw, 0.0));
			SkyCard->SetWorldScale3D(FVector(1.0, R * 7.0 / 100.0, R * 3.0 / 100.0));
		}
	}
	const bool bWorldPrevOn = WorldPrev && !Preview && !PilotPrev;
	if (PrevSun && PrevSun->IsVisible() != bWorldPrevOn) PrevSun->SetVisibility(bWorldPrevOn);
	if (SkyCard && SkyCard->IsVisible() != bWorldPrevOn) SkyCard->SetVisibility(bWorldPrevOn);
	if (PilotPrev)
	{
		// sürekli dönme yerine öne bakarak hafif salınım (±30°): yüz, vizör ve tulum önü görünsün
		if (FPlatformTime::Seconds() > ManualUntil)
		{
			const float Want = 30.f * FMath::Sin((float)(FPlatformTime::Seconds() - T0) * 0.45f);
			PreviewYaw = FRotator::NormalizeAxis(PreviewYaw + FMath::Clamp(FRotator::NormalizeAxis(Want - PreviewYaw), -Dt * 40.f, Dt * 40.f));
		}
		FaceK = FMath::FInterpTo(FaceK, bFaceZoom ? 1.f : 0.f, Dt, 4.f);
		PilotPrev->SetActorLocationAndRotation(GSmvPreviewAt, FRotator(0.f, 180.f + PreviewYaw, 0.f));
		// tam boy: tüm figür (yatay FOV 28° → dikey ~18°) için ~6,4 m; yüz: 0,95 m
		const FVector Target = GSmvPreviewAt + FVector(0, 0, FMath::Lerp(92.f, PilotPrev->HeadHeightCm() - 4.f, FaceK));
		const float Dist = FMath::Lerp(640.f, 95.f, FaceK);
		StudioFloor->SetWorldLocation(GSmvPreviewAt + FVector(0.0, 0.0, -1.0));
		const FVector CamAt = Target + FVector(-Dist, 0.0, FMath::Lerp(20.f, 4.f, FaceK));
		Capture->FOVAngle = 28.f;
		Capture->SetWorldLocationAndRotation(CamAt, (Target - CamAt).Rotation());
		// tam boy figür: ışıklar 4–5 m uzakta (yakın ışık yüzü ve zemini patlatıyordu)
		// tam ters kare telafisi tam boy figürü aşırı pozluyordu (duman testi): %35
		PlaceLights(GSmvPreviewAt + FVector(0, 0, 110), GSmvPreviewAt + FVector(-420, 300, 380), GSmvPreviewAt + FVector(380, -320, 340), 0.35f);
		const double Now = FPlatformTime::Seconds();
		if (Now - LastChannelFix > 1.0) { LastChannelFix = Now; ApplyPreviewChannels(); }
	}
	if (Preview)
	{
		if (FPlatformTime::Seconds() > ManualUntil) PreviewYaw = FMath::Fmod(PreviewYaw + Dt * 24.f, 360.f);
		// "hangara indi": yukarıdan yumuşak iniş (son anda hafif sekme)
		const double Lk = FMath::Clamp((FPlatformTime::Seconds() - LandAt) / 1.6, 0.0, 1.0);
		const double Drop = Lk < 1.0 ? FMath::Pow(1.0 - Lk, 3.0) * PreviewDistCm * 0.9 : 0.0;
		Preview->SetActorLocationAndRotation(GSmvPreviewAt + FVector(0, 0, Drop), FRotator(0.f, PreviewYaw + (float)((1.0 - Lk) * 180.0), 0.f));
		// kadraja sığdır (GLB gövdesi yüklenince yarıçap değişir): yatay FOV 28° → mesafe ≈ yarıçap / tan 14° × 1,2
		{
			const float Want = FMath::Clamp(Preview->GetVisualRadiusM() * 100.f / 0.2493f * 1.2f, 120.f, 4000.f);
			PreviewDistCm = FMath::FInterpTo(PreviewDistCm, Want, Dt, 4.f);
		}
		const FVector CamAt = GSmvPreviewAt + FVector(-PreviewDistCm, 0.0, PreviewDistCm * 0.32);
		StudioFloor->SetWorldLocation(GSmvPreviewAt + FVector(0.0, 0.0, -PreviewDistCm * 0.16));
		Capture->SetWorldLocationAndRotation(CamAt, (GSmvPreviewAt - CamAt).Rotation());
		PlaceLights(GSmvPreviewAt, GSmvPreviewAt + FVector(-PreviewDistCm * 0.6, PreviewDistCm * 0.7, PreviewDistCm * 0.8), GSmvPreviewAt + FVector(PreviewDistCm * 0.8, -PreviewDistCm * 0.5, PreviewDistCm * 0.4));
		const double Now = FPlatformTime::Seconds();
		if (Now - LastChannelFix > 1.0) { LastChannelFix = Now; ApplyPreviewChannels(); }
	}
}

// ---------------------------------------------------------------------------
// dönen küçük görüntüler: konu ayrı noktada (GSmvSpinAt), yalnız sahne yakalamasında görünür, 2. ışık kanalındaki kendi ışıklarıyla
// 24 açıdan bir kez çekilir. Her açı için FinalColorLDR (renk) + SceneColorHDR (alfa = 1 − örtme) yakalanır, sonraki karede okunur
// (yakalama ertelenmiş olabilir) ve UTexture2D'ye yazılır. Eski tek karelik AvatarSnap stüdyo duvarına bağlıydı, menüde duvar
// yokken / konu aynı karede gizlenince siyah kalıyordu (pilot rozeti siyah kare — kullanıcı, 2026-10-28).
// ---------------------------------------------------------------------------

namespace
{
	const FVector GSmvSpinAt(0.0, 300000.0, 400000.0);

	void SmvSpinPrims(AActor* A)
	{
		if (!A) return;
		TArray<AActor*> Acts;
		A->GetAttachedActors(Acts, true, true);
		Acts.Add(A);
		for (AActor* X : Acts)
		{
			if (!X) continue;
			TInlineComponentArray<UPrimitiveComponent*> Prims(X);
			for (UPrimitiveComponent* Pc : Prims)
			{
				if (!Pc) continue;
				Pc->SetLightingChannels(false, false, true);
				Pc->SetCastShadow(false);
				Pc->SetVisibleInSceneCaptureOnly(true);
			}
		}
	}

	FBox SmvSpinBounds(AActor* A)
	{
		FBox Bx(ForceInit);
		if (!A) return Bx;
		TArray<AActor*> Acts;
		A->GetAttachedActors(Acts, true, true);
		Acts.Add(A);
		for (AActor* Ac : Acts)
		{
			if (!Ac) continue;
			TInlineComponentArray<UMeshComponent*> Ms(Ac);
			for (UMeshComponent* Mc : Ms) if (Mc && Mc->IsRegistered() && Mc->IsVisible() && !Mc->bHiddenInGame) Bx += Mc->Bounds.GetBox();
		}
		return Bx;
	}

	/** Renk (LDR, gama kodlu) + alfa (HDR ters örtme) → saydam doku. Alfa yorumu sınanır: hepsi 0 / hepsi 1 çıkarsa ters, o da
	 *  olmazsa opak (görüntü hiç kaybolmasın). */
	UTexture2D* SmvSpinTexture(UTextureRenderTarget2D* L, UTextureRenderTarget2D* H, UObject* Outer)
	{
		if (!L || !H) return nullptr;
		FTextureRenderTargetResource* RL = L->GameThread_GetRenderTargetResource();
		FTextureRenderTargetResource* RH = H->GameThread_GetRenderTargetResource();
		if (!RL || !RH) return nullptr;
		TArray<FColor> C;
		TArray<FLinearColor> A;
		if (!RL->ReadPixels(C) || !RH->ReadLinearColorPixels(A) || C.Num() != A.Num() || C.Num() == 0) return nullptr;
		const int32 N = C.Num();
		auto Count = [&A, N](bool bInv) { int32 K = 0; for (const FLinearColor& X : A) if ((bInv ? 1.f - X.A : X.A) > 0.5f) ++K; return K; };
		int32 Mode = 0;   // 0: 1 − a, 1: a, 2: opak
		{
			const int32 K0 = Count(true);
			if (K0 == 0 || K0 == N) { const int32 K1 = Count(false); Mode = (K1 == 0 || K1 == N) ? 2 : 1; }
		}
		for (int32 i = 0; i < N; ++i)
		{
			const float Al = Mode == 2 ? 1.f : FMath::Clamp(Mode == 0 ? 1.f - A[i].A : A[i].A, 0.f, 1.f);
			FColor& P = C[i];
			if (Mode != 2 && Al > 0.04f && Al < 0.98f)
			{
				// kenar: siyah fonla karışmış rengi ön-çarpımdan çöz (koyu hale olmasın)
				P.R = (uint8)FMath::Min(255.f, P.R / Al); P.G = (uint8)FMath::Min(255.f, P.G / Al); P.B = (uint8)FMath::Min(255.f, P.B / Al);
			}
			P.A = (uint8)FMath::RoundToInt(Al * 255.f);
		}
		const int32 W = L->SizeX, Ht = L->SizeY;
		UTexture2D* T = UTexture2D::CreateTransient(W, Ht, PF_B8G8R8A8, NAME_None);
		if (!T || !T->GetPlatformData() || T->GetPlatformData()->Mips.Num() == 0) return nullptr;
		T->SRGB = true;
		T->Filter = TF_Bilinear;
		T->LODGroup = TEXTUREGROUP_UI;
		T->NeverStream = true;
		FTexture2DMipMap& Mip = T->GetPlatformData()->Mips[0];
		void* Dst = Mip.BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(Dst, C.GetData(), (SIZE_T)N * sizeof(FColor));
		Mip.BulkData.Unlock();
		T->UpdateResource();
		return T;
	}
}

FString ASmvMenuScene::RequestDroneSpin(const FString& Type, const FString& ModelKey, const FString& ColorHex)
{
	const FString Key = TEXT("d|") + Type + TEXT("|") + ModelKey + TEXT("|") + ColorHex;
	if (!Spins.Contains(Key) && Spins.Num() < 48)
	{
		Spins.Add(Key).Want = SpinFrameCount;
		FSpinJob J; J.Key = Key; J.Type = Type; J.Model = ModelKey; J.Color = ColorHex;
		SpinQueue.Add(J);
	}
	return Key;
}

FString ASmvMenuScene::RequestAvatarSpin(const FSmvAvatar& A)
{
	const FString Key = TEXT("a|") + SmvJson::ToString(A.ToJson());
	if (!Spins.Contains(Key) && Spins.Num() < 48)
	{
		Spins.Add(Key).Want = SpinFrameCount;
		FSpinJob J; J.Key = Key; J.Avatar = A; J.bAvatar = true;
		// pilot rozetleri / listeler önce (küçük, hemen görünür)
		SpinQueue.Insert(J, 0);
	}
	return Key;
}

void ASmvMenuScene::EnsureSpinLights()
{
	if (SpinLights.Num()) return;
	// anahtar (sıcak, sol-üst), kenar (soğuk, sağ-arka), dolgu (kamera yanı, zayıf): yalnız 2. ışık kanalı
	const FLinearColor Cols[] = { FLinearColor(1.f, 0.96f, 0.9f), FLinearColor(0.45f, 0.78f, 1.f), FLinearColor(0.9f, 0.93f, 1.f) };
	for (int32 i = 0; i < 3; ++i)
	{
		UPointLightComponent* Lt = NewObject<UPointLightComponent>(this);
		Lt->SetupAttachment(Root);
		Lt->SetIntensityUnits(ELightUnits::Candelas);
		Lt->SetCastShadows(false);
		Lt->SetLightingChannels(false, false, true);
		Lt->SetLightColor(Cols[i]);
		Lt->RegisterComponent();
		SpinLights.Add(Lt);
	}
}

void ASmvMenuScene::EndSpinJob()
{
	if (SpinSubject && SpinSubject != SpinFig) SpinSubject->Destroy();
	if (SpinFig) SpinFig->SetActorHiddenInGame(true);
	SpinSubject = nullptr;
	bSpinRead = false;
	bSpinFramed = false;
}

void ASmvMenuScene::TickSpin()
{
	UWorld* W = GetWorld();
	if (!W || !SnapCap) return;
	if (SpinSubject)
	{
		++SpinTick;
		FSmvSpinSet* Set = Spins.Find(SpinJob.Key);
		if (!Set) { EndSpinJob(); return; }
		if (bSpinRead)
		{
			bSpinRead = false;
			if (UTexture2D* Tx = SmvSpinTexture(SpinRtL, SpinRtH, this)) Set->Frames.Add(Tx);
			else Set->Want = FMath::Max(0, Set->Want - 1);   // okunamayan açı atlanır
			if (Set->Frames.Num() >= Set->Want) { EndSpinJob(); return; }
		}
		// hazır mı: dron GLB gövdesi yüklenip 3 kare geçti (modelsiz 4. kare; en çok 240 kare) · pilot 20 kare (MetaHuman ağları)
		const bool bReady = SpinJob.bAvatar ? SpinTick >= 20
			: ((bSpinHasModel ? (SpinGlbTick >= 0 && SpinTick >= SpinGlbTick + 3) : SpinTick >= 4) || SpinTick >= 240);
		if (!bReady) return;
		if (!bSpinFramed)
		{
			bSpinFramed = true;
			SmvSpinPrims(SpinSubject);   // GLB ile eklenen bileşenler dahil
			if (SpinJob.bAvatar && SpinFig)
			{
				// kadraj baş-omuz; figür ayakları GSmvSpinAt'ta kalır (dönme ekseni dikey → yerel kayma yalnız yukarı)
				SpinAt = GSmvSpinAt + FVector(0.0, 0.0, SpinFig->HeadHeightCm() - 16.0);
				SpinLocalOff = FVector(0.0, 0.0, SpinFig->HeadHeightCm() - 16.0);
				SpinDist = 128.f;
			}
			else
			{
				const FBox Bx = SmvSpinBounds(SpinSubject);
				float Rcm = 60.f;
				if (ASmvDroneActor* Dr = Cast<ASmvDroneActor>(SpinSubject)) Rcm = Dr->GetVisualRadiusM() * 100.f;
				SpinAt = GSmvSpinAt;
				if (Bx.IsValid && Bx.GetExtent().GetMax() > 3.0 && Bx.GetExtent().GetMax() < 5000.0)
				{
					SpinAt = Bx.GetCenter();
					// dönüşte kadrajdan taşmasın: yatay yarıçap (dönen kutunun köşegeni)
					Rcm = FMath::Max(Rcm * 0.5f, (float)Bx.GetExtent().Size2D());
				}
				SpinLocalOff = SpinSubject->GetActorRotation().UnrotateVector(SpinAt - SpinSubject->GetActorLocation());
				// yatay FOV 26° (yarım açı tanjantı 0,231) + %10 pay
				SpinDist = FMath::Clamp(Rcm / 0.231f * 1.1f, 120.f, 4000.f);
			}
		}
		const int32 Idx = Set->Frames.Num();
		const float Yaw0 = SpinJob.bAvatar ? 180.f : 25.f;
		const FRotator Rot(0.f, Yaw0 + 360.f * Idx / FMath::Max(1, SpinFrameCount), 0.f);
		SpinSubject->SetActorLocationAndRotation(SpinAt - Rot.RotateVector(SpinLocalOff), Rot);
		const float D = SpinDist;
		const FVector CamAt = SpinJob.bAvatar ? SpinAt + FVector(-D, 0.0, 6.0) : SpinAt + FVector(-D * 0.88, -D * 0.42, D * 0.3);
		const float Base = SmvCfg(TEXT("SpinLight"), 40.f);
		auto K = [](const FVector& A, const FVector& B) { const float Dd = (float)FVector::Dist(A, B) / 171.f; return FMath::Clamp(Dd * Dd, 0.3f, 60.f); };
		const FVector LAt[3] = { SpinAt + FVector(-D * 0.6, D * 0.7, D * 0.8), SpinAt + FVector(D * 0.8, -D * 0.5, D * 0.4), CamAt + FVector(0.0, -D * 0.3, D * 0.1) };
		const float LMul[3] = { 1.f, 0.8f, 0.45f };
		for (int32 i = 0; i < SpinLights.Num() && i < 3; ++i)
		{
			SpinLights[i]->SetWorldLocation(LAt[i]);
			SpinLights[i]->SetIntensity(Base * LMul[i] * K(SpinAt, LAt[i]));
			SpinLights[i]->SetAttenuationRadius((float)FVector::Dist(SpinAt, LAt[i]) * 3.f);
		}
		SnapCap->FOVAngle = 26.f;
		SnapCap->SetWorldLocationAndRotation(CamAt, (SpinAt - CamAt).Rotation());
		SnapCap->ShowOnlyActors.Reset();
		SnapCap->ShowOnlyActors.Add(SpinSubject);
		{
			TArray<AActor*> Acts;
			SpinSubject->GetAttachedActors(Acts, true, true);
			for (AActor* X : Acts) if (X) SnapCap->ShowOnlyActors.Add(X);
		}
		SnapCap->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
		SnapCap->TextureTarget = SpinRtL;
		SnapCap->CaptureScene();
		SnapCap->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDR;
		SnapCap->TextureTarget = SpinRtH;
		SnapCap->CaptureScene();
		bSpinRead = true;
		return;
	}
	if (SpinQueue.Num() == 0) return;
	SpinJob = SpinQueue[0];
	SpinQueue.RemoveAt(0);
	EnsureSpinLights();
	// hedef boyut: dron 240×150, pilot baş-omuz 160×176
	const int32 Wd = SpinJob.bAvatar ? 160 : 240, Hd = SpinJob.bAvatar ? 176 : 150;
	if (!SpinRtL || SpinRtL->SizeX != Wd || SpinRtL->SizeY != Hd)
	{
		SpinRtL = NewObject<UTextureRenderTarget2D>(this);
		SpinRtL->ClearColor = FLinearColor(0.f, 0.f, 0.f, 0.f);
		SpinRtL->InitAutoFormat(Wd, Hd);
		SpinRtL->UpdateResourceImmediate(true);
		SpinRtH = NewObject<UTextureRenderTarget2D>(this);
		SpinRtH->ClearColor = FLinearColor(0.f, 0.f, 0.f, 1.f);
		SpinRtH->InitCustomFormat(Wd, Hd, PF_FloatRGBA, true);
		SpinRtH->UpdateResourceImmediate(true);
	}
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpinTick = 0;
	SpinGlbTick = -1;
	bSpinHasModel = false;
	bSpinRead = false;
	bSpinFramed = false;
	if (SpinJob.bAvatar)
	{
		if (!SpinFig) SpinFig = W->SpawnActor<ASmvPilotFigure>(ASmvPilotFigure::StaticClass(), FTransform(GSmvSpinAt), P);
		if (!SpinFig) return;
		SpinFig->bLowDetail = false;   // gerçek baş-omuz (MetaHuman varsa)
		SpinFig->Build(SpinJob.Avatar, false);
		SpinFig->SetActorHiddenInGame(false);
		SpinFig->SetActorLocationAndRotation(GSmvSpinAt, FRotator(0.f, 180.f, 0.f));
		SpinSubject = SpinFig;
		SmvSpinPrims(SpinFig);
		return;
	}
	ASmvDroneActor* Dr = W->SpawnActor<ASmvDroneActor>(ASmvDroneActor::StaticClass(), FTransform(FRotator(0.f, 25.f, 0.f), GSmvSpinAt), P);
	if (!Dr) return;
	FCaySimSession S;
	S.Type = SmvMenuCaysimType(SpinJob.Type);
	S.Model.Orient.Forward = TEXT("+x");
	S.Model.Orient.Up = TEXT("+y");
	S.Model.Orient.Scale = 1.f;
	Dr->Setup(S, nullptr);
	Dr->ConfigureVisual(SpinJob.Type, SpinJob.Model, SmvHexColor(SpinJob.Color));
	Dr->SetActorTickEnabled(false);
	Dr->SetActorLocationAndRotation(GSmvSpinAt, FRotator(0.f, 25.f, 0.f));
	if (USmvMotorSynth* M = Dr->GetMotor()) M->Stop();
	SpinSubject = Dr;
	SmvSpinPrims(Dr);
	if (USmvGameInstance* GI = USmvGameInstance::GetSmv(this))
	{
		TWeakObjectPtr<ASmvDroneActor> WP(Dr);
		TWeakObjectPtr<ASmvMenuScene> WS(this);
		if (GI->GetData() && !SpinJob.Model.IsEmpty())
		{
			bSpinHasModel = true;
			GI->GetData()->FetchModelGlb(SpinJob.Model, [WP, WS](bool bOk, const TArray<uint8>& Bytes)
			{
				if (bOk && WP.IsValid()) { WP->LoadGlbBytes(Bytes); SmvSpinPrims(WP.Get()); }
				// yüklenemese de iş sürsün (siluet)
				if (WS.IsValid() && WS->SpinSubject == WP.Get()) WS->SpinGlbTick = WS->SpinTick;
			});
		}
	}
}

void ASmvMenuScene::PlayLanding() { LandAt = FPlatformTime::Seconds(); }

void ASmvMenuScene::RecenterFabMap()
{
	// Fab haritasının içeriği dünya orijininde değilse (Rafineri: tur kamerası boş denize / göğe bakıyordu) menü dronu, kamera
	// istasyonları ve tur yörüngesi içeriğin merkezine (varsa haritanın PlayerStart'ına) kaydırılır: FabStartE/N
	if (!bFabMap || !MenuLevel || !MenuLevel->GetLoadedLevel() || !WorldSetup) return;
	FBox Box(ForceInit);
	TArray<FVector> Starts;
	int32 N = 0;
	TArray<AActor*> AllActors;
	for (ULevel* Lv : MenuLevels()) for (AActor* X : Lv->Actors) AllActors.Add(X);
	for (AActor* A : AllActors)
	{
		if (!IsValid(A)) continue;
		if (A->IsA<APlayerStart>()) { Starts.Add(A->GetActorLocation()); continue; }
		TInlineComponentArray<UStaticMeshComponent*> Cs(A);
		for (UStaticMeshComponent* C : Cs)
		{
			if (!C || !C->IsRegistered() || !C->IsVisible() || !C->GetStaticMesh()) continue;
			const FBoxSphereBounds& B = C->Bounds;
			if (B.BoxExtent.GetMax() > 40000.0) continue;   // gök küresi / dev zemin düzlemi / su
			Box += B.GetBox();
			++N;
		}
	}
	UE_LOG(LogSimumav, Log, TEXT("Menü Fab haritası %s içerik: %d aktör, %d ağ, kutu %s, World Partition %s"), *MapKey, MenuLevel->GetLoadedLevel()->Actors.Num(), N,
		Box.IsValid ? *Box.GetSize().ToString() : TEXT("-"), (MenuLevel->GetLoadedLevel()->GetTypedOuter<UWorld>() && MenuLevel->GetLoadedLevel()->GetTypedOuter<UWorld>()->IsPartitionedWorld()) ? TEXT("evet") : TEXT("hayır"));
	if (!Box.IsValid || N < 5) return;
	const FSmvMapDef* Def = FSmvMapRegistry::Find(MapKey);
	const bool bFenced = Def && Def->FenceM >= 0.0;
	TourR = 280.0; TourAlt = 95.0;
	if (bFenced)
	{
		// saha dışı gösterilmesin: tur yörüngesi sahanın içinde, alçakta
		TourR = FMath::Clamp(0.36 * FMath::Min(Box.GetSize().X, Box.GetSize().Y) / 100.0, 40.0, 280.0);
		TourAlt = FMath::Clamp(TourR * 0.3, 16.0, 95.0);
	}
	const double La = Lat0 + FMath::RadiansToDegrees(FabStartN / 6378137.0);
	const double Lo = Lon0 + FMath::RadiansToDegrees(FabStartE / (6378137.0 * FMath::Cos(FMath::DegreesToRadians(Lat0))));
	const FVector P0 = WorldSetup->GeoToUnreal(Lo, La, WorldSetup->MslToWorldHeight(La, Lo, GroundMsl));
	const FBox Inner = Box.ExpandBy(-FMath::Min(5000.0, 0.25 * FMath::Min(Box.GetSize().X, Box.GetSize().Y)));
	if (!bFenced && Inner.IsValid && Inner.IsInsideXY(P0)) return;
	FVector C = Box.GetCenter();
	if (!bFenced) for (const FVector& S : Starts) if (Box.IsInsideXY(S)) { C = S; break; }
	const double DE = (C.X - P0.X) / 100.0, DN = -(C.Y - P0.Y) / 100.0;
	FabStartE += DE; FabStartN += DN;
	GroundCache.Reset();
	bCamInit = false;
	UE_LOG(LogSimumav, Log, TEXT("Menü Fab haritası %s: içerik orijinde değil (%d ağ, kutu %.0f x %.0f m) — sahne merkezi %.0f m doğu, %.0f m kuzey kaydırıldı%s"),
		*MapKey, N, Box.GetSize().X / 100.0, Box.GetSize().Y / 100.0, DE, DN, Starts.Num() ? TEXT(" (PlayerStart)") : TEXT(""));
}

void ASmvMenuScene::TickScenery()
{
	if (bSceneryDone || !bLive || !WorldSetup) return;
	FSmvFabContent::DumpFabApis();
	UWorld* W = GetWorld();
	if (!W) return;
	if (bFabMap && (!MenuLevel || !MenuLevel->IsLevelVisible() || !MenuLevel->GetLoadedLevel())) return;
	if (bFabMap && !bMenuSubsReq)
	{
		bMenuSubsReq = true;
		MenuSubsAt = FPlatformTime::Seconds();
		UWorld* LW = MenuLevel->GetLoadedLevel()->GetTypedOuter<UWorld>();
		if (LW && LW != W)
			for (ULevelStreaming* Sub : LW->GetStreamingLevels())
			{
				if (!Sub || Sub->GetWorldAssetPackageName().IsEmpty()) continue;
				bool bOk = false;
				if (ULevelStreamingDynamic* Lv = ULevelStreamingDynamic::LoadLevelInstance(W, Sub->GetWorldAssetPackageName(), Sub->LevelTransform.GetLocation(), Sub->LevelTransform.Rotator(), bOk); bOk && Lv) MenuSubs.Add(Lv);
			}
		if (MenuSubs.Num()) { UE_LOG(LogSimumav, Log, TEXT("Menü Fab haritası %s: %d alt seviye yükleniyor"), *MapKey, MenuSubs.Num()); return; }
	}
	if (bFabMap && MenuSubs.Num() && FPlatformTime::Seconds() - MenuSubsAt < 45.0)
		for (const TObjectPtr<ULevelStreamingDynamic>& L : MenuSubs) if (L && !L->IsLevelVisible()) return;
	bSceneryDone = true;
	if (const FSmvMapDef* Kd = bFabMap ? FSmvMapRegistry::Find(MapKey) : nullptr)
		if (Kd->KitDir && *Kd->KitDir) FSmvKitYard::Build(W, MenuLevel->GetLoadedLevel(), Kd->KitDir, 7);
	RecenterFabMap();
	ClearShowcaseSpot();
	FActorSpawnParameters Sp;
	Sp.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// Fab haritası "havada ada" ise çevresine arazi diski + uzak dağlar (uçuştakiyle aynı: ASmvScenery::BuildSurround)
	const FSmvMapDef* Def = bFabMap ? FSmvMapRegistry::Find(MapKey) : nullptr;
	// yalnız küçük sahalar (çit < 10 m: Yıkık İnşaat, Askerî Üs, Rafineri); büyük açık haritalarda (Karakol, Adalar) arazinin kendisi oyun alanı
	if (Def && Def->FenceM >= 0.0 && Def->FenceM < 10.0) FSmvScenery::HideOutsideSite(MenuLevels(), 3000.0);
	if (Def && Def->Surround && *Def->Surround && FSmvScenery::Enabled())
	{
		if (ASmvScenery* Sc = W->SpawnActor<ASmvScenery>(ASmvScenery::StaticClass(), FTransform::Identity, Sp))
		{
			TArray<AActor*> Ig;
			if (Flyer) Ig.Add(Flyer);
			for (const TObjectPtr<ASmvDroneActor>& X : Extras) if (X) Ig.Add(X);
			if (Sc->BuildSurround(MenuLevels(), Def->Surround, 11, Ig, Def->FenceM < 0.0)) SceneryActors.Add(Sc);
			else Sc->Destroy();
		}
		GroundCache.Reset();
	}
	// alçak sis kümeleri (Fab Niagara Examples BP_FogBankCard): ufukta, uzak planda sinematik pus katmanı (MenuFogBanks)
	bool bFog = true;
	if (GConfig) GConfig->GetBool(GSmvMenuSection, TEXT("MenuFogBanks"), bFog, GGameIni);
	if (bFog && FPackageName::DoesPackageExist(TEXT("/Game/NiagaraExamples/FX_Fog/BP_FogBankCard")))
	{
		if (UClass* FogC = LoadClass<AActor>(nullptr, TEXT("/Game/NiagaraExamples/FX_Fog/BP_FogBankCard.BP_FogBankCard_C"), nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			FSmvFabContent::DumpClassApi(FogC, TEXT("FogBankCard"));
			const float Sc = SmvCfg(TEXT("MenuFogScale"), 40.f);
			static const double Spots[][2] = { { 600.0, 450.0 }, { -650.0, 380.0 }, { 300.0, -700.0 }, { -450.0, -520.0 }, { 900.0, -150.0 } };
			int32 k = 0;
			for (const auto& Sp2 : Spots)
			{
				const FVector At = EnuToWorld(Sp2[0], Sp2[1], 6.0);
				if (AActor* Fg = W->SpawnActor<AActor>(FogC, FTransform(FRotator(0.0, 70.0 * k++, 0.0), At, FVector(Sc)), Sp)) { SceneryActors.Add(Fg); FogBanks.Add(Fg); Fg->SetActorHiddenInGame(IsShowStation(Station)); }
			}
		}
	}
	if (ASmvSkyTraffic* St = W->SpawnActor<ASmvSkyTraffic>(ASmvSkyTraffic::StaticClass(), FTransform::Identity, Sp))
	{
		const FVector C = WorldSetup->GeoToUnreal(Lon0, Lat0, WorldSetup->MslToWorldHeight(Lat0, Lon0, GroundMsl));
		if (St->Build(C, 4, 160000.0, 22000.0, 52000.0, 7)) SceneryActors.Add(St);
		else St->Destroy();
	}
}

ASmvDroneActor* ASmvMenuScene::SpawnFlyer(const FString& Type, const FLinearColor& Color)
{
	UWorld* W = GetWorld();
	if (!W) return nullptr;
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASmvDroneActor* D = W->SpawnActor<ASmvDroneActor>(ASmvDroneActor::StaticClass(), FTransform::Identity, P);
	if (!D) return nullptr;
	FCaySimSession S;
	S.Type = SmvMenuCaysimType(Type);
	S.Model.Orient.Forward = TEXT("+x");
	S.Model.Orient.Up = TEXT("+y");
	S.Model.Orient.Scale = 1.f;
	D->Setup(S, nullptr);
	// katalogdan bu tipin varsayılan modeli (dinamik: yeni modeller kendiliğinden)
	FString Model = USmvDataService::DefaultModelFor(Type);
	if (Type == TEXT("plane") && USmvDataService::FindModel(TEXT("smv:caymaz"))) Model = TEXT("smv:caymaz");
	if (Type == TEXT("vtol") && USmvDataService::FindModel(TEXT("smv:caymaz_vtol"))) Model = TEXT("smv:caymaz_vtol");
	D->ConfigureVisual(Type, Model, Color);
	D->SetActorTickEnabled(false);   // konumu biz sürüyoruz (telemetri yok)
	if (USmvMotorSynth* M = D->GetMotor()) M->Stop();
	if (USmvGameInstance* GI = USmvGameInstance::GetSmv(this))
	{
		TWeakObjectPtr<ASmvDroneActor> WD(D);
		if (GI->GetData() && !Model.IsEmpty()) GI->GetData()->FetchModelGlb(Model, [WD](bool bOk, const TArray<uint8>& Bytes) { if (bOk && WD.IsValid()) WD->LoadGlbBytes(Bytes); });
	}
	return D;
}

float ASmvMenuScene::GroundAt(double E, double N)
{
	// 10 m ızgarada önbellekli aşağı iz düşümü (Fab haritası yüklendikçe dolar)
	const int64 Key = ((int64)FMath::RoundToInt(E / 10.0) << 32) ^ (int64)(uint32)FMath::RoundToInt(N / 10.0);
	if (const float* G = GroundCache.Find(Key)) return *G;
	const double Lat = Lat0 + FMath::RadiansToDegrees((N + FabStartN) / 6378137.0);
	const double Lon = Lon0 + FMath::RadiansToDegrees((E + FabStartE) / (6378137.0 * FMath::Cos(FMath::DegreesToRadians(Lat0))));
	const FVector P = WorldSetup ? WorldSetup->GeoToUnreal(Lon, Lat, WorldSetup->MslToWorldHeight(Lat, Lon, GroundMsl)) : FVector(E * 100.0, -N * 100.0, 0.0);
	FHitResult Hit;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(SmvMenuGround), true);
	for (const TObjectPtr<ASmvDroneActor>& X : Extras) Q.AddIgnoredActor(X);
	if (Flyer) Q.AddIgnoredActor(Flyer);
	if (Showcase) Q.AddIgnoredActor(Showcase);
	if (ShowPilot) Q.AddIgnoredActor(ShowPilot);
	if (GetWorld() && GetWorld()->LineTraceSingleByChannel(Hit, P + FVector(0, 0, 200000.0), P - FVector(0, 0, 200000.0), ECC_Visibility, Q))
	{
		GroundCache.Add(Key, (float)Hit.ImpactPoint.Z);
		return (float)Hit.ImpactPoint.Z;
	}
	return (float)P.Z;   // henüz yüklenmedi: önbelleğe yazma
}

void ASmvMenuScene::SetWorldPreview(const FSmvTrack& Track)
{
	if (!FSmvUiPrefs::Get().bPreview3d || !WorldSetup) return;
	UWorld* W = GetWorld();
	if (!W) return;
	if (Preview) { Preview->Destroy(); Preview = nullptr; PrevType.Empty(); }
	if (PilotPrev) { PilotPrev->Destroy(); PilotPrev = nullptr; PilotKey.Empty(); }
	if (WorldPrev) { WorldPrev->Destroy(); WorldPrev = nullptr; }
	// menü dünyasından ~0,6° uzakta (görünmez) — yalnız önizleme yakalaması görür
	FSmvTrack T = Track;
	WpLat = Lat0 + 0.6; WpLon = Lon0 + 0.6; WpSize = FMath::Clamp(T.World.SizeM, 500.0, 8000.0);
	T.Lat = WpLat; T.Lon = WpLon; T.GroundMsl = GroundMsl;
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	WorldPrev = W->SpawnActor<ASmvProcTerrain>(ASmvProcTerrain::StaticClass(), FTransform::Identity, P);
	if (!WorldPrev) return;
	WorldPrev->bPlainMaterial = true;
	WorldPrev->Build(T, GroundMsl, WorldSetup);
	Capture->ShowOnlyActors.Reset();
	StudioWall->SetVisibility(false); StudioFloor->SetVisibility(false);
	Capture->ShowOnlyActors.Add(WorldPrev);
	// arazinin oluşturduğu alt aktörler (ağaç / bina ISM'leri ayrı aktörse) de görünsün
	TArray<AActor*> Att;
	WorldPrev->GetAttachedActors(Att, true);
	for (AActor* A : Att) Capture->ShowOnlyActors.Add(A);
	// ışık: menü dünyasının güneşi ~67 km ötedeki önizleme yakalamasında araziyi siyah bırakıyordu (yalnız beyaz kenarlar) →
	// önizleme arazisi 1. ışık kanalında kendi alçak güneşiyle aydınlanır; arkasında gök rengi kart (yalnız yakalamada görünür)
	TArray<AActor*> All = Att;
	All.Add(WorldPrev);
	for (AActor* A : All)
	{
		TInlineComponentArray<UPrimitiveComponent*> Pr(A);
		for (UPrimitiveComponent* Pc : Pr) if (Pc) Pc->SetLightingChannels(true, true, false);
	}
	if (!PrevSun)
	{
		PrevSun = NewObject<UDirectionalLightComponent>(this, TEXT("PrevSun"));
		PrevSun->SetupAttachment(Root);
		PrevSun->RegisterComponent();
		PrevSun->SetLightingChannels(false, true, false);
		// gölge kapalı: ~67 km ötedeki yakalamada (gölge kaskadları ana kameraya göre) önizleme zemini tümüyle gölgede / siyah kalıyordu
		PrevSun->SetCastShadows(false);
		PrevSun->SetAtmosphereSunLight(false);
	}
	PrevSun->SetIntensity(SmvCfg(TEXT("PreviewSunLux"), 2.5f));
	PrevSun->SetLightColor(Track.TimeOfDay == TEXT("golden") || Track.TimeOfDay == TEXT("dusk") ? FLinearColor(1.f, 0.82f, 0.62f) : FLinearColor(1.f, 0.97f, 0.92f));
	PrevSun->SetWorldRotation(FRotator(Track.TimeOfDay == TEXT("day") ? -48.f : -24.f, 140.f, 0.f));
	PrevSun->SetVisibility(true);
	if (!SkyCard)
	{
		SkyCard = NewObject<UStaticMeshComponent>(this, TEXT("SkyCard"));
		SkyCard->SetupAttachment(Root);
		SkyCard->RegisterComponent();
		SkyCard->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		SkyCard->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SkyCard->SetCastShadow(false);
		SkyCard->SetLightingChannels(false, true, false);
		SkyCard->SetVisibleInSceneCaptureOnly(true);
		if (UMaterialInterface* Sm = SmvSolidMat(LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"))))
		{
			UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Sm, this);
			SmvSetSolidColor(M, FLinearColor(0.f, 0.f, 0.f));
			M->SetVectorParameterValue(TEXT("Emissive"), Track.TimeOfDay == TEXT("night") ? FLinearColor(0.02f, 0.03f, 0.07f) : FLinearColor(0.42f, 0.58f, 0.82f));
			SkyCard->SetMaterial(0, M);
		}
	}
	SkyCard->SetVisibility(true);
	Capture->ShowOnlyComponents.AddUnique(SkyCard.Get());
	Capture->bCaptureEveryFrame = bPreviewOn;
}

FText ASmvMenuScene::GetMapName() const
{
	// geçerli dilde (dil sonradan değişebilir)
	if (!MapKey.IsEmpty()) for (const FSmvFabMap& M : FSmvFabContent::AllMaps()) if (M.Key == MapKey) return M.Name;
	return FText::GetEmpty();
}

void ASmvMenuScene::PlaceLights(const FVector& Target, const FVector& KeyAt, const FVector& RimAt, float Scale)
{
	// temel şiddet 1,71 m uzaklık için ayarlıydı (Iris önizlemesi); uzak ışıkta ters kare telafisi
	const float Base = SmvCfg(TEXT("PreviewLight"), 40.f) * Scale;
	auto K = [](const FVector& A, const FVector& B) { const float D = (float)FVector::Dist(A, B) / 171.f; return FMath::Clamp(D * D, 0.3f, 60.f); };
	KeyLight->SetWorldLocation(KeyAt);
	RimLight->SetWorldLocation(RimAt);
	KeyLight->SetIntensity(Base * K(Target, KeyAt));
	RimLight->SetIntensity(Base * 0.8f * K(Target, RimAt));
	KeyLight->SetAttenuationRadius((float)FVector::Dist(Target, KeyAt) * 3.f);
	RimLight->SetAttenuationRadius((float)FVector::Dist(Target, RimAt) * 3.f);
}

void ASmvMenuScene::PlaceStudio(float FloorZ)
{
	StudioWall->SetVisibility(true); StudioFloor->SetVisibility(true);
	StudioWall->SetWorldLocationAndRotation(GSmvPreviewAt + FVector(2600.0, 0.0, 1500.0), FRotator::ZeroRotator);
	StudioWall->SetWorldScale3D(FVector(0.2f, 120.f, 80.f));
	// duvar halesi: figür/araç hizasında, duvarın 3 m önünde
	WallLight->SetWorldLocation(GSmvPreviewAt + FVector(2300.0, 0.0, 160.0));
	StudioFloor->SetWorldLocation(GSmvPreviewAt + FVector(0.0, 0.0, FloorZ - 1.0));
	StudioFloor->SetWorldScale3D(FVector(16.f, 16.f, 0.02f));
}

// ---------------------------------------------------------------------------
// yol haritası: canlı dünyada kamera istasyonları + vitrin aracı / pilot + harita değişimi
// ---------------------------------------------------------------------------

namespace
{
	FSmvTrack SmvMenuTrack(double Lat, double Lon, double Msl)
	{
		FSmvTrack Tr;
		Tr.Name = TEXT("Menu");
		Tr.Lat = Lat; Tr.Lon = Lon; Tr.GroundMsl = Msl;
		Tr.TimeOfDay = TEXT("golden");
		Tr.World.Seed = 7;
		Tr.World.SizeM = 4000.0;
		Tr.World.MountainCount = 6;
		Tr.World.MountainHeight = 260.0;
		Tr.World.TreeDensity = 0.35;
		Tr.World.BuildingCount = 30;
		Tr.World.bRunway = true;
		Tr.World.RunwayHeading = 70.0;
		Tr.World.Water = 0.12;
		Tr.World.Fog = 0.03;
		return Tr;
	}
}

void ASmvMenuScene::LoadMenuMap(const FString& Want)
{
	UWorld* W = GetWorld();
	if (!W) return;
	DropMenuLevels();
	for (TObjectPtr<AActor>& X : SceneryActors) if (X) X->Destroy();
	SceneryActors.Reset(); FogBanks.Reset();
	if (Terrain) { Terrain->Destroy(); Terrain = nullptr; }
	bSceneryDone = false;
	bFabMap = false;
	MapKey.Empty();
	FabStartE = FabStartN = 0;
	GroundCache.Reset();
	const FSmvFabMap* Pick = nullptr;
	if (Want != TEXT("none") && Want != TEXT("proc"))
	{
		const TArray<FSmvFabMap>& Maps = FSmvFabContent::Maps();
		for (const FSmvFabMap& M : Maps) if (M.Kind == ESmvMapKind::Level && !M.Level.IsEmpty() && M.Key == Want) Pick = &M;
		// varsayılan öncelik: orman (Brushify), rafineri (KitBash3D), askeri üs — gerçekçi altın saat arka planı
		static const TCHAR* Pref[] = { TEXT("orman"), TEXT("forest"), TEXT("rafineri"), TEXT("refinery"), TEXT("askeri"), TEXT("military"), TEXT("base") };
		for (const TCHAR* Pf : Pref) { if (Pick) break; for (const FSmvFabMap& M : Maps) if (M.Kind == ESmvMapKind::Level && !M.Level.IsEmpty() && M.Key.Contains(Pf)) { Pick = &M; break; } }
	}
	if (Pick)
	{
		bool bOk = false;
		FSmvMapRegistry::ApplyGpuProfile(FSmvMapRegistry::Find(Pick->Key));
		MenuLevel = ULevelStreamingDynamic::LoadLevelInstance(W, Pick->Level, FVector::ZeroVector, FRotator::ZeroRotator, bOk);
		bFabMap = bOk && MenuLevel;
		if (bFabMap) MapKey = Pick->Key;
		FabStartE = Pick->StartE; FabStartN = Pick->StartN;
		UE_LOG(LogSimumav, Log, TEXT("Menü sahnesi Fab haritası: %s (%s)"), *Pick->Level, bFabMap ? TEXT("yükleniyor") : TEXT("yüklenemedi"));
	}
	if (!Pick) FSmvMapRegistry::ApplyGpuProfile(nullptr);
	if (!bFabMap && WorldSetup)
	{
		Terrain = W->SpawnActor<ASmvProcTerrain>();
		if (Terrain) Terrain->Build(SmvMenuTrack(Lat0, Lon0, GroundMsl), GroundMsl, WorldSetup);
	}
	bCamInit = false;
	// hazırlık kapısı: ilk yükleme açılış ("menu"), sonrakiler harita turu ("tur")
	Gate.Begin(GateCount++ == 0 ? TEXT("menu") : TEXT("tur"), { ESmvLoad::World, ESmvLoad::Atmos, ESmvLoad::Shaders, ESmvLoad::Streaming, ESmvLoad::Warmup });
	GAtmosT0 = GWarmT0 = GStreamT0 = 0; GAtmosF0 = GWarmF0 = 0; GStreamPeak = GShaderPeak = 0; GWarmRecap = 0;
}

void ASmvMenuScene::TickGate()
{
	if (!Gate.IsActive() || Gate.IsDone()) return;
	UWorld* W = GetWorld();
	const double Now = FPlatformTime::Seconds();
	if (!Gate.Done(ESmvLoad::World))
	{
		float P = 0.05f;
		if (bFabMap && MenuLevel)
		{
			if (MenuLevel->IsLevelVisible()) P = 1.f;
			else if (MenuLevel->HasLoadedLevel()) P = 0.9f;
			else { const float A = GetAsyncLoadPercentage(MenuLevel->GetWorldAssetPackageFName()); P = A > 0.f ? 0.85f * A / 100.f : 0.05f; }
		}
		else if (Terrain) P = 1.f;
		Gate.Update(ESmvLoad::World, P, IsMapReady() && FSmvLoadGate::WorldPartitionDone(W) && !(W && W->IsVisibilityRequestPending()), MapKey, 45.0, 3);
	}
	const bool bWorld = Gate.Done(ESmvLoad::World);
	if (!Gate.Done(ESmvLoad::Atmos))
	{
		float P = 0.2f; bool bReady = false;
		if (bWorld)
		{
			if (GAtmosT0 <= 0) { GAtmosT0 = Now; GAtmosF0 = GFrameCounter; FSmvLoadGate::RecaptureSky(W); }
			const double K = FMath::Min((double)(GFrameCounter - GAtmosF0) / 20.0, (Now - GAtmosT0) / 0.6);
			P = 0.2f + 0.8f * (float)FMath::Clamp(K, 0.0, 1.0);
			bReady = K >= 1.0;
		}
		Gate.Update(ESmvLoad::Atmos, P, bReady, SkyActors.Num() ? TEXT("UDS") : TEXT("builtin"), 50.0);
	}
	if (!Gate.Done(ESmvLoad::Shaders))
	{
		const int32 J = FSmvLoadGate::ShaderJobs() + FSmvLoadGate::PsoPending();
		GShaderPeak = FMath::Max(GShaderPeak, J);
		Gate.Update(ESmvLoad::Shaders, !bWorld ? 0.f : GShaderPeak > 0 ? 1.f - (float)J / GShaderPeak : 1.f, bWorld && J == 0, FString::FromInt(J), 60.0, 8);
	}
	if (!Gate.Done(ESmvLoad::Streaming))
	{
		const int32 Np = FSmvLoadGate::StreamingPending();
		float P = 0.f; bool bReady = false;
		if (bWorld && Gate.Done(ESmvLoad::Atmos))
		{
			if (GStreamT0 <= 0) GStreamT0 = Now;
			GStreamPeak = FMath::Max(GStreamPeak, Np);
			P = GStreamPeak > 0 ? 1.f - (float)Np / GStreamPeak : 1.f;
			bReady = Np <= (Now - GStreamT0 > 5.0 ? FMath::Max(2, GStreamPeak / 50) : 0);
		}
		Gate.Update(ESmvLoad::Streaming, P, bReady, FString::FromInt(Np), 60.0, 10);
	}
	bool bOthers = true;
	for (int32 i = 0; i < (int32)ESmvLoad::Warmup; ++i) if (!Gate.Done((ESmvLoad)i)) bOthers = false;
	if (bOthers && !Gate.Done(ESmvLoad::Warmup))
	{
		if (GWarmT0 <= 0) { GWarmT0 = Now; GWarmF0 = GFrameCounter; FSmvLoadGate::RecaptureSky(W); GWarmRecap = 1; }
		if (GWarmRecap > 0 && Now - GWarmT0 > 0.5) { FSmvLoadGate::RecaptureSky(W); GWarmRecap = 0; }
		const double K = FMath::Min((double)(GFrameCounter - GWarmF0) / 30.0, (Now - GWarmT0) / 1.0);
		Gate.Update(ESmvLoad::Warmup, (float)FMath::Clamp(K, 0.0, 1.0), K >= 1.0, FString(), FMath::Max(70.0, Gate.Elapsed() + 6.0 - (Now - GWarmT0)));
	}
	// hazırlık boyunca göz uyumu hızlı (sahne açıldığında pozlama oturmuş olsun)
	if (Cam && Cam->GetCameraComponent())
	{
		FPostProcessSettings& Pp = Cam->GetCameraComponent()->PostProcessSettings;
		const bool bFast = !Gate.IsDone();
		Pp.bOverride_AutoExposureSpeedUp = bFast; Pp.AutoExposureSpeedUp = 30.f;
		Pp.bOverride_AutoExposureSpeedDown = bFast; Pp.AutoExposureSpeedDown = 30.f;
	}
	if (Gate.IsDone()) Gate.LogSummary();
}

void ASmvMenuScene::SetMap(const FString& Key)
{
	if (!bLive) return;
	const FString K = Key.IsEmpty() ? FString(TEXT("proc")) : Key;
	if (K == MapKey || (K == TEXT("proc") && !bFabMap && MapKey.IsEmpty() && Terrain)) return;
	LoadMenuMap(K);
}

void ASmvMenuScene::DropMenuLevels()
{
	// harita turu art arda büyük haritalar yükler (Karakol 4 × 4 km, Modern Şehir, Adalar 16 km): önceki seviye boşaltılırken çöp toplama
	// da zorlanır (bellek birikip Modern Şehir'de GPU zaman aşımı — duman 40954102653)
	const bool bHad = MenuLevel != nullptr || MenuSubs.Num() > 0;
	for (TObjectPtr<ULevelStreamingDynamic>& L : MenuSubs) if (L) { L->SetShouldBeLoaded(false); L->SetShouldBeVisible(false); L->SetIsRequestingUnloadAndRemoval(true); }
	MenuSubs.Reset();
	bMenuSubsReq = false;
	if (MenuLevel) { MenuLevel->SetShouldBeLoaded(false); MenuLevel->SetShouldBeVisible(false); MenuLevel->SetIsRequestingUnloadAndRemoval(true); MenuLevel = nullptr; }
	if (bHad && GEngine) GEngine->ForceGarbageCollection(true);
}

TArray<ULevel*> ASmvMenuScene::MenuLevels() const
{
	TArray<ULevel*> Out;
	if (MenuLevel && MenuLevel->GetLoadedLevel()) Out.Add(MenuLevel->GetLoadedLevel());
	for (const TObjectPtr<ULevelStreamingDynamic>& L : MenuSubs) if (L && L->GetLoadedLevel() && L->IsLevelVisible()) Out.Add(L->GetLoadedLevel());
	return Out;
}

bool ASmvMenuScene::IsMapReady() const
{
	if (!bFabMap) return Terrain != nullptr;
	return MenuLevel && MenuLevel->IsLevelVisible() && MenuLevel->GetLoadedLevel() != nullptr;
}

bool ASmvMenuScene::IsShowStation(EStation S) { return S == EStation::Hangar || S == EStation::Fly || S == EStation::Crew; }

void ASmvMenuScene::SetStation(EStation S)
{
	if (S == Station) return;
	Station = S;
	StationAt = FPlatformTime::Seconds();
	if (Showcase) Showcase->SetActorHiddenInGame(!(S == EStation::Hangar || S == EStation::Fly));
	if (ShowPilot) ShowPilot->SetActorHiddenInGame(S != EStation::Crew);
	for (const TObjectPtr<AActor>& Fg : FogBanks) if (Fg) Fg->SetActorHiddenInGame(IsShowStation(S));
	ApplyShowcaseClear(IsShowStation(S));
}

void ASmvMenuScene::ApplyShowcaseClear(bool bOn)
{
	// dron / pilot vitrininde bulut, sis ve duman tamamen kapalı (kullanıcı 2026-11-03: hangarda ve tüm dron gösterimlerinde
	// aracın önünde beyaz bulut çıkıyordu). Kaynaklar: kameraya bağlı toz zerreleri (Motes), haritanın kendi Niagara / Cascade
	// sis-buhar efektleri, hacimsel sis ve yere yakın hacimsel bulut. Diğer istasyonlarda eski hâline döner.
	if (bOn == bShowClear) return;
	bShowClear = bOn;
	if (Motes) Motes->SetVisibility(!bOn);
	UWorld* W = GetWorld();
	if (!W) return;
	if (bOn)
	{
		HiddenFx.Reset();
		for (TObjectIterator<UFXSystemComponent> It; It; ++It)
		{
			UFXSystemComponent* C = *It;
			if (!C || C->GetWorld() != W || !C->IsVisible() || C == Motes.Get()) continue;
			if (C->GetOwner() && C->GetOwner()->IsA<ASmvDroneActor>()) continue;   // aracın kendi efektleri (pervane, ışık) kalır
			C->SetVisibility(false);
			HiddenFx.Add(C);
		}
	}
	else
	{
		for (const TWeakObjectPtr<UFXSystemComponent>& C : HiddenFx) if (C.IsValid()) C->SetVisibility(true);
		HiddenFx.Reset();
	}
	auto Cv = [](const TCHAR* N, float V, float& Old, bool bSet)
	{
		if (IConsoleVariable* X = IConsoleManager::Get().FindConsoleVariable(N))
		{
			if (bSet) { Old = X->GetFloat(); X->Set(V, ECVF_SetByCode); }
			else X->Set(Old, ECVF_SetByCode);
		}
	};
	Cv(TEXT("r.VolumetricFog"), 0.f, OldVolFog, bOn);
	for (TActorIterator<AExponentialHeightFog> It(W); It; ++It)
		if (UExponentialHeightFogComponent* F = It->GetComponent()) F->SetVisibility(!bOn);
	for (TActorIterator<AActor> It(W); It; ++It)
		for (UActorComponent* C : It->GetComponents())
			if (UVolumetricCloudComponent* Vc = Cast<UVolumetricCloudComponent>(C))
			{
				// yer yakınındaki bulut katmanı vitrin kamerasına giriyordu: kapalı (gökyüzü açık kalır)
				if (bOn) { if (Vc->IsVisible()) { Vc->SetVisibility(false); HiddenClouds.Add(Vc); } }
			}
	if (!bOn) { for (const TWeakObjectPtr<UVolumetricCloudComponent>& Vc : HiddenClouds) if (Vc.IsValid()) Vc->SetVisibility(true); HiddenClouds.Reset(); }
	UE_LOG(LogSimumav, Log, TEXT("Vitrin bulut/sis temizliği: %s (%d efekt gizli)"), bOn ? TEXT("açık") : TEXT("kapalı"), HiddenFx.Num());
}

void ASmvMenuScene::ClearShowcaseSpot()
{
	// vitrin açıklığı: hangar / mürettebat / uçuş istasyonlarında araç ve pilot orman haritasının ağaç, çalı ve uzun otlarının içinde
	// kalıyordu (kamera yaprakların arasından bakıyordu). Vitrin noktası çevresindeki bitki örneklerini kaldırır (yalnız menü kopyası;
	// uçuş haritasına dokunmaz). Yarıçap: alçak bitki MenuClearM (varsayılan 16 m), ağaç taçları MenuClearTreeM (26 m).
	UWorld* W = GetWorld();
	if (!W || !bFabMap || !MenuLevel || !MenuLevel->GetLoadedLevel()) return;
	const FVector C = ShowGround();
	const double Rlow = SmvCfg(TEXT("MenuClearM"), 16.f) * 100.0, Rtree = SmvCfg(TEXT("MenuClearTreeM"), 26.f) * 100.0;
	int32 Removed = 0, Hidden = 0;
	for (AActor* A : MenuLevel->GetLoadedLevel()->Actors)
	{
		if (!A || A->GetClass()->GetName().Contains(TEXT("Landscape"))) continue;
		TInlineComponentArray<UStaticMeshComponent*> Comps(A);
		for (UStaticMeshComponent* Mc : Comps)
		{
			if (!Mc || !Mc->GetStaticMesh()) continue;
			if (UInstancedStaticMeshComponent* Ism = Cast<UInstancedStaticMeshComponent>(Mc))
			{
				const FBoxSphereBounds Mb = Ism->GetStaticMesh()->GetBounds();
				TArray<int32> Kill;
				for (int32 i = 0; i < Ism->GetInstanceCount(); ++i)
				{
					FTransform Xf;
					if (!Ism->GetInstanceTransform(i, Xf, true)) continue;
					const double Hcm = Mb.BoxExtent.Z * 2.0 * Xf.GetScale3D().Z;
					const double Rad = Hcm > 400.0 ? Rtree : Rlow;
					if (FVector::Dist2D(Xf.GetLocation(), C) < Rad && FMath::Abs(Xf.GetLocation().Z - C.Z) < 3000.0) Kill.Add(i);
				}
				if (Kill.Num()) { Ism->RemoveInstances(Kill); Removed += Kill.Num(); }
			}
			else
			{
				const FBoxSphereBounds B = Mc->Bounds;
				// zemin parçaları (geniş / alçak ağlar) kalır: araç havada kalmasın
				if (B.BoxExtent.Z < 2500.0 && FMath::Max(B.BoxExtent.X, B.BoxExtent.Y) < 2500.0 && B.Origin.Z + B.BoxExtent.Z > C.Z + 80.0
					&& FVector::Dist2D(B.Origin, C) < Rtree && FMath::Abs(B.Origin.Z - C.Z) < 3000.0 && Mc->IsVisible())
				{
					Mc->SetVisibility(false);
					Mc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					++Hidden;
				}
			}
		}
	}
	UE_LOG(LogSimumav, Log, TEXT("Menü vitrin açıklığı: %d bitki örneği kaldırıldı, %d ağ gizlendi (yarıçap %.0f / %.0f m)"), Removed, Hidden, Rlow / 100.0, Rtree / 100.0);
	ClearGrassAt = FPlatformTime::Seconds();
}

void ASmvMenuScene::ClearShowcaseGrass()
{
	// arazi otları (Landscape grass) kamera yakınında yeniden üretilir: vitrin çevresindekiler periyodik kaldırılır
	if (!bFabMap || FPlatformTime::Seconds() - ClearGrassAt < 1.5) return;
	ClearGrassAt = FPlatformTime::Seconds();
	const FVector C = ShowGround();
	const double R = SmvCfg(TEXT("MenuClearM"), 16.f) * 100.0;
	for (TObjectIterator<UHierarchicalInstancedStaticMeshComponent> It; It; ++It)
	{
		UHierarchicalInstancedStaticMeshComponent* H = *It;
		if (!H || H->GetWorld() != GetWorld() || !H->GetClass()->GetName().Contains(TEXT("Grass"))) continue;
		if (!H->Bounds.GetBox().ExpandBy(R).IsInsideXY(C)) continue;
		TArray<int32> Kill;
		for (int32 i = 0; i < H->GetInstanceCount(); ++i)
		{
			FTransform Xf;
			if (H->GetInstanceTransform(i, Xf, true) && FVector::Dist2D(Xf.GetLocation(), C) < R) Kill.Add(i);
		}
		if (Kill.Num()) H->RemoveInstances(Kill);
	}
}

FVector ASmvMenuScene::ShowGround()
{
	return EnuToWorld(ShowE, ShowN, 0.0);
}

void ASmvMenuScene::ClearShowcase()
{
	if (Showcase) { Showcase->Destroy(); Showcase = nullptr; }
	ShowKey.Empty();
}

void ASmvMenuScene::SetShowcase(const FSmvDrone& D)
{
	UWorld* W = GetWorld();
	if (!W || !bLive) return;
	const FString Body = D.Type + TEXT("|") + D.ModelKey + TEXT("|");
	const FString Key = Body + D.Color + TEXT("|") + SmvJson::ToString(D.Equipment.ToJson(false));
	if (Showcase && Key == ShowKey) return;
	const bool bSameBody = Showcase && ShowKey.StartsWith(Body);
	ShowKey = Key;
	const FLinearColor Col = SmvHexColor(D.Color);
	const FSmvEquipment Eq = D.Equipment;
	if (!bSameBody)
	{
		if (Showcase) { Showcase->Destroy(); Showcase = nullptr; }
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Showcase = W->SpawnActor<ASmvDroneActor>(ASmvDroneActor::StaticClass(), FTransform(FRotator(0.f, 35.f, 0.f), ShowGround()), P);
		if (!Showcase) return;
		FCaySimSession S;
		S.Type = SmvMenuCaysimType(D.Type);
		S.Model.Orient.Forward = TEXT("+x");
		S.Model.Orient.Up = TEXT("+y");
		S.Model.Orient.Scale = 1.f;
		Showcase->Setup(S, nullptr);
		Showcase->SetActorTickEnabled(false);
		ShowOrbit = 0.f;
		PlayLanding();
	}
	USmvGlbBody::SetGlowScale(0.25f);
	Showcase->ConfigureVisual(D.Type, D.ModelKey, Col);
	Showcase->SetActorHiddenInGame(!(Station == EStation::Hangar || Station == EStation::Fly));
	if (USmvMotorSynth* M = Showcase->GetMotor()) M->Stop();
	{
		// vitrinde motor sesi yok (Fab motor döngüsü dahil)
		TInlineComponentArray<UAudioComponent*> Au(Showcase.Get());
		for (UAudioComponent* A : Au) if (A) A->Stop();
	}
	Showcase->ApplyEquipment(Eq, Col);
	if (USmvGameInstance* GI = USmvGameInstance::GetSmv(this))
	{
		TWeakObjectPtr<ASmvDroneActor> WP(Showcase);
		if (GI->GetData() && !D.ModelKey.IsEmpty())
			GI->GetData()->FetchModelGlb(D.ModelKey, [WP, Eq, Col](bool bOk, const TArray<uint8>& Bytes)
			{
				if (bOk && WP.IsValid()) { WP->LoadGlbBytes(Bytes); WP->ApplyEquipment(Eq, Col); }
			});
	}
}

void ASmvMenuScene::SetShowcasePilot(const FSmvAvatar& A, bool bFace)
{
	UWorld* W = GetWorld();
	if (!W || !bLive) return;
	bShowFace = bFace;
	const FString Key = SmvJson::ToString(A.ToJson());
	if (ShowPilot && Key == ShowPilotKey) return;
	ShowPilotKey = Key;
	if (!ShowPilot)
	{
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ShowPilot = W->SpawnActor<ASmvPilotFigure>(ASmvPilotFigure::StaticClass(), FTransform(FRotator(0.f, 215.f, 0.f), ShowGround()), P);
	}
	if (!ShowPilot) return;
	bool bRc = false;
	if (GConfig) GConfig->GetBool(GSmvMenuSection, TEXT("CrewRcPose"), bRc, GGameIni);
	ShowPilot->Build(A, bRc);
	ShowPilot->SetActorHiddenInGame(Station != EStation::Crew);
}

void ASmvMenuScene::SettleShowcase()
{
	if (!bLive) return;
	const bool bShowOn = Showcase && !Showcase->IsHidden();
	// vitrin ışıkları: kamera tarafından anahtar + karşı yandan dolgu; yalnız 2. ışık kanalındaki araç gövdesini aydınlatır
	if (ShowLights.Num() == 0 && bShowOn)
	{
		for (int32 i = 0; i < 2; ++i)
		{
			UPointLightComponent* Lt = NewObject<UPointLightComponent>(this);
			Lt->SetupAttachment(Root);
			Lt->SetIntensityUnits(ELightUnits::Candelas);
			Lt->SetCastShadows(false);
			Lt->SetLightingChannels(false, false, true);
			// hacimsel sis ışık kanalını dinlemez: güçlü vitrin ışığı sisi parlatıp tüm görüntüyü beyaz pusla kaplıyordu (dronlar dumanlı)
			Lt->SetVolumetricScatteringIntensity(0.f);
			Lt->bAffectTranslucentLighting = false;
			Lt->SetLightColor(i == 0 ? FLinearColor(1.f, 0.97f, 0.92f) : FLinearColor(0.75f, 0.85f, 1.f));
			Lt->RegisterComponent();
			ShowLights.Add(Lt);
		}
	}
	for (UPointLightComponent* Lt : ShowLights) if (Lt && Lt->IsVisible() != bShowOn) Lt->SetVisibility(bShowOn);
	if (bShowOn && ShowLights.Num() == 2)
	{
		FVector O, E;
		Showcase->GetActorBounds(false, O, E);
		const FVector ToCam = (CamPos - O).GetSafeNormal2D();
		const FVector Side = FVector::CrossProduct(FVector::UpVector, ToCam);
		const double D = FMath::Max(150.0, (double)E.Size() * 2.2);
		const FVector Kp = O + ToCam * D + Side * D * 0.45 + FVector(0, 0, D * 0.7), Fp = O - ToCam * D * 0.3 - Side * D + FVector(0, 0, D * 0.3);
		const float Base = SmvCfg(TEXT("ShowcaseLight"), 5000.f);   // 9000'de beyaz gövdeler patlıyordu (soluk, dumanlı görünüm)
		auto K = [](const FVector& A, const FVector& B) { const float Dd = (float)FVector::Dist(A, B) / 171.f; return FMath::Clamp(Dd * Dd, 0.3f, 400.f); };
		ShowLights[0]->SetWorldLocation(Kp); ShowLights[0]->SetIntensity(Base * K(O, Kp)); ShowLights[0]->SetAttenuationRadius((float)D * 4.f);
		ShowLights[1]->SetWorldLocation(Fp); ShowLights[1]->SetIntensity(Base * 0.4f * K(O, Fp)); ShowLights[1]->SetAttenuationRadius((float)D * 4.f);
		// gövde (GLB ve donanım dahil) 0 + 2. kanalda: güneş + vitrin ışıkları (GLB sonradan yüklendiği için saniyede bir)
		const double Now = FPlatformTime::Seconds();
		if (Now - ShowChanAt > 1.0)
		{
			ShowChanAt = Now;
			TArray<AActor*> Acts; Showcase->GetAttachedActors(Acts, true, true); Acts.Add(Showcase.Get());
			for (AActor* A : Acts)
			{
				if (!A) continue;
				TInlineComponentArray<UPrimitiveComponent*> Ps(A);
				for (UPrimitiveComponent* Pc : Ps) if (Pc && !Pc->LightingChannels.bChannel2) Pc->SetLightingChannels(true, false, true);
			}
		}
	}
	if (Showcase && !Showcase->IsHidden())
	{
		// gövdenin alt yüzü zemine oturur (GLB gövdesi yüklenince boyut değişir; her kare ucuz bir sınır kutusu); yeni araç yukarıdan süzülür
		const FVector G = ShowGround();
		FVector O, E;
		Showcase->GetActorBounds(false, O, E);
		const double Off = Showcase->GetActorLocation().Z - (O.Z - E.Z);
		const double Lk = FMath::Clamp((FPlatformTime::Seconds() - LandAt) / 1.6, 0.0, 1.0);
		const double Drop = Lk < 1.0 ? FMath::Pow(1.0 - Lk, 3.0) * 600.0 : 0.0;
		Showcase->SetActorLocation(FVector(G.X, G.Y, G.Z + FMath::Clamp(Off, 0.0, 2000.0) + 3.0 + Drop));
	}
	if (ShowPilot && !ShowPilot->IsHidden())
	{
		const FVector G = ShowGround();
		ShowPilot->SetActorLocation(FVector(G.X, G.Y, G.Z + 1.0));
	}
}

void ASmvMenuScene::UpdateStationCamera(float Dt)
{
	if (!Cam) return;
	const double T = FPlatformTime::Seconds() - StationAt;
	FVector Want = CamPos, Look = StationFocus;
	float Fstop = 5.6f;
	ShowFaceK = FMath::FInterpTo(ShowFaceK, bShowFace ? 1.f : 0.f, Dt, 3.f);
	switch (Station)
	{
	case EStation::Hangar:
	case EStation::Fly:
	{
		FVector C = ShowGround() + FVector(0, 0, 60.0);
		double R = 140.0;
		if (Showcase)
		{
			FVector O, E;
			Showcase->GetActorBounds(false, O, E);
			C = O;
			R = FMath::Max(60.0, (double)FVector(E.X, E.Y, 0.0).Size());
		}
		if (Station == EStation::Hangar)
		{
			// yavaş yörünge (vitrin): aracın çevresinde 7°/sn, hafif yukarıdan; sığ alan derinliği
			ShowOrbit += Dt * 7.f;
			const double A = FMath::DegreesToRadians((double)ShowOrbit + 210.0);
			const double D = R * 3.3 + 80.0;
			Want = C + FVector(FMath::Cos(A) * D, FMath::Sin(A) * D, R * 0.7 + 50.0);
			Look = C + FVector(0, 0, R * 0.05);
			Fstop = 2.8f;
		}
		else
		{
			// kalkış hazırlığı: aracın sol-arkasından alçak kahraman açısı, yavaşça yaklaşır
			const FVector Fwd = Showcase ? Showcase->GetActorForwardVector() : FVector(1, 0, 0);
			const FVector Right = FVector::CrossProduct(FVector::UpVector, Fwd).GetSafeNormal();
			const double Push = FMath::Clamp(T / 14.0, 0.0, 1.0);
			Want = C - Fwd * (R * (4.4 - 1.4 * Push) + 60.0) - Right * R * 1.5 + FVector(0, 0, R * 0.25 + 35.0);
			Look = C + Fwd * R * 2.5 + FVector(0, 0, R * 0.35);
			Fstop = 4.f;
		}
		break;
	}
	case EStation::Crew:
	{
		const FVector G = ShowGround();
		const float H = ShowPilot ? ShowPilot->HeadHeightCm() : 175.f;
		const FVector Target = G + FVector(0, 0, FMath::Lerp(H * 0.6f, H - 6.f, ShowFaceK));
		const FVector Fwd = ShowPilot ? ShowPilot->GetActorForwardVector() : FVector(1, 0, 0);
		const FVector Right = FVector::CrossProduct(FVector::UpVector, Fwd).GetSafeNormal();
		const double D = FMath::Lerp(430.0, 120.0, (double)ShowFaceK);
		const double Sway = 0.18 * FMath::Sin(T * 0.22);
		Want = Target + Fwd * D + Right * D * (0.32 + Sway) + FVector(0, 0, FMath::Lerp(12.0, 2.0, (double)ShowFaceK));
		Look = Target;
		Fstop = ShowFaceK > 0.5f ? 2.0f : 2.8f;
		break;
	}
	case EStation::Parkur:
	{
		// yüksek kuş bakışı: harita merkezinin çevresinde çok yavaş yörünge
		const double A = T * 0.045 + 0.6;
		Want = EnuToWorld(FMath::Cos(A) * 640.0, FMath::Sin(A) * 640.0, 240.0);
		Look = EnuToWorld(0.0, 0.0, 0.0);
		Fstop = 11.f;
		break;
	}
	case EStation::Tour:
	{
		// harita içinden tur: alçak geçiş → yörünge → yükselen vinç → dron takibi (her biri 10 sn); ölçek TourR (çitli sahada küçük)
		const int32 Shot = (int32)(T / 10.0) % 4;
		const double U = FMath::Fmod(T, 10.0) / 10.0;
		const double Kr = TourR / 280.0;
		if (Shot == 0)
		{
			Want = EnuToWorld(FMath::Lerp(-320.0, 320.0, U) * Kr, (-90.0 + 30.0 * FMath::Sin(U * 3.0)) * Kr, FMath::Max(8.0, 22.0 * Kr));
			Look = EnuToWorld((FMath::Lerp(-320.0, 320.0, U) + 120.0) * Kr, -40.0 * Kr, 8.0 * Kr);
		}
		else if (Shot == 1)
		{
			const double A = U * 1.4 + 2.0;
			Want = EnuToWorld(FMath::Cos(A) * TourR, FMath::Sin(A) * TourR, TourAlt);
			Look = EnuToWorld(0.0, 0.0, 0.0);
		}
		else if (Shot == 2)
		{
			Want = EnuToWorld(140.0 * Kr, 160.0 * Kr, FMath::Lerp(4.0, 150.0 * Kr, U));
			Look = EnuToWorld(0.0, 0.0, 10.0 * Kr);
		}
		else if (Flyer && Kr > 0.95)
		{
			const FVector D0 = Flyer->GetActorLocation(), Fw = Flyer->GetActorForwardVector();
			Want = D0 - Fw * 1300.0 + FVector(0, 0, 320.0);
			Look = D0;
		}
		else
		{
			// çitli saha: menü dronu sahanın dışına uçar — onun yerine saha çevresinde ters yönde yavaş yörünge
			const double A = -U * 1.1 + 0.5;
			Want = EnuToWorld(FMath::Cos(A) * TourR * 0.8, FMath::Sin(A) * TourR * 0.8, TourAlt * 0.6);
			Look = EnuToWorld(0.0, 0.0, 4.0);
		}
		Fstop = 8.f;
		break;
	}
	default: break;
	}
	StationFocus = Look;
	if (!bCamInit || FVector::Dist(Want, CamPos) > 60000.0) { CamPos = Want; bCamInit = true; }
	const double Dc = FMath::Clamp((double)Dt, 0.0, 0.1);
	CamPos = FMath::Lerp(CamPos, Want, 1.0 - FMath::Exp(-Dc * 2.0));
	// bakış noktası da yumuşatılır (Fab haritasında EnuToWorld yer yüksekliği 10 m hücrelerde basamaklı)
	LookS = (LookS.IsZero() || FVector::Dist(LookS, Look) > 60000.0) ? Look : FMath::Lerp(LookS, Look, 1.0 - FMath::Exp(-Dc * 3.0));
	const FRotator WantRot = (LookS - CamPos).Rotation();
	CamRot = FQuat::Slerp(CamRot.Quaternion(), WantRot.Quaternion(), (float)(1.0 - FMath::Exp(-Dc * 4.5))).Rotator();
	CamRot.Roll = 0.f;
	Cam->SetActorLocationAndRotation(CamPos, CamRot);
	if (UCameraComponent* Cc = Cam->GetCameraComponent())
	{
		Cc->PostProcessSettings.DepthOfFieldFocalDistance = (float)FVector::Dist(CamPos, Look);
		Cc->PostProcessSettings.DepthOfFieldFstop = Fstop;
	}
}

UTextureRenderTarget2D* ASmvMenuScene::GetCardTarget()
{
	if (!CardRT)
	{
		CardRT = NewObject<UTextureRenderTarget2D>(this, TEXT("SmvCardRT"));
		CardRT->ClearColor = FLinearColor(0.02f, 0.03f, 0.05f, 1.f);
		CardRT->InitAutoFormat(640, 360);
		CardRT->UpdateResourceImmediate(true);
	}
	if (!CardCap)
	{
		CardCap = NewObject<USceneCaptureComponent2D>(this, TEXT("SmvCardCapture"));
		CardCap->SetupAttachment(Root);
		CardCap->RegisterComponent();
		CardCap->TextureTarget = CardRT;
		CardCap->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
		CardCap->FOVAngle = 70.f;
		CardCap->bCaptureEveryFrame = false;
		CardCap->bCaptureOnMovement = false;
		CardCap->ShowFlags.SetMotionBlur(false);
		CardCap->ShowFlags.SetDepthOfField(false);
	}
	return CardRT;
}

void ASmvMenuScene::SetCardActive(bool bOn)
{
	if (bOn) GetCardTarget();
	bCardOn = bOn && bLive;
	if (CardCap)
	{
		CardCap->bCaptureEveryFrame = bCardOn;
		CardCap->HiddenActors.Reset();
		if (Showcase) CardCap->HiddenActors.Add(Showcase);
		if (ShowPilot) CardCap->HiddenActors.Add(ShowPilot);
	}
}

void ASmvMenuScene::SetCameraFx(const FString& Kind)
{
	if (!Cam || !Cam->GetCameraComponent()) return;
	const FString K = Kind.IsEmpty() ? FString(TEXT("day")) : Kind;
	if (K == CamFx) return;
	FPostProcessSettings& Pp = Cam->GetCameraComponent()->PostProcessSettings;
	if (!bBasePp) { BasePp = Pp; bBasePp = true; }
	// her geçişte temel sinematik ayara dön, sonra kamera türü üstüne yazılır (alan derinliği istasyon kodunda)
	const float Fd = Pp.DepthOfFieldFocalDistance, Fs = Pp.DepthOfFieldFstop;
	Pp = BasePp;
	Pp.DepthOfFieldFocalDistance = Fd; Pp.DepthOfFieldFstop = Fs;
	CamFx = K;
	if (K == TEXT("night"))
	{
		Pp.bOverride_ColorSaturation = true; Pp.ColorSaturation = FVector4(0, 0, 0, 1);
		Pp.bOverride_ColorGain = true; Pp.ColorGain = FVector4(0.35f, 1.f, 0.4f, 1.f);
		Pp.bOverride_AutoExposureBias = true; Pp.AutoExposureBias = 2.2f;
		Pp.bOverride_FilmGrainIntensity = true; Pp.FilmGrainIntensity = 0.55f;
		Pp.bOverride_VignetteIntensity = true; Pp.VignetteIntensity = 0.8f;
	}
	else if (K == TEXT("thermal"))
	{
		Pp.bOverride_ColorSaturation = true; Pp.ColorSaturation = FVector4(0, 0, 0, 1);
		Pp.bOverride_ColorContrast = true; Pp.ColorContrast = FVector4(1.8f, 1.8f, 1.8f, 1.f);
		Pp.bOverride_ColorGamma = true; Pp.ColorGamma = FVector4(0.7f, 0.7f, 0.7f, 1.f);
		Pp.bOverride_FilmGrainIntensity = true; Pp.FilmGrainIntensity = 0.25f;
	}
	else if (K == TEXT("multispectral"))
	{
		if (UTexture2D* Lut = SmvMultispectralLut())
		{
			Pp.bOverride_ColorGradingLUT = true; Pp.ColorGradingLUT = Lut;
			Pp.bOverride_ColorGradingIntensity = true; Pp.ColorGradingIntensity = 1.f;
		}
		Pp.bOverride_ColorSaturation = true; Pp.ColorSaturation = FVector4(1.25f, 1.25f, 1.25f, 1.f);
	}
}
