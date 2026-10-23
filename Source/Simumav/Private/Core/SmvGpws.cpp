// SIMUMAV — §42 uçuş uyarıları (bkz. SmvGpws.h). JS kaynağıyla satır satır eşleşecek biçimde yazıldı; değişiklikte iki tarafı birlikte güncelleyin.
#include "SmvGpws.h"
#include <algorithm>
#include <cctype>
#include <cmath>

namespace SmvCore
{
	static const std::vector<FGpwsCodeDef> GCodes = {
		{ "PULL_UP", "warning", 100, "PULL UP", "Pull up", "YUKARI ÇEK", false, false },
		{ "TERRAIN", "warning", 95, "TERRAIN", "Terrain, terrain", "ARAZİ", false, false },
		{ "STALL", "warning", 92, "STALL", "Stall, stall", "PERDÖVİTES", false, true },
		{ "IMU_FAULT", "warning", 90, "GYRO / IMU FAULT", "Gyro fault", "JİROSKOP / IMU ARIZASI", true, false },
		{ "ENGINE", "warning", 88, "ENGINE OUT", "Engine out", "MOTOR DURDU", false, false },
		{ "OVERSPEED", "warning", 85, "OVERSPEED", "Overspeed", "AŞIRI HIZ", false, false },
		{ "BANK_ANGLE", "warning", 80, "BANK ANGLE", "Bank angle, bank angle", "YATIŞ AÇISI", false, false },
		{ "PITCH", "warning", 78, "PITCH", "Pitch, pitch", "YUNUSLAMA AÇISI", false, false },
		{ "SINK_RATE", "warning", 75, "SINK RATE", "Sink rate", "ALÇALMA HIZI", false, false },
		{ "BATT_CRIT", "warning", 70, "BATTERY CRITICAL", "Battery critical", "BATARYA KRİTİK", true, false },
		{ "FUEL", "warning", 69, "FUEL", "Fuel, fuel", "YAKIT KRİTİK", true, false },
		{ "GPS_LOST", "warning", 65, "GPS LOST", "G P S lost", "GPS KAYBI", true, false },
		{ "GEOFENCE", "warning", 60, "GEOFENCE", "Geofence", "SANAL ÇİT", true, false },
		{ "TOO_LOW", "caution", 50, "TOO LOW", "Too low, terrain", "ÇOK ALÇAK", false, false },
		{ "DONT_SINK", "caution", 48, "DON'T SINK", "Don't sink", "İRTİFA KAYBI", false, false },
		{ "EKF", "caution", 45, "EKF VARIANCE", "E K F variance", "EKF SAPMASI", true, false },
		{ "VIBRATION", "caution", 42, "VIBRATION", "Vibration", "TİTREŞİM", true, false },
		{ "BATT_LOW", "caution", 40, "BATTERY LOW", "Battery low", "BATARYA DÜŞÜK", true, false },
		{ "FUEL_LOW", "caution", 39, "FUEL LOW", "Fuel low", "YAKIT DÜŞÜK", true, false },
		{ "WIND", "caution", 35, "HIGH WIND", "High wind", "KUVVETLİ RÜZGÂR", false, false },
		{ "LINK", "caution", 30, "LINK LOST", "Link lost", "BAĞLANTI KAYBI", true, false },
	};

	const std::vector<FGpwsCodeDef>& GpwsCodes() { return GCodes; }
	const FGpwsCodeDef* GpwsCode(const std::string& Code)
	{
		for (const FGpwsCodeDef& D : GCodes) if (Code == D.Code) return &D;
		return nullptr;
	}

	static double Round2(double V) { return std::floor(V * 100.0 + 0.5) / 100.0; }   // JS Math.round(x*100)/100 (pozitif/negatif yarım yukarı)
	static double Wrap180(double D) { double M = std::fmod(std::fmod(D, 360.0) + 540.0, 360.0); return M - 180.0; }

	void FGpwsCfg::Normalize()
	{
		if (Mode != "off" && Mode != "visual" && Mode != "full") Mode = "full";
		if (Lang != "aviation" && Lang != "ui") Lang = "aviation";
		if (Sens != "low" && Sens != "normal" && Sens != "high") Sens = "normal";
		BankCopter = std::min(60, std::max(20, BankCopter));
		BankPlane = std::min(70, std::max(30, BankPlane));
		std::set<std::string> O;
		for (const std::string& K : Off) if (GpwsCode(K)) O.insert(K);
		Off = O;
	}

