// Fab haritası FPV parkuru: kapıların otomatik dizilmesi (kullanıcı 2026-11-04 — "FPV parkurları gerçek, dokulu ortamda olsun").
// Harita yüklendikten sonra çağrılır. Başlangıç pedinden kalkış yönüne teğet bir döngü (çember) denenir; her kapı için yarıçap
// ve yükseklik çeşitlemeleri arasından ilk uygun konum seçilir: zemin bulunmalı (çatı üstü değil), kapı açıklığı ve tavanı boş olmalı,
// önceki kapıdan buraya giden yol (küre süpürme) engelsiz olmalı. En çok kapıyı yerleştiren döngü seçilir; son kapıdan başlangıç
// kapısına kapanış yolu da denetlenir. Koordinatlar parkur ENU (m; x doğu, y kuzey, z kalkış zemininden yukarı).
#include "SmvFpv.h"
#include "SimumavModule.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"

namespace
{
	struct FAutoCtx
	{
		UWorld* W = nullptr;
		TFunctionRef<FVector(double, double, double)> ToW;
		FCollisionQueryParams Q;
		FVector O, Uw;
		double UwLen2 = 1.0;
		FAutoCtx(UWorld* InW, TFunctionRef<FVector(double, double, double)> In) : W(InW), ToW(In), Q(SCENE_QUERY_STAT(SmvFpvAuto), true) {}

		double ZOf(const FVector& Pw) const { return FVector::DotProduct(Pw - O, Uw) / UwLen2; }

		/** (x, y) noktasında Zt'den aşağı ilk yüzey (parkur z, m); yoksa false */
		bool Ground(double X, double Y, double Zt, double Zb, double& OutZ) const
		{
			FHitResult H;
			if (!W->LineTraceSingleByChannel(H, ToW(X, Y, Zt), ToW(X, Y, Zb), ECC_Visibility, Q)) return false;
			OutZ = ZOf(H.ImpactPoint);
			return true;
		}
		bool Blocked(const FVector& A, const FVector& B, double Rm) const
		{
			FHitResult H;
			const float Rc = (float)(Rm * FMath::Sqrt(UwLen2));
			return W->SweepSingleByChannel(H, ToW(A.X, A.Y, A.Z), ToW(B.X, B.Y, B.Z), FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(Rc), Q);
		}
		bool Occupied(const FVector& P, double Rm) const
		{
			const float Rc = (float)(Rm * FMath::Sqrt(UwLen2));
			return W->OverlapAnyTestByChannel(ToW(P.X, P.Y, P.Z), FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(Rc), Q);
		}
	};

	double YawOf(const FVector2D& D) { return FMath::Fmod(FMath::RadiansToDegrees(FMath::Atan2(D.X, D.Y)) + 360.0, 360.0); }

	/** Bir döngü denemesi: Dir +1 saat yönü (sağa dönüş), −1 sola; R yarıçap (m) */
	TArray<FSmvFpvGate> TryLoop(const FAutoCtx& Cx, double StartYaw, double R, double Dir, int32 N, bool& bClosed)
	{
		bClosed = false;
		TArray<FSmvFpvGate> Out;
		const FVector Rt = SmvFpv::Right(StartYaw);
		const FVector2D C(Rt.X * R * Dir, Rt.Y * R * Dir);        // çember merkezi: başlangıç noktası çemberin üstünde, kalkış yönüne teğet
		const double A0 = FMath::Atan2(-C.Y, -C.X);              // merkezden başlangıca açı
		const double Arc = 2.0 * PI * R;
		const double S0 = FMath::Min(9.0, Arc * 0.04);           // başlangıç / bitiş kapısı pedin 9 m önünde
		FVector Prev(0, 0, 1.6);
		double PrevG = 0.0;
		static const double Radial[] = { 1.0, 0.85, 1.15, 0.7, 1.3 };
		static const double Heights[] = { 2.2, 3.5, 5.5 };
		for (int32 k = 0; k < N; ++k)
		{
			const double S = S0 + (Arc - S0 * 1.5) * k / N;
			const double A = A0 - Dir * S / R;                     // saat yönü (Dir +1) açıyı azaltır
			const bool bStart = k == 0;
			bool bOk = false;
			for (double Fr : Radial)
			{
				if (bStart && Fr != 1.0) continue;
				const double X = C.X + FMath::Cos(A) * R * Fr, Y = C.Y + FMath::Sin(A) * R * Fr;
				double G = 0;
				// zemin: önceki kapı zemininin 10 m üstünden aşağı — daha yüksekte bir yüzey (çatı, kaya) varsa kapı oraya konmaz
				if (!Cx.Ground(X, Y, PrevG + 10.0, PrevG - 40.0, G)) continue;
				if (FMath::Abs(G - PrevG) > 7.0) continue;
				for (double Hh : Heights)
				{
					const FVector P(X, Y, G + Hh);
					if (Cx.Occupied(P, 1.7)) continue;
					if (Cx.Blocked(FVector(X, Y, G + 0.6), FVector(X, Y, G + Hh + 2.2), 0.5)) continue;   // kapı tavanı
					if (Cx.Blocked(Prev, P, 0.9)) continue;
					// kapı yönü: çembere teğet (bir önceki kapıdan buraya)
					const FVector2D Tg(-FMath::Sin(A) * -Dir, FMath::Cos(A) * -Dir);
					FSmvFpvGate Gt;
					Gt.Kind = bStart ? TEXT("start") : TEXT("gate");
					Gt.X = X; Gt.Y = Y; Gt.Z = P.Z; Gt.Yaw = YawOf(Tg);
					Gt.W = 4.0; Gt.H = 3.0; Gt.bFrame = true;
					Out.Add(Gt);
					Prev = P; PrevG = G;
					bOk = true;
					break;
				}
				if (bOk) break;
			}
			if (!bOk && bStart) return Out;                        // başlangıç kapısı konamadı: bu döngü olmaz
		}
		if (Out.Num() >= 2)
		{
			const FSmvFpvGate& L = Out.Last();
			const FSmvFpvGate& S = Out[0];
			bClosed = !Cx.Blocked(FVector(L.X, L.Y, L.Z), FVector(S.X, S.Y, S.Z), 0.9);
		}
		return Out;
	}
}

