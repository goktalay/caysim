#include "SmvWorldActors.h"
#include "SimumavModule.h"
#include "SmvMotorSynth.h"
#include "CaySimGeo.h"
#include "SmvRace.h"
#include "SmvPyRandom.h"
#include "ProceduralMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const TCHAR* GSmvCube = TEXT("/Engine/BasicShapes/Cube.Cube");
	const TCHAR* GSmvCyl = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	const TCHAR* GSmvSphere = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	const TCHAR* GSmvCone = TEXT("/Engine/BasicShapes/Cone.Cone");
	const TCHAR* GSmvMat = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
	/** Editor Python betiğinin ürettiği malzemeler (varsa) — yoksa BasicShapeMaterial */
	const TCHAR* GSmvTerrainMat = TEXT("/Game/Simumav/Materials/M_SmvTerrain.M_SmvTerrain");
	const TCHAR* GSmvGlowMat = TEXT("/Game/Simumav/Materials/M_SmvGlow.M_SmvGlow");
}

// ---------------------------------------------------------------------------
// ASmvDroneActor
// ---------------------------------------------------------------------------

ASmvDroneActor::ASmvDroneActor()
{
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(GSmvCube);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cyl(GSmvCyl);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sph(GSmvSphere);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(GSmvCone);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Mat(GSmvMat);
	CubeMesh = Cube.Object; CylinderMesh = Cyl.Object; SphereMesh = Sph.Object; ConeMesh = Cone.Object; BaseMaterial = Mat.Object;
	Motor = CreateDefaultSubobject<USmvMotorSynth>(TEXT("Motor"));
	Motor->SetupAttachment(Body);
}

UStaticMeshComponent* ASmvDroneActor::AddPart(UStaticMesh* M, const FVector& PosM, const FVector& ScaleM, const FRotator& Rot, const FLinearColor& Color)
{
	if (!M) return nullptr;
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetStaticMesh(M);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetupAttachment(Body);
	C->RegisterComponent();
	// BasicShapes 100 cm birim ağlar → ölçek (m)
	C->SetRelativeLocationAndRotation(PosM * 100.f, Rot);
	C->SetRelativeScale3D(ScaleM);
	if (BaseMaterial)
	{
		UMaterialInstanceDynamic* D = UMaterialInstanceDynamic::Create(BaseMaterial, C);
		D->SetVectorParameterValue(TEXT("Color"), Color);
		C->SetMaterial(0, D);
	}
	Parts.Add(C);
	return C;
}

void ASmvDroneActor::ConfigureVisual(const FString& InType, const FString& /*ModelKey*/, const FLinearColor& Color)
{
	SmvType = InType;
	BodyColor = Color;
	if (Motor)
	{
		ESmvEngineKind K = ESmvEngineKind::ElectricMulti;
		int32 Rotors = 4;
		if (InType == TEXT("plane")) K = ESmvEngineKind::ElectricProp;
		else if (InType == TEXT("interceptor")) K = ESmvEngineKind::ElectricProp;
		else if (InType == TEXT("rover")) K = ESmvEngineKind::Rover;
		else if (InType == TEXT("heli")) { K = ESmvEngineKind::Combustion; Rotors = 2; }
		Motor->SetKind(K, Rotors);
		Motor->Start();
	}
	BuildPlaceholder();
}

