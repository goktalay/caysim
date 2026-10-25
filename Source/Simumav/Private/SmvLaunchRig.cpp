#include "SmvLaunchRig.h"
#include "SimumavModule.h"
#include "SmvWorldActors.h"
#include "SmvCityLife.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "HAL/PlatformTime.h"

namespace
{
	const FLinearColor GFrame(0.027f, 0.042f, 0.062f), GYellow(0.89f, 0.45f, 0.01f), GSteel(0.27f, 0.30f, 0.33f), GRed(0.58f, 0.03f, 0.025f);
	const FLinearColor GRubber(0.012f, 0.012f, 0.014f), GBlue(0.014f, 0.075f, 0.26f), GChrome(0.6f, 0.62f, 0.65f), GAsphalt(0.035f, 0.036f, 0.04f);
	constexpr double RailLenM = 6.0, RailBaseM = 1.05;   // ray boyu, ray arka ucu yüksekliği (m)
	double Smooth01(double X) { X = FMath::Clamp(X, 0.0, 1.0); return X * X * (3.0 - 2.0 * X); }
}

ASmvLaunchRig::ASmvLaunchRig()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cyl(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Mat(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	CubeMesh = Cube.Object; CylMesh = Cyl.Object; BaseMat = Mat.Object;
}

UStaticMeshComponent* ASmvLaunchRig::Part(UStaticMesh* M, USceneComponent* Parent, const FVector& PosM, const FVector& ScaleM, const FRotator& Rot, const FLinearColor& Color)
{
	if (!M) return nullptr;
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetStaticMesh(M);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);   // yalnız görsel (uçak çarpışma çözücüsü düzeneğe takılmasın)
	C->SetupAttachment(Parent ? Parent : Root.Get());
	C->RegisterComponent();
	C->SetRelativeLocationAndRotation(PosM * 100.0, Rot);
	C->SetRelativeScale3D(ScaleM);
	if (BaseMat)
	{
		UMaterialInstanceDynamic* D = UMaterialInstanceDynamic::Create(BaseMat, C);
		D->SetVectorParameterValue(TEXT("Color"), Color);
		C->SetMaterial(0, D);
	}
	return C;
}

void ASmvLaunchRig::Build(const FString& InMethod, const FVector& InHome, float YawDeg, float InRampDeg, ASmvDroneActor* InDrone)
{
	Method = InMethod.IsEmpty() ? FString(TEXT("wheels")) : InMethod;
	Home = InHome;
	Yaw = YawDeg;
	RampDeg = FMath::Clamp(InRampDeg, 10.f, 30.f);
	Drone = InDrone;
	Fwd = FRotator(0.f, Yaw, 0.f).Vector();
	SetActorLocationAndRotation(Home, FRotator(0.f, Yaw, 0.f));
	if (Method == TEXT("ramp")) BuildRamp();
	else if (Method == TEXT("vehicle")) BuildVehicle();
	else if (Method == TEXT("wheels")) BuildRunway();
	SetPhase(TEXT("idle"));
	UE_LOG(LogSimumav, Log, TEXT("Kalkış düzeneği: %s (yön %.0f°, rampa %.0f°)"), *Method, Yaw, RampDeg);
}

