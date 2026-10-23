// SIMUMAV — §42 uçuş uyarıları (GPWS / stall / bank angle / IMU …) değerlendiricisi ve "uçuş disiplini" puanı.
// Kaynak: fabrika-yonetim static/js/modules/flightsim_gpws.js (değerlendirici) + app/flightsim_gpws.py (özet/puan) — birebir port.
// Yalnız C++17, Unreal'sız. "Yok" değerler NaN ile gösterilir. tests/cpp/test_core.py Node (JS) ve Python'a karşı sınar.
#pragma once

#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace SmvCore
{
	constexpr double NaN = std::numeric_limits<double>::quiet_NaN();
	// V == V, /fp:fast (MSVC/UE) altında derleyicinin "her zaman doğru" sayabileceği bir NaN sınamasıdır → std::isnan
	inline bool Has(double V) { return !std::isnan(V); }

	struct FGpwsCodeDef { const char* Code; const char* Level; int Prio; const char* Av; const char* Say; const char* Tr; bool bRover; bool bPlaneOnly; };
	const std::vector<FGpwsCodeDef>& GpwsCodes();
	const FGpwsCodeDef* GpwsCode(const std::string& Code);

	struct FGpwsCfg
	{
		std::string Mode = "full";   // off | visual | full
		std::string Lang = "aviation";
		std::string Sens = "normal"; // low | normal | high
		int BankCopter = 35;         // 20–60
		int BankPlane = 45;          // 30–70
		std::set<std::string> Off;
		void Normalize();
	};

	/** teleInput çıktısı (normalleştirilmiş kare). Açılar °, hızlar °/s. */
	struct FGpwsInput
	{
		double T = 0;
		bool bArmed = false;
		std::string Mode;
		double Agl = NaN, AglAhead = NaN, Vs = 0, Gs = NaN, As = NaN, Roll = NaN, Pitch = NaN;
		bool bRates = false; double P = 0, Q = 0, R = 0;
		double BattPct = NaN; bool bBattEmpty = false;
		double FuelPct = NaN; bool bFuelEmpty = false;
		double GpsFix = NaN, Sats = NaN, SensBad = 0, Ekf = NaN, Vibe = NaN, Wind = NaN;
		bool bMotorCut = false;
		std::string Phase;
		double DistHome = NaN, LinkAgeMs = NaN;
		bool bCrashed = false;
	};

	struct FGpwsEpisode { std::string Code, Level; double T0 = 0, StartS = 0, DurS = 0; };
	struct FGpwsActive { std::string Code, Level; int Prio = 0; double Since = 0; bool bAcked = false, bClearing = false; };
	struct FGpwsStepResult { std::vector<FGpwsActive> Active; std::vector<std::string> Fresh; std::vector<FGpwsEpisode> Ended; };

	/** SITL / SIMUMAV tipi → copter | heli | plane | vtol | icept | rover */
	std::string GpwsVehicleClass(const std::string& Type);

	class FGpws
	{
	public:
		FGpws(const std::string& VehicleType, const FGpwsCfg& Cfg, double MaxSpeed = NaN, double Fence = NaN);
		FGpwsStepResult Step(const FGpwsInput& I);
		int Ack(const std::string& Level = std::string());
		std::vector<FGpwsEpisode> Close(double T);
		void Reset();
		void SetCfg(const FGpwsCfg& C);
		void SetMaxSpeed(double V) { MaxSpeed = V; }
		void SetFence(double F) { Fence = F; }
		const std::vector<FGpwsEpisode>& Episodes() const { return Eps; }
		const FGpwsCfg& Cfg() const { return C; }
		const std::string& VClass() const { return Vc; }
	private:
		struct FCodeState { bool bOn = false; double RawSince = NaN, OffSince = NaN, OnAt = NaN; bool bAcked = false; };
		struct FMem
		{
			double ArmedAt = NaN, FirstArmT = NaN, WasArmedAt = NaN, AirAt = NaN, PeakAgl = 0;
			bool bGpsSeen = false;
			double RateSince = NaN, SpinSince = NaN, JumpUntil = NaN;
			bool bPrevAtt = false; double PrevT = 0, PrevRoll = 0, PrevPitch = 0;
		};
		std::string Vc;
		FGpwsCfg C;
		double MaxSpeed, Fence;
		FMem S;
		std::map<std::string, FCodeState> St;
		std::vector<std::string> Order;   // ekleme sırası (JS Map yineleme sırası)
		std::vector<FGpwsEpisode> Eps;
		double LastT = NaN;
		bool Enabled(const std::string& Code) const;
		void CloseCode(const std::string& Code, FCodeState& Cs, double TEnd, std::vector<FGpwsEpisode>* Out);
		std::map<std::string, bool> RawConditions(const FGpwsInput& I, const std::string& Kind, const std::set<std::string>& On);
	};

	// ---- özet / puan (sunucu ozetle ile aynı) ----
	struct FGpwsByCode { std::string Level; int N = 0; double TotalS = 0, FirstS = NaN; };
	struct FGpwsSummary
	{
		std::string Mode; std::vector<std::string> Off;
		std::vector<FGpwsEpisode> Events;
		std::map<std::string, FGpwsByCode> ByCode;
		int Warnings = 0, Cautions = 0;
		double TotalS = 0;
		int Score = -1;          // < 0 → yok (mod off)
		std::string Grade;       // boş → yok
	};
	FGpwsSummary SummarizeAlerts(const std::vector<FGpwsEpisode>& List, const std::string& Mode = "full", const std::vector<std::string>& Off = {});
	std::string GradeOf(int Score);
}