void ASmvDroneActor::BuildPlaceholder()
{
	Super::BuildPlaceholder();   // dahili (BP) mesh yoksa gövde gizli kalır
	for (TObjectPtr<UStaticMeshComponent>& P : Parts) if (P) P->DestroyComponent();
	Parts.Reset();
	if (!IsUsingPlaceholder()) return;
	if (PlaceholderMesh && PlaceholderMesh->GetStaticMesh()) return;   // Blueprint gövdesi atanmış
	const FLinearColor Dark(0.05f, 0.05f, 0.06f), Grey(0.4f, 0.4f, 0.42f), Glass(0.1f, 0.2f, 0.35f);
	Propellers.Reset();
	auto Prop = [this, &Dark](const FVector& P, float DiaM, const FRotator& R)
	{
		if (UStaticMeshComponent* C = AddPart(CylinderMesh, P, FVector(DiaM, DiaM, 0.01f), R, Dark)) Propellers.Add(C);
	};
	if (SmvType == TEXT("plane") || SmvType == TEXT("vtol") || SmvType == TEXT("interceptor"))
	{
		AddPart(CylinderMesh, FVector(0, 0, 0), FVector(0.12f, 0.12f, 1.0f), FRotator(90, 0, 0), BodyColor);   // gövde (X boyunca)
		AddPart(CubeMesh, FVector(0.05f, 0, 0.03f), FVector(0.25f, 1.6f, 0.02f), FRotator::ZeroRotator, BodyColor);   // kanat
		AddPart(CubeMesh, FVector(-0.45f, 0, 0.03f), FVector(0.12f, 0.5f, 0.015f), FRotator::ZeroRotator, BodyColor);   // yatay kuyruk
		AddPart(CubeMesh, FVector(-0.45f, 0, 0.1f), FVector(0.12f, 0.015f, 0.15f), FRotator::ZeroRotator, BodyColor);   // dikey kuyruk
		AddPart(SphereMesh, FVector(0.2f, 0, 0.06f), FVector(0.12f, 0.08f, 0.06f), FRotator::ZeroRotator, Glass);
		Prop(FVector(0.52f, 0, 0), 0.25f, FRotator(90, 0, 0));
		if (SmvType == TEXT("vtol"))
		{
			AddPart(CubeMesh, FVector(0.05f, 0.4f, 0.0f), FVector(0.8f, 0.03f, 0.03f), FRotator::ZeroRotator, Grey);
			AddPart(CubeMesh, FVector(0.05f, -0.4f, 0.0f), FVector(0.8f, 0.03f, 0.03f), FRotator::ZeroRotator, Grey);
			for (const FVector& P : { FVector(0.4f, 0.4f, 0.05f), FVector(-0.3f, 0.4f, 0.05f), FVector(0.4f, -0.4f, 0.05f), FVector(-0.3f, -0.4f, 0.05f) }) Prop(P, 0.3f, FRotator::ZeroRotator);
		}
		GroundOffsetM = -0.08f;
		return;
	}
	if (SmvType == TEXT("rover"))
	{
		AddPart(CubeMesh, FVector(0, 0, 0.25f), FVector(1.8f, 0.9f, 0.4f), FRotator::ZeroRotator, BodyColor);
		AddPart(CubeMesh, FVector(0.3f, 0, 0.55f), FVector(0.6f, 0.7f, 0.25f), FRotator::ZeroRotator, Glass);
		for (const FVector& P : { FVector(0.6f, 0.5f, 0.15f), FVector(-0.6f, 0.5f, 0.15f), FVector(0.6f, -0.5f, 0.15f), FVector(-0.6f, -0.5f, 0.15f) })
			AddPart(CylinderMesh, P, FVector(0.3f, 0.3f, 0.15f), FRotator(0, 0, 90), Dark);
		GroundOffsetM = 0.f;
		return;
	}
	if (SmvType == TEXT("heli"))
	{
		AddPart(SphereMesh, FVector(0, 0, 0), FVector(0.6f, 0.3f, 0.3f), FRotator::ZeroRotator, BodyColor);
		AddPart(CylinderMesh, FVector(-0.6f, 0, 0.05f), FVector(0.05f, 0.05f, 0.8f), FRotator(90, 0, 0), BodyColor);
		AddPart(CubeMesh, FVector(0, 0.15f, -0.2f), FVector(0.6f, 0.02f, 0.02f), FRotator::ZeroRotator, Dark);
		AddPart(CubeMesh, FVector(0, -0.15f, -0.2f), FVector(0.6f, 0.02f, 0.02f), FRotator::ZeroRotator, Dark);
		Prop(FVector(0, 0, 0.22f), 1.7f, FRotator::ZeroRotator);
		Prop(FVector(-1.0f, 0.05f, 0.1f), 0.3f, FRotator(0, 0, 90));
		GroundOffsetM = -0.22f;
		return;
	}
	// çok rotor (X düzeni; FPV daha küçük)
	const bool bFpv = SmvType == TEXT("fpv");
	const float Arm = bFpv ? 0.12f : 0.25f;
	AddPart(CubeMesh, FVector(0, 0, 0), FVector(bFpv ? 0.12f : 0.2f, bFpv ? 0.08f : 0.14f, 0.06f), FRotator::ZeroRotator, BodyColor);
	AddPart(SphereMesh, FVector(bFpv ? 0.07f : 0.11f, 0, 0.01f), FVector(0.04f, 0.04f, 0.04f), FRotator::ZeroRotator, Glass);   // kamera
	for (int32 i = 0; i < 4; ++i)
	{
		const float A = FMath::DegreesToRadians(45.f + 90.f * i);
		const FVector Tip(FMath::Cos(A) * Arm, FMath::Sin(A) * Arm, 0.02f);
		AddPart(CubeMesh, Tip * 0.5f, FVector(Arm * 1.0f, 0.02f, 0.015f), FRotator(0, FMath::RadiansToDegrees(A), 0), Grey);
		AddPart(CylinderMesh, Tip, FVector(0.035f, 0.035f, 0.03f), FRotator::ZeroRotator, Dark);
		Prop(Tip + FVector(0, 0, 0.03f), bFpv ? 0.13f : 0.25f, FRotator::ZeroRotator);
	}
	AddPart(CubeMesh, FVector(0, 0.08f, -0.08f), FVector(0.2f, 0.01f, 0.01f), FRotator::ZeroRotator, Dark);   // iniş takımı
	AddPart(CubeMesh, FVector(0, -0.08f, -0.08f), FVector(0.2f, 0.01f, 0.01f), FRotator::ZeroRotator, Dark);
	GroundOffsetM = -0.09f;
}

