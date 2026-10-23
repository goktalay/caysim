// SIMUMAV çekirdek (Source/Simumav/Private/Core) sınama sürücüsü — Unreal'sız. stdin satır komutları, stdout sonuç satırları.
// Python tarafı: tests/cpp/test_core.py (sunucu Python modülleri ve JS değerlendiricisiyle karşılaştırır).
#include "../../Source/Simumav/Private/Core/SmvPyRandom.h"
#include "../../Source/Simumav/Private/Core/SmvRace.h"
#include "../../Source/Simumav/Private/Core/SmvPower.h"
#include "../../Source/Simumav/Private/Core/SmvGpws.h"
#include "../../Source/Simumav/Private/Core/SmvTargets.h"
#include "../../Source/Simumav/Private/Core/SmvSitlConfig.h"
#include "../../Source/Simumav/Private/Core/SmvBot.h"
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>

using namespace SmvCore;

static double Opt(const std::string& S) { return S == "null" ? NaN : std::stod(S); }
static void P(const char* Fmt, double V) { if (Has(V)) std::printf(Fmt, V); else std::printf(" null"); }

int main()
{
	std::string Line;
	while (std::getline(std::cin, Line))
	{
		std::istringstream In(Line);
		std::string Cmd;
		In >> Cmd;
		if (Cmd == "RAND")
		{
			unsigned long long Seed; int N; In >> Seed >> N;
			FPyRandom R(Seed);
			std::printf("RAND");
			for (int i = 0; i < N; ++i) std::printf(" %.17g", R.Random());
			std::printf("\n");
		}
		else if (Cmd == "SHUF")
		{
			unsigned long long Seed; int N; In >> Seed >> N;
			FPyRandom R(Seed);
			std::vector<int> V; for (int i = 0; i < N; ++i) V.push_back(i);
			R.Shuffle(V);
			std::printf("SHUF"); for (int X : V) std::printf(" %d", X); std::printf("\n");
		}
		else if (Cmd == "RINGS")
		{
			FRingCfg C; double La, Lo, G; int Ns;
			In >> C.Count >> C.MinAltM >> C.MaxAltM >> C.DiameterM >> C.RadiusM >> C.Seed >> La >> Lo >> G >> Ns;
			std::vector<FStart> St(Ns);
			for (FStart& S : St) In >> S.X >> S.Y;
			for (const FRing& R : GenerateRings(C, La, Lo, G, St))
				std::printf("RING %d %.7f %.7f %.2f %.2f %.1f %.2f %.1f %.5f %.5f %.2f\n", R.I, R.Lat, R.Lon, R.X, R.Y, R.AglM, R.AltMsl, R.YawDeg, R.Nx, R.Ny, R.RadiusM);
			std::printf("END\n");
		}
		else if (Cmd == "STARTS")
		{
			int N; std::string Cl; double Rwy; int HasT; double Tx = 0, Ty = 0;
			In >> N >> Cl >> Rwy >> HasT;
			if (HasT) In >> Tx >> Ty;
			const double T[2] = { Tx, Ty };
			for (const FStart& S : StartPoints(N, Cl == "ucak" ? EVClass::Plane : EVClass::Vertical, Rwy, START_SPACING_M, HasT ? T : nullptr))
				std::printf("ST %.2f %.2f %.1f\n", S.X, S.Y, S.YawDeg);
			std::printf("END\n");
		}
		else if (Cmd == "DFSTART")
		{
			FDogfightCfg C; std::string Cl; double Rwy;
			In >> C.DistanceM >> Cl >> C.Seed >> Rwy >> C.Roles;
			C.Class = Cl == "ucak" ? EVClass::Plane : EVClass::Vertical;
			FStart A, B; DogfightStarts(C, Rwy, A, B);
			std::printf("DF %.2f %.2f %.17g %.2f %.2f %.17g %d\n", A.X, A.Y, A.YawDeg, B.X, B.Y, B.YawDeg, HostIsChaser(C) ? 1 : 0);
		}
		else if (Cmd == "TRACK")
		{
			int Nr; In >> Nr;
			std::vector<FRing> Rs(Nr);
			for (FRing& R : Rs) In >> R.I >> R.X >> R.Y >> R.AltMsl >> R.Nx >> R.Ny >> R.RadiusM;
			FRingTracker Tr(Rs);
			int Np; In >> Np;
			for (int k = 0; k < Np; ++k)
			{
				double X, Y, Z, T; In >> X >> Y >> Z >> T;
				for (const FRingEvent& E : Tr.Update(X, Y, Z, T)) std::printf("EV %d %s %d %.3f\n", k, E.Kind.c_str(), E.I, E.T);
				std::printf("D %d %.6f\n", k, Tr.DistNext());
			}
			std::printf("END\n");
		}
		else if (Cmd == "DOGF")
		{
			FDogfightCfg C; std::string Cl; double T0; int Np;
			In >> Cl >> C.AltM >> C.RangeM >> C.ConeDeg >> C.HoldS >> C.AreaRadiusM >> T0 >> Np;
			C.Class = Cl == "ucak" ? EVClass::Plane : EVClass::Vertical;
			FDogfight D(C, T0);
			for (int k = 0; k < Np; ++k)
			{
				int Role; double X, Y, Z, H, Pi, T; In >> Role >> X >> Y >> Z >> H >> Pi >> T;
				for (const FDogfightEvent& E : D.Update(Role, X, Y, Z, H, Pi, T)) std::printf("EV %d %s %.2f\n", k, E.Kind.c_str(), E.HeldS);
			}
			std::printf("DS %.2f %.6f\nEND\n", D.MaxHoldS(), D.MinDistM());
		}
		else if (Cmd == "POWER")
		{
			FPowerCfg C; int N;
			In >> C.MaxSpeedMs >> C.FuelL >> C.FuelKmPerL >> C.ElecCells >> C.ElecMah >> C.ElecW >> N;
			FPowerState S(C);
			for (int k = 0; k < N; ++k)
			{
				int Ar; double Gs, Thr; long long Tbm; In >> Ar >> Gs >> Thr >> Tbm;
				std::vector<int> Sv(8); for (int& V : Sv) In >> V;
				for (const std::string& B : S.Step(Ar != 0, Gs, Thr, Sv, Tbm)) std::printf("EMPTY %d %s\n", k, B.c_str());
				const FPowerTele T = S.Tele();
				std::printf("PT %d", k);
				if (T.bFuelOn) std::printf(" %.3f %.1f %.4f %.2f %d", T.FuelL, T.FuelPct, T.FuelLpm, T.RangeKm, T.bFuelEmpty ? 1 : 0); else std::printf(" nofuel");
				if (T.bElecOn) { std::printf(" %.2f %.1f %.1f", T.Wh, T.ElecPct, T.W); P(" %.1f", T.MinLeft < 0 ? NaN : T.MinLeft); std::printf(" %.2f %d", T.V, T.bElecEmpty ? 1 : 0); }
				else std::printf(" noelec");
				std::printf("\n");
			}
			std::printf("PS %.1f %.1f %.1f %.1f %.3f", S.DistM, S.TimeS, S.ArmedS, S.MaxSpeedSeen, S.FuelUsed);
			P(" %.2f", MeasuredKmPerL(S.DistM, S.FuelUsed) < 0 ? NaN : MeasuredKmPerL(S.DistM, S.FuelUsed));
			P(" %.1f", AverageW(S.WhUsed, S.TimeS) < 0 ? NaN : AverageW(S.WhUsed, S.TimeS));
			std::printf("\nEND\n");
		}
		else if (Cmd == "GSUM")
		{
			std::string Mode; int N; In >> Mode >> N;
			std::vector<FGpwsEpisode> L(N);
			for (FGpwsEpisode& E : L) In >> E.Code >> E.T0 >> E.StartS >> E.DurS;
			const FGpwsSummary S = SummarizeAlerts(L, Mode);
			std::printf("GS %d %s %d %d %.2f\n", S.Score, S.Grade.empty() ? "null" : S.Grade.c_str(), S.Warnings, S.Cautions, S.TotalS);
			for (auto& Kv : S.ByCode) std::printf("BC %s %d %.2f %.2f\n", Kv.first.c_str(), Kv.second.N, Kv.second.TotalS, Kv.second.FirstS);
			std::printf("END\n");
		}
		else if (Cmd == "GPWS")
		{
			std::string Veh, Sens, Mode, MaxS, Fence; int Bc, Bp, N;
			In >> Veh >> Mode >> Sens >> Bc >> Bp >> MaxS >> Fence >> N;
			FGpwsCfg C; C.Mode = Mode; C.Sens = Sens; C.BankCopter = Bc; C.BankPlane = Bp;
			FGpws G(Veh, C, Opt(MaxS), Opt(Fence));
			for (int k = 0; k < N; ++k)
			{
				std::string Line2;
				std::getline(std::cin, Line2);
				std::istringstream F(Line2);
				FGpwsInput I; std::string Tok[28];
				for (std::string& T : Tok) F >> T;
				I.T = std::stod(Tok[0]); I.bArmed = Tok[1] == "1"; I.Mode = Tok[2] == "null" ? "" : Tok[2];
				I.Agl = Opt(Tok[3]); I.AglAhead = Opt(Tok[4]); I.Vs = std::stod(Tok[5]); I.Gs = Opt(Tok[6]); I.As = Opt(Tok[7]);
				I.Roll = Opt(Tok[8]); I.Pitch = Opt(Tok[9]);
				I.bRates = Tok[10] != "null"; if (I.bRates) { I.P = std::stod(Tok[10]); I.Q = std::stod(Tok[11]); I.R = std::stod(Tok[12]); }
				I.BattPct = Opt(Tok[13]); I.bBattEmpty = Tok[14] == "1"; I.FuelPct = Opt(Tok[15]); I.bFuelEmpty = Tok[16] == "1";
				I.GpsFix = Opt(Tok[17]); I.Sats = Opt(Tok[18]); I.SensBad = std::stod(Tok[19]); I.Ekf = Opt(Tok[20]); I.Vibe = Opt(Tok[21]);
				I.Wind = Opt(Tok[22]); I.bMotorCut = Tok[23] == "1"; I.Phase = Tok[24] == "null" ? "" : Tok[24];
				I.DistHome = Opt(Tok[25]); I.LinkAgeMs = Opt(Tok[26]); I.bCrashed = Tok[27] == "1";
				const FGpwsStepResult R = G.Step(I);
				std::printf("A %d", k); for (const FGpwsActive& A : R.Active) std::printf(" %s", A.Code.c_str()); std::printf("\n");
			}
			G.Close(NaN);
			for (const FGpwsEpisode& E : G.Episodes()) std::printf("EP %s %.2f %.2f %.2f\n", E.Code.c_str(), E.T0, E.StartS, E.DurS);
			std::printf("END\n");
		}
		else if (Cmd == "TGT")
		{
			int N; double Rad; In >> N >> Rad;
			std::vector<FTargetCfg> Cs(N);
			for (FTargetCfg& C : Cs) { int Cw; In >> C.I >> C.AltM >> C.DiameterM >> C.SpeedMs >> C.EastM >> C.NorthM >> C.PhaseDeg >> Cw; C.bCw = Cw != 0; C.Name = "T" + std::to_string(C.I); C.Model = "m"; }
			FTargetTracker Tr(Cs, Rad);
			int Np; In >> Np;
			for (int k = 0; k < Np; ++k)
			{
				std::string Op; In >> Op;
				if (Op == "L") { int I; In >> I; for (auto& E : Tr.Lock(I)) std::printf("EV %d %s %d\n", k, E.Kind.c_str(), E.I); continue; }
				if (Op == "R") { std::string Rs; In >> Rs; for (auto& E : Tr.Release(Rs)) std::printf("EV %d %s %d %s\n", k, E.Kind.c_str(), E.I, E.Reason.c_str()); continue; }
				double T, X, Y, Z; int Ar; In >> T >> X >> Y >> Z >> Ar;   // Op == "S"
				for (auto& E : Tr.Step(T, X, Y, Z, Ar != 0))
				{
					if (E.Kind == "intercept") std::printf("EV %d %s %d %.1f %d\n", k, E.Kind.c_str(), E.I, E.DistM, E.N);
					else if (E.Kind == "cut") std::printf("EV %d cut %d %s\n", k, E.I, E.Reason.c_str());
					else std::printf("EV %d %s %d\n", k, E.Kind.c_str(), E.I);
				}
			}
			for (auto& D : Tr.Tracks()) std::printf("TK %d %d %.1f %.1f\n", D.I, D.Intercepts, D.FirstS, D.MinDistM);
			std::vector<std::string> Bd; const int Sc = Tr.Score(Bd);
			std::printf("SC %d", Sc); for (auto& B : Bd) std::printf(" %s", B.c_str()); std::printf("\n");
			for (const FTargetCfg& C : Cs) { const FTargetState S = TargetAt(C, 12.5); std::printf("TS %d %.6f %.6f %.6f %.6f\n", C.I, S.X, S.Y, S.HeadingDeg, S.RollRad); }
			std::printf("END\n");
		}
		else if (Cmd == "PARM")
		{
			FSitlSession S; int Nu, Bin; In >> S.SitlType >> S.Airframe >> S.Drive >> S.SysId >> Bin >> Nu; S.bBinLog = Bin != 0;
			for (int i = 0; i < Nu; ++i) { std::string N; double V; In >> N >> V; S.UserParams.push_back({ N, V }); }
			std::istringstream T(DefaultsParmText(S));
			std::string L2; while (std::getline(T, L2)) if (!L2.empty() && L2[0] != '#') std::printf("PL %s\n", L2.c_str());
			std::printf("END\n");
		}
		else if (Cmd == "LAUNCH")
		{
			FSitlLaunch L; In >> L.Method >> L.AngleDeg >> L.ReleaseKmh >> L.TakeoffAltM;
			for (auto& Pp : LaunchParams(L)) std::printf("LP %s %.6f\n", Pp.first.c_str(), Pp.second);
			std::printf("END\n");
		}
		else if (Cmd == "GUST")
		{
			double T; In >> T; std::printf("GU %.4f\n", GustWindSpeed(0, 1, T));
		}
		else if (Cmd == "CMDLINE")
		{
			FSitlSession S; In >> S.SitlType >> S.Airframe >> S.Launch.Method >> S.HomeLat >> S.HomeLon >> S.HomeAlt >> S.HomeHeading >> S.BasePort;
			std::printf("CL"); for (auto& A : CommandLine(S, "D")) std::printf(" %s", A.c_str()); std::printf("\n");
		}
		else if (Cmd == "BOT")
		{
			// Bot halka parkurunu tamamlayabiliyor mu (duman sınaması)
			std::string Lv, Cl; In >> Lv >> Cl;
			const EVClass C = Cl == "ucak" ? EVClass::Plane : EVClass::Vertical;
			FRingCfg Rc = FRingCfg::Defaults(C); Rc.Seed = 4242;
			auto Rs = GenerateRings(Rc, 39.9, 32.8, 0, { { 0, 0 } });
			FRingTracker Tr(Rs);
			FBotPilot B(Lv, C, 7);
			B.Place(0, 0, 0, 0);
			double T = 0;
			for (; T < 900 && !Tr.Finished(); T += 0.1)
			{
				B.StepRings(0.1, Rs, Tr.NextIndex());
				Tr.Update(B.Pose().X, B.Pose().Y, B.Pose().Z, T);
			}
			std::printf("BOT %s %s %d/%d %.1f\n", Lv.c_str(), Cl.c_str(), Tr.Passed(), (int)Rs.size(), T);
		}
	}
	return 0;
}
