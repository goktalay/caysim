// Çevrimdışı bölge paketinden gerçek dünya (bkz. SmvRegionWorld.h).
#include "SmvRegionWorld.h"
#include "SimumavModule.h"
#include "SimumavTypes.h"
#include "CaySimGeo.h"
#include "SmvRegionCore.h"
#include "SmvScenery.h"
#include "SmvUiKit.h"
#include "ProceduralMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Math/RandomStream.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"

namespace
{
	const TCHAR* GFacadeMat = TEXT("/Game/Simumav/Materials/M_SmvFacade.M_SmvFacade");
	const TCHAR* GTerrainWxMat = TEXT("/Game/Simumav/Materials/M_SmvTerrainWx.M_SmvTerrainWx");
	const TCHAR* GBaseMat = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");

	struct FMeshBuf
	{
		TArray<FVector> V; TArray<int32> T; TArray<FVector> N; TArray<FVector2D> Uv; TArray<FLinearColor> C; TArray<FProcMeshTangent> Tg;
		int32 Add(const FVector& P, const FVector& Nrm, const FVector2D& U, const FLinearColor& Col)
		{
			V.Add(P); N.Add(Nrm); Uv.Add(U); C.Add(Col);
			const FVector Tx = (FVector::ForwardVector - Nrm * FVector::DotProduct(Nrm, FVector::ForwardVector)).GetSafeNormal();
			Tg.Add(FProcMeshTangent(Tx.IsNearlyZero() ? FVector::RightVector : Tx, false));
			return V.Num() - 1;
		}
		/** üçgen; görünen yüz Want yönüne bakacak şekilde sarılır */
		void Tri(int32 A, int32 B, int32 Cc, const FVector& Want)
		{
			const FVector Fn = FVector::CrossProduct(V[B] - V[A], V[Cc] - V[A]);
			// ProceduralMesh: Cross(v1-v0, v2-v0) Want yönündeyse ön yüz o yöne bakar (BuildGroundDisk sözleşmesi: Cross.Z > 0 → yukarıdan görünür)
			if (FVector::DotProduct(Fn, Want) >= 0) T.Append({ A, B, Cc }); else T.Append({ A, Cc, B });
		}
		bool IsEmpty() const { return V.Num() == 0; }
	};

	FLinearColor RgbHex(uint32 C) { return FLinearColor(FColor((C >> 16) & 255, (C >> 8) & 255, C & 255)); }

	struct FArea { uint8 Kind; std::vector<SmvRegion::FXY> P; double MinE, MinN, MaxE, MaxN; };
	struct FBld { float H; std::vector<SmvRegion::FXY> P; };
	struct FRoad { float W; uint8 Sub; std::vector<SmvRegion::FXY> P; };

	void ReadPts(const TArray<TSharedPtr<FJsonValue>>& A, int32 From, std::vector<SmvRegion::FXY>& Out)
	{
		for (int32 i = From; i + 1 < A.Num(); i += 2) Out.push_back({ A[i]->AsNumber(), A[i + 1]->AsNumber() });
	}
}

ASmvRegionWorld::ASmvRegionWorld()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

void ASmvRegionWorld::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	FollowT += DeltaSeconds;
	if (FollowT < 0.5f || !ToUe) return;
	FollowT = 0;
	const FVector Now = ToUe(0, 0, CenterMsl);
	if (!Now.Equals(GetActorLocation(), 5.0)) SetActorLocation(Now);
}

void ASmvRegionWorld::Destroyed()
{
	if (Scenery) { Scenery->Destroy(); Scenery = nullptr; }
	Super::Destroyed();
}

UProceduralMeshComponent* ASmvRegionWorld::NewPart(const TCHAR* Name, bool bCollide)
{
	UProceduralMeshComponent* P = NewObject<UProceduralMeshComponent>(this, MakeUniqueObjectName(this, UProceduralMeshComponent::StaticClass(), Name));
	P->SetupAttachment(Root);
	P->bUseAsyncCooking = true;
	// yüzey sınıfı (SmvPhysics::SurfaceKind: çarpışma / sensör tanısı)
	const FString Nm(Name);
	P->ComponentTags.Add(Nm.Contains(TEXT("Building")) ? FName(TEXT("building")) : Nm.Contains(TEXT("Sea")) || Nm.Contains(TEXT("Water")) ? FName(TEXT("water")) : FName(TEXT("terrain")));
	P->RegisterComponent();
	if (bCollide)
	{
		P->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		P->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}
	else P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Parts.Add(P);
	return P;
}

