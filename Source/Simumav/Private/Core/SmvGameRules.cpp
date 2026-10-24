#include "SmvGameRules.h"

#include <algorithm>
#include <cmath>

namespace SmvCore
{
	namespace
	{
		constexpr double GR_PI = 3.14159265358979323846;
		constexpr double GR_G = 9.80665;
		double Deg2Rad(double D) { return D * GR_PI / 180.0; }
		/** Basit LCG (0..1) — tohumlu, platformdan bağımsız */
		double Lcg(unsigned& S) { S = S * 1664525u + 1013904223u; return (double)(S >> 8) / 16777216.0; }
		double WrapDeg(double A) { A = std::fmod(A + 540.0, 360.0); if (A < 0) A += 360.0; return A - 180.0; }
	}

	const char* GameKindId(EGameKind K)
	{
		switch (K)
		{
		case EGameKind::Rings: return "rings";
		case EGameKind::CargoDrop: return "cargo_drop";
		case EGameKind::Spraying: return "spraying";
		case EGameKind::Dogfight: return "dogfight";
		case EGameKind::FacilityDefense: return "facility_defense";
		case EGameKind::ShipDefense: return "ship_defense";
		default: return "free";
		}
	}

	EGameKind GameKindFromId(const std::string& Id)
	{
		if (Id == "rings") return EGameKind::Rings;
		if (Id == "cargo_drop") return EGameKind::CargoDrop;
		if (Id == "spraying") return EGameKind::Spraying;
		if (Id == "dogfight") return EGameKind::Dogfight;
		if (Id == "facility_defense") return EGameKind::FacilityDefense;
		if (Id == "ship_defense") return EGameKind::ShipDefense;
		return EGameKind::Free;
	}

	bool GameKindIsMatch(EGameKind K) { return K == EGameKind::Rings || K == EGameKind::Dogfight; }

	int GameKindSingleAi(EGameKind K)
	{
		// modes.MODES ai.single: halka toplama tek oyunculuda 0 (zaman denemesi; istenirse rakip eklenir), it dalaşı 1
		return K == EGameKind::Dogfight ? 1 : 0;
	}

	int DefenseWaveSize(int Wave) { return std::min(8, 2 + std::max(0, Wave)); }

	int DefenseThreatTotal(int Waves)
	{
		int N = 0;
		for (int i = 0; i < Waves; ++i) N += DefenseWaveSize(i);
		return N;
	}

	// ------------------------------------------------------------------ yük bırakma

	void FDropGame::Setup(const FDropCfg& C)
	{
		Cfg = C;
		Cfg.Payloads = std::max(1, std::min(20, Cfg.Payloads));
		Cfg.Zones = std::max(1, std::min(8, Cfg.Zones));
		Z.clear();
		P.clear();
		unsigned S = Cfg.Seed ? Cfg.Seed : 1u;
		int Guard = 0;
		while ((int)Z.size() < Cfg.Zones && Guard++ < 400)
		{
			const double A = Lcg(S) * 2.0 * GR_PI;
			const double R = Cfg.AreaMinR + Lcg(S) * (Cfg.AreaMaxR - Cfg.AreaMinR);
			FDropZone Zn;
			Zn.E = R * std::sin(A);
			Zn.N = R * std::cos(A);
			Zn.R = Cfg.ZoneR;
			bool bOk = true;
			for (const FDropZone& X : Z) if (std::hypot(X.E - Zn.E, X.N - Zn.N) < 70.0) bOk = false;
			if (bOk) Z.push_back(Zn);
		}
	}

	bool FDropGame::Release(double T, double E, double N, double U, double Ve, double Vn, double Vu)
	{
		if (Left() <= 0) return false;
		FPayload X;
		X.E = E; X.N = N; X.U = std::max(0.0, U); X.Ve = Ve; X.Vn = Vn; X.Vu = Vu; X.T0 = T;
		P.push_back(X);
		return true;
	}

	int FDropGame::PointsFor(double DistM)
	{
		if (DistM < 0) return 0;
		return DistM <= 5.0 ? 120 : DistM <= 15.0 ? 70 : DistM <= 30.0 ? 30 : 0;
	}

