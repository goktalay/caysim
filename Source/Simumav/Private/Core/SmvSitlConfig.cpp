// SIMUMAV — yerel SITL yapılandırması (bkz. SmvSitlConfig.h). Değerler sunucu kaynağından birebir; kaynak satırları yorumlarda.
#include "SmvSitlConfig.h"
#include "SmvRace.h"   // RoundN
#include "SmvPower.h"  // SpeedParams
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <sstream>

namespace SmvCore
{
	static std::string Num(double V)
	{
		// parm satırı: tam duyarlık (icept PARM 6+ basamak içerir; rover değerleri {:g} ile aynı çıkar)
		char B[64];
		std::snprintf(B, sizeof(B), "%.10g", V);
		for (char* C = B; *C; ++C) if (*C == ',') *C = '.';   // ondalık virgüllü yerel ayar olsa da SITL nokta bekler
		return B;
	}

	static std::string Fixed(double V, int N)
	{
		char B[64];
		std::snprintf(B, sizeof(B), "%.*f", N, V);
		for (char* C = B; *C; ++C) if (*C == ',') *C = '.';
		return B;
	}

	std::string SitlTypeFromSmv(const std::string& T, const std::string& /*ModelKey*/)
	{
		// app/simumav/bridge.py TIP_ESLEME (hexa modeli de 'multirotor' → --model +; sunucu da öyle)
		if (T == "fpv" || T == "multirotor") return "multirotor";
		if (T == "plane") return "sabit_kanat";
		if (T == "heli") return "helikopter";
		if (T == "vtol" || T == "interceptor" || T == "rover") return T;
		if (T == "sabit_kanat" || T == "helikopter" || T == "hexarotor") return T;
		return "multirotor";
	}

	void BinaryAndModel(const std::string& T, std::string& Bin, std::string& Model)
	{
		// drone_sitl.TIP_ESLEME; Windows ikilileri Mission Planner sitl klasöründeki adlarla (ArduCopter.elf = PE32+)
		if (T == "hexarotor") { Bin = "ArduCopter"; Model = "hexa"; }
		else if (T == "helikopter") { Bin = "ArduHeli"; Model = "heli"; }
		else if (T == "sabit_kanat") { Bin = "ArduPlane"; Model = "plane"; }
		else if (T == "vtol") { Bin = "ArduPlane"; Model = "quadplane"; }
		else if (T == "interceptor") { Bin = "ArduPlane"; Model = "quadplane-copter_tailsitter"; }
		else if (T == "rover") { Bin = "ArduRover"; Model = "rover"; }
		else { Bin = "ArduCopter"; Model = "+"; }
	}

	bool IsVerticalType(const std::string& T) { return T != "sabit_kanat" && T != "rover"; }
	bool IsPlaneType(const std::string& T) { return T == "sabit_kanat" || T == "vtol" || T == "interceptor"; }

	std::string PlaneModelSuffix(const std::string& Airframe, bool bRamp)
	{
		std::string S;
		if (Airframe == "elevon") S = "-elevon";
		else if (Airframe == "vtail") S = "-vtail";
		if (bRamp) S += "-catapult";
		return S;
	}

	std::vector<std::string> CommandLine(const FSitlSession& S, const std::string& DefaultsPath)
	{
		std::string Bin, Model;
		BinaryAndModel(S.SitlType, Bin, Model);
		if (S.SitlType == "sabit_kanat") Model += PlaneModelSuffix(S.Airframe, S.Launch.Method == "ramp");
		if (S.SitlType == "rover" && S.Drive == "skid") Model = "rover-skid";
		char Home[160];
		double Yaw = std::fmod(S.HomeHeading, 360.0);
		if (Yaw < 0) Yaw += 360.0;
		std::snprintf(Home, sizeof(Home), "%s,%s,%s,%s", Fixed(S.HomeLat, 7).c_str(), Fixed(S.HomeLon, 7).c_str(), Fixed(S.HomeAlt, 1).c_str(), Num(Yaw).c_str());
		// Tuzak C: --base-port, -I'dan önce; -I kullanılmaz
		return { "--model", Model, "--home", Home, "--base-port", std::to_string(S.BasePort), "--defaults", DefaultsPath, "--sysid", std::to_string(S.SysId) };
	}

