#include "SmvMenuScene.h"
#include "SimumavModule.h"
#include "SmvWorldActors.h"
#include "SmvGameInstance.h"
#include "SmvDataService.h"
#include "SmvMotorSynth.h"
#include "SmvUiPrefs.h"
#include "SmvPilotFigure.h"
#include "CesiumWorldSetup.h"
#include "CaySimTypes.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/PointLightComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ConfigCacheIni.h"
#include "SmvFabContent.h"
#include "Engine/LevelStreamingDynamic.h"

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
	Capture->ShowFlags.SetMotionBlur(false);
	FPostProcessSettings& PP = Capture->PostProcessSettings;
	PP.bOverride_AutoExposureMethod = true;
	PP.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	PP.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	PP.AutoExposureApplyPhysicalCameraExposure = false;
	PP.bOverride_AutoExposureBias = true;
	PP.AutoExposureBias = SmvCfg(TEXT("PreviewExposure"), 2.f);
	Capture->PostProcessBlendWeight = 1.f;
	SnapCap->ShowFlags = Capture->ShowFlags;
	SnapCap->PostProcessSettings = Capture->PostProcessSettings;
	SnapCap->PostProcessBlendWeight = 1.f;
	for (UPointLightComponent* Lt : { KeyLight.Get(), RimLight.Get() })
	{
		Lt->SetIntensityUnits(ELightUnits::Candelas);
		Lt->SetAttenuationRadius(2500.f);
		Lt->SetCastShadows(false);
		Lt->SetLightingChannels(false, true, false);
	}
	KeyLight->SetIntensity(SmvCfg(TEXT("PreviewLight"), 60.f));
	KeyLight->SetLightColor(FLinearColor(1.f, 0.96f, 0.9f));
	RimLight->SetIntensity(SmvCfg(TEXT("PreviewLight"), 60.f) * 0.7f);
	RimLight->SetLightColor(FLinearColor(0.35f, 0.75f, 1.f));

	// ---- canlı dünya ----
	if (FSmvUiPrefs::Get().bMenuScene) BuildWorld();
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
	Tr.TimeOfDay = TEXT("dusk");
	Tr.World.Seed = 7;
	Tr.World.SizeM = 4000.0;
	Tr.World.MountainCount = 6;
	Tr.World.MountainHeight = 260.0;
	Tr.World.TreeDensity = 0.35;
	Tr.World.BuildingCount = 30;
	Tr.World.bRunway = true;
	Tr.World.RunwayHeading = 70.0;
	Tr.World.Water = 0.12;
	Tr.World.Fog = 0.25;
	// Fab haritası (ini MenuMap = anahtar; boş → askeri üs / rafineri / orman önceliği) — yoksa prosedürel arazi
	{
		FString Want;
		if (GConfig) GConfig->GetString(GSmvMenuSection, TEXT("MenuMap"), Want, GGameIni);
		const FSmvFabMap* Pick = nullptr;
		const TArray<FSmvFabMap>& Maps = FSmvFabContent::Maps();
		for (const FSmvFabMap& M : Maps) if (M.Kind == ESmvMapKind::Level && !M.Level.IsEmpty() && M.Key == Want) Pick = &M;
		static const TCHAR* Pref[] = { TEXT("askeri"), TEXT("military"), TEXT("base"), TEXT("rafineri"), TEXT("refinery"), TEXT("orman"), TEXT("forest") };
		for (const TCHAR* Pf : Pref) { if (Pick || Want == TEXT("none")) break; for (const FSmvFabMap& M : Maps) if (M.Kind == ESmvMapKind::Level && !M.Level.IsEmpty() && M.Key.Contains(Pf)) { Pick = &M; break; } }
		if (Pick)
		{
			bool bOk = false;
			MenuLevel = ULevelStreamingDynamic::LoadLevelInstance(W, Pick->Level, FVector::ZeroVector, FRotator::ZeroRotator, bOk);
			bFabMap = bOk && MenuLevel;
			FabStartE = Pick->StartE; FabStartN = Pick->StartN;
			UE_LOG(LogTemp, Log, TEXT("Menü sahnesi Fab haritası: %s (%s)"), *Pick->Level, bFabMap ? TEXT("yükleniyor") : TEXT("yüklenemedi"));
		}
	}
	if (!bFabMap)
	{
		Terrain = W->SpawnActor<ASmvProcTerrain>();
		if (Terrain) Terrain->Build(Tr, GroundMsl, WorldSetup);
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
		Pp.bOverride_DepthOfFieldFstop = true; Pp.DepthOfFieldFstop = 2.2f;
		Pp.bOverride_DepthOfFieldSensorWidth = true; Pp.DepthOfFieldSensorWidth = 36.f;
		Pp.bOverride_DepthOfFieldFocalDistance = true; Pp.DepthOfFieldFocalDistance = 2000.f;
		Pp.bOverride_BloomIntensity = true; Pp.BloomIntensity = 1.1f;
		Pp.bOverride_VignetteIntensity = true; Pp.VignetteIntensity = 0.55f;
		Pp.bOverride_FilmGrainIntensity = true; Pp.FilmGrainIntensity = 0.12f;
		Pp.bOverride_ColorSaturation = true; Pp.ColorSaturation = FVector4(1.f, 1.f, 1.f, 1.08f);
		Pp.bOverride_ColorGainShadows = true; Pp.ColorGainShadows = FVector4(0.92f, 0.98f, 1.08f, 1.f);
		Pp.bOverride_ColorGainHighlights = true; Pp.ColorGainHighlights = FVector4(1.06f, 1.0f, 0.92f, 1.f);
		Pp.bOverride_SceneFringeIntensity = true; Pp.SceneFringeIntensity = 0.6f;
		Cc->PostProcessBlendWeight = 1.f;
	}
	bLive = Flyer && Cam;
	UE_LOG(LogSimumav, Log, TEXT("Menü 3B sahnesi kuruldu (%s)"), bLive ? TEXT("canlı") : TEXT("eksik"));
}

