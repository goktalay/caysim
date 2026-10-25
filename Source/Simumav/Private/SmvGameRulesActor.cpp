#include "SmvGameRulesActor.h"
#include "SimumavModule.h"
#include "SmvWorldActors.h"
#include "SmvDataService.h"
#include "SmvFlow.h"
#include "SmvI18n.h"
#include "SmvUiKit.h"
#include "SmvUiPrefs.h"
#include "SmvGameRules.h"
#include "SmvTargets.h"
#include "SmvFabContent.h"
#include "Misc/ConfigCacheIni.h"
#include "CaySimTypes.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SWeakWidget.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"

using namespace SmvUi;

// ---------------------------------------------------------------------------
// multispektral renk tablosu (sahte renk: NIR ≈ yeşil kanal → kırmızı; bitki örtüsü kırmızı-eflatun, beton / su camgöbeği)
// ---------------------------------------------------------------------------

UTexture2D* SmvMultispectralLut()
{
	static TStrongObjectPtr<UTexture2D> Lut;
	if (Lut.IsValid()) return Lut.Get();
	UTexture2D* T = UTexture2D::CreateTransient(256, 16, PF_B8G8R8A8);
	if (!T) return nullptr;
	T->SRGB = false;
	T->Filter = TF_Bilinear;
	T->LODGroup = TEXTUREGROUP_ColorLookupTable;
	T->AddressX = TA_Clamp; T->AddressY = TA_Clamp;
	FTexture2DMipMap& Mip = T->GetPlatformData()->Mips[0];
	FColor* Px = (FColor*)Mip.BulkData.Lock(LOCK_READ_WRITE);
	for (int32 y = 0; y < 16; ++y)
	{
		for (int32 x = 0; x < 256; ++x)
		{
			const float R = (x % 16) / 15.f, G = y / 15.f, B = (x / 16) / 15.f;
			// renk kızılötesi (CIR) benzeri: çıkış (NIR, R, G); NIR ≈ yeşil baskınlığı + parlaklık
			const float Veg = FMath::Clamp((G - FMath::Max(R, B)) * 2.2f + 0.15f, 0.f, 1.f);
			const float Lum = 0.2126f * R + 0.7152f * G + 0.0722f * B;
			const float Nir = FMath::Clamp(Lum * 0.55f + Veg * 0.75f, 0.f, 1.f);
			const FLinearColor C(Nir, FMath::Clamp(R * 0.85f + Lum * 0.1f, 0.f, 1.f), FMath::Clamp(G * 0.55f + B * 0.35f, 0.f, 1.f));
			Px[y * 256 + x] = FColor((uint8)FMath::RoundToInt(C.R * 255.f), (uint8)FMath::RoundToInt(C.G * 255.f), (uint8)FMath::RoundToInt(C.B * 255.f), 255);
		}
	}
	Mip.BulkData.Unlock();
	T->UpdateResource();
	Lut.Reset(T);
	return T;
}

// ---------------------------------------------------------------------------

struct ASmvGameRulesActor::FImpl
{
	SmvCore::EGameKind Kind = SmvCore::EGameKind::Free;
	SmvCore::FDropGame Drop;
	SmvCore::FSprayGame Spray;
	SmvCore::FDefenseGame Def;
	TMap<int32, int32> ThreatIndex;
	int32 Released = 0;
	double LastSprayCmd = -10;
	bool bSprayCmdOn = false;
};

namespace
{
	const TCHAR* GSmvGrCube = TEXT("/Engine/BasicShapes/Cube.Cube");
	const TCHAR* GSmvGrCyl = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	const TCHAR* GSmvGrCone = TEXT("/Engine/BasicShapes/Cone.Cone");
	const TCHAR* GSmvGrSphere = TEXT("/Engine/BasicShapes/Sphere.Sphere");

	UMaterialInterface* GrLoadMat(const TCHAR* Path)
	{
		const FString P(Path);
		const FString Pkg = P.Left(P.Find(TEXT(".")));
		if (!FPackageName::DoesPackageExist(Pkg)) return nullptr;
		return LoadObject<UMaterialInterface>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
}

ASmvGameRulesActor::ASmvGameRulesActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(GSmvGrCube);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cyl(GSmvGrCyl);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(GSmvGrCone);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sph(GSmvGrSphere);
	CubeMesh = Cube.Object; CylMesh = Cyl.Object; ConeMesh = Cone.Object; SphereMesh = Sph.Object;
}

FVector ASmvGameRulesActor::Ground(double InE, double InN, double Lift) const
{
	if (!ToWorld) return FVector::ZeroVector;
	const FVector Hi = ToWorld(InE, InN, 400.0), Lo = ToWorld(InE, InN, -400.0);
	FHitResult Hit;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(SmvRulesGround), true);
	Q.AddIgnoredActor(this);
	if (Drone) Q.AddIgnoredActor(Drone);
	for (const TObjectPtr<ASmvDroneActor>& T : ThreatActors) if (T) Q.AddIgnoredActor(T);
	if (GetWorld() && GetWorld()->LineTraceSingleByChannel(Hit, Hi, Lo, ECC_Visibility, Q))
		return Hit.ImpactPoint + FVector(0, 0, Lift * 100.0);
	return ToWorld(InE, InN, Lift);
}

UStaticMeshComponent* ASmvGameRulesActor::AddPart(UStaticMesh* M, const FVector& At, const FVector& Scale, const FRotator& Rot, const FLinearColor& C, bool bGlow, float Glow)
{
	if (!M) return nullptr;
	UStaticMeshComponent* P = NewObject<UStaticMeshComponent>(this);
	P->SetStaticMesh(M);
	P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	P->SetupAttachment(Root);
	P->RegisterComponent();
	P->SetWorldLocationAndRotation(At, Rot);
	P->SetWorldScale3D(Scale);
	P->SetCastShadow(!bGlow);
	UMaterialInterface* Base = bGlow && GlowMat ? GlowMat.Get() : SmvSolidMat(M->GetMaterial(0));
	if (Base)
	{
		UMaterialInstanceDynamic* D = UMaterialInstanceDynamic::Create(Base, P);
		if (bGlow && GlowMat) { D->SetVectorParameterValue(TEXT("Color"), C); D->SetScalarParameterValue(TEXT("Glow"), Glow); }
		else SmvSetSolidColor(D, C);
		P->SetMaterial(0, D);
	}
	Parts.Add(P);
	return P;
}

void ASmvGameRulesActor::Emit(const FString& Key, int32 Sev, const TMap<FString, FString>& Args)
{
	if (OnEvent) OnEvent(FSmvI18n::Format(Key, Args).ToString(), Sev);
}

