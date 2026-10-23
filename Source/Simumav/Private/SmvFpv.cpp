#include "SmvFpv.h"
#include "SimumavModule.h"
#include "SimumavTypes.h"
#include "SmvI18n.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "HAL/FileManager.h"

namespace
{
	const double GDronR = 0.25;          // dron yarıçapı (m)
	const double GSampleDt = 0.1;        // hayalet 10 Hz
	const int32 GLookAhead = 2;          // kaçırma: en çok 2 kapı ileri
	const double GHitGap = 1.0;          // aynı temas için yeni çarpma aralığı

	FVector Vec3(const TArray<TSharedPtr<FJsonValue>>& A)
	{
		return FVector(A.Num() > 0 ? A[0]->AsNumber() : 0.0, A.Num() > 1 ? A[1]->AsNumber() : 0.0, A.Num() > 2 ? A[2]->AsNumber() : 0.0);
	}

	TArray<FVector> Points(const TSharedPtr<FJsonObject>& O, const TCHAR* K)
	{
		TArray<FVector> Out;
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (O->TryGetArrayField(K, A) && A) for (const TSharedPtr<FJsonValue>& V : *A) if (V.IsValid() && V->Type == EJson::Array) Out.Add(Vec3(V->AsArray()));
		return Out;
	}

	FSmvFpvObst ParseObst(const TSharedPtr<FJsonObject>& O)
	{
		FSmvFpvObst E;
		E.T = SmvJson::Str(O, TEXT("t"));
		E.Mat = SmvJson::Str(O, TEXT("mat"));
		E.Part = SmvJson::Str(O, TEXT("part"));
		E.X = SmvJson::Num(O, TEXT("x")); E.Y = SmvJson::Num(O, TEXT("y")); E.Z = SmvJson::Num(O, TEXT("z"));
		E.W = SmvJson::Num(O, TEXT("w")); E.D = SmvJson::Num(O, TEXT("d")); E.H = SmvJson::Num(O, TEXT("h"));
		E.Yaw = SmvJson::Num(O, TEXT("yaw")); E.R = SmvJson::Num(O, TEXT("r"));
		E.RR = SmvJson::Num(O, TEXT("R")); E.Tube = SmvJson::Num(O, TEXT("tube"));
		E.Axis = SmvJson::Str(O, TEXT("axis"), TEXT("y"));
		E.bVis = SmvJson::Bool(O, TEXT("vis"), true);
		E.bFlat = SmvJson::Bool(O, TEXT("flat"), false);
		{
			const TSharedPtr<FJsonValue> Gv = O->TryGetField(TEXT("gate"));
			E.Gate = Gv.IsValid() && Gv->Type == EJson::Number ? (int32)Gv->AsNumber() : -1;
		}
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (O->TryGetArrayField(TEXT("a"), A) && A) E.A = Vec3(*A);
		if (O->TryGetArrayField(TEXT("b"), A) && A) E.B = Vec3(*A);
		if (O->TryGetArrayField(TEXT("holes"), A) && A)
		{
			for (const TSharedPtr<FJsonValue>& V : *A)
			{
				const TSharedPtr<FJsonObject> H = V.IsValid() ? V->AsObject() : nullptr;
				if (!H.IsValid()) continue;
				FSmvFpvHole Hl;
				Hl.U = SmvJson::Num(H, TEXT("u")); Hl.V = SmvJson::Num(H, TEXT("v")); Hl.W = SmvJson::Num(H, TEXT("w")); Hl.H = SmvJson::Num(H, TEXT("h"));
				E.Holes.Add(Hl);
			}
		}
		return E;
	}

