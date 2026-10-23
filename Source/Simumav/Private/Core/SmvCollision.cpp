// SIMUMAV — fizik temas sınıflandırması ve engel sensörü yardımcıları (bkz. SmvCollision.h).
#include "SmvCollision.h"

#include <algorithm>
#include <cmath>

namespace SmvCore
{
	FContactOut ClassifyContact(const FContactIn& In, const FContactCfg& Cfg)
	{
		FContactOut O;
		double Nx = In.Nx, Ny = In.Ny, Nz = In.Nz;
		const double L = std::sqrt(Nx * Nx + Ny * Ny + Nz * Nz);
		if (L > 1e-9) { Nx /= L; Ny /= L; Nz /= L; }
		else { Nx = 0; Ny = 0; Nz = 1; }
		const double Dot = In.Vx * Nx + In.Vy * Ny + In.Vz * Nz;   // normal yüzeyden dışarı: yaklaşırken Dot < 0
		O.Vn = -Dot;
		const double Tx = In.Vx - Dot * Nx, Ty = In.Vy - Dot * Ny, Tz = In.Vz - Dot * Nz;
		O.Vt = std::sqrt(Tx * Tx + Ty * Ty + Tz * Tz);
		O.Speed = std::sqrt(In.Vx * In.Vx + In.Vy * In.Vy + In.Vz * In.Vz);
		const double Vn = O.Vn > 0 ? O.Vn : 0.0;

		if (In.bWater)
		{
			O.Class = (O.Speed >= Cfg.WaterMinMs) ? EContact::Water : EContact::Landing;
		}
		else if (Nz >= Cfg.FloorNz)
		{
			// zemin / çatı / masa üstü: iniş takımı + dikey hız
			if (In.Vehicle == EVehicleClass::Rover) O.Class = EContact::None;   // kara aracı zaten yerde
			else
			{
				const double VtMax = In.Vehicle == EVehicleClass::Plane ? Cfg.LandMaxVtPlaneMs : Cfg.LandMaxVtVerticalMs;
				O.Class = (Vn < Cfg.LandMaxVzMs && O.Vt <= VtMax) ? EContact::Landing : EContact::Fall;
			}
		}
		else if (Nz <= Cfg.CeilingNz)
		{
			O.Class = Vn >= Cfg.CeilingHardMs ? EContact::CeilingHit : EContact::CeilingTouch;
		}
		else
		{
			// duvar / gövde / dal: yanal
			O.Class = (Vn < Cfg.GrazeMaxMs && O.Vt <= Cfg.GrazeMaxTangentMs) ? EContact::Graze : EContact::Hard;
		}
		O.bCrash = ContactIsCrash(O.Class);
		return O;
	}

	const char* ContactName(EContact C)
	{
		switch (C)
		{
		case EContact::Graze: return "graze";
		case EContact::Hard: return "hard";
		case EContact::CeilingTouch: return "ceiling_touch";
		case EContact::CeilingHit: return "ceiling_hit";
		case EContact::Landing: return "landing";
		case EContact::Fall: return "fall";
		case EContact::Water: return "water";
		default: return "none";
		}
	}

	bool ContactIsCrash(EContact C)
	{
		return C == EContact::Hard || C == EContact::CeilingHit || C == EContact::Fall || C == EContact::Water;
	}

	EContact EscalateSustained(EContact C, double PenetrationM, double DurationS, double PenCrashM, double SustainCrashS)
	{
		if (C != EContact::Graze && C != EContact::CeilingTouch) return C;
		if (PenetrationM >= PenCrashM || (DurationS >= SustainCrashS && PenetrationM >= 0.3 * PenCrashM))
			return C == EContact::CeilingTouch ? EContact::CeilingHit : EContact::Hard;
		return C;
	}

	double SectorBearingDeg(int Index, int Sectors)
	{
		if (Sectors <= 0) return 0;
		const double Inc = 360.0 / Sectors;
		double B = std::fmod(Index * Inc, 360.0);
		if (B < 0) B += 360.0;
		return B;
	}