void ASmvGameRulesActor::Setup(const FSmvFlightRequest& InReq, USmvFlow* InFlow, ASmvDroneActor* InDrone, TFunction<FVector(double, double, double)> InToWorld)
{
	Req = InReq;
	Flow = InFlow;
	Drone = InDrone;
	ToWorld = MoveTemp(InToWorld);
	Mode = Req.GameMode;
	Impl = MakeShared<FImpl>();
	Impl->Kind = SmvCore::GameKindFromId(std::string(TCHAR_TO_UTF8(*Mode)));
	GlowMat = GrLoadMat(TEXT("/Game/Simumav/Materials/M_SmvGlow.M_SmvGlow"));
	FxMat = GrLoadMat(TEXT("/Game/Simumav/Materials/M_SmvFx.M_SmvFx"));
	switch (Impl->Kind)
	{
	case SmvCore::EGameKind::CargoDrop: BuildDrop(); break;
	case SmvCore::EGameKind::Spraying: BuildSpray(); break;
	case SmvCore::EGameKind::FacilityDefense: BuildDefense(false); break;
	case SmvCore::EGameKind::ShipDefense: BuildDefense(true); break;
	default: break;
	}
	if (Drone) Drone->SetPayloadAttached(true);
	StartAt = FPlatformTime::Seconds();
	BuildHud();
	PushResult();
	UE_LOG(LogSimumav, Log, TEXT("Oyun türü kuruldu: %s"), *Mode);
}

// ---------------------------------------------------------------------------
// yük bırakma
// ---------------------------------------------------------------------------

void ASmvGameRulesActor::BuildDrop()
{
	SmvCore::FDropCfg C;
	C.Seed = (unsigned)(FDateTime::Now().GetTicks() % 100000) + 7u;
	const double Kg = Req.Drone.Equipment.bGripper ? Req.Drone.Equipment.PayloadKg : 1.0;
	C.Payloads = Kg > 5.0 ? 3 : 5;
	C.Zones = 4;
	const bool bPlane = Req.Drone.Type == TEXT("plane") || Req.Drone.Type == TEXT("vtol");
	C.AreaMinR = bPlane ? 250 : 80;
	C.AreaMaxR = bPlane ? 700 : 240;
	Impl->Drop.Setup(C);
	int32 i = 0;
	for (const SmvCore::FDropZone& Z : Impl->Drop.Zones())
	{
		const FVector G = Ground(Z.E, Z.N, 0.06);
		// hedef halkaları: dış (30 m) / orta (15 m) / iç (5 m) — isabet eşikleri ile aynı
		AddPart(CylMesh, G, FVector(60.0, 60.0, 0.002), FRotator::ZeroRotator, FLinearColor(0.95f, 0.75f, 0.1f), true, 1.2f);
		AddPart(CylMesh, G + FVector(0, 0, 2.0), FVector(30.0, 30.0, 0.002), FRotator::ZeroRotator, FLinearColor(1.f, 0.45f, 0.08f), true, 1.6f);
		AddPart(CylMesh, G + FVector(0, 0, 4.0), FVector(10.0, 10.0, 0.002), FRotator::ZeroRotator, FLinearColor(1.f, 0.12f, 0.08f), true, 2.4f);
		// işaret direği + bayrak (uzaktan görünsün)
		AddPart(CylMesh, G + FVector(0, 0, 400.0), FVector(0.08, 0.08, 8.0), FRotator::ZeroRotator, FLinearColor(0.9f, 0.9f, 0.92f), false);
		AddPart(CubeMesh, G + FVector(60.0, 0, 760.0), FVector(1.2, 0.03, 0.7), FRotator(0, 25.0 * i, 0), FLinearColor(1.f, 0.35f, 0.05f), true, 1.5f);
		++i;
	}
	Emit(TEXT("game.drop_start"), 5, { { TEXT("n"), FString::FromInt(C.Payloads) }, { TEXT("z"), FString::FromInt((int32)Impl->Drop.Zones().size()) } });
}

// ---------------------------------------------------------------------------
// tarla ilaçlama
// ---------------------------------------------------------------------------

void ASmvGameRulesActor::BuildSpray()
{
	SmvCore::FSprayCfg C;
	const FSmvEquipment& Eq = Req.Drone.Equipment;
	C.CenterE = 0; C.CenterN = 110; C.W = 64; C.L = 112;
	C.HeadingDeg = FMath::Fmod(Req.Track.World.RunwayHeading, 360.0);
	C.Cell = 4;
	C.SwathW = FMath::Clamp(Eq.bSprayer ? Eq.SprayWidthM * 1.5 : 6.0, 3.0, 18.0);
	C.TankL = Eq.bSprayer ? Eq.SprayTankL : 10.0;
	C.RateLps = C.TankL / 300.0 * FMath::Clamp(Eq.SprayPumpRate / 10.0, 0.5, 2.0);
	Impl->Spray.Setup(C);
	// tarla sınırı: köşe direkleri + kenar şeritleri
	const double H = FMath::DegreesToRadians(C.HeadingDeg);
	const double Fe = FMath::Sin(H), Fn = FMath::Cos(H), Re = FMath::Cos(H), Rn = -FMath::Sin(H);
	auto Pt = [&](double Uu, double Vv, double& Oe, double& On) { Oe = C.CenterE + Uu * Re + Vv * Fe; On = C.CenterN + Uu * Rn + Vv * Fn; };
	const double Hw = C.W * 0.5, Hl = C.L * 0.5;
	const double Corners[4][2] = { { -Hw, -Hl }, { Hw, -Hl }, { Hw, Hl }, { -Hw, Hl } };
	for (int32 k = 0; k < 4; ++k)
	{
		double E0, N0, E1, N1;
		Pt(Corners[k][0], Corners[k][1], E0, N0);
		Pt(Corners[(k + 1) % 4][0], Corners[(k + 1) % 4][1], E1, N1);
		const FVector A = Ground(E0, N0, 0.0), B = Ground(E1, N1, 0.0);
		AddPart(CylMesh, A + FVector(0, 0, 150.0), FVector(0.12, 0.12, 3.0), FRotator::ZeroRotator, FLinearColor(0.95f, 0.85f, 0.1f), true, 1.4f);
		const FVector Mid = (A + B) * 0.5 + FVector(0, 0, 8.0);
		const FVector D = B - A;
		AddPart(CubeMesh, Mid, FVector(D.Size() / 100.0, 0.25, 0.04), D.Rotation(), FLinearColor(0.95f, 0.85f, 0.1f), true, 0.9f);
	}
	// kapsama haritası: ilaçlanan hücreler yeşil karo (ISM)
	Cells = NewObject<UInstancedStaticMeshComponent>(this);
	Cells->SetStaticMesh(CubeMesh);
	Cells->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Cells->SetCastShadow(false);
	Cells->SetupAttachment(Root);
	Cells->RegisterComponent();
	if (UMaterialInterface* Base = GlowMat ? GlowMat.Get() : SmvSolidMat(CubeMesh->GetMaterial(0)))
	{
		UMaterialInstanceDynamic* D = UMaterialInstanceDynamic::Create(Base, Cells);
		if (GlowMat) { D->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.15f, 0.85f, 0.35f)); D->SetScalarParameterValue(TEXT("Glow"), 0.6f); }
		else SmvSetSolidColor(D, FLinearColor(0.15f, 0.7f, 0.3f));
		Cells->SetMaterial(0, D);
	}
	Emit(TEXT("game.spray_start"), 5, { { TEXT("l"), FString::Printf(TEXT("%.0f"), C.TankL) } });
}

