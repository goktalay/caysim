#include "SmvCityLife.h"
#include "SimumavModule.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ReferenceSkeleton.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SmvUiKit.h"

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
		TEXT("/Game/Drone-Sounds/Cue/44_1kHz-24bit"),
		TEXT("/Game/150SpaceRank/PNG/128"),
		// City Sample Crowds (Epic; yakın plan MetaHuman yayalar): gövde / giysi / ayakkabı ağları, yüzler, saç "kask"ları, yürüme / bekleme
		TEXT("/Game/CitySampleCrowd/Character/Male/NormalWeight/Meshes"),
		TEXT("/Game/CitySampleCrowd/Character/Male/OverWeight/Meshes"),
		TEXT("/Game/CitySampleCrowd/Character/Male/UnderWeight/Meshes"),
		TEXT("/Game/CitySampleCrowd/Character/Female/NormalWeight/Meshes"),
		TEXT("/Game/CitySampleCrowd/Character/Female/OverWeight/Meshes"),
		TEXT("/Game/CitySampleCrowd/Character/Female/UnderWeight/Meshes"),
		TEXT("/Game/CitySampleCrowd/Character/Male/m_001"),
		TEXT("/Game/CitySampleCrowd/Character/Male/m_002"),
		TEXT("/Game/CitySampleCrowd/Character/Male/m_003"),
		TEXT("/Game/CitySampleCrowd/Character/Male/m_004"),
		TEXT("/Game/CitySampleCrowd/Character/Male/m_005"),
		TEXT("/Game/CitySampleCrowd/Character/Male/m_006"),
		TEXT("/Game/CitySampleCrowd/Character/Female/f_001"),
		TEXT("/Game/CitySampleCrowd/Character/Female/f_002"),
		TEXT("/Game/CitySampleCrowd/Character/Female/f_003"),
		TEXT("/Game/CitySampleCrowd/Character/Female/f_004"),
		TEXT("/Game/CitySampleCrowd/Character/Female/f_005"),
		TEXT("/Game/CitySampleCrowd/Character/Female/f_006"),
		TEXT("/Game/CitySampleCrowd/Character/Anims/Loco"),
		// Workbrand Techwear (MetaHuman giysisi; pilot kıyafeti — SmvPilotFigure)
		TEXT("/Game/WB_Techwear/SkeletalMeshes/MTN"),
		TEXT("/Game/WB_Techwear/SkeletalMeshes/FTN"),
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

	// ---- City Sample Crowds (/Game/CitySampleCrowd): modüler MetaHuman yaya — gövde + üst + alt + ayakkabı + yüz + saç kaskı ----
	struct FCsBody { USkeletalMesh* Body = nullptr; TArray<USkeletalMesh*> Tops, Bottoms, Shoes; bool bF = false; };
	struct FCsFace { USkeletalMesh* Face = nullptr; TArray<UObject*> Hair; bool bF = false; };
	struct FCsKit
	{
		TArray<FCsBody> Bodies;
		TArray<FCsFace> Faces;
		TArray<UAnimSequence*> Walk[2], Idle[2];   // [0] erkek, [1] kadın
		bool Has(bool bF) const
		{
			bool bB = false, bFc = false;
			for (const FCsBody& B : Bodies) bB |= B.bF == bF;
			for (const FCsFace& F : Faces) bFc |= F.bF == bF;
			return bB && bFc && Walk[bF ? 1 : 0].Num() > 0;
		}
	};

	const FCsKit& SmvCsKit()
	{
		static FCsKit K;
		static bool bRead = false;
		if (bRead) return K;
		bRead = true;
		const FString R = TEXT("/Game/CitySampleCrowd/Character");
		for (int32 g = 0; g < 2; ++g)
		{
			const TCHAR* Sex = g ? TEXT("Female") : TEXT("Male");
			for (const TCHAR* Wt : { TEXT("NormalWeight"), TEXT("OverWeight"), TEXT("UnderWeight") })
			{
				TArray<USkeletalMesh*> Ms;
				SmvAssetsIn<USkeletalMesh>(*FString::Printf(TEXT("%s/%s/%s/Meshes"), *R, Sex, Wt), Ms, [](const FAssetData&) { return true; });
				FCsBody B; B.bF = g == 1;
				for (USkeletalMesh* M : Ms)
				{
					const FString N = M->GetName().ToLower();
					if (N.EndsWith(TEXT("_body"))) B.Body = M;
					else if (N.EndsWith(TEXT("_base"))) continue;
					else if (N.Contains(TEXT("loafers")) || N.Contains(TEXT("oxfords")) || N.Contains(TEXT("flats"))) B.Shoes.Add(M);
					else if (N.Contains(TEXT("jeans")) || N.Contains(TEXT("slacks")) || N.Contains(TEXT("skirt"))) B.Bottoms.Add(M);
					else B.Tops.Add(M);
				}
				if (B.Body && B.Tops.Num() && B.Bottoms.Num()) K.Bodies.Add(B);
			}
			for (int32 i = 1; i <= 6; ++i)
			{
				const FString Dir = FString::Printf(TEXT("%s/%s/%s_%03d"), *R, Sex, g ? TEXT("f") : TEXT("m"), i);
				TArray<USkeletalMesh*> Fm;
				SmvAssetsIn<USkeletalMesh>(*(Dir + TEXT("/Face")), Fm, [](const FAssetData&) { return true; });
				if (Fm.Num() == 0) continue;
				FCsFace F; F.Face = Fm[0]; F.bF = g == 1;
				// saç / sakal "kask"ları (MetaHuman uzak LOD saçı; LOD5 en ayrıntılısı) — groom yerine, kalabalıkta ucuz
				TArray<FAssetData> Ha;
				IAssetRegistry::GetChecked().GetAssetsByPath(FName(*(Dir + TEXT("/Hair/Helmets"))), Ha, true);
				for (const FAssetData& A : Ha)
					if (A.AssetName.ToString().Contains(TEXT("LOD5")))
						if (UObject* O = A.GetAsset()) if (O->IsA<USkeletalMesh>() || O->IsA<UStaticMesh>()) { O->AddToRoot(); F.Hair.Add(O); }
				K.Faces.Add(F);
			}
		}
		TArray<UAnimSequence*> An;
		SmvAssetsIn<UAnimSequence>(*FString::Printf(TEXT("%s/Anims/Loco"), *R), An, [](const FAssetData&) { return true; });
		for (UAnimSequence* A : An)
		{
			const FString N = A->GetName();
			const int32 g = N.StartsWith(TEXT("FTN")) ? 1 : N.StartsWith(TEXT("MTN")) ? 0 : -1;
			if (g < 0) continue;
			const bool bWalk = N.Contains(TEXT("_N_Walk")) && !N.Contains(TEXT("Quickly")) && !N.Contains(TEXT("_B")) && !N.Contains(TEXT("45")) && !N.Contains(TEXT("LF")) && !N.Contains(TEXT("RF"));
			const bool bIdle = N.Contains(TEXT("_N_Idle"));
			// kök hareketi kilitli: konum kodla yürütülür (yerinde yürüme yoksa kök kemiği ileri kayıp geri sıçrıyordu)
			if (bWalk) { A->bForceRootLock = true; K.Walk[g].Add(A); }
			else if (bIdle) { A->bForceRootLock = true; K.Idle[g].Add(A); }
		}
		UE_LOG(LogSimumav, Log, TEXT("City Sample yayaları: %d gövde kümesi, %d yüz, yürüme %d/%d, bekleme %d/%d (erkek/kadın)"), K.Bodies.Num(), K.Faces.Num(),
			K.Walk[0].Num(), K.Walk[1].Num(), K.Idle[0].Num(), K.Idle[1].Num());
		return K;
	}
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
	// yalnız VAT malzemesi bağlı pişirilmiş ağlar (SM_<ağ>_VAT; malzeme M_SmvCrowdVat örneği) — yarım kalmış pişirme kullanılmaz
	SmvAssetsIn<UStaticMesh>(GVatDir, Out, [](const FAssetData& X) { return X.AssetName.ToString().EndsWith(TEXT("_VAT")); });
	Out.RemoveAll([](UStaticMesh* M)
	{
		UMaterialInterface* Mi = M ? M->GetMaterial(0) : nullptr;
		UMaterial* B = Mi ? Mi->GetBaseMaterial() : nullptr;
		return !B || B->GetName() != TEXT("M_SmvCrowdVat");
	});
	if (FSmvCityLife::GetFloat(TEXT("VatCrowd"), 1.f) < 0.5f) Out.Reset();
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

