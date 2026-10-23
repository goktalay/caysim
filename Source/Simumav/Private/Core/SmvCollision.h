// SIMUMAV — fizik temas sınıflandırması ve engel sensörü yardımcıları (yalnız C++17; Unreal'sız, tests/cpp/test_core.py sınar).
// Çarpışmanın KENDİSİ Unreal'ın fizik sorgularıyla bulunur (SmvPhysics.cpp: gövde şekli süpürmesi, başlangıç çakışması MTD'si).
// Bu dosya yalnız bulunan temasın ne olduğuna karar verir: temas normali + çarpma hızı → sürtünme / sert çarpma / tavan / iniş / düşüş / su.
// Eşikler HEPGO ile tutarlıdır: flightsim_mission.SITL_HIT_HIZ_MIN = 3,0 m/s (bunun altındaki dikey temas iniş, üstü düşüş),
// flightsim3d.js COLL_SPEED_MIN = 2 m/s (bunun altındaki yanal temas sürtünme, çarpışma değil).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace SmvCore
{
	/** Temas sınıfı. */
	enum class EContact : int
	{
		None = 0,
		Graze = 1,          // yanal hafif temas / sürtünme (yüzeyden dışarı itilir)
		Hard = 2,           // sert çarpma (pervane/gövde hasarı → motor kesme, çarpışma kaydı)
		CeilingTouch = 3,   // tavana hafif temas (aşağı itilir)
		CeilingHit = 4,     // tavana çarpma (çarpışma)
		Landing = 5,        // iniş takımı üstüne yumuşak temas (iniş / yerde durma)
		Fall = 6,           // yere düşüş (dikey hız eşiği aşıldı ya da yerde savrulma)
		Water = 7,          // suya temas
	};

	/** Araç sınıfı (temas kuralları için). */
	enum class EVehicleClass : int { Vertical = 0, Plane = 1, Rover = 2 };

	struct FContactCfg
	{
		double FloorNz = 0.7;            // normal·yukarı ≥ bu → zemin/çatı benzeri yüzey (~45°)
		double CeilingNz = -0.7;         // normal·yukarı ≤ bu → tavan
		double GrazeMaxMs = 2.0;         // yanal: yüzeye dik yaklaşma hızı bunun altında → sürtünme
		double GrazeMaxTangentMs = 8.0;  // yanal: yüzeye paralel hız bunun üstünde → pervane çarpması (sert)
		double LandMaxVzMs = 3.0;        // zemin: dikey yaklaşma hızı bunun altında → iniş (HEPGO SITL_HIT_HIZ_MIN)
		double LandMaxVtVerticalMs = 3.0;// dikey kalkışlı araç yerde bu yatay hızın üstünde savrulursa → düşüş
		double LandMaxVtPlaneMs = 60.0;  // sabit kanat iniş koşusu (tekerlek)
		double CeilingHardMs = 1.5;      // tavan: yukarı yaklaşma hızı bunun üstünde → çarpma
		double WaterMinMs = 0.0;         // su: her temas çarpışma (kara aracı dahil)
	};

	struct FContactIn
	{
		// yüzey normali (birim; yüzeyden araca doğru) ve araç hızı (m/s) — yerel çerçeve: z YUKARI
		double Nx = 0, Ny = 0, Nz = 1;
		double Vx = 0, Vy = 0, Vz = 0;
		EVehicleClass Vehicle = EVehicleClass::Vertical;
		bool bWater = false;
	};

	struct FContactOut
	{
		EContact Class = EContact::None;
		double Vn = 0;      // yüzeye dik yaklaşma hızı (m/s; > 0 yaklaşıyor)
		double Vt = 0;      // yüzeye paralel hız (m/s)
		double Speed = 0;   // toplam hız (m/s)
		bool bCrash = false;
	};

	FContactOut ClassifyContact(const FContactIn& In, const FContactCfg& Cfg = FContactCfg());
	const char* ContactName(EContact C);
	bool ContactIsCrash(EContact C);
	/** Kalıcı (sürekli) temasta sınıfı yükselt: sürtünme/tavan teması yüzeye BatmaM kadar itilmeye devam ediyorsa ya da SureS
	 *  boyunca sürüyorsa (araç yüzeye dayanmış, pervaneler sürtüyor) → sert çarpma. */
	EContact EscalateSustained(EContact C, double PenetrationM, double DurationS, double PenCrashM = 1.0, double SustainCrashS = 3.0);

	// ---- engel sensörleri ----
	/** N sektörlü 360° yakınlık: i. ışının gövdeye göre yönü (derece; 0 = burun, saat yönünde +; MAVLink BODY_FRD). */
	double SectorBearingDeg(int Index, int Sectors);
	/** Gövdeye göre yön (derece, her değer) → sektör indeksi (en yakın merkez). */
	int SectorIndex(double BearingDeg, int Sectors);
	/** Sektör başına alt ışın açıları (sektör genişliğini taramak için; 72'de 1 ışın, 8'de 5 ışın). */
	std::vector<double> SectorSubRays(int Index, int Sectors, int SubRays);

	/** Ölçüm (m; < 0 = engel yok) → MAVLink mesafe (cm). Menzil dışı yakın → MinCm, engel yok / uzak → MaxCm + 1 (ArduPilot: "menzil dışı"). */
	uint16_t RangeToCm(double DistM, double MinM, double MaxM);
	/** OBSTACLE_DISTANCE dizisi: ilk N eleman ölçüm (RangeToCm), kalan 72 − N eleman UINT16_MAX (kullanılmıyor). */
	void ObstacleArray(const double* DistM, int N, double MinM, double MaxM, uint16_t Out[72]);

	/** Fizik sensörleri SITL parametreleri (varsayilan.parm'a eklenir; ArduPilot 4.6+ adları — RNGFND1_MIN/MAX metre).
	 *  SitlType: multirotor/hexarotor/helikopter (menzil bulucu + yakınlık + kaçınma), sabit_kanat/vtol/interceptor (menzil bulucu,
	 *  iniş için), rover (yakınlık + kaçınma). Kullanıcının kayıtlı parametrelerinde aynı ad varsa yazılmaz (UserNames). */
	using FPhysParamList = std::vector<std::pair<std::string, double>>;
	FPhysParamList PhysicsSensorParams(const std::string& SitlType, bool bAvoid, double RngMaxM = 40.0);
	bool PhysicsHasRangefinder(const std::string& SitlType);
	bool PhysicsHasProximity(const std::string& SitlType);
}