// ---------------------------------------------------------------------------
// savunma (kritik tesis / gemi)
// ---------------------------------------------------------------------------

namespace
{
	float SmvRulesCfg(const TCHAR* Key, float Def)
	{
		float V = Def;
		if (GConfig) GConfig->GetFloat(TEXT("/Script/Simumav.SmvFabContent"), Key, V, GGameIni);
		return V;
	}
}

void ASmvGameRulesActor::BuildDefense(bool bShip)
{
	SmvCore::FDefCfg C;
	const FString& Lv = Flow.IsValid() ? Flow->AiLevel : FString(TEXT("normal"));
	C.Waves = Lv == TEXT("easy") ? 2 : Lv == TEXT("hard") ? 4 : 3;
	C.Speed = Lv == TEXT("easy") ? 13 : Lv == TEXT("hard") ? 22 : 17;
	C.bShip = bShip;
	C.FacE = bShip ? -160 : 0;
	C.FacN = bShip ? 230 : 170;
	C.ShipSpeed = 2.5;
	C.ShipHeadingDeg = 90;
	C.Seed = (unsigned)(FDateTime::Now().GetTicks() % 100000) + 3u;
	Impl->Def.Setup(C);
	// gemi deniz seviyesinde (ev zemini), tesis arazide (zemine iz düşümü)
	const FVector F = bShip ? ToWorld(C.FacE, C.FacN, 0.0) : Ground(C.FacE, C.FacN, 0.0);
	if (bShip)
	{
		// deniz: geniş, parlak (düşük pürüzlülük) su düzlemi — Lumen yansımaları gökyüzünü ve gemiyi yansıtır; ev noktasında kalkış güvertesi
		UMaterialInterface* Water = GrLoadMat(TEXT("/Game/Simumav/Materials/M_SmvWater.M_SmvWater"));
		const FVector G0 = Ground(0, 0, 0.0);
		if (UStaticMeshComponent* Sea = AddPart(CubeMesh, FVector(G0.X, G0.Y, G0.Z - 40.0), FVector(6000.0, 6000.0, 0.2), FRotator::ZeroRotator, FLinearColor(0.02f, 0.07f, 0.11f), false))
		{
			if (Water) Sea->SetMaterial(0, Water);
			else if (UMaterialInstanceDynamic* D = Cast<UMaterialInstanceDynamic>(Sea->GetMaterial(0))) { D->SetScalarParameterValue(TEXT("Roughness"), 0.04f); D->SetScalarParameterValue(TEXT("Metallic"), 0.f); }
			Sea->SetCastShadow(false);
		}
		AddPart(CubeMesh, G0 - FVector(0, 0, 40.0), FVector(26.0, 26.0, 0.8), FRotator::ZeroRotator, FLinearColor(0.22f, 0.24f, 0.26f), false);
		AddPart(CylMesh, G0 + FVector(0, 0, 1.5), FVector(9.0, 9.0, 0.01), FRotator::ZeroRotator, FLinearColor(0.95f, 0.8f, 0.1f), true, 0.8f);
		// gemi (basit model; yalnız görsel): gövde, pruva, üst yapı, direk, radar kubbesi
		const FLinearColor Gray(0.36f, 0.39f, 0.42f), Dark(0.12f, 0.13f, 0.14f), Deck(0.22f, 0.23f, 0.22f);
		auto Ship = [&](UStaticMesh* M, const FVector& Off, const FVector& S, const FRotator& R, const FLinearColor& Col)
		{
			if (UStaticMeshComponent* P = AddPart(M, F + Off, S, R, Col, false)) FacilityParts.Add(P);
		};
		// Fab gemi modeli (JMSDF Mogami sınıfı fırkateyn; /Game/Fab/Ships — yalnız görsel, korunacak nesne; silah davranışı yok)
		const TArray<UStaticMesh*>& ShipMeshes = FSmvFabContent::StaticMeshesIn(TEXT("/Game/Fab/Ships"));
		if (ShipMeshes.Num() > 0)
		{
			// çapa: gemi merkezinde görünmez parça (FacilityParts[0]; gemi hareketi buna göre uygulanır)
			if (UStaticMeshComponent* Anchor = AddPart(CubeMesh, F + FVector(0, 0, 150.0), FVector(0.01), FRotator::ZeroRotator, Gray, false))
			{
				Anchor->SetVisibility(false);
				FacilityParts.Add(Anchor);
			}
			FBox B(ForceInit);
			FVector GunAt = FVector::ZeroVector;
			for (UStaticMesh* M : ShipMeshes)
			{
				B += M->GetBoundingBox();
				if (M->GetName().Contains(TEXT("gun"))) GunAt = M->GetBoundingBox().GetCenter();
			}
			const FVector Sz = B.GetSize();
			const bool bAlongY = Sz.Y > Sz.X;
			const double LongCm = FMath::Max(1.0, FMath::Max(Sz.X, Sz.Y));
			const double Sc = FMath::Max(10.0, (double)SmvRulesCfg(TEXT("ShipLengthM"), 133.f)) * 100.0 / LongCm;
			// burun +X (gemi rotası doğu = +X): uzun eksen Y ise 90°; top (gun) merkezin arka yanındaysa 180° çevrilir
			const FVector Ctr = B.GetCenter();
			const double GunAlong = GunAt.IsNearlyZero() ? 1.0 : (bAlongY ? GunAt.Y - Ctr.Y : GunAt.X - Ctr.X);
			const float Yaw = (bAlongY ? -90.f : 0.f) + (GunAlong < 0.0 ? 180.f : 0.f);
			// OBJ Y-yukarı: içe aktarılan gemi yan yatmış olabilir (en > yükseklik; birleşik ağda üst yapı yana / tepeye taşıyordu) —
			// ikinci yatay eksen düşey eksenden belirgin büyükse uzun eksen çevresinde 90° (ShipUpRollDeg ile değiştirilebilir) döndürülür
			const double Side = bAlongY ? Sz.X : Sz.Y;
			const float UpRoll = Side > Sz.Z * 1.25 ? SmvRulesCfg(TEXT("ShipUpRollDeg"), 90.f) : 0.f;
			const FQuat Qup = bAlongY ? FQuat(FVector::YAxisVector, FMath::DegreesToRadians(UpRoll)) : FQuat(FVector::XAxisVector, FMath::DegreesToRadians(UpRoll));
			const FRotator R = (FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(Yaw)) * Qup).Rotator();
			const FBox Bw = B.TransformBy(FTransform(R.Quaternion(), FVector::ZeroVector, FVector(Sc)));
			const double Draft = SmvRulesCfg(TEXT("ShipDraftM"), 5.f) * 100.0;
			const FVector Loc = F - FVector(Bw.GetCenter().X, Bw.GetCenter().Y, 0.0) + FVector(0, 0, -Bw.Min.Z - 30.0 - Draft);
			UE_LOG(LogSimumav, Log, TEXT("Gemi ağı sınırı (yerel): %.0f x %.0f x %.0f; yan yatma düzeltmesi %.0f°"), Sz.X, Sz.Y, Sz.Z, UpRoll);
			for (UStaticMesh* M : ShipMeshes)
			{
				UStaticMeshComponent* P = NewObject<UStaticMeshComponent>(this);
				P->SetStaticMesh(M);
				P->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
				P->SetupAttachment(Root);
				P->RegisterComponent();
				P->SetWorldLocationAndRotation(Loc, R);
				P->SetWorldScale3D(FVector(Sc));
				Parts.Add(P);
				FacilityParts.Add(P);
			}
			UE_LOG(LogSimumav, Log, TEXT("Gemi savunma: Fab gemi modeli %d parça, boy %.0f m (ölçek %.3f, yön %.0f°; yalnız görsel)"), ShipMeshes.Num(), LongCm * Sc / 100.0, Sc, Yaw);
		}
		else
		{
		Ship(CubeMesh, FVector(0, 0, 150.0), FVector(70.0, 12.0, 6.0), FRotator::ZeroRotator, Gray);
		Ship(CubeMesh, FVector(0, 0, 455.0), FVector(70.0, 12.0, 0.1), FRotator::ZeroRotator, Deck);
		Ship(ConeMesh, FVector(4300.0, 0, 150.0), FVector(12.0, 6.0, 16.0), FRotator(-90.0, 0, 0), Gray);
		Ship(CubeMesh, FVector(-900.0, 0, 900.0), FVector(18.0, 9.0, 9.0), FRotator::ZeroRotator, Gray);
		Ship(CubeMesh, FVector(-900.0, 0, 1500.0), FVector(10.0, 7.0, 4.0), FRotator::ZeroRotator, Gray);
		Ship(CylMesh, FVector(-900.0, 0, 2400.0), FVector(0.6, 0.6, 14.0), FRotator::ZeroRotator, Dark);
		Ship(SphereMesh, FVector(-900.0, 0, 3200.0), FVector(2.4, 2.4, 2.4), FRotator::ZeroRotator, FLinearColor(0.85f, 0.86f, 0.88f));
		Ship(CubeMesh, FVector(2200.0, 0, 560.0), FVector(6.0, 5.0, 2.0), FRotator::ZeroRotator, Gray);
		Ship(CylMesh, FVector(-2600.0, 0, 520.0), FVector(8.0, 8.0, 0.1), FRotator::ZeroRotator, FLinearColor(0.9f, 0.9f, 0.9f));
		}
	}
	else
	{
		// kritik tesis: depolama tankları, işlem kulesi, boru köprüsü, kontrol binası (yalnız görsel)
		const FLinearColor Tank(0.82f, 0.83f, 0.8f), Steel(0.42f, 0.44f, 0.46f), Conc(0.55f, 0.54f, 0.5f);
		auto Fac = [&](UStaticMesh* M, const FVector& Off, const FVector& S, const FRotator& R, const FLinearColor& Col)
		{
			if (UStaticMeshComponent* P = AddPart(M, F + Off, S, R, Col, false)) FacilityParts.Add(P);
		};
		Fac(CylMesh, FVector(-1800.0, -1400.0, 650.0), FVector(16.0, 16.0, 13.0), FRotator::ZeroRotator, Tank);
		Fac(CylMesh, FVector(-1800.0, 1400.0, 650.0), FVector(16.0, 16.0, 13.0), FRotator::ZeroRotator, Tank);
		Fac(CylMesh, FVector(1700.0, -1500.0, 500.0), FVector(12.0, 12.0, 10.0), FRotator::ZeroRotator, Tank);
		Fac(CylMesh, FVector(1500.0, 1600.0, 1600.0), FVector(3.2, 3.2, 32.0), FRotator::ZeroRotator, Steel);
		Fac(CylMesh, FVector(2300.0, 1600.0, 1200.0), FVector(2.4, 2.4, 24.0), FRotator::ZeroRotator, Steel);
		Fac(CubeMesh, FVector(0.0, 0.0, 600.0), FVector(52.0, 1.6, 0.8), FRotator::ZeroRotator, Steel);
		Fac(CubeMesh, FVector(0.0, 0.0, 300.0), FVector(52.0, 0.4, 6.0), FRotator::ZeroRotator, Steel);
		Fac(CubeMesh, FVector(200.0, -3200.0, 350.0), FVector(20.0, 12.0, 7.0), FRotator::ZeroRotator, Conc);
		// Fab tesis dekoru (kışla, radar çanağı, gözetleme kulesi …; /Game/Fab/Base — yalnız görsel): tesisin çevresinde
		TMap<FName, TArray<UStaticMesh*>> ByProduct;
		for (UStaticMesh* M : FSmvFabContent::StaticMeshesIn(TEXT("/Game/Fab/Base"))) ByProduct.FindOrAdd(FName(*FPackageName::GetLongPackagePath(M->GetOutermost()->GetName()))).Add(M);
		int32 k = 0;
		for (TPair<FName, TArray<UStaticMesh*>>& It : ByProduct)
		{
			FBox B(ForceInit);
			for (UStaticMesh* M : It.Value) B += M->GetBoundingBox();
			const double L = FMath::Max3(B.GetSize().X, B.GetSize().Y, B.GetSize().Z);
			const double Sc = (L < 200.0 || L > 6000.0) ? 1200.0 / FMath::Max(L, 1.0) : 1.0;
			const double A = FMath::DegreesToRadians(200.0 + 55.0 * k++);
			const FVector At = Ground(C.FacE + 48.0 * FMath::Cos(A), C.FacN + 48.0 * FMath::Sin(A), 0.0);
			for (UStaticMesh* M : It.Value)
			{
				UStaticMeshComponent* P = NewObject<UStaticMeshComponent>(this);
				P->SetStaticMesh(M);
				P->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
				P->SetupAttachment(Root);
				P->RegisterComponent();
				const FRotator R(0.0, FMath::RadiansToDegrees(A) + 180.0, 0.0);
				P->SetWorldLocationAndRotation(At - R.RotateVector(FVector(B.GetCenter().X, B.GetCenter().Y, 0.0) * Sc) + FVector(0, 0, -B.Min.Z * Sc), R);
				P->SetWorldScale3D(FVector(Sc));
				Parts.Add(P);
			}
		}
		if (ByProduct.Num()) UE_LOG(LogSimumav, Log, TEXT("Kritik tesis: %d Fab tesis dekoru (yalnız görsel)"), ByProduct.Num());
	}
	// korunan alan halkası (isabet yarıçapı)
	if (UStaticMeshComponent* Ring = AddPart(CylMesh, F + FVector(0, 0, 8.0), FVector(C.HitR * 2.0, C.HitR * 2.0, 0.002), FRotator::ZeroRotator,
		FLinearColor(0.2f, 0.75f, 1.f), true, 0.9f))
	{
		FacilityParts.Add(Ring);
	}
	Emit(bShip ? TEXT("game.ship_start") : TEXT("game.facility_start"), 5, { { TEXT("w"), FString::FromInt(C.Waves) }, { TEXT("n"), FString::FromInt(Impl->Def.Total()) } });
}