UMaterialInterface* ASmvRegionWorld::TerrainMaterial()
{
	UMaterialInterface* M = FSmvScenery::TerrainMaterial();
	if (!M) M = LoadObject<UMaterialInterface>(nullptr, GTerrainWxMat, nullptr, LOAD_NoWarn | LOAD_Quiet);
	return M;
}

void ASmvRegionWorld::SetSurfaceWeather(float Wetness, float Snow)
{
	for (UMaterialInstanceDynamic* M : TerrainMids)
	{
		if (!M) continue;
		M->SetScalarParameterValue(TEXT("Wetness"), FMath::Clamp(Wetness, 0.f, 1.f));
		M->SetScalarParameterValue(TEXT("Snow"), FMath::Clamp(Snow, 0.f, 1.f));
	}
}

bool ASmvRegionWorld::Build(const FSmvRegionInfo& Region, const FSmvTrack& Track, double GroundMsl, AActor* GeoActor)
{
	ICaySimGeoTransform* Geo = Cast<ICaySimGeoTransform>(GeoActor);
	if (!Geo || !Dem.Load(Region)) { UE_LOG(LogSimumav, Warning, TEXT("Bölge dünyası: paket / dönüşüm yok (%s)"), *Region.Id); return false; }
	const double T0 = FPlatformTime::Seconds();
	Ground = GroundMsl;
	const double Lat0 = Region.Lat, Lon0 = Region.Lon;
	// paket merkezi ≠ parkur noktası olabilir (daha büyük bir paket parkuru örter): ENU paket merkezine göre, Unreal orijini parkur noktası
	ToUe = [Geo, Lat0, Lon0](double E, double N, double Hmsl)
	{
		double La, Lo;
		SmvRegion::EnuToLatLon(Lat0, Lon0, E, N, La, Lo);
		return Geo->GeoToUnreal(Lo, La, Hmsl);
	};
	Origin = ToUe(0, 0, Region.CenterMsl);
	SetActorLocation(Origin);
	double TrE, TrN;
	SmvRegion::LatLonToEnu(Lat0, Lon0, Track.Lat, Track.Lon, TrE, TrN);

	double StepM = FMath::Clamp(Region.RadiusM * 2.0 / 800.0, 5.0, 14.0);
	if (GConfig) GConfig->GetDouble(TEXT("/Script/Simumav.SmvRegion"), TEXT("TerrainStepM"), StepM, GGameIni);
	BuildTerrain(Region.RadiusM, StepM);
	BuildHorizon(Region.RadiusM * 0.985, 30000.0);
	BuildOsm(Track);
	SMV_KLOG(TEXT("Bölge dünyası %s: arazi adımı %.1f m, %d bina, %d yol, %d su, %d ağaç — %.2f sn (parkur noktası paket merkezine %.0f m)"), *Region.Id, StepM,
		NBuild, NRoad, NWater, NTree, FPlatformTime::Seconds() - T0, FMath::Sqrt(TrE * TrE + TrN * TrN));
	return true;
}

bool ASmvRegionWorld::BuildCollisionOnly(const FSmvRegionInfo& Region, const FSmvTrack& Track, AActor* GeoActor)
{
	ICaySimGeoTransform* Geo = Cast<ICaySimGeoTransform>(GeoActor);
	if (!Geo || !Dem.Load(Region)) return false;
	const double Lat0 = Region.Lat, Lon0 = Region.Lon;
	ToUe = [Geo, Lat0, Lon0](double E, double N, double Hmsl)
	{
		double La, Lo;
		SmvRegion::EnuToLatLon(Lat0, Lon0, E, N, La, Lo);
		return Geo->GeoToUnreal(Lo, La, Hmsl);
	};
	Origin = ToUe(0, 0, Region.CenterMsl);
	CenterMsl = Region.CenterMsl;
	SetActorLocation(Origin);
	BuildOsm(Track, true);
	SetActorTickEnabled(true);
	SMV_KLOG(TEXT("Bölge %s: Google akışı altında görünmez bina çarpışması — %d bina"), *Region.Id, NBuild);
	return NBuild > 0;
}

