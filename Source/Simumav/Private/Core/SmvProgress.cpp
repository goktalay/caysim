#include "SmvProgress.h"

#include <algorithm>
#include <cmath>

namespace SmvCore
{
	long long XpForLevel(int Level)
	{
		const int L = std::max(1, std::min(XP_MAX_LEVEL, Level));
		return (long long)std::llround(100.0 * std::pow((double)(L - 1), 1.8));
	}

	int LevelForXp(long long Xp)
	{
		if (Xp < 0) Xp = 0;
		int L = 1;
		for (int i = 1; i <= XP_MAX_LEVEL; ++i)
			if (Xp >= XpForLevel(i)) L = i;
		return L;
	}

	double LevelProgress(long long Xp)
	{
		const int L = LevelForXp(Xp);
		if (L >= XP_MAX_LEVEL) return 1.0;
		const double A = (double)XpForLevel(L), B = (double)XpForLevel(L + 1);
		return std::max(0.0, std::min(1.0, ((double)Xp - A) / std::max(1.0, B - A)));
	}

	const std::vector<FMedal>& Medals()
	{
		static const std::vector<FMedal> M = {
			{ "wings_bronze", 5 }, { "wings_silver", 10 }, { "wings_gold", 25 }, { "wings_platinum", 50 }, { "wings_diamond", 75 }, { "wings_legend", 100 }
		};
		return M;
	}

	namespace
	{
		void Add(std::vector<FXpPart>& P, const char* K, int V) { if (V != 0) P.push_back({ K, V }); }
		int Floor(double V) { return (int)std::floor(V); }
		int Trunc(double V) { return (int)V; }
	}

	std::vector<FXpPart> ModeXp(const FFlightFacts& F)
	{
		std::vector<FXpPart> P;
		const bool bMatch = F.Mode == "rings" || F.Mode == "dogfight";
		// ---- uçuş bileşenleri (modes.ucus_bilesenleri) ----
		Add(P, "air_time", std::min(270, Floor(F.AirS / 10.0)));
		Add(P, "distance", std::min(200, Floor(F.DistM / 100.0)));
		if (F.AlertScore >= 0 && F.DurationS >= 60.0) Add(P, "discipline", std::max(0, std::min(50, Trunc(F.AlertScore * 0.5))));
		if (F.Result == "ok" && F.bLandedSoft && F.AirS >= 20.0) Add(P, "soft_landing", 25 + (F.bInCircle ? 15 : 0));
		if (F.Mode == "spraying" && F.SprayCoverage >= 0.0)
		{
			const double R = std::max(0.0, std::min(1.0, F.SprayCoverage));
			Add(P, "mission", (int)std::llround(R * 100.0) + (R >= 0.98 ? 60 : 0));
		}
		if ((F.Mode == "facility_defense" || F.Mode == "ship_defense") && F.DefenseScore >= 0.0)
			Add(P, "defense", std::min(400, std::max(0, Trunc(F.DefenseScore * 0.2))) + (F.bDefenseOk ? 80 : 0));
		if (F.TargetScore > 0) Add(P, "targets", std::min(300, std::max(0, Trunc(F.TargetScore * 0.1))));
		if (F.Mode == "cargo_drop" && F.bReleased)
		{
			Add(P, "release", 40);
			if (F.DropBestM >= 0.0) Add(P, "accuracy", F.DropBestM <= 5.0 ? 120 : F.DropBestM <= 15.0 ? 70 : F.DropBestM <= 30.0 ? 30 : 0);
		}
		// ---- karşılaşma bileşeni (modes.mac_puani) ----
		if (bMatch)
		{
			std::vector<FXpPart> M;
			if (F.MatchPlayers <= 1) { if (F.bRaceFinished) M.push_back({ "match_finish", 20 }); }
			else if (F.Mode == "dogfight") M.push_back({ "match_rank", F.bWon ? 150 : 30 });
			else
			{
				if (F.RingsPassed > 0) M.push_back({ "rings", std::min(250, 5 * F.RingsPassed) });
				if (F.bRaceFinished && F.RaceRank > 0) M.push_back({ "match_rank", F.RaceRank == 1 ? 100 : F.RaceRank == 2 ? 60 : F.RaceRank == 3 ? 40 : 20 });
				if (F.bWon) M.push_back({ "win", 50 });
			}
			if (F.bBotsOnly && F.MatchPlayers > 1) for (FXpPart& X : M) X.Xp = Trunc(X.Xp * 0.5);
			for (const FXpPart& X : M) if (X.Xp) P.push_back(X);
		}
		return P;
	}