namespace
{
	const FLinearColor GSmvTops[] = { FLinearColor(0.32f, 0.05f, 0.05f), FLinearColor(0.05f, 0.12f, 0.3f), FLinearColor(0.55f, 0.5f, 0.42f), FLinearColor(0.08f, 0.2f, 0.1f),
		FLinearColor(0.6f, 0.6f, 0.62f), FLinearColor(0.35f, 0.22f, 0.08f), FLinearColor(0.04f, 0.04f, 0.05f), FLinearColor(0.45f, 0.3f, 0.36f) };
}

bool ASmvCityLife::BuildVatCrowd(const TArray<FVector>& Spots, int32 Count, uint32& Rs)
{
	const TArray<UStaticMesh*>& Vat = FSmvCityLife::VatCrowdMeshes();
	if (Vat.Num() == 0) return false;
	const FVector Me = GetActorLocation();
	const float CullCm = FSmvCityLife::GetFloat(TEXT("PeopleCullM"), 450.f) * 100.f;
	for (UStaticMesh* M : Vat)
	{
		UInstancedStaticMeshComponent* I = NewObject<UInstancedStaticMeshComponent>(this);
		I->SetMobility(EComponentMobility::Movable);
		I->SetStaticMesh(M);
		I->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		I->SetCastShadow(true);
		I->SetNumCustomDataFloats(6);   // 0 zaman kayması, 1 klip (0 yürüme / 1 bekleme), 2 oynatma hızı, 3-5 giysi rengi
		I->SetCullDistances(0, (int32)CullCm);
		I->LDMaxDrawDistance = CullCm;
		I->SetCachedMaxDrawDistance(CullCm);
		I->SetupAttachment(Root);
		I->RegisterComponent();
		CrowdHisms.Add(I);
		VatWalkXf.AddDefaulted();
	}
	auto Add = [&](int32 Ism, const FTransform& X, float Clip, float Rate) -> int32
	{
		UInstancedStaticMeshComponent* I = CrowdHisms[Ism];
		const int32 Idx = I->AddInstance(X, false);
		const FLinearColor C = GSmvTops[(int32)(SmvRand(Rs) * 8) % 8];
		const TArray<float> Data = { (float)(SmvRand(Rs) * 10.0), Clip, Rate, C.R, C.G, C.B };
		I->SetCustomData(Idx, Data, false);
		++VatPeople;
		return Idx;
	};
	// %70 yürüyen (kaldırım döngüleri), %30 duran gruplar (meydan) — iskeletli yolla aynı dağılım
	const int32 NWalk = WalkPaths.Num() ? FMath::RoundToInt(Count * (Spots.Num() ? 0.7 : 1.0)) : 0;
	for (int32 i = 0; i < NWalk; ++i)
	{
		FWalker Wk;
		Wk.Path = (int32)(SmvRand(Rs) * WalkPaths.Num()) % WalkPaths.Num();
		Wk.S = SmvRand(Rs) * WalkLen[Wk.Path];
		Wk.Dir = SmvRand(Rs) < 0.5 ? 1 : -1;
		Wk.V = 120.0 + 40.0 * SmvRand(Rs);
		Wk.Side = (SmvRand(Rs) - 0.5) * 160.0;
		Wk.Ism = (int32)(SmvRand(Rs) * CrowdHisms.Num()) % CrowdHisms.Num();
		FVector Dir;
		const FVector P = PathPoint(Wk.Path, Wk.S, Dir);
		const FTransform X(FRotator(0.0, (Dir * (double)Wk.Dir).Rotation().Yaw - 90.0, 0.0), P);
		Wk.Inst = Add(Wk.Ism, X, 0.f, (float)(Wk.V / 140.0));
		VatWalkXf[Wk.Ism].Add(X);
		Walkers.Add(Wk);
	}
	for (int32 i = NWalk; i < Count && Spots.Num(); )
	{
		const FVector C0 = Spots[(int32)(SmvRand(Rs) * Spots.Num()) % Spots.Num()] - Me;
		const int32 Grp = 2 + (int32)(SmvRand(Rs) * 4.0);
		for (int32 k = 0; k < Grp && i < Count; ++k, ++i)
		{
			const double A = 2.0 * UE_DOUBLE_PI * k / Grp + SmvRand(Rs) * 0.4;
			const FVector P = C0 + FVector(FMath::Cos(A), FMath::Sin(A), 0.0) * (70.0 + 30.0 * SmvRand(Rs));
			const FVector To = (C0 - P).GetSafeNormal2D();
			Add((int32)(SmvRand(Rs) * CrowdHisms.Num()) % CrowdHisms.Num(), FTransform(FRotator(0.0, To.Rotation().Yaw - 90.0, 0.0), P), 1.f, 0.9f + 0.2f * (float)SmvRand(Rs));
		}
	}
	for (UInstancedStaticMeshComponent* I : CrowdHisms) I->MarkRenderStateDirty();
	UE_LOG(LogSimumav, Log, TEXT("Şehir kalabalığı kuruldu (OverCrowd VAT): %d kişi (%d yürüyen, %d ağ, %d yol; yalnız görsel)"), VatPeople, Walkers.Num(), CrowdHisms.Num(), WalkPaths.Num());
	return VatPeople > 0;
}

