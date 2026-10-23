// SIMUMAV — hedefler / angajman / servo tetik (bkz. SmvTargets.h).
#include "SmvTargets.h"
#include "SmvRace.h"   // RoundN
#include <algorithm>
#include <cmath>

namespace SmvCore
{
	static constexpr double PI_T = 3.14159265358979323846;
	static constexpr double G_T = 9.80665;

	FTargetState TargetAt(const FTargetCfg& H, double T)
	{
		FTargetState S;
		const double R = H.DiameterM / 2.0, V = H.SpeedMs, W = V / R;
		const double Sg = H.bCw ? -1.0 : 1.0;
		const double Th = H.PhaseDeg * PI_T / 180.0 + Sg * W * T;
		S.X = H.EastM + R * std::cos(Th);
		S.Y = H.NorthM + R * std::sin(Th);
		S.Vx = -R * std::sin(Th) * Sg * W;
		S.Vy = R * std::cos(Th) * Sg * W;
		double Hd = std::fmod(std::atan2(S.Vx, S.Vy) * 180.0 / PI_T, 360.0);
		if (Hd < 0) Hd += 360.0;
		S.HeadingDeg = Hd;
		S.YawRad = Hd * PI_T / 180.0;
		S.RollRad = std::atan(V * V / (R * G_T)) * (Sg < 0 ? 1.0 : -1.0);
		S.Z = H.AltM;
		S.Speed = V;
		return S;
	}

	double DefaultPhase(int I0) { return std::fmod(I0 * 137.5, 360.0); }

	void TargetLocal(double Lat0, double Lon0, double Lat, double Lon, double& X, double& Y)
	{
		X = (Lon - Lon0) * TGT_M_LON * std::cos(Lat0 * PI_T / 180.0);
		Y = (Lat - Lat0) * TGT_M_LAT;
	}

	void TargetLatLon(double Lat0, double Lon0, double X, double Y, double& Lat, double& Lon)
	{
		Lat = Lat0 + Y / TGT_M_LAT;
		Lon = Lon0 + X / (TGT_M_LON * std::cos(Lat0 * PI_T / 180.0));
	}

	FTargetTracker::FTargetTracker(const std::vector<FTargetCfg>& Items, double RadiusM) : Cfg(Items), R(RadiusM)
	{
		for (const FTargetCfg& H : Cfg)
		{
			FTargetTrack T; T.I = H.I; T.Name = H.Name; T.Model = H.Model;
			Iz.push_back(T);
		}
	}

	double FTargetTracker::DistTo(int I, double T) const
	{
		for (const FTargetCfg& H : Cfg)
		{
			if (H.I != I || !bHasPos) continue;
			const FTargetState S = TargetAt(H, T);
			return std::sqrt((S.X - Pos[0]) * (S.X - Pos[0]) + (S.Y - Pos[1]) * (S.Y - Pos[1]) + (S.Z - Pos[2]) * (S.Z - Pos[2]));
		}
		return -1;
	}

	FTargetEvent FTargetTracker::Finish(const std::string& Result, const std::string& Reason, double Dist)
	{
		const double T = TLast >= 0 ? TLast : Ang.T0;
		FEngagement E = Ang;
		E.DurS = RoundN(std::max(0.0, T - Ang.T0), 1);
		E.Result = Result; E.Reason = Reason;
		if (Dist >= 0) E.EndDistM = RoundN(Dist, 1);
		Done.push_back(E);
		if (Done.size() > 200) Done.erase(Done.begin());
		bAng = false;
		FTargetEvent Ev;
		Ev.Kind = Result == "success" ? "success" : "cut";
		Ev.I = E.I; Ev.Reason = Reason; Ev.DistM = E.EndDistM;
		return Ev;
	}

	std::vector<FTargetEvent> FTargetTracker::Lock(int I)
	{
		std::vector<FTargetEvent> Out;
		const FTargetCfg* H = nullptr;
		for (const FTargetCfg& X : Cfg) if (X.I == I) H = &X;
		if (!H) return Out;
		if (bAng)
		{
			if (Ang.I == I) return Out;
			Out.push_back(Finish("cut", "switched"));
		}
		const double T = TLast >= 0 ? TLast : 0.0;
		double D = DistTo(I, T);
		D = D >= 0 ? RoundN(D, 1) : -1;
		Ang = FEngagement();
		Ang.I = I; Ang.Name = H->Name; Ang.T0 = RoundN(T, 3);
		Ang.StartS = RoundN(T - (TArm >= 0 ? TArm : T), 1);
		Ang.StartDistM = D; Ang.MinDistM = D;
		bAng = true;
		FTargetEvent Ev; Ev.Kind = "start"; Ev.I = I; Ev.DistM = D;
		Out.push_back(Ev);
		return Out;
	}

	std::vector<FTargetEvent> FTargetTracker::Release(const std::string& Reason)
	{
		std::vector<FTargetEvent> Out;
		if (!bAng) return Out;
		const bool Known = Reason == "released" || Reason == "lost" || Reason == "switched" || Reason == "reset" || Reason == "end";
		Out.push_back(Finish("cut", Known ? Reason : "released"));
		return Out;
	}