void ASmvLaunchRig::BuildRamp()
{
	// römork şasisi 1,7 × 4,2 m (uçak ev noktası rayın arka ucunda; ray öne doğru yükselir)
	const double Rear = -0.6;
	Part(CubeMesh, nullptr, FVector(Rear + 1.6, 0, 0.55), FVector(4.2, 1.7, 0.16), FRotator::ZeroRotator, GFrame);
	Part(CubeMesh, nullptr, FVector(Rear + 3.9, 0, 0.5), FVector(0.9, 0.12, 0.1), FRotator::ZeroRotator, GFrame);   // çeki oku
	for (int32 i = 0; i < 4; ++i)
	{
		const double X = Rear + (i < 2 ? 0.4 : 1.2), Y = (i % 2 ? 1 : -1) * 0.95;
		Part(CylMesh, nullptr, FVector(X, Y, 0.36), FVector(0.72, 0.72, 0.24), FRotator(0, 0, 90), GRubber);
	}
	for (int32 i = 0; i < 4; ++i)   // sarı denge ayakları
	{
		const double X = Rear + (i < 2 ? -0.35 : 3.4), Y = (i % 2 ? 1 : -1) * 0.8;
		Part(CubeMesh, nullptr, FVector(X, Y, 0.27), FVector(0.1, 0.1, 0.5), FRotator::ZeroRotator, GYellow);
		Part(CubeMesh, nullptr, FVector(X, Y, 0.02), FVector(0.3, 0.3, 0.04), FRotator::ZeroRotator, GYellow);
	}
	Part(CylMesh, nullptr, FVector(Rear + 0.2, 0, 0.66), FVector(1.6, 1.6, 0.08), FRotator::ZeroRotator, GSteel);   // azimut tablası
	// eğim pivotu: ray, kızak, pnömatik tüp
	RailPivot = NewObject<USceneComponent>(this);
	RailPivot->SetupAttachment(Root);
	RailPivot->RegisterComponent();
	RailPivot->SetRelativeLocationAndRotation(FVector(Rear * 100.0, 0, RailBaseM * 100.0 - 30.0), FRotator(RampDeg, 0, 0));
	Part(CubeMesh, RailPivot, FVector(RailLenM * 0.5, 0, 0), FVector(RailLenM, 0.34, 0.12), FRotator::ZeroRotator, GSteel);
	Part(CubeMesh, RailPivot, FVector(RailLenM * 0.5, 0.2, 0.05), FVector(RailLenM, 0.05, 0.08), FRotator::ZeroRotator, GChrome);
	Part(CubeMesh, RailPivot, FVector(RailLenM * 0.5, -0.2, 0.05), FVector(RailLenM, 0.05, 0.08), FRotator::ZeroRotator, GChrome);
	Part(CylMesh, RailPivot, FVector(RailLenM * 0.45, 0, -0.2), FVector(0.22, 0.22, RailLenM * 0.85), FRotator(90, 0, 0), GYellow);   // pnömatik tüp
	Part(CubeMesh, RailPivot, FVector(RailLenM, 0, 0.08), FVector(0.12, 0.5, 0.2), FRotator::ZeroRotator, GRed);    // ray sonu tamponu
	Sled = Part(CubeMesh, RailPivot, FVector(0.35, 0, 0.12), FVector(0.66, 0.7, 0.1), FRotator::ZeroRotator, GYellow);
	// eğim silindiri (şasiden raya) + kompresör kutusu + işaret lambası
	Part(CylMesh, nullptr, FVector(Rear + 2.2, 0, 1.0), FVector(0.14, 0.14, 1.0), FRotator(-35, 0, 0), GChrome);
	Part(CubeMesh, nullptr, FVector(Rear + 3.0, 0.55, 0.8), FVector(0.5, 0.4, 0.35), FRotator::ZeroRotator, GYellow);
	Part(CylMesh, nullptr, FVector(Rear + 3.0, 0.55, 1.03), FVector(0.1, 0.1, 0.1), FRotator::ZeroRotator, FLinearColor(1.f, 0.35f, 0.02f));
}

