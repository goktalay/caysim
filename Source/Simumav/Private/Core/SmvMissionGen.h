// Görev rotası üreteçleri (motordan bağımsız; tests/cpp ile g++ altında da derlenir).
// Görev türleri (kullanıcı 2026-11-01): koridor (kalkıştan seçilen noktalara uzanan koridor), ilaçlama (çokgen içi tarama),
// spiral (kalkış noktası çevresinde yükselen sarmal), serbest (nokta başına hız / yükseklik / işlem).
// Çıktı ArduPilot görev öğeleri (MAV_CMD; irtifa eve göre, frame 3) + çizim için kenar çokgeni + özet.
#pragma once

#include <string>
#include <vector>

namespace SmvCore
{
	/** kullanıcının haritada koyduğu nokta (serbest görevde nokta başına ayarlar) */
	struct FMissionPoint
	{
		double Lat = 0, Lon = 0;
		double Alt = 30;          // eve göre (m)
		double Speed = 0;         // m/s; 0 = değiştirme
		int Action = 0;           // 0 sonraki noktaya git, 1 tur at, 2 betik
		int Turns = 1;            // tur at: tur sayısı
		double Radius = 20;       // tur at: çap (m)
		std::string Script;       // betik: dosya adı
		double Args[4] = { 0, 0, 0, 0 };
	};

	struct FMissionParams
	{
		// koridor
		double CorridorWidth = 300, CorridorAlt = 120, TakeoffAlt = 30;
		// ilaçlama
		double SprayStep = 3, SprayAlt = 10, SprayAngle = 0;
		// spiral (çap ve yükseklik)
		double SpiralD0 = 100, SpiralD1 = 400, SpiralH0 = 30, SpiralH1 = 120;
		// varsayılan seyir hızı (özet süresi)
		double CruiseMs = 8;
	};

	/** ArduPilot görev öğesi (MISSION_ITEM_INT) */
	struct FMissionItem
	{
		int Cmd = 16;             // 22 kalkış, 16 nokta, 18 tur, 178 hız, 20 RTL, 42702 betik
		double Lat = 0, Lon = 0, Alt = 0;
		double P1 = 0, P2 = 0, P3 = 0, P4 = 0;
		bool bNav = true;         // konumlu (çizimde nokta)
	};

	struct FMissionSummary
	{
		double DistM = 0, TimeS = 0, MaxAlt = 0, MaxSpeed = 0, AreaM2 = 0;
		int Waypoints = 0, Items = 0;
	};

	struct FMissionResult
	{
		std::vector<FMissionItem> Items;
		/** çizim: koridor / ilaçlama kenarı (enlem, boylam) */
		std::vector<std::pair<double, double>> Outline;
		FMissionSummary Summary;
	};

	/** Kind: "corridor" | "spray" | "spiral" | "free". Ev: kalkış noktası (enlem / boylam). */
	FMissionResult BuildMission(const std::string& Kind, double HomeLat, double HomeLon, const std::vector<FMissionPoint>& Points, const FMissionParams& P);

	/** yerel ENU (m) ↔ enlem / boylam (ev çevresinde eşdikdörtgen) */
	void ToLocal(double HomeLat, double HomeLon, double Lat, double Lon, double& E, double& N);
	void ToGeo(double HomeLat, double HomeLon, double E, double N, double& Lat, double& Lon);
	/** iki nokta arası yatay mesafe (m) */
	double DistM(double La0, double Lo0, double La1, double Lo1);
	/** Kaydet için gereken en az nokta (spiral: 0) */
	int MinPoints(const std::string& Kind);
}