	TArray<FSmvFpvCourse> LoadCourses()
	{
		TArray<FSmvFpvCourse> Out;
		FString Text;
		const FString File = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Simumav"), TEXT("Fpv"), TEXT("courses.json"));
		if (!FFileHelper::LoadFileToString(Text, *File)) { UE_LOG(LogSimumav, Warning, TEXT("FPV parkurları yok: %s"), *File); return Out; }
		const TSharedPtr<FJsonObject> Root = SmvJson::Parse(Text);
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!Root.IsValid() || !Root->TryGetArrayField(TEXT("courses"), Arr) || !Arr) return Out;
		for (const TSharedPtr<FJsonValue>& CV : *Arr)
		{
			const TSharedPtr<FJsonObject> O = CV.IsValid() ? CV->AsObject() : nullptr;
			if (!O.IsValid()) continue;
			FSmvFpvCourse C;
			C.Id = SmvJson::Str(O, TEXT("id"));
			C.Venue = SmvJson::Str(O, TEXT("venue"));
			C.Difficulty = (int32)SmvJson::Num(O, TEXT("difficulty"), 1);
			C.Laps = (int32)SmvJson::Num(O, TEXT("laps"), 1);
			C.ParLapS = SmvJson::Num(O, TEXT("par_lap_s"), 30);
			C.LengthM = SmvJson::Num(O, TEXT("length_m"), 0);
			C.GateCount = (int32)SmvJson::Num(O, TEXT("gate_count"), 0);
			C.Sectors = (int32)SmvJson::Num(O, TEXT("sectors"), 1);
			const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
			if (O->TryGetArrayField(TEXT("kinds"), A) && A) for (const TSharedPtr<FJsonValue>& V : *A) C.Kinds.Add(V->AsString());
			if (O->TryGetArrayField(TEXT("gates"), A) && A)
			{
				for (const TSharedPtr<FJsonValue>& V : *A)
				{
					const TSharedPtr<FJsonObject> G = V->AsObject();
					if (!G.IsValid()) continue;
					FSmvFpvGate X;
					X.Kind = SmvJson::Str(G, TEXT("kind"));
					X.X = SmvJson::Num(G, TEXT("x")); X.Y = SmvJson::Num(G, TEXT("y")); X.Z = SmvJson::Num(G, TEXT("z"));
					X.Yaw = SmvJson::Num(G, TEXT("yaw")); X.W = SmvJson::Num(G, TEXT("w"), 3); X.H = SmvJson::Num(G, TEXT("h"), 3);
					X.R = G->HasField(TEXT("r")) ? SmvJson::Num(G, TEXT("r")) : -1.0;
					X.A = G->HasField(TEXT("a")) ? SmvJson::Num(G, TEXT("a")) : -1.0;
					X.bFrame = SmvJson::Bool(G, TEXT("frame"), true);
					X.bSectorEnd = SmvJson::Bool(G, TEXT("sector_end"), false);
					X.Via = Points(G, TEXT("via"));
					X.ViaLap = Points(G, TEXT("via_lap"));
					X.Side = SmvJson::Str(G, TEXT("side"));
					X.Label = SmvJson::Str(G, TEXT("label"));
					const TArray<TSharedPtr<FJsonValue>>* P = nullptr;
					if (G->TryGetArrayField(TEXT("pole"), P) && P && P->Num() >= 2) { X.bPole = true; X.PoleX = (*P)[0]->AsNumber(); X.PoleY = (*P)[1]->AsNumber(); }
					X.PoleR = SmvJson::Num(G, TEXT("pole_r"), 0.09);
					C.Gates.Add(X);
				}
			}
			if (O->TryGetArrayField(TEXT("obstacles"), A) && A) for (const TSharedPtr<FJsonValue>& V : *A) if (V->AsObject().IsValid()) C.Obstacles.Add(ParseObst(V->AsObject()));
			if (O->TryGetArrayField(TEXT("decor"), A) && A) for (const TSharedPtr<FJsonValue>& V : *A) if (V->AsObject().IsValid()) C.Decor.Add(ParseObst(V->AsObject()));
			if (const TSharedPtr<FJsonObject> S = SmvJson::Obj(O, TEXT("start")))
			{
				C.StartX = SmvJson::Num(S, TEXT("x")); C.StartY = SmvJson::Num(S, TEXT("y")); C.StartYaw = SmvJson::Num(S, TEXT("yaw"));
			}
			if (const TSharedPtr<FJsonObject> En = SmvJson::Obj(O, TEXT("env")))
			{
				C.EnvTime = SmvJson::Str(En, TEXT("time"), TEXT("day"));
				C.EnvSky = SmvJson::Str(En, TEXT("sky"));
				C.EnvGround = SmvJson::Str(En, TEXT("ground"));
				C.EnvFog = SmvJson::Num(En, TEXT("fog"), 0.1);
				C.Ceiling = SmvJson::Num(En, TEXT("ceiling"), 0);
				C.bCanyon = SmvJson::Bool(En, TEXT("canyon"), false);
				C.bHill = SmvJson::Bool(En, TEXT("hill"), false);
				const TArray<TSharedPtr<FJsonValue>>* Sz = nullptr;
				if (En->TryGetArrayField(TEXT("size"), Sz) && Sz && Sz->Num() >= 2) C.Size = FVector2D((*Sz)[0]->AsNumber(), (*Sz)[1]->AsNumber());
			}
			Out.Add(MoveTemp(C));
		}
		UE_LOG(LogSimumav, Log, TEXT("FPV parkurları: %d"), Out.Num());
		return Out;
	}

	FString RecordFile(const FString& Id, int32 Laps)
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Simumav"), TEXT("Fpv"), FString::Printf(TEXT("%s_%d.json"), *Id, Laps));
	}
}