void ASmvDroneActor::UpdateAudio(const FVector& Listener, float Volume)
{
	if (!Motor) return;
	const FCaySimTelemetry& T = GetLatestTelemetry();
	const FVector Pos = GetActorLocation();
	const float Dt = FMath::Max(GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f, 1e-3f);
	const FVector Vel = (Pos - LastPos) / Dt / 100.f;   // m/s
	LastPos = Pos;
	const FVector ToL = (Listener - Pos).GetSafeNormal();
	const float Vr = (float)FVector::DotProduct(Vel, ToL);   // dinleyiciye yaklaşma hızı
	const float Doppler = 343.f / FMath::Max(100.f, 343.f - Vr);
	const float DistM = (float)(FVector::Dist(Listener, Pos) / 100.0);
	const float Atten = FMath::Clamp(30.f / FMath::Max(DistM, 1.f), 0.02f, 1.f);
	Motor->SetParams(T.Throttle / 100.f, T.Airspeed > 0 ? T.Airspeed : T.Groundspeed, T.bArmed, Doppler, Volume * Atten);
}

// ---------------------------------------------------------------------------
// ASmvGateActor — prosedürel torus
// ---------------------------------------------------------------------------

ASmvGateActor::ASmvGateActor()
{
	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Torus"));
	SetRootComponent(Mesh);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->bUseAsyncCooking = true;
}

void ASmvGateActor::Build(float RadiusM, float TubeM, const FLinearColor& Color)
{
	Base = Color;
	const int32 Seg = 48, Side = 12;
	TArray<FVector> V; TArray<int32> Tri; TArray<FVector> N; TArray<FVector2D> UV; TArray<FLinearColor> C; TArray<FProcMeshTangent> T;
	const float R = RadiusM * 100.f, r = TubeM * 100.f;
	for (int32 i = 0; i <= Seg; ++i)
	{
		const float U = 2.f * UE_PI * i / Seg;
		// halka düzlemi YZ (normal +X = geçiş yönü; aktör yaw'ı halka yönüne çevrilir)
		const FVector Ctr(0.f, R * FMath::Cos(U), R * FMath::Sin(U));
		for (int32 j = 0; j <= Side; ++j)
		{
			const float W = 2.f * UE_PI * j / Side;
			const FVector Radial = FVector(0.f, FMath::Cos(U), FMath::Sin(U));
			const FVector Nrm = Radial * FMath::Cos(W) + FVector(1, 0, 0) * FMath::Sin(W);
			V.Add(Ctr + Nrm * r);
			N.Add(Nrm);
			UV.Add(FVector2D((float)i / Seg, (float)j / Side));
			C.Add(Color);
			T.Add(FProcMeshTangent(FVector(0.f, -FMath::Sin(U), FMath::Cos(U)), false));
		}
	}
	for (int32 i = 0; i < Seg; ++i)
		for (int32 j = 0; j < Side; ++j)
		{
			const int32 A = i * (Side + 1) + j, B = (i + 1) * (Side + 1) + j;
			Tri.Append({ A, B, A + 1, B, B + 1, A + 1 });
		}
	Mesh->CreateMeshSection_LinearColor(0, V, Tri, N, UV, C, T, false);
	UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, GSmvGlowMat);
	if (!M) M = LoadObject<UMaterialInterface>(nullptr, GSmvMat);
	if (M)
	{
		Mat = UMaterialInstanceDynamic::Create(M, this);
		Mat->SetVectorParameterValue(TEXT("Color"), Color);
		Mesh->SetMaterial(0, Mat);
	}
}