	int SectorIndex(double BearingDeg, int Sectors)
	{
		if (Sectors <= 0) return 0;
		const double Inc = 360.0 / Sectors;
		double B = std::fmod(BearingDeg, 360.0);
		if (B < 0) B += 360.0;
		int I = (int)std::floor(B / Inc + 0.5);
		if (I >= Sectors) I -= Sectors;
		return I;
	}

	std::vector<double> SectorSubRays(int Index, int Sectors, int SubRays)
	{
		std::vector<double> Out;
		const double C = SectorBearingDeg(Index, Sectors);
		if (SubRays <= 1 || Sectors <= 0) { Out.push_back(C); return Out; }
		const double Inc = 360.0 / Sectors;
		const double Span = Inc * (SubRays - 1) / SubRays;   // sektör genişliğinin içinde eşit aralıklı
		for (int k = 0; k < SubRays; ++k)
		{
			double B = C - Span / 2 + Span * k / (SubRays - 1);
			B = std::fmod(B, 360.0);
			if (B < 0) B += 360.0;
			Out.push_back(B);
		}
		return Out;
	}

	uint16_t RangeToCm(double DistM, double MinM, double MaxM)
	{
		const double MaxCm = std::floor(MaxM * 100.0 + 0.5);
		if (DistM < 0 || DistM > MaxM) return (uint16_t)std::min(65534.0, MaxCm + 1);
		const double Cm = std::floor(DistM * 100.0 + 0.5);
		const double MinCm = std::floor(MinM * 100.0 + 0.5);
		return (uint16_t)std::max(MinCm, std::min(Cm, MaxCm));
	}

	void ObstacleArray(const double* DistM, int N, double MinM, double MaxM, uint16_t Out[72])
	{
		for (int i = 0; i < 72; ++i) Out[i] = 65535;
		for (int i = 0; i < N && i < 72; ++i) Out[i] = RangeToCm(DistM[i], MinM, MaxM);
	}

	bool PhysicsHasRangefinder(const std::string& T)
	{
		return T != "rover";
	}

	bool PhysicsHasProximity(const std::string& T)
	{
		return T == "multirotor" || T == "hexarotor" || T == "helikopter" || T == "rover";
	}

	FPhysParamList PhysicsSensorParams(const std::string& T, bool bAvoid, double RngMaxM)
	{
		FPhysParamList P;
		if (PhysicsHasRangefinder(T))
		{
			// aşağı bakan menzil bulucu: MAVLink (DISTANCE_SENSOR, yön 25 = PITCH_270), metre (ArduPilot 4.6+ adları)
			P.push_back({ "RNGFND1_TYPE", 10 });
			P.push_back({ "RNGFND1_ORIENT", 25 });
			P.push_back({ "RNGFND1_MIN", 0.2 });
			P.push_back({ "RNGFND1_MAX", RngMaxM });
			P.push_back({ "RNGFND1_GNDCLEAR", 0.1 });
			if (T == "sabit_kanat" || T == "vtol" || T == "interceptor") P.push_back({ "RNGFND_LANDING", 1 });
		}
		if (PhysicsHasProximity(T))
		{
			// 360° yakınlık: MAVLink (OBSTACLE_DISTANCE gövde çerçevesi + yukarı DISTANCE_SENSOR yön 24)
			P.push_back({ "PRX1_TYPE", 2 });
			P.push_back({ "PRX1_ORIENT", 0 });
			if (bAvoid)
			{
				P.push_back({ "AVOID_ENABLE", 3 });   // çit + yakınlık sensörü
				P.push_back({ "AVOID_MARGIN", 2 });
				P.push_back({ "AVOID_BEHAVE", 0 });   // kaydır (dur yerine)
			}
			else P.push_back({ "AVOID_ENABLE", 1 });
		}
		return P;
	}
}