	std::string GpwsVehicleClass(const std::string& TypeIn)
	{
		std::string T = TypeIn;
		for (char& Ch : T) Ch = (char)std::tolower((unsigned char)Ch);
		if (T == "rover" || T.rfind("rover", 0) == 0) return "rover";
		if (T == "interceptor") return "icept";
		if (T == "vtol") return "vtol";
		if (T == "sabit_kanat" || T == "plane" || T == "delta" || T == "caymaz") return "plane";
		if (T == "helikopter" || T == "heli") return "heli";
		return "copter";
	}

	static std::string ModeKind(const std::string& Vc, const std::string& Mode)
	{
		if (Vc == "rover") return "rover";
		if (Vc == "plane") return "plane";
		if (Vc == "vtol" || Vc == "icept") return (!Mode.empty() && Mode[0] == 'Q') ? "copter" : "plane";
		return "copter";
	}

	static bool CodeApplies(const std::string& Code, const std::string& Vc)
	{
		const FGpwsCodeDef* D = GpwsCode(Code);
		if (!D) return false;
		if (Vc == "rover") return D->bRover;
		if (D->bPlaneOnly) return Vc == "plane" || Vc == "vtol" || Vc == "icept";
		return true;
	}

	struct FThresh
	{
		double AglMax = NaN, SinkMin = NaN, SinkHard = NaN, TPull = NaN, TTerrain = NaN, Look = NaN, PitchUp = NaN, PitchDn = NaN, Vne = NaN, Stall = NaN,
			Grace = NaN, AirAgl = NaN, TooLowAgl = NaN, TooLowGs = NaN, TooLowPeak = NaN, DsAgl = NaN, DsLoss = NaN, Wind = NaN, FinalAgl = NaN, FinalDist = NaN,
			Bank = NaN, Fence = NaN;
	};

	static FThresh Thresholds(const std::string& Vc, const std::string& Kind, const FGpwsCfg& Cfg, double MaxSpeed)
	{
		FThresh P;
		if (Kind == "plane")
		{
			P.AglMax = 300; P.SinkMin = 4; P.SinkHard = 12; P.TPull = 5; P.TTerrain = 10; P.Look = 8; P.PitchUp = 30; P.PitchDn = 25; P.Vne = 38; P.Stall = 9;
			P.Grace = 8; P.AirAgl = 5; P.TooLowAgl = 30; P.TooLowGs = 12; P.TooLowPeak = 60; P.DsAgl = 200; P.DsLoss = 15; P.Wind = 15; P.FinalAgl = 60; P.FinalDist = 400;
		}
		else if (Kind == "rover")
		{
			P.Wind = 99;
			return P;
		}
		else
		{
			P.AglMax = 80; P.SinkMin = 2.5; P.SinkHard = 6; P.TPull = 3.5; P.TTerrain = 7; P.Look = 4; P.PitchUp = 35; P.PitchDn = 35; P.Vne = 22;
			P.Grace = 3; P.AirAgl = 1.5; P.TooLowAgl = 4; P.TooLowGs = 8; P.TooLowPeak = 15; P.DsAgl = 40; P.DsLoss = 6; P.Wind = 12; P.FinalAgl = 25; P.FinalDist = 60;
		}
		const double K = Cfg.Sens == "low" ? 0.75 : Cfg.Sens == "high" ? 1.3 : 1.0;
		P.TPull *= K; P.TTerrain *= K; P.SinkHard /= K; P.TooLowAgl *= K;
		P.Bank = Kind == "plane" ? Cfg.BankPlane : Cfg.BankCopter;
		if (Vc == "icept" && Kind == "plane") P.Vne = 70;
		if (Vc == "heli") P.Vne = 30;
		if (Has(MaxSpeed) && MaxSpeed > 3) P.Vne = MaxSpeed * 1.08;
		return P;
	}

	static double OnDelay(const std::string& C)
	{
		static const std::map<std::string, double> M = { { "GPS_LOST", 1 }, { "BATT_CRIT", 1 }, { "BATT_LOW", 1 }, { "FUEL", 1 }, { "FUEL_LOW", 1 }, { "WIND", 2 },
			{ "VIBRATION", 1 }, { "EKF", 1 }, { "TOO_LOW", 0.5 }, { "DONT_SINK", 0.5 }, { "IMU_FAULT", 0 }, { "LINK", 1 }, { "ENGINE", 0.5 } };
		auto It = M.find(C);
		return It == M.end() ? 0.3 : It->second;
	}
	static constexpr double OFF_DELAY = 1.0, MIN_SHOW = 2.5;