	FParamList RoverParams(bool bSkid)
	{
		FParamList P = { { "CRUISE_SPEED", 5 }, { "CRUISE_THROTTLE", 30 }, { "ATC_SPEED_P", 0.1 }, { "ATC_STR_RAT_FF", 0.75 }, { "WP_RADIUS", 3 }, { "WP_SPEED", 5 },
			{ "RC1_MIN", 1000 }, { "RC1_MAX", 2000 }, { "RC3_MIN", 1000 }, { "RC3_MAX", 2000 }, { "SERVO1_MIN", 1000 }, { "SERVO1_MAX", 2000 },
			{ "SERVO3_MIN", 1000 }, { "SERVO3_MAX", 2000 }, { "BATT_MONITOR", 4 } };
		if (bSkid)
		{
			const FParamList K = { { "ATC_STR_RAT_FF", 0.2 }, { "CRUISE_SPEED", 3 }, { "CRUISE_THROTTLE", 75 }, { "SERVO1_FUNCTION", 73 }, { "SERVO3_FUNCTION", 74 }, { "WP_SPEED", 3 } };
			P.insert(P.end(), K.begin(), K.end());
		}
		else
		{
			P.push_back({ "SERVO1_FUNCTION", 26 });
			P.push_back({ "SERVO3_FUNCTION", 70 });
		}
		return P;
	}