void ASmvMenuScene::EndPlay(const EEndPlayReason::Type Reason)
{
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
	if (MenuLevel) { MenuLevel->SetShouldBeLoaded(false); MenuLevel->SetShouldBeVisible(false); MenuLevel->SetIsRequestingUnloadAndRemoval(true); MenuLevel = nullptr; }
	if (Preview) Preview->Destroy();
	if (PilotPrev) PilotPrev->Destroy();
	if (SnapFig) SnapFig->Destroy();
	if (DSnapActor) DSnapActor->Destroy();
	if (WorldPrev) WorldPrev->Destroy();
	if (Cam) Cam->Destroy();
	if (Terrain) Terrain->Destroy();
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
		const double Lat = Lat0 + FMath::RadiansToDegrees((N + FabStartN) / 6378137.0);
		const double Lon = Lon0 + FMath::RadiansToDegrees((E + FabStartE) / (6378137.0 * FMath::Cos(FMath::DegreesToRadians(Lat0))));
		FVector P = WorldSetup->GeoToUnreal(Lon, Lat, WorldSetup->MslToWorldHeight(Lat, Lon, GroundMsl));
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
	PreviewDistCm = FMath::Clamp(Preview->GetVisualRadiusM() * 100.f * 3.4f, 140.f, 2400.f);
	Capture->ShowOnlyActors.Reset();
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
	PilotPrev->Build(A, false);
	Capture->ShowOnlyActors.Reset();
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
	const double T = FPlatformTime::Seconds() - T0;
	const double Period = 54.0;
	const double U = FMath::Fmod(T, Period);
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Fwd).GetSafeNormal();
	FVector Want;
	FVector Look = D;
	if (U < 18.0)
	{
		Want = D - Fwd * 1400.0 + FVector(0, 0, 380.0) + Right * (220.0 * FMath::Sin(T * 0.3));
	}
	else if (U < 30.0)
	{
		const double K = (U - 18.0) / 12.0;
		Want = D + Right * 1700.0 - Fwd * FMath::Lerp(900.0, -900.0, K) + FVector(0, 0, 150.0);
	}
	else if (U < 44.0)
	{
		const double A = (U - 30.0) * 0.35;
		Want = D + FVector(FMath::Cos(A) * 1800.0, FMath::Sin(A) * 1800.0, 520.0);
	}
	else
	{
		// yerden izleme: sabit nokta (kalkış alanı yanı)
		Want = EnuToWorld(120.0, -160.0, 1.7);
		Look = D;
	}
	if (!bCamInit || FVector::Dist(Want, CamPos) > 60000.0) { CamPos = Want; bCamInit = true; }
	const float K = FMath::Clamp(Dt * 2.2f, 0.f, 1.f);
	CamPos = FMath::Lerp(CamPos, Want, (double)K);
	const FRotator WantRot = (Look - CamPos).Rotation();
	CamRot = FMath::RInterpTo(CamRot, WantRot, Dt, 5.f);
	Cam->SetActorLocationAndRotation(CamPos, CamRot);
}