	static bool IsLandMode(const std::string& M) { return M == "LAND" || M == "QLAND" || M == "AUTO.LAND" || M == "AUTO.PRECLAND"; }
	static bool IsRtlMode(const std::string& M) { return M == "RTL" || M == "QRTL" || M == "SMART_RTL" || M == "AUTO_RTL" || M == "AUTO.RTL"; }
	static bool IsAcroMode(const std::string& M) { return M == "ACRO" || M == "FLIP" || M == "TRAINING" || M == "AUTOTUNE" || M == "QACRO" || M == "QAUTOTUNE" || M == "STABILIZE_ACRO"; }

	FGpws::FGpws(const std::string& VehicleType, const FGpwsCfg& InCfg, double InMax, double InFence)
		: Vc(GpwsVehicleClass(VehicleType)), C(InCfg), MaxSpeed(InMax), Fence(InFence)
	{
		C.Normalize();
	}

	bool FGpws::Enabled(const std::string& Code) const
	{
		return C.Mode != "off" && CodeApplies(Code, Vc) && !C.Off.count(Code);
	}

	void FGpws::CloseCode(const std::string& Code, FCodeState& Cs, double TEnd, std::vector<FGpwsEpisode>* Out)
	{
		FGpwsEpisode E;
		E.Code = Code;
		E.Level = GpwsCode(Code)->Level;
		E.T0 = Round2(Cs.OnAt);
		E.StartS = Has(S.FirstArmT) ? Round2(std::max(0.0, Cs.OnAt - S.FirstArmT)) : 0;
		E.DurS = Round2(std::max(0.0, TEnd - Cs.OnAt));
		Eps.push_back(E);
		if (Out) Out->push_back(E);
		Cs.bOn = false; Cs.bAcked = false; Cs.RawSince = NaN; Cs.OffSince = NaN; Cs.OnAt = NaN;
	}

	std::vector<FGpwsEpisode> FGpws::Close(double T)
	{
		std::vector<FGpwsEpisode> Out;
		const double TEnd = Has(T) ? T : LastT;   // JS close(t = lastT)
		for (const std::string& Code : Order)
		{
			FCodeState& Cs = St[Code];
			if (Cs.bOn) CloseCode(Code, Cs, Has(TEnd) ? TEnd : Cs.OnAt, &Out);
		}
		return Out;
	}

	void FGpws::Reset()
	{
		Close(LastT);
		St.clear(); Order.clear(); S = FMem(); LastT = NaN;
	}

	void FGpws::SetCfg(const FGpwsCfg& In)
	{
		C = In; C.Normalize();
		if (C.Mode == "off") Close(LastT);
	}

	int FGpws::Ack(const std::string& Level)
	{
		int N = 0;
		for (const std::string& Code : Order)
		{
			FCodeState& Cs = St[Code];
			if (Cs.bOn && !Cs.bAcked && (Level.empty() || Level == GpwsCode(Code)->Level)) { Cs.bAcked = true; ++N; }
		}
		return N;
	}