	const FParamList& InterceptorParams()
	{
		// app/flightsim_icept.py PARM (autotest quadplane.parm + quadplane-copter_tailsitter.parm + HEPGO)
		// app/flightsim_icept.py PARM satır sırasıyla (tests/cpp/test_core.py birebir denetler)
		static const FParamList P = {
			{ "ARMING_RUDDER", 2 }, { "AIRSPEED_MAX", 35 }, { "AIRSPEED_MIN", 13 }, { "ARSPD_USE", 1 }, { "EK3_ENABLE", 1 },
			{ "COMPASS_OFS_X", -0.453570 }, { "COMPASS_OFS_Y", -0.585846 }, { "COMPASS_OFS_Z", 6.815743 }, { "COMPASS_OFS2_X", -0.420265 }, { "COMPASS_OFS2_Y", -0.726942 },
			{ "COMPASS_OFS2_Z", 6.665476 }, { "COMPASS_OFS3_X", -0.420265 }, { "COMPASS_OFS3_Y", -0.726942 }, { "COMPASS_OFS3_Z", 6.665476 }, { "FBWB_CLIMB_RATE", 10 },
			{ "FLTMODE1", 10 }, { "FLTMODE2", 11 }, { "FLTMODE3", 12 }, { "FLTMODE4", 5 }, { "FLTMODE5", 19 },
			{ "FLTMODE6", 5 }, { "FLTMODE_CH", 5 }, { "INS_ACCOFFS_X", 0.001 }, { "INS_ACCOFFS_Y", 0.001 }, { "INS_ACCOFFS_Z", 0.001 },
			{ "INS_ACCSCAL_X", 1.001 }, { "INS_ACCSCAL_Y", 1.001 }, { "INS_ACCSCAL_Z", 1.001 }, { "INS_ACC2OFFS_X", 0.001 }, { "INS_ACC2OFFS_Y", 0.001 },
			{ "INS_ACC2OFFS_Z", 0.001 }, { "INS_ACC2SCAL_X", 1.001 }, { "INS_ACC2SCAL_Y", 1.001 }, { "INS_ACC2SCAL_Z", 1.001 }, { "INS_GYR_CAL", 0 },
			{ "KFF_RDDRMIX", 0.5 }, { "PTCH_LIM_MAX_DEG", 30 }, { "PTCH_LIM_MIN_DEG", -30 }, { "ROLL_LIMIT_DEG", 45 }, { "NAVL1_PERIOD", 14 },
			{ "PTCH_RATE_D", 0 }, { "PTCH_RATE_I", 0.2125 }, { "PTCH_RATE_IMAX", 0.888889 }, { "PTCH_RATE_P", 0.309954 }, { "PTCH_RATE_FF", 1.407055 },
			{ "PTCH2SRV_RLL", 1 }, { "RLL_RATE_D", 0 }, { "RLL_RATE_I", 0.2125 }, { "RLL_RATE_IMAX", 0.888889 }, { "RLL_RATE_P", 0.141009 },
			{ "RLL_RATE_FF", 0.552741 }, { "Q_ASSIST_SPEED", 18 }, { "Q_ENABLE", 1 }, { "RC1_DZ", 30 }, { "RC1_MAX", 1886 },
			{ "RC1_MIN", 1087 }, { "RC1_TRIM", 1500 }, { "RC3_MAX", 2000 }, { "RC3_MIN", 1000 }, { "SERVO3_MIN", 1000 },
			{ "SERVO3_MAX", 2000 }, { "SCHED_LOOP_RATE", 300 }, { "THR_MAX", 100 }, { "AIRSPEED_CRUISE", 25 }, { "Q_M_THST_EXPO", 0.65 },
			{ "Q_M_PWM_MIN", 1000 }, { "Q_M_PWM_MAX", 2000 }, { "Q_M_BAT_VOLT_MAX", 12.8 }, { "Q_M_BAT_VOLT_MIN", 9.6 }, { "RTL_ALTITUDE", 100 },
			{ "Q_FRAME_CLASS", 1 }, { "Q_FRAME_TYPE", 0 }, { "Q_TAILSIT_MOTMX", 3 }, { "Q_A_ACC_P_MAX", 2070.69 }, { "Q_A_ACC_R_MAX", 1178.85 },
			{ "Q_A_ACC_Y_MAX", 122.87 }, { "Q_A_ANGLE_BOOST", 1 }, { "Q_A_ANG_LIM_TC", 1 }, { "Q_A_ANG_PIT_P", 18 }, { "Q_A_ANG_RLL_P", 3.455029 },
			{ "Q_A_ANG_YAW_P", 2.068653 }, { "Q_A_INPUT_TC", 0.2 }, { "Q_A_RATE_FF_ENAB", 1 }, { "Q_A_RATE_P_MAX", 0 }, { "Q_A_RATE_R_MAX", 0 },
			{ "Q_A_RATE_Y_MAX", 0 }, { "Q_A_RAT_PIT_D", 0.004973 }, { "Q_A_RAT_PIT_FF", 0 }, { "Q_A_RAT_PIT_I", 0.528246 }, { "Q_A_RAT_PIT_IMAX", 0.5 },
			{ "Q_A_RAT_PIT_P", 0.528246 }, { "Q_A_RAT_RLL_D", 0.001 }, { "Q_A_RAT_RLL_FF", 0 }, { "Q_A_RAT_RLL_I", 0.541613 }, { "Q_A_RAT_RLL_IMAX", 0.5 },
			{ "Q_A_RAT_RLL_P", 0.541613 }, { "Q_A_RAT_YAW_D", 0 }, { "Q_A_RAT_YAW_FF", 0 }, { "Q_A_RAT_YAW_I", 0.039192 }, { "Q_A_RAT_YAW_IMAX", 0.5 },
			{ "Q_A_RAT_YAW_P", 0.391919 }, { "Q_TRANS_DECEL", 8 }, { "SIM_SONAR_ROT", 4 }, { "SIM_SONAR_SCALE", 10 }, { "RNGFND1_TYPE", 100 },
			{ "RNGFND1_PIN", 0 }, { "RNGFND1_SCALING", 10 }, { "RNGFND1_MIN", 0.10 }, { "RNGFND1_MAX", 50 }, { "RNGFND1_ORIENT", 12 },
			{ "RNGFND_LANDING", 1 }, { "RNGFND_LND_ORNT", 12 }, { "Q_TAILSIT_ENABLE", 1 }, { "Q_WP_SPD_UP", 5 }, { "Q_WP_SPD_DN", 4 },
			{ "Q_PILOT_SPD_UP", 5 }, { "Q_PILOT_SPD_DN", 4 }, { "Q_RTL_MODE", 1 }, { "Q_LAND_FINAL_ALT", 6 }, { "Q_LAND_FINAL_SPD", 0.6 },
			{ "TRIM_THROTTLE", 55 }, { "ARSPD_OPTIONS", 0 }, { "THR_MIN", 0 },
		};
		return P;
	}

	const FParamList& InterceptorSessionParams()
	{
		static const FParamList P = { { "Q_TAILSIT_ENABLE", 1 }, { "Q_RTL_MODE", 1 }, { "Q_WP_SPD_UP", 5 }, { "Q_WP_SPD_DN", 4 }, { "RTL_ALTITUDE", 100 },
			{ "TRIM_THROTTLE", 55 }, { "ROLL_LIMIT_DEG", 45 }, { "ARSPD_OPTIONS", 0 }, { "THR_MIN", 0 } };
		return P;
	}

	static const FParamList& PlaneGroundParams()
	{
		static const FParamList P = { { "SERVO3_MIN", 1000 }, { "SERVO3_TRIM", 1000 }, { "CRASH_DETECT", 0 } };
		return P;
	}

