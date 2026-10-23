// SIMUMAV — araç çeşitleri (bkz. SmvVehicles.h). Değerler ArduPilot 4.7.2 autotest dosyalarından; kendi çeşitlerimizin JSON'ları gerçek
// sınıf örneklerinin kütle/boyut/batarya değerleriyle hesaplandı ve Linux SITL 4.7.2 ile uçurularak ayarlandı (tests/sitl_varyant.py).
#include "SmvVehicles.h"
#include <cmath>
#include <cstdio>
#include <sstream>

namespace SmvCore
{
	namespace
	{
		std::string N(double V)
		{
			char B[48];
			std::snprintf(B, sizeof(B), "%.6g", V);
			for (char* C = B; *C; ++C) if (*C == ',') *C = '.';
			return B;
		}

		/** SITL çok rotor çerçeve JSON'u (SIM_Frame load_frame_params adları). DiscArea: toplam etkin pervane alanı (m²; koaksiyel çift = 1). */
		struct FMulti
		{
			double Mass, Diag, RefSpd, RefAngle, RefV, RefI, MaxV, CapAh, Expo, RotRate, Hover, SpinMin, SpinMax, Disc, BatRes;
			int Motors;
		};

		std::string MultiJson(const FMulti& M)
		{
			std::ostringstream O;
			O << "{\n"
			  << "  \"mass\": " << N(M.Mass) << ",\n"
			  << "  \"diagonal_size\": " << N(M.Diag) << ",\n"
			  << "  \"refSpd\": " << N(M.RefSpd) << ",\n"
			  << "  \"refAngle\": " << N(M.RefAngle) << ",\n"
			  << "  \"refVoltage\": " << N(M.RefV) << ",\n"
			  << "  \"refCurrent\": " << N(M.RefI) << ",\n"
			  << "  \"refAlt\": 100,\n"
			  << "  \"refTempC\": 25,\n"
			  << "  \"refBatRes\": " << N(M.BatRes) << ",\n"
			  << "  \"maxVoltage\": " << N(M.MaxV) << ",\n"
			  << "  \"battCapacityAh\": " << N(M.CapAh) << ",\n"
			  << "  \"propExpo\": " << N(M.Expo) << ",\n"
			  << "  \"refRotRate\": " << N(M.RotRate) << ",\n"
			  << "  \"hoverThrOut\": " << N(M.Hover) << ",\n"
			  << "  \"pwmMin\": 1000,\n"
			  << "  \"pwmMax\": 2000,\n"
			  << "  \"spin_min\": " << N(M.SpinMin) << ",\n"
			  << "  \"spin_max\": " << N(M.SpinMax) << ",\n"
			  << "  \"slew_max\": 0,\n"
			  << "  \"disc_area\": " << N(M.Disc) << ",\n"
			  << "  \"mdrag_coef\": 0.1,\n"
			  << "  \"num_motors\": " << M.Motors << "\n"
			  << "}\n";
			return O.str();
		}

		/** Uçak katsayı JSON'u (SIM_Plane load_coeffs) — verilmeyen katsayılar SITL varsayılanında (skywalker_2013) kalır. */
		std::string PlaneJson(const FVehParams& Coeffs)
		{
			std::ostringstream O;
			O << "{\n";
			for (size_t i = 0; i < Coeffs.size(); ++i) O << "  \"" << Coeffs[i].first << "\": " << N(Coeffs[i].second) << (i + 1 < Coeffs.size() ? ",\n" : "\n");
			O << "}\n";
			return O.str();
		}

		double DiscArea(int Rotors, double PropInch) { const double R = PropInch * 0.0254 * 0.5; return Rotors * 3.14159265358979 * R * R; }

		/** Çok rotor batarya / askı uyumu: MOT_* (motor karıştırıcısının voltaj telafisi ve askı gazı tahmini) + BATT_CAPACITY. */
		FVehParams CopterBase(double Hover, double Cells, double CapMah, double Expo)
		{
			return { { "MOT_THST_HOVER", Hover }, { "MOT_THST_EXPO", Expo }, { "MOT_BAT_VOLT_MAX", 4.2 * Cells }, { "MOT_BAT_VOLT_MIN", 3.3 * Cells },
				{ "BATT_CAPACITY", CapMah }, { "BATT_MONITOR", 4 } };
		}

		FVehParams Join(FVehParams A, const FVehParams& B) { A.insert(A.end(), B.begin(), B.end()); return A; }

