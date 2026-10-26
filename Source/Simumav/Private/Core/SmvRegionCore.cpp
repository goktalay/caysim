// Gerçek dünya bölge paketi çekirdeği (bkz. SmvRegionCore.h).
#include "SmvRegionCore.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace SmvRegion
{
	static constexpr double PI_D = 3.14159265358979323846;
	static constexpr double R_EARTH = 6378137.0;   // SmvCore::R_EARTH ile aynı (yerel düzlem)
	static inline double Rad(double D) { return D * PI_D / 180.0; }
	static inline double Deg(double R) { return R * 180.0 / PI_D; }

	void LonLatToPixel(double Lon, double Lat, int Z, double& Px, double& Py)
	{
		const double N = 256.0 * (double)(1u << Z);
		const double La = std::max(-85.05112878, std::min(85.05112878, Lat));
		const double S = std::sin(Rad(La));
		Px = (Lon + 180.0) / 360.0 * N;
		Py = (0.5 - std::log((1 + S) / (1 - S)) / (4 * PI_D)) * N;
	}

	void PixelToLonLat(double Px, double Py, int Z, double& Lon, double& Lat)
	{
		const double N = 256.0 * (double)(1u << Z);
		Lon = Px / N * 360.0 - 180.0;
		const double Yn = 0.5 - Py / N;
		Lat = 90.0 - 360.0 * std::atan(std::exp(-Yn * 2 * PI_D)) / PI_D;
	}

	double MetersPerPixel(double Lat, int Z)
	{
		return 2 * PI_D * R_EARTH * std::cos(Rad(Lat)) / (256.0 * (double)(1u << Z));
	}

	void TileRange(double Lat, double Lon, double RadiusM, int Z, int& X0, int& Y0, int& X1, int& Y1)
	{
		double La0, Lo0, La1, Lo1;
		EnuToLatLon(Lat, Lon, -RadiusM, RadiusM, La0, Lo0);   // kuzey-batı
		EnuToLatLon(Lat, Lon, RadiusM, -RadiusM, La1, Lo1);   // güney-doğu
		double Ax, Ay, Bx, By;
		LonLatToPixel(Lo0, La0, Z, Ax, Ay);
		LonLatToPixel(Lo1, La1, Z, Bx, By);
		const int Nt = 1 << Z;
		X0 = std::max(0, (int)std::floor(Ax / 256.0)); Y0 = std::max(0, (int)std::floor(Ay / 256.0));
		X1 = std::min(Nt - 1, (int)std::floor(Bx / 256.0)); Y1 = std::min(Nt - 1, (int)std::floor(By / 256.0));
	}

	void TileBounds(int Z, int X, int Y, double& South, double& West, double& North, double& East)
	{
		PixelToLonLat(X * 256.0, Y * 256.0, Z, West, North);
		PixelToLonLat((X + 1) * 256.0, (Y + 1) * 256.0, Z, East, South);
	}

	void EnuToLatLon(double Lat0, double Lon0, double E, double N, double& Lat, double& Lon)
	{
		Lat = Lat0 + Deg(N / R_EARTH);
		Lon = Lon0 + Deg(E / (R_EARTH * std::cos(Rad(Lat0))));
	}

	void LatLonToEnu(double Lat0, double Lon0, double Lat, double Lon, double& E, double& N)
	{
		N = Rad(Lat - Lat0) * R_EARTH;
		E = Rad(Lon - Lon0) * R_EARTH * std::cos(Rad(Lat0));
	}

	void GeodeticToEcef(double LonDeg, double LatDeg, double H, double Out[3])
	{
		const double A = 6378137.0, F = 1.0 / 298.257223563, E2 = F * (2 - F);
		const double La = Rad(LatDeg), Lo = Rad(LonDeg);
		const double Sl = std::sin(La), Cl = std::cos(La);
		const double Nr = A / std::sqrt(1 - E2 * Sl * Sl);
		Out[0] = (Nr + H) * Cl * std::cos(Lo);
		Out[1] = (Nr + H) * Cl * std::sin(Lo);
		Out[2] = (Nr * (1 - E2) + H) * Sl;
	}

	bool SegmentHitsBox(const double Box[12], const double A[3], const double B[3])
	{
		// kutu uzayında her yarı eksen için s(t) = ((A + t(B−A)) − C)·u / |u|² ∈ [−1, 1]; t ∈ [0, 1] aralıkları kesiştirilir
		double T0 = 0.0, T1 = 1.0;
		for (int k = 0; k < 3; ++k)
		{
			const double* U = Box + 3 + k * 3;
			const double L2 = U[0] * U[0] + U[1] * U[1] + U[2] * U[2];
			if (L2 <= 1e-9) continue;   // yassı kutu ekseni (Google'da olmaz)
			const double S0 = ((A[0] - Box[0]) * U[0] + (A[1] - Box[1]) * U[1] + (A[2] - Box[2]) * U[2]) / L2;
			const double Ds = ((B[0] - A[0]) * U[0] + (B[1] - A[1]) * U[1] + (B[2] - A[2]) * U[2]) / L2;
			if (std::fabs(Ds) < 1e-12)
			{
				if (S0 < -1.0 || S0 > 1.0) return false;
				continue;
			}
			double Ta = (-1.0 - S0) / Ds, Tb = (1.0 - S0) / Ds;
			if (Ta > Tb) std::swap(Ta, Tb);
			T0 = std::max(T0, Ta); T1 = std::min(T1, Tb);
			if (T0 > T1) return false;
		}
		return true;
	}

	bool PointInRegion(const double Reg[6], double LonRad, double LatRad, double H0, double H1)
	{
		const double W = Reg[0], S = Reg[1], E = Reg[2], N = Reg[3];
		if (LatRad < S || LatRad > N) return false;
		bool bLon = W <= E ? (LonRad >= W && LonRad <= E) : (LonRad >= W || LonRad <= E);   // 180° geçişi
		if (!bLon) return false;
		return !(H1 < Reg[4] || H0 > Reg[5]);
	}

	bool SegmentHitsSphere(const double Sph[4], const double A[3], const double B[3])
	{
		const double D[3] = { B[0] - A[0], B[1] - A[1], B[2] - A[2] };
		const double L2 = D[0] * D[0] + D[1] * D[1] + D[2] * D[2];
		double T = L2 > 0 ? ((Sph[0] - A[0]) * D[0] + (Sph[1] - A[1]) * D[1] + (Sph[2] - A[2]) * D[2]) / L2 : 0.0;
		T = std::max(0.0, std::min(1.0, T));
		const double P[3] = { A[0] + D[0] * T - Sph[0], A[1] + D[1] * T - Sph[1], A[2] + D[2] * T - Sph[2] };
		return P[0] * P[0] + P[1] * P[1] + P[2] * P[2] <= Sph[3] * Sph[3];
	}

	double BoxSize(const double Box[12])
	{
		double M = 0;
		for (int k = 0; k < 3; ++k)
		{
			const double* U = Box + 3 + k * 3;
			M = std::max(M, std::sqrt(U[0] * U[0] + U[1] * U[1] + U[2] * U[2]));
		}
		return 2.0 * M;
	}

	std::vector<FXY> CircleSamples(double RadiusM, int Rings)
	{
		std::vector<FXY> Out;
		Out.push_back({ 0, 0 });
		for (int k = 1; k <= Rings; ++k)
		{
			const double R = RadiusM * k / Rings;
			const int N = std::max(6, (int)std::lround(2 * PI_D * k));
			const double Ph = (k % 2) ? 0.0 : PI_D / N;   // halkalar arası kaydırma
			for (int i = 0; i < N; ++i)
			{
				const double A = Ph + 2 * PI_D * i / N;
				Out.push_back({ R * std::cos(A), R * std::sin(A) });
			}
		}
		return Out;
	}

	double SignedArea(const std::vector<FXY>& P)
	{
		double S = 0;
		for (size_t i = 0, n = P.size(); i < n; ++i)
		{
			const FXY& A = P[i]; const FXY& B = P[(i + 1) % n];
			S += A.X * B.Y - B.X * A.Y;
		}
		return S * 0.5;
	}

	bool PointInPolygon(const std::vector<FXY>& P, double X, double Y)
	{
		bool In = false;
		for (size_t i = 0, j = P.size() - 1; i < P.size(); j = i++)
		{
			if (((P[i].Y > Y) != (P[j].Y > Y)) && (X < (P[j].X - P[i].X) * (Y - P[i].Y) / (P[j].Y - P[i].Y) + P[i].X)) In = !In;
		}
		return In;
	}

	bool CleanRing(std::vector<FXY>& P, double MinEdgeM)
	{
		if (P.size() >= 2)
		{
			const FXY& A = P.front(); const FXY& B = P.back();
			if (std::fabs(A.X - B.X) < 1e-6 && std::fabs(A.Y - B.Y) < 1e-6) P.pop_back();
		}
		std::vector<FXY> Q;
		for (const FXY& X : P)
		{
			if (!Q.empty() && std::hypot(X.X - Q.back().X, X.Y - Q.back().Y) < MinEdgeM) continue;
			Q.push_back(X);
		}
		while (Q.size() >= 2 && std::hypot(Q.front().X - Q.back().X, Q.front().Y - Q.back().Y) < MinEdgeM) Q.pop_back();
		// doğrusal köşeler
		bool bChanged = true;
		while (bChanged && Q.size() > 3)
		{
			bChanged = false;
			for (size_t i = 0; i < Q.size() && Q.size() > 3; ++i)
			{
				const FXY& A = Q[(i + Q.size() - 1) % Q.size()]; const FXY& B = Q[i]; const FXY& C = Q[(i + 1) % Q.size()];
				const double Cr = (B.X - A.X) * (C.Y - B.Y) - (B.Y - A.Y) * (C.X - B.X);
				const double L = std::hypot(B.X - A.X, B.Y - A.Y) * std::hypot(C.X - B.X, C.Y - B.Y);
				if (L <= 0 || std::fabs(Cr) / L < 0.004) { Q.erase(Q.begin() + i); bChanged = true; break; }
			}
		}
		if (Q.size() < 3) return false;
		if (SignedArea(Q) < 0) std::reverse(Q.begin(), Q.end());
		P.swap(Q);
		return std::fabs(SignedArea(P)) > 0.5;
	}

	static bool InTri(const FXY& P, const FXY& A, const FXY& B, const FXY& C)
	{
		const double D1 = (P.X - B.X) * (A.Y - B.Y) - (A.X - B.X) * (P.Y - B.Y);
		const double D2 = (P.X - C.X) * (B.Y - C.Y) - (B.X - C.X) * (P.Y - C.Y);
		const double D3 = (P.X - A.X) * (C.Y - A.Y) - (C.X - A.X) * (P.Y - A.Y);
		const bool Neg = (D1 < 0) || (D2 < 0) || (D3 < 0), Pos = (D1 > 0) || (D2 > 0) || (D3 > 0);
		return !(Neg && Pos);
	}

	void Triangulate(const std::vector<FXY>& P, std::vector<int>& OutTri)
	{
		OutTri.clear();
		const int N = (int)P.size();
		if (N < 3) return;
		std::vector<int> V(N);
		for (int i = 0; i < N; ++i) V[i] = i;
		if (SignedArea(P) < 0) std::reverse(V.begin(), V.end());
		int Guard = 0;
		while (V.size() > 3 && Guard++ < N * N)
		{
			bool bCut = false;
			for (size_t i = 0; i < V.size(); ++i)
			{
				const int Ia = V[(i + V.size() - 1) % V.size()], Ib = V[i], Ic = V[(i + 1) % V.size()];
				const FXY& A = P[Ia]; const FXY& B = P[Ib]; const FXY& C = P[Ic];
				const double Cr = (B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X);
				if (Cr <= 1e-9) continue;   // içbükey köşe
				bool bEmpty = true;
				for (int j : V)
				{
					if (j == Ia || j == Ib || j == Ic) continue;
					if (InTri(P[j], A, B, C)) { bEmpty = false; break; }
				}
				if (!bEmpty) continue;
				OutTri.push_back(Ia); OutTri.push_back(Ib); OutTri.push_back(Ic);
				V.erase(V.begin() + i);
				bCut = true;
				break;
			}
			if (!bCut) break;
		}
		if (V.size() == 3) { OutTri.push_back(V[0]); OutTri.push_back(V[1]); OutTri.push_back(V[2]); return; }
		if (V.size() > 3)
		{
			// bozuk (kendini kesen) çokgen: kalan köşelerden yelpaze
			for (size_t i = 1; i + 1 < V.size(); ++i) { OutTri.push_back(V[0]); OutTri.push_back(V[i]); OutTri.push_back(V[i + 1]); }
		}
	}

	void MinAreaRect(const std::vector<FXY>& P, FXY& Center, double& HalfL, double& HalfW, double& Angle)
	{
		double Best = 1e300;
		Center = { 0, 0 }; HalfL = HalfW = 0; Angle = 0;
		for (size_t i = 0; i < P.size(); ++i)
		{
			const FXY& A = P[i]; const FXY& B = P[(i + 1) % P.size()];
			const double L = std::hypot(B.X - A.X, B.Y - A.Y);
			if (L < 1e-6) continue;
			const double Ux = (B.X - A.X) / L, Uy = (B.Y - A.Y) / L;
			double Mn0 = 1e300, Mx0 = -1e300, Mn1 = 1e300, Mx1 = -1e300;
			for (const FXY& Q : P)
			{
				const double S = Q.X * Ux + Q.Y * Uy, T = -Q.X * Uy + Q.Y * Ux;
				Mn0 = std::min(Mn0, S); Mx0 = std::max(Mx0, S); Mn1 = std::min(Mn1, T); Mx1 = std::max(Mx1, T);
			}
			const double Ar = (Mx0 - Mn0) * (Mx1 - Mn1);
			if (Ar < Best)
			{
				Best = Ar;
				const double Cs = (Mn0 + Mx0) * 0.5, Ct = (Mn1 + Mx1) * 0.5;
				Center = { Cs * Ux - Ct * Uy, Cs * Uy + Ct * Ux };
				double Hl = (Mx0 - Mn0) * 0.5, Hw = (Mx1 - Mn1) * 0.5, An = std::atan2(Uy, Ux);
				if (Hw > Hl) { std::swap(Hl, Hw); An += PI_D * 0.5; }
				HalfL = Hl; HalfW = Hw; Angle = An;
			}
		}
	}

	double ParseNumber(const std::string& S)
	{
		if (S.empty()) return -1.0;
		const char* C = S.c_str();
		while (*C == ' ') ++C;
		char* End = nullptr;
		const double V = std::strtod(C, &End);
		if (End == C) return -1.0;
		// "40 ft" / "40'"
		std::string Rest(End);
		if (Rest.find("ft") != std::string::npos || Rest.find('\'') != std::string::npos) return V * 0.3048;
		return V;
	}

	static double Hash01(uint64_t X)
	{
		X ^= X >> 33; X *= 0xff51afd7ed558ccdULL; X ^= X >> 33; X *= 0xc4ceb9fe1a85ec53ULL; X ^= X >> 33;
		return (double)(X >> 11) / 9007199254740992.0;
	}

	double BuildingHeight(const std::string& Building, const std::string& Height, const std::string& Levels, const std::string& RoofLevels, uint64_t Seed, double AreaM2)
	{
		const double H = ParseNumber(Height);
		if (H > 1.5 && H < 900.0) return H;
		const double Lv = ParseNumber(Levels);
		const double Rl = std::max(0.0, ParseNumber(RoofLevels));
		if (Lv >= 1.0 && Lv < 200.0) return (Lv + Rl * 0.6) * 3.1 + 1.2;
		const double U = Hash01(Seed);
		const std::string& B = Building;
		if (B == "garage" || B == "garages" || B == "shed" || B == "kiosk" || B == "hut" || B == "carport" || B == "toilets") return 2.6 + U * 0.8;
		if (B == "house" || B == "detached" || B == "bungalow" || B == "farm" || B == "cabin" || B == "semidetached_house") return 4.5 + std::floor(U * 2.0) * 3.0;
		if (B == "industrial" || B == "warehouse" || B == "hangar" || B == "manufacture" || B == "retail" || B == "supermarket") return 7.0 + U * 6.0;
		if (B == "mosque" || B == "church" || B == "cathedral") return 14.0 + U * 8.0;
		if (B == "greenhouse" || B == "roof" || B == "ruins" || B == "construction") return 3.0 + U * 2.0;
		if (B == "apartments" || B == "residential" || B == "commercial" || B == "office" || B == "hotel")
			return (4.0 + std::floor(U * 5.0)) * 3.1 + 1.2;   // 4–8 kat (Türkiye konut dokusu)
		// building=yes (çoğunluk): küçük taban → 2–3 kat, orta → 3–6, büyük → 4–8
		const double Floors = AreaM2 < 90.0 ? 2.0 + std::floor(U * 2.0) : AreaM2 < 400.0 ? 3.0 + std::floor(U * 4.0) : 4.0 + std::floor(U * 5.0);
		return Floors * 3.1 + 1.2;
	}

	double RoadWidth(const std::string& Hw, const std::string& Lanes)
	{
		const double Ln = ParseNumber(Lanes);
		double W = 0;
		if (Hw == "motorway" || Hw == "trunk") W = 14.0;
		else if (Hw == "motorway_link" || Hw == "trunk_link") W = 7.0;
		else if (Hw == "primary") W = 11.0;
		else if (Hw == "secondary") W = 9.0;
		else if (Hw == "tertiary" || Hw == "primary_link" || Hw == "secondary_link") W = 7.5;
		else if (Hw == "residential" || Hw == "unclassified" || Hw == "living_street" || Hw == "tertiary_link") W = 6.0;
		else if (Hw == "service") W = 4.0;
		else if (Hw == "pedestrian") W = 5.0;
		else if (Hw == "track") W = 3.0;
		else if (Hw == "footway" || Hw == "path" || Hw == "cycleway" || Hw == "steps") W = 1.8;
		else return 0.0;
		if (Ln >= 1.0 && Ln <= 10.0 && W >= 6.0) W = std::max(W, Ln * 3.3);
		return W;
	}

	std::vector<FXY> Resample(const std::vector<FXY>& P, double StepM)
	{
		// köşeler korunur; her kenar ceil(L / Step) eşit parçaya bölünür
		std::vector<FXY> Out;
		if (P.empty()) return Out;
		Out.push_back(P[0]);
		for (size_t i = 1; i < P.size(); ++i)
		{
			const FXY A = P[i - 1], B = P[i];
			const double L = std::hypot(B.X - A.X, B.Y - A.Y);
			if (L < 1e-6) continue;
			const int K = std::max(1, (int)std::ceil(L / std::max(0.1, StepM)));
			for (int k = 1; k <= K; ++k) Out.push_back({ A.X + (B.X - A.X) * k / K, A.Y + (B.Y - A.Y) * k / K });
		}
		return Out;
	}
}
