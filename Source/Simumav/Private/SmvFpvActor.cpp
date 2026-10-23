#include "SmvFpvActor.h"
#include "SimumavModule.h"
#include "SmvWorldActors.h"
#include "SmvMotorSynth.h"
#include "CaySimTypes.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "Misc/DateTime.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const TCHAR* GFpvGlowMat = TEXT("/Game/Simumav/Materials/M_SmvGlow.M_SmvGlow");
	const FLinearColor GFpvNext(0.15f, 1.f, 0.35f), GFpvSoon(1.f, 0.3f, 0.8f), GFpvWait(1.f, 0.5f, 0.08f), GFpvDone(0.25f, 0.27f, 0.3f);
}

ASmvFpvCourseActor::ASmvFpvCourseActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cyl(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sph(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Mat(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	CubeMesh = Cube.Object; CylMesh = Cyl.Object; SphMesh = Sph.Object; BaseMat = Mat.Object;
}

void ASmvFpvCourseActor::EndPlay(const EEndPlayReason::Type Reason)
{
	for (TObjectPtr<ASmvGateActor>& R : Rings) if (R) R->Destroy();
	Rings.Reset();
	if (Ghost) { Ghost->Destroy(); Ghost = nullptr; }
	Super::EndPlay(Reason);
}

FVector ASmvFpvCourseActor::P(double X, double Y, double Z) const
{
	const double E = X - Course.StartX, N = Y - Course.StartY;
	return Origin + Ew * E + Nw * N + Uw * Z;
}

FVector ASmvFpvCourseActor::Dir(const FVector& Enu) const
{
	return (Ew * Enu.X + Nw * Enu.Y + Uw * Enu.Z).GetSafeNormal();
}

UInstancedStaticMeshComponent* ASmvFpvCourseActor::IsmFor(int32 Shape, const FString& Mat)
{
	const FString Key = FString::Printf(TEXT("%d|%s"), Shape, *Mat);
	if (UInstancedStaticMeshComponent** F = IsmBy.Find(Key)) return *F;
	UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this);
	C->SetupAttachment(Root);
	C->SetStaticMesh(Shape == 0 ? CubeMesh.Get() : Shape == 1 ? CylMesh.Get() : SphMesh.Get());
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetMobility(EComponentMobility::Movable);
	if (BaseMat)
	{
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(BaseMat, this);
		M->SetVectorParameterValue(TEXT("Color"), SmvFpv::MatColor(Mat));
		C->SetMaterial(0, M);
	}
	C->RegisterComponent();
	Ism.Add(C);
	IsmBy.Add(Key, C);
	return C;
}

void ASmvFpvCourseActor::AddBox(const FVector& C, const FVector& R, const FVector& F, double Wd, double D, double H, const FString& Mat)
{
	if (Wd <= 0.01 || D <= 0.01 || H <= 0.01) return;
	const FRotator Rot = FRotationMatrix::MakeFromXY(Dir(R), Dir(F)).Rotator();
	const double S = Ew.Size() / 100.0;
	IsmFor(0, Mat)->AddInstance(FTransform(Rot, P(C.X, C.Y, C.Z), FVector(Wd, D, H) * S), true);
}

void ASmvFpvCourseActor::AddPrim(const FSmvFpvObst& E)
{
	if (!E.bVis) return;
	const double S = Ew.Size() / 100.0;
	if (E.T == TEXT("box"))
	{
		const FVector R = SmvFpv::Right(E.Yaw), F = SmvFpv::Forward(E.Yaw);
		const FVector Base(E.X, E.Y, E.Z);
		auto Sub = [&](double U0, double U1, double A0, double A1)
		{
			// U: sağ eksen aralığı; A: ikinci eksen (axis y → z, axis z → ileri)
			if (U1 - U0 <= 0.01 || A1 - A0 <= 0.01) return;
			const double Um = (U0 + U1) * 0.5, Am = (A0 + A1) * 0.5;
			if (E.Axis == TEXT("z")) AddBox(Base + R * Um + F * Am + FVector(0, 0, E.H * 0.5), R, F, U1 - U0, A1 - A0, E.H, E.Mat);
			else AddBox(Base + R * Um + FVector(0, 0, Am), R, F, U1 - U0, E.D, A1 - A0, E.Mat);
		};
		const double Amax = E.Axis == TEXT("z") ? E.D : E.H;
		const double Amin = E.Axis == TEXT("z") ? -E.D * 0.5 : 0.0;
		const double AmaxAbs = E.Axis == TEXT("z") ? E.D * 0.5 : Amax;
		if (E.Holes.Num() == 0) { Sub(-E.W * 0.5, E.W * 0.5, Amin, AmaxAbs); return; }
		// delikler U boyunca sıralanır: delik sütunlarında altı/üstü, aralarda tam yükseklik
		TArray<FSmvFpvHole> Hs = E.Holes;
		Hs.Sort([](const FSmvFpvHole& A, const FSmvFpvHole& B) { return A.U < B.U; });
		double U = -E.W * 0.5;
		for (const FSmvFpvHole& H : Hs)
		{
			const double H0 = FMath::Max(H.U - H.W * 0.5, -E.W * 0.5), H1 = FMath::Min(H.U + H.W * 0.5, E.W * 0.5);
			if (H0 > U) Sub(U, H0, Amin, AmaxAbs);
			Sub(FMath::Max(U, H0), H1, Amin, FMath::Max(Amin, H.V - H.H * 0.5));
			Sub(FMath::Max(U, H0), H1, FMath::Min(AmaxAbs, H.V + H.H * 0.5), AmaxAbs);
			U = FMath::Max(U, H1);
		}
		if (U < E.W * 0.5) Sub(U, E.W * 0.5, Amin, AmaxAbs);
		return;
	}
	if (E.T == TEXT("cyl"))
	{
		IsmFor(1, E.Mat)->AddInstance(FTransform(FRotator::ZeroRotator, P(E.X, E.Y, E.Z + E.H * 0.5), FVector(2 * E.R, 2 * E.R, E.H) * S), true);
		return;
	}
	if (E.T == TEXT("sph"))
	{
		IsmFor(2, E.Mat)->AddInstance(FTransform(FRotator::ZeroRotator, P(E.X, E.Y, E.Z), FVector(2 * E.R) * S), true);
		return;
	}
	if (E.T == TEXT("pipe"))
	{
		const FVector A = P(E.A.X, E.A.Y, E.A.Z), B = P(E.B.X, E.B.Y, E.B.Z);
		const double L = FVector::Dist(E.A, E.B);
		IsmFor(1, E.Mat)->AddInstance(FTransform(FRotationMatrix::MakeFromZ((B - A).GetSafeNormal()).Rotator(), (A + B) * 0.5, FVector(2 * E.R, 2 * E.R, L) * S), true);
		return;
	}
	if (E.T == TEXT("torus"))
	{
		FActorSpawnParameters Sp;
		Sp.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FVector N = E.bFlat ? FVector(0, 0, 1) : SmvFpv::Forward(E.Yaw);
		ASmvGateActor* G = GetWorld()->SpawnActor<ASmvGateActor>(ASmvGateActor::StaticClass(), FTransform(FRotationMatrix::MakeFromX(Dir(N)).Rotator(), P(E.X, E.Y, E.Z)), Sp);
		if (G) { G->Build((float)E.RR, (float)E.Tube, SmvFpv::MatColor(E.Mat)); Rings.Add(G); }
	}
}

UStaticMeshComponent* ASmvFpvCourseActor::AddGateBar(const FVector& C, const FVector& R, const FVector& F, double Wd, double D, double H, UMaterialInstanceDynamic* M)
{
	UStaticMeshComponent* Sc = NewObject<UStaticMeshComponent>(this);
	Sc->SetupAttachment(Root);
	Sc->SetStaticMesh(CubeMesh);
	Sc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Sc->SetMobility(EComponentMobility::Movable);
	if (M) Sc->SetMaterial(0, M);
	Sc->RegisterComponent();
	const double S = Ew.Size() / 100.0;
	Sc->SetWorldTransform(FTransform(FRotationMatrix::MakeFromXY(Dir(R), Dir(F)).Rotator(), P(C.X, C.Y, C.Z), FVector(Wd, D, H) * S));
	GateParts.Add(Sc);
	return Sc;
}

void ASmvFpvCourseActor::BuildGate(int32 Index, const FSmvFpvGate& G)
{
	GateRingIdx.Add(-1);
	GateMatIdx.Add(-1);
	if (G.Kind == TEXT("circle"))
	{
		FActorSpawnParameters Sp;
		Sp.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const double Rr = G.R > 0 ? G.R : G.W * 0.5;
		ASmvGateActor* A = GetWorld()->SpawnActor<ASmvGateActor>(ASmvGateActor::StaticClass(),
			FTransform(FRotationMatrix::MakeFromX(Dir(SmvFpv::Forward(G.Yaw))).Rotator(), P(G.X, G.Y, G.Z)), Sp);
		if (A) { A->Build((float)(Rr + 0.15), 0.15f, GFpvWait); GateRingIdx.Last() = Rings.Add(A); }
		return;
	}
	UMaterialInterface* Gm = LoadObject<UMaterialInterface>(nullptr, GFpvGlowMat);
	if (!Gm) Gm = BaseMat;
	UMaterialInstanceDynamic* M = Gm ? UMaterialInstanceDynamic::Create(Gm, this) : nullptr;
	if (M) { M->SetVectorParameterValue(TEXT("Color"), GFpvWait); M->SetScalarParameterValue(TEXT("Glow"), 2.f); }
	GateMatIdx.Last() = GateMats.Add(M);
	const bool bThin = !G.bFrame || G.Kind == TEXT("window") || G.Kind == TEXT("tunnel") || G.Kind == TEXT("flag") || G.Kind == TEXT("slalom");
	const double B = bThin ? 0.06 : 0.25, T = bThin ? 0.06 : 0.22;
	const FVector C(G.X, G.Y, G.Z);
	if (G.Kind == TEXT("dive"))
	{
		const FVector R = SmvFpv::Right(G.Yaw), F = SmvFpv::Forward(G.Yaw);
		AddGateBar(C + F * (G.H * 0.5 + B * 0.5), R, F, G.W + 2 * B, B, T, M);
		AddGateBar(C - F * (G.H * 0.5 + B * 0.5), R, F, G.W + 2 * B, B, T, M);
		AddGateBar(C + R * (G.W * 0.5 + B * 0.5), R, F, B, G.H, T, M);
		AddGateBar(C - R * (G.W * 0.5 + B * 0.5), R, F, B, G.H, T, M);
		return;
	}
	const FVector R = SmvFpv::Right(G.Yaw), F = SmvFpv::Forward(G.Yaw), Up(0, 0, 1);
	AddGateBar(C + Up * (G.H * 0.5 + B * 0.5), R, F, G.W + 2 * B, T, B, M);
	AddGateBar(C - Up * (G.H * 0.5 + B * 0.5), R, F, G.W + 2 * B, T, B, M);
	AddGateBar(C + R * (G.W * 0.5 + B * 0.5), R, F, B, T, G.H, M);
	AddGateBar(C - R * (G.W * 0.5 + B * 0.5), R, F, B, T, G.H, M);
	if (G.Kind == TEXT("ladder"))
	{
		// merdiven kapı: basamak çizgileri (görsel)
		for (int32 k = 1; k < 4; ++k) AddGateBar(C + Up * (-G.H * 0.5 + G.H * k / 4.0), R, F, G.W, 0.04, 0.04, M);
	}
	if (G.bPole)
	{
		// bayrak bezi direğin tepesinde (geçiş yanına doğru)
		const double Side = G.Side == TEXT("right") ? 1.0 : -1.0;
		AddGateBar(FVector(G.PoleX, G.PoleY, (G.Kind == TEXT("flag") ? 5.5 : 3.2) - 0.6) + R * Side * 0.45, R, F, 0.9, 0.04, 1.1, M);
	}
}

void ASmvFpvCourseActor::Init(const FSmvFpvCourse& InCourse, const FSmvFpvCfg& InCfg, TFunction<FVector(double, double, double)> ToWorld, const FString& VehicleType)
{
	Course = InCourse;
	Cfg = InCfg;
	W = MoveTemp(ToWorld);
	Origin = W(0, 0, 0);
	Ew = W(1, 0, 0) - Origin;
	Nw = W(0, 1, 0) - Origin;
	Uw = W(0, 0, 1) - Origin;
	SetActorLocation(Origin);
	for (const FSmvFpvObst& E : Course.Obstacles) AddPrim(E);
	for (const FSmvFpvObst& E : Course.Decor) AddPrim(E);
	// başlangıç pedi
	AddBox(FVector(Course.StartX, Course.StartY, 0.01), SmvFpv::Right(Course.StartYaw), SmvFpv::Forward(Course.StartYaw), 2.4, 2.4, 0.04, TEXT("pole"));
	for (int32 i = 0; i < Course.Gates.Num(); ++i) BuildGate(i, Course.Gates[i]);
	Record = SmvFpv::LoadRecord(Course.Id, Cfg.Laps);
	if (Cfg.bGhost && Record.Ghost.Num() > 10)
	{
		FActorSpawnParameters Sp;
		Sp.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Ghost = GetWorld()->SpawnActor<ASmvDroneActor>(ASmvDroneActor::StaticClass(), FTransform(P(Course.StartX, Course.StartY, 0.3)), Sp);
		if (Ghost)
		{
			FCaySimSession S;
			S.Type = ECaySimDroneType::Multirotor;
			S.Model.Orient.Forward = TEXT("+x"); S.Model.Orient.Up = TEXT("+y"); S.Model.Orient.Scale = 1.f;
			Ghost->Setup(S, nullptr);
			Ghost->ConfigureVisual(VehicleType, FString(), FLinearColor(0.4f, 0.85f, 1.f));
			Ghost->SetActorTickEnabled(false);
			if (USmvMotorSynth* Mo = Ghost->GetMotor()) Mo->Stop();
			Ghost->SetActorHiddenInGame(true);
		}
	}
	RefreshGates();
	UE_LOG(LogSimumav, Log, TEXT("FPV parkuru kuruldu: %s (%d kapı, %d engel, %d dekor, rekor %.2f)"), *Course.Id, Course.Gates.Num(), Course.Obstacles.Num(), Course.Decor.Num(), Record.TotalS);
}

void ASmvFpvCourseActor::StartRace(double T0)
{
	Tracker = MakeUnique<FSmvFpvTracker>(Course, Cfg, T0);
	RaceT0 = T0;
	GhostIdx = 0;
	LastStateHash = -1;
	if (Ghost) Ghost->SetActorHiddenInGame(false);
	RefreshGates();
}

TArray<FSmvFpvEvent> ASmvFpvCourseActor::Feed(double E, double N, double Up, float YawDeg, double T)
{
	if (!Tracker.IsValid()) return TArray<FSmvFpvEvent>();
	TArray<FSmvFpvEvent> Ev = Tracker->Update(FVector(E + Course.StartX, N + Course.StartY, Up), T, YawDeg);
	RefreshGates();
	return Ev;
}

void ASmvFpvCourseActor::RefreshGates()
{
	int32 Hash = 17;
	for (int32 i = 0; i < Course.Gates.Num(); ++i) Hash = Hash * 31 + (Tracker.IsValid() ? Tracker->GateState(i) : (i == 0 ? 1 : 0));
	for (int32 i = 0; i < Course.Gates.Num(); ++i)
	{
		const int32 St = Tracker.IsValid() ? Tracker->GateState(i) : (i == 0 ? 1 : 0);
		const FLinearColor Col = St == 1 ? GFpvNext : St == 2 ? GFpvSoon : St == 3 ? GFpvDone : GFpvWait;
		const float Glow = St == 1 ? 6.f + 3.f * FMath::Sin((float)FPlatformTime::Seconds() * 6.f) : St == 2 ? 3.f : St == 3 ? 0.4f : 1.5f;
		if (GateMatIdx.IsValidIndex(i) && GateMats.IsValidIndex(GateMatIdx[i]) && GateMats[GateMatIdx[i]])
		{
			GateMats[GateMatIdx[i]]->SetVectorParameterValue(TEXT("Color"), Col);
			GateMats[GateMatIdx[i]]->SetScalarParameterValue(TEXT("Glow"), Glow);
		}
		if (GateRingIdx.IsValidIndex(i) && Rings.IsValidIndex(GateRingIdx[i]) && Rings[GateRingIdx[i]] && Hash != LastStateHash)
			Rings[GateRingIdx[i]]->SetState(St == 1 ? 1 : St == 3 ? 2 : 0);
	}
	LastStateHash = Hash;
}

void ASmvFpvCourseActor::UpdateGhost(double T)
{
	if (!Ghost || !Tracker.IsValid() || Record.Ghost.Num() < 2) return;
	const TArray<FSmvFpvGhostSample>& G = Record.Ghost;
	const float Tr = (float)(T - RaceT0);
	while (GhostIdx + 1 < G.Num() && G[GhostIdx + 1].T <= Tr) ++GhostIdx;
	if (GhostIdx + 1 >= G.Num()) { Ghost->SetActorHiddenInGame(true); return; }
	const FSmvFpvGhostSample& A = G[GhostIdx];
	const FSmvFpvGhostSample& B = G[GhostIdx + 1];
	const float K = FMath::Clamp((Tr - A.T) / FMath::Max(0.001f, B.T - A.T), 0.f, 1.f);
	const FVector Pc = FMath::Lerp(FVector(A.X, A.Y, A.Z), FVector(B.X, B.Y, B.Z), (double)K);
	const float Yaw = A.Yaw + FMath::FindDeltaAngleDegrees(A.Yaw, B.Yaw) * K;
	Ghost->SetActorLocationAndRotation(P(Pc.X, Pc.Y, Pc.Z), FRotationMatrix::MakeFromXZ(Dir(SmvFpv::Forward(Yaw)), Uw.GetSafeNormal()).Rotator());
}

double ASmvFpvCourseActor::GhostDelta() const
{
	if (!Tracker.IsValid() || Record.Ghost.Num() < 2) return TNumericLimits<double>::Max();
	// rekor koşusu bu kontrol noktasına ne zaman geldi: hayalet örneklerinde kapı merkezine en yakın an (bu tura kadar ilerleyerek)
	const TArray<double>& Sp = Tracker->Splits();
	if (Sp.Num() == 0) return TNumericLimits<double>::Max();
	const int32 Idx = Sp.Num() - 1;
	const FSmvFpvCheckpoint* Cp = nullptr;
	TArray<FSmvFpvCheckpoint> Pts = SmvFpv::Checkpoints(Course, Cfg.Laps);
	if (Pts.IsValidIndex(Idx)) Cp = &Pts[Idx];
	if (!Cp) return TNumericLimits<double>::Max();
	const FSmvFpvGate& G = Course.Gates[Cp->Gate];
	const FVector Gc(G.X, G.Y, G.Z);
	// en yakın örnek, rekorun toplam süresi oranında ileri kısımda aranır (tur tekrarları ayrışsın)
	const double Frac = (double)Idx / FMath::Max(1, Pts.Num() - 1);
	const int32 N = Record.Ghost.Num();
	const int32 Lo = FMath::Clamp((int32)((Frac - 0.25) * N), 0, N - 1), Hi = FMath::Clamp((int32)((Frac + 0.25) * N), 0, N - 1);
	double Best = 1e18; float BestT = -1;
	for (int32 i = Lo; i <= Hi; ++i)
	{
		const double D = FVector::DistSquared(FVector(Record.Ghost[i].X, Record.Ghost[i].Y, Record.Ghost[i].Z), Gc);
		if (D < Best) { Best = D; BestT = Record.Ghost[i].T; }
	}
	if (BestT < 0) return TNumericLimits<double>::Max();
	return Sp[Idx] - BestT;
}

bool ASmvFpvCourseActor::SaveIfRecord()
{
	if (!Tracker.IsValid() || !Tracker->Finished()) return false;
	const double Tot = Tracker->TotalS();
	Record.Runs++;
	const bool bNew = Record.TotalS < 0 || Tot < Record.TotalS;
	if (bNew)
	{
		Record.TotalS = Tot;
		Record.BestLapS = Tracker->BestLap();
		Record.Laps = Cfg.Laps;
		Record.Date = FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M"));
		Record.Ghost = Tracker->Samples();
	}
	SmvFpv::SaveRecord(Course.Id, Cfg.Laps, Record);
	return bNew;
}

FVector ASmvFpvCourseActor::NextGateEnu() const
{
	if (!Tracker.IsValid() || Tracker->Finished()) return FVector::ZeroVector;
	const FSmvFpvGate& G = Course.Gates[Tracker->NextGate()];
	return FVector(G.X - Course.StartX, G.Y - Course.StartY, G.Z);
}