	void FDropGame::Step(double Dt, std::vector<FGameEvent>& Ev, double GroundU)
	{
		if (Dt <= 0) return;
		for (size_t i = 0; i < P.size(); ++i)
		{
			FPayload& X = P[i];
			if (X.bLanded) continue;
			double Rem = Dt;
			while (Rem > 1e-9 && !X.bLanded)
			{
				const double H = std::min(0.02, Rem);
				Rem -= H;
				X.Ve += -Cfg.DragK * X.Ve * H;
				X.Vn += -Cfg.DragK * X.Vn * H;
				X.Vu += (-GR_G - Cfg.DragK * X.Vu) * H;
				X.E += X.Ve * H; X.N += X.Vn * H; X.U += X.Vu * H;
				if (X.U <= GroundU)
				{
					X.U = GroundU;
					X.bLanded = true;
					double Best = 1e18; int Bi = -1;
					for (size_t k = 0; k < Z.size(); ++k)
					{
						const double D = std::hypot(Z[k].E - X.E, Z[k].N - X.N);
						if (D < Best) { Best = D; Bi = (int)k; }
					}
					X.Zone = Bi;
					X.DistM = Bi >= 0 ? Best : -1;
					X.Points = PointsFor(X.DistM);
					if (Bi >= 0 && X.Points > 0)
					{
						Z[Bi].Hits++;
						if (Z[Bi].BestM < 0 || X.DistM < Z[Bi].BestM) Z[Bi].BestM = X.DistM;
					}
					FGameEvent G; G.Kind = "land"; G.I = (int)i; G.Value = X.DistM; G.E = X.E; G.N = X.N;
					Ev.push_back(G);
				}
			}
		}
	}

	int FDropGame::Score() const
	{
		int S = Released() ? 40 : 0;
		for (const FPayload& X : P) S += X.Points;
		// her bölgeye en az bir isabet: +50
		int Hit = 0;
		for (const FDropZone& Zn : Z) if (Zn.Hits > 0) ++Hit;
		if (!Z.empty() && Hit == (int)Z.size()) S += 50;
		return S;
	}

	double FDropGame::BestM() const
	{
		double B = -1;
		for (const FPayload& X : P) if (X.bLanded && X.DistM >= 0 && (B < 0 || X.DistM < B)) B = X.DistM;
		return B;
	}

	bool FDropGame::Done() const
	{
		if (Left() > 0) return false;
		for (const FPayload& X : P) if (!X.bLanded) return false;
		return true;
	}

	// ------------------------------------------------------------------ tarla ilaçlama

	void FSprayGame::Setup(const FSprayCfg& C)
	{
		Cfg = C;
		Cfg.Cell = std::max(1.0, Cfg.Cell);
		Nx = std::max(1, (int)std::ceil(Cfg.W / Cfg.Cell));
		Ny = std::max(1, (int)std::ceil(Cfg.L / Cfg.Cell));
		Grid.assign((size_t)Nx * Ny, 0);
		Fresh.clear();
		Over = 0; Marked = 0; OverAcc = 0.0;
		Tank = Cfg.TankL;
		LastWarnT = -100;
		bDone = false; bEmptySaid = false;
	}

	bool FSprayGame::ToLocal(double E, double N, double& U, double& V) const
	{
		const double H = Deg2Rad(Cfg.HeadingDeg);
		const double Fe = std::sin(H), Fn = std::cos(H), Re = std::cos(H), Rn = -std::sin(H);
		const double De = E - Cfg.CenterE, Dn = N - Cfg.CenterN;
		U = De * Re + Dn * Rn;
		V = De * Fe + Dn * Fn;
		return std::fabs(U) <= Cfg.W * 0.5 && std::fabs(V) <= Cfg.L * 0.5;
	}

	void FSprayGame::CellCenter(int Cx, int Cy, double& E, double& N) const
	{
		const double U = -Cfg.W * 0.5 + (Cx + 0.5) * Cfg.Cell, V = -Cfg.L * 0.5 + (Cy + 0.5) * Cfg.Cell;
		const double H = Deg2Rad(Cfg.HeadingDeg);
		const double Fe = std::sin(H), Fn = std::cos(H), Re = std::cos(H), Rn = -std::sin(H);
		E = Cfg.CenterE + U * Re + V * Fe;
		N = Cfg.CenterN + U * Rn + V * Fn;
	}

