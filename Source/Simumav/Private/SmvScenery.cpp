#include "SmvScenery.h"
#include "SimumavModule.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "CollisionQueryParams.h"

namespace
{
	const TCHAR* GSmvScenerySection = TEXT("/Script/Simumav.SmvScenery");

	// CI (Tools/fab/fab-hazirla.ps1) bu bloktaki klasörleri diskte varsa DirectoriesToAlwaysCook'a ekler — biçimi bozmayın.
	const TCHAR* const GSmvSceneryCookDirs[] =
	{
		// <MANZARA-KLASORLERI>
		TEXT("/Game/Brushify/Meshes/Foliage"),
		TEXT("/Game/Brushify/Meshes/Ground/Forest/Rocks"),
		TEXT("/Game/Brushify/Meshes/Cliffs"),
		TEXT("/Game/Brushify/DistanceMeshes"),
		TEXT("/Game/_iraisynn_attinom/assets/rock1"),
		TEXT("/Game/_iraisynn_attinom/assets/rock2"),
		TEXT("/Game/_iraisynn_attinom/assets/grass"),
		TEXT("/Game/_iraisynn_attinom/assets/fern_02"),
		TEXT("/Game/_iraisynn_attinom/assets/weed_plant_02"),
		TEXT("/Game/FlyingAI/Meshes"),
		TEXT("/Game/FlyingAI/Materials"),
		TEXT("/Game/InteriorCubemapSystem"),
		// </MANZARA-KLASORLERI>
	};

	const TCHAR* const GTrees[] =
	{
		TEXT("/Game/Brushify/Meshes/Foliage/Trees/Birch/S_Birch_Large"),
		TEXT("/Game/Brushify/Meshes/Foliage/Trees/Birch/S_Birch_Medium"),
		TEXT("/Game/Brushify/Meshes/Foliage/Trees/Birch/S_Birch_Small"),
		TEXT("/Game/Brushify/Meshes/Foliage/Trees/Cedar/S_Cedar_Large"),
		TEXT("/Game/Brushify/Meshes/Foliage/Trees/Cedar/S_Cedar_Medium"),
		TEXT("/Game/Brushify/Meshes/Foliage/Trees/Cedar/S_Cedar_Small"),
		TEXT("/Game/Brushify/Meshes/Foliage/Trees/Maple/S_Maple_Large"),
		TEXT("/Game/Brushify/Meshes/Foliage/Trees/Maple/S_Maple_Medium"),
		TEXT("/Game/Brushify/Meshes/Foliage/Trees/Maple/S_Maple_Small"),
	};
	const TCHAR* const GBushes[] =
	{
		TEXT("/Game/Brushify/Meshes/Foliage/Bushes/GenericBush/S_GenericBush_Large"),
		TEXT("/Game/Brushify/Meshes/Foliage/Bushes/GenericBush/S_GenericBush_Medium"),
		TEXT("/Game/Brushify/Meshes/Foliage/Bushes/GenericBush/S_GenericBush_Small"),
	};
	const TCHAR* const GRocks[] =
	{
		TEXT("/Game/Brushify/Meshes/Ground/Forest/Rocks/ForestRock_01/S_ForestRock_01"),
		TEXT("/Game/Brushify/Meshes/Ground/Forest/Rocks/ForestRock_02/S_ForestRock_02"),
		TEXT("/Game/Brushify/Meshes/Ground/Forest/Rocks/ForestRock_03/S_ForestRock_03"),
		TEXT("/Game/_iraisynn_attinom/assets/rock1/SM_rock_moss_set_01_rock01"),
		TEXT("/Game/_iraisynn_attinom/assets/rock1/SM_rock_moss_set_01_rock02"),
		TEXT("/Game/_iraisynn_attinom/assets/rock1/SM_rock_moss_set_01_rock03"),
		TEXT("/Game/_iraisynn_attinom/assets/rock1/SM_rock_moss_set_01_rock04"),
		TEXT("/Game/_iraisynn_attinom/assets/rock1/SM_rock_moss_set_01_rock05"),
		TEXT("/Game/_iraisynn_attinom/assets/rock1/SM_rock_moss_set_01_rock06"),
		TEXT("/Game/_iraisynn_attinom/assets/rock2/SM_rock_moss_set_02_rock07"),
		TEXT("/Game/_iraisynn_attinom/assets/rock2/SM_rock_moss_set_02_rock09"),
		TEXT("/Game/_iraisynn_attinom/assets/rock2/SM_rock_moss_set_02_rock11"),
		TEXT("/Game/_iraisynn_attinom/assets/rock2/SM_rock_moss_set_02_rock13"),
	};
	const TCHAR* const GGrass[] =
	{
		TEXT("/Game/_iraisynn_attinom/assets/grass/SM_grass_medium_01_large_a_LOD0"),
		TEXT("/Game/_iraisynn_attinom/assets/grass/SM_grass_medium_01_large_b_LOD0"),
		TEXT("/Game/_iraisynn_attinom/assets/grass/SM_grass_medium_01_large_c_LOD0"),
		TEXT("/Game/_iraisynn_attinom/assets/grass/SM_grass_medium_01_mid_a_LOD0"),
		TEXT("/Game/_iraisynn_attinom/assets/grass/SM_grass_medium_01_mid_b_LOD0"),
		TEXT("/Game/_iraisynn_attinom/assets/grass/SM_grass_medium_01_mid_c_LOD0"),
		TEXT("/Game/_iraisynn_attinom/assets/grass/SM_grass_medium_01_small_a_LOD0"),
		TEXT("/Game/_iraisynn_attinom/assets/fern_02/SM_fern_02_a"),
		TEXT("/Game/_iraisynn_attinom/assets/fern_02/SM_fern_02_b"),
		TEXT("/Game/_iraisynn_attinom/assets/weed_plant_02/SM_weed_plant_02_a_LOD0"),
		TEXT("/Game/_iraisynn_attinom/assets/weed_plant_02/SM_weed_plant_02_c_LOD0"),
	};
	const TCHAR* const GMountains[] =
	{
		TEXT("/Game/Brushify/DistanceMeshes/Mountain_Generic_03/S_Mountain_Generic_03"),
		TEXT("/Game/Brushify/DistanceMeshes/Generic_Erosion_01/S_Generic_Erosion_01"),
		TEXT("/Game/Brushify/DistanceMeshes/Generic_Erosion_02/S_Generic_Erosion_02"),
		TEXT("/Game/Brushify/DistanceMeshes/Generic_Erosion_04/S_Generic_Erosion_04"),
	};
	const TCHAR* const GCliffs[] =
	{
		TEXT("/Game/Brushify/Meshes/Cliffs/Meshes/Cliff_Large_01/S_Cliff_Large_01"),
		TEXT("/Game/Brushify/Meshes/Cliffs/Meshes/Cliff_Large_02/S_Cliff_Large_02"),
	};
	const TCHAR* const GFlyers[] =
	{
		TEXT("/Game/FlyingAI/Meshes/FlyingAI_SM"),
	};

