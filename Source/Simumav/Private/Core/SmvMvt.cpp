// Mapbox Vector Tile çözücüsü (bkz. SmvMvt.h).
#include "SmvMvt.h"
#include <cmath>
#include <cstring>

namespace SmvMvt
{
	namespace
	{
		struct FReader
		{
			const uint8_t* P; const uint8_t* E; bool bOk = true;
			FReader(const uint8_t* A, size_t N) : P(A), E(A + N) {}
			bool More() const { return bOk && P < E; }
			uint64_t Varint()
			{
				uint64_t V = 0; int S = 0;
				while (P < E && S < 64) { const uint8_t B = *P++; V |= (uint64_t)(B & 0x7f) << S; if (!(B & 0x80)) return V; S += 7; }
				bOk = false; return 0;
			}
			/** uzunluk önekli alt bölüm */
			FReader Sub()
			{
				const uint64_t N = Varint();
				if (!bOk || N > (uint64_t)(E - P)) { bOk = false; return FReader(P, 0); }
				FReader R(P, (size_t)N); P += N; return R;
			}
			void Skip(int Wire)
			{
				if (Wire == 0) Varint();
				else if (Wire == 1) { if (E - P < 8) bOk = false; else P += 8; }
				else if (Wire == 2) Sub();
				else if (Wire == 5) { if (E - P < 4) bOk = false; else P += 4; }
				else bOk = false;
			}
			double Fixed64() { double D = 0; if (E - P < 8) { bOk = false; return 0; } std::memcpy(&D, P, 8); P += 8; return D; }
			float Fixed32() { float F = 0; if (E - P < 4) { bOk = false; return 0; } std::memcpy(&F, P, 4); P += 4; return F; }
		};

		int64_t Zig(uint64_t V) { return (int64_t)(V >> 1) ^ -(int64_t)(V & 1); }

		FValue ReadValue(FReader R)
		{
			FValue V;
			while (R.More())
			{
				const uint64_t K = R.Varint(); const int F = (int)(K >> 3), W = (int)(K & 7);
				if (F == 1 && W == 2) { FReader S = R.Sub(); V.Kind = 1; V.S.assign((const char*)S.P, (size_t)(S.E - S.P)); }
				else if (F == 2 && W == 5) { V.Kind = 2; V.D = R.Fixed32(); }
				else if (F == 3 && W == 1) { V.Kind = 2; V.D = R.Fixed64(); }
				else if (F == 4 && W == 0) { V.Kind = 2; V.D = (double)(int64_t)R.Varint(); }
				else if (F == 5 && W == 0) { V.Kind = 2; V.D = (double)R.Varint(); }
				else if (F == 6 && W == 0) { V.Kind = 2; V.D = (double)Zig(R.Varint()); }
				else if (F == 7 && W == 0) { V.Kind = 3; V.D = R.Varint() ? 1.0 : 0.0; }
				else R.Skip(W);
			}
			return V;
		}

		void ReadGeometry(FReader R, int Type, FFeature& Out)
		{
			int64_t X = 0, Y = 0;
			std::vector<uint32_t> C;
			while (R.More()) C.push_back((uint32_t)R.Varint());
			size_t i = 0;
			while (i < C.size())
			{
				const uint32_t Cmd = C[i] & 7, N = C[i] >> 3;
				++i;
				if (Cmd == 1 || Cmd == 2)
				{
					for (uint32_t k = 0; k < N; ++k)
					{
						if (i + 1 >= C.size()) return;   // eksik parametre: bozuk geometri
						X += Zig(C[i]); Y += Zig(C[i + 1]); i += 2;
						if (Cmd == 1 || Out.Parts.empty()) Out.Parts.emplace_back();
						Out.Parts.back().push_back({ (double)X, (double)Y });
					}
				}
				else if (Cmd == 7) { /* ClosePath: halka kapalı kabul edilir (ilk nokta tekrarlanmaz) */ }
				else return;
			}
			(void)Type;
		}
	}

	std::string FFeature::Str(const char* Key) const
	{
		for (const auto& P : Props) if (P.first == Key && P.second.Kind == 1) return P.second.S;
		return std::string();
	}

	double FFeature::Num(const char* Key, double Def) const
	{
		for (const auto& P : Props) if (P.first == Key && P.second.Kind >= 2) return P.second.D;
		return Def;
	}