	FParamList LaunchParams(const FSitlLaunch& L)
	{
		FParamList P = PlaneGroundParams();
		if (L.TakeoffAltM > 0) P.push_back({ "TKOFF_ALT", L.TakeoffAltM });
		if (L.Method == "ramp")
		{
			const FParamList K = { { "TKOFF_THR_MINACC", 8.0 }, { "TKOFF_THR_DELAY", 2.0 }, { "TKOFF_THR_MINSPD", 0 }, { "TKOFF_ROTATE_SPD", 0 },
				{ "TKOFF_LVL_PITCH", std::min(30.0, std::max(10.0, L.AngleDeg)) }, { "SERVO7_FUNCTION", 0 } };
			P.insert(P.end(), K.begin(), K.end());
		}
		else if (L.Method == "vehicle")
		{
			const FParamList K = { { "TKOFF_THR_MINACC", 0 }, { "TKOFF_THR_MINSPD", 0 }, { "TKOFF_ROTATE_SPD", RoundN(L.ReleaseKmh / 3.6, 2) }, { "TKOFF_LVL_PITCH", 15 } };
			P.insert(P.end(), K.begin(), K.end());
		}
		else
		{
			const FParamList K = { { "TKOFF_THR_MINACC", 0 }, { "TKOFF_THR_MINSPD", 0 }, { "TKOFF_ROTATE_SPD", 12 }, { "TKOFF_LVL_PITCH", 15 } };
			P.insert(P.end(), K.begin(), K.end());
		}
		return P;
	}

	FParamList LayoutParams(const std::string& A)
	{
		if (A == "elevon") return { { "SERVO1_FUNCTION", 78 }, { "SERVO2_FUNCTION", 77 }, { "SERVO4_FUNCTION", 0 }, { "SERVO1_REVERSED", 1 }, { "SERVO2_REVERSED", 1 }, { "SERVO4_REVERSED", 0 }, { "MIXING_GAIN", 0.5 } };
		if (A == "vtail") return { { "SERVO1_FUNCTION", 4 }, { "SERVO2_FUNCTION", 80 }, { "SERVO4_FUNCTION", 79 }, { "SERVO1_REVERSED", 0 }, { "SERVO2_REVERSED", 1 }, { "SERVO4_REVERSED", 0 }, { "MIXING_GAIN", 0.5 } };
		return { { "SERVO1_FUNCTION", 4 }, { "SERVO2_FUNCTION", 19 }, { "SERVO4_FUNCTION", 21 }, { "SERVO1_REVERSED", 0 }, { "SERVO2_REVERSED", 0 }, { "SERVO4_REVERSED", 0 } };
	}

	static std::set<std::string> UserNames(const FSitlSession& S)
	{
		std::set<std::string> N;
		for (auto& P : S.UserParams) N.insert(P.first);
		return N;
	}

	static bool IsSensorName(const std::string& N)
	{
		// drone_params: SITL'e hiç yazılmayan sensör kalibrasyon adları
		if (N.rfind("COMPASS_", 0) == 0) return true;
		if (N.rfind("INS_ACC", 0) == 0 && (N.find("OFFS_") != std::string::npos || N.find("SCAL_") != std::string::npos)) return true;
		static const std::set<std::string> X = { "AHRS_ORIENTATION", "INS_ACC_ID", "INS_ACC2_ID", "INS_ACC3_ID", "INS_GYR_ID", "INS_GYR2_ID", "INS_GYR3_ID" };
		return X.count(N) > 0;
	}

	std::string DefaultsParmText(const FSitlSession& S)
	{
		std::ostringstream O;
		O << "FRAME_CLASS 1\nFS_GCS_ENABLE 0\nMAV_SYSID " << S.SysId << "\nARMING_SKIPCHK 16\n";
		if (S.bBinLog) O << "LOG_BACKEND_TYPE 1\nLOG_DISARMED 0\nLOG_FILE_DSRMROT 1\n";
		else O << "LOG_BACKEND_TYPE 0\nLOG_DISARMED 0\n";
		if (S.SitlType == "interceptor")
		{
			O << "# SIMUMAV INTERCEPTOR (quadplane-copter_tailsitter)\n";
			for (auto& P : InterceptorParams()) O << P.first << " " << Num(P.second) << "\n";
		}
		else if (S.SitlType == "rover")
		{
			O << "# SIMUMAV kara araci (rover.parm ozu)\n";
			for (auto& P : RoverParams(S.Drive == "skid")) O << P.first << " " << Num(P.second) << "\n";
		}
		if (!S.UserParams.empty())
		{
			O << "# SIMUMAV kayitli parametreler\n";
			for (auto& P : S.UserParams) if (!IsSensorName(P.first)) O << P.first << " " << Num(P.second) << "\n";
		}
		if (S.SitlType == "sabit_kanat")
		{
			const std::set<std::string> U = UserNames(S);
			for (auto& P : LayoutParams(S.Airframe)) if (!U.count(P.first)) O << P.first << " " << Num(P.second) << "\n";
		}
		return O.str();
	}

