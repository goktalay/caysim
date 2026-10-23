// tests/cpp/test_glb.py yardımcısı: SmvGlb ile her GLB'yi ayrıştırıp özet JSON satırı yazar (düğüm/ağ/üçgen sayısı, dünya sınır kutusu, adlı düğüm konumları).
#include "SmvGlb.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

int main(int Argc, char** Argv)
{
	for (int a = 1; a < Argc; ++a)
	{
		std::ifstream F(Argv[a], std::ios::binary);
		std::vector<uint8_t> B((std::istreambuf_iterator<char>(F)), std::istreambuf_iterator<char>());
		SmvCore::FGlbModel M;
		std::string Err;
		if (!M.Parse(B.data(), B.size(), Err)) { std::printf("{\"file\": \"%s\", \"err\": \"%s\"}\n", Argv[a], Err.c_str()); continue; }
		double Lo[3] = { 1e30, 1e30, 1e30 }, Hi[3] = { -1e30, -1e30, -1e30 };
		for (const SmvCore::FGlbNode& N : M.Nodes)
		{
			if (N.Mesh < 0) continue;
			for (const SmvCore::FGlbPrimitive& P : M.Meshes[N.Mesh].Prims)
				for (size_t i = 0; i < P.Pos.size(); i += 3)
				{
					double W[3];
					SmvCore::FGlbModel::TransformPoint(N.World, &P.Pos[i], W);
					for (int c = 0; c < 3; ++c) { if (W[c] < Lo[c]) Lo[c] = W[c]; if (W[c] > Hi[c]) Hi[c] = W[c]; }
				}
		}
		size_t Img = 0;
		for (const auto& I : M.Images) Img += I.Bytes.size();
		std::printf("{\"file\": \"%s\", \"nodes\": %zu, \"meshes\": %zu, \"mats\": %zu, \"images\": %zu, \"img_bytes\": %zu, \"tris\": %zu, \"lo\": [%.4f, %.4f, %.4f], \"hi\": [%.4f, %.4f, %.4f], \"named\": {",
			Argv[a], M.Nodes.size(), M.Meshes.size(), M.Materials.size(), M.Images.size(), Img, M.TriangleCount(), Lo[0], Lo[1], Lo[2], Hi[0], Hi[1], Hi[2]);
		bool bFirst = true;
		for (const SmvCore::FGlbNode& N : M.Nodes)
		{
			if (N.Name.empty()) continue;
			std::printf("%s\"%s\": [%.4f, %.4f, %.4f]", bFirst ? "" : ", ", N.Name.c_str(), N.World[12], N.World[13], N.World[14]);
			bFirst = false;
		}
		std::printf("}}\n");
	}
	return 0;
}