	void FSprayGame::Step(double T, double Dt, double E, double N, double Agl, double Gs, bool bSpraying, std::vector<FGameEvent>& Ev)
	{
		Fresh.clear();
		if (bDone || Dt <= 0) return;
		if (!bSpraying) return;
		if (Tank <= 0.0)
		{
			if (!bEmptySaid) { bEmptySaid = true; FGameEvent G; G.Kind = "tank_empty"; Ev.push_back(G); }
			return;
		}
		Tank = std::max(0.0, Tank - Cfg.RateLps * Dt);
		auto Warn = [&](const char* K)
		{
			if (T - LastWarnT < 3.0) return;
			LastWarnT = T;
			FGameEvent G; G.Kind = K; Ev.push_back(G);
		};
		if (Agl > Cfg.MaxAgl) { Warn("too_high"); return; }
		if (Agl < Cfg.MinAgl) return;
		if (Gs > Cfg.MaxGs) { Warn("too_fast"); return; }
		double U = 0, V = 0;
		const bool bIn = ToLocal(E, N, U, V);
		const double R = Cfg.SwathW * 0.5;
		if (!bIn && (std::fabs(U) > Cfg.W * 0.5 + R || std::fabs(V) > Cfg.L * 0.5 + R))
		{
			// tarla dışına püskürtme: alan karşılığı (hücre) birikir
			OverAcc += Cfg.SwathW * std::max(1.0, Gs) * Dt / (Cfg.Cell * Cfg.Cell);
			while (OverAcc >= 1.0) { OverAcc -= 1.0; ++Over; }
			return;
		}
		const int X0 = std::max(0, (int)std::floor((U - R + Cfg.W * 0.5) / Cfg.Cell)), X1 = std::min(Nx - 1, (int)std::floor((U + R + Cfg.W * 0.5) / Cfg.Cell));
		const int Y0 = std::max(0, (int)std::floor((V - R + Cfg.L * 0.5) / Cfg.Cell)), Y1 = std::min(Ny - 1, (int)std::floor((V + R + Cfg.L * 0.5) / Cfg.Cell));
		for (int Cy = Y0; Cy <= Y1; ++Cy)
		{
			for (int Cx = X0; Cx <= X1; ++Cx)
			{
				const double Cu = -Cfg.W * 0.5 + (Cx + 0.5) * Cfg.Cell, Cv = -Cfg.L * 0.5 + (Cy + 0.5) * Cfg.Cell;
				if (std::hypot(Cu - U, Cv - V) > R + Cfg.Cell * 0.35) continue;
				unsigned char& G = Grid[(size_t)Cy * Nx + Cx];
				if (!G) { G = 1; ++Marked; Fresh.push_back(Cy * Nx + Cx); }
			}
		}
		if (Coverage() >= 0.98 && !bDone)
		{
			bDone = true;
			FGameEvent G; G.Kind = "done"; G.Value = Coverage(); Ev.push_back(G);
		}
	}

	double FSprayGame::Coverage() const
	{
		return Grid.empty() ? 0.0 : (double)Marked / (double)Grid.size();
	}

	int FSprayGame::Score() const
	{
		const double C = Coverage();
		int S = (int)std::llround(C * 1000.0) - std::min(300, Over * 2);
		if (C >= 0.9 && Cfg.TankL > 0) S += (int)std::llround(Tank / Cfg.TankL * 100.0);
		return std::max(0, S);
	}

	// ------------------------------------------------------------------ savunma

	double FDefenseGame::Rand01() { return Lcg(Rng); }

	void FDefenseGame::Setup(const FDefCfg& C)
	{
		Cfg = C;
		Cfg.Waves = std::max(1, std::min(10, Cfg.Waves));
		Th.clear();
		FacE = Cfg.FacE; FacN = Cfg.FacN;
		T0 = -1; LastT = 0;
		HpNow = std::max(1, Cfg.Hp);
		Neut = Imp = LockedContacts = WaveNow = Spawned = 0;
		NextId = 1; Lock = 0; LockS = 0;
		bDone = false; bPlayerLock = false;
		Rng = Cfg.Seed ? Cfg.Seed : 1u;
	}

	void FDefenseGame::SpawnWave(int W, std::vector<FGameEvent>& Ev)
	{
		const int N = DefenseWaveSize(W);
		const double Base = Rand01() * 2.0 * GR_PI;
		for (int i = 0; i < N; ++i)
		{
			// dalga bir yönden yelpaze halinde gelir (±50°)
			const double A = Base + (Rand01() - 0.5) * Deg2Rad(100.0);
			FThreat X;
			X.Id = NextId++;
			X.Wave = W;
			X.E = FacE + Cfg.SpawnR * (0.85 + 0.3 * Rand01()) * std::sin(A);
			X.N = FacN + Cfg.SpawnR * (0.85 + 0.3 * Rand01()) * std::cos(A);
			X.U = Cfg.AltM + 25.0 * Rand01();
			X.Phase = Rand01() * 2.0 * GR_PI;
			X.bAlive = true;
			Th.push_back(X);
			++Spawned;
			FGameEvent G; G.Kind = "spawn"; G.I = X.Id; G.E = X.E; G.N = X.N; Ev.push_back(G);
		}
		WaveNow = W + 1;
		FGameEvent G; G.Kind = "wave"; G.I = WaveNow; G.Value = N; Ev.push_back(G);
	}

