// Menü sahnesi — görev düzenleyici kipi (kullanıcı 2026-11-01): menü haritası ve araçları gizlenir, görevin evinde gerçek dünya
// (Google Photorealistic 3D Tiles ya da Cesium World Terrain + uydu) kurulur. Kamera ev çevresinde fareyle döner / eğilir / yakınlaşır;
// görev çizgileri kabuk (SSmvShell) tarafından bu görüntünün üstüne izdüşümle çizilir (MissionProject). "Oynat": CAYMAZ görev yolunda uçar.
#include "SmvMenuScene.h"
#include "SimumavModule.h"
#include "SmvWorldActors.h"
#include "SmvPilotFigure.h"
#include "SmvGameInstance.h"
#include "SmvDataService.h"
#include "SmvUiPrefs.h"
#include "CesiumWorldSetup.h"
#include "CaySimTypes.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	int64 SmvMKey(double Lat, double Lon) { return ((int64)FMath::RoundToInt(Lat * 20000.0) << 32) ^ (int64)(uint32)FMath::RoundToInt(Lon * 20000.0); }
}

void ASmvMenuScene::EnterMissionMode(double Lat, double Lon)
{
	UWorld* W = GetWorld();
	if (!W) return;
	if (bMission && FMath::Abs(Lat - MLat) < 1e-7 && FMath::Abs(Lon - MLon) < 1e-7) return;
	if (MissionWorld) { MissionWorld->Destroy(); MissionWorld = nullptr; }
	MissionStop();
	MLat = Lat; MLon = Lon;
	bMHomeGw = false; MHomeGw = 0; MHomeAt = 0;
	MGroundCache.Reset();
	if (!bMission)
	{
		bMission = true;
		// menü sahnesi gizlenir (Fab haritası ve alt seviyeleri, süzülen araçlar, vitrin, çevre) — görev dünyası aynı orijinde kurulur
		if (MenuLevel) MenuLevel->SetShouldBeVisible(false);
		for (const TObjectPtr<ULevelStreamingDynamic>& L : MenuSubs) if (L) L->SetShouldBeVisible(false);
		TArray<AActor*> Hide = { Flyer, Showcase, ShowPilot, Preview, PilotPrev, Terrain, WorldPrev };
		for (const TObjectPtr<ASmvDroneActor>& X : Extras) Hide.Add(X);
		for (const TObjectPtr<AActor>& X : SceneryActors) Hide.Add(X);
		for (AActor* A : Hide) if (A) A->SetActorHiddenInGame(true);
	}
	FCaySimSession S;
	S.Center.Lat = Lat; S.Center.Lon = Lon; S.Center.bHasAlt = false;
	S.World.Mode = TEXT("real");
	S.World.Time = TEXT("day");
	S.World.Fog = 0.02f;
	FActorSpawnParameters Sp;
	Sp.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	MissionWorld = W->SpawnActor<ACaySimWorldSetup>(ACaySimWorldSetup::StaticClass(), FTransform::Identity, Sp);
	if (!MissionWorld) return;
	if (USmvGameInstance* GI = USmvGameInstance::GetSmv(this))
		if (GI->GetData() && !GI->GetData()->Settings().CesiumToken.IsEmpty()) MissionWorld->CesiumIonToken = GI->GetData()->Settings().CesiumToken;
	ECaySimWorldSource Src = ECaySimWorldSource::Flat;
	if (ACaySimWorldSetup::CesiumCompiled() && !MissionWorld->CesiumIonToken.IsEmpty())
		Src = FSmvUiPrefs::Get().RealWorld == TEXT("terrain") ? ECaySimWorldSource::CesiumIon : ECaySimWorldSource::GooglePhotoreal;
	MissionWorld->Build(S, nullptr, Src);
	MissionWorld->DisableBuiltinSky();   // menünün kendi gökyüzü / güneşi kullanılır
	MTarget = MissionWorld->GeoToUnreal(Lon, Lat, 0.0);
	MYaw = 200.f; MPitch = -48.f; MDist = 1500.f;
	UE_LOG(LogSimumav, Log, TEXT("Görev kipi: %s — %.5f, %.5f"), *MissionWorld->SourceName(), Lat, Lon);
}

void ASmvMenuScene::ExitMissionMode()
{
	if (!bMission) return;
	MissionStop();
	if (MissionWorld) { MissionWorld->Destroy(); MissionWorld = nullptr; }
	if (MissionDrone) { MissionDrone->Destroy(); MissionDrone = nullptr; }
	bMission = false;
	if (MenuLevel) MenuLevel->SetShouldBeVisible(true);
	for (const TObjectPtr<ULevelStreamingDynamic>& L : MenuSubs) if (L) L->SetShouldBeVisible(true);
	TArray<AActor*> Show = { Flyer, Terrain };
	for (const TObjectPtr<ASmvDroneActor>& X : Extras) Show.Add(X);
	for (const TObjectPtr<AActor>& X : SceneryActors) Show.Add(X);
	for (AActor* A : Show) if (A) A->SetActorHiddenInGame(false);
	// vitrin / pilot / önizlemeler istasyonlarına göre (SetStation) yeniden gösterilir
	if (Showcase) Showcase->SetActorHiddenInGame(!(Station == EStation::Hangar || Station == EStation::Fly));
	if (ShowPilot) ShowPilot->SetActorHiddenInGame(Station != EStation::Crew);
	for (const TObjectPtr<AActor>& Fg : FogBanks) if (Fg) Fg->SetActorHiddenInGame(IsShowStation(Station));
	bCamInit = false;
}