	std::map<std::string, bool> FGpws::RawConditions(const FGpwsInput& I, const std::string& Kind, const std::set<std::string>& On)
	{
		std::map<std::string, bool> R;
		FThresh P = Thresholds(Vc, Kind, C, MaxSpeed);
		P.Fence = Fence;
		const double T = I.T;
		const double AirAgl = Has(P.AirAgl) ? P.AirAgl : 1.5;
		auto on = [&](const char* K) { return On.count(K) > 0; };
		if (I.bArmed)
		{
			if (!Has(S.ArmedAt)) S.ArmedAt = T;
			if (!Has(S.FirstArmT)) S.FirstArmT = T;
			S.WasArmedAt = T;
			if (Has(I.Agl) && I.Agl > AirAgl) { if (!Has(S.AirAt)) S.AirAt = T; }
			if (Has(S.AirAt) && Has(I.Agl)) S.PeakAgl = std::max(S.PeakAgl, I.Agl);
		}
		else { S.ArmedAt = NaN; S.AirAt = NaN; S.PeakAgl = 0; }
		if (Has(I.GpsFix) && I.GpsFix >= 3) S.bGpsSeen = true;
		const bool A = I.bArmed && !I.bCrashed;
		const bool InAir = A && Has(S.AirAt) && Kind != "rover";
		const double SinceAir = InAir ? T - S.AirAt : 0;
		const double Grace = Has(P.Grace) ? P.Grace : 3;
		const bool Takeoff = !InAir || SinceAir < Grace || I.Phase == "armed" || I.Phase == "launch" || I.Mode == "TAKEOFF";
		const double FinalAgl = Has(P.FinalAgl) ? P.FinalAgl : 25, FinalDist = Has(P.FinalDist) ? P.FinalDist : 60;
		const bool Landing = IsLandMode(I.Mode) || ((IsRtlMode(I.Mode) || I.Mode == "AUTO" || I.Mode == "AUTO.MISSION") && Has(I.Agl) && I.Agl < FinalAgl
			&& (!Has(I.DistHome) || I.DistHome < FinalDist));
		const bool Acro = IsAcroMode(I.Mode);
		const double Agl = Has(I.Agl) ? I.Agl : 0, Sink = std::max(0.0, -I.Vs);
		const double Look = (Has(P.Look) && P.Look != 0) ? P.Look : 4;
		const double CloseAhead = Has(I.AglAhead) ? std::max(0.0, (Agl - I.AglAhead) / Look) : 0;
		const double Closure = std::max(Sink, CloseAhead);
		const bool TerrainRise = CloseAhead - Sink > 1;
		const double Ttc = Closure > 0.1 ? Agl / Closure : INFINITY;
		const double Gs0 = Has(I.Gs) ? I.Gs : 0;
		if (InAir && !Takeoff && Kind != "rover")
		{
			R["PULL_UP"] = Agl < P.AglMax && Closure > P.SinkMin && Ttc < P.TPull * (on("PULL_UP") ? 1.3 : 1);
			R["TERRAIN"] = !Landing && TerrainRise && Agl < P.AglMax * 1.5 && Ttc < P.TTerrain * (on("TERRAIN") ? 1.2 : 1);
			R["SINK_RATE"] = (Sink > P.SinkHard * (on("SINK_RATE") ? 0.8 : 1) && Agl < P.AglMax * 3)
				|| (!Landing && !TerrainRise && Sink > P.SinkMin && Ttc < P.TTerrain * (on("SINK_RATE") ? 1.2 : 1));
			if (Kind == "plane" && Has(P.Stall) && P.Stall != 0 && Has(I.As))
			{
				const double Vs0 = P.Stall * (on("STALL") ? 1.15 : 1);
				const double Pi = Has(I.Pitch) ? I.Pitch : 0;
				R["STALL"] = Agl > 3 && (I.As < Vs0 || (Pi > 15 && I.As < P.Stall * 1.3));
			}
			if (Has(I.Pitch)) { const double H = on("PITCH") ? 5 : 0; R["PITCH"] = I.Pitch > P.PitchUp - H || I.Pitch < -(P.PitchDn - H); }
			R["TOO_LOW"] = !Landing && S.PeakAgl > P.TooLowPeak && Agl < P.TooLowAgl * (on("TOO_LOW") ? 1.3 : 1) && Gs0 > P.TooLowGs * (on("TOO_LOW") ? 0.8 : 1);
			R["DONT_SINK"] = !Landing && SinceAir < 60 && Agl < P.DsAgl && S.PeakAgl - Agl > P.DsLoss * (on("DONT_SINK") ? 0.6 : 1) && I.Vs < (on("DONT_SINK") ? 0.5 : -0.5);
		}
		if (InAir && Has(I.Roll)) R["BANK_ANGLE"] = std::fabs(I.Roll) > P.Bank - (on("BANK_ANGLE") ? 5 : 0);
		if (InAir)
		{
			const double Spd = Kind == "plane" ? (Has(I.As) ? I.As : I.Gs) : I.Gs;
			if (Has(Spd)) R["OVERSPEED"] = Spd > P.Vne * (on("OVERSPEED") ? 0.95 : 1);
			if (Has(I.Wind)) R["WIND"] = I.Wind > P.Wind - (on("WIND") ? 2 : 0);
		}
		R["ENGINE"] = (InAir && I.bMotorCut) || (Kind != "rover" && !I.bArmed && !I.bCrashed && Has(S.WasArmedAt) && T - S.WasArmedAt < 8 && Agl > 3 && Sink > 1.5);
		bool Imu = false;
		if (A)
		{
			if (((long long)I.SensBad & 0x3) != 0) Imu = true;
			if (Has(I.Ekf) && I.Ekf >= 0.8) Imu = true;
			if (!Acro && I.bRates)
			{
				const bool Fast = std::fabs(I.P) > 400 || std::fabs(I.Q) > 400;
				S.RateSince = Fast ? (Has(S.RateSince) ? S.RateSince : T) : NaN;
				if (Has(S.RateSince) && T - S.RateSince >= 0.4) Imu = true;
				const double SpinLim = Kind == "plane" ? 120 : 200, SpinDur = Kind == "plane" ? 3 : 2;
				const bool Spin = std::fabs(I.R) > SpinLim;
				S.SpinSince = Spin ? (Has(S.SpinSince) ? S.SpinSince : T) : NaN;
				if (Has(S.SpinSince) && T - S.SpinSince >= SpinDur) Imu = true;
			}
			else { S.RateSince = NaN; S.SpinSince = NaN; }
			if (!Acro && S.bPrevAtt && Has(I.Roll) && Has(I.Pitch))
			{
				const double Dt = T - S.PrevT;
				if (Dt > 0 && Dt <= 0.15)
				{
					const double Dr = Wrap180(I.Roll - S.PrevRoll), Dp = Wrap180(I.Pitch - S.PrevPitch);
					const bool Big = std::fabs(Dr) > 45 || std::fabs(Dp) > 45;
					const bool Odd = !I.bRates || std::fabs(Dr / Dt - I.P) > 250 || std::fabs(Dp / Dt - I.Q) > 250;
					if (Big && Odd) S.JumpUntil = T + 3;
				}
			}
			if (Has(S.JumpUntil) && T < S.JumpUntil) Imu = true;
		}
		if (Has(I.Roll) && Has(I.Pitch)) { S.bPrevAtt = true; S.PrevT = T; S.PrevRoll = I.Roll; S.PrevPitch = I.Pitch; }
		else S.bPrevAtt = false;
		R["IMU_FAULT"] = Imu;
		if (A && Has(I.Ekf)) R["EKF"] = I.Ekf >= 0.5 - (on("EKF") ? 0.05 : 0) && I.Ekf < 0.8;
		if (A && Has(I.Vibe)) R["VIBRATION"] = I.Vibe > (on("VIBRATION") ? 25 : 30);
		if (A)
		{
			const bool Bc = I.bBattEmpty || (Has(I.BattPct) && I.BattPct < 10 + (on("BATT_CRIT") ? 3 : 0));
			R["BATT_CRIT"] = Bc;
			R["BATT_LOW"] = !Bc && Has(I.BattPct) && I.BattPct < 25 + (on("BATT_LOW") ? 3 : 0);
			const bool Fc = I.bFuelEmpty || (Has(I.FuelPct) && I.FuelPct < 5 + (on("FUEL") ? 2 : 0));
			R["FUEL"] = Fc;
			R["FUEL_LOW"] = !Fc && Has(I.FuelPct) && I.FuelPct < 20 + (on("FUEL_LOW") ? 3 : 0);
			R["GPS_LOST"] = S.bGpsSeen && Has(I.GpsFix) && I.GpsFix < 3;
			if (Has(P.Fence) && P.Fence != 0) R["GEOFENCE"] = Has(I.DistHome) && I.DistHome > P.Fence * (on("GEOFENCE") ? 0.95 : 1);
			if (Has(I.LinkAgeMs)) R["LINK"] = I.LinkAgeMs > (on("LINK") ? 800 : 1500);
		}
		if (I.bCrashed) for (auto& Kv : R) Kv.second = false;
		return R;
	}