namespace SmvFpv
{
	bool GenerateFabGates(FSmvFpvCourse& C, UWorld* World, TFunctionRef<FVector(double, double, double)> ToWorld, const TArray<AActor*>& Ignore)
	{
		if (!World) return false;
		FAutoCtx Cx(World, ToWorld);
		Cx.O = ToWorld(0, 0, 0);
		Cx.Uw = ToWorld(0, 0, 1) - Cx.O;
		Cx.UwLen2 = FMath::Max(1e-6, Cx.Uw.SizeSquared());
		for (AActor* A : Ignore) if (A) Cx.Q.AddIgnoredActor(A);
		// başlangıç zemini (harita demiri MSL'si gerçek zeminden farklı olabilir)
		double G0 = 0;
		if (!Cx.Ground(0, 0, 60.0, -120.0, G0)) G0 = 0;
		const int32 N = FMath::Clamp(C.GateCount > 0 ? C.GateCount : 10, 6, 14);
		TArray<FSmvFpvGate> Best;
		bool bBestClosed = false;
		double BestR = 0;
		static const double Radii[] = { 45.0, 60.0, 32.0, 80.0, 24.0, 100.0 };
		for (double R : Radii)
		{
			for (double Dir : { 1.0, -1.0 })
			{
				bool bClosed = false;
				TArray<FSmvFpvGate> G = TryLoop(Cx, C.StartYaw, R, Dir, N, bClosed);
				const int32 Score = G.Num() * 2 + (bClosed ? 3 : 0);
				const int32 BestScore = Best.Num() * 2 + (bBestClosed ? 3 : 0);
				if (Score > BestScore) { Best = MoveTemp(G); bBestClosed = bClosed; BestR = R; }
			}
			if (Best.Num() >= N - 1 && bBestClosed) break;
		}
		UE_LOG(LogSimumav, Log, TEXT("FPV Fab parkuru %s: %d kapı (yarıçap %.0f m, döngü %s, başlangıç zemini %.1f m)"),
			*C.Id, Best.Num(), BestR, bBestClosed ? TEXT("kapalı") : TEXT("açık"), G0);
		if (Best.Num() < 5) return false;
		// sektör sonları üçte birlerde; uzunluk ve hedef tur süresi kapı aralarından
		double Len = 0;
		for (int32 i = 0; i < Best.Num(); ++i)
		{
			const FSmvFpvGate& A = Best[i];
			const FSmvFpvGate& B = Best[(i + 1) % Best.Num()];
			Len += FVector::Dist(FVector(A.X, A.Y, A.Z), FVector(B.X, B.Y, B.Z));
		}
		const int32 M = Best.Num();
		Best[FMath::Max(1, M / 3)].bSectorEnd = true;
		Best[FMath::Max(1, 2 * M / 3)].bSectorEnd = true;
		C.Gates = MoveTemp(Best);
		C.GateCount = M;
		C.LengthM = FMath::RoundToDouble(Len);
		C.ParLapS = FMath::RoundToDouble(Len / 9.0);
		C.Sectors = 3;
		return true;
	}
}
