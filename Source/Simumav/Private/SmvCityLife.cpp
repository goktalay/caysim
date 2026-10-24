#include "SmvCityLife.h"
#include "SimumavModule.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Misc/ConfigCacheIni.h"
#include "ReferenceSkeleton.h"

namespace
{
	const TCHAR* GSmvCityLifeSection = TEXT("/Script/Simumav.SmvCityLife");

	const TCHAR* const GCookDirs[] =
	{
		// <PISIR-KLASORLERI>
		TEXT("/Game/City_Traffic_Pro/Cars/Meshes/Skeletal_Meshes"),
		TEXT("/Game/City_Traffic_Pro/Demo/Road_Creator_Minimal/Meshes/Intersection"),
		TEXT("/Game/AT_Drone/Demo/Character/Mannequins/Meshes"),
		TEXT("/Game/AT_Drone/Demo/Character/Mannequins/Animations"),
		TEXT("/Game/AT_Drone/Mesh/Rigged"),
		TEXT("/Game/BendylabStudios/BLS_Hexacopter_v1/Blueprint"),
		TEXT("/Game/BendylabStudios/BLS_Interceptor_Drone_v1/Blueprint"),
		TEXT("/Game/DroneMegaPack/Drone_A/Models"),
		TEXT("/Game/DroneMegaPack/Drone_B/Blueprint"),
		TEXT("/Game/DroneMegaPack/Drone_C/Blueprint"),
		TEXT("/Game/DroneMegaPack/Drone_D/Models"),
		// </PISIR-KLASORLERI>
	};
	const TCHAR* GCarDir = TEXT("/Game/City_Traffic_Pro/Cars/Meshes/Skeletal_Meshes");
	const TCHAR* GPeopleDir = TEXT("/Game/AT_Drone/Demo/Character/Mannequins");
	const TCHAR* GVatDir = TEXT("/Game/Fab/Crowd");

	template <class T>
	void SmvAssetsIn(const TCHAR* Dir, TArray<T*>& Out, TFunctionRef<bool(const FAssetData&)> Filter)
	{
		TArray<FAssetData> A;
		IAssetRegistry::GetChecked().GetAssetsByPath(FName(Dir), A, true);
		A.Sort([](const FAssetData& X, const FAssetData& Y) { return X.AssetName.LexicalLess(Y.AssetName); });
		for (const FAssetData& X : A)
		{
			if (X.AssetClassPath != T::StaticClass()->GetClassPathName() || !Filter(X)) continue;
			if (T* O = Cast<T>(X.GetAsset())) { O->AddToRoot(); Out.Add(O); }
		}
	}

	double SmvRand(uint32& S) { S = S * 1664525u + 1013904223u; return (S >> 8) / 16777216.0; }
}

TArrayView<const TCHAR* const> FSmvCityLife::CookDirs() { return MakeArrayView(GCookDirs); }

float FSmvCityLife::GetFloat(const TCHAR* Key, float Default)
{
	float V = Default;
	if (GConfig) GConfig->GetFloat(GSmvCityLifeSection, Key, V, GGameIni);
	return V;
}

const TArray<USkeletalMesh*>& FSmvCityLife::CarModels()
{
	static TArray<USkeletalMesh*> Out;
	static bool bRead = false;
	if (bRead) return Out;
	bRead = true;
	SmvAssetsIn<USkeletalMesh>(GCarDir, Out, [](const FAssetData&) { return true; });
	Out.RemoveAll([](USkeletalMesh* M)
	{
		const FVector E = M->GetBounds().BoxExtent * 2.0;
		const double L = FMath::Max(E.X, E.Y);
		return L < 280.0 || L > 800.0 || E.Z < 90.0 || E.Z > 350.0;
	});
	UE_LOG(LogSimumav, Log, TEXT("Şehir trafiği: %d araç modeli (%s)"), Out.Num(), GCarDir);
	return Out;
}

const TArray<USkeletalMesh*>& FSmvCityLife::PeopleModels()
{
	static TArray<USkeletalMesh*> Out;
	static bool bRead = false;
	if (bRead) return Out;
	bRead = true;
	// sade (daha az kemikli) gövdeler yeğlenir: kalabalıkta çizim maliyeti düşük
	SmvAssetsIn<USkeletalMesh>(GPeopleDir, Out, [](const FAssetData& X) { return X.AssetName.ToString().Contains(TEXT("Simple")); });
	if (Out.Num() == 0) SmvAssetsIn<USkeletalMesh>(GPeopleDir, Out, [](const FAssetData& X) { return X.AssetName.ToString().StartsWith(TEXT("SKM_")); });
	Out.RemoveAll([](USkeletalMesh* M) { return !WalkAnim(M); });
	UE_LOG(LogSimumav, Log, TEXT("Şehir kalabalığı: %d insan modeli (%s)"), Out.Num(), GPeopleDir);
	return Out;
}

