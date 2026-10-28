// MVT çözücüsü sınaması: karo dosyasını çözüp katman başına özellik / parça / nokta sayısı ve koordinat toplamı yazar (test_mvt.py karşılaştırır)
#include "../../Source/Simumav/Private/Core/SmvMvt.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

int main(int argc, char** argv)
{
	if (argc < 2) return 2;
	std::ifstream F(argv[1], std::ios::binary);
	std::vector<uint8_t> B((std::istreambuf_iterator<char>(F)), std::istreambuf_iterator<char>());
	std::vector<SmvMvt::FLayer> L;
	if (!SmvMvt::Decode(B.data(), B.size(), L)) { std::printf("HATA\n"); return 1; }
	for (const auto& Ly : L)
	{
		long Parts = 0, Pts = 0, Ext = 0; double Sx = 0, Sy = 0; long Props = 0;
		for (const auto& Fe : Ly.Features)
		{
			Props += (long)Fe.Props.size();
			for (const auto& P : Fe.Parts)
			{
				++Parts; Pts += (long)P.size();
				if (Fe.Type == 3 && SmvMvt::RingArea(P) > 0) ++Ext;
				for (const auto& Q : P) { Sx += Q.X; Sy += Q.Y; }
			}
		}
		std::printf("%s %zu %ld %ld %ld %.0f %.0f %ld\n", Ly.Name.c_str(), Ly.Features.size(), Parts, Pts, Ext, Sx, Sy, Props);
	}
	// kırpma: köşeden taşan 200×200 kare → içte 100×100; çizginin kare içinde tek parçası kalır (0,10)…(10,4096)
	std::vector<SmvMvt::FPt> R = { { -100, -100 }, { 100, -100 }, { 100, 100 }, { -100, 100 } };
	const double A = SmvMvt::RingArea(SmvMvt::ClipRing(R, 4096));
	auto Ls = SmvMvt::ClipLine({ { -10, 10 }, { 10, 10 }, { 10, 5000 }, { 20, 5000 } }, 4096);
	std::printf("CLIP %.0f %zu %.1f\n", A, Ls.size(), Ls.empty() ? -1.0 : Ls[0][0].X);
	return 0;
}