	double GustWindSpeed(double WindMs, double GustMs, double T, double Seed)
	{
		// flightsim.esinti_carpani: g(t) = clamp(0,5 + 0,5·a, 0, 1)²
		const double Pi = 3.14159265358979323846;
		const double S = Seed * 0.7311;
		const double A = 0.55 * std::sin(2 * Pi * T / 11.3 + S) + 0.3 * std::sin(2 * Pi * T / 4.7 + 2.1 * S) + 0.15 * std::sin(2 * Pi * T / 1.9 + 3.3 * S);
		double G = std::max(0.0, std::min(1.0, 0.5 + 0.5 * A));
		G = RoundN(G * G, 4);
		return RoundN(WindMs + GustMs * G, 2);
	}

	FParamList FirstHeartbeatParams(const FSitlSession& S, bool bAfterReset)
	{
		FParamList P;
		const std::set<std::string> U = UserNames(S);
		// 4. kayıtlı parametreler (sensör adları hariç)
		for (auto& Kv : S.UserParams) if (!IsSensorName(Kv.first)) P.push_back(Kv);
		// 5. rüzgâr
		P.push_back({ "SIM_WIND_SPD", GustWindSpeed(S.WindMs, S.WindGust, 0) });
		P.push_back({ "SIM_WIND_DIR", S.WindDir });
		P.push_back({ "SIM_WIND_TURB", S.WindTurb });
		// 6. RC / failsafe
		const FParamList Rc = { { "SIM_RC_FAIL", 0 }, { "FS_THR_ENABLE", 0 }, { "THR_FAILSAFE", 0 }, { "RC_OVERRIDE_TIME", 3.0 } };
		P.insert(P.end(), Rc.begin(), Rc.end());
		// 7. GPS RTK
		const FParamList Gps = { { "SIM_GPS1_ACC", 0.02 }, { "SIM_GPS1_NUMSATS", 18 }, { "SIM_GPS1_NOISE", 0 }, { "SIM_GPS1_VERR_X", 0 }, { "SIM_GPS1_VERR_Y", 0 },
			{ "SIM_GPS1_VERR_Z", 0 }, { "SIM_GPS1_GLTCH_X", 0 }, { "SIM_GPS1_GLTCH_Y", 0 }, { "SIM_GPS1_GLTCH_Z", 0 }, { "SIM_GPS1_DRFTALT", 0 } };
		P.insert(P.end(), Gps.begin(), Gps.end());
		// 8. sabit kanat: kalkış seti + flaplar + düzen (kayıtlı adlar atlanır)
		if (S.SitlType == "sabit_kanat")
		{
			const FParamList L = LaunchParams(S.Launch);
			P.insert(P.end(), L.begin(), L.end());
			const FParamList Flap = { { "SERVO5_FUNCTION", 3 }, { "SERVO6_FUNCTION", 3 }, { "TKOFF_FLAP_PCNT", 50 }, { "LAND_FLAP_PERCNT", 100 } };
			for (auto& F : Flap) if (!U.count(F.first)) P.push_back(F);
			for (auto& F : LayoutParams(S.Airframe)) if (!U.count(F.first)) P.push_back(F);
		}
		// 9. INTERCEPTOR oturum seti + yer itki seti
		if (S.SitlType == "interceptor")
		{
			for (auto& F : InterceptorSessionParams()) P.push_back(F);
			for (auto& F : PlaneGroundParams()) P.push_back(F);
		}
		// 10. azami hız (resmi ikili: SIM_PLN_THR_MUL yok)
		if (IsPlaneType(S.SitlType) && S.MaxSpeedMs > 0)
		{
			double RMax = -1, RCruise = -1, RMin = -1;
			for (auto& Kv : S.UserParams)
			{
				if (Kv.first == "AIRSPEED_MAX") RMax = Kv.second;
				else if (Kv.first == "AIRSPEED_CRUISE") RCruise = Kv.second;
				else if (Kv.first == "AIRSPEED_MIN") RMin = Kv.second;
			}
			for (auto& F : SpeedParams(S.MaxSpeedMs, S.SitlType == "interceptor", RMax, RCruise, RMin)) P.push_back(F);
		}
		// 11. simülasyon hızı
		if (S.Speedup != 1.0 || bAfterReset) P.push_back({ "SIM_SPEEDUP", S.Speedup });
		// 12. arazi: yerel kipte TERRAIN_REQUEST yanıtlanmaz (bkz. docs/ozellik-esligi.md) → TERRAIN_ENABLE yazılmaz
		return P;
	}