	FGpwsStepResult FGpws::Step(const FGpwsInput& I)
	{
		FGpwsStepResult Res;
		const double T = I.T;
		if (Has(LastT) && T < LastT - 0.5)
		{
			std::vector<FGpwsEpisode> E = Close(LastT);
			Res.Ended.insert(Res.Ended.end(), E.begin(), E.end());
			St.clear(); Order.clear(); S = FMem();
		}
		LastT = T;
		const std::string Kind = ModeKind(Vc, I.Mode);
		std::set<std::string> On;
		for (const std::string& Code : Order) if (St[Code].bOn) On.insert(Code);
		std::map<std::string, bool> R;
		if (C.Mode != "off") R = RawConditions(I, Kind, On);
		for (const FGpwsCodeDef& D : GCodes)
		{
			const std::string Code = D.Code;
			auto It = St.find(Code);
			auto Rv = R.find(Code);
			const bool Raw = Enabled(Code) && Rv != R.end() && Rv->second;
			if (It == St.end())
			{
				if (!Raw) continue;
				It = St.emplace(Code, FCodeState()).first;
				Order.push_back(Code);
			}
			FCodeState& Cs = It->second;
			if (Raw)
			{
				Cs.OffSince = NaN;
				if (!Cs.bOn)
				{
					if (!Has(Cs.RawSince)) Cs.RawSince = T;
					if (T - Cs.RawSince >= OnDelay(Code) - 1e-9) { Cs.bOn = true; Cs.OnAt = Cs.RawSince; Cs.bAcked = false; Res.Fresh.push_back(Code); }
				}
			}
			else
			{
				Cs.RawSince = NaN;
				if (Cs.bOn)
				{
					if (!Enabled(Code)) { CloseCode(Code, Cs, T, &Res.Ended); continue; }
					if (!Has(Cs.OffSince)) Cs.OffSince = T;
					if (T - Cs.OffSince >= OFF_DELAY && T - Cs.OnAt >= MIN_SHOW) CloseCode(Code, Cs, Cs.OffSince, &Res.Ended);
				}
			}
		}
		for (const std::string& Code : Order)
		{
			const FCodeState& Cs = St[Code];
			if (!Cs.bOn) continue;
			const FGpwsCodeDef* D = GpwsCode(Code);
			FGpwsActive A;
			A.Code = Code; A.Level = D->Level; A.Prio = D->Prio; A.Since = Cs.OnAt; A.bAcked = Cs.bAcked; A.bClearing = Has(Cs.OffSince);
			Res.Active.push_back(A);
		}
		std::stable_sort(Res.Active.begin(), Res.Active.end(), [](const FGpwsActive& X, const FGpwsActive& Y) { return X.Prio > Y.Prio; });
		return Res;
	}