void ASmvLaunchRig::BuildVehicle()
{
	Car = NewObject<USceneComponent>(this);
	Car->SetupAttachment(Root);
	Car->RegisterComponent();
	// sivil araç: City Traffic Pro modellerinden pikap / SUV / büyük gövde tercih edilir (GERAN pikabı değil); yoksa basit mavi pikap
	USkeletalMesh* Pick = nullptr;
	double BestLen = 0;
	for (USkeletalMesh* M : FSmvCityLife::CarModels())
	{
		if (!M) continue;
		const FString N = M->GetName().ToLower();
		const FBoxSphereBounds B = M->GetBounds();
		const double L = FMath::Max(B.BoxExtent.X, B.BoxExtent.Y) * 2.0;
		const bool bPref = N.Contains(TEXT("pickup")) || N.Contains(TEXT("truck")) || N.Contains(TEXT("suv")) || N.Contains(TEXT("van"));
		if (bPref) { Pick = M; break; }
		if (L > BestLen) { BestLen = L; Pick = M; }
	}
	if (Pick)
	{
		USkeletalMeshComponent* Sk = NewObject<USkeletalMeshComponent>(this);
		Sk->SetSkeletalMeshAsset(Pick);
		Sk->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Sk->SetupAttachment(Car);
		Sk->RegisterComponent();
		const FBoxSphereBounds B = Pick->GetBounds();
		// burun yönü: model uzun ekseni X değilse 90° çevir; far kemiği yoksa +X kabul
		const bool bAlongY = B.BoxExtent.Y > B.BoxExtent.X;
		Sk->SetRelativeRotation(FRotator(0.f, bAlongY ? -90.f : 0.f, 0.f));
		Sk->SetRelativeLocation(FVector(-B.Origin.X, -B.Origin.Y, -(B.Origin.Z - B.BoxExtent.Z)));
		RoofZ = B.BoxExtent.Z * 2.0 + 25.0;
		UE_LOG(LogSimumav, Log, TEXT("Araç üstü fırlatma aracı: %s (tavan %.0f cm)"), *Pick->GetName(), RoofZ);
	}
	else
	{
		Part(CubeMesh, Car, FVector(0, 0, 0.62), FVector(5.2, 1.6, 0.5), FRotator::ZeroRotator, GBlue);   // şasi + kasa tabanı
		Part(CubeMesh, Car, FVector(0.9, 0, 1.25), FVector(1.7, 1.56, 0.8), FRotator::ZeroRotator, GBlue);   // kabin
		Part(CubeMesh, Car, FVector(2.15, 0, 0.98), FVector(0.9, 1.56, 0.3), FRotator::ZeroRotator, GBlue);  // kaput
		Part(CubeMesh, Car, FVector(-1.4, 0.78, 1.05), FVector(2.2, 0.06, 0.4), FRotator::ZeroRotator, GBlue);
		Part(CubeMesh, Car, FVector(-1.4, -0.78, 1.05), FVector(2.2, 0.06, 0.4), FRotator::ZeroRotator, GBlue);
		Part(CubeMesh, Car, FVector(2.62, 0, 0.6), FVector(0.08, 1.62, 0.18), FRotator::ZeroRotator, GChrome);
		Part(CubeMesh, Car, FVector(-2.62, 0, 0.6), FVector(0.08, 1.62, 0.18), FRotator::ZeroRotator, GChrome);
		Part(CubeMesh, Car, FVector(1.0, 0, 1.66), FVector(1.4, 1.5, 0.04), FRotator::ZeroRotator, FLinearColor(0.02f, 0.03f, 0.04f));
		for (int32 i = 0; i < 4; ++i)
			Part(CylMesh, Car, FVector(i < 2 ? 1.7 : -1.6, (i % 2 ? 1 : -1) * 0.8, 0.42), FVector(0.84, 0.84, 0.28), FRotator(0, 0, 90), GRubber);
		RoofZ = 186.0;
	}
	// tavan beşiği: iki A destekli sarı kauçuk yuva
	for (int32 i = 0; i < 2; ++i)
	{
		const double X = i ? -0.6 : 0.7;
		Part(CubeMesh, Car, FVector(X, 0.45, RoofZ / 100.0 - 0.15), FVector(0.08, 0.08, 0.35), FRotator(0, 0, 18), GYellow);
		Part(CubeMesh, Car, FVector(X, -0.45, RoofZ / 100.0 - 0.15), FVector(0.08, 0.08, 0.35), FRotator(0, 0, -18), GYellow);
		Part(CubeMesh, Car, FVector(X, 0, RoofZ / 100.0 - 0.02), FVector(0.14, 0.7, 0.05), FRotator::ZeroRotator, GRubber);
	}
	Part(CylMesh, Car, FVector(0.9, 0, RoofZ / 100.0 - 0.18), FVector(0.12, 0.12, 0.08), FRotator::ZeroRotator, FLinearColor(1.f, 0.35f, 0.02f));   // işaret lambası
	Car->SetRelativeLocation(FVector(-60.0, 0, 0));   // uçak tavan ortasında (ev noktası)
}

void ASmvLaunchRig::BuildRunway()
{
	// ev noktasından kalkış yönünde 400 m asfalt şerit (40 m geriden başlar), kenar ve orta çizgiler; zeminden 2 cm yukarıda
	Part(CubeMesh, nullptr, FVector(160.0, 0, 0.0), FVector(400.0, 18.0, 0.04), FRotator::ZeroRotator, GAsphalt);
	Part(CubeMesh, nullptr, FVector(160.0, 8.6, 0.025), FVector(400.0, 0.3, 0.04), FRotator::ZeroRotator, FLinearColor(0.7f, 0.7f, 0.68f));
	Part(CubeMesh, nullptr, FVector(160.0, -8.6, 0.025), FVector(400.0, 0.3, 0.04), FRotator::ZeroRotator, FLinearColor(0.7f, 0.7f, 0.68f));
	for (int32 i = 0; i < 20; ++i)
		Part(CubeMesh, nullptr, FVector(-30.0 + i * 20.0, 0, 0.025), FVector(9.0, 0.35, 0.04), FRotator::ZeroRotator, FLinearColor(0.75f, 0.75f, 0.72f));
	for (int32 i = 0; i < 6; ++i)   // eşik çizgileri
	{
		Part(CubeMesh, nullptr, FVector(-36.0, -6.5 + i * 2.6, 0.025), FVector(6.0, 0.9, 0.04), FRotator::ZeroRotator, FLinearColor(0.75f, 0.75f, 0.72f));
	}
}

FVector ASmvLaunchRig::PilotSpot() const
{
	const FVector Right = FRotator(0.f, Yaw + 90.f, 0.f).Vector();
	return Home - Fwd * 55.0 + Right * 30.0;
}

void ASmvLaunchRig::SetPhase(const FString& InPhase)
{
	if (InPhase == Phase) return;
	// bırakma anındaki görsel ofset: SITL pozuna yumuşak geçişin başlangıcı
	if (InPhase == TEXT("released") && Drone.IsValid())
	{
		ReleaseAt = FPlatformTime::Seconds();
	}
	Phase = InPhase;
	PhaseAt = FPlatformTime::Seconds();
}

