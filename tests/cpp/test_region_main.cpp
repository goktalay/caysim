// Bölge paketi çekirdeği (SmvRegionCore) sınaması — Unreal'sız; tests/cpp/test_region.py derleyip çalıştırır.
#include "../../Source/Simumav/Private/Core/SmvRegionCore.h"
#include <cmath>
#include <cstdio>

using namespace SmvRegion;
static int Fail = 0;
#define OK(c, ...) do { if (!(c)) { ++Fail; std::printf("HATA: " __VA_ARGS__); std::printf("\n"); } } while (0)

int main()
{
	// Mercator gidiş-dönüş + bilinen karo (Taksim z14 → 9511/6140)
	double Px, Py, Lo, La;
	LonLatToPixel(28.9850, 41.0369, 14, Px, Py);
	OK((int)(Px / 256) == 9511 && (int)(Py / 256) == 6140, "taksim karo %d/%d", (int)(Px / 256), (int)(Py / 256));
	PixelToLonLat(Px, Py, 14, Lo, La);
	OK(std::fabs(Lo - 28.9850) < 1e-9 && std::fabs(La - 41.0369) < 1e-9, "mercator dönüş %f %f", Lo, La);
	int X0, Y0, X1, Y1;
	TileRange(41.0369, 28.9850, 2000, 14, X0, Y0, X1, Y1);
	OK(X1 - X0 >= 2 && X1 - X0 <= 3 && Y1 - Y0 >= 2 && Y1 - Y0 <= 3, "aralık %d..%d %d..%d", X0, X1, Y0, Y1);
	OK(std::fabs(MetersPerPixel(0, 0) - 156543.03) < 0.1, "m/px %f", MetersPerPixel(0, 0));
	// terrarium: 0 m = (128, 0, 0)
	OK(TerrariumHeight(128, 0, 0) == 0.0 && TerrariumHeight(128, 100, 128) == 100.5, "terrarium");
	// ENU gidiş-dönüş
	double E, N; EnuToLatLon(41, 29, 1234, -567, La, Lo); LatLonToEnu(41, 29, La, Lo, E, N);
	OK(std::fabs(E - 1234) < 1e-6 && std::fabs(N + 567) < 1e-6, "enu %f %f", E, N);
	// ECEF: ekvator / başlangıç meridyeni
	double P[3]; GeodeticToEcef(0, 0, 0, P);
	OK(std::fabs(P[0] - 6378137.0) < 1e-6 && std::fabs(P[1]) < 1e-6, "ecef");
	GeodeticToEcef(0, 90, 0, P);
	OK(std::fabs(P[2] - 6356752.314) < 0.01, "kutup %f", P[2]);
	// kutu: noktanın çevresinde 100 m'lik ECEF kutusu; düşey parça içinden geçer, 1 km ötesi geçmez
	double C[3]; GeodeticToEcef(28.985, 41.0369, 50, C);
	double Box[12] = { C[0], C[1], C[2], 100, 0, 0, 0, 100, 0, 0, 0, 100 };
	double A[3], B[3]; GeodeticToEcef(28.985, 41.0369, -100, A); GeodeticToEcef(28.985, 41.0369, 300, B);
	OK(SegmentHitsBox(Box, A, B), "kutu isabet");
	GeodeticToEcef(28.997, 41.0369, -100, A); GeodeticToEcef(28.997, 41.0369, 300, B);
	OK(!SegmentHitsBox(Box, A, B), "kutu ıska");
	// döndürülmüş ince kutu (yarı eksenler eğik)
	double Rb[12] = { 0, 0, 0, 10, 10, 0, -1, 1, 0, 0, 0, 5 };
	double Pa[3] = { 0, 0, -50 }, Pb[3] = { 0, 0, 50 }, Pc[3] = { 8, -8, -50 }, Pd[3] = { 8, -8, 50 };
	OK(SegmentHitsBox(Rb, Pa, Pb) && !SegmentHitsBox(Rb, Pc, Pd), "eğik kutu");
	OK(std::fabs(BoxSize(Box) - 200) < 1e-9, "kutu boyu");
	double Reg[6] = { 0.5, 0.7, 0.51, 0.72, -10, 200 };
	OK(PointInRegion(Reg, 0.505, 0.71, 0, 50) && !PointInRegion(Reg, 0.52, 0.71, 0, 50) && !PointInRegion(Reg, 0.505, 0.71, 300, 400), "region");
	double Sp[4] = { 0, 0, 0, 5 };
	OK(SegmentHitsSphere(Sp, Pa, Pb) && !SegmentHitsSphere(Sp, Pc, Pd), "küre");
	// örnekler
	std::vector<FXY> S = CircleSamples(5000, 5);
	OK(S.size() == 95, "örnek sayısı %d", (int)S.size());
	for (const FXY& Q : S) OK(std::hypot(Q.X, Q.Y) <= 5000.001, "örnek daire dışında");
	// çokgen: CW kare → CCW, alan 100
	std::vector<FXY> Sq = { { 0, 0 }, { 0, 10 }, { 10, 10 }, { 10, 0 }, { 0, 0 } };
	OK(CleanRing(Sq) && Sq.size() == 4 && std::fabs(SignedArea(Sq) - 100) < 1e-9, "kare temizlik");
	OK(PointInPolygon(Sq, 5, 5) && !PointInPolygon(Sq, 15, 5), "içerme");
	// doğrusal köşe atılır
	std::vector<FXY> Ln = { { 0, 0 }, { 5, 0 }, { 10, 0 }, { 10, 10 }, { 0, 10 } };
	OK(CleanRing(Ln) && Ln.size() == 4, "doğrusal köşe %d", (int)Ln.size());
	// L biçimli içbükey çokgen üçgenleme: alan korunur
	std::vector<FXY> L = { { 0, 0 }, { 20, 0 }, { 20, 5 }, { 5, 5 }, { 5, 20 }, { 0, 20 } };
	std::vector<int> T; Triangulate(L, T);
	double Ar = 0;
	for (size_t i = 0; i < T.size(); i += 3)
	{
		const FXY& a = L[T[i]]; const FXY& b = L[T[i + 1]]; const FXY& c = L[T[i + 2]];
		const double Cr = ((b.X - a.X) * (c.Y - a.Y) - (b.Y - a.Y) * (c.X - a.X)) * 0.5;
		OK(Cr > 0, "üçgen yönü");
		Ar += Cr;
	}
	OK(T.size() == 12 && std::fabs(Ar - 175) < 1e-9, "L üçgenleme %d %f", (int)T.size(), Ar);
	// en küçük dikdörtgen: 30° döndürülmüş 20×8
	std::vector<FXY> R;
	const double An = 0.5235987756;
	for (auto Q : { FXY{ -10, -4 }, FXY{ 10, -4 }, FXY{ 10, 4 }, FXY{ -10, 4 } })
		R.push_back({ 50 + Q.X * std::cos(An) - Q.Y * std::sin(An), 7 + Q.X * std::sin(An) + Q.Y * std::cos(An) });
	FXY Cc; double Hl, Hw, Aa; MinAreaRect(R, Cc, Hl, Hw, Aa);
	OK(std::fabs(Hl - 10) < 1e-6 && std::fabs(Hw - 4) < 1e-6 && std::fabs(Cc.X - 50) < 1e-6 && std::fabs(Cc.Y - 7) < 1e-6, "dikdörtgen %f %f", Hl, Hw);
	OK(std::fabs(std::fmod(Aa - An + 3 * 3.14159265358979, 3.14159265358979)) < 1e-6 || std::fabs(std::fmod(Aa - An + 3 * 3.14159265358979, 3.14159265358979) - 3.14159265358979) < 1e-6, "açı %f", Aa);
	// bina yüksekliği
	OK(BuildingHeight("yes", "21", "", "", 1, 100) == 21, "height");
	OK(std::fabs(BuildingHeight("yes", "", "5", "", 1, 100) - 16.7) < 1e-9, "levels");
	OK(std::fabs(BuildingHeight("yes", "40 ft", "", "", 1, 100) - 12.192) < 1e-9, "ft");
	for (uint64_t s = 1; s < 200; ++s)
	{
		const double H = BuildingHeight("yes", "", "", "", s, 250);
		OK(H >= 10.5 && H <= 23.0, "varsayılan %f", H);
		OK(BuildingHeight("yes", "", "", "", s, 250) == H, "kararlı");
	}
	OK(RoadWidth("primary", "") == 11 && RoadWidth("primary", "4") == 13.2 && RoadWidth("bus_stop", "") == 0, "yol");
	std::vector<FXY> Rs = Resample({ { 0, 0 }, { 10, 0 }, { 10, 3 } }, 4);
	OK(Rs.size() == 5 && std::fabs(Rs[1].X - 10.0 / 3) < 1e-9 && Rs.back().Y == 3, "örnekleme %d", (int)Rs.size());
	std::printf(Fail ? "BAŞARISIZ %d\n" : "TAMAM\n", Fail);
	return Fail ? 1 : 0;
}
