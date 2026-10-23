// SIMUMAV — GLB okuyucu (bkz. SmvGlb.h). Küçük, bağımlılıksız JSON ayrıştırıcı içerir (yalnız glTF'nin ihtiyacı kadar).
#include "SmvGlb.h"
#include <cmath>
#include <cstring>
#include <map>
#include <memory>

namespace SmvCore
{
	namespace
	{
		// ---------------- en küçük JSON ağacı ----------------
		struct FJ
		{
			enum EType { Null, Bool, Num, Str, Arr, Obj } T = Null;
			bool B = false;
			double N = 0;
			std::string S;
			std::vector<FJ> A;
			std::vector<std::pair<std::string, FJ>> O;

			const FJ* Get(const char* K) const
			{
				if (T != Obj) return nullptr;
				for (const auto& P : O) if (P.first == K) return &P.second;
				return nullptr;
			}
			double NumOr(const char* K, double D) const { const FJ* V = Get(K); return (V && V->T == Num) ? V->N : D; }
			int IntOr(const char* K, int D) const { const FJ* V = Get(K); return (V && V->T == Num) ? (int)V->N : D; }
			std::string StrOr(const char* K, const std::string& D) const { const FJ* V = Get(K); return (V && V->T == Str) ? V->S : D; }
			bool BoolOr(const char* K, bool D) const { const FJ* V = Get(K); return (V && V->T == Bool) ? V->B : D; }
		};

		struct FJParser
		{
			const char* P;
			const char* E;
			bool bErr = false;

			void Ws() { while (P < E && (*P == ' ' || *P == '\n' || *P == '\r' || *P == '\t')) ++P; }
			bool Lit(const char* L) { size_t n = std::strlen(L); if ((size_t)(E - P) >= n && std::memcmp(P, L, n) == 0) { P += n; return true; } return false; }

			static void Utf8(std::string& O, unsigned C)
			{
				if (C < 0x80) O += (char)C;
				else if (C < 0x800) { O += (char)(0xC0 | (C >> 6)); O += (char)(0x80 | (C & 0x3F)); }
				else { O += (char)(0xE0 | (C >> 12)); O += (char)(0x80 | ((C >> 6) & 0x3F)); O += (char)(0x80 | (C & 0x3F)); }
			}

			bool Str(std::string& O)
			{
				if (P >= E || *P != '"') return false;
				++P;
				while (P < E && *P != '"')
				{
					if (*P == '\\' && P + 1 < E)
					{
						++P;
						const char C = *P++;
						if (C == 'n') O += '\n'; else if (C == 't') O += '\t'; else if (C == 'r') O += '\r'; else if (C == 'b') O += '\b'; else if (C == 'f') O += '\f';
						else if (C == 'u' && E - P >= 4)
						{
							unsigned V = 0;
							for (int i = 0; i < 4; ++i) { const char H = *P++; V = V * 16 + (unsigned)(H >= '0' && H <= '9' ? H - '0' : (H | 32) - 'a' + 10); }
							Utf8(O, V);
						}
						else O += C;
					}
					else O += *P++;
				}
				if (P >= E) return false;
				++P;
				return true;
			}