namespace
{
	UAnimSequence* SmvFindAnim(const USkeletalMesh* For, const TArray<const TCHAR*>& Want)
	{
		static TArray<UAnimSequence*> All;
		static bool bRead = false;
		if (!bRead)
		{
			bRead = true;
			SmvAssetsIn<UAnimSequence>(GPeopleDir, All, [](const FAssetData& X)
			{
				const FString N = X.AssetName.ToString();
				return N.Contains(TEXT("Walk")) || N.Contains(TEXT("Idle"));
			});
		}
		if (!For) return nullptr;
		for (const TCHAR* W : Want)
			for (UAnimSequence* A : All)
				if (A && A->GetName().Contains(W) && A->GetSkeleton() == For->GetSkeleton()) return A;
		return nullptr;
	}
}

UAnimSequence* FSmvCityLife::WalkAnim(const USkeletalMesh* For)
{
	return SmvFindAnim(For, { TEXT("Walk_Fwd"), TEXT("Walk") });
}

UAnimSequence* FSmvCityLife::IdleAnim(const USkeletalMesh* For)
{
	return SmvFindAnim(For, { TEXT("Idle") });
}

const TArray<UStaticMesh*>& FSmvCityLife::VatCrowdMeshes()
{
	static TArray<UStaticMesh*> Out;
	static bool bRead = false;
	if (bRead) return Out;
	bRead = true;
	SmvAssetsIn<UStaticMesh>(GVatDir, Out, [](const FAssetData&) { return true; });
	UE_LOG(LogSimumav, Log, TEXT("OverCrowd VAT kalabalık ağları: %d (%s)"), Out.Num(), GVatDir);
	return Out;
}

// ---------------------------------------------------------------------------

ASmvCityLife::ASmvCityLife()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.f;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

UMeshComponent* ASmvCityLife::MakeCar(USkeletalMesh* Model, FCar& C)
{
	UPoseableMeshComponent* P = NewObject<UPoseableMeshComponent>(this);
	P->SetSkinnedAssetAndUpdate(Model);
	P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	P->SetCastShadow(true);
	P->SetupAttachment(Root);
	P->RegisterComponent();
	P->LDMaxDrawDistance = FSmvCityLife::GetFloat(TEXT("CarCullM"), 900.f) * 100.f;
	P->SetCachedMaxDrawDistance(P->LDMaxDrawDistance);
	// tekerlekler + burun yönü (far kemikleri burunda): ön +X değilse 180° çevrilir
	const FReferenceSkeleton& Ref = Model->GetRefSkeleton();
	const TArray<FTransform>& Local = Ref.GetRefBonePose();
	TArray<FTransform> Cs; Cs.SetNum(Ref.GetNum());
	double HeadX = 0; int32 NHead = 0;
	for (int32 i = 0; i < Ref.GetNum(); ++i)
	{
		const int32 Pi = Ref.GetParentIndex(i);
		Cs[i] = Pi >= 0 ? Local[i] * Cs[Pi] : Local[i];
		const FString N = Ref.GetBoneName(i).ToString();
		if (N.StartsWith(TEXT("Wheel"))) { C.Wheels.Add(Ref.GetBoneName(i)); C.WheelRef.Add(Cs[i].GetRotation()); C.WheelR = FMath::Max(25.0, Cs[i].GetLocation().Z); }
		if (N.Contains(TEXT("Headlight")) || N.Contains(TEXT("RegPlateF"))) { HeadX += Cs[i].GetLocation().X; ++NHead; }
	}
	C.YawFix = (NHead > 0 && HeadX / NHead < 0.0) ? 180.f : 0.f;
	const FBoxSphereBounds B = Model->GetBounds();
	C.LenCm = FMath::Max(B.BoxExtent.X, B.BoxExtent.Y) * 2.0;
	C.LocalOff = FVector(-B.Origin.X, -B.Origin.Y, -(B.Origin.Z - B.BoxExtent.Z) + 4.0);
	CarComps.Add(P);
	return P;
}

void ASmvCityLife::PlaceCar(int32 i, const FVector& At, float Yaw)
{
	if (!CarComps.IsValidIndex(i) || !CarComps[i]) return;
	const FCar& C = CarState[i];
	const FRotator R(0.f, Yaw + C.YawFix, 0.f);
	CarComps[i]->SetRelativeLocationAndRotation(At + R.RotateVector(FVector(C.LocalOff.X, C.LocalOff.Y, 0.0)) + FVector(0, 0, C.LocalOff.Z), R);
}