	bool Decode(const uint8_t* Data, size_t Size, std::vector<FLayer>& Out)
	{
		Out.clear();
		if (Size >= 2 && Data[0] == 0x1f && Data[1] == 0x8b) return false;   // gzip: önce açılmalı
		FReader T(Data, Size);
		while (T.More())
		{
			const uint64_t K = T.Varint(); const int F = (int)(K >> 3), W = (int)(K & 7);
			if (F != 3 || W != 2) { T.Skip(W); continue; }
			FReader L = T.Sub();
			FLayer Ly;
			std::vector<std::string> Keys;
			std::vector<FValue> Vals;
			struct FRaw { int Type = 0; uint64_t Id = 0; std::vector<uint32_t> Tags; const uint8_t* G = nullptr; size_t GN = 0; };
			std::vector<FRaw> Raw;
			while (L.More())
			{
				const uint64_t K2 = L.Varint(); const int F2 = (int)(K2 >> 3), W2 = (int)(K2 & 7);
				if (F2 == 1 && W2 == 2) { FReader S = L.Sub(); Ly.Name.assign((const char*)S.P, (size_t)(S.E - S.P)); }
				else if (F2 == 2 && W2 == 2)
				{
					FReader Fe = L.Sub();
					FRaw R;
					while (Fe.More())
					{
						const uint64_t K3 = Fe.Varint(); const int F3 = (int)(K3 >> 3), W3 = (int)(K3 & 7);
						if (F3 == 1 && W3 == 0) R.Id = Fe.Varint();
						else if (F3 == 2 && W3 == 2) { FReader Tg = Fe.Sub(); while (Tg.More()) R.Tags.push_back((uint32_t)Tg.Varint()); }
						else if (F3 == 3 && W3 == 0) R.Type = (int)Fe.Varint();
						else if (F3 == 4 && W3 == 2) { FReader G = Fe.Sub(); R.G = G.P; R.GN = (size_t)(G.E - G.P); }
						else Fe.Skip(W3);
					}
					if (!Fe.bOk) return false;
					Raw.push_back(R);
				}
				else if (F2 == 3 && W2 == 2) { FReader S = L.Sub(); Keys.emplace_back((const char*)S.P, (size_t)(S.E - S.P)); }
				else if (F2 == 4 && W2 == 2) Vals.push_back(ReadValue(L.Sub()));
				else if (F2 == 5 && W2 == 0) Ly.Extent = (int)L.Varint();
				else L.Skip(W2);
			}
			if (!L.bOk) return false;
			for (const FRaw& R : Raw)
			{
				FFeature Fo;
				Fo.Type = R.Type; Fo.Id = R.Id;
				for (size_t i = 0; i + 1 < R.Tags.size(); i += 2)
					if (R.Tags[i] < Keys.size() && R.Tags[i + 1] < Vals.size()) Fo.Props.emplace_back(Keys[R.Tags[i]], Vals[R.Tags[i + 1]]);
				if (R.G) ReadGeometry(FReader(R.G, R.GN), R.Type, Fo);
				Ly.Features.push_back(std::move(Fo));
			}
			Out.push_back(std::move(Ly));
		}
		return T.bOk;
	}

	double RingArea(const std::vector<FPt>& R)
	{
		double S = 0;
		for (size_t i = 0, n = R.size(); i < n; ++i) { const FPt& A = R[i]; const FPt& B = R[(i + 1) % n]; S += A.X * B.Y - B.X * A.Y; }
		return S * 0.5;
	}

	std::vector<FPt> ClipRing(const std::vector<FPt>& R, double E)
	{
		std::vector<FPt> Cur = R;
		for (int Edge = 0; Edge < 4 && !Cur.empty(); ++Edge)
		{
			auto In = [Edge, E](const FPt& P) { return Edge == 0 ? P.X >= 0 : Edge == 1 ? P.X <= E : Edge == 2 ? P.Y >= 0 : P.Y <= E; };
			auto Cut = [Edge, E](const FPt& A, const FPt& B)
			{
				const double V = Edge == 0 ? 0 : Edge == 1 ? E : Edge == 2 ? 0 : E;
				const double T = Edge < 2 ? (V - A.X) / (B.X - A.X) : (V - A.Y) / (B.Y - A.Y);
				return FPt{ A.X + (B.X - A.X) * T, A.Y + (B.Y - A.Y) * T };
			};
			std::vector<FPt> Next;
			for (size_t i = 0, n = Cur.size(); i < n; ++i)
			{
				const FPt& A = Cur[i]; const FPt& B = Cur[(i + 1) % n];
				const bool ia = In(A), ib = In(B);
				if (ia && ib) Next.push_back(B);
				else if (ia && !ib) Next.push_back(Cut(A, B));
				else if (!ia && ib) { Next.push_back(Cut(A, B)); Next.push_back(B); }
			}
			Cur.swap(Next);
		}
		return Cur;
	}

	std::vector<std::vector<FPt>> ClipLine(const std::vector<FPt>& L, double E)
	{
		std::vector<std::vector<FPt>> Out;
		std::vector<FPt> Run;
		auto Flush = [&]() { if (Run.size() >= 2) Out.push_back(Run); Run.clear(); };
		for (size_t i = 0; i + 1 < L.size(); ++i)
		{
			// Liang–Barsky
			const FPt A = L[i], B = L[i + 1];
			double T0 = 0, T1 = 1;
			const double Dx = B.X - A.X, Dy = B.Y - A.Y;
			const double Pp[4] = { -Dx, Dx, -Dy, Dy }, Qq[4] = { A.X, E - A.X, A.Y, E - A.Y };
			bool bIn = true;
			for (int k = 0; k < 4 && bIn; ++k)
			{
				if (std::fabs(Pp[k]) < 1e-12) { if (Qq[k] < 0) bIn = false; continue; }
				const double T = Qq[k] / Pp[k];
				if (Pp[k] < 0) { if (T > T1) bIn = false; else if (T > T0) T0 = T; }
				else { if (T < T0) bIn = false; else if (T < T1) T1 = T; }
			}
			if (!bIn) { Flush(); continue; }
			const FPt C{ A.X + Dx * T0, A.Y + Dy * T0 }, D{ A.X + Dx * T1, A.Y + Dy * T1 };
			if (Run.empty() || T0 > 0) { Flush(); Run.push_back(C); }
			Run.push_back(D);
			if (T1 < 1) Flush();
		}
		Flush();
		return Out;
	}
}