		// ---- autotest parametre özleri (kalibrasyon/RC aralık satırları hariç) ----
		const FVehParams& HeliParms()
		{
			// Tools/autotest/default_params/copter-heli.parm
			static const FVehParams P = {
				{ "FLTMODE1", 7 }, { "FLTMODE2", 9 }, { "FLTMODE3", 6 }, { "FLTMODE4", 3 }, { "FLTMODE5", 5 }, { "BATT_MONITOR", 4 },
				{ "ATC_INPUT_TC", 0.2 }, { "ATC_ANG_PIT_P", 6 }, { "ATC_ANG_RLL_P", 4 }, { "ATC_ANG_YAW_P", 5.8 },
				{ "ATC_RAT_PIT_D", 0.002 }, { "ATC_RAT_PIT_FLTD", 0 }, { "ATC_RAT_PIT_FLTE", 20 }, { "ATC_RAT_PIT_FLTT", 20 }, { "ATC_RAT_PIT_I", 0.095 },
				{ "ATC_RAT_PIT_ILMI", 0.1 }, { "ATC_RAT_PIT_IMAX", 0.4 }, { "ATC_RAT_PIT_P", 0.024 }, { "ATC_RAT_PIT_FF", 0.19 },
				{ "ATC_RAT_RLL_D", 0.0001 }, { "ATC_RAT_RLL_FLTD", 0 }, { "ATC_RAT_RLL_FLTE", 20 }, { "ATC_RAT_RLL_FLTT", 20 }, { "ATC_RAT_RLL_I", 0.105 },
				{ "ATC_RAT_RLL_ILMI", 0.1 }, { "ATC_RAT_RLL_IMAX", 0.4 }, { "ATC_RAT_RLL_P", 0.06 }, { "ATC_RAT_RLL_FF", 0.21 },
				{ "ATC_RAT_YAW_D", 0.0039 }, { "ATC_RAT_YAW_FLTD", 0 }, { "ATC_RAT_YAW_FLTE", 0 }, { "ATC_RAT_YAW_FLTT", 20 }, { "ATC_RAT_YAW_I", 0.0245 },
				{ "ATC_RAT_YAW_ILMI", 0 }, { "ATC_RAT_YAW_IMAX", 1 }, { "ATC_RAT_YAW_P", 0.24685 }, { "ATC_RAT_YAW_FF", 0 },
				{ "ATC_ACC_P_MAX", 600 }, { "ATC_ACC_R_MAX", 600 }, { "ATC_ACC_Y_MAX", 600 }, { "ATC_HOVR_ROL_TRM", 320 },
				{ "H_COL_MAX", 1740 }, { "H_COL_MIN", 1460 }, { "H_COL_ANG_MAX", 12 }, { "H_COL_ANG_MIN", -2 }, { "H_RSC_MODE", 2 }, { "H_RSC_SETPOINT", 66 },
				{ "RPM1_TYPE", 10 }, { "IM_ACRO_COL_EXP", 0.2 }, { "PSC_NE_VEL_D", 0.5 }, { "SERVO8_MIN", 1000 }, { "SERVO8_MAX", 2000 },
				{ "RC1_MIN", 1000 }, { "RC1_MAX", 2000 }, { "RC2_MIN", 1000 }, { "RC2_MAX", 2000 }, { "RC3_MIN", 1000 }, { "RC3_MAX", 2000 },
				{ "RC4_MIN", 1000 }, { "RC4_MAX", 2000 } };
			return P;
		}

		const FVehParams& QuadplaneParms()
		{
			// Tools/autotest/default_params/quadplane.parm (fizik/kontrol satırları)
			static const FVehParams P = {
				{ "ARMING_RUDDER", 2 }, { "AIRSPEED_MAX", 35 }, { "AIRSPEED_MIN", 13 }, { "ARSPD_USE", 1 }, { "FBWB_CLIMB_RATE", 10 },
				{ "FLTMODE1", 10 }, { "FLTMODE2", 11 }, { "FLTMODE3", 12 }, { "FLTMODE4", 5 }, { "FLTMODE5", 19 }, { "FLTMODE6", 5 }, { "FLTMODE_CH", 5 },
				{ "KFF_RDDRMIX", 0.5 }, { "PTCH_LIM_MAX_DEG", 30 }, { "PTCH_LIM_MIN_DEG", -30 }, { "ROLL_LIMIT_DEG", 65 }, { "NAVL1_PERIOD", 14 },
				{ "PTCH_RATE_D", 0 }, { "PTCH_RATE_I", 0.2125 }, { "PTCH_RATE_IMAX", 0.888889 }, { "PTCH_RATE_P", 0.309954 }, { "PTCH2SRV_RLL", 1 },
				{ "Q_ASSIST_SPEED", 18 }, { "Q_ENABLE", 1 }, { "RC1_DZ", 30 }, { "SERVO3_MIN", 1000 }, { "SERVO3_MAX", 2000 },
				{ "RLL_RATE_D", 0 }, { "RLL_RATE_I", 0.2125 }, { "RLL_RATE_IMAX", 0.888889 }, { "RLL_RATE_P", 0.141009 }, { "SCHED_LOOP_RATE", 300 },
				{ "THR_MAX", 100 }, { "AIRSPEED_CRUISE", 25 }, { "Q_M_THST_EXPO", 0.65 }, { "Q_M_PWM_MIN", 1000 }, { "Q_M_PWM_MAX", 2000 },
				{ "Q_M_BAT_VOLT_MAX", 12.8 }, { "Q_M_BAT_VOLT_MIN", 9.6 }, { "Q_A_RAT_RLL_P", 0.108 }, { "Q_A_RAT_RLL_I", 0.108 }, { "Q_A_RAT_RLL_D", 0.001 },
				{ "Q_A_RAT_PIT_P", 0.103 }, { "Q_A_RAT_PIT_I", 0.103 }, { "Q_A_RAT_PIT_D", 0.001 }, { "Q_A_RAT_YAW_P", 0.2 }, { "Q_A_RAT_YAW_I", 0.02 },
				{ "Q_A_ANG_RLL_P", 6 }, { "Q_A_ANG_PIT_P", 6 }, { "RTL_ALTITUDE", 40 }, { "PTCH_RATE_FF", 1.407055 }, { "RLL_RATE_FF", 0.552741 },
				{ "BATT_MONITOR", 4 } };
			return P;
		}

		const FVehParams& JetParms()
		{
			// Tools/autotest/default_params/plane-jet.parm
			static const FVehParams P = {
				{ "RTL_ALTITUDE", 200 }, { "AIRSPEED_MAX", 80 }, { "AIRSPEED_MIN", 25 }, { "FBWB_CLIMB_RATE", 5 }, { "LAND_FLARE_SEC", 2 },
				{ "PTCH_LIM_MAX_DEG", 20 }, { "PTCH_LIM_MIN_DEG", -25 }, { "ROLL_LIMIT_DEG", 70 }, { "NAVL1_PERIOD", 17 },
				{ "PTCH_RATE_D", 0 }, { "PTCH_RATE_FF", 0.69 }, { "PTCH_RATE_I", 0.5 }, { "PTCH_RATE_P", 0.09 }, { "PTCH2SRV_RLL", 1.04 },
				{ "RLL_RATE_D", 0 }, { "RLL_RATE_FF", 0.57 }, { "RLL_RATE_I", 0.5 }, { "RLL_RATE_P", 0.13 }, { "RLL2SRV_RMAX", 70 },
				{ "SCALING_SPEED", 40 }, { "TECS_CLMB_MAX", 15 }, { "TECS_LAND_ARSPD", 30 }, { "TECS_RLL2THR", 5 }, { "TECS_SINK_MAX", 8.5 },
				{ "TECS_SINK_MIN", 10 }, { "THR_SLEWRATE", 30 }, { "TKOFF_ROTATE_SPD", 20 }, { "TKOFF_THR_MAX", 100 }, { "AIRSPEED_CRUISE", 40 },
				{ "TRIM_THROTTLE", 36 }, { "WP_LOITER_RAD", 180 }, { "WP_RADIUS", 140 }, { "PTCH_RATE_IMAX", 0.666667 }, { "RLL_RATE_IMAX", 0.666667 } };
			return P;
		}