			bool Val(FJ& V, int Depth)
			{
				if (Depth > 64) return false;
				Ws();
				if (P >= E) return false;
				const char C = *P;
				if (C == '{')
				{
					V.T = FJ::Obj; ++P; Ws();
					if (P < E && *P == '}') { ++P; return true; }
					while (true)
					{
						Ws();
						std::string K;
						if (!Str(K)) return false;
						Ws();
						if (P >= E || *P != ':') return false;
						++P;
						V.O.emplace_back(std::move(K), FJ());
						if (!Val(V.O.back().second, Depth + 1)) return false;
						Ws();
						if (P < E && *P == ',') { ++P; continue; }
						if (P < E && *P == '}') { ++P; return true; }
						return false;
					}
				}
				if (C == '[')
				{
					V.T = FJ::Arr; ++P; Ws();
					if (P < E && *P == ']') { ++P; return true; }
					while (true)
					{
						V.A.emplace_back();
						if (!Val(V.A.back(), Depth + 1)) return false;
						Ws();
						if (P < E && *P == ',') { ++P; continue; }
						if (P < E && *P == ']') { ++P; return true; }
						return false;
					}
				}
				if (C == '"') { V.T = FJ::Str; return Str(V.S); }
				if (Lit("true")) { V.T = FJ::Bool; V.B = true; return true; }
				if (Lit("false")) { V.T = FJ::Bool; V.B = false; return true; }
				if (Lit("null")) { V.T = FJ::Null; return true; }
				// sayı (yerel ayardan bağımsız elle)
				const char* S0 = P;
				double Sign = 1;
				if (*P == '-') { Sign = -1; ++P; }
				double Ip = 0;
				bool bAny = false;
				while (P < E && *P >= '0' && *P <= '9') { Ip = Ip * 10 + (*P - '0'); ++P; bAny = true; }
				double Fp = 0, Sc = 1;
				if (P < E && *P == '.') { ++P; while (P < E && *P >= '0' && *P <= '9') { Fp = Fp * 10 + (*P - '0'); Sc *= 10; ++P; bAny = true; } }
				int Ex = 0;
				if (P < E && (*P == 'e' || *P == 'E'))
				{
					++P;
					int Es = 1;
					if (P < E && (*P == '+' || *P == '-')) { if (*P == '-') Es = -1; ++P; }
					while (P < E && *P >= '0' && *P <= '9') { Ex = Ex * 10 + (*P - '0'); ++P; }
					Ex *= Es;
				}
				if (!bAny) { P = S0; return false; }
				V.T = FJ::Num;
				V.N = Sign * (Ip + Fp / Sc) * std::pow(10.0, Ex);
				return true;
			}
		};

		void MatIdentity(double M[16]) { for (int i = 0; i < 16; ++i) M[i] = (i % 5 == 0) ? 1 : 0; }

		void MatMul(const double A[16], const double B[16], double O[16])
		{
			// sütun öncelikli: O = A · B
			double R[16];
			for (int c = 0; c < 4; ++c)
				for (int r = 0; r < 4; ++r)
				{
					double S = 0;
					for (int k = 0; k < 4; ++k) S += A[k * 4 + r] * B[c * 4 + k];
					R[c * 4 + r] = S;
				}
			std::memcpy(O, R, sizeof(R));
		}