// ---------------------------------------------------------------------------
// arazi: kare ızgara (paket yarıçapı), 96 hücrelik parçalar (eşzamansız çarpışma pişirme); köşe rengi örtü sınıfından (OSM alanları BuildOsm'da)
// ---------------------------------------------------------------------------
void ASmvRegionWorld::BuildTerrain(double HalfM, double StepM)
{
	const int32 Cells = FMath::Max(8, FMath::CeilToInt(2.0 * HalfM / StepM));
	const int32 Chunk = 96;
	const int32 NCh = FMath::DivideAndRoundUp(Cells, Chunk);
	UMaterialInterface* Tm = TerrainMaterial();
	UMaterialInstanceDynamic* Mid = Tm ? UMaterialInstanceDynamic::Create(Tm, this) : nullptr;
	if (Mid)
	{
		Mid->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 1.f, 1.f));
		Mid->SetScalarParameterValue(TEXT("Sand"), 0.f);
		Mid->SetScalarParameterValue(TEXT("Snow"), 0.f);
		Mid->SetScalarParameterValue(TEXT("Wetness"), 0.f);
		Mid->SetScalarParameterValue(TEXT("RockSlope"), 0.24f);
		// kar çizgisi: bölgenin en yüksek %20'si 1800 m'yi aşıyorsa
		const double SnowLine = Dem.Info().MaxMsl > 1800.0 ? ToUe(0, 0, FMath::Max(1800.0, Dem.Info().MaxMsl - 300.0)).Z : 1.0e8;
		Mid->SetScalarParameterValue(TEXT("SnowLine"), (float)SnowLine);
		Mid->SetScalarParameterValue(TEXT("SnowFade"), 6000.f);
		TerrainMids.Add(Mid);
	}
	const double D = StepM * 0.5;
	auto Pos = [this](double E, double N) { return ToUe(E, N, Dem.HeightEnu(E, N)) - Origin; };
	for (int32 cy = 0; cy < NCh; ++cy)
		for (int32 cx = 0; cx < NCh; ++cx)
		{
			const int32 X0 = cx * Chunk, Y0 = cy * Chunk;
			const int32 Nx = FMath::Min(Chunk, Cells - X0), Ny = FMath::Min(Chunk, Cells - Y0);
			if (Nx <= 0 || Ny <= 0) continue;
			FMeshBuf B;
			for (int32 y = 0; y <= Ny; ++y)
				for (int32 x = 0; x <= Nx; ++x)
				{
					const double E = -HalfM + (X0 + x) * StepM, N = -HalfM + (Y0 + y) * StepM;
					const FVector P = Pos(E, N);
					FVector Nrm = FVector::CrossProduct(Pos(E + D, N) - Pos(E - D, N), Pos(E, N + D) - Pos(E, N - D)).GetSafeNormal();
					if (Nrm.Z < 0) Nrm = -Nrm;
					B.Add(P, Nrm, FVector2D(E / 10.0, N / 10.0), FLinearColor(0.2f, 0.3f, 0.14f));
				}
			for (int32 y = 0; y < Ny; ++y)
				for (int32 x = 0; x < Nx; ++x)
				{
					const int32 A = y * (Nx + 1) + x, Bq = A + 1, C = A + Nx + 1, Dq = C + 1;
					B.Tri(A, Bq, C, FVector::UpVector); B.Tri(Bq, Dq, C, FVector::UpVector);
				}
			UProceduralMeshComponent* P = NewPart(TEXT("RegionTerrain"), true);
			P->CreateMeshSection_LinearColor(0, B.V, B.T, B.N, B.Uv, B.C, B.Tg, true);
			if (Mid) P->SetMaterial(0, Mid);
			P->SetCastShadow(true);
		}
	// deniz: bölgede deniz seviyesinde / altında arazi varsa 0 m'de su düzlemi (Terrarium batimetrisi tabanı -3 m'ye kırpılır)
	bool bSea = false;
	for (int32 i = -16; i <= 16 && !bSea; ++i)
		for (int32 j = -16; j <= 16 && !bSea; ++j) bSea = Dem.RawEnu(i * HalfM / 16.0, j * HalfM / 16.0) <= 0.0;
	if (bSea)
	{
		FMeshBuf S;
		const double R = 32000.0;
		const int32 Gn = 16;
		for (int32 y = 0; y <= Gn; ++y)
			for (int32 x = 0; x <= Gn; ++x)
			{
				const double E = -R + 2 * R * x / Gn, N = -R + 2 * R * y / Gn;
				S.Add(ToUe(E, N, 0.0) - Origin, FVector::UpVector, FVector2D(E / 50.0, N / 50.0), FLinearColor::White);
			}
		for (int32 y = 0; y < Gn; ++y)
			for (int32 x = 0; x < Gn; ++x)
			{
				const int32 A = y * (Gn + 1) + x;
				S.Tri(A, A + 1, A + Gn + 1, FVector::UpVector); S.Tri(A + 1, A + Gn + 2, A + Gn + 1, FVector::UpVector);
			}
		UProceduralMeshComponent* P = NewPart(TEXT("RegionSea"), false);
		P->CreateMeshSection_LinearColor(0, S.V, S.T, S.N, S.Uv, S.C, S.Tg, false);
		if (UMaterialInterface* Src = SmvSolidMat(LoadObject<UMaterialInterface>(nullptr, GBaseMat)))
		{
			UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Src, this);
			SmvSetSolidColor(M, FLinearColor(0.02f, 0.09f, 0.14f));
			M->SetScalarParameterValue(TEXT("Roughness"), 0.05f);
			M->SetScalarParameterValue(TEXT("Metallic"), 0.f);
			M->SetScalarParameterValue(TEXT("Specular"), 0.6f);
			P->SetMaterial(0, M);
		}
		P->SetCastShadow(false);
		++NWater;
	}
}

