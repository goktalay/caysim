#include "CaySimGeo.h"

namespace
{
	// WGS84 jeodezik → ECEF (m)
	FVector GeoToEcef(double LonDeg, double LatDeg, double H)
	{
		const double Lat = FMath::DegreesToRadians(LatDeg), Lon = FMath::DegreesToRadians(LonDeg);
		const double sLat = FMath::Sin(Lat), cLat = FMath::Cos(Lat);
		const double N = CaySimGeo::WGS84_A / FMath::Sqrt(1.0 - CaySimGeo::WGS84_E2 * sLat * sLat);
		return FVector((N + H) * cLat * FMath::Cos(Lon), (N + H) * cLat * FMath::Sin(Lon), (N * (1.0 - CaySimGeo::WGS84_E2) + H) * sLat);
	}
	void EcefToGeo(const FVector& P, double& LonDeg, double& LatDeg, double& H)
	{
		const double a = CaySimGeo::WGS84_A, e2 = CaySimGeo::WGS84_E2;
		const double b = a * (1.0 - CaySimGeo::WGS84_F);
		const double ep2 = (a * a - b * b) / (b * b);
		const double p = FMath::Sqrt(P.X * P.X + P.Y * P.Y);
		const double th = FMath::Atan2(a * P.Z, b * p);
		const double Lon = FMath::Atan2(P.Y, P.X);
		const double Lat = FMath::Atan2(P.Z + ep2 * b * FMath::Pow(FMath::Sin(th), 3.0), p - e2 * a * FMath::Pow(FMath::Cos(th), 3.0));
		const double N = a / FMath::Sqrt(1.0 - e2 * FMath::Sin(Lat) * FMath::Sin(Lat));
		H = p / FMath::Cos(Lat) - N;
		LonDeg = FMath::RadiansToDegrees(Lon);
		LatDeg = FMath::RadiansToDegrees(Lat);
	}
	// ECEF → ENU dönüş matrisi satırları (E, N, U) merkez için
	void EnuBasis(double Lon0Deg, double Lat0Deg, FVector& E, FVector& N, FVector& U)
	{
		const double Lat = FMath::DegreesToRadians(Lat0Deg), Lon = FMath::DegreesToRadians(Lon0Deg);
		const double sL = FMath::Sin(Lat), cL = FMath::Cos(Lat), sO = FMath::Sin(Lon), cO = FMath::Cos(Lon);
		E = FVector(-sO, cO, 0.0);
		N = FVector(-sL * cO, -sL * sO, cL);
		U = FVector(cL * cO, cL * sO, sL);
	}
}

FVector CaySimGeo::GeoToEnuMeters(double Lon, double Lat, double HeightM, double Lon0, double Lat0, double Height0)
{
	const FVector P = GeoToEcef(Lon, Lat, HeightM);
	const FVector O = GeoToEcef(Lon0, Lat0, Height0);
	const FVector D = P - O;
	FVector E, N, U;
	EnuBasis(Lon0, Lat0, E, N, U);
	return FVector(FVector::DotProduct(D, E), FVector::DotProduct(D, N), FVector::DotProduct(D, U));
}

void CaySimGeo::EnuMetersToGeo(const FVector& Enu, double Lon0, double Lat0, double Height0, double& Lon, double& Lat, double& HeightM)
{
	FVector E, N, U;
	EnuBasis(Lon0, Lat0, E, N, U);
	const FVector P = GeoToEcef(Lon0, Lat0, Height0) + E * Enu.X + N * Enu.Y + U * Enu.Z;
	EcefToGeo(P, Lon, Lat, HeightM);
}

FRotator CaySimGeo::NedToUnrealRotator(float RollRad, float PitchRad, float YawRad, bool bEnuXNorth)
{
	const float HeadingDeg = FMath::RadiansToDegrees(YawRad);
	const float YawUe = bEnuXNorth ? HeadingDeg : (HeadingDeg - 90.f);
	// FRotator(Pitch, Yaw, Roll)
	return FRotator(FMath::RadiansToDegrees(PitchRad), FRotator::NormalizeAxis(YawUe), FMath::RadiansToDegrees(RollRad));
}

FVector CaySimGeo::NedVelocityToUnreal(const FVector& VelNed, bool bEnuXNorth)
{
	// NED (n, e, d) → ENU (e, n, -d) → Unreal
	const double E = VelNed.Y, N = VelNed.X, U = -VelNed.Z;
	const FVector Unreal = bEnuXNorth ? FVector(N, E, U) : FVector(E, -N, U); // Cesium: X Doğu, Y Güney
	return Unreal * 100.0; // m → cm
}

double CaySimGeo::HaversineM(double Lat1, double Lon1, double Lat2, double Lon2)
{
	const double R = 6371000.0;
	const double p1 = FMath::DegreesToRadians(Lat1), p2 = FMath::DegreesToRadians(Lat2);
	const double dp = p2 - p1, dl = FMath::DegreesToRadians(Lon2 - Lon1);
	const double a = FMath::Sin(dp / 2) * FMath::Sin(dp / 2) + FMath::Cos(p1) * FMath::Cos(p2) * FMath::Sin(dl / 2) * FMath::Sin(dl / 2);
	return 2.0 * R * FMath::Asin(FMath::Sqrt(FMath::Clamp(a, 0.0, 1.0)));
}

double CaySimGeo::BearingDeg(double Lat1, double Lon1, double Lat2, double Lon2)
{
	const double p1 = FMath::DegreesToRadians(Lat1), p2 = FMath::DegreesToRadians(Lat2);
	const double dl = FMath::DegreesToRadians(Lon2 - Lon1);
	const double y = FMath::Sin(dl) * FMath::Cos(p2);
	const double x = FMath::Cos(p1) * FMath::Sin(p2) - FMath::Sin(p1) * FMath::Cos(p2) * FMath::Cos(dl);
	double B = FMath::RadiansToDegrees(FMath::Atan2(y, x));
	if (B < 0) B += 360.0;
	return B;
}
