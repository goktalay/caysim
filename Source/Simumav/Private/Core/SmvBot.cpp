// SIMUMAV — yapay zekâ pilot (bkz. SmvBot.h).
#include "SmvBot.h"
#include <algorithm>
#include <cmath>

namespace SmvCore
{
	static constexpr double PI_B = 3.14159265358979323846;
	static double Wrap180B(double D) { D = std::fmod(D + 180.0, 360.0); if (D < 0) D += 360.0; return D - 180.0; }

	FBotLevel FBotLevel::Get(const std::string& Lv)
	{
		// flightsim_match_core.BOT_SEVIYE
		if (Lv == "easy") return { 0.55, 1.5, 45, 0.45, 0.15, 30, false, 30, 0.15, 7, 45, 0, false, 0.9, 0.75 };
		if (Lv == "hard") return { 0.95, 0.25, 120, 0.06, 0, 50, true, 3, 0.8, 2.8, 95, 1, true, 0.72, 1 };
		return { 0.75, 0.7, 75, 0.22, 0.03, 40, false, 12, 0.45, 4.5, 70, 0.5, true, 0.8, 1 };
	}

	FBotPilot::FBotPilot(const std::string& Level, EVClass InC, uint64_t Seed) : L(FBotLevel::Get(Level)), C(InC), Rng(Seed * 2654435761ULL + 1)
	{
		const bool Plane = C == EVClass::Plane;
		Vmax = (Plane ? 30.0 : 16.0) * L.Speed;
		if (Plane) Vmax = std::max(Vmax, 15.0);
		Floor = (Plane ? 35.0 : 8.0) * L.FloorMargin;
		Climb = Plane ? 5 : 3;
		Sink = Plane ? 4 : 2;
		ApproachL = Plane ? 110.0 * std::max(1.0, (Vmax / 22.0) * (Vmax / 22.0)) : 22.0;
	}

	double FBotPilot::NextRand()
	{
		// xorshift64* — botun saçılması için (sunucunun rastgele akışıyla birebir olması gerekmez)
		Rng ^= Rng >> 12; Rng ^= Rng << 25; Rng ^= Rng >> 27;
		return (double)((Rng * 2685821657736338717ULL) >> 11) / 9007199254740992.0;
	}

	void FBotPilot::Place(double X, double Y, double Z, double H)
	{
		P = FBotPose(); P.X = X; P.Y = Y; P.Z = Z; P.HeadingDeg = H; bAirborne = false;
	}

	void FBotPilot::Steer(double Dt, double Tx, double Ty, double Tz, double Want)
	{
		const double Dx = Tx - P.X, Dy = Ty - P.Y;
		const double Bearing = std::atan2(Dx, Dy) * 180.0 / PI_B;
		const double Err = Wrap180B(Bearing - P.HeadingDeg);
		const double MaxTurn = L.TurnDps * Dt;
		const double Turn = std::max(-MaxTurn, std::min(MaxTurn, Err));
		P.HeadingDeg = std::fmod(P.HeadingDeg + Turn + 360.0, 360.0);
		double V = Want;
		if (C == EVClass::Vertical) V = Want * (0.35 + 0.65 * std::max(0.0, std::cos((Err - Turn) * PI_B / 180.0)));
		else V = std::max(15.0, Want);
		P.Speed = V;
		const double Vz = std::max(-Sink, std::min(Climb, 0.6 * (std::max(Floor, Tz) - P.Z)));
		const double H = P.HeadingDeg * PI_B / 180.0;
		P.X += std::sin(H) * V * Dt;
		P.Y += std::cos(H) * V * Dt;
		P.Z += Vz * Dt;
		P.PitchRad = std::atan2(Vz, std::max(1.0, V));
		P.RollRad = C == EVClass::Plane ? std::atan(V * (Turn / std::max(1e-3, Dt)) * PI_B / 180.0 / 9.80665) : 0.0;
	}

