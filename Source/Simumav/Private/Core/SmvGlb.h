// SIMUMAV — glTF 2.0 ikili (GLB) okuyucu: düğüm ağacı, ağlar (üçgen ilkelleri), PBR malzemeler, gömülü görseller.
// Yalnız C++17, Unreal'sız (tests/cpp/test_glb.py gerçek model dosyalarıyla sınar). Desteklenen: POSITION / NORMAL / TEXCOORD_0 (float ya da
// KHR_mesh_quantization tamsayı, normalized dahil), indeks u8/u16/u32 (yoksa sıralı), mode 4 (üçgen), TRS ya da matris, baseColorTexture
// (bufferView'deki PNG/JPEG), emissive, doubleSided, alphaMode BLEND. Sparse erişimciler, morph/skin, animasyon ve dış (URI) tamponlar desteklenmez
// (bu dosyalarda yok). Eksenler dönüştürülmez: çıktı glTF çerçevesindedir (sağ el, Y yukarı); Unreal çevirisi ASmvDroneActor'da.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace SmvCore
{
	struct FGlbPrimitive
	{
		std::vector<float> Pos;       // 3·n
		std::vector<float> Nrm;       // 3·n (yoksa boş)
		std::vector<float> Uv;        // 2·n (yoksa boş)
		std::vector<uint32_t> Idx;    // 3·üçgen
		int Material = -1;
	};

	struct FGlbMesh
	{
		std::string Name;
		std::vector<FGlbPrimitive> Prims;
	};

	struct FGlbNode
	{
		std::string Name;
		int Parent = -1;
		int Mesh = -1;
		std::vector<int> Children;
		/** Yerel dönüşüm, sütun öncelikli 4×4 (glTF ile aynı) */
		double Local[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
		/** Dünya dönüşümü (sahne kökünden) */
		double World[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	};

	struct FGlbMaterial
	{
		std::string Name;
		float BaseColor[4] = { 1, 1, 1, 1 };
		float Metallic = 1, Roughness = 1;
		float Emissive[3] = { 0, 0, 0 };
		int BaseColorImage = -1;
		bool bDoubleSided = false;
		bool bBlend = false;
	};

	struct FGlbImage
	{
		std::string Mime;
		std::vector<uint8_t> Bytes;
	};

	struct FGlbModel
	{
		std::vector<FGlbNode> Nodes;
		std::vector<FGlbMesh> Meshes;
		std::vector<FGlbMaterial> Materials;
		std::vector<FGlbImage> Images;
		std::vector<int> Roots;
		std::string Generator;

		/** GLB baytlarını ayrıştırır; hata metni Err'e. Dünya dönüşümleri de hesaplanır. */
		bool Parse(const uint8_t* Data, size_t Size, std::string& Err);
		/** Düğümün dünya çerçevesindeki noktası. */
		static void TransformPoint(const double M[16], const float* P, double Out[3]);
		static void TransformDir(const double M[16], const float* D, double Out[3]);
		size_t TriangleCount() const;
	};
}