	namespace
	{
		/** Koşul en az Hold sn sürdüğünde bir olay sayar; koşul düşünce (ya da Rearm koşulu sağlanınca) yeniden kurulur. */
		struct FDetector
		{
			double Hold = 1.0;
			double Since = -1.0;
			bool bFired = false;
			int Count = 0;
			void Step(double T, bool bOn, bool bRearm)
			{
				if (bOn)
				{
					if (Since < 0.0) Since = T;
					if (!bFired && T - Since >= Hold) { bFired = true; ++Count; }
				}
				else
				{
					Since = -1.0;
					if (bRearm) bFired = false;
				}
			}
		};
	}

	std::vector<FManeuver> AnalyzeManeuvers(const std::vector<FTrackSample>& S)
	{
		std::vector<FManeuver> Out;
		if (S.size() < 3) return Out;
		double Air = 0.0;
		for (size_t i = 1; i < S.size(); ++i)
			if (S[i].bArmed && S[i].AltRel > 1.0) Air += std::max(0.0, std::min(1.0, S[i].T - S[i - 1].T));
		if (Air < (double)XP_MIN_AIR_S) return Out;

		FDetector Turn; Turn.Hold = 1.0;
		FDetector Roll; Roll.Hold = 0.0;
		FDetector Loop; Loop.Hold = 0.0;
		FDetector Climb; Climb.Hold = 2.0;
		FDetector Dive; Dive.Hold = 1.4;
		FDetector Low; Low.Hold = 1.0;
		FDetector Fast; Fast.Hold = 2.0;
		FDetector Hover; Hover.Hold = 8.0;
		int SoftLand = 0;
		bool bAirborne = false;
		double LastClimb = 0.0;
		for (size_t i = 0; i < S.size(); ++i)
		{
			const FTrackSample& X = S[i];
			const bool bFly = X.bArmed && X.AltRel > 1.5;
			const double AbsRoll = std::fabs(X.Roll), AbsPitch = std::fabs(X.Pitch);
			Turn.Step(X.T, bFly && AbsRoll >= 45.0 && AbsRoll < 120.0 && X.Gs > 3.0, AbsRoll < 20.0);
			Roll.Step(X.T, bFly && AbsRoll >= 150.0, AbsRoll < 60.0);
			Loop.Step(X.T, bFly && AbsPitch >= 75.0, AbsPitch < 30.0);
			Climb.Step(X.T, bFly && X.Climb >= 5.0, X.Climb < 1.0);
			Dive.Step(X.T, bFly && X.Climb <= -6.0 && X.AltRel > 4.0, X.Climb > -1.0);
			Low.Step(X.T, X.bArmed && X.AltRel >= 0.4 && X.AltRel <= 3.0 && X.Gs >= 8.0, X.AltRel > 6.0 || X.Gs < 4.0);
			Fast.Step(X.T, bFly && X.Gs >= 25.0, X.Gs < 15.0);
			Hover.Step(X.T, bFly && X.AltRel >= 2.0 && X.Gs < 0.6 && std::fabs(X.Climb) < 0.3, X.Gs > 2.0 || std::fabs(X.Climb) > 1.0);
			// yumuşak iniş: havadan (> 2 m) yere (< 0,5 m) — son alçalma hızı > −1,2 m/s
			if (X.bArmed && X.AltRel > 2.0) bAirborne = true;
			if (bAirborne && X.AltRel < 0.5)
			{
				if (LastClimb > -1.2) ++SoftLand;
				bAirborne = false;
			}
			if (X.AltRel >= 0.5) LastClimb = X.Climb;
		}
		auto Push = [&Out](const char* K, int N, int Each, int Cap) { if (N > 0) Out.push_back({ K, N, std::min(Cap, N * Each) }); };
		Push("sharp_turn", Turn.Count, 10, 100);
		Push("roll", Roll.Count, 50, 250);
		Push("loop", Loop.Count, 40, 200);
		Push("climb", Climb.Count, 10, 50);
		Push("dive", Dive.Count, 15, 60);
		Push("low_pass", Low.Count, 25, 100);
		Push("high_speed", Fast.Count, 20, 60);
		Push("hover", Hover.Count, 15, 45);
		Push("soft_landing", SoftLand, 50, 100);
		return Out;
	}

	int FlightXp(const FFlightFacts& F, const std::vector<FTrackSample>& Track, std::vector<FXpPart>& OutParts, std::vector<FManeuver>& OutManeuvers)
	{
		OutParts.clear();
		OutManeuvers.clear();
		if (F.AirS < (double)XP_MIN_AIR_S && !(F.Mode == "rings" || F.Mode == "dogfight")) return 0;
		OutParts = ModeXp(F);
		OutManeuvers = AnalyzeManeuvers(Track);
		for (const FManeuver& M : OutManeuvers) OutParts.push_back({ std::string("m:") + M.Key, M.Xp });
		int Sum = 0;
		for (const FXpPart& P : OutParts) Sum += P.Xp;
		return std::max(0, Sum);
	}
}
