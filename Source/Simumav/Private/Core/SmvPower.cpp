// SIMUMAV — güç / yakıt modeli (bkz. SmvPower.h; kaynak: app/flightsim_power.py).
#include "SmvPower.h"
#include "SmvRace.h"   // RoundN
#include <algorithm>
#include <cmath>

namespace SmvCore
{
	static constexpr double CELL_V = 3.7;
	static constexpr double SERVO_J_PER_US = 0.0005;
	static constexpr double FUEL_MIN_KM = 0.3, FUEL_MIN_L = 0.02, ELEC_MIN_S = 60.0, W_WINDOW_S = 30.0;

	FPowerCfg FPowerCfg::Defaults(const std::string& T)
	{
		FPowerCfg C;
		if (T == "interceptor") { C.MaxSpeedMs = 111.1; C.FuelL = 0; C.FuelKmPerL = 25; C.ElecCells = 6; C.ElecMah = 8000; C.ElecW = 40; }
		else if (T == "rover") { C.MaxSpeedMs = -1; C.FuelL = 0; C.FuelKmPerL = 12; C.ElecCells = 4; C.ElecMah = 16000; C.ElecW = 60; }
		else if (T == "sabit_kanat" || T == "vtol" || T == "plane") { C.MaxSpeedMs = 45; C.FuelL = 5; C.FuelKmPerL = 25; C.ElecCells = 4; C.ElecMah = 10000; C.ElecW = 30; }
		else { C.MaxSpeedMs = -1; C.FuelL = 0; C.FuelKmPerL = 25; C.ElecCells = 4; C.ElecMah = 10000; C.ElecW = 30; }
		return C;
	}

	void FPowerCfg::Clamp(bool bPlane)
	{
		if (!bPlane) MaxSpeedMs = -1;
		else if (MaxSpeedMs >= 0) MaxSpeedMs = std::min(150.0, std::max(5.0, MaxSpeedMs));
		FuelL = std::min(500.0, std::max(0.0, FuelL));
		FuelKmPerL = std::min(500.0, std::max(0.5, FuelKmPerL));
		ElecCells = std::min(16, std::max(1, ElecCells));
		ElecMah = std::min(200000.0, std::max(100.0, ElecMah));
		ElecW = std::min(2000.0, std::max(0.0, ElecW));
	}

	double FuelLpm(double V, double Thr, double Kml)
	{
		if (Kml <= 0) return 0.0;
		const double VKmh = std::max(0.0, V) * 3.6;
		const double T = std::max(0.0, std::min(1.0, Thr / 100.0));
		return (VKmh / 60.0) / Kml * (0.5 + 0.5 * T);
	}

	static double ServoJoule(const std::vector<int>& S, const std::vector<int>& P)
	{
		if (S.empty() || P.empty()) return 0.0;
		double J = 0;
		const size_t N = std::min(S.size(), P.size());
		for (size_t i = 0; i < N; ++i)
		{
			const int A = S[i], B = P[i];
			if (A >= 800 && A <= 2200 && B >= 800 && B <= 2200) J += std::abs(A - B) * SERVO_J_PER_US;
		}
		return J;
	}

	double MeasuredKmPerL(double DistM, double UsedL)
	{
		if (DistM / 1000.0 < FUEL_MIN_KM || UsedL < FUEL_MIN_L) return -1;
		return RoundN(DistM / 1000.0 / UsedL, 2);
	}

	double AverageW(double WhUsed, double TimeS)
	{
		if (TimeS <= 0 || TimeS < ELEC_MIN_S) return -1;
		return RoundN(WhUsed / (TimeS / 3600.0), 1);
	}

	FPowerState::FPowerState(const FPowerCfg& InCfg) : Cfg(InCfg)
	{
		bFuelOn = Cfg.FuelL > 0 && Cfg.FuelKmPerL > 0;
		FuelL0 = FuelL = Cfg.FuelL;
		WhCap = Cfg.ElecMah / 1000.0 * Cfg.ElecCells * CELL_V;
		bElecOn = WhCap > 0;
		WNow = Cfg.ElecW;
	}

	void FPowerState::SetSpeedup(double F)
	{
		if (F != Speedup) ++SpeedupChanges;
		Speedup = F;
		SpeedupMax = std::max(SpeedupMax, F);
		SpeedupMin = std::min(SpeedupMin, F);
	}

