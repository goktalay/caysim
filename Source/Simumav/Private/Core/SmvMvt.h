// Mapbox Vector Tile (MVT 2.1) çözücüsü — Unreal'sız (tests/cpp/test_mvt.py ile g++'ta, mapbox_vector_tile'a karşı sınanır).
// Bölge haritası OpenStreetMap verisini OpenFreeMap karolarından (OpenMapTiles şeması, z14) alır: building, transportation, water,
// landcover, landuse, park katmanları. Geometri karo koordinatında (0..extent, y aşağı); karo kenarına kırpma yardımcıları.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace SmvMvt
{
	struct FPt { double X = 0, Y = 0; };

	struct FValue
	{
		int Kind = 0;          // 0 yok, 1 metin, 2 sayı, 3 mantıksal
		std::string S;
		double D = 0;
	};

	struct FFeature
	{
		int Type = 0;          // 1 nokta, 2 çizgi, 3 çokgen
		uint64_t Id = 0;
		std::vector<std::vector<FPt>> Parts;   // her MoveTo yeni parça (halka / çizgi); çokgen halkaları kapanış noktası olmadan
		std::vector<std::pair<std::string, FValue>> Props;
		std::string Str(const char* Key) const;
		double Num(const char* Key, double Def) const;
	};

	struct FLayer
	{
		std::string Name;
		int Extent = 4096;
		std::vector<FFeature> Features;
	};

	/** false → bozuk / desteklenmeyen veri (sıkıştırılmış olabilir) */
	bool Decode(const uint8_t* Data, size_t Size, std::vector<FLayer>& Out);

	/** Karo koordinatında işaretli alan (MVT: dış halka > 0, delik < 0) */
	double RingArea(const std::vector<FPt>& R);
	/** Çokgen halkasını [0, E] karesine kırp (Sutherland–Hodgman) */
	std::vector<FPt> ClipRing(const std::vector<FPt>& R, double E);
	/** Çizgiyi [0, E] karesine kırp; kare içinde kalan parçalar */
	std::vector<std::vector<FPt>> ClipLine(const std::vector<FPt>& L, double E);
}