	std::vector<FTargetEvent> FTargetTracker::Step(double T, double X, double Y, double Z, bool bArmed)
	{
		std::vector<FTargetEvent> Out;
		const double Dt = (TLast < 0 || !(0 < T - TLast && T - TLast < 5)) ? 0.0 : T - TLast;
		if (TLast >= 0 && T < TLast - 1)
		{
			TArm = -1;
			for (FTargetTrack& D : Iz) D.bInside = false;
			if (bAng) Out.push_back(Finish("cut", "reset"));
		}
		TLast = T;
		Pos[0] = X; Pos[1] = Y; Pos[2] = Z; bHasPos = true;
		if (bArmed && TArm < 0) TArm = T;
		for (size_t k = 0; k < Cfg.size(); ++k)
		{
			const FTargetCfg& H = Cfg[k];
			FTargetTrack& D = Iz[k];
			const FTargetState S = TargetAt(H, T);
			const double Dist = std::sqrt((S.X - X) * (S.X - X) + (S.Y - Y) * (S.Y - Y) + (S.Z - Z) * (S.Z - Z));
			if (D.MinDistM < 0 || Dist < D.MinDistM) D.MinDistM = RoundN(Dist, 1);
			if (D.bInside)
			{
				D.InS += Dt;
				if (Dist > R * 1.5) D.bInside = false;
			}
			else if (Dist <= R && bArmed)
			{
				D.bInside = true;
				D.Intercepts++;
				const bool First = D.FirstS < 0;
				if (First) D.FirstS = RoundN(T - (TArm >= 0 ? TArm : T), 1);
				FTargetEvent E;
				E.Kind = "intercept"; E.I = H.I; E.DistM = RoundN(Dist, 1); E.N = D.Intercepts; E.bFirst = First;
				E.AfterS = First ? D.FirstS : RoundN(T - (TArm >= 0 ? TArm : T), 1);
				if (bAng && Ang.I == H.I)
				{
					const double Rd = RoundN(Dist, 1);
					Ang.MinDistM = Ang.MinDistM < 0 ? Rd : std::min(Ang.MinDistM, Rd);
					E.bLocked = true;
					Out.push_back(E);
					Out.push_back(Finish("success", "intercept", Dist));
					D.LockedHits++;
				}
				else Out.push_back(E);
			}
			if (bAng && Ang.I == H.I)
			{
				const double Rd = RoundN(Dist, 1);
				if (Ang.MinDistM < 0 || Rd < Ang.MinDistM) Ang.MinDistM = Rd;
			}
		}
		return Out;
	}

	int FTargetTracker::Score(std::vector<std::string>& Badges) const
	{
		int P = 0;
		Badges.clear();
		double MinFirst = -1;
		int Total = 0, Hit = 0;
		bool bClose = false;
		for (const FTargetTrack& D : Iz)
		{
			Total += D.Intercepts;
			if (D.FirstS >= 0) MinFirst = MinFirst < 0 ? D.FirstS : std::min(MinFirst, D.FirstS);
			if (D.Intercepts)
			{
				++Hit;
				P += std::max(200, (int)(1000 - 5 * std::max(0.0, D.FirstS)));
				P += 100 * (D.Intercepts - 1);
				if (D.MinDistM >= 0 && D.MinDistM <= 5) { P += 150; bClose = true; }
				if (D.LockedHits) P += 250;
			}
		}
		if (MinFirst >= 0) { Badges.push_back("first"); if (MinFirst <= 60) Badges.push_back("quick"); }
		if (!Iz.empty() && Hit == (int)Iz.size() && Iz.size() > 1) Badges.push_back("ace");
		if (bClose) Badges.push_back("close");
		if (Total >= 5) Badges.push_back("hunter");
		return P;
	}

	// ---- servo tetik ----
	void FServoTrigger::Configure(const FServoTrigCfg& In)
	{
		C = In;
		C.Ch = std::min(16, std::max(1, C.Ch));
		C.Pwm = std::min(2200, std::max(800, C.Pwm));
		C.Tol = std::min(500, std::max(1, C.Tol));
		C.DelayMs = std::min(60000, std::max(0, C.DelayMs));
		if (C.Cmp != "ge" && C.Cmp != "le" && C.Cmp != "eq") C.Cmp = "ge";
		St = C.bOn ? EState::Listening : EState::Idle;
		bBase = false; bPrevMatch = false;
	}

	bool FServoTrigger::Matches(int V) const
	{
		if (V <= 0 || V >= 65535) return false;
		if (C.Cmp == "ge") return V >= C.Pwm;
		if (C.Cmp == "le") return V <= C.Pwm;
		return std::abs(V - C.Pwm) <= C.Tol;
	}

	std::string FServoTrigger::Sample(const int Servo16[16], long long Tbm, double Wall)
	{
		if (St == EState::Idle || St == EState::Fired) return std::string();
		if (St == EState::Armed)
		{
			if (Tbm - TrigTbm >= C.DelayMs) { St = EState::Fired; return "fired"; }
			return std::string();
		}
		const bool M = Matches(Servo16[C.Ch - 1]);
		if (!bBase)
		{
			bBase = true; bPrevMatch = M;
			if (M) { St = EState::AlreadyInCondition; return "already"; }
			return std::string();
		}
		const bool bEdge = M && !bPrevMatch;
		bPrevMatch = M;
		if (!M && St == EState::AlreadyInCondition) St = EState::Listening;
		if (bEdge)
		{
			TrigTbm = Tbm; TrigWall = Wall;
			if (C.DelayMs <= 0) { St = EState::Fired; return "fired"; }
			St = EState::Armed;
			return "triggered";
		}
		return std::string();
	}

	std::string FServoTrigger::Watchdog(double Wall)
	{
		if (St == EState::Armed && Wall - TrigWall >= C.DelayMs / 1000.0 * 2 + 5) { St = EState::Fired; return "fired"; }
		return std::string();
	}
}