int32 ASmvCityLife::BuildCrowd(const TArray<TArray<FVector>>& Walk, const TArray<FVector>& Spots, int32 Count, int32 Seed)
{
	const TArray<USkeletalMesh*>& People = FSmvCityLife::PeopleModels();
	if ((People.Num() == 0 && FSmvCityLife::VatCrowdMeshes().Num() == 0 && !FSmvCityLife::HasCsCrowd()) || Count <= 0) return 0;
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
	if (BuildVatCrowd(Spots, Count, Rs)) return VatPeople + BuildCsNear(Spots, Rs);
	if (People.Num() == 0) return BuildCsNear(Spots, Rs);
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
		// manken gövdesi gümüş/robot gibi görünüyordu: yuvalara mat giysi renkleri (üst: canlı/ton, alt: koyu pantolon)
		if (UMaterialInterface* Solid = SmvSolidMat(C->GetMaterial(0)))
		{
			static const FLinearColor Tops[] = { FLinearColor(0.32f, 0.05f, 0.05f), FLinearColor(0.05f, 0.12f, 0.3f), FLinearColor(0.55f, 0.5f, 0.42f), FLinearColor(0.08f, 0.2f, 0.1f),
				FLinearColor(0.6f, 0.6f, 0.62f), FLinearColor(0.35f, 0.22f, 0.08f), FLinearColor(0.04f, 0.04f, 0.05f), FLinearColor(0.45f, 0.3f, 0.36f) };
			static const FLinearColor Bottoms[] = { FLinearColor(0.03f, 0.04f, 0.07f), FLinearColor(0.06f, 0.06f, 0.06f), FLinearColor(0.18f, 0.15f, 0.11f), FLinearColor(0.05f, 0.08f, 0.16f) };
			for (int32 k = 0; k < C->GetNumMaterials(); ++k)
			{
				UMaterialInstanceDynamic* D = UMaterialInstanceDynamic::Create(Solid, C);
				SmvSetSolidColor(D, k == 0 ? Tops[(int32)(SmvRand(Rs) * 8) % 8] : Bottoms[(int32)(SmvRand(Rs) * 4) % 4]);
				D->SetScalarParameterValue(TEXT("Roughness"), 0.85f);
				D->SetScalarParameterValue(TEXT("Metallic"), 0.f);
				C->SetMaterial(k, D);
			}
		}
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
	BuildCsNear(Spots, Rs);
	return PeopleComps.Num();
}