double ASmvMenuScene::MGroundAt(double Lat, double Lon) const
{
	if (!MissionWorld) return 0.0;
	const int64 K = SmvMKey(Lat, Lon);
	if (const double* C = MGroundCache.Find(K)) return *C;
	double G = 0.0;
	if (MissionWorld->SampleGroundHeight(Lat, Lon, G)) { MGroundCache.Add(K, G); return G; }
	return bMHomeGw ? MHomeGw : 0.0;
}

FVector ASmvMenuScene::MWorld(double Lat, double Lon, double AltRel) const
{
	if (!MissionWorld) return FVector::ZeroVector;
	return MissionWorld->GeoToUnreal(Lon, Lat, (bMHomeGw ? MHomeGw : 0.0) + AltRel);
}

void ASmvMenuScene::MissionOrbit(float DYaw, float DPitch, float Zoom, const FVector2D& PanPx)
{
	MYaw = FMath::Fmod(MYaw + DYaw + 360.f, 360.f);
	MPitch = FMath::Clamp(MPitch + DPitch, -89.f, -6.f);
	MDist = FMath::Clamp(MDist * FMath::Max(0.05f, Zoom), 40.f, 30000.f);
	if (!PanPx.IsNearlyZero())
	{
		const FRotator Yr(0.f, MYaw, 0.f);
		const FVector Right = Yr.RotateVector(FVector::RightVector), Fwd = Yr.RotateVector(FVector::ForwardVector);
		const double K = MDist * 100.0 * 0.0016;   // uzaklıkla ölçeklenen piksel → cm
		MTarget += (-Right * PanPx.X + Fwd * PanPx.Y) * K;
	}
}

void ASmvMenuScene::MissionFocus(double Lat, double Lon, float DistM)
{
	if (!MissionWorld) return;
	MTarget = MissionWorld->GeoToUnreal(Lon, Lat, MGroundAt(Lat, Lon));
	if (DistM > 0.f) MDist = FMath::Clamp(DistM, 40.f, 30000.f);
}

bool ASmvMenuScene::MissionProject(double Lat, double Lon, double AltRel, bool bGround, FVector2D& OutPx) const
{
	if (!MissionWorld) return false;
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!PC) return false;
	const FVector P = bGround ? MissionWorld->GeoToUnreal(Lon, Lat, MGroundAt(Lat, Lon)) : MWorld(Lat, Lon, AltRel);
	return PC->ProjectWorldLocationToScreen(P, OutPx, true);
}

bool ASmvMenuScene::MissionPick(const FVector2D& Px, double& OutLat, double& OutLon) const
{
	UWorld* W = GetWorld();
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!W || !PC || !MissionWorld) return false;
	FVector O, D;
	if (!PC->DeprojectScreenPositionToWorld(Px.X, Px.Y, O, D)) return false;
	FHitResult H;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(SmvMissionPick), true);
	if (MissionDrone) Q.AddIgnoredActor(MissionDrone);
	FVector Hit;
	if (W->LineTraceSingleByChannel(H, O, O + D * 5.0e7, ECC_Visibility, Q)) Hit = H.ImpactPoint;
	else
	{
		// karolar yüklenmemiş / çarpışmasız: ev zemini düzlemiyle kesişim
		const double Z0 = MWorld(MLat, MLon, 0.0).Z;
		if (FMath::Abs(D.Z) < 1e-4) return false;
		const double T = (Z0 - O.Z) / D.Z;
		if (T <= 0) return false;
		Hit = O + D * T;
	}
	double Hm;
	MissionWorld->UnrealToGeo(Hit, OutLon, OutLat, Hm);
	return true;
}

FString ASmvMenuScene::MissionWorldInfo() const
{
	if (!MissionWorld) return FString();
	return MissionWorld->IsReady() ? MissionWorld->SourceName() : FString::Printf(TEXT("%s · %%%.0f"), *MissionWorld->SourceName(), MissionWorld->GetLoadProgress());
}

