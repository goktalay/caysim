#include "SmvSiteActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

ASmvSiteActor::ASmvSiteActor()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	RadarHead = CreateDefaultSubobject<USceneComponent>(TEXT("RadarHead"));
	RadarHead->SetupAttachment(Root);
	EoHead = CreateDefaultSubobject<USceneComponent>(TEXT("EoHead"));
	EoHead->SetupAttachment(Root);
	EoCap = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("EoCapture"));
	EoCap->SetupAttachment(Root);
	EoCap->bCaptureEveryFrame = false;
	EoCap->bCaptureOnMovement = false;
	EoCap->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cyl(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sph(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Mat(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	CubeMesh = Cube.Object; CylMesh = Cyl.Object; SphMesh = Sph.Object; BaseMat = Mat.Object;
}

UStaticMeshComponent* ASmvSiteActor::Part(USceneComponent* Parent, int32 Shape, const FVector& Pos, const FVector& SizeCm, const FLinearColor& Color, const FRotator& Rot)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetupAttachment(Parent ? Parent : Root.Get());
	C->SetStaticMesh(Shape == 0 ? CubeMesh.Get() : Shape == 1 ? CylMesh.Get() : SphMesh.Get());
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (BaseMat)
	{
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(BaseMat, this);
		M->SetVectorParameterValue(TEXT("Color"), Color);
		C->SetMaterial(0, M);
	}
	C->SetRelativeTransform(FTransform(Rot, Pos, SizeCm / 100.f));
	C->RegisterComponent();
	Parts.Add(C);
	return C;
}

void ASmvSiteActor::Build(const FSmvSite& Site, double Heading, TFunction<FVector(double, double, double)> ToWorld)
{
	Cfg = Site;
	T0 = FPlatformTime::Seconds();
	const double H = FMath::DegreesToRadians(Heading), Fx = FMath::Sin(H), Fy = FMath::Cos(H), Rx = FMath::Cos(H), Ry = -FMath::Sin(H);
	auto Place = [&](double F, double R) { return FVector2D(F * Fx + R * Rx, F * Fy + R * Ry); };
	const FVector2D St = Place(-40, 55);
	TowerXY = Place(-40, 85);
	// aktör kökü kule tabanında; parçalar dünya yönünde (kuzey = −Y değil: ToWorld ile hesaplanan noktalara göre yerleştirilir)
	const FVector Base = ToWorld(TowerXY.X, TowerXY.Y, 0.0);
	SetActorLocation(Base);
	auto Local = [&](double E, double N, double Up) { return ToWorld(E, N, Up) - Base; };
	const FLinearColor Olive(0.24f, 0.28f, 0.18f), Steel(0.55f, 0.57f, 0.6f), Dark(0.12f, 0.13f, 0.15f), White(0.85f, 0.86f, 0.88f);
	const float Yaw = (float)(Heading - 90.0);   // kerteriz (kuzeyden saat yönü) → Unreal yaw (X doğu, Y güney)
	if (Site.bStation)
	{
		const FVector S = Local(St.X, St.Y, 0.0);
		Part(nullptr, 0, S + FVector(0, 0, 130), FVector(600, 244, 260), Olive, FRotator(0, Yaw, 0));            // shelter
		Part(nullptr, 0, S + FVector(0, 0, 268), FVector(610, 254, 16), Dark, FRotator(0, Yaw, 0));               // çatı
		Part(nullptr, 1, S + FVector(260, 160, 600), FVector(12, 12, 1200), Steel);                               // anten direği
		Part(nullptr, 0, S + FVector(260, 160, 1200), FVector(80, 8, 8), Steel);
		Part(nullptr, 0, S + FVector(-420, 0, 70), FVector(180, 110, 140), Dark, FRotator(0, Yaw, 0));            // jeneratör
	}
	if (Site.bRadar || Site.bEo)
	{
		// kafes kule: 4 ayak (eğik değil, sade) + 6 kat çapraz
		const float Half = 160.f;
		for (int32 k = 0; k < 4; ++k)
		{
			const FVector L(k < 2 ? -Half : Half, (k % 2) ? -Half : Half, 0.f);
			Part(nullptr, 1, L + FVector(0, 0, 1000), FVector(14, 14, 2000), Steel);
		}
		for (int32 lv = 1; lv <= 6; ++lv)
		{
			const float Z = lv * 300.f;
			Part(nullptr, 0, FVector(0, -Half, Z), FVector(Half * 2, 8, 8), Steel);
			Part(nullptr, 0, FVector(0, Half, Z), FVector(Half * 2, 8, 8), Steel);
			Part(nullptr, 0, FVector(-Half, 0, Z), FVector(8, Half * 2, 8), Steel);
			Part(nullptr, 0, FVector(Half, 0, Z), FVector(8, Half * 2, 8), Steel);
		}
		Part(nullptr, 0, FVector(0, 0, 1790), FVector(420, 420, 14), Dark);   // platform (18 m)
		if (Site.bEo)
		{
			EoHead->SetRelativeLocation(FVector(140, 140, 1830));
			Part(EoHead, 1, FVector(0, 0, -20), FVector(30, 30, 40), Dark);
			Part(EoHead, 2, FVector::ZeroVector, FVector(60, 60, 60), White);
			Part(EoHead, 1, FVector(32, 0, 0), FVector(22, 22, 20), Dark, FRotator(90, 0, 0));
			EoRT = NewObject<UTextureRenderTarget2D>(this);
			EoRT->InitAutoFormat(640, 400);
			EoRT->UpdateResourceImmediate(true);
			EoCap->TextureTarget = EoRT;
		}
		if (Site.bRadar)
		{
			RadarHead->SetRelativeLocation(FVector(0, 0, 2150));
			Part(nullptr, 1, FVector(0, 0, 2000), FVector(30, 30, 300), Steel);
			Part(RadarHead, 0, FVector(0, 0, 40), FVector(40, 320, 110), White);      // anten yüzü
			Part(RadarHead, 0, FVector(-30, 0, 0), FVector(60, 60, 40), Dark);
		}
	}
}

double ASmvSiteActor::SweepDeg() const { return FMath::Fmod((FPlatformTime::Seconds() - T0) / 4.0 * 360.0, 360.0); }

void ASmvSiteActor::Tick(float Dt)
{
	Super::Tick(Dt);
	if (Cfg.bRadar) RadarHead->SetRelativeRotation(FRotator(0.f, (float)SweepDeg(), 0.f));
}

FVector ASmvSiteActor::EoWorld() const { return EoHead->GetComponentLocation(); }

void ASmvSiteActor::AimEo(const FVector& Target, float FovDeg, bool bCapture)
{
	if (!Cfg.bEo || !EoRT) return;
	const FVector From = EoWorld();
	const FRotator R = (Target - From).Rotation();
	EoHead->SetWorldRotation(FRotator(0.f, R.Yaw, 0.f));
	EoCap->SetWorldLocationAndRotation(From + R.Vector() * 60.0, R);
	EoCap->FOVAngle = FMath::Clamp(FovDeg, 0.35f, 48.f);
	if (bCapture) EoCap->CaptureScene();
}

void ASmvSiteActor::SetEoPost(const FPostProcessSettings& P)
{
	EoCap->PostProcessSettings = P;
	EoCap->PostProcessBlendWeight = 1.f;
}