void ASmvGateActor::SetState(int32 State)
{
	if (!Mat) return;
	const FLinearColor C = State == 1 ? FLinearColor(0.1f, 1.f, 0.2f) : State == 2 ? FLinearColor(0.3f, 0.3f, 0.3f) : Base;
	Mat->SetVectorParameterValue(TEXT("Color"), C);
	Mat->SetScalarParameterValue(TEXT("Glow"), State == 1 ? 8.f : 1.f);
}

// ---------------------------------------------------------------------------
// ASmvProcTerrain
// ---------------------------------------------------------------------------

ASmvProcTerrain::ASmvProcTerrain()
{
	Ground = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Ground"));
	SetRootComponent(Ground);
	Ground->bUseAsyncCooking = true;
	TreeTrunks = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("TreeTrunks"));
	TreeTrunks->SetupAttachment(Ground);
	TreeCrowns = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("TreeCrowns"));
	TreeCrowns->SetupAttachment(Ground);
	Buildings = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Buildings"));
	Buildings->SetupAttachment(Ground);
	Runway = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Runway"));
	Runway->SetupAttachment(Ground);
	WaterPlane = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Water"));
	WaterPlane->SetupAttachment(Ground);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(GSmvCube);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cyl(GSmvCyl);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(GSmvCone);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Mat(GSmvMat);
	TreeTrunks->SetStaticMesh(Cyl.Object);
	TreeCrowns->SetStaticMesh(Cone.Object);
	Buildings->SetStaticMesh(Cube.Object);
	Runway->SetStaticMesh(Cube.Object);
	WaterPlane->SetStaticMesh(Cube.Object);
	BaseMaterial = Mat.Object;
	TreeTrunks->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	TreeCrowns->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Buildings->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Buildings->ComponentTags.Add(TEXT("building"));
	WaterPlane->ComponentTags.Add(TEXT("water"));
	WaterPlane->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
}

double ASmvProcTerrain::Noise2(double X, double Y) const
{
	// değer gürültüsü (tohumlu, 3 oktav)
	auto Hash = [this](int32 Ix, int32 Iy) { uint32 H = (uint32)(Ix * 374761393 + Iy * 668265263) ^ (uint32)(Seed * 2246822519u); H = (H ^ (H >> 13)) * 1274126177u; return ((H ^ (H >> 16)) & 0xffff) / 65535.0; };
	double Sum = 0, Amp = 1, Freq = 1, Norm = 0;
	for (int32 O = 0; O < 3; ++O)
	{
		const double Fx = X * Freq, Fy = Y * Freq;
		const int32 Ix = FMath::FloorToInt(Fx), Iy = FMath::FloorToInt(Fy);
		const double Tx = Fx - Ix, Ty = Fy - Iy;
		const double Sx = Tx * Tx * (3 - 2 * Tx), Sy = Ty * Ty * (3 - 2 * Ty);
		const double A = Hash(Ix, Iy), B = Hash(Ix + 1, Iy), C = Hash(Ix, Iy + 1), D = Hash(Ix + 1, Iy + 1);
		Sum += Amp * FMath::Lerp(FMath::Lerp(A, B, Sx), FMath::Lerp(C, D, Sx), Sy);
		Norm += Amp; Amp *= 0.5; Freq *= 2;
	}
	return Sum / Norm;
}

