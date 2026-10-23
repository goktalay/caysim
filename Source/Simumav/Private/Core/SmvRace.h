// SIMUMAV — halka yarışı ve it dalaşı çekirdeği (app/flightsim_match_core.py birebir; yalnız C++17, Unreal'sız).
// Misafir kipte yerel karşılaşma (yapay zekâ pilotlarla) bunu kullanır; çevrim içi karşılaşmada hakem sunucudur, istemci bunu yalnız
// HUD önizlemesi için çalıştırır. tests/cpp/test_core.py Python modülüne karşı sınar.
#pragma once

#include <string>
#include <vector>

namespace SmvCore
{
	constexpr double R_EARTH = 6378137.0;
	constexpr double JUMP_M = 250.0;          // SICRAMA_M
	constexpr double START_SPACING_M = 40.0;  // AYRIK_M
	constexpr double COUNTDOWN_S = 6.0;

	/** 'ucak' (sabit kanat) ya da 'dikey' (diğer tipler) — sınıf seçimi. */
	enum class EVClass { Vertical, Plane };
	EVClass ClassOfType(const std::string& SitlOrSmvType);

	struct FRingCfg
	{
		int Count = 8;
		double MinAltM = 15.0, MaxAltM = 60.0;
		double DiameterM = 12.0, RadiusM = 220.0;
		int TimeLimitMin = 15;
		long long Seed = 1;
		static FRingCfg Defaults(EVClass C);
		/** Sınırlara kırpar (sunucu doğrulaması hatada reddeder; yerelde kırpmak yeterli). */
		void Clamp();
	};

	struct FRing
	{
		int I = 0;            // 1'den başlar
		double Lat = 0, Lon = 0, X = 0, Y = 0, AglM = 0, AltMsl = 0, YawDeg = 0, Nx = 0, Ny = 1, RadiusM = 6;
	};

	struct FStart { double X = 0, Y = 0, YawDeg = 0; };

	void Enu(double Lat0, double Lon0, double Lat, double Lon, double& X, double& Y);
	void LatLon(double Lat0, double Lon0, double X, double Y, double& Lat, double& Lon);

	/** halkalar_uret — Python ile aynı tohum → aynı halkalar (yuvarlamalar dahil). */
	std::vector<FRing> GenerateRings(const FRingCfg& Cfg, double CenterLat, double CenterLon, double GroundMsl, const std::vector<FStart>& Starts);
	/** baslangic_noktalari */
	std::vector<FStart> StartPoints(int N, EVClass C, double RunwayHeadingDeg, double Spacing = START_SPACING_M, const double* TargetXY = nullptr);

	/** halka_kesisim: 0 = kesişim yok, 1 = ileri, -1 = ters; OutDist kesişimin merkeze 3B uzaklığı. */
	int RingIntersect(const double P0[3], const double P1[3], const FRing& R, double& OutDist);

	struct FRingEvent { std::string Kind; int I = 0; double T = 0; };   // ring | finish | wrong_way

	class FRingTracker
	{
	public:
		explicit FRingTracker(const std::vector<FRing>& InRings) : Rings(InRings) {}
		std::vector<FRingEvent> Update(double X, double Y, double ZMsl, double T);
		bool Finished() const { return Next >= (int)Rings.size(); }
		int NextIndex() const { return Next; }
		int Passed() const { return Next; }
		double DistNext() const { return Dist; }
		double FinishT() const { return FinishTime; }
	private:
		std::vector<FRing> Rings;
		int Next = 0;
		bool bHasLast = false;
		double Last[3] = { 0, 0, 0 };
		double Dist = -1;
		double FinishTime = -1;
	};

	// ---- it dalaşı ----
	struct FDogfightCfg
	{
		double DistanceM = 400, AltM = 40;
		int TimeLimitMin = 5;
		double RangeM = 120, ConeDeg = 30, HoldS = 3, AreaRadiusM = 800;
		std::string Roles = "random";   // random | host_chaser | host_evader
		long long Seed = 1;
		EVClass Class = EVClass::Vertical;
		static FDogfightCfg Defaults(EVClass C);
		/** Alan yarıçapı verilmediyse büyütme kuralı + kırpma. */
		void Normalize(bool bAreaGiven);
	};

	/** it_baslangiclari: Chaser / Evader başlangıcı (x, y, yaw). */
	void DogfightStarts(const FDogfightCfg& Cfg, double RunwayHeadingDeg, FStart& Chaser, FStart& Evader);
	/** Rol kuralı `random`: Random(seed) < 0.5 ise kurucu kovalar. */
	bool HostIsChaser(const FDogfightCfg& Cfg);

	struct FDogfightEvent { std::string Kind; int Role = 0; double HeldS = 0, DistM = 0, OffDeg = 0; };   // area|guard_off|lock_on|lock_off|engage

	class FDogfight
	{
	public:
		enum { Chaser = 0, Evader = 1 };
		FDogfight(const FDogfightCfg& InCfg, double T0) : Cfg(InCfg), Time0(T0) {}
		std::vector<FDogfightEvent> Update(int Role, double X, double Y, double Z, double HeadingDeg, double PitchRad, double T);
		bool Done() const { return bDone; }
		bool InCone() const { return bInCone; }
		bool Guard() const { return bGuard; }
		double HoldS() const { return Hold; }
		double MaxHoldS() const { return MaxHold; }
		double MinDistM() const { return MinDist; }
		double LastDist() const { return LDist; }
		double LastOffDeg() const { return LOff; }
		double LastBearingDeg() const { return LBearing; }
	private:
		FDogfightCfg Cfg;
		double Time0 = 0;
		bool bHave[2] = { false, false };
		double Pose[2][6] = {};
		double Hold = 0, MaxHold = 0, ConeTotal = 0, MinDist = -1, LastT = -1;
		bool bInCone = false, bGuard = true, bDone = false;
		double LDist = -1, LOff = -1, LBearing = -1;
	};

	/** Python round(x, n) (çift yuvarlama değil; burada yarımlar sıfırdan uzağa — üretimde ikili kesirlerde farkı yok). */
	double RoundN(double V, int N);
}