void ASmvMenuScene::Tick(float Dt)
{
	Super::Tick(Dt);
	const double T = FPlatformTime::Seconds() - T0;
	if (bLive && Flyer && Cam)
	{
		// Gerono lemniskatı (web flightcore.js flightPath): rx 900, ry 520 m, periyot 84 sn, 46 m AGL
		const double Wv = UE_DOUBLE_TWO_PI / 84.0;
		auto Path = [Wv](double t, double& E, double& N) { const double A = t * Wv; E = 900.0 * FMath::Cos(A); N = 520.0 * FMath::Sin(A) * FMath::Cos(A); };
		double E0, N0, E1, N1, E2, N2;
		Path(T, E0, N0); Path(T + 0.5, E1, N1); Path(T + 1.0, E2, N2);
		const double Agl = 46.0 + 7.0 * FMath::Sin(T * 0.37);
		const FVector P0 = EnuToWorld(E0, N0, Agl), P1 = EnuToWorld(E1, N1, Agl), P2 = EnuToWorld(E2, N2, Agl);
		const FVector Fwd = (P1 - P0).GetSafeNormal();
		const FVector Fwd2 = (P2 - P1).GetSafeNormal();
		const float Turn = (float)FMath::RadiansToDegrees(FMath::Atan2(FVector::CrossProduct(Fwd, Fwd2).Z, FVector::DotProduct(Fwd, Fwd2)));
		FRotator R = Fwd.Rotation();
		R.Roll = FMath::Clamp(Turn * 9.f, -40.f, 40.f);
		Flyer->SetActorLocationAndRotation(P0, R);
		UpdateCamera(Dt, P0, Fwd);
		// kopter: kalkış alanı yakınında yavaş yörünge (18 m, 25 sn); VTOL: geniş tur (1,4 km, 120 sn) yüksekte
		if (Extras.Num() > 0 && Extras[0])
		{
			const double A = T * UE_DOUBLE_TWO_PI / 25.0;
			const FVector Q0 = EnuToWorld(140.0 + 18.0 * FMath::Cos(A), -120.0 + 18.0 * FMath::Sin(A), 14.0 + 3.0 * FMath::Sin(T * 0.6));
			Extras[0]->SetActorLocationAndRotation(Q0, FRotator(-6.0 * FMath::Cos(T * 0.5), FMath::RadiansToDegrees(A) + 90.0, 4.0 * FMath::Sin(A)));
		}
		if (Extras.Num() > 1 && Extras[1])
		{
			const double A = T * UE_DOUBLE_TWO_PI / 120.0;
			const FVector V0 = EnuToWorld(1400.0 * FMath::Cos(A), 900.0 * FMath::Sin(A), 140.0), V1 = EnuToWorld(1400.0 * FMath::Cos(A + 0.01), 900.0 * FMath::Sin(A + 0.01), 140.0);
			FRotator Rv = (V1 - V0).Rotation(); Rv.Roll = -18.0;
			Extras[1]->SetActorLocationAndRotation(V0, Rv);
		}
		if (Cam && Cam->GetCameraComponent()) Cam->GetCameraComponent()->PostProcessSettings.DepthOfFieldFocalDistance = (float)FVector::Dist(CamPos, P0);
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
		{
			if (PC->GetViewTarget() != Cam)
			{
				PC->bAutoManageActiveCameraTarget = false;
				PC->SetViewTarget(Cam);
			}
		}
	}
	TickSnaps();
	if (WorldPrev && WorldSetup && !Preview && !PilotPrev)
	{
		// dünya önizlemesi: merkez çevresinde yavaş yörünge (eğik kuş bakışı)
		if (FPlatformTime::Seconds() > ManualUntil) PreviewYaw = FMath::Fmod(PreviewYaw + Dt * 6.f, 360.f);
		const FVector Ctr = WorldSetup->GeoToUnreal(WpLon, WpLat, WorldSetup->MslToWorldHeight(WpLat, WpLon, GroundMsl));
		const double R = WpSize * 100.0 * 0.62, A = FMath::DegreesToRadians(PreviewYaw);
		const FVector At = Ctr + FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, WpSize * 100.0 * 0.32);
		Capture->FOVAngle = 55.f;
		Capture->SetWorldLocationAndRotation(At, (Ctr - At).Rotation());
	}
	if (PilotPrev)
	{
		if (FPlatformTime::Seconds() > ManualUntil) PreviewYaw = FMath::Fmod(PreviewYaw + Dt * 14.f, 360.f);
		FaceK = FMath::FInterpTo(FaceK, bFaceZoom ? 1.f : 0.f, Dt, 4.f);
		PilotPrev->SetActorLocationAndRotation(GSmvPreviewAt, FRotator(0.f, 180.f + PreviewYaw, 0.f));
		const FVector Target = GSmvPreviewAt + FVector(0, 0, FMath::Lerp(95.f, PilotPrev->HeadHeightCm(), FaceK));
		const float Dist = FMath::Lerp(330.f, 75.f, FaceK);
		const FVector CamAt = Target + FVector(-Dist, 0.0, FMath::Lerp(20.f, 4.f, FaceK));
		Capture->FOVAngle = 28.f;
		Capture->SetWorldLocationAndRotation(CamAt, (Target - CamAt).Rotation());
		KeyLight->SetWorldLocation(GSmvPreviewAt + FVector(-180, 160, 220));
		RimLight->SetWorldLocation(GSmvPreviewAt + FVector(160, -120, 200));
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
		const FVector CamAt = GSmvPreviewAt + FVector(-PreviewDistCm, 0.0, PreviewDistCm * 0.32);
		Capture->SetWorldLocationAndRotation(CamAt, (GSmvPreviewAt - CamAt).Rotation());
		KeyLight->SetWorldLocation(GSmvPreviewAt + FVector(-PreviewDistCm * 0.6, PreviewDistCm * 0.7, PreviewDistCm * 0.8));
		RimLight->SetWorldLocation(GSmvPreviewAt + FVector(PreviewDistCm * 0.8, -PreviewDistCm * 0.5, PreviewDistCm * 0.4));
		const double Now = FPlatformTime::Seconds();
		if (Now - LastChannelFix > 1.0) { LastChannelFix = Now; ApplyPreviewChannels(); }
	}
}