FText FSmvFpvCourse::Name() const { return FSmvI18n::T(FString(TEXT("fpv.c_")) + Id); }
FText FSmvFpvCourse::VenueName() const { return FSmvI18n::T(FString(TEXT("fpv.v_")) + Id); }

namespace SmvFpv
{
	const TArray<FSmvFpvCourse>& Courses()
	{
		static const TArray<FSmvFpvCourse> C = LoadCourses();
		return C;
	}

	const FSmvFpvCourse* Find(const FString& Id)
	{
		for (const FSmvFpvCourse& C : Courses()) if (C.Id == Id) return &C;
		return nullptr;
	}

	FVector Forward(double YawDeg) { const double A = FMath::DegreesToRadians(YawDeg); return FVector(FMath::Sin(A), FMath::Cos(A), 0.0); }
	FVector Right(double YawDeg) { const double A = FMath::DegreesToRadians(YawDeg); return FVector(FMath::Cos(A), -FMath::Sin(A), 0.0); }

	TArray<FSmvFpvCheckpoint> Checkpoints(const FSmvFpvCourse& C, int32 Laps)
	{
		TArray<FSmvFpvCheckpoint> Out;
		const int32 M = C.Gates.Num();
		if (M == 0) return Out;
		TArray<TPair<int32, int32>> Seq;   // (kapı, tur)
		Seq.Add(TPair<int32, int32>(0, 0));
		for (int32 Lap = 1; Lap <= FMath::Max(1, Laps); ++Lap)
		{
			for (int32 k = 1; k < M; ++k) Seq.Add(TPair<int32, int32>(k, Lap));
			Seq.Add(TPair<int32, int32>(0, Lap));
		}
		int32 Sector = 1;
		for (int32 i = 0; i < Seq.Num(); ++i)
		{
			const int32 K = Seq[i].Key, Lap = Seq[i].Value;
			const bool bSe = K == 0 || C.Gates[K].bSectorEnd;
			FSmvFpvCheckpoint P;
			P.I = i + 1; P.Gate = K; P.Lap = Lap; P.Sector = Lap ? Sector : 0; P.bSectorEnd = bSe && Lap > 0;
			Out.Add(P);
			if (Lap) Sector = K == 0 ? 1 : (bSe ? Sector + 1 : Sector);
		}
		return Out;
	}

	void GateAxes(const FSmvFpvGate& G, FVector& N, FVector& U, FVector& V)
	{
		if (G.Kind == TEXT("dive")) { N = FVector(0, 0, -1); U = Right(G.Yaw); V = Forward(G.Yaw); return; }
		N = Forward(G.Yaw); U = Right(G.Yaw); V = FVector(0, 0, 1);
	}