FVector ASmvCityLife::LanePoint(const FLane& L, double S, float& OutYaw) const
{
	const double U = L.Dir > 0 ? -Half + S : Half - S;
	if (L.Axis == 0) { OutYaw = L.Dir > 0 ? 90.f : -90.f; return FVector(L.Fixed, U, 0.0); }
	OutYaw = L.Dir > 0 ? 0.f : 180.f;
	return FVector(U, L.Fixed, 0.0);
}

bool ASmvCityLife::Green(int32 Axis) const
{
	// iki evreli ışık: X yönlü caddeler 0–12 sn yeşil, 12–15 tümü kırmızı, Y yönlüler 15–27 yeşil, 27–30 tümü kırmızı
	const double Ph = FMath::Fmod(T, 30.0);
	return Axis == 0 ? (Ph < 12.0) : (Ph >= 15.0 && Ph < 27.0);
}

bool ASmvCityLife::BuildGridTraffic(const TArray<double>& Coords, double RoadCm, double HalfCm, int32 Cars, int32 Seed)
{
	const TArray<USkeletalMesh*>& Models = FSmvCityLife::CarModels();
	if (Models.Num() == 0 || Coords.Num() == 0 || Cars <= 0) return false;
	Half = HalfCm; Road = RoadCm;
	uint32 Rs = (uint32)Seed * 2654435761u + 17u;
	// her caddede yön başına iki şerit (sağdan gidiş): merkez çizgisinden 0,22 ve 0,40 × genişlik
	const double Off[2] = { RoadCm * 0.14, RoadCm * 0.34 };
	for (int32 Axis = 0; Axis < 2; ++Axis)
		for (double F : Coords)
			for (int32 Dir : { 1, -1 })
				for (double O : Off)
				{
					FLane L;
					L.Axis = Axis; L.Dir = Dir; L.Len = 2.0 * HalfCm;
					// sağ şerit: gidiş yönünün sağı (UE: X ileri, Y sağ) — Axis 0 (+Y gidiş) için sağ = −X
					const double Right = Axis == 0 ? (Dir > 0 ? -1.0 : 1.0) : (Dir > 0 ? 1.0 : -1.0);
					L.Fixed = F + Right * O;
					for (double Cc : Coords)
					{
						const double Sc = Dir > 0 ? Cc + HalfCm : HalfCm - Cc;
						L.Stops.Add(Sc - RoadCm * 0.5 - 300.0);
					}
					Lanes.Add(L);
				}
	const int32 N = FMath::Min(Cars, Lanes.Num() * 6);
	for (int32 i = 0; i < N; ++i)
	{
		const int32 Li = i % Lanes.Num();
		FLane& L = Lanes[Li];
		FCar C;
		C.Lane = Li;
		MakeCar(Models[(int32)(SmvRand(Rs) * Models.Num()) % Models.Num()], C);
		C.VMax = FSmvCityLife::GetFloat(TEXT("CarSpeedKmh"), 38.f) / 3.6 * 100.0 * (0.8 + 0.35 * SmvRand(Rs));
		C.V = C.VMax * 0.5;
		// şeritte eşit aralıklı + rastgele kayma (üst üste binmesin)
		const int32 K = L.Cars.Num();
		C.S = FMath::Fmod(L.Len * (K / 6.0) + SmvRand(Rs) * 900.0, L.Len);
		CarState.Add(C);
		L.Cars.Add(CarState.Num() - 1);
	}
	for (FLane& L : Lanes) L.Cars.Sort([this](int32 A, int32 B) { return CarState[A].S < CarState[B].S; });
	for (int32 i = 0; i < CarState.Num(); ++i)
	{
		float Yaw = 0.f;
		PlaceCar(i, LanePoint(Lanes[CarState[i].Lane], CarState[i].S, Yaw), Yaw);
	}
	UE_LOG(LogSimumav, Log, TEXT("Şehir trafiği kuruldu: %d araç, %d şerit (iki evreli kavşak ışığı; yalnız görsel)"), CarState.Num(), Lanes.Num());
	return true;
}

bool ASmvCityLife::AddParkedCar(const FVector& World, float Yaw, int32 Pick)
{
	const TArray<USkeletalMesh*>& Models = FSmvCityLife::CarModels();
	if (Models.Num() == 0) return false;
	FCar C;
	MakeCar(Models[FMath::Abs(Pick) % Models.Num()], C);
	CarState.Add(C);
	PlaceCar(CarState.Num() - 1, World - GetActorLocation(), Yaw);
	return true;
}

