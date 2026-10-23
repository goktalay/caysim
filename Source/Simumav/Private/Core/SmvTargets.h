// SIMUMAV — hedefler (§24) + kilit/angajman (§40) + servo tetik (§34) çekirdeği.
// Kaynak: app/flightsim_targets.py, app/flightsim_servotrig.py (birebir kurallar). Yalnız C++17, Unreal'sız.
#pragma once

#include <string>
#include <vector>

namespace SmvCore
{
	constexpr double TGT_M_LAT = 110574.0;
	constexpr double TGT_M_LON = 111320.0;

	struct FTargetCfg
	{
		int I = 1;
		std::string Name, Model;
		double AltM = 120, DiameterM = 800, SpeedMs = 25, EastM = 0, NorthM = 0, PhaseDeg = 0;
		bool bCw = true;
	};

	struct FTargetState { double X = 0, Y = 0, Z = 0, Vx = 0, Vy = 0, HeadingDeg = 0, YawRad = 0, RollRad = 0, Speed = 0; };

	/** durum(h, t) — t = SITL tbm/1000. */
	FTargetState TargetAt(const FTargetCfg& H, double T);
	/** Varsayılan faz: (i−1)·137,5 mod 360 (i 1'den). */
	double DefaultPhase(int IndexFrom0);
	void TargetLocal(double Lat0, double Lon0, double Lat, double Lon, double& X, double& Y);
	void TargetLatLon(double Lat0, double Lon0, double X, double Y, double& Lat, double& Lon);

	struct FTargetEvent
	{
		std::string Kind;        // intercept | start | success | cut
		int I = 0; double DistM = 0; int N = 0; bool bFirst = false, bLocked = false; double AfterS = 0;
		std::string Reason;
	};

	struct FTargetTrack { int I = 0; std::string Name, Model; int Intercepts = 0; double FirstS = -1, InS = 0, MinDistM = -1; bool bInside = false; int LockedHits = 0; };
	struct FEngagement { int I = 0; std::string Name; double T0 = 0, StartS = 0, DurS = 0, StartDistM = -1, MinDistM = -1, EndDistM = -1; std::string Result, Reason; };

	class FTargetTracker
	{
	public:
		FTargetTracker(const std::vector<FTargetCfg>& Items, double RadiusM = 25.0);
		std::vector<FTargetEvent> Step(double T, double X, double Y, double Z, bool bArmed);
		std::vector<FTargetEvent> Lock(int I);
		std::vector<FTargetEvent> Release(const std::string& Reason = "released");
		bool HasEngagement() const { return bAng; }
		int EngagedIndex() const { return bAng ? Ang.I : 0; }
		/** puan(): skor + rozetler (first, quick, ace, close, hunter). */
		int Score(std::vector<std::string>& Badges) const;
		const std::vector<FTargetTrack>& Tracks() const { return Iz; }
		const std::vector<FEngagement>& Engagements() const { return Done; }
		double Radius() const { return R; }
	private:
		std::vector<FTargetCfg> Cfg;
		std::vector<FTargetTrack> Iz;
		double R;
		double TArm = -1, TLast = -1;
		bool bHasPos = false; double Pos[3] = { 0, 0, 0 };
		bool bAng = false; FEngagement Ang;
		std::vector<FEngagement> Done;
		double DistTo(int I, double T) const;
		FTargetEvent Finish(const std::string& Result, const std::string& Reason, double Dist = -1);
	};

	// ---- servo tetik ----
	struct FServoTrigCfg { bool bOn = false; int Ch = 9; int Pwm = 1900; std::string Cmp = "ge"; int Tol = 50; int DelayMs = 0; };

	class FServoTrigger
	{
	public:
		enum class EState { Idle, Listening, AlreadyInCondition, Armed, Fired };
		void Configure(const FServoTrigCfg& C);
		/** SERVO_OUTPUT_RAW (port 0) örneği: 16 kanal µs, tbm ms, duvar saati sn. Dönüş: "already" | "triggered" | "fired" | "" */
		std::string Sample(const int Servo16[16], long long TbmMs, double WallS);
		/** Duvar saati bekçisi (SITL donarsa): gecikme × 2 + 5 sn. */
		std::string Watchdog(double WallS);
		EState State() const { return St; }
		const FServoTrigCfg& Cfg() const { return C; }
		bool Matches(int V) const;
	private:
		FServoTrigCfg C;
		EState St = EState::Idle;
		bool bBase = false, bPrevMatch = false;
		long long TrigTbm = 0; double TrigWall = 0;
	};
}