	int32 GateCross(const FVector& P0, const FVector& P1, const FSmvFpvGate& G, bool& bInside)
	{
		bInside = false;
		const FVector C(G.X, G.Y, G.Z);
		FVector N, U, V;
		GateAxes(G, N, U, V);
		const double D0 = FVector::DotProduct(P0 - C, N), D1 = FVector::DotProduct(P1 - C, N);
		int32 Dir = 0;
		if (D0 < 0 && D1 >= 0) Dir = 1;
		else if (D0 >= 0 && D1 < 0) Dir = -1;
		else return 0;
		const double T = D0 / (D0 - D1);
		const FVector Q = P0 + (P1 - P0) * T;
		const FVector Rv = Q - C;
		const double A = FVector::DotProduct(Rv, U), B = FVector::DotProduct(Rv, V);
		if (G.Kind == TEXT("circle")) bInside = FMath::Sqrt(A * A + B * B) <= (G.R > 0 ? G.R : G.W * 0.5);
		else bInside = FMath::Abs(A) <= G.W * 0.5 && FMath::Abs(B) <= G.H * 0.5;
		return Dir;
	}

	bool InObstacle(const FVector& P, const FSmvFpvObst& E, double Inf)
	{
		if (E.T == TEXT("box"))
		{
			const FVector F = Forward(E.Yaw), Rr = Right(E.Yaw);
			const double Dx = P.X - E.X, Dy = P.Y - E.Y;
			const double U = Dx * Rr.X + Dy * Rr.Y, V = Dx * F.X + Dy * F.Y, Z = P.Z - E.Z;
			if (FMath::Abs(U) > E.W / 2 + Inf || FMath::Abs(V) > E.D / 2 + Inf || Z < -Inf || Z > E.H + Inf) return false;
			for (const FSmvFpvHole& Hl : E.Holes)
			{
				if (E.Axis == TEXT("y")) { if (FMath::Abs(U - Hl.U) <= Hl.W / 2 - Inf && FMath::Abs(Z - Hl.V) <= Hl.H / 2 - Inf) return false; }
				else { if (FMath::Abs(U - Hl.U) <= Hl.W / 2 - Inf && FMath::Abs(V - Hl.V) <= Hl.H / 2 - Inf) return false; }
			}
			return true;
		}
		if (E.T == TEXT("cyl")) return FVector2D(P.X - E.X, P.Y - E.Y).Size() <= E.R + Inf && P.Z >= E.Z - Inf && P.Z <= E.Z + E.H + Inf;
		if (E.T == TEXT("sph")) return FVector::Dist(P, FVector(E.X, E.Y, E.Z)) <= E.R + Inf;
		if (E.T == TEXT("pipe"))
		{
			const FVector Ab = E.B - E.A;
			const double L2 = FMath::Max(Ab.SizeSquared(), 1e-9);
			const double S = FMath::Clamp(FVector::DotProduct(P - E.A, Ab) / L2, 0.0, 1.0);
			return FVector::Dist(P, E.A + Ab * S) <= E.R + Inf;
		}
		if (E.T == TEXT("torus"))
		{
			const FVector N = E.bFlat ? FVector(0, 0, 1) : Forward(E.Yaw);
			const FVector D = P - FVector(E.X, E.Y, E.Z);
			const double Dn = FVector::DotProduct(D, N);
			const double Rho = (D - N * Dn).Size();
			return FMath::Sqrt(FMath::Square(Rho - E.RR) + Dn * Dn) <= E.Tube + Inf;
		}
		return false;
	}

	int32 SegmentHit(const TArray<FSmvFpvObst>& Obs, const FVector& P0, const FVector& P1, double Inf)
	{
		const double L = FVector::Dist(P0, P1);
		const int32 N = FMath::Max(1, FMath::CeilToInt(L / 0.35));
		const FVector Mn = P0.ComponentMin(P1) - FVector(20.0), Mx = P0.ComponentMax(P1) + FVector(20.0);
		for (int32 k = 0; k < Obs.Num(); ++k)
		{
			const FSmvFpvObst& E = Obs[k];
			// kaba eleme (engel merkezi 20 m içinde)
			const FVector Ec = E.T == TEXT("pipe") ? (E.A + E.B) * 0.5 : FVector(E.X, E.Y, E.Z);
			const double Ext = FMath::Max3(E.W, E.D, E.H) + E.R + E.RR + (E.T == TEXT("pipe") ? (E.B - E.A).Size() : 0.0);
			if (Ec.X + Ext < Mn.X || Ec.X - Ext > Mx.X || Ec.Y + Ext < Mn.Y || Ec.Y - Ext > Mx.Y) continue;
			for (int32 s = 1; s <= N; ++s)
			{
				const FVector Q = P0 + (P1 - P0) * ((double)s / N);
				if (InObstacle(Q, E, Inf)) return k;
			}
		}
		return -1;
	}

