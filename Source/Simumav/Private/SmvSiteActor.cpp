#include "SmvSiteActor.h"
#include "Components/StaticMeshComponent.h"
#include "SmvUiKit.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "SmvHoliday.h"

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
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(SmvSolidMat(BaseMat), this);
		SmvSetSolidColor(M, Color);
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

// ---------------------------------------------------------------------------
// rüzgâr tulumu
// ---------------------------------------------------------------------------

ASmvWindSock::ASmvWindSock()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Pivot = CreateDefaultSubobject<USceneComponent>(TEXT("Pivot"));
	Pivot->SetupAttachment(Root);
	Pivot->SetRelativeLocation(FVector(0, 0, 600));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cyl(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Mat(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	auto Make = [&](const TCHAR* Name, USceneComponent* Parent, const FVector& Pos, const FVector& Sc, const FRotator& R, const FLinearColor& Col) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		C->SetupAttachment(Parent);
		C->SetStaticMesh(Cyl.Object);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetRelativeTransform(FTransform(R, Pos, Sc));
		if (Mat.Object) C->SetMaterial(0, Mat.Object);
		C->ComponentTags.Add(FName(*Col.ToString()));
		return C;
	};
	Make(TEXT("Pole"), Root, FVector(0, 0, 300), FVector(0.08f, 0.08f, 6.f), FRotator::ZeroRotator, FLinearColor(0.7f, 0.7f, 0.72f));
	for (int32 i = 0; i < 5; ++i)
	{
		const float D = 0.55f - i * 0.07f;
		UStaticMeshComponent* C = Make(*FString::Printf(TEXT("Ring%d"), i), Pivot, FVector(30.f + i * 55.f, 0, 0), FVector(D, D, 0.52f), FRotator(90, 0, 0),
			(i % 2) ? FLinearColor(0.95f, 0.95f, 0.95f) : FLinearColor(1.f, 0.35f, 0.05f));
		Rings.Add(C);
	}
}

void ASmvWindSock::Tick(float Dt)
{
	Super::Tick(Dt);
	T += Dt;
	const float Inf = FMath::Clamp(WindMs / 7.7f, 0.12f, 1.f);
	const float TargetDroop = (1.f - Inf) * 78.f;
	Droop = FMath::FInterpTo(Droop, TargetDroop, Dt, 1.5f);
	// tulum rüzgârın GİTTİĞİ yöne döner: kerteriz (geldiği + 180) → Unreal yaw (X doğu, Y güney) = kerteriz − 90
	const float TargetYaw = WindFrom + 180.f - 90.f;
	Yaw = FMath::FInterpTo(Yaw + FMath::FindDeltaAngleDegrees(Yaw, TargetYaw), TargetYaw, Dt, 1.2f);
	const float Flutter = FMath::Clamp(0.04f + WindMs * 0.012f, 0.f, 0.25f);
	Pivot->SetRelativeRotation(FRotator(-Droop + FMath::Sin(T * 7.f) * Flutter * 20.f, Yaw + FMath::Sin(T * 3.1f) * Flutter * 25.f, 0.f));
}

void ASmvWindSock::BeginPlay()
{
	Super::BeginPlay();
	// renkler çalışma zamanında (CDO'da dinamik malzeme oluşturulamaz): bileşen etiketindeki renk
	TInlineComponentArray<UStaticMeshComponent*> Comps(this);
	for (UStaticMeshComponent* C : Comps)
	{
		if (!C || C->ComponentTags.Num() == 0) continue;
		FLinearColor Col;
		Col.InitFromString(C->ComponentTags[0].ToString());
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(SmvSolidMat(C->GetMaterial(0)), this);
		C->SetMaterial(0, M);
		if (M) SmvSetSolidColor(M, Col);
	}
}

// ---------------------------------------------------------------------------
// bayram süsleri
// ---------------------------------------------------------------------------

