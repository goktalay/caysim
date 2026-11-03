// Görev rotası üreteçleri — bkz. SmvMissionGen.h
#include "SmvMissionGen.h"

#include <algorithm>
#include <cmath>

namespace SmvCore
{
	namespace
	{
		constexpr double KPi = 3.14159265358979323846;
		constexpr double KMLat = 110574.0;       // m / derece enlem
		constexpr double KMLon = 111320.0;       // m / derece boylam (ekvator)
		constexpr int KMaxItems = 700;           // ArduPilot SITL görev deposu için güvenli üst sınır

		struct FV { double E, N; };

		FMissionItem Nav(int Cmd, double La, double Lo, double Alt)
		{
			FMissionItem I; I.Cmd = Cmd; I.Lat = La; I.Lon = Lo; I.Alt = Alt; I.bNav = true;
			return I;
		}

		/** çokgen içi tarama (boustrophedon): Step aralıklı paralel şeritler, Angle derece (kuzeyden saat yönü) */
		std::vector<FV> Sweep(const std::vector<FV>& Poly, double Step, double AngleDeg)
		{
			std::vector<FV> Out;
			if (Poly.size() < 3 || Step <= 0.1) return Out;
			const double A = AngleDeg * KPi / 180.0;
			// şerit yönü: (sin A, cos A); dik eksen: (cos A, −sin A)
			const double Dx = std::sin(A), Dy = std::cos(A), Px = std::cos(A), Py = -std::sin(A);
			double Mn = 1e18, Mx = -1e18;
			for (const FV& V : Poly) { const double S = V.E * Px + V.N * Py; Mn = std::min(Mn, S); Mx = std::max(Mx, S); }
			bool bFlip = false;
			for (double S = Mn + Step * 0.5; S < Mx; S += Step)
			{
				// S doğrusunun çokgen kenarlarıyla kesişimleri (doğru boyunca parametre t)
				std::vector<double> Ts;
				for (size_t i = 0; i < Poly.size(); ++i)
				{
					const FV& P0 = Poly[i]; const FV& P1 = Poly[(i + 1) % Poly.size()];
					const double S0 = P0.E * Px + P0.N * Py - S, S1 = P1.E * Px + P1.N * Py - S;
					if ((S0 < 0) == (S1 < 0)) continue;
					const double U = S0 / (S0 - S1);
					const double Ex = P0.E + (P1.E - P0.E) * U, Ny = P0.N + (P1.N - P0.N) * U;
					Ts.push_back(Ex * Dx + Ny * Dy);
				}
				if (Ts.size() < 2) continue;
				std::sort(Ts.begin(), Ts.end());
				// içbükey çokgende birden çok parça: çiftler halinde
				std::vector<std::pair<double, double>> Seg;
				for (size_t k = 0; k + 1 < Ts.size(); k += 2) Seg.push_back({ Ts[k], Ts[k + 1] });
				if (bFlip) { std::reverse(Seg.begin(), Seg.end()); for (auto& Sg : Seg) std::swap(Sg.first, Sg.second); }
				for (const auto& Sg : Seg)
				{
					Out.push_back({ S * Px + Sg.first * Dx, S * Py + Sg.first * Dy });
					Out.push_back({ S * Px + Sg.second * Dx, S * Py + Sg.second * Dy });
				}
				bFlip = !bFlip;
				if (Out.size() > (size_t)KMaxItems - 10) break;
			}
			return Out;
		}