	std::vector<std::string> FPowerState::Step(bool bArmed, double Gs, double Thr, const std::vector<int>& Servo, long long T)
	{
		std::vector<std::string> Out;
		if (!bHasT || T < TMs)
		{
			bHasT = true; TMs = T; PrevServo = Servo;
			return Out;
		}
		const double Dt = (T - TMs) / 1000.0;
		TMs = T;
		if (Dt <= 0 || Dt > 5.0) { PrevServo = Servo; return Out; }
		TimeS += Dt;
		if (bArmed) MaxSpeedSeen = std::max(MaxSpeedSeen, Gs);
		if (bArmed) { ArmedS += Dt; DistM += Gs * Dt; }
		if (bFuelOn)
		{
			FuelLpm = bArmed ? SmvCore::FuelLpm(Gs, Thr, Cfg.FuelKmPerL) : 0.0;
			if (bArmed && FuelL > 0)
			{
				const double Use = std::min(FuelL, FuelLpm * Dt / 60.0);
				FuelL -= Use; FuelUsed += Use;
				if (FuelL <= 1e-9) { FuelL = 0; FuelEmptyT = RoundN(TimeS, 1); Out.push_back("fuel"); }
			}
		}
		if (bElecOn)
		{
			const double J = Cfg.ElecW * Dt + ServoJoule(Servo, PrevServo);
			PrevServo = Servo;
			Window.push_back({ TimeS, J });
			while (!Window.empty() && TimeS - Window.front().first > W_WINDOW_S) Window.pop_front();
			const double Span = Window.size() > 1 ? TimeS - Window.front().first : 0;
			if (Span > 1.0) { double Sum = 0; for (auto& P : Window) Sum += P.second; WNow = Sum / Span; }
			else WNow = Cfg.ElecW;
			if (WhUsed < WhCap)
			{
				WhUsed = std::min(WhCap, WhUsed + J / 3600.0);
				if (WhCap - WhUsed <= 1e-9) { WhUsed = WhCap; ElecEmptyT = RoundN(TimeS, 1); Out.push_back("elec"); }
			}
		}
		return Out;
	}

	FPowerTele FPowerState::Tele() const
	{
		FPowerTele P;
		P.Speedup = Speedup;
		P.bFuelOn = bFuelOn;
		if (bFuelOn)
		{
			P.FuelL = RoundN(FuelL, 3);
			P.FuelPct = FuelL0 > 0 ? RoundN(100.0 * FuelL / FuelL0, 1) : 0.0;
			P.FuelLpm = RoundN(FuelLpm, 4);
			P.RangeKm = RoundN(FuelL * Cfg.FuelKmPerL, 2);
			P.bFuelEmpty = FuelL <= 0;
		}
		P.bElecOn = bElecOn;
		if (bElecOn)
		{
			const double Left = WhCap - WhUsed;
			P.Wh = RoundN(Left, 2);
			P.ElecPct = RoundN(100.0 * Left / WhCap, 1);
			P.W = RoundN(WNow, 1);
			P.MinLeft = WNow > 0.01 ? RoundN(Left / WNow * 60.0, 1) : -1;
			P.Cells = Cfg.ElecCells;
			P.V = RoundN(Cfg.ElecCells * (3.5 + 0.7 * std::max(0.0, Left / WhCap)), 2);
			P.bElecEmpty = Left <= 0;
		}
		return P;
	}

	std::vector<std::pair<std::string, double>> SpeedParams(double V, bool bIcept, double RMax, double RCruise, double RMin)
	{
		std::vector<std::pair<std::string, double>> Out;
		if (V <= 0) return Out;
		double Ratio = bIcept ? 0.8 : 12.0 / 22.0;
		if (RMax > 0 && RCruise > 0) Ratio = std::max(0.2, std::min(0.95, RCruise / RMax));
		double Cruise = RoundN(V * Ratio, 1);
		if (RMin > 0 && Cruise <= RMin) Cruise = RoundN(std::min(V, RMin + 2.0), 1);
		Out.push_back({ "AIRSPEED_MAX", RoundN(V, 1) });
		Out.push_back({ "AIRSPEED_CRUISE", Cruise });
		Out.push_back({ "THR_MAX", 100.0 });
		return Out;
	}
}