	std::string GradeOf(int S)
	{
		if (S < 0) return std::string();
		return S >= 90 ? "A" : S >= 75 ? "B" : S >= 60 ? "C" : S >= 40 ? "D" : "F";
	}

	FGpwsSummary SummarizeAlerts(const std::vector<FGpwsEpisode>& List, const std::string& Mode, const std::vector<std::string>& Off)
	{
		FGpwsSummary Sm;
		Sm.Mode = Mode; Sm.Off = Off;
		std::vector<FGpwsEpisode> Ev;
		for (const FGpwsEpisode& E : List) if (GpwsCode(E.Code)) Ev.push_back(E);
		std::stable_sort(Ev.begin(), Ev.end(), [](const FGpwsEpisode& A, const FGpwsEpisode& B) { return A.T0 < B.T0; });
		double Total = 0, Pen = 0;
		for (const FGpwsEpisode& E : Ev)
		{
			const std::string Lvl = GpwsCode(E.Code)->Level;
			const double D = std::max(0.0, E.DurS);
			FGpwsByCode& B = Sm.ByCode[E.Code];
			B.Level = Lvl;
			B.N++;
			B.TotalS = Round2(B.TotalS + D);
			const double Fs = E.StartS;
			if (!Has(B.FirstS) || Fs < B.FirstS) B.FirstS = Round2(Fs);
			if (Lvl == "warning") Sm.Warnings++; else Sm.Cautions++;
			Total += D;
			const bool W = Lvl == "warning";
			Pen += std::min(W ? 20.0 : 8.0, (W ? 6.0 : 2.0) + (W ? 1.5 : 0.5) * D);
		}
		for (size_t i = 0; i < Ev.size() && i < 200; ++i) Sm.Events.push_back(Ev[i]);
		Sm.TotalS = Round2(Total);
		Sm.Score = Mode == "off" ? -1 : std::max(0, (int)std::floor(100 - Pen + 0.5));
		Sm.Grade = GradeOf(Sm.Score);
		return Sm;
	}
}
