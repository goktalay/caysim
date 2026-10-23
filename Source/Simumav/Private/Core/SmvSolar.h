// SIMUMAV — güneş konumu ve gün olayları (NOAA güneş hesabı; ±1 dk). Yalnız C++17, Unreal'sız (tests/cpp/test_solar.py sınar).
// Kullanım: atmosfer (Ultra Dynamic Sky "Simulate Real Sun" saati, yerleşik gökte güneş yönü), parkur saati seçimi (gündüz / alacakaranlık / gece).
#pragma once

namespace SmvCore
{
	struct FSolarPos
	{
		double ElevationDeg = 0;   // ufuktan (kırılma düzeltmeli)
		double AzimuthDeg = 0;     // kuzeyden saat yönünde
	};

	/** Gün olayları (yerel saat, saat cinsinden 0–24; kutup gündüzü/gecesinde Sunrise = Sunset = −1). */
	struct FSolarDay
	{
		double Sunrise = -1, Sunset = -1, SolarNoon = 12;
		double CivilDawn = -1, CivilDusk = -1;   // güneş −6°
	};

	/** Jülyen günü (UTC). */
	double JulianDay(int Year, int Month, int Day, double HourUtc);
	/** Enlem/boylam (derece, doğu +), tarih ve UTC saatinde güneş konumu. */
	FSolarPos SolarPosition(double LatDeg, double LonDeg, int Year, int Month, int Day, double HourUtc);
	/** Yerel saat dilimiyle (UTC+TzHours) gün olayları. */
	FSolarDay SolarDayEvents(double LatDeg, double LonDeg, int Year, int Month, int Day, double TzHours);
	/** Konumun yaklaşık saat dilimi: boylam/15 yuvarlanmış; Türkiye kutusu (UTC+3) gibi bilinen sapmalar düzeltilir. */
	double ApproxTimeZone(double LatDeg, double LonDeg);
	/** Parkur saati (day | dusk | night | dawn) → yerel saat (0–24): gündüz öğle+1 sa, alacakaranlık gün batımı −20 dk (altın saat),
	 *  şafak gün doğumu +10 dk, gece gün batımı +2,5 sa (kutup koşullarında sabit 13 / 19,5 / 23 / 6). */
	double TimeOfDayHours(const char* TimeOfDay, double LatDeg, double LonDeg, int Year, int Month, int Day, double TzHours);
}