	int32 Score(const FSmvFpvCourse& C, double TotalS, int32 Laps)
	{
		if (TotalS <= 0 || Laps <= 0) return 0;
		const double Avg = TotalS / Laps;
		return FMath::Clamp(FMath::RoundToInt(250.0 * C.ParLapS / FMath::Max(1.0, Avg)), 0, 375);
	}

	FSmvFpvRecord LoadRecord(const FString& Id, int32 Laps)
	{
		FSmvFpvRecord R;
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *RecordFile(Id, Laps))) return R;
		const TSharedPtr<FJsonObject> O = SmvJson::Parse(Text);
		if (!O.IsValid()) return R;
		R.TotalS = SmvJson::Num(O, TEXT("total_s"), -1);
		R.BestLapS = SmvJson::Num(O, TEXT("best_lap_s"), -1);
		R.Laps = (int32)SmvJson::Num(O, TEXT("laps"), Laps);
		R.Runs = (int32)SmvJson::Num(O, TEXT("runs"), 0);
		R.Date = SmvJson::Str(O, TEXT("date"));
		const TArray<TSharedPtr<FJsonValue>>* G = nullptr;
		if (O->TryGetArrayField(TEXT("ghost"), G) && G)
		{
			for (const TSharedPtr<FJsonValue>& V : *G)
			{
				const TArray<TSharedPtr<FJsonValue>>& A = V->AsArray();
				if (A.Num() < 5) continue;
				FSmvFpvGhostSample S;
				S.T = (float)A[0]->AsNumber(); S.X = (float)A[1]->AsNumber(); S.Y = (float)A[2]->AsNumber(); S.Z = (float)A[3]->AsNumber(); S.Yaw = (float)A[4]->AsNumber();
				R.Ghost.Add(S);
			}
		}
		return R;
	}

	void SaveRecord(const FString& Id, int32 Laps, const FSmvFpvRecord& R)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("total_s"), R.TotalS);
		O->SetNumberField(TEXT("best_lap_s"), R.BestLapS);
		O->SetNumberField(TEXT("laps"), Laps);
		O->SetNumberField(TEXT("runs"), R.Runs);
		O->SetStringField(TEXT("date"), R.Date);
		TArray<TSharedPtr<FJsonValue>> G;
		for (const FSmvFpvGhostSample& S : R.Ghost)
		{
			TArray<TSharedPtr<FJsonValue>> A;
			A.Add(MakeShared<FJsonValueNumber>(FMath::RoundToFloat(S.T * 100.f) / 100.f));
			A.Add(MakeShared<FJsonValueNumber>(FMath::RoundToFloat(S.X * 50.f) / 50.f));
			A.Add(MakeShared<FJsonValueNumber>(FMath::RoundToFloat(S.Y * 50.f) / 50.f));
			A.Add(MakeShared<FJsonValueNumber>(FMath::RoundToFloat(S.Z * 50.f) / 50.f));
			A.Add(MakeShared<FJsonValueNumber>(FMath::RoundToFloat(S.Yaw)));
			G.Add(MakeShared<FJsonValueArray>(A));
		}
		O->SetArrayField(TEXT("ghost"), G);
		const FString File = RecordFile(Id, Laps);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
		FFileHelper::SaveStringToFile(SmvJson::ToString(O), *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	FLinearColor MatColor(const FString& M)
	{
		auto H = [](uint32 Rgb) { return FLinearColor::FromSRGBColor(FColor((uint8)((Rgb >> 16) & 0xff), (uint8)((Rgb >> 8) & 0xff), (uint8)(Rgb & 0xff))); };
		if (M == TEXT("needle")) return H(0x1f4d2b);
		if (M == TEXT("leaf") || M == TEXT("bush")) return H(0x3f7a33);
		if (M == TEXT("bark") || M == TEXT("log")) return H(0x5b3a24);
		if (M.StartsWith(TEXT("rock"))) return H(0x7b746b);
		if (M == TEXT("timber") || M == TEXT("planks") || M == TEXT("pallet") || M == TEXT("crate")) return H(0xa0763f);
		if (M.StartsWith(TEXT("steel")) || M == TEXT("truss") || M == TEXT("rack") || M == TEXT("grating") || M == TEXT("scaffold") || M == TEXT("scaffold_deck")) return H(0x6b7685);
		if (M == TEXT("brick")) return H(0x8f4a35);
		if (M == TEXT("plaster")) return H(0xd9d0c1);
		if (M.StartsWith(TEXT("container"))) { static const uint32 Cc[] = { 0x1d4ed8, 0xb91c1c, 0x15803d, 0xc2410c }; return H(Cc[FMath::Clamp(FCString::Atoi(*M.Right(1)), 0, 3)]); }
		if (M.StartsWith(TEXT("pipe")) || M == TEXT("duct") || M == TEXT("chimney") || M == TEXT("tank")) return H(0x9ca3af);
		if (M == TEXT("pole")) return H(0xf5f5f5);
		if (M == TEXT("railing_yellow") || M == TEXT("crane") || M == TEXT("forklift")) return H(0xeab308);
		if (M == TEXT("hay")) return H(0xd6b656);
		if (M == TEXT("water") || M == TEXT("waterfall")) return H(0x2f6fa8);
		if (M == TEXT("inflatable")) return H(0xef4444);
		if (M == TEXT("stand")) return H(0x475569);
		if (M == TEXT("rubble") || M == TEXT("rebar")) return H(0x6f6a61);
		return H(0x9a9a96);   // beton / levha / blok
	}
}

