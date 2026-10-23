// SIMUMAV — NOAA güneş hesabı (bkz. SmvSolar.h). Kaynak: NOAA Global Monitoring Laboratory "Solar Calculation Details" (Meeus).
#include "SmvSolar.h"
#include <cmath>
#include <cstring>

namespace SmvCore
{
	namespace
	{
		constexpr double Pi = 3.14159265358979323846;
		double Rad(double D) { return D * Pi / 180.0; }
		double Deg(double R) { return R * 180.0 / Pi; }

		struct FSun { double DeclRad, EqTimeMin; };

		FSun SunAt(double Jd)
		{
			const double T = (Jd - 2451545.0) / 36525.0;
			const double L0 = std::fmod(280.46646 + T * (36000.76983 + T * 0.0003032), 360.0);
			const double M = 357.52911 + T * (35999.05029 - 0.0001537 * T);
			const double E = 0.016708634 - T * (0.000042037 + 0.0000001267 * T);
			const double C = std::sin(Rad(M)) * (1.914602 - T * (0.004817 + 0.000014 * T)) + std::sin(Rad(2 * M)) * (0.019993 - 0.000101 * T)
				+ std::sin(Rad(3 * M)) * 0.000289;
			const double TrueLong = L0 + C;
			const double Omega = 125.04 - 1934.136 * T;
			const double Lambda = TrueLong - 0.00569 - 0.00478 * std::sin(Rad(Omega));
			const double Eps0 = 23.0 + (26.0 + (21.448 - T * (46.815 + T * (0.00059 - T * 0.001813))) / 60.0) / 60.0;
			const double Eps = Eps0 + 0.00256 * std::cos(Rad(Omega));
			const double Decl = std::asin(std::sin(Rad(Eps)) * std::sin(Rad(Lambda)));
			const double Y = std::tan(Rad(Eps / 2)) * std::tan(Rad(Eps / 2));
			const double Eq = 4.0 * Deg(Y * std::sin(2 * Rad(L0)) - 2 * E * std::sin(Rad(M)) + 4 * E * Y * std::sin(Rad(M)) * std::cos(2 * Rad(L0))
				- 0.5 * Y * Y * std::sin(4 * Rad(L0)) - 1.25 * E * E * std::sin(2 * Rad(M)));
			return { Decl, Eq };
		}

		/** Güneşin verilen yükseklik açısına ulaştığı saat açısı (derece); ulaşmıyorsa < 0. */
		double HourAngle(double LatDeg, double DeclRad, double ZenithDeg)
		{
			const double C = std::cos(Rad(ZenithDeg)) / (std::cos(Rad(LatDeg)) * std::cos(DeclRad)) - std::tan(Rad(LatDeg)) * std::tan(DeclRad);
			if (C > 1.0 || C < -1.0) return -1.0;
			return Deg(std::acos(C));
		}
	}

	double JulianDay(int Year, int Month, int Day, double HourUtc)
	{
		if (Month <= 2) { Year -= 1; Month += 12; }
		const int A = Year / 100;
		const int B = 2 - A + A / 4;
		return std::floor(365.25 * (Year + 4716)) + std::floor(30.6001 * (Month + 1)) + Day + B - 1524.5 + HourUtc / 24.0;
	}