FVector ASmvGameRulesActor::ThreatWorld(int32 Id) const
{
	if (!Impl.IsValid()) return FVector::ZeroVector;
	if (const int32* Ix = Impl->ThreatIndex.Find(Id)) if (ThreatActors.IsValidIndex(*Ix) && ThreatActors[*Ix]) return ThreatActors[*Ix]->GetActorLocation();
	return FVector::ZeroVector;
}

// ---------------------------------------------------------------------------
// telemetri, eylemler, adım
// ---------------------------------------------------------------------------

void ASmvGameRulesActor::OnTele(const FCaySimTelemetry& T)
{
	double X = 0, Y = 0;
	SmvCore::TargetLocal(Req.Track.Lat, Req.Track.Lon, T.Lat, T.Lon, X, Y);
	E = X; N = Y; U = T.AltRel;
	Ve = T.Ve; Vn = T.Vn; Vu = -T.Vd;
	Gs = FMath::Sqrt(Ve * Ve + Vn * Vn);
	YawDeg = FMath::RadiansToDegrees((double)T.Yaw);
	bArmed = T.bArmed;
	bHasTele = true;
}

void ASmvGameRulesActor::Action(const FString& Name)
{
	if (!Impl.IsValid()) return;
	if (Name == TEXT("drop") && Impl->Kind == SmvCore::EGameKind::CargoDrop)
	{
		if (!bHasTele || U < 1.5) { Emit(TEXT("game.drop_low"), 4); return; }
		if (!Impl->Drop.Release(SimT, E, N, U, Ve, Vn, Vu)) { Emit(TEXT("game.drop_empty"), 4); return; }
		Impl->Released++;
		if (OnCommand) { TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>(); P->SetStringField(TEXT("action"), TEXT("release")); OnCommand(TEXT("gripper"), P); }
		// görsel yük kutusu (balistik konum her karede çekirdekten)
		const float Kg = (float)FMath::Clamp(Req.Drone.Equipment.PayloadKg, 0.2, 20.0);
		const float Sz = FMath::Clamp(0.25f + 0.06f * FMath::Sqrt(Kg), 0.25f, 0.6f);
		if (UStaticMeshComponent* Box = AddPart(CubeMesh, Drone ? Drone->GetActorLocation() : ToWorld(E, N, U), FVector(Sz, Sz * 0.8f, Sz * 0.7f), FRotator::ZeroRotator, FLinearColor(0.46f, 0.31f, 0.16f), false))
			PayloadMeshes.Add(Box);
		if (Drone)
		{
			Drone->SetPayloadAttached(Impl->Drop.Left() > 0);
		}
		Emit(TEXT("game.drop_released"), 6, { { TEXT("n"), FString::FromInt(Impl->Drop.Left()) } });
	}
	else if (Name == TEXT("spray") && Impl->Kind == SmvCore::EGameKind::Spraying)
	{
		bSpraying = !bSpraying;
		if (Drone) Drone->SetSprayActive(bSpraying);
		if (OnCommand) { TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>(); P->SetBoolField(TEXT("on"), bSpraying); OnCommand(TEXT("sprayer"), P); }
		Emit(bSpraying ? TEXT("game.spray_on") : TEXT("game.spray_off"), 6);
	}
	else if (Name == TEXT("chute"))
	{
		if (!Req.Drone.Equipment.bParachute) { Emit(TEXT("game.no_chute"), 4); return; }
		if (bChute) return;
		bChute = true;
		if (Drone) Drone->SetChuteDeployed(true);
		if (OnCommand) OnCommand(TEXT("parachute"), MakeShared<FJsonObject>());
		Emit(TEXT("game.chute_out"), 4);
	}
	else if (Name == TEXT("lock") && (Impl->Kind == SmvCore::EGameKind::FacilityDefense || Impl->Kind == SmvCore::EGameKind::ShipDefense))
	{
		// YALNIZ HUD: aday tehdit "kilitli" çerçeveye geçer; araç hiçbir şeye kendiliğinden yönelmez
		Impl->Def.PlayerLock(!Impl->Def.IsPlayerLocked());
	}
}