	const std::vector<std::pair<std::string, uint32_t>>& ModeTable(const std::string& V)
	{
		static const std::vector<std::pair<std::string, uint32_t>> Copter = {
			{ "STABILIZE", 0 }, { "ACRO", 1 }, { "ALT_HOLD", 2 }, { "AUTO", 3 }, { "GUIDED", 4 }, { "LOITER", 5 }, { "RTL", 6 }, { "CIRCLE", 7 }, { "LAND", 9 },
			{ "DRIFT", 11 }, { "SPORT", 13 }, { "FLIP", 14 }, { "AUTOTUNE", 15 }, { "POSHOLD", 16 }, { "BRAKE", 17 }, { "THROW", 18 }, { "AVOID_ADSB", 19 },
			{ "GUIDED_NOGPS", 20 }, { "SMART_RTL", 21 }, { "FLOWHOLD", 22 }, { "FOLLOW", 23 }, { "ZIGZAG", 24 }, { "SYSTEMID", 25 }, { "AUTOROTATE", 26 },
			{ "AUTO_RTL", 27 }, { "TURTLE", 28 }, { "RATE_ACRO", 29 } };
		static const std::vector<std::pair<std::string, uint32_t>> Plane = {
			{ "MANUAL", 0 }, { "CIRCLE", 1 }, { "STABILIZE", 2 }, { "TRAINING", 3 }, { "ACRO", 4 }, { "FBWA", 5 }, { "FBWB", 6 }, { "CRUISE", 7 }, { "AUTOTUNE", 8 },
			{ "AUTO", 10 }, { "RTL", 11 }, { "LOITER", 12 }, { "TAKEOFF", 13 }, { "AVOID_ADSB", 14 }, { "GUIDED", 15 }, { "INITIALISING", 16 }, { "QSTABILIZE", 17 },
			{ "QHOVER", 18 }, { "QLOITER", 19 }, { "QLAND", 20 }, { "QRTL", 21 }, { "QAUTOTUNE", 22 }, { "QACRO", 23 }, { "THERMAL", 24 }, { "LOITERALTQLAND", 25 },
			{ "AUTOLAND", 26 } };
		static const std::vector<std::pair<std::string, uint32_t>> Rover = {
			{ "MANUAL", 0 }, { "ACRO", 1 }, { "LEARNING", 2 }, { "STEERING", 3 }, { "HOLD", 4 }, { "LOITER", 5 }, { "FOLLOW", 6 }, { "SIMPLE", 7 }, { "DOCK", 8 },
			{ "CIRCLE", 9 }, { "AUTO", 10 }, { "RTL", 11 }, { "SMART_RTL", 12 }, { "GUIDED", 15 }, { "INITIALISING", 16 } };
		if (V == "plane") return Plane;
		if (V == "rover") return Rover;
		return Copter;
	}

	bool ModeNumber(const std::string& V, const std::string& Name, uint32_t& Out)
	{
		for (auto& P : ModeTable(V)) if (P.first == Name) { Out = P.second; return true; }
		return false;
	}

	std::string ModeName(const std::string& V, uint32_t N)
	{
		for (auto& P : ModeTable(V)) if (P.second == N) return P.first;
		return "MODE(" + std::to_string(N) + ")";
	}

	std::string ModeVehicle(const std::string& T)
	{
		if (T == "rover") return "rover";
		if (IsPlaneType(T)) return "plane";
		return "copter";
	}

	std::string ModeVehicleFromMavType(uint8_t Ty)
	{
		if (Ty == 1 || (Ty >= 19 && Ty <= 22)) return "plane";
		if (Ty == 10 || Ty == 11) return "rover";
		return "copter";
	}

	const std::vector<double>& Speedups()
	{
		static const std::vector<double> S = { 0.25, 0.5, 1, 2, 4, 8 };
		return S;
	}
}