// ---------------------------------------------------------------------------
// izleyici
// ---------------------------------------------------------------------------

FSmvFpvTracker::FSmvFpvTracker(const FSmvFpvCourse& InCourse, const FSmvFpvCfg& InCfg, double InT0)
	: C(InCourse), K(InCfg), T0(InT0)
{
	Points = SmvFpv::Checkpoints(C, K.Laps);
}

int32 FSmvFpvTracker::Lap() const
{
	if (Finished()) return K.Laps;
	return Points.IsValidIndex(Seq) ? FMath::Max(1, Points[Seq].Lap) : 1;
}

double FSmvFpvTracker::BestLap() const
{
	double B = -1;
	for (double L : LapTimes) if (B < 0 || L < B) B = L;
	return B;
}

int32 FSmvFpvTracker::GateState(int32 GateIdx) const
{
	if (Finished()) return 3;
	for (int32 j = 0; j < 3 && Seq + j < Points.Num(); ++j) if (Points[Seq + j].Gate == GateIdx) return j == 0 ? 1 : 2;
	// bu turda geçildi mi
	const int32 CurLap = Points[Seq].Lap;
	for (int32 i = Seq - 1; i >= 0 && Points[i].Lap == CurLap; --i) if (Points[i].Gate == GateIdx) return 3;
	return 0;
}