	void FDefenseGame::Step(double T, double Dt, double PE, double PN, double PU, double PlayerYawDeg, std::vector<FGameEvent>& Ev)
	{
		if (bDone) return;
		if (T0 < 0) T0 = T;
		const double Rel = T - T0;
		LastT = T;
		if (Cfg.bShip)
		{
			const double H = Deg2Rad(Cfg.ShipHeadingDeg);
			FacE += Cfg.ShipSpeed * std::sin(H) * Dt;
			FacN += Cfg.ShipSpeed * std::cos(H) * Dt;
		}
		// dalga zamanlaması: ilk dalga FirstAt sn sonra; sonrakiler WaveGap aralıkla ya da önceki dalga temizlenince (en erken 4 sn)
		int Alive = 0;
		for (const FThreat& X : Th) if (X.bAlive) ++Alive;
		if (WaveNow < Cfg.Waves)
		{
			const double Due = Cfg.FirstAt + WaveNow * Cfg.WaveGap;
			const double LastSpawn = WaveNow > 0 ? Cfg.FirstAt + (WaveNow - 1) * Cfg.WaveGap : 0.0;
			if (Rel >= Due || (WaveNow > 0 && Alive == 0 && Rel >= std::min(Due, LastSpawn + 4.0))) SpawnWave(WaveNow, Ev);
		}
		// tehdit hareketi: tesise doğru, yanal salınım (±25°); son 250 m'de alçalma
		for (FThreat& X : Th)
		{
			if (!X.bAlive) continue;
			const double De = FacE - X.E, Dn = FacN - X.N;
			const double D = std::max(1e-6, std::hypot(De, Dn));
			const double Weave = Deg2Rad(25.0) * std::sin(Rel * 0.55 + X.Phase) * std::min(1.0, D / 400.0);
			const double Ce = De / D, Cn = Dn / D;
			const double Ue = Ce * std::cos(Weave) - Cn * std::sin(Weave), Un = Ce * std::sin(Weave) + Cn * std::cos(Weave);
			X.Ve = Ue * Cfg.Speed; X.Vn = Un * Cfg.Speed;
			X.Vu = D < 250.0 ? -std::max(0.0, X.U - (Cfg.FacU + 6.0)) * Cfg.Speed / std::max(30.0, D) : 0.0;
			X.E += X.Ve * Dt; X.N += X.Vn * Dt; X.U += X.Vu * Dt;
			// temas: pilotun aracı tehdide yeterince yaklaştı (oyuncu kontrolü; otomatik bir şey yok)
			const double Dp = std::sqrt((X.E - PE) * (X.E - PE) + (X.N - PN) * (X.N - PN) + (X.U - PU) * (X.U - PU));
			if (Dp <= Cfg.ContactR)
			{
				X.bAlive = false; X.bNeutralized = true; ++Neut;
				const bool bLocked = bPlayerLock && Lock == X.Id;
				if (bLocked) ++LockedContacts;
				FGameEvent G; G.Kind = "contact"; G.I = X.Id; G.Value = bLocked ? 1.0 : 0.0; G.E = X.E; G.N = X.N; Ev.push_back(G);
				continue;
			}
			if (std::hypot(FacE - X.E, FacN - X.N) <= Cfg.HitR && X.U <= Cfg.FacU + 30.0)
			{
				X.bAlive = false; X.bImpact = true; ++Imp;
				HpNow = std::max(0, HpNow - Cfg.DamagePerHit);
				FGameEvent G; G.Kind = "impact"; G.I = X.Id; G.Value = HpNow; G.E = X.E; G.N = X.N; Ev.push_back(G);
			}
		}
		// HUD kilidi (yalnız gösterge): burnun ±LockConeDeg önündeki en yakın tehdit
		int Best = 0; double BestD = 1e18;
		for (const FThreat& X : Th)
		{
			if (!X.bAlive) continue;
			const double De = X.E - PE, Dn = X.N - PN;
			const double D = std::sqrt(De * De + Dn * Dn + (X.U - PU) * (X.U - PU));
			if (D > Cfg.LockR) continue;
			const double Brg = std::atan2(De, Dn) * 180.0 / GR_PI;
			if (std::fabs(WrapDeg(Brg - PlayerYawDeg)) > Cfg.LockConeDeg) continue;
			if (D < BestD) { BestD = D; Best = X.Id; }
		}
		if (Best != Lock)
		{
			if (Lock != 0) { FGameEvent G; G.Kind = "unlock"; G.I = Lock; Ev.push_back(G); }
			Lock = Best; LockS = 0; bPlayerLock = false;
			if (Lock != 0) { FGameEvent G; G.Kind = "lock"; G.I = Lock; G.Value = BestD; Ev.push_back(G); }
		}
		else if (Lock != 0) LockS += Dt;
		// bitiş
		int AliveNow = 0;
		for (const FThreat& X : Th) if (X.bAlive) ++AliveNow;
		if (HpNow <= 0 || (WaveNow >= Cfg.Waves && Spawned >= Total() && AliveNow == 0))
		{
			bDone = true;
			Lock = 0;
			FGameEvent G; G.Kind = "done"; G.Value = HpNow > 0 ? 1.0 : 0.0; Ev.push_back(G);
		}
	}

	int FDefenseGame::Score() const
	{
		return Neut * 100 + LockedContacts * 50 + (bDone ? HpNow * 3 : 0);
	}
}