// ---------------------------------------------------------------------------
// avatar anlık görüntüleri (TOP10 vb.): kare N'de figür kurulur, N+1'de yakalanır
// ---------------------------------------------------------------------------

UTextureRenderTarget2D* ASmvMenuScene::AvatarSnap(const FSmvAvatar& A)
{
	const FString Key = SmvJson::ToString(A.ToJson());
	if (TObjectPtr<UTextureRenderTarget2D>* Found = Snaps.Find(Key)) return Found->Get();
	if (Snaps.Num() > 64) return nullptr;
	UTextureRenderTarget2D* Rt = NewObject<UTextureRenderTarget2D>(this);
	Rt->ClearColor = FLinearColor(0.f, 0.f, 0.f, 0.f);
	Rt->InitAutoFormat(160, 160);
	Rt->UpdateResourceImmediate(true);
	Snaps.Add(Key, Rt);
	SnapQueue.Add(TPair<FString, FSmvAvatar>(Key, A));
	return Rt;
}

void ASmvMenuScene::TickSnaps()
{
	UWorld* W = GetWorld();
	if (!W) return;
	if (SnapPhase == 1 && SnapFig)
	{
		if (TObjectPtr<UTextureRenderTarget2D>* Rt = Snaps.Find(SnapPending))
		{
			const FVector At = GSmvPreviewAt + FVector(0.0, 0.0, SnapFig->HeadHeightCm() - 14.0);
			const FVector CamAt = At + FVector(-125.0, 0.0, 6.0);
			SnapCap->TextureTarget = Rt->Get();
			SnapCap->SetWorldLocationAndRotation(CamAt, (At - CamAt).Rotation());
			KeyLight->SetWorldLocation(GSmvPreviewAt + FVector(-180, 160, 220));
			RimLight->SetWorldLocation(GSmvPreviewAt + FVector(160, -120, 200));
			SnapCap->ShowOnlyActors.Reset();
			SnapCap->ShowOnlyActors.Add(SnapFig);
			SnapCap->CaptureScene();
		}
		SnapFig->SetActorHiddenInGame(true);
		SnapPhase = 0;
		return;
	}
	if (SnapQueue.Num() == 0)
	{
		TickDroneSnaps();
		return;
	}
	const TPair<FString, FSmvAvatar> Job = SnapQueue[0];
	SnapQueue.RemoveAt(0);
	if (!SnapFig)
	{
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SnapFig = W->SpawnActor<ASmvPilotFigure>(ASmvPilotFigure::StaticClass(), FTransform(GSmvPreviewAt), P);
	}
	if (!SnapFig) return;
	SnapFig->Build(Job.Value, false);
	SnapFig->SetActorHiddenInGame(false);
	SnapFig->SetActorLocationAndRotation(GSmvPreviewAt, FRotator(0.f, 200.f, 0.f));
	TInlineComponentArray<UPrimitiveComponent*> Prims(SnapFig.Get());
	for (UPrimitiveComponent* Pc : Prims) if (Pc) { Pc->SetLightingChannels(false, true, false); Pc->SetCastShadow(false); }
	SnapPending = Job.Key;
	SnapPhase = 1;
}