void ASmvGameRulesActor::Tick(float Dt)
{
	Super::Tick(Dt);
	if (!Impl.IsValid()) return;
	Dt = FMath::Min(Dt, 0.1f);
	SimT += Dt;
	// tuşlar (uçuş arayüzü dışındaki tuşlar: Z yük bırak · M ilaçlama · J paraşüt · kilit tuşu)
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		if (PC->WasInputKeyJustPressed(EKeys::Z)) Action(TEXT("drop"));
		if (PC->WasInputKeyJustPressed(EKeys::M)) Action(TEXT("spray"));
		if (PC->WasInputKeyJustPressed(EKeys::J)) Action(TEXT("chute"));
		const FKey LockKey = FSmvUiPrefs::Get().KeyFor(FName(TEXT("Lock")), false);
		if (LockKey.IsValid() && PC->WasInputKeyJustPressed(LockKey)) Action(TEXT("lock"));
	}
	TickGame(Dt);
	PushResult();
}

void ASmvGameRulesActor::TickGame(float Dt)
{
	std::vector<SmvCore::FGameEvent> Ev;
	switch (Impl->Kind)
	{
	case SmvCore::EGameKind::CargoDrop:
	{
		Impl->Drop.Step(Dt, Ev, 0.0);
		const std::vector<SmvCore::FPayload>& P = Impl->Drop.Payloads();
		for (int32 i = 0; i < (int32)P.size() && i < PayloadMeshes.Num(); ++i)
		{
			if (!PayloadMeshes[i]) continue;
			const FVector W = P[i].bLanded ? Ground(P[i].E, P[i].N, 0.15) : ToWorld(P[i].E, P[i].N, P[i].U);
			PayloadMeshes[i]->SetWorldLocation(W);
			if (!P[i].bLanded) PayloadMeshes[i]->AddLocalRotation(FRotator(Dt * 40.f, Dt * 25.f, 0.f));
		}
		for (const SmvCore::FGameEvent& G : Ev)
		{
			if (G.Kind != "land") continue;
			const int32 Pts = SmvCore::FDropGame::PointsFor(G.Value);
			Emit(Pts > 0 ? TEXT("game.drop_hit") : TEXT("game.drop_miss"), Pts > 0 ? 5 : 4, { { TEXT("m"), FString::Printf(TEXT("%.1f"), G.Value) }, { TEXT("p"), FString::FromInt(Pts) } });
		}
		if (Impl->Drop.Done() && !bDone) { bDone = true; Emit(TEXT("game.drop_done"), 5, { { TEXT("s"), FString::FromInt(Impl->Drop.Score()) } }); }
		break;
	}
	case SmvCore::EGameKind::Spraying:
	{
		if (bHasTele) Impl->Spray.Step(SimT, Dt, E, N, U, Gs, bSpraying && bArmed, Ev);
		if (Cells)
		{
			for (int32 Ci : Impl->Spray.NewCells())
			{
				double Ce = 0, Cn = 0;
				Impl->Spray.CellCenter(Ci % Impl->Spray.Cols(), Ci / Impl->Spray.Cols(), Ce, Cn);
				const FVector G = Ground(Ce, Cn, 0.05);
				const double Cell = Impl->Spray.Config().Cell;
				Cells->AddInstance(FTransform(FRotator(0.0, Impl->Spray.Config().HeadingDeg - 90.0, 0.0), G, FVector(Cell * 0.92, Cell * 0.92, 0.015)), true);
			}
		}
		for (const SmvCore::FGameEvent& G : Ev)
		{
			if (G.Kind == "too_high") Emit(TEXT("game.spray_high"), 4);
			else if (G.Kind == "too_fast") Emit(TEXT("game.spray_fast"), 4);
			else if (G.Kind == "tank_empty") { Emit(TEXT("game.spray_empty"), 4); bSpraying = false; if (Drone) Drone->SetSprayActive(false); }
			else if (G.Kind == "done") { bDone = true; Emit(TEXT("game.spray_done"), 5, { { TEXT("s"), FString::FromInt(Impl->Spray.Score()) } }); }
		}
		break;
	}
	case SmvCore::EGameKind::FacilityDefense:
	case SmvCore::EGameKind::ShipDefense:
	{
		if (!bHasTele) break;
		Impl->Def.Step(SimT, Dt, E, N, U, YawDeg, Ev);
		for (const SmvCore::FGameEvent& G : Ev)
		{
			if (G.Kind == "spawn")
			{
				FActorSpawnParameters Sp;
				Sp.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				ASmvDroneActor* A = GetWorld()->SpawnActor<ASmvDroneActor>(ASmvDroneActor::StaticClass(), FTransform::Identity, Sp);
				if (!A) continue;
				FCaySimSession S;
				S.Type = ECaySimDroneType::FixedWing;
				S.Model.Orient.Forward = TEXT("+x");
				S.Model.Orient.Up = TEXT("+y");
				S.Model.Orient.Scale = 1.f;
				A->Setup(S, nullptr);
				A->ConfigureVisual(TEXT("plane"), FString(), FLinearColor(0.55f, 0.12f, 0.1f));
				A->SetActorTickEnabled(false);
				static const TCHAR* Files[] = { TEXT("tgt_lancet.glb"), TEXT("tgt_sw300.glb"), TEXT("tgt_altius.glb"), TEXT("tgt_octopus.glb") };
				TArray<uint8> B;
				if (USmvDataService::LoadBundledGlb(Files[G.I % 4], B)) A->LoadGlbBytes(B);
				Impl->ThreatIndex.Add(G.I, ThreatActors.Num());
				ThreatActors.Add(A);
			}
			else if (G.Kind == "wave") Emit(TEXT("game.def_wave"), 4, { { TEXT("w"), FString::FromInt(G.I) }, { TEXT("n"), FString::FromInt((int32)G.Value) } });
			else if (G.Kind == "contact" || G.Kind == "impact")
			{
				if (const int32* Ix = Impl->ThreatIndex.Find(G.I)) if (ThreatActors.IsValidIndex(*Ix) && ThreatActors[*Ix]) { ThreatActors[*Ix]->Destroy(); ThreatActors[*Ix] = nullptr; }
				if (G.Kind == "contact") Emit(G.Value > 0.5 ? TEXT("game.def_contact_lock") : TEXT("game.def_contact"), 5);
				else Emit(TEXT("game.def_impact"), 3, { { TEXT("hp"), FString::FromInt((int32)G.Value) } });
			}
			else if (G.Kind == "done")
			{
				bDone = true;
				Emit(G.Value > 0.5 ? TEXT("game.def_success") : TEXT("game.def_fail"), G.Value > 0.5 ? 5 : 3, { { TEXT("s"), FString::FromInt(Impl->Def.Score()) } });
			}
		}
		// tehdit konum / yön
		for (const SmvCore::FThreat& X : Impl->Def.Threats())
		{
			if (!X.bAlive) continue;
			const int32* Ix = Impl->ThreatIndex.Find(X.Id);
			if (!Ix || !ThreatActors.IsValidIndex(*Ix) || !ThreatActors[*Ix]) continue;
			const double Hdg = FMath::RadiansToDegrees(FMath::Atan2(X.Ve, X.Vn));
			ThreatActors[*Ix]->SetActorLocationAndRotation(ToWorld(X.E, X.N, X.U), FRotator(0.0, Hdg - 90.0, 0.0));
		}
		// gemi hareketi
		if (Impl->Kind == SmvCore::EGameKind::ShipDefense && FacilityParts.Num())
		{
			// gövde (ilk parça) gemi merkezinin 1,5 m üstünde kuruldu: yatay fark tüm parçalara uygulanır
			const FVector Now = ToWorld(Impl->Def.FacilityE(), Impl->Def.FacilityN(), 0.0);
			const FVector Base = FacilityParts[0] ? FacilityParts[0]->GetComponentLocation() : Now;
			const FVector Delta = FVector(Now.X - Base.X, Now.Y - Base.Y, 0.0);
			if (!Delta.IsNearlyZero(0.1)) for (TObjectPtr<UStaticMeshComponent>& P : FacilityParts) if (P) P->AddWorldOffset(Delta);
		}
		break;
	}
	default: break;
	}
}

