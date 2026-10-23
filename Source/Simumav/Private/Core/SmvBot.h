// SIMUMAV — yapay zekâ pilot (app/flightsim_match_bot.py kuralları + BOT_SEVIYE sabitleri). Yalnız C++17, Unreal'sız.
// FARK (bilinçli): sunucuda bot kendi SITL'ini uçurur (GUIDED hız / MISSION_ITEM komutları). Misafir kipte ikinci bir SITL açmamak için bot
// aynı yönlendirme yasasıyla KİNEMATİK uçar (dönüş hızı, tırmanma/alçalma, hız sınırları aynı) — bkz. docs/ozellik-esligi.md.
#pragma once

#include "SmvRace.h"
#include <cstdint>
#include <string>
#include <vector>

namespace SmvCore
{
	struct FBotLevel
	{
		double Speed, DelayS, TurnDps, Scatter, Miss, EntryDeg; bool bPreTurn;
		double PredErrM, Lead, ManeuverS, ManeuverDeg, Randomness; bool bAltGame; double AreaThresh, FloorMargin;
		static FBotLevel Get(const std::string& Level);   // easy | normal | hard
	};

	struct FBotPose { double X = 0, Y = 0, Z = 0, HeadingDeg = 0, PitchRad = 0, RollRad = 0, Speed = 0; };

	class FBotPilot
	{
	public:
		FBotPilot(const std::string& Level, EVClass Class, uint64_t Seed);
		void Place(double X, double Y, double ZRel, double HeadingDeg);
		/** Halka yarışı: Rings yerel x/y + AglM (z referansı = zemin). Dt sn. */
		void StepRings(double Dt, const std::vector<FRing>& Rings, int NextIndex);
		/** İt dalaşı: rakibin gecikmeli pozu; bChaser rol; AreaR alan yarıçapı; RangeM menzil; AltM başlangıç irtifası. */
		void StepDogfight(double Dt, double T, const FBotPose& Opp, bool bChaser, double AreaR, double RangeM, double AltM);
		const FBotPose& Pose() const { return P; }
		double VMax() const { return Vmax; }
		bool bAirborne = false;
	private:
		FBotLevel L;
		EVClass C;
		FBotPose P;
		double Vmax, Floor, Climb, Sink, ApproachL;
		uint64_t Rng;
		double NextRand();
		int AimRing = -1; double AimOx = 0, AimOy = 0, AimOz = 0; bool bWasInFront = false;
		void Aim(const FRing& R, bool bAllowMiss);
		double ManeuverT = 0, ManeuverSign = 1;
		void Steer(double Dt, double Tx, double Ty, double Tz, double WantSpeed);
	};
}