// ---------------------------------------------------------------------------
// ufuk halkası: paket kenarından 30 km'ye kutupsal ızgara (z10 ≈ 150 m), 4 m alçakta (iç arazi önde kalsın)
// ---------------------------------------------------------------------------
void ASmvRegionWorld::BuildHorizon(double InnerM, double OuterM)
{
	const int32 Rings = 44, Segs = 192;
	FMeshBuf B;
	auto RingR = [InnerM, OuterM, Rings](int32 r) { const double U = (double)r / Rings; return InnerM + (OuterM - InnerM) * U * U; };
	for (int32 r = 0; r <= Rings; ++r)
		for (int32 s = 0; s < Segs; ++s)
		{
			const double A = UE_DOUBLE_TWO_PI * s / Segs, Rm = RingR(r);
			const double E = Rm * FMath::Cos(A), N = Rm * FMath::Sin(A);
			// iç kenar paketle aynı yükseklikte (dikiş), dışa doğru 4 m alçalır
			const double Drop = r == 0 ? 1.0 : 4.0;
			B.Add(ToUe(E, N, Dem.HeightEnu(E, N) - Drop) - Origin, FVector::UpVector, FVector2D(E / 10.0, N / 10.0), FLinearColor(0.2f, 0.3f, 0.14f));
		}
	for (int32 r = 0; r < Rings; ++r)
		for (int32 s = 0; s < Segs; ++s)
		{
			const int32 A = r * Segs + s, Bq = r * Segs + (s + 1) % Segs, C = A + Segs, Dq = Bq + Segs;
			B.Tri(A, Bq, C, FVector::UpVector); B.Tri(Bq, Dq, C, FVector::UpVector);
		}
	// normaller yüzlerden
	for (FVector& Nv : B.N) Nv = FVector::ZeroVector;
	for (int32 i = 0; i < B.T.Num(); i += 3)
	{
		FVector Fn = FVector::CrossProduct(B.V[B.T[i + 1]] - B.V[B.T[i]], B.V[B.T[i + 2]] - B.V[B.T[i]]).GetSafeNormal();
		if (Fn.Z < 0) Fn = -Fn;
		B.N[B.T[i]] += Fn; B.N[B.T[i + 1]] += Fn; B.N[B.T[i + 2]] += Fn;
	}
	for (FVector& Nv : B.N) Nv = Nv.GetSafeNormal();
	UProceduralMeshComponent* P = NewPart(TEXT("RegionHorizon"), true);
	P->CreateMeshSection_LinearColor(0, B.V, B.T, B.N, B.Uv, B.C, B.Tg, true);
	if (TerrainMids.Num()) P->SetMaterial(0, TerrainMids[0]);
	P->SetCastShadow(false);
}

