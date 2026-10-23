#include "SmvMenuScene.h"
#include "SimumavModule.h"
#include "SmvWorldActors.h"
#include "SmvGameInstance.h"
#include "SmvDataService.h"
#include "SmvMotorSynth.h"
#include "SmvUiPrefs.h"
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
	Terrain = W->SpawnActor<ASmvProcTerrain>();
	if (Terrain) Terrain->Build(Tr, GroundMsl, WorldSetup);

	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Flyer = W->SpawnActor<ASmvDroneActor>(ASmvDroneActor::StaticClass(), FTransform::Identity, P);
	if (Flyer)
	{
		Flyer->Setup(S, nullptr);
		Flyer->ConfigureVisual(TEXT("plane"), FString(), FLinearColor(0.92f, 0.94f, 0.97f));
		Flyer->SetActorTickEnabled(false);   // konumu biz sürüyoruz (telemetri yok)
		if (USmvMotorSynth* M = Flyer->GetMotor()) M->Stop();
	}
	Cam = W->SpawnActor<ACameraActor>(FVector::ZeroVector, FRotator::ZeroRotator, P);
	if (Cam && Cam->GetCameraComponent())
	{
		Cam->GetCameraComponent()->SetFieldOfView(62.f);
		Cam->GetCameraComponent()->bConstrainAspectRatio = false;
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
	if (Preview) Preview->Destroy();
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
	if (!Preview) return;
	TInlineComponentArray<UPrimitiveComponent*> Prims(Preview);
	for (UPrimitiveComponent* P : Prims) if (P) { P->SetLightingChannels(false, true, false); P->SetCastShadow(false); }
}

void ASmvMenuScene::SetPreviewActive(bool bOn)
{
	bPreviewOn = bOn && FSmvUiPrefs::Get().bPreview3d;
	Capture->bCaptureEveryFrame = bPreviewOn && Preview != nullptr;
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
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
		{
			if (PC->GetViewTarget() != Cam)
			{
				PC->bAutoManageActiveCameraTarget = false;
				PC->SetViewTarget(Cam);
			}
		}
	}
	if (Preview)
	{
		PreviewYaw = FMath::Fmod(PreviewYaw + Dt * 24.f, 360.f);
		Preview->SetActorLocationAndRotation(GSmvPreviewAt, FRotator(0.f, PreviewYaw, 0.f));
		const FVector CamAt = GSmvPreviewAt + FVector(-PreviewDistCm, 0.0, PreviewDistCm * 0.32);
		Capture->SetWorldLocationAndRotation(CamAt, (GSmvPreviewAt - CamAt).Rotation());
		KeyLight->SetWorldLocation(GSmvPreviewAt + FVector(-PreviewDistCm * 0.6, PreviewDistCm * 0.7, PreviewDistCm * 0.8));
		RimLight->SetWorldLocation(GSmvPreviewAt + FVector(PreviewDistCm * 0.8, -PreviewDistCm * 0.5, PreviewDistCm * 0.4));
		const double Now = FPlatformTime::Seconds();
		if (Now - LastChannelFix > 1.0) { LastChannelFix = Now; ApplyPreviewChannels(); }
	}
}