void FSmvFpvTracker::Advance(double T, bool bPassed, TArray<FSmvFpvEvent>& Ev)
{
	const FSmvFpvCheckpoint& H = Points[Seq];
	FSmvFpvEvent E;
	E.I = H.I; E.Gate = H.Gate; E.Lap = H.Lap; E.T = T;
	if (bPassed) { E.Kind = TEXT("ring"); Ev.Add(E); }
	else
	{
		Missed.Add(H.I);
		Pen += K.MissPenS;
		E.Kind = TEXT("miss"); E.PenS = K.MissPenS;
		Ev.Add(E);
	}
	SplitT.Add(T - T0);
	if (H.Lap == 0)
	{
		LapT0 = SectorT0 = T;
		Sectors.AddDefaulted();
	}
	else
	{
		if (H.bSectorEnd && SectorT0 >= 0)
		{
			const double S = T - SectorT0;
			const double* Prev = BestSector.Find(H.Sector);
			const bool bBest = !Prev || S < *Prev;
			BestSector.Add(H.Sector, Prev ? FMath::Min(*Prev, S) : S);
			if (Sectors.Num()) Sectors.Last().Add(S);
			FSmvFpvEvent Se; Se.Kind = TEXT("sector"); Se.Lap = H.Lap; Se.Sector = H.Sector; Se.S = S; Se.bBest = bBest; Se.T = T;
			Ev.Add(Se);
			SectorT0 = T;
		}
		if (H.Gate == 0 && LapT0 >= 0)
		{
			const double S = T - LapT0;
			const double Prev = BestLap();
			LapTimes.Add(S);
			FSmvFpvEvent Le; Le.Kind = TEXT("lap"); Le.Lap = H.Lap; Le.S = S; Le.bBest = Prev < 0 || S < Prev; Le.T = T;
			Ev.Add(Le);
			LapT0 = T;
			if (H.Lap < K.Laps) Sectors.AddDefaulted();
		}
	}
	++Seq;
	if (Finished())
	{
		FinishT = T;
		FSmvFpvEvent Fe; Fe.Kind = TEXT("finish"); Fe.T = T; Fe.TotalS = T - T0 + Pen;
		Ev.Add(Fe);
	}
}

TArray<FSmvFpvEvent> FSmvFpvTracker::Update(const FVector& P, double T, float YawDeg)
{
	TArray<FSmvFpvEvent> Ev;
	if (T - SampleT >= GSampleDt - 1e-6 && Ghost.Num() < 36000 && !Finished())
	{
		SampleT = T;
		FSmvFpvGhostSample S; S.T = (float)(T - T0); S.X = (float)P.X; S.Y = (float)P.Y; S.Z = (float)P.Z; S.Yaw = YawDeg;
		Ghost.Add(S);
	}
	const FVector Prev = Last;
	const bool bPrev = bHasLast;
	Last = P; bHasLast = true;
	if (bPrev && FVector::Dist(Prev, P) <= 250.0)
	{
		const int32 Hit = SmvFpv::SegmentHit(C.Obstacles, Prev, P, GDronR);
		if (Hit >= 0)
		{
			if (!bContact && T - ContactT >= GHitGap)
			{
				++HitCount;
				ContactT = T;
				FSmvFpvEvent E; E.Kind = TEXT("hit"); E.Obstacle = Hit; E.Mat = C.Obstacles[Hit].Mat; E.Rule = K.CrashRule; E.T = T;
				if (K.CrashRule == TEXT("penalty")) { Pen += K.CrashPenS; E.PenS = K.CrashPenS; }
				Ev.Add(E);
				if (K.CrashRule == TEXT("reset")) { FSmvFpvEvent R; R.Kind = TEXT("reset"); R.T = T; Ev.Add(R); }
			}
			bContact = true;
		}
		else bContact = false;
		if (!Finished())
		{
			for (int32 j = 0; j <= GLookAhead; ++j)
			{
				if (Seq + j >= Points.Num()) break;
				const FSmvFpvCheckpoint& H = Points[Seq + j];
				bool bIn = false;
				const int32 Dir = SmvFpv::GateCross(Prev, P, C.Gates[H.Gate], bIn);
				if (Dir > 0 && bIn)
				{
					for (int32 m = 0; m < j; ++m) Advance(T, false, Ev);
					Advance(T, true, Ev);
					break;
				}
				if (j == 0 && Dir < 0 && bIn) { FSmvFpvEvent W; W.Kind = TEXT("wrong_way"); W.I = H.I; Ev.Add(W); }
			}
		}
	}
	if (!Finished())
	{
		const FSmvFpvGate& G = C.Gates[Points[Seq].Gate];
		Dist = FVector::Dist(P, FVector(G.X, G.Y, G.Z));
	}
	else Dist = 0;
	return Ev;
}