int32 ASmvGameRulesActor::Score() const
{
	if (!Impl.IsValid()) return 0;
	switch (Impl->Kind)
	{
	case SmvCore::EGameKind::CargoDrop: return Impl->Drop.Score();
	case SmvCore::EGameKind::Spraying: return Impl->Spray.Score();
	case SmvCore::EGameKind::FacilityDefense:
	case SmvCore::EGameKind::ShipDefense: return Impl->Def.Score();
	default: return 0;
	}
}

void ASmvGameRulesActor::PushResult()
{
	if (!Flow.IsValid() || !Impl.IsValid()) return;
	USmvFlow::FGameResult& R = Flow->LastGame;
	R.Mode = Mode;
	R.Score = Score();
	switch (Impl->Kind)
	{
	case SmvCore::EGameKind::CargoDrop:
		R.bReleased = Impl->Drop.Released();
		R.DropBestM = Impl->Drop.BestM();
		R.Payloads = (int32)Impl->Drop.Payloads().size();
		R.Hits = 0;
		for (const SmvCore::FPayload& P : Impl->Drop.Payloads()) if (P.Points > 0) ++R.Hits;
		break;
	case SmvCore::EGameKind::Spraying:
		R.SprayCoverage = Impl->Spray.Coverage();
		R.SprayTankLeft = Impl->Spray.TankLeft();
		break;
	case SmvCore::EGameKind::FacilityDefense:
	case SmvCore::EGameKind::ShipDefense:
		R.DefenseScore = Impl->Def.Score();
		R.bDefenseOk = Impl->Def.Success();
		R.Neutralized = Impl->Def.Neutralized();
		R.Impacts = Impl->Def.Impacts();
		R.Hp = Impl->Def.Hp();
		R.Threats = Impl->Def.Total();
		break;
	default: break;
	}
}