void ASmvMenuScene::MissionPlay(const TArray<FVector>& LatLonAlt, float SpeedMs)
{
	UWorld* W = GetWorld();
	if (!W || !bMission || LatLonAlt.Num() < 2) return;
	MPath = LatLonAlt;
	MPathCum.Reset();
	double Acc = 0;
	MPathCum.Add(0);
	for (int32 i = 1; i < MPath.Num(); ++i)
	{
		const double Dh = FVector2D((MPath[i].Y - MPath[i - 1].Y) * 111320.0 * FMath::Cos(FMath::DegreesToRadians(MPath[i].X)), (MPath[i].X - MPath[i - 1].X) * 110574.0).Size();
		Acc += FVector2D(Dh, MPath[i].Z - MPath[i - 1].Z).Size();
		MPathCum.Add(Acc);
	}
	MPlaySpeed = FMath::Max(2.f, SpeedMs);
	MPlayT0 = FPlatformTime::Seconds();
	MissionPlayU = 0.f;
	bMissionPlay = true;
	if (!MissionDrone)
	{
		FActorSpawnParameters Sp;
		Sp.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		MissionDrone = W->SpawnActor<ASmvDroneActor>(ASmvDroneActor::StaticClass(), FTransform::Identity, Sp);
		if (MissionDrone)
		{
			MissionDrone->SetPoseFrozen(true);
			MissionDrone->ConfigureVisual(TEXT("plane"), TEXT("smv:caymaz"), FLinearColor(0.9f, 0.9f, 0.92f));
			if (USmvGameInstance* GI = USmvGameInstance::GetSmv(this))
			{
				TWeakObjectPtr<ASmvDroneActor> WD(MissionDrone);
				if (GI->GetData()) GI->GetData()->FetchModelGlb(TEXT("smv:caymaz"), [WD](bool bOk, const TArray<uint8>& Bytes) { if (bOk && WD.IsValid()) WD->LoadGlbBytes(Bytes); });
			}
		}
	}
	if (MissionDrone) MissionDrone->SetActorHiddenInGame(false);
}

void ASmvMenuScene::MissionStop()
{
	bMissionPlay = false;
	MissionPlayU = 0.f;
	if (MissionDrone) MissionDrone->SetActorHiddenInGame(true);
}

void ASmvMenuScene::TickMission(float Dt)
{
	if (!MissionWorld || !Cam) return;
	const double Now = FPlatformTime::Seconds();
	// ev zemini: karolar yüklendikçe ölçülür (ilk 30 sn her 1,5 sn)
	if ((!bMHomeGw || Now - MHomeAt < 30.0) && Now - MHomeAt > 1.5)
	{
		if (MHomeAt == 0) MHomeAt = Now;
		double G;
		if (MissionWorld->SampleGroundHeight(MLat, MLon, G))
		{
			const bool bFirst = !bMHomeGw;
			MHomeGw = G; bMHomeGw = true;
			if (bFirst) { MTarget = MissionWorld->GeoToUnreal(MLon, MLat, G); MGroundCache.Reset(); }
		}
		else if (!bMHomeGw) MHomeAt = Now - 1.0;   // tekrar dene
	}
	// yörünge kamerası
	const FRotator R(MPitch, MYaw, 0.f);
	const FVector At = MTarget - R.Vector() * MDist * 100.0;
	CamPos = At; CamRot = R;
	Cam->SetActorLocationAndRotation(At, R);
	if (UCameraComponent* Cc = Cam->GetCameraComponent())
	{
		Cc->PostProcessSettings.bOverride_DepthOfFieldFstop = true;
		Cc->PostProcessSettings.DepthOfFieldFstop = 32.f;
		Cc->PostProcessSettings.DepthOfFieldFocalDistance = MDist * 100.f;
	}
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
		if (PC->GetViewTarget() != Cam) { PC->bAutoManageActiveCameraTarget = false; PC->SetViewTarget(Cam); }
	// oynatma: yol boyunca sabit hız (yatay + dikey); burun bir sonraki noktaya
	if (bMissionPlay && MissionDrone && MPath.Num() >= 2 && MPathCum.Num() == MPath.Num())
	{
		const double Total = FMath::Max(1.0, MPathCum.Last());
		const double S = (Now - MPlayT0) * MPlaySpeed;
		if (S >= Total) { MissionStop(); return; }
		MissionPlayU = (float)(S / Total);
		int32 i = 1;
		while (i < MPathCum.Num() - 1 && MPathCum[i] < S) ++i;
		const double Seg = FMath::Max(1e-3, MPathCum[i] - MPathCum[i - 1]);
		const double U = FMath::Clamp((S - MPathCum[i - 1]) / Seg, 0.0, 1.0);
		const FVector A = MWorld(MPath[i - 1].X, MPath[i - 1].Y, MPath[i - 1].Z), B = MWorld(MPath[i].X, MPath[i].Y, MPath[i].Z);
		const FVector P = FMath::Lerp(A, B, U);
		FRotator Rd = (B - A).Rotation();
		Rd.Pitch = FMath::Clamp(Rd.Pitch, -25.f, 25.f);
		MissionDrone->SetActorLocationAndRotation(P, Rd);
	}
}
