// SIMUMAV — güç / yakıt modeli (app/flightsim_power.py GucDurumu birebir; yalnız C++17, Unreal'sız).
// Misafir kipte yerel SITL telemetrisinden `tele.fuel` / `tele.elec` / `speedup` üretir. tests/cpp/test_core.py Python'a karşı sınar.
#pragma once

#include <deque>
#include <string>
#include <vector>

namespace SmvCore
{
	struct FPowerCfg
	{
		double MaxSpeedMs = -1;      // < 0 → yok (kopter / rover)
		double FuelL = 0;
		double FuelKmPerL = 25;
		int ElecCells = 4;
		double ElecMah = 10000;
		double ElecW = 30;
		/** Tip varsayılanları: sabit_kanat|vtol|plane → CAYMAZ; interceptor; rover; diğer → kopter. */
		static FPowerCfg Defaults(const std::string& SitlType);
		void Clamp(bool bPlaneType);
	};

	struct FPowerTele
	{
		bool bFuelOn = false; double FuelL = 0, FuelPct = 0, FuelLpm = 0, RangeKm = 0; bool bFuelEmpty = false;
		bool bElecOn = false; double Wh = 0, ElecPct = 0, W = 0, MinLeft = -1; int Cells = 0; double V = 0; bool bElecEmpty = false;
		double Speedup = 1;
	};

	class FPowerState
	{
	public:
		explicit FPowerState(const FPowerCfg& Cfg);
		/** Her telemetri karesinde (SITL time_boot_ms). Servo: 8 kanal µs (yoksa boş). Dönüş: yeni biten kaynaklar ("fuel" / "elec"). */
		std::vector<std::string> Step(bool bArmed, double GroundSpeed, double ThrottlePct, const std::vector<int>& Servo, long long TbmMs);
		void SetSpeedup(double F);
		void ResetTime() { bHasT = false; PrevServo.clear(); }
		FPowerTele Tele() const;

		// özet alanları
		double DistM = 0, TimeS = 0, ArmedS = 0, MaxSpeedSeen = 0;
		double FuelL0 = 0, FuelL = 0, FuelUsed = 0, FuelLpm = 0, FuelEmptyT = -1;
		double WhCap = 0, WhUsed = 0, WNow = 0, ElecEmptyT = -1;
		double Speedup = 1, SpeedupMax = 1, SpeedupMin = 1; int SpeedupChanges = 0;
		bool bFuelOn = false, bElecOn = false;
		FPowerCfg Cfg;
	private:
		bool bHasT = false;
		long long TMs = 0;
		std::vector<int> PrevServo;
		std::deque<std::pair<double, double>> Window;
	};

	/** L/dk = (v km/h ÷ 60) ÷ km/L × (0,5 + 0,5·gaz). */
	double FuelLpm(double VMs, double ThrPct, double KmPerL);
	/** Ölçülen km/L (≥ 0,3 km ve ≥ 0,02 L) — yoksa < 0. */
	double MeasuredKmPerL(double DistM, double UsedL);
	/** Ortalama W (≥ 60 sn) — yoksa < 0. */
	double AverageW(double WhUsed, double TimeS);
	/** ArduPlane hız parametreleri (yerel resmi ikili SIM_PLN_THR_MUL tanımaz → itki ölçeği YAZILMAZ). Oran 12/22 (interceptor 0,8). */
	std::vector<std::pair<std::string, double>> SpeedParams(double VMax, bool bInterceptor, double RecordedMax = -1, double RecordedCruise = -1, double RecordedMin = -1);
}