void ASmvMenuScene::PlayLanding() { LandAt = FPlatformTime::Seconds(); }

UTextureRenderTarget2D* ASmvMenuScene::DroneSnap(const FString& Type, const FString& ModelKey, const FString& ColorHex)
{
	const FString Key = TEXT("d|") + Type + TEXT("|") + ModelKey + TEXT("|") + ColorHex;
	if (TObjectPtr<UTextureRenderTarget2D>* Found = Snaps.Find(Key)) return Found->Get();
	if (Snaps.Num() > 96) return nullptr;
	UTextureRenderTarget2D* Rt = NewObject<UTextureRenderTarget2D>(this);
	Rt->ClearColor = FLinearColor(0.f, 0.f, 0.f, 0.f);
	Rt->InitAutoFormat(240, 150);
	Rt->UpdateResourceImmediate(true);
	Snaps.Add(Key, Rt);
	DQueue.Add({ Key, Type, ModelKey, ColorHex });
	return Rt;
}

void ASmvMenuScene::TickDroneSnaps()
{
	UWorld* W = GetWorld();
	if (!W) return;
	if (DSnapActor)
	{
		++DFrames;
		// 20. karede (model gelmeden siluet) ve 150. karede (GLB yüklendiyse model) çekilir
		if (DFrames == 20 || DFrames == 150)
		{
			if (TObjectPtr<UTextureRenderTarget2D>* Rt = Snaps.Find(DPending))
			{
				const FVector At = GSmvPreviewAt;
				const float D = FMath::Clamp(DSnapActor->GetVisualRadiusM() * 100.f * 3.0f, 120.f, 2400.f);
				const FVector CamAt = At + FVector(-D * 0.86, -D * 0.5, D * 0.36);
				DSnapActor->SetActorHiddenInGame(false);
				SnapCap->TextureTarget = Rt->Get();
				SnapCap->SetWorldLocationAndRotation(CamAt, (At - CamAt).Rotation());
				KeyLight->SetWorldLocation(At + FVector(-D * 0.6, D * 0.7, D * 0.8));
				RimLight->SetWorldLocation(At + FVector(D * 0.8, -D * 0.5, D * 0.4));
				SnapCap->ShowOnlyActors.Reset();
				SnapCap->ShowOnlyActors.Add(DSnapActor);
				SnapCap->CaptureScene();
			}
		}
		if (DFrames >= 150) { DSnapActor->Destroy(); DSnapActor = nullptr; }
		return;
	}
	if (DQueue.Num() == 0) return;
	const FDroneJob Job = DQueue[0];
	DQueue.RemoveAt(0);
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	DSnapActor = W->SpawnActor<ASmvDroneActor>(ASmvDroneActor::StaticClass(), FTransform(FRotator(0.f, 25.f, 0.f), GSmvPreviewAt), P);
	if (!DSnapActor) return;
	FCaySimSession S;
	S.Type = SmvMenuCaysimType(Job.Type);
	S.Model.Orient.Forward = TEXT("+x");
	S.Model.Orient.Up = TEXT("+y");
	S.Model.Orient.Scale = 1.f;
	DSnapActor->Setup(S, nullptr);
	DSnapActor->ConfigureVisual(Job.Type, Job.Model, SmvHexColor(Job.Color));
	DSnapActor->SetActorTickEnabled(false);
	DSnapActor->SetActorLocationAndRotation(GSmvPreviewAt, FRotator(0.f, 25.f, 0.f));
	if (USmvMotorSynth* M = DSnapActor->GetMotor()) M->Stop();
	TInlineComponentArray<UPrimitiveComponent*> Prims(DSnapActor.Get());
	for (UPrimitiveComponent* Pc : Prims) if (Pc) { Pc->SetLightingChannels(false, true, false); Pc->SetCastShadow(false); }
	DPending = Job.Key;
	DFrames = 0;
	if (USmvGameInstance* GI = USmvGameInstance::GetSmv(this))
	{
		TWeakObjectPtr<ASmvDroneActor> WP(DSnapActor);
		if (GI->GetData() && !Job.Model.IsEmpty())
			GI->GetData()->FetchModelGlb(Job.Model, [WP](bool bOk, const TArray<uint8>& Bytes)
			{
				if (bOk && WP.IsValid())
				{
					WP->LoadGlbBytes(Bytes);
					TInlineComponentArray<UPrimitiveComponent*> Pr(WP.Get());
					for (UPrimitiveComponent* Pc : Pr) if (Pc) { Pc->SetLightingChannels(false, true, false); Pc->SetCastShadow(false); }
				}
			});
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
	const FString Model = USmvDataService::DefaultModelFor(Type);
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
	WorldPrev->Build(T, GroundMsl, WorldSetup);
	Capture->ShowOnlyActors.Reset();
	Capture->ShowOnlyActors.Add(WorldPrev);
	// arazinin oluşturduğu alt aktörler (ağaç / bina ISM'leri ayrı aktörse) de görünsün
	TArray<AActor*> Att;
	WorldPrev->GetAttachedActors(Att, true);
	for (AActor* A : Att) Capture->ShowOnlyActors.Add(A);
	Capture->bCaptureEveryFrame = bPreviewOn;
}