		const FVehParams& ElevonLayout()
		{
			// SmvSitlConfig LayoutParams("elevon") ile aynı (quadplane'de sabit kanat düzen seti yazılmadığı için çeşide gömülü)
			static const FVehParams P = { { "SERVO1_FUNCTION", 78 }, { "SERVO2_FUNCTION", 77 }, { "SERVO4_FUNCTION", 0 }, { "SERVO1_REVERSED", 1 },
				{ "SERVO2_REVERSED", 1 }, { "SERVO4_REVERSED", 0 }, { "MIXING_GAIN", 0.5 } };
			return P;
		}

		std::vector<FVehicleSpec> Build()
		{
			std::vector<FVehicleSpec> V;
			auto Add = [&V](FVehicleSpec S) { V.push_back(std::move(S)); };

			// ================= FPV =================
			{
				FVehicleSpec S; S.Key = "glb:iris_fpv"; S.SmvType = "fpv"; S.Frame = "x";
				S.Json = MultiJson({ 1.05, 0.50, 25, 40, 14.8, 14, 16.8, 1.5, 0.7, 450, 0.30, 0.05, 0.95, DiscArea(4, 10), 0.03, 4 });
				S.Params = Join(CopterBase(0.30, 4, 1500, 0.7), { { "ATC_ACC_R_MAX", 900 }, { "ATC_ACC_P_MAX", 900 }, { "ATC_ACC_Y_MAX", 400 },
					{ "ATC_ANGLE_MAX", 60 }, { "ACRO_RP_RATE", 540 }, { "ACRO_Y_RATE", 360 }, { "PILOT_SPD_UP", 8 }, { "PILOT_SPD_DN", 6 },
					{ "LOIT_SPEED_MS", 25 }, { "WP_SPD", 25 } });
				S.FrameParams = { { "FRAME_CLASS", 1 }, { "FRAME_TYPE", 1 } };
				S.MassKg = 1.05; S.SpanM = 0.55; S.LengthM = 0.45; S.CruiseKmh = 60; S.MaxKmh = 120; S.EnduranceMin = 7; S.Motors = 4; S.Battery = "4S 1500 mAh";
				S.Physics = "ArduCopter · quad X · 1,05 kg / 0,50 m JSON";
				Add(S);
			}
			{
				// 5" yarışçı: ArduPilot'un kendi freestyle modeli (models/freestyle.json + .param özü)
				FVehicleSpec S; S.Key = "proc:quad250"; S.SmvType = "fpv"; S.Frame = "x";
				S.Json = MultiJson({ 0.8, 0.25, 20, 45, 23.2, 5, 25.2, 1.3, 0.7, 700, 0.125, 0.01, 0.95, 0.204, 0.0226, 4 });
				S.Params = { { "ACRO_RP_EXPO", 0.4 }, { "ACRO_RP_RATE", 533 }, { "ACRO_THR_MID", 0.3 }, { "ACRO_Y_EXPO", 0.4 }, { "ACRO_Y_RATE", 533 },
					{ "ATC_ANGLE_MAX", 70 }, { "ATC_ACC_P_MAX", 1600 }, { "ATC_ACC_R_MAX", 1600 }, { "ATC_ACC_Y_MAX", 1200 }, { "ATC_ANG_PIT_P", 15 },
					{ "ATC_ANG_RLL_P", 15 }, { "ATC_ANG_YAW_P", 10 }, { "ATC_INPUT_TC", 0.04 }, { "ATC_RAT_PIT_P", 0.04 }, { "ATC_RAT_PIT_I", 0.04 },
					{ "ATC_RAT_PIT_D", 0.0012 }, { "ATC_RAT_PIT_FLTD", 60 }, { "ATC_RAT_PIT_FLTE", 40 }, { "ATC_RAT_PIT_FLTT", 60 }, { "ATC_RAT_RLL_P", 0.048 },
					{ "ATC_RAT_RLL_I", 0.048 }, { "ATC_RAT_RLL_D", 0.0008 }, { "ATC_RAT_RLL_FLTD", 60 }, { "ATC_RAT_RLL_FLTT", 60 }, { "ATC_RAT_YAW_P", 0.5 },
					{ "ATC_RAT_YAW_I", 0.05 }, { "ATC_RAT_YAW_D", 0.01 }, { "ATC_RAT_YAW_FLTD", 60 }, { "ATC_RAT_YAW_FLTE", 2 }, { "ATC_RAT_YAW_FLTT", 60 },
					{ "ATC_THR_MIX_MAN", 4 }, { "BATT_CAPACITY", 1300 }, { "BATT_MONITOR", 4 }, { "MOT_BAT_VOLT_MAX", 25.2 }, { "MOT_BAT_VOLT_MIN", 19.8 },
					{ "MOT_SPIN_ARM", 0.01 }, { "MOT_SPIN_MIN", 0.01 }, { "MOT_SPOOL_TIME", 0.25 }, { "MOT_THST_EXPO", 0.7 }, { "MOT_THST_HOVER", 0.125 },
					{ "PILOT_SPD_UP", 5 }, { "PILOT_Y_RATE", 250 }, { "LOIT_SPEED_MS", 40 }, { "LOIT_ACC_MAX_M", 10 }, { "WP_ACC", 11 }, { "WP_SPD", 30 },
					{ "PSC_D_VEL_P", 3 } };
				S.FrameParams = { { "FRAME_CLASS", 1 }, { "FRAME_TYPE", 1 } };
				S.MassKg = 0.8; S.SpanM = 0.25; S.LengthM = 0.2; S.CruiseKmh = 70; S.MaxKmh = 140; S.EnduranceMin = 5; S.Motors = 4; S.Battery = "6S 1300 mAh";
				S.Physics = "ArduCopter · quad X · ArduPilot freestyle 5\" modeli";
				Add(S);
			}
			{
				FVehicleSpec S; S.Key = "fab:fpv"; S.SmvType = "fpv"; S.Frame = "x";
				S.Json = MultiJson({ 0.9, 0.35, 22, 45, 22.2, 8, 25.2, 1.5, 0.7, 600, 0.18, 0.03, 0.95, DiscArea(4, 6), 0.025, 4 });
				S.Params = Join(CopterBase(0.18, 6, 1500, 0.7), { { "ATC_ACC_R_MAX", 1200 }, { "ATC_ACC_P_MAX", 1200 }, { "ATC_ACC_Y_MAX", 600 },
					{ "ATC_ANGLE_MAX", 65 }, { "ACRO_RP_RATE", 540 }, { "ACRO_Y_RATE", 400 }, { "LOIT_SPEED_MS", 30 }, { "WP_SPD", 30 } });
				S.FrameParams = { { "FRAME_CLASS", 1 }, { "FRAME_TYPE", 1 } };
				S.MassKg = 0.9; S.SpanM = 0.35; S.LengthM = 0.3; S.CruiseKmh = 70; S.MaxKmh = 130; S.EnduranceMin = 6; S.Motors = 4; S.Battery = "6S 1500 mAh";
				S.Physics = "ArduCopter · quad X · 0,9 kg / 0,35 m JSON (Fab FPV Interceptor gövdesi)";
				Add(S);
			}

			// ================= MULTIROTOR =================
			{
				FVehicleSpec S; S.Key = "glb:iris"; S.SmvType = "multirotor"; S.Frame = "x";
				S.Json = MultiJson({ 1.5, 0.55, 15, 30, 11.1, 15, 12.6, 5.1, 0.65, 200, 0.45, 0.15, 0.95, DiscArea(4, 10), 0.03, 4 });
				S.Params = CopterBase(0.45, 3, 5100, 0.65);
				S.FrameParams = { { "FRAME_CLASS", 1 }, { "FRAME_TYPE", 1 } };
				S.MassKg = 1.5; S.SpanM = 0.55; S.LengthM = 0.45; S.CruiseKmh = 36; S.MaxKmh = 60; S.EnduranceMin = 16; S.Motors = 4; S.Battery = "3S 5100 mAh";
				S.Physics = "ArduCopter · quad X · 1,5 kg / 0,55 m JSON";
				Add(S);
			}
			{
				FVehicleSpec S; S.Key = "glb:x500"; S.SmvType = "multirotor"; S.Frame = "x";
				S.Json = MultiJson({ 2.0, 0.50, 16, 30, 14.8, 18, 16.8, 5.0, 0.65, 220, 0.40, 0.15, 0.95, DiscArea(4, 10), 0.025, 4 });
				S.Params = CopterBase(0.40, 4, 5000, 0.65);
				S.FrameParams = { { "FRAME_CLASS", 1 }, { "FRAME_TYPE", 1 } };
				S.MassKg = 2.0; S.SpanM = 0.61; S.LengthM = 0.5; S.CruiseKmh = 36; S.MaxKmh = 65; S.EnduranceMin = 18; S.Motors = 4; S.Battery = "4S 5000 mAh";
				S.Physics = "ArduCopter · quad X · 2,0 kg / 0,50 m JSON";
				Add(S);
			}
			{
				FVehicleSpec S; S.Key = "fab:quad"; S.SmvType = "multirotor"; S.Frame = "x";
				S.Json = MultiJson({ 1.8, 0.52, 15, 30, 14.8, 16, 16.8, 5.0, 0.65, 200, 0.42, 0.15, 0.95, DiscArea(4, 10), 0.025, 4 });
				S.Params = CopterBase(0.42, 4, 5000, 0.65);
				S.FrameParams = { { "FRAME_CLASS", 1 }, { "FRAME_TYPE", 1 } };
				S.MassKg = 1.8; S.SpanM = 0.52; S.LengthM = 0.46; S.CruiseKmh = 36; S.MaxKmh = 60; S.EnduranceMin = 18; S.Motors = 4; S.Battery = "4S 5000 mAh";
				S.Physics = "ArduCopter · quad X · 1,8 kg / 0,52 m JSON (Fab Quadcopter gövdesi)";
				Add(S);
			}
			{
				// Typhoon H480: X düzenli altı rotor (kollar 30° + 60°·i) → SITL hexax + copter-hexa.parm + copter-X.parm
				FVehicleSpec S; S.Key = "glb:typhoon_h480"; S.SmvType = "multirotor"; S.Frame = "hexax";
				S.Json = MultiJson({ 1.95, 0.48, 15, 30, 14.8, 18, 16.8, 5.4, 0.65, 160, 0.45, 0.15, 0.95, DiscArea(6, 9), 0.03, 6 });
				S.Params = CopterBase(0.45, 4, 5400, 0.65);
				S.FrameParams = { { "FRAME_CLASS", 2 }, { "FRAME_TYPE", 1 } };
				S.MassKg = 1.95; S.SpanM = 0.52; S.LengthM = 0.48; S.CruiseKmh = 36; S.MaxKmh = 70; S.EnduranceMin = 22; S.Motors = 6; S.Battery = "4S 5400 mAh";
				S.Physics = "ArduCopter · hexa X · 1,95 kg / 0,48 m JSON";
				Add(S);
			}
			{
				// Düz sekiz rotor (kollar 0° + 45°·i; SITL octa = artı düzen) — haritalama/sinema sınıfı
				FVehicleSpec S; S.Key = "smv:octa"; S.SmvType = "multirotor"; S.Frame = "octa";
				S.Json = MultiJson({ 5.0, 1.0, 15, 25, 22.2, 40, 25.2, 16, 0.6, 120, 0.42, 0.15, 0.95, DiscArea(8, 15), 0.02, 8 });
				S.Params = Join(CopterBase(0.42, 6, 16000, 0.6), { { "ATC_ACC_R_MAX", 500 }, { "ATC_ACC_P_MAX", 500 }, { "ATC_ACC_Y_MAX", 120 } });
				S.FrameParams = { { "FRAME_CLASS", 3 }, { "FRAME_TYPE", 0 } };
				S.MassKg = 5.0; S.SpanM = 1.3; S.LengthM = 1.3; S.CruiseKmh = 40; S.MaxKmh = 60; S.EnduranceMin = 28; S.Motors = 8; S.PayloadKg = 3; S.Battery = "6S 16000 mAh";
				S.Physics = "ArduCopter · octa (+) · 5 kg / 1,0 m JSON";
				Add(S);
			}
			{
				// Y6: üç kol, kol başına üst/alt karşıt dönen koaksiyel çift (SITL y6, FRAME_CLASS 5)
				FVehicleSpec S; S.Key = "smv:y6"; S.SmvType = "multirotor"; S.Frame = "y6";
				S.Json = MultiJson({ 2.5, 0.75, 16, 30, 14.8, 28, 16.8, 8.0, 0.65, 150, 0.50, 0.15, 0.95, DiscArea(3, 13), 0.025, 6 });
				S.Params = CopterBase(0.50, 4, 8000, 0.65);
				S.FrameParams = { { "FRAME_CLASS", 5 }, { "FRAME_TYPE", 0 } };
				S.MassKg = 2.5; S.SpanM = 0.9; S.LengthM = 0.8; S.CruiseKmh = 40; S.MaxKmh = 65; S.EnduranceMin = 20; S.Motors = 6; S.PayloadKg = 1; S.Battery = "4S 8000 mAh";
				S.Physics = "ArduCopter · Y6 koaksiyel · 2,5 kg / 0,75 m JSON";
				Add(S);
			}
			{
				// Ağır yük: X8 koaksiyel (octa-quad X) — ArduPilot Callisto modeli (32,5 kg, 1,33 m, 12S 44 Ah) + Callisto.param ayarları
				FVehicleSpec S; S.Key = "smv:cargo_x8"; S.SmvType = "multirotor"; S.Frame = "octa-quad";
				S.Json = MultiJson({ 32.5, 1.325, 25, 30, 46.9, 65.36, 50.4, 44, 0.5, 120, 0.36, 0.2, 0.975, 1.82, 0.024, 8 });
				S.Params = { { "ACRO_Y_RATE", 90 }, { "ATC_ACC_P_MAX", 300 }, { "ATC_ACC_R_MAX", 300 }, { "ATC_ACC_Y_MAX", 60 }, { "ATC_ANG_PIT_P", 7 },
					{ "ATC_ANG_RLL_P", 7 }, { "ATC_ANG_YAW_P", 5 }, { "ATC_INPUT_TC", 0.25 }, { "ATC_RAT_PIT_D", 0.00375 }, { "ATC_RAT_PIT_FLTD", 10 },
					{ "ATC_RAT_PIT_I", 0.455 }, { "ATC_RAT_PIT_P", 0.455 }, { "ATC_RAT_RLL_D", 0.00375 }, { "ATC_RAT_RLL_FLTD", 10 }, { "ATC_RAT_RLL_I", 0.455 },
					{ "ATC_RAT_RLL_P", 0.455 }, { "ATC_RAT_YAW_FLTE", 10 }, { "ATC_RAT_YAW_D", 0.5 }, { "ATC_RAT_YAW_I", 0.05 }, { "ATC_RAT_YAW_P", 0.5 },
					{ "ATC_THR_MIX_MAX", 2 }, { "MOT_BAT_VOLT_MAX", 50.4 }, { "MOT_BAT_VOLT_MIN", 39.6 }, { "MOT_PWM_MAX", 2000 }, { "MOT_PWM_MIN", 1000 },
					{ "MOT_SPIN_ARM", 0.16 }, { "MOT_SPIN_MAX", 0.975 }, { "MOT_SPIN_MIN", 0.2 }, { "MOT_THST_EXPO", 0.5 }, { "MOT_THST_HOVER", 0.36 },
					{ "MOT_YAW_HEADROOM", 50 }, { "PILOT_Y_RATE", 60 }, { "PSC_D_POS_P", 0.5 }, { "PSC_D_VEL_P", 2.5 }, { "BATT_CAPACITY", 44000 },
					{ "BATT_MONITOR", 4 }, { "WP_SPD", 15 }, { "LOIT_ANG_MAX", 25 } };
				S.FrameParams = { { "FRAME_CLASS", 4 }, { "FRAME_TYPE", 1 } };
				S.MassKg = 32.5; S.SpanM = 2.1; S.LengthM = 2.1; S.CruiseKmh = 50; S.MaxKmh = 70; S.EnduranceMin = 25; S.Motors = 8; S.PayloadKg = 10; S.Battery = "12S 44 Ah";
				S.Physics = "ArduCopter · octa-quad X · ArduPilot Callisto modeli (32,5 kg)";
				Add(S);
			}

			// ================= SABİT KANAT =================
			{
				// Caymaz V3 — HEPGO'daki gibi: ArduPlane plane-elevon (rampada +catapult), elevon düzeni; ek parametre yok
				FVehicleSpec S; S.Key = "smv:caymaz"; S.Airframe = "elevon"; S.SmvType = "plane"; S.Frame = "plane";
				S.MassKg = 6.5; S.SpanM = 3.03; S.LengthM = 1.25; S.CruiseKmh = 80; S.MaxKmh = 110; S.EnduranceMin = 90; S.Motors = 1; S.Battery = "6S 22000 mAh";
				S.Physics = "ArduPlane · plane-elevon (HEPGO Caymaz ile aynı)";
				Add(S);
			}
			{
				FVehicleSpec S; S.Key = "glb:rc_cessna"; S.Airframe = "classic"; S.SmvType = "plane"; S.Frame = "plane";
				S.MassKg = 2.0; S.SpanM = 1.1; S.LengthM = 0.82; S.CruiseKmh = 72; S.MaxKmh = 100; S.EnduranceMin = 20; S.Motors = 1; S.Battery = "3S 2200 mAh";
				S.Physics = "ArduPlane · plane (SITL varsayılan aerodinamiği)";
				Add(S);
			}
			{
				// Eğitim uçağı: yüksek kanat, geniş alan (düşük kanat yükü), sürüklemeli; yumuşak sınırlar
				FVehicleSpec S; S.Key = "smv:trainer"; S.Airframe = "classic"; S.SmvType = "plane"; S.Frame = "plane";
				S.Json = PlaneJson({ { "s", 0.62 }, { "b", 1.6 }, { "c", 0.39 }, { "c_lift_0", 0.62 }, { "c_drag_p", 0.14 }, { "c_l_deltaa", 0.2 } });
				S.Params = { { "AIRSPEED_CRUISE", 14 }, { "AIRSPEED_MIN", 9 }, { "AIRSPEED_MAX", 22 }, { "ROLL_LIMIT_DEG", 35 }, { "PTCH_LIM_MAX_DEG", 15 },
					{ "PTCH_LIM_MIN_DEG", -15 }, { "TRIM_THROTTLE", 40 }, { "THR_MAX", 80 }, { "TECS_CLMB_MAX", 3 }, { "TECS_SINK_MAX", 3 },
					{ "NAVL1_PERIOD", 18 }, { "STALL_PREVENTION", 1 }, { "TKOFF_ROTATE_SPD", 9 } };
				S.MassKg = 2.0; S.SpanM = 1.6; S.LengthM = 1.15; S.CruiseKmh = 50; S.MaxKmh = 80; S.EnduranceMin = 25; S.Motors = 1; S.Battery = "3S 2600 mAh";
				S.Physics = "ArduPlane · plane · eğitim kanadı katsayıları (0,62 m², 1,6 m)";
				Add(S);
			}
			{
				FVehicleSpec S; S.Key = "glb:believer"; S.Airframe = "classic"; S.SmvType = "plane"; S.Frame = "plane";
				S.Json = PlaneJson({ { "s", 0.55 }, { "b", 1.96 }, { "c", 0.28 }, { "c_drag_p", 0.06 } });
				S.Params = { { "AIRSPEED_CRUISE", 18 }, { "AIRSPEED_MIN", 13 }, { "AIRSPEED_MAX", 28 }, { "TRIM_THROTTLE", 60 }, { "ROLL_LIMIT_DEG", 45 },
					{ "TKOFF_ROTATE_SPD", 14 } };
				S.MassKg = 4.5; S.SpanM = 1.96; S.LengthM = 1.17; S.CruiseKmh = 65; S.MaxKmh = 100; S.EnduranceMin = 100; S.Motors = 2; S.Battery = "6S 2×10000 mAh";
				S.Physics = "ArduPlane · plane · 0,55 m² / 1,96 m düşük sürüklemeli haritalama kanadı";
				Add(S);
			}
			{
				FVehicleSpec S; S.Key = "glb:talon_vtail"; S.Airframe = "vtail"; S.SmvType = "plane"; S.Frame = "plane";
				S.Json = PlaneJson({ { "s", 0.36 }, { "b", 1.72 }, { "c", 0.21 }, { "c_drag_p", 0.08 } });
				S.Params = { { "AIRSPEED_CRUISE", 18 }, { "AIRSPEED_MIN", 12 }, { "AIRSPEED_MAX", 30 } };
				S.MassKg = 2.0; S.SpanM = 1.72; S.LengthM = 1.1; S.CruiseKmh = 65; S.MaxKmh = 110; S.EnduranceMin = 45; S.Motors = 1; S.Battery = "4S 5000 mAh";
				S.Physics = "ArduPlane · plane-vtail · 0,36 m² / 1,72 m katsayıları";
				Add(S);
			}
			{
				FVehicleSpec S; S.Key = "glb:zephyr"; S.Airframe = "elevon"; S.SmvType = "plane"; S.Frame = "plane";
				S.Json = PlaneJson({ { "s", 0.32 }, { "b", 1.5 }, { "c", 0.24 }, { "c_drag_p", 0.06 } });
				S.MassKg = 2.0; S.SpanM = 1.5; S.LengthM = 0.6; S.CruiseKmh = 72; S.MaxKmh = 130; S.EnduranceMin = 40; S.Motors = 1; S.Battery = "4S 4000 mAh";
				S.Physics = "ArduPlane · plane-elevon · 0,32 m² / 1,5 m uçan kanat katsayıları";
				Add(S);
			}
			{
				FVehicleSpec S; S.Key = "glb:jet"; S.Airframe = "classic"; S.SmvType = "plane"; S.Frame = "plane-jet";
				S.Params = JetParms();
				S.MassKg = 22; S.SpanM = 3.6; S.LengthM = 4.0; S.CruiseKmh = 300; S.MaxKmh = 610; S.EnduranceMin = 40; S.Motors = 1; S.Battery = "jet yakıtı";
				S.Physics = "ArduPlane · plane-jet (22 kg jet modeli) + plane-jet.parm";
				Add(S);
			}
			{
				FVehicleSpec S; S.Key = "proc:interceptor"; S.Airframe = "elevon"; S.SmvType = "plane"; S.Frame = "plane-jet";
				S.Params = JetParms();
				S.MassKg = 22; S.SpanM = 2.2; S.LengthM = 2.0; S.CruiseKmh = 300; S.MaxKmh = 610; S.EnduranceMin = 30; S.Motors = 1; S.Battery = "jet yakıtı";
				S.Physics = "ArduPlane · plane-jet-elevon (22 kg) + plane-jet.parm";
				Add(S);
			}

			// ================= VTOL =================
			{
				FVehicleSpec S; S.Key = "glb:standard_vtol"; S.SmvType = "vtol"; S.Frame = "quadplane";
				S.Params = QuadplaneParms();
				S.FrameParams = { { "Q_FRAME_CLASS", 1 }, { "Q_FRAME_TYPE", 1 } };
				S.MassKg = 3.0; S.SpanM = 2.15; S.LengthM = 1.0; S.CruiseKmh = 90; S.MaxKmh = 125; S.EnduranceMin = 60; S.Motors = 5; S.Battery = "4S 10000 mAh";
				S.Physics = "ArduPlane · quadplane (X kaldırma + itici) + quadplane.parm";
				Add(S);
			}
			{
				// VTOL-Caymaz V3 (HEPGO "VTOL-Caymaz V3" eşi): elevonlu quadplane; dört kaldırma rotoru kollarla gövdeye eklenir
				FVehicleSpec S; S.Key = "smv:caymaz_vtol"; S.SmvType = "vtol"; S.Frame = "quadplane-elevon";
				// tek JSON iki modele de okunur: kaldırma çerçevesi (SIM_Frame: kütle ×1,5 = gövde dahil) + kanat katsayıları (SIM_Plane)
				std::string J = MultiJson({ 5.2, 1.3, 15, 25, 22.2, 55, 25.2, 22, 0.6, 120, 0.5, 0.15, 0.95, DiscArea(4, 18), 0.02, 4 });
				J = J.substr(0, J.rfind('}')) ;
				J = J.substr(0, J.rfind('\n')) + ",\n  \"s\": 0.78,\n  \"b\": 3.03,\n  \"c\": 0.27,\n  \"c_drag_p\": 0.05\n}\n";
				S.Json = J;
				S.Params = Join(Join(QuadplaneParms(), ElevonLayout()), { { "Q_M_BAT_VOLT_MAX", 25.2 }, { "Q_M_BAT_VOLT_MIN", 19.8 }, { "Q_M_THST_HOVER", 0.5 },
					{ "AIRSPEED_CRUISE", 22 }, { "AIRSPEED_MIN", 15 }, { "AIRSPEED_MAX", 30 } });
				S.FrameParams = { { "Q_FRAME_CLASS", 1 }, { "Q_FRAME_TYPE", 1 } };
				S.MassKg = 8.5; S.SpanM = 3.03; S.LengthM = 1.25; S.CruiseKmh = 80; S.MaxKmh = 110; S.EnduranceMin = 60; S.Motors = 5; S.Battery = "6S 22000 mAh";
				S.Physics = "ArduPlane · quadplane-elevon (HEPGO VTOL-Caymaz ile aynı model) + quadplane.parm";
				Add(S);
			}
			{
				// Dört rotorlu kanatlı tailsitter (kuyruk üstünde kalkış, yatay seyir): ArduPlane quadplane-copter_tailsitter, autotest
				// quadplane.parm + quadplane-copter_tailsitter.parm (INTERCEPTOR'dan farkı: seyir 22 m/s, azami 30 m/s, yumuşak sınırlar).
				// Not: tek motorlu "plane-tailsitter" autotest'te "kararsız / uçamaz" olarak işaretli olduğundan kullanılmaz.
				FVehicleSpec S; S.Key = "smv:tailsitter"; S.SmvType = "vtol"; S.Frame = "quadplane-copter_tailsitter";
				S.Params = Join(QuadplaneParms(), { { "Q_TAILSIT_ENABLE", 1 }, { "Q_TAILSIT_MOTMX", 3 }, { "Q_A_ACC_P_MAX", 2070.69 }, { "Q_A_ACC_R_MAX", 1178.85 },
					{ "Q_A_ACC_Y_MAX", 122.87 }, { "Q_A_ANGLE_BOOST", 1 }, { "Q_A_ANG_LIM_TC", 1 }, { "Q_A_ANG_PIT_P", 18 }, { "Q_A_ANG_RLL_P", 3.455029 },
					{ "Q_A_ANG_YAW_P", 2.068653 }, { "Q_A_INPUT_TC", 0.2 }, { "Q_A_RATE_FF_ENAB", 1 }, { "Q_A_RAT_PIT_D", 0.004973 }, { "Q_A_RAT_PIT_FF", 0 },
					{ "Q_A_RAT_PIT_I", 0.528246 }, { "Q_A_RAT_PIT_IMAX", 0.5 }, { "Q_A_RAT_PIT_P", 0.528246 }, { "Q_A_RAT_RLL_D", 0.001 }, { "Q_A_RAT_RLL_FF", 0 },
					{ "Q_A_RAT_RLL_I", 0.541613 }, { "Q_A_RAT_RLL_IMAX", 0.5 }, { "Q_A_RAT_RLL_P", 0.541613 }, { "Q_A_RAT_YAW_D", 0 }, { "Q_A_RAT_YAW_FF", 0 },
					{ "Q_A_RAT_YAW_I", 0.039192 }, { "Q_A_RAT_YAW_IMAX", 0.5 }, { "Q_A_RAT_YAW_P", 0.391919 }, { "Q_TRANS_DECEL", 8 },
					{ "AIRSPEED_CRUISE", 22 }, { "AIRSPEED_MAX", 30 }, { "AIRSPEED_MIN", 12 }, { "ROLL_LIMIT_DEG", 45 } });
				S.FrameParams = { { "Q_FRAME_CLASS", 1 }, { "Q_FRAME_TYPE", 0 } };
				S.MassKg = 3.0; S.SpanM = 1.2; S.LengthM = 0.7; S.CruiseKmh = 80; S.MaxKmh = 110; S.EnduranceMin = 35; S.Motors = 4; S.Battery = "4S 6000 mAh";
				S.Physics = "ArduPlane · quadplane-copter_tailsitter (kuyruk üstünde kalkış) + autotest tailsitter seti";
				Add(S);
			}

			// ================= HELİKOPTER =================
			{
				FVehicleSpec S; S.Key = "glb:heli"; S.SmvType = "heli"; S.Frame = "heli";
				S.Params = HeliParms();
				S.FrameParams = { { "FRAME_CLASS", 6 } };
				S.MassKg = 4.54; S.SpanM = 1.75; S.LengthM = 1.75; S.CruiseKmh = 60; S.MaxKmh = 110; S.EnduranceMin = 15; S.Motors = 1; S.Battery = "12S 5000 mAh";
				S.Physics = "ArduCopter-heli · tek ana rotor + kuyruk rotoru (4,54 kg) + copter-heli.parm";
				Add(S);
			}
			{
				// Tandem (Chinook düzeni) iki ana rotorlu kargo helikopteri: ArduHeli heli-dual (9,08 kg), FRAME_CLASS 11
				FVehicleSpec S; S.Key = "smv:heli_tandem"; S.SmvType = "heli"; S.Frame = "heli-dual";
				S.Params = Join(HeliParms(), { { "ATC_ANG_PIT_P", 4.5 }, { "ATC_ANG_YAW_P", 4.5 }, { "ATC_RAT_PIT_D", 0.005 }, { "ATC_RAT_PIT_P", 0.15 },
					{ "ATC_RAT_PIT_FF", 0.44 }, { "ATC_RAT_YAW_D", 0.0015 }, { "ATC_RAT_YAW_P", 0.14685 }, { "ATC_HOVR_ROL_TRM", 0 }, { "H_COL2_MAX", 1740 },
					{ "H_COL2_MIN", 1460 }, { "PILOT_Y_RATE_TC", 0.08 }, { "PILOT_Y_RATE", 120 } });
				S.FrameParams = { { "FRAME_CLASS", 11 } };
				S.MassKg = 9.08; S.SpanM = 1.6; S.LengthM = 2.6; S.CruiseKmh = 60; S.MaxKmh = 100; S.EnduranceMin = 15; S.Motors = 2; S.PayloadKg = 4; S.Battery = "12S 10000 mAh";
				S.Physics = "ArduCopter-heli · heli-dual tandem (9,08 kg) + copter-heli-dual.parm";
				Add(S);
			}
			{
				// Küçük koaksiyel helikopter: ArduCopter coaxcopter (iki karşıt dönen rotor + kanatçıklar), FRAME_CLASS 9
				FVehicleSpec S; S.Key = "smv:coax"; S.SmvType = "heli"; S.Frame = "coaxcopter";
				S.Params = { { "FLTMODE1", 7 }, { "FLTMODE2", 9 }, { "FLTMODE3", 6 }, { "FLTMODE4", 3 }, { "FLTMODE5", 5 }, { "BATT_MONITOR", 4 },
					{ "ATC_RATE_FF_ENAB", 1 }, { "ATC_RAT_PIT_D", 0.2 }, { "ATC_RAT_PIT_FLTD", 20 }, { "ATC_RAT_PIT_I", 2 }, { "ATC_RAT_PIT_IMAX", 1 },
					{ "ATC_RAT_PIT_P", 2 }, { "ATC_RAT_RLL_D", 0.2 }, { "ATC_RAT_RLL_FLTD", 20 }, { "ATC_RAT_RLL_I", 2 }, { "ATC_RAT_RLL_IMAX", 1 },
					{ "ATC_RAT_RLL_P", 2 }, { "ATC_RAT_YAW_D", 0 }, { "ATC_RAT_YAW_FLTE", 5 }, { "ATC_RAT_YAW_I", 0.139709 }, { "ATC_RAT_YAW_IMAX", 0.222 },
					{ "ATC_RAT_YAW_P", 1.397085 }, { "SERVO5_MIN", 1000 }, { "SERVO5_MAX", 2000 }, { "SERVO6_MIN", 1000 }, { "SERVO6_MAX", 2000 },
					{ "RC1_MIN", 1000 }, { "RC1_MAX", 2000 }, { "RC2_MIN", 1000 }, { "RC2_MAX", 2000 }, { "RC3_MIN", 1000 }, { "RC3_MAX", 2000 },
					{ "RC4_MIN", 1000 }, { "RC4_MAX", 2000 } };
				S.FrameParams = { { "FRAME_CLASS", 9 } };
				S.MassKg = 2.0; S.SpanM = 0.9; S.LengthM = 0.6; S.CruiseKmh = 40; S.MaxKmh = 70; S.EnduranceMin = 15; S.Motors = 2; S.Battery = "4S 5000 mAh";
				S.Physics = "ArduCopter · coaxcopter (koaksiyel, 2 kg) + copter-single/coax.parm";
				Add(S);
			}
			return V;
		}
	}

	const std::vector<FVehicleSpec>& VehicleSpecs()
	{
		static const std::vector<FVehicleSpec> V = Build();
		return V;
	}

	const FVehicleSpec* FindVehicleSpec(const std::string& Key)
	{
		for (const FVehicleSpec& S : VehicleSpecs()) if (S.Key == Key) return &S;
		return nullptr;
	}

	bool IsPlaneFrame(const std::string& F) { return F.rfind("plane", 0) == 0 || F.rfind("quadplane", 0) == 0; }

	std::string BinaryForFrame(const std::string& F)
	{
		if (F.rfind("heli", 0) == 0) return "ArduHeli";
		if (IsPlaneFrame(F)) return "ArduPlane";
		if (F.rfind("rover", 0) == 0) return "ArduRover";
		return "ArduCopter";
	}

	std::string VehicleJsonName(const FVehicleSpec& S)
	{
		return (S.Frame.rfind("plane", 0) == 0) ? "kanat.json" : "cerceve.json";
	}
}
