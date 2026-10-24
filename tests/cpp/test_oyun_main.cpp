// tests/cpp/test_oyun.py sürücüsü: SmvProgress + SmvGameRules çıktılarını satır satır yazar (Python denetler).
#include "../../Source/Simumav/Private/Core/SmvProgress.h"
#include "../../Source/Simumav/Private/Core/SmvGameRules.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace SmvCore;

int main()
{
	// ---- seviye eğrisi ----
	for (int L = 1; L <= 100; ++L) std::printf("curve %d %lld\n", L, XpForLevel(L));
	const long long Probe[] = { 0, 99, 100, 349, 350, 5000, 123456, 389000, 400000, 9999999 };
	for (long long X : Probe) std::printf("level %lld %d %.4f\n", X, LevelForXp(X), LevelProgress(X));
	for (const FMedal& M : Medals()) std::printf("medal %s %d\n", M.Id, M.Level);

	// ---- tür XP ----
	{
		FFlightFacts F; F.Mode = "free"; F.AirS = 400; F.DistM = 25000; F.DurationS = 420; F.AlertScore = 88; F.bLandedSoft = true; F.bInCircle = true;
		int S = 0; for (const FXpPart& P : ModeXp(F)) { std::printf("free %s %d\n", P.Key.c_str(), P.Xp); S += P.Xp; }
		std::printf("free_total %d\n", S);
	}
	{
		FFlightFacts F; F.Mode = "rings"; F.AirS = 120; F.MatchPlayers = 4; F.bBotsOnly = true; F.RingsPassed = 8; F.bRaceFinished = true; F.RaceRank = 1; F.bWon = true;
		for (const FXpPart& P : ModeXp(F)) std::printf("rings %s %d\n", P.Key.c_str(), P.Xp);
	}
	{
		FFlightFacts F; F.Mode = "cargo_drop"; F.AirS = 200; F.bReleased = true; F.DropBestM = 4.2;
		for (const FXpPart& P : ModeXp(F)) std::printf("cargo %s %d\n", P.Key.c_str(), P.Xp);
	}
	{
		FFlightFacts F; F.Mode = "facility_defense"; F.AirS = 200; F.DefenseScore = 1500; F.bDefenseOk = true;
		for (const FXpPart& P : ModeXp(F)) std::printf("defense %s %d\n", P.Key.c_str(), P.Xp);
	}

	// ---- manevralar: sentetik iz (5 Hz) ----
	{
		std::vector<FTrackSample> S;
		double T = 0;
		auto Add = [&](double Alt, double Roll, double Pitch, double Climb, double Gs, bool bArm) { FTrackSample X; X.T = T; X.AltRel = Alt; X.Roll = Roll; X.Pitch = Pitch; X.Climb = Climb; X.Gs = Gs; X.bArmed = bArm; S.push_back(X); T += 0.2; };
		for (int i = 0; i < 10; ++i) Add(0, 0, 0, 0, 0, true);
		for (int i = 0; i < 15; ++i) Add(i * 1.0, 0, 0, 5.5, 1, true);           // tırmanış 3 sn
		for (int i = 0; i < 50; ++i) Add(15, 0, 0, 0, 0.2, true);                // askı 10 sn
		for (int i = 0; i < 10; ++i) Add(15, 60, 0, 0, 8, true);                 // keskin dönüş 2 sn
		for (int i = 0; i < 5; ++i) Add(15, 0, 0, 0, 8, true);
		for (int i = 0; i < 3; ++i) Add(15, 170, 0, 0, 8, true);                 // takla
		for (int i = 0; i < 5; ++i) Add(15, 0, 0, 0, 8, true);
		for (int i = 0; i < 3; ++i) Add(15, 0, 85, 0, 8, true);                  // luping
		for (int i = 0; i < 5; ++i) Add(15, 0, 0, 0, 8, true);
		for (int i = 0; i < 12; ++i) Add(15 - i * 1.2, 0, 0, -7, 6, true);      // dalış
		for (int i = 0; i < 10; ++i) Add(2, 0, 0, 0, 10, true);                  // alçak geçiş
		for (int i = 0; i < 10; ++i) Add(2 - i * 0.2, 0, 0, -0.8, 0.5, true);   // yumuşak iniş
		for (int i = 0; i < 5; ++i) Add(0, 0, 0, 0, 0, false);
		for (const FManeuver& M : AnalyzeManeuvers(S)) std::printf("man %s %d %d\n", M.Key.c_str(), M.Count, M.Xp);
		FFlightFacts F; F.Mode = "free"; F.AirS = 30; F.DistM = 400; F.DurationS = 35; F.bLandedSoft = true;
		std::vector<FXpPart> Parts; std::vector<FManeuver> Mans;
		std::printf("flightxp %d\n", FlightXp(F, S, Parts, Mans));
	}

	// ---- yük bırakma ----
	{
		FDropGame G; FDropCfg C; C.Seed = 7; C.Payloads = 3; C.Zones = 4; G.Setup(C);
		std::printf("drop_zones %d\n", (int)G.Zones().size());
		const FDropZone Z = G.Zones()[0];
		// bölgenin tam üstünden sıfır hızla 20 m'den bırak → 5 m içinde
		G.Release(0, Z.E, Z.N, 20, 0, 0, 0);
		// 15 m/s doğuya giderken bölgenin 60 m batısından bırak → sürüklenme ile yakın düşer mi (yalnız iniş olur)
		G.Release(0, Z.E - 60, Z.N, 25, 15, 0, 0);
		std::vector<FGameEvent> Ev;
		for (int i = 0; i < 400; ++i) G.Step(0.02, Ev);
		for (const FGameEvent& E : Ev) std::printf("drop_land %d %.2f\n", E.I, E.Value);
		std::printf("drop_left %d score %d best %.2f done %d\n", G.Left(), G.Score(), G.BestM(), G.Done() ? 1 : 0);
	}

	// ---- ilaçlama ----
	{
		FSprayGame G; FSprayCfg C; C.CenterE = 0; C.CenterN = 0; C.W = 40; C.L = 60; C.SwathW = 6; C.Cell = 4; C.TankL = 100; G.Setup(C);
		std::vector<FGameEvent> Ev;
		double T = 0;
		// şerit şerit tara: 6 m aralıkla
		for (double U = -18; U <= 18.01; U += 5)
		{
			for (double V = -32; V <= 32; V += 0.5) { G.Step(T, 0.05, U, V, 3, 8, true, Ev); T += 0.05; }
		}
		std::printf("spray_cov %.4f score %d over %d\n", G.Coverage(), G.Score(), G.OversprayCells());
		G.Step(T, 0.05, 0, 0, 20, 5, true, Ev); T += 4;
		G.Step(T, 0.05, 200, 200, 3, 5, true, Ev);
		for (const FGameEvent& E : Ev) std::printf("spray_ev %s\n", E.Kind.c_str());
	}

	// ---- savunma ----
	{
		FDefenseGame G; FDefCfg C; C.Waves = 3; C.Seed = 3; C.FacE = 0; C.FacN = 0; G.Setup(C);
		std::vector<FGameEvent> Ev;
		double T = 0;
		// oyuncu tesis üstünde bekler (temas yok) → tehditler isabet eder
		for (int i = 0; i < 6000 && !G.Done(); ++i) { G.Step(T, 0.05, 0, 0, 200, 0, Ev); T += 0.05; }
		std::printf("def_idle hp %d imp %d neut %d done %d ok %d total %d\n", G.Hp(), G.Impacts(), G.Neutralized(), G.Done() ? 1 : 0, G.Success() ? 1 : 0, G.Total());
		// ikinci oyun: oyuncu her adımda en yakın tehdidin yanına "ışınlanır" (test: temas sayımı; oyunda pilot uçar)
		FDefenseGame H; H.Setup(C); Ev.clear(); T = 0;
		for (int i = 0; i < 6000 && !H.Done(); ++i)
		{
			double PE = 0, PN = 300, PU = 45;
			for (const FThreat& X : H.Threats()) if (X.bAlive) { PE = X.E + 3; PN = X.N; PU = X.U; break; }
			H.Step(T, 0.05, PE, PN, PU, 0, Ev); T += 0.05;
		}
		int Contacts = 0; for (const FGameEvent& E : Ev) if (E.Kind == "contact") ++Contacts;
		std::printf("def_active hp %d neut %d contacts %d done %d ok %d score %d\n", H.Hp(), H.Neutralized(), Contacts, H.Done() ? 1 : 0, H.Success() ? 1 : 0, H.Score());
		FDefCfg S = C; S.bShip = true; S.ShipSpeed = 3; S.ShipHeadingDeg = 90; FDefenseGame Sh; Sh.Setup(S); Ev.clear();
		for (int i = 0; i < 200; ++i) Sh.Step(i * 0.05, 0.05, 0, 0, 300, 0, Ev);
		std::printf("ship_e %.2f n %.2f\n", Sh.FacilityE(), Sh.FacilityN());
	}
	std::printf("kinds %s %s %s %d %d %d\n", GameKindId(GameKindFromId("cargo_drop")), GameKindId(GameKindFromId("ship_defense")), GameKindId(GameKindFromId("bilinmez")),
		GameKindIsMatch(EGameKind::Rings) ? 1 : 0, GameKindSingleAi(EGameKind::Dogfight), DefenseThreatTotal(3));
	return 0;
}