void ASmvLaunchRig::Tick(float Dt)
{
	Super::Tick(Dt);
	ASmvDroneActor* D = Drone.Get();
	if (!D || Method == TEXT("wheels")) { if (D) D->SetLaunchVisual(FVector::ZeroVector, 0.f); return; }
	const double Now = FPlatformTime::Seconds();
	// SITL uçağının ev noktasından kalkış yönünde aldığı yol (m) — görsel ofset konum çözümünden önceki ham poz yerine aktör konumundan
	const FVector P = D->GetRawRenderLoc();
	const double Dm = FVector::DotProduct(FVector(P.X - Home.X, P.Y - Home.Y, 0.0), Fwd) / 100.0;
	FVector Off = FVector::ZeroVector;
	float Pitch = 0.f;
	const bool bHeld = Phase == TEXT("idle") || Phase == TEXT("armed") || Phase == TEXT("launch");
	if (Method == TEXT("ramp"))
	{
		const double Tn = FMath::Tan(FMath::DegreesToRadians(RampDeg));
		const double Lh = RailLenM * FMath::Cos(FMath::DegreesToRadians(RampDeg));
		const double S = FMath::Clamp(Dm + 0.35, 0.0, Lh);
		const FVector OnRail = FVector(0, 0, (RailBaseM + 0.25 + S * Tn) * 100.0 - FMath::Max(0.0, (double)(P.Z - Home.Z)));
		if (Sled && bHeld) Sled->SetRelativeLocation(FVector(FMath::Clamp(S / FMath::Cos(FMath::DegreesToRadians(RampDeg)), 0.35, RailLenM - 0.3) * 100.0, 0, 12.0));
		if (bHeld && Dm < Lh) { Off = OnRail; Pitch = RampDeg; BlendFrom = Off; BlendPitchFrom = Pitch; }
		else
		{
			if (ReleaseAt < 0) ReleaseAt = Now;
			const double K = 1.0 - Smooth01((Now - ReleaseAt) / 0.6);
			Off = BlendFrom * K; Pitch = BlendPitchFrom * (float)K;
			// kızak 1 sn sonra 1,2 m/s ile geri döner
			if (Sled && Now - ReleaseAt > 1.0) { FVector L = Sled->GetRelativeLocation(); L.X = FMath::Max(35.0, L.X - 120.0 * Dt); Sled->SetRelativeLocation(L); }
		}
	}
	else if (Method == TEXT("vehicle"))
	{
		if (Car)
		{
			if (bHeld)
			{
				const double S = FMath::Max(0.0, Dm) * 100.0;
				CarV = Dt > 0 ? FMath::Max(0.0, (S - CarS) / Dt) : CarV;
				CarS = S;
			}
			else
			{
				CarV = FMath::Max(0.0, CarV - 400.0 * Dt);   // bırakma sonrası 4 m/s² fren
				CarS += CarV * Dt;
			}
			Car->SetRelativeLocation(FVector(CarS - 60.0, 0, 0));
		}
		if (bHeld) { Off = FVector(0, 0, RoofZ + 10.0 - FMath::Max(0.0, (double)(P.Z - Home.Z))); BlendFrom = Off; }
		else
		{
			if (ReleaseAt < 0) ReleaseAt = Now;
			Off = BlendFrom * (1.0 - Smooth01((Now - ReleaseAt) / 0.4));
		}
	}
	else if (Method == TEXT("hand"))
	{
		// pilotun omzu: +1,6 m, 0,55 m önde; atışta kol 0,25 sn geri, 0,45 sn ileri savrulur
		const FVector Shoulder = Fwd * 55.0 + FVector(0, 0, 160.0);
		if (Phase == TEXT("idle") || Phase == TEXT("armed")) { Off = Shoulder; Pitch = 6.f; BlendFrom = Off; }
		else if (Phase == TEXT("launch"))
		{
			const double T = Now - PhaseAt;
			const double Back = T < 0.25 ? Smooth01(T / 0.25) : 1.0 - Smooth01((T - 0.25) / 0.45);
			Off = Shoulder - Fwd * 35.0 * Back + FVector(0, 0, 15.0 * Back);
			Pitch = 8.f;
			BlendFrom = Off;
		}
		else
		{
			if (ReleaseAt < 0) ReleaseAt = Now;
			Off = BlendFrom * (1.0 - Smooth01((Now - ReleaseAt) / 1.2));
			Pitch = 8.f * (float)(1.0 - Smooth01((Now - ReleaseAt) / 1.2));
		}
	}
	D->SetLaunchVisual(Off, Pitch);
}
