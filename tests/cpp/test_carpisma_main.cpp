// SmvCollision (Source/Simumav/Private/Core) sınama sürücüsü — Unreal'sız. stdin satır komutları, stdout sonuç satırları.
// Python tarafı: tests/cpp/test_carpisma.py
#include "../../Source/Simumav/Private/Core/SmvCollision.h"
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>

using namespace SmvCore;

int main()
{
	std::string Line;
	while (std::getline(std::cin, Line))
	{
		std::istringstream In(Line);
		std::string Cmd;
		In >> Cmd;
		if (Cmd == "CLS")
		{
			// CLS nx ny nz vx vy vz arac(0 dikey,1 ucak,2 kara) su(0/1)
			FContactIn C; int Veh = 0, W = 0;
			In >> C.Nx >> C.Ny >> C.Nz >> C.Vx >> C.Vy >> C.Vz >> Veh >> W;
			C.Vehicle = (EVehicleClass)Veh; C.bWater = W != 0;
			const FContactOut O = ClassifyContact(C);
			std::printf("CLS %s %.6f %.6f %.6f %d\n", ContactName(O.Class), O.Vn, O.Vt, O.Speed, O.bCrash ? 1 : 0);
		}
		else if (Cmd == "ESC")
		{
			std::string Name; double Pen, Dur; In >> Name >> Pen >> Dur;
			const EContact C = Name == "graze" ? EContact::Graze : Name == "ceiling_touch" ? EContact::CeilingTouch : Name == "landing" ? EContact::Landing : EContact::Hard;
			std::printf("ESC %s\n", ContactName(EscalateSustained(C, Pen, Dur)));
		}
		else if (Cmd == "SECT")
		{
			int N; double B; In >> N >> B;
			std::printf("SECT %d %.6f\n", SectorIndex(B, N), SectorBearingDeg(SectorIndex(B, N), N));
		}
		else if (Cmd == "SUB")
		{
			int I, N, K; In >> I >> N >> K;
			std::printf("SUB");
			for (double X : SectorSubRays(I, N, K)) std::printf(" %.6f", X);
			std::printf("\n");
		}
		else if (Cmd == "RNG")
		{
			double D, Mn, Mx; In >> D >> Mn >> Mx;
			std::printf("RNG %u\n", (unsigned)RangeToCm(D, Mn, Mx));
		}
		else if (Cmd == "OBS")
		{
			int N; In >> N;
			double D[72];
			for (int i = 0; i < N && i < 72; ++i) In >> D[i];
			uint16_t O[72];
			ObstacleArray(D, N, 0.2, 30.0, O);
			std::printf("OBS");
			for (int i = 0; i < 72; ++i) std::printf(" %u", (unsigned)O[i]);
			std::printf("\n");
		}
		else if (Cmd == "PARM")
		{
			std::string T; int Avoid; In >> T >> Avoid;
			std::printf("PARM");
			for (auto& P : PhysicsSensorParams(T, Avoid != 0)) std::printf(" %s=%g", P.first.c_str(), P.second);
			std::printf("\n");
		}
	}
	return 0;
}