	void FBotPilot::StepRings(double Dt, const std::vector<FRing>& Rings, int Next)
	{
		if (Dt <= 0) return;
		if (!bAirborne)
		{
			// kalkış: dikey tırmanma (uçakta ileri ivmelenerek)
			const double Alt0 = Rings.empty() ? 20.0 : std::max(C == EVClass::Plane ? 40.0 : 10.0, Rings[std::min<size_t>(Next, Rings.size() - 1)].AglM);
			P.Z += Climb * Dt;
			if (C == EVClass::Plane) { const double H = P.HeadingDeg * PI_B / 180.0; P.Speed = std::min(Vmax, P.Speed + 3 * Dt); P.X += std::sin(H) * P.Speed * Dt; P.Y += std::cos(H) * P.Speed * Dt; }
			if (P.Z >= 0.7 * Alt0) bAirborne = true;
			return;
		}
		if (Next < 0 || Next >= (int)Rings.size()) { Steer(Dt, P.X + std::sin(P.HeadingDeg * PI_B / 180.0) * 50, P.Y + std::cos(P.HeadingDeg * PI_B / 180.0) * 50, P.Z, Vmax * 0.5); return; }
		const FRing& R = Rings[Next];
		if (AimRing != Next) { AimRing = Next; Aim(R, true); bWasInFront = false; }
		const double Cx = R.X + AimOx, Cy = R.Y + AimOy, Cz = R.AglM + AimOz;
		const double Rel = (P.X - R.X) * R.Nx + (P.Y - R.Y) * R.Ny;   // < 0: halkanın önünde
		const double Lx = Cx - P.X, Ly = Cy - P.Y;
		const double Dist = std::hypot(Lx, Ly);
		const double Off = std::fabs(Wrap180B(std::atan2(Lx, Ly) * 180.0 / PI_B - std::atan2(R.Nx, R.Ny) * 180.0 / PI_B));
		if (Rel < 0) bWasInFront = true;
		else if (bWasInFront) { bWasInFront = false; Aim(R, false); }   // ıskaladı: ikinci deneme saçılmayla, ıska olasılığı yok
		if (Rel < 0 && Off <= L.EntryDeg)
		{
			Steer(Dt, Cx + R.Nx * 0.6 * ApproachL, Cy + R.Ny * 0.6 * ApproachL, Cz, Vmax);   // "geç" kipi
		}
		else if (Rel >= 0 && Dist < 3 * R.RadiusM + ApproachL)
		{
			Steer(Dt, Cx + R.Ny * 3 * R.RadiusM - R.Nx * ApproachL, Cy - R.Nx * 3 * R.RadiusM - R.Ny * ApproachL, Cz, Vmax);   // arkadan dolaş
		}
		else
		{
			Steer(Dt, Cx - R.Nx * ApproachL, Cy - R.Ny * ApproachL, Cz, Vmax);   // yaklaşma noktası
		}
	}

	void FBotPilot::Aim(const FRing& R, bool bAllowMiss)
	{
		// halka merkezinden kaçıklık: ilk denemede `iska` olasılıkla halka dışı (1,45R), aksi R·sapma·√U
		const bool bMiss = bAllowMiss && NextRand() < L.Miss;
		const double Rr = bMiss ? 1.45 * R.RadiusM : R.RadiusM * L.Scatter * std::sqrt(NextRand());
		const double A = NextRand() * 2 * PI_B;
		AimOx = Rr * std::cos(A) * R.Ny;   // yanal (normal'e dik)
		AimOy = -Rr * std::cos(A) * R.Nx;
		AimOz = 0.6 * Rr * std::sin(A);
	}

	void FBotPilot::StepDogfight(double Dt, double T, const FBotPose& O, bool bChaser, double AreaR, double RangeM, double AltM)
	{
		if (Dt <= 0) return;
		if (!bAirborne)
		{
			P.Z += Climb * Dt;
			if (C == EVClass::Plane) { const double H = P.HeadingDeg * PI_B / 180.0; P.Speed = std::min(Vmax, P.Speed + 3 * Dt); P.X += std::sin(H) * P.Speed * Dt; P.Y += std::cos(H) * P.Speed * Dt; }
			if (P.Z >= 0.7 * AltM) bAirborne = true;
			return;
		}
		// tahmin hatası (sunucuyla aynı biçim; faz sabit)
		const double E = L.PredErrM;
		const double Ox = O.X + E * std::sin(0.37 * T + 0.3), Oy = O.Y + E * std::cos(0.29 * T + 1.1), Oz = O.Z + 0.25 * E * std::sin(0.21 * T + 2.0);
		double Tx, Ty, Tz, V = Vmax;
		const double D = std::sqrt((Ox - P.X) * (Ox - P.X) + (Oy - P.Y) * (Oy - P.Y) + (Oz - P.Z) * (Oz - P.Z));
		if (bChaser)
		{
			const double Tau = std::min(4.0, std::max(0.0, D / std::max(5.0, Vmax))) * L.Lead;
			const double Oh = O.HeadingDeg * PI_B / 180.0;
			Tx = Ox + std::sin(Oh) * O.Speed * Tau; Ty = Oy + std::cos(Oh) * O.Speed * Tau; Tz = Oz;
			if (C == EVClass::Vertical && D < 0.6 * RangeM) V = std::min(Vmax, std::max(3.0, O.Speed + 0.25 * D));
		}
		else
		{
			ManeuverT -= Dt;
			if (ManeuverT <= 0) { ManeuverT = L.ManeuverS * (0.7 + 0.6 * NextRand()); ManeuverSign = NextRand() < 0.5 ? -1 : 1; }
			const double Away = std::atan2(P.X - Ox, P.Y - Oy) + ManeuverSign * L.ManeuverDeg * PI_B / 180.0 * (0.5 + 0.5 * L.Randomness);
			Tx = P.X + std::sin(Away) * 100; Ty = P.Y + std::cos(Away) * 100;
			Tz = L.bAltGame ? AltM * (1.0 + 0.5 * std::sin(0.2 * T)) : AltM;
		}
		// alan sınırı koruması
		const double Rr = std::hypot(Tx, Ty), Lim = L.AreaThresh * AreaR;
		if (Rr > Lim && Rr > 1e-6) { Tx *= Lim / Rr; Ty *= Lim / Rr; }
		Tz = std::max(Floor, std::min(std::max(2.5 * AltM, Floor + 30), Tz));
		Steer(Dt, Tx, Ty, Tz, V);
	}
}