	FSolarPos SolarPosition(double LatDeg, double LonDeg, int Year, int Month, int Day, double HourUtc)
	{
		const FSun S = SunAt(JulianDay(Year, Month, Day, HourUtc));
		double Tst = std::fmod(HourUtc * 60.0 + S.EqTimeMin + 4.0 * LonDeg, 1440.0);
		if (Tst < 0) Tst += 1440.0;
		double Ha = Tst / 4.0 - 180.0;
		const double CosZ = std::sin(Rad(LatDeg)) * std::sin(S.DeclRad) + std::cos(Rad(LatDeg)) * std::cos(S.DeclRad) * std::cos(Rad(Ha));
		const double Zen = Deg(std::acos(std::fmax(-1.0, std::fmin(1.0, CosZ))));
		double El = 90.0 - Zen;
		// atmosferik kırılma (NOAA yaklaşığı)
		double Ref = 0;
		if (El < 85.0)
		{
			const double Te = std::tan(Rad(El));
			if (El > 5.0) Ref = 58.1 / Te - 0.07 / (Te * Te * Te) + 0.000086 / std::pow(Te, 5);
			else if (El > -0.575) Ref = 1735.0 + El * (-518.2 + El * (103.4 + El * (-12.79 + El * 0.711)));
			else Ref = -20.772 / Te;
			Ref /= 3600.0;
		}
		El += Ref;
		const double SinZ = std::sin(Rad(Zen));
		double Az = 0;
		if (SinZ > 1e-6)
		{
			const double CosAz = (std::sin(Rad(LatDeg)) * std::cos(Rad(Zen)) - std::sin(S.DeclRad)) / (std::cos(Rad(LatDeg)) * SinZ);
			const double A = Deg(std::acos(std::fmax(-1.0, std::fmin(1.0, CosAz))));
			Az = Ha > 0 ? std::fmod(A + 180.0, 360.0) : std::fmod(540.0 - A, 360.0);
		}
		return { El, Az };
	}

	FSolarDay SolarDayEvents(double LatDeg, double LonDeg, int Year, int Month, int Day, double TzHours)
	{
		FSolarDay D;
		// öğle civarındaki güneş değerleriyle (±1 dk yeterli)
		const double NoonUtcGuess = 12.0 - LonDeg / 15.0;
		const FSun S = SunAt(JulianDay(Year, Month, Day, NoonUtcGuess));
		const double NoonMin = 720.0 - 4.0 * LonDeg - S.EqTimeMin + TzHours * 60.0;
		D.SolarNoon = NoonMin / 60.0;
		const double Ha = HourAngle(LatDeg, S.DeclRad, 90.833);
		if (Ha >= 0)
		{
			D.Sunrise = (NoonMin - 4.0 * Ha) / 60.0;
			D.Sunset = (NoonMin + 4.0 * Ha) / 60.0;
		}
		const double Hc = HourAngle(LatDeg, S.DeclRad, 96.0);
		if (Hc >= 0)
		{
			D.CivilDawn = (NoonMin - 4.0 * Hc) / 60.0;
			D.CivilDusk = (NoonMin + 4.0 * Hc) / 60.0;
		}
		return D;
	}

	double ApproxTimeZone(double LatDeg, double LonDeg)
	{
		// Türkiye (UTC+3, yaz saati yok) ve yakın çevresi; diğerleri boylamdan (resmi dilim sınırları burada tutulmaz)
		if (LatDeg > 35.5 && LatDeg < 42.5 && LonDeg > 25.5 && LonDeg < 45.0) return 3.0;
		return std::round(LonDeg / 15.0);
	}

	double TimeOfDayHours(const char* Tod, double LatDeg, double LonDeg, int Year, int Month, int Day, double TzHours)
	{
		const FSolarDay D = SolarDayEvents(LatDeg, LonDeg, Year, Month, Day, TzHours);
		const bool bNormal = D.Sunrise >= 0 && D.Sunset > D.Sunrise;
		auto Wrap = [](double H) { H = std::fmod(H, 24.0); return H < 0 ? H + 24.0 : H; };
		if (Tod && std::strcmp(Tod, "night") == 0) return bNormal ? Wrap(D.Sunset + 2.5) : 23.0;
		if (Tod && std::strcmp(Tod, "dusk") == 0) return bNormal ? Wrap(D.Sunset - 20.0 / 60.0) : 19.5;
		if (Tod && std::strcmp(Tod, "dawn") == 0) return bNormal ? Wrap(D.Sunrise + 10.0 / 60.0) : 6.0;
		return bNormal ? Wrap(D.SolarNoon + 1.0) : 13.0;
	}
}