FVector ASmvCityLife::PathPoint(int32 Path, double S, FVector& OutDir) const
{
	const TArray<FVector>& P = WalkPaths[Path];
	S = FMath::Fmod(S, WalkLen[Path]);
	if (S < 0) S += WalkLen[Path];
	for (int32 i = 0; i < P.Num(); ++i)
	{
		const FVector& A = P[i];
		const FVector& B = P[(i + 1) % P.Num()];
		const double Sg = FVector::Dist(A, B);
		if (S <= Sg || i == P.Num() - 1)
		{
			OutDir = (B - A).GetSafeNormal();
			return A + OutDir * FMath::Min(S, Sg);
		}
		S -= Sg;
	}
	OutDir = FVector::ForwardVector;
	return P[0];
}

int32 ASmvCityLife::BuildCrowd(const TArray<TArray<FVector>>& Walk, const TArray<FVector>& Spots, int32 Count, int32 Seed)
{
	const TArray<USkeletalMesh*>& People = FSmvCityLife::PeopleModels();
	if (People.Num() == 0 || Count <= 0) return 0;
	uint32 Rs = (uint32)Seed * 2246822519u + 5u;
	const FVector Me = GetActorLocation();
	for (const TArray<FVector>& W : Walk)
	{
		if (W.Num() < 2) continue;
		TArray<FVector> L;
		for (const FVector& X : W) L.Add(X - Me);
		double Len = 0;
		for (int32 i = 0; i < L.Num(); ++i) Len += FVector::Dist(L[i], L[(i + 1) % L.Num()]);
		WalkPaths.Add(L);
		WalkLen.Add(FMath::Max(1.0, Len));
	}
	const float CullCm = FSmvCityLife::GetFloat(TEXT("PeopleCullM"), 450.f) * 100.f;
	auto Make = [&](USkeletalMesh* M, UAnimSequence* A, float Rate) -> USkeletalMeshComponent*
	{
		USkeletalMeshComponent* C = NewObject<USkeletalMeshComponent>(this);
		C->SetSkeletalMeshAsset(M);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(true);
		C->bEnableUpdateRateOptimizations = true;
		C->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
		C->LDMaxDrawDistance = CullCm;
		C->SetCachedMaxDrawDistance(CullCm);
		C->SetupAttachment(Root);
		C->RegisterComponent();
		if (A)
		{
			C->PlayAnimation(A, true);
			C->SetPlayRate(Rate);
			C->SetPosition((float)(SmvRand(Rs) * A->GetPlayLength()), false);
		}
		PeopleComps.Add(C);
		return C;
	};
	// %70 yürüyen (kaldırım döngüleri), %30 duran gruplar (meydan)
	const int32 NWalk = WalkPaths.Num() ? FMath::RoundToInt(Count * (Spots.Num() ? 0.7 : 1.0)) : 0;
	for (int32 i = 0; i < NWalk; ++i)
	{
		USkeletalMesh* M = People[(int32)(SmvRand(Rs) * People.Num()) % People.Num()];
		FWalker Wk;
		Wk.Path = (int32)(SmvRand(Rs) * WalkPaths.Num()) % WalkPaths.Num();
		Wk.S = SmvRand(Rs) * WalkLen[Wk.Path];
		Wk.Dir = SmvRand(Rs) < 0.5 ? 1 : -1;
		Wk.V = 120.0 + 40.0 * SmvRand(Rs);
		Wk.Side = (SmvRand(Rs) - 0.5) * 160.0;
		Make(M, FSmvCityLife::WalkAnim(M), (float)(Wk.V / 140.0));
		Wk.Comp = PeopleComps.Num() - 1;
		Walkers.Add(Wk);
	}
	for (int32 i = NWalk; i < Count && Spots.Num(); )
	{
		const FVector C0 = Spots[(int32)(SmvRand(Rs) * Spots.Num()) % Spots.Num()] - Me;
		const int32 Grp = 2 + (int32)(SmvRand(Rs) * 4.0);
		for (int32 k = 0; k < Grp && i < Count; ++k, ++i)
		{
			USkeletalMesh* M = People[(int32)(SmvRand(Rs) * People.Num()) % People.Num()];
			const double A = 2.0 * UE_DOUBLE_PI * k / Grp + SmvRand(Rs) * 0.4;
			const FVector P = C0 + FVector(FMath::Cos(A), FMath::Sin(A), 0.0) * (70.0 + 30.0 * SmvRand(Rs));
			UAnimSequence* Idle = FSmvCityLife::IdleAnim(M);
			USkeletalMeshComponent* C = Make(M, Idle ? Idle : FSmvCityLife::WalkAnim(M), Idle ? 1.f : 0.f);
			// grubun ortasına bakar (UE5 Manny/Quinn ağı +Y'ye bakar → −90° düzeltme)
			const FVector To = (C0 - P).GetSafeNormal2D();
			C->SetRelativeLocationAndRotation(P, FRotator(0.0, To.Rotation().Yaw - 90.0, 0.0));
		}
	}
	UE_LOG(LogSimumav, Log, TEXT("Şehir kalabalığı kuruldu: %d kişi (%d yürüyen, %d yol; yalnız görsel)"), PeopleComps.Num(), Walkers.Num(), WalkPaths.Num());
	return PeopleComps.Num();
}