void ASmvGameRulesActor::Augment(FSmvFlightSummary& S) const
{
	if (!Impl.IsValid()) return;
	S.GameMode = Mode;
	S.GameScore = Score();
	if ((Impl->Kind == SmvCore::EGameKind::FacilityDefense || Impl->Kind == SmvCore::EGameKind::ShipDefense) && Impl->Def.Success()) S.Badges.AddUnique(TEXT("defense_ok"));
	if (Impl->Kind == SmvCore::EGameKind::Spraying && Impl->Spray.Coverage() >= 0.9) S.Badges.AddUnique(TEXT("spray_90"));
	if (Impl->Kind == SmvCore::EGameKind::CargoDrop && Impl->Drop.BestM() >= 0 && Impl->Drop.BestM() <= 5.0) S.Badges.AddUnique(TEXT("bullseye"));
}

FText ASmvGameRulesActor::Line1() const
{
	if (!Impl.IsValid()) return FText::GetEmpty();
	switch (Impl->Kind)
	{
	case SmvCore::EGameKind::CargoDrop:
		return FSmvI18n::Format(TEXT("game.hud_drop"), { { TEXT("left"), FString::FromInt(Impl->Drop.Left()) }, { TEXT("best"), Impl->Drop.BestM() >= 0 ? FString::Printf(TEXT("%.1f m"), Impl->Drop.BestM()) : FString(TEXT("—")) } });
	case SmvCore::EGameKind::Spraying:
		return FSmvI18n::Format(TEXT("game.hud_spray"), { { TEXT("cov"), FString::Printf(TEXT("%.0f"), Impl->Spray.Coverage() * 100.0) }, { TEXT("tank"), FString::Printf(TEXT("%.1f"), Impl->Spray.TankLeft()) } });
	case SmvCore::EGameKind::FacilityDefense:
	case SmvCore::EGameKind::ShipDefense:
		return FSmvI18n::Format(TEXT("game.hud_def"), { { TEXT("w"), FString::FromInt(Impl->Def.Wave()) }, { TEXT("ww"), FString::FromInt(Impl->Def.Waves()) },
			{ TEXT("n"), FString::FromInt(Impl->Def.Neutralized()) }, { TEXT("t"), FString::FromInt(Impl->Def.Total()) } });
	default: return FText::GetEmpty();
	}
}

FText ASmvGameRulesActor::Line2() const
{
	if (!Impl.IsValid()) return FText::GetEmpty();
	switch (Impl->Kind)
	{
	case SmvCore::EGameKind::CargoDrop: return FSmvI18n::T(TEXT("game.hint_drop"));
	case SmvCore::EGameKind::Spraying: return FSmvI18n::T(bSpraying ? TEXT("game.hint_spray_on") : TEXT("game.hint_spray"));
	case SmvCore::EGameKind::FacilityDefense:
	case SmvCore::EGameKind::ShipDefense:
		return FSmvI18n::T(Impl->Def.IsPlayerLocked() ? TEXT("game.hint_locked") : Impl->Def.LockId() ? TEXT("game.hint_lockable") : TEXT("game.hint_def"));
	default: return FText::GetEmpty();
	}
}

float ASmvGameRulesActor::Bar01() const
{
	if (!Impl.IsValid()) return 0.f;
	switch (Impl->Kind)
	{
	case SmvCore::EGameKind::CargoDrop: return Impl->Drop.Payloads().empty() ? 0.f : (float)Impl->Drop.Payloads().size() / (float)(Impl->Drop.Payloads().size() + Impl->Drop.Left());
	case SmvCore::EGameKind::Spraying: return (float)Impl->Spray.Coverage();
	case SmvCore::EGameKind::FacilityDefense:
	case SmvCore::EGameKind::ShipDefense: return Impl->Def.Hp() / 100.f;
	default: return 0.f;
	}
}

FLinearColor ASmvGameRulesActor::BarColor() const
{
	if (!Impl.IsValid()) return Accent();
	if (Impl->Kind == SmvCore::EGameKind::FacilityDefense || Impl->Kind == SmvCore::EGameKind::ShipDefense)
	{
		const float H = Impl->Def.Hp() / 100.f;
		return H > 0.6f ? Accent3() : H > 0.3f ? Warn() : Danger();
	}
	return Impl->Kind == SmvCore::EGameKind::Spraying ? Accent3() : Warn();
}

// ---------------------------------------------------------------------------
// HUD (Slate; görünüm katmanında): üstte tür paneli, kilit çerçevesi, eylem düğmeleri
// ---------------------------------------------------------------------------