		void NodeLocal(const FJ& N, double M[16])
		{
			MatIdentity(M);
			if (const FJ* Mx = N.Get("matrix"))
			{
				if (Mx->T == FJ::Arr && Mx->A.size() == 16) for (int i = 0; i < 16; ++i) M[i] = Mx->A[i].N;
				return;
			}
			double T[3] = { 0, 0, 0 }, Q[4] = { 0, 0, 0, 1 }, S[3] = { 1, 1, 1 };
			if (const FJ* V = N.Get("translation")) if (V->T == FJ::Arr && V->A.size() == 3) for (int i = 0; i < 3; ++i) T[i] = V->A[i].N;
			if (const FJ* V = N.Get("rotation")) if (V->T == FJ::Arr && V->A.size() == 4) for (int i = 0; i < 4; ++i) Q[i] = V->A[i].N;
			if (const FJ* V = N.Get("scale")) if (V->T == FJ::Arr && V->A.size() == 3) for (int i = 0; i < 3; ++i) S[i] = V->A[i].N;
			const double x = Q[0], y = Q[1], z = Q[2], w = Q[3];
			const double R[9] = { 1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w),     // sütun 0
				2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w),                       // sütun 1
				2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y) };                     // sütun 2
			for (int c = 0; c < 3; ++c) for (int r = 0; r < 3; ++r) M[c * 4 + r] = R[c * 3 + r] * S[c];
			M[12] = T[0]; M[13] = T[1]; M[14] = T[2];
		}

		struct FAccessorView
		{
			const uint8_t* Base = nullptr;
			size_t Count = 0, Stride = 0;
			int Comp = 0, NComp = 0;
			bool bNorm = false;
			bool Ok() const { return Base != nullptr; }
			double At(size_t i, int c) const
			{
				const uint8_t* P = Base + i * Stride;
				switch (Comp)
				{
				case 5126: { float f; std::memcpy(&f, P + c * 4, 4); return f; }
				case 5120: { int8_t v = (int8_t)P[c]; return bNorm ? std::max(-1.0, v / 127.0) : v; }
				case 5121: { uint8_t v = P[c]; return bNorm ? v / 255.0 : v; }
				case 5122: { int16_t v; std::memcpy(&v, P + c * 2, 2); return bNorm ? std::max(-1.0, v / 32767.0) : v; }
				case 5123: { uint16_t v; std::memcpy(&v, P + c * 2, 2); return bNorm ? v / 65535.0 : v; }
				case 5125: { uint32_t v; std::memcpy(&v, P + c * 4, 4); return v; }
				default: return 0;
				}
			}
		};

		int CompSize(int C) { return (C == 5120 || C == 5121) ? 1 : (C == 5122 || C == 5123) ? 2 : (C == 5125 || C == 5126) ? 4 : 0; }
		int TypeN(const std::string& T) { return T == "SCALAR" ? 1 : T == "VEC2" ? 2 : T == "VEC3" ? 3 : T == "VEC4" ? 4 : T == "MAT4" ? 16 : 0; }
	}

	void FGlbModel::TransformPoint(const double M[16], const float* P, double Out[3])
	{
		for (int r = 0; r < 3; ++r) Out[r] = M[r] * P[0] + M[4 + r] * P[1] + M[8 + r] * P[2] + M[12 + r];
	}

	void FGlbModel::TransformDir(const double M[16], const float* D, double Out[3])
	{
		for (int r = 0; r < 3; ++r) Out[r] = M[r] * D[0] + M[4 + r] * D[1] + M[8 + r] * D[2];
		const double L = std::sqrt(Out[0] * Out[0] + Out[1] * Out[1] + Out[2] * Out[2]);
		if (L > 1e-12) for (int r = 0; r < 3; ++r) Out[r] /= L;
	}

	size_t FGlbModel::TriangleCount() const
	{
		size_t N = 0;
		for (const FGlbNode& Nd : Nodes)
			if (Nd.Mesh >= 0 && Nd.Mesh < (int)Meshes.size()) for (const FGlbPrimitive& P : Meshes[Nd.Mesh].Prims) N += P.Idx.size() / 3;
		return N;
	}

	bool FGlbModel::Parse(const uint8_t* Data, size_t Size, std::string& Err)
	{
		*this = FGlbModel();
		auto U32 = [Data](size_t O) { uint32_t v; std::memcpy(&v, Data + O, 4); return v; };
		if (Size < 20 || std::memcmp(Data, "glTF", 4) != 0) { Err = "GLB değil (glTF imzası yok)"; return false; }
		if (U32(4) != 2) { Err = "glTF sürümü 2 değil"; return false; }
		const size_t Total = std::min<size_t>(U32(8), Size);
		const char* JsonP = nullptr;
		size_t JsonN = 0;
		const uint8_t* Bin = nullptr;
		size_t BinN = 0;
		for (size_t O = 12; O + 8 <= Total;)
		{
			const uint32_t Len = U32(O), Type = U32(O + 4);
			if (O + 8 + Len > Total) { Err = "GLB parçası kesik"; return false; }
			if (Type == 0x4E4F534A) { JsonP = (const char*)Data + O + 8; JsonN = Len; }
			else if (Type == 0x004E4942) { Bin = Data + O + 8; BinN = Len; }
			O += 8 + ((Len + 3) & ~3u);
		}
		if (!JsonP) { Err = "JSON parçası yok"; return false; }
		FJ Root;
		FJParser Pr{ JsonP, JsonP + JsonN };
		if (!Pr.Val(Root, 0) || Root.T != FJ::Obj) { Err = "JSON ayrıştırılamadı"; return false; }
		if (const FJ* A = Root.Get("asset")) Generator = A->StrOr("generator", "");

		const FJ* BVs = Root.Get("bufferViews");
		const FJ* Accs = Root.Get("accessors");
		auto View = [&](int Ai, FAccessorView& V) -> bool
		{
			if (!Accs || Ai < 0 || Ai >= (int)Accs->A.size() || !BVs) return false;
			const FJ& A = Accs->A[Ai];
			const int Bv = A.IntOr("bufferView", -1);
			if (Bv < 0 || Bv >= (int)BVs->A.size()) return false;
			if (A.Get("sparse")) return false;
			const FJ& B = BVs->A[Bv];
			if (B.IntOr("buffer", 0) != 0 || !Bin) return false;
			V.Comp = A.IntOr("componentType", 0);
			V.NComp = TypeN(A.StrOr("type", ""));
			V.Count = (size_t)A.NumOr("count", 0);
			V.bNorm = A.BoolOr("normalized", false);
			const int Cs = CompSize(V.Comp);
			if (!Cs || !V.NComp) return false;
			const size_t Elem = (size_t)Cs * V.NComp;
			V.Stride = (size_t)B.NumOr("byteStride", 0);
			if (V.Stride == 0) V.Stride = Elem;
			const size_t Off = (size_t)B.NumOr("byteOffset", 0) + (size_t)A.NumOr("byteOffset", 0);
			const size_t Need = V.Count ? Off + (V.Count - 1) * V.Stride + Elem : Off;
			if (Need > BinN || Off + 0 > BinN) return false;
			V.Base = Bin + Off;
			return true;
		};

		// malzemeler
		if (const FJ* Ms = Root.Get("materials"))
		{
			for (const FJ& M : Ms->A)
			{
				FGlbMaterial O;
				O.Name = M.StrOr("name", "");
				O.bDoubleSided = M.BoolOr("doubleSided", false);
				O.bBlend = M.StrOr("alphaMode", "") == "BLEND";
				if (const FJ* Pb = M.Get("pbrMetallicRoughness"))
				{
					if (const FJ* C = Pb->Get("baseColorFactor")) if (C->A.size() == 4) for (int i = 0; i < 4; ++i) O.BaseColor[i] = (float)C->A[i].N;
					O.Metallic = (float)Pb->NumOr("metallicFactor", 1.0);
					O.Roughness = (float)Pb->NumOr("roughnessFactor", 1.0);
					if (const FJ* T = Pb->Get("baseColorTexture"))
					{
						const int Ti = T->IntOr("index", -1);
						const FJ* Ts = Root.Get("textures");
						if (Ts && Ti >= 0 && Ti < (int)Ts->A.size()) O.BaseColorImage = Ts->A[Ti].IntOr("source", -1);
					}
				}
				if (const FJ* Em = M.Get("emissiveFactor")) if (Em->A.size() == 3) for (int i = 0; i < 3; ++i) O.Emissive[i] = (float)Em->A[i].N;
				Materials.push_back(O);
			}
		}
		// görseller (yalnız bufferView'de gömülü)
		if (const FJ* Is = Root.Get("images"))
		{
			for (const FJ& I : Is->A)
			{
				FGlbImage O;
				O.Mime = I.StrOr("mimeType", "");
				const int Bv = I.IntOr("bufferView", -1);
				if (BVs && Bin && Bv >= 0 && Bv < (int)BVs->A.size())
				{
					const FJ& B = BVs->A[Bv];
					const size_t Off = (size_t)B.NumOr("byteOffset", 0), Len = (size_t)B.NumOr("byteLength", 0);
					if (Off + Len <= BinN) O.Bytes.assign(Bin + Off, Bin + Off + Len);
				}
				Images.push_back(std::move(O));
			}
		}
		// ağlar
		if (const FJ* Ms = Root.Get("meshes"))
		{
			for (const FJ& M : Ms->A)
			{
				FGlbMesh O;
				O.Name = M.StrOr("name", "");
				if (const FJ* Ps = M.Get("primitives"))
				{
					for (const FJ& P : Ps->A)
					{
						if (P.IntOr("mode", 4) != 4) continue;
						const FJ* At = P.Get("attributes");
						if (!At) continue;
						FAccessorView Vp, Vn, Vu, Vi;
						if (!View(At->IntOr("POSITION", -1), Vp) || Vp.NComp != 3) continue;
						FGlbPrimitive G;
						G.Material = P.IntOr("material", -1);
						G.Pos.resize(Vp.Count * 3);
						for (size_t i = 0; i < Vp.Count; ++i) for (int c = 0; c < 3; ++c) G.Pos[i * 3 + c] = (float)Vp.At(i, c);
						if (View(At->IntOr("NORMAL", -1), Vn) && Vn.NComp == 3 && Vn.Count == Vp.Count)
						{
							G.Nrm.resize(Vn.Count * 3);
							for (size_t i = 0; i < Vn.Count; ++i) for (int c = 0; c < 3; ++c) G.Nrm[i * 3 + c] = (float)Vn.At(i, c);
						}
						if (View(At->IntOr("TEXCOORD_0", -1), Vu) && Vu.NComp == 2 && Vu.Count == Vp.Count)
						{
							G.Uv.resize(Vu.Count * 2);
							for (size_t i = 0; i < Vu.Count; ++i) for (int c = 0; c < 2; ++c) G.Uv[i * 2 + c] = (float)Vu.At(i, c);
						}
						if (View(P.IntOr("indices", -1), Vi) && Vi.NComp == 1)
						{
							G.Idx.resize(Vi.Count);
							for (size_t i = 0; i < Vi.Count; ++i) G.Idx[i] = (uint32_t)Vi.At(i, 0);
						}
						else
						{
							G.Idx.resize(Vp.Count);
							for (size_t i = 0; i < Vp.Count; ++i) G.Idx[i] = (uint32_t)i;
						}
						G.Idx.resize(G.Idx.size() / 3 * 3);
						bool bBad = false;
						for (uint32_t I : G.Idx) if (I >= Vp.Count) { bBad = true; break; }
						if (bBad) continue;
						O.Prims.push_back(std::move(G));
					}
				}
				Meshes.push_back(std::move(O));
			}
		}
		// düğümler
		if (const FJ* Ns = Root.Get("nodes"))
		{
			Nodes.resize(Ns->A.size());
			for (size_t i = 0; i < Ns->A.size(); ++i)
			{
				const FJ& N = Ns->A[i];
				FGlbNode& O = Nodes[i];
				O.Name = N.StrOr("name", "");
				O.Mesh = N.IntOr("mesh", -1);
				if (O.Mesh >= (int)Meshes.size()) O.Mesh = -1;
				NodeLocal(N, O.Local);
				if (const FJ* Cs = N.Get("children"))
					for (const FJ& C : Cs->A) if (C.T == FJ::Num && C.N >= 0 && C.N < (double)Ns->A.size()) O.Children.push_back((int)C.N);
			}
			for (size_t i = 0; i < Nodes.size(); ++i) for (int C : Nodes[i].Children) if (Nodes[C].Parent < 0 && C != (int)i) Nodes[C].Parent = (int)i;
		}
		// kökler: sahne düğümleri (yoksa ebeveynsizler)
		if (const FJ* Scs = Root.Get("scenes"))
		{
			const int Si = Root.IntOr("scene", 0);
			if (Si >= 0 && Si < (int)Scs->A.size())
				if (const FJ* Rn = Scs->A[Si].Get("nodes")) for (const FJ& R : Rn->A) if (R.N >= 0 && R.N < (double)Nodes.size()) Roots.push_back((int)R.N);
		}
		if (Roots.empty()) for (size_t i = 0; i < Nodes.size(); ++i) if (Nodes[i].Parent < 0) Roots.push_back((int)i);
		// dünya dönüşümleri (döngüye karşı derinlik sınırı)
		std::vector<int> Stack;
		std::vector<char> Seen(Nodes.size(), 0);
		for (int R : Roots)
		{
			if (Seen[R]) continue;
			std::memcpy(Nodes[R].World, Nodes[R].Local, sizeof(Nodes[R].World));
			Seen[R] = 1;
			Stack.push_back(R);
			while (!Stack.empty())
			{
				const int I = Stack.back();
				Stack.pop_back();
				for (int C : Nodes[I].Children)
				{
					if (Seen[C]) continue;
					Seen[C] = 1;
					Nodes[C].Parent = I;
					MatMul(Nodes[I].World, Nodes[C].Local, Nodes[C].World);
					Stack.push_back(C);
				}
			}
		}
		// sahnede olmayan düğümler çizilmez
		for (size_t i = 0; i < Nodes.size(); ++i) if (!Seen[i]) Nodes[i].Mesh = -1;
		return true;
	}
}
