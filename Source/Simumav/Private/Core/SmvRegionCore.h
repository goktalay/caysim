// Gerçek dünya bölge paketi çekirdeği (Unreal'sız; tests/cpp/test_region.py ile g++'ta sınanır):
//   - Web Mercator karo matematiği (z/x/y ↔ enlem/boylam, piksel)
//   - Terrarium yükseklik kodu (AWS Terrain Tiles: h = R·256 + G + B/256 − 32768)
//   - WGS84 ECEF dönüşümü ve 3D Tiles sınır hacmi (box / region / sphere) ↔ düşey doğru parçası kesişimi
//     (Google fotogerçekçi 3B kapsama denetimi: örnek noktanın üstündeki en ince karo)
//   - Daire içi örnek noktaları, çokgen alanı / yönü / içerme, kulak kırpma üçgenleme, en küçük alanlı sınır dikdörtgeni
//   - OSM bina yüksekliği sezgisi
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace SmvRegion
{
	struct FXY { double X = 0, Y = 0; };

	// ---- Web Mercator ----
	/** Enlem/boylam → z düzeyinde küresel piksel (256 px karo); X doğuya, Y güneye artar. */
	void LonLatToPixel(double Lon, double Lat, int Z, double& Px, double& Py);
	void PixelToLonLat(double Px, double Py, int Z, double& Lon, double& Lat);
	/** Merkez çevresinde yarıçap (m) daireyi örten karo aralığı (dahil). */
	void TileRange(double Lat, double Lon, double RadiusM, int Z, int& X0, int& Y0, int& X1, int& Y1);
	/** Karonun güney-batı / kuzey-doğu köşeleri */
	void TileBounds(int Z, int X, int Y, double& South, double& West, double& North, double& East);
	/** Yaklaşık metre/piksel (enlemde) */
	double MetersPerPixel(double Lat, int Z);

	// ---- Yerel düzlem (SmvCore::LatLon ile aynı küre yaklaşımı) ----
	void EnuToLatLon(double Lat0, double Lon0, double E, double N, double& Lat, double& Lon);
	void LatLonToEnu(double Lat0, double Lon0, double Lat, double Lon, double& E, double& N);

	// ---- Terrarium ----
	inline double TerrariumHeight(uint8_t R, uint8_t G, uint8_t B) { return (double)R * 256.0 + (double)G + (double)B / 256.0 - 32768.0; }

	// ---- WGS84 / 3D Tiles sınır hacmi ----
	void GeodeticToEcef(double LonDeg, double LatDeg, double H, double Out[3]);
	/** box: 12 sayı (merkez + üç yarı eksen, ECEF m). A–B doğru parçası kutuya değiyor mu */
	bool SegmentHitsBox(const double Box[12], const double A[3], const double B[3]);
	/** region: [batı, güney, doğu, kuzey (radyan), min h, max h] — nokta (radyan) ve yükseklik aralığı */
	bool PointInRegion(const double Reg[6], double LonRad, double LatRad, double H0, double H1);
	/** sphere: [x, y, z, r] */
	bool SegmentHitsSphere(const double Sph[4], const double A[3], const double B[3]);
	/** Kutunun en uzun yarı ekseninin iki katı (m) — karo boyu ölçüsü */
	double BoxSize(const double Box[12]);

	// ---- örnekler ----
	/** Merkez + halkalar: yarıçap R (m), Rings halka (k. halkada max(6, round(2πk)) nokta). Yerel ENU metre. */
	std::vector<FXY> CircleSamples(double RadiusM, int Rings);

	// ---- çokgen ----
	double SignedArea(const std::vector<FXY>& P);
	bool PointInPolygon(const std::vector<FXY>& P, double X, double Y);
	/** Kapalı halkadaki son tekrar ve ardışık çiftleri / doğrusal köşeleri at; en az 3 köşe kalırsa true. Yön CCW yapılır. */
	bool CleanRing(std::vector<FXY>& P, double MinEdgeM = 0.3);
	/** Kulak kırpma (basit çokgen, CCW). Çıktı köşe indisleri (üçlüler). Başarısızsa yelpaze. */
	void Triangulate(const std::vector<FXY>& P, std::vector<int>& OutTri);
	/** En küçük alanlı sınır dikdörtgeni (kenar yönleri denenir): merkez, yarı boylar, açı (radyan, X ekseninden) */
	void MinAreaRect(const std::vector<FXY>& P, FXY& Center, double& HalfL, double& HalfW, double& Angle);

	/** OSM etiketlerinden bina yüksekliği (m): height, building:levels, roof:levels, min_height; yoksa türe göre + kimliğe bağlı çeşitleme.
	 *  Etiket yoksa "" verin. Seed: OSM kimliği (aynı bina her seferinde aynı yükseklik). */
	double BuildingHeight(const std::string& Building, const std::string& Height, const std::string& Levels, const std::string& RoofLevels, uint64_t Seed, double AreaM2);
	/** Yol sınıfı genişliği (m); 0 → çizilmez */
	double RoadWidth(const std::string& Highway, const std::string& Lanes);
	/** Polyline yeniden örnekleme: köşeler korunur, kenarlar en çok Step m aralıkla bölünür */
	std::vector<FXY> Resample(const std::vector<FXY>& P, double StepM);
	/** Basit metin sayısı ("12", "12.5 m", "12;14" → 12) — çözülemezse < 0 */
	double ParseNumber(const std::string& S);
}