class SSmvGameHud : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSmvGameHud) {}
		SLATE_ARGUMENT(ASmvGameRulesActor*, Owner)
	SLATE_END_ARGS()

	void Construct(const FArguments& A)
	{
		Owner = A._Owner;
		TWeakObjectPtr<ASmvGameRulesActor> W = Owner;
		TSharedRef<SSmvCanvas> Bracket = SNew(SSmvCanvas).Size(FVector2D(1, 1)).Visibility(EVisibility::HitTestInvisible);
		Bracket->Painter = [W](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
		{
			ASmvGameRulesActor* O = W.Get();
			if (!O || !O->Impl.IsValid()) return;
			const bool bDef = O->Impl->Kind == SmvCore::EGameKind::FacilityDefense || O->Impl->Kind == SmvCore::EGameKind::ShipDefense;
			if (!bDef) return;
			APlayerController* PC = UGameplayStatics::GetPlayerController(O, 0);
			if (!PC || !GEngine || !GEngine->GameViewport) return;
			FVector2D Vs;
			GEngine->GameViewport->GetViewportSize(Vs);
			if (Vs.X < 1 || Vs.Y < 1) return;
			const FVector2D Ls = FVector2D(G.GetLocalSize());
			const FVector2D K(Ls.X / Vs.X, Ls.Y / Vs.Y);
			const int32 Lock = O->Impl->Def.LockId();
			const bool bLocked = O->Impl->Def.IsPlayerLocked();
			for (const SmvCore::FThreat& X : O->Impl->Def.Threats())
			{
				if (!X.bAlive) continue;
				const FVector Wp = O->ThreatWorld(X.Id);
				if (Wp.IsZero()) continue;
				FVector2D Sp;
				if (!PC->ProjectWorldLocationToScreen(Wp, Sp, true)) continue;
				const FVector2D P(Sp.X * K.X, Sp.Y * K.Y);
				if (P.X < -50 || P.Y < -50 || P.X > Ls.X + 50 || P.Y > Ls.Y + 50) continue;
				const double D = FMath::Sqrt((X.E - O->E) * (X.E - O->E) + (X.N - O->N) * (X.N - O->N) + (X.U - O->U) * (X.U - O->U));
				if (X.Id == Lock)
				{
					// kilit çerçevesi (yalnız gösterge): aday kesikli turuncu, oyuncu kilidi dolu kırmızı
					const float R = 26.f;
					const FLinearColor C = bLocked ? Danger() : Warn();
					const float Th = bLocked ? 2.5f : 1.5f;
					for (int32 s = 0; s < 4; ++s)
					{
						const float Sx = (s & 1) ? 1.f : -1.f, Sy = (s & 2) ? 1.f : -1.f;
						const FVector2D Cn = P + FVector2D(Sx * R, Sy * R);
						DrSeg(Out, L, G, Cn, Cn - FVector2D(Sx * 10.f, 0.f), C, Th);
						DrSeg(Out, L, G, Cn, Cn - FVector2D(0.f, Sy * 10.f), C, Th);
					}
					DrText(Out, L + 1, G, FString::Printf(TEXT("%.0f m"), D), P + FVector2D(0, R + 14.f), Font(11, "Bold"), C, 0);
					if (bLocked) DrText(Out, L + 1, G, FSmvI18n::T(TEXT("game.locked")).ToString(), P - FVector2D(0, R + 12.f), Font(10, "Bold"), C, 0);
				}
				else
				{
					DrLine(Out, L, G, { P + FVector2D(0, -7), P + FVector2D(7, 0), P + FVector2D(0, 7), P + FVector2D(-7, 0), P + FVector2D(0, -7) }, Danger().CopyWithNewOpacity(0.75f), 1.5f);
				}
			}
		};
		auto Act = [W](const TCHAR* Name) { return [W, Name]() { if (ASmvGameRulesActor* O = W.Get()) O->Action(Name); }; };
		TSharedRef<SHorizontalBox> Btns = SNew(SHorizontalBox);
		const FString& M = Owner ? Owner->Mode : FString();
		if (M == TEXT("cargo_drop")) Btns->AddSlot().AutoWidth().Padding(4)[SNew(SSmvButton).Kind(ESmvBtn::Primary).Icon(ESmvIcon::Next).FontSize(12).Text(FSmvI18n::T(TEXT("game.btn_drop"))).OnClick(Act(TEXT("drop")))];
		if (M == TEXT("spraying")) Btns->AddSlot().AutoWidth().Padding(4)[SNew(SSmvButton).Kind(ESmvBtn::Primary).Icon(ESmvIcon::Rain).FontSize(12).Text(FSmvI18n::T(TEXT("game.btn_spray"))).OnClick(Act(TEXT("spray")))
			.Selected_Lambda([W]() { return W.IsValid() && W->bSpraying; })];
		if (M == TEXT("facility_defense") || M == TEXT("ship_defense")) Btns->AddSlot().AutoWidth().Padding(4)[SNew(SSmvButton).Kind(ESmvBtn::Normal).Icon(ESmvIcon::Shield).FontSize(12).Text(FSmvI18n::T(TEXT("game.btn_lock"))).OnClick(Act(TEXT("lock")))];
		if (Owner && Owner->Req.Drone.Equipment.bParachute) Btns->AddSlot().AutoWidth().Padding(4)[SNew(SSmvButton).Kind(ESmvBtn::Danger).Icon(ESmvIcon::Ghost).FontSize(12).Text(FSmvI18n::T(TEXT("game.btn_chute"))).OnClick(Act(TEXT("chute")))];
		TSharedRef<SSmvCanvas> Bar = SNew(SSmvCanvas).Size(FVector2D(360, 6));
		Bar->Painter = [W](const FGeometry& G, FSlateWindowElementList& Out, int32 L)
		{
			ASmvGameRulesActor* O = W.Get();
			if (!O) return;
			const FVector2D S = FVector2D(G.GetLocalSize());
			DrBox(Out, L, G, FVector2D::ZeroVector, S, FLinearColor(1, 1, 1, 0.12f), Round(3.f));
			DrBox(Out, L + 1, G, FVector2D::ZeroVector, FVector2D(S.X * FMath::Clamp(O->Bar01(), 0.f, 1.f), S.Y), O->BarColor(), Round(3.f));
		};
		ChildSlot
		[
			SNew(SOverlay)
			+ SOverlay::Slot()[Bracket]
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, 14, 0, 0)
			[
				SNew(SBorder).BorderImage(GlassCard()).Padding(FMargin(20.f, 10.f)).Visibility(EVisibility::HitTestInvisible)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 14, 0)
						[
							SmvUi::Text(FSmvI18n::T(FString(TEXT("mode.")) + M), 13, "Bold", Accent(), false)
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(STextBlock).Font(Font(18, "Black")).ColorAndOpacity(Fg())
							.Text_Lambda([W]() { return FText::FromString(W.IsValid() ? FString::Printf(TEXT("%d"), W->Score()) : FString()); })
						]
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 4, 0, 6)
					[
						SNew(STextBlock).Font(Font(12, "Regular")).ColorAndOpacity(Fg())
						.Text_Lambda([W]() { return W.IsValid() ? W->Line1() : FText::GetEmpty(); })
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[Bar]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 6, 0, 0)
					[
						SNew(STextBlock).Font(Font(10, "Regular")).ColorAndOpacity(Dim())
						.Text_Lambda([W]() { return W.IsValid() ? W->Line2() : FText::GetEmpty(); })
					]
				]
			]
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0, 0, 0, 190)
			[
				Btns
			]
		];
		SetVisibility(EVisibility::SelfHitTestInvisible);
	}

private:
	ASmvGameRulesActor* Owner = nullptr;
};

void ASmvGameRulesActor::BuildHud()
{
	if (!GEngine || !GEngine->GameViewport || !Impl.IsValid() || Impl->Kind == SmvCore::EGameKind::Free) return;
	TSharedRef<SSmvGameHud> H = SNew(SSmvGameHud).Owner(this);
	HudHost = SNew(SWeakWidget).PossiblyNullContent(H);
	GEngine->GameViewport->AddViewportWidgetContent(HudHost.ToSharedRef(), 6);
}

void ASmvGameRulesActor::EndPlay(const EEndPlayReason::Type Reason)
{
	if (HudHost.IsValid() && GEngine && GEngine->GameViewport) GEngine->GameViewport->RemoveViewportWidgetContent(HudHost.ToSharedRef());
	HudHost.Reset();
	for (TObjectPtr<ASmvDroneActor>& T : ThreatActors) if (T) T->Destroy();
	ThreatActors.Reset();
	Super::EndPlay(Reason);
}
