// SIMUMAV — halka yarışı / it dalaşı çekirdeği (bkz. SmvRace.h; kaynak: app/flightsim_match_core.py).
#include "SmvRace.h"
#include "SmvPyRandom.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace SmvCore
{
	static constexpr double PI_D = 3.14159265358979323846;
	static inline double Rad(double D) { return D * PI_D / 180.0; }
	static inline double Deg(double R) { return R * 180.0 / PI_D; }
	static inline double PyMod(double A, double B) { double M = std::fmod(A, B); if (M < 0) M += B; return M; }

	double RoundN(double V, int N)
	{
		// Python round(): ikili değerin tam ondalık açılımına göre en yakın (yarımda çift) — printf aynı kuralı uygular
		char Buf[64];
		std::snprintf(Buf, sizeof(Buf), "%.*f", N, V);
		return std::strtod(Buf, nullptr);
	}

	EVClass ClassOfType(const std::string& T)
	{
		return (T == "sabit_kanat" || T == "plane") ? EVClass::Plane : EVClass::Vertical;
	}

	FRingCfg FRingCfg::Defaults(EVClass C)
	{
		FRingCfg R;
		if (C == EVClass::Plane) { R.MinAltM = 40; R.MaxAltM = 140; R.DiameterM = 36; R.RadiusM = 700; }
		return R;
	}

	void FRingCfg::Clamp()
	{
		Count = std::min(30, std::max(1, Count));
		MinAltM = std::min(400.0, std::max(5.0, MinAltM));
		MaxAltM = std::min(400.0, std::max(5.0, MaxAltM));
		if (MinAltM > MaxAltM) std::swap(MinAltM, MaxAltM);
		DiameterM = std::min(80.0, std::max(4.0, DiameterM));
		RadiusM = std::min(3000.0, std::max(80.0, RadiusM));
		TimeLimitMin = std::min(60, std::max(2, TimeLimitMin));
		Seed = std::min(2147483647LL, std::max(1LL, Seed));
	}

	void Enu(double Lat0, double Lon0, double Lat, double Lon, double& X, double& Y)
	{
		X = Rad(Lon - Lon0) * R_EARTH * std::cos(Rad(Lat0));
		Y = Rad(Lat - Lat0) * R_EARTH;
	}

	void LatLon(double Lat0, double Lon0, double X, double Y, double& Lat, double& Lon)
	{
		Lat = Lat0 + Deg(Y / R_EARTH);
		Lon = Lon0 + Deg(X / (R_EARTH * std::cos(Rad(Lat0))));
	}

	std::vector<FRing> GenerateRings(const FRingCfg& Cfg, double CLat, double CLon, double Ground, const std::vector<FStart>& Starts)
	{
		const int N = Cfg.Count;
		FPyRandom Rng((uint64_t)(Cfg.Seed ? Cfg.Seed : 1));
		const double R = Cfg.RadiusM, Lo = Cfg.MinAltM, Hi = Cfg.MaxAltM;
		std::vector<double> Alts;
		if (N > 1) for (int k = 0; k < N; ++k) Alts.push_back(Lo + (Hi - Lo) * ((double)k / (N - 1)));
		else Alts.push_back((Lo + Hi) / 2.0);
		Rng.Shuffle(Alts);
		const double A0 = Rng.Uniform(0, 2 * PI_D);
		const int Dir = Rng.Random() < 0.5 ? 1 : -1;
		const double Step = 2 * PI_D / std::max(N, 3);
		std::vector<std::pair<double, double>> Pts;
		double A = A0;
		for (int k = 0; k < N; ++k)
		{
			const double r = R * Rng.Uniform(0.45, 1.0);
			Pts.push_back({ r * std::sin(A), r * std::cos(A) });
			A += Dir * Step * Rng.Uniform(0.7, 1.3);
		}
		double Bx = 0, By = 0;
		if (!Starts.empty())
		{
			for (const FStart& S : Starts) { Bx += S.X; By += S.Y; }
			Bx /= (double)Starts.size(); By /= (double)Starts.size();
		}
		std::vector<FRing> Out;
		double Px = Bx, Py = By;
		const double Rad2 = Cfg.DiameterM / 2.0;
		for (int k = 0; k < N; ++k)
		{
			const double x = Pts[k].first, y = Pts[k].second;
			const double Dx = x - Px, Dy = y - Py;
			double L = std::hypot(Dx, Dy);
			if (L == 0) L = 1.0;
			const double Nx = Dx / L, Ny = Dy / L;
			double Lat, Lon;
			LatLon(CLat, CLon, x, y, Lat, Lon);
			FRing G;
			G.I = k + 1;
			G.Lat = RoundN(Lat, 7); G.Lon = RoundN(Lon, 7);
			G.X = RoundN(x, 2); G.Y = RoundN(y, 2);
			G.AglM = RoundN(Alts[k], 1);
			G.AltMsl = RoundN(Ground + G.AglM, 2);
			G.YawDeg = RoundN(PyMod(Deg(std::atan2(Nx, Ny)), 360.0), 1);
			G.Nx = RoundN(Nx, 5); G.Ny = RoundN(Ny, 5);
			G.RadiusM = RoundN(Rad2, 2);
			Out.push_back(G);
			Px = x; Py = y;
		}
		return Out;
	}

	std::vector<FStart> StartPoints(int N, EVClass C, double Rwy, double Spacing, const double* Target)
	{
		N = std::max(1, N);
		std::vector<FStart> Out;
		if (C == EVClass::Plane)
		{
			const double H = Rad(Rwy);
			const double Px = std::cos(H), Py = -std::sin(H);
			for (int k = 0; k < N; ++k)
			{
				const double O = (k - (N - 1) / 2.0) * Spacing;
				Out.push_back({ RoundN(Px * O, 2), RoundN(Py * O, 2), RoundN(PyMod(Rwy, 360.0), 1) });
			}
			return Out;
		}
		if (N == 1) { Out.push_back({ 0, 0, 0 }); return Out; }
		const double r = std::max(Spacing / (2 * std::sin(PI_D / N)), Spacing / 2.0);
		for (int k = 0; k < N; ++k)
		{
			const double A = 2 * PI_D * k / N;
			const double x = r * std::sin(A), y = r * std::cos(A);
			const double Yaw = Target ? PyMod(Deg(std::atan2(Target[0] - x, Target[1] - y)), 360.0) : PyMod(Deg(A), 360.0);
			Out.push_back({ RoundN(x, 2), RoundN(y, 2), RoundN(Yaw, 1) });
		}
		return Out;
	}

	int RingIntersect(const double P0[3], const double P1[3], const FRing& H, double& OutDist)
	{
		const double Cx = H.X, Cy = H.Y, Cz = H.AltMsl;
		const double D0 = (P0[0] - Cx) * H.Nx + (P0[1] - Cy) * H.Ny;
		const double D1 = (P1[0] - Cx) * H.Nx + (P1[1] - Cy) * H.Ny;
		int Dir;
		if (D0 < 0 && 0 <= D1) Dir = 1;
		else if (D0 >= 0 && 0 > D1) Dir = -1;
		else { OutDist = -1; return 0; }
		const double T = D0 / (D0 - D1);
		const double Qx = P0[0] + (P1[0] - P0[0]) * T, Qy = P0[1] + (P1[1] - P0[1]) * T, Qz = P0[2] + (P1[2] - P0[2]) * T;
		OutDist = std::sqrt((Qx - Cx) * (Qx - Cx) + (Qy - Cy) * (Qy - Cy) + (Qz - Cz) * (Qz - Cz));
		return Dir;
	}

	std::vector<FRingEvent> FRingTracker::Update(double X, double Y, double Z, double T)
	{
		std::vector<FRingEvent> Ev;
		const double P[3] = { X, Y, Z };
		const bool bPrev = bHasLast;
		double Prev[3] = { Last[0], Last[1], Last[2] };
		Last[0] = X; Last[1] = Y; Last[2] = Z; bHasLast = true;
		if (bPrev && !Finished())
		{
			const double Jump = std::sqrt((P[0] - Prev[0]) * (P[0] - Prev[0]) + (P[1] - Prev[1]) * (P[1] - Prev[1]) + (P[2] - Prev[2]) * (P[2] - Prev[2]));
			if (Jump <= JUMP_M)
			{
				const FRing& H = Rings[Next];
				double D;
				const int Dir = RingIntersect(Prev, P, H, D);
				if (Dir == 1 && D <= H.RadiusM)
				{
					++Next;
					Ev.push_back({ "ring", H.I, T });
					if (Finished()) { FinishTime = T; Ev.push_back({ "finish", 0, T }); }
				}
				else if (Dir == -1 && D <= H.RadiusM)
				{
					Ev.push_back({ "wrong_way", H.I, T });
				}
			}
		}
		if (Finished()) Dist = 0;
		else
		{
			const FRing& H = Rings[Next];
			Dist = std::sqrt((X - H.X) * (X - H.X) + (Y - H.Y) * (Y - H.Y) + (Z - H.AltMsl) * (Z - H.AltMsl));
		}
		return Ev;
	}

	// ---- it dalaşı ----
	FDogfightCfg FDogfightCfg::Defaults(EVClass C)
	{
		FDogfightCfg D;
		D.Class = C;
		if (C == EVClass::Plane) { D.DistanceM = 1000; D.AltM = 120; D.TimeLimitMin = 6; D.RangeM = 300; D.ConeDeg = 25; D.HoldS = 3; D.AreaRadiusM = 3000; }
		return D;
	}

	void FDogfightCfg::Normalize(bool bAreaGiven)
	{
		DistanceM = std::min(1500.0, std::max(300.0, DistanceM));
		AltM = std::min(400.0, std::max(20.0, AltM));
		TimeLimitMin = std::min(10, std::max(2, TimeLimitMin));
		RangeM = std::min(400.0, std::max(50.0, RangeM));
		ConeDeg = std::min(45.0, std::max(15.0, ConeDeg));
		HoldS = std::min(5.0, std::max(2.0, HoldS));
		if (!bAreaGiven) AreaRadiusM = std::max(AreaRadiusM, std::min(5000.0, DistanceM / 2 + 400.0));
		AreaRadiusM = std::min(5000.0, std::max(300.0, AreaRadiusM));
		if (AreaRadiusM < DistanceM / 2 + 150.0) AreaRadiusM = DistanceM / 2 + 150.0;
	}

	void DogfightStarts(const FDogfightCfg& Cfg, double Rwy, FStart& Chaser, FStart& Evader)
	{
		double Yaw;
		if (Cfg.Class == EVClass::Plane) Yaw = PyMod(Rwy, 360.0);
		else
		{
			FPyRandom R((uint64_t)((Cfg.Seed ? Cfg.Seed : 1) * 7 + 3));
			Yaw = RoundN(R.Uniform(0, 360), 1);
		}
		const double H = Rad(Yaw), Fx = std::sin(H), Fy = std::cos(H), D = Cfg.DistanceM / 2.0;
		Chaser = { RoundN(-Fx * D, 2), RoundN(-Fy * D, 2), Yaw };
		Evader = { RoundN(Fx * D, 2), RoundN(Fy * D, 2), Yaw };
	}

	bool HostIsChaser(const FDogfightCfg& Cfg)
	{
		if (Cfg.Roles == "host_chaser") return true;
		if (Cfg.Roles == "host_evader") return false;
		FPyRandom R((uint64_t)(Cfg.Seed ? Cfg.Seed : 1));
		return R.Random() < 0.5;
	}

	std::vector<FDogfightEvent> FDogfight::Update(int Role, double X, double Y, double Z, double Heading, double Pitch, double T)
	{
		std::vector<FDogfightEvent> Ev;
		if (bDone) return Ev;
		double* Pp = Pose[Role];
		Pp[0] = X; Pp[1] = Y; Pp[2] = Z; Pp[3] = Heading; Pp[4] = Pitch; Pp[5] = T;
		bHave[Role] = true;
		if (std::hypot(X, Y) > Cfg.AreaRadiusM)
		{
			bDone = true;
			FDogfightEvent E; E.Kind = "area"; E.Role = Role;
			Ev.push_back(E);
			return Ev;
		}
		if (!bHave[Chaser] || !bHave[Evader]) return Ev;
		const double* C = Pose[Chaser];
		const double* E = Pose[Evader];
		if (std::fabs(C[5] - E[5]) > 2.0) return Ev;
		const double D[3] = { E[0] - C[0], E[1] - C[1], E[2] - C[2] };
		const double Dist = std::sqrt(D[0] * D[0] + D[1] * D[1] + D[2] * D[2]);
		const double H = Rad(C[3]);
		const double Pr = Cfg.Class == EVClass::Plane ? C[4] : 0.0;
		const double Nose[3] = { std::sin(H) * std::cos(Pr), std::cos(H) * std::cos(Pr), std::sin(Pr) };
		double Off = 0;
		if (Dist >= 1e-6)
		{
			double Cs = (Nose[0] * D[0] + Nose[1] * D[1] + Nose[2] * D[2]) / Dist;
			Cs = std::max(-1.0, std::min(1.0, Cs));
			Off = Deg(std::acos(Cs));
		}
		LDist = Dist; LOff = Off; LBearing = PyMod(Deg(std::atan2(D[0], D[1])), 360.0);
		if (bGuard)
		{
			const double Th = Cfg.AltM * 0.8;
			if ((C[2] >= Th && E[2] >= Th) || T - Time0 >= 60.0)
			{
				bGuard = false;
				FDogfightEvent G; G.Kind = "guard_off"; Ev.push_back(G);
			}
		}
		if (MinDist < 0 || Dist < MinDist) MinDist = Dist;
		const double Dt = LastT < 0 ? 0.0 : std::max(0.0, std::min(0.5, T - LastT));
		LastT = LastT < 0 ? T : std::max(LastT, T);
		const bool bIn = !bGuard && Dist <= Cfg.RangeM && Off <= Cfg.ConeDeg;
		if (bIn)
		{
			if (bInCone) { Hold += Dt; ConeTotal += Dt; }
			else { FDogfightEvent L; L.Kind = "lock_on"; Ev.push_back(L); }
		}
		else
		{
			if (bInCone) { FDogfightEvent L; L.Kind = "lock_off"; L.HeldS = RoundN(Hold, 2); Ev.push_back(L); }
			Hold = 0;
		}
		bInCone = bIn;
		MaxHold = std::max(MaxHold, Hold);
		if (bIn && Hold >= Cfg.HoldS)
		{
			bDone = true;
			FDogfightEvent G; G.Kind = "engage"; G.HeldS = RoundN(Hold, 2); G.DistM = RoundN(Dist, 1); G.OffDeg = RoundN(Off, 1);
			Ev.push_back(G);
		}
		return Ev;
	}
}