// ---------------------------------------------------------------------------
// OSM: binalar, yollar, su, ağaçlar
// ---------------------------------------------------------------------------
void ASmvRegionWorld::BuildOsm(const FSmvTrack& Track, bool bCollisionOnly)
{
	const FSmvRegionInfo& R = Dem.Info();
	double HomeE, HomeN;
	SmvRegion::LatLonToEnu(R.Lat, R.Lon, Track.Lat, Track.Lon, HomeE, HomeN);
	TArray<FBld> Blds; TArray<FRoad> Roads; TArray<FArea> Areas; TArray<SmvRegion::FXY> Trees;
	{
		TArray<FString> Files;
		const FString Dir = R.Dir() / TEXT("osm");
		IFileManager::Get().FindFiles(Files, *(Dir / TEXT("15_*.json")), true, false);   // z15 karoları (eski z14 denemeleri yok sayılır)
		for (const FString& F : Files)
		{
			FString Txt;
			if (!FFileHelper::LoadFileToString(Txt, *(Dir / F))) continue;
			TSharedPtr<FJsonObject> O = SmvJson::Parse(Txt);
			if (!O.IsValid()) continue;
			const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
			if (O->TryGetArrayField(TEXT("b"), A))
				for (const TSharedPtr<FJsonValue>& V : *A)
				{
					const TArray<TSharedPtr<FJsonValue>>& X = V->AsArray();
					if (X.Num() < 8) continue;
					FBld B; B.H = (float)X[0]->AsNumber(); ReadPts(X, 2, B.P);
					Blds.Add(MoveTemp(B));
				}
			if (O->TryGetArrayField(TEXT("r"), A))
				for (const TSharedPtr<FJsonValue>& V : *A)
				{
					const TArray<TSharedPtr<FJsonValue>>& X = V->AsArray();
					if (X.Num() < 6) continue;
					FRoad Rd; Rd.W = (float)X[0]->AsNumber(); Rd.Sub = (uint8)X[1]->AsNumber(); ReadPts(X, 2, Rd.P);
					Roads.Add(MoveTemp(Rd));
				}
			if (O->TryGetArrayField(TEXT("a"), A))
				for (const TSharedPtr<FJsonValue>& V : *A)
				{
					const TArray<TSharedPtr<FJsonValue>>& X = V->AsArray();
					if (X.Num() < 7) continue;
					FArea Ar; Ar.Kind = (uint8)X[0]->AsNumber(); ReadPts(X, 1, Ar.P);
					Ar.MinE = Ar.MinN = 1e18; Ar.MaxE = Ar.MaxN = -1e18;
					for (const SmvRegion::FXY& Q : Ar.P) { Ar.MinE = FMath::Min(Ar.MinE, Q.X); Ar.MaxE = FMath::Max(Ar.MaxE, Q.X); Ar.MinN = FMath::Min(Ar.MinN, Q.Y); Ar.MaxN = FMath::Max(Ar.MaxN, Q.Y); }
					Areas.Add(MoveTemp(Ar));
				}
			if (O->TryGetArrayField(TEXT("t"), A))
				for (int32 i = 0; i + 1 < A->Num(); i += 2) Trees.Add({ (*A)[i]->AsNumber(), (*A)[i + 1]->AsNumber() });
		}
	}
	const double Lim = R.RadiusM;
	auto Inside = [Lim](double E, double N) { return E * E + N * N <= Lim * Lim; };

	// ---- binalar: hücre (400 m) başına bir bileşen; bölüm 0 duvar, 1 çatı ----
	UMaterialInterface* Facade = LoadObject<UMaterialInterface>(nullptr, GFacadeMat, nullptr, LOAD_NoWarn | LOAD_Quiet);
	UMaterialInterface* Solid = SmvSolidMat(LoadObject<UMaterialInterface>(nullptr, GBaseMat));
	UMaterialInstanceDynamic* WallMid = nullptr; UMaterialInstanceDynamic* RoofMid = nullptr;
	if (Facade)
	{
		WallMid = UMaterialInstanceDynamic::Create(Facade, this);
		WallMid->SetScalarParameterValue(TEXT("Windows"), 1.f);
		// gece / alacakaranlık: hücre başına rastgele yanan pencereler
		WallMid->SetScalarParameterValue(TEXT("Night"), Track.TimeOfDay == TEXT("night") ? 1.f : Track.TimeOfDay == TEXT("dusk") ? 0.45f : 0.f);
		RoofMid = UMaterialInstanceDynamic::Create(Facade, this);
		RoofMid->SetScalarParameterValue(TEXT("Windows"), 0.f);
	}
	else if (Solid)
	{
		WallMid = UMaterialInstanceDynamic::Create(Solid, this); SmvSetSolidColor(WallMid, FLinearColor(0.62f, 0.6f, 0.56f));
		WallMid->SetScalarParameterValue(TEXT("Roughness"), 0.85f);
		RoofMid = UMaterialInstanceDynamic::Create(Solid, this); SmvSetSolidColor(RoofMid, FLinearColor(0.3f, 0.29f, 0.28f));
		RoofMid->SetScalarParameterValue(TEXT("Roughness"), 0.9f);
	}
	static const uint32 Plaster[] = { 0xe9e2d4, 0xd8cdb8, 0xc9c2b6, 0xe6d6b8, 0xbfb4a3, 0xd9d2c8, 0xcfc0a8, 0xb9b3ab, 0xe3dccf, 0xc8b79c, 0xd4c4a6, 0xa9a49c };
	static const uint32 Roofs[] = { 0x8a4b33, 0x7a4030, 0x94573b, 0x6e6a66, 0x5c5955, 0x807a72 };
	TMap<FIntPoint, TPair<FMeshBuf, FMeshBuf>> Cells;
	FRandomStream Rng(Track.World.Seed ? Track.World.Seed : 7);
	for (int32 bi = 0; bi < Blds.Num(); ++bi)
	{
		FBld& B = Blds[bi];
		if (B.P.size() < 3) continue;
		// kalkış noktası (ev) bina içinde / dibinde kalmasın
		bool bNearHome = SmvRegion::PointInPolygon(B.P, HomeE, HomeN);
		double Ce = 0, Cn = 0;
		for (const SmvRegion::FXY& Q : B.P) { Ce += Q.X; Cn += Q.Y; if (FMath::Square(Q.X - HomeE) + FMath::Square(Q.Y - HomeN) < 64.0) bNearHome = true; }
		Ce /= B.P.size(); Cn /= B.P.size();
		if (bNearHome || !Inside(Ce, Cn)) continue;
		double Gmin = 1e9, Gsum = 0;
		TArray<double> Gh;
		for (const SmvRegion::FXY& Q : B.P) { const double H = Dem.HeightEnu(Q.X, Q.Y); Gh.Add(H); Gmin = FMath::Min(Gmin, H); Gsum += H; }
		const double Base = Gmin - 0.8, Top = Gsum / B.P.size() + B.H;
		const uint32 Hs = GetTypeHash(FIntPoint((int32)(Ce * 10), (int32)(Cn * 10)));
		const double Area = FMath::Abs(SmvRegion::SignedArea(B.P));
		const bool bTiled = Area < 160.0 && B.H < 10.0;
		const FLinearColor Wc = RgbHex(Plaster[Hs % UE_ARRAY_COUNT(Plaster)]) * FMath::Lerp(0.9f, 1.05f, (float)((Hs >> 8) % 100) / 100.f);
		const FLinearColor Rc = bTiled ? RgbHex(Roofs[(Hs >> 4) % 3]) : RgbHex(Roofs[3 + (Hs >> 4) % 3]);
		TPair<FMeshBuf, FMeshBuf>& Cell = Cells.FindOrAdd(FIntPoint(FMath::FloorToInt(Ce / 400.0), FMath::FloorToInt(Cn / 400.0)));
		FMeshBuf& W = Cell.Key; FMeshBuf& Rf = Cell.Value;
		const FVector Cen = ToUe(Ce, Cn, Base) - Origin;
		double U = 0;
		for (size_t i = 0; i < B.P.size(); ++i)
		{
			const SmvRegion::FXY& A = B.P[i]; const SmvRegion::FXY& C = B.P[(i + 1) % B.P.size()];
			const double L = FMath::Sqrt(FMath::Square(C.X - A.X) + FMath::Square(C.Y - A.Y));
			const FVector A0 = ToUe(A.X, A.Y, Base) - Origin, C0 = ToUe(C.X, C.Y, Base) - Origin;
			const FVector A1 = ToUe(A.X, A.Y, Top) - Origin, C1 = ToUe(C.X, C.Y, Top) - Origin;
			FVector Out = FVector::CrossProduct(C0 - A0, FVector::UpVector).GetSafeNormal();
			const FVector Mid = (A0 + C0) * 0.5;
			if (FVector::DotProduct(Out, Mid - Cen) < 0) Out = -Out;
			const double Hm = Top - Base;
			// kenarlar arası yumuşatma yok: her kenar kendi köşeleri (keskin köşe normali)
			const int32 i0 = W.Add(A0, Out, FVector2D(U, 0), Wc), i1 = W.Add(C0, Out, FVector2D(U + L, 0), Wc);
			const int32 i2 = W.Add(C1, Out, FVector2D(U + L, Hm), Wc), i3 = W.Add(A1, Out, FVector2D(U, Hm), Wc);
			W.Tri(i0, i1, i2, Out); W.Tri(i0, i2, i3, Out);
			U += L;
		}
		std::vector<int> Tri;
		SmvRegion::Triangulate(B.P, Tri);
		const int32 R0 = Rf.V.Num();
		for (const SmvRegion::FXY& Q : B.P) Rf.Add(ToUe(Q.X, Q.Y, Top) - Origin, FVector::UpVector, FVector2D(Q.X, Q.Y), Rc);
		for (size_t i = 0; i + 2 < Tri.size(); i += 3) Rf.Tri(R0 + Tri[i], R0 + Tri[i + 1], R0 + Tri[i + 2], FVector::UpVector);
		++NBuild;
	}
	for (TPair<FIntPoint, TPair<FMeshBuf, FMeshBuf>>& It : Cells)
	{
		UProceduralMeshComponent* P = NewPart(TEXT("RegionBuildings"), true);
		FMeshBuf& W = It.Value.Key; FMeshBuf& Rf = It.Value.Value;
		P->CreateMeshSection_LinearColor(0, W.V, W.T, W.N, W.Uv, W.C, W.Tg, true);
		P->CreateMeshSection_LinearColor(1, Rf.V, Rf.T, Rf.N, Rf.Uv, Rf.C, Rf.Tg, true);
		if (WallMid) P->SetMaterial(0, WallMid);
		if (RoofMid) P->SetMaterial(1, RoofMid);
		if (bCollisionOnly) { P->SetVisibility(false); P->SetHiddenInGame(true); P->SetCastShadow(false); }
	}
	if (bCollisionOnly) return;   // yollar / su / ağaçlar Google karolarında var

	// ---- yollar: araziye serilen şeritler (çarpışmasız); bölüm 0 asfalt, 1 kaldırım / yaya, 2 toprak ----
	{
		FMeshBuf Rb[3];
		for (const FRoad& Rd : Roads)
		{
			const std::vector<SmvRegion::FXY> P = SmvRegion::Resample(Rd.P, 8.0);
			if (P.size() < 2) continue;
			FMeshBuf& M = Rb[FMath::Clamp((int32)Rd.Sub, 0, 2)];
			const double Hw = Rd.W * 0.5, Lift = 0.22 + (Rd.W >= 9.0 ? 0.06 : Rd.Sub == 1 ? 0.0 : 0.03);
			double Along = 0;
			int32 Prev = -1;
			bool bAny = false;
			for (size_t i = 0; i < P.size(); ++i)
			{
				const SmvRegion::FXY& A = P[i == 0 ? 0 : i - 1]; const SmvRegion::FXY& C = P[i + 1 < P.size() ? i + 1 : i];
				double Dx = C.X - A.X, Dy = C.Y - A.Y;
				const double L = FMath::Sqrt(Dx * Dx + Dy * Dy);
				if (L < 1e-6) continue;
				Dx /= L; Dy /= L;
				if (i > 0) Along += FMath::Sqrt(FMath::Square(P[i].X - P[i - 1].X) + FMath::Square(P[i].Y - P[i - 1].Y));
				if (!Inside(P[i].X, P[i].Y)) { Prev = -1; continue; }
				const double Le = P[i].X - Dy * Hw, Ln = P[i].Y + Dx * Hw, Re = P[i].X + Dy * Hw, Rn = P[i].Y - Dx * Hw;
				const int32 iL = M.Add(ToUe(Le, Ln, Dem.HeightEnu(Le, Ln) + Lift) - Origin, FVector::UpVector, FVector2D(0, Along / FMath::Max(2.0, Rd.W)), FLinearColor::White);
				const int32 iR = M.Add(ToUe(Re, Rn, Dem.HeightEnu(Re, Rn) + Lift) - Origin, FVector::UpVector, FVector2D(1, Along / FMath::Max(2.0, Rd.W)), FLinearColor::White);
				if (Prev >= 0) { M.Tri(Prev, Prev + 1, iL, FVector::UpVector); M.Tri(Prev + 1, iR, iL, FVector::UpVector); bAny = true; }
				Prev = iL;
			}
			if (bAny) ++NRoad;
		}
		UMaterialInterface* St = FSmvScenery::StreetMaterial();
		UProceduralMeshComponent* P = NewPart(TEXT("RegionRoads"), false);
		static const FLinearColor Cols[3] = { FLinearColor(0.05f, 0.05f, 0.055f), FLinearColor(0.32f, 0.3f, 0.28f), FLinearColor(0.27f, 0.2f, 0.13f) };
		for (int32 k = 0; k < 3; ++k)
		{
			if (Rb[k].IsEmpty()) continue;
			P->CreateMeshSection_LinearColor(k, Rb[k].V, Rb[k].T, Rb[k].N, Rb[k].Uv, Rb[k].C, Rb[k].Tg, false);
			UMaterialInterface* Src = St ? St : Solid;
			if (!Src) continue;
			UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Src, this);
			M->SetVectorParameterValue(TEXT("Color"), Cols[k]);
			if (!St) SmvSetSolidColor(M, Cols[k]);
			M->SetScalarParameterValue(TEXT("Rough"), k == 0 ? 0.8f : 0.92f);
			M->SetScalarParameterValue(TEXT("TileCm"), k == 1 ? 60.f : 0.f);
			P->SetMaterial(k, M);
		}
		P->SetCastShadow(false);
	}

	// ---- su yüzeyleri (göl, nehir, havuz) ----
	{
		FMeshBuf Wb;
		for (const FArea& Ar : Areas)
		{
			if (Ar.Kind != 1 || Ar.P.size() < 3) continue;
			double Hmin = 1e9;
			for (const SmvRegion::FXY& Q : Ar.P) Hmin = FMath::Min(Hmin, Dem.HeightEnu(Q.X, Q.Y));
			std::vector<int> Tri;
			SmvRegion::Triangulate(Ar.P, Tri);
			const int32 W0 = Wb.V.Num();
			for (const SmvRegion::FXY& Q : Ar.P) Wb.Add(ToUe(Q.X, Q.Y, Hmin + 0.15) - Origin, FVector::UpVector, FVector2D(Q.X / 20.0, Q.Y / 20.0), FLinearColor::White);
			for (size_t i = 0; i + 2 < Tri.size(); i += 3) Wb.Tri(W0 + Tri[i], W0 + Tri[i + 1], W0 + Tri[i + 2], FVector::UpVector);
			++NWater;
		}
		if (!Wb.IsEmpty())
		{
			UProceduralMeshComponent* P = NewPart(TEXT("RegionWater"), false);
			P->CreateMeshSection_LinearColor(0, Wb.V, Wb.T, Wb.N, Wb.Uv, Wb.C, Wb.Tg, false);
			if (Solid)
			{
				UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Solid, this);
				SmvSetSolidColor(M, FLinearColor(0.03f, 0.1f, 0.13f));
				M->SetScalarParameterValue(TEXT("Roughness"), 0.05f);
				M->SetScalarParameterValue(TEXT("Specular"), 0.6f);
				P->SetMaterial(0, M);
			}
			P->SetCastShadow(false);
		}
	}

	// ---- ağaçlar: orman / çalılık / park alanları + tek ağaç düğümleri (Brushify ağları; yoksa atlanır) ----
	const TArray<UStaticMesh*>& TreeM = FSmvScenery::Meshes(ESmvSceneryKind::Tree);
	const TArray<UStaticMesh*>& BushM = FSmvScenery::Meshes(ESmvSceneryKind::Bush);
	if (TreeM.Num() && GetWorld())
	{
		FActorSpawnParameters Sp;
		Sp.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Sp.Owner = this;
		Scenery = GetWorld()->SpawnActor<ASmvScenery>(ASmvScenery::StaticClass(), FTransform(Origin), Sp);
	}
	if (Scenery)
	{
		int32 Budget = 45000;
		if (GConfig) GConfig->GetInt(TEXT("/Script/Simumav.SmvRegion"), TEXT("TreeBudget"), Budget, GGameIni);
		// alan yoğunlukları (m² başına ağaç) ve bütçeye göre ölçek
		auto Dens = [](uint8 K) { return K == 2 ? 1.0 / 90.0 : K == 4 ? 1.0 / 160.0 : K == 3 ? 1.0 / 450.0 : K == 7 ? 1.0 / 260.0 : K == 5 ? 1.0 / 4000.0 : 0.0; };
		double Want = Trees.Num();
		for (const FArea& Ar : Areas) Want += FMath::Abs(SmvRegion::SignedArea(Ar.P)) * Dens(Ar.Kind);
		const double Scale = Want > Budget ? Budget / Want : 1.0;
		TMap<UStaticMesh*, TArray<FTransform>> Xf;
		auto Place = [&](double E, double N, bool bBush)
		{
			if (!Inside(E, N) || FMath::Square(E - HomeE) + FMath::Square(N - HomeN) < 225.0) return;
			const TArray<UStaticMesh*>& Ms = bBush && BushM.Num() ? BushM : TreeM;
			UStaticMesh* M = Ms[Rng.RandHelper(Ms.Num())];
			const FVector P = ToUe(E, N, Dem.HeightEnu(E, N) - 0.25);
			const float S = Rng.FRandRange(0.75f, 1.25f);
			Xf.FindOrAdd(M).Add(FTransform(FRotator(0, Rng.FRandRange(0.f, 360.f), 0), P, FVector(S)));
			++NTree;
		};
		for (const SmvRegion::FXY& T : Trees) Place(T.X, T.Y, false);
		for (const FArea& Ar : Areas)
		{
			const double Dn = Dens(Ar.Kind) * Scale;
			if (Dn <= 0) continue;
			const int32 N = FMath::Min(20000, (int32)(FMath::Abs(SmvRegion::SignedArea(Ar.P)) * Dn + Rng.FRand()));
			for (int32 i = 0, Tries = 0; i < N && Tries < N * 4; ++Tries)
			{
				const double E = Ar.MinE + (Ar.MaxE - Ar.MinE) * Rng.FRand(), Nn = Ar.MinN + (Ar.MaxN - Ar.MinN) * Rng.FRand();
				if (!SmvRegion::PointInPolygon(Ar.P, E, Nn)) continue;
				Place(E, Nn, Ar.Kind == 4 || (Ar.Kind == 2 && Rng.FRand() < 0.18f));
				++i;
			}
		}
		for (TPair<UStaticMesh*, TArray<FTransform>>& It : Xf) Scenery->AddMeshInstances(It.Key, It.Value, true, 2500.0);
	}
}