		/** çoklu çizginin iki yana W/2 tamponu (koridor kenarı; uçlar yarım daire) */
		std::vector<FV> Buffer(const std::vector<FV>& Line, double W)
		{
			std::vector<FV> L, R;
			const double H = W * 0.5;
			if (Line.size() < 2) return {};
			for (size_t i = 0; i < Line.size(); ++i)
			{
				// köşede iki parçanın normal ortalaması (miter; aşırı uzamaya karşı sınırlı)
				FV Na = { 0, 0 }, Nb = { 0, 0 };
				if (i > 0) { const double dx = Line[i].E - Line[i - 1].E, dy = Line[i].N - Line[i - 1].N, l = std::max(1e-6, std::hypot(dx, dy)); Na = { -dy / l, dx / l }; }
				if (i + 1 < Line.size()) { const double dx = Line[i + 1].E - Line[i].E, dy = Line[i + 1].N - Line[i].N, l = std::max(1e-6, std::hypot(dx, dy)); Nb = { -dy / l, dx / l }; }
				if (i == 0) Na = Nb;
				if (i + 1 == Line.size()) Nb = Na;
				FV Nm = { Na.E + Nb.E, Na.N + Nb.N };
				const double Lm = std::hypot(Nm.E, Nm.N);
				if (Lm < 1e-6) Nm = Na; else { Nm.E /= Lm; Nm.N /= Lm; }
				const double Cs = std::max(0.35, Nm.E * Na.E + Nm.N * Na.N);
				L.push_back({ Line[i].E + Nm.E * H / Cs, Line[i].N + Nm.N * H / Cs });
				R.push_back({ Line[i].E - Nm.E * H / Cs, Line[i].N - Nm.N * H / Cs });
			}
			// uç yuvarlakları
			auto Cap = [H](const FV& C, double Ang0, std::vector<FV>& Out)
			{
				for (int k = 1; k < 8; ++k) { const double A = Ang0 + KPi * k / 8.0; Out.push_back({ C.E + std::cos(A) * H, C.N + std::sin(A) * H }); }
			};
			std::vector<FV> Out = L;
			{
				const FV& A = Line[Line.size() - 2]; const FV& B = Line.back();
				const double Ang = std::atan2(B.N - A.N, B.E - A.E) + KPi * 0.5;
				Cap(B, Ang, Out);
			}
			for (auto It = R.rbegin(); It != R.rend(); ++It) Out.push_back(*It);
			{
				const FV& A = Line[1]; const FV& B = Line[0];
				const double Ang = std::atan2(B.N - A.N, B.E - A.E) + KPi * 0.5;
				Cap(B, Ang, Out);
			}
			return Out;
		}

		double PolyArea(const std::vector<FV>& P)
		{
			double A = 0;
			for (size_t i = 0; i < P.size(); ++i) { const FV& a = P[i]; const FV& b = P[(i + 1) % P.size()]; A += a.E * b.N - b.E * a.N; }
			return std::fabs(A) * 0.5;
		}
	}

	void ToLocal(double HomeLat, double HomeLon, double Lat, double Lon, double& E, double& N)
	{
		E = (Lon - HomeLon) * KMLon * std::cos(HomeLat * KPi / 180.0);
		N = (Lat - HomeLat) * KMLat;
	}

	void ToGeo(double HomeLat, double HomeLon, double E, double N, double& Lat, double& Lon)
	{
		Lat = HomeLat + N / KMLat;
		Lon = HomeLon + E / (KMLon * std::max(1e-6, std::cos(HomeLat * KPi / 180.0)));
	}

	double DistM(double La0, double Lo0, double La1, double Lo1)
	{
		double E, N;
		ToLocal(La0, Lo0, La1, Lo1, E, N);
		return std::hypot(E, N);
	}

	int MinPoints(const std::string& Kind) { return Kind == "spiral" ? 0 : 3; }