bool FSmvCityLife::HasCsCrowd() { return SmvCsKit().Has(false) || SmvCsKit().Has(true); }

// Yakın plan (kalkış noktasının ~90 m çevresi) City Sample MetaHuman yayaları: uzakta OverCrowd VAT / manken kalabalığı kalır.
// [/Script/Simumav.SmvCityLife] CsWalkers (12), CsIdle (10), CsCullM (80), CsFaceMinLod (1)
int32 ASmvCityLife::BuildCsNear(const TArray<FVector>& Spots, uint32& Rs)
{
	const FCsKit& K = SmvCsKit();
	const bool bM = K.Has(false), bFm = K.Has(true);
	if (!bM && !bFm) return 0;
	const int32 NWalk = FMath::Clamp((int32)FSmvCityLife::GetFloat(TEXT("CsWalkers"), 12.f), 0, 60);
	const int32 NIdle = FMath::Clamp((int32)FSmvCityLife::GetFloat(TEXT("CsIdle"), 10.f), 0, 60);
	const float CullCm = FSmvCityLife::GetFloat(TEXT("CsCullM"), 80.f) * 100.f;
	const int32 FaceLod = FMath::Clamp((int32)FSmvCityLife::GetFloat(TEXT("CsFaceMinLod"), 1.f), 0, 7);
	const FVector Home = -GetActorLocation();   // dünya orijini (kalkış noktası) aktöre göre
	static bool bDumped = false;
	auto Part = [this, CullCm](USkeletalMeshComponent* Leader, USkeletalMesh* M) -> USkeletalMeshComponent*
	{
		USkeletalMeshComponent* C = NewObject<USkeletalMeshComponent>(this);
		C->SetSkeletalMeshAsset(M);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(true);
		C->LDMaxDrawDistance = CullCm;
		C->SetCachedMaxDrawDistance(CullCm);
		C->SetupAttachment(Leader ? (USceneComponent*)Leader : (USceneComponent*)Root.Get());
		C->RegisterComponent();
		if (Leader) C->SetLeaderPoseComponent(Leader);
		CsParts.Add(C);
		return C;
	};
	auto MakeOne = [&](bool bF, UAnimSequence* A, float Rate) -> int32
	{
		TArray<const FCsBody*> Bs; for (const FCsBody& B : K.Bodies) if (B.bF == bF) Bs.Add(&B);
		TArray<const FCsFace*> Fs; for (const FCsFace& F : K.Faces) if (F.bF == bF) Fs.Add(&F);
		const FCsBody& B = *Bs[(int32)(SmvRand(Rs) * Bs.Num()) % Bs.Num()];
		const FCsFace& F = *Fs[(int32)(SmvRand(Rs) * Fs.Num()) % Fs.Num()];
		USkeletalMeshComponent* L = NewObject<USkeletalMeshComponent>(this);
		L->SetSkeletalMeshAsset(B.Body);
		L->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		L->SetCastShadow(true);
		L->bEnableUpdateRateOptimizations = true;
		L->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
		L->LDMaxDrawDistance = CullCm;
		L->SetCachedMaxDrawDistance(CullCm);
		L->SetupAttachment(Root);
		L->RegisterComponent();
		USkeletalMesh* Top = B.Tops[(int32)(SmvRand(Rs) * B.Tops.Num()) % B.Tops.Num()];
		USkeletalMesh* Bot = B.Bottoms[(int32)(SmvRand(Rs) * B.Bottoms.Num()) % B.Bottoms.Num()];
		Part(L, Top);
		Part(L, Bot);
		if (B.Shoes.Num()) Part(L, B.Shoes[(int32)(SmvRand(Rs) * B.Shoes.Num()) % B.Shoes.Num()]);
		USkeletalMeshComponent* Fc = Part(L, F.Face);
		Fc->OverrideMinLOD(FaceLod);
		for (UObject* H : F.Hair)
		{
			if (USkeletalMesh* Hs = Cast<USkeletalMesh>(H)) Part(L, Hs);
			else if (UStaticMesh* Hm = Cast<UStaticMesh>(H))
			{
				UStaticMeshComponent* Sc = NewObject<UStaticMeshComponent>(this);
				Sc->SetStaticMesh(Hm);
				Sc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Sc->LDMaxDrawDistance = CullCm;
				Sc->SetCachedMaxDrawDistance(CullCm);
				Sc->SetupAttachment(L);
				Sc->RegisterComponent();
				CsStatic.Add(Sc);
			}
		}
		if (!bDumped)
		{
			bDumped = true;
			auto NaniteOn = [](UObject* M) -> int32
			{
				FStructProperty* Sp = M ? CastField<FStructProperty>(M->GetClass()->FindPropertyByName(TEXT("NaniteSettings"))) : nullptr;
				FBoolProperty* Bp = Sp ? CastField<FBoolProperty>(Sp->Struct->FindPropertyByName(TEXT("bEnabled"))) : nullptr;
				return Bp ? (Bp->GetPropertyValue_InContainer(Sp->ContainerPtrToValuePtr<void>(M)) ? 1 : 0) : -1;
			};
			UE_LOG(LogSimumav, Log, TEXT("City Sample Nanite (1 açık, 0 kapalı, -1 alan yok): gövde %d, üst %d, yüz %d · LOD gövde %d, yüz %d"), NaniteOn(B.Body), NaniteOn(Top), NaniteOn(F.Face),
				B.Body->GetLODNum(), F.Face->GetLODNum());
			// malzeme parametreleri günlüğe (giysi rengi çeşitlemesi için; FSmvFabContent::DumpMaterialParams)
			UE_LOG(LogSimumav, Log, TEXT("City Sample yaya örneği: gövde %s, üst %s, alt %s, yüz %s, saç %d"), *B.Body->GetName(), *Top->GetName(), *Bot->GetName(), *F.Face->GetName(), F.Hair.Num());
			for (USkeletalMeshComponent* Pc : { L, CsParts.Last().Get() })
				for (int32 k = 0; k < Pc->GetNumMaterials(); ++k)
					if (UMaterialInterface* Mi = Pc->GetMaterial(k)) UE_LOG(LogSimumav, Log, TEXT("  %s yuva %d: %s"), *Pc->GetSkeletalMeshAsset()->GetName(), k, *Mi->GetPathName());
		}
		if (A)
		{
			L->PlayAnimation(A, true);
			L->SetPlayRate(Rate);
			L->SetPosition((float)(SmvRand(Rs) * A->GetPlayLength()), false);
		}
		PeopleComps.Add(L);
		return PeopleComps.Num() - 1;
	};
	auto PickSex = [&]() { return bM && bFm ? SmvRand(Rs) < 0.5 : bFm; };
	auto PickAnim = [&](bool bF, bool bWalk) -> UAnimSequence*
	{
		const TArray<UAnimSequence*>& A = bWalk ? K.Walk[bF ? 1 : 0] : K.Idle[bF ? 1 : 0];
		return A.Num() ? A[(int32)(SmvRand(Rs) * A.Num()) % A.Num()] : nullptr;
	};
	int32 Made = 0;
	// yürüyenler: kalkış noktasına en yakın yollar (≤ 120 m), en yakın noktadan ±25 m
	TArray<TPair<double, int32>> Near;
	for (int32 p = 0; p < WalkPaths.Num(); ++p)
	{
		double Best = 1e18, S = 0, BestS = 0;
		const TArray<FVector>& L = WalkPaths[p];
		for (int32 i = 0; i < L.Num(); ++i)
		{
			const double D = FVector::Dist2D(L[i], Home);
			if (D < Best) { Best = D; BestS = S; }
			S += FVector::Dist(L[i], L[(i + 1) % L.Num()]);
		}
		if (Best < 12000.0) Near.Add(TPair<double, int32>(BestS, p));
	}
	for (int32 i = 0; i < NWalk && Near.Num(); ++i)
	{
		const TPair<double, int32>& Np = Near[i % Near.Num()];
		const bool bF = PickSex();
		FWalker Wk;
		Wk.Path = Np.Value;
		Wk.S = Np.Key + (SmvRand(Rs) - 0.5) * 5000.0;
		Wk.Dir = SmvRand(Rs) < 0.5 ? 1 : -1;
		Wk.V = 115.0 + 35.0 * SmvRand(Rs);
		Wk.Side = (SmvRand(Rs) - 0.5) * 180.0;
		Wk.Comp = MakeOne(bF, PickAnim(bF, true), (float)(Wk.V / 135.0));
		Walkers.Add(Wk);
		++Made;
	}
	// duranlar: en yakın gruplar (yoksa kalkış noktasının 10–30 m çevresi), 2–3 kişilik, ortaya bakan
	TArray<FVector> Sp;
	for (const FVector& X : Spots) Sp.Add(X - GetActorLocation());
	Sp.Sort([&Home](const FVector& A, const FVector& B) { return FVector::DistSquared2D(A, Home) < FVector::DistSquared2D(B, Home); });
	if (FParse::Param(FCommandLine::Get(), TEXT("SmvSmoke"))) Sp.Insert(Home + FVector(0.0, -800.0, 0.0), 0);   // duman: kameranın önünde görünür grup (kuzey = −Y)
	if (Sp.Num() == 0 || FVector::Dist2D(Sp[0], Home) > 6000.0)
		for (int32 k = 0; k < 4; ++k) { const double A = SmvRand(Rs) * UE_DOUBLE_TWO_PI, Rr = 1000.0 + 2000.0 * SmvRand(Rs); Sp.Insert(Home + FVector(FMath::Cos(A) * Rr, FMath::Sin(A) * Rr, 0.0), 0); }
	for (int32 i = 0, g = 0; i < NIdle && Sp.Num(); ++g)
	{
		const FVector C0 = Sp[g % FMath::Min(Sp.Num(), 4)] + (g == 0 ? FVector::ZeroVector : FVector((SmvRand(Rs) - 0.5) * 600.0, (SmvRand(Rs) - 0.5) * 600.0, 0.0));
		const int32 Grp = 2 + (int32)(SmvRand(Rs) * 2.0);
		for (int32 k = 0; k < Grp && i < NIdle; ++k, ++i)
		{
			const bool bF = PickSex();
			UAnimSequence* A = PickAnim(bF, false);
			const int32 Ci = MakeOne(bF, A ? A : PickAnim(bF, true), A ? 1.f : 0.f);
			const double Ang = UE_DOUBLE_TWO_PI * k / Grp + SmvRand(Rs) * 0.4;
			const FVector P = C0 + FVector(FMath::Cos(Ang), FMath::Sin(Ang), 0.0) * (60.0 + 25.0 * SmvRand(Rs));
			const FVector To = (C0 - P).GetSafeNormal2D();
			// zemin: grup merkezinin yüksekliği (Spots / yollar zemin üstünde verilir)
			PeopleComps[Ci]->SetRelativeLocationAndRotation(P, FRotator(0.0, To.Rotation().Yaw - 90.0, 0.0));
			++Made;
		}
	}
	UE_LOG(LogSimumav, Log, TEXT("City Sample yakın plan yayalar: %d (%d yürüyen, %d duran; %d parça bileşeni, %.0f m'de kaybolur)"), Made, FMath::Min(NWalk, Near.Num() ? NWalk : 0),
		Made - FMath::Min(NWalk, Near.Num() ? NWalk : 0), CsParts.Num() + CsStatic.Num(), CullCm / 100.f);
	return Made;
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
	// City Sample tanısı: 6 sn sonra ilk yayaların çizilip çizilmediği
	if (!bCsDiag && CsParts.Num() && T > 70.0)   // yükleme bittikten sonra
	{
		bCsDiag = true;
		int32 Shown = 0, N = 0;
		for (USkeletalMeshComponent* C : PeopleComps)
		{
			if (!C || !C->GetSkeletalMeshAsset() || !C->GetSkeletalMeshAsset()->GetPathName().StartsWith(TEXT("/Game/CitySampleCrowd"))) continue;
			if (N < 3) UE_LOG(LogSimumav, Log, TEXT("City Sample yaya %d: %s dünya %s, sınır %.0f cm, görünür %d, çizildi %d, LOD %d"), N, *C->GetSkeletalMeshAsset()->GetName(),
				*C->GetComponentLocation().ToString(), C->Bounds.SphereRadius, C->IsVisible() ? 1 : 0, C->WasRecentlyRendered(1.f) ? 1 : 0, C->GetPredictedLODLevel());
			Shown += C->WasRecentlyRendered(1.f) ? 1 : 0; ++N;
		}
		UE_LOG(LogSimumav, Log, TEXT("City Sample yayaları çizim: %d / %d son 1 sn'de çizildi"), Shown, N);
	}
	// ---- yayalar ----
	for (FWalker& Wk : Walkers)
	{
		USkeletalMeshComponent* C = PeopleComps.IsValidIndex(Wk.Comp) ? PeopleComps[Wk.Comp].Get() : nullptr;
		const bool bVat = VatWalkXf.IsValidIndex(Wk.Ism) && VatWalkXf[Wk.Ism].IsValidIndex(Wk.Inst);
		if (!C && !bVat) continue;
		Wk.S += Wk.V * Dt * Wk.Dir;
		FVector Dir;
		const FVector P = PathPoint(Wk.Path, Wk.S, Dir);
		const FVector Fw = Dir * (double)Wk.Dir;
		const FVector Side = FVector(-Fw.Y, Fw.X, 0.0) * Wk.Side;
		const FRotator R(0.0, Fw.Rotation().Yaw - 90.0, 0.0);
		if (C) C->SetRelativeLocationAndRotation(P + Side, R);
		else VatWalkXf[Wk.Ism][Wk.Inst] = FTransform(R, P + Side);
	}
	// VAT kalabalığı: yürüyenler her ISM'de 0..n-1 (önce eklendiler) — tek toplu güncelleme
	for (int32 k = 0; k < CrowdHisms.Num() && k < VatWalkXf.Num(); ++k)
		if (CrowdHisms[k] && VatWalkXf[k].Num() > 0)
			CrowdHisms[k]->BatchUpdateInstancesTransforms(0, VatWalkXf[k], false, true, false);
}