double ASmvProcTerrain::HeightAt(double E, double N) const
{
	const double Dist = FMath::Sqrt(E * E + N * N);
	double H = (Noise2(E / 600.0, N / 600.0) - 0.5) * 30.0;   // hafif dalgalı ova
	for (const FMountain& M : Mountains)
	{
		const double D = FMath::Sqrt((E - M.E) * (E - M.E) + (N - M.N) * (N - M.N));
		if (D < M.R) H += M.H * FMath::Square(FMath::Cos(D / M.R * UE_DOUBLE_PI * 0.5));
	}
	// ev çevresi düz (SITL'in düz zemini) — 300 m'den 600 m'ye yumuşak geçiş
	const double Blend = FMath::Clamp((Dist - FlatR) / FlatR, 0.0, 1.0);
	return H * Blend * Blend;
}

void ASmvProcTerrain::Build(const FSmvTrack& Track, double GroundMsl, AActor* GeoActor)
{
	ICaySimGeoTransform* Geo = Cast<ICaySimGeoTransform>(GeoActor);
	if (!Geo) { UE_LOG(LogSimumav, Warning, TEXT("Prosedürel dünya: dönüşüm aktörü yok")); return; }
	W = Track.World;
	Seed = W.Seed ? W.Seed : 1;
	const double Half = FMath::Clamp(W.SizeM, 1000.0, 12000.0) / 2.0;
	SmvCore::FPyRandom Rng((uint64_t)Seed);
	Mountains.Reset();
	for (int32 i = 0; i < FMath::Clamp(W.MountainCount, 0, 12); ++i)
	{
		const double A = Rng.Uniform(0.0, 2.0 * UE_DOUBLE_PI), R = Rng.Uniform(0.45, 0.95) * Half;
		Mountains.Add({ R * FMath::Sin(A), R * FMath::Cos(A), W.MountainHeight * Rng.Uniform(0.5, 1.0), Rng.Uniform(0.15, 0.35) * Half });
	}
	auto ToUe = [Geo, &Track, GroundMsl](double E, double N, double H)
	{
		double Lat, Lon;
		SmvCore::LatLon(Track.Lat, Track.Lon, E, N, Lat, Lon);
		return Geo->GeoToUnreal(Lon, Lat, GroundMsl + H);
	};
	const FVector Origin = ToUe(0, 0, 0);
	SetActorLocation(Origin);
	// arazi ızgarası
	const int32 Grid = 160;
	const double Step = 2 * Half / Grid;
	TArray<FVector> V; TArray<int32> Tri; TArray<FVector> Nrm; TArray<FVector2D> UV; TArray<FLinearColor> Col; TArray<FProcMeshTangent> Tan;
	V.Reserve((Grid + 1) * (Grid + 1));
	for (int32 y = 0; y <= Grid; ++y)
		for (int32 x = 0; x <= Grid; ++x)
		{
			const double E = -Half + x * Step, N = -Half + y * Step;
			const double H = HeightAt(E, N);
			V.Add(ToUe(E, N, H) - Origin);
			UV.Add(FVector2D(x / 8.f, y / 8.f));
			const float Tint = (float)FMath::Clamp(H / FMath::Max(1.0, W.MountainHeight), 0.0, 1.0);
			Col.Add(FMath::Lerp(FLinearColor(0.18f, 0.32f, 0.12f), FLinearColor(0.45f, 0.42f, 0.38f), Tint));
		}
	for (int32 y = 0; y < Grid; ++y)
		for (int32 x = 0; x < Grid; ++x)
		{
			const int32 A = y * (Grid + 1) + x, B = A + 1, C = A + Grid + 1, D = C + 1;
			Tri.Append({ A, C, B, B, C, D });
		}
	// normaller (yüz normallerinin ortalaması)
	Nrm.Init(FVector::ZeroVector, V.Num());
	for (int32 i = 0; i < Tri.Num(); i += 3)
	{
		const FVector Fn = FVector::CrossProduct(V[Tri[i + 2]] - V[Tri[i]], V[Tri[i + 1]] - V[Tri[i]]).GetSafeNormal();
		Nrm[Tri[i]] += Fn; Nrm[Tri[i + 1]] += Fn; Nrm[Tri[i + 2]] += Fn;
	}
	for (FVector& X : Nrm) { X = X.GetSafeNormal(); if (X.Z < 0) X = -X; }
	Ground->CreateMeshSection_LinearColor(0, V, Tri, Nrm, UV, Col, Tan, true);
	UMaterialInterface* TerrainMat = LoadObject<UMaterialInterface>(nullptr, GSmvTerrainMat);
	if (TerrainMat) Ground->SetMaterial(0, TerrainMat);
	else if (BaseMaterial)
	{
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		M->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.2f, 0.33f, 0.14f));
		Ground->SetMaterial(0, M);
	}
	auto Tinted = [this](UMeshComponent* C, const FLinearColor& Color)
	{
		if (!BaseMaterial) return;
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		M->SetVectorParameterValue(TEXT("Color"), Color);
		C->SetMaterial(0, M);
	};
	Tinted(TreeTrunks, FLinearColor(0.25f, 0.15f, 0.07f));
	Tinted(TreeCrowns, FLinearColor(0.05f, 0.22f, 0.06f));
	Tinted(Buildings, FLinearColor(0.6f, 0.58f, 0.55f));
	Tinted(Runway, FLinearColor(0.08f, 0.08f, 0.09f));
	Tinted(WaterPlane, FLinearColor(0.05f, 0.18f, 0.3f));

	// ağaçlar ve binalar (ev çevresinin dışında)
	TreeTrunks->ClearInstances(); TreeCrowns->ClearInstances(); Buildings->ClearInstances();
	const int32 Trees = FMath::Clamp((int32)(W.TreeDensity * 1500.0), 0, 4000);
	for (int32 i = 0; i < Trees; ++i)
	{
		const double E = Rng.Uniform(-Half, Half), N = Rng.Uniform(-Half, Half);
		if (FMath::Sqrt(E * E + N * N) < FlatR * 1.2) continue;
		const double H = HeightAt(E, N);
		const float Tall = (float)Rng.Uniform(6.0, 16.0);
		const FVector P = ToUe(E, N, H) - Origin;
		TreeTrunks->AddInstance(FTransform(FRotator::ZeroRotator, P + FVector(0, 0, Tall * 20.f), FVector(0.3f, 0.3f, Tall * 0.4f)));
		TreeCrowns->AddInstance(FTransform(FRotator::ZeroRotator, P + FVector(0, 0, Tall * 75.f), FVector(Tall * 0.35f, Tall * 0.35f, Tall * 0.7f)));
	}
	for (int32 i = 0; i < FMath::Clamp(W.BuildingCount, 0, 400); ++i)
	{
		const double A = Rng.Uniform(0.0, 2.0 * UE_DOUBLE_PI), R = Rng.Uniform(FlatR * 1.3, Half * 0.6);
		const double E = R * FMath::Sin(A), N = R * FMath::Cos(A);
		const FVector P = ToUe(E, N, HeightAt(E, N)) - Origin;
		const FVector S((float)Rng.Uniform(0.08, 0.3), (float)Rng.Uniform(0.08, 0.3), (float)Rng.Uniform(0.06, 0.4));
		Buildings->AddInstance(FTransform(FRotator(0, (float)Rng.Uniform(0, 90), 0), P + FVector(0, 0, S.Z * 5000.f), S * 100.f));
	}
	// pist (ev noktasından pist yönünde, 600 × 30 m)
	Runway->SetVisibility(W.bRunway);
	if (W.bRunway)
	{
		const double H = FMath::DegreesToRadians(W.RunwayHeading);
		const FVector Fwd = ToUe(FMath::Sin(H) * 100.0, FMath::Cos(H) * 100.0, 0) - Origin;
		Runway->SetRelativeLocation(Fwd * 2.5f + FVector(0, 0, 10.f));
		Runway->SetRelativeRotation(FRotator(0, Fwd.Rotation().Yaw, 0));
		Runway->SetRelativeScale3D(FVector(600.f, 30.f, 0.2f));   // 600 × 30 m (BasicShapes küpü 1 m)
	}
	// su: alçak kesimleri kaplayan düzlem (water oranıyla)
	WaterPlane->SetVisibility(W.Water > 0.01);
	if (W.Water > 0.01)
	{
		const FVector Wc = ToUe(Half * 0.55, -Half * 0.5, -2.0) - Origin;
		WaterPlane->SetRelativeLocation(Wc);
		const float S = (float)(Half * W.Water * 2.0);
		WaterPlane->SetRelativeScale3D(FVector(S, S, 0.01f));
	}
}