	FMissionResult BuildMission(const std::string& Kind, double HomeLat, double HomeLon, const std::vector<FMissionPoint>& Points, const FMissionParams& P)
	{
		FMissionResult R;
		auto& It = R.Items;
		std::vector<FV> Loc;
		for (const FMissionPoint& Q : Points) { FV V; ToLocal(HomeLat, HomeLon, Q.Lat, Q.Lon, V.E, V.N); Loc.push_back(V); }
		auto Geo = [&](const FV& V, double& La, double& Lo) { ToGeo(HomeLat, HomeLon, V.E, V.N, La, Lo); };
		double MaxSpeed = P.CruiseMs;

		if (Kind == "corridor")
		{
			const double To = std::max(10.0, P.TakeoffAlt), Ca = std::max(30.0, P.CorridorAlt);
			It.push_back(Nav(22, HomeLat, HomeLon, To));
			std::vector<FV> Line = { { 0, 0 } };
			for (const FV& V : Loc) Line.push_back(V);
			for (size_t i = 1; i < Line.size(); ++i) { double La, Lo; Geo(Line[i], La, Lo); It.push_back(Nav(16, La, Lo, Ca)); }
			if (Line.size() >= 2) for (const FV& V : Buffer(Line, std::max(100.0, P.CorridorWidth))) { double La, Lo; Geo(V, La, Lo); R.Outline.push_back({ La, Lo }); }
		}
		else if (Kind == "spray")
		{
			const double Alt = std::min(30.0, std::max(2.0, P.SprayAlt));
			It.push_back(Nav(22, HomeLat, HomeLon, Alt));
			if (Loc.size() >= 3)
			{
				for (const FV& V : Sweep(Loc, std::min(30.0, std::max(1.0, P.SprayStep)), P.SprayAngle)) { double La, Lo; Geo(V, La, Lo); It.push_back(Nav(16, La, Lo, Alt)); }
				for (const FMissionPoint& Q : Points) R.Outline.push_back({ Q.Lat, Q.Lon });
				R.Summary.AreaM2 = PolyArea(Loc);
			}
		}
		else if (Kind == "spiral")
		{
			const double R0 = std::max(5.0, P.SpiralD0 * 0.5), R1 = std::max(15.0, P.SpiralD1 * 0.5);
			const double H0 = std::max(10.0, P.SpiralH0), H1 = std::max(20.0, P.SpiralH1);
			// tur sayısı: her turda en çok ~30 m tırmanış, en az 2 tur; nokta aralığı 15°
			const int Turns = std::min(16, std::max(2, (int)std::ceil(std::fabs(H1 - H0) / 30.0)));
			const int Steps = Turns * 24;
			It.push_back(Nav(22, HomeLat, HomeLon, H0));
			for (int k = 0; k <= Steps; ++k)
			{
				const double U = (double)k / Steps, A = U * Turns * 2.0 * KPi;
				const double Rr = R0 + (R1 - R0) * U;
				double La, Lo; Geo({ std::sin(A) * Rr, std::cos(A) * Rr }, La, Lo);
				It.push_back(Nav(16, La, Lo, H0 + (H1 - H0) * U));
			}
		}
		else   // serbest
		{
			const double To = Points.empty() ? 30.0 : std::max(5.0, Points[0].Alt);
			It.push_back(Nav(22, HomeLat, HomeLon, To));
			double CurSpeed = 0;
			for (const FMissionPoint& Q : Points)
			{
				if (Q.Speed > 0.1 && std::fabs(Q.Speed - CurSpeed) > 0.05)
				{
					FMissionItem S; S.Cmd = 178; S.bNav = false; S.P1 = 1; S.P2 = Q.Speed; S.P3 = -1;   // hava hızı (1) / yer hızı ayrımı: kopterde yok sayılır
					It.push_back(S);
					CurSpeed = Q.Speed;
					MaxSpeed = std::max(MaxSpeed, Q.Speed);
				}
				if (Q.Action == 1)
				{
					FMissionItem L = Nav(18, Q.Lat, Q.Lon, Q.Alt);
					L.P1 = std::max(1, Q.Turns); L.P3 = std::max(5.0, Q.Radius * 0.5);   // LOITER_TURNS: tur, yarıçap
					It.push_back(L);
				}
				else
				{
					It.push_back(Nav(16, Q.Lat, Q.Lon, Q.Alt));
					if (Q.Action == 2)
					{
						// NAV_SCRIPT_TIME (42702): betik komut numarası, süre, iki argüman (+ x / y argümanları); Lua tarafı mission_receive ile okur
						FMissionItem S = Nav(42702, Q.Args[2], Q.Args[3], Q.Alt);
						S.P1 = 1; S.P2 = 0; S.P3 = Q.Args[0]; S.P4 = Q.Args[1];
						S.bNav = false;
						It.push_back(S);
					}
				}
			}
		}
		if (It.size() > 1) { FMissionItem Rtl; Rtl.Cmd = 20; Rtl.bNav = false; It.push_back(Rtl); }
		if (It.size() > (size_t)KMaxItems) It.resize(KMaxItems);

		// özet: yatay + dikey yol, süre (seyir hızı ya da nokta hızı), en yüksek irtifa
		FMissionSummary& S = R.Summary;
		double La = HomeLat, Lo = HomeLon, Al = 0, Sp = P.CruiseMs;
		for (const FMissionItem& I : It)
		{
			if (I.Cmd == 178) { Sp = std::max(0.5, I.P2); continue; }
			if (!I.bNav) continue;
			const double H = (I.Cmd == 22) ? 0.0 : DistM(La, Lo, I.Lat, I.Lon);
			const double V = std::fabs(I.Alt - Al);
			const double D = std::hypot(H, V);
			S.DistM += D;
			S.TimeS += H / Sp + V / 2.5;   // tırmanma ~2,5 m/s
			if (I.Cmd == 18) { const double C = 2.0 * KPi * std::max(5.0, I.P3) * std::max(1.0, I.P1); S.DistM += C; S.TimeS += C / Sp; }
			S.MaxAlt = std::max(S.MaxAlt, I.Alt);
			if (I.Cmd != 22) { La = I.Lat; Lo = I.Lon; }
			Al = I.Alt;
			if (I.Cmd == 16 || I.Cmd == 18) ++S.Waypoints;
		}
		// eve dönüş (RTL)
		if (It.size() > 1) { const double H = DistM(La, Lo, HomeLat, HomeLon); S.DistM += H + Al; S.TimeS += H / Sp + Al / 1.5; }
		S.MaxSpeed = MaxSpeed;
		S.Items = (int)It.size();
		return R;
	}
}