void ASmvCityLife::Tick(float Dt)
{
	Super::Tick(Dt);
	Dt = FMath::Min(Dt, 0.1f);
	T += Dt;
	// ---- trafik ----
	for (FLane& L : Lanes)
	{
		const int32 N = L.Cars.Num();
		for (int32 k = 0; k < N; ++k)
		{
			FCar& C = CarState[L.Cars[k]];
			// öndeki araç (döngüsel): güvenli aralık 4 m + boy
			double Allow = 1e9;
			if (N > 1)
			{
				const FCar& Ld = CarState[L.Cars[(k + 1) % N]];
				double Gap = Ld.S - C.S;
				if (Gap <= 0) Gap += L.Len;
				Allow = Gap - (C.LenCm + Ld.LenCm) * 0.5 - 400.0;
			}
			// kırmızı ışık: önümüzdeki ilk dur çizgisi (kavşağa girmiş araç geçer)
			if (!Green(L.Axis))
				for (double St : L.Stops)
				{
					const double D = St - (C.S + C.LenCm * 0.5);
					if (D >= -50.0 && D < 4000.0) { Allow = FMath::Min(Allow, D); break; }
				}
			const double Vw = Allow <= 0 ? 0.0 : FMath::Min(C.VMax, FMath::Sqrt(2.0 * 450.0 * Allow));
			C.V = Vw < C.V ? FMath::Max(Vw, C.V - 900.0 * Dt) : FMath::Min(Vw, C.V + 250.0 * Dt);
			C.S += C.V * Dt;
			if (C.S >= L.Len) C.S -= L.Len;
		}
		// sıra döngüsel korunur; sarma sonrası en küçük s başa
		int32 Mn = 0;
		for (int32 k = 1; k < N; ++k) if (CarState[L.Cars[k]].S < CarState[L.Cars[Mn]].S) Mn = k;
		if (Mn > 0) { TArray<int32> R; for (int32 k = 0; k < N; ++k) R.Add(L.Cars[(Mn + k) % N]); L.Cars = R; }
	}
	for (int32 i = 0; i < CarState.Num(); ++i)
	{
		FCar& C = CarState[i];
		if (C.Lane < 0) continue;
		float Yaw = 0.f;
		PlaceCar(i, LanePoint(Lanes[C.Lane], C.S, Yaw), Yaw);
		if (C.V > 1.0 && C.Wheels.Num())
		{
			C.WheelPhase = FMath::Fmod(C.WheelPhase + C.V * Dt / C.WheelR, 2.0 * UE_DOUBLE_PI);
			if (UPoseableMeshComponent* P = Cast<UPoseableMeshComponent>(CarComps[i]))
				if (P->WasRecentlyRendered(0.5f))
					for (int32 w = 0; w < C.Wheels.Num(); ++w)
						P->SetBoneRotationByName(C.Wheels[w], (FQuat(FVector::RightVector, -C.WheelPhase) * C.WheelRef[w]).Rotator(), EBoneSpaces::ComponentSpace);
		}
	}
	// ---- yayalar ----
	for (FWalker& Wk : Walkers)
	{
		USkeletalMeshComponent* C = PeopleComps.IsValidIndex(Wk.Comp) ? PeopleComps[Wk.Comp].Get() : nullptr;
		if (!C) continue;
		Wk.S += Wk.V * Dt * Wk.Dir;
		FVector Dir;
		const FVector P = PathPoint(Wk.Path, Wk.S, Dir);
		const FVector Fw = Dir * (double)Wk.Dir;
		const FVector Side = FVector(-Fw.Y, Fw.X, 0.0) * Wk.Side;
		C->SetRelativeLocationAndRotation(P + Side, FRotator(0.0, Fw.Rotation().Yaw - 90.0, 0.0));
	}
}