ASmvHolidayDecor::ASmvHolidayDecor()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cyl(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sph(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Mat(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	CubeMesh = Cube.Object; CylMesh = Cyl.Object; SphMesh = Sph.Object; ConeMesh = Cone.Object; BaseMat = Mat.Object;
}

UStaticMeshComponent* ASmvHolidayDecor::Part(int32 Shape, const FVector& Pos, const FVector& SizeCm, const FLinearColor& Color, const FRotator& Rot)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetupAttachment(Root);
	C->SetStaticMesh(Shape == 0 ? CubeMesh.Get() : Shape == 1 ? CylMesh.Get() : Shape == 2 ? SphMesh.Get() : ConeMesh.Get());
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (BaseMat)
	{
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(SmvSolidMat(BaseMat), this);
		SmvSetSolidColor(M, Color);
		C->SetMaterial(0, M);
	}
	C->SetRelativeTransform(FTransform(Rot, Pos, SizeCm / 100.f));
	C->RegisterComponent();
	Parts.Add(C);
	return C;
}

void ASmvHolidayDecor::Build(const FString& Th, double Heading, TFunction<FVector(double, double, double)> ToWorld)
{
	const FVector Base = ToWorld(0, 0, 0);
	SetActorLocation(Base);
	const FLinearColor C1 = SmvHoliday::ThemeColor(Th), C2 = SmvHoliday::ThemeColor2(Th);
	const double Hd = FMath::DegreesToRadians(Heading);
	FRandomStream R(Th.Len() * 7919 + 17);
	int32 Placed = 0;
	for (int32 Try = 0; Try < 400 && Placed < 18; ++Try)
	{
		const double A = R.FRandRange(0.0, 2.0 * PI), D = R.FRandRange(45.0, 95.0);
		const double E = D * FMath::Sin(A), N = D * FMath::Cos(A);
		// pist ekseni tamponu (kalkış yönü boyunca ±25 m)
		const double Along = E * FMath::Sin(Hd) + N * FMath::Cos(Hd), Lat = E * FMath::Cos(Hd) - N * FMath::Sin(Hd);
		if (FMath::Abs(Lat) < 25.0) continue;
		// yer kontrol sahası tamponu (ileri −40, sağ 55–85)
		if (Along > -75.0 && Along < -5.0 && Lat > 20.0 && Lat < 120.0) continue;
		const FVector P = ToWorld(E, N, 0.0) - Base;
		const float Yaw = R.FRandRange(0.f, 360.f);
		++Placed;
		if (Th == TEXT("kurban"))
		{
			// koyun: gövde, baş, bacaklar
			Part(2, P + FVector(0, 0, 70), FVector(130, 80, 75), FLinearColor(0.92f, 0.9f, 0.85f), FRotator(0, Yaw, 0));
			const FVector Fw = FRotator(0, Yaw, 0).Vector();
			Part(2, P + Fw * 70 + FVector(0, 0, 90), FVector(40, 32, 38), FLinearColor(0.15f, 0.13f, 0.12f));
			for (int32 k = 0; k < 4; ++k) Part(1, P + FRotator(0, Yaw + 45 + k * 90, 0).Vector() * 38 + FVector(0, 0, 20), FVector(10, 10, 40), FLinearColor(0.15f, 0.13f, 0.12f));
		}
		else if (Th == TEXT("noel"))
		{
			Part(1, P + FVector(0, 0, 30), FVector(30, 30, 60), FLinearColor(0.35f, 0.2f, 0.1f));
			for (int32 k = 0; k < 3; ++k) Part(3, P + FVector(0, 0, 90 + k * 85), FVector(220 - k * 55, 220 - k * 55, 130), FLinearColor(0.05f, 0.4f, 0.15f));
			Part(2, P + FVector(0, 0, 360), FVector(30, 30, 30), FLinearColor(1.f, 0.85f, 0.2f));
			Part(0, P + FVector(90, 40, 20), FVector(40, 40, 40), C1, FRotator(0, Yaw, 0));
			Part(0, P + FVector(-70, 80, 15), FVector(30, 30, 30), FLinearColor(0.2f, 0.4f, 0.9f), FRotator(0, Yaw * 0.5f, 0));
		}
		else if (Th == TEXT("cadilar"))
		{
			for (int32 k = 0; k < 3; ++k) Part(2, P + FVector(k * 70 - 70, (k % 2) * 40, 30), FVector(70, 70, 55), FLinearColor(1.f, 0.42f, 0.05f));
			if (Placed % 4 == 0) { Part(1, P + FVector(0, -120, 120), FVector(10, 10, 240), FLinearColor(0.35f, 0.25f, 0.12f)); Part(0, P + FVector(0, -120, 190), FVector(140, 8, 8), FLinearColor(0.35f, 0.25f, 0.12f)); Part(2, P + FVector(0, -120, 255), FVector(40, 40, 40), FLinearColor(0.85f, 0.75f, 0.45f)); }
		}
		else if (Th == TEXT("paskalya"))
		{
			for (int32 k = 0; k < 4; ++k) Part(2, P + FVector(k * 45 - 70, R.FRandRange(-40.f, 40.f), 25), FVector(36, 36, 50), FLinearColor::MakeFromHSV8((uint8)R.RandRange(0, 255), 160, 240));
		}
		else if (Th == TEXT("ramazan") || Th == TEXT("hicri") || Th == TEXT("yilbasi") || Th == TEXT("cin_yeni") || Th == TEXT("diwali"))
		{
			// iki direk arası ışık / fener dizisi
			const FVector Side = FRotator(0, Yaw, 0).Vector() * 300.f;
			Part(1, P - Side + FVector(0, 0, 200), FVector(10, 10, 400), FLinearColor(0.3f, 0.3f, 0.32f));
			Part(1, P + Side + FVector(0, 0, 200), FVector(10, 10, 400), FLinearColor(0.3f, 0.3f, 0.32f));
			for (int32 k = 0; k <= 8; ++k)
			{
				const float U = k / 8.f;
				const FVector Q = FMath::Lerp(P - Side, P + Side, U) + FVector(0, 0, 380 - FMath::Sin(U * PI) * 70);
				const FLinearColor Lc = (Th == TEXT("ramazan") || Th == TEXT("hicri")) ? FLinearColor(1.f, 0.75f, 0.25f) : (k % 3 == 0 ? C1 : k % 3 == 1 ? C2 : FLinearColor(0.3f, 0.8f, 1.f));
				Part(Th == TEXT("ramazan") || Th == TEXT("hicri") ? 1 : 2, Q, FVector(22, 22, 30), Lc);
			}
		}
		else
		{
			// bayrak direği + iki renkli bez (dalgalanır)
			Part(1, P + FVector(0, 0, 400), FVector(10, 10, 800), FLinearColor(0.75f, 0.76f, 0.78f));
			Part(2, P + FVector(0, 0, 805), FVector(16, 16, 16), FLinearColor(0.9f, 0.8f, 0.3f));
			UStaticMeshComponent* Cl = Part(0, P + FVector(0, 0, 720), FVector(160, 4, 100), C1, FRotator(0, Yaw, 0));
			Cl->AddLocalOffset(FVector(80, 0, 0));
			Cloths.Add(Cl);
			UStaticMeshComponent* St = Part(0, P + FVector(0, 0, 720), FVector(50, 5, 50), C2, FRotator(0, Yaw, 0));
			St->AddLocalOffset(FVector(60, 0, 0));
		}
	}
}

void ASmvHolidayDecor::Tick(float Dt)
{
	Super::Tick(Dt);
	T += Dt;
	for (int32 i = 0; i < Cloths.Num(); ++i)
		if (Cloths[i]) Cloths[i]->SetRelativeScale3D(FVector(1.6f * (0.92f + 0.08f * FMath::Sin(T * 3.f + i)), 0.04f, 1.f));
}