	FString SmvObjPath(const FString& Pkg)
	{
		return Pkg + TEXT(".") + FPaths::GetBaseFilename(Pkg);
	}

	template <typename T>
	T* SmvLoadPkg(const FString& Pkg)
	{
		if (Pkg.IsEmpty() || !FPackageName::DoesPackageExist(Pkg)) return nullptr;
		return LoadObject<T>(nullptr, *SmvObjPath(Pkg), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	template <SIZE_T N>
	void SmvLoadList(TArray<UStaticMesh*>& Out, const TCHAR* const (&Paths)[N])
	{
		for (const TCHAR* P : Paths)
		{
			if (UStaticMesh* M = SmvLoadPkg<UStaticMesh>(P))
			{
				M->AddToRoot();   // statik önbellek: çöp toplayıcı silmesin
				Out.Add(M);
			}
		}
	}

	/** Yükseklik: halka dışında kum tepeleri / dalgalı ova (m) */
	double SmvDune(double E, double N, double FlatM, int32 Seed)
	{
		const double R = FMath::Sqrt(E * E + N * N);
		const double K = FMath::Clamp((R - FlatM) / FMath::Max(FlatM, 200.0), 0.0, 1.0);
		if (K <= 0.0) return 0.0;
		const double Sx = Seed * 0.137;
		const double A = FMath::PerlinNoise2D(FVector2D(E / 420.0 + Sx, N / 420.0 - Sx)) * 9.0;
		const double B = FMath::PerlinNoise2D(FVector2D(E / 1600.0 - Sx, N / 1600.0 + Sx)) * 34.0;
		const double C = FMath::Max(0.0, FMath::PerlinNoise2D(FVector2D(E / 3800.0, N / 3800.0))) * 110.0 * FMath::Clamp((R - FlatM * 3.0) / 3000.0, 0.0, 1.0);
		return (A + B + C) * K * K + 2.0 * K;
	}
}

// ---------------------------------------------------------------------------
// FSmvScenery
// ---------------------------------------------------------------------------

bool FSmvScenery::Enabled()
{
	bool bOn = true;
	if (GConfig) GConfig->GetBool(GSmvScenerySection, TEXT("Enabled"), bOn, GGameIni);
	return bOn;
}

float FSmvScenery::GetFloat(const TCHAR* Key, float Default)
{
	float V = Default;
	if (GConfig) GConfig->GetFloat(GSmvScenerySection, Key, V, GGameIni);
	return V;
}

TArrayView<const TCHAR* const> FSmvScenery::CookDirs()
{
	return TArrayView<const TCHAR* const>(GSmvSceneryCookDirs, UE_ARRAY_COUNT(GSmvSceneryCookDirs));
}

const TArray<UStaticMesh*>& FSmvScenery::Meshes(ESmvSceneryKind Kind)
{
	static TArray<UStaticMesh*> Lists[7];
	static bool bLoaded[7] = { false, false, false, false, false, false, false };
	const int32 I = FMath::Clamp((int32)Kind, 0, 6);
	if (!bLoaded[I])
	{
		bLoaded[I] = true;
		if (Enabled())
		{
			switch (Kind)
			{
			case ESmvSceneryKind::Tree: SmvLoadList(Lists[I], GTrees); break;
			case ESmvSceneryKind::Bush: SmvLoadList(Lists[I], GBushes); break;
			case ESmvSceneryKind::Rock: SmvLoadList(Lists[I], GRocks); break;
			case ESmvSceneryKind::Grass: SmvLoadList(Lists[I], GGrass); break;
			case ESmvSceneryKind::Mountain: SmvLoadList(Lists[I], GMountains); break;
			case ESmvSceneryKind::Cliff: SmvLoadList(Lists[I], GCliffs); break;
			case ESmvSceneryKind::Flyer: SmvLoadList(Lists[I], GFlyers); break;
			}
		}
		UE_LOG(LogSimumav, Log, TEXT("Manzara ağları (%d): %d"), I, Lists[I].Num());
	}
	return Lists[I];
}

UMaterialInterface* FSmvScenery::TerrainMaterial()
{
	static TWeakObjectPtr<UMaterialInterface> M;
	static bool bTried = false;
	if (!bTried)
	{
		bTried = true;
		bool bUse = true;
		if (GConfig) GConfig->GetBool(GSmvScenerySection, TEXT("HyperTerrain"), bUse, GGameIni);
		if (bUse) M = SmvLoadPkg<UMaterialInterface>(TEXT("/Game/Simumav/Materials/M_SmvTerrainHyper"));
		UE_LOG(LogSimumav, Log, TEXT("Hyper arazi malzemesi: %s"), M.IsValid() ? TEXT("var") : TEXT("yok (M_SmvTerrainWx)"));
	}
	return M.Get();
}

UMaterialInterface* FSmvScenery::StreetMaterial()
{
	static TWeakObjectPtr<UMaterialInterface> M;
	static bool bTried = false;
	if (!bTried)
	{
		bTried = true;
		M = SmvLoadPkg<UMaterialInterface>(TEXT("/Game/Simumav/Materials/M_SmvStreet"));
	}
	return M.Get();
}

UMaterialInterface* FSmvScenery::MountainMaterial(const UStaticMesh* Mesh, const FString& Biome)
{
	if (!Mesh || Biome.IsEmpty()) return nullptr;
	// /Game/Brushify/DistanceMeshes/<Ad>/S_<Ad> → /Game/Brushify/DistanceMeshes/<Ad>/MI_<Ad>_<Biyom>
	const FString Pkg = Mesh->GetPackage()->GetName();
	FString Name = FPaths::GetBaseFilename(Pkg);
	if (Name.StartsWith(TEXT("S_"))) Name.RightChopInline(2);
	return SmvLoadPkg<UMaterialInterface>(FPaths::GetPath(Pkg) / (TEXT("MI_") + Name + TEXT("_") + Biome));
}

// ---------------------------------------------------------------------------
// ASmvScenery
// ---------------------------------------------------------------------------

ASmvScenery::ASmvScenery()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

UHierarchicalInstancedStaticMeshComponent* ASmvScenery::HismFor(UStaticMesh* Mesh, bool bCollide, double CullM, UMaterialInterface* Material, bool bShadow)
{
	for (UHierarchicalInstancedStaticMeshComponent* H : Hisms)
	{
		if (H && H->GetStaticMesh() == Mesh && H->GetMaterial(0) == (Material ? Material : H->GetMaterial(0))) return H;
	}
	UHierarchicalInstancedStaticMeshComponent* H = NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
	H->SetupAttachment(Root);
	H->SetStaticMesh(Mesh);
	if (Material) for (int32 i = 0; i < Mesh->GetStaticMaterials().Num(); ++i) H->SetMaterial(i, Material);
	H->SetCollisionEnabled(bCollide ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	H->SetCastShadow(bShadow);
	H->SetCullDistances(0, (int32)FMath::Clamp(CullM * 100.0, 1000.0, 2.0e7));
	H->RegisterComponent();
	Hisms.Add(H);
	return H;
}

void ASmvScenery::AddMeshInstances(UStaticMesh* Mesh, const TArray<FTransform>& WorldXf, bool bCollide, double CullM, UMaterialInterface* Material, bool bShadow)
{
	if (!Mesh || WorldXf.Num() == 0) return;
	HismFor(Mesh, bCollide, CullM, Material, bShadow)->AddInstances(WorldXf, false, true);
	Placed += WorldXf.Num();
}

void ASmvScenery::Scatter(double InnerM, double OuterM, const FSmvScatterDensity& D, int32 Seed, TFunctionRef<bool(double, double, FVector&, FVector&)> GroundFn)
{
	FRandomStream R(Seed * 7919 + 13);
	struct FBatch { UStaticMesh* Mesh; bool bCollide; double Cull; bool bShadow; TArray<FTransform> Xf; };
	TArray<FBatch> Batches;
	auto Add = [&Batches](UStaticMesh* M, bool bCollide, double Cull, bool bShadow, const FTransform& X)
	{
		for (FBatch& B : Batches) if (B.Mesh == M) { B.Xf.Add(X); return; }
		FBatch B{ M, bCollide, Cull, bShadow, {} };
		B.Xf.Add(X);
		Batches.Add(MoveTemp(B));
	};
	auto Place = [&](ESmvSceneryKind K, int32 Count, double In, double Out, bool bCollide, double CullM, float SMin, float SMax, bool bAlign, float SinkCm, bool bShadow)
	{
		const TArray<UStaticMesh*>& Ms = FSmvScenery::Meshes(K);
		if (Ms.Num() == 0 || Count <= 0 || Out <= In) return;
		for (int32 i = 0; i < Count; ++i)
		{
			const double Rr = FMath::Sqrt(FMath::Lerp(In * In, Out * Out, (double)R.FRand()));
			const double A = R.FRand() * UE_DOUBLE_TWO_PI;
			const double E = Rr * FMath::Sin(A), N = Rr * FMath::Cos(A);
			FVector P, Up;
			if (!GroundFn(E, N, P, Up)) continue;
			UStaticMesh* M = Ms[R.RandRange(0, Ms.Num() - 1)];
			const float S = R.FRandRange(SMin, SMax);
			FQuat Q(FVector::UpVector, R.FRand() * UE_TWO_PI);
			if (bAlign) Q = FQuat::FindBetweenNormals(FVector::UpVector, Up.GetSafeNormal()) * Q;
			Add(M, bCollide, CullM, bShadow, FTransform(Q, P - Up.GetSafeNormal() * SinkCm * S, FVector(S)));
		}
	};
	const double TreeIn = FMath::Max(InnerM, D.TreeInnerM);
	Place(ESmvSceneryKind::Tree, D.bDry ? D.Trees / 5 : D.Trees, TreeIn, OuterM, true, FSmvScenery::GetFloat(TEXT("TreeCullM"), 3000.f), 0.75f, 1.3f, false, 30.f, true);
	Place(ESmvSceneryKind::Bush, D.Bushes, TreeIn * 0.8, OuterM, false, 900.0, 0.7f, 1.4f, false, 10.f, true);
	Place(ESmvSceneryKind::Rock, D.Rocks, InnerM, OuterM, true, 1500.0, 0.6f, 2.2f, true, 25.f, true);
	Place(ESmvSceneryKind::Grass, D.Grass, InnerM, FMath::Min(OuterM, D.GrassOuterM), false, 160.0, 0.8f, 1.5f, true, 2.f, false);
	for (FBatch& B : Batches) AddMeshInstances(B.Mesh, B.Xf, B.bCollide, B.Cull, nullptr, B.bShadow);
	UE_LOG(LogSimumav, Log, TEXT("Manzara dağıtımı: %d örnek, %d ağ türü (halka %.0f–%.0f m)"), Placed, Batches.Num(), InnerM, OuterM);
}

void ASmvScenery::AddMountains(const FVector& Center, double InnerCm, double OuterCm, double BaseZ, int32 Count, const FString& Biome, int32 Seed, double MinHCm, double MaxHCm)
{
	const TArray<UStaticMesh*>& Ms = FSmvScenery::Meshes(ESmvSceneryKind::Mountain);
	if (Ms.Num() == 0 || Count <= 0) return;
	FRandomStream R(Seed * 31 + 5);
	const double A0 = R.FRand() * UE_DOUBLE_TWO_PI;
	for (int32 i = 0; i < Count; ++i)
	{
		UStaticMesh* M = Ms[(i + FMath::Abs(Seed)) % Ms.Num()];
		const FBoxSphereBounds B = M->GetBounds();
		const double Hm = FMath::Max(2.0 * B.BoxExtent.Z, 1.0);
		const double S = FMath::Lerp(MinHCm, MaxHCm, (double)R.FRand()) / Hm;
		const double A = A0 + UE_DOUBLE_TWO_PI * (i + R.FRandRange(-0.3f, 0.3f)) / Count;
		const double Rr = FMath::Lerp(InnerCm, OuterCm, (double)R.FRand());
		const FRotator Rot(0.0, R.FRand() * 360.0, 0.0);
		FVector P = Center + FVector(FMath::Cos(A) * Rr, FMath::Sin(A) * Rr, 0.0);
		P -= Rot.RotateVector(FVector(B.Origin.X, B.Origin.Y, 0.0) * S);
		P.Z = BaseZ - (B.Origin.Z - B.BoxExtent.Z) * S - Hm * S * 0.06;
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetupAttachment(Root);
		C->SetStaticMesh(M);
		if (UMaterialInterface* Mi = FSmvScenery::MountainMaterial(M, Biome)) for (int32 k = 0; k < M->GetStaticMaterials().Num(); ++k) C->SetMaterial(k, Mi);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->SetWorldTransform(FTransform(Rot, P, FVector(S)));
		C->RegisterComponent();
		Singles.Add(C);
	}
	UE_LOG(LogSimumav, Log, TEXT("Manzara: %d uzak dağ (%s, %.1f–%.1f km)"), Count, *Biome, InnerCm / 100000.0, OuterCm / 100000.0);
}

bool ASmvScenery::BuildSurround(const TArray<ULevel*>& Levels, const FString& Biome, int32 Seed, const TArray<AActor*>& Ignore)
{
	UWorld* W = GetWorld();
	if (!W) return false;
	FBox Box(ForceInit);
	for (ULevel* L : Levels)
	{
		if (!L) continue;
		for (AActor* A : L->Actors)
		{
			if (!IsValid(A)) continue;
			if (A->GetClass()->GetName().Contains(TEXT("Landscape")))
			{
				const FBox Lb = A->GetComponentsBoundingBox(true);
				if (Lb.IsValid && Lb.GetSize().GetMax() < 2.0e6) Box += Lb;
				continue;
			}
			TInlineComponentArray<UStaticMeshComponent*> Comps(A);
			for (UStaticMeshComponent* Sc : Comps)
			{
				if (!Sc || !Sc->IsRegistered() || !Sc->GetStaticMesh()) continue;
				const FBox Sb = Sc->Bounds.GetBox();
				if (Sb.IsValid && Sb.GetSize().GetMax() < 2.0e6) Box += Sb;
			}
		}
	}
	if (!Box.IsValid) return false;
	// adanın yürünebilir yüzeyi: kutu içinde ızgara ışınları → alt %10'luk yükseklik (SurroundPercentile); disk bunun biraz altında
	FCollisionQueryParams Q(SCENE_QUERY_STAT(SmvSurround), true);
	Q.AddIgnoredActors(Ignore);
	Q.AddIgnoredActor(this);
	TArray<double> Zs;
	const int32 G = 16;
	for (int32 i = 0; i < G; ++i)
	{
		for (int32 j = 0; j < G; ++j)
		{
			const double X = FMath::Lerp(Box.Min.X, Box.Max.X, (i + 0.5) / G), Y = FMath::Lerp(Box.Min.Y, Box.Max.Y, (j + 0.5) / G);
			FHitResult H;
			if (W->LineTraceSingleByChannel(H, FVector(X, Y, Box.Max.Z + 1000.0), FVector(X, Y, Box.Min.Z - 1000.0), ECC_Visibility, Q)) Zs.Add(H.ImpactPoint.Z);
		}
	}
	if (Zs.Num() < 8) { UE_LOG(LogSimumav, Warning, TEXT("Manzara çevresi: zemin bulunamadı (%d ışın)"), Zs.Num()); return false; }
	Zs.Sort();
	const int32 Pi = FMath::Clamp((int32)(Zs.Num() * FSmvScenery::GetFloat(TEXT("SurroundPercentile"), 0.1f)), 0, Zs.Num() - 1);
	const double BaseZ = Zs[Pi] - FSmvScenery::GetFloat(TEXT("SurroundSinkCm"), 60.f);
	const FVector C(Box.GetCenter().X, Box.GetCenter().Y, BaseZ);
	const double BoxR = 0.5 * FMath::Max(Box.GetSize().X, Box.GetSize().Y);
	const double FlatM = BoxR / 100.0 * 1.05;
	const double OuterM = FMath::Max(FlatM * 8.0, (double)FSmvScenery::GetFloat(TEXT("SurroundRadiusM"), 12000.f));
	BuildGroundDisk(C, FlatM, OuterM, Biome, Seed, true);
	UE_LOG(LogSimumav, Log, TEXT("Fab çevresi kuruldu (%s): ada yarıçapı %.0f m, zemin Z %.0f cm, disk %.0f km"), *Biome, BoxR / 100.0, BaseZ, OuterM / 1000.0);
	return true;
}

void ASmvScenery::BuildGroundDisk(const FVector& C, double FlatM, double OuterM, const FString& Biome, int32 Seed, bool bScatter)
{
	const double BaseZ = C.Z;
	// aktör taşınmaz (daha önce eklenen dünya uzayı örnekleri kaymasın): disk köşeleri aktöre göre Off kadar kaydırılır
	const FVector Off = C - GetActorLocation();
	// ---- zemin diski: kutup ızgarası (iç halkalar sık, dışa doğru seyrek) ----
	Ground = NewObject<UProceduralMeshComponent>(this);
	Ground->SetupAttachment(Root);
	Ground->bUseAsyncCooking = true;
	Ground->RegisterComponent();
	const int32 Rings = 72, Segs = 160;
	TArray<FVector> V; TArray<int32> Tri; TArray<FVector> Nrm; TArray<FVector2D> Uv; TArray<FLinearColor> Col; TArray<FProcMeshTangent> Tan;
	auto HeightM = [FlatM, Seed](double E, double N) { return SmvDune(E, N, FlatM, Seed); };
	auto RingR = [OuterM, Rings](int32 r) { const double U = (double)r / Rings; return OuterM * U * U * (0.35 + 0.65 * U) + OuterM * 0.002 * r; };
	V.Reserve((Rings + 1) * Segs + 1);
	V.Add(Off);
	for (int32 r = 1; r <= Rings; ++r)
	{
		const double Rm = RingR(r);
		for (int32 s = 0; s < Segs; ++s)
		{
			const double A = UE_DOUBLE_TWO_PI * s / Segs;
			const double E = Rm * FMath::Cos(A), N = Rm * FMath::Sin(A);
			V.Add(Off + FVector(E * 100.0, -N * 100.0, HeightM(E, N) * 100.0));
		}
	}
	for (int32 s = 0; s < Segs; ++s) Tri.Append({ 0, 1 + (s + 1) % Segs, 1 + s });
	for (int32 r = 1; r < Rings; ++r)
	{
		for (int32 s = 0; s < Segs; ++s)
		{
			const int32 A = 1 + (r - 1) * Segs + s, B = 1 + (r - 1) * Segs + (s + 1) % Segs, Cc = A + Segs, D = B + Segs;
			Tri.Append({ A, B, Cc, B, D, Cc });
		}
	}
	Nrm.Init(FVector::ZeroVector, V.Num());
	for (int32 i = 0; i < Tri.Num(); i += 3)
	{
		const FVector Fn = FVector::CrossProduct(V[Tri[i + 1]] - V[Tri[i]], V[Tri[i + 2]] - V[Tri[i]]).GetSafeNormal();
		Nrm[Tri[i]] += Fn; Nrm[Tri[i + 1]] += Fn; Nrm[Tri[i + 2]] += Fn;
	}
	for (FVector& X : Nrm) { X = X.GetSafeNormal(); if (X.Z < 0) X = -X; }
	// üçgen sarımı (ASmvProcTerrain ile aynı): Cross(v1-v0, v2-v0).Z > 0 görünen üst yüz — değilse tümünü çevir
	{
		const FVector Fn = FVector::CrossProduct(V[Tri[1]] - V[Tri[0]], V[Tri[2]] - V[Tri[0]]);
		if (Fn.Z < 0) for (int32 i = 0; i < Tri.Num(); i += 3) Swap(Tri[i + 1], Tri[i + 2]);
	}
	Tan.Reserve(V.Num());
	Uv.Reserve(V.Num());
	for (int32 i = 0; i < V.Num(); ++i)
	{
		const FVector Tx = (FVector::ForwardVector - Nrm[i] * FVector::DotProduct(Nrm[i], FVector::ForwardVector)).GetSafeNormal();
		Tan.Add(FProcMeshTangent(Tx, false));
		Uv.Add(FVector2D((V[i].X - Off.X) / 1000.0, (V[i].Y - Off.Y) / 1000.0));
	}
	// köşe rengi: Hyper malzemesi yoksa (M_SmvTerrainWx: köşe rengi × Color) biyom rengi
	Col.Init(Biome == TEXT("Desert") ? FLinearColor(0.55f, 0.45f, 0.3f) : Biome == TEXT("Snow") ? FLinearColor(0.8f, 0.82f, 0.86f) : FLinearColor(0.2f, 0.3f, 0.14f), V.Num());
	Ground->CreateMeshSection_LinearColor(0, V, Tri, Nrm, Uv, Col, Tan, true);
	Ground->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Ground->SetCastShadow(false);
	UMaterialInterface* Tm = FSmvScenery::TerrainMaterial();
	if (!Tm) Tm = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Simumav/Materials/M_SmvTerrainWx.M_SmvTerrainWx"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (Tm)
	{
		GroundMid = UMaterialInstanceDynamic::Create(Tm, this);
		const bool bDesert = Biome == TEXT("Desert");
		GroundMid->SetScalarParameterValue(TEXT("Sand"), bDesert ? 1.f : 0.f);
		GroundMid->SetVectorParameterValue(TEXT("Tint"), bDesert ? FLinearColor(1.f, 0.93f, 0.84f) : FLinearColor::White);
		GroundMid->SetScalarParameterValue(TEXT("RockSlope"), bDesert ? 0.35f : 0.22f);
		Ground->SetMaterial(0, GroundMid);
	}
	// ---- kaya / çalı (kuru) ve uzak dağlar ----
	AddMountains(C, OuterM * 100.0 * 0.42, OuterM * 100.0 * 0.85, BaseZ, 14, Biome, Seed, 45000.0, 140000.0);
	if (!bScatter) return;
	FSmvScatterDensity D;
	D.bDry = Biome == TEXT("Desert");
	D.Rocks = 260; D.Bushes = D.bDry ? 90 : 260; D.Trees = D.bDry ? 0 : 600; D.Grass = 0;
	D.TreeInnerM = FlatM * 1.3;
	const FVector Origin = C;
	Scatter(FlatM * 1.1, FMath::Min(OuterM * 0.45, FlatM * 6.0), D, Seed, [&HeightM, &Origin](double E, double N, FVector& P, FVector& Up)
	{
		const double H = HeightM(E, N);
		const double Hx = HeightM(E + 2.0, N) - HeightM(E - 2.0, N), Hy = HeightM(E, N + 2.0) - HeightM(E, N - 2.0);
		P = Origin + FVector(E * 100.0, -N * 100.0, H * 100.0);
		Up = FVector(-Hx / 4.0, Hy / 4.0, 1.0).GetSafeNormal();
		return true;
	});
}

void ASmvScenery::SetSurfaceWeather(float Wetness, float Snow)
{
	if (!GroundMid) return;
	GroundMid->SetScalarParameterValue(TEXT("Wetness"), FMath::Clamp(Wetness, 0.f, 1.f));
	GroundMid->SetScalarParameterValue(TEXT("Snow"), FMath::Clamp(Snow, 0.f, 1.f));
}

// ---------------------------------------------------------------------------
// ASmvSkyTraffic
// ---------------------------------------------------------------------------

ASmvSkyTraffic::ASmvSkyTraffic()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

bool ASmvSkyTraffic::Build(const FVector& Center, int32 Count, double RadiusCm, double AltMinCm, double AltMaxCm, int32 Seed)
{
	bool bOn = true;
	if (GConfig) GConfig->GetBool(GSmvScenerySection, TEXT("SkyTraffic"), bOn, GGameIni);
	const TArray<UStaticMesh*>& Ms = FSmvScenery::Meshes(ESmvSceneryKind::Flyer);
	if (!bOn || Ms.Num() == 0 || Count <= 0) return false;
	C0 = Center;
	SetActorLocation(Center);
	FRandomStream R(Seed * 101 + 7);
	UStaticMesh* M = Ms[0];
	const FBoxSphereBounds B = M->GetBounds();
	// gövde ekseni: yatayda uzun olan eksen (+X kabul; Y daha uzunsa 90° düzeltme)
	MeshFix = B.BoxExtent.Y > B.BoxExtent.X * 1.2 ? FRotator(0.f, -90.f, 0.f) : FRotator::ZeroRotator;
	const double Len = 2.0 * FMath::Max3(B.BoxExtent.X, B.BoxExtent.Y, 1.0);
	const double S = FSmvScenery::GetFloat(TEXT("SkyTrafficLengthM"), 22.f) * 100.0 / Len;
	for (int32 i = 0; i < Count; ++i)
	{
		FLeg L;
		L.Rx = RadiusCm * R.FRandRange(0.65f, 1.35f);
		L.Ry = RadiusCm * R.FRandRange(0.45f, 1.1f);
		L.Alt = FMath::Lerp(AltMinCm, AltMaxCm, (double)R.FRand());
		L.Speed = R.FRandRange(4500.f, 7500.f) * (R.FRand() < 0.5f ? -1.0 : 1.0);   // cm/sn (45–75 m/sn)
		L.Phase = R.FRand() * UE_DOUBLE_TWO_PI;
		L.Yaw0 = R.FRand() * 180.0;
		Legs.Add(L);
		UStaticMeshComponent* Cm = NewObject<UStaticMeshComponent>(this);
		Cm->SetupAttachment(Root);
		Cm->SetStaticMesh(M);
		Cm->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Cm->SetCastShadow(false);
		Cm->SetWorldScale3D(FVector(S));
		Cm->RegisterComponent();
		Craft.Add(Cm);
	}
	UE_LOG(LogSimumav, Log, TEXT("Gökte uzak trafik: %d araç (Flying AI ağı, yalnız görsel)"), Count);
	return true;
}

void ASmvSkyTraffic::Tick(float Dt)
{
	Super::Tick(Dt);
	T += Dt;
	for (int32 i = 0; i < Craft.Num() && i < Legs.Num(); ++i)
	{
		const FLeg& L = Legs[i];
		const double Rm = 0.5 * (L.Rx + L.Ry);
		const double A = L.Phase + T * L.Speed / FMath::Max(Rm, 1.0);
		const FRotator Ell(0.0, L.Yaw0, 0.0);
		const FVector P = C0 + Ell.RotateVector(FVector(L.Rx * FMath::Cos(A), L.Ry * FMath::Sin(A), 0.0)) + FVector(0, 0, L.Alt);
		const double Sg = L.Speed >= 0 ? 1.0 : -1.0;
		const FVector Dir = Ell.RotateVector(FVector(-L.Rx * FMath::Sin(A) * Sg, L.Ry * FMath::Cos(A) * Sg, 0.0)).GetSafeNormal();
		FRotator Rot = Dir.Rotation();
		Rot.Roll = -12.0 * Sg;
		Craft[i]->SetWorldLocationAndRotation(P, (Rot.Quaternion() * MeshFix.Quaternion()).Rotator());
	}
}
